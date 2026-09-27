#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
. "$project_dir/scripts/weston14-env.sh"
export CCACHE_DIR="${CCACHE_DIR:-$PWD/.deps/ccache}"

case "$("$WESTON_BIN" --version)" in
    'weston 14.'*) ;;
    *) echo 'This branch requires a Weston 14 runtime. Set WESTON_PREFIX or WESTON_BIN.' >&2; exit 1 ;;
esac

if [ ! -f build-weston14/build.ninja ]; then
    meson setup build-weston14 -Dweston_binary="$WESTON_BIN"
fi
meson compile -C build-weston14
