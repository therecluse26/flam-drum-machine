// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace flam {

/**
 * @brief Visual vertical fader component with rectangular track and thumb
 *
 * Professional-looking fader with:
 * - Rectangular track background
 * - Draggable thumb (handle)
 * - Optional text value display
 * - Smooth mouse drag behavior
 * - Center detent for 0 dB
 */
class VerticalFader : public juce::Component
{
public:
    VerticalFader()
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    /**
     * @brief Set the fader's value range
     * @param newMinimum Minimum value (bottom of fader)
     * @param newMaximum Maximum value (top of fader)
     */
    void setRange(double newMinimum, double newMaximum)
    {
        minimum = newMinimum;
        maximum = newMaximum;
        value = juce::jlimit(minimum, maximum, value);
        repaint();
    }

    /**
     * @brief Set current value
     * @param newValue New value (will be clamped to range)
     * @param sendNotification Whether to trigger onValueChange callback
     */
    void setValue(double newValue, bool sendNotification = true)
    {
        newValue = juce::jlimit(minimum, maximum, newValue);

        if (value != newValue)
        {
            value = newValue;
            repaint();

            if (sendNotification && onValueChange)
                onValueChange(value);
        }
    }

    /**
     * @brief Get current value
     */
    double getValue() const { return value; }

    /**
     * @brief Set value suffix for display (e.g., " dB")
     */
    void setTextValueSuffix(const juce::String& suffix)
    {
        textSuffix = suffix;
        repaint();
    }

    /**
     * @brief Set whether to show value text
     */
    void setShowValue(bool shouldShow)
    {
        showValue = shouldShow;
        repaint();
    }

    /**
     * @brief Callback triggered when value changes
     */
    std::function<void(double)> onValueChange;

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Track background (groove)
        const float trackWidth = 8.0f;
        const float trackX = (bounds.getWidth() - trackWidth) * 0.5f;
        const float trackTop = 10.0f;
        const float trackBottom = bounds.getHeight() - 30.0f;
        const float trackHeight = trackBottom - trackTop;

        auto trackBounds = juce::Rectangle<float>(trackX, trackTop, trackWidth, trackHeight);

        // Draw track groove
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillRoundedRectangle(trackBounds, 2.0f);

        // Draw track border
        g.setColour(juce::Colours::darkgrey);
        g.drawRoundedRectangle(trackBounds, 2.0f, 1.0f);

        // Calculate thumb position
        const float normalizedValue = (value - minimum) / (maximum - minimum);
        const float thumbY = trackBottom - (normalizedValue * trackHeight);

        // Draw filled portion (below thumb)
        if (normalizedValue > 0.0f)
        {
            auto filledBounds = trackBounds.withTop(thumbY).withBottom(trackBottom);
            g.setColour(juce::Colour(0xff4a9eff).withAlpha(0.7f));
            g.fillRoundedRectangle(filledBounds, 2.0f);
        }

        // Draw center detent line (0 dB position if in range)
        if (minimum <= 0.0 && maximum >= 0.0)
        {
            const float zeroNormalized = (0.0 - minimum) / (maximum - minimum);
            const float zeroY = trackBottom - (zeroNormalized * trackHeight);

            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawLine(trackX - 2.0f, zeroY, trackX + trackWidth + 2.0f, zeroY, 1.0f);
        }

        // Draw thumb (rectangular handle)
        const float thumbWidth = 20.0f;
        const float thumbHeight = 10.0f;
        const float thumbX = (bounds.getWidth() - thumbWidth) * 0.5f;

        auto thumbBounds = juce::Rectangle<float>(
            thumbX,
            thumbY - thumbHeight * 0.5f,
            thumbWidth,
            thumbHeight
        );

        // Thumb body
        g.setColour(isMouseOver || isDragging ? juce::Colour(0xff6fb6ff) : juce::Colour(0xff5a5a5a));
        g.fillRoundedRectangle(thumbBounds, 2.0f);

        // Thumb border
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawRoundedRectangle(thumbBounds, 2.0f, 1.5f);

        // Value display at bottom
        if (showValue)
        {
            g.setColour(juce::Colours::white);
            g.setFont(9.0f);

            juce::String valueText = juce::String(value, 1) + textSuffix;
            auto textBounds = bounds.removeFromBottom(20.0f);
            g.drawText(valueText, textBounds, juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        isDragging = true;
        dragStartValue = value;
        dragStartY = e.position.y;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!isDragging)
            return;

        // Calculate value from mouse position
        const float trackTop = 10.0f;
        const float trackBottom = getHeight() - 30.0f;
        const float trackHeight = trackBottom - trackTop;

        // Invert Y coordinate (drag up = increase value)
        const float mouseY = juce::jlimit(trackTop, trackBottom, e.position.y);
        const float normalizedValue = (trackBottom - mouseY) / trackHeight;

        // Fine control with Shift key
        double newValue;
        if (e.mods.isShiftDown())
        {
            // Fine adjustment: scale mouse movement by 0.1x
            const float deltaY = dragStartY - e.position.y;
            const float deltaNormalized = deltaY / trackHeight * 0.1;
            const float startNormalized = (dragStartValue - minimum) / (maximum - minimum);
            newValue = minimum + (startNormalized + deltaNormalized) * (maximum - minimum);
        }
        else
        {
            // Normal adjustment
            newValue = minimum + normalizedValue * (maximum - minimum);
        }

        setValue(newValue, true);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        isDragging = false;
        repaint();
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        isMouseOver = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        isMouseOver = false;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        // Double-click to reset to 0 dB (if in range)
        if (minimum <= 0.0 && maximum >= 0.0)
            setValue(0.0, true);
    }

private:
    double minimum{-96.0};
    double maximum{6.0};
    double value{0.0};

    juce::String textSuffix{" dB"};
    bool showValue{true};

    bool isDragging{false};
    bool isMouseOver{false};

    double dragStartValue{0.0};
    float dragStartY{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VerticalFader)
};

} // namespace flam
