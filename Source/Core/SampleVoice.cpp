// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "SampleVoice.h"
#include "../Formats/FlamKitLoader.h"

namespace flam {

SampleVoice::SampleVoice()
{
    // Default ADSR: fast attack, no hold, short decay, full sustain, short release
    envParams.attack = 0.001f;  // 1ms attack
    envParams.decay = 0.1f;     // 100ms decay
    envParams.sustain = 1.0f;   // Full sustain
    envParams.release = 0.05f;  // 50ms release
}

SampleVoice::~SampleVoice() = default;

void SampleVoice::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    targetSampleRate = sampleRate;
    envelope.setSampleRate(sampleRate);
    envelope.setParameters(envParams);
    reset();
}

void SampleVoice::reset()
{
    active.store(false, std::memory_order_release);
    playbackPosition = 0.0;
    age = 0;
    velocity = 0.0f;
    envelope.reset();
    lastSample[0] = lastSample[1] = 0.0f;

    // Clear fade-out state
    fadingOut = false;
    fadeOutSamplesRemaining = 0;
    fadeOutTotalSamples = 1;

    // Clear streaming state
    preloadBuffer.reset();
    streamedChunks.clear();
    currentLayer = nullptr;
    voiceId = -1;
    currentStreamId = -1;
    streamingComplete = false;
    streamingRequested = false;
}

void SampleVoice::startNote(const SampleLayer* layer, float noteVelocity, int sampleOffset)
{
    if (!layer)
        return;

    // Store velocity and gain from layer
    velocity = noteVelocity;
    gain = layer->gain;

    // Reset playback state
    playbackPosition = 0.0;
    age = sampleOffset;

    // Calculate playback rate for sample rate conversion
    if (sourceSampleRate > 0.0)
        playbackRate = sourceSampleRate / targetSampleRate;
    else
        playbackRate = 1.0;

    // Trigger envelope
    envelope.noteOn();

    // Mark as active
    active.store(true, std::memory_order_release);
}

void SampleVoice::stopNote(int sampleOffset)
{
    juce::ignoreUnused(sampleOffset);
    envelope.noteOff();
}

void SampleVoice::forceQuickRelease()
{
    // Override envelope parameters to force a 20ms release
    // This creates a smooth crossfade when retriggering without pops
    // 20ms gives natural overlap while still cleaning up quickly
    juce::ADSR::Parameters fastParams = envParams;
    fastParams.release = 0.040f;  // 40ms quick release
    envelope.setParameters(fastParams);
    envelope.noteOff();
}

void SampleVoice::beginFadeOut(int durationSamples)
{
    // Start a per-sample linear fade ramp applied multiplicatively on top of the
    // ADSR. Used for voice stealing so the outgoing voice ramps to silence
    // instead of hard-cutting. Real-time safe — no allocation.
    const int safeLen    = juce::jmax(1, durationSamples);
    fadeOutTotalSamples  = safeLen;
    fadeOutSamplesRemaining = safeLen;
    fadingOut            = true;
    streamingRequested   = true;  // no new stream data needed while fading
}

void SampleVoice::loadSampleData(const SampleLayer* layer, int newVoiceId, int newStreamId)
{
    if (!layer || !layer->preloadBuffer)
        return;

    // Store layer, voice ID, and stream ID
    currentLayer = layer;
    voiceId = newVoiceId;
    currentStreamId = newStreamId;

    // Load preload buffer
    preloadBuffer = layer->preloadBuffer;
    preloadSampleCount = preloadBuffer->getNumSamples();
    sourceSampleRate = layer->sourceSampleRate;
    totalSampleLength = layer->totalSampleLength;

    // Calculate playback rate for sample rate conversion
    if (sourceSampleRate > 0.0 && targetSampleRate > 0.0)
        playbackRate = sourceSampleRate / targetSampleRate;
    else
        playbackRate = 1.0;

    // Reset streaming state
    streamedChunks.clear();
    streamingComplete = false;
    streamingRequested = false;
}

void SampleVoice::appendStreamedChunk(std::shared_ptr<juce::AudioBuffer<float>> chunk, bool isLastChunk)
{
    if (!chunk)
        return;

    streamedChunks.push_back(chunk);

    if (isLastChunk)
        streamingComplete = true;
}

bool SampleVoice::needsStreaming() const
{
    // Need streaming if:
    // 1. We're active
    // 2. There's more data to stream (not complete)
    // 3. We haven't already requested streaming
    // 4. We're getting close to end of preload buffer
    if (!active.load(std::memory_order_acquire) || streamingComplete || streamingRequested)
        return false;

    const int currentPos = static_cast<int>(playbackPosition);
    const int preloadThreshold = preloadSampleCount / 2;  // Start streaming at 50% through preload

    return currentPos >= preloadThreshold;
}

void SampleVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                   int startSample, int numSamples)
{
    if (!active.load(std::memory_order_acquire))
        return;

    if (!preloadBuffer || preloadBuffer->getNumSamples() == 0)
    {
        active.store(false, std::memory_order_release);
        return;
    }

    const int numChannels = juce::jmin(outputBuffer.getNumChannels(),
                                       preloadBuffer->getNumChannels());

    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (playbackPosition >= totalSampleLength)
        {
            active.store(false, std::memory_order_release);
            break;
        }

        float envelopeValue;
        bool  shouldStop = false;

        if (fadingOut)
        {
            // Linear ramp from 1→0 over fadeOutTotalSamples, multiplied with ADSR.
            // This ensures voice-stolen audio ramps to silence without a click even
            // when the ADSR is at full sustain.
            const float fadeMultiplier =
                static_cast<float>(fadeOutSamplesRemaining) /
                static_cast<float>(fadeOutTotalSamples);

            if (--fadeOutSamplesRemaining <= 0)
            {
                fadingOut  = false;
                shouldStop = true;
            }

            envelopeValue = envelope.getNextSample() * fadeMultiplier;

            if (!envelope.isActive() && !shouldStop)
                shouldStop = true;
        }
        else
        {
            envelopeValue = envelope.getNextSample();
            if (!envelope.isActive())
                shouldStop = true;
        }

        const float finalGain = gain * velocity * envelopeValue;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float sampleValue = readSample(channel, playbackPosition);
            outputBuffer.addSample(channel, startSample + sample,
                                  sampleValue * finalGain);
        }

        playbackPosition += playbackRate;
        ++age;

        if (shouldStop)
        {
            active.store(false, std::memory_order_release);
            break;
        }
    }
}

void SampleVoice::setEnvelopeParameters(float attack, float hold, float decay,
                                        float sustain, float release)
{
    juce::ignoreUnused(hold); // JUCE ADSR doesn't support hold time

    envParams.attack = juce::jmax(0.001f, attack);
    envParams.decay = juce::jmax(0.001f, decay);
    envParams.sustain = juce::jlimit(0.0f, 1.0f, sustain);
    envParams.release = juce::jmax(0.001f, release);

    envelope.setParameters(envParams);
}

float SampleVoice::readSample(int channel, double position) const
{
    const int posInt = static_cast<int>(position);

    // Check if we're still in the preload buffer
    if (posInt < preloadSampleCount)
    {
        if (!preloadBuffer || channel >= preloadBuffer->getNumChannels())
            return 0.0f;

        const float* channelData = preloadBuffer->getReadPointer(channel);
        const float sample0 = channelData[posInt];

        // Linear interpolation
        if (posInt + 1 < preloadSampleCount)
        {
            const float sample1 = channelData[posInt + 1];
            const float fraction = static_cast<float>(position - posInt);
            return sample0 + fraction * (sample1 - sample0);
        }

        return sample0;
    }

    // We're beyond preload - read from streamed chunks
    int chunkOffset = posInt - preloadSampleCount;
    int chunkSamplesPassed = 0;

    for (const auto& chunk : streamedChunks)
    {
        const int chunkSize = chunk->getNumSamples();

        if (chunkOffset < chunkSamplesPassed + chunkSize)
        {
            // This chunk contains our sample
            const int indexInChunk = chunkOffset - chunkSamplesPassed;

            if (channel >= chunk->getNumChannels())
                return 0.0f;

            const float* channelData = chunk->getReadPointer(channel);
            const float sample0 = channelData[indexInChunk];

            // Linear interpolation
            if (indexInChunk + 1 < chunkSize)
            {
                const float sample1 = channelData[indexInChunk + 1];
                const float fraction = static_cast<float>(position - posInt);
                return sample0 + fraction * (sample1 - sample0);
            }

            return sample0;
        }

        chunkSamplesPassed += chunkSize;
    }

    // Sample not available yet (still streaming) - return silence
    return 0.0f;
}

} // namespace flam
