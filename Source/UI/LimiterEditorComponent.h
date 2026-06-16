// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FlamLookAndFeel.h"
#include "../Core/Mixer.h"

namespace flam {

/**
 * @brief Limiter Editor with threshold and release controls
 *
 * Displays:
 * - Threshold slider (-1.0 to 0.0 dB)
 * - Release slider (10.0 to 500.0 ms)
 *
 * Enable/disable handled by power button in FXButtonComponent.
 * Used in CallOutBox popovers from LIM button on master channel.
 */
class LimiterEditorComponent : public juce::Component
{
public:
    LimiterEditorComponent(Mixer& mixer)
        : mixerRef(mixer)
    {
        setLookAndFeel(&FlamLookAndFeel::instance());
        // Threshold slider
        addAndMakeVisible(thresholdSlider);
        thresholdSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        thresholdSlider.setRange(-1.0, 0.0, 0.01);
        thresholdSlider.setValue(-0.1);
        thresholdSlider.setTextValueSuffix(" dB");
        thresholdSlider.onValueChange = [this] { onThresholdChanged(); };

        addAndMakeVisible(thresholdLabel);
        thresholdLabel.setText("Threshold:", juce::dontSendNotification);
        thresholdLabel.setJustificationType(juce::Justification::centredLeft);
        thresholdLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextSecondary));

        // Release slider
        addAndMakeVisible(releaseSlider);
        releaseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        releaseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        releaseSlider.setRange(10.0, 500.0, 1.0);
        releaseSlider.setValue(50.0);
        releaseSlider.setTextValueSuffix(" ms");
        releaseSlider.setSkewFactorFromMidPoint(50.0);  // Logarithmic scaling
        releaseSlider.onValueChange = [this] { onReleaseChanged(); };

        addAndMakeVisible(releaseLabel);
        releaseLabel.setText("Release:", juce::dontSendNotification);
        releaseLabel.setJustificationType(juce::Justification::centredLeft);
        releaseLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextSecondary));

        updateFromMixer();
        setSize(350, 110);
    }

    ~LimiterEditorComponent() override
    {
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(FlamColors::Surface));

        auto header = getLocalBounds().removeFromTop(32).toFloat();
        g.setColour(juce::Colour(FlamColors::AccentRed).withAlpha(0.12f));
        g.fillRect(header);
        g.setColour(juce::Colour(FlamColors::AccentRed).withAlpha(0.6f));
        g.fillRect(header.removeFromBottom(1.5f));

        g.setColour(juce::Colour(FlamColors::TextPrimary));
        g.setFont(FlamType::labelBold());
        g.drawText("LIMITER", getLocalBounds().removeFromTop(32), juce::Justification::centred);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10);

        // Title takes top space (handled in paint())
        bounds.removeFromTop(30);
        bounds.removeFromTop(10);

        const int labelWidth = 80;
        const int rowHeight = 25;
        const int rowSpacing = 5;

        // Threshold row
        auto thresholdRow = bounds.removeFromTop(rowHeight);
        thresholdLabel.setBounds(thresholdRow.removeFromLeft(labelWidth));
        thresholdSlider.setBounds(thresholdRow);
        bounds.removeFromTop(rowSpacing);

        // Release row
        auto releaseRow = bounds.removeFromTop(rowHeight);
        releaseLabel.setBounds(releaseRow.removeFromLeft(labelWidth));
        releaseSlider.setBounds(releaseRow);
    }

private:
    void onThresholdChanged()
    {
        float threshold = static_cast<float>(thresholdSlider.getValue());
        mixerRef.setMasterLimiterThreshold(threshold);
    }

    void onReleaseChanged()
    {
        float release = static_cast<float>(releaseSlider.getValue());
        mixerRef.setMasterLimiterRelease(release);
    }

    void updateFromMixer()
    {
        float threshold = mixerRef.getMasterLimiterThreshold();
        thresholdSlider.setValue(threshold, juce::dontSendNotification);

        float release = mixerRef.getMasterLimiterRelease();
        releaseSlider.setValue(release, juce::dontSendNotification);
    }

    Mixer& mixerRef;

    juce::Slider thresholdSlider;
    juce::Label thresholdLabel;

    juce::Slider releaseSlider;
    juce::Label releaseLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LimiterEditorComponent)
};

} // namespace flam
