#!/usr/bin/env bash
# run-sanitized-tests.sh — one-command L4 sanitizer build and test run.
#
# Configures a separate build tree with ASan+UBSan enabled, builds the L1/L2
# test targets, then runs them under CTest.  Any heap-use-after-free, stack
# overflow, UB, or use-of-uninitialized-memory will abort the test with a
# diagnostic.
#
# Platform: Linux and macOS only (Clang or GCC).
#
# Environment overrides:
#   BUILD_DIR   — sanitizer build directory  (default: <repo-root>/build_sanitized)
#   JOBS        — parallel build jobs        (default: nproc)
#   ASAN_OPTIONS, UBSAN_OPTIONS — passed through to the test runner
#
# Usage:
#   ./scripts/run-sanitized-tests.sh
#   BUILD_DIR=/tmp/flam_asan ./scripts/run-sanitized-tests.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build_sanitized}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

OS="$(uname -s)"
if [[ "$OS" != "Linux" && "$OS" != "Darwin" ]]; then
    echo "ERROR: ASan/UBSan is only supported on Linux and macOS." >&2
    exit 1
fi

echo "╔══════════════════════════════════════════════════╗"
echo "║  FLAM L4 sanitizer tests (ASan + UBSan)           ║"
echo "╠══════════════════════════════════════════════════╣"
echo "║  Repo root : $REPO_ROOT"
echo "║  Build dir : $BUILD_DIR"
echo "║  Jobs      : $JOBS"
echo "╚══════════════════════════════════════════════════╝"
echo ""

# Configure with ASan+UBSan
echo ">>> cmake configure (Debug + sanitizers)..."
cmake -B "$BUILD_DIR" -S "$REPO_ROOT" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DFLAM_SANITIZE=ON \
    -DFLAM_PLUGINVAL=OFF

# Build only the test targets (not the VST3 — no sanitizer for the plugin binary)
echo ""
echo ">>> Building test targets..."
cmake --build "$BUILD_DIR" --target flam-tests FlamL1Tests -j"$JOBS"

# Run L1 and L2 tests, skip L3 pluginval (separate concern)
echo ""
echo ">>> Running L1/L2 under sanitizers..."
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:abort_on_error=1}"
UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:abort_on_error=1}"
export ASAN_OPTIONS UBSAN_OPTIONS

ctest --test-dir "$BUILD_DIR" \
    --exclude-regex FLAM_L3_PluginvalHostContract \
    --output-on-failure \
    -V \
    --timeout 120

echo ""
echo "=== L4 sanitizer tests PASSED ==="
