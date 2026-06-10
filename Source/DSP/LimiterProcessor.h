// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

namespace flam {

/**
 * @brief Brick-wall limiter with instant attack
 *
 * Prevents signal from exceeding threshold (typically -0.1 dBFS).
 * Uses instant attack and configurable release for transparent limiting.
 * Essentially a compressor with infinite ratio and zero attack time.
 *
 * Thread-safe: Parameters can be updated from UI thread while processing audio.
 * Audio processing is allocation-free and real-time safe.
 *
 * Note: Envelope state is NOT atomic (audio thread only).
 */
class LimiterProcessor
{
public:
    LimiterProcessor();
    ~LimiterProcessor() = default;

    /**
     * @brief Prepare the limiter for audio processing
     * @param sampleRate Sample rate in Hz
     *
     * Call this before processing audio. Initializes envelope state.
     */
    void prepareToPlay(double sampleRate);

    /**
     * @brief Process audio through the limiter
     * @param buffer Audio buffer to process (modified in-place)
     * @param numSamples Number of samples to process
     *
     * Real-time safe, allocation-free. Early returns if bypassed.
     */
    void process(juce::AudioBuffer<float>& buffer, int numSamples);

    /**
     * @brief Enable or bypass the limiter
     * @param shouldBeEnabled True to enable, false to bypass
     *
     * Thread-safe: can be called from UI thread.
     */
    void setEnabled(bool shouldBeEnabled) { enabled.store(shouldBeEnabled); }

    /**
     * @brief Check if limiter is enabled
     * @return True if enabled, false if bypassed
     */
    bool isEnabled() const { return enabled.load(); }

    /**
     * @brief Set limiting threshold
     * @param thresholdDb Threshold in decibels (-1.0 to 0.0 dB)
     *
     * Typical values: -0.1 dB (safe headroom) to -0.3 dB (conservative)
     * Thread-safe: can be called from UI thread.
     */
    void setThreshold(float thresholdDb);

    /**
     * @brief Get current threshold
     * @return Threshold in decibels
     */
    float getThreshold() const { return thresholdDb.load(); }

    /**
     * @brief Set release time
     * @param releaseMs Release time in milliseconds (10 to 500 ms)
     *
     * Thread-safe: can be called from UI thread.
     */
    void setRelease(float releaseMs);

    /**
     * @brief Get current release time
     * @return Release time in milliseconds
     */
    float getRelease() const { return releaseMs.load(); }

    /**
     * @brief Reset envelope state
     */
    void reset();

private:
    std::atomic<bool> enabled{false};
    std::atomic<float> thresholdDb{-0.1f};  // Just below 0 dBFS
    std::atomic<float> releaseMs{50.0f};    // Fast release

    // Envelope state (audio thread only - not atomic!)
    float envelope{0.0f};
    double sampleRate{44100.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LimiterProcessor)
};

} // namespace flam
