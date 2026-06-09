# Per-Channel Mixer Integration Guide

## Current Status

✅ **COMPLETE:**
- Phase 1: All DSP effect processors implemented and tested
- Phase 2: PerChannelMixer core class fully implemented

🔨 **IN PROGRESS:**
- Phase 3: Plugin integration (requires careful implementation)

⏳ **PENDING:**
- Phase 4: UI components
- Phase 5: State management hooks
- Phase 6: Testing and optimization

---

## Phase 3: Plugin Integration Steps

### Step 3.1: Add PerChannelMixer to PluginProcessor

#### 3.1.1: Update PluginProcessor.h

Add include and member variable:

```cpp
#include "../Core/PerChannelMixer.h"

// In FlamAudioProcessor class:
private:
    std::unique_ptr<PerChannelMixer> perChannelMixer;
```

#### 3.1.2: Update Constructor in PluginProcessor.cpp

Modify the `BusesProperties` to start with Main Mix output:

```cpp
FlamAudioProcessor::FlamAudioProcessor()
    : AudioProcessor(BusesProperties()
                        .withOutput("Main Mix", juce::AudioChannelSet::stereo(), true))
    , parameters(*this, nullptr, juce::Identifier("FLAM"), createParameterLayout())
{
    // Initialize mixer
    perChannelMixer = std::make_unique<PerChannelMixer>();

    // ... existing parameter initialization ...
}
```

#### 3.1.3: Update prepareToPlay()

```cpp
void FlamAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.prepareToPlay(sampleRate, samplesPerBlock);

    if (perChannelMixer)
        perChannelMixer->prepareToPlay(sampleRate, samplesPerBlock);

    updateEngineParameters();
}
```

---

### Step 3.2: Implement Kit Loading Integration

The mixer needs to know how many channels the current kit has. This requires hooking into the kit loading process.

#### Option A: Add callback to FlamEngine

In `FlamEngine.h`, add:

```cpp
public:
    std::function<void(int numChannels, const std::vector<juce::String>& channelNames)> onKitLoaded;
```

In `FlamEngine::loadKit()`, after loading kit metadata:

```cpp
void FlamEngine::loadKit(const juce::File& kitFile)
{
    // ... existing kit loading code ...

    // Extract channel info from loaded kit
    if (onKitLoaded)
    {
        std::vector<juce::String> channelNames;
        // Extract from kit metadata
        int numChannels = /* get from kit */;

        onKitLoaded(numChannels, channelNames);
    }
}
```

In `PluginProcessor`, register the callback:

```cpp
FlamAudioProcessor::FlamAudioProcessor()
    : // ... initialization ...
{
    // ... mixer initialization ...

    engine.onKitLoaded = [this](int numChannels, const std::vector<juce::String>& channelNames)
    {
        this->onKitLoaded(numChannels, channelNames);
    };
}

void FlamAudioProcessor::onKitLoaded(int numChannels, const std::vector<juce::String>& channelNames)
{
    suspendProcessing(true);

    // Configure mixer
    perChannelMixer->setNumChannels(numChannels, channelNames);
    perChannelMixer->prepareToPlay(getSampleRate(), getBlockSize());

    // Configure bus layout
    configureDynamicBuses(numChannels, channelNames);

    suspendProcessing(false);
}
```

---

### Step 3.3: Implement Dynamic Bus Configuration

Add method to `PluginProcessor`:

```cpp
void FlamAudioProcessor::configureDynamicBuses(int numChannels, const std::vector<juce::String>& channelNames)
{
    // Build new bus layout
    BusesLayout newLayout;

    // Bus 0: Main Mix (stereo)
    newLayout.outputBuses.add(juce::AudioChannelSet::stereo());

    // Bus 1-N: Individual channel outputs (mono)
    for (int i = 0; i < numChannels; ++i)
    {
        newLayout.outputBuses.add(juce::AudioChannelSet::mono());
    }

    // Apply layout
    if (!setBusesLayout(newLayout))
    {
        DBG("Warning: Failed to set bus layout for " << numChannels << " channels");
    }
}
```

Update `isBusesLayoutSupported()`:

```cpp
bool FlamAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Must have at least Main Mix (stereo) output
    if (layouts.outputBuses.isEmpty())
        return false;

    // First bus must be stereo (Main Mix)
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Additional buses should be mono (individual channels)
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        if (layouts.getChannelSet(false, i) != juce::AudioChannelSet::mono())
            return false;
    }

    return true;
}
```

---

### Step 3.4: Update ProcessBlock Integration

Modify `processBlock()` to route through mixer:

```cpp
void FlamAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Process MIDI and render voices through engine
    engine.processBlock(buffer, midiMessages);

    // If mixer is configured, process through it
    if (perChannelMixer && perChannelMixer->getNumChannels() > 0)
    {
        // Engine output is multi-channel (N mic channels)
        // Mixer routes to: Bus 0 (Main Mix stereo) + Bus 1-N (individual mono)

        // For now, just pass through to Main Mix
        // Full multi-output routing requires engine modifications
        perChannelMixer->process(buffer, buffer, buffer.getNumSamples());
    }
}
```

**Note:** The engine currently outputs stereo. For full multi-channel routing, the engine would need to output N-channel audio (one per microphone), which requires deeper integration with VoiceManager and sample playback.

---

### Step 3.5: Add Basic Parameter Bindings (Master Only)

For initial integration, add master mixer parameters:

In `createParameterLayout()`:

```cpp
// Master Mixer parameters
layout.add(std::make_unique<juce::AudioParameterFloat>(
    "mixer_master_volume", "Master Volume",
    juce::NormalisableRange<float>(-96.0f, 6.0f, 0.1f),
    0.0f, "dB"));

layout.add(std::make_unique<juce::AudioParameterBool>(
    "mixer_master_eq_enabled", "Master EQ",
    false));

layout.add(std::make_unique<juce::AudioParameterBool>(
    "mixer_master_limiter_enabled", "Master Limiter",
    false));
```

In `updateEngineParameters()`:

```cpp
// Update mixer parameters
if (perChannelMixer)
{
    if (auto* param = parameters.getParameter("mixer_master_volume"))
        perChannelMixer->setMasterVolume(param->getValue());

    if (auto* param = parameters.getParameter("mixer_master_eq_enabled"))
        perChannelMixer->setMasterEQEnabled(param->getValue() > 0.5f);

    if (auto* param = parameters.getParameter("mixer_master_limiter_enabled"))
        perChannelMixer->setMasterLimiterEnabled(param->getValue() > 0.5f);
}
```

**Full parameter binding (16 channels × all FX) requires hundreds of parameters and should be added incrementally.**

---

## Phase 4: UI Components (Future Work)

### Components to Implement:

1. **MixerPanel** (Source/UI/MixerPanel.h/cpp)
   - Container for all channel strips + master section
   - Horizontal scrolling layout
   - Instantiates N `ChannelStripComponent`s based on kit

2. **ChannelStripComponent** (Source/UI/ChannelStripComponent.h/cpp)
   - Name label
   - Output selector dropdown (Main Mix, Bus 1-16)
   - Volume slider, pan knob
   - Solo/mute buttons
   - Peak meter
   - FX chain panel

3. **FXChainComponent** (Source/UI/FXChainComponent.h/cpp)
   - Vertical stack of FX buttons
   - EQ, Saturation, Compressor buttons
   - Power toggles and settings popovers

4. **PeakMeter** (Source/UI/PeakMeter.h/cpp)
   - Vertical level indicator
   - Color gradient (green → yellow → red)
   - Clip indicator

5. **Main UI Tab Integration**
   - Add "Mixer" tab to main interface
   - Show/hide MixerPanel based on tab selection

---

## Phase 5: State Management (Future Work)

### Integration with Plugin State:

Update `getStateInformation()`:

```cpp
void FlamAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state("FlamKitState");

    // Add mixer state
    if (perChannelMixer)
        state.appendChild(perChannelMixer->getState(), nullptr);

    // Add existing plugin state
    state.appendChild(parameters.copyState(), nullptr);

    juce::MemoryOutputStream stream(destData, false);
    state.writeToStream(stream);
}
```

Update `setStateInformation()`:

```cpp
void FlamAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    juce::ValueTree state = juce::ValueTree::readFromData(data, sizeInBytes);

    if (state.isValid())
    {
        // Restore mixer state
        juce::ValueTree mixerState = state.getChildWithName("MixerState");
        if (mixerState.isValid() && perChannelMixer)
            perChannelMixer->setState(mixerState);

        // Restore parameters
        juce::ValueTree paramState = state.getChildWithName("FLAM");
        if (paramState.isValid())
            parameters.replaceState(paramState);
    }
}
```

---

## Phase 6: Testing and Optimization (Future Work)

### Testing Checklist:

- [ ] Load 2-channel kit → verify 3 output buses (Main Mix + 2 mono)
- [ ] Load 16-channel kit → verify 17 output buses
- [ ] Switch kits mid-session → verify smooth reconfiguration
- [ ] Route channels to different buses → verify correct audio routing
- [ ] Enable FX on channels → verify processing and CPU usage
- [ ] Automate parameters in DAW → verify smooth changes
- [ ] Save/load project → verify state persistence
- [ ] Test in multiple DAWs (Reaper, Ableton, Logic)

### Performance Targets:

- **Baseline (no FX):** <1% CPU
- **Full FX (16 channels):** <5% CPU
- **Latency:** <5ms at 64-sample buffer
- **Memory:** ~36 KB for FX buffers (per mixer instance)

---

## Build System Integration

Ensure CMakeLists.txt includes new files:

```cmake
target_sources(FLAM PRIVATE
    # ... existing sources ...

    # DSP Processors
    Source/DSP/TenBandGraphicEQ.h
    Source/DSP/TenBandGraphicEQ.cpp
    Source/DSP/SaturationProcessor.h
    Source/DSP/SaturationProcessor.cpp
    Source/DSP/CompressorProcessor.h
    Source/DSP/CompressorProcessor.cpp
    Source/DSP/LimiterProcessor.h
    Source/DSP/LimiterProcessor.cpp

    # Mixer Core
    Source/Core/PerChannelMixer.h
    Source/Core/PerChannelMixer.cpp

    # UI Components (when implemented)
    # Source/UI/MixerPanel.h
    # Source/UI/MixerPanel.cpp
    # Source/UI/ChannelStripComponent.h
    # Source/UI/ChannelStripComponent.cpp
    # ... etc
)
```

---

## Summary

**Completed:**
- ✅ All DSP effect processors (Phase 1)
- ✅ PerChannelMixer core class (Phase 2)

**Next Steps:**
1. Add mixer member to PluginProcessor
2. Implement kit loading callback
3. Configure dynamic output buses
4. Update processBlock() routing
5. Add basic parameter bindings
6. Test with simple 2-channel kit

**Future Work:**
- Complete UI implementation (Phase 4)
- Full parameter automation (hundreds of parameters)
- State persistence integration (Phase 5)
- Cross-platform testing (Phase 6)

The foundation is solid and ready for integration. The remaining work can be done incrementally with testing at each step.
