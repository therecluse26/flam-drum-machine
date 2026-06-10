// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>

namespace flam {

/**
 * @brief A simple audio level meter component with peak and RMS display
 *
 * Thread-safe level meter that can be updated from the audio thread
 * and displayed on the UI thread with automatic decay.
 */
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter()
    {
        startTimerHz(30); // 30Hz UI refresh rate
    }

    ~LevelMeter() override
    {
        stopTimer();
    }

    /**
     * @brief Set the current level (call from audio thread)
     * @param newLevel Linear level (0.0 to 1.0+)
     */
    void setLevel(float newLevel)
    {
        currentLevel.store(newLevel, std::memory_order_relaxed);
    }

    /**
     * @brief Reset the meter
     */
    void reset()
    {
        currentLevel.store(0.0f, std::memory_order_relaxed);
        displayLevel = 0.0f;
        peakLevel = 0.0f;
        peakHoldCounter = 0;
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Background
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillRoundedRectangle(bounds, 2.0f);

        // Border
        g.setColour(juce::Colours::darkgrey);
        g.drawRoundedRectangle(bounds, 2.0f, 1.0f);

        if (displayLevel <= 0.0f)
            return;

        // Convert to dB for display
        const float displayDB = juce::Decibels::gainToDecibels(displayLevel);
        const float normalizedLevel = juce::jmap(displayDB, -60.0f, 6.0f, 0.0f, 1.0f);

        auto meterBounds = bounds.reduced(2.0f);
        const float meterHeight = meterBounds.getHeight() * juce::jlimit(0.0f, 1.0f, normalizedLevel);

        // Draw level bar with gradient
        juce::ColourGradient gradient(
            juce::Colours::green, meterBounds.getX(), meterBounds.getBottom(),
            juce::Colours::red, meterBounds.getX(), meterBounds.getY(),
            false);
        gradient.addColour(0.3, juce::Colours::yellow);
        g.setGradientFill(gradient);

        auto levelRect = meterBounds.removeFromBottom(meterHeight);
        g.fillRect(levelRect);

        // Draw peak hold line
        if (peakLevel > 0.0f)
        {
            const float peakDB = juce::Decibels::gainToDecibels(peakLevel);
            const float normalizedPeak = juce::jmap(peakDB, -60.0f, 6.0f, 0.0f, 1.0f);
            const float peakY = meterBounds.getY() + meterBounds.getHeight() * (1.0f - normalizedPeak);

            g.setColour(juce::Colours::white);
            g.drawHorizontalLine(static_cast<int>(peakY), meterBounds.getX(), meterBounds.getRight());
        }

        // Draw dB scale markers
        g.setColour(juce::Colours::grey.withAlpha(0.5f));
        g.setFont(8.0f);

        const std::array<float, 5> dbMarkers = {0.0f, -6.0f, -12.0f, -24.0f, -48.0f};
        for (float db : dbMarkers)
        {
            const float normalized = juce::jmap(db, -60.0f, 6.0f, 0.0f, 1.0f);
            const float y = bounds.getY() + bounds.getHeight() * (1.0f - normalized);
            g.drawText(juce::String(static_cast<int>(db)),
                      bounds.getRight() + 2, static_cast<int>(y) - 6,
                      20, 12, juce::Justification::left);
        }
    }

    void resized() override
    {
        repaint();
    }

private:
    void timerCallback() override
    {
        // Read current level from audio thread
        const float newLevel = currentLevel.load(std::memory_order_relaxed);

        // Decay coefficient: fast attack, slower decay
        constexpr float decayRate = 0.95f;

        if (newLevel > displayLevel)
        {
            // Fast attack
            displayLevel = newLevel;
        }
        else
        {
            // Slow decay
            displayLevel *= decayRate;
        }

        // Update peak hold
        if (newLevel > peakLevel)
        {
            peakLevel = newLevel;
            peakHoldCounter = peakHoldFrames;
        }
        else if (peakHoldCounter > 0)
        {
            --peakHoldCounter;
        }
        else
        {
            // Decay peak
            peakLevel *= 0.99f;
            if (peakLevel < 0.001f)
                peakLevel = 0.0f;
        }

        repaint();
    }

    std::atomic<float> currentLevel{0.0f};  // Updated from audio thread
    float displayLevel{0.0f};                // UI thread only
    float peakLevel{0.0f};                   // UI thread only
    int peakHoldCounter{0};                  // UI thread only

    static constexpr int peakHoldFrames = 60; // Hold for ~2 seconds at 30Hz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

} // namespace flam
