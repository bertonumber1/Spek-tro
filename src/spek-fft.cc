// The display FFT.
//
// Upstream Spek used ffmpeg's `libavcodec/avfft.h` (av_rdft_*). That API was
// removed in ffmpeg 8, so upstream Spek does not build against a current ffmpeg
// at all — the header is simply gone. Rather than pin the whole project to an
// old ffmpeg, this is a self-contained radix-2 transform with no external
// dependency.
//
// It reproduces av_rdft's DFT_R2C output packing exactly, so execute()'s
// magnitude arithmetic — and therefore the picture on screen — is unchanged:
//
//     out[0]       = Re(X[0])          (DC)
//     out[1]       = Re(X[n/2])        (Nyquist, packed into the imaginary slot
//                                       of DC, which is always zero for real input)
//     out[2i]      = Re(X[i])
//     out[2i + 1]  = Im(X[i])          for i = 1 .. n/2 - 1
//
// tests/test-fft.cc checks this against known answers; on a machine whose ffmpeg
// still ships avfft, tests/fft-compare.cc checks it against av_rdft directly.
//
// Note this is the *display* transform and is deliberately float, matching what
// upstream drew. The detector in spek-gate.cc has its own double-precision
// transform, because its thresholds are calibrated to that precision.

#include <cmath>
#include <vector>

#include "spek-fft.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class FFTPlanImpl : public FFTPlan
{
public:
    FFTPlanImpl(int nbits);
    ~FFTPlanImpl() override {}

    void execute() override;

private:
    void transform(std::vector<float>& re, std::vector<float>& im) const;

    int nbits;
    int n;
    std::vector<int> reverse;    // bit-reversal permutation, precomputed
    std::vector<float> cos_tab;  // twiddles, precomputed per stage
    std::vector<float> sin_tab;
    std::vector<float> re;       // scratch, reused across calls
    std::vector<float> im;
};

std::unique_ptr<FFTPlan> FFT::create(int nbits)
{
    return std::unique_ptr<FFTPlan>(new FFTPlanImpl(nbits));
}

FFTPlanImpl::FFTPlanImpl(int nbits)
    : FFTPlan(nbits), nbits(nbits), n(1 << nbits),
      reverse(1 << nbits), re(1 << nbits), im(1 << nbits)
{
    // Bit reversal, computed once: this runs per image column, so the inner
    // loops should do arithmetic and nothing else.
    for (int i = 0; i < this->n; i++) {
        int r = 0;
        for (int b = 0; b < nbits; b++) {
            if (i & (1 << b)) {
                r |= 1 << (nbits - 1 - b);
            }
        }
        this->reverse[i] = r;
    }

    // One twiddle per (stage, k). Sized n - 1 across all stages.
    this->cos_tab.resize(this->n);
    this->sin_tab.resize(this->n);
    for (int len = 2, base = 0; len <= this->n; len <<= 1) {
        int half = len / 2;
        for (int k = 0; k < half; k++) {
            double ang = -2.0 * M_PI * k / len;
            this->cos_tab[base + k] = (float)std::cos(ang);
            this->sin_tab[base + k] = (float)std::sin(ang);
        }
        base += half;
    }
}

void FFTPlanImpl::transform(std::vector<float>& vre, std::vector<float>& vim) const
{
    for (int len = 2, base = 0; len <= this->n; len <<= 1) {
        int half = len / 2;
        for (int i = 0; i < this->n; i += len) {
            for (int k = 0; k < half; k++) {
                float wr = this->cos_tab[base + k];
                float wi = this->sin_tab[base + k];
                int a = i + k;
                int b = a + half;
                float xr = vre[b] * wr - vim[b] * wi;
                float xi = vre[b] * wi + vim[b] * wr;
                vre[b] = vre[a] - xr;
                vim[b] = vim[a] - xi;
                vre[a] += xr;
                vim[a] += xi;
            }
        }
        base += half;
    }
}

void FFTPlanImpl::execute()
{
    // Real input, so the imaginary part starts at zero. Load in bit-reversed
    // order so the butterflies below run in natural order.
    for (int i = 0; i < this->n; i++) {
        this->re[this->reverse[i]] = this->get_input(i);
        this->im[i] = 0.0f;
    }
    this->transform(this->re, this->im);

    // Magnitudes, in the same dB form upstream produced.
    float n2 = (float)this->n * (float)this->n;
    this->set_output(0, 10.0f * log10f(this->re[0] * this->re[0] / n2));
    this->set_output(this->n / 2,
                     10.0f * log10f(this->re[this->n / 2] * this->re[this->n / 2] / n2));
    for (int i = 1; i < this->n / 2; i++) {
        float r = this->re[i];
        float m = this->im[i];
        this->set_output(i, 10.0f * log10f((r * r + m * m) / n2));
    }
}
