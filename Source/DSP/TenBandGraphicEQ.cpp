// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "TenBandGraphicEQ.h"
#include <cmath>

namespace flam {

TenBandGraphicEQ::TenBandGraphicEQ()
{
    // Initialize all band gains to 0 dB (flat response)
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        bandGains[i].store(0.0f);
    }
}

void TenBandGraphicEQ::prepareToPlay(double sr, int maximumBlockSize)
{
    sampleRate = sr;

    // Reset all filter state
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        filters[i].reset();
    }

    // Force coefficient update on next process call
    coefficientsNeedUpdate.store(true);
}

void TenBandGraphicEQ::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    // Early return if bypassed
    if (!enabled.load())
        return;

    // Update filter coefficients if parameters changed
    if (coefficientsNeedUpdate.load())
    {
        updateCoefficients();
        coefficientsNeedUpdate.store(false);
    }

    // Process each band filter in series using JUCE's DSP framework
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        filters[i].process(context);
    }
}

void TenBandGraphicEQ::setBandGain(int bandIndex, float gainDb)
{
    if (bandIndex < 0 || bandIndex >= NUM_BANDS)
        return;

    // Clamp to valid range
    gainDb = juce::jlimit(-12.0f, 12.0f, gainDb);

    bandGains[bandIndex].store(gainDb);

    // Signal that coefficients need update on next audio callback
    coefficientsNeedUpdate.store(true);
}

float TenBandGraphicEQ::getBandGain(int bandIndex) const
{
    if (bandIndex < 0 || bandIndex >= NUM_BANDS)
        return 0.0f;

    return bandGains[bandIndex].load();
}

void TenBandGraphicEQ::reset()
{
    for (int i = 0; i < NUM_BANDS; ++i)
    {
        bandGains[i].store(0.0f);
    }

    coefficientsNeedUpdate.store(true);
}

void TenBandGraphicEQ::updateCoefficients()
{
    // Butterworth Q factor for smooth frequency response
    constexpr float q = 1.0f / std::sqrt(2.0f);

    for (int i = 0; i < NUM_BANDS; ++i)
    {
        const float frequency = BAND_FREQUENCIES[i];
        const float gainDb = bandGains[i].load();

        // Convert dB to linear gain
        const float linearGain = juce::Decibels::decibelsToGain(gainDb);

        // Create peaking filter coefficients
        auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            sampleRate,
            frequency,
            q,
            linearGain
        );

        // Update filter coefficients (thread-safe in JUCE)
        *filters[i].coefficients = *coeffs;
    }
}

} // namespace flam
