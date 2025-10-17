#include "FlamEngine.h"
#include "VoiceManager.h"
#include "MixerBus.h"
#include "../Formats/FlamKitLoader.h"
#include "../DSP/SimpleEQ.h"
#include "../DSP/SimpleCompressor.h"

namespace flam {

FlamEngine::FlamEngine()
    : voiceManager(std::make_unique<VoiceManager>())
    , mixerBus(std::make_unique<MixerBus>())
    , kitLoader(std::make_unique<FlamKitLoader>())
    , eq(std::make_unique<SimpleEQ>())
    , compressor(std::make_unique<SimpleCompressor>())
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
    eq->prepareToPlay(sampleRate, samplesPerBlock);
    compressor->prepareToPlay(sampleRate, samplesPerBlock);

    // Allocate internal buffer for multi-channel rendering
    // Get required channel count from loaded kit (defaults to 2 if no kit loaded)
    const int requiredChannels = voiceManager->getRequiredChannelCount();
    internalBuffer.setSize(requiredChannels, samplesPerBlock, false, false, true);

    AudioProcessorGraph::prepareToPlay(sampleRate, samplesPerBlock);
}

void FlamEngine::releaseResources()
{
    AudioProcessorGraph::releaseResources();

    voiceManager->releaseResources();
    mixerBus->releaseResources();
    eq->reset();
    compressor->reset();
}

void FlamEngine::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        const auto samplePosition = metadata.samplePosition;
        handleMidiEvent(message, samplePosition);
    }

    // Ensure internal buffer is correct size (may need resize if block size changed)
    if (internalBuffer.getNumSamples() < numSamples)
        internalBuffer.setSize(internalBuffer.getNumChannels(), numSamples, false, false, true);

    // Clear internal buffer and render drum voices to full multi-channel output
    internalBuffer.clear();
    voiceManager->renderNextBlock(internalBuffer, 0, numSamples);

    // Copy multi-channel internal buffer to output buffer
    // This is where the Mixer will process later - for now just copy what fits
    buffer.clear();
    const int channelsToCopy = juce::jmin(buffer.getNumChannels(), internalBuffer.getNumChannels());
    for (int ch = 0; ch < channelsToCopy; ++ch)
    {
        buffer.copyFrom(ch, 0, internalBuffer, ch, 0, numSamples);
    }

    // Measure input level (before input gain)
    float inLevel = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float channelLevel = buffer.getMagnitude(ch, 0, numSamples);
        inLevel = std::max(inLevel, channelLevel);
    }
    inputLevel.store(inLevel, std::memory_order_relaxed);

    // Apply input gain
    buffer.applyGain(inputGain);

    // Update and apply EQ
    eq->updateIfNeeded(currentSampleRate);
    eq->processBlock(buffer);

    // Apply compressor
    compressor->processBlock(buffer);

    // Apply master level (bypass multi-bus processing for now)
    const float masterGain = juce::Decibels::decibelsToGain(mixerBus->getMasterLevel());
    buffer.applyGain(masterGain);

    // Measure output level (after all processing)
    float outLevel = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float channelLevel = buffer.getMagnitude(ch, 0, numSamples);
        outLevel = std::max(outLevel, channelLevel);
    }
    outputLevel.store(outLevel, std::memory_order_relaxed);

    midiMessages.clear();
}

void FlamEngine::loadKit(const juce::File& kitFile)
{
    // Do the loading synchronously - the caller is responsible for threading
    // This avoids nested thread launches which can cause race conditions
    auto kit = kitLoader->loadKit(kitFile);
    if (kit)
    {
        voiceManager->loadKit(std::move(kit));
    }
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

int FlamEngine::getRequiredChannelCount() const
{
    return voiceManager->getRequiredChannelCount();
}

} // namespace flam