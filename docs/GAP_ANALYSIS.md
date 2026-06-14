# FlamKit Gap Analysis — Agent Handoff Document

**Generated:** 2026-06-11 | **Branch:** main | **Author:** CTO (Paperclip FLA-75)

This document is written for an AI coding agent inheriting this codebase. It describes what is built,
what is broken, and what needs to be done — with precise file paths, class names, and enough
architectural context to act without extensive exploration.

---

## Project Overview

FlamKit is an open-source, professional-grade drum sampler built in C++ with JUCE. It targets
VST3/AU/AAX/Standalone formats and aims to rival commercial tools (Superior Drummer, BFD3) while
remaining free. Full mission and design philosophy: `CLAUDE.md`, `VISION.md`.

**Build system:** CMake + JUCE 8.0.4 (FetchContent). See `CMakeLists.txt` and `BUILD.md`.

---

## Architecture Summary

### Signal Path (audio thread, no allocation permitted)

```
MIDI → FlamAudioProcessor::processBlock()
     → FlamEngine::processBlock()
        → VoiceManager::triggerNote() / releaseNote()
        → VoiceManager::renderNextBlock() → internalBuffer (N-channel AudioBuffer<float>)
           └─ per active SampleVoice:
              ADSR envelope × hybrid playback (preload buffer OR SampleStreamingManager stream)
     → Mixer::process(internalBuffer, outputBuses)
        → per channel: mute/solo → vol/pan → TenBandGraphicEQ → SaturationProcessor → CompressorProcessor
        → master: vol → TenBandGraphicEQ → SaturationProcessor → CompressorProcessor → LimiterProcessor
        → output: Bus 0 (stereo Main Mix) + Buses 1–16 (mono per-channel DAW routing)
```

### Key Classes and Files

| Class | File | Role |
|-------|------|------|
| `FlamAudioProcessor` | `Source/Plugin/PluginProcessor.h/cpp` | JUCE AudioProcessor entry point; owns FlamEngine + Mixer; 17-bus static layout |
| `FlamEngine` | `Source/Core/FlamEngine.h/cpp` | AudioProcessorGraph subclass; drives voice rendering to N-channel buffer |
| `VoiceManager` | `Source/Core/VoiceManager.h/cpp` | 128-voice pool, round-robin, 16 choke groups, LRU voice stealing, kit-load thread |
| `SampleVoice` | `Source/Core/SampleVoice.h/cpp` | Single voice: ADSR + hybrid playback (100ms preload → disk streaming) |
| `SampleStreamingManager` | `Source/Core/SampleStreamingManager.h/cpp` | High-priority background thread; lock-free FIFO (requestFifo + dataFifo); 8192-sample chunks |
| `Mixer` | `Source/Core/Mixer.h/cpp` | 1–16 channel routing; per-channel FX chain; master FX chain; output bus routing |
| `FlamKitLoader` | `Source/Formats/FlamKitLoader.h/cpp` | Parses `flamkit.yaml` (primary) and `.json` (secondary/test only) into DrumKit data model |
| `TenBandGraphicEQ` | `Source/DSP/TenBandGraphicEQ.h/cpp` | 10-band ISO 1/3-oct EQ, ±12 dB, JUCE dsp::IIR, deferred coefficient updates |
| `SaturationProcessor` | `Source/DSP/SaturationProcessor.h/cpp` | Tape/Tube/Digital modes; wet/dry blend |
| `CompressorProcessor` | `Source/DSP/CompressorProcessor.h/cpp` | Full ADSR compressor, log-domain gain reduction |
| `LimiterProcessor` | `Source/DSP/LimiterProcessor.h/cpp` | Brick-wall master limiter |
| `IRConvolver` | `Source/DSP/IRConvolver.h/cpp` | Convolution reverb (juce::dsp::Convolution) — **built but never instantiated** |
| `SimpleEQ` | `Source/DSP/SimpleEQ.h` | Header-only; **orphaned — no callers, candidate for deletion** |
| `SimpleCompressor` | `Source/DSP/SimpleCompressor.h` | Header-only; **orphaned — no callers, candidate for deletion** |
| `PluginEditor` | `Source/Plugin/PluginEditor.h/cpp` | Main JUCE Component; owns MixerPanel; drum pad grid; kit browser |
| `MixerPanel` | `Source/UI/MixerPanel.h` | Container for ChannelStripComponent array + MasterChannelStripComponent |
| `ChannelStripComponent` | `Source/UI/ChannelStripComponent.h` | Per-channel fader, pan knob, mute, solo, FX button |

### Threading Model

| Thread | Owner | Communicates via |
|--------|-------|-----------------|
| Audio | DAW/JUCE host | Reads atomics; polls `SampleStreamingManager::dataFifo` |
| Streaming | `SampleStreamingManager` (juce::Thread, high priority) | Lock-free FIFO pair (requestFifo + dataFifo) |
| Kit-load | `juce::Thread::launch()` in PluginEditor | `VoiceManager::isKitLoading` atomic |
| UI/Message | JUCE message loop | Atomics for parameter reads; juce::Timer for meter updates |

**RT-safety rule:** The audio thread must never allocate, lock a mutex, block on I/O, or throw.
Pre-allocate everything in `prepareToPlay()`. Use atomics for UI↔audio communication.

### Plugin Bus Layout

Static 17-bus configuration (set in `FlamAudioProcessor` constructor, not dynamic):
- Bus 0: stereo Main Mix (always active)
- Buses 1–16: mono per-channel direct outputs

Rationale: Logic Pro and Cubase query bus layout at plugin load before a kit is selected.
Dynamic bus configuration caused host crashes. Unused buses emit silence — this is correct behavior.

### Kit Format

`flamkit.yaml` is the primary format. Example: `Resources/Kits/minimal-kit/flamkit.yaml`.
Data model: `DrumKit` → `DrumPiece[]` → `Articulation[]` → `SampleLayer[]`.
Each `SampleLayer` has `sampleFile`, `velocityMin`, `velocityMax`, `gain`, `roundRobinGroup`.

---

## What Is Complete and Working

The following are implemented, integrated into the active signal path, and CI-green on macOS Universal
(arm64+x86_64), Windows x64, and Linux x86_64:

- Multi-channel hybrid streaming (100ms preload + lock-free disk streaming via SampleStreamingManager)
- 128-voice management with round-robin cycling, choke groups, LRU voice stealing, ADSR envelopes
- Per-channel FX chain: TenBandGraphicEQ → SaturationProcessor → CompressorProcessor
- Master FX chain: TenBandGraphicEQ → SaturationProcessor → CompressorProcessor → LimiterProcessor
- Multi-output DAW routing (17-bus static layout)
- flamkit.yaml kit format with parser
- VST3, AU, AAX, Standalone plugin targets
- L1 unit tests (JUCE UnitTest), L2 golden render (Catch2), L3 pluginval host contract, L4 ASan+UBSan
- CI: `.github/workflows/ci.yml` — 3 platform matrix, all required to pass before merge to main

---

## Confirmed Defects (Observed in Production Testing)

### BUG-1: Voice Steal Clicks and Pops [CRITICAL — P0]

**Symptom:** Audible clicks and pops when samples are triggered rapidly (fast hi-hat rolls,
double-kick, etc.). Confirmed by running the standalone plugin.

**Root cause:** `VoiceManager` steals the oldest voice by killing it immediately when the 128-voice
pool is full, or when a new note triggers the same drum piece (depending on polyphony policy).
`SampleVoice` does not apply a fade-out ramp before releasing — it cuts to zero instantly, creating
a step discontinuity in the audio output. Any non-zero amplitude step generates a broadband click.

**Files to fix:**
- `Source/Core/SampleVoice.h/cpp` — add a fade-out ramp mechanism
- `Source/Core/VoiceManager.cpp` — when stealing a voice, trigger fade-out mode rather than
  immediate kill; only release the voice once the ramp reaches zero

**Implementation approach:**
1. Add `int fadeOutSamplesRemaining` and `float fadeOutGain` (or use existing ADSR release) to
   `SampleVoice`. When `startFadeOut(int durationSamples)` is called, set a flag and apply a linear
   gain ramp from current amplitude to zero over `durationSamples` samples in `renderNextBlock()`.
2. In `VoiceManager`, replace immediate `voice->stopNote()` / `voice->reset()` calls in the
   voice-stealing path with `voice->startFadeOut(targetSamples)`. Mark the voice as "fading" so it
   is not re-stolen mid-fade. Release it when `fadeOutSamplesRemaining == 0`.
3. Typical fade-out duration: 5–20ms at the current sample rate. Calculate from `sampleRate` in
   `prepareToPlay()`. Store as member, not a magic number.
4. After implementing, regenerate the L2 golden render:
   `FLAM_UPDATE_GOLDEN=1 ./build/tests/flam-tests "[golden_render]"`
5. Verify: `scripts/run-sanitized-tests.sh` must pass (no new allocations on audio thread).

**Acceptance criteria:** Fast hi-hat rolls at 180bpm produce zero audible clicks.

---

## UI Defects (Observed in Production Testing)

### UI-1: Drum Pad View Has No Visual Design [P1]

**File:** `Source/Plugin/PluginEditor.h/cpp` (drum pad grid section)

**Symptom:** The "Main" tab shows plain dark gray `juce::TextButton`-style rectangles with a
centered text label (drum name) and MIDI note number. There is no hit animation, no velocity
feedback, no visual identity. Users see a developer placeholder.

**Required:**
- Hit animation triggered on each note-on event. Approach: set an atomic flag in the audio callback
  when a pad is triggered; read it in a `juce::Timer` (30–60fps) on the UI thread to trigger a
  brief visual flash or glow. Reset the flag after reading.
- Velocity-scaled animation intensity: soft hit → subtle flash; hard hit → bright flash.
- Visual identity per drum type: distinct color, icon, or label style (kick ≠ snare ≠ cymbal).
- Font/spacing consistent with the Valhalla-series reference (see CLAUDE.md design philosophy).

### UI-2: Mixer Layout Is Broken at Low Channel Counts [P1]

**File:** `Source/UI/MixerPanel.h` (and wherever `resized()` is implemented)

**Symptom:** With 2 channels loaded (Minimal Kit), 2 narrow strips are positioned in the top-left
corner while ~80% of the mixer window is an empty black void. Level meters appear invisible.

**Root cause (likely):** Channel strips are positioned with fixed pixel offsets from the left edge.
When fewer than the maximum number of strips are rendered, empty space is not redistributed.

**Required:**
- Calculate strip width dynamically: `stripWidth = getWidth() / numActiveChannels` (with a
  comfortable maximum, e.g. 120px). Or center the strip group within the available width.
- Use `juce::FlexBox` or manual centering based on `getWidth()` and channel count.
- Ensure level meters are actually updating: verify the `juce::Timer` is running, that
  `Mixer::getLevelForChannel()` (or equivalent) returns non-zero values when audio is playing,
  and that the meter `Component::repaint()` is being called.
- Layout must look intentional at 1 channel and at 16 channels.

### UI-3: No Design Pass Has Been Done [P1]

**Symptom:** Every view (drum pads, mixer, FX editors, kit browser) is functional scaffolding with
no visual design. The plugin looks like an internal debug tool, not a product.

**Required:** A full visual design pass across all views:
- Consistent dark color palette with accent colors (reference: Valhalla DSP plugin series)
- Typography hierarchy (font, weight, size)
- Custom-styled controls: knobs (`Source/UI/RotaryKnob.h`), faders (`Source/UI/VerticalFader.h`),
  buttons, labels
- Consistent padding and spacing
- FX editor panels: `Source/UI/EQEditorComponent.h`, `CompressorEditorComponent.h`,
  `LimiterEditorComponent.h`, `SaturationEditorComponent.h`
- Kit browser window (in `PluginEditor`)
- Header / title bar area

---

## Technical Debt (Lower Priority)

### DEBT-1: Delete Orphaned DSP Classes

**Files to delete:**
- `Source/DSP/SimpleEQ.h` — header-only, no callers, predates `TenBandGraphicEQ`
- `Source/DSP/SimpleCompressor.h` — header-only, no callers, predates `CompressorProcessor`

Safe to delete — verify CI is green after.

### DEBT-2: Resolve JSON Kit Format

`FlamKitLoader` supports both `flamkit.yaml` and `.json`. JSON is used only in test fixtures with
no documentation. Either:
- (a) Promote JSON to first-class: document it, add test coverage, update README
- (b) Remove JSON: update `Source/Formats/FlamKitLoader.h/cpp` to remove the JSON parser path,
  replace `Tests/Fixtures/` JSON kit files with YAML equivalents

Recommendation: remove JSON for v1.0 simplicity. YAML is the open format; JSON adds no value.

### DEBT-3: `internalBuffer` Resize Guard

**File:** `Source/Core/FlamEngine.cpp`

`internalBuffer` (the N-channel internal mixing buffer) has a resize guard that can allocate on the
audio thread if the channel count changes mid-stream. With the static 17-bus layout this is rare
in practice, but technically violates the RT-safety invariant. Fix: pre-size `internalBuffer` to
the maximum expected channel count (16) in `prepareToPlay()` and never resize it on the audio thread.

### DEBT-4: `IRConvolver` Is Dead Code

**File:** `Source/DSP/IRConvolver.h/cpp`

A full convolution reverb using `juce::dsp::Convolution` is implemented and compiles, but is never
instantiated anywhere. Decision needed:
- Wire it into `Mixer` master FX chain (post-`LimiterProcessor`) for v1.0
- Or defer to v1.1 and clearly mark in code/docs

CTO recommendation: defer to v1.1. Fix P0/P1 issues first; ship a polished plugin without reverb
rather than a rough plugin with reverb. If deferring, add a `// TODO(v1.1): IRConvolver` comment
in `Mixer.h` and remove the dead files from the main build target to avoid confusion.

---

## What Is Missing for v1.0 Ship

| Item | Who | Status | Paperclip Issue |
|------|-----|--------|----------------|
| Voice steal fix (P0) | Coder | todo | FLA-76 |
| Drum pad UI redesign (P1) | Coder | todo | FLA-77 |
| Mixer layout + metering fix (P1) | Coder | todo | FLA-78 |
| Full UI/UX design pass (P1) | Coder | todo | FLA-79 |
| IRConvolver: wire or defer decision (P2) | CTO | todo | FLA-83 |
| 2–3 CC-licensed bridge kits (P2) | CMO | todo | FLA-81 |
| Distribution website (P2) | CMO | todo | FLA-82 |
| Orphaned DSP class cleanup (P3) | Coder | todo | FLA-80 |

---

## Testing

Run all tests (requires `BUILD_TESTING=ON`):
```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build
cd build && ctest --output-on-failure
```

Run L4 sanitizers (Linux/macOS only):
```bash
./scripts/run-sanitized-tests.sh
```

Regenerate L2 golden render (after any audio output change):
```bash
FLAM_UPDATE_GOLDEN=1 ./build/tests/flam-tests "[golden_render]"
```

Run pluginval (L3, after building):
```bash
./build/cmake/pluginval --strictness-level 8 --validate-in-process \
  ./build/FlamKit_artefacts/VST3/FlamKit.vst3
```

**CI gate:** All 3 platform legs (Linux, macOS Universal, Windows) must be green before merging to `main`.
CI config: `.github/workflows/ci.yml`.

---

## Feature Specs for Future Work

Detailed specs for post-v1.0 features are in `FeatureSpecs/`:
- `DistributedRepositories.md` — in-plugin kit browser (v1.1)
- `FlamForge.md` — companion recording tool for capturing new kits (v1.1)
- `EffectsChain.md` — IRConvolver integration details
- `HybridStreaming.md` — streaming architecture reference
- `FlamKitFormat.md` — full kit format specification
- `PerChannelMixer.md` — per-channel mixer design

---

## Quick Start for a New Agent

1. Read `CLAUDE.md` for project goals, design constraints, and coding standards.
2. Read `ARCHITECTURE.md` for the current module map and known debt (updated 2026-06-11).
3. Read `VISION.md` for the product mission and scope guardrails.
4. The audio signal path is the most important invariant: **no allocations, no locks, no blocking
   I/O on the audio thread.** Violating this causes real-time audio dropout under load.
5. The highest-priority task is `FLA-76` (voice steal clicks). Fix that before any UI work.
6. After any change to the audio output path, regenerate the golden render.
7. All changes must pass CI on all 3 platforms before merging.
