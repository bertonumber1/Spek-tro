# Running Spek-tro in Docker

Spek-tro is a GUI application, so the container needs access to a display. That
makes it most useful on a Linux desktop, or on a headless box you reach over X
forwarding — it is not a way to run the app on a machine with no X server at all.

## Build

```sh
docker build -t spek-tro .
```

Or pull a published image:

```sh
docker pull ghcr.io/bertonumber1/spek-tro:latest
```

## Run on a Linux desktop

```sh
xhost +local:docker            # allow the container to reach your display
docker run --rm \
    -e DISPLAY="$DISPLAY" \
    -v /tmp/.X11-unix:/tmp/.X11-unix:ro \
    -v /path/to/your/music:/music \
    spek-tro
xhost -local:docker            # put it back afterwards
```

Your library appears at `/music` inside the container, which is where the app
starts. Mount it read-write if you intend to use Quarantine or Delete, and
read-only (`:ro`) if you only want to scan and report — a read-only mount is the
surest way to guarantee a scan cannot touch your files.

## Run over SSH X forwarding

From a machine with an X server, on the host running Docker:

```sh
ssh -X user@host
docker run --rm -e DISPLAY="$DISPLAY" \
    -v "$HOME/.Xauthority:/home/spek/.Xauthority:ro" \
    --net=host \
    -v /path/to/your/music:/music \
    spek-tro
```

## Notes

- The container runs as uid 1000, not root. If your library is owned by a
  different uid, add `--user "$(id -u):$(id -g)"` so quarantine and delete can
  write.
- The image carries the runtime libraries only; the build stage with the
  compiler and headers is discarded.
- Wayland-only desktops need `xwayland` running, which most provide by default.
