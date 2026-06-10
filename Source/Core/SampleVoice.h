// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <memory>

namespace flam {

// Forward declaration
struct SampleLayer;

/**
 * Real-time safe sample playback voice with velocity layers,
 * round-robin support, and ADSR envelope shaping.
 */
class SampleVoice
{
public:
    SampleVoice();
    ~SampleVoice();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void reset();

    /**
     * Trigger a new note with the given layer, velocity, and sample offset.
     * This is real-time safe - no allocations occur.
     */
    void startNote(const SampleLayer* layer, float velocity, int sampleOffset);

    /**
     * Release the note (enter release phase of ADSR)
     */
    void stopNote(int sampleOffset);

    /**
     * Force a fast release for retriggering (prevents pops but cleans up quickly)
     * This temporarily overrides the envelope release time to create a smooth crossfade
     */
    void forceQuickRelease();

    /**
     * Render the next block of samples
     * @param outputBuffer Buffer to write samples to (will be added, not replaced)
     * @param startSample Starting sample index in the buffer
     * @param numSamples Number of samples to render
     */
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                         int startSample, int numSamples);

    /**
     * Check if this voice is currently active and producing sound
     */
    bool isActive() const { return active.load(std::memory_order_acquire); }

    /**
     * Get the current sample position in the playing sample
     */
    int getSamplePosition() const { return static_cast<int>(playbackPosition); }

    /**
     * Get the age of this voice (in samples since triggered)
     */
    int getAge() const { return age; }

    /**
     * Set ADSR envelope parameters
     */
    void setEnvelopeParameters(float attack, float hold, float decay,
                               float sustain, float release);

    /**
     * Load sample data for streaming playback
     * @param layer Pointer to sample layer with preload buffer
     * @param voiceId Voice ID for stream tracking
     * @param streamId Unique stream ID for this playback instance
     */
    void loadSampleData(const SampleLayer* layer, int voiceId, int streamId);

    /**
     * Append streamed data from disk (called from audio thread when available)
     */
    void appendStreamedChunk(std::shared_ptr<juce::AudioBuffer<float>> chunk, bool isLastChunk);

    /**
     * Check if this voice needs streaming (preload consumed)
     */
    bool needsStreaming() const;

    /**
     * Mark that streaming has been requested for this voice
     */
    void markStreamingRequested() { streamingRequested = true; }

    /**
     * Get the voice ID for stream management
     */
    int getVoiceId() const { return voiceId; }

    /**
     * Get the current stream ID for verification
     */
    int getCurrentStreamId() const { return currentStreamId; }

    /**
     * Get pointer to the sample layer being played
     */
    const SampleLayer* getCurrentLayer() const { return currentLayer; }

private:
    std::atomic<bool> active{false};

    // Streaming buffers
    std::shared_ptr<juce::AudioBuffer<float>> preloadBuffer;  // Initial cached portion
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> streamedChunks;  // Chunks from disk

    const SampleLayer* currentLayer{nullptr};
    int voiceId{-1};
    int currentStreamId{-1};  // Unique ID for current stream instance
    juce::int64 totalSampleLength{0};
    int preloadSampleCount{0};
    bool streamingComplete{false};
    bool streamingRequested{false};

    double playbackPosition{0.0};
    double playbackRate{1.0};
    double sourceSampleRate{44100.0};
    double targetSampleRate{44100.0};

    float velocity{0.0f};
    float gain{1.0f};
    int age{0};

    juce::ADSR envelope;
    juce::ADSR::Parameters envParams;

    // Interpolation state for high-quality resampling
    float lastSample[2]{0.0f, 0.0f};

    // Real-time safe sample reading with linear interpolation
    float readSample(int channel, double position) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleVoice)
};

} // namespace flam
