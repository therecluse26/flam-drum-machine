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

        auto meterBounds = bounds.reduced(2.0f);

        // Convert to dB for display
        const float levelDb = juce::Decibels::gainToDecibels(displayLevel);

        // Map dB to meter height (-60 dB to +6 dB range)
        const float normalizedLevel = juce::jmap(levelDb, -60.0f, 6.0f, 0.0f, 1.0f);
        const float meterHeight = meterBounds.getHeight() * juce::jlimit(0.0f, 1.0f, normalizedLevel);

        // Draw meter bar with color gradient
        if (meterHeight > 0.0f)
        {
            auto meterRect = meterBounds.removeFromBottom(meterHeight);

            // Color zones:
            // Green: -∞ to -12 dB
            // Yellow: -12 dB to -3 dB
            // Red: -3 dB to 0 dB (and above)

            const float greenThreshold = juce::jmap(-12.0f, -60.0f, 6.0f, 0.0f, 1.0f);
            const float yellowThreshold = juce::jmap(-3.0f, -60.0f, 6.0f, 0.0f, 1.0f);

            if (normalizedLevel < greenThreshold)
            {
                // All green
                g.setColour(juce::Colours::green);
                g.fillRect(meterRect);
            }
            else if (normalizedLevel < yellowThreshold)
            {
                // Green + yellow
                float greenHeight = meterBounds.getHeight() * greenThreshold;
                auto greenRect = meterRect.removeFromBottom(greenHeight);

                g.setColour(juce::Colours::green);
                g.fillRect(greenRect);

                g.setColour(juce::Colours::yellow);
                g.fillRect(meterRect);
            }
            else
            {
                // Green + yellow + red
                float greenHeight = meterBounds.getHeight() * greenThreshold;
                float yellowHeight = meterBounds.getHeight() * (yellowThreshold - greenThreshold);

                auto greenRect = meterRect;
                greenRect.removeFromTop(meterRect.getHeight() - greenHeight);

                auto yellowRect = meterRect;
                yellowRect.removeFromTop(meterRect.getHeight() - greenHeight - yellowHeight);
                yellowRect.removeFromBottom(greenHeight);

                auto redRect = meterRect;
                redRect.removeFromBottom(greenHeight + yellowHeight);

                g.setColour(juce::Colours::green);
                g.fillRect(greenRect);

                g.setColour(juce::Colours::yellow);
                g.fillRect(yellowRect);

                g.setColour(juce::Colours::red);
                g.fillRect(redRect);
            }
        }

        // Clip indicator at top
        if (displayClipped)
        {
            g.setColour(juce::Colours::red);
            g.fillRect(bounds.removeFromTop(4.0f));

            // Draw "CLIP" text
            g.setFont(8.0f);
            g.setColour(juce::Colours::white);
            g.drawText("CLIP", bounds.removeFromTop(10.0f), juce::Justification::centred);
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
