#!/usr/bin/env bash
# pluginval-linux-launch.sh — headless-safe pluginval wrapper for Linux.
#
# When running in a headless CI environment (no $DISPLAY / $WAYLAND_DISPLAY):
#   1. Use xvfb-run if available — this exercises GUI open/close paths.
#   2. Fall back to --skip-gui-tests if xvfb-run is absent.
#
# Usage: pluginval-linux-launch.sh <pluginval-exe> [pluginval-args...]

set -euo pipefail

PLUGINVAL_EXE="${1:?Usage: pluginval-linux-launch.sh <pluginval-exe> [args...]}"
shift

if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    exec "$PLUGINVAL_EXE" "$@"
elif command -v xvfb-run &>/dev/null; then
    echo "[pluginval-launch] No native display found; using xvfb-run for GUI tests"
    exec xvfb-run -a --server-args="-screen 0 1280x800x24" "$PLUGINVAL_EXE" "$@"
else
    echo "[pluginval-launch] No display and no xvfb-run; appending --skip-gui-tests"
    exec "$PLUGINVAL_EXE" --skip-gui-tests "$@"
fi
