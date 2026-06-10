# Contributing to FlamKit

Thank you for contributing to FlamKit — the free, open-source professional drum sampler.

---

## Getting Started

### 1. Fork and Clone

```bash
git clone https://github.com/<your-fork>/flam-drum-machine.git
cd flam-drum-machine
```

### 2. Read the Architecture

Before writing code, skim these docs:
- [`CLAUDE.md`](CLAUDE.md) — project overview, design goals, module map
- [`ARCHITECTURE.md`](ARCHITECTURE.md) — C++ architecture and module responsibilities
- [`VISION.md`](VISION.md) — long-term north star

---

## Testing — Clone to Green in One Shot

FlamKit uses a five-layer test pyramid (L1–L5). Everything in L1–L4 is automated and must pass before any PR can merge. Full details are in [`docs/testing.md`](docs/testing.md).

### Quick path (NixOS / Nix)

```bash
nix-shell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -V
```

### Quick path (Ubuntu / Debian)

```bash
sudo apt-get install -y \
  cmake ninja-build pkg-config \
  libasound2-dev \
  libx11-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
  libfreetype6-dev libfontconfig1-dev libgl1-mesa-dev

cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -V
```

### Quick path (macOS)

```bash
xcode-select --install
brew install cmake

cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure -V
```

### Quick path (Windows / MSVC)

```powershell
cmake -B build -S . -A x64 -DBUILD_TESTING=ON
cmake --build build --config Debug --parallel
ctest --test-dir build --config Debug --output-on-failure -V
```

### Run pluginval (L3 host-contract tests)

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DFLAM_PLUGINVAL=ON
cmake --build build --parallel
ctest --test-dir build -R "FLAM_L3_PluginvalHostContract" --output-on-failure -V
```

### Run sanitizer tests (L4, Linux/macOS only)

```bash
./scripts/run-sanitized-tests.sh
```

For full testing documentation — golden-file regeneration, the L1–L5 pyramid, sanitizer options — see [`docs/testing.md`](docs/testing.md).

---

## Verifying with the Standalone App (L5)

The Standalone app is the preferred manual verification tool. It runs the identical engine as the plugin but without a DAW:

```bash
cmake --build build --target FLAM_Standalone --parallel
./build/FLAM_artefacts/Debug/Standalone/FLAM
```

Use it to confirm kit loading, UI interactions, and audio output before submitting a PR. Full steps in [`docs/testing.md §Standalone`](docs/testing.md#standalone-app--manual--l5).

---

## Code Style

- C++20, JUCE conventions
- No dynamic allocation in the audio callback (real-time safety is non-negotiable)
- `std::unique_ptr` and RAII throughout — no raw `new`/`delete`
- Comments only for non-obvious *why*, never for *what*
- Prefer composition over inheritance in engine subsystems

---

## Submitting a PR

1. Branch from `main`: `git checkout -b feat/your-feature`
2. Make your change, run L1–L2 tests locally: `ctest --test-dir build --output-on-failure -V`
3. If you changed DSP output intentionally, regenerate the golden file (see [`docs/testing.md §Golden-File Regeneration`](docs/testing.md#golden-file-regeneration))
4. Push and open a PR — CI runs the full L1–L4 matrix automatically
5. All three CI legs (Linux, macOS, Windows) must be green before merge

---

## Reporting Issues

Open a GitHub issue with:
- Platform and OS version
- Steps to reproduce
- Expected vs. actual behavior
- Relevant log output or stack trace
