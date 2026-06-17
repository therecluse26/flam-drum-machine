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
#include <cmath>
#include <vector>

namespace flamforge
{

// Map a measured peak (dBFS) to a MIDI velocity 1..127 against an observed
// soft/loud range. This is the "retroactive calibration": the range is derived
// from the hits already recorded, so there is no separate calibrate step.
inline int mapPeakToVelocity (float db, float softDb, float loudDb)
{
    if (loudDb <= softDb)
        return 100;
    const float t = juce::jlimit (0.0f, 1.0f, (db - softDb) / (loudDb - softDb));
    return juce::jlimit (1, 127, (int) std::round (1.0f + t * 126.0f));
}

// ---------------------------------------------------------------------------
// CoverageMeter — the "fingerprint registration" view.
//
// Each captured hit is bucketed into one of kNumBins velocity bins (1..127).
// Bins are coloured by how many hits landed in them:
//   red 1, yellow 2-5, green 6+ (ideal round-robin depth); empty = grey.
// ---------------------------------------------------------------------------
class CoverageMeter : public juce::Component
{
public:
    static constexpr int kNumBins = 16;

    void setHits (const std::vector<int>& velocities)
    {
        counts.fill (0);
        filled = 0;
        for (int v : velocities)
        {
            int bin = (juce::jlimit (1, 127, v) - 1) * kNumBins / 127;
            bin = juce::jlimit (0, kNumBins - 1, bin);
            ++counts[(size_t) bin];
        }
        for (int n : counts) if (n > 0) ++filled;
        repaint();
    }

    int binsFilled() const { return filled; }

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
            g.drawText ("soft  <  velocity coverage  >  hard   (play hits to fill)",
                        getLocalBounds(), juce::Justification::centred);
        }
    }

private:
    std::array<int, kNumBins> counts { {} };
    int filled = 0;
};

// ---------------------------------------------------------------------------
// StatsPanel — live, automatically-derived readouts. Everything here is
// computed from the captured hits, not entered by the user.
// ---------------------------------------------------------------------------
class StatsPanel : public juce::Component
{
public:
    void setStats (int hits, bool hasRange, float softDb, float loudDb,
                   int layers, int rr, int binsFilled, int numBins)
    {
        lines.clear();
        lines.add ("Hits captured:     " + juce::String (hits));
        lines.add ("Dynamic range:     " + (hasRange
                      ? juce::String (softDb, 1) + " .. " + juce::String (loudDb, 1) + " dBFS  (auto)"
                      : juce::String ("play 2+ hits to detect")));
        lines.add ("Velocity layers:   " + (layers > 0
                      ? juce::String (layers) + "  x up to " + juce::String (rr) + " round-robins"
                      : juce::String ("--")));
        lines.add ("Coverage:          " + juce::String (binsFilled) + " / " + juce::String (numBins)
                      + " bins"  + (binsFilled >= numBins ? "  (full)" : ""));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff14171c));
        g.fillRoundedRectangle (r, 6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Monospaced")));

        auto area = getLocalBounds().reduced (14, 10);
        const int lh = area.getHeight() / juce::jmax (1, lines.size());
        for (auto& l : lines)
            g.drawText (l, area.removeFromTop (lh), juce::Justification::centredLeft);
    }

private:
    juce::StringArray lines;
};

// ---------------------------------------------------------------------------
// ForgeContent — the scrolled content column.
//
// Record-centric flow: pick a piece, hit Record, play. Calibration (dynamic
// range) and velocity-layer synthesis are derived automatically and shown live
// in the StatsPanel + CoverageMeter; the only explicit actions are Record and
// Export. Lives inside a Viewport because the AudioDeviceSelectorComponent
// positions its rows absolutely and does not clip to its bounds.
// ---------------------------------------------------------------------------
class ForgeContent : public juce::Component,
                     private juce::Timer
{
public:
    static constexpr int kPad      = 22;
    static constexpr int kTitleH   = 38;
    static constexpr int kSubH     = 20;
    static constexpr int kDeviceH  = 210;   // input device + input channels (advanced/output hidden)
    static constexpr int kPieceH   = 34;
    static constexpr int kRecordH  = 56;
    static constexpr int kStatsH   = 104;
    static constexpr int kCoverageH= 58;
    static constexpr int kExportH  = 44;
    static constexpr int kStatusH  = 22;
    static constexpr int kRevealH  = 28;
    static constexpr int kGap      = 12;

    ForgeContent()
    {
        // Recorder: we need inputs, not outputs. Hiding output + advanced rows
        // keeps the device panel compact so the window isn't dominated by it.
        deviceManager.initialiseWithDefaultDevices (/*inputs=*/2, /*outputs=*/0);
        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            deviceManager,
            /*minInput=*/1, /*maxInput=*/16,
            /*minOutput=*/0, /*maxOutput=*/0,
            /*showMidiIn=*/false, /*showMidiOut=*/false,
            /*stereoPairs=*/false, /*hideAdvanced=*/true);
        addAndMakeVisible (*deviceSelector);

        title.setText ("FlamForge", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (32.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (title);

        subtitle.setText ("Play each drum from soft to hard - layers build themselves.",
                          juce::dontSendNotification);
        subtitle.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        addAndMakeVisible (subtitle);

        // --- piece bar -----------------------------------------------------
        pieceLabel.setText ("Drum piece:", juce::dontSendNotification);
        pieceLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (pieceLabel);

        pieceName.setText ("Kick", juce::dontSendNotification);
        pieceName.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1b1f25));
        pieceName.onTextChange = [this] { currentPiece().name = pieceName.getText(); };
        addAndMakeVisible (pieceName);

        configureButton (prevBtn, "<");      prevBtn.onClick = [this] { switchPiece (-1); };
        configureButton (nextBtn, ">");      nextBtn.onClick = [this] { switchPiece (+1); };
        configureButton (addBtn,  "+ Add");  addBtn.onClick  = [this] { addPiece(); };

        pieceCount.setJustificationType (juce::Justification::centred);
        pieceCount.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (pieceCount);

        // --- record / export ----------------------------------------------
        recordBtn.onClick = [this] { toggleRecord(); };
        addAndMakeVisible (recordBtn);

        addAndMakeVisible (stats);
        addAndMakeVisible (coverage);

        // Persist export destination across sessions.
        juce::PropertiesFile::Options propOpts;
        propOpts.applicationName     = "FlamForge";
        propOpts.filenameSuffix      = ".settings";
        propOpts.osxLibrarySubFolder = "Application Support";
        appProps.setStorageParameters (propOpts);

        configureButton (exportBtn, "Export kit  (flamkit.yaml + samples)");
        exportBtn.onClick = [this] { onExport(); };

        // Reveal button — hidden until a successful export.
        configureButton (revealBtn, "Reveal in file manager");
        revealBtn.setVisible (false);
        revealBtn.onClick = [this] {
            if (lastExportedKit != juce::File{})
                lastExportedKit.revealToUser();
        };

        status.setColour (juce::Label::textColourId, juce::Colours::aquamarine.withAlpha (0.85f));
        status.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (status);

        deviceManager.addAudioCallback (&engine);
        captures.push_back ({});            // first piece
        captures[0].name = "Kick";

        refreshAll();
        setStatus ("Ready. Choose your input above, name the drum, then Record.");
        startTimerHz (30);
    }

    ~ForgeContent() override
    {
        stopTimer();
        deviceManager.removeAudioCallback (&engine);
    }

    int naturalHeight() const
    {
        return kPad + kTitleH + kSubH + kGap
             + kDeviceH + kGap
             + kPieceH + kGap
             + kRecordH + kGap
             + kStatsH + kGap
             + kCoverageH + kGap
             + kExportH + kGap
             + kStatusH + kGap
             + kRevealH + kPad;
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0f12)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (kPad, kPad);

        title.setBounds    (area.removeFromTop (kTitleH));
        subtitle.setBounds (area.removeFromTop (kSubH));
        area.removeFromTop (kGap);

        deviceSelector->setBounds (area.removeFromTop (kDeviceH));
        area.removeFromTop (kGap);

        {
            auto row = area.removeFromTop (kPieceH);
            pieceLabel.setBounds (row.removeFromLeft (84));
            addBtn.setBounds     (row.removeFromRight (70));
            row.removeFromRight (6);
            nextBtn.setBounds    (row.removeFromRight (34));
            pieceCount.setBounds (row.removeFromRight (90));
            prevBtn.setBounds    (row.removeFromRight (34));
            row.removeFromRight (8);
            pieceName.setBounds  (row);
        }
        area.removeFromTop (kGap);

        recordBtn.setBounds (area.removeFromTop (kRecordH));
        area.removeFromTop (kGap);
        stats.setBounds (area.removeFromTop (kStatsH));
        area.removeFromTop (kGap);
        coverage.setBounds (area.removeFromTop (kCoverageH));
        area.removeFromTop (kGap);
        exportBtn.setBounds (area.removeFromTop (kExportH));
        area.removeFromTop (kGap);
        status.setBounds (area.removeFromTop (kStatusH));
        area.removeFromTop (kGap);
        revealBtn.setBounds (area.removeFromTop (kRevealH));
    }

private:
    void configureButton (juce::TextButton& b, const juce::String& label)
    {
        b.setButtonText (label);
        addAndMakeVisible (b);
    }

    PieceCapture& currentPiece() { return captures[(size_t) currentPieceIndex]; }

    void setStatus (const juce::String& t) { status.setText (t, juce::dontSendNotification); }

    bool isRecording() const { return engine.getMode() == CaptureEngine::Mode::Recording; }

    // --- piece management --------------------------------------------------
    void addPiece()
    {
        stopRecording();
        captures.push_back ({});
        captures.back().name = "Piece " + juce::String ((int) captures.size());
        currentPieceIndex = (int) captures.size() - 1;
        refreshAll();
    }

    void switchPiece (int delta)
    {
        stopRecording();
        currentPieceIndex = juce::jlimit (0, (int) captures.size() - 1, currentPieceIndex + delta);
        refreshAll();
    }

    void refreshAll()
    {
        pieceName.setText (currentPiece().name, juce::dontSendNotification);
        pieceCount.setText (juce::String (currentPieceIndex + 1) + " / "
                            + juce::String ((int) captures.size()), juce::dontSendNotification);
        updateRecordButton();
        recompute();
    }

    // --- record toggle -----------------------------------------------------
    void toggleRecord()
    {
        if (isRecording()) stopRecording();
        else
        {
            engine.setMode (CaptureEngine::Mode::Recording);
            updateRecordButton();
            setStatus ("Recording \"" + currentPiece().name + "\" - play it soft to hard.");
        }
    }

    void stopRecording()
    {
        if (isRecording())
        {
            engine.setMode (CaptureEngine::Mode::Idle);
            setStatus ("Stopped. \"" + currentPiece().name + "\" has "
                       + juce::String ((int) currentPiece().hits.size()) + " hits.");
        }
        updateRecordButton();
    }

    void updateRecordButton()
    {
        const bool rec = isRecording();
        recordBtn.setButtonText (rec ? "STOP recording" : "RECORD  -  play this drum");
        recordBtn.setColour (juce::TextButton::buttonColourId,
                             rec ? juce::Colour (0xffd0473f) : juce::Colour (0xff2f7d4f));
    }

    // --- live pump (30Hz): drain hits, re-derive everything ----------------
    void timerCallback() override
    {
        auto newHits = engine.drainNewHits();
        if (isRecording() && ! newHits.empty())
        {
            auto& piece = currentPiece();
            for (auto& h : newHits)
                piece.hits.push_back (std::move (h));
        }
        recompute();
    }

    // Recompute retroactive calibration + velocities + live layer preview.
    void recompute()
    {
        auto& piece = currentPiece();

        float softDb = 1.0e9f, loudDb = -1.0e9f;
        for (const auto& h : piece.hits)
        {
            softDb = juce::jmin (softDb, h.peakDb);
            loudDb = juce::jmax (loudDb, h.peakDb);
        }
        const bool hasRange = piece.hits.size() >= 2 && loudDb > softDb;

        // Retroactive velocity assignment from the observed range.
        for (auto& h : piece.hits)
            h.midiVelocity = hasRange ? mapPeakToVelocity (h.peakDb, softDb, loudDb) : 100;

        std::vector<int> vels;
        vels.reserve (piece.hits.size());
        for (const auto& h : piece.hits) vels.push_back (h.midiVelocity);
        coverage.setHits (vels);

        int layers = 0, rr = 0;
        if (! piece.hits.empty())
        {
            const auto out = synthesizePiece (piece, options);
            if (! out.articulations.empty())
            {
                const auto& L = out.articulations[0].layers;
                layers = (int) L.size();
                int m = -1;
                for (const auto& l : L) m = juce::jmax (m, l.roundRobinGroup);
                rr = m + 1;
            }
        }

        stats.setStats ((int) piece.hits.size(), hasRange, softDb, loudDb,
                        layers, rr, coverage.binsFilled(), CoverageMeter::kNumBins);

        if (isRecording())
            setStatus ("Recording \"" + piece.name + "\" - "
                       + juce::String ((int) piece.hits.size()) + " hits. Click STOP when coverage looks good.");
    }

    // --- export (the one remaining explicit action besides Record) ---------
    void onExport()
    {
        stopRecording();

        bool anyHits = false;
        for (const auto& p : captures)
            if (! p.hits.empty()) { anyHits = true; break; }
        if (! anyHits) { setStatus ("Nothing to export yet - record some hits first."); return; }

        auto musicDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (! musicDir.isDirectory())
            musicDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        const juce::File defaultDest = musicDir.getChildFile ("FlamForgeKits");

        // Use the last-chosen directory as the starting point if still valid.
        juce::File startDir = defaultDest;
        if (auto* props = appProps.getUserSettings())
        {
            const juce::String last = props->getValue ("lastExportDir");
            if (last.isNotEmpty())
            {
                const juce::File lastDir (last);
                if (lastDir.isDirectory())
                    startDir = lastDir;
            }
        }

        chooser = std::make_unique<juce::FileChooser> (
            "Choose export destination folder", startDir, "");

        chooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, defaultDest] (const juce::FileChooser& fc)
            {
                // If the user cancelled, fall back to the default destination.
                juce::File destDir = defaultDest;
                const auto results = fc.getResults();
                if (! results.isEmpty() && results[0].isDirectory())
                {
                    destDir = results[0];
                    if (auto* props = appProps.getUserSettings())
                        props->setValue ("lastExportDir", destDir.getFullPathName());
                }

                setStatus ("Exporting to " + destDir.getFullPathName() + " ...");
                const ExportResult res = exportKit (kitName, captures, options, destDir);
                setStatus (res.message);

                if (res.ok)
                {
                    lastExportedKit = res.kitYaml.getParentDirectory();
                    revealBtn.setVisible (true);
                }
            });
    }

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    juce::Label       title, subtitle, pieceLabel, pieceCount, status;
    juce::TextEditor  pieceName;
    juce::TextButton  prevBtn, nextBtn, addBtn, recordBtn, exportBtn, revealBtn;
    StatsPanel        stats;
    CoverageMeter     coverage;

    CaptureEngine             engine;
    std::vector<PieceCapture> captures;
    int                       currentPieceIndex = 0;
    SynthOptions              options;
    juce::String              kitName = "FlamForge Kit";

    std::unique_ptr<juce::FileChooser> chooser;
    juce::ApplicationProperties        appProps;
    juce::File                         lastExportedKit;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeContent)
};

// ---------------------------------------------------------------------------
// MainComponent — hosts the content in a Viewport, sized so the whole layout
// is visible on open (no cut-off); the viewport only scrolls on small screens.
// ---------------------------------------------------------------------------
class MainComponent : public juce::Component
{
public:
    MainComponent()
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        // Open tall enough to show everything (clamped so it fits typical screens).
        setSize (760, juce::jmin (content.naturalHeight(), 940));
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0f12)); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        const int w = viewport.getMaximumVisibleWidth();
        content.setSize (w, juce::jmax (content.naturalHeight(), viewport.getHeight()));
    }

private:
    juce::Viewport viewport;
    ForgeContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace flamforge
