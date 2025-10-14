# FLAM (Free Layered Audio Machine)

You are assisting in developing **FLAM (Free Layered Audio Machine)** — a professional-grade, open-source drum engine and plugin framework built in **C++** with **JUCE**.

---

## 🎯 Project Overview

**FLAM** is a cross-platform, low-latency drum engine and plugin that aims to rival commercial drum samplers like Superior Drummer or BFD — but with open, extensible architecture and realistic playback.

It will:
- Load multi-layered, velocity-sensitive drum samples defined in open `.flamkit` files (YAML/JSON).
- Provide realistic playback via humanization, choke groups, and multi-mic bleed simulation.
- Offer both standalone and plugin builds (VST3, AU, AAX).
- Feature a modular C++ core built around JUCE’s `AudioProcessorGraph` for routing and extensibility.
- Target sample-accurate playback with <5 ms latency at 64-sample buffers.

---

## 🧩 Core Architecture

**Modules (planned):**
- `/Source/Core` — Audio engine (voice management, articulation logic, sample playback)
- `/Source/Formats` — `.flamkit` parser and serializer (YAML/JSON)
- `/Source/DSP` — Envelopes, filters, convolution/IR processing
- `/Source/UI` — JUCE components for mixer, routing, and visual feedback
- `/Source/Plugin` — VST3/AU entry points, parameter bindings, automation hooks
- `/Resources` — Example kits, configuration templates

---

## ⚙️ Design Goals

1. **Low Latency:** Maintain stable performance at 64-sample buffers (≈3 ms @ 48 kHz).
2. **Realistic Playback:** Layered samples, round-robins, and timing humanization.
3. **Open Format:** `.flamkit` files are human-readable and fully community-editable.
4. **Extensibility:** Clean JUCE module design; minimal coupling for third-party add-ons.
5. **Cross-Platform:** Builds cleanly for Linux (PipeWire/ALSA), macOS (CoreAudio), Windows (ASIO/WASAPI).

---

## 🧠 Key Components

- `FlamEngine` — handles sample scheduling, round-robin logic, and velocity mapping.  
- `VoiceManager` — manages voice allocation, choke groups, and per-articulation envelopes.  
- `MixerBus` — multi-channel mixing (close, overhead, room, ambient).  
- `IRConvolver` — optional convolution reverb for room realism.  
- `FlamKitLoader` — reads YAML kit metadata and resolves sample file paths.  
- `FlamAudioProcessor` — JUCE entry point for plugin builds.  

---

## 💡 Development Preferences

- C++20 or newer
- JUCE (latest release)
- Follow real-time safe audio thread principles (no dynamic allocation in callback)
- Use `AudioBuffer<float>` and `AudioSampleBuffer` abstractions for clean sample handling
- Prefer composition over inheritance for engine subsystems

---

## 📦 Output Targets

FLAM must build cleanly and function identically across all major plugin formats and hosts.

| Target | Purpose | Notes |
|---------|----------|-------|
| **Standalone App** | For live performance, testing, and demo playback | Built using JUCE's Standalone wrapper; identical engine as plugin. |
| **VST3** | Cross-platform industry standard | Supported natively by JUCE (VST3 SDK required; Steinberg license allows redistribution). |
| **AU (Audio Unit)** | Required for Logic Pro, MainStage, and GarageBand (macOS only) | Fully supported by JUCE; requires Xcode toolchain and proper signing for Apple notarization. |
| **AAX (Avid Audio eXtension)** | Required for Pro Tools | Supported via JUCE’s AAX wrapper; needs Avid Developer Program membership and AAX SDK. |
| **(Future) CLAP / LV2** | For open-source hosts (Bitwig, Ardour, etc.) | Optional, post-1.0 milestone; community contributions encouraged. |

### Build Notes

- The JUCE CMake template will generate **all plugin formats** from one project definition:
  ```cmake
  juce_add_plugin(FLAM
      ...
      FORMATS VST3 AU AAX Standalone
  )

---

## 🧰 Commands & Suggestions

When assisting:
- Generate idiomatic C++ and JUCE code.
- Use `std::unique_ptr`, `juce::OwnedArray`, and RAII patterns for memory safety.
- Avoid blocking I/O in audio callbacks — use background loading threads.
- Provide concise class stubs or scaffolding that compile cleanly in JUCE.
- Include short comments explaining real-time safety or design intent.

---

## 🧭 Future Vision

Eventually, FLAM will host a **community kit repository** — open, shareable `.flamkit` packages recorded and contributed by engineers.  
The long-term goal: democratize high-quality drum sampling and provide a transparent, hackable engine for musicians and developers alike.

---

**Tagline suggestion:**  
> *FLAM — Free Layered Audio Machine. The open drum engine built for realism, freedom, and speed.*
