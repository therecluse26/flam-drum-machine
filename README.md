# FlamKit - Free Layered Audio Machine

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![JUCE](https://img.shields.io/badge/JUCE-8.0-green.svg)](https://juce.com/)

> A professional-grade, open-source drum sampler and plugin framework built with C++ and JUCE

FlamKit is a cross-platform, low-latency drum engine designed to rival commercial samplers like Superior Drummer and BFD — but with an open, extensible architecture and community-driven kit format.

## Features

- **Multi-layered Velocity Sampling**: Realistic dynamics with unlimited velocity layers per drum
- **Round-Robin Sample Cycling**: Eliminates "machine gun" effect with intelligent sample rotation
- **Choke Groups**: Realistic hi-hat open/closed behavior and other mutually exclusive sounds
- **Humanization Engine**: Built-in timing and velocity variation for natural feel
- **Multi-Mic Mixing**: Independent control over close, overhead, room, and ambient mics
- **Mic Bleed Simulation**: Realistic crosstalk between microphone channels
- **Convolution Reverb**: High-quality IR-based room simulation
- **Low Latency**: <5ms latency at 64-sample buffers
- **Cross-Platform**: Runs on Linux, macOS, and Windows
- **Open Kit Format**: Human-readable YAML/JSON kit definitions

## Plugin Formats

- **Standalone Application** - For live performance and testing
- **VST3** - Industry standard, all major DAWs
- **AU (Audio Unit)** - Logic Pro, GarageBand (macOS)
- **AAX** - Pro Tools (requires AAX SDK)

## Quick Start

### Installation

See [BUILD.md](BUILD.md) for detailed build instructions.

```bash
# Clone and build
git clone https://github.com/your-username/flam-drum-machine.git
cd flam-drum-machine
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Loading a Kit

1. Launch FlamKit (standalone or in your DAW)
2. Click "Load Kit" button
3. Select a `.flamkit` file
4. Play drums via MIDI controller or sequencer

### Creating Your Own Kits

See [Resources/Kits/README.md](Resources/Kits/README.md) for the kit format specification.

Example kit structure:
```
MyDrumKit/
├── MyDrumKit.flamkit
└── Samples/
    ├── Kick/
    │   ├── kick_soft.wav
    │   ├── kick_medium.wav
    │   └── kick_hard.wav
    ├── Snare/
    └── HiHat/
```

## Architecture

FLAM is built around a modular architecture:

```
Source/
├── Core/              # Audio engine
│   ├── FlamEngine     # Main engine coordinator
│   ├── VoiceManager   # Voice allocation and playback
│   ├── SampleVoice    # Individual sample playback
│   ├── SampleLoader   # Background sample loading
│   └── MixerBus       # Multi-channel mixing
├── Formats/           # Kit loading and parsing
│   └── FlamKitLoader  # YAML/JSON kit parser
├── DSP/               # Audio processing
│   └── IRConvolver    # Convolution reverb
├── Plugin/            # Plugin wrapper
│   ├── PluginProcessor
│   └── PluginEditor
└── UI/                # User interface components
```

### Key Design Principles

1. **Real-Time Safety**: No allocations or I/O in audio thread
2. **Lock-Free Design**: Atomic operations for thread-safe state sharing
3. **Background Loading**: Sample loading happens off the audio thread
4. **Sample-Accurate Timing**: Precise MIDI event handling
5. **Efficient Voice Management**: Voice stealing with age-based prioritization

## Technical Specifications

| Feature | Specification |
|---------|--------------|
| Max Polyphony | 128 voices |
| Sample Rate | 44.1/48/88.2/96 kHz |
| Bit Depth | 16/24/32-bit float |
| Latency | <5ms @ 64 samples |
| Formats | WAV, AIFF, FLAC, OGG |
| MIDI Range | 0-127 (GM compatible) |
| Velocity Layers | Unlimited |
| Round Robin | 16 groups per layer |
| Choke Groups | 16 groups |

## MIDI Mapping (General MIDI Standard)

- **36** - Kick Drum
- **38** - Snare
- **42** - Hi-Hat Closed
- **46** - Hi-Hat Open
- **41/43/45** - Floor/Low/Mid Toms
- **47/48/50** - Mid/High Toms
- **49/52/55/57** - Crash Cymbals
- **51/53/59** - Ride Cymbals

## Roadmap

### Version 1.0 (Current)
- [x] Core engine architecture
- [x] Multi-layer velocity sampling
- [x] Round-robin sample cycling
- [x] Choke groups
- [x] Humanization
- [x] Multi-mic mixing
- [x] Convolution reverb
- [x] YAML/JSON kit format
- [x] Basic UI

### Version 1.1 (Planned)
- [ ] Advanced articulation switching
- [ ] Per-piece effects (EQ, compression)
- [ ] MIDI learn for all parameters
- [ ] Preset system
- [ ] Improved UI with waveform display
- [ ] Kit browser

### Version 2.0 (Future)
- [ ] Multi-articulation per piece (center, edge, rim)
- [ ] Mic bleed matrix editor
- [ ] Built-in groove engine
- [ ] CLAP and LV2 plugin formats
- [ ] Community kit repository
- [ ] Cloud kit sharing

## Contributing

We welcome contributions! Areas where you can help:

- **Kit Creation**: Record and share drum kits in the FLAM format
- **Code**: Implement features, fix bugs, improve performance
- **Documentation**: Improve guides, tutorials, and examples
- **Testing**: Report bugs, test on different platforms
- **Design**: UI/UX improvements

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Community Kits

FLAM is designed for community kit sharing. Visit our [kit repository](https://github.com/flam-audio/kits) to download free, open-source drum kits.

## License

FLAM is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.

Third-party components:
- **JUCE** - ISC License / Commercial License
- **yaml-cpp** - MIT License (planned)

## Credits

Created by the FLAM Project contributors.

Built with:
- [JUCE Framework](https://juce.com/) - Cross-platform audio framework
- [CMake](https://cmake.org/) - Build system

Special thanks to the open-source audio development community.

## Support

- **Documentation**: [docs.flam-audio.org](https://docs.flam-audio.org) (planned)
- **Issues**: [GitHub Issues](https://github.com/your-username/flam-drum-machine/issues)
- **Discussions**: [GitHub Discussions](https://github.com/your-username/flam-drum-machine/discussions)
- **Discord**: [FLAM Community Server](https://discord.gg/flam) (planned)

## Tagline

> *FlamKit — Free Layered Audio Machine. The open drum engine built for realism, freedom, and speed.*

---

**Note**: FLAM is in active development. While the core engine is functional, some features are still being refined. Contributions and feedback are welcome!
