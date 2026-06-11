# Changelog

All notable changes to FLAM are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [v1.0.0] — 2026-06-11

### Added
- 16-pad drum sampler engine with velocity-sensitive multi-layer sample playback
- JUCE-based plugin supporting VST3, AU, and Standalone formats
- `flamkit.yaml` kit format — human-readable, version-controllable kit definitions
- Voice manager: 128-voice polyphony, choke groups, round-robin cycling
- Hybrid streaming engine: 100 ms preload + disk streaming for tail samples
- Per-channel Mixer with volume, pan, mute, and solo per mic channel
- Master effects chain: 10-band graphic EQ, compressor, saturator, limiter
- Animated drum pad UI: hit flash, velocity bar, per-instrument colour coding
- PeakMeter component with LUFS-calibrated zone colouring
- Real-time safe audio thread: zero heap allocation or mutex acquisition in callback
- Lock-free ring buffer cross-thread communication
- Voice-steal fade-out ramp eliminating click artefacts at high concurrency
- Headless CLI renderer (`flam-render`) for offline rendering and CI regression
- ASan + UBSan sanitizer CI leg (Linux)
- pluginval L3 host-contract validation (strictness 8) in CI for VST3 and AU
- Universal Binary macOS builds (arm64 + x86_64 in one artifact)
- Cross-platform CI green gate: Linux x86_64, macOS Universal, Windows x64

### Architecture
- `FlamEngine` — pure multichannel voice renderer, no plugin-framework coupling
- `VoiceManager` — voice allocation, choke, envelope, round-robin
- `SampleStreamingManager` — background disk-streaming with lock-free hand-off
- `Mixer` — per-channel and master bus FX routing
- `FlamKitLoader` — YAML kit metadata parser, channel-count detection
- `TenBandGraphicEQ`, `CompressorProcessor`, `SaturationProcessor`, `LimiterProcessor` — DSP modules

### Known Limitations (v1.1 targets)
- AAX format requires the proprietary Avid AAX SDK — not included in this release
- IRConvolver (convolution reverb) deferred to v1.1
- In-plugin kit browser and repository system deferred to v1.1
- CLAP format deferred to v1.1
- MIDI Learn deferred to v1.1
