// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FlamLookAndFeel.h"
#include "../Core/Mixer.h"

namespace flam {

/**
 * @brief Compressor Editor with full dynamics control
 *
 * Displays:
 * - Threshold slider (-60.0 to 0.0 dB)
 * - Ratio slider (1.0 to 20.0)
 * - Attack slider (0.1 to 100.0 ms)
 * - Release slider (10.0 to 1000.0 ms)
 * - Makeup gain slider (-20.0 to 20.0 dB)
 *
 * Enable/disable handled by power button in FXButtonComponent.
 * Used in CallOutBox popovers from COMP buttons.
 */
class CompressorEditorComponent : public juce::Component
{
public:
    CompressorEditorComponent(Mixer& mixer, int channelIdx, bool isMaster = false)
        : mixerRef(mixer)
        , channelIndex(channelIdx)
        , isMasterCompressor(isMaster)
    {
        setLookAndFeel(&FlamLookAndFeel::instance());
        // Threshold slider
        addAndMakeVisible(thresholdSlider);
        thresholdSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        thresholdSlider.setRange(-60.0, 0.0, 0.1);
        thresholdSlider.setValue(-20.0);
        thresholdSlider.setTextValueSuffix(" dB");
        thresholdSlider.onValueChange = [this] { onThresholdChanged(); };

        addAndMakeVisible(thresholdLabel);
        thresholdLabel.setText("Threshold:", juce::dontSendNotification);
        thresholdLabel.setJustificationType(juce::Justification::centredLeft);
        thresholdLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextSecondary));

        // Ratio slider
        addAndMakeVisible(ratioSlider);
        ratioSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        ratioSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        ratioSlider.setRange(1.0, 20.0, 0.1);
        ratioSlider.setValue(4.0);
        ratioSlider.setTextValueSuffix(":1");
        ratioSlider.onValueChange = [this] { onRatioChanged(); };

        addAndMakeVisible(ratioLabel);
        ratioLabel.setText("Ratio:", juce::dontSendNotification);
        ratioLabel.setJustificationType(juce::Justification::centredLeft);
        ratioLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextSecondary));

        // Attack slider
        addAndMakeVisible(attackSlider);
        attackSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        attackSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        attackSlider.setRange(0.1, 100.0, 0.1);
        attackSlider.setValue(10.0);
        attackSlider.setTextValueSuffix(" ms");
        attackSlider.setSkewFactorFromMidPoint(10.0);  // Logarithmic scaling
        attackSlider.onValueChange = [this] { onAttackChanged(); };

        addAndMakeVisible(attackLabel);
        attackLabel.setText("Attack:", juce::dontSendNotification);
        attackLabel.setJustificationType(juce::Justification::centredLeft);
        attackLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextSecondary));

        // Release slider
        addAndMakeVisible(releaseSlider);
        releaseSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        releaseSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        releaseSlider.setRange(10.0, 1000.0, 1.0);
        releaseSlider.setValue(100.0);
        releaseSlider.setTextValueSuffix(" ms");
        releaseSlider.setSkewFactorFromMidPoint(100.0);  // Logarithmic scaling
        releaseSlider.onValueChange = [this] { onReleaseChanged(); };

        addAndMakeVisible(releaseLabel);
        releaseLabel.setText("Release:", juce::dontSendNotification);
        releaseLabel.setJustificationType(juce::Justification::centredLeft);
        releaseLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextSecondary));

        // Makeup gain slider
        addAndMakeVisible(makeupGainSlider);
        makeupGainSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        makeupGainSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        makeupGainSlider.setRange(-20.0, 20.0, 0.1);
        makeupGainSlider.setValue(0.0);
        makeupGainSlider.setTextValueSuffix(" dB");
        makeupGainSlider.onValueChange = [this] { onMakeupGainChanged(); };

        addAndMakeVisible(makeupGainLabel);
        makeupGainLabel.setText("Makeup:", juce::dontSendNotification);
        makeupGainLabel.setJustificationType(juce::Justification::centredLeft);
        makeupGainLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextSecondary));

        updateFromMixer();
        setSize(350, 220);
    }

    ~CompressorEditorComponent() override
    {
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(FlamColors::Surface));

        auto header = getLocalBounds().removeFromTop(32).toFloat();
        g.setColour(juce::Colour(FlamColors::AccentGreen).withAlpha(0.12f));
        g.fillRect(header);
        g.setColour(juce::Colour(FlamColors::AccentGreen).withAlpha(0.6f));
        g.fillRect(header.removeFromBottom(1.5f));

        g.setColour(juce::Colour(FlamColors::TextPrimary));
        g.setFont(FlamType::labelBold());
        g.drawText("COMPRESSOR", getLocalBounds().removeFromTop(32), juce::Justification::centred);
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

        // Ratio row
        auto ratioRow = bounds.removeFromTop(rowHeight);
        ratioLabel.setBounds(ratioRow.removeFromLeft(labelWidth));
        ratioSlider.setBounds(ratioRow);
        bounds.removeFromTop(rowSpacing);

        // Attack row
        auto attackRow = bounds.removeFromTop(rowHeight);
        attackLabel.setBounds(attackRow.removeFromLeft(labelWidth));
        attackSlider.setBounds(attackRow);
        bounds.removeFromTop(rowSpacing);

        // Release row
        auto releaseRow = bounds.removeFromTop(rowHeight);
        releaseLabel.setBounds(releaseRow.removeFromLeft(labelWidth));
        releaseSlider.setBounds(releaseRow);
        bounds.removeFromTop(rowSpacing);

        // Makeup gain row
        auto makeupRow = bounds.removeFromTop(rowHeight);
        makeupGainLabel.setBounds(makeupRow.removeFromLeft(labelWidth));
        makeupGainSlider.setBounds(makeupRow);
    }

private:
    void onThresholdChanged()
    {
        float threshold = static_cast<float>(thresholdSlider.getValue());

        if (isMasterCompressor)
            mixerRef.setMasterCompressorThreshold(threshold);
        else
            mixerRef.setChannelCompressorThreshold(channelIndex, threshold);
    }

    void onRatioChanged()
    {
        float ratio = static_cast<float>(ratioSlider.getValue());

        if (isMasterCompressor)
            mixerRef.setMasterCompressorRatio(ratio);
        else
            mixerRef.setChannelCompressorRatio(channelIndex, ratio);
    }

    void onAttackChanged()
    {
        float attack = static_cast<float>(attackSlider.getValue());

        if (isMasterCompressor)
            mixerRef.setMasterCompressorAttack(attack);
        else
            mixerRef.setChannelCompressorAttack(channelIndex, attack);
    }

    void onReleaseChanged()
    {
        float release = static_cast<float>(releaseSlider.getValue());

        if (isMasterCompressor)
            mixerRef.setMasterCompressorRelease(release);
        else
            mixerRef.setChannelCompressorRelease(channelIndex, release);
    }

    void onMakeupGainChanged()
    {
        float makeup = static_cast<float>(makeupGainSlider.getValue());

        if (isMasterCompressor)
            mixerRef.setMasterCompressorMakeupGain(makeup);
        else
            mixerRef.setChannelCompressorMakeupGain(channelIndex, makeup);
    }

    void updateFromMixer()
    {
        float threshold = isMasterCompressor ? mixerRef.getMasterCompressorThreshold()
                                              : mixerRef.getChannelCompressorThreshold(channelIndex);
        thresholdSlider.setValue(threshold, juce::dontSendNotification);

        float ratio = isMasterCompressor ? mixerRef.getMasterCompressorRatio()
                                          : mixerRef.getChannelCompressorRatio(channelIndex);
        ratioSlider.setValue(ratio, juce::dontSendNotification);

        float attack = isMasterCompressor ? mixerRef.getMasterCompressorAttack()
                                           : mixerRef.getChannelCompressorAttack(channelIndex);
        attackSlider.setValue(attack, juce::dontSendNotification);

        float release = isMasterCompressor ? mixerRef.getMasterCompressorRelease()
                                            : mixerRef.getChannelCompressorRelease(channelIndex);
        releaseSlider.setValue(release, juce::dontSendNotification);

        float makeup = isMasterCompressor ? mixerRef.getMasterCompressorMakeupGain()
                                           : mixerRef.getChannelCompressorMakeupGain(channelIndex);
        makeupGainSlider.setValue(makeup, juce::dontSendNotification);
    }

    Mixer& mixerRef;
    int channelIndex;
    bool isMasterCompressor;

    juce::Slider thresholdSlider;
    juce::Label thresholdLabel;

    juce::Slider ratioSlider;
    juce::Label ratioLabel;

    juce::Slider attackSlider;
    juce::Label attackLabel;

    juce::Slider releaseSlider;
    juce::Label releaseLabel;

    juce::Slider makeupGainSlider;
    juce::Label makeupGainLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompressorEditorComponent)
};

} // namespace flam
