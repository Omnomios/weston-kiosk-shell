#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

# Prefer normal system development packages. Use the local Fedora package
# extraction from initial setup if present.
if [ -d .deps/weston/usr/lib64/pkgconfig ]; then
    export PKG_CONFIG_PATH="$PWD/.deps/weston/usr/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi
export CCACHE_DIR="${CCACHE_DIR:-$PWD/.deps/ccache}"

if [ ! -f build/build.ninja ]; then
    if [ -f /usr/lib64/weston/libexec_weston.so ]; then
        meson setup build -Dweston_module_dir=/usr/lib64/weston
    else
        meson setup build
    fi
fi
meson compile -C build
