# FlamKit Architecture

> **Living document.** Every code change that affects module boundaries, threading, or the audio signal path must update the relevant section. File paths and class names here must match the actual `/Source` tree — phantom classes are not permitted.

---

## Status Taxonomy

Every subsystem in this document carries one of three tags:

| Tag | Meaning |
|-----|---------|
| ✅ **Implemented & integrated** | In the live signal/control path; changes here affect production audio |
| 🟡 **Built but not integrated** | Code compiles and links; not wired into the active signal path |
| 🔵 **Planned** | Specced in `FeatureSpecs/` or this doc; no production code yet |

**Why this matters:** The README roadmap marks several items `[x]` done that are partial or stubbed. Do not trust checkmarks without verifying against this taxonomy.

---

## 1. System Overview

FlamKit is a multi-format audio plugin (VST3 / AU / AAX / Standalone) and headless CLI renderer built on JUCE 8. The production signal path is:

```
DAW MIDI → FlamAudioProcessor::processBlock()
             └─ FlamEngine::processBlock()
                  ├─ MIDI events → VoiceManager::triggerNote()
                  └─ VoiceManager::renderNextBlock()   ← voices → internalBuffer (N-ch)
             └─ Mixer::process()                        ✅ per-channel routing + FX (FLA-72)
                  ├─ per-channel strip: vol / pan / solo / mute / EQ / saturation / compressor
                  └─ master section: vol / EQ / saturation / compressor / limiter
             └─ DAW output buses (stereo Main Mix + 16 mono individual buses)
```

**CLI renderer (`flam-render`) uses the same path** (FLA-72): `FlamEngine → internalBuffer → Mixer::process() → stereo Main Mix`. This means the offline render and the plugin produce identical audio given the same Mixer settings.

**Golden test alignment (FLA-72):** `GoldenRenderTest` was updated in FLA-72 to route through `Mixer` identically. The committed golden (`Tests/Fixtures/goldens/golden_render.f32`) was regenerated intentionally — the old golden captured pre-Mixer audio from before FLA-70 cleared the legacy stereo output. The new golden captures the actual production-path audio.

**Note on `AudioProcessorGraph` inheritance:** `FlamEngine` (`Source/Core/FlamEngine.h:20`) extends `juce::AudioProcessorGraph`, but the current `processBlock()` override **does not use the graph node system** — it drives `VoiceManager` directly. The graph inheritance is retained for future modular routing (e.g. per-channel FX nodes). Do not assume the JUCE graph is active.

---

## 2. Module Map

```
Source/
├── CLI/
│   └── FlamRender.cpp          ← Headless offline renderer entry point ✅
├── Core/
│   ├── FlamEngine.h/.cpp       ← Top-level audio engine (AudioProcessorGraph subclass) ✅
│   ├── VoiceManager.h/.cpp     ← Voice pool (128), round-robin, choke groups ✅
│   ├── SampleVoice.h/.cpp      ← Single playing voice (ADSR, interpolation, streaming) ✅
│   ├── SampleStreamingManager.h/.cpp  ← Hybrid preload+disk streaming thread ✅
│   └── Mixer.h/.cpp            ← Per-channel mixer with FX chain (16 buses) ✅
├── DSP/
│   ├── TenBandGraphicEQ.h/.cpp ← 10-band graphic EQ (ISO 1/3-octave) ✅ (via Mixer)
│   ├── SaturationProcessor.h/.cpp  ← Soft-clip saturation ✅ (via Mixer)
│   ├── CompressorProcessor.h/.cpp  ← Full-featured compressor ✅ (via Mixer)
│   ├── LimiterProcessor.h/.cpp ← Brick-wall limiter ✅ (via Mixer)
│   ├── SimpleEQ.h              ← Header-only EQ — orphaned, not included anywhere 🟡
│   ├── SimpleCompressor.h      ← Header-only compressor — orphaned, not included anywhere 🟡
│   └── IRConvolver.h/.cpp      ← Convolution reverb — deferred to v1.1 (FLA-83) 🔵
├── Formats/
│   └── FlamKitLoader.h/.cpp    ← flamkit.yaml (YAML) + .json parser ✅
├── Plugin/
│   ├── PluginProcessor.h/.cpp  ← JUCE plugin entry; owns FlamEngine + Mixer ✅
│   └── PluginEditor.h/.cpp     ← Plugin UI (drum pads, tabs, kit browser window) ✅
└── UI/
    ├── ChannelStripComponent.h     ← Per-channel fader/pan/mute/solo + FX button ✅
    ├── CompressorEditorComponent.h ← Compressor parameter panel ✅
    ├── EQEditorComponent.h         ← EQ band editor ✅
    ├── FXButtonComponent.h         ← FX toggle button ✅
    ├── LevelMeter.h                ← RMS level meter (Timer-driven) ✅
    ├── LimiterEditorComponent.h    ← Limiter parameter panel ✅
    ├── MasterChannelStripComponent.h ← Master fader/level/compressor strip ✅
    ├── MixerPanel.h                ← Mixer panel container ✅
    ├── PeakMeter.h                 ← Peak+clip indicator ✅
    ├── RotaryKnob.h                ← Custom knob component ✅
    ├── SaturationEditorComponent.h ← Saturation parameter panel ✅
    └── VerticalFader.h             ← Custom vertical fader ✅
```

**Classes that appear in the README but do not exist in `/Source`:**
`MultiChannelRouter`, `HybridStreamingLoader`, `KitBrowser`, `RoutingMatrix` — do not reference these in code or issues; use the real class names above.

---

## 3. Core Engine

### FlamEngine (`Source/Core/FlamEngine.h`, `FlamEngine.cpp`) ✅

Pure multichannel voice renderer (FLA-70). Owns:

| Member | Type | Role |
|--------|------|------|
| `voiceManager` | `unique_ptr<VoiceManager>` | Voice pool + kit state |
| `kitLoader` | `unique_ptr<FlamKitLoader>` | Loads `flamkit.yaml` / `.json` |
| `internalBuffer` | `AudioBuffer<float>` | Multi-channel render scratch (N channels) |
| `humanization` | `atomic<float>` | Velocity jitter amount (0–1) |
| `latencyCompensation` | `atomic<int>` | Latency offset in samples |
| `inputLevel` / `outputLevel` | `atomic<float>` | Level metering (read by UI) |

`processBlock()` sequence (audio thread):
1. Iterate `MidiBuffer` → `handleMidiEvent()` → `VoiceManager::triggerNote/releaseNote()`
2. Resize `internalBuffer` if block size increased (known allocation risk — see §4)
3. `internalBuffer.clear()` then `voiceManager->renderNextBlock(internalBuffer, ...)`
4. `buffer.clear()` — zero the legacy stereo output buffer; callers use `getMultiChannelBuffer()`

All post-render processing (EQ, compression, gain, mixing) lives in `Mixer::process()`, called by `PluginProcessor` and `flam-render` after this method returns.

### VoiceManager (`Source/Core/VoiceManager.h`, `VoiceManager.cpp`) ✅

Manages the voice pool and kit metadata.

- `MAX_VOICES = 128`, `MAX_CHOKE_GROUPS = 16`
- Active polyphony limit: `maxActiveVoices` atomic (default 64)
- Voice allocation: linear scan via `findFreeVoice()` + LRU stealing
- **Choke groups:** when a new note triggers a choke group, `handleChokeGroup()` stops all voices in that group except the new one
- **Round-robin:** per-MIDI-note `RecentSampleHistory` (ring buffer, 10 entries) prevents immediate repetition; `findBestLayer()` selects from available `SampleLayer`s weighted away from recent plays
- **Kit loading:** `loadKit(unique_ptr<DrumKit>)` — called from a background thread launched in `PluginEditor`; guarded by `isKitLoading` atomic. Audio thread checks `isKitLoading` before voice allocation
- **SpinLock:** `voiceLock` guards the `voices` vector at 8 call sites in voice management. Note: spin-locks are real-time safe (no OS yield) but add latency under contention — see RT-safety rules §4

### SampleVoice (`Source/Core/SampleVoice.h`, `SampleVoice.cpp`) ✅

Renders a single playing sample voice. Each `VoiceManager::Voice` owns one `SampleVoice`.

- ADSR envelope per `Articulation` (attack/hold/decay/sustain/release)
- Hybrid playback: plays `preloadBuffer` (first 100ms in RAM) then hands off to streamed data from `SampleStreamingManager`
- `active` atomic (`SampleVoice.h:116`) — set/cleared across threads; audio thread reads it without lock

### SampleStreamingManager (`Source/Core/SampleStreamingManager.h`, `SampleStreamingManager.cpp`) ✅

Background thread (`juce::Thread`, priority `high`) for disk streaming.

- `PRELOAD_MS = 100` — 100 ms of each sample preloaded into `SampleLayer::preloadBuffer`
- `STREAM_BUFFER_SIZE = 8192` samples per streaming chunk
- **Audio → streaming:** `requestFifo` (capacity 32) + `requestQueue` array — `AbstractFifo` for lock-free SPSC
- **Streaming → audio:** `dataFifo` (capacity 64) + `dataQueue` array — lock-free SPSC
- `cancelStream(voiceId)` sets `StreamRequest::cancelled` atomic; streaming thread skips cancelled requests
- `getNextStreamedData()` — polled from audio thread; returns `nullptr` if no data yet (non-blocking)

---

## 4. Real-Time Safety Rules (The Contract)

These rules apply to **every code path reachable from `processBlock()`**. Violations are correctness defects, not style issues. PRs that add RT violations must not be merged.

### Hard prohibitions on the audio thread

| Prohibited | Why | Allowed alternative |
|-----------|-----|---------------------|
| `new`, `delete`, `malloc`, `free` | Heap allocation can block indefinitely | Pre-allocate in `prepareToPlay()` |
| `std::mutex`, `std::unique_lock`, `std::lock_guard` | May block or make OS calls | `juce::SpinLock` (RT-safe spin) or lock-free atomics |
| `juce::CriticalSection` | OS mutex — may block | `juce::SpinLock` |
| File I/O, `fopen`, `juce::File::read*` | Kernel I/O, unbounded latency | Background thread + lock-free queue |
| `juce::Thread::sleep()`, `std::this_thread::sleep_for()` | Blocks the audio thread | Never; restructure as state machine |
| `throw` / exceptions | May allocate | Use return codes / `juce::Result` |
| Logging (`DBG()`, `std::cout`) — in release builds | May allocate / flush | Conditional on `JUCE_DEBUG` only |
| Dynamic `std::vector` resize or `push_back` (may reallocate) | Heap allocation | Pre-size vectors in `prepareToPlay()` |

### Required patterns

- **Pre-allocate in `prepareToPlay()`:** All buffers, filter state, FX chain state sized there. `processBlock()` must work with pre-allocated memory only.
- **Atomics for UI↔audio communication:** All parameters bridged via `std::atomic<T>` or `juce::Atomic<T>`. UI thread writes; audio thread reads with `memory_order_relaxed` for meter values, `memory_order_acquire/release` for control signals.
- **Lock-free queues for thread handoff:** Use `juce::AbstractFifo` + a fixed-size array (see `SampleStreamingManager`). Never pass `std::queue` or `std::deque` across thread boundaries without locking.
- **SpinLock scope must be minimal:** `VoiceManager`'s `voiceLock` is held only for the voice list mutation. DSP processing happens outside the lock.

### Current known violations

- `VoiceManager::waitForKitLoaded()` (`VoiceManager.h:60`) spins with `juce::Thread::sleep(10)` — this is **test/CLI use only**. Do not call from the audio thread.
- The `internalBuffer` resize guard in `FlamEngine::processBlock()` (line 68–69) can allocate if block size grows unexpectedly. This is a known gap; correct fix is to re-run `prepareToPlay()` when block size changes.

---

## 5. Threading Model

| Thread | Owner | Priority | Communicates via |
|--------|-------|----------|-----------------|
| Audio thread | DAW / JUCE | Realtime | Reads atomics; polls `SampleStreamingManager::dataFifo` |
| Streaming thread | `SampleStreamingManager` | High | `requestFifo` (in), `dataFifo` (out) — both lock-free |
| Kit-load thread | `juce::Thread::launch()` in `PluginEditor` | Normal | `isKitLoading` atomic; `loadKit()` on `VoiceManager` |
| UI / message thread | JUCE message loop | Normal | Writes atomics read by audio thread; reads meter atomics |

**State ownership rules:**

- `voices` vector in `VoiceManager` — audio thread primary; mutations guarded by `voiceLock` (SpinLock)
- `currentKit` in `VoiceManager` — written by kit-load thread under `isKitLoading` flag; audio thread must not access during load
- All DSP parameters (`std::atomic<float/bool>`) — written by UI thread, read by audio thread
- Peak/level meters (`std::atomic<float>` in Mixer channels, `LevelMeter.h:156`, `PeakMeter.h:174`) — written by audio thread, read by UI timer

**Lock-free FIFO protocol (`AbstractFifo`):**

```
// Writing (streaming thread):
int start1, size1, start2, size2;
fifo.prepareToWrite(1, start1, size1, start2, size2);
if (size1 > 0) queue[start1] = data;
fifo.finishedWrite(size1 + size2);

// Reading (audio thread):
int start1, size1, start2, size2;
fifo.prepareToRead(1, start1, size1, start2, size2);
if (size1 > 0) result = std::move(queue[start1]);
fifo.finishedRead(size1 + size2);
```

---

## 6. Audio/Mixer Pipeline

### Production signal path ✅

```
FlamEngine::processBlock()
  → internalBuffer (N-channel, kit-defined — pure voice rendering, no EQ/compression)
Mixer::process(internalBuffer, allOutputBuses, numSamples)
  ├─ per-channel strip: vol → pan → solo/mute → EQ → saturation → compressor → limiter
  │    MainMix channels: summed into stereo output (allOutputBuses[0..1])
  │    Bus channels: written to corresponding mono bus (allOutputBuses[2..N+1])
  └─ master section: EQ → saturation → compressor → limiter → masterVolume
  → DAW output: stereo Main Mix (channels 0–1) + up to 16 mono buses (channels 2–17)
```

`Mixer::process()` is called by both `PluginProcessor::processBlock()` (plugin) and `flam-render` (CLI) after `FlamEngine::processBlock()` fills `internalBuffer`. This ensures offline renders and plugin output are sample-identical given the same Mixer settings.

### Mixer ✅

`Mixer` (`Source/Core/Mixer.h`) is owned by `PluginProcessor` as `perChannelMixer`. It provides:

- `setNumChannels(int n, vector<String> names)` — configures N channel strips (1–16) from kit metadata
- Per-channel `OutputDestination` enum: `MainMix` or `Bus1`–`Bus16`
- Per-channel FX chain order: `TenBandGraphicEQ` → `SaturationProcessor` → `CompressorProcessor` → `LimiterProcessor`
- Per-channel atomics: volume, pan, solo, mute, peak level, clip detection
- Master bus: volume, peak atomics + full FX chain (`TenBandGraphicEQ` → `SaturationProcessor` → `CompressorProcessor` → `LimiterProcessor`)

APVTS master parameters (`masterVolume`, EQ bands, compressor settings) are mirrored into `Mixer` via `PluginProcessor::updateEngineParameters()` using the `setMaster*()` setters (FLA-69).

### MixerBus — deleted (FLA-73)

`MixerBus` (`Source/Core/MixerBus.h/.cpp`) was a 5-bus fixed routing layer predating `Mixer`. It was deleted in FLA-73. Master volume responsibility moved to `Mixer::setMasterVolume()`.

### FX chain per channel (Mixer) ✅

| Stage | Class | File |
|-------|-------|------|
| 10-band EQ | `TenBandGraphicEQ` | `Source/DSP/TenBandGraphicEQ.h/.cpp` |
| Saturation | `SaturationProcessor` | `Source/DSP/SaturationProcessor.h/.cpp` |
| Compression | `CompressorProcessor` | `Source/DSP/CompressorProcessor.h/.cpp` |
| Limiting | `LimiterProcessor` | `Source/DSP/LimiterProcessor.h/.cpp` |

### Panning

Equal-power panning formula in `Mixer`. Panning state exposed as `std::atomic<float>` per channel strip.

### Metering

`LevelMeter` and `PeakMeter` UI components hold `std::atomic<float>` written by the audio thread. A `juce::Timer` (20 ms tick) reads atomics on the message thread and repaints. This is the correct pattern — UI never touches audio buffers.

---

## 7. Kit Format

### flamkit.yaml ✅

Primary kit definition format. Human-readable, version-controllable, community-editable.

```
MyKit/
├── flamkit.yaml
└── Samples/
    ├── Kick/
    │   ├── kick_v001_rr1.wav   ← multi-channel (1–16 channels per file)
    │   └── kick_v032_rr2.wav
    └── Snare/
```

### Data model (`Source/Formats/FlamKitLoader.h`)

```
DrumKit
├── name, author, version, description, tags
├── channelNames: vector<String>        ← mic channel names for DAW bus labels
├── GlobalSettings { masterGain, maxPolyphony, useRoundRobin, defaultHumanization }
└── pieces: vector<DrumPiece>
    ├── name, midiNote
    ├── micChannels: vector<MicChannel> { name, gain, pan, delay }
    └── articulations: vector<Articulation>
        ├── name, chokeGroup, ADSR params
        └── layers: vector<SampleLayer>
            ├── sampleFile (juce::File)
            ├── velocityMin, velocityMax, gain, roundRobinGroup
            ├── preloadBuffer (shared_ptr<AudioBuffer<float>>) ← first 100ms
            └── totalSampleLength                              ← for streaming handoff
```

### Parsing (`Source/Formats/FlamKitLoader.cpp`)

| Format | Status | Notes |
|--------|--------|-------|
| YAML (`.yaml`, `.yml`) | ✅ (conditional) | Requires yaml-cpp at configure time (`FLAM_YAML_SUPPORT=1`). Without it, `FlamKitLoader` reports an error. |
| JSON (`.json`) | 🟡 | Code exists (`parseJsonKit()`); JSON path compiles and links. Primarily used in test fixtures. |

**yaml-cpp is optional:** The CMake build finds it via `pkg-config`. Without it, YAML loading is disabled and a diagnostic message is printed. Build with `nix-shell` on NixOS, or install `yaml-cpp-dev` on Debian/Ubuntu, to enable YAML support.

---

## 8. DSP Conventions

### Processor design pattern

Each DSP processor follows this interface:

```cpp
void prepareToPlay(double sampleRate, int samplesPerBlock);  // allocate; set coefficients
void processBlock(juce::AudioBuffer<float>& buffer);         // in-place; no allocation
void reset();                                                 // clear state
```

Parameters are exposed as `std::atomic<T>` member variables. Processors call `updateIfNeeded()` at the start of `processBlock()` to apply pending parameter changes (coefficient recalculation stays off the audio thread hot path).

### Composition over inheritance

DSP processors do not inherit from each other. `Mixer` composes `TenBandGraphicEQ`, `SaturationProcessor`, `CompressorProcessor`, and `LimiterProcessor` as members per channel strip. `FlamEngine` composes `SimpleEQ` and `SimpleCompressor` inline.

### JUCE DSP usage

JUCE `juce::dsp::` components used: `IIR::Filter`, `Gain`, `Panner`, `Compressor`, `Limiter`, `Convolution`, `DelayLine`, `StateVariableTPTFilter`. All are prepared via `juce::dsp::ProcessSpec` in `prepareToPlay()`.

### Bypass pattern

```cpp
if (!enabled.load()) return;   // early return; no processing, no allocation
```

### Audio specifications (official kit standard)

| Property | Value | Rationale |
|----------|-------|-----------|
| Sample rate | 44.1 kHz or 48 kHz | Nyquist covers 20 kHz hearing limit; 96 kHz wastes disk/RAM with zero audible benefit |
| Bit depth | 24-bit minimum | Headroom for mixing headroom |
| Format | WAV (uncompressed) | Zero decode overhead; byte-level seeking for streaming; no FLAC decode spikes |
| Channels | 1–16 per file | Each WAV file = one hit, N mic channels |

---

## 9. Plugin & Bus Configuration

### BusesProperties (`Source/Plugin/PluginProcessor.cpp`)

Created by `FlamAudioProcessor::createBusLayout()` before base class initialization:
- 1 stereo output (`MainOutputChannels`)
- 16 mono output buses (`Bus1`–`Bus16`, initially disabled)

`CMakeLists.txt` declares `PLUGIN_OUTPUTS 2` for the JUCE plugin manifest. All 17 output buses are enabled statically at construction — see Static 17-bus layout below.

### Static 17-bus layout ✅

All 17 buses (1 stereo Main Mix + 16 mono individual channels) are declared **and enabled** at construction time in `createBusLayout()` (FLA-71). This is necessary because JUCE does not reliably support changing bus count after processor construction — hosts such as Logic (AU) and Cubase/Reaper (VST3) query bus layout during plugin load before any kit is selected, and enabling buses dynamically after that point caused observed crashes. Unused buses (kit has fewer than 16 channels) emit silence, which is safe and accepted by all tested hosts.

`configureBusesForChannelCount()` was deleted in FLA-71 as a dead code path.

### Parameters and automation (`AudioProcessorValueTreeState`)

`FlamAudioProcessor` registers these parameter groups via `createParameterLayout()`:

| Group | Parameters |
|-------|-----------|
| Engine | `humanization`, `masterVolume`, `polyphony`, `roundRobin` |
| Channel volumes | `closeVolume`, `overheadVolume`, `roomVolume`, `ambientVolume`, `bleedAmount` |
| Input | `inputGain` |
| 10-band EQ | `eqBypass`, `eq31Hz`…`eq16kHz` (10 float params) |
| Compressor | `compBypass`, `compAttack`, `compRelease`, `compHold`, `compThreshold`, `compRatio`, `compMakeupGain` |

All parameters are exposed for full DAW automation. State is persisted via `getStateInformation()` / `setStateInformation()` using `AudioProcessorValueTreeState::state` serialized as XML.

**`compLookahead` migration note (FLA-73):** The `comp_lookahead` APVTS parameter was removed because `SimpleCompressor::setLookahead()` was a no-op stub with no audible effect. `AudioProcessorValueTreeState` gracefully ignores unknown parameter keys when loading saved state, so pre-FLA-73 plugin presets load without crashing — the lookahead value is simply discarded.

---

## 10. Build System

### Requirements

| Component | Version |
|-----------|---------|
| CMake | 3.22+ |
| C++ standard | C++20 (`CMAKE_CXX_STANDARD 20`) |
| JUCE | 8.0.4 (via `FetchContent` from GitHub) |
| yaml-cpp | Optional (via `pkg-config`) |
| Xcode | Required for AU target (macOS) |
| MSVC / clang-cl | Required for VST3 on Windows |

### One-step build (hermetic)

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

JUCE is fetched automatically on first configure. No system JUCE install required.

### Build targets

| Target | Type | CMake name |
|--------|------|-----------|
| Plugin (VST3/AU/AAX/Standalone) | `juce_add_plugin` | `FLAM` |
| Headless renderer | `juce_add_console_app` | `flam-render` |
| Unit / integration tests | `add_subdirectory` | `Tests/` |

`FLAM_ENGINE_SOURCES` (defined in `CMakeLists.txt:154–166`) is a shared source list used by `FLAM`, `flam-render`, and test targets to avoid duplicate compilation.

### Platform-specific notes

| Platform | Notes |
|----------|-------|
| Linux | `JUCE_USE_PIPEWIRE=1`; requires X11 headers (`libx11-dev`, `libxrandr-dev`, `libxinerama-dev`, `libxext-dev`, `libxcursor-dev`) |
| macOS | Universal Binary (arm64 + x86_64) via `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` |
| Windows | x64 only; ASIO SDK optional |

### Testing options

| CMake option | Default | Purpose |
|-------------|---------|---------|
| `FLAM_PLUGINVAL` | OFF | Downloads pluginval (GPLv3, Tracktion) and runs L3 host-contract tests; strictness 8; process-isolated (not linked) |
| `FLAM_SANITIZE` | OFF | Enables ASan+UBSan on L1/L2 test targets (Linux/macOS); use `scripts/run-sanitized-tests.sh` |

### CI matrix

GitHub Actions green-gates all three platform targets on every PR. See `.github/workflows/` for the matrix definition ([FLA-58](https://flam.paperclip.ing/FLA/issues/FLA-58) — 3-platform CI).

---

## 11. Coding Standards

### Language

C++20. Requires standard `concepts`, `ranges`, `std::span`, structured bindings, `if constexpr`, `[[nodiscard]]`.

### Memory management

- Prefer `std::unique_ptr` for sole-ownership heap objects
- `juce::OwnedArray<T>` for JUCE-managed component collections
- `std::shared_ptr<AudioBuffer<float>>` for sample data shared between `SampleLayer` and streaming system
- No raw `new`/`delete` in new code

### Style

- RAII for all resource management; no manual cleanup in destructors if a smart pointer handles it
- `const`-correctness: const member functions, const references for read-only parameters
- No comments explaining *what* the code does — name identifiers clearly instead
- Comments reserved for non-obvious *why*: RT-safety constraints, subtle invariants, host-compatibility workarounds
- `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassName)` in every audio class

### License

GPLv3 (migrated from LGPL in [FLA-61](https://flam.paperclip.ing/FLA/issues/FLA-61)). Every source file must carry the SPDX header:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.
```

**Dependency license policy:** MIT, BSD, ISC, Apache-2.0, LGPL are acceptable. Proprietary or GPL-incompatible licenses are a hard merge block. JUCE 8 free tier is AGPL-3.0; for a local desktop plugin §13 network copyleft does not trigger, making GPLv3 for FlamKit compatible.

---

## 12. Known Architectural Debt

This section consolidates all 🟡 items. These are tracked, not hidden. Each item must be resolved before v1.0 ships.

### ~~Engine stereo → multi-channel integration gap~~ ✅ Resolved (FLA-72)

`VoiceManager::renderNextBlock()` writes to an N-channel `internalBuffer`. After FLA-70 the legacy stereo output was cleared; after FLA-72, `PluginProcessor::processBlock()` and `flam-render` both call `Mixer::process(engine.getMultiChannelBuffer(), ...)` to produce the final stereo output. The `Mixer` is the active signal path.

### ~~MixerBus orphan~~ ✅ Resolved (FLA-73)

`MixerBus.h/.cpp` was deleted. Master volume responsibility moved to `Mixer::setMasterVolume()` (wired via APVTS in FLA-69). No call sites remain.

### ~~SampleLoader orphan~~ ✅ Resolved (FLA-73)

`SampleLoader.h/.cpp` was deleted. It was never compiled into any target; its intended role is fully covered by `VoiceManager`'s inline preloading + `SampleStreamingManager`.

### ~~IRConvolver — no owner~~ ✅ Decision recorded (FLA-83)

**File:** `Source/DSP/IRConvolver.h/.cpp`

**Decision: Defer to v1.1.**

`IRConvolver` is a fully implemented convolution reverb using `juce::dsp::Convolution`. It is compiled (in `FLAM_ENGINE_SOURCES`) but intentionally not instantiated in `FlamEngine`, `PluginProcessor`, or `Mixer` for v1.0.

**Rationale:** Wiring convolution reverb into v1.0 requires bundling licensed IR files, implementing a file-chooser or IR browser UI, adding `flamkit.yaml` IR metadata support, and regenerating the L2 golden render. That scope conflicts with the v1.0 priority of shipping a polished, defect-free plugin. A plugin that works exceptionally well without reverb is preferable to one that ships reverb poorly.

**v1.1 integration plan (when scheduled):**
- Instantiate `IRConvolver` in `Mixer` master section, post-`LimiterProcessor`
- Add basic UI (wet/dry knob, IR file browser)
- Update `flamkit.yaml` spec to support per-kit IR file references
- Source CC0 or CC-BY licensed IR files for bundling
- Regenerate L2 golden render

### ~~SimpleCompressor lookahead stub~~ ✅ Resolved (FLA-73)

`comp_lookahead` APVTS parameter and `SimpleCompressor::setLookahead()` were removed. Saved states from before FLA-73 load without crashing — APVTS discards unknown parameter keys on state restore. See the migration note in §9.

### ~~Dynamic bus enablement — host negotiation~~ ✅ Resolved (FLA-71)

`configureBusesForChannelCount()` was deleted. The static 17-bus layout (1 stereo Main Mix + 16 mono buses, all enabled at construction) was adopted as the permanent strategy. Unused buses emit silence. See §9 for rationale.

### SimpleEQ and SimpleCompressor — orphaned headers

**Files:** `Source/DSP/SimpleEQ.h`, `Source/DSP/SimpleCompressor.h`

These header-only files were the FlamEngine-internal EQ and compressor before FLA-70/FLA-73 moved all post-render processing into `Mixer`. They are not listed in `FLAM_ENGINE_SOURCES` and are not `#include`d by any compiled file — invisible to the build system. They should be deleted once confirmed there is no plan to use them as lightweight single-strip processors elsewhere.

### JSON format — secondary path

**File:** `Source/Formats/FlamKitLoader.cpp:302`

`parseJsonKit()` exists and compiles. It is used by test fixtures. The YAML format is the primary and documented format. The JSON path should either be promoted to full parity or deprecated and removed to reduce maintenance surface.

---

*Last verified against `/Source` tree on 2026-06-11. Any class name added, removed, or renamed must be reflected here in the same PR.*
