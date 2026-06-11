// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FlamLookAndFeel.h"
#include "PeakMeter.h"
#include "FXButtonComponent.h"
#include "EQEditorComponent.h"
#include "SaturationEditorComponent.h"
#include "CompressorEditorComponent.h"
#include "VerticalFader.h"
#include "RotaryKnob.h"
#include "../Core/Mixer.h"

namespace flam {

/**
 * @brief Single channel strip with controls and metering
 *
 * Displays:
 * - Channel name
 * - Output routing selector (Main Mix / Bus 1-16)
 * - Volume slider
 * - Pan knob
 * - Solo/Mute buttons
 * - Peak meter
 *
 * Controls are disabled when routed to bus (not Main Mix).
 */
class ChannelStripComponent : public juce::Component, private juce::Timer
{
public:
    ChannelStripComponent(Mixer& mixer, int channelIndex)
        : mixerRef(mixer)
        , channelIdx(channelIndex)
    {
        // Channel name label
        addAndMakeVisible(nameLabel);
        nameLabel.setFont(juce::Font(11.0f, juce::Font::bold));
        nameLabel.setJustificationType(juce::Justification::centred);
        nameLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextPrimary));

        // Output selector dropdown
        addAndMakeVisible(outputSelector);
        outputSelector.addItem("Main Mix", 1);
        for (int i = 1; i <= 16; ++i)
            outputSelector.addItem("Bus " + juce::String(i), i + 1);

        outputSelector.setSelectedId(1);  // Default to Main Mix
        outputSelector.onChange = [this] { onOutputChanged(); };

        // Volume fader (visual)
        addAndMakeVisible(volumeFader);
        volumeFader.setRange(-96.0, 6.0);
        volumeFader.setValue(0.0, false);
        volumeFader.setTextValueSuffix(" dB");
        volumeFader.onValueChange = [this](double value) {
            mixerRef.setChannelVolume(channelIdx, static_cast<float>(value));
        };

        // Pan knob (visual)
        addAndMakeVisible(panKnob);
        panKnob.setLabel("Pan");
        panKnob.setRange(-1.0, 1.0);
        panKnob.setValue(0.0, false);
        panKnob.setTextValueSuffix("L/R");
        panKnob.onValueChange = [this](double value) {
            mixerRef.setChannelPan(channelIdx, static_cast<float>(value));
        };

        // Solo button
        addAndMakeVisible(soloButton);
        soloButton.setButtonText("S");
        soloButton.setClickingTogglesState(true);
        soloButton.onClick = [this] { onSoloClicked(); };
        soloButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(FlamColors::AccentYellow));
        soloButton.setColour(juce::TextButton::textColourOnId, juce::Colour(FlamColors::Background));

        // Mute button
        addAndMakeVisible(muteButton);
        muteButton.setButtonText("M");
        muteButton.setClickingTogglesState(true);
        muteButton.onClick = [this] { onMuteClicked(); };
        muteButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(FlamColors::AccentRed));
        muteButton.setColour(juce::TextButton::textColourOnId, juce::Colour(FlamColors::TextPrimary));

        // Peak meter
        addAndMakeVisible(peakMeter);

        // FX buttons with power toggles
        addAndMakeVisible(eqButton);
        eqButton.onEnabledChanged = [this](bool enabled) {
            mixerRef.setChannelEQEnabled(channelIdx, enabled);
        };
        eqButton.onEditorRequested = [this] { showEQEditor(); };

        addAndMakeVisible(saturationButton);
        saturationButton.onEnabledChanged = [this](bool enabled) {
            mixerRef.setChannelSaturationEnabled(channelIdx, enabled);
        };
        saturationButton.onEditorRequested = [this] { showSaturationEditor(); };

        addAndMakeVisible(compressorButton);
        compressorButton.onEnabledChanged = [this](bool enabled) {
            mixerRef.setChannelCompressorEnabled(channelIdx, enabled);
        };
        compressorButton.onEditorRequested = [this] { showCompressorEditor(); };

        // Start timer for meter updates
        startTimerHz(30);

        // Update from mixer state
        updateFromMixer();
    }

    ~ChannelStripComponent() override
    {
        stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(juce::Colour(FlamColors::Elevated));
        g.fillAll();

        // Right-side separator line between strips
        g.setColour(juce::Colour(FlamColors::BorderSubtle));
        g.drawLine((float)getWidth() - 0.5f, 4.0f, (float)getWidth() - 0.5f,
                   (float)getHeight() - 4.0f, 1.0f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(4);

        // Channel name at top
        nameLabel.setBounds(bounds.removeFromTop(25));

        bounds.removeFromTop(5);

        // Output selector
        outputSelector.setBounds(bounds.removeFromTop(25));

        bounds.removeFromTop(10);

        // FX buttons (3 rows: EQ, Saturation, Compressor)
        eqButton.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(2);
        saturationButton.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(2);
        compressorButton.setBounds(bounds.removeFromTop(20));

        bounds.removeFromTop(10);

        // Peak meter (tall)
        auto meterBounds = bounds.removeFromTop(120);  // Reduced from 150 to make room for FX
        peakMeter.setBounds(meterBounds);

        bounds.removeFromTop(10);

        // Solo and Mute buttons (side by side)
        auto buttonRow = bounds.removeFromTop(25);
        soloButton.setBounds(buttonRow.removeFromLeft(buttonRow.getWidth() / 2).reduced(2));
        muteButton.setBounds(buttonRow.reduced(2));

        bounds.removeFromTop(5);

        // Pan knob (visual rotary)
        panKnob.setBounds(bounds.removeFromTop(80));

        bounds.removeFromTop(5);

        // Volume fader (visual, remaining space)
        volumeFader.setBounds(bounds);
    }

private:
    void timerCallback() override
    {
        // Update meter from mixer
        float level = mixerRef.getChannelPeakLevel(channelIdx);
        peakMeter.setPeakLevel(level);

        // Check if clipped (would need to add to mixer API)
        // For now, just check if level > 1.0
        if (level > 1.0f)
            peakMeter.setClipped(true);
    }

    void onOutputChanged()
    {
        int selectedId = outputSelector.getSelectedId();

        Mixer::OutputDestination dest;
        if (selectedId == 1)
            dest = Mixer::OutputDestination::MainMix;
        else
            dest = static_cast<Mixer::OutputDestination>(selectedId - 1);

        mixerRef.setChannelOutput(channelIdx, dest);

        updateControlsEnabled();
    }


    void onSoloClicked()
    {
        mixerRef.setChannelSolo(channelIdx, soloButton.getToggleState());
    }

    void onMuteClicked()
    {
        mixerRef.setChannelMute(channelIdx, muteButton.getToggleState());
    }

    void showEQEditor()
    {
        // Create EQ editor component with 10 band sliders + enable toggle
        auto* eqEditor = new EQEditorComponent(mixerRef, channelIdx, false);

        // Show in CallOutBox popover near the EQ button
        auto& callOutBox = juce::CallOutBox::launchAsynchronously(
            std::unique_ptr<juce::Component>(eqEditor),
            eqButton.getScreenBounds(),
            nullptr  // parent component (nullptr = desktop)
        );

        // Optional: customize callout appearance
        callOutBox.setArrowSize(8.0f);
    }

    void showSaturationEditor()
    {
        // Create saturation editor with mode dropdown + amount slider + enable toggle
        auto* satEditor = new SaturationEditorComponent(mixerRef, channelIdx, false);

        // Show in CallOutBox popover near the Saturation button
        auto& callOutBox = juce::CallOutBox::launchAsynchronously(
            std::unique_ptr<juce::Component>(satEditor),
            saturationButton.getScreenBounds(),
            nullptr
        );

        callOutBox.setArrowSize(8.0f);
    }

    void showCompressorEditor()
    {
        // Create compressor editor with threshold/ratio/attack/release/makeup + enable toggle
        auto* compEditor = new CompressorEditorComponent(mixerRef, channelIdx, false);

        // Show in CallOutBox popover near the Compressor button
        auto& callOutBox = juce::CallOutBox::launchAsynchronously(
            std::unique_ptr<juce::Component>(compEditor),
            compressorButton.getScreenBounds(),
            nullptr
        );

        callOutBox.setArrowSize(8.0f);
    }

    void updateControlsEnabled()
    {
        bool isMainMix = (mixerRef.getChannelOutput(channelIdx) == Mixer::OutputDestination::MainMix);

        volumeFader.setEnabled(isMainMix);
        panKnob.setEnabled(isMainMix);
        soloButton.setEnabled(isMainMix);
        muteButton.setEnabled(isMainMix);
        eqButton.setEnabled(isMainMix);
        saturationButton.setEnabled(isMainMix);
        compressorButton.setEnabled(isMainMix);

        // Visual feedback for disabled state
        volumeFader.setAlpha(isMainMix ? 1.0f : 0.5f);
        panKnob.setAlpha(isMainMix ? 1.0f : 0.5f);
        soloButton.setAlpha(isMainMix ? 1.0f : 0.5f);
        muteButton.setAlpha(isMainMix ? 1.0f : 0.5f);
        eqButton.setAlpha(isMainMix ? 1.0f : 0.5f);
        saturationButton.setAlpha(isMainMix ? 1.0f : 0.5f);
        compressorButton.setAlpha(isMainMix ? 1.0f : 0.5f);
    }

    void updateFromMixer()
    {
        // Update UI from mixer state
        nameLabel.setText("Ch " + juce::String(channelIdx + 1), juce::dontSendNotification);

        volumeFader.setValue(mixerRef.getChannelVolume(channelIdx), false);
        panKnob.setValue(mixerRef.getChannelPan(channelIdx), false);
        soloButton.setToggleState(mixerRef.isChannelSolo(channelIdx), juce::dontSendNotification);
        muteButton.setToggleState(mixerRef.isChannelMute(channelIdx), juce::dontSendNotification);
        eqButton.setEnabled(mixerRef.isChannelEQEnabled(channelIdx));
        saturationButton.setEnabled(mixerRef.isChannelSaturationEnabled(channelIdx));
        compressorButton.setEnabled(mixerRef.isChannelCompressorEnabled(channelIdx));

        auto output = mixerRef.getChannelOutput(channelIdx);
        outputSelector.setSelectedId(static_cast<int>(output) + 1, juce::dontSendNotification);

        updateControlsEnabled();
    }

    Mixer& mixerRef;
    int channelIdx;

    juce::Label nameLabel;
    juce::ComboBox outputSelector;
    VerticalFader volumeFader;
    RotaryKnob panKnob;
    juce::TextButton soloButton;
    juce::TextButton muteButton;
    FXButtonComponent eqButton{"EQ", juce::Colour(0xff4a9eff)};
    FXButtonComponent saturationButton{"SAT", juce::Colour(0xffff8c42)};
    FXButtonComponent compressorButton{"COMP", juce::Colour(0xff6fdc6f)};
    PeakMeter peakMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStripComponent)
};

} // namespace flam
