#!/bin/sh
# Sourced by launch/build scripts after they set project_dir.
# An optional private prefix lets Weston 14 coexist with the system compositor.
weston_prefix=${WESTON_PREFIX:-$project_dir/.deps/weston14}
if [ -d "$weston_prefix/lib/pkgconfig" ] || [ -d "$weston_prefix/lib64/pkgconfig" ]; then
    export PKG_CONFIG_PATH="$weston_prefix/lib/pkgconfig:$weston_prefix/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export LD_LIBRARY_PATH="$weston_prefix/lib:$weston_prefix/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export PATH="$weston_prefix/bin:$PATH"
fi
WESTON_BIN=${WESTON_BIN:-weston}
export WESTON_BIN
