# flux

Minimal Wayland compositor prototype in C.

## Features

- `wlroots` compositor with xdg-shell client support.
- Input through `libinput` (evdev-backed on Linux).
- Solid desktop background color: `#008080`.
- You can use your own mouse cursor image:
  - Set `FLUX_CURSOR_IMAGE=0` to force the built-in drawn pointer.
  - Set `FLUX_CURSOR_IMAGE_PATH=/path/to/mouse.png` to override cursor PNG path.
  - Set `FLUX_CURSOR_DRAW_SCALE` (e.g. `0.75` or `0.5`) to shrink/grow cursor size.
  - Image cursor hotspot is auto-detected from `mouse/mouse.png` (env hotspot overrides apply only to drawn cursor mode).
- Decoration policy:
  - Flux does not draw compositor-side window creator frames.
  - Apps/toolkits provide their own window decorations (CSD/native behavior).
- Drag windows by app titlebar controls or window border.
- Border behavior in Flux: outer edge ring resizes, inner border ring moves.
- Resize windows from any corner or side edge.
- `Alt+M` restores one minimized window.
- `Mod+M` restores one minimized window.
- `Mod+Enter` launches an app (`FLUX_LAUNCH_CMD` or terminal fallback).
- `Mod+Esc` exits compositor.
  - `Mod` defaults to `Alt or Super(Command)` and is configurable with `FLUX_BIND_MOD`.

## Platform

- Tested so far on Debian.

## Dependencies (Debian)

Required to build:

```bash
sudo apt install -y build-essential pkg-config \
  libwlroots-0.18-dev libwayland-dev libwayland-bin wayland-protocols \
  libxkbcommon-dev libinput-dev libdrm-dev libpng-dev
```

Recommended runtime packages:

```bash
sudo apt install -y seatd foot xterm
```

## Build

```bash
make
```

Install system-wide (default prefix `/usr/local`):

```bash
sudo make install
```

Install to a custom prefix:

```bash
sudo make install PREFIX=/usr
```

Package/staging install (uses `DESTDIR`):

```bash
make install DESTDIR="$PWD/pkgroot" PREFIX=/usr
```

Remove installed files:

```bash
sudo make uninstall
```

## Source Layout

- `src/core/`: startup, config, logging, launch, theme glue.
- `src/compositor/`: input, output, and cursor/pointer handling.
- `src/wm/`: xdg-shell view/window management and taskbar logic.
- `tools/`: standalone utilities (`kprobe`).
- `flux.h`: shared types/prototypes used across modules.

## KMS Probe

`kprobe` is a standalone DRM/KMS probe tool (no `wlroots` runtime required).

```bash
kprobe
# or choose a card explicitly
kprobe /dev/dri/card0
```

It prints driver info, DRM caps, connectors, CRTCs, and plane types (including cursor planes).

## Run

Run from a TTY (not inside your current desktop session):

```bash
flux
```

Quit with `Mod+Esc` (or `Ctrl+C` in the launch terminal).

Launch command override:

```bash
FLUX_LAUNCH_CMD="foot" flux
```


## Logs

`flux` now persists logs so you can read them after quitting.

- Default log file: `~/.local/state/flux/flux.log`
- Override path: set `FLUX_LOG_FILE=/your/path/flux.log`
- Log level: `FLUX_LOG_LEVEL=debug|info|error|silent` (default: `info`)

Example:

```bash
tail -n 200 ~/.local/state/flux/flux.log
```

## Cursor Tuning

If your drawn cursor appears visually offset from click location, tune hotspot:

```bash
FLUX_CURSOR_HOTSPOT_X=0 FLUX_CURSOR_HOTSPOT_Y=0 ./build/flux
```

Increase `X` and `Y` to move the drawn cursor up/left relative to input focus.

On HiDPI outputs, Flux auto-scales cursor size down by output scale.
Use `FLUX_CURSOR_DRAW_SCALE` to override for both image and drawn cursor modes.

If no supported accelerated graphics driver is available, `flux` automatically
falls back to software rendering (`pixman`) and software cursors.
On Parallels VMs, this software path is forced by default for pointer stability.
When this fallback is active, launched apps also default to software GL
(`LIBGL_ALWAYS_SOFTWARE=1`, `MESA_LOADER_DRIVER_OVERRIDE=llvmpipe`) unless you
already set those variables.

## Notes

- This is a prototype compositor intended for learning and extension.
- Minimize/restore internals are still present, but with client-side decorations
  there is no Flux minimize titlebar button.
- Flux does not force toolkit decoration env hints; clients decide their own style.
- This was tested in Debian, and other distros are not ensured for working state.
