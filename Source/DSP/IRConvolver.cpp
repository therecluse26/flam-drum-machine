#include "IRConvolver.h"

namespace flam {

IRConvolver::IRConvolver()
{
    convolution.reset();
}

IRConvolver::~IRConvolver() = default;

void IRConvolver::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    this->sampleRate = sampleRate;
    this->blockSize = samplesPerBlock;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    convolution.prepare(spec);

    preDelay.prepare(spec);
    preDelay.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.5)); // Max 500ms pre-delay

    highPassFilter.prepare(spec);
    highPassFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);

    lowPassFilter.prepare(spec);
    lowPassFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    dryBuffer.setSize(2, samplesPerBlock);
    wetBuffer.setSize(2, samplesPerBlock);

    smoothedDryWet.reset(sampleRate, 0.05); // 50ms smoothing
    smoothedDryWet.setCurrentAndTargetValue(dryWetMix.load());

    updateFilters();
}

void IRConvolver::releaseResources()
{
    convolution.reset();
    preDelay.reset();
    highPassFilter.reset();
    lowPassFilter.reset();

    dryBuffer.setSize(0, 0);
    wetBuffer.setSize(0, 0);
}

void IRConvolver::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!irLoaded.load())
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Make sure our temp buffers are large enough
    if (dryBuffer.getNumSamples() < numSamples)
    {
        dryBuffer.setSize(numChannels, numSamples, false, false, true);
        wetBuffer.setSize(numChannels, numSamples, false, false, true);
    }

    // Store dry signal
    for (int ch = 0; ch < numChannels; ++ch)
    {
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }

    // Apply pre-delay if needed
    const float preDelayAmount = preDelayMs.load();
    if (preDelayAmount > 0.0f)
    {
        const int delaySamples = static_cast<int>((preDelayAmount / 1000.0f) * sampleRate);
        preDelay.setDelay(static_cast<float>(delaySamples));

        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        preDelay.process(context);
    }

    // Apply high-pass filter
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        highPassFilter.process(context);
    }

    // Apply convolution
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        convolution.process(context);
    }

    // Apply low-pass filter
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        lowPassFilter.process(context);
    }

    // Mix dry and wet signals
    smoothedDryWet.setTargetValue(dryWetMix.load());

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float wetAmount = smoothedDryWet.getNextValue();
        const float dryAmount = 1.0f - wetAmount;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float dryValue = dryBuffer.getSample(ch, sample);
            const float wetValue = buffer.getSample(ch, sample);

            buffer.setSample(ch, sample, dryValue * dryAmount + wetValue * wetAmount);
        }
    }
}

bool IRConvolver::loadImpulseResponse(const juce::File& irFile)
{
    if (!irFile.existsAsFile())
        return false;

    // Load audio file
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(irFile));

    if (!reader)
        return false;

    // Read the entire file
    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);

    juce::AudioBuffer<float> irBuffer(numChannels, numSamples);
    reader->read(&irBuffer, 0, numSamples, 0, true, true);

    // Load into convolution engine
    const bool success = loadImpulseResponse(irBuffer.getReadPointer(0),
                                            static_cast<size_t>(numSamples),
                                            reader->sampleRate);

    if (success)
    {
        currentIRName = irFile.getFileNameWithoutExtension();
    }

    return success;
}

bool IRConvolver::loadImpulseResponse(const float* irData, size_t irSize, double irSampleRate)
{
    if (!irData || irSize == 0)
        return false;

    try
    {
        // Load the IR into the convolution engine
        convolution.loadImpulseResponse(irData, irSize, juce::dsp::Convolution::Stereo::no,
                                       juce::dsp::Convolution::Trim::yes,
                                       irSize, juce::dsp::Convolution::Normalise::yes);

        irLoaded.store(true);
        return true;
    }
    catch (...)
    {
        irLoaded.store(false);
        return false;
    }
}

void IRConvolver::clearImpulseResponse()
{
    convolution.reset();
    irLoaded.store(false);
    currentIRName.clear();
}

void IRConvolver::setDryWet(float mix)
{
    dryWetMix.store(juce::jlimit(0.0f, 1.0f, mix));
}

void IRConvolver::setPreDelay(float delayMs)
{
    preDelayMs.store(juce::jlimit(0.0f, 500.0f, delayMs));
}

void IRConvolver::setHighPassFreq(float freq)
{
    highPassFreq.store(juce::jlimit(20.0f, 20000.0f, freq));
    updateFilters();
}

void IRConvolver::setLowPassFreq(float freq)
{
    lowPassFreq.store(juce::jlimit(20.0f, 20000.0f, freq));
    updateFilters();
}

void IRConvolver::updateFilters()
{
    const float hpFreq = highPassFreq.load();
    const float lpFreq = lowPassFreq.load();

    highPassFilter.setCutoffFrequency(hpFreq);
    lowPassFilter.setCutoffFrequency(lpFreq);
}

} // namespace flam
