// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FlamLookAndFeel.h"
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
        g.setColour(juce::Colour(FlamColors::Background));
        g.fillRoundedRectangle(bounds, 2.0f);

        // Clip indicator strip at top — 14px tall so the micro() 12px label fits
        const float clipH = 14.0f;
        auto clipBounds = bounds.removeFromTop(clipH);
        g.setColour(displayClipped ? juce::Colour(FlamColors::AccentRed)
                                   : juce::Colour(FlamColors::Elevated));
        g.fillRoundedRectangle(clipBounds.reduced(1.0f, 0.0f), 2.0f);

        if (displayClipped)
        {
            g.setFont(FlamType::micro());
            g.setColour(juce::Colour(FlamColors::TextPrimary));
            g.drawText("CLIP", clipBounds, juce::Justification::centred);
        }

        bounds.removeFromTop(2.0f);

        // Segmented LED meter: 3px segments with 1px gaps
        const float segH    = 3.0f;
        const float segGap  = 1.0f;
        const float segW    = bounds.getWidth() - 4.0f;
        const float segX    = bounds.getX() + 2.0f;
        const float totalH  = bounds.getHeight();

        const int numSegs   = static_cast<int>(totalH / (segH + segGap));
        const float levelDb = juce::Decibels::gainToDecibels(juce::jmax(displayLevel, 0.00001f));
        const float norm    = juce::jlimit(0.0f, 1.0f, juce::jmap(levelDb, -60.0f, 6.0f, 0.0f, 1.0f));
        const int   litSegs = static_cast<int>(norm * numSegs);

        // Zone boundaries in segment indices
        const int greenTop  = static_cast<int>(juce::jmap(-12.0f, -60.0f, 6.0f, 0.0f, 1.0f) * numSegs);
        const int yellowTop = static_cast<int>(juce::jmap( -3.0f, -60.0f, 6.0f, 0.0f, 1.0f) * numSegs);

        for (int i = 0; i < numSegs; ++i)
        {
            const float segY = bounds.getBottom() - (i + 1) * (segH + segGap) + segGap;
            auto segRect = juce::Rectangle<float>(segX, segY, segW, segH);

            const bool lit = (i < litSegs);
            juce::Colour col;
            if (i < greenTop)
                col = juce::Colour(FlamColors::MeterSafe);
            else if (i < yellowTop)
                col = juce::Colour(FlamColors::MeterWarn);
            else
                col = juce::Colour(FlamColors::MeterClip);

            g.setColour(lit ? col : col.withAlpha(0.10f));
            g.fillRoundedRectangle(segRect, 1.0f);
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
