#!/bin/sh
# Capture the compositor selected by WAYLAND_DISPLAY with matching Weston tools.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$project_dir/scripts/weston14-env.sh"
exec weston-screenshooter "$@"
