# Per-Channel Mixer Implementation Plan

**Feature:** Per-Channel Mixer with Multi-Output Routing
**Status:** v1.0 MVP Core Feature
**Priority:** Critical
**Spec Reference:** `FeatureSpecs/PerChannelMixer.md`

---

## Overview

This document outlines the step-by-step implementation strategy for the Per-Channel Mixer feature. The mixer provides flexible routing where each microphone channel can be directed to either the internal Main Mix (with volume/pan/effects processing) or individual DAW output buses (direct routing, bypassing internal processing).

---

## Implementation Phases

### Phase 1: Core DSP Effects Processors (Foundation)
**Goal:** Build the DSP building blocks that will be used in both channel strips and master section.

#### Step 1.1: 10-Band Graphic EQ Processor
**Files:** `Source/DSP/TenBandGraphicEQ.h`, `Source/DSP/TenBandGraphicEQ.cpp`

**Tasks:**
1. Create `TenBandGraphicEQ` class with:
   - 10 fixed-frequency bands (31.25 Hz to 16 kHz, ISO standard)
   - Per-band gain control (-12 dB to +12 dB)
   - JUCE `dsp::IIR::Filter<float>` for each band
   - `prepareToPlay()` for sample rate initialization
   - `updateCoefficients()` for real-time parameter changes
   - `process()` method using JUCE's `AudioBlock` API
2. Use peaking filters with Butterworth Q (1/√2) for smooth response
3. Store band gains in `std::atomic<float>` arrays for thread-safe UI updates
4. Implement enabled/bypassed state with atomic boolean
5. Test with white noise and sine sweeps to verify frequency response

**Validation:**
- Verify no audible artifacts when toggling bands on/off
- Confirm flat response when all bands at 0 dB
- Test extreme boosts/cuts (±12 dB) for stability

---

#### Step 1.2: Saturation Processor
**Files:** `Source/DSP/SaturationProcessor.h`, `Source/DSP/SaturationProcessor.cpp`

**Tasks:**
1. Create `SaturationProcessor` class with three modes:
   - **Tape:** Soft clipping using `std::tanh()` with asymmetric curve
   - **Tube:** Odd-harmonic saturation with pre-gain boost
   - **Digital:** Hard clipping with subtle overshoot
2. Implement amount parameter (0.0-1.0) for wet/dry blend
3. Use atomic variables for thread-safe parameter access
4. Optimize for SIMD where possible (evaluate `juce::FloatVectorOperations`)
5. Add enabled/bypassed state

**Validation:**
- Analyze harmonic content with spectrum analyzer for each mode
- Verify tape mode produces warm, even-order harmonics
- Confirm tube mode emphasizes odd harmonics
- Test digital mode for clean hard clipping at threshold

---

#### Step 1.3: Compressor Processor
**Files:** `Source/DSP/CompressorProcessor.h`, `Source/DSP/CompressorProcessor.cpp`

**Tasks:**
1. Create `CompressorProcessor` class with:
   - Threshold (dB)
   - Ratio (1:1 to 20:1)
   - Attack time (ms)
   - Release time (ms)
   - Makeup gain (dB)
2. Implement envelope follower with separate attack/release coefficients
3. Calculate gain reduction using log-domain processing
4. Apply makeup gain after compression
5. Store envelope state as member variable (not atomic, audio thread only)
6. Use atomic parameters for threshold/ratio/attack/release/makeup

**Validation:**
- Test with drum transients to verify attack captures peaks
- Confirm release smoothly returns to unity gain
- Measure gain reduction accuracy against theoretical values
- Test extreme ratios (20:1) for limiter-like behavior

---

#### Step 1.4: Limiter Processor
**Files:** `Source/DSP/LimiterProcessor.h`, `Source/DSP/LimiterProcessor.cpp`

**Tasks:**
1. Create `LimiterProcessor` class (simplified compressor):
   - Threshold (dB, typically -0.1 dBFS)
   - Release time (ms, fast default ~50ms)
   - Instant attack (no attack parameter needed)
   - Infinite ratio (hard limiting)
2. Implement envelope follower with instant attack
3. Calculate gain reduction to prevent signal exceeding threshold
4. Use look-ahead buffer (optional v1.1 enhancement)

**Validation:**
- Verify output never exceeds threshold under any input
- Test with brick-wall transients (0 dBFS square wave)
- Confirm transparent limiting at conservative thresholds (-1 dBFS)
- Measure latency (should be zero without look-ahead)

---

### Phase 2: PerChannelMixer Core Class
**Goal:** Build the central mixer engine with dynamic channel configuration and routing.

#### Step 2.1: PerChannelMixer Class Structure
**Files:** `Source/Core/PerChannelMixer.h`, `Source/Core/PerChannelMixer.cpp`

**Tasks:**
1. Define `PerChannelMixer` class with:
   - `OutputDestination` enum (MainMix, Bus1-16)
   - `ChannelStrip` struct containing:
     - Name (from YAML)
     - Output routing (atomic int)
     - Volume/pan/solo/mute (atomic)
     - Embedded FX instances (EQ, saturation, compressor)
     - Peak metering (atomic float)
   - `std::vector<ChannelStrip>` for dynamic channel count
   - Master section with volume, FX chain (EQ, saturation, compressor, limiter)
2. Implement `setNumChannels()` for dynamic reconfiguration
3. Add getter/setter methods for all parameters (thread-safe)
4. Create `getState()` and `setState()` for JUCE ValueTree serialization

**Key Design Decisions:**
- Use composition (FX embedded in structs) rather than inheritance
- All UI-controllable parameters use `std::atomic<>` for lock-free access
- Audio thread never allocates memory after `prepareToPlay()`

**Validation:**
- Test resizing from 2 to 16 channels and back
- Verify all parameters accessible from UI thread without blocking audio

---

#### Step 2.2: Buffer Pre-Allocation Strategy
**Files:** `Source/Core/PerChannelMixer.cpp` (in `prepareToPlay()`)

**Tasks:**
1. Allocate `channelFXBuffers` vector:
   - One mono `AudioBuffer<float>` per channel
   - Sized to `maximumBlockSize` from `prepareToPlay()`
   - Never resized during audio processing
2. Allocate `masterFXBuffer`:
   - Stereo `AudioBuffer<float>` (2 channels)
   - Sized to `maximumBlockSize`
3. Call `prepareToPlay()` on all FX instances:
   - Pass sample rate and block size
   - Initialize filter coefficients
   - Reset envelope states
4. Document memory overhead in code comments

**Memory Calculation:**
```
Per-channel buffer: maximumBlockSize * sizeof(float) = 512 * 4 = 2 KB
Master buffer: 2 * maximumBlockSize * sizeof(float) = 512 * 2 * 4 = 4 KB
Total for 16 channels: 16 * 2 KB + 4 KB = 36 KB
```

**Validation:**
- Confirm zero allocations in audio thread using Instruments (macOS) or Valgrind (Linux)
- Verify buffers correctly reused across multiple process() calls
- Test sample rate changes trigger proper reallocation

---

#### Step 2.3: Audio Processing Pipeline
**Files:** `Source/Core/PerChannelMixer.cpp` (implement `process()`)

**Tasks:**
1. Implement main `process()` method:
   ```cpp
   void process(
       const AudioBuffer<float>& multiChannelInput,  // All mic channels
       AudioBuffer<float>& allOutputBuses,            // Bus 0 = Main Mix, Bus 1-16 = individual
       int numSamples
   );
   ```
2. Clear all output buses at start of block
3. Detect if any Main Mix channels are soloed (affects routing logic)
4. For each channel:
   - Check output destination (MainMix vs Bus)
   - Route to appropriate processing path
5. Apply master FX chain to Main Mix:
   - Copy Main Mix to master buffer
   - Process: EQ → Saturation → Compressor → Limiter
   - Apply master volume
   - Copy back to output bus 0
6. Update peak meters for all channels and master

**Processing Flow:**
```
Input (multi-channel)
  ↓
Per-Channel Routing Decision
  ↓                    ↓
Main Mix Path       Bus Path (direct)
  ↓                    ↓
Apply mute/solo    Copy to bus output
  ↓                    ↓
FX: EQ → Sat → Comp   Update peak meter
  ↓
Apply volume/pan
  ↓
Sum to Main Mix buffer
  ↓
Update peak meter
  ↓
Master FX: EQ → Sat → Comp → Limiter
  ↓
Master volume
  ↓
Output Bus 0 (stereo)
```

**Validation:**
- Verify sample-accurate output (compare manual routing to automated)
- Test with all channels Main Mix, all channels Bus, and mixed configurations
- Confirm solo logic works correctly (only soloed channels audible)
- Measure CPU usage (target <5% with all FX enabled on 16 channels)

---

#### Step 2.4: Channel-to-Main-Mix Processing
**Files:** `Source/Core/PerChannelMixer.cpp` (implement `processChannelToMainMix()`)

**Tasks:**
1. Create helper method:
   ```cpp
   void processChannelToMainMix(
       size_t chIdx,
       const ChannelStrip& channel,
       const AudioBuffer<float>& multiChannelInput,
       AudioBuffer<float>& allOutputBuses,
       int numSamples,
       bool anySoloed
   );
   ```
2. Implement mute/solo logic:
   - Skip if muted
   - Skip if other channels soloed and this one isn't
3. Copy channel to pre-allocated mono FX buffer
4. Process FX chain in order: EQ → Saturation → Compressor
5. Apply volume and pan (constant-power panning):
   ```cpp
   panAngle = (pan + 1.0f) * π / 4.0f
   leftGain = cos(panAngle) * volumeGain
   rightGain = sin(panAngle) * volumeGain
   ```
6. Add processed signal to Main Mix output (bus 0, stereo)
7. Update peak meter with decay

**Panning Law:**
- Use constant-power panning to maintain perceived loudness
- Center: L=0.707, R=0.707 (-3 dB)
- Hard left: L=1.0, R=0.0
- Hard right: L=0.0, R=1.0

**Validation:**
- Verify panning maintains equal loudness across stereo field
- Test FX bypass (disabled effects should consume zero CPU)
- Confirm peak meter accurately reflects post-FX level

---

#### Step 2.5: Channel-to-Bus Direct Routing
**Files:** `Source/Core/PerChannelMixer.cpp` (implement `routeChannelToBus()`)

**Tasks:**
1. Create helper method:
   ```cpp
   void routeChannelToBus(
       size_t chIdx,
       int busIndex,
       const AudioBuffer<float>& multiChannelInput,
       AudioBuffer<float>& allOutputBuses,
       int numSamples
   );
   ```
2. Calculate output bus offset:
   - Bus 0 (Main Mix) = channels 0-1 (stereo)
   - Bus 1 = channel 2 (mono)
   - Bus 2 = channel 3 (mono)
   - Bus N = channel N+1 (mono)
3. Copy input channel directly to output bus (bypass all processing)
4. Update peak meter (for visual feedback even when bypassing FX)

**Bus Layout:**
```
allOutputBuses channels:
  [0, 1]    = Main Mix (stereo)
  [2]       = Bus 1 (mono)
  [3]       = Bus 2 (mono)
  ...
  [17]      = Bus 16 (mono)
Total: 18 output channels
```

**Validation:**
- Verify bit-perfect passthrough (input === output)
- Confirm no FX processing occurs (measure CPU, should be negligible)
- Test boundary conditions (16 channels routed to buses)

---

### Phase 3: Plugin Integration
**Goal:** Integrate mixer into JUCE AudioProcessor with dynamic bus configuration.

#### Step 3.1: Dynamic Bus Configuration
**Files:** `Source/Plugin/PluginProcessor.h`, `Source/Plugin/PluginProcessor.cpp`

**Tasks:**
1. Add `perChannelMixer` member to `FlamAudioProcessor`:
   ```cpp
   std::unique_ptr<PerChannelMixer> perChannelMixer;
   ```
2. Initialize in constructor:
   ```cpp
   FlamAudioProcessor()
       : AudioProcessor(BusesProperties()
           .withOutput("Main Mix", AudioChannelSet::stereo(), true))
   {
       perChannelMixer = std::make_unique<PerChannelMixer>();
   }
   ```
3. Implement `onKitLoaded()` callback:
   - Extract channel count and names from `KitMetadata`
   - Call `perChannelMixer->setNumChannels()`
   - Suspend processing during reconfiguration
   - Build new `BusesLayout` with Main Mix + N mono buses
   - Call `setBusesLayout()` to apply
   - Call `prepareToPlay()` on mixer to allocate buffers
   - Resume processing
4. Override `isBusesLayoutSupported()`:
   - Validate requested layout matches current kit configuration
   - Reject layouts with wrong bus count

**Bus Naming:**
- Read channel names from `flamkit.yaml` metadata
- Assign to bus properties for DAW display
- Example: "Kick In", "OH-L", "Room-R"

**Validation:**
- Load 2-channel kit → verify 3 output buses (Main Mix + 2 mono)
- Load 16-channel kit → verify 17 output buses (Main Mix + 16 mono)
- Switch kits → verify buses reconfigure without crashes
- Test in multiple DAWs (Reaper, Ableton, Logic)

---

#### Step 3.2: ProcessBlock Integration
**Files:** `Source/Plugin/PluginProcessor.cpp` (modify `processBlock()`)

**Tasks:**
1. Locate existing `processBlock()` method
2. After voice rendering, call mixer:
   ```cpp
   void FlamAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
   {
       // Existing: Process MIDI, render voices to multiChannelBuffer
       voiceManager->processMidi(midi);

       AudioBuffer<float> multiChannelBuffer(
           perChannelMixer->getNumChannels(),
           buffer.getNumSamples()
       );
       multiChannelBuffer.clear();

       // Render all active voices
       for (auto* voice : voiceManager->getActiveVoices())
       {
           AudioBuffer<float> voiceOutput = voice->renderNextBlock(buffer.getNumSamples());
           for (int ch = 0; ch < voiceOutput.getNumChannels(); ++ch)
               multiChannelBuffer.addFrom(ch, 0, voiceOutput, ch, 0, buffer.getNumSamples());
       }

       // NEW: Process through mixer
       perChannelMixer->process(multiChannelBuffer, buffer, buffer.getNumSamples());
   }
   ```
3. Ensure `buffer` passed to `process()` has correct number of channels
4. Handle standalone mode (buffer may only be stereo) vs plugin mode (multi-output)

**Integration Points:**
- `VoiceManager` renders to multi-channel buffer
- `PerChannelMixer` routes/processes and writes to output buses
- No intermediate mixing step needed (mixer handles everything)

**Validation:**
- Play MIDI notes → verify audio reaches correct output buses
- Change routing → verify audio switches buses in real-time
- Test latency (should be unchanged from pre-mixer implementation)

---

#### Step 3.3: Parameter Bindings and Automation
**Files:** `Source/Plugin/PluginProcessor.cpp`

**Tasks:**
1. Create JUCE `AudioProcessorValueTreeState` parameters:
   - Per-channel: volume, pan, solo, mute, output destination
   - Per-channel FX: EQ band gains, saturation mode/amount, compressor settings
   - Master: volume, FX settings
2. Use parameter ranges matching mixer specs:
   - Volume: -∞ to +6 dB (use `NormalisableRange` with skew for dB scale)
   - Pan: -1.0 (left) to +1.0 (right)
   - EQ bands: -12 dB to +12 dB
3. Connect parameters to mixer setters in `parameterChanged()` callback
4. Implement parameter smoothing for volume/pan (prevent zipper noise)
5. Add parameter groups for organization in DAW automation lanes

**Parameter ID Format:**
```
channel_0_volume
channel_0_pan
channel_0_output
channel_0_eq_enabled
channel_0_eq_band_0
...
master_volume
master_limiter_enabled
```

**Validation:**
- Automate volume in DAW → verify smooth changes without clicks
- Record automation → verify recall matches original settings
- Save/load project → verify all mixer state persists
- Test undo/redo in DAW → verify parameter changes reversible

---

### Phase 4: UI Implementation
**Goal:** Create intuitive mixer interface with dynamic channel strips and visual feedback.

#### Step 4.0: Main UI Navigation Tab Integration
**Files:** `Source/UI/FlamEditorComponent.h`, `Source/UI/FlamEditorComponent.cpp` (or equivalent main UI manager)

**Tasks:**
1. Add "Mixer" tab to main FlamKit navigation:
   - Create tab button (label: "Mixer" or "Mix")
   - Position alongside other tabs (Kit Browser, Mapping, Settings)
   - Use `juce::TabbedComponent` or custom tab implementation
2. Implement tab content management:
   - Create placeholder component for "no kit loaded" state
   - Show `MixerPanel` component when kit is loaded
   - Handle tab visibility and switching
3. Implement state persistence:
   - Store last selected tab in plugin state
   - Restore tab selection on plugin reload
4. Ensure audio processing independence:
   - Mixer continues processing regardless of visible tab
   - UI updates (metering, parameter changes) occur even when tab hidden
   - Use timer callbacks for meter updates (pause when tab not visible for efficiency)

**Tab Layout Structure:**
```
┌─────────────────────────────────────────────────────────┐
│ FlamKit                                                 │
├─────────────────────────────────────────────────────────┤
│ [Kit Browser] [Mapping] [MIXER] [Settings]             │
├─────────────────────────────────────────────────────────┤
│                Mixer Panel or Placeholder               │
└─────────────────────────────────────────────────────────┘
```

**Validation:**
- Switch between tabs → verify mixer tab displays correctly
- Load kit → verify mixer tab auto-populates with channel strips
- Unload kit → verify placeholder message appears
- Close/reopen plugin → verify last selected tab restored
- Play audio with different tabs selected → verify mixer processes correctly

---

#### Step 4.1: Channel Strip Component
**Files:** `Source/UI/ChannelStripComponent.h`, `Source/UI/ChannelStripComponent.cpp`

**Tasks:**
1. Create `ChannelStripComponent` class:
   - Constructor takes `PerChannelMixer&` and channel index
   - Add UI elements:
     - `juce::Label` for channel name
     - `juce::ComboBox` for output routing dropdown
     - `juce::Slider` for volume (vertical)
     - `juce::Slider` for pan (rotary or horizontal)
     - `juce::TextButton` for solo ("S")
     - `juce::TextButton` for mute ("M")
     - Custom `PeakMeter` component
2. Implement layout in `resized()`:
   - Vertical arrangement: Name → Output → Peak Meter → Solo/Mute → Pan → Volume
   - Fixed width (~80-100px), variable height based on parent
3. Connect UI controls to mixer:
   - Sliders/buttons modify mixer parameters
   - Timer callback updates meter from mixer peak levels
4. Implement `updateControlsEnabled()`:
   - Disable volume/pan/solo/mute when output != Main Mix
   - Gray out disabled controls (alpha = 0.5)

**Visual Design:**
- Clean, minimal aesthetic (inspired by Valhalla plugins)
- Use `juce::LookAndFeel_V4` as base, customize colors
- Peak meter: green (< -12 dB), yellow (-12 to -3 dB), red (> -3 dB)
- Clip indicator: bright red, latches until reset

**Validation:**
- Resize plugin window → verify channel strips scale properly
- Change output routing → verify controls disable/enable instantly
- Adjust parameters → verify real-time audio updates
- Test with 2, 8, and 16 channel configurations

---

#### Step 4.2: FX Chain UI Components
**Files:** `Source/UI/FXButtonComponent.h`, `Source/UI/FXButtonComponent.cpp`, `Source/UI/FXChainComponent.h`, `Source/UI/FXChainComponent.cpp`

**Tasks:**
1. Create `FXButtonComponent` (compact effect toggle):
   - Display effect name
   - Power button (⏻) in corner toggles enabled state
   - Click effect name to open settings popover
   - Visual feedback: name lights up when enabled, border highlights
2. Create `FXChainComponent` (container for all FX):
   - Vertical stack of FX buttons
   - Fixed order: EQ → Saturation → Compressor
   - Manage popover lifecycle (only one open at a time)
3. Implement popovers for each effect:
   - **EQ Popover:** 10 vertical sliders (band frequencies labeled)
   - **Saturation Popover:** Mode dropdown + amount slider
   - **Compressor Popover:** Threshold, ratio, attack, release, makeup sliders
4. Position popovers near button (use `juce::CallOutBox` or custom)

**Popover Behavior:**
- Click FX button → show popover
- Click outside popover or click another FX → close current popover
- Parameter changes apply immediately (no "OK/Cancel")
- Popover state doesn't persist (reads current values on open)

**Validation:**
- Open EQ popover → adjust band → verify frequency response changes
- Toggle power button → verify bypass works (effect name dims)
- Open multiple popovers sequentially → verify only one shown at a time
- Test with different channel counts (verify FX per channel independent)

---

#### Step 4.3: Mixer Panel Layout
**Files:** `Source/UI/MixerPanel.h`, `Source/UI/MixerPanel.cpp`

**Tasks:**
1. Create `MixerPanel` component:
   - Horizontal scrollable container for channel strips
   - Master section on right (fixed position)
   - Dynamic number of channel strips based on kit
2. Implement channel strip instantiation:
   - Clear existing strips when kit changes
   - Create N `ChannelStripComponent` instances
   - Add to horizontal layout with spacing
3. Implement master section:
   - Wider channel strip (150px vs 80px for channels)
   - Label: "Master"
   - Master volume fader
   - Master peak meter
   - Master FX chain (includes limiter)
   - Clip indicator with reset button
4. Add horizontal scroll support:
   - Use `juce::Viewport` for scrolling
   - Ensure master section always visible (consider split layout)

**Layout Strategy:**
```
┌─────────────────────────────────────────────────┐
│ [Channel 1] [Channel 2] ... [Channel N] │ Master │
│                                          │        │
│            ← scrollable →                │ fixed  │
└─────────────────────────────────────────────────┘
```

**Validation:**
- Load 16-channel kit → verify horizontal scrolling works
- Resize window → verify master section always visible
- Switch from 2-channel to 16-channel kit → verify smooth transition
- Test on different screen sizes (1920x1080 down to 1280x720)

---

#### Step 4.4: Peak Metering Implementation
**Files:** `Source/UI/PeakMeter.h`, `Source/UI/PeakMeter.cpp`

**Tasks:**
1. Create `PeakMeter` component:
   - Vertical bar graph
   - Reads peak level from `PerChannelMixer` via timer callback
   - Implements smooth visual decay (slower than audio thread decay)
   - Color gradient: green → yellow → red
   - Clip indicator at top (latches, manual reset)
2. Implement `paint()`:
   - Draw background (dark gray/black)
   - Draw filled bar based on peak level
   - Draw clip indicator if clipped
   - Optional: dB scale markings (-∞, -12, -6, -3, 0)
3. Add timer callback (30-60 Hz):
   - Read `mixer.getChannelPeakLevel(channelIndex)`
   - Update display with smooth interpolation
   - Reset clip indicator on user action
4. Optimize rendering:
   - Cache gradient colors
   - Only repaint when level changes significantly (>0.1 dB)

**Metering Standards:**
```
  0 dBFS ────── Red (clip)
 -3 dBFS ────── Red
 -6 dBFS ────── Yellow
-12 dBFS ────── Yellow/Green
-∞  dBFS ────── Green
```

**Validation:**
- Play loud transient → verify clip indicator latches
- Play sustained tone → verify smooth meter movement
- Measure CPU usage (metering should be <1% with 16 channels)
- Compare to reference meter (Reaper's built-in) for accuracy

---

### Phase 5: State Management and Persistence
**Goal:** Ensure all mixer settings save/load correctly with project state.

#### Step 5.1: ValueTree State Serialization
**Files:** `Source/Core/PerChannelMixer.cpp` (implement `getState()`/`setState()`)

**Tasks:**
1. Implement `getState()`:
   - Create `ValueTree` with identifier "MixerState"
   - Store master volume and FX settings
   - For each channel:
     - Create child `ValueTree` with identifier "Channel"
     - Store: index, name, output destination, volume, pan, solo, mute
     - Store all FX parameters (EQ bands, saturation, compressor)
   - Return tree structure
2. Implement `setState()`:
   - Validate tree structure (check identifiers)
   - Restore master settings
   - For each channel in tree:
     - Match by index or name
     - Restore routing, volume, pan, solo, mute
     - Restore FX parameters
     - Update UI (if applicable)
3. Handle edge cases:
   - State has more channels than current kit → ignore extras
   - State has fewer channels than current kit → use defaults for missing
   - Invalid parameter values → clamp to valid range

**ValueTree Structure:**
```xml
<MixerState masterVolume="0.0" masterEQEnabled="true" ...>
  <Channel index="0" name="Kick In" outputDest="0" volume="0.0" pan="0.0" ...>
    ...
  </Channel>
  <Channel index="1" name="Snare Top" ...>
    ...
  </Channel>
  ...
</MixerState>
```

**Validation:**
- Configure mixer → save project → close → reload → verify exact state
- Load project with different kit → verify graceful parameter mapping
- Corrupt state file → verify defaults applied without crash

---

#### Step 5.2: Plugin Processor State Integration
**Files:** `Source/Plugin/PluginProcessor.cpp`

**Tasks:**
1. Override `getStateInformation()`:
   ```cpp
   void getStateInformation(MemoryBlock& destData) override
   {
       ValueTree state("FlamKitState");
       state.appendChild(perChannelMixer->getState(), nullptr);
       // Add other plugin state (kit path, MIDI mappings, etc.)

       MemoryOutputStream stream(destData, false);
       state.writeToStream(stream);
   }
   ```
2. Override `setStateInformation()`:
   ```cpp
   void setStateInformation(const void* data, int sizeInBytes) override
   {
       ValueTree state = ValueTree::readFromData(data, sizeInBytes);

       if (state.isValid())
       {
           ValueTree mixerState = state.getChildWithName("MixerState");
           if (mixerState.isValid())
               perChannelMixer->setState(mixerState);

           // Restore other plugin state
       }
   }
   ```
3. Ensure state restoration happens after kit loads:
   - Kit loading may change channel count
   - Defer mixer state restoration until after `onKitLoaded()`
4. Test preset system:
   - Save mixer preset (using JUCE preset mechanism)
   - Verify preset loading restores all settings

**State Restoration Order:**
```
1. Plugin loads
2. Kit path restored from state
3. Kit loads (triggers bus reconfiguration)
4. Mixer state restored (parameters applied)
5. UI updated to reflect state
```

**Validation:**
- Save project with complex mixer settings → reload → verify exact match
- Save preset → load different kit → load preset → verify graceful adaptation
- Test across DAWs (Reaper, Ableton, Logic) for compatibility

---

### Phase 6: Testing and Optimization
**Goal:** Validate real-time safety, performance, and cross-platform compatibility.

#### Step 6.1: Real-Time Safety Audit
**Files:** All `PerChannelMixer` and DSP processor files

**Tasks:**
1. Audit `process()` method for violations:
   - ✅ No `new`/`delete`/`malloc`/`free`
   - ✅ No `std::vector::push_back()` or dynamic resizing
   - ✅ No `std::mutex` locks (only atomics)
   - ✅ No file I/O or system calls
   - ✅ No `DBG()` or logging (move to non-RT thread)
2. Use static analysis tools:
   - **macOS:** Instruments (Allocations template) to detect RT thread allocations
   - **Linux:** Valgrind with `--track-origins=yes` to trace allocations
   - **Cross-platform:** RTSan (Real-Time Sanitizer, if available)
3. Run under stress test:
   - 128 simultaneous voices
   - All FX enabled on all channels
   - Rapid parameter changes from UI
   - Monitor for dropouts or glitches
4. Document findings and fix any violations

**Common Violations to Check:**
- String operations in audio thread (e.g., `juce::String` concatenation)
- `jassert()` in release builds (disabled, but check debug builds)
- Accidental `std::vector` reallocation from miscalculated size

**Validation:**
- Zero allocations detected in 10-minute stress test
- Stable performance under high CPU load
- No priority inversions or scheduling issues

---

#### Step 6.2: Performance Benchmarking
**Files:** Create `Tests/MixerPerformanceTest.cpp`

**Tasks:**
1. Create benchmark harness:
   - Load 16-channel kit
   - Configure all channels to Main Mix
   - Enable all FX on all channels
   - Process 10,000 blocks of 512 samples
   - Measure total CPU time
2. Test configurations:
   - **Baseline:** No FX, simple passthrough
   - **Light:** EQ only on 4 channels
   - **Medium:** EQ + Saturation on 8 channels
   - **Heavy:** All FX on all 16 channels + master FX
3. Compare against targets:
   - Baseline: <1% CPU (modern CPU @ 48 kHz, 64-sample buffer)
   - Heavy: <5% CPU
4. Profile hotspots using profiler:
   - **macOS:** Instruments (Time Profiler)
   - **Linux:** `perf` or Valgrind (Callgrind)
   - **Windows:** Visual Studio Profiler
5. Optimize bottlenecks (common candidates):
   - Filter coefficient updates (cache when unchanged)
   - SIMD-ify saturation processing
   - Reduce unnecessary buffer copies

**Target Performance:**
```
Configuration          | CPU Usage (64-sample buffer @ 48kHz)
-----------------------|-------------------------------------
16 channels, no FX     | <1%
16 channels, all FX    | <5%
16 channels, master FX | <2%
```

**Validation:**
- Meet or exceed performance targets on reference hardware (Intel i5/AMD Ryzen)
- No performance regressions on older hardware (test on 5-year-old laptop)
- Stable CPU usage (no gradual increase over time)

---

#### Step 6.3: Cross-Platform Testing
**Files:** All source files

**Tasks:**
1. Build and test on all target platforms:
   - **Linux:** Ubuntu 22.04 LTS (ALSA, PipeWire)
   - **macOS:** macOS 12+ (CoreAudio)
   - **Windows:** Windows 10/11 (ASIO, WASAPI)
2. Test in multiple DAWs per platform:
   - **Linux:** Reaper, Ardour, Qtractor
   - **macOS:** Logic Pro, Reaper, Ableton Live
   - **Windows:** Reaper, FL Studio, Cubase
3. Verify plugin format compatibility:
   - VST3: All platforms
   - AU: macOS only
   - AAX: All platforms (if AAX SDK available)
4. Test standalone build on all platforms
5. Validate multi-output routing:
   - Ensure all output buses visible in DAW
   - Verify bus names display correctly
   - Test routing changes persist on save/reload

**Platform-Specific Checks:**
- **macOS:** App notarization, code signing, AU validation
- **Windows:** ASIO latency, WASAPI exclusive mode
- **Linux:** PipeWire graph integration, JACK support

**Validation:**
- Identical behavior across all platforms (audio bit-accurate)
- No crashes or glitches on any platform
- UI scales properly on HiDPI/Retina displays

---

#### Step 6.4: Edge Case and Stress Testing
**Files:** All source files

**Tasks:**
1. Test extreme configurations:
   - 1-channel kit (mono)
   - 16-channel kit (max)
   - Switch from 2-channel to 16-channel mid-session
   - Rapidly load/unload kits (every 100ms)
2. Test parameter extremes:
   - All EQ bands at +12 dB (verify no clipping/distortion)
   - All compressors with 20:1 ratio and 0.1ms attack
   - Master limiter with -0.1 dB threshold on 0 dBFS input
3. Test UI edge cases:
   - Resize window to minimum size (verify scrollbars appear)
   - Automate 100+ parameters simultaneously
   - Open all FX popovers on all channels (verify layout)
4. Test automation:
   - Record complex automation (volume swells, pan sweeps)
   - Verify smooth playback without zipper noise
   - Test undo/redo of automated parameters
5. Test state management:
   - Save project mid-playback (verify no glitches)
   - Load project with invalid state (verify defaults)
   - Corrupt project file (verify graceful failure)

**Known Edge Cases to Handle:**
- Kit has 0 channels (should error gracefully)
- Kit metadata missing channel names (use defaults: "Channel 1", etc.)
- DAW doesn't support multi-output (fall back to Main Mix only)
- User tries to route channel to bus when standalone (disable bus routing UI)

**Validation:**
- No crashes on any edge case
- Graceful error messages for invalid configurations
- All edge cases documented in code comments

---

### Phase 7: Documentation and Polish
**Goal:** Finalize user-facing documentation and code comments.

#### Step 7.1: Code Documentation
**Files:** All header files

**Tasks:**
1. Add Doxygen-style comments to all public APIs:
   - Class descriptions
   - Method parameter documentation
   - Return value documentation
   - Thread safety notes (e.g., "Audio thread safe")
2. Document non-obvious design decisions:
   - Why constant-power panning (vs linear)
   - Why 100ms preload (vs other values)
   - Why EQ band frequencies chosen (ISO standard)
3. Add usage examples in header comments
4. Generate Doxygen documentation (optional, for contributors)

**Example Documentation:**
```cpp
/**
 * Sets the output destination for a specific channel.
 *
 * @param channelIndex Zero-based channel index (0 to N-1)
 * @param output Destination (MainMix or Bus1-16)
 *
 * Thread-safe: Can be called from UI thread while audio is processing.
 * Changes take effect on next audio block boundary.
 */
void setChannelOutput(int channelIndex, OutputDestination output);
```

**Validation:**
- All public APIs documented
- Doxygen builds without warnings
- Code comments helpful to contributors (not just restating code)

---

#### Step 7.2: User Manual Section
**Files:** Create `Docs/PerChannelMixer.md`

**Tasks:**
1. Write user-facing documentation:
   - Overview of mixer features
   - How to route channels to DAW tracks
   - How to use internal mixer (volume/pan/solo/mute)
   - How to apply FX (with screenshots)
   - Workflow examples (beginner, intermediate, advanced)
2. Create tutorial videos (optional):
   - "Quick Start: Internal Mixing"
   - "Advanced: Multi-Output Routing in Reaper"
   - "FX Chain Tutorial: Mixing a Kick Drum"
3. Add FAQ section:
   - "Why can't I adjust volume when routed to bus?"
   - "How do I reset a clipped channel?"
   - "Why does my DAW only show 2 outputs?" (multi-output setup)

**Example Workflows (from spec):**
- **Beginner:** Full internal mixing (all channels to Main Mix)
- **Intermediate:** Hybrid (kick/snare to DAW, rest to Main Mix)
- **Advanced:** Full DAW routing (all channels to individual buses)

**Validation:**
- Documentation reviewed by non-developer user
- All workflows tested step-by-step
- Screenshots match current UI design

---

#### Step 7.3: Final Polish
**Files:** All UI files

**Tasks:**
1. UI polish pass:
   - Align all controls to grid
   - Consistent spacing and padding
   - Professional color scheme (dark theme by default)
   - Smooth animations (fader movements, meter decay)
2. Performance polish:
   - Lazy UI updates (only repaint changed controls)
   - Debounce parameter changes (avoid flooding audio thread)
3. Error handling:
   - User-friendly error messages (no technical jargon)
   - Fallback behavior for unsupported features
4. Final QA pass:
   - Test all features end-to-end
   - Verify no regressions from earlier phases

**Visual Consistency Checklist:**
- All buttons same height
- All sliders same width (per type)
- Consistent font sizes and weights
- Matching corner radii on all components

**Validation:**
- UI reviewed by designer (or design-minded developer)
- Accessibility tested with screen reader (if supported)
- Performance matches targets from Phase 6.2

---

## Dependencies and Integrations

### Required Existing Components
- `FlamKitLoader` — provides kit metadata (channel count, names)
- `VoiceManager` — renders multi-channel sample output
- `FlamAudioProcessor` — plugin entry point and processBlock()

### New Components Created
- `Source/DSP/TenBandGraphicEQ.{h,cpp}`
- `Source/DSP/SaturationProcessor.{h,cpp}`
- `Source/DSP/CompressorProcessor.{h,cpp}`
- `Source/DSP/LimiterProcessor.{h,cpp}`
- `Source/Core/PerChannelMixer.{h,cpp}`
- `Source/UI/ChannelStripComponent.{h,cpp}`
- `Source/UI/FXButtonComponent.{h,cpp}`
- `Source/UI/FXChainComponent.{h,cpp}`
- `Source/UI/MixerPanel.{h,cpp}`
- `Source/UI/PeakMeter.{h,cpp}`

### Integration Points
1. **Kit Loading:** `FlamKitLoader::loadFromFile()` → `onKitLoaded()` → `mixer.setNumChannels()`
2. **Audio Rendering:** `VoiceManager::renderNextBlock()` → `mixer.process()` → output buses
3. **Parameter Binding:** JUCE `AudioProcessorValueTreeState` → `mixer.setChannelVolume()` etc.
4. **UI Updates:** Timer callbacks read `mixer.getChannelPeakLevel()` for metering

---

## Testing Checklist

### Functional Tests
- ✅ 2-channel kit loads and plays correctly
- ✅ 16-channel kit loads and plays correctly
- ✅ Switching kits reconfigures mixer without crashes
- ✅ Output routing changes take effect immediately
- ✅ Solo/mute logic works correctly (including edge cases)
- ✅ Volume/pan changes apply smoothly without zipper noise
- ✅ All FX process correctly (EQ, saturation, compressor, limiter)
- ✅ Master FX chain processes Main Mix only
- ✅ Peak metering accurate and responsive
- ✅ Clip indicators latch and reset properly

### Performance Tests
- ✅ CPU usage <1% with no FX
- ✅ CPU usage <5% with all FX enabled on 16 channels
- ✅ Zero allocations in audio thread (verified with Instruments)
- ✅ Stable latency under load (<5ms at 64-sample buffer)
- ✅ No priority inversions or thread stalls

### Cross-Platform Tests
- ✅ Builds on Linux, macOS, Windows
- ✅ VST3 works in Reaper, Ableton, Cubase
- ✅ AU works in Logic Pro, GarageBand
- ✅ Standalone app works on all platforms
- ✅ Multi-output routing visible in all tested DAWs

### DAW Integration Tests
- ✅ Bus names appear correctly in DAW
- ✅ Routing changes persist on project save/reload
- ✅ Automation records and plays back correctly
- ✅ Undo/redo works for all parameters
- ✅ Preset system works (save/load mixer state)

### Edge Case Tests
- ✅ 1-channel kit (mono)
- ✅ 16-channel kit (max)
- ✅ Rapid kit switching (stress test)
- ✅ All parameters at extremes (no crashes/artifacts)
- ✅ Corrupt project file loads gracefully
- ✅ DAW doesn't support multi-output (fallback to Main Mix)

---

## Success Criteria

The Per-Channel Mixer implementation is considered **complete** when:

1. ✅ All 7 phases implemented and tested
2. ✅ All functional tests passing
3. ✅ Performance targets met (<5% CPU with all FX)
4. ✅ Zero allocations in audio thread confirmed
5. ✅ Works on Linux, macOS, Windows (VST3, AU, Standalone)
6. ✅ Multi-output routing validated in 3+ DAWs per platform
7. ✅ State persistence working (save/load projects and presets)
8. ✅ User documentation complete
9. ✅ Code reviewed and approved by maintainer
10. ✅ No known bugs or crashes

---

## Risk Mitigation

### Potential Risks
1. **Performance:** FX processing exceeds CPU budget
   - **Mitigation:** Profile early and often, optimize hotspots, consider SIMD
2. **DAW Compatibility:** Multi-output not supported in some hosts
   - **Mitigation:** Graceful fallback to Main Mix, document limitations
3. **UI Complexity:** 16-channel mixer overwhelming to users
   - **Mitigation:** Scrollable layout, preset system, sane defaults
4. **Real-Time Safety:** Accidental allocation in audio thread
   - **Mitigation:** Regular audits with Instruments/Valgrind, code reviews
5. **State Management:** Complex ValueTree serialization bugs
   - **Mitigation:** Extensive save/load testing, version migration plan

---

## Future Enhancements (Post-v1.0)

These features are **not** part of the v1.0 implementation plan:

- Bus groups (assign multiple channels to same bus)
- Routing presets (save/load common configurations)
- Stereo buses (currently mono only)
- Bounce-in-place (export stems in standalone mode)
- Advanced FX (parametric EQ, multi-band compression, stereo widening)
- FX presets (per-instrument type)
- Visual FX feedback (gain reduction meters, spectrum analyzer)
- Sidechaining (one channel controls compression on another)

These will be tracked as separate feature specs and implementation plans.

---

## Estimated Timeline

**Experienced JUCE Developer:**
- Phase 1 (DSP Processors): 3-5 days
- Phase 2 (Mixer Core): 4-6 days
- Phase 3 (Plugin Integration): 2-3 days
- Phase 4 (UI): 5-7 days
- Phase 5 (State Management): 2-3 days
- Phase 6 (Testing/Optimization): 3-5 days
- Phase 7 (Documentation): 2-3 days

**Total: 21-32 days (3-5 weeks)**

**Note:** Timeline assumes single full-time developer familiar with JUCE and real-time audio programming. Adjust for team size and experience level.

---

## References

- **Spec Document:** `FeatureSpecs/PerChannelMixer.md`
- **JUCE Documentation:** https://juce.com/learn/documentation
- **JUCE Tutorial:** "Build an audio plugin" (multi-output buses)
- **Real-Time Safety:** Ross Bencina's "Time Waits for Nothing" article
- **Panning Laws:** Wikipedia "Panning (audio)" article

---

**Last Updated:** 2025-10-16
**Status:** Ready for Implementation
**Reviewed By:** [Pending]
