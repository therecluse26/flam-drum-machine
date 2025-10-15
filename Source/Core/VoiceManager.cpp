#include "VoiceManager.h"
#include "SampleVoice.h"
#include "SampleStreamingManager.h"
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

    streamingManager = std::make_unique<SampleStreamingManager>();

    std::fill(chokeGroupLastVoice.begin(), chokeGroupLastVoice.end(), -1);
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

    if (streamingManager)
        streamingManager->prepareToPlay(sampleRate, samplesPerBlock);
}

void VoiceManager::releaseResources()
{
    clearKit();

    for (auto& voice : voices)
    {
        voice.sampleVoice->reset();
        voice.isActive = false;
        voice.midiNote = -1;
        voice.chokeGroup = -1;
    }

    if (streamingManager)
        streamingManager->releaseResources();
}

void VoiceManager::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                   int startSample, int numSamples)
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);

    // Poll for streamed data and distribute to voices
    if (streamingManager)
    {
        while (auto streamedData = streamingManager->getNextStreamedData())
        {
            // Find the voice with this ID
            for (auto& voice : voices)
            {
                if (voice.sampleVoice && voice.sampleVoice->getVoiceId() == streamedData->voiceId)
                {
                    voice.sampleVoice->appendStreamedChunk(
                        streamedData->buffer, streamedData->isComplete);
                    break;
                }
            }
        }

        // Check if any voices need streaming
        for (auto& voice : voices)
        {
            if (voice.isActive && voice.sampleVoice && voice.sampleVoice->needsStreaming())
            {
                const auto* layer = voice.sampleVoice->getCurrentLayer();
                const int voiceId = voice.sampleVoice->getVoiceId();

                if (layer && layer->preloadBuffer)
                {
                    const int preloadSamples = layer->preloadBuffer->getNumSamples();
                    streamingManager->requestStream(layer, voiceId, preloadSamples);
                    voice.sampleVoice->markStreamingRequested();
                }
            }
        }
    }

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
                voice.chokeGroup = -1;  // Reset choke group
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

    // Find best velocity layer (uses smart round-robin internally)
    const SampleLayer* bestLayer = findBestLayer(targetPiece, velocity, midiNote);

    if (!bestLayer)
        return;

    // Track this sample to avoid immediate repetition in future round-robin selections
    if (midiNote >= 0 && midiNote < 128)
        recentSamples[midiNote].addSample(bestLayer);

    // Find free voice
    const int voiceIndex = findFreeVoice();
    if (voiceIndex < 0)
        return;

    auto& voice = voices[voiceIndex];

    // First, stop any currently playing instances of the same MIDI note
    // This prevents "machine gun" buildup when retriggering the same drum
    for (int i = 0; i < static_cast<int>(voices.size()); ++i)
    {
        if (i != voiceIndex && voices[i].isActive && voices[i].midiNote == midiNote)
        {
            stopVoice(i, sampleOffset);
        }
    }

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

    // Load preload buffer into voice (streamed remainder will be handled by SampleVoice)
    if (bestLayer->preloadBuffer)
    {
        voice.sampleVoice->loadSampleData(bestLayer, voiceIndex);
    }
    else
    {
        return;  // Preload not ready yet
    }

    // Start the voice
    startVoice(voiceIndex, midiNote, velocity, sampleOffset);

    // Trigger the sample voice with the selected layer
    voice.sampleVoice->startNote(bestLayer, velocity, sampleOffset);
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
    if (!kit)
        return;

    // Store kit immediately so it can be used (samples will load in background)
    {
        const juce::SpinLock::ScopedLockType lock(voiceLock);
        currentKit = std::move(kit);

        // Apply kit settings
        setPolyphony(currentKit->settings.maxPolyphony);
        setRoundRobinEnabled(currentKit->settings.useRoundRobin);
    }

    // Load preload buffers in background thread (first 5ms of each sample)
    juce::Thread::launch([this]() {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        // Build a list of all sample layers that need loading WITHOUT holding the lock
        std::vector<SampleLayer*> layersToLoad;

        {
            const juce::SpinLock::ScopedLockType lock(voiceLock);
            if (!currentKit)
                return;

            // Collect all layer pointers
            for (auto& piece : currentKit->pieces)
            {
                for (auto& articulation : piece.articulations)
                {
                    for (auto& layer : articulation.layers)
                    {
                        layersToLoad.push_back(&layer);
                    }
                }
            }
        }
        // Lock is now released - we can do slow I/O operations

        // Load preload buffer for each sample WITHOUT holding the lock
        for (auto* layer : layersToLoad)
        {
            if (!layer->sampleFile.existsAsFile())
                continue;

            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager.createReaderFor(layer->sampleFile));

            if (!reader)
                continue;

            const auto totalLength = reader->lengthInSamples;
            const int numChannels = static_cast<int>(reader->numChannels);
            const double srcSampleRate = reader->sampleRate;

            // Calculate preload size (5ms worth of samples)
            const int preloadSamples = static_cast<int>((srcSampleRate * 5.0) / 1000.0);
            const int actualPreloadSize = juce::jmin(preloadSamples, static_cast<int>(totalLength));

            // Allocate and read only the preload portion
            auto preloadBuffer = std::make_shared<juce::AudioBuffer<float>>(numChannels, actualPreloadSize);

            if (!reader->read(preloadBuffer.get(), 0, actualPreloadSize, 0, true, true))
                continue;

            // Store the preload buffer and metadata
            layer->preloadBuffer = preloadBuffer;
            layer->sourceSampleRate = srcSampleRate;
            layer->totalSampleLength = totalLength;
        }
    });
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


const SampleLayer* VoiceManager::findBestLayer(const DrumPiece* piece,
                                               float velocity, int midiNote)
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

    // If only one matching layer, use it directly (no round-robin needed)
    if (matchingLayers.size() == 1)
        return matchingLayers[0];

    // Multiple matching layers - use smart round-robin to avoid repetition
    if (useRoundRobin.load() && midiNote >= 0 && midiNote < 128)
    {
        const int totalSamples = static_cast<int>(matchingLayers.size());

        // Calculate history size: min(3, samples - 1)
        // This ensures we exclude last 3 samples, or (samples-1) if fewer samples exist
        // Examples: 2 samples -> history 1, 5 samples -> history 3, 10 samples -> history 3
        const int historySize = juce::jmin(3, totalSamples - 1);

        // Filter out recently played samples
        std::vector<const SampleLayer*> availableLayers;
        for (const auto* layer : matchingLayers)
        {
            if (!recentSamples[midiNote].wasRecentlyPlayed(layer, historySize))
                availableLayers.push_back(layer);
        }

        // If all samples were recently played (shouldn't happen with proper history size),
        // fall back to all matching layers
        if (availableLayers.empty())
            availableLayers = matchingLayers;

        // Pick randomly from available layers
        const int randomIndex = static_cast<int>(
            juce::Random::getSystemRandom().nextInt(static_cast<int>(availableLayers.size()))
        );

        return availableLayers[randomIndex];
    }

    // Round-robin disabled - return first matching layer
    return matchingLayers[0];
}

} // namespace flam
