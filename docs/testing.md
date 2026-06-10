# FLAM Testing Guide

> **One-step path:** clone → build → `ctest` → green.  
> This document is the single authoritative reference for running FlamKit's test suite locally. See also the [testing strategy plan](/FLA/issues/FLA-50#document-plan) (§3, §7) for architectural rationale.

---

## Table of Contents

1. [The L1–L5 Test Pyramid](#the-l1l5-test-pyramid)
2. [Quick Start](#quick-start)
3. [Platform Setup](#platform-setup)
4. [Build & Run All Tests](#build--run-all-tests)
5. [Running Individual Test Layers](#running-individual-test-layers)
6. [Standalone App — Manual / L5](#standalone-app--manual--l5)
7. [Golden-File Regeneration](#golden-file-regeneration)
8. [Sanitizer Build (L4)](#sanitizer-build-l4)
9. [Pluginval (L3)](#pluginval-l3)
10. [CI Matrix](#ci-matrix)

---

## The L1–L5 Test Pyramid

FlamKit's test strategy is organized as a five-layer pyramid. Layers L1–L4 are fully automated and run in CI on every push. L5 is the manual release gate.

| Layer | What it proves | Tooling | Speed | Needs DAW? |
|-------|---------------|---------|-------|-----------|
| **L1 — Unit** | Pure-logic correctness: kit parser, voice allocation, DSP math | Catch2 v3 via `ctest` | milliseconds | No |
| **L2 — Golden render** | Rendered audio is bit-identical to the committed reference | `flam-render` CLI + null-test harness | sub-second | No |
| **L3 — Plugin host-contract** | State save/restore, bus layouts, no-alloc in `processBlock`, parameter sweeps | `pluginval` (headless, strictness 8) | seconds | No |
| **L4 — Real-time safety** | No alloc / lock / blocking I/O on audio thread | ASan + UBSan instrumented build | seconds | No |
| **L5 — Manual / interactive** | Final human listen-through, UI/UX, kit loading, MIDI input | **Standalone app** (this document's §6) | manual | Standalone (not a full DAW) |

All L1–L4 layers run automatically in the three-platform CI matrix. L5 is a release gate only — not required for PRs.

---

## Quick Start

For the impatient: the fastest path from a fresh clone to a green L1/L2 run.

### NixOS / Nix (recommended for Linux)

```bash
git clone https://github.com/therecluse26/flam-drum-machine.git
cd flam-drum-machine

# Enter the hermetic shell (provides all JUCE system deps)
nix-shell

# Configure + build + test in one shot (inside nix-shell)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -V
```

### Ubuntu / Debian

```bash
git clone https://github.com/therecluse26/flam-drum-machine.git
cd flam-drum-machine

sudo apt-get install -y \
  cmake ninja-build pkg-config \
  libasound2-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev \
  libfreetype6-dev libfontconfig1-dev \
  libgl1-mesa-dev

cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -V
```

### macOS

```bash
xcode-select --install        # Xcode Command Line Tools
brew install cmake

git clone https://github.com/therecluse26/flam-drum-machine.git
cd flam-drum-machine

cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -V
```

### Windows (MSVC)

```powershell
# Requires: Visual Studio 2019+ and CMake in PATH
git clone https://github.com/therecluse26/flam-drum-machine.git
cd flam-drum-machine

cmake -B build -S . -A x64 -DBUILD_TESTING=ON
cmake --build build --config Debug --parallel
ctest --test-dir build --config Debug --output-on-failure -V
```

> **Note on JUCE download:** The first configure step fetches JUCE 8.0.4 via `FetchContent` into the `JUCE/` directory. If `JUCE/` is already populated (e.g. after a first run, or when working in CI with the cache warm), subsequent configures are instant due to `UPDATE_DISCONNECTED TRUE`.

---

## Platform Setup

### Linux system dependencies

JUCE requires the following libraries at link time. Install them via your package manager before running `cmake`:

| Library | Ubuntu/Debian | Fedora/RHEL | Arch Linux |
|---------|--------------|-------------|-----------|
| ALSA | `libasound2-dev` | `alsa-lib-devel` | `alsa-lib` |
| X11 | `libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev` | `libX11-devel libXext-devel libXrandr-devel libXinerama-devel libXcursor-devel` | `libx11 libxext libxrandr libxinerama libxcursor` |
| FreeType + Fontconfig | `libfreetype6-dev libfontconfig1-dev` | `freetype-devel fontconfig-devel` | `freetype2 fontconfig` |
| OpenGL | `libgl1-mesa-dev` | `mesa-libGL-devel` | `mesa` |

**NixOS:** all dependencies are provided by `shell.nix` — just run `nix-shell`.

### yaml-cpp (optional, enables YAML kit format)

Without `yaml-cpp`, FlamKit loads kits in JSON format only (via JUCE's built-in parser). YAML support is enabled automatically when `yaml-cpp` is detected via `pkg-config`.

```bash
# Ubuntu/Debian
sudo apt-get install libyaml-cpp-dev

# macOS
brew install yaml-cpp

# NixOS — already in shell.nix
nix-shell
```

---

## Build & Run All Tests

### Configure

```bash
# Standard development build with all tests enabled
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
```

Key CMake options:

| Option | Default | Purpose |
|--------|---------|---------|
| `BUILD_TESTING` | `OFF` | Enables L1 and L2 test targets |
| `FLAM_PLUGINVAL` | `OFF` | Downloads pluginval and registers L3 test |
| `FLAM_SANITIZE` | `OFF` | Adds ASan+UBSan to test targets (Linux/macOS only) |
| `CMAKE_BUILD_TYPE` | *(none)* | Use `Debug` for tests, `Release` for performance |

### Build

```bash
# Build everything (plugin + tests)
cmake --build build --parallel

# Build only test targets (faster for iteration)
cmake --build build --target flam-tests FlamL1Tests --parallel
```

### Run Tests

```bash
# Run all registered CTest tests
ctest --test-dir build --output-on-failure -V

# Run only L1 + L2 (skip pluginval)
ctest --test-dir build \
  --exclude-regex "FLAM_L3_PluginvalHostContract" \
  --output-on-failure -V
```

---

### Make shortcuts

The root `Makefile` wraps every workflow below in a one-word target so you don't have to memorise the `cmake`/`ctest` invocations. On NixOS the recipes run inside `nix-shell` automatically; on other platforms append `RUN='sh -c'` (e.g. `make test RUN='sh -c'`).

| Target | Equivalent to | Layer |
|--------|---------------|-------|
| `make test` | configure + build + `ctest --exclude-regex FLAM_L3` | L1+L2 (everyday loop) |
| `make test-unit` | `ctest -R FlamL1UnitTests` | L1 |
| `make test-golden` | `flam-tests "[golden_render]"` | L2 |
| `make test-determinism` | `flam-tests "[determinism]"` | L2 |
| `make golden-update` | `FLAM_UPDATE_GOLDEN=1 flam-tests "[golden_render]"` | golden re-bless |
| `make test-pluginval` | reconfigure `-DFLAM_PLUGINVAL=ON` + `ctest -R FLAM_L3_…` | L3 |
| `make test-sanitize` | `./scripts/run-sanitized-tests.sh` | L4 |
| `make test-all` | `test` + `test-pluginval` + `test-sanitize` (serial) | L1–L4 |
| `make standalone` | build the `FLAM_Standalone` target | L5 |
| `make run` | build + launch the Standalone app | L5 |

Run `make help` for the full list. The raw commands in the sections below remain valid and are what each target expands to.

---

## Running Individual Test Layers

### L1 — Unit tests (FlamKitLoader, VoiceManager, DSP)

L1 tests use JUCE's built-in `UnitTest` framework with a headless runner. They cover the pure-logic layer: kit file parsing, voice allocation, DSP math invariants.

```bash
# Via CTest
ctest --test-dir build -R FlamL1UnitTests --output-on-failure -V

# Directly (verbose JUCE UnitTest output)
./build/Tests/FlamL1Tests_artefacts/FlamL1Tests
```

### L2 — Offline render / golden

L2 tests (via Catch2 in `tests/`) render a synthetic fixture kit with a fixed MIDI sequence and compare the output bit-for-bit against a committed golden reference. Round-robin and humanization use a fixed seed to guarantee determinism.

```bash
# Via CTest (runs all Catch2-registered tests including golden)
ctest --test-dir build --exclude-regex "FLAM_L3" --output-on-failure -V

# Run only the golden render tests
./build/tests/flam-tests_artefacts/flam-tests "[golden_render]"

# Run determinism tests
./build/tests/flam-tests_artefacts/flam-tests "[determinism]"

# List all available Catch2 test cases
./build/tests/flam-tests_artefacts/flam-tests --list-tests
```

---

## Standalone App — Manual / L5

The Standalone build is FlamKit's **L5 interactive test vehicle**. It uses the identical engine as the VST3/AU plugin but runs as a desktop app — no DAW required. Use it for:

- Loading and listening to kit files before making them available
- Manual verification of UI interactions, MIDI routing, multi-output routing
- Fast iteration during active development (no DAW launch required)

### Build Standalone

```bash
# Build only the Standalone target
cmake --build build --target FLAM_Standalone --parallel
```

Output locations:
- **Linux / macOS:** `build/FLAM_artefacts/<Config>/Standalone/FLAM`
- **Windows:** `build\FLAM_artefacts\<Config>\Standalone\FLAM.exe`
- **macOS (bundle):** `build/FLAM_artefacts/<Config>/Standalone/FLAM.app`

### Run Standalone

```bash
# Linux / macOS (binary)
./build/FLAM_artefacts/Debug/Standalone/FLAM

# Or via the root Makefile shortcut (uses existing build/ dir)
make run
```

> **Linux note:** The Standalone app requires a live display (`DISPLAY` or Wayland compositor). For CI or headless servers, use `Xvfb`:
> ```bash
> Xvfb :99 -screen 0 1024x768x24 &
> DISPLAY=:99 ./build/FLAM_artefacts/Debug/Standalone/FLAM
> ```

### Verified build — 2026-06-10

Confirmed building on **Linux x86_64 (NixOS)** with the following steps:

```bash
nix-shell --run "cmake -S . -B build_verify -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON"
nix-shell --run "cmake --build build_verify --target FLAM_Standalone -- -j4"
# Output: [100%] Built target FLAM_Standalone
# Binary: build_verify/FLAM_artefacts/Debug/Standalone/FLAM (96 MB debug, 12 MB release)
```

---

## Golden-File Regeneration

The golden reference file is committed at `Tests/Fixtures/goldens/golden_render.f32`. It must be regenerated when you make an **intentional** DSP change that alters the rendered output.

### When to regenerate

- You changed envelope math, filter coefficients, or mixing logic and the change is intentional
- You added a new DSP stage that shifts the output
- A golden test fails due to your change, and you've confirmed the new output is correct

### How to regenerate

```bash
# Build the test executable first
cmake --build build --target flam-tests --parallel

# Run with the update flag set
FLAM_UPDATE_GOLDEN=1 ./build/tests/flam-tests_artefacts/flam-tests "[golden_render]"
```

This overwrites `Tests/Fixtures/goldens/golden_render.f32` in-place.

### Review the diff before committing

```bash
# The golden file is raw 32-bit float PCM — listen to it with ffplay or similar:
ffplay -f f32le -ar 44100 -ac 2 Tests/Fixtures/goldens/golden_render.f32

# Then commit only if the audio is what you intended
git add Tests/Fixtures/goldens/golden_render.f32
git commit -m "test(golden): update reference after <describe your DSP change>"
```

> **Never regenerate the golden file to make a failing test pass** unless you have confirmed the new audio is correct. An unreviewed golden update defeats the purpose of the harness.

---

## Sanitizer Build (L4)

The L4 sanitizer leg catches real-time safety violations: heap-use-after-free, stack overflows, undefined behavior, and use-of-uninitialized memory.

### One-command run

```bash
./scripts/run-sanitized-tests.sh
```

This script:
1. Configures a separate `build_sanitized/` tree with `FLAM_SANITIZE=ON`
2. Builds only the test targets (not the plugin binary)
3. Runs L1/L2 under ASan + UBSan
4. Reports any violations with full stack traces

### Manual equivalent

```bash
cmake -B build_sanitized -S . \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DFLAM_SANITIZE=ON \
  -DFLAM_PLUGINVAL=OFF

cmake --build build_sanitized --target flam-tests FlamL1Tests --parallel

ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:abort_on_error=1 \
ctest --test-dir build_sanitized \
  --exclude-regex FLAM_L3_PluginvalHostContract \
  --output-on-failure -V
```

> **Platform:** ASan+UBSan works on Linux and macOS with GCC or Clang. Windows (MSVC) does not support these sanitizers — the CI Windows leg skips L4.

---

## Pluginval (L3)

Pluginval validates the built VST3 (and AU on macOS) against the JUCE plugin host contract at strictness level 8. It tests state save/restore, bus layout negotiation, no-allocation guarantees, and parameter sweep behaviour.

### Enable and run

```bash
cmake -B build -S . \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DFLAM_PLUGINVAL=ON

cmake --build build --parallel

# Run only the L3 pluginval test
ctest --test-dir build \
  -R "FLAM_L3_PluginvalHostContract" \
  --output-on-failure -V
```

Pluginval is downloaded at configure time via `cmake/FetchPluginval.cmake` and cached at `build/_pluginval_bin/`.

> **Linux note:** The pluginval test is wrapped in `scripts/pluginval-linux-launch.sh` which starts a temporary `Xvfb` display so the GUI open/close tests pass in headless environments.

---

## CI Matrix

Every push and pull-request to `main` runs the full three-platform matrix defined in `.github/workflows/ci.yml`. All three legs must be green before merging.

| Leg | Runner | Tests |
|-----|--------|-------|
| Linux x86_64 | `ubuntu-22.04` | L1+L2 (Catch2), L3 pluginval (VST3), L4 ASan+UBSan |
| macOS Universal | `macos-14` (arm64) | L1+L2, L3 pluginval (VST3 + AU) |
| Windows x64 | `windows-2022` (MSVC) | L1+L2, L3 pluginval (VST3) |

See `BUILD.md §CI / Branch Protection` for instructions on enabling GitHub branch protection with these required status checks.
