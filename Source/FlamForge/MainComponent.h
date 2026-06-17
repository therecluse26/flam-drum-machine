// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace flamforge
{

// ---------------------------------------------------------------------------
// CoverageMeter — placeholder for the "fingerprint registration" coverage view.
//
// The finished widget (FLA-121) maps every captured hit into a perceptual
// loudness bin and colours each bin red/yellow/green by round-robin depth.
// This skeleton draws the empty bin grid so the workflow reads correctly; the
// live capture feed that fills it is dispatched on FLA-120/FLA-121.
// ---------------------------------------------------------------------------
class CoverageMeter : public juce::Component
{
public:
    static constexpr int kNumBins = 16;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff14171c));
        g.fillRoundedRectangle (r, 4.0f);

        const float gap = 3.0f;
        const float binW = (r.getWidth() - gap * (kNumBins - 1)) / (float) kNumBins;

        for (int i = 0; i < kNumBins; ++i)
        {
            auto bin = juce::Rectangle<float> (r.getX() + i * (binW + gap), r.getY(), binW, r.getHeight());
            // Empty = not yet sampled. Live coverage colouring arrives with FLA-121.
            g.setColour (juce::Colour (0xff23272e));
            g.fillRoundedRectangle (bin, 2.0f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("velocity coverage (empty — capture not wired yet)",
                    getLocalBounds(), juce::Justification::centred);
    }
};

// ---------------------------------------------------------------------------
// MainComponent — FlamForge's top-level UI.
//
// Walking skeleton: the audio-device selector is fully live (you can pick an
// interface and input channels right now), and the fingerprint-registration
// workflow is laid out end to end. The capture engine (FLA-120), coverage
// scoring (FLA-121), layer synthesis (FLA-122) and YAML export (FLA-124) are
// stubbed — each button reports which issue owns the real implementation.
// ---------------------------------------------------------------------------
class MainComponent : public juce::Component
{
public:
    MainComponent()
    {
        // 2 input channels min so the device opens with capture available;
        // FlamForge supports up to 16 input channels for multi-mic kits.
        deviceManager.initialiseWithDefaultDevices (/*inputs=*/2, /*outputs=*/2);

        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            deviceManager,
            /*minInput=*/1, /*maxInput=*/16,
            /*minOutput=*/0, /*maxOutput=*/2,
            /*showMidiIn=*/false, /*showMidiOut=*/false,
            /*stereoPairs=*/false, /*hideAdvanced=*/false);
        addAndMakeVisible (*deviceSelector);

        title.setText ("FlamForge", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (34.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (title);

        subtitle.setText ("Kit Recording Tool  \xc2\xb7  record a complete flamkit by playing it",
                          juce::dontSendNotification);
        subtitle.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        addAndMakeVisible (subtitle);

        addAndMakeVisible (coverage);

        configureStep (calibrateBtn, "1 \xe2\x80\x94 Calibrate dynamic range",
                       "Strike softest, then hardest", "FLA-120");
        configureStep (recordBtn,    "2 \xe2\x80\x94 Record hits",
                       "Play freely; hits auto-bin by loudness", "FLA-120 / FLA-121");
        configureStep (synthBtn,     "3 \xe2\x80\x94 Build velocity layers",
                       "Timbre-aware layer synthesis + round-robin", "FLA-122");
        configureStep (exportBtn,    "4 \xe2\x80\x94 Export flamkit.yaml",
                       "Round-tripped through FlamKitLoader", "FLA-124");

        status.setText ("Ready. Select an input device above to begin.", juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, juce::Colours::aquamarine.withAlpha (0.85f));
        status.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (status);

        setSize (760, 640);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0d0f12));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (24);

        title.setBounds (area.removeFromTop (44));
        subtitle.setBounds (area.removeFromTop (24));
        area.removeFromTop (12);

        deviceSelector->setBounds (area.removeFromTop (220));
        area.removeFromTop (16);

        for (auto* b : { &calibrateBtn, &recordBtn, &synthBtn, &exportBtn })
        {
            b->setBounds (area.removeFromTop (40));
            area.removeFromTop (8);
        }

        area.removeFromTop (4);
        coverage.setBounds (area.removeFromTop (46));
        area.removeFromTop (8);
        status.setBounds (area.removeFromTop (24));
    }

private:
    void configureStep (juce::TextButton& b, const juce::String& label,
                        const juce::String& hint, const juce::String& owningIssue)
    {
        b.setButtonText (label + "   \xc2\xb7   " + hint);
        b.onClick = [this, owningIssue]
        {
            status.setText ("Not implemented yet \xe2\x80\x94 tracked on " + owningIssue
                                + ". This is the FlamForge skeleton (FLA-119).",
                            juce::dontSendNotification);
        };
        addAndMakeVisible (b);
    }

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    juce::Label title, subtitle, status;
    juce::TextButton calibrateBtn, recordBtn, synthBtn, exportBtn;
    CoverageMeter coverage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace flamforge
