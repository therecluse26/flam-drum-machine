// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cmath>

namespace flam {

/**
 * @brief Dynamic range compressor with envelope follower
 *
 * Standard dynamics processor with configurable threshold, ratio, attack,
 * release, and makeup gain. Uses log-domain processing for accurate gain reduction.
 *
 * Thread-safe: Parameters can be updated from UI thread while processing audio.
 * Audio processing is allocation-free and real-time safe.
 *
 * Note: Envelope state is NOT atomic (audio thread only).
 */
class CompressorProcessor
{
public:
    CompressorProcessor();
    ~CompressorProcessor() = default;

    /**
     * @brief Prepare the compressor for audio processing
     * @param sampleRate Sample rate in Hz
     *
     * Call this before processing audio. Initializes envelope state.
     */
    void prepareToPlay(double sampleRate);

    /**
     * @brief Process audio through the compressor
     * @param buffer Audio buffer to process (modified in-place)
     * @param numSamples Number of samples to process
     *
     * Real-time safe, allocation-free. Early returns if bypassed.
     */
    void process(juce::AudioBuffer<float>& buffer, int numSamples);

    /**
     * @brief Enable or bypass the compressor
     * @param shouldBeEnabled True to enable, false to bypass
     *
     * Thread-safe: can be called from UI thread.
     */
    void setEnabled(bool shouldBeEnabled) { enabled.store(shouldBeEnabled); }

    /**
     * @brief Check if compressor is enabled
     * @return True if enabled, false if bypassed
     */
    bool isEnabled() const { return enabled.load(); }

    /**
     * @brief Set compression threshold
     * @param thresholdDb Threshold in decibels (-60 to 0 dB)
     *
     * Thread-safe: can be called from UI thread.
     */
    void setThreshold(float thresholdDb);

    /**
     * @brief Get current threshold
     * @return Threshold in decibels
     */
    float getThreshold() const { return thresholdDb.load(); }

    /**
     * @brief Set compression ratio
     * @param newRatio Ratio (1.0 to 20.0, where 1.0 = no compression)
     *
     * Thread-safe: can be called from UI thread.
     */
    void setRatio(float newRatio);

    /**
     * @brief Get current ratio
     * @return Compression ratio
     */
    float getRatio() const { return ratio.load(); }

    /**
     * @brief Set attack time
     * @param attackMs Attack time in milliseconds (0.1 to 100 ms)
     *
     * Thread-safe: can be called from UI thread.
     */
    void setAttack(float attackMs);

    /**
     * @brief Get current attack time
     * @return Attack time in milliseconds
     */
    float getAttack() const { return attackMs.load(); }

    /**
     * @brief Set release time
     * @param releaseMs Release time in milliseconds (10 to 1000 ms)
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
     * @brief Set makeup gain (post-compression gain boost)
     * @param gainDb Makeup gain in decibels (0 to 24 dB)
     *
     * Thread-safe: can be called from UI thread.
     */
    void setMakeupGain(float gainDb);

    /**
     * @brief Get current makeup gain
     * @return Makeup gain in decibels
     */
    float getMakeupGain() const { return makeupGainDb.load(); }

    /**
     * @brief Reset envelope state
     */
    void reset();

private:
    std::atomic<bool> enabled{false};
    std::atomic<float> thresholdDb{-10.0f};
    std::atomic<float> ratio{4.0f};
    std::atomic<float> attackMs{5.0f};
    std::atomic<float> releaseMs{100.0f};
    std::atomic<float> makeupGainDb{0.0f};

    // Envelope state (audio thread only - not atomic!)
    float envelope{0.0f};
    double sampleRate{44100.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompressorProcessor)
};

} // namespace flam
