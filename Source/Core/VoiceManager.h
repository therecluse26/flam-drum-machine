#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>
#include <array>
#include <atomic>

namespace flam {

struct DrumKit;
struct DrumVoice;

class VoiceManager
{
public:
    static constexpr int MAX_VOICES = 128;
    static constexpr int MAX_CHOKE_GROUPS = 16;
    
    VoiceManager();
    ~VoiceManager();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);

    void triggerNote(int midiNote, float velocity, int sampleOffset);
    void releaseNote(int midiNote, int sampleOffset);

    void loadKit(std::unique_ptr<DrumKit> kit);
    void clearKit();

    void setPolyphony(int maxVoices);
    int getPolyphony() const { return maxActiveVoices.load(); }

    void setRoundRobinEnabled(bool enabled) { useRoundRobin.store(enabled); }
    bool isRoundRobinEnabled() const { return useRoundRobin.load(); }

private:
    struct Voice
    {
        std::unique_ptr<DrumVoice> drumVoice;
        int midiNote{-1};
        int chokeGroup{-1};
        int samplePosition{0};
        float velocity{0.0f};
        bool isActive{false};
        juce::ADSR envelope;
    };

    std::vector<Voice> voices;
    std::unique_ptr<DrumKit> currentKit;

    std::atomic<int> maxActiveVoices{64};
    std::atomic<bool> useRoundRobin{true};

    std::array<int, MAX_CHOKE_GROUPS> chokeGroupLastVoice;
    std::array<int, 128> roundRobinCounters;

    double sampleRate{44100.0};
    int blockSize{512};

    juce::SpinLock voiceLock;

    int findFreeVoice() const;
    void startVoice(int voiceIndex, int midiNote, float velocity, int sampleOffset);
    void stopVoice(int voiceIndex, int sampleOffset);
    void handleChokeGroup(int chokeGroup, int excludeVoice);
    int getNextRoundRobinIndex(int midiNote);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceManager)
};

} // namespace flam