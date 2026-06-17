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
        g.drawText ("velocity coverage (empty - capture not wired yet)",
                    getLocalBounds(), juce::Justification::centred);
    }
};

// ---------------------------------------------------------------------------
// ForgeContent — the scrolled content column.
//
// Laid out top-to-bottom with FIXED row heights and a known natural height, so
// it can live inside a Viewport. This matters because JUCE's
// AudioDeviceSelectorComponent positions its rows at absolute offsets and does
// not clip to its bounds — give it too little height and its lower rows draw
// on top of whatever follows. Reserving its true height + scrolling the column
// is what keeps the layout from colliding.
// ---------------------------------------------------------------------------
class ForgeContent : public juce::Component
{
public:
    // Row heights (px). kDeviceH is generous enough for the default 2 device
    // rows + 2 channel list-boxes + sample-rate + buffer-size rows.
    static constexpr int kPad      = 24;
    static constexpr int kTitleH   = 44;
    static constexpr int kSubH     = 22;
    static constexpr int kDeviceH  = 360;
    static constexpr int kStepH    = 44;
    static constexpr int kCoverageH= 46;
    static constexpr int kStatusH  = 24;
    static constexpr int kGap      = 10;

    ForgeContent()
    {
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

        subtitle.setText ("Kit Recording Tool - record a complete flamkit by playing it",
                          juce::dontSendNotification);
        subtitle.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        addAndMakeVisible (subtitle);

        addAndMakeVisible (coverage);

        configureStep (calibrateBtn, "1.  Calibrate dynamic range",
                       "Strike softest, then hardest", "FLA-120");
        configureStep (recordBtn,    "2.  Record hits",
                       "Play freely; hits auto-bin by loudness", "FLA-120 / FLA-121");
        configureStep (synthBtn,     "3.  Build velocity layers",
                       "Timbre-aware layer synthesis + round-robin", "FLA-122");
        configureStep (exportBtn,    "4.  Export flamkit.yaml",
                       "Round-tripped through FlamKitLoader", "FLA-124");

        status.setText ("Ready. Select an input device above to begin.", juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, juce::Colours::aquamarine.withAlpha (0.85f));
        status.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (status);
    }

    // Total height this column needs — used by MainComponent to size the
    // viewport's content so the whole layout is reachable by scrolling.
    int naturalHeight() const
    {
        return kPad + kTitleH + kSubH + kGap
             + kDeviceH + kGap
             + 4 * (kStepH + 8)
             + kGap + kCoverageH + kGap + kStatusH
             + kPad;
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0f12)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (kPad, kPad);

        title.setBounds (area.removeFromTop (kTitleH));
        subtitle.setBounds (area.removeFromTop (kSubH));
        area.removeFromTop (kGap);

        deviceSelector->setBounds (area.removeFromTop (kDeviceH));
        area.removeFromTop (kGap);

        for (auto* b : { &calibrateBtn, &recordBtn, &synthBtn, &exportBtn })
        {
            b->setBounds (area.removeFromTop (kStepH));
            area.removeFromTop (8);
        }

        area.removeFromTop (kGap - 8);
        coverage.setBounds (area.removeFromTop (kCoverageH));
        area.removeFromTop (kGap);
        status.setBounds (area.removeFromTop (kStatusH));
    }

private:
    void configureStep (juce::TextButton& b, const juce::String& label,
                        const juce::String& hint, const juce::String& owningIssue)
    {
        b.setButtonText (label + "   -   " + hint);
        b.onClick = [this, owningIssue]
        {
            status.setText ("Not implemented yet - tracked on " + owningIssue
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeContent)
};

// ---------------------------------------------------------------------------
// MainComponent — hosts the content column in a scrolling Viewport so the UI
// stays correct (no overlap) at any window size.
// ---------------------------------------------------------------------------
class MainComponent : public juce::Component
{
public:
    MainComponent()
    {
        viewport.setViewedComponent (&content, false);  // content not owned (member)
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        setSize (820, 700);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0f12)); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        // Fill the viewport width (minus the scrollbar); reserve full natural
        // height so every row is reachable by scrolling.
        const int w = viewport.getMaximumVisibleWidth();
        content.setSize (w, juce::jmax (content.naturalHeight(), viewport.getHeight()));
    }

private:
    juce::Viewport viewport;
    ForgeContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace flamforge
