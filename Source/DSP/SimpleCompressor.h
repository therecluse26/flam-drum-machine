// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_dsp/juce_dsp.h>

namespace flam {

/**
 * Simple stereo compressor using JUCE's built-in compressor.
 * Provides standard controls: attack, release, hold (via release delay),
 * threshold, ratio, and lookahead.
 */
class SimpleCompressor
{
public:
    SimpleCompressor() = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 2;

        compressor.prepare(spec);
        compressor.reset();

        // Set reasonable defaults
        compressor.setThreshold(-20.0f);
        compressor.setRatio(4.0f);
        compressor.setAttack(10.0f);
        compressor.setRelease(100.0f);
    }

    void reset()
    {
        compressor.reset();
    }

    /**
     * Set compressor parameters
     */
    void setAttack(float attackMs)
    {
        compressor.setAttack(attackMs);
    }

    void setRelease(float releaseMs)
    {
        this->releaseMs = releaseMs;
        // Note: JUCE's compressor doesn't have separate hold parameter,
        // but we can approximate it by adjusting release time
        actualRelease = this->releaseMs + holdMs;
        compressor.setRelease(actualRelease);
    }

    void setHold(float holdMs)
    {
        this->holdMs = holdMs;
        // Update release to include hold time
        actualRelease = releaseMs + this->holdMs;
        compressor.setRelease(actualRelease);
    }

    void setThreshold(float thresholdDB)
    {
        compressor.setThreshold(thresholdDB);
    }

    void setRatio(float ratio)
    {
        compressor.setRatio(ratio);
    }

    void setLookahead(float lookaheadMs)
    {
        // JUCE's Compressor class doesn't have built-in lookahead,
        // but we store it for potential future implementation
        lookahead = lookaheadMs;
        // For now, lookahead is not implemented (would require delay buffer)
    }

    void setMakeupGain(float gainDB)
    {
        makeupGain = juce::Decibels::decibelsToGain(gainDB);
    }

    /**
     * Bypass the compressor (pass audio through unchanged)
     */
    void setBypassed(bool shouldBypassed)
    {
        bypassed = shouldBypassed;
    }

    bool isBypassed() const { return bypassed; }

    /**
     * Get meter readings (call from UI thread)
     */
    float getInputLevel() const { return inputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const { return outputLevel.load(std::memory_order_relaxed); }
    float getGainReduction() const { return gainReduction.load(std::memory_order_relaxed); }

    /**
     * Process stereo audio buffer
     */
    void processBlock(juce::AudioBuffer<float>& buffer)
    {
        if (bypassed)
        {
            // Reset meters when bypassed
            inputLevel.store(0.0f, std::memory_order_relaxed);
            outputLevel.store(0.0f, std::memory_order_relaxed);
            gainReduction.store(0.0f, std::memory_order_relaxed);
            return;  // Pass through unchanged
        }

        // Measure input level
        float inLevel = buffer.getMagnitude(0, 0, buffer.getNumSamples());
        for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
            inLevel = std::max(inLevel, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
        inputLevel.store(inLevel, std::memory_order_relaxed);

        // Apply compression
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        compressor.process(context);

        // Measure level after compression (before makeup gain)
        float compressedLevel = buffer.getMagnitude(0, 0, buffer.getNumSamples());
        for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
            compressedLevel = std::max(compressedLevel, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));

        // Calculate gain reduction (input / compressed, in dB)
        float gr = 0.0f;
        if (compressedLevel > 0.0001f && inLevel > 0.0001f)
            gr = juce::Decibels::gainToDecibels(compressedLevel / inLevel);
        gainReduction.store(gr, std::memory_order_relaxed);

        // Apply makeup gain
        if (makeupGain != 1.0f)
            buffer.applyGain(makeupGain);

        // Measure output level (after makeup gain)
        float outLevel = buffer.getMagnitude(0, 0, buffer.getNumSamples());
        for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
            outLevel = std::max(outLevel, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
        outputLevel.store(outLevel, std::memory_order_relaxed);
    }

private:
    juce::dsp::Compressor<float> compressor;
    float holdMs{0.0f};
    float releaseMs{100.0f};
    float actualRelease{100.0f};
    float lookahead{0.0f};  // Not yet implemented
    float makeupGain{1.0f};
    bool bypassed{false};

    // Thread-safe meter values (read by UI thread, written by audio thread)
    std::atomic<float> inputLevel{0.0f};
    std::atomic<float> outputLevel{0.0f};
    std::atomic<float> gainReduction{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleCompressor)
};

} // namespace flam
