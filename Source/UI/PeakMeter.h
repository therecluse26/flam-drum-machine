// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>

namespace flam {

/**
 * @brief Vertical peak meter with color gradient and clip indicator
 *
 * Visual peak meter component that reads levels from the mixer and displays
 * them with smooth decay and color-coded zones (green → yellow → red).
 */
class PeakMeter : public juce::Component, private juce::Timer
{
public:
    PeakMeter()
    {
        startTimerHz(30);  // 30 Hz refresh rate for smooth visual decay
    }

    ~PeakMeter() override
    {
        stopTimer();
    }

    /**
     * @brief Set current peak level
     * @param level Peak level (0.0 to 1.0+, where 1.0 = 0 dBFS)
     *
     * Thread-safe: can be called from audio thread.
     */
    void setPeakLevel(float level)
    {
        peakLevel.store(level, std::memory_order_relaxed);
    }

    /**
     * @brief Set clip indicator state
     * @param hasClipped True if signal clipped
     */
    void setClipped(bool hasClipped)
    {
        clipped.store(hasClipped, std::memory_order_relaxed);
    }

    /**
     * @brief Reset clip indicator
     */
    void resetClip()
    {
        clipped.store(false, std::memory_order_relaxed);
        displayClipped = false;
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

        // Clip indicator overlays the top of the component
        if (displayClipped)
        {
            auto clipArea = bounds;
            g.setColour(juce::Colours::red);
            g.fillRect(clipArea.removeFromTop(4.0f));
            g.setFont(8.0f);
            g.setColour(juce::Colours::white);
            g.drawText("CLIP", clipArea.removeFromTop(10.0f), juce::Justification::centred);
        }

        const auto meterArea = bounds.reduced(2.0f);
        const float fullHeight = meterArea.getHeight();

        if (displayLevel <= 0.0f || fullHeight <= 0.0f)
            return;

        // Convert to dB and map to a normalised [0,1] position in the meter.
        const float levelDb = juce::Decibels::gainToDecibels(displayLevel);
        const float normalizedLevel = juce::jlimit(0.0f, 1.0f,
            juce::jmap(levelDb, -60.0f, 6.0f, 0.0f, 1.0f));
        const float meterHeight = fullHeight * normalizedLevel;

        if (meterHeight <= 0.0f)
            return;

        // Zone thresholds (normalised):  Green → Yellow at -12 dB,  Yellow → Red at -3 dB
        const float greenThreshold  = juce::jmap(-12.0f, -60.0f, 6.0f, 0.0f, 1.0f);
        const float yellowThreshold = juce::jmap( -3.0f, -60.0f, 6.0f, 0.0f, 1.0f);

        // Absolute pixel heights of each colour zone (measured from the bottom)
        const float greenZoneH  = fullHeight * greenThreshold;
        const float yellowZoneH = fullHeight * (yellowThreshold - greenThreshold);

        // Y-coordinate of the top of the filled bar
        const float barTop     = meterArea.getBottom() - meterHeight;
        const float greenTop   = meterArea.getBottom() - greenZoneH;
        const float yellowTop  = greenTop - yellowZoneH;

        if (normalizedLevel <= greenThreshold)
        {
            g.setColour(juce::Colours::green);
            g.fillRect(meterArea.withTop(barTop));
        }
        else if (normalizedLevel <= yellowThreshold)
        {
            // Yellow section (top of bar) then green section (bottom of bar)
            g.setColour(juce::Colours::yellow);
            g.fillRect(meterArea.withTop(barTop).withBottom(greenTop));
            g.setColour(juce::Colours::green);
            g.fillRect(meterArea.withTop(greenTop));
        }
        else
        {
            // Red (top) → Yellow (middle) → Green (bottom)
            g.setColour(juce::Colours::red);
            g.fillRect(meterArea.withTop(barTop).withBottom(yellowTop));
            g.setColour(juce::Colours::yellow);
            g.fillRect(meterArea.withTop(yellowTop).withBottom(greenTop));
            g.setColour(juce::Colours::green);
            g.fillRect(meterArea.withTop(greenTop));
        }
    }

private:
    void timerCallback() override
    {
        const float newLevel = peakLevel.load(std::memory_order_relaxed);
        const bool newClipped = clipped.load(std::memory_order_relaxed);

        // Fast visual decay for responsive meter behavior
        // At 30 Hz refresh rate, this drops to ~1% in 0.5 seconds
        constexpr float decayRate = 0.85f;

        if (newLevel > displayLevel)
            displayLevel = newLevel;
        else
            displayLevel *= decayRate;

        // Clip indicator latches (must be manually reset)
        if (newClipped)
            displayClipped = true;

        repaint();
    }

    std::atomic<float> peakLevel{0.0f};
    std::atomic<bool> clipped{false};

    float displayLevel{0.0f};
    bool displayClipped{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeakMeter)
};

} // namespace flam
