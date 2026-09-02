// Dumps the display FFT's output for a deterministic input.
//
// Built twice — once against the old avfft-backed spek-fft.cc, once against the
// self-contained replacement — so the two can be diffed. The replacement exists
// because ffmpeg 8 removed avfft; this is what proves it draws the same picture.

#include <cstdio>
#include <cmath>

#include "../src/spek-fft.h"

int main(int argc, char **argv)
{
    int nbits = argc > 1 ? atoi(argv[1]) : 11;

    FFT fft;
    std::unique_ptr<FFTPlan> plan = fft.create(nbits);
    int n = plan->get_input_size();

    // A deterministic signal with content across the spectrum: a few tones plus
    // a reproducible pseudo-random component, so every bin is exercised.
    unsigned int seed = 12345;
    for (int i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        float noise = ((float)((seed >> 16) & 0x7FFF) / 16384.0f) - 1.0f;
        float v = 0.5f * sinf(2.0f * (float)M_PI * 3.0f * i / n)
                + 0.3f * sinf(2.0f * (float)M_PI * 57.0f * i / n)
                + 0.2f * sinf(2.0f * (float)M_PI * (n / 2.0f - 1.0f) * i / n)
                + 0.05f * noise;
        plan->set_input(i, v);
    }

    plan->execute();

    for (int i = 0; i < plan->get_output_size(); i++) {
        printf("%d %.4f\n", i, plan->get_output(i));
    }
    return 0;
}
