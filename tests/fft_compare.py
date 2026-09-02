"""Compare the self-contained display FFT against ffmpeg's av_rdft.

ffmpeg 8 removed libavcodec/avfft.h, so Spek's original FFT backend cannot be
built any more. This checks the replacement produces the same picture, on a
machine whose ffmpeg is old enough to still have avfft to compare against.
"""

import subprocess
import sys

old_bin, new_bin = sys.argv[1], sys.argv[2]
failures = 0

for nbits in (9, 10, 11, 12, 13):
    def run(binary):
        out = subprocess.run([binary, str(nbits)], capture_output=True, text=True,
                             check=True).stdout
        return [float(line.split()[1]) for line in out.splitlines() if line.strip()]

    old = run(old_bin)
    new = run(new_bin)

    if len(old) != len(new):
        print("FAIL nbits=%d: %d bins vs %d" % (nbits, len(old), len(new)))
        failures += 1
        continue

    # Compare in dB. These are float transforms, so the last bits differ; what
    # matters is that no bin moves enough to change the picture.
    worst = 0.0
    worst_bin = -1
    for i, (a, b) in enumerate(zip(old, new)):
        d = abs(a - b)
        if d > worst:
            worst, worst_bin = d, i

    ok = worst < 0.01
    print("%s nbits=%-3d bins=%-5d worst difference %.6f dB (bin %d)"
          % ("ok  " if ok else "FAIL", nbits, len(old), worst, worst_bin))
    if not ok:
        failures += 1
        for i, (a, b) in enumerate(zip(old, new)):
            if abs(a - b) > 0.01:
                print("     bin %d: avfft %.4f  new %.4f" % (i, a, b))
                if i > 8:
                    break

print()
print("FFT replacement differs from av_rdft" if failures else
      "FFT replacement matches av_rdft on every size")
sys.exit(1 if failures else 0)
