#include "spek-gate-scan.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

const char *const GATE_QUARANTINE_DIRNAME = "_transcode-quarantine";
const char *const GATE_RESTORE_LOG = "restore.tsv";

int GateScan::count_of(GateVerdict v) const
{
    int n = 0;
    for (const auto& r : this->results) {
        if (r.verdict == v) {
            n++;
        }
    }
    return n;
}

std::vector<const GateResult*> GateScan::flagged() const
{
    std::vector<const GateResult*> out;
    for (const auto& r : this->results) {
        switch (r.verdict) {
        case GateVerdict::LOSSY:
        case GateVerdict::PADDED:
        case GateVerdict::UPSAMPLED:
        case GateVerdict::SUSPECT:
            out.push_back(&r);
            break;
        default:
            break;
        }
    }
    return out;
}

std::vector<std::string> gate_find_audio_files(const std::string& root, bool recursive)
{
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return out;
    }

    auto consider = [&](const fs::path& p) {
        std::error_code e;
        if (!fs::is_regular_file(p, e)) {
            return;
        }
        if (!gate_is_checkable(p.string())) {
            return;
        }
        // Never re-report what a previous scan pulled out.
        for (const auto& part : p) {
            if (part.string() == GATE_QUARANTINE_DIRNAME) {
                return;
            }
        }
        out.push_back(p.string());
    };

    if (recursive) {
        for (auto it = fs::recursive_directory_iterator(
                 root, fs::directory_options::skip_permission_denied, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            consider(it->path());
        }
    } else {
        for (auto it = fs::directory_iterator(
                 root, fs::directory_options::skip_permission_denied, ec);
             it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            consider(it->path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

GateScan gate_scan_paths(const std::vector<std::string>& paths,
                         const std::string& root,
                         const std::function<void(int, int, const std::string&)>& on_progress,
                         const std::function<bool()>& should_stop,
                         double max_seconds)
{
    GateScan scan;
    scan.root = root;
    scan.total_files = (int)paths.size();

    for (size_t i = 0; i < paths.size(); i++) {
        if (should_stop && should_stop()) {
            scan.stopped = true;
            break;
        }
        if (on_progress) {
            on_progress((int)i + 1, (int)paths.size(), paths[i]);
        }
        scan.results.push_back(gate_analyse(paths[i], max_seconds));
    }
    scan.scanned = (int)scan.results.size();

    // Worst first, then most confident, then by name — so the thing you need to
    // look at is the first row, every time.
    std::sort(scan.results.begin(), scan.results.end(),
              [](const GateResult& a, const GateResult& b) {
                  int ra = gate_verdict_rank(a.verdict), rb = gate_verdict_rank(b.verdict);
                  if (ra != rb) return ra < rb;
                  if (a.confidence != b.confidence) return a.confidence > b.confidence;
                  return a.name < b.name;
              });
    return scan;
}

std::string gate_safe_name(const std::string& name)
{
    std::string out;
    for (unsigned char c : name) {
        if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            out += '_';
        } else {
            out += (char)c;
        }
    }
    size_t b = out.find_first_not_of(" .");
    size_t e = out.find_last_not_of(" .");
    out = (b == std::string::npos) ? "" : out.substr(b, e - b + 1);
    if (out.size() > 120) {
        out.resize(120);
    }
    return out.empty() ? "spectrogram" : out;
}

std::string gate_export_basename(const GateResult& r)
{
    std::vector<std::string> parts;
    parts.push_back(gate_verdict_name(r.verdict));
    if (!r.artist.empty()) parts.push_back(r.artist);
    if (!r.album.empty()) parts.push_back(r.album);
    parts.push_back(!r.title.empty() ? r.title : r.name);

    std::string joined;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) joined += " - ";
        joined += parts[i];
    }
    return gate_safe_name(joined);
}

std::string gate_format_report(const GateScan& scan)
{
    std::ostringstream o;
    char buf[512];

    std::time_t now = std::time(nullptr);
    char when[64] = {0};
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", std::localtime(&now));

    o << "TRANSCODE / FAKE-LOSSLESS SCAN\n";
    o << "Folder : " << scan.root << "\n";
    o << "Date   : " << when << "\n";
    snprintf(buf, sizeof(buf), "Files  : %d of %d analysed\n", scan.scanned, scan.total_files);
    o << buf;
    o << "Method : long-term average spectrum, 16384-point Hann FFT, 50% overlap,\n";
    o << "         digital silence skipped, normalised to the loudest bin.\n";
    o << "\nSUMMARY\n";

    static const GateVerdict order[] = {
        GateVerdict::LOSSY, GateVerdict::PADDED, GateVerdict::UPSAMPLED,
        GateVerdict::SUSPECT, GateVerdict::UNREADABLE, GateVerdict::CLEAN,
        GateVerdict::LOSSY_FORMAT,
    };
    for (GateVerdict v : order) {
        int n = scan.count_of(v);
        if (n) {
            snprintf(buf, sizeof(buf), "  %-10s %d\n", gate_verdict_name(v), n);
            o << buf;
        }
    }
    o << "\n";

    for (const auto& r : scan.results) {
        if (r.verdict == GateVerdict::CLEAN || r.verdict == GateVerdict::LOSSY_FORMAT) {
            continue;
        }
        o << std::string(78, '-') << "\n";
        std::string upper = gate_verdict_name(r.verdict);
        for (auto& c : upper) c = (char)std::toupper((unsigned char)c);
        snprintf(buf, sizeof(buf), "%s  (%d%% confidence)  %s\n",
                 upper.c_str(), r.confidence, r.name.c_str());
        o << buf << "  " << r.path << "\n";
        if (!r.error.empty()) {
            o << "  error: " << r.error << "\n";
            continue;
        }
        snprintf(buf, sizeof(buf), "  %d Hz  %dch  %d-bit declared / %d-bit used\n",
                 r.sample_rate, r.channels, r.declared_bits, r.effective_bits);
        o << buf;
        snprintf(buf, sizeof(buf),
                 "  cutoff %.0f Hz   wall %.1f dB   above cutoff %.1f dB   side/mid %.1f dB\n",
                 r.cutoff_hz, r.wall_db, r.above_db, r.side_ratio_db);
        o << buf;
        if (!r.estimated_source.empty()) {
            o << "  consistent with: " << r.estimated_source << "\n";
        }
        for (const auto& reason : r.reasons) {
            o << "  - " << reason << "\n";
        }
        if (!r.bands.empty()) {
            o << "  band (Hz)      mean dB\n";
            for (const auto& b : r.bands) {
                snprintf(buf, sizeof(buf), "  %8.0f+     %7.1f\n", b.first, b.second);
                o << buf;
            }
        }
    }
    o << std::string(78, '-') << "\n\n";
    o << "A lossy verdict needs all three of: a cutoff well below Nyquist, a sharp\n";
    o << "wall at it, and a dead band above it. Mastering can produce any one of\n";
    o << "those on its own; only an encoder produces all three together.\n";
    return o.str();
}

static std::string json_escape(const std::string& s)
{
    std::string o;
    for (unsigned char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 32) {
                char b[8];
                snprintf(b, sizeof(b), "\\u%04x", c);
                o += b;
            } else {
                o += (char)c;
            }
        }
    }
    return o;
}

std::string gate_format_json(const GateScan& scan)
{
    std::ostringstream o;
    char buf[256];
    o << "{\n  \"root\": \"" << json_escape(scan.root) << "\",\n";
    snprintf(buf, sizeof(buf), "  \"total_files\": %d,\n  \"scanned\": %d,\n  \"stopped\": %s,\n",
             scan.total_files, scan.scanned, scan.stopped ? "true" : "false");
    o << buf << "  \"results\": [\n";
    for (size_t i = 0; i < scan.results.size(); i++) {
        const GateResult& r = scan.results[i];
        o << "    {";
        o << "\"path\": \"" << json_escape(r.path) << "\", ";
        o << "\"name\": \"" << json_escape(r.name) << "\", ";
        o << "\"artist\": \"" << json_escape(r.artist) << "\", ";
        o << "\"album\": \"" << json_escape(r.album) << "\", ";
        o << "\"title\": \"" << json_escape(r.title) << "\", ";
        o << "\"verdict\": \"" << gate_verdict_name(r.verdict) << "\", ";
        snprintf(buf, sizeof(buf),
                 "\"confidence\": %d, \"sample_rate\": %d, \"channels\": %d, "
                 "\"declared_bits\": %d, \"effective_bits\": %d, ",
                 r.confidence, r.sample_rate, r.channels, r.declared_bits, r.effective_bits);
        o << buf;
        snprintf(buf, sizeof(buf),
                 "\"cutoff_hz\": %.1f, \"wall_db\": %.1f, \"above_db\": %.1f, "
                 "\"side_ratio_db\": %.1f, ",
                 r.cutoff_hz, r.wall_db, r.above_db, r.side_ratio_db);
        o << buf;
        o << "\"estimated_source\": \"" << json_escape(r.estimated_source) << "\", ";
        o << "\"reasons\": [";
        for (size_t j = 0; j < r.reasons.size(); j++) {
            if (j) o << ", ";
            o << "\"" << json_escape(r.reasons[j]) << "\"";
        }
        o << "]}";
        if (i + 1 < scan.results.size()) o << ",";
        o << "\n";
    }
    o << "  ]\n}\n";
    return o.str();
}

// ---- file actions --------------------------------------------------------------

bool gate_under_root(const std::string& root, const std::string& p)
{
    std::error_code ec;
    fs::path r = fs::weakly_canonical(fs::path(root), ec);
    if (ec) r = fs::path(root);
    fs::path f = fs::weakly_canonical(fs::path(p), ec);
    if (ec) f = fs::path(p);

    std::string rs = r.string(), ps = f.string();
#ifdef _WIN32
    // Windows treats C:\Music and c:\music as the same folder; a case-sensitive
    // compare would refuse a file the user can plainly see.
    auto lower = [](std::string& s) {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    };
    lower(rs);
    lower(ps);
    for (auto& c : rs) if (c == '/') c = '\\';
    for (auto& c : ps) if (c == '/') c = '\\';
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    while (!rs.empty() && rs.back() == sep) {
        rs.pop_back();
    }
    return ps == rs || ps.compare(0, rs.size() + 1, rs + sep) == 0;
}

// A destination that does not collide with a file already there.
static fs::path unique_dest(fs::path dest)
{
    if (!fs::exists(dest)) {
        return dest;
    }
    fs::path stem = dest.stem();
    fs::path ext = dest.extension();
    for (int i = 2; i < 10000; i++) {
        fs::path candidate = dest.parent_path() /
            (stem.string() + " (" + std::to_string(i) + ")" + ext.string());
        if (!fs::exists(candidate)) {
            return candidate;
        }
    }
    return dest;
}

GateActionResult gate_quarantine_files(const std::vector<std::string>& paths,
                                       const std::string& root)
{
    GateActionResult out;
    fs::path qdir = fs::path(root) / GATE_QUARANTINE_DIRNAME;
    out.dir = qdir.string();

    std::error_code ec;
    fs::create_directories(qdir, ec);
    if (ec) {
        for (const auto& p : paths) {
            out.failed.push_back({p, "cannot create quarantine folder"});
        }
        return out;
    }

    // Appended, not rewritten: a second quarantine run must not lose the first
    // run's record of where its files came from.
    std::ofstream log((qdir / GATE_RESTORE_LOG).string(), std::ios::app);

    for (const auto& p : paths) {
        if (!gate_under_root(root, p)) {
            out.failed.push_back({p, "outside the scanned folder"});
            continue;
        }
        std::error_code e;
        // Mirror the tree under the quarantine dir so two tracks with the same
        // filename on different albums do not collide.
        fs::path rel = fs::relative(fs::path(p), fs::path(root), e);
        fs::path dest = e ? (qdir / fs::path(p).filename()) : (qdir / rel);
        fs::create_directories(dest.parent_path(), e);
        dest = unique_dest(dest);

        fs::rename(fs::path(p), dest, e);
        if (e) {
            // Across a filesystem boundary rename fails; fall back to copy+delete.
            fs::copy_file(fs::path(p), dest, fs::copy_options::overwrite_existing, e);
            if (!e) {
                fs::remove(fs::path(p), e);
            }
        }
        if (e) {
            out.failed.push_back({p, e.message()});
            continue;
        }
        log << dest.string() << "\t" << p << "\n";
        out.done++;
    }
    return out;
}

GateActionResult gate_restore_quarantined(const std::string& quarantine_dir)
{
    GateActionResult out;
    out.dir = quarantine_dir;
    fs::path log_path = fs::path(quarantine_dir) / GATE_RESTORE_LOG;

    std::ifstream log(log_path.string());
    if (!log) {
        return out;
    }
    std::vector<std::pair<std::string, std::string>> kept;
    std::string line;
    while (std::getline(log, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        std::string from = line.substr(0, tab), to = line.substr(tab + 1);
        std::error_code e;
        if (!fs::exists(fs::path(from), e)) {
            continue;  // already restored or removed by hand
        }
        fs::create_directories(fs::path(to).parent_path(), e);
        fs::path dest = unique_dest(fs::path(to));
        fs::rename(fs::path(from), dest, e);
        if (e) {
            fs::copy_file(fs::path(from), dest, fs::copy_options::overwrite_existing, e);
            if (!e) {
                fs::remove(fs::path(from), e);
            }
        }
        if (e) {
            out.failed.push_back({from, e.message()});
            kept.push_back({from, to});
        } else {
            out.done++;
        }
    }
    log.close();

    // Rewrite the log with only what could not be put back, so a retry is exact.
    std::ofstream rewrite(log_path.string(), std::ios::trunc);
    for (const auto& kv : kept) {
        rewrite << kv.first << "\t" << kv.second << "\n";
    }
    return out;
}

GateActionResult gate_delete_files(const std::vector<std::string>& paths,
                                   const std::string& root)
{
    GateActionResult out;
    out.dir = root;
    for (const auto& p : paths) {
        // The one guard between a wrong verdict and lost audio.
        if (!gate_under_root(root, p)) {
            out.failed.push_back({p, "outside the scanned folder"});
            continue;
        }
        std::error_code e;
        if (fs::remove(fs::path(p), e) && !e) {
            out.done++;
        } else {
            out.failed.push_back({p, e ? e.message() : "could not delete"});
        }
    }
    return out;
}
