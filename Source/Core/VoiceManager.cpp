#include "VoiceManager.h"
#include "SampleVoice.h"
#include "../Formats/FlamKitLoader.h"
#include <juce_audio_formats/juce_audio_formats.h>

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
    std::cout << "[VoiceManager] triggerNote called: Note " << midiNote
              << ", Velocity " << velocity << std::endl;

    const juce::SpinLock::ScopedLockType lock(voiceLock);

    if (!currentKit)
    {
        std::cout << "[VoiceManager] ERROR: No kit loaded!" << std::endl;
        return;
    }

    std::cout << "[VoiceManager] Kit loaded with " << currentKit->pieces.size()
              << " pieces" << std::endl;

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

    if (!targetPiece)
    {
        std::cout << "[VoiceManager] No drum piece found for MIDI note " << midiNote << std::endl;
        return;
    }

    if (targetPiece->articulations.empty())
    {
        std::cout << "[VoiceManager] Drum piece has no articulations" << std::endl;
        return;
    }

    std::cout << "[VoiceManager] Found drum piece: " << targetPiece->name
              << " with " << targetPiece->articulations.size() << " articulations" << std::endl;

    // For now, use the first articulation (later could support articulation switching)
    const auto& articulation = targetPiece->articulations[0];

    if (articulation.layers.empty())
    {
        std::cout << "[VoiceManager] Articulation has no layers" << std::endl;
        return;
    }

    std::cout << "[VoiceManager] Articulation has " << articulation.layers.size()
              << " layers" << std::endl;

    // Get round-robin index
    const int rrIndex = getNextRoundRobinIndex(midiNote);

    // Find best velocity layer
    const SampleLayer* bestLayer = findBestLayer(targetPiece, velocity, rrIndex);

    if (!bestLayer)
    {
        std::cout << "[VoiceManager] No suitable layer found" << std::endl;
        return;
    }

    std::cout << "[VoiceManager] Selected layer: " << bestLayer->sampleFile.getFullPathName() << std::endl;

    // Find free voice
    const int voiceIndex = findFreeVoice();
    if (voiceIndex < 0)
    {
        std::cout << "[VoiceManager] No free voice available" << std::endl;
        return;
    }

    std::cout << "[VoiceManager] Using voice index " << voiceIndex << std::endl;

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

    // Load sample data into voice if not already loaded
    if (bestLayer->loadedSampleBuffer)
    {
        voice.sampleVoice->loadSampleData(bestLayer->loadedSampleBuffer, bestLayer->sourceSampleRate);
        std::cout << "[VoiceManager] Sample data loaded into voice" << std::endl;
    }
    else
    {
        std::cout << "[VoiceManager] ERROR: No sample buffer available for layer" << std::endl;
        return;
    }

    // Start the voice
    startVoice(voiceIndex, midiNote, velocity, sampleOffset);

    // Trigger the sample voice with the selected layer
    voice.sampleVoice->startNote(bestLayer, velocity, sampleOffset);

    std::cout << "[VoiceManager] Voice triggered successfully" << std::endl;
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
    std::cout << "[VoiceManager] loadKit called" << std::endl;

    if (!kit)
    {
        std::cout << "[VoiceManager] ERROR: Received null kit" << std::endl;
        return;
    }

    std::cout << "[VoiceManager] Loading kit: " << kit->name
              << " with " << kit->pieces.size() << " pieces" << std::endl;

    // Store kit immediately so it can be used (samples will load in background)
    {
        const juce::SpinLock::ScopedLockType lock(voiceLock);
        currentKit = std::move(kit);

        // Apply kit settings
        setPolyphony(currentKit->settings.maxPolyphony);
        setRoundRobinEnabled(currentKit->settings.useRoundRobin);
    }

    // Load samples in background thread to avoid UI freeze
    juce::Thread::launch([this]() {
        std::cout << "[VoiceManager] Starting background sample loading..." << std::endl;

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        int totalSamples = 0;
        int loadedSamples = 0;

        // Access kit data safely
        const juce::SpinLock::ScopedLockType lock(voiceLock);
        if (!currentKit)
        {
            std::cout << "[VoiceManager] ERROR: Kit was cleared during background load" << std::endl;
            return;
        }

        for (auto& piece : currentKit->pieces)
        {
            std::cout << "[VoiceManager] Processing piece: " << piece.name
                      << " (MIDI " << piece.midiNote << ")" << std::endl;

            for (auto& articulation : piece.articulations)
            {
                for (auto& layer : articulation.layers)
                {
                    totalSamples++;

                    if (!layer.sampleFile.existsAsFile())
                    {
                        std::cout << "[VoiceManager] WARNING: Sample file not found: "
                                  << layer.sampleFile.getFullPathName() << std::endl;
                        continue;
                    }

                    std::unique_ptr<juce::AudioFormatReader> reader(
                        formatManager.createReaderFor(layer.sampleFile));

                    if (!reader)
                    {
                        std::cout << "[VoiceManager] ERROR: Could not create reader for: "
                                  << layer.sampleFile.getFullPathName() << std::endl;
                        continue;
                    }

                    const int numSamples = static_cast<int>(reader->lengthInSamples);
                    const int numChannels = static_cast<int>(reader->numChannels);
                    const double srcSampleRate = reader->sampleRate;

                    // Allocate and read sample data
                    auto buffer = std::make_shared<juce::AudioBuffer<float>>(numChannels, numSamples);

                    if (!reader->read(buffer.get(), 0, numSamples, 0, true, true))
                    {
                        std::cout << "[VoiceManager] ERROR: Failed to read samples from: "
                                  << layer.sampleFile.getFullPathName() << std::endl;
                        continue;
                    }

                    // Store the loaded sample data in the layer (atomic operation)
                    layer.loadedSampleBuffer = buffer;
                    layer.sourceSampleRate = srcSampleRate;

                    loadedSamples++;

                    if (loadedSamples % 10 == 0)
                        std::cout << "[VoiceManager] Loaded " << loadedSamples << "/" << totalSamples << " samples..." << std::endl;
                }
            }
        }

        std::cout << "[VoiceManager] Background loading complete: " << loadedSamples
                  << " of " << totalSamples << " samples loaded successfully" << std::endl;
    });

    std::cout << "[VoiceManager] Kit structure loaded, samples loading in background..." << std::endl;
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
