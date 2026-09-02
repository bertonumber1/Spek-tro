# Spek-tro in a container.
#
# It is a GUI application, so running it needs the host's X11 socket passed in
# (see docker/README.md). The image is built in two stages so the shipped layer
# carries the runtime libraries only, not the ~1 GB of build tooling.

FROM debian:bookworm-slim AS build

# Debian bookworm is the oldest release with both of Spek-tro's floors:
# wxWidgets >= 3.1.7 and ffmpeg >= 5. Ubuntu 22.04 has neither.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential autoconf automake autopoint libtool pkg-config gettext \
        libwxgtk3.2-dev libavformat-dev libavcodec-dev libavutil-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# NLS is off: the new strings are not in POTFILES.in yet, and the container has
# no use for translated menus it cannot display differently anyway.
RUN ./autogen.sh --disable-nls \
    && make -C src -j"$(nproc)" \
    && strip src/spek

FROM debian:bookworm-slim

LABEL org.opencontainers.image.title="Spek-tro" \
      org.opencontainers.image.description="Spectrum analyser that detects fake lossless files" \
      org.opencontainers.image.source="https://github.com/bertonumber1/Spek-tro" \
      org.opencontainers.image.licenses="GPL-3.0"

RUN apt-get update && apt-get install -y --no-install-recommends \
        libwxgtk3.2-1 libavformat59 libavcodec59 libavutil57 \
        libgtk-3-0 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/src/spek /usr/local/bin/spek-tro

# Runs as a normal user: the file actions move and delete audio, and doing that
# as root against a mounted library is a bad idea.
RUN useradd -m -u 1000 spek
USER spek
WORKDIR /music

ENTRYPOINT ["/usr/local/bin/spek-tro"]
