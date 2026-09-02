// Known-answer harness for the gate's measurement half.
//
// Reads a spectrum on stdin and prints the measurements it produces, so the same
// spectrum can be pushed through the Python original and the two compared. The
// verdicts inherit their authority from that comparison; without it this is just
// plausible-looking arithmetic.
//
// stdin format (whitespace separated):
//   sample_rate declared_bits effective_bits n
//   mid_db[0] .. mid_db[n-1]          (already peak-normalised to 0 dB)
//   mid_pow[0] .. mid_pow[n-1]        (linear power, for the side ratio)
//   side_pow[0] .. side_pow[n-1]

#include "../src/spek-gate.h"

#include <cstdio>
#include <vector>

int main()
{
    int sample_rate = 0, declared_bits = 0, effective_bits = 0, n = 0;
    if (scanf("%d %d %d %d", &sample_rate, &declared_bits, &effective_bits, &n) != 4) {
        fprintf(stderr, "bad header\n");
        return 2;
    }

    std::vector<double> mid_db(n), mid_pow(n), side_pow(n);
    for (int i = 0; i < n; i++) {
        if (scanf("%lf", &mid_db[i]) != 1) { fprintf(stderr, "bad mid_db\n"); return 2; }
    }
    for (int i = 0; i < n; i++) {
        if (scanf("%lf", &mid_pow[i]) != 1) { fprintf(stderr, "bad mid_pow\n"); return 2; }
    }
    for (int i = 0; i < n; i++) {
        if (scanf("%lf", &side_pow[i]) != 1) { fprintf(stderr, "bad side_pow\n"); return 2; }
    }

    // rfftfreq(FFT_SIZE, 1/sample_rate) for the n bins we were handed.
    std::vector<double> freqs(n);
    for (int i = 0; i < n; i++) {
        freqs[i] = (double)i * sample_rate / (double)GATE_FFT_SIZE;
    }

    GateResult res;
    res.sample_rate = sample_rate;
    res.declared_bits = declared_bits;
    res.effective_bits = effective_bits;
    res.nyquist_hz = sample_rate / 2.0;

    std::vector<double> smooth = gate_smooth(mid_db, 9);
    gate_measure(res, freqs, smooth, mid_db);

    bool any_side = false;
    for (int i = 0; i < n; i++) {
        if (side_pow[i] != 0.0) { any_side = true; break; }
    }
    if (any_side) {
        res.side_ratio_db = gate_side_ratio(freqs, mid_pow, side_pow, res.cutoff_hz);
    }
    res.bands = gate_band_table(freqs, smooth);
    gate_verdict(res);

    printf("cutoff_hz=%.1f\n", res.cutoff_hz);
    printf("wall_db=%.1f\n", res.wall_db);
    printf("above_db=%.1f\n", res.above_db);
    printf("top_band_db=%.1f\n", res.top_band_db);
    printf("side_ratio_db=%.1f\n", res.side_ratio_db);
    printf("verdict=%s\n", gate_verdict_name(res.verdict));
    printf("confidence=%d\n", res.confidence);
    printf("estimated_source=%s\n", res.estimated_source.c_str());
    printf("bands=%d\n", (int)res.bands.size());
    for (auto& b : res.bands) {
        printf("band %.0f %.1f\n", b.first, b.second);
    }
    printf("reasons=%d\n", (int)res.reasons.size());
    for (auto& r : res.reasons) {
        printf("reason %s\n", r.c_str());
    }
    return 0;
}
