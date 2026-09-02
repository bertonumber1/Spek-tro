// Runs the full gate — decode, accumulate, measure, verdict — over the files
// named on the command line and prints every field, so the same files can be put
// through the Python original and the two compared end to end.
//
// The measurement half is already proven against known answers; this exists to
// prove the half that decodes, windows, transforms and normalises, which is the
// other place a port can silently drift.

#include "../src/spek-gate.h"

#include <cstdio>

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        GateResult r = gate_analyse(argv[i]);
        printf("file=%s\n", r.name.c_str());
        printf("ok=%d\n", r.ok ? 1 : 0);
        printf("error=%s\n", r.error.c_str());
        printf("sample_rate=%d\n", r.sample_rate);
        printf("channels=%d\n", r.channels);
        printf("declared_bits=%d\n", r.declared_bits);
        printf("effective_bits=%d\n", r.effective_bits);
        printf("frames=%d\n", r.frames);
        printf("cutoff_hz=%.1f\n", r.cutoff_hz);
        printf("wall_db=%.1f\n", r.wall_db);
        printf("above_db=%.1f\n", r.above_db);
        printf("top_band_db=%.1f\n", r.top_band_db);
        printf("side_ratio_db=%.1f\n", r.side_ratio_db);
        printf("verdict=%s\n", gate_verdict_name(r.verdict));
        printf("confidence=%d\n", r.confidence);
        printf("estimated_source=%s\n", r.estimated_source.c_str());
        for (auto& reason : r.reasons) {
            printf("reason=%s\n", reason.c_str());
        }
        printf("--\n");
    }
    return 0;
}
