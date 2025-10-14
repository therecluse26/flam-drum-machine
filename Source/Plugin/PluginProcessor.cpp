#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Core/VoiceManager.h"
#include "../Core/MixerBus.h"

namespace flam {

FlamAudioProcessor::FlamAudioProcessor()
    : AudioProcessor(BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
                         )
    , parameters(*this, nullptr, juce::Identifier("FLAM"), createParameterLayout())
{
    std::cout << "[FLAM] FlamAudioProcessor constructor called" << std::endl;
    humanizationParam = dynamic_cast<juce::AudioParameterFloat*>(
        parameters.getParameter("humanization"));
    masterVolumeParam = dynamic_cast<juce::AudioParameterFloat*>(
        parameters.getParameter("master_volume"));
    closeVolumeParam = dynamic_cast<juce::AudioParameterFloat*>(
        parameters.getParameter("close_volume"));
    overheadVolumeParam = dynamic_cast<juce::AudioParameterFloat*>(
        parameters.getParameter("overhead_volume"));
    roomVolumeParam = dynamic_cast<juce::AudioParameterFloat*>(
        parameters.getParameter("room_volume"));
    ambientVolumeParam = dynamic_cast<juce::AudioParameterFloat*>(
        parameters.getParameter("ambient_volume"));
    bleedAmountParam = dynamic_cast<juce::AudioParameterFloat*>(
        parameters.getParameter("bleed_amount"));
    polyphonyParam = dynamic_cast<juce::AudioParameterInt*>(
        parameters.getParameter("polyphony"));
    roundRobinParam = dynamic_cast<juce::AudioParameterBool*>(
        parameters.getParameter("round_robin"));
}

FlamAudioProcessor::~FlamAudioProcessor() = default;

const juce::String FlamAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool FlamAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool FlamAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool FlamAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double FlamAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int FlamAudioProcessor::getNumPrograms()
{
    return 1;
}

int FlamAudioProcessor::getCurrentProgram()
{
    return 0;
}

void FlamAudioProcessor::setCurrentProgram(int index)
{
    juce::ignoreUnused(index);
}

const juce::String FlamAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void FlamAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void FlamAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    std::cout << "[FLAM] prepareToPlay called: " << sampleRate << " Hz, " << samplesPerBlock << " samples" << std::endl;
    engine.prepareToPlay(sampleRate, samplesPerBlock);
    updateEngineParameters();
}

void FlamAudioProcessor::releaseResources()
{
    engine.releaseResources();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FlamAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
#else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

#if !JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif

    return true;
#endif
}
#endif

void FlamAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    updateEngineParameters();
    
    engine.processBlock(buffer, midiMessages);
}

bool FlamAudioProcessor::hasEditor() const
{
    std::cout << "[FLAM] hasEditor() called, returning true" << std::endl;
    return true;
}

juce::AudioProcessorEditor* FlamAudioProcessor::createEditor()
{
    std::cout << "[FLAM] createEditor() called" << std::endl;
    return new FlamAudioProcessorEditor(*this);
}

void FlamAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void FlamAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));

    if (xmlState != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessorValueTreeState::ParameterLayout FlamAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "humanization", "Humanization", 
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 
        0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "master_volume", "Master Volume", 
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 
        0.0f, "dB"));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "close_volume", "Close Mics", 
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 
        0.0f, "dB"));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "overhead_volume", "Overhead Mics", 
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 
        -6.0f, "dB"));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "room_volume", "Room Mics", 
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 
        -12.0f, "dB"));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "ambient_volume", "Ambient Mics", 
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f), 
        -18.0f, "dB"));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "bleed_amount", "Mic Bleed", 
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 
        0.2f));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        "polyphony", "Max Polyphony", 
        1, 128, 64));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        "round_robin", "Round Robin", 
        true));

    return layout;
}

void FlamAudioProcessor::updateEngineParameters()
{
    if (humanizationParam)
        engine.setHumanizationAmount(humanizationParam->get());

    if (auto* mixerBus = engine.getMixerBus())
    {
        if (masterVolumeParam)
            mixerBus->setMasterLevel(masterVolumeParam->get());
        
        if (closeVolumeParam)
            mixerBus->setBusLevel(MixerBus::BusType::Close, closeVolumeParam->get());
        
        if (overheadVolumeParam)
            mixerBus->setBusLevel(MixerBus::BusType::Overhead, overheadVolumeParam->get());
        
        if (roomVolumeParam)
            mixerBus->setBusLevel(MixerBus::BusType::Room, roomVolumeParam->get());
        
        if (ambientVolumeParam)
            mixerBus->setBusLevel(MixerBus::BusType::Ambient, ambientVolumeParam->get());
        
        if (bleedAmountParam)
            mixerBus->setBleedAmount(bleedAmountParam->get());
    }

    if (auto* voiceManager = engine.getVoiceManager())
    {
        if (polyphonyParam)
            voiceManager->setPolyphony(polyphonyParam->get());
        
        if (roundRobinParam)
            voiceManager->setRoundRobinEnabled(roundRobinParam->get());
    }
}

} // namespace flam

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new flam::FlamAudioProcessor();
}