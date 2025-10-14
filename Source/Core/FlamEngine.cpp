#include "FlamEngine.h"
#include "VoiceManager.h"
#include "MixerBus.h"
#include "../Formats/FlamKitLoader.h"

namespace flam {

FlamEngine::FlamEngine()
    : voiceManager(std::make_unique<VoiceManager>())
    , mixerBus(std::make_unique<MixerBus>())
    , kitLoader(std::make_unique<FlamKitLoader>())
{
    setPlayConfigDetails(0, 2, 44100.0, 512);
}

FlamEngine::~FlamEngine() = default;

void FlamEngine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    
    voiceManager->prepareToPlay(sampleRate, samplesPerBlock);
    mixerBus->prepareToPlay(sampleRate, samplesPerBlock);
    
    AudioProcessorGraph::prepareToPlay(sampleRate, samplesPerBlock);
}

void FlamEngine::releaseResources()
{
    AudioProcessorGraph::releaseResources();
    
    voiceManager->releaseResources();
    mixerBus->releaseResources();
}

void FlamEngine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const auto samplePosition = metadata.samplePosition;
        handleMidiEvent(message, samplePosition);
    }
    
    voiceManager->renderNextBlock(buffer, 0, buffer.getNumSamples());
    
    mixerBus->processBlock(buffer);
    
    midiMessages.clear();
}

void FlamEngine::loadKit(const juce::File& kitFile)
{
    // Launch the entire kit loading process on a background thread to prevent UI freeze
    juce::Thread::launch([this, kitFile]()
    {
        auto kit = kitLoader->loadKit(kitFile);
        if (kit)
        {
            voiceManager->loadKit(std::move(kit));
        }
    });
}

void FlamEngine::clearKit()
{
    voiceManager->clearKit();
}

void FlamEngine::setHumanizationAmount(float amount)
{
    humanization.store(juce::jlimit(0.0f, 1.0f, amount));
}

void FlamEngine::setLatencyCompensation(int samples)
{
    latencyCompensation.store(std::max(0, samples));
}

void FlamEngine::handleMidiEvent(const juce::MidiMessage& message, int samplePosition)
{
    if (message.isNoteOn())
    {
        const int note = message.getNoteNumber();
        float velocity = message.getVelocity() / 127.0f;
        
        velocity = applyHumanization(velocity);
        
        voiceManager->triggerNote(note, velocity, samplePosition);
    }
    else if (message.isNoteOff())
    {
        const int note = message.getNoteNumber();
        voiceManager->releaseNote(note, samplePosition);
    }
}

float FlamEngine::applyHumanization(float velocity)
{
    const float humanizationFactor = humanization.load();
    if (humanizationFactor > 0.0f)
    {
        const float randomOffset = (randomGenerator.nextFloat() - 0.5f) * 2.0f * humanizationFactor * 0.1f;
        velocity = juce::jlimit(0.0f, 1.0f, velocity + randomOffset);
    }
    return velocity;
}

void FlamEngine::triggerNote(int midiNote, float velocity, int sampleOffset)
{
    velocity = applyHumanization(velocity);
    voiceManager->triggerNote(midiNote, velocity, sampleOffset);
}

void FlamEngine::releaseNote(int midiNote, int sampleOffset)
{
    voiceManager->releaseNote(midiNote, sampleOffset);
}

} // namespace flam