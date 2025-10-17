#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../DSP/TenBandGraphicEQ.h"
#include "../DSP/SaturationProcessor.h"
#include "../DSP/CompressorProcessor.h"
#include "../DSP/LimiterProcessor.h"
#include <vector>
#include <atomic>
#include <memory>

namespace flam {

/**
 * @brief Multi-channel mixer with flexible multi-output routing
 *
 * Each microphone channel can be routed to:
 * - Main Mix (internal stereo mixer with volume/pan/effects)
 * - Individual DAW output buses (direct routing, bypass internal mixer)
 *
 * Provides integrated mixing, FX processing, and metering.
 * All audio processing is real-time safe and allocation-free.
 *
 * Thread-safety:
 * - Parameter setters/getters are thread-safe (atomics)
 * - process() is audio-thread only
 * - setNumChannels() and prepareToPlay() must be called with processing suspended
 */
class Mixer
{
public:
    /**
     * @brief Output routing destinations
     */
    enum class OutputDestination
    {
        MainMix = 0,  // Internal mixer processing
        Bus1, Bus2, Bus3, Bus4, Bus5, Bus6, Bus7, Bus8,
        Bus9, Bus10, Bus11, Bus12, Bus13, Bus14, Bus15, Bus16
    };

    Mixer();
    ~Mixer() = default;

    // ========================================================================
    // Configuration (called when kit loads, processing must be suspended)

    /**
     * @brief Configure mixer for a specific number of channels
     * @param numChannels Number of microphone channels (1-16)
     * @param channelNames Names for each channel (from flamkit.yaml)
     *
     * Must be called with processing suspended. Clears existing configuration.
     * Call prepareToPlay() after this to allocate FX buffers.
     */
    void setNumChannels(int numChannels, const std::vector<juce::String>& channelNames);

    /**
     * @brief Get current number of channels
     * @return Channel count
     */
    int getNumChannels() const { return static_cast<int>(channels.size()); }

    /**
     * @brief Prepare mixer for audio processing
     * @param sampleRate Sample rate in Hz
     * @param maximumBlockSize Maximum expected block size
     *
     * Must be called with processing suspended. Allocates FX buffers.
     */
    void prepareToPlay(double sampleRate, int maximumBlockSize);

    // ========================================================================
    // Output routing (thread-safe)

    /**
     * @brief Set output destination for a channel
     * @param channelIndex Channel index (0 to N-1)
     * @param output Destination (MainMix or Bus1-16)
     */
    void setChannelOutput(int channelIndex, OutputDestination output);

    /**
     * @brief Get output destination for a channel
     * @param channelIndex Channel index
     * @return Current output destination
     */
    OutputDestination getChannelOutput(int channelIndex) const;

    // ========================================================================
    // Per-channel controls (thread-safe, only apply when output == MainMix)

    void setChannelVolume(int channelIndex, float volumeDb);
    float getChannelVolume(int channelIndex) const;

    void setChannelPan(int channelIndex, float pan);  // -1.0 (left) to +1.0 (right)
    float getChannelPan(int channelIndex) const;

    void setChannelSolo(int channelIndex, bool solo);
    bool isChannelSolo(int channelIndex) const;

    void setChannelMute(int channelIndex, bool mute);
    bool isChannelMute(int channelIndex) const;

    // ========================================================================
    // Per-channel FX controls (thread-safe, only apply when output == MainMix)

    // EQ
    void setChannelEQEnabled(int channelIndex, bool enabled);
    bool isChannelEQEnabled(int channelIndex) const;

    void setChannelEQBandGain(int channelIndex, int bandIndex, float gainDb);
    float getChannelEQBandGain(int channelIndex, int bandIndex) const;

    // Saturation
    void setChannelSaturationEnabled(int channelIndex, bool enabled);
    bool isChannelSaturationEnabled(int channelIndex) const;

    void setChannelSaturationAmount(int channelIndex, float amount);  // 0.0 to 1.0
    float getChannelSaturationAmount(int channelIndex) const;

    void setChannelSaturationMode(int channelIndex, int mode);  // 0=Tape, 1=Tube, 2=Digital
    int getChannelSaturationMode(int channelIndex) const;

    // Compressor
    void setChannelCompressorEnabled(int channelIndex, bool enabled);
    bool isChannelCompressorEnabled(int channelIndex) const;

    void setChannelCompressorThreshold(int channelIndex, float thresholdDb);
    float getChannelCompressorThreshold(int channelIndex) const;

    void setChannelCompressorRatio(int channelIndex, float ratio);
    float getChannelCompressorRatio(int channelIndex) const;

    void setChannelCompressorAttack(int channelIndex, float attackMs);
    float getChannelCompressorAttack(int channelIndex) const;

    void setChannelCompressorRelease(int channelIndex, float releaseMs);
    float getChannelCompressorRelease(int channelIndex) const;

    void setChannelCompressorMakeupGain(int channelIndex, float gainDb);
    float getChannelCompressorMakeupGain(int channelIndex) const;

    // ========================================================================
    // Master controls (thread-safe, for Main Mix)

    void setMasterVolume(float volumeDb);
    float getMasterVolume() const { return masterVolumeDb.load(); }

    // Master FX controls
    void setMasterEQEnabled(bool enabled);
    bool isMasterEQEnabled() const;

    void setMasterEQBandGain(int bandIndex, float gainDb);
    float getMasterEQBandGain(int bandIndex) const;

    void setMasterSaturationEnabled(bool enabled);
    bool isMasterSaturationEnabled() const;

    void setMasterSaturationAmount(float amount);
    float getMasterSaturationAmount() const;

    void setMasterSaturationMode(int mode);
    int getMasterSaturationMode() const;

    void setMasterCompressorEnabled(bool enabled);
    bool isMasterCompressorEnabled() const;

    void setMasterCompressorThreshold(float thresholdDb);
    float getMasterCompressorThreshold() const;

    void setMasterCompressorRatio(float ratio);
    float getMasterCompressorRatio() const;

    void setMasterCompressorAttack(float attackMs);
    float getMasterCompressorAttack() const;

    void setMasterCompressorRelease(float releaseMs);
    float getMasterCompressorRelease() const;

    void setMasterCompressorMakeupGain(float gainDb);
    float getMasterCompressorMakeupGain() const;

    void setMasterLimiterEnabled(bool enabled);
    bool isMasterLimiterEnabled() const;

    void setMasterLimiterThreshold(float thresholdDb);
    float getMasterLimiterThreshold() const;

    void setMasterLimiterRelease(float releaseMs);
    float getMasterLimiterRelease() const;

    // ========================================================================
    // Audio processing (audio thread only)

    /**
     * @brief Process audio through mixer
     * @param multiChannelInput Input buffer with all mic channels
     * @param allOutputBuses Output buffer (Bus 0 = Main Mix stereo, Bus 1-16 = mono buses)
     * @param numSamples Number of samples to process
     *
     * Real-time safe, allocation-free. Uses pre-allocated buffers from prepareToPlay().
     */
    void process(
        const juce::AudioBuffer<float>& multiChannelInput,
        juce::AudioBuffer<float>& allOutputBuses,
        int numSamples
    );

    // ========================================================================
    // Metering (thread-safe read, audio thread writes)

    /**
     * @brief Get peak level for a channel
     * @param channelIndex Channel index
     * @return Peak level (0.0 to 1.0+, where 1.0 = 0 dBFS)
     */
    float getChannelPeakLevel(int channelIndex) const;

    /**
     * @brief Get master peak level
     * @return Peak level (0.0 to 1.0+)
     */
    float getMasterPeakLevel() const { return masterPeakLevel.load(); }

    /**
     * @brief Reset all clip indicators
     */
    void resetClipIndicators();

    // ========================================================================
    // Bus info (for DAW integration)

    /**
     * @brief Get number of output buses required
     * @return 1 (Main Mix stereo) + number of channels (for individual routing)
     */
    int getNumRequiredOutputBuses() const;

    /**
     * @brief Get bus name for display in DAW
     * @param busIndex Bus index (0 = Main Mix, 1+ = individual channels)
     * @return Bus name
     */
    juce::String getBusName(int busIndex) const;

    // ========================================================================
    // State persistence

    /**
     * @brief Get current state as ValueTree
     * @return ValueTree containing all mixer state
     */
    juce::ValueTree getState() const;

    /**
     * @brief Restore state from ValueTree
     * @param state State to restore
     */
    void setState(const juce::ValueTree& state);

private:
    // ========================================================================
    // Internal structures

    struct ChannelStrip
    {
        juce::String name;

        // Routing
        std::atomic<int> outputDestination{0};  // 0 = MainMix, 1-16 = Bus 1-16

        // Mixer controls (only active if outputDestination == 0)
        std::atomic<float> volumeDb{0.0f};
        std::atomic<float> volumeGain{1.0f};  // Linear gain (computed from dB)
        std::atomic<float> pan{0.0f};
        std::atomic<bool> solo{false};
        std::atomic<bool> mute{false};

        // FX Chain (only active if outputDestination == 0)
        TenBandGraphicEQ eq;
        SaturationProcessor saturation;
        CompressorProcessor compressor;

        // Metering
        std::atomic<float> peakLevel{0.0f};
        std::atomic<bool> clipped{false};
    };

    std::vector<std::unique_ptr<ChannelStrip>> channels;

    // Master section (Main Mix only)
    std::atomic<float> masterVolumeDb{0.0f};
    std::atomic<float> masterVolumeGain{1.0f};
    std::atomic<float> masterPeakLevel{0.0f};
    std::atomic<bool> masterClipped{false};

    // Master FX Chain (Main Mix only)
    TenBandGraphicEQ masterEQ;
    SaturationProcessor masterSaturation;
    CompressorProcessor masterCompressor;
    LimiterProcessor masterLimiter;

    // Pre-allocated buffers for zero-allocation audio processing
    std::vector<juce::AudioBuffer<float>> channelFXBuffers;  // One mono buffer per channel
    juce::AudioBuffer<float> masterFXBuffer;  // Stereo buffer for master processing

    double currentSampleRate{44100.0};
    int maxBlockSize{512};

    // ========================================================================
    // Helper functions

    float dbToGain(float db) const;
    float gainToDb(float gain) const;
    void updateVolumeGain(int channelIndex);

    void processChannelToMainMix(
        size_t chIdx,
        ChannelStrip& channel,
        const juce::AudioBuffer<float>& multiChannelInput,
        juce::AudioBuffer<float>& allOutputBuses,
        int numSamples,
        bool anySoloed
    );

    void routeChannelToBus(
        size_t chIdx,
        int busIndex,
        const juce::AudioBuffer<float>& multiChannelInput,
        juce::AudioBuffer<float>& allOutputBuses,
        int numSamples
    );

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Mixer)
};

} // namespace flam
