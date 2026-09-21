"""Trim MXE's ffmpeg to a decode-only build for Spek-tro.

Spek-tro never encodes, never filters and never muxes: it decodes audio and draws
its own spectrogram. MXE's stock ffmpeg pulls in x264, x265, libass, vpx, theora,
opus, vorbis and more, which is hours of cross-compiling for code this app cannot
reach. This is the modern equivalent of spek's dist/win/mxe.diff, which did the
same job for ffmpeg 5.1 and no longer applies to MXE's tree.
"""

import re
import shutil

PATH = "/work/mxe/src/ffmpeg.mk"
shutil.copyfile(PATH, PATH + ".orig")

with open(PATH) as fh:
    lines = fh.readlines()

DROP = {
    "libass", "libbluray", "libbs2b", "libcaca", "libmp3lame",
    "libopencore-amrnb", "libopencore-amrwb", "libopus", "libspeex",
    "libtheora", "libvidstab", "libvo-amrwbenc", "libvorbis", "libvpx",
    "libx264", "libx265", "libxvid", "gnutls", "avisynth", "sdl2",
}

# Not "postproc": ffmpeg 9.x dropped --disable-postproc entirely (the library
# itself is gone), so configure now rejects it outright — this failed the
# 2026-09-21 build with "Unknown option --disable-postproc" until removed.
EXTRA = [
    "programs", "avdevice", "swscale", "avfilter",
    "encoders", "muxers", "devices", "filters", "network", "iconv",
]

out = []
dropped = []
for line in lines:
    stripped = line.strip()

    # The dependency list: only the C compiler and the two compression libs the
    # containers actually need.
    if stripped.startswith("$(PKG)_DEPS"):
        out.append("$(PKG)_DEPS     := cc bzip2 zlib\n")
        continue

    # --enable-<lib> for a library we just removed from DEPS.
    m = re.match(r"^--enable-([\w-]+)\s*\\$", stripped)
    if m and m.group(1) in DROP:
        dropped.append(m.group(1))
        continue

    # libmpg123 is pulled in through --extra-libs via pkg-config; it goes too.
    if "--extra-libs=" in line and "mpg123" in line:
        out.append('        --extra-libs="-mconsole" \\\n')
        continue

    out.append(line)

    # Everything ffmpeg builds that this app can never call.
    if stripped == "--disable-doc \\":
        for opt in EXTRA:
            out.append("        --disable-%s \\\n" % opt)

text = "".join(out)
with open(PATH, "w") as fh:
    fh.write(text)

print("DEPS      :", re.search(r"\$\(PKG\)_DEPS.*", text).group(0))
print("dropped   :", ", ".join(dropped) or "(none)")
print("added     :", sum(("--disable-%s \\" % o) in text for o in EXTRA), "of", len(EXTRA))
print("leftover  :", sorted(set(re.findall(r"--enable-lib[\w-]+", text))) or "(none)")
print("mpg123    :", "mpg123" in text)
