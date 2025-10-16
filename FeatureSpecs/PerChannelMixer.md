# Per-Channel Mixer with Multi-Output Routing

**Status:** v1.0 MVP Core Feature
**Priority:** Critical
**Dependencies:** `FlamKitLoader`, `VoiceManager`

---

## Overview

Implement integrated per-channel mixer with flexible multi-output routing. Each microphone channel can be:
- Routed to **Main Mix** (internal stereo mixer with volume/pan/effects)
- Routed to **individual DAW output buses** (direct routing, bypass internal mixer)

UI provides per-channel output selection dropdown (familiar workflow from Kontakt/Superior Drummer), allowing users to choose between internal mixing or DAW-based mixing on a per-channel basis.

---

## Technical Requirements

### Core Functionality

1. **Dynamic Channel Strips**
   - Detect number of mic channels when kit loads (1-16)
   - Instantiate one channel strip per microphone
   - Label each strip with mic name from `flamkit.yaml`

2. **Per-Channel Output Routing**
   - **Output Selector**: Dropdown on each channel strip
     - "Main Mix" (default) → internal processing
     - "Bus 1" through "Bus 16" → direct DAW routing
   - Channels routed to Main Mix use internal volume/pan/effects
   - Channels routed to buses bypass internal processing

3. **Per-Channel Controls** (Active only when routed to Main Mix)
   - **Volume**: -∞ to +6 dB (default 0 dB)
   - **Pan**: Hard left to hard right (default center)
   - **Solo**: Mute all other channels routed to Main Mix
   - **Mute**: Silence this channel
   - **Peak Meter**: Real-time level indication with clip detection
   - **FX Chain**: Fixed-order effects with toggleable bypass (only when routed to Main Mix)
     - 10-Band Graphic EQ (default off)
     - Saturation/Clipping (default off)
     - Compressor (default off)

4. **Master Section**
   - Master volume fader (for Main Mix only)
   - Master peak meter
   - Clip indicator with reset button
   - **Master FX Chain**: Same as channel FX plus Limiter
     - 10-Band Graphic EQ (default off)
     - Saturation/Clipping (default off)
     - Compressor (default off)
     - Limiter (default off)

5. **Dynamic Bus Configuration**
   - Automatically create DAW output buses based on kit channel count
   - Bus names from `flamkit.yaml` metadata
   - Reconfigure buses when different kit loaded

---

## FlamKit YAML Channel Configuration

### Channel Metadata Structure

Each microphone channel in a FlamKit must be defined in `flamkit.yaml` with a name and index. The mixer uses this metadata to automatically configure channel strips with proper labels.

```yaml
# Kit Metadata
name: "Studio Rock Kit"
version: "1.0.0"
author: "John Drummer"
sampleRate: 48000

# Microphone Channel Definitions
# CRITICAL: Order must match channel order in WAV files (0-indexed)
# These names become mixer channel strip labels
channels:
  - name: "Kick In"
    index: 0
  - name: "Kick Out"
    index: 1
  - name: "Snare Top"
    index: 2
  - name: "Snare Btm"
    index: 3
  - name: "HH Close"
    index: 4
  - name: "Tom 1"
    index: 5
  - name: "Tom 2"
    index: 6
  - name: "OH-L"
    index: 7
  - name: "OH-R"
    index: 8
  - name: "Room-L"
    index: 9
  - name: "Room-R"
    index: 10

# Drum Pieces (reference the channels above)
pieces:
  - name: "Kick"
    midiNote: 36
    velocityLayers:
      - velocityRange: [0, 127]
        roundRobins:
          - file: "Samples/Kick/kick_v064_rr0.wav"  # This WAV has 11 channels (0-10)
```

### Channel Configuration Rules

1. **Index Sequencing**: Channel indices must be sequential starting from 0
2. **Name Length**: Channel names should be ≤16 characters for optimal UI display
3. **WAV Channel Count**: All sample WAV files must have the same number of channels as defined in `channels` array
4. **Naming Conventions**:
   - **Close Mics**: "Kick In", "Kick Out", "Snare Top", "Snare Btm", "HH Close"
   - **Overheads**: "OH-L", "OH-R" (stereo pair)
   - **Room Mics**: "Room-L", "Room-R", "Ambient-L", "Ambient-R"
   - **Effect Mics**: "Crush", "Dist", "Reverb"
   - **Toms**: "Tom 1", "Tom 2", "Tom 3", "Floor Tom"

### Common Channel Configurations

**Minimal Stereo (2 channels)**:
```yaml
channels:
  - name: "L"
    index: 0
  - name: "R"
    index: 1
```

**Standard Rock Kit (8 channels)**:
```yaml
channels:
  - name: "Kick Close"
    index: 0
  - name: "Snare Top"
    index: 1
  - name: "Hi-Hat"
    index: 2
  - name: "Toms"
    index: 3
  - name: "OH-L"
    index: 4
  - name: "OH-R"
    index: 5
  - name: "Room-L"
    index: 6
  - name: "Room-R"
    index: 7
```

**Professional Studio Kit (16 channels)**:
```yaml
channels:
  - name: "Kick In"
    index: 0
  - name: "Kick Out"
    index: 1
  - name: "Kick Sub"
    index: 2
  - name: "Snare Top"
    index: 3
  - name: "Snare Btm"
    index: 4
  - name: "HH Close"
    index: 5
  - name: "Tom 1"
    index: 6
  - name: "Tom 2"
    index: 7
  - name: "Tom 3"
    index: 8
  - name: "Floor Tom"
    index: 9
  - name: "OH-L"
    index: 10
  - name: "OH-R"
    index: 11
  - name: "Room-L"
    index: 12
  - name: "Room-R"
    index: 13
  - name: "Ambient"
    index: 14
  - name: "Crush"
    index: 15
```

### Automatic Mixer Configuration

When a kit loads, FlamKit automatically:

1. **Parses `flamkit.yaml`** to extract channel count and names
2. **Validates WAV files** have matching channel count
3. **Configures mixer** with exact number of channel strips
4. **Labels strips** using channel names from YAML
5. **Creates output buses** in DAW (Main Mix + optional individual buses)

This ensures the mixer UI always matches the kit's microphone configuration without manual setup.

---

## Implementation Details

### Class Structure

```cpp
// Source/Core/PerChannelMixer.h
class PerChannelMixer
{
public:
    PerChannelMixer();

    // Configuration (called when kit loads)
    void setNumChannels(int numChannels, const std::vector<juce::String>& channelNames);

    // Output routing
    enum class OutputDestination
    {
        MainMix = 0,  // Internal mixer processing
        Bus1, Bus2, Bus3, Bus4, Bus5, Bus6, Bus7, Bus8,
        Bus9, Bus10, Bus11, Bus12, Bus13, Bus14, Bus15, Bus16
    };

    void setChannelOutput(int channelIndex, OutputDestination output);
    OutputDestination getChannelOutput(int channelIndex) const;

    // Per-channel controls (thread-safe, only apply when output == MainMix)
    void setChannelVolume(int channelIndex, float volumeDb);
    void setChannelPan(int channelIndex, float pan);  // -1.0 (left) to +1.0 (right)
    void setChannelSolo(int channelIndex, bool solo);
    void setChannelMute(int channelIndex, bool mute);

    float getChannelVolume(int channelIndex) const;
    float getChannelPan(int channelIndex) const;
    bool isChannelSolo(int channelIndex) const;
    bool isChannelMute(int channelIndex) const;

    // Per-channel FX controls (only apply when output == MainMix)
    void setChannelEQEnabled(int channelIndex, bool enabled);
    void setChannelEQBandGain(int channelIndex, int bandIndex, float gainDb);  // bandIndex 0-9
    void setChannelSaturationEnabled(int channelIndex, bool enabled);
    void setChannelSaturationAmount(int channelIndex, float amount);  // 0.0 to 1.0
    void setChannelSaturationMode(int channelIndex, int mode);  // 0=Tape, 1=Tube, 2=Digital
    void setChannelCompressorEnabled(int channelIndex, bool enabled);
    void setChannelCompressorThreshold(int channelIndex, float thresholdDb);
    void setChannelCompressorRatio(int channelIndex, float ratio);
    void setChannelCompressorAttack(int channelIndex, float attackMs);
    void setChannelCompressorRelease(int channelIndex, float releaseMs);
    void setChannelCompressorMakeupGain(int channelIndex, float gainDb);

    bool isChannelEQEnabled(int channelIndex) const;
    float getChannelEQBandGain(int channelIndex, int bandIndex) const;
    bool isChannelSaturationEnabled(int channelIndex) const;
    float getChannelSaturationAmount(int channelIndex) const;
    int getChannelSaturationMode(int channelIndex) const;
    bool isChannelCompressorEnabled(int channelIndex) const;
    float getChannelCompressorThreshold(int channelIndex) const;
    float getChannelCompressorRatio(int channelIndex) const;
    float getChannelCompressorAttack(int channelIndex) const;
    float getChannelCompressorRelease(int channelIndex) const;
    float getChannelCompressorMakeupGain(int channelIndex) const;

    // Master controls (for Main Mix)
    void setMasterVolume(float volumeDb);
    float getMasterVolume() const;

    // Master FX controls (for Main Mix)
    void setMasterEQEnabled(bool enabled);
    void setMasterEQBandGain(int bandIndex, float gainDb);
    void setMasterSaturationEnabled(bool enabled);
    void setMasterSaturationAmount(float amount);
    void setMasterSaturationMode(int mode);
    void setMasterCompressorEnabled(bool enabled);
    void setMasterCompressorThreshold(float thresholdDb);
    void setMasterCompressorRatio(float ratio);
    void setMasterCompressorAttack(float attackMs);
    void setMasterCompressorRelease(float releaseMs);
    void setMasterCompressorMakeupGain(float gainDb);
    void setMasterLimiterEnabled(bool enabled);
    void setMasterLimiterThreshold(float thresholdDb);
    void setMasterLimiterRelease(float releaseMs);

    bool isMasterEQEnabled() const;
    float getMasterEQBandGain(int bandIndex) const;
    bool isMasterSaturationEnabled() const;
    float getMasterSaturationAmount() const;
    int getMasterSaturationMode() const;
    bool isMasterCompressorEnabled() const;
    float getMasterCompressorThreshold() const;
    float getMasterCompressorRatio() const;
    float getMasterCompressorAttack() const;
    float getMasterCompressorRelease() const;
    float getMasterCompressorMakeupGain() const;
    bool isMasterLimiterEnabled() const;
    float getMasterLimiterThreshold() const;
    float getMasterLimiterRelease() const;

    // Processing (audio thread)
    void process(
        const AudioBuffer<float>& multiChannelInput,  // All mic channels
        AudioBuffer<float>& allOutputBuses,            // Bus 0 = Main Mix, Bus 1-16 = DAW outputs
        int numSamples
    );

    // Metering
    float getChannelPeakLevel(int channelIndex) const;
    float getMasterPeakLevel() const;
    void resetClipIndicators();

    // Bus info (for DAW integration)
    int getNumRequiredOutputBuses() const;  // 1 (Main Mix) + number of unique bus assignments
    juce::String getBusName(int busIndex) const;

    // State persistence
    juce::ValueTree getState() const;
    void setState(const juce::ValueTree& state);

private:
    // DSP Effect Processors
    struct TenBandGraphicEQ
    {
        static constexpr int NUM_BANDS = 10;
        static constexpr float BAND_FREQUENCIES[NUM_BANDS] = {
            31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
        };

        std::atomic<bool> enabled{false};
        std::atomic<float> bandGains[NUM_BANDS] = {0.0f};  // -12 to +12 dB per band

        juce::dsp::IIR::Filter<float> filters[NUM_BANDS];
        double sampleRate{44100.0};

        void prepareToPlay(double sr, int maximumBlockSize);
        void process(juce::AudioBuffer<float>& buffer, int numSamples);
        void updateCoefficients();
    };

    struct SaturationProcessor
    {
        enum Mode { Tape = 0, Tube = 1, Digital = 2 };

        std::atomic<bool> enabled{false};
        std::atomic<float> amount{0.5f};  // 0.0 to 1.0
        std::atomic<int> mode{Tape};

        void prepareToPlay(double sr) { /* No state to initialize */ }
        void process(juce::AudioBuffer<float>& buffer, int numSamples);
    };

    struct CompressorProcessor
    {
        std::atomic<bool> enabled{false};
        std::atomic<float> thresholdDb{-10.0f};
        std::atomic<float> ratio{4.0f};
        std::atomic<float> attackMs{5.0f};
        std::atomic<float> releaseMs{100.0f};
        std::atomic<float> makeupGainDb{0.0f};

        float envelope{0.0f};
        double sampleRate{44100.0};

        void prepareToPlay(double sr);
        void process(juce::AudioBuffer<float>& buffer, int numSamples);
    };

    struct LimiterProcessor
    {
        std::atomic<bool> enabled{false};
        std::atomic<float> thresholdDb{-0.1f};  // Just below 0 dBFS
        std::atomic<float> releaseMs{50.0f};

        float envelope{0.0f};
        double sampleRate{44100.0};

        void prepareToPlay(double sr);
        void process(juce::AudioBuffer<float>& buffer, int numSamples);
    };

    struct ChannelStrip
    {
        juce::String name;

        // Routing
        std::atomic<int> outputDestination{0};  // 0 = MainMix, 1-16 = Bus 1-16

        // Mixer controls (only active if outputDestination == 0)
        std::atomic<float> volumeDb{0.0f};
        std::atomic<float> volumeGain{1.0f};  // Linear gain (computed from dB)
        std::atomic<float> pan{0.0f};
        std::atomic<bool> solo{false};
        std::atomic<bool> mute{false};

        // FX Chain (only active if outputDestination == 0)
        TenBandGraphicEQ eq;
        SaturationProcessor saturation;
        CompressorProcessor compressor;

        // Metering
        std::atomic<float> peakLevel{0.0f};
        std::atomic<bool> clipped{false};
    };

    std::vector<ChannelStrip> channels;

    // Master section (Main Mix only)
    std::atomic<float> masterVolumeDb{0.0f};
    std::atomic<float> masterVolumeGain{1.0f};
    std::atomic<float> masterPeakLevel{0.0f};
    std::atomic<bool> masterClipped{false};

    // Master FX Chain (Main Mix only)
    TenBandGraphicEQ masterEQ;
    SaturationProcessor masterSaturation;
    CompressorProcessor masterCompressor;
    LimiterProcessor masterLimiter;

    // Pre-allocated buffers for zero-allocation audio processing
    std::vector<AudioBuffer<float>> channelFXBuffers;  // One mono buffer per channel
    AudioBuffer<float> masterFXBuffer;  // Stereo buffer for master processing

    // Helper functions
    float dbToGain(float db) const;
    float gainToDb(float gain) const;
    void updateVolumeGain(int channelIndex);
    void prepareToPlay(double sampleRate, int maximumBlockSize);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PerChannelMixer)
};
```

### Initialization and Buffer Allocation

```cpp
void PerChannelMixer::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    // Prepare master FX
    masterEQ.prepareToPlay(sampleRate, maximumBlockSize);
    masterSaturation.prepareToPlay(sampleRate);
    masterCompressor.prepareToPlay(sampleRate);
    masterLimiter.prepareToPlay(sampleRate);

    // Allocate master FX buffer (stereo)
    masterFXBuffer.setSize(2, maximumBlockSize, false, false, true);

    // Prepare channel FX
    for (auto& channel : channels)
    {
        channel.eq.prepareToPlay(sampleRate, maximumBlockSize);
        channel.saturation.prepareToPlay(sampleRate);
        channel.compressor.prepareToPlay(sampleRate);
    }

    // Allocate per-channel FX buffers (mono)
    channelFXBuffers.clear();
    for (size_t i = 0; i < channels.size(); ++i)
    {
        AudioBuffer<float> buffer(1, maximumBlockSize);
        channelFXBuffers.push_back(std::move(buffer));
    }
}

void PerChannelMixer::setNumChannels(int numChannels, const std::vector<juce::String>& channelNames)
{
    channels.clear();
    channels.reserve(numChannels);

    for (int i = 0; i < numChannels; ++i)
    {
        ChannelStrip strip;
        strip.name = (i < channelNames.size()) ? channelNames[i] : "Channel " + juce::String(i + 1);
        channels.push_back(std::move(strip));
    }

    // Note: Call prepareToPlay() after this to allocate FX buffers
}
```

### Audio Processing (Real-Time Safe)

```cpp
void PerChannelMixer::process(
    const AudioBuffer<float>& multiChannelInput,
    AudioBuffer<float>& allOutputBuses,
    int numSamples)
{
    // Clear all output buses
    allOutputBuses.clear(0, numSamples);

    // Check if any Main Mix channels are soloed
    bool anySoloed = false;
    for (const auto& channel : channels)
    {
        if (channel.outputDestination.load() == 0 && channel.solo.load())
        {
            anySoloed = true;
            break;
        }
    }

    // Process each channel
    for (size_t chIdx = 0; chIdx < channels.size(); ++chIdx)
    {
        if (chIdx >= multiChannelInput.getNumChannels())
            break;

        const auto& channel = channels[chIdx];
        const int outputDest = channel.outputDestination.load();

        if (outputDest == 0)
        {
            // Route to Main Mix (bus 0) with internal processing
            processChannelToMainMix(chIdx, channel, multiChannelInput, allOutputBuses, numSamples, anySoloed);
        }
        else
        {
            // Route directly to DAW bus (bypass internal mixer)
            routeChannelToBus(chIdx, outputDest, multiChannelInput, allOutputBuses, numSamples);
        }
    }

    // Use pre-allocated stereo buffer for master FX processing (Main Mix only)
    // Clear only the portion we'll use
    masterFXBuffer.clear(0, numSamples);

    // Copy Main Mix to master FX buffer
    masterFXBuffer.copyFrom(0, 0, allOutputBuses, 0, 0, numSamples);  // L
    masterFXBuffer.copyFrom(1, 0, allOutputBuses, 1, 0, numSamples);  // R

    // Process Master FX chain in order: EQ → Saturation → Compressor → Limiter
    if (masterEQ.enabled.load())
        masterEQ.process(masterFXBuffer, numSamples);

    if (masterSaturation.enabled.load())
        masterSaturation.process(masterFXBuffer, numSamples);

    if (masterCompressor.enabled.load())
        masterCompressor.process(masterFXBuffer, numSamples);

    if (masterLimiter.enabled.load())
        masterLimiter.process(masterFXBuffer, numSamples);

    // Apply master volume to Main Mix
    const float masterGain = masterVolumeGain.load();
    masterFXBuffer.applyGain(0, numSamples, masterGain);

    // Copy processed master back to output
    allOutputBuses.copyFrom(0, 0, masterFXBuffer, 0, 0, numSamples);  // L
    allOutputBuses.copyFrom(1, 0, masterFXBuffer, 1, 0, numSamples);  // R

    // Update master peak meter
    float masterPeakThisBlock = std::max(
        masterFXBuffer.getMagnitude(0, 0, numSamples),
        masterFXBuffer.getMagnitude(1, 0, numSamples)
    );

    float currentMasterPeak = masterPeakLevel.load();
    const float decayFactor = 0.9995f;
    float newMasterPeak = std::max(masterPeakThisBlock, currentMasterPeak * decayFactor);

    if (newMasterPeak > 1.0f)
        masterClipped.store(true);

    masterPeakLevel.store(newMasterPeak);
}

void PerChannelMixer::processChannelToMainMix(
    size_t chIdx,
    ChannelStrip& channel,
    const AudioBuffer<float>& multiChannelInput,
    AudioBuffer<float>& allOutputBuses,
    int numSamples,
    bool anySoloed)
{
    // Determine if this channel should be audible
    bool audible = true;

    if (channel.mute.load())
        audible = false;

    if (anySoloed && !channel.solo.load())
        audible = false;

    if (!audible)
        return;

    // Use pre-allocated mono buffer for FX processing
    auto& channelBuffer = channelFXBuffers[chIdx];
    channelBuffer.clear(0, numSamples);  // Clear only the portion we'll use
    channelBuffer.copyFrom(0, 0, multiChannelInput, chIdx, 0, numSamples);

    // Process FX chain in order: EQ → Saturation → Compressor
    if (channel.eq.enabled.load())
        channel.eq.process(channelBuffer, numSamples);

    if (channel.saturation.enabled.load())
        channel.saturation.process(channelBuffer, numSamples);

    if (channel.compressor.enabled.load())
        channel.compressor.process(channelBuffer, numSamples);

    // Get channel parameters
    const float gain = channel.volumeGain.load();
    const float pan = channel.pan.load();

    // Calculate stereo pan gains (constant power panning)
    const float panAngle = (pan + 1.0f) * juce::MathConstants<float>::pi / 4.0f;
    const float leftGain = std::cos(panAngle) * gain;
    const float rightGain = std::sin(panAngle) * gain;

    // Add channel to Main Mix (bus 0)
    const float* processedData = channelBuffer.getReadPointer(0);
    float* leftOutput = allOutputBuses.getWritePointer(0);   // Main Mix L
    float* rightOutput = allOutputBuses.getWritePointer(1);  // Main Mix R

    float peakThisBlock = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float sample = processedData[i];
        leftOutput[i] += sample * leftGain;
        rightOutput[i] += sample * rightGain;

        peakThisBlock = std::max(peakThisBlock, std::abs(sample));
    }

    // Update peak meter (with decay)
    float currentPeak = channel.peakLevel.load();
    const float decayFactor = 0.9995f;
    float newPeak = std::max(peakThisBlock, currentPeak * decayFactor);

    if (newPeak > 1.0f)
        channel.clipped.store(true);

    channel.peakLevel.store(newPeak);
}

void PerChannelMixer::routeChannelToBus(
    size_t chIdx,
    int busIndex,
    const AudioBuffer<float>& multiChannelInput,
    AudioBuffer<float>& allOutputBuses,
    int numSamples)
{
    // Calculate output channel offset for this bus
    // Bus 0 = channels 0-1 (Main Mix stereo)
    // Bus 1 = channel 2 (mono)
    // Bus 2 = channel 3 (mono)
    // etc.
    const int outputChannelIndex = 2 + (busIndex - 1);  // Bus 1 → channel 2, Bus 2 → channel 3, etc.

    if (outputChannelIndex >= allOutputBuses.getNumChannels())
        return;  // Safety check

    // Copy channel directly to output bus (no processing)
    const float* inputData = multiChannelInput.getReadPointer(chIdx);
    float* outputData = allOutputBuses.getWritePointer(outputChannelIndex);

    float peakThisBlock = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        outputData[i] = inputData[i];
        peakThisBlock = std::max(peakThisBlock, std::abs(inputData[i]));
    }

    // Update peak meter
    float currentPeak = channels[chIdx].peakLevel.load();
    const float decayFactor = 0.9995f;
    float newPeak = std::max(peakThisBlock, currentPeak * decayFactor);

    if (newPeak > 1.0f)
        channels[chIdx].clipped.store(true);

    channels[chIdx].peakLevel.store(newPeak);
}
```

---

## DSP Effect Implementations

### 10-Band Graphic EQ

```cpp
void PerChannelMixer::TenBandGraphicEQ::prepareToPlay(double sr, int maximumBlockSize)
{
    sampleRate = sr;

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        filters[i].reset();
    }

    updateCoefficients();
}

void PerChannelMixer::TenBandGraphicEQ::updateCoefficients()
{
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        const float frequency = BAND_FREQUENCIES[i];
        const float gainDb = bandGains[i].load();
        const float q = 1.0f / std::sqrt(2.0f);  // Butterworth Q

        // Create peaking filter coefficients
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate,
            frequency,
            q,
            juce::Decibels::decibelsToGain(gainDb)
        );

        *filters[i].coefficients = *coeffs;
    }
}

void PerChannelMixer::TenBandGraphicEQ::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!enabled.load())
        return;

    // Process each band filter in series
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        filters[i].process(context);
    }
}
```

### Saturation Processor

```cpp
void PerChannelMixer::SaturationProcessor::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!enabled.load())
        return;

    const float amt = amount.load();
    const Mode satMode = static_cast<Mode>(mode.load());

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float input = data[i];
            float output = 0.0f;

            switch (satMode)
            {
                case Tape:
                    // Soft clipping with asymmetric curve (tape-style)
                    output = std::tanh(input * (1.0f + amt * 4.0f));
                    break;

                case Tube:
                    // Tube-style saturation (odd harmonics)
                    output = input * (1.0f + amt * std::abs(input));
                    output = std::tanh(output);
                    break;

                case Digital:
                    // Hard clipping
                    output = juce::jlimit(-1.0f - amt * 0.1f, 1.0f + amt * 0.1f, input);
                    break;
            }

            // Wet/dry blend
            data[i] = input * (1.0f - amt) + output * amt;
        }
    }
}
```

### Compressor Processor

```cpp
void PerChannelMixer::CompressorProcessor::prepareToPlay(double sr)
{
    sampleRate = sr;
    envelope = 0.0f;
}

void PerChannelMixer::CompressorProcessor::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!enabled.load())
        return;

    const float threshold = thresholdDb.load();
    const float ratio = this->ratio.load();
    const float attack = attackMs.load() / 1000.0f;
    const float release = releaseMs.load() / 1000.0f;
    const float makeupGain = std::pow(10.0f, makeupGainDb.load() / 20.0f);

    const float attackCoeff = std::exp(-1.0f / (attack * sampleRate));
    const float releaseCoeff = std::exp(-1.0f / (release * sampleRate));

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = data[i];
            const float inputLevelDb = 20.0f * std::log10(std::abs(input) + 1e-6f);

            // Envelope follower
            if (inputLevelDb > envelope)
                envelope = attackCoeff * envelope + (1.0f - attackCoeff) * inputLevelDb;
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * inputLevelDb;

            // Gain reduction calculation
            float gainReductionDb = 0.0f;
            if (envelope > threshold)
            {
                gainReductionDb = (envelope - threshold) * (1.0f - 1.0f / ratio);
            }

            const float gain = std::pow(10.0f, -gainReductionDb / 20.0f) * makeupGain;

            // Apply gain
            data[i] = input * gain;
        }
    }
}
```

### Limiter Processor

```cpp
void PerChannelMixer::LimiterProcessor::prepareToPlay(double sr)
{
    sampleRate = sr;
    envelope = 0.0f;
}

void PerChannelMixer::LimiterProcessor::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!enabled.load())
        return;

    const float threshold = thresholdDb.load();
    const float release = releaseMs.load() / 1000.0f;
    const float releaseCoeff = std::exp(-1.0f / (release * sampleRate));

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = data[i];
            const float inputLevelDb = 20.0f * std::log10(std::abs(input) + 1e-6f);

            // Envelope follower with instant attack
            if (inputLevelDb > envelope)
                envelope = inputLevelDb;  // Instant attack for limiter
            else
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * inputLevelDb;

            // Hard limiting (infinite ratio)
            float gainReductionDb = 0.0f;
            if (envelope > threshold)
            {
                gainReductionDb = envelope - threshold;
            }

            const float gain = std::pow(10.0f, -gainReductionDb / 20.0f);

            // Apply gain
            data[i] = input * gain;
        }
    }
}
```

---

### AudioProcessor Integration

```cpp
// Source/Plugin/PluginProcessor.h
class FlamAudioProcessor : public juce::AudioProcessor
{
public:
    FlamAudioProcessor();

    // Override to support dynamic bus layouts
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    // Called when kit is loaded
    void onKitLoaded(const KitMetadata& metadata);

private:
    void configureDynamicBuses(const KitMetadata& metadata);

    std::unique_ptr<PerChannelMixer> perChannelMixer;
};
```

### Constructor Bus Setup

```cpp
FlamAudioProcessor::FlamAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Main Mix", AudioChannelSet::stereo(), true))  // Always have Main Mix
{
    perChannelMixer = std::make_unique<PerChannelMixer>();
}

// Called by JUCE when audio device initializes
void FlamAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Prepare mixer with current audio settings
    perChannelMixer->prepareToPlay(sampleRate, samplesPerBlock);
}

void FlamAudioProcessor::releaseResources()
{
    // Mixer buffers will be deallocated automatically
}
```

### Automatic Kit Loading and Mixer Configuration

```cpp
// Complete flow from YAML to mixer configuration
void FlamAudioProcessor::onKitLoaded(const KitMetadata& metadata)
{
    // metadata.channels was parsed from flamkit.yaml:
    //
    // channels:
    //   - name: "Kick In"
    //     index: 0
    //   - name: "Kick Out"
    //     index: 1
    //   ... etc

    const auto& channelDefinitions = metadata.channels;

    if (channelDefinitions.empty())
    {
        jassertfalse;  // Invalid kit - no channels defined
        return;
    }

    // Validate channel indices are sequential
    for (size_t i = 0; i < channelDefinitions.size(); ++i)
    {
        jassert(channelDefinitions[i].index == static_cast<int>(i));
    }

    // Suspend audio processing while reconfiguring
    suspendProcessing(true);

    // Extract channel names from YAML metadata
    std::vector<juce::String> channelNames;
    channelNames.reserve(channelDefinitions.size());

    for (const auto& channelDef : channelDefinitions)
    {
        channelNames.push_back(channelDef.name);
    }

    // Configure mixer: creates channel strips with labels from YAML
    perChannelMixer->setNumChannels(channelDefinitions.size(), channelNames);

    // Allocate FX buffers for new channel count
    perChannelMixer->prepareToPlay(getSampleRate(), getBlockSize());

    // Build DAW output bus layout dynamically based on channel count
    // Bus 0: Main Mix (stereo) - always present
    // Bus 1-N: Individual channel outputs (mono) - optional routing
    BusesLayout newLayout;
    newLayout.outputBuses.add(AudioChannelSet::stereo());  // Bus 0: Main Mix

    for (const auto& channelDef : channelDefinitions)
    {
        // Each channel gets its own mono output bus for DAW routing
        newLayout.outputBuses.add(AudioChannelSet::mono());
    }

    // Apply new bus layout to plugin
    if (!setBusesLayout(newLayout))
    {
        DBG("Warning: Failed to set bus layout for " << channelDefinitions.size() << " channels");
    }

    // Resume audio processing
    suspendProcessing(false);

    // At this point:
    // - Mixer has N channel strips labeled with names from flamkit.yaml
    // - DAW sees 1 stereo bus (Main Mix) + N mono buses (individual channels)
    // - User can route each channel to Main Mix or individual DAW bus
}
```

### YAML-to-Mixer Data Flow

```
flamkit.yaml
    ↓
┌─────────────────────────────────────────┐
│ channels:                               │
│   - name: "Kick In"    index: 0         │
│   - name: "Kick Out"   index: 1         │
│   - name: "Snare Top"  index: 2         │
│   ... etc                               │
└─────────────────────────────────────────┘
    ↓ Parsed by FlamKitLoader
┌─────────────────────────────────────────┐
│ KitMetadata {                           │
│   channels: [                           │
│     {name: "Kick In",   index: 0},      │
│     {name: "Kick Out",  index: 1},      │
│     {name: "Snare Top", index: 2}       │
│   ]                                     │
│ }                                       │
└─────────────────────────────────────────┘
    ↓ onKitLoaded()
┌─────────────────────────────────────────┐
│ PerChannelMixer::setNumChannels(3, [...])│
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│ Mixer UI:                               │
│ ┌─────────────┐ ┌─────────────┐        │
│ │  Kick In    │ │  Kick Out   │  ...   │
│ │ [Main Mix ▼]│ │ [Main Mix ▼]│        │
│ │     ║       │ │     ║       │        │
│ │    [S][M]   │ │    [S][M]   │        │
│ └─────────────┘ └─────────────┘        │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│ DAW Output Buses:                       │
│   Bus 0: Main Mix (stereo)              │
│   Bus 1: Kick In (mono)                 │
│   Bus 2: Kick Out (mono)                │
│   Bus 3: Snare Top (mono)               │
└─────────────────────────────────────────┘
```

### Kit Metadata Structure (from FlamKitLoader)

```cpp
// Source/Formats/FlamKitLoader.h
struct MicrophoneChannel
{
    juce::String name;  // From YAML: "Kick In", "OH-L", etc.
    int index;          // From YAML: 0, 1, 2, etc.
};

struct KitMetadata
{
    juce::String name;
    juce::String version;
    juce::String author;
    int sampleRate;

    std::vector<MicrophoneChannel> channels;  // ← Parsed from flamkit.yaml
    // ... pieces, velocity layers, etc.
};
```

### Kit Validation (ensures WAV files match YAML)

```cpp
// Source/Formats/FlamKitLoader.cpp
KitMetadata FlamKitLoader::loadFromFile(const juce::File& yamlFile)
{
    KitMetadata metadata = parseYAML(yamlFile);

    // Validate channel configuration
    if (metadata.channels.empty())
        throw std::runtime_error("Kit has no channels defined in flamkit.yaml");

    if (metadata.channels.size() > 16)
        throw std::runtime_error("Kit has too many channels (max 16)");

    // Validate all sample WAV files have correct channel count
    for (const auto& piece : metadata.pieces)
    {
        for (const auto& velocityLayer : piece.velocityLayers)
        {
            for (const auto& roundRobin : velocityLayer.roundRobins)
            {
                juce::File sampleFile = yamlFile.getParentDirectory().getChildFile(roundRobin.file);

                if (!sampleFile.existsAsFile())
                {
                    throw std::runtime_error("Sample file not found: " + roundRobin.file.toStdString());
                }

                // Load WAV header to check channel count
                std::unique_ptr<juce::AudioFormatReader> reader(
                    formatManager.createReaderFor(sampleFile)
                );

                if (reader == nullptr)
                {
                    throw std::runtime_error("Invalid WAV file: " + roundRobin.file.toStdString());
                }

                // CRITICAL: WAV channel count must match flamkit.yaml channel count
                if (reader->numChannels != static_cast<unsigned int>(metadata.channels.size()))
                {
                    throw std::runtime_error(
                        "Channel count mismatch: " + roundRobin.file.toStdString() +
                        " has " + juce::String(reader->numChannels).toStdString() + " channels, " +
                        "but flamkit.yaml defines " + juce::String(metadata.channels.size()).toStdString()
                    );
                }
            }
        }
    }

    return metadata;
}
```

**Example Error Handling**:

```
❌ ERROR: Channel count mismatch
   File: Samples/Kick/kick_v064_rr0.wav has 8 channels
   Expected: 11 channels (defined in flamkit.yaml)

   flamkit.yaml defines:
   - Kick In, Kick Out, Snare Top, Snare Btm, HH Close,
     Tom 1, Tom 2, OH-L, OH-R, Room-L, Room-R

   Solution: Re-record samples with 11 channels or update
   flamkit.yaml to match 8-channel configuration.
```

### Dynamic Reconfiguration Example

**Scenario**: User loads different kits with different channel counts during session

```cpp
// User loads "Minimal Stereo Kit" (2 channels)
// flamkit.yaml:
//   channels:
//     - name: "L"
//       index: 0
//     - name: "R"
//       index: 1

onKitLoaded(minimalKitMetadata);
// → Mixer creates 2 channel strips: "L", "R"
// → DAW sees: Main Mix (stereo) + Bus 1 (L) + Bus 2 (R)

// User switches to "Studio Rock Kit" (11 channels)
// flamkit.yaml:
//   channels:
//     - name: "Kick In"
//       index: 0
//     - name: "Kick Out"
//       index: 1
//     ... (9 more channels)

onKitLoaded(studioKitMetadata);
// → Mixer destroys old 2 strips
// → Mixer creates 11 new strips with names from YAML
// → FX buffers reallocated for 11 channels
// → DAW sees: Main Mix (stereo) + Bus 1-11 (individual channels)
// → All routing/FX settings reset to defaults

// User switches back to "Minimal Stereo Kit"
onKitLoaded(minimalKitMetadata);
// → Mixer shrinks back to 2 strips
// → DAW buses reconfigure to 3 total (Main Mix + 2)
```

**Key Behavior**:
- Mixer channel count **always matches** `flamkit.yaml` channel count
- Switching kits **resets all mixer state** (routing, FX, volume, pan)
- DAW bus layout **automatically reconfigures** on kit change
- No manual configuration required - fully automatic based on YAML

### ProcessBlock Integration

```cpp
void FlamAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    // Process MIDI and trigger voices
    voiceManager->processMidi(midiMessages);

    // Render all active voices to multi-channel buffer
    AudioBuffer<float> multiChannelBuffer(
        perChannelMixer->getNumChannels(),
        buffer.getNumSamples()
    );
    multiChannelBuffer.clear();

    for (auto* voice : voiceManager->getActiveVoices())
    {
        AudioBuffer<float> voiceOutput = voice->renderNextBlock(buffer.getNumSamples());

        // Sum this voice's channels into multi-channel buffer
        for (int ch = 0; ch < voiceOutput.getNumChannels(); ++ch)
        {
            multiChannelBuffer.addFrom(ch, 0, voiceOutput, ch, 0, buffer.getNumSamples());
        }
    }

    // Process through mixer (handles routing + mixing)
    perChannelMixer->process(multiChannelBuffer, buffer, buffer.getNumSamples());
}
```

---

## UI Implementation

### Main UI Navigation Tab

The Per-Channel Mixer resides in its own dedicated navigation tab within the FlamKit main UI.

**Tab Structure:**
```
┌─────────────────────────────────────────────────────────┐
│ FlamKit                                                 │
├─────────────────────────────────────────────────────────┤
│ [Kit Browser] [Mapping] [MIXER] [Settings]             │ ← Navigation tabs
├─────────────────────────────────────────────────────────┤
│                                                         │
│              Mixer Panel Content                        │
│  (Channel strips, master section, scrollable layout)   │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

**Tab Behavior:**
- **Tab Label:** "Mixer" or "Mix"
- **Visibility:** Tab is always visible (not hidden when kit unloaded)
- **State When No Kit Loaded:** Shows placeholder message: "Load a kit to access mixer"
- **State When Kit Loaded:** Displays full mixer interface with N channel strips + master section
- **Persistence:** Selected tab persists across sessions (restore last active tab on launch)

**Integration with Main UI:**
- Mixer tab content managed by `MixerPanel` component
- Tab switching handled by main `FlamEditorComponent` or equivalent top-level UI manager
- Mixer state (parameters, routing) persists independently of tab visibility
- Audio processing continues regardless of which tab is visible (mixer runs in background)

---

### Channel Strip Component with Output Selector

```cpp
// Source/UI/MixerPanel.h
class ChannelStripComponent : public juce::Component
{
public:
    ChannelStripComponent(PerChannelMixer& mixer, int channelIndex);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PerChannelMixer& mixer;
    int channelIndex;

    juce::Label nameLabel;
    juce::ComboBox outputSelector;  // NEW: Output routing dropdown
    juce::Slider volumeSlider;
    juce::Slider panSlider;
    juce::TextButton soloButton{"S"};
    juce::TextButton muteButton{"M"};
    PeakMeter peakMeter;

    void onOutputChanged();
    void onVolumeChanged();
    void onPanChanged();
    void onSoloClicked();
    void onMuteClicked();

    void updateControlsEnabled();  // Enable/disable controls based on output routing
};
```

### Channel Strip Layout with Output Selector

```
┌──────────────────┐
│   Kick Close     │  ← Channel name
├──────────────────┤
│  Output:         │
│  [Main Mix    ▼] │  ← Dropdown: Main Mix, Bus 1, Bus 2, ..., Bus 16
├──────────────────┤
│       ║          │  ← Peak meter (always active)
│       ║▓▓▓       │
├──────────────────┤
│    [  S  ]       │  ← Solo (disabled if output != Main Mix)
│    [  M  ]       │  ← Mute (disabled if output != Main Mix)
├──────────────────┤
│       ◄═►        │  ← Pan (disabled if output != Main Mix)
├──────────────────┤
│       ║          │
│       ║          │  ← Volume fader (disabled if output != Main Mix)
│       █          │
│      ═╬═         │
└──────────────────┘
```

### Output Selector Implementation

```cpp
void ChannelStripComponent::ChannelStripComponent(PerChannelMixer& m, int idx)
    : mixer(m), channelIndex(idx)
{
    // Populate output selector
    outputSelector.addItem("Main Mix", 1);

    for (int i = 1; i <= 16; ++i)
    {
        outputSelector.addItem("Bus " + juce::String(i), i + 1);
    }

    outputSelector.setSelectedId(1);  // Default to Main Mix
    outputSelector.onChange = [this] { onOutputChanged(); };

    addAndMakeVisible(outputSelector);

    // ... rest of UI setup
}

void ChannelStripComponent::onOutputChanged()
{
    int selectedId = outputSelector.getSelectedId();

    PerChannelMixer::OutputDestination dest;
    if (selectedId == 1)
        dest = PerChannelMixer::OutputDestination::MainMix;
    else
        dest = static_cast<PerChannelMixer::OutputDestination>(selectedId - 1);

    mixer.setChannelOutput(channelIndex, dest);

    updateControlsEnabled();
}

void ChannelStripComponent::updateControlsEnabled()
{
    bool isMainMix = (mixer.getChannelOutput(channelIndex) == PerChannelMixer::OutputDestination::MainMix);

    volumeSlider.setEnabled(isMainMix);
    panSlider.setEnabled(isMainMix);
    soloButton.setEnabled(isMainMix);
    muteButton.setEnabled(isMainMix);

    // Gray out disabled controls
    volumeSlider.setAlpha(isMainMix ? 1.0f : 0.5f);
    panSlider.setAlpha(isMainMix ? 1.0f : 0.5f);
    soloButton.setAlpha(isMainMix ? 1.0f : 0.5f);
    muteButton.setAlpha(isMainMix ? 1.0f : 0.5f);
}
```

### FX Chain UI Component

```cpp
// Source/UI/FXChainPanel.h
class FXButtonComponent : public juce::Component
{
public:
    FXButtonComponent(const juce::String& effectName);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void setEnabled(bool enabled);
    bool isEnabled() const { return effectEnabled; }

    std::function<void()> onEffectClicked;  // Opens settings popover
    std::function<void(bool)> onPowerToggled;  // Toggles on/off

private:
    juce::String name;
    juce::TextButton powerButton{"⏻"};  // Power symbol
    bool effectEnabled{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FXButtonComponent)
};

class FXChainComponent : public juce::Component
{
public:
    FXChainComponent(PerChannelMixer& mixer, int channelIndex);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    PerChannelMixer& mixer;
    int channelIndex;

    FXButtonComponent eqButton{"EQ"};
    FXButtonComponent saturationButton{"Saturation"};
    FXButtonComponent compressorButton{"Compressor"};

    std::unique_ptr<juce::Component> currentPopover;  // Active settings popover

    void showEQPopover();
    void showSaturationPopover();
    void showCompressorPopover();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FXChainComponent)
};
```

### FX Button Implementation

```cpp
FXButtonComponent::FXButtonComponent(const juce::String& effectName)
    : name(effectName)
{
    // Power button setup
    powerButton.setButtonText("⏻");
    powerButton.onClick = [this]
    {
        effectEnabled = !effectEnabled;
        if (onPowerToggled)
            onPowerToggled(effectEnabled);
        repaint();
    };

    addAndMakeVisible(powerButton);
}

void FXButtonComponent::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colours::darkgrey.darker());

    // Effect name - light up when enabled, dark when disabled
    g.setColour(effectEnabled ? juce::Colours::white : juce::Colours::darkgrey);
    g.setFont(14.0f);
    g.drawText(name, getLocalBounds().reduced(4), juce::Justification::centred);

    // Border highlight when enabled
    if (effectEnabled)
    {
        g.setColour(juce::Colours::cyan);
        g.drawRect(getLocalBounds(), 2);
    }
}

void FXButtonComponent::resized()
{
    // Power button in top-right corner
    powerButton.setBounds(getWidth() - 24, 2, 20, 20);
}

void FXButtonComponent::mouseDown(const juce::MouseEvent& e)
{
    // Only trigger if NOT clicking power button
    if (!powerButton.getBounds().contains(e.getPosition()))
    {
        if (onEffectClicked)
            onEffectClicked();
    }
}

void FXButtonComponent::setEnabled(bool enabled)
{
    effectEnabled = enabled;
    repaint();
}
```

### FX Chain Panel Implementation

```cpp
FXChainComponent::FXChainComponent(PerChannelMixer& m, int idx)
    : mixer(m), channelIndex(idx)
{
    // Setup EQ button
    eqButton.onPowerToggled = [this](bool enabled)
    {
        mixer.setChannelEQEnabled(channelIndex, enabled);
    };
    eqButton.onEffectClicked = [this]
    {
        showEQPopover();
    };
    addAndMakeVisible(eqButton);

    // Setup Saturation button
    saturationButton.onPowerToggled = [this](bool enabled)
    {
        mixer.setChannelSaturationEnabled(channelIndex, enabled);
    };
    saturationButton.onEffectClicked = [this]
    {
        showSaturationPopover();
    };
    addAndMakeVisible(saturationButton);

    // Setup Compressor button
    compressorButton.onPowerToggled = [this](bool enabled)
    {
        mixer.setChannelCompressorEnabled(channelIndex, enabled);
    };
    compressorButton.onEffectClicked = [this]
    {
        showCompressorPopover();
    };
    addAndMakeVisible(compressorButton);
}

void FXChainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::grey);
    g.drawRect(getLocalBounds(), 1);
}

void FXChainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(4);
    const int buttonHeight = 30;
    const int spacing = 4;

    eqButton.setBounds(bounds.removeFromTop(buttonHeight));
    bounds.removeFromTop(spacing);

    saturationButton.setBounds(bounds.removeFromTop(buttonHeight));
    bounds.removeFromTop(spacing);

    compressorButton.setBounds(bounds.removeFromTop(buttonHeight));
}

void FXChainComponent::showEQPopover()
{
    // Create popover with 10 band sliders
    auto* popover = new juce::Component();
    popover->setSize(300, 400);

    // Add 10 vertical sliders for each band
    for (int i = 0; i < 10; ++i)
    {
        auto* slider = new juce::Slider(juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
        slider->setRange(-12.0, 12.0, 0.1);
        slider->setValue(mixer.getChannelEQBandGain(channelIndex, i));
        slider->onValueChange = [this, i, slider]
        {
            mixer.setChannelEQBandGain(channelIndex, i, slider->getValue());
        };

        popover->addAndMakeVisible(slider);
        slider->setBounds(i * 30, 0, 30, 350);
    }

    currentPopover.reset(popover);
    // Show popover at button location (implementation depends on JUCE popover system)
}

void FXChainComponent::showSaturationPopover()
{
    // Create popover with mode selector and amount slider
    auto* popover = new juce::Component();
    popover->setSize(250, 150);

    auto* modeSelector = new juce::ComboBox();
    modeSelector->addItem("Tape", 1);
    modeSelector->addItem("Tube", 2);
    modeSelector->addItem("Digital", 3);
    modeSelector->setSelectedId(mixer.getChannelSaturationMode(channelIndex) + 1);
    modeSelector->onChange = [this, modeSelector]
    {
        mixer.setChannelSaturationMode(channelIndex, modeSelector->getSelectedId() - 1);
    };
    popover->addAndMakeVisible(modeSelector);
    modeSelector->setBounds(10, 10, 230, 30);

    auto* amountSlider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    amountSlider->setRange(0.0, 1.0, 0.01);
    amountSlider->setValue(mixer.getChannelSaturationAmount(channelIndex));
    amountSlider->onValueChange = [this, amountSlider]
    {
        mixer.setChannelSaturationAmount(channelIndex, amountSlider->getValue());
    };
    popover->addAndMakeVisible(amountSlider);
    amountSlider->setBounds(10, 50, 230, 50);

    currentPopover.reset(popover);
}

void FXChainComponent::showCompressorPopover()
{
    // Create popover with threshold, ratio, attack, release, makeup gain
    auto* popover = new juce::Component();
    popover->setSize(300, 300);

    int y = 10;
    const int rowHeight = 50;

    // Threshold
    auto* thresholdSlider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    thresholdSlider->setRange(-60.0, 0.0, 0.1);
    thresholdSlider->setValue(mixer.getChannelCompressorThreshold(channelIndex));
    thresholdSlider->onValueChange = [this, thresholdSlider]
    {
        mixer.setChannelCompressorThreshold(channelIndex, thresholdSlider->getValue());
    };
    popover->addAndMakeVisible(thresholdSlider);
    thresholdSlider->setBounds(10, y, 280, 40);
    y += rowHeight;

    // Ratio
    auto* ratioSlider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    ratioSlider->setRange(1.0, 20.0, 0.1);
    ratioSlider->setValue(mixer.getChannelCompressorRatio(channelIndex));
    ratioSlider->onValueChange = [this, ratioSlider]
    {
        mixer.setChannelCompressorRatio(channelIndex, ratioSlider->getValue());
    };
    popover->addAndMakeVisible(ratioSlider);
    ratioSlider->setBounds(10, y, 280, 40);
    y += rowHeight;

    // Attack
    auto* attackSlider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    attackSlider->setRange(0.1, 100.0, 0.1);
    attackSlider->setValue(mixer.getChannelCompressorAttack(channelIndex));
    attackSlider->onValueChange = [this, attackSlider]
    {
        mixer.setChannelCompressorAttack(channelIndex, attackSlider->getValue());
    };
    popover->addAndMakeVisible(attackSlider);
    attackSlider->setBounds(10, y, 280, 40);
    y += rowHeight;

    // Release
    auto* releaseSlider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    releaseSlider->setRange(10.0, 1000.0, 1.0);
    releaseSlider->setValue(mixer.getChannelCompressorRelease(channelIndex));
    releaseSlider->onValueChange = [this, releaseSlider]
    {
        mixer.setChannelCompressorRelease(channelIndex, releaseSlider->getValue());
    };
    popover->addAndMakeVisible(releaseSlider);
    releaseSlider->setBounds(10, y, 280, 40);
    y += rowHeight;

    // Makeup Gain
    auto* makeupSlider = new juce::Slider(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    makeupSlider->setRange(0.0, 24.0, 0.1);
    makeupSlider->setValue(mixer.getChannelCompressorMakeupGain(channelIndex));
    makeupSlider->onValueChange = [this, makeupSlider]
    {
        mixer.setChannelCompressorMakeupGain(channelIndex, makeupSlider->getValue());
    };
    popover->addAndMakeVisible(makeupSlider);
    makeupSlider->setBounds(10, y, 280, 40);

    currentPopover.reset(popover);
}
```

### Updated Channel Strip Layout with FX

```
┌───────────────────┐
│   Kick Close      │  ← Channel name
├───────────────────┤
│  Output:          │
│  [Main Mix    ▼]  │  ← Dropdown
├───────────────────┤
│  ┌─────────────┐  │
│  │  EQ       ⏻ │  │  ← FX button (click = settings, ⏻ = on/off)
│  └─────────────┘  │     Name lights up when enabled
│  ┌─────────────┐  │
│  │Saturation ⏻ │  │
│  └─────────────┘  │
│  ┌─────────────┐  │
│  │Compressor ⏻ │  │
│  └─────────────┘  │
├───────────────────┤
│        ║          │  ← Peak meter
│        ║▓▓▓       │
├───────────────────┤
│     [  S  ]       │  ← Solo
│     [  M  ]       │  ← Mute
├───────────────────┤
│       ◄═►         │  ← Pan
├───────────────────┤
│        ║          │  ← Volume fader
│        █          │
│       ═╬═         │
└───────────────────┘
```

---

## Testing Requirements

1. **Output Routing**
   - Set channel to Main Mix → verify internal mixer controls active
   - Set channel to Bus 1 → verify controls disabled, audio routes to DAW track
   - Mix channels: some Main Mix, some Bus routing → verify correct behavior

2. **DAW Integration**
   - VST3: Reaper, Ableton Live, FL Studio
   - AU: Logic Pro, GarageBand
   - Verify all output buses appear in DAW routing
   - Verify bus names display correctly

3. **Internal Mixer**
   - Route all channels to Main Mix
   - Test volume, pan, solo, mute
   - Verify peak metering works
   - Test master volume affects only Main Mix

4. **FX Chain Testing**
   - **Per-Channel FX**:
     - Enable EQ on kick channel → adjust 10 bands → verify audible effect
     - Enable Saturation → toggle Tape/Tube/Digital modes → verify different characteristics
     - Enable Compressor → adjust threshold/ratio → verify gain reduction
     - Test FX only affect channels routed to Main Mix (bypassed when routed to bus)
   - **Master FX**:
     - Enable master EQ → verify affects all Main Mix channels
     - Enable master Saturation → verify applies to summed mix
     - Enable master Compressor → verify glues mix together
     - Enable master Limiter → verify prevents clipping at threshold
   - **UI Behavior**:
     - Power button toggles effect on/off → verify name lights up when enabled
     - Click effect name (not power button) → verify popover opens
     - Adjust parameters in popover → verify real-time audio updates
     - Close popover → verify settings persist
   - **Processing Order**:
     - Verify channel FX order: EQ → Saturation → Compressor
     - Verify master FX order: EQ → Saturation → Compressor → Limiter

5. **Edge Cases**
   - Load 2-channel kit → verify 2 channel strips + output options
   - Load 16-channel kit → verify all 16 strips functional
   - Switch kit → verify bus reconfiguration
   - Standalone mode → verify all features work
   - Enable all FX on all channels → measure CPU usage (<10% overhead target)

---

## Real-Time Safety Constraints

- **Audio Thread:**
  - ✅ `process()` - **100% lock-free, zero-allocation**
  - ✅ Read atomic parameters (volume, pan, routing, solo, mute, FX enables/parameters)
  - ✅ FX processing - uses pre-allocated buffers from `prepareToPlay()`
    - `channelFXBuffers[]` - one mono buffer per channel, allocated at max block size
    - `masterFXBuffer` - stereo buffer for master FX, allocated at max block size
    - Only `clear()` and `copyFrom()` called per block (no allocations)
  - ✅ Update atomic peak levels
  - ✅ DSP algorithms - all use stack variables or pre-allocated filter state
  - ❌ **No allocations, locks, or blocking operations**

- **UI Thread:**
  - `setChannelOutput()`, `setChannelVolume()`, `setChannelEQEnabled()`, etc. - atomic writes
  - Read peak levels for meter updates
  - No direct audio buffer access
  - FX popover creation - safe (UI thread only)

- **Non-Audio Thread:**
  - `setNumChannels()` - can allocate, must suspend processing first
  - `prepareToPlay()` - allocates FX buffers, called during initialization/kit loading
  - `configureDynamicBuses()` - JUCE handles thread safety
  - FX `prepareToPlay()` - initializes filter coefficients, resets envelope state

---

## Performance Optimizations

### Zero-Allocation Audio Processing

The mixer achieves **100% allocation-free audio processing** through strategic buffer pre-allocation:

1. **Buffer Pre-Allocation Strategy**:
   - `channelFXBuffers[]` - allocated once in `prepareToPlay()`, sized to `maximumBlockSize`
   - `masterFXBuffer` - allocated once in `prepareToPlay()`, sized to `maximumBlockSize`
   - Buffers persist until next `prepareToPlay()` call (e.g., sample rate change, kit reload)

2. **Per-Block Operations** (zero allocations):
   - `clear(0, numSamples)` - clears only used portion, no allocation
   - `copyFrom()` - copies samples, no allocation
   - DSP processing - uses pre-allocated filter state and stack variables

3. **Memory Overhead**:
   - Per channel: `maximumBlockSize * sizeof(float)` bytes (typically 512 * 4 = 2 KB)
   - Master: `2 * maximumBlockSize * sizeof(float)` bytes (typically 512 * 2 * 4 = 4 KB)
   - Total for 16 channels: ~36 KB of FX buffer overhead (negligible)

4. **CPU Optimization**:
   - FX bypass check (`if (enabled.load())`) happens before processing
   - Disabled effects consume zero CPU cycles (early return)
   - Filter state updated only when parameters change (via `updateCoefficients()`)

### Expected Performance

| Configuration | CPU Usage (64-sample buffer @ 48kHz) |
|---------------|--------------------------------------|
| 16 channels, no FX | <1% (baseline mixing only) |
| 16 channels, all FX enabled | <5% (EQ + Saturation + Compressor per channel + Master FX) |
| 16 channels, master FX only | <2% (master chain processing) |

*Benchmarks assume modern CPU (Intel i5/AMD Ryzen or better)*

---

## State Persistence

```cpp
juce::ValueTree PerChannelMixer::getState() const
{
    juce::ValueTree state("MixerState");

    state.setProperty("masterVolume", masterVolumeDb.load(), nullptr);

    for (size_t i = 0; i < channels.size(); ++i)
    {
        juce::ValueTree channelState("Channel");
        channelState.setProperty("index", static_cast<int>(i), nullptr);
        channelState.setProperty("name", channels[i].name, nullptr);
        channelState.setProperty("outputDest", channels[i].outputDestination.load(), nullptr);
        channelState.setProperty("volume", channels[i].volumeDb.load(), nullptr);
        channelState.setProperty("pan", channels[i].pan.load(), nullptr);
        channelState.setProperty("solo", channels[i].solo.load(), nullptr);
        channelState.setProperty("mute", channels[i].mute.load(), nullptr);

        // FX parameters
        channelState.setProperty("eqEnabled", channels[i].eq.enabled.load(), nullptr);
        for (int band = 0; band < 10; ++band)
        {
            juce::String bandProp = "eqBand" + juce::String(band);
            channelState.setProperty(bandProp, channels[i].eq.bandGains[band].load(), nullptr);
        }

        channelState.setProperty("satEnabled", channels[i].saturation.enabled.load(), nullptr);
        channelState.setProperty("satAmount", channels[i].saturation.amount.load(), nullptr);
        channelState.setProperty("satMode", channels[i].saturation.mode.load(), nullptr);

        channelState.setProperty("compEnabled", channels[i].compressor.enabled.load(), nullptr);
        channelState.setProperty("compThreshold", channels[i].compressor.thresholdDb.load(), nullptr);
        channelState.setProperty("compRatio", channels[i].compressor.ratio.load(), nullptr);
        channelState.setProperty("compAttack", channels[i].compressor.attackMs.load(), nullptr);
        channelState.setProperty("compRelease", channels[i].compressor.releaseMs.load(), nullptr);
        channelState.setProperty("compMakeup", channels[i].compressor.makeupGainDb.load(), nullptr);

        state.appendChild(channelState, nullptr);
    }

    // Master FX parameters
    state.setProperty("masterEQEnabled", masterEQ.enabled.load(), nullptr);
    for (int band = 0; band < 10; ++band)
    {
        juce::String bandProp = "masterEQBand" + juce::String(band);
        state.setProperty(bandProp, masterEQ.bandGains[band].load(), nullptr);
    }

    state.setProperty("masterSatEnabled", masterSaturation.enabled.load(), nullptr);
    state.setProperty("masterSatAmount", masterSaturation.amount.load(), nullptr);
    state.setProperty("masterSatMode", masterSaturation.mode.load(), nullptr);

    state.setProperty("masterCompEnabled", masterCompressor.enabled.load(), nullptr);
    state.setProperty("masterCompThreshold", masterCompressor.thresholdDb.load(), nullptr);
    state.setProperty("masterCompRatio", masterCompressor.ratio.load(), nullptr);
    state.setProperty("masterCompAttack", masterCompressor.attackMs.load(), nullptr);
    state.setProperty("masterCompRelease", masterCompressor.releaseMs.load(), nullptr);
    state.setProperty("masterCompMakeup", masterCompressor.makeupGainDb.load(), nullptr);

    state.setProperty("masterLimiterEnabled", masterLimiter.enabled.load(), nullptr);
    state.setProperty("masterLimiterThreshold", masterLimiter.thresholdDb.load(), nullptr);
    state.setProperty("masterLimiterRelease", masterLimiter.releaseMs.load(), nullptr);

    return state;
}
```

---

## User Workflows

### Beginner: Full Internal Mixing
1. Load kit
2. All channels default to "Main Mix"
3. Adjust volume/pan on each channel strip
4. Apply master volume
5. Single stereo output to DAW

### Intermediate: Hybrid Approach
1. Load 8-channel kit
2. Route kick (Bus 1) and snare (Bus 2) to DAW
3. Leave overheads + room on Main Mix
4. Process kick/snare with DAW plugins
5. Quick mix overheads/room internally

### Advanced: Full DAW Routing
1. Load kit
2. Route every channel to separate bus (Bus 1-8)
3. Bypass all internal mixer controls
4. Mix entirely in DAW with native plugins
5. FlamKit acts as pure sample playback engine

---

## Future Enhancements (Post-v1.0)

- **Bus Groups**: Assign multiple channels to same bus (e.g., OH-L + OH-R → Bus 1 stereo)
- **Routing Presets**: Save/load common routing configurations
- **Stereo Buses**: Support stereo bus pairs (currently mono only)
- **Bounce-in-Place**: Export stems from individual buses in standalone mode
- **Advanced FX**:
  - Parametric EQ mode (adjustable Q and frequency per band)
  - Multi-band compression
  - Stereo widening
  - Convolution reverb per channel
  - Third-party VST3 plugin hosting in FX chain
- **FX Presets**: Save/load FX chain presets per instrument type (kick preset, snare preset, etc.)
- **Visual FX Feedback**: Real-time gain reduction meters on compressor/limiter, spectrum analyzer for EQ
- **Sidechaining**: Route one channel to control compression on another (e.g., kick triggers sidechain on bass)
