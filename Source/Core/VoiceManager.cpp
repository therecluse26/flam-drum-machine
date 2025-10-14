#include "VoiceManager.h"
#include "SampleVoice.h"
#include "../Formats/FlamKitLoader.h"

namespace flam {

VoiceManager::VoiceManager()
{
    voices.resize(MAX_VOICES);

    for (auto& voice : voices)
    {
        voice.sampleVoice = std::make_unique<SampleVoice>();
    }

    std::fill(chokeGroupLastVoice.begin(), chokeGroupLastVoice.end(), -1);
    std::fill(roundRobinCounters.begin(), roundRobinCounters.end(), 0);
}

VoiceManager::~VoiceManager() = default;

void VoiceManager::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    this->sampleRate = sampleRate;
    this->blockSize = samplesPerBlock;

    for (auto& voice : voices)
    {
        voice.sampleVoice->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void VoiceManager::releaseResources()
{
    clearKit();

    for (auto& voice : voices)
    {
        voice.sampleVoice->reset();
        voice.isActive = false;
        voice.midiNote = -1;
    }
}

void VoiceManager::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                   int startSample, int numSamples)
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);

    for (auto& voice : voices)
    {
        if (voice.isActive && voice.sampleVoice)
        {
            voice.sampleVoice->renderNextBlock(outputBuffer, startSample, numSamples);

            // Check if voice has finished playing
            if (!voice.sampleVoice->isActive())
            {
                voice.isActive = false;
                voice.midiNote = -1;
            }
        }
    }
}

void VoiceManager::triggerNote(int midiNote, float velocity, int sampleOffset)
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);

    if (!currentKit)
        return;

    // Find the drum piece for this MIDI note
    const DrumPiece* targetPiece = nullptr;
    for (const auto& piece : currentKit->pieces)
    {
        if (piece.midiNote == midiNote)
        {
            targetPiece = &piece;
            break;
        }
    }

    if (!targetPiece || targetPiece->articulations.empty())
        return;

    // For now, use the first articulation (later could support articulation switching)
    const auto& articulation = targetPiece->articulations[0];

    if (articulation.layers.empty())
        return;

    // Get round-robin index
    const int rrIndex = getNextRoundRobinIndex(midiNote);

    // Find best velocity layer
    const SampleLayer* bestLayer = findBestLayer(targetPiece, velocity, rrIndex);

    if (!bestLayer)
        return;

    // Find free voice
    const int voiceIndex = findFreeVoice();
    if (voiceIndex >= 0)
    {
        auto& voice = voices[voiceIndex];

        // Handle choke groups (e.g., hi-hat open/closed)
        if (articulation.chokeGroup >= 0)
        {
            handleChokeGroup(articulation.chokeGroup, voiceIndex);
            voice.chokeGroup = articulation.chokeGroup;
        }

        // Set envelope parameters from articulation
        voice.sampleVoice->setEnvelopeParameters(
            articulation.attackTime,
            articulation.holdTime,
            articulation.decayTime,
            articulation.sustainLevel,
            articulation.releaseTime
        );

        // Start the voice
        startVoice(voiceIndex, midiNote, velocity, sampleOffset);

        // Trigger the sample voice with the selected layer
        voice.sampleVoice->startNote(bestLayer, velocity, sampleOffset);
    }
}

void VoiceManager::releaseNote(int midiNote, int sampleOffset)
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);

    for (auto& voice : voices)
    {
        if (voice.isActive && voice.midiNote == midiNote && voice.sampleVoice)
        {
            voice.sampleVoice->stopNote(sampleOffset);
        }
    }
}

void VoiceManager::loadKit(std::unique_ptr<DrumKit> kit)
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);
    currentKit = std::move(kit);

    // Apply kit settings
    if (currentKit)
    {
        setPolyphony(currentKit->settings.maxPolyphony);
        setRoundRobinEnabled(currentKit->settings.useRoundRobin);
    }
}

void VoiceManager::clearKit()
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);
    currentKit.reset();
}

void VoiceManager::setPolyphony(int maxVoices)
{
    maxActiveVoices.store(juce::jlimit(1, MAX_VOICES, maxVoices));
}

int VoiceManager::findFreeVoice() const
{
    int oldestVoiceIndex = -1;
    int oldestAge = -1;

    const int maxVoices = maxActiveVoices.load();

    // First pass: find an inactive voice
    for (int i = 0; i < maxVoices; ++i)
    {
        if (!voices[i].isActive)
        {
            return i;
        }
    }

    // Second pass: steal the oldest voice
    for (int i = 0; i < maxVoices; ++i)
    {
        if (voices[i].sampleVoice)
        {
            const int age = voices[i].sampleVoice->getAge();
            if (age > oldestAge)
            {
                oldestAge = age;
                oldestVoiceIndex = i;
            }
        }
    }

    return oldestVoiceIndex;
}

void VoiceManager::startVoice(int voiceIndex, int midiNote, float velocity, int sampleOffset)
{
    juce::ignoreUnused(velocity, sampleOffset);

    if (voiceIndex < 0 || voiceIndex >= static_cast<int>(voices.size()))
        return;

    auto& voice = voices[voiceIndex];

    voice.midiNote = midiNote;
    voice.isActive = true;
}

void VoiceManager::stopVoice(int voiceIndex, int sampleOffset)
{
    if (voiceIndex < 0 || voiceIndex >= static_cast<int>(voices.size()))
        return;

    auto& voice = voices[voiceIndex];
    if (voice.sampleVoice)
    {
        voice.sampleVoice->stopNote(sampleOffset);
    }
}

void VoiceManager::handleChokeGroup(int chokeGroup, int excludeVoice)
{
    if (chokeGroup < 0 || chokeGroup >= MAX_CHOKE_GROUPS)
        return;

    // Stop all voices in this choke group except the new one
    for (int i = 0; i < static_cast<int>(voices.size()); ++i)
    {
        if (i != excludeVoice && voices[i].isActive &&
            voices[i].chokeGroup == chokeGroup)
        {
            stopVoice(i, 0);
        }
    }

    chokeGroupLastVoice[chokeGroup] = excludeVoice;
}

int VoiceManager::getNextRoundRobinIndex(int midiNote)
{
    if (!useRoundRobin.load())
        return 0;

    if (midiNote < 0 || midiNote >= 128)
        return 0;

    const int index = roundRobinCounters[midiNote];
    roundRobinCounters[midiNote] = (index + 1) % 256; // Wrap at 256
    return index;
}

const SampleLayer* VoiceManager::findBestLayer(const DrumPiece* piece,
                                               float velocity, int rrIndex) const
{
    if (!piece || piece->articulations.empty())
        return nullptr;

    // Use first articulation (could be extended for articulation switching)
    const auto& articulation = piece->articulations[0];

    if (articulation.layers.empty())
        return nullptr;

    // Find all layers that match the velocity range
    std::vector<const SampleLayer*> matchingLayers;

    for (const auto& layer : articulation.layers)
    {
        if (velocity >= layer.velocityMin && velocity <= layer.velocityMax)
        {
            matchingLayers.push_back(&layer);
        }
    }

    if (matchingLayers.empty())
    {
        // If no exact match, return the first layer as fallback
        return &articulation.layers[0];
    }

    // If round-robin is enabled and we have multiple matching layers,
    // select based on round-robin group
    if (matchingLayers.size() > 1 && useRoundRobin.load())
    {
        // Group by round-robin group
        std::vector<const SampleLayer*> rrGroup;

        for (const auto* layer : matchingLayers)
        {
            if (layer->roundRobinGroup == (rrIndex % 16))
                rrGroup.push_back(layer);
        }

        if (!rrGroup.empty())
            return rrGroup[0];
    }

    // Return first matching layer
    return matchingLayers[0];
}

} // namespace flam
