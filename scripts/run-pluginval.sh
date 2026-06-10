#!/usr/bin/env bash
# run-pluginval.sh — one-command L3 host-contract validation for the FLAM VST3.
#
# What this does:
#   1. Configures CMake with FLAM_PLUGINVAL=ON (downloads pluginval if needed)
#   2. Builds the FLAM VST3 target
#   3. Runs the FLAM_L3_PluginvalHostContract CTest at strictness 8
#
# Environment overrides:
#   BUILD_DIR       — CMake build directory  (default: <repo-root>/build)
#   STRICTNESS      — pluginval strictness   (default: 8)
#   JOBS            — parallel build jobs    (default: nproc)
#   CMAKE_EXTRA     — extra cmake flags      (e.g. -DYAML_CPP_FOUND=OFF)
#
# Usage:
#   ./scripts/run-pluginval.sh
#   BUILD_DIR=build_pval STRICTNESS=10 ./scripts/run-pluginval.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
STRICTNESS="${STRICTNESS:-8}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

echo "╔══════════════════════════════════════════════════╗"
echo "║  FLAM pluginval L3 host-contract validation       ║"
echo "╠══════════════════════════════════════════════════╣"
echo "║  Repo root  : $REPO_ROOT"
echo "║  Build dir  : $BUILD_DIR"
echo "║  Strictness : $STRICTNESS"
echo "║  Jobs       : $JOBS"
echo "╚══════════════════════════════════════════════════╝"
echo ""

# 1 — Configure (downloads pluginval if not already cached)
echo ">>> cmake configure..."
cmake -B "$BUILD_DIR" -S "$REPO_ROOT" \
    -DFLAM_PLUGINVAL=ON \
    -DBUILD_TESTING=ON \
    ${CMAKE_EXTRA:-}

# 2 — Build the VST3
echo ""
echo ">>> Building FLAM_VST3..."
cmake --build "$BUILD_DIR" --target FLAM_VST3 -j"$JOBS"

# 3 — Run the pluginval CTest
echo ""
echo ">>> Running pluginval (strictness $STRICTNESS)..."
ctest --test-dir "$BUILD_DIR" \
    -R FLAM_L3_PluginvalHostContract \
    --output-on-failure \
    -V

echo ""
echo "=== pluginval L3 PASSED ==="
