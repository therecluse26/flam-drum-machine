# Effects Chain Feature Specification

**Status:** v1.0 MVP (Master Effects), v1.5 (Per-Channel Effects)
**Priority:** Medium (v1.0), High (v1.5)
**Dependencies:** `PerChannelMixer`, DSP modules

---

## Overview

Implement professional-grade effects processing for FlamKit with two tiers:
1. **v1.0 Master Effects**: Single effects chain on master output (EQ, compression, saturation, convolution reverb)
2. **v1.5 Per-Channel Effects**: Independent effects on each microphone channel for advanced mixing

---

## Technical Requirements

### v1.0: Master Effects Chain

1. **Master Bus Processing**
   - Process final stereo mix before output
   - Bypassable effects with A/B comparison
   - User-adjustable parameters with DAW automation
   - Real-time safe processing (no allocations)

2. **Effect Types**
   - **3-Band EQ**: Low/Mid/High shelving filters
   - **Compressor**: Threshold, ratio, attack, release, makeup gain
   - **Saturation**: Tape/tube-style harmonic saturation
   - **Convolution Reverb**: IR-based room simulation with wet/dry mix

3. **Signal Flow**
   ```
   Drum Voices → Per-Channel Mixer → Master Volume → EQ → Compression → Saturation → Reverb → Output
   ```

### v1.5: Per-Channel Effects

1. **Independent Channel Processing**
   - Each mic channel has its own effects chain
   - Allows surgical mixing (e.g., compress only snare top)
   - Preset system for common channel treatments

2. **Channel Effect Types**
   - **Parametric EQ**: 4-band with Q control
   - **Compressor**: Studio-grade compression per channel
   - **Gate**: Noise gate with threshold/attack/release
   - **Transient Shaper**: Attack/sustain control
   - **Saturation**: Per-channel harmonic enhancement

---

## Implementation Details

### Class Structure (v1.0: Master Effects)

```cpp
// Source/DSP/EffectsChain.h
class EffectsChain
{
public:
    EffectsChain();

    void prepareToPlay(double sampleRate, int maximumBlockSize);
    void releaseResources();

    // Process audio (audio thread)
    void process(AudioBuffer<float>& buffer, int numSamples);

    // Effect control (thread-safe)
    void setEQEnabled(bool enabled);
    void setEQLowGain(float gainDb);    // -12 to +12 dB
    void setEQMidGain(float gainDb);
    void setEQHighGain(float gainDb);

    void setCompressorEnabled(bool enabled);
    void setCompressorThreshold(float thresholdDb);
    void setCompressorRatio(float ratio);
    void setCompressorAttack(float attackMs);
    void setCompressorRelease(float releaseMs);
    void setCompressorMakeupGain(float gainDb);

    void setSaturationEnabled(bool enabled);
    void setSaturationAmount(float amount);  // 0.0 to 1.0
    void setSaturationMode(SaturationMode mode);

    void setReverbEnabled(bool enabled);
    void setReverbImpulseResponse(const AudioBuffer<float>& ir);
    void setReverbWetDryMix(float mix);  // 0.0 (dry) to 1.0 (wet)

    // Bypass all effects
    void setGlobalBypass(bool bypass);

    // State persistence
    juce::ValueTree getState() const;
    void setState(const juce::ValueTree& state);

private:
    // Effect modules
    struct ThreeBandEQ
    {
        juce::dsp::IIR::Filter<float> lowShelf;
        juce::dsp::IIR::Filter<float> midPeak;
        juce::dsp::IIR::Filter<float> highShelf;

        std::atomic<bool> enabled{true};
        std::atomic<float> lowGainDb{0.0f};
        std::atomic<float> midGainDb{0.0f};
        std::atomic<float> highGainDb{0.0f};

        void process(AudioBuffer<float>& buffer, int numSamples);
    };

    struct Compressor
    {
        std::atomic<bool> enabled{true};
        std::atomic<float> thresholdDb{-10.0f};
        std::atomic<float> ratio{4.0f};
        std::atomic<float> attackMs{5.0f};
        std::atomic<float> releaseMs{100.0f};
        std::atomic<float> makeupGainDb{0.0f};

        float envelope{0.0f};
        double sampleRate{44100.0};

        void process(AudioBuffer<float>& buffer, int numSamples);
    };

    struct Saturation
    {
        enum class Mode { Tape, Tube, Digital };

        std::atomic<bool> enabled{false};
        std::atomic<float> amount{0.5f};
        std::atomic<int> mode{static_cast<int>(Mode::Tape)};

        void process(AudioBuffer<float>& buffer, int numSamples);
    };

    struct ConvolutionReverb
    {
        juce::dsp::Convolution convolution;

        std::atomic<bool> enabled{false};
        std::atomic<float> wetDryMix{0.3f};

        AudioBuffer<float> wetBuffer;

        void process(AudioBuffer<float>& buffer, int numSamples);
    };

    ThreeBandEQ eq;
    Compressor compressor;
    Saturation saturation;
    ConvolutionReverb reverb;

    std::atomic<bool> globalBypass{false};
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsChain)
};
```

### Master Effects Processing

```cpp
void EffectsChain::process(AudioBuffer<float>& buffer, int numSamples)
{
    if (globalBypass.load())
        return;

    // Process in series
    if (eq.enabled.load())
        eq.process(buffer, numSamples);

    if (compressor.enabled.load())
        compressor.process(buffer, numSamples);

    if (saturation.enabled.load())
        saturation.process(buffer, numSamples);

    if (reverb.enabled.load())
        reverb.process(buffer, numSamples);
}
```

### EQ Implementation

```cpp
void EffectsChain::ThreeBandEQ::process(AudioBuffer<float>& buffer, int numSamples)
{
    // Update filter coefficients if parameters changed
    // (In real implementation, detect parameter changes and update coefficients)

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);

    // Apply filters in series
    lowShelf.process(context);
    midPeak.process(context);
    highShelf.process(context);
}
```

### Compressor Implementation

```cpp
void EffectsChain::Compressor::process(AudioBuffer<float>& buffer, int numSamples)
{
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

            // Gain reduction
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

### Saturation Implementation

```cpp
void EffectsChain::Saturation::process(AudioBuffer<float>& buffer, int numSamples)
{
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
                case Mode::Tape:
                    // Soft clipping with asymmetric curve
                    output = std::tanh(input * (1.0f + amt * 4.0f));
                    break;

                case Mode::Tube:
                    // Tube-style saturation (odd harmonics)
                    output = input * (1.0f + amt * std::abs(input));
                    output = std::tanh(output);
                    break;

                case Mode::Digital:
                    // Hard clipping
                    output = juce::jlimit(-1.0f - amt * 0.1f, 1.0f + amt * 0.1f, input);
                    break;
            }

            data[i] = input * (1.0f - amt) + output * amt;  // Wet/dry blend
        }
    }
}
```

### Convolution Reverb Implementation

```cpp
void EffectsChain::ConvolutionReverb::process(AudioBuffer<float>& buffer, int numSamples)
{
    const float mix = wetDryMix.load();

    if (wetBuffer.getNumSamples() < numSamples)
        wetBuffer.setSize(buffer.getNumChannels(), numSamples, false, false, true);

    // Copy dry signal
    wetBuffer.makeCopyOf(buffer, true);

    // Process through convolution
    juce::dsp::AudioBlock<float> block(wetBuffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    convolution.process(context);

    // Mix wet and dry
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* dry = buffer.getWritePointer(ch);
        const float* wet = wetBuffer.getReadPointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            dry[i] = dry[i] * (1.0f - mix) + wet[i] * mix;
        }
    }
}
```

---

## Per-Channel Effects (v1.5)

```cpp
// Source/DSP/ChannelEffectsChain.h
class ChannelEffectsChain
{
public:
    ChannelEffectsChain();

    void prepareToPlay(double sampleRate, int maximumBlockSize);
    void process(AudioBuffer<float>& singleChannelBuffer, int numSamples);

    // Per-channel effects
    void setGateEnabled(bool enabled);
    void setGateThreshold(float thresholdDb);

    void setEQEnabled(bool enabled);
    void setEQBand(int bandIndex, float frequencyHz, float gainDb, float q);

    void setCompressorEnabled(bool enabled);
    // ... (same as master compressor)

    void setTransientShaperEnabled(bool enabled);
    void setTransientAttack(float amount);  // -1.0 to +1.0
    void setTransientSustain(float amount);

private:
    struct NoiseGate
    {
        std::atomic<bool> enabled{false};
        std::atomic<float> thresholdDb{-40.0f};
        float envelope{0.0f};

        void process(AudioBuffer<float>& buffer, int numSamples, double sampleRate);
    };

    struct ParametricEQ
    {
        static constexpr int NUM_BANDS = 4;
        juce::dsp::IIR::Filter<float> bands[NUM_BANDS];

        std::atomic<bool> enabled{false};

        void process(AudioBuffer<float>& buffer, int numSamples);
    };

    struct TransientShaper
    {
        std::atomic<bool> enabled{false};
        std::atomic<float> attackAmount{0.0f};
        std::atomic<float> sustainAmount{0.0f};

        float envelope{0.0f};

        void process(AudioBuffer<float>& buffer, int numSamples, double sampleRate);
    };

    NoiseGate gate;
    ParametricEQ eq;
    Compressor compressor;
    TransientShaper transientShaper;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelEffectsChain)
};
```

---

## UI Design

### Master Effects Panel (v1.0)

```
┌─────────────────────────────────────────────────┐
│  Master Effects                                  │
├─────────────────────────────────────────────────┤
│  [✓] EQ                                         │
│    Low: [====|====]  Mid: [====|====]           │
│    High: [====|====]                            │
├─────────────────────────────────────────────────┤
│  [✓] Compressor                                 │
│    Threshold: [-10 dB]  Ratio: [4:1]           │
│    Attack: [5 ms]  Release: [100 ms]            │
│    Makeup: [0 dB]                               │
├─────────────────────────────────────────────────┤
│  [ ] Saturation                                 │
│    Type: [Tape ▼]  Amount: [||||    ]          │
├─────────────────────────────────────────────────┤
│  [✓] Reverb                                     │
│    IR: [Studio A ▼]  Mix: [||||    ]           │
└─────────────────────────────────────────────────┘
```

### Per-Channel Effects (v1.5)

Each channel strip in mixer has "FX" button → opens channel effects panel

---

## Testing Requirements

1. **Master Effects**
   - Load kit, enable all effects
   - Adjust parameters while playing
   - Verify no dropouts or clicks
   - Measure CPU overhead (<3% per effect)

2. **Per-Channel Effects (v1.5)**
   - Enable EQ on kick channel only
   - Verify other channels unaffected
   - Test with 8 channels, all with effects enabled

3. **A/B Comparison**
   - Toggle global bypass
   - Verify instant, click-free switching

4. **State Persistence**
   - Adjust all effect parameters
   - Save and reload kit
   - Verify all settings restored

---

## Real-Time Safety Constraints

- **Audio Thread:**
  - ✅ `process()` - lock-free, allocation-free
  - ✅ Read atomic parameters
  - ✅ DSP math only (no I/O, no locks)
  - ❌ **No allocations, locks, or blocking operations**

- **UI Thread:**
  - Set parameters via atomics
  - Update filter coefficients in `prepareToPlay()` when possible

---

## Performance Optimizations

1. **SIMD Processing**
   - Use JUCE's dsp::SIMD utilities for EQ/compression
   - Vectorize saturation algorithms

2. **Parameter Smoothing**
   - Smooth parameter changes to avoid zipper noise
   - Use `juce::SmoothedValue` for automated smoothing

3. **Convolution Optimization**
   - Use FFT-based convolution for long IRs (>1024 samples)
   - Partition convolution for low latency

---

## Future Enhancements (Post-v1.5)

- Third-party plugin support (VST3 effects in FlamKit)
- Graphic EQ mode (31-band)
- Multi-band compression
- Stereo widening per channel
- Effect presets (save/load effect chains)
- Parallel compression routing
