#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
koya_bin=${KOYA_BIN:-$HOME/projects/helix/build/koya-base/koya}
koya_assets=${KOYA_ASSETS:-$HOME/projects/helix/koya-base/assets}
if [ "${1:-}" = '--help' ]; then
    cat <<'HELP'
Usage: ./scripts/test-koya.sh [--connect WAYLAND_SOCKET]

Default: start nested Weston with the fork and run the Koya layer-shell test.
--connect: use an existing compositor socket instead.
KOYA_BIN and KOYA_ASSETS override the Koya binary and base assets paths.
WESTON_WIDTH, WESTON_HEIGHT and WESTON_RENDERER configure nested Weston.
HELP
    exit 0
fi
if [ ! -x "$koya_bin" ] || [ ! -f "$koya_assets/fonts/test.ttf" ]; then
    echo 'Koya binary/base assets missing. Set KOYA_BIN and KOYA_ASSETS.' >&2
    exit 1
fi
if [ "$#" -gt 0 ]; then
    if [ "$#" -ne 2 ] || [ "$1" != '--connect' ]; then
        echo 'Usage: ./scripts/test-koya.sh [--connect WAYLAND_SOCKET]' >&2
        exit 1
    fi
    export WAYLAND_DISPLAY="$2"
    exec "$koya_bin" -m "$koya_assets" -m "$project_dir/tests/koya-shell" -i index.js
fi
export WESTON_RENDERER="${WESTON_RENDERER:-gl}"
exec "$project_dir/scripts/test-nested.sh" "$koya_bin" \
    -m "$koya_assets" -m "$project_dir/tests/koya-shell" -i index.js
