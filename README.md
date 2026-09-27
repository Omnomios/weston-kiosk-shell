# Weston kiosk shell fork

A standalone fork of Weston's kiosk-shell plugin, based on Weston 15.0.1.
The goal is to support `wlr-layer-shell` so Koya can provide shell elements.
The shell behavior is initially unchanged; layer-shell support is not yet
implemented. This project builds
`weston-kiosk-shell.so`, using a distinct name so it can coexist with the
standard kiosk shell. See [UPSTREAM.md](UPSTREAM.md) for source provenance
and [COPYING](COPYING) for the MIT license.

## Build

Requires Weston **15.x** (at least 15.0.1), its development headers, a C
compiler, Meson, Ninja, pkg-config, and development packages for Wayland
server, Pixman, and xkbcommon. Python 3 runs the optional headless smoke test.

On Fedora, install dependencies with:

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config weston weston-devel \
  wayland-devel pixman-devel libxkbcommon-devel python3
```

Then build:

```sh
./scripts/build.sh
```

This workspace already has matching Weston headers extracted under
`.deps/weston` from `weston-devel-15.0.1-2.fc44.x86_64.rpm`. The build script
automatically uses them and keeps the compiler cache inside the workspace.
These machine-local dependencies and build outputs are ignored by Git.
Other checkouts should install the development packages above.

For a standard system installation, the equivalent Meson commands are:

```sh
meson setup build
meson compile -C build
```

If Weston stores its frontend library outside the default
`libweston` library directory plus `/weston`, pass
`-Dweston_module_dir=/path/to/weston/modules` to `meson setup`.

## Verify and run

```sh
meson test -C build --print-errorlogs
```

The smoke test launches the built shell in an isolated headless Weston with
a Pixman renderer and fake seat, checks module loading, and verifies clean
shutdown. It requires permission to create a local Unix socket. It does not
test application rendering or window management interactions.

To test in a window inside your GNOME session:

```sh
./scripts/test-nested.sh
```

The script rebuilds the fork and launches a 1280×720 nested compositor with
an empty background. It uses the Wayland backend in GNOME Wayland sessions
and the X11 backend in X11 sessions. Close the Weston window or press Ctrl+C
to exit. Each launch uses a separate socket and ignores your normal Weston
configuration. If you supply an application, Weston exits when it exits.

Optionally pass a Wayland application and its arguments, or change the size:

```sh
./scripts/test-nested.sh your-app --your-app-option
WESTON_WIDTH=1920 WESTON_HEIGHT=1080 ./scripts/test-nested.sh
```

For Vulkan applications such as Koya, use `WESTON_RENDERER=gl` to enable
Weston's GPU renderer and the buffer formats needed by those clients.

For direct screenshots of the nested output, launch with
`WESTON_DEBUG=1 ./scripts/test-koya.sh`. In another terminal, set
`WAYLAND_DISPLAY` to the nested `weston-kiosk-*` socket and run
`weston-screenshooter`. Set `XDG_PICTURES_DIR` to choose the PNG directory.
Debug mode permits capture by clients connected to that test compositor.

To install the plugin into Weston's module directory:

```sh
meson install -C build
```

Installation may require elevated privileges. Local development and testing
can use the absolute build path directly. Shell plugins must be built for
the same Weston major version as the runtime; Weston 16 is not supported by
this initial fork.

## Start modifying the shell

The shell policy and surface/output handling live in
`kiosk-shell/kiosk-shell.c`. Pointer and touch move grabs live in
`kiosk-shell/kiosk-shell-grab.c`. Rebuild with `./scripts/build.sh` and rerun
the smoke test after changes. The `upstream` Git remote points to the Weston
project for reference; this extracted plugin fork does not import the full
compositor or its Git history.

## Koya shell test application

```sh
./scripts/test-koya.sh
```

This starts the fork in nested Weston with the GL renderer and launches
the application in [tests/koya-shell](tests/koya-shell). Koya uses the nested
display socket. The script defaults to the binary and assets in
`$HOME/projects/helix`; override them with `KOYA_BIN` and `KOYA_ASSETS`.

To target a compositor you already started:

```sh
./scripts/test-koya.sh --connect weston-kiosk-12345
```

The XDG diagnostic window remains available if layer-shell surface creation
fails. Each layer-shell role is attempted independently and failures are
reported on screen and in the log. There is no XDG substitution for layer
surfaces. Current Weston is expected to fail those roles because it does not
advertise `zwlr_layer_shell_v1` yet.

See [the test checklist](tests/koya-shell/README.md) for expected geometry,
stacking, reservation and input behavior. Successful creation alone is not
a protocol conformance result.
