#include "LimiterProcessor.h"

namespace flam {

LimiterProcessor::LimiterProcessor()
{
    // Set conservative defaults
    thresholdDb.store(-0.1f);  // Just below 0 dBFS
    releaseMs.store(50.0f);     // Fast release for transparency
}

void LimiterProcessor::prepareToPlay(double sr)
{
    sampleRate = sr;
    envelope = 0.0f;
}

void LimiterProcessor::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    // Early return if bypassed
    if (!enabled.load())
        return;

    // Load parameters once per block for consistency
    const float threshold = thresholdDb.load();
    const float release = releaseMs.load() / 1000.0f;  // Convert to seconds

    // Calculate release coefficient
    const float releaseCoeff = std::exp(-1.0f / (release * static_cast<float>(sampleRate)));

    // Process all channels
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        float* data = buffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = data[i];

            // Convert to dB (add small value to avoid log(0))
            const float inputLevelDb = 20.0f * std::log10(std::abs(input) + 1e-6f);

            // Envelope follower with instant attack
            if (inputLevelDb > envelope)
            {
                // Instant attack for limiter
                envelope = inputLevelDb;
            }
            else
            {
                // Release: signal below envelope
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * inputLevelDb;
            }

            // Calculate gain reduction (hard limiting = infinite ratio)
            float gainReductionDb = 0.0f;
            if (envelope > threshold)
            {
                // Reduce by full amount over threshold
                gainReductionDb = envelope - threshold;
            }

            // Convert gain reduction to linear
            const float gain = std::pow(10.0f, -gainReductionDb / 20.0f);

            // Apply gain
            data[i] = input * gain;
        }
    }
}

void LimiterProcessor::setThreshold(float newThreshold)
{
    newThreshold = juce::jlimit(-1.0f, 0.0f, newThreshold);
    thresholdDb.store(newThreshold);
}

void LimiterProcessor::setRelease(float newRelease)
{
    newRelease = juce::jlimit(10.0f, 500.0f, newRelease);
    releaseMs.store(newRelease);
}

void LimiterProcessor::reset()
{
    envelope = 0.0f;
}

} // namespace flam
