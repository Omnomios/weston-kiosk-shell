# weston-kiosk

A fork of Weston's kiosk-shell plugin with **wlr-layer-shell support**, intended
for building shell elements with Koya.

Koya can provide backgrounds, panels, and overlays while kiosk applications
fill the remaining screen space. A panel's positive exclusive zone reduces the
area available to applications; removing the panel restores that space.

This **`weston-14` branch** builds a standalone `weston-kiosk-shell.so` module
for Weston **14.x**, tested against **14.0.2**. The original fork is based on
Weston 15.0.1. It uses the installed Weston compositor and can coexist with
the standard kiosk-shell module. It does not contain the full Weston source tree.

The implementation is scoped to Koya's shell surfaces. It advertises layer-shell
version **4**; layer-shell XDG popups are outside the current scope. See
[behavior and limitations](#behavior-and-limitations) below.

## Requirements

- Weston **14.x** with matching development headers, including the Xwayland API header.
- A C compiler, Meson 0.63 or newer, Ninja, and pkg-config.
- Wayland server/client development libraries and `wayland-scanner`.
- Wayland protocols.
- Pixman and xkbcommon development libraries.
- Python 3 for the integration tests and screenshot helper.

The wlr-layer-shell protocol XML is included in this repository. Koya is only
needed for the interactive Koya fixture, not for building the module or running
the headless tests. Weston 15, 16, and other major versions are not supported by this branch;
Weston's plugin ABI changes between major releases.

On a Fedora release providing Weston 14, install the dependencies with:

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config weston weston-devel \
  wayland-devel wayland-protocols-devel pixman-devel libxkbcommon-devel python3
```

## Build

From the repository root:

```sh
./scripts/build.sh
```

The resulting module is `build-weston14/weston-kiosk-shell.so`. This separate
build directory preserves any existing Weston 15 build. The script configures
Meson on the first run and rebuilds on subsequent runs.

To configure the build directly:

```sh
meson setup build-weston14
meson compile -C build-weston14
```

If Meson cannot find `libexec_weston`, specify the directory containing Weston's
modules when configuring:

```sh
meson setup build-weston14 -Dweston_module_dir=/path/to/weston/modules
```

If your system runs another Weston version, install Weston 14 into a separate
prefix and use it for both building and launching:

```sh
export WESTON_PREFIX=/path/to/weston14
./scripts/build.sh
./scripts/test-nested.sh
```

The scripts add the prefix's `bin`, `lib`/`lib64`, and pkg-config directories to
the relevant environment paths. An installation under `.deps/weston14` is
detected automatically. Set `WESTON_BIN` to override the runtime executable.
For direct Meson configuration, supply matching `PKG_CONFIG_PATH` and
`-Dweston_binary=/path/to/weston14/bin/weston`. The shell must run under Weston
14; it cannot be loaded into the system's Weston 15 compositor.

## Run inside GNOME

Launch an isolated, nested Weston window from a terminal in your graphical
session:

```sh
./scripts/test-nested.sh
```

This rebuilds the module and opens a **1280×720 empty background**. The launcher
selects the Wayland backend in a Wayland session and the X11 backend in an X11
session. Each launch uses its own Wayland socket and ignores your normal Weston
configuration. Close the Weston window or press Ctrl+C to exit.

To launch a Wayland application inside it:

```sh
./scripts/test-nested.sh your-app --your-app-option
```

Weston exits when the supplied application exits. The application receives the
nested compositor's display socket.

| Environment variable | Default | Purpose |
| --- | --- | --- |
| `WESTON_WIDTH` | `1280` | Nested output width |
| `WESTON_HEIGHT` | `720` | Nested output height |
| `WESTON_RENDERER` | `pixman` | Use `gl` for Vulkan applications such as Koya |
| `WESTON_DEBUG` | Disabled | Set to `1` to permit test screenshot capture |

For example:

```sh
WESTON_WIDTH=1920 WESTON_HEIGHT=1080 ./scripts/test-nested.sh
```

## Try the Koya fixture

The included [Koya test application](tests/koya-shell/README.md) creates:

- An XDG diagnostic window that fills the kiosk application area.
- A background covering the output.
- A 52-pixel top panel that reserves space above the application.
- A bottom-right overlay with an animated marker and pointer input.

Use an existing Koya build and its base assets. Set their paths when launching:

```sh
KOYA_BIN=/path/to/helix/build/koya-base/koya \
KOYA_ASSETS=/path/to/helix/koya-base/assets \
./scripts/test-koya.sh
```

Koya's binary and base assets are not bundled. `KOYA_ASSETS` must point to the
base asset directory containing `fonts/test.ttf` and the runtime's other assets.
The launcher's defaults are `$HOME/projects/helix/build/koya-base/koya` and
`$HOME/projects/helix/koya-base/assets`; set the variables for your checkout.
The launcher selects the GL renderer by default.

To connect to a nested compositor already running, use its socket name and the
same Koya path variables:

```sh
./scripts/test-koya.sh --connect weston-kiosk-12345
```

All three layer surfaces should report `CREATED`. Failures remain visible in
the diagnostic window and logs; the fixture does not substitute XDG windows for
failed layer surfaces. Creation alone does not verify correct placement or
protocol conformance.

Press **P** to toggle the panel, **O** to toggle the overlay, **E** to toggle the
overlay's exclusive keyboard focus, **R** to log surface geometry, and **Esc**
to quit. See the [manual checklist](tests/koya-shell/README.md#manual-checks) for
expected geometry, stacking, and input behavior.

## Tests and screenshots

Run the automated tests after building:

```sh
meson test -C build-weston14 --print-errorlogs
```

These tests use isolated headless Weston instances and do not require Koya or a
graphical desktop. They check module loading, configure/acknowledge sequencing,
exclusive zones and margins, commit buffering, keyboard focus, unmapping and
remapping, invalid requests, resource destruction, and compositor shutdown with
mapped clients. Local Unix socket access is required. A test-only module creates
the input seat because Weston 14's headless backend has no `--fake-seat` option.
Weston 14 does not implement commit timing, so the timed deferred-commit test
from the Weston 15 version is replaced with a commit-state buffering check.

To capture the nested Koya fixture, set `KOYA_BIN` and `KOYA_ASSETS` as above and
run:

```sh
python3 scripts/capture-koya-test.py
```

The helper launches the fixture, checks that all three layer surfaces report
`CREATED`, captures the compositor output, and closes the temporary session.
PNGs are written to `build-weston14/screenshots/` and logs to
`build-weston14/koya-capture.log`.
It requires a graphical session, Weston **14's** `weston-screenshooter`, and
`stdbuf`. A private Weston prefix must include the screenshot client; a
Weston 15 screenshot client cannot capture from Weston 14.

For manual capture, launch with `WESTON_DEBUG=1`, then run
`./scripts/capture-output.sh` in another terminal with `WAYLAND_DISPLAY` set to the
nested compositor's `weston-kiosk-*` socket. Set `XDG_PICTURES_DIR` to choose the
destination. The capture comes directly from the nested compositor's output.

## Install

Local development uses the build module directly; installation is optional.
To install into the configured Weston module directory:

```sh
meson install -C build-weston14
```

Writing to a system module directory may require elevated privileges. After
installation, select the module with:

```sh
weston --shell=weston-kiosk-shell.so
```

Alternatively, select it in `weston.ini`:

```ini
[core]
shell=weston-kiosk-shell.so
```

## Behavior and limitations

| Layer | Position relative to kiosk applications |
| --- | --- |
| Background | Below applications |
| Bottom | Below applications, above backgrounds |
| Top | Above applications |
| Overlay | Above applications and top surfaces |

Layer state takes effect on surface commit. The implementation handles anchors,
margins, output selection, dynamic layer changes, buffer unmapping/remapping,
and close events when an output disappears.

Only mapped surfaces with valid exclusive-edge anchors reserve space. Positive
zones shrink the usable area, a zero zone does not reserve space, and a `-1`
zone allows a surface to extend across the full output, ignoring reservations.
Reservations are processed from the highest layer down, in creation order
within a layer. The usable area has a one-pixel minimum to avoid invalid kiosk
configure sizes when reservations exhaust the output.

Kiosk fullscreen and maximized windows are sized, positioned, and clipped to
the usable area. Removing or unmapping a reserving surface restores space.

Keyboard modes `none`, `exclusive`, and version-4 `on_demand` are supported.
Exclusive top/overlay surfaces take priority over kiosk focus; on-demand
surfaces acquire focus through a click or touch. Surface input regions control
pointer and touch delivery.

The current scope has two explicit limitations:

- **Layer-shell XDG popups are unsupported.** Weston 14's libweston-desktop
  rejects the parentless popup required by `get_popup`; support would require
  desktop-library changes in addition to this plugin. Koya's background,
  panel, and overlay fixture does not use these popups.
- **Version-5 explicit exclusive-edge selection is not advertised.** The
  server advertises version 4 even though the vendored XML describes version 5.

The automated tests and Koya fixture cover the intended use cases; this project
does not claim complete layer-shell protocol conformance.

## Development

- [kiosk-shell/kiosk-shell.c](kiosk-shell/kiosk-shell.c): kiosk policy,
  application geometry, output handling, and focus integration.
- [kiosk-shell/layer-shell.c](kiosk-shell/layer-shell.c): layer-shell protocol,
  surface lifecycle, and arrangement.
- [kiosk-shell/kiosk-shell-grab.c](kiosk-shell/kiosk-shell-grab.c): pointer and
  touch move grabs inherited from upstream.
- [tests/layer-shell-client.c](tests/layer-shell-client.c): protocol integration
  test client.
- [tests/koya-shell](tests/koya-shell): interactive Koya fixture.

Rebuild with `./scripts/build.sh` and run the headless tests after changes.
Use the Koya checklist to verify visible layout and input behavior.

## License and upstream

The shell is licensed under the MIT license; see [COPYING](COPYING).
Imported files and the vendored protocol retain their original copyright and
license notices.

See [UPSTREAM.md](UPSTREAM.md) for the pinned Weston release, source archive
checksum, imported files, and protocol provenance.
