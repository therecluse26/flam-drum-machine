#include "PerChannelMixer.h"
#include <cmath>

namespace flam {

PerChannelMixer::PerChannelMixer()
{
    // Initialize with sensible defaults
    masterVolumeDb.store(0.0f);
    masterVolumeGain.store(1.0f);
}

// ============================================================================
// Configuration

void PerChannelMixer::setNumChannels(int numChannels, const std::vector<juce::String>& channelNames)
{
    channels.clear();
    channels.reserve(static_cast<size_t>(numChannels));

    for (int i = 0; i < numChannels; ++i)
    {
        auto strip = std::make_unique<ChannelStrip>();
        strip->name = (i < static_cast<int>(channelNames.size())) ? channelNames[i] : "Channel " + juce::String(i + 1);
        channels.push_back(std::move(strip));
    }
}

void PerChannelMixer::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    currentSampleRate = sampleRate;
    maxBlockSize = maximumBlockSize;

    // Prepare master FX
    masterEQ.prepareToPlay(sampleRate, maximumBlockSize);
    masterSaturation.prepareToPlay(sampleRate);
    masterCompressor.prepareToPlay(sampleRate);
    masterLimiter.prepareToPlay(sampleRate);

    // Allocate master FX buffer (stereo)
    masterFXBuffer.setSize(2, maximumBlockSize, false, false, true);

    // Prepare channel FX and allocate per-channel buffers
    channelFXBuffers.clear();
    channelFXBuffers.reserve(channels.size());

    for (auto& channel : channels)
    {
        channel->eq.prepareToPlay(sampleRate, maximumBlockSize);
        channel->saturation.prepareToPlay(sampleRate);
        channel->compressor.prepareToPlay(sampleRate);

        // Allocate mono buffer for this channel
        juce::AudioBuffer<float> buffer(1, maximumBlockSize);
        channelFXBuffers.push_back(std::move(buffer));
    }
}

// ============================================================================
// Output routing

void PerChannelMixer::setChannelOutput(int channelIndex, OutputDestination output)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->outputDestination.store(static_cast<int>(output));
}

PerChannelMixer::OutputDestination PerChannelMixer::getChannelOutput(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return OutputDestination::MainMix;

    return static_cast<OutputDestination>(channels[channelIndex]->outputDestination.load());
}

// ============================================================================
// Per-channel controls

void PerChannelMixer::setChannelVolume(int channelIndex, float volumeDb)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    volumeDb = juce::jlimit(-96.0f, 6.0f, volumeDb);
    channels[channelIndex]->volumeDb.store(volumeDb);
    channels[channelIndex]->volumeGain.store(dbToGain(volumeDb));
}

float PerChannelMixer::getChannelVolume(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return 0.0f;

    return channels[channelIndex]->volumeDb.load();
}

void PerChannelMixer::setChannelPan(int channelIndex, float pan)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    pan = juce::jlimit(-1.0f, 1.0f, pan);
    channels[channelIndex]->pan.store(pan);
}

float PerChannelMixer::getChannelPan(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return 0.0f;

    return channels[channelIndex]->pan.load();
}

void PerChannelMixer::setChannelSolo(int channelIndex, bool solo)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->solo.store(solo);
}

bool PerChannelMixer::isChannelSolo(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return false;

    return channels[channelIndex]->solo.load();
}

void PerChannelMixer::setChannelMute(int channelIndex, bool mute)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->mute.store(mute);
}

bool PerChannelMixer::isChannelMute(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return false;

    return channels[channelIndex]->mute.load();
}

// ============================================================================
// Per-channel EQ controls

void PerChannelMixer::setChannelEQEnabled(int channelIndex, bool enabled)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->eq.setEnabled(enabled);
}

bool PerChannelMixer::isChannelEQEnabled(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return false;

    return channels[channelIndex]->eq.isEnabled();
}

void PerChannelMixer::setChannelEQBandGain(int channelIndex, int bandIndex, float gainDb)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->eq.setBandGain(bandIndex, gainDb);
}

float PerChannelMixer::getChannelEQBandGain(int channelIndex, int bandIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return 0.0f;

    return channels[channelIndex]->eq.getBandGain(bandIndex);
}

// ============================================================================
// Per-channel saturation controls

void PerChannelMixer::setChannelSaturationEnabled(int channelIndex, bool enabled)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->saturation.setEnabled(enabled);
}

bool PerChannelMixer::isChannelSaturationEnabled(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return false;

    return channels[channelIndex]->saturation.isEnabled();
}

void PerChannelMixer::setChannelSaturationAmount(int channelIndex, float amount)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->saturation.setAmount(amount);
}

float PerChannelMixer::getChannelSaturationAmount(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return 0.0f;

    return channels[channelIndex]->saturation.getAmount();
}

void PerChannelMixer::setChannelSaturationMode(int channelIndex, int mode)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->saturation.setMode(static_cast<SaturationProcessor::Mode>(mode));
}

int PerChannelMixer::getChannelSaturationMode(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return 0;

    return static_cast<int>(channels[channelIndex]->saturation.getMode());
}

// ============================================================================
// Per-channel compressor controls

void PerChannelMixer::setChannelCompressorEnabled(int channelIndex, bool enabled)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->compressor.setEnabled(enabled);
}

bool PerChannelMixer::isChannelCompressorEnabled(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return false;

    return channels[channelIndex]->compressor.isEnabled();
}

void PerChannelMixer::setChannelCompressorThreshold(int channelIndex, float thresholdDb)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->compressor.setThreshold(thresholdDb);
}

float PerChannelMixer::getChannelCompressorThreshold(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return -10.0f;

    return channels[channelIndex]->compressor.getThreshold();
}

void PerChannelMixer::setChannelCompressorRatio(int channelIndex, float ratio)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->compressor.setRatio(ratio);
}

float PerChannelMixer::getChannelCompressorRatio(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return 4.0f;

    return channels[channelIndex]->compressor.getRatio();
}

void PerChannelMixer::setChannelCompressorAttack(int channelIndex, float attackMs)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->compressor.setAttack(attackMs);
}

float PerChannelMixer::getChannelCompressorAttack(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return 5.0f;

    return channels[channelIndex]->compressor.getAttack();
}

void PerChannelMixer::setChannelCompressorRelease(int channelIndex, float releaseMs)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->compressor.setRelease(releaseMs);
}

float PerChannelMixer::getChannelCompressorRelease(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return 100.0f;

    return channels[channelIndex]->compressor.getRelease();
}

void PerChannelMixer::setChannelCompressorMakeupGain(int channelIndex, float gainDb)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    channels[channelIndex]->compressor.setMakeupGain(gainDb);
}

float PerChannelMixer::getChannelCompressorMakeupGain(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return 0.0f;

    return channels[channelIndex]->compressor.getMakeupGain();
}

// ============================================================================
// Master controls

void PerChannelMixer::setMasterVolume(float volumeDb)
{
    volumeDb = juce::jlimit(-96.0f, 6.0f, volumeDb);
    masterVolumeDb.store(volumeDb);
    masterVolumeGain.store(dbToGain(volumeDb));
}

// Master EQ
void PerChannelMixer::setMasterEQEnabled(bool enabled)
{
    masterEQ.setEnabled(enabled);
}

bool PerChannelMixer::isMasterEQEnabled() const
{
    return masterEQ.isEnabled();
}

void PerChannelMixer::setMasterEQBandGain(int bandIndex, float gainDb)
{
    masterEQ.setBandGain(bandIndex, gainDb);
}

float PerChannelMixer::getMasterEQBandGain(int bandIndex) const
{
    return masterEQ.getBandGain(bandIndex);
}

// Master Saturation
void PerChannelMixer::setMasterSaturationEnabled(bool enabled)
{
    masterSaturation.setEnabled(enabled);
}

bool PerChannelMixer::isMasterSaturationEnabled() const
{
    return masterSaturation.isEnabled();
}

void PerChannelMixer::setMasterSaturationAmount(float amount)
{
    masterSaturation.setAmount(amount);
}

float PerChannelMixer::getMasterSaturationAmount() const
{
    return masterSaturation.getAmount();
}

void PerChannelMixer::setMasterSaturationMode(int mode)
{
    masterSaturation.setMode(static_cast<SaturationProcessor::Mode>(mode));
}

int PerChannelMixer::getMasterSaturationMode() const
{
    return static_cast<int>(masterSaturation.getMode());
}

// Master Compressor
void PerChannelMixer::setMasterCompressorEnabled(bool enabled)
{
    masterCompressor.setEnabled(enabled);
}

bool PerChannelMixer::isMasterCompressorEnabled() const
{
    return masterCompressor.isEnabled();
}

void PerChannelMixer::setMasterCompressorThreshold(float thresholdDb)
{
    masterCompressor.setThreshold(thresholdDb);
}

float PerChannelMixer::getMasterCompressorThreshold() const
{
    return masterCompressor.getThreshold();
}

void PerChannelMixer::setMasterCompressorRatio(float ratio)
{
    masterCompressor.setRatio(ratio);
}

float PerChannelMixer::getMasterCompressorRatio() const
{
    return masterCompressor.getRatio();
}

void PerChannelMixer::setMasterCompressorAttack(float attackMs)
{
    masterCompressor.setAttack(attackMs);
}

float PerChannelMixer::getMasterCompressorAttack() const
{
    return masterCompressor.getAttack();
}

void PerChannelMixer::setMasterCompressorRelease(float releaseMs)
{
    masterCompressor.setRelease(releaseMs);
}

float PerChannelMixer::getMasterCompressorRelease() const
{
    return masterCompressor.getRelease();
}

void PerChannelMixer::setMasterCompressorMakeupGain(float gainDb)
{
    masterCompressor.setMakeupGain(gainDb);
}

float PerChannelMixer::getMasterCompressorMakeupGain() const
{
    return masterCompressor.getMakeupGain();
}

// Master Limiter
void PerChannelMixer::setMasterLimiterEnabled(bool enabled)
{
    masterLimiter.setEnabled(enabled);
}

bool PerChannelMixer::isMasterLimiterEnabled() const
{
    return masterLimiter.isEnabled();
}

void PerChannelMixer::setMasterLimiterThreshold(float thresholdDb)
{
    masterLimiter.setThreshold(thresholdDb);
}

float PerChannelMixer::getMasterLimiterThreshold() const
{
    return masterLimiter.getThreshold();
}

void PerChannelMixer::setMasterLimiterRelease(float releaseMs)
{
    masterLimiter.setRelease(releaseMs);
}

float PerChannelMixer::getMasterLimiterRelease() const
{
    return masterLimiter.getRelease();
}

// ============================================================================
// Audio processing

void PerChannelMixer::process(
    const juce::AudioBuffer<float>& multiChannelInput,
    juce::AudioBuffer<float>& allOutputBuses,
    int numSamples)
{
    // Clear all output buses
    allOutputBuses.clear(0, numSamples);

    // Check if any Main Mix channels are soloed
    bool anySoloed = false;
    for (const auto& channel : channels)
    {
        if (channel->outputDestination.load() == 0 && channel->solo.load())
        {
            anySoloed = true;
            break;
        }
    }

    // Process each channel
    for (size_t chIdx = 0; chIdx < channels.size(); ++chIdx)
    {
        if (chIdx >= static_cast<size_t>(multiChannelInput.getNumChannels()))
            break;

        auto& channel = *channels[chIdx];
        const int outputDest = channel.outputDestination.load();

        if (outputDest == 0)
        {
            // Route to Main Mix (bus 0) with internal processing
            processChannelToMainMix(chIdx, channel, multiChannelInput, allOutputBuses, numSamples, anySoloed);
        }
        else
        {
            // Route directly to DAW bus (bypass internal mixer)
            routeChannelToBus(chIdx, outputDest, multiChannelInput, allOutputBuses, numSamples);
        }
    }

    // Process Master FX chain on Main Mix
    // Use pre-allocated stereo buffer for master FX processing
    masterFXBuffer.clear(0, numSamples);

    // Copy Main Mix to master FX buffer
    if (allOutputBuses.getNumChannels() >= 2)
    {
        masterFXBuffer.copyFrom(0, 0, allOutputBuses, 0, 0, numSamples);  // L
        masterFXBuffer.copyFrom(1, 0, allOutputBuses, 1, 0, numSamples);  // R

        // Process Master FX chain in order: EQ → Saturation → Compressor → Limiter
        if (masterEQ.isEnabled())
            masterEQ.process(masterFXBuffer, numSamples);

        if (masterSaturation.isEnabled())
            masterSaturation.process(masterFXBuffer, numSamples);

        if (masterCompressor.isEnabled())
            masterCompressor.process(masterFXBuffer, numSamples);

        if (masterLimiter.isEnabled())
            masterLimiter.process(masterFXBuffer, numSamples);

        // Apply master volume
        const float masterGain = masterVolumeGain.load();
        masterFXBuffer.applyGain(0, numSamples, masterGain);

        // Copy processed master back to output
        allOutputBuses.copyFrom(0, 0, masterFXBuffer, 0, 0, numSamples);  // L
        allOutputBuses.copyFrom(1, 0, masterFXBuffer, 1, 0, numSamples);  // R

        // Update master peak meter with fast visual decay
        float masterPeakThisBlock = std::max(
            masterFXBuffer.getMagnitude(0, 0, numSamples),
            masterFXBuffer.getMagnitude(1, 0, numSamples)
        );

        // Fast decay: meter should drop noticeably after each block
        // At 44.1kHz with 128-sample blocks = ~345 blocks/sec
        // This decay rate gives smooth but responsive meter behavior
        float currentMasterPeak = masterPeakLevel.load();
        const float decayFactor = 0.93f;  // Drops to ~1% in 0.5 seconds
        float newMasterPeak = std::max(masterPeakThisBlock, currentMasterPeak * decayFactor);

        if (newMasterPeak > 1.0f)
            masterClipped.store(true);

        masterPeakLevel.store(newMasterPeak);
    }
}

void PerChannelMixer::processChannelToMainMix(
    size_t chIdx,
    ChannelStrip& channel,
    const juce::AudioBuffer<float>& multiChannelInput,
    juce::AudioBuffer<float>& allOutputBuses,
    int numSamples,
    bool anySoloed)
{
    // Determine if this channel should be audible
    bool audible = true;

    if (channel.mute.load())
        audible = false;

    if (anySoloed && !channel.solo.load())
        audible = false;

    if (!audible)
        return;

    // Use pre-allocated mono buffer for FX processing
    auto& channelBuffer = channelFXBuffers[chIdx];
    channelBuffer.clear(0, numSamples);
    channelBuffer.copyFrom(0, 0, multiChannelInput, static_cast<int>(chIdx), 0, numSamples);

    // Process FX chain in order: EQ → Saturation → Compressor
    if (channel.eq.isEnabled())
        channel.eq.process(channelBuffer, numSamples);

    if (channel.saturation.isEnabled())
        channel.saturation.process(channelBuffer, numSamples);

    if (channel.compressor.isEnabled())
        channel.compressor.process(channelBuffer, numSamples);

    // Get channel parameters
    const float gain = channel.volumeGain.load();
    const float pan = channel.pan.load();

    // Calculate stereo pan gains (constant power panning)
    const float panAngle = (pan + 1.0f) * juce::MathConstants<float>::pi / 4.0f;
    const float leftGain = std::cos(panAngle) * gain;
    const float rightGain = std::sin(panAngle) * gain;

    // Add channel to Main Mix (bus 0)
    if (allOutputBuses.getNumChannels() >= 2)
    {
        const float* processedData = channelBuffer.getReadPointer(0);
        float* leftOutput = allOutputBuses.getWritePointer(0);   // Main Mix L
        float* rightOutput = allOutputBuses.getWritePointer(1);  // Main Mix R

        float peakThisBlock = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float sample = processedData[i];
            leftOutput[i] += sample * leftGain;
            rightOutput[i] += sample * rightGain;

            peakThisBlock = std::max(peakThisBlock, std::abs(sample));
        }

        // Update peak meter with fast visual decay
        // Fast decay: meter should drop noticeably after each block
        float currentPeak = channel.peakLevel.load();
        const float decayFactor = 0.93f;  // Drops to ~1% in 0.5 seconds
        float newPeak = std::max(peakThisBlock, currentPeak * decayFactor);

        if (newPeak > 1.0f)
            channels[chIdx]->clipped.store(true);

        channels[chIdx]->peakLevel.store(newPeak);
    }
}

void PerChannelMixer::routeChannelToBus(
    size_t chIdx,
    int busIndex,
    const juce::AudioBuffer<float>& multiChannelInput,
    juce::AudioBuffer<float>& allOutputBuses,
    int numSamples)
{
    // Calculate output channel offset for this bus
    // Bus 0 = channels 0-1 (Main Mix stereo)
    // Bus 1 = channel 2 (mono)
    // Bus 2 = channel 3 (mono)
    // etc.
    const int outputChannelIndex = 2 + (busIndex - 1);

    if (outputChannelIndex >= allOutputBuses.getNumChannels())
        return;  // Safety check

    // Copy channel directly to output bus (no processing)
    const float* inputData = multiChannelInput.getReadPointer(static_cast<int>(chIdx));
    float* outputData = allOutputBuses.getWritePointer(outputChannelIndex);

    float peakThisBlock = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        outputData[i] = inputData[i];
        peakThisBlock = std::max(peakThisBlock, std::abs(inputData[i]));
    }

    // Update peak meter with fast visual decay
    float currentPeak = channels[chIdx]->peakLevel.load();
    const float decayFactor = 0.93f;  // Drops to ~1% in 0.5 seconds
    float newPeak = std::max(peakThisBlock, currentPeak * decayFactor);

    if (newPeak > 1.0f)
        channels[chIdx]->clipped.store(true);

    channels[chIdx]->peakLevel.store(newPeak);
}

// ============================================================================
// Metering

float PerChannelMixer::getChannelPeakLevel(int channelIndex) const
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return 0.0f;

    return channels[channelIndex]->peakLevel.load();
}

void PerChannelMixer::resetClipIndicators()
{
    for (auto& channel : channels)
    {
        channel->clipped.store(false);
    }

    masterClipped.store(false);
}

// ============================================================================
// Bus info

int PerChannelMixer::getNumRequiredOutputBuses() const
{
    // Main Mix (stereo) + one mono bus per channel
    return 1 + static_cast<int>(channels.size());
}

juce::String PerChannelMixer::getBusName(int busIndex) const
{
    if (busIndex == 0)
        return "Main Mix";

    const int channelIndex = busIndex - 1;
    if (channelIndex >= 0 && channelIndex < static_cast<int>(channels.size()))
        return channels[channelIndex]->name;

    return "Bus " + juce::String(busIndex);
}

// ============================================================================
// State persistence

juce::ValueTree PerChannelMixer::getState() const
{
    juce::ValueTree state("MixerState");

    state.setProperty("masterVolume", masterVolumeDb.load(), nullptr);
    state.setProperty("masterEQEnabled", masterEQ.isEnabled(), nullptr);

    // Master EQ bands
    for (int band = 0; band < TenBandGraphicEQ::NUM_BANDS; ++band)
    {
        juce::String bandProp = "masterEQBand" + juce::String(band);
        state.setProperty(bandProp, masterEQ.getBandGain(band), nullptr);
    }

    state.setProperty("masterSatEnabled", masterSaturation.isEnabled(), nullptr);
    state.setProperty("masterSatAmount", masterSaturation.getAmount(), nullptr);
    state.setProperty("masterSatMode", static_cast<int>(masterSaturation.getMode()), nullptr);

    state.setProperty("masterCompEnabled", masterCompressor.isEnabled(), nullptr);
    state.setProperty("masterCompThreshold", masterCompressor.getThreshold(), nullptr);
    state.setProperty("masterCompRatio", masterCompressor.getRatio(), nullptr);
    state.setProperty("masterCompAttack", masterCompressor.getAttack(), nullptr);
    state.setProperty("masterCompRelease", masterCompressor.getRelease(), nullptr);
    state.setProperty("masterCompMakeup", masterCompressor.getMakeupGain(), nullptr);

    state.setProperty("masterLimiterEnabled", masterLimiter.isEnabled(), nullptr);
    state.setProperty("masterLimiterThreshold", masterLimiter.getThreshold(), nullptr);
    state.setProperty("masterLimiterRelease", masterLimiter.getRelease(), nullptr);

    // Channel states
    for (size_t i = 0; i < channels.size(); ++i)
    {
        juce::ValueTree channelState("Channel");
        channelState.setProperty("index", static_cast<int>(i), nullptr);
        channelState.setProperty("name", channels[i]->name, nullptr);
        channelState.setProperty("outputDest", channels[i]->outputDestination.load(), nullptr);
        channelState.setProperty("volume", channels[i]->volumeDb.load(), nullptr);
        channelState.setProperty("pan", channels[i]->pan.load(), nullptr);
        channelState.setProperty("solo", channels[i]->solo.load(), nullptr);
        channelState.setProperty("mute", channels[i]->mute.load(), nullptr);

        // FX parameters
        channelState.setProperty("eqEnabled", channels[i]->eq.isEnabled(), nullptr);
        for (int band = 0; band < TenBandGraphicEQ::NUM_BANDS; ++band)
        {
            juce::String bandProp = "eqBand" + juce::String(band);
            channelState.setProperty(bandProp, channels[i]->eq.getBandGain(band), nullptr);
        }

        channelState.setProperty("satEnabled", channels[i]->saturation.isEnabled(), nullptr);
        channelState.setProperty("satAmount", channels[i]->saturation.getAmount(), nullptr);
        channelState.setProperty("satMode", static_cast<int>(channels[i]->saturation.getMode()), nullptr);

        channelState.setProperty("compEnabled", channels[i]->compressor.isEnabled(), nullptr);
        channelState.setProperty("compThreshold", channels[i]->compressor.getThreshold(), nullptr);
        channelState.setProperty("compRatio", channels[i]->compressor.getRatio(), nullptr);
        channelState.setProperty("compAttack", channels[i]->compressor.getAttack(), nullptr);
        channelState.setProperty("compRelease", channels[i]->compressor.getRelease(), nullptr);
        channelState.setProperty("compMakeup", channels[i]->compressor.getMakeupGain(), nullptr);

        state.appendChild(channelState, nullptr);
    }

    return state;
}

void PerChannelMixer::setState(const juce::ValueTree& state)
{
    if (!state.isValid() || state.getType() != juce::Identifier("MixerState"))
        return;

    // Restore master settings
    setMasterVolume(state.getProperty("masterVolume", 0.0f));
    setMasterEQEnabled(state.getProperty("masterEQEnabled", false));

    for (int band = 0; band < TenBandGraphicEQ::NUM_BANDS; ++band)
    {
        juce::String bandProp = "masterEQBand" + juce::String(band);
        setMasterEQBandGain(band, state.getProperty(bandProp, 0.0f));
    }

    setMasterSaturationEnabled(state.getProperty("masterSatEnabled", false));
    setMasterSaturationAmount(state.getProperty("masterSatAmount", 0.5f));
    setMasterSaturationMode(state.getProperty("masterSatMode", 0));

    setMasterCompressorEnabled(state.getProperty("masterCompEnabled", false));
    setMasterCompressorThreshold(state.getProperty("masterCompThreshold", -10.0f));
    setMasterCompressorRatio(state.getProperty("masterCompRatio", 4.0f));
    setMasterCompressorAttack(state.getProperty("masterCompAttack", 5.0f));
    setMasterCompressorRelease(state.getProperty("masterCompRelease", 100.0f));
    setMasterCompressorMakeupGain(state.getProperty("masterCompMakeup", 0.0f));

    setMasterLimiterEnabled(state.getProperty("masterLimiterEnabled", false));
    setMasterLimiterThreshold(state.getProperty("masterLimiterThreshold", -0.1f));
    setMasterLimiterRelease(state.getProperty("masterLimiterRelease", 50.0f));

    // Restore channel settings
    for (int i = 0; i < state.getNumChildren(); ++i)
    {
        juce::ValueTree channelState = state.getChild(i);
        if (!channelState.isValid() || channelState.getType() != juce::Identifier("Channel"))
            continue;

        int channelIndex = channelState.getProperty("index", -1);
        if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
            continue;

        setChannelOutput(channelIndex, static_cast<OutputDestination>(
            static_cast<int>(channelState.getProperty("outputDest", 0))));
        setChannelVolume(channelIndex, channelState.getProperty("volume", 0.0f));
        setChannelPan(channelIndex, channelState.getProperty("pan", 0.0f));
        setChannelSolo(channelIndex, channelState.getProperty("solo", false));
        setChannelMute(channelIndex, channelState.getProperty("mute", false));

        setChannelEQEnabled(channelIndex, channelState.getProperty("eqEnabled", false));
        for (int band = 0; band < TenBandGraphicEQ::NUM_BANDS; ++band)
        {
            juce::String bandProp = "eqBand" + juce::String(band);
            setChannelEQBandGain(channelIndex, band, channelState.getProperty(bandProp, 0.0f));
        }

        setChannelSaturationEnabled(channelIndex, channelState.getProperty("satEnabled", false));
        setChannelSaturationAmount(channelIndex, channelState.getProperty("satAmount", 0.5f));
        setChannelSaturationMode(channelIndex, channelState.getProperty("satMode", 0));

        setChannelCompressorEnabled(channelIndex, channelState.getProperty("compEnabled", false));
        setChannelCompressorThreshold(channelIndex, channelState.getProperty("compThreshold", -10.0f));
        setChannelCompressorRatio(channelIndex, channelState.getProperty("compRatio", 4.0f));
        setChannelCompressorAttack(channelIndex, channelState.getProperty("compAttack", 5.0f));
        setChannelCompressorRelease(channelIndex, channelState.getProperty("compRelease", 100.0f));
        setChannelCompressorMakeupGain(channelIndex, channelState.getProperty("compMakeup", 0.0f));
    }
}

// ============================================================================
// Helper functions

float PerChannelMixer::dbToGain(float db) const
{
    return std::pow(10.0f, db / 20.0f);
}

float PerChannelMixer::gainToDb(float gain) const
{
    return 20.0f * std::log10(gain);
}

void PerChannelMixer::updateVolumeGain(int channelIndex)
{
    if (channelIndex < 0 || channelIndex >= static_cast<int>(channels.size()))
        return;

    const float volumeDb = channels[channelIndex]->volumeDb.load();
    channels[channelIndex]->volumeGain.store(dbToGain(volumeDb));
}

} // namespace flam
