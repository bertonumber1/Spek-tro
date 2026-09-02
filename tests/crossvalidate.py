"""Cross-validate the C++ gate port against the Python original.

Both are handed the *same* already-accumulated spectrum, so this isolates the
measurement and verdict logic from decoding — a difference here is a porting bug,
not an ffmpeg version difference. Every number the UI shows is compared, plus the
verdict, confidence, estimated source and the full band table.

Run on the Linux box, where spectral.py and numpy live:
    python3 crossvalidate.py ./test-gate
"""

import os
import subprocess
import sys

import numpy as np

# The reference implementation lives outside this repository — it is the Python
# the detector was ported from, not a dependency of the app. Point this at the
# folder holding spectral.py to run the comparison.
_ref = os.environ.get("SPECTRAL_PY_DIR", "")
if not _ref or not os.path.isfile(os.path.join(_ref, "spectral.py")):
    sys.exit(
        "Set SPECTRAL_PY_DIR to the directory containing the reference "
        "spectral.py.\nThis test compares the C++ port against it; CI instead "
        "uses tests/gate-cli.cc against round-trip controls."
    )
sys.path.insert(0, _ref)
import spectral  # noqa: E402

FFT = spectral.FFT_SIZE
NBINS = FFT // 2 + 1


def make_spectrum(sr, cutoff_hz, above_db, wall=True, tilt=-25.0, seed=0):
    """A peak-normalised dB spectrum shaped like a real long-term average.

    `tilt` is the gentle fall real music has from bass to treble; `wall` switches
    between an encoder's brick wall and a master's gradual roll-off.
    """
    rng = np.random.default_rng(seed)
    freqs = np.fft.rfftfreq(FFT, 1.0 / sr)
    db = tilt * (freqs / max(freqs[-1], 1.0))
    db += rng.normal(0.0, 0.6, size=freqs.shape)
    if wall:
        db = np.where(freqs > cutoff_hz, above_db + rng.normal(0.0, 1.0, size=freqs.shape), db)
    else:
        over = np.maximum(freqs - cutoff_hz, 0.0)
        db = db - (over / 1000.0) * 6.0
    db -= db.max()
    return freqs, db


def python_side(sr, declared_bits, effective_bits, mid_db, mid_pow, side_pow):
    res = spectral.Analysis()
    res.sample_rate = sr
    res.declared_bits = declared_bits
    res.effective_bits = effective_bits
    res.nyquist_hz = sr / 2.0
    freqs = np.fft.rfftfreq(FFT, 1.0 / sr)
    smooth = spectral._smooth(mid_db, 9)
    spectral._measure(res, freqs, smooth, mid_db)
    if np.any(side_pow):
        res.side_ratio_db = spectral._side_ratio(freqs, mid_pow, side_pow, res.cutoff_hz)
    res.bands = spectral._band_table(freqs, smooth)
    spectral._verdict(res)
    return res


def cpp_side(binary, sr, declared_bits, effective_bits, mid_db, mid_pow, side_pow):
    parts = [f"{sr} {declared_bits} {effective_bits} {len(mid_db)}"]
    for arr in (mid_db, mid_pow, side_pow):
        parts.append(" ".join(f"{v:.12g}" for v in arr))
    out = subprocess.run([binary], input="\n".join(parts), text=True,
                         capture_output=True, check=True).stdout
    got = {}
    bands = []
    reasons = []
    for line in out.splitlines():
        if line.startswith("band "):
            _, f, v = line.split()
            bands.append((float(f), float(v)))
        elif line.startswith("reason "):
            reasons.append(line[len("reason "):])
        elif "=" in line:
            k, v = line.split("=", 1)
            got[k] = v
    got["_bands"] = bands
    got["_reasons"] = reasons
    return got


CASES = [
    # name,               sr,     cutoff,  above,  wall,  declared, effective
    ("clean 16-bit",      44100,  21000.0, -68.0,  False, 16, 16),
    ("clean 24-bit",      44100,  21500.0, -96.0,  False, 24, 24),
    ("mp3 128",           44100,  16000.0, -107.0, True,  16, 16),
    ("mp3 192",           44100,  19000.0, -107.0, True,  16, 16),
    ("mp3 320",           44100,  20400.0, -107.0, True,  16, 16),
    ("mp3 V0",            44100,  21200.0, -107.0, True,  16, 16),
    ("aac 256",           48000,  20000.0, -105.0, True,  16, 16),
    ("padded 24/16",      44100,  21000.0, -68.0,  False, 24, 16),
    ("upsampled 96k",     96000,  20000.0, -100.0, True,  24, 24),
    ("upsampled 88.2k",   88200,  19000.0, -100.0, True,  24, 24),
    ("borderline wall",   44100,  20800.0, -88.0,  True,  16, 16),
    ("gentle rolloff",    44100,  19000.0, -70.0,  False, 16, 16),
]


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "./test-gate"
    failures = 0
    checked = 0

    cases = list(CASES)
    # Random spectra on top of the named cases, to hit branch combinations no
    # hand-written case thought of.
    rng = np.random.default_rng(1234)
    for i in range(40):
        sr = int(rng.choice([44100, 48000, 88200, 96000]))
        cases.append((
            f"random {i}", sr,
            float(rng.uniform(12000.0, sr / 2.0 - 200.0)),
            float(rng.uniform(-115.0, -55.0)),
            bool(rng.integers(0, 2)),
            int(rng.choice([16, 24])),
            int(rng.choice([16, 20, 24])),
        ))

    for idx, (name, sr, cutoff, above, wall, dbits, ebits) in enumerate(cases):
        freqs, mid_db = make_spectrum(sr, cutoff, above, wall=wall, seed=idx)
        mid_pow = 10.0 ** (mid_db / 10.0)
        # A joint-stereo collapse near the cutoff, present on half the cases.
        side_pow = mid_pow * (0.0001 if idx % 2 == 0 else 0.5)

        py = python_side(sr, dbits, ebits, mid_db, mid_pow, side_pow)
        cpp = cpp_side(binary, sr, dbits, ebits, mid_db, mid_pow, side_pow)

        problems = []
        for field in ("cutoff_hz", "wall_db", "above_db", "top_band_db", "side_ratio_db"):
            a = float(getattr(py, field))
            b = float(cpp[field])
            if abs(a - b) > 0.05:
                problems.append(f"{field}: py={a:.1f} cpp={b:.1f}")
        if py.verdict != cpp["verdict"]:
            problems.append(f"verdict: py={py.verdict} cpp={cpp['verdict']}")
        if int(py.confidence) != int(cpp["confidence"]):
            problems.append(f"confidence: py={py.confidence} cpp={cpp['confidence']}")
        if py.estimated_source != cpp["estimated_source"]:
            problems.append(f"source: py={py.estimated_source!r} cpp={cpp['estimated_source']!r}")

        py_bands = [(round(f), round(v, 1)) for f, v in py.bands]
        cpp_bands = [(round(f), round(v, 1)) for f, v in cpp["_bands"]]
        if len(py_bands) != len(cpp_bands):
            problems.append(f"bands: py={len(py_bands)} cpp={len(cpp_bands)}")
        else:
            for (pf, pv), (cf, cv) in zip(py_bands, cpp_bands):
                if pf != cf or abs(pv - cv) > 0.05:
                    problems.append(f"band {pf}: py={pv} cpp={cv}")
                    break

        if len(py.reasons) != len(cpp["_reasons"]):
            problems.append(f"reasons: py={len(py.reasons)} cpp={len(cpp['_reasons'])}")
        else:
            for pr, cr in zip(py.reasons, cpp["_reasons"]):
                if pr != cr:
                    problems.append(f"reason text:\n      py={pr!r}\n      cpp={cr!r}")
                    break

        checked += 1
        if problems:
            failures += 1
            print(f"FAIL  {name}")
            for p in problems:
                print(f"      {p}")
        else:
            print(f"ok    {name:18s} {py.verdict:12s} "
                  f"cut={py.cutoff_hz/1000:6.2f}k wall={py.wall_db:6.1f} above={py.above_db:7.1f}")

    print(f"\n{checked - failures}/{checked} cases match")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
