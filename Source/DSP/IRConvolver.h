#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <atomic>

namespace flam {

class IRConvolver
{
public:
    IRConvolver();
    ~IRConvolver();
    
    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    
    void processBlock(juce::AudioBuffer<float>& buffer);
    
    bool loadImpulseResponse(const juce::File& irFile);
    bool loadImpulseResponse(const float* irData, size_t irSize, double irSampleRate);
    
    void clearImpulseResponse();
    
    void setDryWet(float mix);
    float getDryWet() const { return dryWetMix.load(); }
    
    void setPreDelay(float delayMs);
    float getPreDelay() const { return preDelayMs.load(); }
    
    void setHighPassFreq(float freq);
    float getHighPassFreq() const { return highPassFreq.load(); }
    
    void setLowPassFreq(float freq);
    float getLowPassFreq() const { return lowPassFreq.load(); }
    
    bool isImpulseResponseLoaded() const { return irLoaded.load(); }
    juce::String getCurrentIRName() const { return currentIRName; }
    
private:
    juce::dsp::Convolution convolution;
    juce::dsp::DelayLine<float> preDelay;
    juce::dsp::StateVariableTPTFilter<float> highPassFilter;
    juce::dsp::StateVariableTPTFilter<float> lowPassFilter;
    
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer;
    
    std::atomic<float> dryWetMix{0.3f};
    std::atomic<float> preDelayMs{0.0f};
    std::atomic<float> highPassFreq{20.0f};
    std::atomic<float> lowPassFreq{20000.0f};
    std::atomic<bool> irLoaded{false};
    
    juce::String currentIRName;
    
    double sampleRate{44100.0};
    int blockSize{512};
    
    juce::SmoothedValue<float> smoothedDryWet;
    
    void updateFilters();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IRConvolver)
};

} // namespace flam