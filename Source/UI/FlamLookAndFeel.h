// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace flam {

// === Valhalla-inspired Color Palette ===
// Dark, purposeful, professional. One source of truth for all component paint calls.
struct FlamColors
{
    // Backgrounds — deep to elevated
    static constexpr juce::uint32 Background    = 0xFF0F1219; // Darkest base layer
    static constexpr juce::uint32 Surface       = 0xFF181D2A; // Panel / section background
    static constexpr juce::uint32 Elevated      = 0xFF222840; // Channel strips, controls
    static constexpr juce::uint32 Interactive   = 0xFF2C3452; // Hover / focus state

    // Text
    static constexpr juce::uint32 TextPrimary   = 0xFFCDD5E8; // Main labels
    static constexpr juce::uint32 TextSecondary = 0xFF7A88A8; // Dim / secondary labels
    static constexpr juce::uint32 TextDisabled  = 0xFF3A4260; // Disabled controls

    // Accent colors — used for value arcs, fills, power-on states
    static constexpr juce::uint32 AccentBlue    = 0xFF5BA4FF; // Primary (faders, arcs)
    static constexpr juce::uint32 AccentTeal    = 0xFF38C8B0; // EQ / secondary
    static constexpr juce::uint32 AccentOrange  = 0xFFFF8C42; // Saturation
    static constexpr juce::uint32 AccentGreen   = 0xFF4DC96A; // Compressor / meter safe
    static constexpr juce::uint32 AccentYellow  = 0xFFFFCC44; // Solo / meter warn
    static constexpr juce::uint32 AccentRed     = 0xFFE83C3C; // Clip / Mute / Limiter

    // Borders
    static constexpr juce::uint32 BorderSubtle  = 0xFF252C42; // Subtle dividers
    static constexpr juce::uint32 BorderActive  = 0xFF3C4868; // Active / focused border
    static constexpr juce::uint32 BorderMaster  = 0xFF4A4020; // Master strip warm tint

    // Meters
    static constexpr juce::uint32 MeterSafe     = 0xFF3AB868; // Green zone
    static constexpr juce::uint32 MeterWarn     = 0xFFD4A820; // Yellow zone
    static constexpr juce::uint32 MeterClip     = 0xFFD42020; // Red zone / clip
};

// === Central LookAndFeel ===
// Subclass of LookAndFeel_V4 that overrides all JUCE-native widget rendering.
// Use instance() to share one copy across components that are off the tree.
class FlamLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static FlamLookAndFeel& instance()
    {
        static FlamLookAndFeel laf;
        return laf;
    }

    FlamLookAndFeel()
    {
        // --- Global / Window ---
        setColour(juce::ResizableWindow::backgroundColourId,      juce::Colour(FlamColors::Background));
        setColour(juce::DocumentWindow::textColourId,             juce::Colour(FlamColors::TextPrimary));

        // --- Label ---
        setColour(juce::Label::textColourId,                      juce::Colour(FlamColors::TextPrimary));
        setColour(juce::Label::backgroundColourId,                juce::Colours::transparentBlack);
        setColour(juce::Label::outlineColourId,                   juce::Colours::transparentBlack);
        setColour(juce::Label::textWhenEditingColourId,           juce::Colour(FlamColors::TextPrimary));

        // --- TextButton ---
        setColour(juce::TextButton::buttonColourId,               juce::Colour(FlamColors::Elevated));
        setColour(juce::TextButton::buttonOnColourId,             juce::Colour(FlamColors::AccentBlue));
        setColour(juce::TextButton::textColourOffId,              juce::Colour(FlamColors::TextPrimary));
        setColour(juce::TextButton::textColourOnId,               juce::Colour(FlamColors::Background));

        // --- ComboBox + PopupMenu ---
        setColour(juce::ComboBox::backgroundColourId,             juce::Colour(FlamColors::Elevated));
        setColour(juce::ComboBox::textColourId,                   juce::Colour(FlamColors::TextPrimary));
        setColour(juce::ComboBox::outlineColourId,                juce::Colour(FlamColors::BorderSubtle));
        setColour(juce::ComboBox::buttonColourId,                 juce::Colour(FlamColors::Interactive));
        setColour(juce::ComboBox::arrowColourId,                  juce::Colour(FlamColors::TextSecondary));
        setColour(juce::PopupMenu::backgroundColourId,            juce::Colour(FlamColors::Surface));
        setColour(juce::PopupMenu::textColourId,                  juce::Colour(FlamColors::TextPrimary));
        setColour(juce::PopupMenu::headerTextColourId,            juce::Colour(FlamColors::TextSecondary));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(FlamColors::Interactive));
        setColour(juce::PopupMenu::highlightedTextColourId,       juce::Colour(FlamColors::TextPrimary));

        // --- Slider ---
        setColour(juce::Slider::trackColourId,                    juce::Colour(FlamColors::BorderSubtle));
        setColour(juce::Slider::thumbColourId,                    juce::Colour(FlamColors::TextPrimary));
        setColour(juce::Slider::textBoxTextColourId,              juce::Colour(FlamColors::TextSecondary));
        setColour(juce::Slider::textBoxBackgroundColourId,        juce::Colour(FlamColors::Surface));
        setColour(juce::Slider::textBoxHighlightColourId,         juce::Colour(FlamColors::AccentBlue).withAlpha(0.3f));
        setColour(juce::Slider::textBoxOutlineColourId,           juce::Colour(FlamColors::BorderSubtle));

        // --- TabbedComponent / TabbedButtonBar ---
        setColour(juce::TabbedComponent::backgroundColourId,      juce::Colour(FlamColors::Background));
        setColour(juce::TabbedComponent::outlineColourId,         juce::Colour(FlamColors::BorderSubtle));
        setColour(juce::TabbedButtonBar::tabOutlineColourId,      juce::Colour(FlamColors::BorderSubtle));
        setColour(juce::TabbedButtonBar::frontOutlineColourId,    juce::Colour(FlamColors::AccentBlue));

        // --- ScrollBar ---
        setColour(juce::ScrollBar::thumbColourId,                 juce::Colour(FlamColors::Interactive));
        setColour(juce::ScrollBar::trackColourId,                 juce::Colour(FlamColors::Surface));

        // --- GroupComponent ---
        setColour(juce::GroupComponent::textColourId,             juce::Colour(FlamColors::TextSecondary));
        setColour(juce::GroupComponent::outlineColourId,          juce::Colour(FlamColors::BorderSubtle));

        // --- Alert ---
        setColour(juce::AlertWindow::backgroundColourId,          juce::Colour(FlamColors::Surface));
        setColour(juce::AlertWindow::textColourId,                juce::Colour(FlamColors::TextPrimary));
        setColour(juce::AlertWindow::outlineColourId,             juce::Colour(FlamColors::BorderActive));
    }

    // --- Tab bar: flat with blue bottom accent line on active tab ---

    void drawTabButton(juce::TabBarButton& button, juce::Graphics& g,
                       bool isMouseOver, bool isMouseDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        const bool isActive = button.getToggleState();

        if (isActive)
        {
            g.setColour(juce::Colour(FlamColors::Surface));
            g.fillRect(bounds);

            g.setColour(juce::Colour(FlamColors::AccentBlue));
            g.fillRect(bounds.removeFromBottom(2.0f));
        }
        else if (isMouseOver)
        {
            g.setColour(juce::Colour(FlamColors::Elevated).withAlpha(0.5f));
            g.fillRect(bounds);
        }

        g.setFont(juce::Font(12.0f, isActive ? juce::Font::bold : juce::Font::plain));
        g.setColour(isActive ? juce::Colour(FlamColors::TextPrimary)
                             : juce::Colour(FlamColors::TextSecondary));
        g.drawText(button.getButtonText(), button.getTextArea(), juce::Justification::centred);
    }

    int getTabButtonBestWidth(juce::TabBarButton& button, int) override
    {
        const auto font = juce::Font(13.0f, juce::Font::bold);
        return juce::jmax(80, font.getStringWidth(button.getButtonText()) + 36);
    }

    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar& bar, juce::Graphics& g,
                                      int w, int h) override
    {
        g.setColour(juce::Colour(FlamColors::BorderSubtle));
        g.fillRect(0, h - 1, w, 1);
    }

    // --- Buttons ---

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& bgColour,
                              bool isMouseOver, bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        const bool isOn = button.getToggleState();

        juce::Colour fill;
        if (isOn)
            fill = bgColour;
        else if (isButtonDown)
            fill = juce::Colour(FlamColors::Interactive).brighter(0.15f);
        else if (isMouseOver)
            fill = juce::Colour(FlamColors::Interactive);
        else
            fill = juce::Colour(FlamColors::Elevated);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 3.0f);

        g.setColour(isOn ? bgColour.brighter(0.4f) : juce::Colour(FlamColors::BorderSubtle));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool, bool) override
    {
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.setColour(button.getToggleState() ? juce::Colour(FlamColors::Background)
                                            : juce::Colour(FlamColors::TextPrimary));
        g.drawText(button.getButtonText(), button.getLocalBounds(),
                   juce::Justification::centred, true);
    }

    // --- Sliders ---
    // Horizontal: left-to-thumb accent fill. Vertical: bipolar center fill for EQ,
    // bottom-to-thumb fill for gain-style parameters.

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearHorizontal)
        {
            drawHorizontalSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, slider);
        }
        else if (style == juce::Slider::LinearVertical)
        {
            drawVerticalSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, slider);
        }
        else
        {
            juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                                    minSliderPos, maxSliderPos, style, slider);
        }
    }

    // --- ComboBox ---

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox&) override
    {
        auto bounds = juce::Rectangle<float>(0.0f, 0.0f, (float)width, (float)height);
        g.setColour(juce::Colour(FlamColors::Elevated));
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(juce::Colour(FlamColors::BorderSubtle));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

        // Arrow
        const float arrowX = width - 14.0f;
        const float arrowY = height * 0.5f;
        const float arrowSize = 4.0f;
        juce::Path arrow;
        arrow.addTriangle(arrowX - arrowSize, arrowY - arrowSize * 0.5f,
                          arrowX + arrowSize, arrowY - arrowSize * 0.5f,
                          arrowX,             arrowY + arrowSize * 0.5f);
        g.setColour(juce::Colour(FlamColors::TextSecondary));
        g.fillPath(arrow);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(6, 0, box.getWidth() - 22, box.getHeight());
        label.setFont(juce::Font(12.0f));
    }

    // --- GroupComponent ---

    void drawGroupComponentOutline(juce::Graphics& g, int width, int height,
                                   const juce::String& text,
                                   const juce::Justification& justification,
                                   juce::GroupComponent& group) override
    {
        const float cornerSize = 4.0f;
        const float labelH     = 13.0f;
        const float indent     = 3.0f;
        const float gap        = 6.0f;

        auto font = juce::Font(11.0f);
        const float labelW = text.isEmpty() ? 0.0f
                                            : font.getStringWidthFloat(text) + gap * 2.0f;

        const float trackTop = labelH * 0.5f;
        auto outerBounds = juce::Rectangle<float>(indent, trackTop,
                                                   width - indent * 2.0f,
                                                   height - trackTop - indent);

        const float labelX = outerBounds.getX() + cornerSize + gap;
        juce::Path p;

        // Draw box outline with a gap for the label text
        p.startNewSubPath(labelX + labelW, outerBounds.getY());
        p.lineTo(outerBounds.getRight() - cornerSize, outerBounds.getY());
        p.addArc(outerBounds.getRight() - cornerSize * 2.0f, outerBounds.getY(),
                 cornerSize * 2.0f, cornerSize * 2.0f,
                 0.0f, juce::MathConstants<float>::halfPi);
        p.lineTo(outerBounds.getRight(), outerBounds.getBottom() - cornerSize);
        p.addArc(outerBounds.getRight() - cornerSize * 2.0f, outerBounds.getBottom() - cornerSize * 2.0f,
                 cornerSize * 2.0f, cornerSize * 2.0f,
                 juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi);
        p.lineTo(outerBounds.getX() + cornerSize, outerBounds.getBottom());
        p.addArc(outerBounds.getX(), outerBounds.getBottom() - cornerSize * 2.0f,
                 cornerSize * 2.0f, cornerSize * 2.0f,
                 juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 1.5f);
        p.lineTo(outerBounds.getX(), outerBounds.getY() + cornerSize);
        p.addArc(outerBounds.getX(), outerBounds.getY(),
                 cornerSize * 2.0f, cornerSize * 2.0f,
                 juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi);
        p.lineTo(labelX, outerBounds.getY());

        g.setColour(group.findColour(juce::GroupComponent::outlineColourId));
        g.strokePath(p, juce::PathStrokeType(1.0f));

        if (text.isNotEmpty())
        {
            g.setFont(font);
            g.setColour(group.findColour(juce::GroupComponent::textColourId));
            g.drawText(text,
                       (int)(labelX), 0,
                       (int)(labelW), (int)(labelH),
                       juce::Justification::centred);
        }
    }

    // --- ScrollBar ---

    int getScrollbarButtonSize(juce::ScrollBar&) override { return 0; }

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar,
                       int x, int y, int width, int height, bool isVertical,
                       int thumbStart, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override
    {
        g.setColour(juce::Colour(FlamColors::Surface));
        g.fillRect(x, y, width, height);

        if (thumbSize > 0)
        {
            juce::Rectangle<float> thumb;
            if (isVertical)
                thumb = { (float)x + 2.0f, (float)(y + thumbStart) + 2.0f,
                          (float)width - 4.0f, (float)thumbSize - 4.0f };
            else
                thumb = { (float)(x + thumbStart) + 2.0f, (float)y + 2.0f,
                          (float)thumbSize - 4.0f, (float)height - 4.0f };

            g.setColour(isMouseOver || isMouseDown ? juce::Colour(FlamColors::BorderActive)
                                                   : juce::Colour(FlamColors::Interactive));
            g.fillRoundedRectangle(thumb, 3.0f);
        }
    }

private:
    void drawHorizontalSlider(juce::Graphics& g, int x, int y, int width, int height,
                               float sliderPos, float, float, juce::Slider&)
    {
        const float trackH = 4.0f;
        const float trackY = y + (height - trackH) * 0.5f;
        const float thumbW = 12.0f;
        const float thumbH = 20.0f;

        // Track background
        juce::Rectangle<float> track((float)x, trackY, (float)width, trackH);
        g.setColour(juce::Colour(FlamColors::BorderSubtle));
        g.fillRoundedRectangle(track, 2.0f);

        // Accent fill: left edge to thumb
        auto fill = track.withRight(sliderPos);
        g.setColour(juce::Colour(FlamColors::AccentBlue).withAlpha(0.85f));
        g.fillRoundedRectangle(fill, 2.0f);

        // Thumb
        auto thumb = juce::Rectangle<float>(sliderPos - thumbW * 0.5f,
                                             y + (height - thumbH) * 0.5f,
                                             thumbW, thumbH);
        g.setColour(juce::Colour(FlamColors::TextPrimary));
        g.fillRoundedRectangle(thumb, 3.0f);
        g.setColour(juce::Colour(FlamColors::AccentBlue));
        g.drawRoundedRectangle(thumb, 3.0f, 1.5f);
    }

    void drawVerticalSlider(juce::Graphics& g, int x, int y, int width, int height,
                             float sliderPos, float minSliderPos, float maxSliderPos,
                             juce::Slider& slider)
    {
        const float trackW = 4.0f;
        const float trackX = x + (width - trackW) * 0.5f;
        const float thumbW = 20.0f;
        const float thumbH = 10.0f;

        // Track background (maxSliderPos = top, minSliderPos = bottom)
        const float trackTop    = maxSliderPos;
        const float trackBottom = minSliderPos;
        juce::Rectangle<float> track(trackX, trackTop, trackW, trackBottom - trackTop);
        g.setColour(juce::Colour(FlamColors::BorderSubtle));
        g.fillRoundedRectangle(track, 2.0f);

        // Bipolar fill when range crosses zero (EQ bands)
        const bool isBipolar = (slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0);
        if (isBipolar)
        {
            const float centerY = (minSliderPos + maxSliderPos) * 0.5f;
            const float fillTop    = juce::jmin(sliderPos, centerY);
            const float fillBottom = juce::jmax(sliderPos, centerY);
            if (fillBottom > fillTop)
            {
                g.setColour(juce::Colour(FlamColors::AccentTeal).withAlpha(0.85f));
                g.fillRoundedRectangle(trackX, fillTop, trackW, fillBottom - fillTop, 2.0f);
            }

            // Center detent tick
            g.setColour(juce::Colour(FlamColors::BorderActive));
            g.fillRect(trackX - 3.0f, centerY - 0.5f, trackW + 6.0f, 1.0f);
        }
        else
        {
            // Fill from bottom to thumb
            auto fill = track.withTop(sliderPos);
            g.setColour(juce::Colour(FlamColors::AccentTeal).withAlpha(0.85f));
            g.fillRoundedRectangle(fill, 2.0f);
        }

        // Thumb
        auto thumb = juce::Rectangle<float>(x + (width - thumbW) * 0.5f,
                                             sliderPos - thumbH * 0.5f,
                                             thumbW, thumbH);
        g.setColour(juce::Colour(FlamColors::TextPrimary));
        g.fillRoundedRectangle(thumb, 3.0f);
        g.setColour(juce::Colour(FlamColors::AccentTeal));
        g.drawRoundedRectangle(thumb, 3.0f, 1.5f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlamLookAndFeel)
};

} // namespace flam
