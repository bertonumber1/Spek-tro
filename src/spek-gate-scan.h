#pragma once

// Folder scanning, reporting and the file actions that act on a scan's results.
//
// Ported from label2lossless's spectral.py alongside spek-gate.cc. Kept free of
// wxWidgets so it can be driven by the GUI, by a headless CLI, or by a test.

#include "spek-gate.h"

#include <functional>
#include <string>
#include <vector>

// Files that have been pulled out of a scanned tree live here, so a second scan
// does not re-report what the first one quarantined.
extern const char *const GATE_QUARANTINE_DIRNAME;
extern const char *const GATE_RESTORE_LOG;

struct GateScan
{
    std::string root;
    int total_files = 0;
    int scanned = 0;
    bool stopped = false;
    std::vector<GateResult> results;   // sorted worst-first

    int count_of(GateVerdict v) const;
    // Everything a scan flagged: lossy, padded, upsampled or suspect. Never
    // clean, never lossy-by-design, never unreadable.
    std::vector<const GateResult*> flagged() const;
};

// Every lossless-claiming file under `root`, minus anything already quarantined.
// `include_lossy` also lists .mp3/.aac/.ogg/etc — see gate_is_checkable().
std::vector<std::string> gate_find_audio_files(const std::string& root, bool recursive,
                                               bool include_lossy = false);

// Analyse each path. `on_progress(done, total, name)` is called before each file;
// `should_stop()` is polled between files so a long scan stays interruptible.
// `force_measure` is passed straight to gate_analyse() — see there.
GateScan gate_scan_paths(
    const std::vector<std::string>& paths,
    const std::string& root,
    const std::function<void(int, int, const std::string&)>& on_progress = nullptr,
    const std::function<bool()>& should_stop = nullptr,
    double max_seconds = GATE_MAX_ANALYSIS_SECONDS,
    bool force_measure = false);

// A plain-text report including the per-band table, so a finding can be handed to
// a seller or a label as numbers rather than a screenshot.
std::string gate_format_report(const GateScan& scan);

// The same scan as machine-readable JSON, for the export bundle.
std::string gate_format_json(const GateScan& scan);

// A filename both Windows and Linux will accept. Track titles routinely carry
// `?`, `:` and `/` — a remix credit alone can hold two — and on Windows every one
// of those is illegal in a filename.
std::string gate_safe_name(const std::string& name);

// The name an exported spectrogram gets: "verdict - artist - album - title".
// Album is included because the same artist and title legitimately appear on an
// original and a compilation cut, and two identically named files are no use as
// evidence.
std::string gate_export_basename(const GateResult& r);

struct GateActionResult
{
    int done = 0;
    std::vector<std::pair<std::string, std::string>> failed;  // path, error
    std::string dir;
};

// Move flagged files into `root`/_transcode-quarantine, writing a restore log so
// the move can be undone. Refuses any path outside `root`.
GateActionResult gate_quarantine_files(const std::vector<std::string>& paths,
                                       const std::string& root);

// Put quarantined files back where the restore log says they came from.
GateActionResult gate_restore_quarantined(const std::string& quarantine_dir);

// Delete permanently. Refuses any path outside `root` — the one guard between a
// wrong verdict and lost audio.
GateActionResult gate_delete_files(const std::vector<std::string>& paths,
                                   const std::string& root);

// Is `p` inside `root`? Windows compares paths case-insensitively and a plain
// string prefix test does not, so this normalises case on Windows or a delete
// refuses a file the user can plainly see.
bool gate_under_root(const std::string& root, const std::string& p);
