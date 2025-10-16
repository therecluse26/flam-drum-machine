#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <atomic>

namespace flam {

class VoiceManager;
class MixerBus;
class FlamKitLoader;
class SimpleEQ;
class SimpleCompressor;

class FlamEngine : public juce::AudioProcessorGraph
{
public:
    FlamEngine();
    ~FlamEngine();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    void loadKit(const juce::File& kitFile);
    void clearKit();

    void setHumanizationAmount(float amount);
    float getHumanizationAmount() const { return humanization.load(); }

    void setLatencyCompensation(int samples);
    int getLatencyCompensation() const { return latencyCompensation.load(); }

    // Direct note triggering (for UI pads, etc.)
    void triggerNote(int midiNote, float velocity, int sampleOffset = 0);
    void releaseNote(int midiNote, int sampleOffset = 0);

    VoiceManager* getVoiceManager() const { return voiceManager.get(); }
    MixerBus* getMixerBus() const { return mixerBus.get(); }
    SimpleEQ* getEQ() const { return eq.get(); }
    SimpleCompressor* getCompressor() const { return compressor.get(); }

    // Master input gain control
    void setInputGain(float gainDB) { inputGain = juce::Decibels::decibelsToGain(gainDB); }
    float getInputGain() const { return inputGain; }

    // Level metering
    float getInputLevel() const { return inputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return outputLevel.load(std::memory_order_relaxed); }

    // Multi-channel support
    int getRequiredChannelCount() const;

    /**
     * Get the internal multi-channel rendering buffer.
     * This contains the full multi-channel output from voice rendering,
     * before any downmixing or processing. Used by PerChannelMixer.
     */
    const juce::AudioBuffer<float>& getMultiChannelBuffer() const { return internalBuffer; }

private:
    std::unique_ptr<VoiceManager> voiceManager;
    std::unique_ptr<MixerBus> mixerBus;
    std::unique_ptr<FlamKitLoader> kitLoader;
    std::unique_ptr<SimpleEQ> eq;
    std::unique_ptr<SimpleCompressor> compressor;

    std::atomic<float> humanization{0.0f};
    std::atomic<int> latencyCompensation{0};
    float inputGain{1.0f};

    // Level metering (updated from audio thread, read from UI thread)
    std::atomic<float> inputLevel{0.0f};
    std::atomic<float> outputLevel{0.0f};

    double currentSampleRate{44100.0};
    int currentBlockSize{512};

    // Multi-channel internal rendering buffer
    juce::AudioBuffer<float> internalBuffer;

    juce::Random randomGenerator;

    void handleMidiEvent(const juce::MidiMessage& message, int samplePosition);
    float applyHumanization(float velocity);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlamEngine)
};

} // namespace flam