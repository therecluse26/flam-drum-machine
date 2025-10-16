#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

namespace flam {

/**
 * @brief Multi-mode saturation/distortion processor
 *
 * Provides three saturation modes:
 * - Tape: Soft clipping with asymmetric curve (warm, even-order harmonics)
 * - Tube: Odd-harmonic saturation with pre-gain boost
 * - Digital: Hard clipping with subtle overshoot
 *
 * Thread-safe: Parameters can be updated from UI thread while processing audio.
 * Audio processing is allocation-free and real-time safe.
 */
class SaturationProcessor
{
public:
    enum Mode
    {
        Tape = 0,
        Tube = 1,
        Digital = 2
    };

    SaturationProcessor();
    ~SaturationProcessor() = default;

    /**
     * @brief Prepare the processor for audio processing
     * @param sampleRate Sample rate in Hz (unused, but kept for API consistency)
     *
     * Call this before processing audio. Saturation has no state to initialize.
     */
    void prepareToPlay(double sampleRate) { /* No state to initialize */ }

    /**
     * @brief Process audio through the saturation
     * @param buffer Audio buffer to process (modified in-place)
     * @param numSamples Number of samples to process
     *
     * Real-time safe, allocation-free. Early returns if bypassed.
     */
    void process(juce::AudioBuffer<float>& buffer, int numSamples);

    /**
     * @brief Enable or bypass the saturation
     * @param shouldBeEnabled True to enable, false to bypass
     *
     * Thread-safe: can be called from UI thread.
     */
    void setEnabled(bool shouldBeEnabled) { enabled.store(shouldBeEnabled); }

    /**
     * @brief Check if saturation is enabled
     * @return True if enabled, false if bypassed
     */
    bool isEnabled() const { return enabled.load(); }

    /**
     * @brief Set saturation amount (wet/dry mix)
     * @param newAmount Amount from 0.0 (dry) to 1.0 (fully saturated)
     *
     * Thread-safe: can be called from UI thread.
     */
    void setAmount(float newAmount);

    /**
     * @brief Get current saturation amount
     * @return Amount from 0.0 to 1.0
     */
    float getAmount() const { return amount.load(); }

    /**
     * @brief Set saturation mode
     * @param newMode Mode (Tape, Tube, or Digital)
     *
     * Thread-safe: can be called from UI thread.
     */
    void setMode(Mode newMode) { mode.store(static_cast<int>(newMode)); }

    /**
     * @brief Get current saturation mode
     * @return Current mode
     */
    Mode getMode() const { return static_cast<Mode>(mode.load()); }

private:
    std::atomic<bool> enabled{false};
    std::atomic<float> amount{0.5f};  // 0.0 to 1.0
    std::atomic<int> mode{Tape};

    /**
     * @brief Apply tape-style saturation (soft clipping)
     * @param input Input sample
     * @param amt Saturation amount
     * @return Saturated sample
     */
    inline float processTape(float input, float amt) const;

    /**
     * @brief Apply tube-style saturation (odd harmonics)
     * @param input Input sample
     * @param amt Saturation amount
     * @return Saturated sample
     */
    inline float processTube(float input, float amt) const;

    /**
     * @brief Apply digital-style saturation (hard clipping)
     * @param input Input sample
     * @param amt Saturation amount
     * @return Saturated sample
     */
    inline float processDigital(float input, float amt) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SaturationProcessor)
};

} // namespace flam
