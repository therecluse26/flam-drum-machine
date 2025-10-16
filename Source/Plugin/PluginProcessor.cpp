#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Core/VoiceManager.h"
#include "../Core/MixerBus.h"
#include "../DSP/SimpleEQ.h"
#include "../DSP/SimpleCompressor.h"

namespace flam {

FlamAudioProcessor::FlamAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         )
    , perChannelMixer(std::make_unique<PerChannelMixer>())
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
    compLookaheadParam = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter("comp_lookahead"));
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
    // We only support mono or stereo output, no input needed
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
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

    // Process audio through the engine (renders voices to multi-channel internal buffer)
    engine.processBlock(buffer, midiMessages);

    // Process multi-channel audio through per-channel mixer
    if (perChannelMixer)
    {
        const int numSamples = buffer.getNumSamples();

        // Ensure output buffer is large enough (may need resize if block size changed)
        if (mixerOutputBuffer.getNumSamples() < numSamples)
            mixerOutputBuffer.setSize(2, numSamples, false, false, true);

        // Clear output buffer
        mixerOutputBuffer.clear();

        // Get multi-channel buffer from engine and process through mixer
        // This buffer contains the full multi-channel output (e.g., 8 channels for 8-mic kit)
        const auto& multiChannelBuffer = engine.getMultiChannelBuffer();

        // Process through per-channel mixer (routing, FX, solo/mute, etc.)
        perChannelMixer->process(multiChannelBuffer, mixerOutputBuffer, numSamples);

        // Copy stereo mixer output back to main buffer
        buffer.clear();
        buffer.copyFrom(0, 0, mixerOutputBuffer, 0, 0, numSamples);
        if (buffer.getNumChannels() >= 2)
            buffer.copyFrom(1, 0, mixerOutputBuffer, 1, 0, numSamples);
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
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "comp_lookahead", "Compressor Lookahead",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f),
        0.0f, "ms"));
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

    // Input gain
    if (inputGainParam)
        engine.setInputGain(inputGainParam->get());

    // EQ parameters
    if (auto* eq = engine.getEQ())
    {
        if (eqBypassParam) eq->setBypassed(!eqBypassParam->get());  // Invert: enabled=true means bypass=false
        if (eq31HzParam) eq->setBandGain(0, eq31HzParam->get());
        if (eq62HzParam) eq->setBandGain(1, eq62HzParam->get());
        if (eq125HzParam) eq->setBandGain(2, eq125HzParam->get());
        if (eq250HzParam) eq->setBandGain(3, eq250HzParam->get());
        if (eq500HzParam) eq->setBandGain(4, eq500HzParam->get());
        if (eq1kHzParam) eq->setBandGain(5, eq1kHzParam->get());
        if (eq2kHzParam) eq->setBandGain(6, eq2kHzParam->get());
        if (eq4kHzParam) eq->setBandGain(7, eq4kHzParam->get());
        if (eq8kHzParam) eq->setBandGain(8, eq8kHzParam->get());
        if (eq16kHzParam) eq->setBandGain(9, eq16kHzParam->get());
    }

    // Compressor parameters
    if (auto* compressor = engine.getCompressor())
    {
        if (compBypassParam) compressor->setBypassed(!compBypassParam->get());  // Invert: enabled=true means bypass=false
        if (compAttackParam) compressor->setAttack(compAttackParam->get());
        if (compReleaseParam) compressor->setRelease(compReleaseParam->get());
        if (compHoldParam) compressor->setHold(compHoldParam->get());
        if (compThresholdParam) compressor->setThreshold(compThresholdParam->get());
        if (compRatioParam) compressor->setRatio(compRatioParam->get());
        if (compLookaheadParam) compressor->setLookahead(compLookaheadParam->get());
        if (compMakeupGainParam) compressor->setMakeupGain(compMakeupGainParam->get());
    }

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