// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>
#include <array>
#include <atomic>
#include <thread>
#include <mutex>

namespace flam {

struct DrumKit;
struct SampleLayer;
class SampleVoice;
class SampleStreamingManager;

class VoiceManager
{
public:
    static constexpr int MAX_VOICES          = 128;
    static constexpr int MAX_CHOKE_GROUPS    = 16;

    // Extra slots beyond MAX_VOICES reserved for voices that are fading out after
    // a voice steal. The stolen voice is swapped here so the main slot is
    // immediately available for the new note — no allocation on the audio thread.
    static constexpr int FADE_OVERFLOW_SLOTS = 8;
    static constexpr int TOTAL_VOICE_SLOTS   = MAX_VOICES + FADE_OVERFLOW_SLOTS;
    
    VoiceManager();
    ~VoiceManager();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);

    void triggerNote(int midiNote, float velocity, int sampleOffset);
    void releaseNote(int midiNote, int sampleOffset);

    /** Load/replace the kit. Cancels and joins any in-flight preload first, so it is
     *  NOT real-time safe — call only from a non-audio thread. */
    void loadKit(std::unique_ptr<DrumKit> kit);
    /** Clear the kit. Cancels/joins the preload worker — non-audio thread only. */
    void clearKit();

    void setPolyphony(int maxVoices);
    int getPolyphony() const { return maxActiveVoices.load(); }

    int getActiveVoiceCount() const;  // For testing / metering

    void setRoundRobinEnabled(bool enabled) { useRoundRobin.store(enabled); }
    bool isRoundRobinEnabled() const { return useRoundRobin.load(); }

    /** Seed the internal RNG for deterministic/test mode.
     *  Not real-time safe — call only from the non-audio thread before playback starts. */
    void seedRNG(uint64_t seed) noexcept { rng = juce::Random(static_cast<juce::int64>(seed)); }

    /** In offline mode the full sample is loaded into preloadBuffer; no streaming needed. */
    void setOfflineMode(bool offline) { offlineMode.store(offline); }
    bool isOfflineMode() const { return offlineMode.load(); }

    /** Returns false while the background preload thread is still running. */
    bool isKitLoaded() const { return !isKitLoading.load(); }
    /** Blocks the calling thread until kit preloading is complete. CLI/test use only. */
    void waitForKitLoaded() const { while (isKitLoading.load()) juce::Thread::sleep(10); }

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

    // Background preload worker. Owned + joinable (not detached) so it can be cancelled
    // and joined before currentKit / this are destroyed — a detached worker would
    // dereference freed SampleLayers or a dangling this (use-after-free). abortLoad makes
    // a join return promptly; loadMutex serializes concurrent loadKit/clearKit calls.
    // Declared before currentKit so it is destroyed after it (the explicit join in
    // ~VoiceManager is the real guarantee; this ordering is defense in depth).
    std::thread loaderThread;
    std::atomic<bool> abortLoad{false};
    std::mutex loadMutex;

    std::unique_ptr<DrumKit> currentKit;
    std::unique_ptr<SampleStreamingManager> streamingManager;

    std::atomic<int> maxActiveVoices{64};
    std::atomic<bool> useRoundRobin{true};
    std::atomic<bool> offlineMode{false};
    std::atomic<bool> isKitLoading{false};

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

    // Per-instance RNG — replaces juce::Random::getSystemRandom() for seedability.
    // Audio-thread only; seed via seedRNG() before playback.
    juce::Random rng;

    // Cancel + join the preload worker. Caller must hold loadMutex and must NOT hold
    // voiceLock (the worker takes voiceLock to publish → would deadlock).
    void stopBackgroundLoad();

    int findFreeVoice() const;
    // Returns an inactive slot in the FADE_OVERFLOW zone, or -1 if all are busy.
    int findFadeSlot() const;
    void startVoice(int voiceIndex, int midiNote, float velocity, int sampleOffset);
    void stopVoice(int voiceIndex, int sampleOffset);
    void handleChokeGroup(int chokeGroup, int excludeVoice);

    const struct SampleLayer* findBestLayer(const struct DrumPiece* piece,
                                           float velocity, int midiNote);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceManager)
};

} // namespace flam