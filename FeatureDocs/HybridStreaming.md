# Hybrid Streaming Feature Specification

**Status:** v1.0 MVP Core Feature
**Priority:** Critical
**Dependencies:** `SampleLoader`, `VoiceManager`

---

## Overview

Implement memory-efficient hybrid streaming system that keeps RAM usage at 50-100MB per kit (vs. 1-2GB for competitors) while maintaining <5ms latency and zero audio dropouts. Only the first 100ms of each sample is preloaded into RAM, with the remaining tail streamed from disk on-demand.

---

## Technical Requirements

### Core Functionality

1. **Preload Strategy**
   - Load first 100ms of each sample into RAM (attack transient)
   - Smart caching: fully load 3-4 most frequently triggered samples
   - Streaming buffer: circular buffer for tail playback from disk

2. **Disk Streaming**
   - Background thread reads sample tails from WAV files
   - Lock-free FIFO queue feeds audio thread
   - Prefetch strategy based on voice activity prediction

3. **Performance Targets**
   - RAM usage: 50-100MB per full kit (10-20x improvement)
   - CPU overhead: <5% for streaming thread
   - Latency: Zero additional latency (100ms preload covers attack)
   - No dropouts: Robust buffering with underrun protection

---

## Implementation Details

### Class Structure

```cpp
// Source/Core/HybridStreamingLoader.h
class HybridStreamingLoader
{
public:
    HybridStreamingLoader(int maxVoices = 128);
    ~HybridStreamingLoader();

    // Load kit and preload attack portions
    bool loadKit(const KitMetadata& metadata, const juce::File& kitDirectory);

    // Unload kit and free memory
    void unloadKit();

    // Request streaming data for a voice (called from audio thread)
    const StreamingBuffer* requestStreamingBuffer(
        int sampleIndex,
        int velocityLayer,
        int roundRobinIndex
    );

    // Release streaming buffer when voice finishes (audio thread)
    void releaseStreamingBuffer(const StreamingBuffer* buffer);

    // Get preloaded attack portion (audio thread)
    const AudioBuffer<float>* getPreloadedAttack(
        int sampleIndex,
        int velocityLayer,
        int roundRobinIndex
    ) const;

    // Background thread processing
    void run();

private:
    struct PreloadedSample
    {
        AudioBuffer<float> attackBuffer;  // First 100ms
        juce::File sampleFile;            // Path to full WAV file
        int64 tailStartFrame;             // Frame offset where tail begins
        int64 totalFrames;                // Total sample length
        std::atomic<int> triggerCount{0}; // For smart caching heuristics
    };

    struct StreamingBuffer
    {
        AudioBuffer<float> circularBuffer;  // Circular buffer for tail data
        std::atomic<int> writePos{0};
        std::atomic<int> readPos{0};
        std::atomic<bool> isActive{false};
        juce::File sourceFile;
        int64 currentReadFrame{0};
    };

    // Sample storage
    std::vector<std::vector<std::vector<PreloadedSample>>> samples;  // [piece][velocity][roundRobin]

    // Streaming infrastructure
    std::array<StreamingBuffer, 128> streamingBuffers;  // One per voice
    juce::AbstractFifo availableBufferQueue{128};
    juce::AbstractFifo activeStreamQueue{128};

    // Smart caching
    std::vector<int> fullyCachedSamples;  // Indices of fully loaded samples
    static constexpr int NUM_FULLY_CACHED = 4;

    // Background streaming thread
    juce::Thread streamingThread{"Sample Streaming"};
    std::atomic<bool> shouldExit{false};

    // Constants
    static constexpr int PRELOAD_MS = 100;
    static constexpr int STREAMING_BUFFER_SIZE = 8192;  // Frames per chunk

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HybridStreamingLoader)
};
```

### Kit Loading (Background Thread)

```cpp
bool HybridStreamingLoader::loadKit(const KitMetadata& metadata, const juce::File& kitDirectory)
{
    unloadKit();  // Clear previous kit

    const int sampleRate = 44100;  // Will be configurable
    const int preloadFrames = (PRELOAD_MS * sampleRate) / 1000;

    // Iterate through all samples in kit
    for (const auto& piece : metadata.getPieces())
    {
        for (const auto& velocityLayer : piece.velocityLayers)
        {
            for (const auto& roundRobin : velocityLayer.roundRobins)
            {
                juce::File sampleFile = kitDirectory.getChildFile(roundRobin.filepath);

                if (!sampleFile.existsAsFile())
                {
                    jassertfalse;
                    continue;
                }

                // Load full sample temporarily to extract attack
                std::unique_ptr<AudioFormatReader> reader(
                    formatManager.createReaderFor(sampleFile)
                );

                if (reader == nullptr)
                    continue;

                PreloadedSample preloaded;
                preloaded.sampleFile = sampleFile;
                preloaded.totalFrames = reader->lengthInSamples;

                // Calculate preload length
                const int numFramesToPreload = std::min(
                    preloadFrames,
                    static_cast<int>(reader->lengthInSamples)
                );

                // Allocate attack buffer
                preloaded.attackBuffer.setSize(
                    reader->numChannels,
                    numFramesToPreload,
                    false,
                    true,
                    false
                );

                // Read attack portion
                reader->read(
                    &preloaded.attackBuffer,
                    0,
                    numFramesToPreload,
                    0,
                    true,
                    true
                );

                preloaded.tailStartFrame = numFramesToPreload;

                // Store preloaded sample
                samples[piece.index][velocityLayer.index][roundRobin.index] = std::move(preloaded);
            }
        }
    }

    // Identify most common samples for full caching
    updateSmartCache();

    // Start streaming thread
    streamingThread.startThread(juce::Thread::Priority::high);

    return true;
}
```

### Smart Caching Logic

```cpp
void HybridStreamingLoader::updateSmartCache()
{
    // Heuristic: fully cache kick, snare, closed hi-hat (most common hits)
    // Can be refined based on actual trigger counts during playback

    std::vector<std::pair<int, int>> candidatesByTriggerCount;

    for (size_t piece = 0; piece < samples.size(); ++piece)
    {
        for (size_t vel = 0; vel < samples[piece].size(); ++vel)
        {
            for (size_t rr = 0; rr < samples[piece][vel].size(); ++rr)
            {
                int triggers = samples[piece][vel][rr].triggerCount.load();
                candidatesByTriggerCount.push_back({triggers, /* sampleIndex */ });
            }
        }
    }

    // Sort by trigger count (descending)
    std::sort(candidatesByTriggerCount.begin(), candidatesByTriggerCount.end(),
        [](auto& a, auto& b) { return a.first > b.first; });

    // Fully load top N samples
    fullyCachedSamples.clear();
    for (int i = 0; i < std::min(NUM_FULLY_CACHED, (int)candidatesByTriggerCount.size()); ++i)
    {
        int sampleIdx = candidatesByTriggerCount[i].second;
        loadFullSample(sampleIdx);
        fullyCachedSamples.push_back(sampleIdx);
    }
}

void HybridStreamingLoader::loadFullSample(int sampleIndex)
{
    // Load entire sample into attackBuffer (replace preload-only version)
    // This is called from background thread during kit load or cache update

    // Implementation: re-read full WAV file into attackBuffer
    // Set tailStartFrame = totalFrames (no tail to stream)
}
```

### Audio Thread: Request Streaming Buffer

```cpp
const StreamingBuffer* HybridStreamingLoader::requestStreamingBuffer(
    int sampleIndex,
    int velocityLayer,
    int roundRobinIndex
)
{
    const auto& preloaded = samples[sampleIndex][velocityLayer][roundRobinIndex];

    // Check if sample is fully cached
    if (std::find(fullyCachedSamples.begin(), fullyCachedSamples.end(), sampleIndex)
        != fullyCachedSamples.end())
    {
        // No streaming needed - entire sample in attackBuffer
        return nullptr;
    }

    // Check if sample has no tail (short sample)
    if (preloaded.tailStartFrame >= preloaded.totalFrames)
        return nullptr;

    // Get available streaming buffer from pool
    int bufferIndex = -1;
    {
        int start1, size1, start2, size2;
        availableBufferQueue.prepareToRead(1, start1, size1, start2, size2);

        if (size1 > 0)
        {
            // TODO: Read buffer index from queue
            bufferIndex = start1;
            availableBufferQueue.finishedRead(1);
        }
    }

    if (bufferIndex == -1)
    {
        // No available buffers - voice stealing should handle this
        jassertfalse;
        return nullptr;
    }

    // Configure streaming buffer
    StreamingBuffer& buffer = streamingBuffers[bufferIndex];
    buffer.sourceFile = preloaded.sampleFile;
    buffer.currentReadFrame = preloaded.tailStartFrame;
    buffer.writePos.store(0);
    buffer.readPos.store(0);
    buffer.isActive.store(true);

    // Add to active stream queue for background thread
    {
        int start1, size1, start2, size2;
        activeStreamQueue.prepareToWrite(1, start1, size1, start2, size2);

        if (size1 > 0)
        {
            // TODO: Write buffer index to queue
            activeStreamQueue.finishedWrite(1);
        }
    }

    // Increment trigger count for smart caching heuristic
    preloaded.triggerCount.fetch_add(1);

    return &buffer;
}
```

### Background Thread: Stream Sample Tails

```cpp
void HybridStreamingLoader::run()
{
    while (!shouldExit.load())
    {
        // Process active streaming buffers
        int start1, size1, start2, size2;
        activeStreamQueue.prepareToRead(1, start1, size1, start2, size2);

        if (size1 == 0)
        {
            // No active streams, sleep briefly
            juce::Thread::sleep(1);
            continue;
        }

        // TODO: Get buffer index from queue
        int bufferIndex = start1;
        StreamingBuffer& buffer = streamingBuffers[bufferIndex];

        if (!buffer.isActive.load())
        {
            activeStreamQueue.finishedRead(1);
            continue;
        }

        // Open WAV file and seek to current read position
        std::unique_ptr<AudioFormatReader> reader(
            formatManager.createReaderFor(buffer.sourceFile)
        );

        if (reader == nullptr)
        {
            buffer.isActive.store(false);
            activeStreamQueue.finishedRead(1);
            continue;
        }

        // Read next chunk into circular buffer
        const int writePos = buffer.writePos.load();
        const int readPos = buffer.readPos.load();
        const int availableSpace = (readPos - writePos - 1 + buffer.circularBuffer.getNumSamples())
                                    % buffer.circularBuffer.getNumSamples();

        if (availableSpace < STREAMING_BUFFER_SIZE)
        {
            // Buffer full, skip this iteration
            activeStreamQueue.finishedRead(1);
            continue;
        }

        // Read from file
        const int framesToRead = std::min(
            STREAMING_BUFFER_SIZE,
            static_cast<int>(reader->lengthInSamples - buffer.currentReadFrame)
        );

        if (framesToRead <= 0)
        {
            // End of sample reached
            buffer.isActive.store(false);
            activeStreamQueue.finishedRead(1);

            // Return buffer to available pool
            int freeStart1, freeSize1, freeStart2, freeSize2;
            availableBufferQueue.prepareToWrite(1, freeStart1, freeSize1, freeStart2, freeSize2);
            if (freeSize1 > 0)
            {
                // TODO: Write buffer index to available queue
                availableBufferQueue.finishedWrite(1);
            }

            continue;
        }

        // Temporary buffer for reading
        AudioBuffer<float> tempBuffer(
            reader->numChannels,
            framesToRead
        );

        reader->read(
            &tempBuffer,
            0,
            framesToRead,
            buffer.currentReadFrame,
            true,
            true
        );

        // Copy to circular buffer
        for (int ch = 0; ch < tempBuffer.getNumChannels(); ++ch)
        {
            float* circBuf = buffer.circularBuffer.getWritePointer(ch);
            const float* src = tempBuffer.getReadPointer(ch);

            for (int i = 0; i < framesToRead; ++i)
            {
                circBuf[(writePos + i) % buffer.circularBuffer.getNumSamples()] = src[i];
            }
        }

        // Update write position atomically
        buffer.writePos.store((writePos + framesToRead) % buffer.circularBuffer.getNumSamples());
        buffer.currentReadFrame += framesToRead;

        activeStreamQueue.finishedRead(1);
    }
}
```

### Voice Integration

```cpp
// Source/Core/SampleVoice.cpp
void SampleVoice::startNote(int midiNoteNumber, float velocity, ...)
{
    // Get preloaded attack
    attackBuffer = streamingLoader->getPreloadedAttack(sampleIndex, velLayer, rrIndex);

    // Request streaming buffer for tail (if needed)
    streamingBuffer = streamingLoader->requestStreamingBuffer(sampleIndex, velLayer, rrIndex);

    currentPhase = Phase::Attack;
    playbackPosition = 0;
}

void SampleVoice::renderNextBlock(AudioBuffer<float>& output, int startSample, int numSamples)
{
    if (currentPhase == Phase::Attack)
    {
        // Play from preloaded attack buffer
        const int framesToRender = std::min(
            numSamples,
            attackBuffer->getNumSamples() - playbackPosition
        );

        for (int ch = 0; ch < attackBuffer->getNumChannels(); ++ch)
        {
            output.copyFrom(ch, startSample,
                *attackBuffer, ch,
                playbackPosition,
                framesToRender
            );
        }

        playbackPosition += framesToRender;

        if (playbackPosition >= attackBuffer->getNumSamples())
        {
            // Transition to streaming phase
            if (streamingBuffer != nullptr)
            {
                currentPhase = Phase::Streaming;
                playbackPosition = 0;
            }
            else
            {
                // Short sample, no tail
                currentPhase = Phase::Finished;
            }
        }
    }
    else if (currentPhase == Phase::Streaming)
    {
        // Read from streaming buffer's circular buffer
        const int readPos = streamingBuffer->readPos.load();
        const int writePos = streamingBuffer->writePos.load();
        const int available = (writePos - readPos + streamingBuffer->circularBuffer.getNumSamples())
                              % streamingBuffer->circularBuffer.getNumSamples();

        const int framesToRender = std::min(numSamples, available);

        if (framesToRender == 0)
        {
            // Underrun - output silence and hope buffer fills
            // TODO: Underrun detection/logging
            return;
        }

        for (int ch = 0; ch < streamingBuffer->circularBuffer.getNumChannels(); ++ch)
        {
            const float* circBuf = streamingBuffer->circularBuffer.getReadPointer(ch);

            for (int i = 0; i < framesToRender; ++i)
            {
                output.addSample(ch, startSample + i,
                    circBuf[(readPos + i) % streamingBuffer->circularBuffer.getNumSamples()]
                );
            }
        }

        // Update read position atomically
        streamingBuffer->readPos.store((readPos + framesToRender) % streamingBuffer->circularBuffer.getNumSamples());
        playbackPosition += framesToRender;
    }
}
```

---

## Testing Requirements

1. **Memory Usage**
   - Measure RAM usage with full kit loaded
   - Verify 50-100MB target for typical 8-channel kit
   - Test with large kits (16 channels, 1000+ samples)

2. **Streaming Performance**
   - Monitor streaming thread CPU usage (<5% target)
   - Test with 128 simultaneous voices
   - Verify zero dropouts during stress test

3. **Edge Cases**
   - Very short samples (<100ms) - no streaming needed
   - Very long samples (>10 seconds) - sustained streaming
   - Rapid voice triggering - buffer pool exhaustion
   - Disk I/O delays - verify underrun recovery

4. **Smart Caching**
   - Play kick drum 100 times, verify it gets fully cached
   - Measure cache hit rate during realistic performance

---

## Real-Time Safety Constraints

- **Audio Thread:**
  - ✅ `getPreloadedAttack()` - lock-free read
  - ✅ `requestStreamingBuffer()` - lock-free FIFO operations
  - ✅ `releaseStreamingBuffer()` - lock-free FIFO write
  - ✅ Read from streaming circular buffer - atomic reads only
  - ❌ **No file I/O, no allocations, no locks**

- **Background Thread:**
  - Disk I/O allowed (reading WAV files)
  - Allocation allowed during kit load
  - Lock-free communication with audio thread via FIFO queues

---

## Performance Optimizations

1. **Prefetching**
   - Predict upcoming voices based on MIDI input
   - Start streaming before voice actually triggers
   - Reduces likelihood of underruns

2. **Buffer Sizing**
   - Circular buffer size: 8192 frames (185ms @ 44.1kHz)
   - Provides cushion for disk I/O latency
   - Trade-off: larger buffers = more RAM, less underruns

3. **File Handle Pooling**
   - Keep WAV file handles open for frequently accessed samples
   - Reduces open/close overhead on streaming thread

---

## Future Enhancements (Post-v1.0)

- Adaptive preload length based on sample onset characteristics
- SSD vs HDD detection with different buffer strategies
- Memory pressure detection with dynamic cache adjustment
- Streaming from compressed formats (if viable)
