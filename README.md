# Spek-tro

**Spectrum analyser that tells you which of your "lossless" files are fakes.**

A fork of [Spek](https://github.com/alexkay/spek) with an automated
transcode detector built in. Spek draws you a spectrogram and leaves the
judgement to your eyes. Spek-tro still draws the spectrogram — but it also scans
a whole folder, measures every file, and tells you which ones were made from an
MP3.

<!-- TODO: screenshot of the split view — results list above, spectrogram below -->

## The problem

A lossy encoder throws away everything above a cutoff frequency and cannot put it
back. Re-encoding the result to FLAC or WAV rebuilds a lossless *container*
around permanently lossy audio: the extension, the bitrate and the file size all
look right, and only the spectrum still knows.

You can see this by eye in Spek — a razor-straight edge across the top of the
picture with dead black above it. But "open 200 files one at a time and squint"
does not scale to a library, and an impression is hard to hand to a seller.

## What Spek-tro adds

- **Scan files or whole folders**, recursively — a release, a label, a library.
- **A verdict per file** with a confidence score and the numbers behind it.
- **Click any row to see it.** The real Spek spectrogram, live, in the same
  window. The numbers are the argument; the picture is what you recognise.
- **Save spectrograms** as PNG or JPEG, one file or a whole release at once,
  named `verdict - artist - album - title` so they still mean something later.
- **Export a bundle** — `report.txt`, `results.json`, and a spectrogram of every
  flagged file, in one timestamped folder you can send to a seller.
- **Act on the results** — quarantine (reversible, with a restore log), or
  delete.

## How the verdict is reached

Decode to raw PCM, take a long-term average power spectrum by FFT (16384-point
Hann, 50% overlap, digital silence skipped), normalise so the loudest bin is
0 dB, then measure:

| Measure | What it means |
|---|---|
| `cutoff_hz` | highest frequency still within 45 dB of the 1–6 kHz reference |
| `wall_db` | how sharply energy falls across that cutoff — mastering rolls off gently, an encoder does not |
| `above_db` | mean level above the cutoff. A genuine 16-bit file still has dither up there (about −68 dB); a decoded MP3 has nothing (about −107 dB) |
| `side_ratio_db` | side-channel energy near the cutoff — joint-stereo encoders collapse the top of the stereo image to mono |

A `lossy` verdict needs **all three** of: a cutoff well below Nyquist, a sharp
wall at it, and a dead band above it. A mastering engineer can produce any one of
those on their own; only an encoder produces all three together.

Two other things get their own verdict:

- **`padded`** — declared 24-bit, but the low 8 bits are never used. Upscaled
  from a 16-bit master. No amount of FFT would show this; it is read from the
  raw integer samples.
- **`upsampled`** — a 96 kHz file with nothing above 20 kHz is not a
  high-resolution master.

### The constants are not tunable

The thresholds are measured round-trip controls, not preferences. In particular
the analysis FFT is fixed at 16384 and runs in double precision, deliberately
*not* reusing Spek's display FFT (whose size is a user setting). Changing the
transform size or dropping to float32 will silently shift every verdict while
still producing plausible-looking numbers.

`tests/` cross-validates the detector against the reference Python
implementation — every measurement, verdict, confidence and reason string — over
both synthetic spectra and real round-trip controls. If you change the analysis,
that suite is what tells you whether you broke it.

## Building

Needs wxWidgets ≥ 3.1.7, ffmpeg ≥ 5.x (libavformat/libavcodec/libavutil), and a
C++17 compiler. Debian bookworm and later have all of these; **Ubuntu 22.04 does
not** (wxWidgets 3.0.5, ffmpeg 4.4 — both below the floor).

```sh
sudo apt install build-essential autoconf automake autopoint libtool pkg-config \
                 gettext libwxgtk3.2-dev libavformat-dev libavcodec-dev libavutil-dev
./autogen.sh
make -j$(nproc)
```

The decoder has a compatibility shim for ffmpeg 4.x's pre-`ch_layout` API, so the
analysis code itself still compiles against older distributions.

### Windows

Cross-compiled from Linux with [MXE](https://mxe.cc/), as upstream Spek does.
Spek's own `dist/win/mxe.diff` no longer applies to current MXE; use
`dist/win/trim-ffmpeg.py`, which cuts ffmpeg to a decode-only build and drops the
18 codec libraries this app cannot reach.

## Credits

Spek is © Alexander Kojevnikov and contributors. The detection method is ported
from `spectral.py` in beatportdl-webui / label2lossless. Spek-tro is GPL-3.0, the
same licence as Spek.
