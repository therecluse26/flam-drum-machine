#include "CompressorProcessor.h"

namespace flam {

CompressorProcessor::CompressorProcessor()
{
    // Set sensible defaults
    thresholdDb.store(-10.0f);
    ratio.store(4.0f);
    attackMs.store(5.0f);
    releaseMs.store(100.0f);
    makeupGainDb.store(0.0f);
}

void CompressorProcessor::prepareToPlay(double sr)
{
    sampleRate = sr;
    envelope = 0.0f;
}

void CompressorProcessor::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    // Early return if bypassed
    if (!enabled.load())
        return;

    // Load parameters once per block for consistency
    const float threshold = thresholdDb.load();
    const float compressionRatio = ratio.load();
    const float attack = attackMs.load() / 1000.0f;  // Convert to seconds
    const float release = releaseMs.load() / 1000.0f;
    const float makeupGain = std::pow(10.0f, makeupGainDb.load() / 20.0f);  // dB to linear

    // Calculate envelope coefficients
    const float attackCoeff = std::exp(-1.0f / (attack * static_cast<float>(sampleRate)));
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

            // Envelope follower
            if (inputLevelDb > envelope)
            {
                // Attack: signal above envelope
                envelope = attackCoeff * envelope + (1.0f - attackCoeff) * inputLevelDb;
            }
            else
            {
                // Release: signal below envelope
                envelope = releaseCoeff * envelope + (1.0f - releaseCoeff) * inputLevelDb;
            }

            // Calculate gain reduction
            float gainReductionDb = 0.0f;
            if (envelope > threshold)
            {
                // Amount over threshold, scaled by ratio
                gainReductionDb = (envelope - threshold) * (1.0f - 1.0f / compressionRatio);
            }

            // Convert gain reduction to linear and apply makeup gain
            const float gain = std::pow(10.0f, -gainReductionDb / 20.0f) * makeupGain;

            // Apply gain
            data[i] = input * gain;
        }
    }
}

void CompressorProcessor::setThreshold(float newThreshold)
{
    newThreshold = juce::jlimit(-60.0f, 0.0f, newThreshold);
    thresholdDb.store(newThreshold);
}

void CompressorProcessor::setRatio(float newRatio)
{
    newRatio = juce::jlimit(1.0f, 20.0f, newRatio);
    ratio.store(newRatio);
}

void CompressorProcessor::setAttack(float newAttack)
{
    newAttack = juce::jlimit(0.1f, 100.0f, newAttack);
    attackMs.store(newAttack);
}

void CompressorProcessor::setRelease(float newRelease)
{
    newRelease = juce::jlimit(10.0f, 1000.0f, newRelease);
    releaseMs.store(newRelease);
}

void CompressorProcessor::setMakeupGain(float gainDb)
{
    gainDb = juce::jlimit(0.0f, 24.0f, gainDb);
    makeupGainDb.store(gainDb);
}

void CompressorProcessor::reset()
{
    envelope = 0.0f;
}

} // namespace flam
