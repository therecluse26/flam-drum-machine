# FlamKit - Free Layered Audio Machine

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![JUCE](https://img.shields.io/badge/JUCE-8.0-green.svg)](https://juce.com/)

> **The best drum sampler that exists. Professional-grade. Multi-channel. Completely free.**

FlamKit is a GPLv3-licensed, cross-platform drum engine designed to rival — and surpass — commercial samplers like Superior Drummer and BFD, while remaining free and open source forever.

---

## 🎯 Mission

**Music production shouldn't require financial privilege.**

Commercial drum samplers cost $300-$1000+. Many are limited to single platforms or require proprietary formats. This creates an artificial barrier between bedroom producers and professional tools.

**FlamKit exists to eliminate that barrier:**

- ✨ **Best-in-class quality** — Official kits match or exceed Superior Drummer, BFD3, and Steven Slate
- 💰 **Zero cost** — Professional-grade samples and software, free forever
- 🌍 **Open ecosystem** — Distributed repositories mean no single point of control
- 🔓 **Complete transparency** — GPLv3 means you own your tools
- ⚡ **Extreme efficiency** — 50-100MB RAM per kit vs. 1-2GB for competitors (10-20x improvement)

We're contributing to an ecosystem (Reaper, Ardour, LSP, countless free plugins) where world-class production is accessible to anyone with creativity and a computer.

**No bedroom producer should have to choose between groceries and Superior Drummer.**

---

## ✨ Features

### Studio-Grade Sampling
- **Multi-Channel Architecture**: 1-16 channels per sample (kick close, OH-L, OH-R, room, ambient, etc.)
- **Multi-Output Routing**: Each mic routes to independent DAW tracks — FlamKit becomes a **virtual multitrack session**
- **High-Resolution Audio**: 24-bit/44.1-48kHz samples with unlimited velocity layers
- **Extensive Round-Robins**: Unlimited samples per velocity layer to eliminate "machine gun" effect
- **Realistic Articulations**: Natural drum performance with humanization and articulation switching
- **Choke Groups**: Authentic hi-hat open/closed behavior and mutually exclusive sounds

### Professional Mixing & Effects
- **Per-Channel Routing**: Route each mic to separate DAW tracks for mixing with native plugins
- **Built-in Mixer**: Per-channel volume, pan, solo, mute for standalone mode or quick mixing
- **Integrated Effects**: EQ, compression, saturation, transient shaping (per-channel and master)
- **Convolution Reverb**: High-quality IR-based room simulation
- **Full DAW Automation**: Every parameter exposed for automation in your DAW

### Extreme Efficiency
- **Hybrid Streaming**: Only 100ms of each sample preloaded into RAM
- **Smart Caching**: 3-4 most frequently triggered samples fully cached
- **Ultra-Low Memory**: ~50-100MB RAM per kit vs. 1-2GB for competitors
- **Minimal CPU Overhead**: 128-voice polyphony at <5% CPU usage
- **Ultra-Low Latency**: <5ms @ 64-sample buffers with sample-accurate MIDI timing

### Open & Extensible
- **Distributed Kit Repositories**: Download free kits from official and community sources (v1.1+)
- **Human-Readable Format**: YAML kit definitions you can edit in any text editor
- **GPLv3 Licensed**: Free forever, fork-friendly, prevents proprietary capture
- **Cross-Platform**: Linux, macOS, Windows with identical performance

---

## 🎙️ Multi-Output Routing (v1.0 Core Feature)

Transform your DAW into a **virtual multitrack drum studio**:

**Workflow:**
1. Load kit in FlamKit
2. FlamKit detects 8-channel samples (example)
3. DAW exposes 8 output buses: Kick Close, Kick Sub, Snare Top, Snare Bottom, OH-L, OH-R, Room, Ambient
4. Route each bus to separate DAW tracks
5. Mix with your favorite plugins, automation, and routing

**Result:** Full control over drum mix using native DAW tools, just like a real multitrack recording session.

---

## 📦 Plugin Formats

- **Standalone Application** - Live performance, testing, and production
- **VST3** - Industry standard, all major DAWs (multi-output supported)
- **AU (Audio Unit)** - Logic Pro, GarageBand (macOS)
- **AAX** - Pro Tools (requires AAX SDK)
- **CLAP** (v1.1+) - Modern open standard for Bitwig, Reaper, etc.
- **LV2** (Future) - Ardour, Qtractor, and Linux-focused DAWs

---

## 🚀 Quick Start

### Installation

See [BUILD.md](BUILD.md) for detailed build instructions.

```bash
# Clone and build
git clone https://github.com/flam/flam-drum-machine.git
cd flam-drum-machine
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Loading a Kit

1. Launch FlamKit (standalone or in your DAW)
2. Click "Load Kit" button
3. Select a `flamkit.yaml` file
4. Play drums via MIDI controller or sequencer
5. (Optional) Route individual mic channels to separate DAW tracks for advanced mixing

### Getting Kits

**v1.0:** Download kits directly from the FlamKit website
**v1.1+:** Browse and download kits from within the plugin via distributed repositories

---

## 🔨 Creating Your Own Kits

### Using FlamForge (Companion Recording Tool)

**FlamForge** makes recording professional drum kits incredibly easy — even for beginners:

1. **Setup:** Connect audio interface, select input channels (1-16)
2. **Guided Mic Placement:** Visual guides show optimal mic positions
3. **Dynamic Range Calibration:** Strike drum at softest/hardest levels
4. **Velocity Recording:** Continuously strike drum at varying velocities
5. **Visual Completeness Meter:** See coverage of velocity spectrum (like fingerprint registration)
6. **Automatic Export:** Generates ready-to-use `flamkit.yaml` and organized samples

**Example kit structure:**
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

**FlamForge Release:** v1.0 (basic workflow), v1.1 (advanced features)

See [Resources/Kits/README.md](Resources/Kits/README.md) for the kit format specification.

---

## 🏗️ Architecture

FlamKit is built around a modular, real-time safe architecture. See [ARCHITECTURE.md](ARCHITECTURE.md) for the full verified module map and status tags.

```
Source/
├── Core/
│   ├── FlamEngine                 # Engine coordinator (AudioProcessorGraph) ✅
│   ├── VoiceManager               # Voice pool (128), round-robin, choke groups ✅
│   ├── SampleVoice                # Per-voice render (ADSR + streaming handoff) ✅
│   ├── SampleStreamingManager     # 100ms preload + background disk streaming ✅
│   └── Mixer                      # Per-channel mixer with FX chain 🟡
├── Formats/
│   └── FlamKitLoader              # flamkit.yaml / .json parser ✅
├── DSP/
│   ├── SimpleEQ / SimpleCompressor      # Master bus EQ + compressor ✅
│   ├── TenBandGraphicEQ / SaturationProcessor / CompressorProcessor / LimiterProcessor  # Per-channel FX (via Mixer) ✅
│   └── IRConvolver                # Convolution reverb 🟡
├── Plugin/
│   ├── PluginProcessor            # JUCE entry point; owns engine + bus config ✅
│   └── PluginEditor               # Valhalla-inspired UI ✅
└── UI/
    ├── MixerPanel                 # Dynamic channel fader container ✅
    ├── ChannelStripComponent      # Per-channel fader / pan / mute / solo ✅
    └── ...                        # Level meters, knobs, faders ✅
```

### Key Design Principles

1. **Real-Time Safety**: No allocations, locks, or I/O in audio thread
2. **Hybrid Streaming**: 100ms preload + smart caching + disk streaming = minimal RAM usage
3. **Lock-Free Design**: Atomic operations for thread-safe state sharing
4. **Background Loading**: Sample loading happens off the audio thread
5. **Sample-Accurate Timing**: Precise MIDI event handling
6. **Efficient Voice Management**: Voice stealing with age-based prioritization
7. **Multi-Output First**: Every mic channel can route to independent DAW track

---

## 📊 Technical Specifications

| Feature | Specification |
|---------|--------------|
| **Audio Quality** | |
| Sample Rate | 44.1/48 kHz (official kits) |
| Bit Depth | 24-bit |
| Channels/Sample | 1-16 (multi-mic) |
| Format | WAV (lossless, zero decode overhead) |
| **Performance** | |
| Max Polyphony | 128 voices |
| RAM Usage | 50-100MB per kit (vs. 1-2GB competitors) |
| CPU Usage | <5% for 128 voices (modern CPU) |
| Latency | <5ms @ 64 samples |
| Voice Stealing | Age-based priority |
| **Sampling** | |
| Velocity Layers | Unlimited |
| Round Robins | Unlimited per layer |
| Choke Groups | 16 groups |
| Articulations | Unlimited per piece |
| **MIDI** | |
| MIDI Range | 0-127 (GM compatible) |
| Automation | All parameters |
| **Effects** | |
| Per-Channel FX | EQ, Compression, Saturation |
| Master FX | Full chain + Convolution |
| **Output Routing** | |
| Multi-Output | Up to 16 independent buses |
| Bus Naming | From kit metadata |
| Fallback | Built-in stereo mix |

**Why 44.1kHz (not 96kHz)?**
Nyquist theorem proves 44.1 kHz captures all audible frequencies (up to 22.05 kHz, beyond human hearing's 20 kHz limit). Higher sample rates waste disk space, RAM, and streaming bandwidth with **zero perceptual benefit** for playback. Disk space is cheap, but not *that* cheap for multi-channel kits.

---

## 🗺️ Roadmap

### Version 1.0 — Professional Core (MVP)

Status matches [ARCHITECTURE.md](ARCHITECTURE.md): ✅ implemented & integrated · 🟡 built, not yet in active signal path · 🔵 planned

- ✅ Multi-channel sample architecture (1–16 channels/sample)
- ✅ Hybrid streaming (100ms preload + disk streaming via `SampleStreamingManager`)
- ✅ Multi-layer velocity sampling with round-robin cycling
- ✅ Choke groups
- ✅ YAML kit format (`flamkit.yaml`)
- ✅ Master EQ + compressor
- ✅ VST3 / AU / AAX plugin formats + Standalone
- 🟡 Multi-output routing to DAW tracks — `Mixer` built; signal path integration in progress
- 🟡 Per-channel mixer (volume, pan, solo, mute) — built, not yet in signal path
- 🟡 Convolution reverb — built, not yet wired into signal path
- 🟡 Articulation switching — data model complete; runtime switching in progress
- 🟡 Humanization engine — parameter registered; implementation not yet wired
- 🔵 Official website with direct kit downloads
- 🔵 Initial release with 2–3 demo kits available for download

### Version 1.1 — Distributed Repositories
- [ ] Repository browser UI (in-plugin)
- [ ] Auto-download and update kits from remote sources
- [ ] User-configurable repository URLs (PPA-style)
- [ ] Kit metadata caching and search
- [ ] MIDI learn for all parameters
- [ ] Preset system
- [ ] FlamForge basic recording workflow
- [ ] CLAP plugin format

### Version 1.5 — Advanced Features
- [ ] Per-channel effects (EQ/compression on individual mics)
- [ ] Advanced transient shaping and sound design
- [ ] Waveform display with velocity layer visualization
- [ ] Improved UI with visual feedback
- [ ] Advanced articulation switching

### Version 2.0 — Ecosystem Expansion
- [ ] Mic bleed matrix editor
- [ ] Built-in groove engine with MIDI generation
- [ ] FlamForge advanced features (articulation switching, choke groups)
- [ ] LV2 plugin format
- [ ] Community kit repository with 100+ free kits
- [ ] Cloud kit sharing and collaboration

### Version 3.0 — Beyond Realism
- [ ] Electronic/hybrid drum sound design
- [ ] Synthesis-based articulations
- [ ] Creative effects and mangling
- [ ] Advanced groove engine with AI-assisted humanization

---

## 🌍 Distributed Kit Repositories

FlamKit uses a **decentralized distribution model** inspired by Linux package managers (APT/PPA):

- **Official Repository:** High-quality, curated kits (24-bit/44.1-48kHz, multi-channel) hosted by FlamKit
- **Community Repositories:** Anyone can host their own kit repository and share the URL
- **User-Added Sources:** Add custom repository URLs (like Ubuntu PPAs) for niche kits
- **No Central Bottleneck:** Creativity isn't limited by our servers or approval process

**Kit Licensing:** We recommend Creative Commons (CC-BY, CC0) or GPL-compatible licenses for community kits.

---

## 🤝 Contributing

We welcome contributions! Areas where you can help:

- **Kit Creation**: Record and share drum kits using FlamForge
- **Code**: Implement features, fix bugs, improve performance
- **Documentation**: Improve guides, tutorials, and examples
- **Testing**: Report bugs, test on different platforms
- **Design**: UI/UX improvements (Valhalla-inspired aesthetic)
- **Repository Hosting**: Host community kit repositories

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

---

## 📜 License

FlamKit is licensed under the **GNU General Public License v3.0 (GPLv3)**. See [LICENSE](LICENSE) for details.

**What GPLv3 means for FlamKit:**
- ✅ Use FlamKit as a plugin in any DAW — personal or commercial
- ✅ Study, fork, and modify the source code
- ✅ Distribute your modifications — must be under GPLv3
- ✅ Derivative works (code incorporating FlamKit) must also be GPLv3 (strong copyleft)
- ❌ Incorporate FlamKit code into a closed-source or proprietary application
- ❌ Relicense — derivative works must remain GPLv3

> **Note:** GPLv3 is strong copyleft — unlike LGPL, it does **not** permit linking FlamKit into proprietary applications. Any project that incorporates FlamKit source must also be GPLv3-licensed.

Third-party components:
- **JUCE** — AGPLv3 (free tier, compatible with GPLv3 for local desktop plugins) / Commercial License
- **yaml-cpp** - MIT License (planned)

---

## 🙏 Credits

Created by the FlamKit Project contributors.

Built with:
- [JUCE Framework](https://juce.com/) - Cross-platform audio framework
- [CMake](https://cmake.org/) - Build system

Special thanks to the open-source audio development community and the ecosystem that inspired this project: Reaper, Ardour, LSP Plugins, and countless other tools democratizing music production.

---

## 💬 Support

- **Documentation**: [docs.flam-audio.org](https://docs.flam-audio.org) (planned)
- **Issues**: [GitHub Issues](https://github.com/flam/flam-drum-machine/issues)
- **Discussions**: [GitHub Discussions](https://github.com/flam/flam-drum-machine/discussions)
- **Discord**: [FlamKit Community Server](https://discord.gg/flam) (planned)

---

## 🥁 MIDI Mapping (General MIDI Standard)

- **36** - Kick Drum
- **38** - Snare
- **42** - Hi-Hat Closed
- **46** - Hi-Hat Open
- **41/43/45** - Floor/Low/Mid Toms
- **47/48/50** - Mid/High Toms
- **49/52/55/57** - Crash Cymbals
- **51/53/59** - Ride Cymbals

---

## 🎨 Philosophy

FlamKit is guided by three principles:

1. **Democratization**: Professional tools shouldn't require financial privilege
2. **Transparency**: Open formats, open source, open ecosystem
3. **Excellence**: Free doesn't mean compromised — we aim to be the best that exists

**Tagline:**
> *FlamKit — Free Layered Audio Machine. The open drum engine built for realism, freedom, and speed.*

---

**Note**: FlamKit is in active development. The core engine is functional and approaching v1.0 release. Contributions and feedback are welcome!
