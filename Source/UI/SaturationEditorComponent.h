// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Core/Mixer.h"

namespace flam {

/**
 * @brief Saturation Editor with mode selection and amount control
 *
 * Displays:
 * - Mode dropdown (Tape/Tube/Digital)
 * - Amount slider (0.0 to 1.0)
 *
 * Enable/disable handled by power button in FXButtonComponent.
 * Used in CallOutBox popovers from SAT buttons.
 */
class SaturationEditorComponent : public juce::Component
{
public:
    SaturationEditorComponent(Mixer& mixer, int channelIdx, bool isMaster = false)
        : mixerRef(mixer)
        , channelIndex(channelIdx)
        , isMasterSaturation(isMaster)
    {
        // Mode selector dropdown
        addAndMakeVisible(modeSelector);
        modeSelector.addItem("Tape", 1);
        modeSelector.addItem("Tube", 2);
        modeSelector.addItem("Digital", 3);
        modeSelector.setSelectedId(1);  // Default to Tape
        modeSelector.onChange = [this] { onModeChanged(); };

        // Mode label
        addAndMakeVisible(modeLabel);
        modeLabel.setText("Mode:", juce::dontSendNotification);
        modeLabel.setJustificationType(juce::Justification::centredLeft);
        modeLabel.setColour(juce::Label::textColourId, juce::Colours::white);

        // Amount slider
        addAndMakeVisible(amountSlider);
        amountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        amountSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        amountSlider.setRange(0.0, 1.0, 0.01);
        amountSlider.setValue(0.5);
        amountSlider.onValueChange = [this] { onAmountChanged(); };

        // Amount label
        addAndMakeVisible(amountLabel);
        amountLabel.setText("Amount:", juce::dontSendNotification);
        amountLabel.setJustificationType(juce::Justification::centredLeft);
        amountLabel.setColour(juce::Label::textColourId, juce::Colours::white);

        // Update UI from mixer state
        updateFromMixer();

        setSize(300, 120);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff2a2a2a));

        // Title
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText("Saturation", getLocalBounds().removeFromTop(30), juce::Justification::centred);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10);

        // Title takes top space (handled in paint())
        bounds.removeFromTop(30);
        bounds.removeFromTop(10);

        // Mode selector row
        auto modeRow = bounds.removeFromTop(25);
        modeLabel.setBounds(modeRow.removeFromLeft(70));
        modeSelector.setBounds(modeRow);

        bounds.removeFromTop(10);

        // Amount slider row
        auto amountRow = bounds.removeFromTop(25);
        amountLabel.setBounds(amountRow.removeFromLeft(70));
        amountSlider.setBounds(amountRow);
    }

private:
    void onModeChanged()
    {
        int selectedId = modeSelector.getSelectedId();

        // Convert UI selector ID (1, 2, 3) to mode value (0=Tape, 1=Tube, 2=Digital)
        int modeValue = selectedId - 1;

        if (isMasterSaturation)
            mixerRef.setMasterSaturationMode(modeValue);
        else
            mixerRef.setChannelSaturationMode(channelIndex, modeValue);
    }

    void onAmountChanged()
    {
        float amount = static_cast<float>(amountSlider.getValue());

        if (isMasterSaturation)
            mixerRef.setMasterSaturationAmount(amount);
        else
            mixerRef.setChannelSaturationAmount(channelIndex, amount);
    }

    void updateFromMixer()
    {
        int modeValue = isMasterSaturation
            ? mixerRef.getMasterSaturationMode()
            : mixerRef.getChannelSaturationMode(channelIndex);

        // Convert mode value (0=Tape, 1=Tube, 2=Digital) to UI selector ID (1, 2, 3)
        int modeId = modeValue + 1;
        modeSelector.setSelectedId(modeId, juce::dontSendNotification);

        float amount = isMasterSaturation ? mixerRef.getMasterSaturationAmount()
                                           : mixerRef.getChannelSaturationAmount(channelIndex);
        amountSlider.setValue(amount, juce::dontSendNotification);
    }

    Mixer& mixerRef;
    int channelIndex;
    bool isMasterSaturation;

    juce::ComboBox modeSelector;
    juce::Label modeLabel;
    juce::Slider amountSlider;
    juce::Label amountLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SaturationEditorComponent)
};

} // namespace flam
