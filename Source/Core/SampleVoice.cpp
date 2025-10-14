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

void SampleVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                   int startSample, int numSamples)
{
    if (!active.load(std::memory_order_acquire))
        return;

    // Get shared pointer to sample buffer (thread-safe atomic load)
    auto buffer = sampleBuffer;
    if (!buffer || buffer->getNumSamples() == 0)
    {
        active.store(false, std::memory_order_release);
        return;
    }

    const int numChannels = juce::jmin(outputBuffer.getNumChannels(),
                                       buffer->getNumChannels());
    const int sampleLength = buffer->getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Check if we've reached the end of the sample
        if (playbackPosition >= sampleLength)
        {
            active.store(false, std::memory_order_release);
            break;
        }

        // Get envelope value
        const float envelopeValue = envelope.getNextSample();

        // Check if envelope has finished
        if (!envelope.isActive())
        {
            active.store(false, std::memory_order_release);
            break;
        }

        // Calculate final gain (layer gain * velocity * envelope)
        const float finalGain = gain * velocity * envelopeValue;

        // Read and interpolate samples for each channel
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float sampleValue = readSample(channel, playbackPosition);
            outputBuffer.addSample(channel, startSample + sample,
                                  sampleValue * finalGain);
        }

        // Advance playback position with sample rate conversion
        playbackPosition += playbackRate;
        ++age;
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

void SampleVoice::loadSampleData(std::shared_ptr<juce::AudioBuffer<float>> buffer,
                                 double srcSampleRate)
{
    // Atomic update of sample buffer (thread-safe)
    sampleBuffer = buffer;
    sourceSampleRate = srcSampleRate;

    // Recalculate playback rate
    if (sourceSampleRate > 0.0 && targetSampleRate > 0.0)
        playbackRate = sourceSampleRate / targetSampleRate;
    else
        playbackRate = 1.0;
}

float SampleVoice::readSample(int channel, double position) const
{
    auto buffer = sampleBuffer;
    if (!buffer || channel >= buffer->getNumChannels())
        return 0.0f;

    const int sampleLength = buffer->getNumSamples();
    if (sampleLength == 0)
        return 0.0f;

    // Get integer and fractional parts of position
    const int index0 = static_cast<int>(position);
    const int index1 = index0 + 1;
    const float fraction = static_cast<float>(position - index0);

    // Boundary checks
    if (index0 < 0 || index0 >= sampleLength)
        return 0.0f;

    const float* channelData = buffer->getReadPointer(channel);
    const float sample0 = channelData[index0];

    // If at the end, don't interpolate
    if (index1 >= sampleLength)
        return sample0;

    // Linear interpolation
    const float sample1 = channelData[index1];
    return sample0 + fraction * (sample1 - sample0);
}

} // namespace flam
