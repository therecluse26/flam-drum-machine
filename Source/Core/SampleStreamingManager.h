// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <atomic>
#include <queue>

namespace flam {

struct SampleLayer;

/**
 * Manages hybrid sample streaming with pre-cached attack portions.
 *
 * Strategy:
 * - Pre-loads first PRELOAD_MS (100ms) of each sample into RAM (the "attack")
 * - Streams the remainder from disk on-demand when samples are triggered
 * - Uses a background thread pool to avoid blocking the audio thread
 * - Provides lock-free handoff between preload and streamed data
 */
class SampleStreamingManager : public juce::Thread
{
public:
    static constexpr int PRELOAD_MS = 100;  // Pre-cache first 100ms of each sample
    static constexpr int STREAM_BUFFER_SIZE = 8192;  // Samples per streaming chunk

    struct StreamRequest
    {
        juce::File sampleFile;  // Copy of the file path — avoids dangling ptr if kit reloads
        int voiceId{-1};
        int streamId{0};  // Unique ID for this stream instance
        double startPosition{0.0};  // Where to start streaming (in samples)
        std::atomic<bool> cancelled{false};
    };

    struct StreamedData
    {
        int voiceId{-1};
        int streamId{0};  // Matches the stream request
        const SampleLayer* layer{nullptr};  // Which layer this data belongs to
        std::shared_ptr<juce::AudioBuffer<float>> buffer;
        double startPosition{0.0};  // Position in original file where this buffer starts
        bool isComplete{false};  // True if this is the final chunk
    };

    SampleStreamingManager();
    ~SampleStreamingManager() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();

    /**
     * Request streaming for a sample (called from audio thread).
     * Returns the stream ID for this request.
     * Data arrives asynchronously via getNextStreamedData().
     */
    int requestStream(const SampleLayer* layer, int voiceId, double startPosition);

    /**
     * Cancel streaming for a voice (e.g., when voice is stolen or stopped).
     */
    void cancelStream(int voiceId);

    /**
     * Poll for streamed data (called from audio thread).
     * Returns nullptr if no data available yet.
     */
    std::unique_ptr<StreamedData> getNextStreamedData();

    /**
     * Get the number of preload samples for a given sample rate.
     */
    static int getPreloadSamples(double sampleRate);

private:
    void run() override;

    void processStreamRequest(const StreamRequest& request);
    void streamSampleChunk(juce::AudioFormatReader* reader, const StreamRequest& request);

    double sampleRate{44100.0};
    int blockSize{512};

    juce::AudioFormatManager formatManager;

    // Lock-free queue for stream requests (audio thread -> streaming thread)
    juce::AbstractFifo requestFifo{32};
    std::array<StreamRequest, 32> requestQueue;

    // Lock-free queue for streamed data (streaming thread -> audio thread)
    juce::AbstractFifo dataFifo{64};
    std::array<std::unique_ptr<StreamedData>, 64> dataQueue;

    // Stream ID counter for unique stream identification
    std::atomic<int> nextStreamId{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleStreamingManager)
};

} // namespace flam
