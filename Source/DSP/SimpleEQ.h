// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>

namespace flam {

/**
 * Simple 10-band parametric EQ using IIR filters.
 * Each band is a peaking filter at a fixed frequency.
 */
class SimpleEQ
{
public:
    // Standard 10-band graphic EQ frequencies
    static constexpr std::array<float, 10> BAND_FREQUENCIES = {
        31.0f, 62.0f, 125.0f, 250.0f, 500.0f,
        1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    static constexpr float Q_VALUE = 1.0f;  // Q factor for all bands

    SimpleEQ() = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 2;

        for (auto& filter : filters)
            filter.prepare(spec);

        updateFilters(sampleRate);
    }

    void reset()
    {
        for (auto& filter : filters)
            filter.reset();
    }

    /**
     * Set gain for a specific band (0-9)
     * @param bandIndex Band index (0 = 31 Hz, 9 = 16 kHz)
     * @param gainDB Gain in decibels (-12 to +12)
     */
    void setBandGain(int bandIndex, float gainDB)
    {
        if (bandIndex >= 0 && bandIndex < 10)
        {
            bandGains[bandIndex] = gainDB;
            needsUpdate = true;
        }
    }

    /**
     * Bypass the EQ (pass audio through unchanged)
     */
    void setBypassed(bool shouldBypassed)
    {
        bypassed = shouldBypassed;
    }

    bool isBypassed() const { return bypassed; }

    /**
     * Process audio block (call before processing)
     */
    void updateIfNeeded(double sampleRate)
    {
        if (needsUpdate)
        {
            updateFilters(sampleRate);
            needsUpdate = false;
        }
    }

    /**
     * Process stereo audio buffer
     */
    void processBlock(juce::AudioBuffer<float>& buffer)
    {
        if (bypassed)
            return;  // Pass through unchanged

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);

        for (auto& filter : filters)
            filter.process(context);
    }

private:
    using FilterType = juce::dsp::ProcessorDuplicator<
        juce::dsp::IIR::Filter<float>,
        juce::dsp::IIR::Coefficients<float>>;

    std::array<FilterType, 10> filters;
    std::array<float, 10> bandGains{0.0f};
    bool needsUpdate{true};
    bool bypassed{false};

    void updateFilters(double sampleRate)
    {
        for (int i = 0; i < 10; ++i)
        {
            const float freq = BAND_FREQUENCIES[i];
            const float gain = bandGains[i];

            // Use peaking filter for each band
            auto coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                sampleRate, freq, Q_VALUE, juce::Decibels::decibelsToGain(gain));

            *filters[i].state = *coefficients;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SimpleEQ)
};

} // namespace flam
