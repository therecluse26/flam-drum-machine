#include "VoiceManager.h"

namespace flam {

struct DrumKit
{
    // Placeholder for kit data structure
};

struct DrumVoice
{
    // Placeholder for voice data structure
};

VoiceManager::VoiceManager()
{
    voices.resize(MAX_VOICES);
    
    for (auto& voice : voices)
    {
        voice.drumVoice = std::make_unique<DrumVoice>();
    }
    
    std::fill(chokeGroupLastVoice.begin(), chokeGroupLastVoice.end(), -1);
    std::fill(roundRobinCounters.begin(), roundRobinCounters.end(), 0);
}

VoiceManager::~VoiceManager() = default;

void VoiceManager::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    this->sampleRate = sampleRate;
    this->blockSize = samplesPerBlock;
    
    juce::ADSR::Parameters envParams;
    envParams.attack = 0.001f;
    envParams.decay = 0.1f;
    envParams.sustain = 1.0f;
    envParams.release = 0.05f;
    
    for (auto& voice : voices)
    {
        voice.envelope.setSampleRate(sampleRate);
        voice.envelope.setParameters(envParams);
    }
}

void VoiceManager::releaseResources()
{
    clearKit();
    
    for (auto& voice : voices)
    {
        voice.isActive = false;
        voice.midiNote = -1;
    }
}

void VoiceManager::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);
    
    for (auto& voice : voices)
    {
        if (voice.isActive)
        {
            // Process active voice (placeholder implementation)
            for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
            {
                auto* channelData = outputBuffer.getWritePointer(channel, startSample);
                
                for (int sample = 0; sample < numSamples; ++sample)
                {
                    const float envelopeValue = voice.envelope.getNextSample();
                    
                    if (!voice.envelope.isActive())
                    {
                        voice.isActive = false;
                        break;
                    }
                    
                    // Placeholder: would read from sample buffer here
                    channelData[sample] += 0.0f;
                }
            }
            
            voice.samplePosition += numSamples;
        }
    }
}

void VoiceManager::triggerNote(int midiNote, float velocity, int sampleOffset)
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);
    
    const int voiceIndex = findFreeVoice();
    if (voiceIndex >= 0)
    {
        startVoice(voiceIndex, midiNote, velocity, sampleOffset);
    }
}

void VoiceManager::releaseNote(int midiNote, int sampleOffset)
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);
    
    for (auto& voice : voices)
    {
        if (voice.isActive && voice.midiNote == midiNote)
        {
            voice.envelope.noteOff();
        }
    }
}

void VoiceManager::loadKit(std::unique_ptr<DrumKit> kit)
{
    const juce::SpinLock::ScopedLockType lock(voiceLock);
    currentKit = std::move(kit);
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
    int oldestSamplePosition = INT_MAX;
    
    for (int i = 0; i < maxActiveVoices.load(); ++i)
    {
        if (!voices[i].isActive)
        {
            return i;
        }
        
        if (voices[i].samplePosition < oldestSamplePosition)
        {
            oldestSamplePosition = voices[i].samplePosition;
            oldestVoiceIndex = i;
        }
    }
    
    return oldestVoiceIndex;
}

void VoiceManager::startVoice(int voiceIndex, int midiNote, float velocity, int sampleOffset)
{
    if (voiceIndex < 0 || voiceIndex >= voices.size())
        return;
    
    auto& voice = voices[voiceIndex];
    
    voice.midiNote = midiNote;
    voice.velocity = velocity;
    voice.samplePosition = 0;
    voice.isActive = true;
    voice.envelope.noteOn();
    
    if (voice.chokeGroup >= 0)
    {
        handleChokeGroup(voice.chokeGroup, voiceIndex);
    }
}

void VoiceManager::stopVoice(int voiceIndex, int sampleOffset)
{
    if (voiceIndex < 0 || voiceIndex >= voices.size())
        return;
    
    auto& voice = voices[voiceIndex];
    voice.envelope.noteOff();
}

void VoiceManager::handleChokeGroup(int chokeGroup, int excludeVoice)
{
    if (chokeGroup < 0 || chokeGroup >= MAX_CHOKE_GROUPS)
        return;
    
    for (int i = 0; i < voices.size(); ++i)
    {
        if (i != excludeVoice && voices[i].isActive && voices[i].chokeGroup == chokeGroup)
        {
            voices[i].envelope.noteOff();
        }
    }
    
    chokeGroupLastVoice[chokeGroup] = excludeVoice;
}

int VoiceManager::getNextRoundRobinIndex(int midiNote)
{
    if (!useRoundRobin.load())
        return 0;
    
    const int index = roundRobinCounters[midiNote]++;
    return index;
}

} // namespace flam