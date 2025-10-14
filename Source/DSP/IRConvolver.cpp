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
    preDelay.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.5));
    
    highPassFilter.prepare(spec);
    highPassFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    
    lowPassFilter.prepare(spec);
    lowPassFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    
    dryBuffer.setSize(2, samplesPerBlock);
    wetBuffer.setSize(2, samplesPerBlock);
    
    smoothedDryWet.reset(sampleRate, 0.05);
    smoothedDryWet.setCurrentAndTargetValue(dryWetMix.load());
    
    updateFilters();
}

void IRConvolver::releaseResources()
{
    convolution.reset();
    preDelay.reset();
    highPassFilter.reset();
    lowPassFilter.reset();
    
    dryBuffer.clear();
    wetBuffer.clear();
}

void IRConvolver::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!irLoaded.load())
        return;
    
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    
    dryBuffer.makeCopyOf(buffer, true);
    
    wetBuffer.makeCopyOf(buffer, true);
    
    juce::dsp::AudioBlock<float> wetBlock(wetBuffer);
    juce::dsp::ProcessContextReplacing<float> context(wetBlock);
    
    const float delayInSamples = (preDelayMs.load() / 1000.0f) * static_cast<float>(sampleRate);
    preDelay.setDelay(delayInSamples);
    preDelay.process(context);
    
    convolution.process(context);
    
    updateFilters();
    highPassFilter.process(context);
    lowPassFilter.process(context);
    
    smoothedDryWet.setTargetValue(dryWetMix.load());
    
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* outputData = buffer.getWritePointer(channel);
        const auto* dryData = dryBuffer.getReadPointer(channel);
        const auto* wetData = wetBuffer.getReadPointer(channel);
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float mix = smoothedDryWet.getNextValue();
            outputData[sample] = dryData[sample] * (1.0f - mix) + wetData[sample] * mix;
        }
    }
}

bool IRConvolver::loadImpulseResponse(const juce::File& irFile)
{
    if (!irFile.existsAsFile())
        return false;
    
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(irFile));
    
    if (!reader)
        return false;
    
    juce::AudioBuffer<float> irBuffer(static_cast<int>(reader->numChannels), 
                                      static_cast<int>(reader->lengthInSamples));
    reader->read(&irBuffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);
    
    convolution.loadImpulseResponse(std::move(irBuffer), 
                                   reader->sampleRate, 
                                   juce::dsp::Convolution::Stereo::yes, 
                                   juce::dsp::Convolution::Trim::yes,
                                   juce::dsp::Convolution::Normalise::yes);
    
    currentIRName = irFile.getFileNameWithoutExtension();
    irLoaded.store(true);
    
    return true;
}

bool IRConvolver::loadImpulseResponse(const float* irData, size_t irSize, double irSampleRate)
{
    if (!irData || irSize == 0)
        return false;
    
    juce::AudioBuffer<float> irBuffer(2, static_cast<int>(irSize));
    
    for (int channel = 0; channel < 2; ++channel)
    {
        irBuffer.copyFrom(channel, 0, irData, static_cast<int>(irSize));
    }
    
    convolution.loadImpulseResponse(std::move(irBuffer),
                                   irSampleRate,
                                   juce::dsp::Convolution::Stereo::yes,
                                   juce::dsp::Convolution::Trim::yes,
                                   juce::dsp::Convolution::Normalise::yes);
    
    currentIRName = "Custom IR";
    irLoaded.store(true);
    
    return true;
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
}

void IRConvolver::setLowPassFreq(float freq)
{
    lowPassFreq.store(juce::jlimit(20.0f, 20000.0f, freq));
}

void IRConvolver::updateFilters()
{
    highPassFilter.setCutoffFrequency(highPassFreq.load());
    lowPassFilter.setCutoffFrequency(lowPassFreq.load());
}

} // namespace flam