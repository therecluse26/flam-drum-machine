#include "MixerBus.h"

namespace flam {

MixerBus::MixerBus()
{
    masterLimiter.setThreshold(-0.1f);
    masterLimiter.setRelease(50.0f);
}

MixerBus::~MixerBus() = default;

void MixerBus::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    this->sampleRate = sampleRate;
    this->blockSize = samplesPerBlock;
    
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;
    
    for (auto& bus : buses)
    {
        bus.gainProcessor.prepare(spec);
        bus.panProcessor.prepare(spec);
        
        bus.smoothedLevel.reset(sampleRate, 0.01);
        bus.smoothedPan.reset(sampleRate, 0.01);
        
        bus.smoothedLevel.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(bus.level.load()));
        bus.smoothedPan.setCurrentAndTargetValue(bus.pan.load());
    }
    
    masterLimiter.prepare(spec);
    smoothedMasterLevel.reset(sampleRate, 0.01);
    smoothedMasterLevel.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(masterLevel.load()));
}

void MixerBus::releaseResources()
{
    for (auto& bus : buses)
    {
        bus.gainProcessor.reset();
        bus.panProcessor.reset();
    }
    
    masterLimiter.reset();
}

void MixerBus::processBlock(juce::AudioBuffer<float>& buffer)
{
    const bool hasSolo = anySoloBusActive();
    
    for (size_t i = 0; i < buses.size() - 1; ++i)
    {
        auto busType = static_cast<BusType>(i);
        applyBusProcessing(busType, buffer);
    }
    
    applyMicBleed(buffer);
    
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    
    smoothedMasterLevel.setTargetValue(juce::Decibels::decibelsToGain(masterLevel.load()));
    
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);
        
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            channelData[sample] *= smoothedMasterLevel.getNextValue();
        }
    }
    
    masterLimiter.process(context);
}

void MixerBus::setBusLevel(BusType bus, float levelDb)
{
    const auto index = static_cast<size_t>(bus);
    if (index < buses.size())
    {
        buses[index].level.store(juce::jlimit(-60.0f, 12.0f, levelDb));
    }
}

float MixerBus::getBusLevel(BusType bus) const
{
    const auto index = static_cast<size_t>(bus);
    return index < buses.size() ? buses[index].level.load() : 0.0f;
}

void MixerBus::setBusPan(BusType bus, float pan)
{
    const auto index = static_cast<size_t>(bus);
    if (index < buses.size())
    {
        buses[index].pan.store(juce::jlimit(-1.0f, 1.0f, pan));
    }
}

float MixerBus::getBusPan(BusType bus) const
{
    const auto index = static_cast<size_t>(bus);
    return index < buses.size() ? buses[index].pan.load() : 0.0f;
}

void MixerBus::setBusSolo(BusType bus, bool solo)
{
    const auto index = static_cast<size_t>(bus);
    if (index < buses.size())
    {
        buses[index].solo.store(solo);
    }
}

bool MixerBus::isBusSoloed(BusType bus) const
{
    const auto index = static_cast<size_t>(bus);
    return index < buses.size() ? buses[index].solo.load() : false;
}

void MixerBus::setBusMute(BusType bus, bool mute)
{
    const auto index = static_cast<size_t>(bus);
    if (index < buses.size())
    {
        buses[index].mute.store(mute);
    }
}

bool MixerBus::isBusMuted(BusType bus) const
{
    const auto index = static_cast<size_t>(bus);
    return index < buses.size() ? buses[index].mute.load() : false;
}

void MixerBus::setMasterLevel(float levelDb)
{
    masterLevel.store(juce::jlimit(-60.0f, 12.0f, levelDb));
}

void MixerBus::setBleedAmount(float amount)
{
    bleedAmount.store(juce::jlimit(0.0f, 1.0f, amount));
}

bool MixerBus::anySoloBusActive() const
{
    for (const auto& bus : buses)
    {
        if (bus.solo.load())
            return true;
    }
    return false;
}

void MixerBus::applyBusProcessing(BusType bus, juce::AudioBuffer<float>& buffer)
{
    const auto index = static_cast<size_t>(bus);
    if (index >= buses.size())
        return;
    
    auto& busChannel = buses[index];
    
    const bool hasSolo = anySoloBusActive();
    const bool shouldProcess = !busChannel.mute.load() && 
                              (!hasSolo || busChannel.solo.load());
    
    if (!shouldProcess)
    {
        busChannel.smoothedLevel.setTargetValue(0.0f);
    }
    else
    {
        busChannel.smoothedLevel.setTargetValue(
            juce::Decibels::decibelsToGain(busChannel.level.load()));
        busChannel.smoothedPan.setTargetValue(busChannel.pan.load());
    }
    
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    
    busChannel.gainProcessor.setGainLinear(busChannel.smoothedLevel.getCurrentValue());
    busChannel.gainProcessor.process(context);
    
    busChannel.panProcessor.setPan(busChannel.smoothedPan.getCurrentValue());
    busChannel.panProcessor.process(context);
}

void MixerBus::applyMicBleed(juce::AudioBuffer<float>& buffer)
{
    const float bleed = bleedAmount.load();
    if (bleed <= 0.0f)
        return;
    
    // Placeholder for mic bleed simulation
    // Would mix channels together based on bleed amount
}

} // namespace flam