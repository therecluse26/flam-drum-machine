#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace flam {

/**
 * @brief 10-Band Graphic EQ with ISO-standard frequency bands
 *
 * Provides fixed-frequency peaking filters at standard 1/3-octave intervals.
 * Each band can be adjusted from -12 dB to +12 dB.
 *
 * Thread-safe: Parameters can be updated from UI thread while processing audio.
 * Audio processing is allocation-free and real-time safe.
 */
class TenBandGraphicEQ
{
public:
    static constexpr int NUM_BANDS = 10;

    // ISO standard 1/3-octave frequencies
    static constexpr float BAND_FREQUENCIES[NUM_BANDS] = {
        31.25f, 62.5f, 125.0f, 250.0f, 500.0f,
        1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    TenBandGraphicEQ();
    ~TenBandGraphicEQ() = default;

    /**
     * @brief Prepare the EQ for audio processing
     * @param sampleRate Sample rate in Hz
     * @param maximumBlockSize Maximum expected block size
     *
     * Call this before processing audio. Not real-time safe.
     */
    void prepareToPlay(double sampleRate, int maximumBlockSize);

    /**
     * @brief Process audio through the EQ
     * @param buffer Audio buffer to process (modified in-place)
     * @param numSamples Number of samples to process
     *
     * Real-time safe, allocation-free. Early returns if bypassed.
     */
    void process(juce::AudioBuffer<float>& buffer, int numSamples);

    /**
     * @brief Enable or bypass the EQ
     * @param shouldBeEnabled True to enable, false to bypass
     *
     * Thread-safe: can be called from UI thread.
     */
    void setEnabled(bool shouldBeEnabled) { enabled.store(shouldBeEnabled); }

    /**
     * @brief Check if EQ is enabled
     * @return True if enabled, false if bypassed
     */
    bool isEnabled() const { return enabled.load(); }

    /**
     * @brief Set gain for a specific frequency band
     * @param bandIndex Band index (0-9)
     * @param gainDb Gain in decibels (-12 to +12 dB)
     *
     * Thread-safe: can be called from UI thread.
     * Filter coefficients are updated on next audio callback.
     */
    void setBandGain(int bandIndex, float gainDb);

    /**
     * @brief Get current gain for a frequency band
     * @param bandIndex Band index (0-9)
     * @return Gain in decibels
     */
    float getBandGain(int bandIndex) const;

    /**
     * @brief Reset all bands to 0 dB (flat response)
     */
    void reset();

private:
    std::atomic<bool> enabled{false};
    std::array<std::atomic<float>, NUM_BANDS> bandGains;  // dB values

    // DSP filter state (audio thread only)
    std::array<juce::dsp::IIR::Filter<float>, NUM_BANDS> filters;
    double sampleRate{44100.0};

    // Flag to trigger coefficient update on next audio callback
    std::atomic<bool> coefficientsNeedUpdate{true};

    /**
     * @brief Update filter coefficients based on current band gains
     *
     * Called from audio thread when parameters change.
     * Uses peaking filters with Butterworth Q for smooth response.
     */
    void updateCoefficients();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TenBandGraphicEQ)
};

} // namespace flam
