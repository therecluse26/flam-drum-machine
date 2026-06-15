// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "VoiceManager.h"
#include "SampleVoice.h"
#include "SampleStreamingManager.h"
#include "../Formats/FlamKitLoader.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace flam {

VoiceManager::VoiceManager()
{
    // TOTAL_VOICE_SLOTS = MAX_VOICES + FADE_OVERFLOW_SLOTS.
    // Slots [0, maxActiveVoices) serve new notes; slots [maxActiveVoices, TOTAL)
    // hold voices that are fading after a steal so the main slot is freed immediately.
    voices.resize(TOTAL_VOICE_SLOTS);

    for (auto& voice : voices)
    {
        voice.sampleVoice = std::make_unique<SampleVoice>();
    }

    streamingManager = std::make_unique<SampleStreamingManager>();

    std::fill(chokeGroupLastVoice.begin(), chokeGroupLastVoice.end(), -1);
}

VoiceManager::~VoiceManager()
{
    // Cancel + join the preload worker before any member it touches (currentKit,
    // voiceLock, streamingManager) begins destructing. A still-joinable std::thread
    // would call std::terminate, and a running loader would dereference freed memory.
    const std::lock_guard<std::mutex> loadGuard(loadMutex);
    stopBackgroundLoad();
}

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
    // Do NOT clear the loaded kit here. releaseResources() runs on every audio device
    // stop/reconfigure — e.g. changing the buffer size or sample rate in the standalone's
    // audio settings — and the host calls prepareToPlay() again immediately afterward,
    // expecting the plugin to keep working. Discarding currentKit silenced all playback
    // until an app restart (which reloaded the last kit). Kit teardown belongs in
    // clearKit()/loadKit()/the destructor, not in the device lifecycle. preloadBuffers live
    // on the kit and are sample-rate-independent, so they remain valid across a reconfigure.

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
            // Find the voice with matching voiceId AND streamId
            for (auto& voice : voices)
            {
                if (voice.sampleVoice &&
                    voice.sampleVoice->getVoiceId() == streamedData->voiceId &&
                    voice.sampleVoice->getCurrentStreamId() == streamedData->streamId)
                {
                    // Only append if voice is still playing the same stream instance
                    voice.sampleVoice->appendStreamedChunk(
                        streamedData->buffer, streamedData->isComplete);
                    break;
                }
            }
            // If no matching voice found, chunk is dropped (voice was stolen/reset)
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
                // Cancel any pending streaming for this voice
                if (streamingManager)
                    streamingManager->cancelStream(voice.sampleVoice->getVoiceId());

                voice.sampleVoice->reset();  // Clear streamed chunks to free memory
                voice.isActive = false;
                voice.midiNote = -1;
                voice.chokeGroup = -1;
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

    // Voice stealing: if the selected slot is still active, fade it out instead
    // of hard-cutting to avoid a click from a step discontinuity.
    if (voice.isActive && voice.sampleVoice)
    {
        if (voice.sampleVoice->isFadingOut())
        {
            // Already fading — it's near-silent, safe to hard-stop.
            if (streamingManager)
                streamingManager->cancelStream(voice.sampleVoice->getVoiceId());
            voice.sampleVoice->reset();
        }
        else
        {
            // Active non-fading voice: swap it to a fade-overflow slot so it
            // can ramp to silence while the new note starts immediately here.
            const int fadeSlot = findFadeSlot();
            if (fadeSlot >= 0)
            {
                std::swap(voice.sampleVoice, voices[fadeSlot].sampleVoice);

                // Cancel streaming for the fading voice; it uses only buffered data.
                if (streamingManager)
                    streamingManager->cancelStream(voices[fadeSlot].sampleVoice->getVoiceId());

                const int fadeSamples = static_cast<int>(sampleRate * 0.010); // 10 ms
                voices[fadeSlot].sampleVoice->beginFadeOut(fadeSamples);
                voices[fadeSlot].isActive   = true;
                voices[fadeSlot].midiNote   = -1;   // prevent choke-group interactions
                voices[fadeSlot].chokeGroup = -1;

                // The swapped-in SampleVoice (from the inactive fade slot) needs reset.
                voice.sampleVoice->reset();
            }
            else
            {
                // All fade-overflow slots occupied — fall back to immediate stop.
                if (streamingManager)
                    streamingManager->cancelStream(voice.sampleVoice->getVoiceId());
                voice.sampleVoice->reset();
            }
        }
        voice.isActive   = false;
        voice.midiNote   = -1;
        voice.chokeGroup = -1;
    }

    // First, stop any currently playing instances of the same MIDI note
    // This prevents "machine gun" buildup when retriggering the same drum
    for (int i = 0; i < static_cast<int>(voices.size()); ++i)
    {
        if (i != voiceIndex && voices[i].isActive && voices[i].midiNote == midiNote)
        {
            // Cancel streaming to prevent more data from loading
            if (streamingManager && voices[i].sampleVoice)
                streamingManager->cancelStream(voices[i].sampleVoice->getVoiceId());

            // Force quick 5ms release - creates smooth crossfade without pops,
            // but cleans up fast enough to prevent memory leak
            if (voices[i].sampleVoice)
                voices[i].sampleVoice->forceQuickRelease();
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
        // Get a stream ID for this voice instance
        // We'll pass it now even though streaming happens later
        const int preloadSamples = bestLayer->preloadBuffer->getNumSamples();
        const int streamId = streamingManager ?
            streamingManager->requestStream(bestLayer, voiceIndex, preloadSamples) : -1;

        voice.sampleVoice->loadSampleData(bestLayer, voiceIndex, streamId);

        // Mark as requested since we already called requestStream
        if (streamingManager && streamId >= 0)
            voice.sampleVoice->markStreamingRequested();
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

    // Serialize against concurrent loadKit()/clearKit() — the editor fires one detached
    // thread per kit click (PluginEditor::loadKitFromPath), so two rapid switches can race
    // here. Not real-time safe: call only from a non-audio thread.
    const std::lock_guard<std::mutex> loadGuard(loadMutex);

    // Cancel + join any in-flight preload before we swap the kit it holds raw pointers
    // into — otherwise the old worker would write into freed SampleLayers (use-after-free).
    stopBackgroundLoad();

    // Store kit immediately so it can be used (samples will load in background)
    {
        const juce::SpinLock::ScopedLockType lock(voiceLock);
        currentKit = std::move(kit);

        // Apply kit settings
        setPolyphony(currentKit->settings.maxPolyphony);
        setRoundRobinEnabled(currentKit->settings.useRoundRobin);
    }

    // Set BEFORE launching so isKitLoaded()/waitForKitLoaded() never observe a false
    // "loaded" in the gap between launch and the worker's first store.
    isKitLoading.store(true);

    // In offline mode, load the full sample; in real-time mode load only the first 100ms (PRELOAD_MS)
    const bool offline = offlineMode.load();

    // Owned + joinable (see stopBackgroundLoad) — replaces the old detached juce::Thread::launch.
    loaderThread = std::thread([this, offline]() {
        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        // Build a list of all sample layers that need loading WITHOUT holding the lock
        std::vector<SampleLayer*> layersToLoad;

        {
            const juce::SpinLock::ScopedLockType lock(voiceLock);
            if (!currentKit)
            {
                isKitLoading.store(false);
                return;
            }

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
            // Bail promptly if a newer loadKit()/clearKit()/destructor asked us to stop —
            // keeps their join() fast (worst case: one in-flight preload read).
            if (abortLoad.load())
            {
                isKitLoading.store(false);
                return;
            }

            if (!layer->sampleFile.existsAsFile())
                continue;

            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager.createReaderFor(layer->sampleFile));

            if (!reader)
                continue;

            const auto totalLength = reader->lengthInSamples;
            const int numChannels = static_cast<int>(reader->numChannels);
            const double srcSampleRate = reader->sampleRate;

            // Offline: load full sample (capped at 30 s) to avoid streaming mid-render.
            // Real-time: load SampleStreamingManager::PRELOAD_MS (100 ms); the streamer
            // delivers the rest. The preload MUST outlast one audio buffer plus the
            // worst-case first-chunk fetch (cold file open + disk read) — the first
            // streamed chunk can never arrive in the same block it was requested in
            // (renderNextBlock polls before the streaming thread runs). A too-small
            // window (the old 5 ms) underran on every hit at common buffer sizes,
            // producing attack-transient pops. Sourced from the shared constant so it
            // stays in lockstep with the streamer's resume offset.
            const int preloadSamples = offline
                ? static_cast<int>(juce::jmin(totalLength,
                      static_cast<juce::int64>(srcSampleRate * 30.0)))
                : SampleStreamingManager::getPreloadSamples(srcSampleRate);
            const int actualPreloadSize = juce::jmin(preloadSamples, static_cast<int>(totalLength));

            // Allocate and read the preload portion
            auto preloadBuffer = std::make_shared<juce::AudioBuffer<float>>(numChannels, actualPreloadSize);

            if (!reader->read(preloadBuffer.get(), 0, actualPreloadSize, 0, true, true))
                continue;

            // Publish under voiceLock. Every reader of these fields (triggerNote,
            // renderNextBlock via loadSampleData, getRequiredChannelCount) holds voiceLock,
            // so this removes the torn shared_ptr read and guarantees a layer is never seen
            // half-published. Keep the critical section to these three stores ONLY — the
            // audio thread spins on this SpinLock, so no I/O or allocation inside it.
            {
                const juce::SpinLock::ScopedLockType pub(voiceLock);
                layer->preloadBuffer = preloadBuffer;
                layer->sourceSampleRate = srcSampleRate;
                layer->totalSampleLength = totalLength;
            }
        }

        isKitLoading.store(false);
    });
}

void VoiceManager::stopBackgroundLoad()
{
    // Caller MUST hold loadMutex, and MUST NOT hold voiceLock: the worker takes voiceLock
    // to publish each layer, so joining while holding it would deadlock.
    abortLoad.store(true);
    if (loaderThread.joinable())
        loaderThread.join();
    abortLoad.store(false);
    isKitLoading.store(false);
}

void VoiceManager::clearKit()
{
    // Serialize with loadKit and cancel/join any in-flight preload BEFORE freeing the kit
    // it points into. Not real-time safe: non-audio thread only.
    const std::lock_guard<std::mutex> loadGuard(loadMutex);
    stopBackgroundLoad();

    const juce::SpinLock::ScopedLockType lock(voiceLock);
    currentKit.reset();
}

void VoiceManager::setPolyphony(int maxVoices)
{
    maxActiveVoices.store(juce::jlimit(1, MAX_VOICES, maxVoices));
}

int VoiceManager::findFreeVoice() const
{
    const int maxVoices = maxActiveVoices.load();

    // Pass 1: inactive voice — best choice, no audible artifact.
    for (int i = 0; i < maxVoices; ++i)
        if (!voices[i].isActive)
            return i;

    // Pass 2: already-fading voice — it will be hard-stopped, but it's nearly
    // silent so the artifact is minimal compared to cutting a fully-active voice.
    int oldestFadingIndex = -1;
    int oldestFadingAge   = -1;
    for (int i = 0; i < maxVoices; ++i)
    {
        if (voices[i].isActive && voices[i].sampleVoice &&
            voices[i].sampleVoice->isFadingOut())
        {
            const int age = voices[i].sampleVoice->getAge();
            if (age > oldestFadingAge)
            {
                oldestFadingAge   = age;
                oldestFadingIndex = i;
            }
        }
    }
    if (oldestFadingIndex >= 0)
        return oldestFadingIndex;

    // Pass 3: steal the oldest active (non-fading) voice. The caller will move
    // it to a fade-overflow slot so it can ramp to silence rather than hard-cut.
    int oldestVoiceIndex = -1;
    int oldestAge        = -1;
    for (int i = 0; i < maxVoices; ++i)
    {
        if (voices[i].sampleVoice && !voices[i].sampleVoice->isFadingOut())
        {
            const int age = voices[i].sampleVoice->getAge();
            if (age > oldestAge)
            {
                oldestAge        = age;
                oldestVoiceIndex = i;
            }
        }
    }

    return oldestVoiceIndex;
}

int VoiceManager::findFadeSlot() const
{
    const int maxVoices = maxActiveVoices.load();
    for (int i = maxVoices; i < TOTAL_VOICE_SLOTS; ++i)
        if (!voices[i].isActive)
            return i;
    return -1;
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
            // Cancel streaming to prevent more data from loading
            if (streamingManager && voices[i].sampleVoice)
                streamingManager->cancelStream(voices[i].sampleVoice->getVoiceId());

            // Force quick release for choke groups (hi-hat open/closed, etc)
            if (voices[i].sampleVoice)
                voices[i].sampleVoice->forceQuickRelease();
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

        // Pick randomly from available layers using the per-instance RNG (seedable for tests)
        const int randomIndex = rng.nextInt(static_cast<int>(availableLayers.size()));

        return availableLayers[randomIndex];
    }

    // Round-robin disabled - return first matching layer
    return matchingLayers[0];
}

int VoiceManager::getActiveVoiceCount() const
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);
    const int maxVoices = maxActiveVoices.load();
    int count = 0;
    // Count only the main voice zone; fade-overflow voices are transparent to
    // the polyphony meter — they're transient artifacts of voice stealing.
    for (int i = 0; i < maxVoices; ++i)
        if (voices[i].isActive)
            ++count;
    return count;
}

int VoiceManager::getRequiredChannelCount() const
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);

    if (!currentKit)
        return 2;  // Default to stereo

    int maxChannels = 2;  // Minimum stereo

    // Scan all samples in the kit to find the maximum channel count
    for (const auto& piece : currentKit->pieces)
    {
        for (const auto& articulation : piece.articulations)
        {
            for (const auto& layer : articulation.layers)
            {
                if (layer.preloadBuffer)
                {
                    maxChannels = juce::jmax(maxChannels, layer.preloadBuffer->getNumChannels());
                }
            }
        }
    }

    return maxChannels;
}

} // namespace flam
