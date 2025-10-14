#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace flam {

class MixerBus
{
public:
    enum class BusType
    {
        Close = 0,
        Overhead,
        Room,
        Ambient,
        Master,
        NumBuses
    };

    MixerBus();
    ~MixerBus();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();

    void processBlock(juce::AudioBuffer<float>& buffer);

    void setBusLevel(BusType bus, float levelDb);
    float getBusLevel(BusType bus) const;

    void setBusPan(BusType bus, float pan);
    float getBusPan(BusType bus) const;

    void setBusSolo(BusType bus, bool solo);
    bool isBusSoloed(BusType bus) const;

    void setBusMute(BusType bus, bool mute);
    bool isBusMuted(BusType bus) const;

    void setMasterLevel(float levelDb);
    float getMasterLevel() const { return masterLevel.load(); }

    void setBleedAmount(float amount);
    float getBleedAmount() const { return bleedAmount.load(); }

private:
    struct BusChannel
    {
        std::atomic<float> level{0.0f};
        std::atomic<float> pan{0.0f};
        std::atomic<bool> solo{false};
        std::atomic<bool> mute{false};
        
        juce::dsp::Gain<float> gainProcessor;
        juce::dsp::Panner<float> panProcessor;
        juce::SmoothedValue<float> smoothedLevel;
        juce::SmoothedValue<float> smoothedPan;
    };

    std::array<BusChannel, static_cast<size_t>(BusType::NumBuses)> buses;
    
    std::atomic<float> masterLevel{0.0f};
    std::atomic<float> bleedAmount{0.0f};
    
    juce::dsp::Limiter<float> masterLimiter;
    juce::SmoothedValue<float> smoothedMasterLevel;

    double sampleRate{44100.0};
    int blockSize{512};

    bool anySoloBusActive() const;
    void applyBusProcessing(BusType bus, juce::AudioBuffer<float>& buffer);
    void applyMicBleed(juce::AudioBuffer<float>& buffer);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerBus)
};

} // namespace flam