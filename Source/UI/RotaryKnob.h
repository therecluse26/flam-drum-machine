// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace flam {

/**
 * @brief Visual rotary knob component with circular arc and pointer
 *
 * Professional-looking rotary knob with:
 * - Circular arc showing value range
 * - Rotating pointer/indicator
 * - Optional text value display
 * - Smooth mouse drag behavior
 * - Center detent for 0 (pan center)
 */
class RotaryKnob : public juce::Component
{
public:
    RotaryKnob()
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    /**
     * @brief Set the knob's value range
     * @param newMinimum Minimum value (fully counter-clockwise)
     * @param newMaximum Maximum value (fully clockwise)
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
     * @brief Set text label for knob (e.g., "Pan", "Volume")
     */
    void setLabel(const juce::String& labelText)
    {
        label = labelText;
        repaint();
    }

    /**
     * @brief Set value suffix for display (e.g., "%", "L/R")
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

        // Label at top
        if (label.isNotEmpty())
        {
            g.setColour(juce::Colours::white);
            g.setFont(10.0f);
            auto labelBounds = bounds.removeFromTop(14.0f);
            g.drawText(label, labelBounds, juce::Justification::centred);
        }

        // Value display at bottom
        if (showValue)
        {
            g.setColour(juce::Colours::white);
            g.setFont(9.0f);

            juce::String valueText;
            if (textSuffix == "L/R")
            {
                // Special formatting for pan (L50, C, R50)
                if (value < -0.01)
                    valueText = "L" + juce::String(std::abs(value * 100.0), 0);
                else if (value > 0.01)
                    valueText = "R" + juce::String(value * 100.0, 0);
                else
                    valueText = "C";
            }
            else
            {
                valueText = juce::String(value, 1) + textSuffix;
            }

            auto textBounds = bounds.removeFromBottom(14.0f);
            g.drawText(valueText, textBounds, juce::Justification::centred);
        }

        // Knob circle in center
        auto knobBounds = bounds.reduced(4.0f);
        const float diameter = juce::jmin(knobBounds.getWidth(), knobBounds.getHeight());
        const float centerX = knobBounds.getCentreX();
        const float centerY = knobBounds.getCentreY();
        const float radius = diameter * 0.5f;

        // Draw knob body
        g.setColour(isMouseOver || isDragging ? juce::Colour(0xff3a3a3a) : juce::Colour(0xff2a2a2a));
        g.fillEllipse(centerX - radius, centerY - radius, diameter, diameter);

        // Draw knob border
        g.setColour(juce::Colours::grey);
        g.drawEllipse(centerX - radius, centerY - radius, diameter, diameter, 1.5f);

        // Draw arc track showing range
        // Pan knob: -1.0 (left) at 7:30, 0.0 (center) at 12:00, +1.0 (right) at 4:30
        // In radians: -pi/2 is 12 o'clock (top)
        // We want 270 degree range centered on -pi/2
        const float arcRadius = radius - 4.0f;

        // Range: from -pi to +pi/2 (270 degrees centered at top)
        // Start: -pi + pi/4 = -3pi/4 (7:30 position, or 225 degrees)
        // End: -pi/2 + pi*3/4 = pi/4 (1:30... wait, let me recalculate)
        // Actually: we want symmetric around -pi/2 (top)
        // Start: -pi/2 - 3pi/4 = -5pi/4 (7:30)
        // End: -pi/2 + 3pi/4 = pi/4 (1:30)

        const float centerAngle = -juce::MathConstants<float>::pi * 0.5f;  // 12 o'clock (top)
        const float halfRange = juce::MathConstants<float>::pi * 0.75f;     // 135 degrees on each side
        const float rotationStart = centerAngle - halfRange;  // 7:30 position
        const float rotationEnd = centerAngle + halfRange;    // 4:30 position
        const float rotationRange = rotationEnd - rotationStart;

        // Rotation offset: arcs are drawn starting at 0 radians (3 o'clock/right)
        // We need to rotate 90 degrees clockwise to align with our coordinate system
        const float arcRotation = juce::MathConstants<float>::pi * 0.5f;  // 90 degrees clockwise

        // Background arc (unfilled portion)
        juce::Path backgroundArc;
        backgroundArc.addCentredArc(centerX, centerY, arcRadius, arcRadius, arcRotation,
                                    rotationStart, rotationEnd, true);
        g.setColour(juce::Colour(0xff1a1a1a));
        g.strokePath(backgroundArc, juce::PathStrokeType(3.0f));

        // Value arc (filled portion from center outward)
        const float normalizedValue = (value - minimum) / (maximum - minimum);
        const float valueRotation = rotationStart + normalizedValue * rotationRange;

        // Calculate center position (where 0 is)
        const float zeroNormalized = (0.0 - minimum) / (maximum - minimum);
        const float centerRotation = rotationStart + zeroNormalized * rotationRange;

        // Draw arc from center to current value
        // If value > 0, draw clockwise from center to pointer
        // If value < 0, draw counter-clockwise from pointer to center
        juce::Path valueArc;
        if (std::abs(value) > 0.01)  // Only draw if away from center
        {
            if (value > 0.0)
            {
                // Right pan: arc from center (12:00) to pointer (clockwise)
                valueArc.addCentredArc(centerX, centerY, arcRadius, arcRadius, arcRotation,
                                       centerRotation, valueRotation, true);
            }
            else
            {
                // Left pan: arc from pointer to center (so it appears on left side)
                valueArc.addCentredArc(centerX, centerY, arcRadius, arcRadius, arcRotation,
                                       valueRotation, centerRotation, true);
            }
            g.setColour(juce::Colour(0xff4a9eff));
            g.strokePath(valueArc, juce::PathStrokeType(3.0f));
        }

        // Center dot indicator
        g.setColour(juce::Colours::white.withAlpha(0.3f));
        g.fillEllipse(centerX - 2.0f, centerY - 2.0f, 4.0f, 4.0f);

        // Draw pointer line from center to edge
        const float pointerLength = radius - 6.0f;
        const float pointerEndX = centerX + std::cos(valueRotation) * pointerLength;
        const float pointerEndY = centerY + std::sin(valueRotation) * pointerLength;

        g.setColour(juce::Colours::white);
        g.drawLine(centerX, centerY, pointerEndX, pointerEndY, 2.0f);

        // Draw center detent marker if 0 is in range
        if (minimum <= 0.0 && maximum >= 0.0)
        {
            // Reuse centerRotation calculated above for the detent position
            const float detentInnerRadius = radius + 2.0f;
            const float detentOuterRadius = radius + 6.0f;
            const float detentStartX = centerX + std::cos(centerRotation) * detentInnerRadius;
            const float detentStartY = centerY + std::sin(centerRotation) * detentInnerRadius;
            const float detentEndX = centerX + std::cos(centerRotation) * detentOuterRadius;
            const float detentEndY = centerY + std::sin(centerRotation) * detentOuterRadius;

            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawLine(detentStartX, detentStartY, detentEndX, detentEndY, 2.0f);
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

        // Vertical drag to change value (drag up = increase)
        const float deltaY = dragStartY - e.position.y;
        const float sensitivity = e.mods.isShiftDown() ? 0.002f : 0.01f;  // Fine control with Shift

        const float deltaValue = deltaY * sensitivity * (maximum - minimum);
        const double newValue = dragStartValue + deltaValue;

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
        // Double-click to reset to 0 (center for pan)
        if (minimum <= 0.0 && maximum >= 0.0)
            setValue(0.0, true);
    }

private:
    double minimum{-1.0};
    double maximum{1.0};
    double value{0.0};

    juce::String label;
    juce::String textSuffix{"L/R"};
    bool showValue{true};

    bool isDragging{false};
    bool isMouseOver{false};

    double dragStartValue{0.0};
    float dragStartY{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RotaryKnob)
};

} // namespace flam
