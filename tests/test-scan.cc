// Exercises the folder scan and, more importantly, the actions that move and
// delete files. A wrong verdict costing someone their audio is the worst thing
// this app could do, so the containment guard and the restore round-trip are
// tested directly rather than trusted.

#include "../src/spek-gate-scan.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool cond, const std::string& what)
{
    printf("%s  %s\n", cond ? "ok  " : "FAIL", what.c_str());
    if (!cond) {
        failures++;
    }
}

static void touch(const fs::path& p, const std::string& content = "x")
{
    fs::create_directories(p.parent_path());
    std::ofstream(p.string()) << content;
}

int main(int argc, char **argv)
{
    fs::path tmp = fs::temp_directory_path() / "spektro-test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // ---- containment guard ----------------------------------------------------
    check(gate_under_root(tmp.string(), (tmp / "a.flac").string()),
          "a file directly inside the root is inside it");
    check(gate_under_root(tmp.string(), (tmp / "album" / "a.flac").string()),
          "a file in a subfolder is inside the root");
    check(!gate_under_root(tmp.string(), "/etc/passwd"),
          "a file outside the root is refused");
    check(!gate_under_root((tmp / "album").string(), (tmp / "other" / "a.flac").string()),
          "a sibling folder is not inside the root");
    // The classic prefix bug: /music must not contain /music-backup/x.
    check(!gate_under_root((tmp / "music").string(), (tmp / "music-backup" / "a.flac").string()),
          "a folder whose name merely starts the same is refused");

    // ---- safe names -----------------------------------------------------------
    check(gate_safe_name("A/B:C?D*E") == "A_B_C_D_E", "illegal filename characters replaced");
    check(gate_safe_name("  ") == "spectrogram", "an empty name falls back");
    check(gate_safe_name(std::string(300, 'x')).size() == 120, "a long name is truncated");

    // ---- file discovery -------------------------------------------------------
    touch(tmp / "album" / "01.flac");
    touch(tmp / "album" / "02.wav");
    touch(tmp / "album" / "cover.jpg");
    touch(tmp / "album" / "notes.txt");
    touch(tmp / "album" / "sub" / "03.flac");
    touch(tmp / GATE_QUARANTINE_DIRNAME / "old.flac");

    auto found = gate_find_audio_files(tmp.string(), true);
    check(found.size() == 3, "recursive scan finds 3 audio files, skipping art and text");
    bool has_quarantined = false;
    for (const auto& f : found) {
        if (f.find(GATE_QUARANTINE_DIRNAME) != std::string::npos) {
            has_quarantined = true;
        }
    }
    check(!has_quarantined, "already-quarantined files are not re-reported");

    auto shallow = gate_find_audio_files((tmp / "album").string(), false);
    check(shallow.size() == 2, "non-recursive scan stays in the one folder");

    // ---- quarantine and restore round-trip ------------------------------------
    fs::path root = tmp / "album";
    std::vector<std::string> victims = {
        (root / "01.flac").string(),
        (root / "sub" / "03.flac").string(),
    };
    auto q = gate_quarantine_files(victims, root.string());
    check(q.done == 2 && q.failed.empty(), "both files quarantined");
    check(!fs::exists(root / "01.flac"), "the quarantined file left its original place");
    check(fs::exists(fs::path(q.dir) / GATE_RESTORE_LOG), "a restore log was written");
    // The tree is mirrored, so same-named tracks on different albums cannot collide.
    check(fs::exists(fs::path(q.dir) / "sub" / "03.flac"), "the source tree is mirrored");

    auto r = gate_restore_quarantined(q.dir);
    check(r.done == 2 && r.failed.empty(), "both files restored");
    check(fs::exists(root / "01.flac"), "the file is back where it started");
    check(fs::exists(root / "sub" / "03.flac"), "the nested file is back too");

    // ---- quarantine refuses to reach outside the root -------------------------
    fs::path outsider = tmp / "elsewhere" / "x.flac";
    touch(outsider);
    auto bad = gate_quarantine_files({outsider.string()}, root.string());
    check(bad.done == 0 && bad.failed.size() == 1, "quarantine refuses a path outside the root");
    check(fs::exists(outsider), "the outside file was not touched");

    // ---- delete, and its guard ------------------------------------------------
    auto badder = gate_delete_files({outsider.string()}, root.string());
    check(badder.done == 0 && badder.failed.size() == 1, "delete refuses a path outside the root");
    check(fs::exists(outsider), "the outside file survived the delete attempt");

    auto del = gate_delete_files({(root / "02.wav").string()}, root.string());
    check(del.done == 1 && del.failed.empty(), "delete removes a file inside the root");
    check(!fs::exists(root / "02.wav"), "the deleted file is gone");

    // ---- report and json over a real scan, if controls were given -------------
    if (argc > 1) {
        auto paths = gate_find_audio_files(argv[1], true);
        auto scan = gate_scan_paths(paths, argv[1]);
        std::string report = gate_format_report(scan);
        std::string json = gate_format_json(scan);
        check(report.find("TRANSCODE / FAKE-LOSSLESS SCAN") != std::string::npos,
              "report has a header");
        check(json.find("\"results\"") != std::string::npos, "json has results");
        check(scan.scanned == (int)paths.size(), "every file was scanned");
        printf("\n--- report ---\n%s\n", report.c_str());
    }

    fs::remove_all(tmp);
    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
