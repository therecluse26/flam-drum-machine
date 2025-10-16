#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>
#include <array>
#include <atomic>

namespace flam {

struct DrumKit;
struct SampleLayer;
class SampleVoice;
class SampleStreamingManager;

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

    /**
     * Get the number of output channels required for the currently loaded kit.
     * Returns the maximum channel count across all samples in the kit.
     * Defaults to 2 (stereo) if no kit is loaded.
     */
    int getRequiredChannelCount() const;

private:
    struct Voice
    {
        std::unique_ptr<SampleVoice> sampleVoice;
        int midiNote{-1};
        int chokeGroup{-1};
        bool isActive{false};
    };

    std::vector<Voice> voices;
    std::unique_ptr<DrumKit> currentKit;
    std::unique_ptr<SampleStreamingManager> streamingManager;

    std::atomic<int> maxActiveVoices{64};
    std::atomic<bool> useRoundRobin{true};

    std::array<int, MAX_CHOKE_GROUPS> chokeGroupLastVoice;

    // Track recently played samples per MIDI note to avoid immediate repetition
    struct RecentSampleHistory
    {
        static constexpr int MAX_HISTORY = 10;
        std::array<const SampleLayer*, MAX_HISTORY> samples{nullptr};
        int writePos{0};
        int count{0};

        void addSample(const SampleLayer* layer)
        {
            samples[writePos] = layer;
            writePos = (writePos + 1) % MAX_HISTORY;
            if (count < MAX_HISTORY)
                ++count;
        }

        bool wasRecentlyPlayed(const SampleLayer* layer, int historySize) const
        {
            if (!layer || historySize <= 0)
                return false;

            const int checkCount = juce::jmin(count, historySize);
            for (int i = 0; i < checkCount; ++i)
            {
                const int idx = (writePos - 1 - i + MAX_HISTORY) % MAX_HISTORY;
                if (samples[idx] == layer)
                    return true;
            }
            return false;
        }

        void clear()
        {
            samples.fill(nullptr);
            writePos = 0;
            count = 0;
        }
    };

    std::array<RecentSampleHistory, 128> recentSamples;  // One per MIDI note

    double sampleRate{44100.0};
    int blockSize{512};

    juce::SpinLock voiceLock;

    int findFreeVoice() const;
    void startVoice(int voiceIndex, int midiNote, float velocity, int sampleOffset);
    void stopVoice(int voiceIndex, int sampleOffset);
    void handleChokeGroup(int chokeGroup, int excludeVoice);

    const struct SampleLayer* findBestLayer(const struct DrumPiece* piece,
                                           float velocity, int midiNote);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceManager)
};

} // namespace flam