// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "SampleStreamingManager.h"
#include "../Formats/FlamKitLoader.h"

namespace flam {

SampleStreamingManager::SampleStreamingManager()
    : juce::Thread("SampleStreaming")
{
    formatManager.registerBasicFormats();
}

SampleStreamingManager::~SampleStreamingManager()
{
    stopThread(2000);
}

void SampleStreamingManager::prepareToPlay(double newSampleRate, int newBlockSize)
{
    sampleRate = newSampleRate;
    blockSize = newBlockSize;

    // Start streaming thread if not already running
    if (!isThreadRunning())
        startThread(juce::Thread::Priority::high);
}

void SampleStreamingManager::releaseResources()
{
    // Clear all pending requests
    requestFifo.reset();
    dataFifo.reset();
}

int SampleStreamingManager::getPreloadSamples(double sr)
{
    // Calculate samples for PRELOAD_MS milliseconds
    return static_cast<int>((sr * PRELOAD_MS) / 1000.0);
}

int SampleStreamingManager::requestStream(const SampleLayer* layer, int voiceId, double startPosition)
{
    if (!layer || voiceId < 0)
        return -1;

    // Assign unique stream ID
    const int streamId = nextStreamId.fetch_add(1);

    // Try to add to request queue
    int start1, size1, start2, size2;
    requestFifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0)
    {
        auto& request = requestQueue[start1];
        request.layer = layer;
        request.voiceId = voiceId;
        request.streamId = streamId;
        request.startPosition = startPosition;
        request.cancelled.store(false);

        requestFifo.finishedWrite(1);
    }
    // If queue is full, drop request (voice will play only preload portion)

    return streamId;
}

void SampleStreamingManager::cancelStream(int voiceId)
{
    // Mark all pending requests for this voice as cancelled
    const int numReady = requestFifo.getNumReady();
    int start1, size1, start2, size2;
    requestFifo.prepareToRead(numReady, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
    {
        if (requestQueue[start1 + i].voiceId == voiceId)
            requestQueue[start1 + i].cancelled.store(true);
    }

    for (int i = 0; i < size2; ++i)
    {
        if (requestQueue[start2 + i].voiceId == voiceId)
            requestQueue[start2 + i].cancelled.store(true);
    }
}

std::unique_ptr<SampleStreamingManager::StreamedData> SampleStreamingManager::getNextStreamedData()
{
    int start1, size1, start2, size2;
    dataFifo.prepareToRead(1, start1, size1, start2, size2);

    if (size1 > 0)
    {
        auto data = std::move(dataQueue[start1]);
        dataFifo.finishedRead(1);
        return data;
    }

    return nullptr;
}

void SampleStreamingManager::run()
{
    while (!threadShouldExit())
    {
        // Check for stream requests
        int start1, size1, start2, size2;
        requestFifo.prepareToRead(1, start1, size1, start2, size2);

        if (size1 > 0)
        {
            // Don't finish the read yet - we need to check cancellation during processing
            const auto& request = requestQueue[start1];

            if (!request.cancelled.load())
            {
                processStreamRequest(request);
            }

            requestFifo.finishedRead(1);
        }
        else
        {
            // No requests - sleep briefly to avoid busy waiting
            wait(1);
        }
    }
}

void SampleStreamingManager::processStreamRequest(const StreamRequest& request)
{
    if (!request.layer || !request.layer->sampleFile.existsAsFile())
        return;

    // Create reader for this sample
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(request.layer->sampleFile));

    if (!reader)
        return;

    const auto totalSamples = reader->lengthInSamples;
    const auto numChannels = reader->numChannels;
    const auto preloadSamples = getPreloadSamples(reader->sampleRate);

    // Start position is where we left off after preload
    auto currentPosition = static_cast<juce::int64>(request.startPosition);

    // If start position is already at or past end, nothing to stream
    if (currentPosition >= totalSamples)
        return;

    // Stream in chunks until we reach the end or get cancelled
    while (currentPosition < totalSamples && !request.cancelled.load())
    {
        const auto remainingSamples = totalSamples - currentPosition;
        const auto chunkSize = juce::jmin(static_cast<juce::int64>(STREAM_BUFFER_SIZE), remainingSamples);

        // Allocate buffer for this chunk
        auto buffer = std::make_shared<juce::AudioBuffer<float>>(
            static_cast<int>(numChannels),
            static_cast<int>(chunkSize));

        // Read chunk from disk
        if (!reader->read(buffer.get(), 0, static_cast<int>(chunkSize),
                         currentPosition, true, true))
        {
            break;  // Read failed
        }

        // Package the data
        auto data = std::make_unique<StreamedData>();
        data->voiceId = request.voiceId;
        data->streamId = request.streamId;  // Copy stream ID for verification
        data->layer = request.layer;  // Include layer so voice can verify it matches
        data->buffer = buffer;
        data->startPosition = static_cast<double>(currentPosition);
        data->isComplete = (currentPosition + chunkSize >= totalSamples);

        // Try to add to output queue
        int outStart1, outSize1, outStart2, outSize2;
        dataFifo.prepareToWrite(1, outStart1, outSize1, outStart2, outSize2);

        if (outSize1 > 0)
        {
            dataQueue[outStart1] = std::move(data);
            dataFifo.finishedWrite(1);
        }
        else
        {
            // Output queue full - drop this chunk (voice will glitch but won't crash)
            break;
        }

        currentPosition += chunkSize;

        // Check for cancellation again
        if (request.cancelled.load())
            break;
    }
}

} // namespace flam
