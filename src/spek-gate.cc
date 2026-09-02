// Spectral transcode detection — the measurement half.
//
// Everything here is a pure function over an already-accumulated spectrum, with
// no libav dependency, so it can be unit-tested against the Python original's
// known answers without decoding anything. The decode lives in
// spek-gate-decode.cc. See spek-gate.h for why the constants are not free
// parameters.

#include "spek-gate.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <limits>

// Cutoff -> the encoder setting that produces it, at 44.1/48 kHz. Ranges are the
// measured round-trip controls, not folklore.
static const struct { double edge; const char *label; } BITRATE_TABLE[] = {
    {16500.0, "128 kbps or lower"},
    {18000.0, "160 kbps"},
    {19000.0, "192 kbps"},
    {19800.0, "256 kbps / MP3 V2"},
    {20400.0, "320 kbps"},
    {21200.0, "MP3 V0 / AAC 256"},
};

std::string gate_estimate_source(double cutoff_hz)
{
    for (const auto& row : BITRATE_TABLE) {
        if (cutoff_hz < row.edge) {
            return row.label;
        }
    }
    return "high-bitrate lossy";
}

const char *gate_verdict_name(GateVerdict v)
{
    switch (v) {
    case GateVerdict::CLEAN: return "clean";
    case GateVerdict::SUSPECT: return "suspect";
    case GateVerdict::LOSSY: return "lossy";
    case GateVerdict::UPSAMPLED: return "upsampled";
    case GateVerdict::PADDED: return "padded";
    case GateVerdict::LOSSY_FORMAT: return "lossy_format";
    case GateVerdict::UNREADABLE: return "unreadable";
    default: return "unknown";
    }
}

int gate_verdict_rank(GateVerdict v)
{
    switch (v) {
    case GateVerdict::LOSSY: return 0;
    case GateVerdict::PADDED: return 1;
    case GateVerdict::UPSAMPLED: return 2;
    case GateVerdict::SUSPECT: return 3;
    case GateVerdict::UNREADABLE: return 4;
    case GateVerdict::CLEAN: return 5;
    case GateVerdict::LOSSY_FORMAT: return 6;
    default: return 9;
    }
}

bool gate_is_checkable(const std::string& path)
{
    static const char *exts[] = {
        ".flac", ".wav", ".aiff", ".aif", ".alac", ".m4a", ".ape", ".wv", ".tta"
    };
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    std::string ext = path.substr(dot);
    for (auto& c : ext) {
        c = (char)std::tolower((unsigned char)c);
    }
    for (const char *e : exts) {
        if (ext == e) {
            return true;
        }
    }
    return false;
}

// Round to one decimal, matching the Python original's round() closely enough for
// display and for the threshold comparisons, which are never near a .05 boundary.
static double round1(double v)
{
    return std::round(v * 10.0) / 10.0;
}

// How many bits the samples actually use. ffmpeg left-aligns into 32 bits, so a
// genuine 16-bit master leaves the low 16 always zero, and a 24-bit file that was
// really 16-bit upscaled leaves the low 8 always zero on top of that.
int gate_effective_bits(int64_t mask)
{
    if (mask <= 0) {
        return 0;
    }
    int trailing = 0;
    while ((mask & 1) == 0) {
        mask >>= 1;
        trailing++;
    }
    return std::max(0, 32 - trailing);
}

// Moving average with the ends held, not zero-padded.
//
// A plain "same" convolution pads with zeros, and these are dB values where zero
// is the *loudest* possible bin — so it would make the top of every spectrum climb
// towards 0 dB and hide the very brick wall this is looking for.
std::vector<double> gate_smooth(const std::vector<double>& a, int width)
{
    if (width < 2 || a.empty()) {
        return a;
    }
    int pad = width / 2;
    int n = (int)a.size();
    std::vector<double> out(n);
    double inv = 1.0 / width;
    for (int i = 0; i < n; i++) {
        double sum = 0.0;
        for (int k = 0; k < width; k++) {
            int idx = i + k - pad;
            // Ends held rather than zero-padded.
            if (idx < 0) idx = 0;
            if (idx >= n) idx = n - 1;
            sum += a[idx];
        }
        out[i] = sum * inv;
    }
    return out;
}

// Mean level over a frequency band, or -inf if the band holds no bins.
double gate_band_at(const std::vector<double>& freqs, const std::vector<double>& spec,
                    double lo, double hi)
{
    double sum = 0.0;
    int count = 0;
    for (size_t i = 0; i < freqs.size() && i < spec.size(); i++) {
        if (freqs[i] >= lo && freqs[i] <= hi) {
            sum += spec[i];
            count++;
        }
    }
    if (!count) {
        return -std::numeric_limits<double>::infinity();
    }
    return sum / count;
}

void gate_measure(GateResult& res, const std::vector<double>& freqs,
                  const std::vector<double>& smooth, const std::vector<double>& raw)
{
    double ref = gate_band_at(freqs, smooth, GATE_REF_LOW_HZ, GATE_REF_HIGH_HZ);
    double threshold = ref - GATE_CUTOFF_DROP_DB;

    // The highest frequency still within CUTOFF_DROP_DB of the mid-band reference.
    res.cutoff_hz = GATE_REF_HIGH_HZ;
    for (size_t i = 0; i < freqs.size() && i < smooth.size(); i++) {
        if (smooth[i] >= threshold && freqs[i] > GATE_REF_HIGH_HZ) {
            res.cutoff_hz = freqs[i];
        }
    }

    double below = gate_band_at(freqs, smooth,
                                std::max(0.0, res.cutoff_hz - GATE_WALL_SPAN_HZ), res.cutoff_hz);
    double over = gate_band_at(freqs, smooth,
                               res.cutoff_hz, res.cutoff_hz + GATE_WALL_SPAN_HZ);
    res.wall_db = std::isfinite(over) ? round1(below - over) : 0.0;

    // Everything above the cutoff, measured on the *unsmoothed* spectrum: smoothing
    // would drag the brick wall's shoulder into the empty band and soften the number
    // that matters most.
    double top_start = std::min(res.cutoff_hz + 500.0, res.nyquist_hz - 200.0);
    res.above_db = round1(gate_band_at(freqs, raw, top_start, res.nyquist_hz - 100.0));
    res.top_band_db = round1(gate_band_at(freqs, raw, res.nyquist_hz * 0.95, res.nyquist_hz));
    res.wall_db = round1(res.wall_db);
    res.cutoff_hz = round1(res.cutoff_hz);
}

// Side-channel energy just under the cutoff, relative to mid. Joint-stereo
// encoders collapse the top of the stereo image to mono, so a near-empty side
// channel up there is a second, independent sign of a lossy ancestor.
double gate_side_ratio(const std::vector<double>& freqs, const std::vector<double>& mid_sum,
                       const std::vector<double>& side_sum, double cutoff)
{
    double lo = std::max(cutoff * 0.75, 8000.0);
    double hi = std::max(cutoff, 8100.0);
    double m = 0.0, s = 0.0;
    bool any = false;
    for (size_t i = 0; i < freqs.size(); i++) {
        if (freqs[i] >= lo && freqs[i] <= hi) {
            m += mid_sum[i];
            s += side_sum[i];
            any = true;
        }
    }
    if (!any || m <= 0.0) {
        return 0.0;
    }
    return round1(10.0 * std::log10(std::max(s, 1e-30) / m));
}

std::vector<std::pair<double, double>> gate_band_table(const std::vector<double>& freqs,
                                                       const std::vector<double>& smooth)
{
    static const double edges[] = {
        0, 1000, 2000, 4000, 8000, 12000, 14000, 16000, 17000, 18000, 19000,
        19500, 20000, 20500, 21000, 22050, 24000, 32000, 48000, 96000
    };
    std::vector<std::pair<double, double>> out;
    if (freqs.empty()) {
        return out;
    }
    double top = freqs.back();
    int n = (int)(sizeof(edges) / sizeof(edges[0]));
    for (int i = 0; i + 1 < n; i++) {
        double lo = edges[i];
        if (lo >= top) {
            break;
        }
        double v = gate_band_at(freqs, smooth, lo, std::min(edges[i + 1], top));
        if (std::isfinite(v)) {
            out.push_back({lo, v});
        }
    }
    return out;
}

// Turn the numbers into a call, and say which numbers made it.
//
// The bar for `lossy` is deliberately all three of: a cutoff well under Nyquist,
// a sharp wall at it, and a dead band above it. A mastering engineer can produce
// any one of those; an encoder produces all three together.
void gate_verdict(GateResult& res)
{
    char buf[256];
    std::vector<std::string> reasons;
    double nyq = res.nyquist_hz;
    double headroom = nyq - res.cutoff_hz;

    if (res.declared_bits >= 24 && res.effective_bits > 0 && res.effective_bits <= 16) {
        res.verdict = GateVerdict::PADDED;
        res.confidence = 95;
        snprintf(buf, sizeof(buf),
                 "declared %d-bit but only %d bits are ever used - padded up from a %d-bit master",
                 res.declared_bits, res.effective_bits, res.effective_bits);
        res.reasons = {buf};
        return;
    }

    if (res.sample_rate > 48000 && res.cutoff_hz < 22000.0) {
        res.verdict = GateVerdict::UPSAMPLED;
        res.confidence = 90;
        snprintf(buf, sizeof(buf),
                 "%.1f kHz file with nothing above %.1f kHz - upsampled, not a high-resolution master",
                 res.sample_rate / 1000.0, res.cutoff_hz / 1000.0);
        res.reasons = {buf};
        return;
    }

    bool dead_above = res.above_db <= -90.0;
    bool sharp_wall = res.wall_db >= 20.0;
    bool low_cutoff = headroom >= 900.0;

    if (low_cutoff) {
        snprintf(buf, sizeof(buf), "cuts off at %.1f kHz, %.1f kHz below Nyquist",
                 res.cutoff_hz / 1000.0, headroom / 1000.0);
        reasons.push_back(buf);
    }
    if (sharp_wall) {
        snprintf(buf, sizeof(buf), "%.0f dB brick wall across the cutoff", res.wall_db);
        reasons.push_back(buf);
    }
    if (dead_above) {
        snprintf(buf, sizeof(buf),
                 "only %.0f dB above it - below the dither floor any real 16-bit master leaves behind",
                 res.above_db);
        reasons.push_back(buf);
    }
    if (res.side_ratio_db <= -40.0 && low_cutoff) {
        snprintf(buf, sizeof(buf),
                 "stereo side channel %.0f dB under mid near the cutoff - joint-stereo collapse",
                 res.side_ratio_db);
        reasons.push_back(buf);
    }

    int score = (low_cutoff ? 30 : 0) + (sharp_wall ? 30 : 0) + (dead_above ? 35 : 0);
    if (res.side_ratio_db <= -40.0 && low_cutoff) {
        score += 5;
    }

    if (low_cutoff && sharp_wall && dead_above) {
        res.verdict = GateVerdict::LOSSY;
        res.estimated_source = gate_estimate_source(res.cutoff_hz);
        res.confidence = std::min(99, score);
    } else if (score >= 50) {
        res.verdict = GateVerdict::SUSPECT;
        res.estimated_source = gate_estimate_source(res.cutoff_hz);
        res.confidence = score;
    } else {
        res.verdict = GateVerdict::CLEAN;
        res.confidence = std::max(60, 100 - score);
        if (reasons.empty()) {
            snprintf(buf, sizeof(buf),
                     "full spectrum to %.1f kHz with a natural noise floor above it (%.0f dB)",
                     res.cutoff_hz / 1000.0, res.above_db);
            reasons.push_back(buf);
        }
    }
    res.reasons = reasons;
}
