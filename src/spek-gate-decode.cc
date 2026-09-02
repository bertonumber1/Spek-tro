// Spectral transcode detection — the decode half.
//
// The Python original shells out to `ffmpeg -f s32le -acodec pcm_s32le`. Spek
// already links libavformat/libavcodec/libavutil, so this decodes in-process
// instead — but it must produce the *same integers* that pipe would, because the
// bit-depth check reads them directly. See the conversion table in to_s32().
//
// Signed 32-bit rather than float is deliberate: it keeps the original integers
// exact, so one decode pass yields both the spectrum and the real bit depth (a
// "24-bit" file padded up from a 16-bit master has eight dead low bits, and no
// amount of FFT would show that).
//
// Spek does not link libswresample, so the format conversion is done by hand
// rather than adding a dependency to a fork that already has to be built by MXE.

#include "spek-gate.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

// M_PI is not in the C++ standard; MSVC omits it without _USE_MATH_DEFINES and
// some embedded toolchains omit it entirely. This fork targets Windows, Linux,
// arm64 and armv7, so it cannot assume any one libm's extensions.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Channel count moved from AVCodecParameters::channels to ::ch_layout in ffmpeg
// 5.1 (libavcodec 59.37). Spek master requires that version, but a fork that is
// meant to build on Raspberry Pi OS, Debian stable and Ubuntu 22.04 has to cope
// with 4.x as well — those ship libavcodec 58.
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 37, 100)
#define GATE_NB_CHANNELS(par) ((par)->ch_layout.nb_channels)
#else
#define GATE_NB_CHANNELS(par) ((par)->channels)
#endif

// ---- a double-precision FFT ---------------------------------------------------
//
// Deliberately not Spek's spek-fft: that one is float and its transform size is a
// user preference. The gate's thresholds are calibrated against numpy's float64
// rfft at a fixed 16384, and a port that quietly used a different precision or
// size would produce numbers that no longer mean what the constants say.

static void fft_radix2(std::vector<double>& re, std::vector<double>& im)
{
    const int n = (int)re.size();
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / len;
        double wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                double xr = re[b] * cr - im[b] * ci;
                double xi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
                double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}

// Power spectrum of a real signal, bins 0..n/2, matching |np.fft.rfft(x)|**2.
static void rfft_power(const std::vector<double>& x, std::vector<double>& out)
{
    const int n = (int)x.size();
    std::vector<double> re(x), im(n, 0.0);
    fft_radix2(re, im);
    const int bins = n / 2 + 1;
    out.resize(bins);
    for (int i = 0; i < bins; i++) {
        out[i] = re[i] * re[i] + im[i] * im[i];
    }
}

// ---- sample conversion --------------------------------------------------------

// One sample as ffmpeg's pcm_s32le would write it. S16 is left-shifted rather
// than scaled, which is exactly what leaves the low 16 bits of a 16-bit master
// dead and makes the `padded` verdict possible.
static inline int32_t to_s32(const uint8_t *data, int offset, AVSampleFormat fmt)
{
    switch (fmt) {
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
        return ((int32_t)((const uint8_t*)data)[offset] - 128) << 24;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
        return (int32_t)((const int16_t*)data)[offset] << 16;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
        return ((const int32_t*)data)[offset];
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP: {
        double v = ((const float*)data)[offset] * 2147483648.0;
        if (v >= 2147483647.0) return INT32_MAX;
        if (v <= -2147483648.0) return INT32_MIN;
        return (int32_t)std::lrint(v);
    }
    case AV_SAMPLE_FMT_DBL:
    case AV_SAMPLE_FMT_DBLP: {
        double v = ((const double*)data)[offset] * 2147483648.0;
        if (v >= 2147483647.0) return INT32_MAX;
        if (v <= -2147483648.0) return INT32_MIN;
        return (int32_t)std::lrint(v);
    }
    default:
        return 0;
    }
}

static bool is_lossy_codec(AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_MP3:
    case AV_CODEC_ID_MP2:
    case AV_CODEC_ID_MP1:
    case AV_CODEC_ID_AAC:
    case AV_CODEC_ID_AAC_LATM:
    case AV_CODEC_ID_VORBIS:
    case AV_CODEC_ID_OPUS:
    case AV_CODEC_ID_WMAV1:
    case AV_CODEC_ID_WMAV2:
    case AV_CODEC_ID_AC3:
    case AV_CODEC_ID_EAC3:
    case AV_CODEC_ID_DTS:
    case AV_CODEC_ID_ATRAC3:
        return true;
    default:
        return false;
    }
}

// ---- the analysis -------------------------------------------------------------

namespace {

// Accumulates the long-term average power spectrum, one FFT frame at a time.
struct Accumulator
{
    std::vector<double> window;
    double win_power = 0.0;
    std::vector<double> mid_sum;
    std::vector<double> side_sum;
    std::vector<double> mid_frame;
    std::vector<double> side_frame;
    std::vector<double> power;
    double silence_amp = 0.0;
    int used = 0;
    bool have_side = false;

    Accumulator()
    {
        int n = GATE_FFT_SIZE;
        window.resize(n);
        // np.hanning: symmetric, 0.5 - 0.5*cos(2*pi*i/(n-1)).
        for (int i = 0; i < n; i++) {
            window[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (n - 1));
            win_power += window[i] * window[i];
        }
        mid_sum.assign(n / 2 + 1, 0.0);
        side_sum.assign(n / 2 + 1, 0.0);
        mid_frame.resize(n);
        side_frame.resize(n);
        // Silence is measured against full scale, so the threshold is a constant
        // number of counts rather than something that drifts with the material.
        silence_amp = std::pow(10.0, GATE_SILENCE_DBFS / 20.0) * 2147483648.0;
    }

    // `pending` holds interleaved s32 frames; consume as many FFT frames as fit.
    void consume(std::vector<int32_t>& pending, int channels)
    {
        const int n = GATE_FFT_SIZE;
        size_t pos = 0;
        size_t total_frames = pending.size() / channels;
        while (pos + n <= total_frames) {
            double sq = 0.0;
            if (channels >= 2) {
                for (int i = 0; i < n; i++) {
                    double l = (double)pending[(pos + i) * channels];
                    double r = (double)pending[(pos + i) * channels + 1];
                    double m = (l + r) * 0.5;
                    mid_frame[i] = m;
                    side_frame[i] = (l - r) * 0.5;
                    sq += m * m;
                }
            } else {
                for (int i = 0; i < n; i++) {
                    double m = (double)pending[(pos + i) * channels];
                    mid_frame[i] = m;
                    sq += m * m;
                }
            }
            pos += GATE_HOP;

            // Digital silence and fades carry no spectrum worth averaging, and
            // including them drags the whole curve down towards the noise floor.
            if (std::sqrt(sq / n) < silence_amp) {
                continue;
            }

            for (int i = 0; i < n; i++) {
                mid_frame[i] *= window[i];
            }
            rfft_power(mid_frame, power);
            for (size_t i = 0; i < mid_sum.size(); i++) {
                mid_sum[i] += power[i];
            }

            if (channels >= 2) {
                for (int i = 0; i < n; i++) {
                    side_frame[i] *= window[i];
                }
                rfft_power(side_frame, power);
                for (size_t i = 0; i < side_sum.size(); i++) {
                    side_sum[i] += power[i];
                }
                have_side = true;
            }
            used++;
        }
        // Keep the unconsumed tail, exactly as the Python slices `pending`.
        if (pos) {
            pending.erase(pending.begin(), pending.begin() + pos * channels);
        }
    }
};

} // namespace

GateResult gate_analyse(const std::string& path, double max_seconds)
{
    GateResult res;
    res.path = path;
    size_t slash = path.find_last_of("/\\");
    res.name = (slash == std::string::npos) ? path : path.substr(slash + 1);

    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;

    // A corrupt file is a result, not a crash: a folder scan must not stop on the
    // one track with a broken header.
    auto fail = [&](const char *msg) -> GateResult& {
        res.ok = false;
        res.error = msg;
        res.verdict = GateVerdict::UNREADABLE;
        return res;
    };
    auto cleanup = [&]() {
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (dec) avcodec_free_context(&dec);
        if (fmt) avformat_close_input(&fmt);
    };

    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        cleanup();
        return fail("cannot open file");
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        cleanup();
        return fail("could not read stream info");
    }

    int stream_index = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        cleanup();
        return fail("no audio stream");
    }

    AVStream *stream = fmt->streams[stream_index];
    AVCodecParameters *par = stream->codecpar;

    res.sample_rate = par->sample_rate;
    res.channels = GATE_NB_CHANNELS(par);
    if (res.channels <= 0) {
        res.channels = 1;
    }
    res.declared_bits = par->bits_per_raw_sample;
    if (!res.declared_bits) {
        res.declared_bits = par->bits_per_coded_sample;
    }
    res.nyquist_hz = res.sample_rate / 2.0;
    if (stream->duration != AV_NOPTS_VALUE) {
        res.duration = stream->duration * av_q2d(stream->time_base);
    } else if (fmt->duration != AV_NOPTS_VALUE) {
        res.duration = fmt->duration / (double)AV_TIME_BASE;
    }

    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    res.codec = codec ? codec->name : "";

    // Vorbis comments, MP4 atoms and ID3 all spell these differently; libav
    // normalises the common ones, and the stream can override the container.
    auto tag = [&](const char *key) -> std::string {
        AVDictionaryEntry *e = av_dict_get(stream->metadata, key, nullptr,
                                           AV_DICT_IGNORE_SUFFIX);
        if (!e) {
            e = av_dict_get(fmt->metadata, key, nullptr, AV_DICT_IGNORE_SUFFIX);
        }
        return (e && e->value) ? e->value : "";
    };
    res.artist = tag("artist");
    if (res.artist.empty()) {
        res.artist = tag("album_artist");
    }
    res.title = tag("title");
    res.album = tag("album");
    res.date = tag("date");
    if (res.date.empty()) {
        res.date = tag("year");
    }

    // A lossy format is not a finding: an .mp3 is *meant* to have a wall, and the
    // same .m4a extension carries both ALAC and AAC, so the codec decides.
    if (is_lossy_codec(par->codec_id)) {
        res.verdict = GateVerdict::LOSSY_FORMAT;
        res.confidence = 100;
        res.reasons = {res.codec + " — a lossy format by design, so there is nothing to fake here"};
        cleanup();
        return res;
    }

    if (!res.sample_rate) {
        cleanup();
        return fail("could not read stream info");
    }
    if (!codec) {
        cleanup();
        return fail("no decoder");
    }

    dec = avcodec_alloc_context3(codec);
    if (!dec || avcodec_parameters_to_context(dec, par) < 0 ||
        avcodec_open2(dec, codec, nullptr) < 0) {
        cleanup();
        return fail("cannot open decoder");
    }

    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !packet) {
        cleanup();
        return fail("out of memory");
    }

    Accumulator acc;
    std::vector<int32_t> pending;
    int64_t used_bits_mask = 0;
    int64_t sample_limit = max_seconds > 0
        ? (int64_t)(max_seconds * res.sample_rate)
        : INT64_MAX;
    int64_t samples_seen = 0;
    bool done = false;

    while (!done && av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == stream_index) {
            if (avcodec_send_packet(dec, packet) >= 0) {
                while (avcodec_receive_frame(dec, frame) >= 0) {
                    int nb = frame->nb_samples;
                    if (nb > 0) {
                        AVSampleFormat fmt_s = (AVSampleFormat)frame->format;
                        int planar = av_sample_fmt_is_planar(fmt_s);
                        int ch = res.channels;
                        size_t base = pending.size();
                        pending.resize(base + (size_t)nb * ch);
                        for (int s = 0; s < nb; s++) {
                            for (int c = 0; c < ch; c++) {
                                const uint8_t *data = planar
                                    ? frame->data[c]
                                    : frame->data[0];
                                int offset = planar ? s : s * ch + c;
                                int32_t v = to_s32(data, offset, fmt_s);
                                pending[base + (size_t)s * ch + c] = v;
                                // abs() then OR, matching the Python's mask.
                                int64_t a = v < 0 ? -(int64_t)v : (int64_t)v;
                                used_bits_mask |= a;
                            }
                        }
                        samples_seen += nb;
                        acc.consume(pending, ch);
                        if (samples_seen >= sample_limit) {
                            done = true;
                            break;
                        }
                    }
                    av_frame_unref(frame);
                }
            }
        }
        av_packet_unref(packet);
    }

    if (acc.used < 4) {
        cleanup();
        return fail("not enough non-silent audio to analyse");
    }
    res.frames = acc.used;

    const int bins = GATE_FFT_SIZE / 2 + 1;
    std::vector<double> freqs(bins), mid_db(bins);
    for (int i = 0; i < bins; i++) {
        freqs[i] = (double)i * res.sample_rate / (double)GATE_FFT_SIZE;
        double v = acc.mid_sum[i] / (acc.used * acc.win_power);
        mid_db[i] = 10.0 * std::log10(std::max(v, 1e-30));
    }
    double peak = *std::max_element(mid_db.begin(), mid_db.end());
    for (int i = 0; i < bins; i++) {
        mid_db[i] -= peak;
    }

    std::vector<double> smooth = gate_smooth(mid_db, 9);

    res.effective_bits = gate_effective_bits(used_bits_mask);
    gate_measure(res, freqs, smooth, mid_db);
    if (acc.have_side) {
        bool any = false;
        for (double v : acc.side_sum) {
            if (v != 0.0) { any = true; break; }
        }
        if (any) {
            res.side_ratio_db = gate_side_ratio(freqs, acc.mid_sum, acc.side_sum, res.cutoff_hz);
        }
    }
    res.bands = gate_band_table(freqs, smooth);
    gate_verdict(res);

    cleanup();
    return res;
}
