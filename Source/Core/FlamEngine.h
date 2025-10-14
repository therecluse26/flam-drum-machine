#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <atomic>

namespace flam {

class VoiceManager;
class MixerBus;
class FlamKitLoader;

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

private:
    std::unique_ptr<VoiceManager> voiceManager;
    std::unique_ptr<MixerBus> mixerBus;
    std::unique_ptr<FlamKitLoader> kitLoader;

    std::atomic<float> humanization{0.0f};
    std::atomic<int> latencyCompensation{0};

    double currentSampleRate{44100.0};
    int currentBlockSize{512};

    juce::Random randomGenerator;

    void handleMidiEvent(const juce::MidiMessage& message, int samplePosition);
    float applyHumanization(float velocity);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlamEngine)
};

} // namespace flam