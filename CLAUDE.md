# FlamKit (Free Layered Audio Machine)

You are assisting in developing **FlamKit (Free Layered Audio Machine)** — a professional-grade, open-source drum engine and plugin framework built in **C++** with **JUCE**.

---

## 🎯 Project Overview

**FlamKit** is a cross-platform, low-latency drum engine and plugin designed to be **the best drum sampler that exists** — rivaling (and surpassing) commercial tools like Superior Drummer 3, BFD3, Steven Slate Drums, and Toontrack Metal Foundry — while remaining **completely free and open source**.

**Core Mission:** Democratize professional music production by eliminating the financial barrier to world-class drum sampling. We believe bedroom producers and professional studios alike deserve access to the same quality tools — without the $300-$1000+ price tags.

It will:
- Load multi-layered, velocity-sensitive drum samples defined in open `flamkit.yaml` files (YAML/JSON)
- Support **multi-channel samples** (1-16 channels per hit) with independent per-channel routing to DAW tracks
- Provide studio-grade realism via humanization, choke groups, round-robin cycling (unlimited per layer), and articulation switching
- Offer both standalone and plugin builds (VST3, AU, AAX, future CLAP/LV2) with **full DAW automation** for every parameter
- Feature a modular C++ core built around JUCE's `AudioProcessorGraph` for routing and extensibility
- Target sample-accurate playback with <5 ms latency at 64-sample buffers
- Ship with access to **professional-grade, free sample libraries** (24-bit/44.1-48kHz) via distributed repository system
- Achieve extreme memory efficiency: ~50-100MB RAM per kit vs. 1-2GB for competitors

---

## 🌍 Distributed Repository System

FlamKit uses a **decentralized kit distribution model** inspired by Linux package managers (APT/PPA):

- **Official Repository:** High-quality, curated kits (24-bit/44.1-48kHz, multi-channel, extensively sampled) hosted by the FlamKit project
- **Community Repositories:** Anyone can host their own kit repository and share the URL
- **User-Added Sources:** Users can add custom repository URLs (like Ubuntu PPAs) to access niche or experimental kits
- **No Central Bottleneck:** Your creativity isn't limited by our servers or approval process
- **Direct Downloads (v1.0):** All official kits available for direct download on the FlamKit website
- **In-Plugin Browser (v1.1+):** Browse, download, and auto-update kits without leaving your DAW

**Kit Licensing:** We recommend Creative Commons licenses (CC-BY, CC0) or GPL-compatible licenses for community kits to preserve the open ecosystem.

---

## 🧩 Core Architecture

**Modules (planned):**
- `/Source/Core` — Audio engine (voice management, articulation logic, hybrid streaming playback)
- `/Source/Formats` — `flamkit.yaml` parser and serializer (YAML/JSON)
- `/Source/DSP` — Envelopes, filters, convolution/IR processing
- `/Source/UI` — JUCE components for mixer, multi-output routing, and visual feedback
- `/Source/Plugin` — VST3/AU/AAX entry points, parameter bindings, automation hooks, multi-output bus configuration
- `/Resources` — Example kits, configuration templates

---

## ⚙️ Design Goals

1. **Best That Exists:** Official kits match or exceed the quality of $500+ commercial libraries (Superior Drummer, BFD3, Steven Slate)
2. **Zero Cost Barrier:** Professional-grade tools shouldn't require financial privilege — FlamKit is free, always
3. **Extreme Efficiency:** Hybrid streaming (100ms preload + disk streaming) keeps RAM usage at 50-100MB per kit vs. 1-2GB for competitors
4. **Ultra-Low Latency:** Maintain stable performance at 64-sample buffers (≈3 ms @ 48 kHz) with <5% CPU usage
5. **Authentic Realism:** Multi-layered samples, extensive round-robins, timing/velocity humanization, and natural articulation switching
6. **Multi-Output Routing:** Each microphone channel routes to independent DAW tracks — FlamKit becomes a virtual multitrack session
7. **Open Format:** `flamkit.yaml` files are human-readable, version-controllable, and community-editable
8. **Complete DAW Integration:** Every parameter exposed for automation — treat FlamKit like a hardware unit
9. **Extensibility:** Clean JUCE module design; GPLv3 prevents proprietary forks while allowing library reuse
10. **Cross-Platform:** Builds cleanly for Linux (PipeWire/ALSA), macOS (CoreAudio), Windows (ASIO/WASAPI)
11. **Distributed Ecosystem:** No single point of failure or control for kit distribution

---

## 🧠 Key Components

- `FlamEngine` — handles sample scheduling, round-robin logic, velocity mapping, and hybrid streaming coordination
- `VoiceManager` — manages voice allocation (128 voices), choke groups, and per-articulation envelopes
- `HybridStreamingLoader` — 100ms preload + smart caching (3-4 most-used samples) + disk streaming for tails
- `MultiChannelRouter` — routes each mic channel to independent DAW output buses (VST3/AU multi-output)
- `PerChannelMixer` — built-in per-channel mixing (volume, pan, solo, mute) for standalone mode or quick mixing
- `EffectsChain` — per-channel and master bus effects (EQ, compression, saturation, transient shaping)
- `IRConvolver` — optional convolution reverb for room realism
- `FlamKitLoader` — reads YAML kit metadata, detects channel count, and configures output buses dynamically
- `RepositoryBrowser` — (v1.1+) discovers and downloads kits from distributed repositories
- `FlamAudioProcessor` — JUCE entry point for plugin builds with full parameter automation and multi-output bus management

---

## 💡 Development Preferences

- C++20 or newer
- JUCE (latest release)
- GPLv3 license (prevents proprietary forks while allowing library reuse)
- Follow real-time safe audio thread principles (no dynamic allocation in callback)
- Use `AudioBuffer<float>` and `AudioSampleBuffer` abstractions for clean sample handling
- Prefer composition over inheritance for engine subsystems
- Optimize for memory efficiency and low CPU overhead

---

## 🎨 UI/UX Design Philosophy

**Inspiration: Valhalla Series (ValhallaVerb, etc.)**

- **Minimal and Clean:** Uncluttered interface with sane defaults
- **Deep but Discoverable:** Extensive features accessible without overwhelming the user
- **Visual Elegance:** Professional aesthetic that feels premium despite being free
- **Focused Workflow:** Quick access to common tasks (kit loading, mixing, routing) with advanced features tucked away

**Key UI Elements:**
- Kit browser with visual previews
- Per-channel mixer with dynamic fader strips (adapts to sample channel count)
- Multi-output routing matrix (visual representation of DAW track assignments)
- Velocity curve editor for humanization
- Waveform display with velocity layer visualization (v1.1+)

---

## 📦 Output Targets

FlamKit must build cleanly and function identically across all major plugin formats and hosts.

| Target | Purpose | Notes |
|---------|----------|-------|
| **Standalone App** | For live performance, testing, and demo playback | Built using JUCE's Standalone wrapper; identical engine as plugin. |
| **VST3** | Cross-platform industry standard | Supported natively by JUCE; multi-output routing fully supported. |
| **AU (Audio Unit)** | Required for Logic Pro, MainStage, and GarageBand (macOS only) | Fully supported by JUCE; requires Xcode toolchain and proper signing for Apple notarization. |
| **AAX (Avid Audio eXtension)** | Required for Pro Tools | Supported via JUCE's AAX wrapper; needs Avid Developer Program membership and AAX SDK. |
| **CLAP (v1.1+)** | Modern open standard for Bitwig, Reaper, etc. | Native multi-output support, excellent for open-source ecosystem. |
| **LV2 (Future)** | For Ardour, Qtractor, and Linux-focused DAWs | Post-1.0 milestone; community contributions encouraged. |

### Build Notes

- The JUCE CMake template will generate **all plugin formats** from one project definition:
  ```cmake
  juce_add_plugin(FLAM
      ...
      FORMATS VST3 AU AAX Standalone
  )
  ```
- Multi-output configuration is handled via `BusesProperties` in `AudioProcessor` constructor

---

## 🎙️ Multi-Output Routing (v1.0 Core Feature)

Each microphone channel can be routed to an independent DAW track, transforming FlamKit into a **virtual multitrack drum session**:

**Example Configuration:**
- **Kick Close** → DAW Track 1
- **Kick Sub** → DAW Track 2
- **Snare Top** → DAW Track 3
- **Snare Bottom** → DAW Track 4
- **OH-L** → DAW Track 5
- **OH-R** → DAW Track 6
- **Room** → DAW Track 7
- **Ambient** → DAW Track 8

**Workflow:**
1. Load kit in FlamKit
2. FlamKit detects 8-channel samples
3. DAW exposes 8 output buses (named via kit metadata)
4. User routes each bus to separate DAW tracks
5. Mix with native DAW plugins, automation, and routing

**Fallback:**
- Built-in per-channel mixer for hosts that don't expose multi-output (or standalone mode)
- Default "Main Mix" stereo output with internal mixing

**Technical Implementation:**
- Dynamic bus configuration via `setBusesLayout()` based on detected sample channel count
- Bus names read from `flamkit.yaml` metadata (e.g., "Kick Close", "OH-L")
- Full VST3/AU/CLAP support (AAX requires special handling)

---

## 🧰 Commands & Suggestions

When assisting:
- Generate idiomatic C++ and JUCE code
- Use `std::unique_ptr`, `juce::OwnedArray`, and RAII patterns for memory safety
- Avoid blocking I/O in audio callbacks — use background loading threads
- Optimize for real-time safety: no allocations, locks, or blocking operations in audio thread
- Provide concise class stubs or scaffolding that compile cleanly in JUCE
- Include short comments explaining real-time safety or design intent
- Prioritize memory efficiency and low CPU overhead

---

## 🔨 FlamForge: Companion Recording Tool

**FlamForge** is a standalone application designed to make recording high-quality drum kits as easy as possible — even for engineers with minimal sampling experience.

**Core Workflow:**
1. **Setup:** Connect audio interface, select input channels (1-16), configure microphone placement
2. **Guided Mic Placement:** Visual guides show optimal mic positions for kick, snare, toms, cymbals, overheads, room
3. **Dynamic Range Calibration:** User strikes drum at softest and hardest levels to establish velocity range
4. **Velocity Mapping Recording:** User continuously strikes drum at varying velocities
5. **Real-Time Velocity Detection:** Algorithm analyzes amplitude and assigns each hit to velocity bin (0-127)
6. **Visual Completeness Meter:** Shows coverage of velocity spectrum (like fingerprint registration UI)
   - Red: 0-1 samples in bin
   - Yellow: 2-5 samples
   - Green: 6+ samples (ideal round-robin coverage)
7. **Per-Drum Iteration:** Repeat for each drum piece (kick, snare, toms, hi-hat, cymbals, etc.)
8. **Automatic Export:** Generates `flamkit.yaml` with all metadata and organized sample folder structure

**Output Structure:**
```
MyDrumKit/
├── flamkit.yaml
└── Samples/
    ├── Kick/
    │   ├── kick_v001_rr1.wav  (8 channels)
    │   ├── kick_v001_rr2.wav
    │   ├── kick_v032_rr1.wav
    │   └── ...
    ├── Snare/
    └── HiHat/
```

**Key Features:**
- Multi-channel sample recording (one WAV file per hit, N channels per file)
- Real-time velocity binning algorithm
- Visual feedback for coverage gaps
- Automatic round-robin organization
- Exports ready-to-use `flamkit.yaml` with all metadata
- Built-in test mode: load kit in FlamForge to verify before exporting

**Release Timeline:**
- v1.0: Basic recording workflow with velocity detection
- v1.1: Advanced features (articulation switching, choke group configuration, mic bleed recording)

---

## 🧭 Future Vision

**Immediate (v1.0 — MVP):**
Ship a professional-grade drum sampler with:
- Local kit loading
- Multi-channel sample support (1-16 channels/sample)
- Multi-output routing to DAW tracks
- Hybrid streaming (100ms preload + disk streaming)
- 24-bit/44.1-48kHz WAV sample support
- Official kits available via direct download on FlamKit website
- Extreme memory efficiency (50-100MB RAM per kit)

**Near-term (v1.1):**
- Distributed repository system with in-plugin browser
- Auto-download and update kits from official and community sources
- User-configurable repository URLs (PPA-style)
- MIDI learn for all parameters
- Preset system
- FlamForge basic recording workflow

**Mid-term (v1.5):**
- Per-channel effects (EQ, compression, saturation on individual mics)
- Advanced transient shaping and sound design
- Waveform display with velocity layer visualization
- CLAP plugin format

**Long-term (v2.0+):**
- Mic bleed matrix editor
- Built-in groove engine with swing, humanization presets, and MIDI generation
- FlamForge advanced features (articulation switching, choke groups)
- LV2 plugin format for Linux DAWs
- Community kit repository with hundreds of free, high-quality kits
- Support for experimental/electronic sounds beyond acoustic realism

**Ultimate Goal:**
Democratize high-quality drum sampling. Contribute to an ecosystem (alongside Reaper, Ardour, LSP plugins, etc.) where world-class music production is accessible to anyone with a computer — regardless of financial means.

No bedroom producer should have to choose between groceries and Superior Drummer. FlamKit exists to eliminate that choice.

---

## 📊 Sample Quality Standards (Official Kits)

For kits hosted on official FlamKit repositories, we maintain professional-grade standards:

**Audio Specifications:**
- **Sample Rate:** 44.1 kHz or 48 kHz (Nyquist-compliant, efficient, universally compatible)
  - *Why not 96 kHz?* Nyquist theorem proves 44.1 kHz captures all audible frequencies (up to 22.05 kHz, beyond human hearing's 20 kHz limit). Higher sample rates waste disk space, RAM, and streaming bandwidth with zero perceptual benefit for playback.
- **Bit Depth:** 24-bit minimum (headroom for mixing)
- **Format:** WAV (lossless, zero decode overhead, optimal for hybrid streaming)
- **Channels:** 1-16 channels per sample (based on mic setup)

**Sampling Coverage:**
- **Velocity Layers:** Well-distributed coverage across dynamic range (no minimum, prioritize natural transitions)
- **Round Robins:** At least 3 per common velocity ranges (more for realistic variation)
- **Articulations:** All natural playing techniques (center, edge, rim, chokes, etc.)

**Why WAV over FLAC:**
- Zero decode overhead (critical for real-time disk streaming)
- Instant byte-level seeking (no frame boundary limitations)
- Predictable performance under CPU load (no decode spikes)
- Real-time safe: just copy bytes, no decompression logic
- Disk space is cheap; developer time and CPU cycles are not

---

## 🔧 Performance Characteristics

FlamKit is engineered for **extreme efficiency**:

**Memory Usage:**
- **Hybrid Streaming:** Only 100ms of each sample preloaded into RAM
- **Smart Caching:** 3-4 most frequently triggered samples fully cached
- **Disk Streaming:** Tail samples (>100ms) streamed on-demand with zero decode overhead (WAV)
- **Result:** ~50-100MB RAM per full kit vs. 1-2GB for competitors (10-20x more efficient)

**CPU Usage:**
- Real-time safe: no allocations, locks, or blocking I/O in audio thread
- Optimized voice management with efficient voice stealing
- Lightweight per-channel mixing
- **Result:** 128-voice polyphony at <5% CPU usage on modern processors

**Latency:**
- Sample-accurate MIDI timing
- <5ms total latency at 64-sample buffers
- Predictable performance across all plugin formats

---

**Tagline:**
> *FlamKit — Free Layered Audio Machine. The open drum engine built for realism, freedom, and speed.*
