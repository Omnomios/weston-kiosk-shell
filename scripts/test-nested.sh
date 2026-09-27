#!/bin/sh
# Launch the fork in a window inside the current graphical session.
set -eu

if [ "${1:-}" = '--help' ] || [ "${1:-}" = '-h' ]; then
    cat <<'EOF'
Usage: ./scripts/test-nested.sh [COMMAND [ARGUMENT...]]

Build and launch the kiosk shell in a nested Weston window.
Defaults to an empty background. Close the Weston window or press Ctrl+C
to exit. If a command is supplied, Weston exits when that application exits.

Environment:
  WESTON_WIDTH   Window width in pixels (default: 1280)
  WESTON_HEIGHT  Window height in pixels (default: 720)
  WESTON_RENDERER Renderer to use (default: pixman; use gl for Vulkan apps)
  WESTON_DEBUG   Set to 1 to allow weston-screenshooter captures for testing
EOF
    exit 0
fi

if [ -z "${XDG_RUNTIME_DIR:-}" ] || [ ! -d "$XDG_RUNTIME_DIR" ]; then
    echo 'Run this script from a terminal in your graphical session (XDG_RUNTIME_DIR is required).' >&2
    exit 1
fi

if [ -n "${WAYLAND_DISPLAY:-}" ]; then
    # Explicitly select the parent display; Weston gives the application its
    # own child display when launching it after --.
    backend=wayland
elif [ -n "${DISPLAY:-}" ]; then
    backend=x11
else
    echo 'No graphical display found. Run this script inside your GNOME session.' >&2
    exit 1
fi

if [ "$#" -gt 0 ] && ! command -v "$1" >/dev/null 2>&1; then
    echo "Application not found: $1" >&2
    exit 1
fi

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
"$project_dir/scripts/build.sh"

if [ "$#" -gt 0 ]; then
    set -- -- "$@"
fi
set -- "--backend=$backend" "--renderer=${WESTON_RENDERER:-pixman}" --no-config --idle-time=0 \
    "--width=${WESTON_WIDTH:-1280}" "--height=${WESTON_HEIGHT:-720}" \
    "--socket=weston-kiosk-$$" \
    "--shell=$project_dir/build/weston-kiosk-shell.so" "$@"
if [ "$backend" = wayland ]; then
    set -- "--display=$WAYLAND_DISPLAY" "$@"
fi
if [ "${WESTON_DEBUG:-0}" = 1 ]; then
    set -- --debug "$@"
fi
exec weston "$@"
