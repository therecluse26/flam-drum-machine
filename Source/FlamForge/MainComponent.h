// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "CaptureEngine.h"
#include "CaptureTypes.h"
#include "LayerSynth.h"
#include "KitExporter.h"

#include <array>
#include <vector>

namespace flamforge
{

// ---------------------------------------------------------------------------
// CoverageMeter — the "fingerprint registration" coverage view.
//
// Every captured hit is bucketed into one of kNumBins velocity bins (1..127
// split into equal ranges). Each bin is coloured by how many hits landed in it:
//   red    0-1 hits  (insufficient)
//   yellow 2-5 hits  (usable)
//   green  6+  hits  (ideal round-robin depth)
// Call setHits() from the capture timer with the live per-hit MIDI velocities.
// ---------------------------------------------------------------------------
class CoverageMeter : public juce::Component
{
public:
    static constexpr int kNumBins = 16;

    // Feed the meter the current set of captured velocities (1..127). Recomputes
    // per-bin counts and repaints. Cheap to call at timer rate.
    void setHits (const std::vector<int>& velocities)
    {
        counts.fill (0);
        for (int v : velocities)
        {
            const int clamped = juce::jlimit (1, 127, v);
            int bin = (clamped - 1) * kNumBins / 127;
            bin = juce::jlimit (0, kNumBins - 1, bin);
            ++counts[(size_t) bin];
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff14171c));
        g.fillRoundedRectangle (r, 4.0f);

        const float gap = 3.0f;
        const float binW = (r.getWidth() - gap * (kNumBins - 1)) / (float) kNumBins;

        int total = 0;
        for (int i = 0; i < kNumBins; ++i)
        {
            auto bin = juce::Rectangle<float> (r.getX() + i * (binW + gap), r.getY(), binW, r.getHeight());

            const int n = counts[(size_t) i];
            total += n;

            juce::Colour c;
            if (n >= 6)      c = juce::Colour (0xff3ecf6b);   // green  — ideal
            else if (n >= 2) c = juce::Colour (0xffe0b341);   // yellow — usable
            else if (n >= 1) c = juce::Colour (0xffd0473f);   // red    — thin
            else             c = juce::Colour (0xff23272e);   // empty

            g.setColour (c);
            g.fillRoundedRectangle (bin, 2.0f);
        }

        if (total == 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText ("velocity coverage (no hits yet)",
                        getLocalBounds(), juce::Justification::centred);
        }
    }

private:
    std::array<int, kNumBins> counts { {} };
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
class ForgeContent : public juce::Component,
                     private juce::Timer
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

        // Step buttons — each drives a real stage of the capture pipeline.
        configureButton (calibrateBtn, "1.  Calibrate dynamic range   -   Strike softest, then hardest");
        calibrateBtn.onClick = [this] { onCalibrate(); };

        configureButton (recordBtn, "2.  Record hits   -   Play freely; hits auto-bin by loudness");
        recordBtn.onClick = [this] { onRecord(); };

        configureButton (synthBtn, "3.  Build velocity layers   -   Layer synthesis + round-robin");
        synthBtn.onClick = [this] { onSynth(); };

        configureButton (exportBtn, "4.  Export flamkit.yaml   -   Round-tripped through FlamKitLoader");
        exportBtn.onClick = [this] { onExport(); };

        status.setText ("Ready. Select an input device above, then Calibrate.",
                        juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, juce::Colours::aquamarine.withAlpha (0.85f));
        status.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (status);

        // The capture engine owns the input side. Register it as the device
        // callback so it receives audio whenever a device is open.
        deviceManager.addAudioCallback (&engine);

        // Start with a single piece to capture into.
        captures.push_back ({});

        startTimerHz (30);
    }

    ~ForgeContent() override
    {
        stopTimer();
        deviceManager.removeAudioCallback (&engine);
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
    void configureButton (juce::TextButton& b, const juce::String& label)
    {
        b.setButtonText (label);
        addAndMakeVisible (b);
    }

    // --- the current piece being captured ---------------------------------
    PieceCapture& currentPiece() { return captures[(size_t) currentPieceIndex]; }

    void setStatus (const juce::String& text)
    {
        status.setText (text, juce::dontSendNotification);
    }

    // --- live capture pump (message thread, ~30Hz) ------------------------
    void timerCallback() override
    {
        auto newHits = engine.drainNewHits();

        const auto mode = engine.getMode();

        if (mode == CaptureEngine::Mode::Recording && ! newHits.empty())
        {
            auto& piece = currentPiece();
            for (auto& h : newHits)
                piece.hits.push_back (std::move (h));

            setStatus ("Recording \"" + piece.name + "\" - captured "
                       + juce::String ((int) piece.hits.size()) + " hits.");
        }
        else if (mode == CaptureEngine::Mode::CalibrateSoft
              || mode == CaptureEngine::Mode::CalibrateLoud)
        {
            // Live feedback of the measured peak while calibrating.
            const float db = engine.lastCalibratedDb();
            const juce::String which = (mode == CaptureEngine::Mode::CalibrateSoft)
                                           ? "softest" : "loudest";
            if (db > -99.0f)
                setStatus ("Calibrating " + which + " - peak "
                           + juce::String (db, 1) + " dBFS. Click Calibrate when ready.");
        }

        refreshCoverage();
    }

    void refreshCoverage()
    {
        std::vector<int> vels;
        const auto& piece = currentPiece();
        vels.reserve (piece.hits.size());
        for (const auto& h : piece.hits)
            vels.push_back (h.midiVelocity);
        coverage.setHits (vels);
    }

    // --- step 1: Calibrate ------------------------------------------------
    // Cycles CalibrateSoft -> CalibrateLoud -> Idle. After the loud pass the
    // calibration is marked valid so recorded hits map to real velocities.
    void onCalibrate()
    {
        switch (engine.getMode())
        {
            case CaptureEngine::Mode::Idle:
            case CaptureEngine::Mode::Recording:
                engine.setMode (CaptureEngine::Mode::CalibrateSoft);
                setStatus ("Calibrating SOFTEST hit - strike the drum as quietly as you will play, "
                           "then click Calibrate again.");
                break;

            case CaptureEngine::Mode::CalibrateSoft:
                engine.setMode (CaptureEngine::Mode::CalibrateLoud);
                setStatus ("Soft = " + juce::String (engine.calibration().softestDb, 1)
                           + " dBFS. Now strike the HARDEST hit, then click Calibrate again.");
                break;

            case CaptureEngine::Mode::CalibrateLoud:
            default:
            {
                auto& c = engine.calibration();
                if (c.loudestDb > c.softestDb)
                    c.valid = true;
                engine.setMode (CaptureEngine::Mode::Idle);
                setStatus (c.valid
                    ? ("Calibrated: " + juce::String (c.softestDb, 1) + " .. "
                       + juce::String (c.loudestDb, 1) + " dBFS. Ready to Record.")
                    : juce::String ("Calibration incomplete (loud must exceed soft). Try Calibrate again."));
                break;
            }
        }
    }

    // --- step 2: Record ---------------------------------------------------
    void onRecord()
    {
        if (engine.getMode() == CaptureEngine::Mode::Recording)
        {
            engine.setMode (CaptureEngine::Mode::Idle);
            setStatus ("Stopped. \"" + currentPiece().name + "\" has "
                       + juce::String ((int) currentPiece().hits.size()) + " hits captured.");
        }
        else
        {
            if (! engine.calibration().valid)
                setStatus ("Recording without calibration - velocities default to mid. "
                           "Calibrate first for accurate mapping.");
            engine.setMode (CaptureEngine::Mode::Recording);
        }
    }

    // --- step 3: Build velocity layers ------------------------------------
    void onSynth()
    {
        const auto& piece = currentPiece();
        if (piece.hits.empty())
        {
            setStatus ("No hits captured for \"" + piece.name + "\" yet - Record some first.");
            return;
        }

        const flam::DrumPiece out = synthesizePiece (piece, options);

        int layerCount = 0;
        int rrGroups   = 0;
        if (! out.articulations.empty())
        {
            const auto& layers = out.articulations[0].layers;
            layerCount = (int) layers.size();
            int maxRr = -1;
            for (const auto& l : layers)
                maxRr = juce::jmax (maxRr, l.roundRobinGroup);
            rrGroups = maxRr + 1;
        }

        if (layerCount == 0)
            setStatus ("Synthesis dropped all " + juce::String ((int) piece.hits.size())
                       + " hits as duds. Capture stronger, more varied hits.");
        else
            setStatus ("Built " + juce::String (layerCount) + " velocity layers (up to "
                       + juce::String (rrGroups) + " round-robins) from "
                       + juce::String ((int) piece.hits.size()) + " hits.");
    }

    // --- step 4: Export ---------------------------------------------------
    void onExport()
    {
        // Only export pieces that actually have hits.
        bool anyHits = false;
        for (const auto& p : captures)
            if (! p.hits.empty()) { anyHits = true; break; }

        if (! anyHits)
        {
            setStatus ("Nothing to export - no hits captured yet.");
            return;
        }

        auto musicDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (! musicDir.isDirectory())
            musicDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        const juce::File destDir = musicDir.getChildFile ("FlamForgeKits");

        setStatus ("Exporting kit \"" + kitName + "\" to " + destDir.getFullPathName() + " ...");

        const ExportResult result = exportKit (kitName, captures, options, destDir);
        setStatus (result.message);
    }

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    juce::Label title, subtitle, status;
    juce::TextButton calibrateBtn, recordBtn, synthBtn, exportBtn;
    CoverageMeter coverage;

    // --- capture / synthesis / export state -------------------------------
    CaptureEngine            engine;
    std::vector<PieceCapture> captures;       // one per drum piece
    int                       currentPieceIndex = 0;
    SynthOptions              options;
    juce::String              kitName = "FlamForge Kit";

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
