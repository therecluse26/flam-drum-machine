// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "SaturationProcessor.h"

namespace flam {

SaturationProcessor::SaturationProcessor()
{
    // Default to tape mode with moderate amount
    amount.store(0.5f);
    mode.store(Tape);
}

void SaturationProcessor::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    // Early return if bypassed
    if (!enabled.load())
        return;

    const float amt = amount.load();
    const Mode satMode = static_cast<Mode>(mode.load());

    // Process all channels
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = data[i];
            float output = 0.0f;

            // Apply saturation based on selected mode
            switch (satMode)
            {
                case Tape:
                    output = processTape(input, amt);
                    break;

                case Tube:
                    output = processTube(input, amt);
                    break;

                case Digital:
                    output = processDigital(input, amt);
                    break;
            }

            // Wet/dry blend
            data[i] = input * (1.0f - amt) + output * amt;
        }
    }
}

void SaturationProcessor::setAmount(float newAmount)
{
    // Clamp to valid range
    newAmount = juce::jlimit(0.0f, 1.0f, newAmount);
    amount.store(newAmount);
}

inline float SaturationProcessor::processTape(float input, float amt) const
{
    // Tape-style: soft clipping with asymmetric curve
    // Uses tanh for smooth saturation characteristic
    const float drive = 1.0f + amt * 4.0f;  // Up to 5x drive at max
    return std::tanh(input * drive);
}

inline float SaturationProcessor::processTube(float input, float amt) const
{
    // Tube-style: emphasizes odd harmonics
    // Pre-gain boost followed by soft clipping
    const float boosted = input * (1.0f + amt * std::abs(input));
    return std::tanh(boosted);
}

inline float SaturationProcessor::processDigital(float input, float amt) const
{
    // Digital-style: hard clipping with slight overshoot allowance
    const float threshold = 1.0f + amt * 0.1f;  // Slight overshoot at high amounts
    return juce::jlimit(-threshold, threshold, input);
}

} // namespace flam
