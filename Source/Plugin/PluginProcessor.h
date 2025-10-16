#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/FlamEngine.h"
#include "../Core/PerChannelMixer.h"

namespace flam {

class FlamAudioProcessor : public juce::AudioProcessor
{
public:
    FlamAudioProcessor();
    ~FlamAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    FlamEngine* getEngine() { return &engine; }

    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    PerChannelMixer* getPerChannelMixer() { return perChannelMixer.get(); }

private:
    FlamEngine engine;
    std::unique_ptr<PerChannelMixer> perChannelMixer;
    juce::AudioProcessorValueTreeState parameters;
    
    juce::AudioParameterFloat* humanizationParam{nullptr};
    juce::AudioParameterFloat* masterVolumeParam{nullptr};
    juce::AudioParameterFloat* closeVolumeParam{nullptr};
    juce::AudioParameterFloat* overheadVolumeParam{nullptr};
    juce::AudioParameterFloat* roomVolumeParam{nullptr};
    juce::AudioParameterFloat* ambientVolumeParam{nullptr};
    juce::AudioParameterFloat* bleedAmountParam{nullptr};
    juce::AudioParameterInt* polyphonyParam{nullptr};
    juce::AudioParameterBool* roundRobinParam{nullptr};

    // Mixer section parameters
    juce::AudioParameterFloat* inputGainParam{nullptr};

    // 10-band EQ parameters (standard frequencies)
    juce::AudioParameterBool* eqBypassParam{nullptr};  // Note: named bypass but stores "enabled" state
    juce::AudioParameterFloat* eq31HzParam{nullptr};
    juce::AudioParameterFloat* eq62HzParam{nullptr};
    juce::AudioParameterFloat* eq125HzParam{nullptr};
    juce::AudioParameterFloat* eq250HzParam{nullptr};
    juce::AudioParameterFloat* eq500HzParam{nullptr};
    juce::AudioParameterFloat* eq1kHzParam{nullptr};
    juce::AudioParameterFloat* eq2kHzParam{nullptr};
    juce::AudioParameterFloat* eq4kHzParam{nullptr};
    juce::AudioParameterFloat* eq8kHzParam{nullptr};
    juce::AudioParameterFloat* eq16kHzParam{nullptr};

    // Compressor parameters
    juce::AudioParameterBool* compBypassParam{nullptr};  // Note: named bypass but stores "enabled" state
    juce::AudioParameterFloat* compAttackParam{nullptr};
    juce::AudioParameterFloat* compReleaseParam{nullptr};
    juce::AudioParameterFloat* compHoldParam{nullptr};
    juce::AudioParameterFloat* compThresholdParam{nullptr};
    juce::AudioParameterFloat* compRatioParam{nullptr};
    juce::AudioParameterFloat* compLookaheadParam{nullptr};
    juce::AudioParameterFloat* compMakeupGainParam{nullptr};

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateEngineParameters();

    // Pre-allocated buffer for mixer stereo output (real-time safe)
    juce::AudioBuffer<float> mixerOutputBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlamAudioProcessor)
};

} // namespace flam