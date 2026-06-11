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
#include "LimiterEditorComponent.h"
#include "VerticalFader.h"
#include "../Core/Mixer.h"

namespace flam {

/**
 * @brief Master channel strip with full FX controls
 *
 * Displays:
 * - Master label
 * - Volume slider
 * - Peak meter
 * - FX buttons (EQ, SAT, COMP, LIMITER)
 * - All controls are always active (never disabled)
 */
class MasterChannelStripComponent : public juce::Component, private juce::Timer
{
public:
    MasterChannelStripComponent(Mixer& mixer)
        : mixerRef(mixer)
    {
        // Master label
        addAndMakeVisible(nameLabel);
        nameLabel.setFont(juce::Font(11.0f, juce::Font::bold));
        nameLabel.setJustificationType(juce::Justification::centred);
        nameLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::AccentBlue));
        nameLabel.setText("MASTER", juce::dontSendNotification);

        // Volume fader (visual)
        addAndMakeVisible(volumeFader);
        volumeFader.setRange(-96.0, 6.0);
        volumeFader.setValue(0.0, false);
        volumeFader.setTextValueSuffix(" dB");
        volumeFader.onValueChange = [this](double value) {
            mixerRef.setMasterVolume(static_cast<float>(value));
        };

        // Peak meter
        addAndMakeVisible(peakMeter);

        // FX buttons with power toggles
        addAndMakeVisible(eqButton);
        eqButton.onEnabledChanged = [this](bool enabled) {
            mixerRef.setMasterEQEnabled(enabled);
        };
        eqButton.onEditorRequested = [this] { showEQEditor(); };

        addAndMakeVisible(saturationButton);
        saturationButton.onEnabledChanged = [this](bool enabled) {
            mixerRef.setMasterSaturationEnabled(enabled);
        };
        saturationButton.onEditorRequested = [this] { showSaturationEditor(); };

        addAndMakeVisible(compressorButton);
        compressorButton.onEnabledChanged = [this](bool enabled) {
            mixerRef.setMasterCompressorEnabled(enabled);
        };
        compressorButton.onEditorRequested = [this] { showCompressorEditor(); };

        addAndMakeVisible(limiterButton);
        limiterButton.onEnabledChanged = [this](bool enabled) {
            mixerRef.setMasterLimiterEnabled(enabled);
        };
        limiterButton.onEditorRequested = [this] { showLimiterEditor(); };

        // Start timer for meter updates
        startTimerHz(30);

        // Update from mixer state
        updateFromMixer();
    }

    ~MasterChannelStripComponent() override
    {
        stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        // Master strip — slightly elevated from regular channels, blue left accent
        g.setColour(juce::Colour(FlamColors::Interactive));
        g.fillAll();

        // Blue left accent line to distinguish master from channel strips
        g.setColour(juce::Colour(FlamColors::AccentBlue));
        g.fillRect(0, 0, 2, getHeight());
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(6);

        // Master label at top
        nameLabel.setBounds(bounds.removeFromTop(30));

        bounds.removeFromTop(5);

        // FX buttons (4 rows: EQ, Saturation, Compressor, Limiter)
        eqButton.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(2);
        saturationButton.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(2);
        compressorButton.setBounds(bounds.removeFromTop(20));
        bounds.removeFromTop(2);
        limiterButton.setBounds(bounds.removeFromTop(20));

        bounds.removeFromTop(10);

        // Peak meter (tall)
        auto meterBounds = bounds.removeFromTop(120);
        peakMeter.setBounds(meterBounds);

        bounds.removeFromTop(10);

        // Volume fader (visual, remaining space)
        volumeFader.setBounds(bounds);
    }

private:
    void timerCallback() override
    {
        // Update meter from mixer
        float level = mixerRef.getMasterPeakLevel();
        peakMeter.setPeakLevel(level);

        // Check if clipped
        if (level > 1.0f)
            peakMeter.setClipped(true);
    }


    void showEQEditor()
    {
        // Create master EQ editor (isMaster = true)
        auto* eqEditor = new EQEditorComponent(mixerRef, 0, true);

        // Show in CallOutBox popover near the EQ button
        auto& callOutBox = juce::CallOutBox::launchAsynchronously(
            std::unique_ptr<juce::Component>(eqEditor),
            eqButton.getScreenBounds(),
            nullptr
        );

        callOutBox.setArrowSize(8.0f);
    }

    void showSaturationEditor()
    {
        // Create master saturation editor (isMaster = true)
        auto* satEditor = new SaturationEditorComponent(mixerRef, 0, true);

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
        // Create master compressor editor (isMaster = true)
        auto* compEditor = new CompressorEditorComponent(mixerRef, 0, true);

        // Show in CallOutBox popover near the Compressor button
        auto& callOutBox = juce::CallOutBox::launchAsynchronously(
            std::unique_ptr<juce::Component>(compEditor),
            compressorButton.getScreenBounds(),
            nullptr
        );

        callOutBox.setArrowSize(8.0f);
    }

    void showLimiterEditor()
    {
        // Create limiter editor with threshold and release controls
        auto* limiterEditor = new LimiterEditorComponent(mixerRef);

        // Show in CallOutBox popover near the Limiter button
        auto& callOutBox = juce::CallOutBox::launchAsynchronously(
            std::unique_ptr<juce::Component>(limiterEditor),
            limiterButton.getScreenBounds(),
            nullptr
        );

        callOutBox.setArrowSize(8.0f);
    }

    void updateFromMixer()
    {
        // Update UI from mixer state
        volumeFader.setValue(mixerRef.getMasterVolume(), false);
        eqButton.setEnabled(mixerRef.isMasterEQEnabled());
        saturationButton.setEnabled(mixerRef.isMasterSaturationEnabled());
        compressorButton.setEnabled(mixerRef.isMasterCompressorEnabled());
        limiterButton.setEnabled(mixerRef.isMasterLimiterEnabled());
    }

    Mixer& mixerRef;

    juce::Label nameLabel;
    VerticalFader volumeFader;
    FXButtonComponent eqButton{"EQ", juce::Colour(0xff4a9eff)};
    FXButtonComponent saturationButton{"SAT", juce::Colour(0xffff8c42)};
    FXButtonComponent compressorButton{"COMP", juce::Colour(0xff6fdc6f)};
    FXButtonComponent limiterButton{"LIM", juce::Colour(0xffff4444)};
    PeakMeter peakMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterChannelStripComponent)
};

} // namespace flam
