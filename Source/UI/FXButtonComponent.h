// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace flam {

/**
 * @brief FX button with integrated power toggle
 *
 * Layout: [FX NAME                    ⏻]
 * - Left side: Label (e.g., "EQ", "SAT", "COMP")
 * - Right side: Power button (⏻) to enable/disable
 * - Background: Highlighted when enabled
 * - Click on left area: Open editor
 * - Click on power button: Toggle enabled state
 */
class FXButtonComponent : public juce::Component
{
public:
    FXButtonComponent(const juce::String& name, juce::Colour enabledColour)
        : fxName(name)
        , highlightColour(enabledColour)
    {
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();

        // Background - highlighted when enabled
        if (isEnabled)
        {
            g.setColour(highlightColour.withAlpha(0.3f));
            g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

            // Border when enabled
            g.setColour(highlightColour);
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
        }
        else
        {
            // Subtle border when disabled
            g.setColour(juce::Colour(0xff3a3a3a));
            g.fillRoundedRectangle(bounds.toFloat(), 3.0f);

            g.setColour(juce::Colours::darkgrey);
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
        }

        // FX name on left side
        auto labelBounds = bounds.reduced(4, 0).removeFromLeft(bounds.getWidth() - 20);
        g.setColour(isEnabled ? juce::Colours::white : juce::Colours::grey);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(fxName, labelBounds, juce::Justification::centredLeft);

        // Power button (custom drawn symbol) on right side
        auto powerBounds = bounds.reduced(2).removeFromRight(16);

        // Draw power symbol (circle with line at top)
        auto symbolBounds = powerBounds.reduced(3).toFloat();
        auto centerX = symbolBounds.getCentreX();
        auto centerY = symbolBounds.getCentreY();
        auto radius = symbolBounds.getWidth() / 2.0f;

        g.setColour(isEnabled ? highlightColour : juce::Colours::grey);

        // Draw circle (open at top)
        juce::Path powerSymbol;
        powerSymbol.addCentredArc(centerX, centerY, radius, radius, 0.0f,
                                   juce::MathConstants<float>::pi * 0.7f,
                                   juce::MathConstants<float>::pi * 2.3f, true);
        g.strokePath(powerSymbol, juce::PathStrokeType(1.5f));

        // Draw vertical line at top
        g.drawLine(centerX, centerY - radius, centerX, centerY - radius * 0.3f, 1.5f);

        // Highlight power button on hover
        if (powerButtonHovered)
        {
            g.setColour(isEnabled ? highlightColour.brighter(0.3f) : juce::Colours::lightgrey);
            g.drawRoundedRectangle(powerBounds.toFloat(), 2.0f, 1.0f);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        bool wasHovered = powerButtonHovered;
        powerButtonHovered = getPowerButtonBounds().contains(e.getPosition());

        if (wasHovered != powerButtonHovered)
            repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (powerButtonHovered)
        {
            powerButtonHovered = false;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (getPowerButtonBounds().contains(e.getPosition()))
        {
            // Toggle enabled state
            setEnabled(!isEnabled);

            if (onEnabledChanged)
                onEnabledChanged(isEnabled);
        }
        else
        {
            // Open editor
            if (onEditorRequested)
                onEditorRequested();
        }
    }

    void setEnabled(bool shouldBeEnabled)
    {
        if (isEnabled != shouldBeEnabled)
        {
            isEnabled = shouldBeEnabled;
            repaint();
        }
    }

    bool getEnabled() const { return isEnabled; }

    // Callbacks
    std::function<void(bool)> onEnabledChanged;
    std::function<void()> onEditorRequested;

private:
    juce::Rectangle<int> getPowerButtonBounds() const
    {
        return getLocalBounds().reduced(2).removeFromRight(16);
    }

    juce::String fxName;
    juce::Colour highlightColour;
    bool isEnabled = false;
    bool powerButtonHovered = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FXButtonComponent)
};

} // namespace flam
