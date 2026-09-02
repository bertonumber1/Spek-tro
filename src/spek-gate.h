#pragma once

// Spectral transcode detection — is this "lossless" file really lossless?
//
// PORT. This is label2lossless's `spectral.py` (itself a vendored copy of
// beatportdl-webui's `bpdl/spectral.py` v2.12.1) rewritten in C++ against Spek's
// own decoder. Three copies now exist; if a real defect is found here, fix all
// three. The constants below are NOT free parameters — they are measured
// round-trip controls, and changing one silently invalidates every verdict.
//
// A lossy encoder throws away everything above a cutoff frequency and cannot put
// it back. Re-encoding to FLAC/WAV rebuilds a lossless *container* around
// permanently lossy audio: extension, bitrate and file size all look right, and
// only the spectrum still knows.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Analysis parameters. Fixed deliberately: Spek's display FFT size is a user
// preference, and these thresholds are calibrated to *these* numbers.
const int GATE_FFT_SIZE = 16384;
const int GATE_HOP = GATE_FFT_SIZE / 2;      // 50% overlap
const double GATE_SILENCE_DBFS = -70.0;      // digital silence / fades, skipped
const double GATE_REF_LOW_HZ = 1000.0;       // mid-band reference, always full of music
const double GATE_REF_HIGH_HZ = 6000.0;
const double GATE_CUTOFF_DROP_DB = 45.0;     // still counts as "energy present"
const double GATE_WALL_SPAN_HZ = 600.0;      // measured either side of the cutoff
const double GATE_MAX_ANALYSIS_SECONDS = 300.0;

enum class GateVerdict
{
    UNKNOWN,
    CLEAN,
    SUSPECT,
    LOSSY,
    UPSAMPLED,   // >48 kHz file with nothing above 22 kHz
    PADDED,      // declared 24-bit, only 16 bits ever used
    LOSSY_FORMAT,// an .mp3/.aac — meant to be lossy, not a finding
    UNREADABLE,
};

const char *gate_verdict_name(GateVerdict v);

// Sort order for a results table: worst first. Mirrors VERDICT_ORDER.
int gate_verdict_rank(GateVerdict v);

struct GateResult
{
    std::string path;
    std::string name;
    bool ok = true;
    std::string error;

    int sample_rate = 0;
    int channels = 0;
    int declared_bits = 0;
    int effective_bits = 0;
    double duration = 0.0;
    int frames = 0;              // FFT frames actually accumulated (silence skipped)

    double cutoff_hz = 0.0;
    double nyquist_hz = 0.0;
    double wall_db = 0.0;        // sharpness of the fall across the cutoff
    double above_db = 0.0;       // mean level above it — the metric that settles arguments
    double top_band_db = 0.0;
    double side_ratio_db = 0.0;  // joint-stereo collapse near the cutoff

    std::string codec;

    // A verdict on "03. Untitled.flac" is not much use when the point is to go
    // back to a seller or refile a release, so every row carries who and what it
    // is. Missing tags are simply blank.
    std::string artist;
    std::string title;
    std::string album;
    std::string date;

    GateVerdict verdict = GateVerdict::UNKNOWN;
    int confidence = 0;
    std::string estimated_source;          // "320 kbps", "MP3 V0 / AAC 256", ...
    std::vector<std::string> reasons;      // why this call was made, in plain words
    std::vector<std::pair<double, double>> bands;  // (freq, dB) for the band table
};

// Analyse one file. Pure function over a path, so it can be run against
// known-answer controls — the only reason to trust any of it.
GateResult gate_analyse(const std::string& path,
                        double max_seconds = GATE_MAX_ANALYSIS_SECONDS);

// The encoder setting a given cutoff implies, at 44.1/48 kHz.
std::string gate_estimate_source(double cutoff_hz);

// Is this an extension worth checking? Deliberately excludes .mp3/.aac/.ogg —
// those are *meant* to be lossy, so a wall in one is noise, not a finding.
bool gate_is_checkable(const std::string& path);

// ---- exposed for the known-answer tests -------------------------------------
//
// The verdicts are only trustworthy if these match the Python original bit for
// bit, so they are reachable from tests/test-gate.cc rather than being static.

int gate_effective_bits(int64_t mask);
std::vector<double> gate_smooth(const std::vector<double>& a, int width);
double gate_band_at(const std::vector<double>& freqs, const std::vector<double>& spec,
                    double lo, double hi);
void gate_measure(GateResult& res, const std::vector<double>& freqs,
                  const std::vector<double>& smooth, const std::vector<double>& raw);
double gate_side_ratio(const std::vector<double>& freqs, const std::vector<double>& mid_sum,
                       const std::vector<double>& side_sum, double cutoff);
std::vector<std::pair<double, double>> gate_band_table(const std::vector<double>& freqs,
                                                       const std::vector<double>& smooth);
void gate_verdict(GateResult& res);
