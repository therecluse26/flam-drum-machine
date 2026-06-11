// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Core/VoiceManager.h"

namespace flam {

juce::AudioProcessor::BusesProperties FlamAudioProcessor::createBusLayout()
{
    // Static 17-bus layout: 1 stereo Main Mix + 16 mono individual channels.
    //
    // All buses are declared and ENABLED here at construction time because JUCE does not
    // reliably support changing bus *count* after the processor is constructed. Hosts such as
    // Logic (AU) and Cubase/Reaper (VST3) query the bus layout during plugin load — before any
    // kit is selected — and use it to build their internal routing graph. Calling bus->enable()
    // or bus->enable(false) after that window is host-undefined behaviour and was the source of
    // observed crashes. Unused buses (kit has fewer than 16 channels) simply emit silence, which
    // is safe and accepted by all tested hosts.
    return juce::AudioProcessor::BusesProperties()
        .withOutput("Main Mix", juce::AudioChannelSet::stereo(), true)
        .withOutput("Bus 1", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 2", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 3", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 4", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 5", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 6", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 7", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 8", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 9", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 10", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 11", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 12", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 13", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 14", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 15", juce::AudioChannelSet::mono(), true)
        .withOutput("Bus 16", juce::AudioChannelSet::mono(), true);
}

FlamAudioProcessor::FlamAudioProcessor()
    : AudioProcessor(createBusLayout())
    , perChannelMixer(std::make_unique<Mixer>())
    , parameters(*this, nullptr, juce::Identifier("FLAM"), createParameterLayout())
{
    // Initialize mixer with 2 default channels
    std::vector<juce::String> defaultChannels = {"Channel 1", "Channel 2"};
    perChannelMixer->setNumChannels(2, defaultChannels);
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

    // Mixer parameters
    inputGainParam = dynamic_cast<juce::AudioParameterFloat*>(
        parameters.getParameter("input_gain"));

    // EQ parameters
    eqBypassParam = dynamic_cast<juce::AudioParameterBool*>(parameters.getParameter("eq_enabled"));
    eq31HzParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("eq_31hz"));
    eq62HzParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("eq_62hz"));
    eq125HzParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("eq_125hz"));
    eq250HzParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("eq_250hz"));
    eq500HzParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("eq_500hz"));
    eq1kHzParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("eq_1khz"));
    eq2kHzParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("eq_2khz"));
    eq4kHzParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("eq_4khz"));
    eq8kHzParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("eq_8khz"));
    eq16kHzParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("eq_16khz"));

    // Compressor parameters
    compBypassParam = dynamic_cast<juce::AudioParameterBool*>(parameters.getParameter("comp_enabled"));
    compAttackParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("comp_attack"));
    compReleaseParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("comp_release"));
    compHoldParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("comp_hold"));
    compThresholdParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("comp_threshold"));
    compRatioParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("comp_ratio"));
    compMakeupGainParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("comp_makeup_gain"));
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
    engine.prepareToPlay(sampleRate, samplesPerBlock);

    if (perChannelMixer)
    {
        // Get required channel count from engine (based on loaded kit)
        const int requiredChannels = engine.getRequiredChannelCount();

        // Configure mixer for correct number of channels
        std::vector<juce::String> channelNames;
        for (int i = 0; i < requiredChannels; ++i)
            channelNames.push_back("Channel " + juce::String(i + 1));

        perChannelMixer->setNumChannels(requiredChannels, channelNames);
        perChannelMixer->prepareToPlay(sampleRate, samplesPerBlock);

        // Pre-allocate output buffer for stereo mix (input buffer not needed - use engine's directly)
        mixerOutputBuffer.setSize(2, samplesPerBlock, false, false, true);
    }

    updateEngineParameters();
}

void FlamAudioProcessor::releaseResources()
{
    engine.releaseResources();
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FlamAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Main Mix must be stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Individual buses (if enabled) must be mono
    for (int busIdx = 1; busIdx < layouts.outputBuses.size(); ++busIdx)
    {
        if (!layouts.getChannelSet(false, busIdx).isDisabled() &&
            layouts.getChannelSet(false, busIdx) != juce::AudioChannelSet::mono())
            return false;
    }

    return true;
}
#endif


void FlamAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    updateEngineParameters();

    // Process audio through the engine (renders voices to multi-channel internal buffer)
    engine.processBlock(buffer, midiMessages);

    // Process multi-channel audio through mixer
    if (perChannelMixer)
    {
        const int numMixerChannels = perChannelMixer->getNumChannels();

        // Allocate unified output buffer:
        // - Channels 0-1: Main Mix (stereo)
        // - Channels 2+: Individual buses (mono, one per mixer channel)
        const int totalOutputChannels = 2 + numMixerChannels;

        if (mixerOutputBuffer.getNumChannels() < totalOutputChannels ||
            mixerOutputBuffer.getNumSamples() < numSamples)
        {
            mixerOutputBuffer.setSize(totalOutputChannels, numSamples, false, false, true);
        }

        // Get multi-channel buffer from engine and process through mixer
        const auto& multiChannelBuffer = engine.getMultiChannelBuffer();

        // Process through mixer (routing, FX, solo/mute, metering)
        // Mixer will write to all output channels based on routing settings
        perChannelMixer->process(multiChannelBuffer, mixerOutputBuffer, numSamples);

        // Split mixer output buffer into JUCE's separate output buses

        // Bus 0: Main Mix (stereo) - copy from mixerOutputBuffer channels 0-1
        if (getBusCount(false) > 0)
        {
            auto mainBus = getBusBuffer(buffer, false, 0);
            mainBus.clear();
            if (mainBus.getNumChannels() >= 1)
                mainBus.copyFrom(0, 0, mixerOutputBuffer, 0, 0, numSamples);
            if (mainBus.getNumChannels() >= 2)
                mainBus.copyFrom(1, 0, mixerOutputBuffer, 1, 0, numSamples);
        }

        // Buses 1-N: Individual channels (mono) - copy from mixerOutputBuffer channels 2+
        const int totalBuses = getBusCount(false);
        for (int busIdx = 1; busIdx <= numMixerChannels && busIdx < totalBuses; ++busIdx)
        {
            // Only process enabled buses
            if (auto* bus = getBus(false, busIdx))
            {
                if (bus->isEnabled())
                {
                    auto individualBus = getBusBuffer(buffer, false, busIdx);
                    if (individualBus.getNumChannels() >= 1)
                    {
                        const int srcChannel = 2 + (busIdx - 1);  // Channel 2 is bus 1, etc.
                        individualBus.clear();
                        individualBus.copyFrom(0, 0, mixerOutputBuffer, srcChannel, 0, numSamples);
                    }
                }
            }
        }
    }
}

bool FlamAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* FlamAudioProcessor::createEditor()
{
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

    // Input gain
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "input_gain", "Input Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f),
        0.0f, "dB"));

    // 10-band EQ (standard graphic EQ frequencies)
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "eq_enabled", "EQ Enabled",
        false));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "eq_31hz", "EQ 31 Hz",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "eq_62hz", "EQ 62 Hz",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "eq_125hz", "EQ 125 Hz",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "eq_250hz", "EQ 250 Hz",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "eq_500hz", "EQ 500 Hz",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "eq_1khz", "EQ 1 kHz",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "eq_2khz", "EQ 2 kHz",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "eq_4khz", "EQ 4 kHz",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "eq_8khz", "EQ 8 kHz",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "eq_16khz", "EQ 16 kHz",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, "dB"));

    // Compressor parameters
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "comp_enabled", "Compressor Enabled",
        false));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "comp_attack", "Compressor Attack",
        juce::NormalisableRange<float>(0.1f, 100.0f, 0.1f, 0.3f),
        10.0f, "ms"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "comp_release", "Compressor Release",
        juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f, 0.3f),
        100.0f, "ms"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "comp_hold", "Compressor Hold",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f),
        0.0f, "ms"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "comp_threshold", "Compressor Threshold",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f),
        -20.0f, "dB"));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "comp_ratio", "Compressor Ratio",
        juce::NormalisableRange<float>(1.0f, 20.0f, 0.1f, 0.3f),
        4.0f, ":1"));
    // comp_lookahead was removed (FLA-73): SimpleCompressor::setLookahead() was a no-op stub.
    // APVTS gracefully ignores this key in pre-FLA-73 saved states (unknown params are skipped).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "comp_makeup_gain", "Compressor Makeup Gain",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f),
        0.0f, "dB"));

    return layout;
}

void FlamAudioProcessor::updateEngineParameters()
{
    if (humanizationParam)
        engine.setHumanizationAmount(humanizationParam->get());

    // Mirror APVTS master params into the audible Mixer master section.
    // All Mixer setters store into std::atomics — safe to call from the message thread.
    if (perChannelMixer)
    {
        if (masterVolumeParam)
            perChannelMixer->setMasterVolume(masterVolumeParam->get());

        if (eqBypassParam)
            perChannelMixer->setMasterEQEnabled(eqBypassParam->get());
        if (eq31HzParam)   perChannelMixer->setMasterEQBandGain(0, eq31HzParam->get());
        if (eq62HzParam)   perChannelMixer->setMasterEQBandGain(1, eq62HzParam->get());
        if (eq125HzParam)  perChannelMixer->setMasterEQBandGain(2, eq125HzParam->get());
        if (eq250HzParam)  perChannelMixer->setMasterEQBandGain(3, eq250HzParam->get());
        if (eq500HzParam)  perChannelMixer->setMasterEQBandGain(4, eq500HzParam->get());
        if (eq1kHzParam)   perChannelMixer->setMasterEQBandGain(5, eq1kHzParam->get());
        if (eq2kHzParam)   perChannelMixer->setMasterEQBandGain(6, eq2kHzParam->get());
        if (eq4kHzParam)   perChannelMixer->setMasterEQBandGain(7, eq4kHzParam->get());
        if (eq8kHzParam)   perChannelMixer->setMasterEQBandGain(8, eq8kHzParam->get());
        if (eq16kHzParam)  perChannelMixer->setMasterEQBandGain(9, eq16kHzParam->get());

        if (compBypassParam)     perChannelMixer->setMasterCompressorEnabled(compBypassParam->get());
        if (compAttackParam)     perChannelMixer->setMasterCompressorAttack(compAttackParam->get());
        if (compReleaseParam)    perChannelMixer->setMasterCompressorRelease(compReleaseParam->get());
        if (compThresholdParam)  perChannelMixer->setMasterCompressorThreshold(compThresholdParam->get());
        if (compRatioParam)      perChannelMixer->setMasterCompressorRatio(compRatioParam->get());
        if (compMakeupGainParam) perChannelMixer->setMasterCompressorMakeupGain(compMakeupGainParam->get());
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