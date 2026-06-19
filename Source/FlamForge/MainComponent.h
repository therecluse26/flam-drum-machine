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
// ForgeMeter — timer-free segmented LED input level meter.
//
// Driven by ForgeContent's existing 30 Hz juce::Timer via tick(); has no
// internal timer so it adds zero scheduling overhead per strip.
// ---------------------------------------------------------------------------
class ForgeMeter : public juce::Component
{
public:
    ForgeMeter() = default;

    // Called at 30 Hz from ForgeContent. Applies ballistic decay and repaints.
    void tick (float rawDb) noexcept
    {
        // Fast attack, ~0.5 s decay to silence at 30 Hz (matches PeakMeter).
        constexpr float kDecay = 0.85f;
        displayDb = rawDb > displayDb ? rawDb : displayDb * kDecay + (-100.0f) * (1.0f - kDecay);
        if (rawDb >= 0.0f) clipLatch = true;
        repaint();
    }

    void resetClip() { clipLatch = false; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff14171c));
        g.fillRoundedRectangle (b, 2.0f);

        // 14 px clip strip
        const float clipH = 14.0f;
        auto clipR = b.removeFromTop (clipH).reduced (1.0f, 0.0f);
        g.setColour (clipLatch ? juce::Colour (0xffd0473f) : juce::Colour (0xff23272e));
        g.fillRoundedRectangle (clipR, 2.0f);
        if (clipLatch)
        {
            g.setColour (juce::Colours::white);
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText ("CLIP", clipR, juce::Justification::centred);
        }
        b.removeFromTop (2.0f);

        // Segmented LED bars — ported from Source/UI/PeakMeter.h
        const float segH   = 3.0f, segGap = 1.0f;
        const int   numSegs = static_cast<int> (b.getHeight() / (segH + segGap));
        const float norm    = juce::jlimit (0.0f, 1.0f, juce::jmap (displayDb, -60.0f, 6.0f, 0.0f, 1.0f));
        const int   litSegs = static_cast<int> (norm * numSegs);
        const int greenTop  = static_cast<int> (juce::jmap (-12.0f, -60.0f, 6.0f, 0.0f, 1.0f) * numSegs);
        const int yellowTop = static_cast<int> (juce::jmap ( -3.0f, -60.0f, 6.0f, 0.0f, 1.0f) * numSegs);
        const float segW    = b.getWidth() - 4.0f;
        const float segX    = b.getX() + 2.0f;

        for (int i = 0; i < numSegs; ++i)
        {
            const float segY = b.getBottom() - (i + 1) * (segH + segGap) + segGap;
            const bool  lit  = i < litSegs;
            juce::Colour col;
            if      (i < greenTop)  col = juce::Colour (0xff3ecf6b);
            else if (i < yellowTop) col = juce::Colour (0xffe0b341);
            else                    col = juce::Colour (0xffd0473f);
            g.setColour (lit ? col : col.withAlpha (0.10f));
            g.fillRoundedRectangle ({segX, segY, segW, segH}, 1.0f);
        }
    }

private:
    float displayDb{-100.0f};
    bool  clipLatch{false};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeMeter)
};

// ---------------------------------------------------------------------------
// ForgeChannelStrip — editable label + vertical meter for one input channel.
// No internal timer; driven by ChannelStripRow::tick().
// ---------------------------------------------------------------------------
class ForgeChannelStrip : public juce::Component
{
public:
    std::function<void (const juce::String&)> onLabelChanged;

    ForgeChannelStrip()
    {
        label.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1b1f25));
        label.setColour (juce::TextEditor::textColourId, juce::Colours::white.withAlpha (0.85f));
        label.setFont (juce::Font (juce::FontOptions (11.0f)));
        label.setJustification (juce::Justification::centred);
        label.onTextChange = [this] { if (onLabelChanged) onLabelChanged (label.getText()); };
        addAndMakeVisible (label);
        addAndMakeVisible (meter);
    }

    void setLabel (const juce::String& t)
    {
        label.setText (t, juce::dontSendNotification);
    }

    juce::String getLabel() const { return label.getText(); }

    void tick (float rawDb) { meter.tick (rawDb); }
    void resetClip()        { meter.resetClip(); }

    void resized() override
    {
        auto b = getLocalBounds().reduced (2, 0);
        label.setBounds (b.removeFromTop (20));
        b.removeFromTop (2);
        meter.setBounds (b);
    }

    void paint (juce::Graphics& g) override
    {
        // Subtle right-edge separator
        g.setColour (juce::Colour (0xff23272e));
        g.fillRect (getLocalBounds().withLeft (getWidth() - 1));
    }

private:
    juce::TextEditor label;
    ForgeMeter       meter;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeChannelStrip)
};

// ---------------------------------------------------------------------------
// ChannelStripRow — horizontal, scrollable row of up to 16 ForgeChannelStrip.
//
// ForgeContent owns the label model (std::vector<juce::String> channelLabels).
// rebuild() seeds strips from that vector and wires onLabelChanged back so
// edits keep the model current. tick() is called at 30 Hz from ForgeContent.
// ---------------------------------------------------------------------------
class ChannelStripRow : public juce::Component
{
public:
    static constexpr int kStripW = 76;  // fixed strip width; 16 strips = 1216 px → horizontal scroll

    ChannelStripRow()
    {
        // inner declared before viewport → viewport destroyed first (see member order below).
        viewport.setViewedComponent (&inner, /*ownedByViewport=*/false);
        viewport.setScrollBarsShown (/*vertical=*/false, /*horizontal=*/true);
        addAndMakeVisible (viewport);
    }

    void rebuild (int n,
                  const std::vector<juce::String>& labelSource,
                  std::function<void (int, juce::String)> onLabelChanged)
    {
        strips.clear();   // deletes old strips; each removes itself from inner
        for (int c = 0; c < n; ++c)
        {
            auto* s = strips.add (new ForgeChannelStrip());
            s->setLabel (c < (int) labelSource.size()
                         ? labelSource[(size_t) c]
                         : "Mic " + juce::String (c + 1));
            const int idx = c;
            s->onLabelChanged = [idx, onLabelChanged] (const juce::String& t)
            {
                onLabelChanged (idx, t);
            };
            inner.addAndMakeVisible (s);
        }
        layoutInner();
    }

    // Pump live input levels for all strips. Called at 30 Hz from ForgeContent.
    void tick (CaptureEngine& eng)
    {
        for (int c = 0; c < strips.size(); ++c)
            strips[c]->tick (eng.channelLevelDb (c));
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        layoutInner();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff14171c));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }

private:
    void layoutInner()
    {
        const int n    = strips.size();
        const int innerW = juce::jmax (viewport.getWidth(), n * kStripW);
        inner.setSize (innerW, viewport.getHeight());
        int x = 0;
        for (auto* s : strips)
        {
            s->setBounds (x, 0, kStripW, inner.getHeight());
            x += kStripW;
        }
    }

    // Declare inner before viewport — C++ reverse-destruction order ensures
    // the viewport (and its raw contentComp pointer) is destroyed first.
    juce::Component              inner;
    juce::OwnedArray<ForgeChannelStrip> strips;
    juce::Viewport               viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripRow)
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
    // kDeviceH / kStatsH / kCoverageH are now computed dynamically in resized().
    static constexpr int kPieceH   = 34;
    static constexpr int kRecordH  = 56;
    static constexpr int kStripsH  = 90;   // per-channel label + meter row
    static constexpr int kDestH    = 28;   // destination row (path editor + Browse)
    static constexpr int kBrowserH = 296;  // embedded folder browser (260 list + 4 gap + 32 actions)
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
        addAndMakeVisible (stripRow);

        // Persist export destination across sessions.
        juce::PropertiesFile::Options propOpts;
        propOpts.applicationName     = "FlamForge";
        propOpts.filenameSuffix      = ".settings";
        propOpts.osxLibrarySubFolder = "Application Support";
        appProps.setStorageParameters (propOpts);

        // --- destination row (always visible — no modal dependency) ---------------
        destLabel.setText ("Export to:", juce::dontSendNotification);
        destLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (destLabel);

        destPathEditor.setText (loadPersistedDest().getFullPathName(), false);
        destPathEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1b1f25));
        destPathEditor.setTooltip ("Type or paste the export destination folder path");
        destPathEditor.onReturnKey = [this] { persistDestPath(); };
        destPathEditor.onFocusLost = [this] { persistDestPath(); };
        addAndMakeVisible (destPathEditor);

        configureButton (browseBtn, "Browse...");
        browseBtn.onClick = [this] { onBrowse(); };

        // Embedded folder browser — appears in-window when no native picker is available.
        configureButton (chooseFolderBtn, "Use This Folder");
        chooseFolderBtn.onClick = [this] { onChooseFolder(); };
        chooseFolderBtn.setVisible (false);

        configureButton (cancelBrowseBtn, "Cancel");
        cancelBrowseBtn.onClick = [this] { hideBrowser(); };
        cancelBrowseBtn.setVisible (false);

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
        // Use jlimit lower bounds for the three elastic sections so the window
        // opens at a sensible minimum without over-sizing on small screens.
        return kPad + kTitleH + kSubH + kGap
             + 160 + kGap   // deviceH lower bound
             + kPieceH + kGap
             + kRecordH + kGap
             + 80 + kGap    // statsH lower bound
             + 48 + kGap    // coverageH lower bound
             + kStripsH + kGap
             + kDestH + kGap
             + (browserVisible ? kBrowserH + kGap : 0)
             + kExportH + kGap
             + kStatusH + kGap
             + kRevealH + kPad;
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0f12)); }

    void resized() override
    {
        // Pass 1 — sum all fixed-height sections and padding.
        const int fixedTotal =
            2 * kPad
            + kTitleH + kSubH + kGap
            + kPieceH + kGap
            + kRecordH + kGap
            + kStripsH + kGap
            + kDestH + kGap
            + (browserVisible ? kBrowserH + kGap : 0)
            + kExportH + kGap
            + kStatusH + kGap
            + kRevealH;

        // Pass 2 — distribute remaining height proportionally to elastic sections.
        const int flexAvail  = juce::jmax (0, getHeight() - fixedTotal);
        const int deviceH    = juce::jlimit (160, 340, 50 + flexAvail * 55 / 100);
        const int statsH     = juce::jlimit (80,  160, flexAvail * 30 / 100);
        const int coverageH  = juce::jlimit (48,  100, flexAvail * 15 / 100);

        // Store for scroll-position calculation in updateBrowserLayout().
        m_deviceH   = deviceH;
        m_statsH    = statsH;
        m_coverageH = coverageH;

        auto area = getLocalBounds().reduced (kPad, kPad);

        title.setBounds    (area.removeFromTop (kTitleH));
        subtitle.setBounds (area.removeFromTop (kSubH));
        area.removeFromTop (kGap);

        deviceSelector->setBounds (area.removeFromTop (deviceH));
        area.removeFromTop (kGap);

        {
            auto row = area.removeFromTop (kPieceH);
            // Change 4 — proportional piece bar widths clamped to safe pixel ranges.
            const int labelW = juce::jlimit (70,  90,  row.getWidth() / 8);
            const int addW   = juce::jlimit (60,  80,  row.getWidth() / 9);
            const int navW   = juce::jlimit (28,  38,  row.getWidth() / 20);
            const int countW = juce::jlimit (80,  100, row.getWidth() / 7);
            pieceLabel.setBounds (row.removeFromLeft (labelW));
            addBtn.setBounds     (row.removeFromRight (addW));
            row.removeFromRight (6);
            nextBtn.setBounds    (row.removeFromRight (navW));
            pieceCount.setBounds (row.removeFromRight (countW));
            prevBtn.setBounds    (row.removeFromRight (navW));
            row.removeFromRight (8);
            pieceName.setBounds  (row);
        }
        area.removeFromTop (kGap);

        recordBtn.setBounds (area.removeFromTop (kRecordH));
        area.removeFromTop (kGap);
        stats.setBounds    (area.removeFromTop (statsH));
        area.removeFromTop (kGap);
        coverage.setBounds (area.removeFromTop (coverageH));
        area.removeFromTop (kGap);
        stripRow.setBounds (area.removeFromTop (kStripsH));
        area.removeFromTop (kGap);
        {
            auto row = area.removeFromTop (kDestH);
            // Change 5 — proportional destination row widths.
            const int destLabelW = juce::jlimit (60, 90, row.getWidth() / 7);
            const int browseBtnW = juce::jlimit (70, 90, row.getWidth() / 7);
            destLabel.setBounds (row.removeFromLeft (destLabelW));
            row.removeFromLeft (4);
            if (browseBtn.isVisible())
            {
                browseBtn.setBounds (row.removeFromRight (browseBtnW));
                row.removeFromRight (4);
            }
            destPathEditor.setBounds (row);
        }
        area.removeFromTop (kGap);
        if (browserVisible && fileBrowser != nullptr)
        {
            fileBrowser->setBounds    (area.removeFromTop (kBrowserH - 36));
            area.removeFromTop (4);
            auto actRow = area.removeFromTop (32);
            chooseFolderBtn.setBounds (actRow.removeFromLeft (actRow.getWidth() / 2 - 4));
            actRow.removeFromLeft (8);
            cancelBrowseBtn.setBounds (actRow);
            area.removeFromTop (kGap);
        }
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
        provisionalPeaks_.clear();
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
            provisionalPeaks_.clear();  // superseded by final drainNewHits() results
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
        // 1. Drain immediate onset events → provisional coverage update.
        //    One peakDb per strike, published at the audio-thread instant of
        //    detection — before the 600ms window assembles. Updates the meter
        //    within one tick (~33ms) rather than waiting 300-600ms.
        if (isRecording())
        {
            for (float peakDb : engine.drainProvisionalOnsets())
                provisionalPeaks_.push_back (peakDb);
        }

        // 2. Drain authoritative 600ms windows. Each new auth hit reconciles
        //    one provisional entry (FIFO order), preventing double-count.
        auto newHits = engine.drainNewHits();
        if (isRecording() && ! newHits.empty())
        {
            auto& piece = currentPiece();
            for (auto& h : newHits)
            {
                piece.hits.push_back (std::move (h));
                if (! provisionalPeaks_.empty())
                    provisionalPeaks_.erase (provisionalPeaks_.begin());
            }
        }
        recompute();

        // Rebuild strips when the device channel count changes; pump levels every tick.
        const int n = engine.channelCount();
        if (n != lastChannelCount)
        {
            while ((int) channelLabels.size() < n)
                channelLabels.push_back ("Mic " + juce::String ((int) channelLabels.size() + 1));
            lastChannelCount = n;
            stripRow.rebuild (n, channelLabels,
                [this] (int c, juce::String t)
                {
                    if (c < (int) channelLabels.size())
                        channelLabels[(size_t) c] = t;
                });
            resized();
        }
        stripRow.tick (engine);
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

        // Build velocity list: authoritative hits + still-provisional onsets.
        // Provisional entries use the same retroactive mapping as auth hits so
        // the bin assignment is consistent; they're replaced once the 600ms
        // window publishes and reconcileswith the FIFO-ordered pop in timerCallback.
        std::vector<int> vels;
        vels.reserve (piece.hits.size() + provisionalPeaks_.size());
        for (const auto& h : piece.hits)
            vels.push_back (h.midiVelocity);
        for (float peakDb : provisionalPeaks_)
            vels.push_back (hasRange ? mapPeakToVelocity (peakDb, softDb, loudDb) : 100);
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

        const int totalVisible = (int) piece.hits.size() + (int) provisionalPeaks_.size();
        stats.setStats (totalVisible, hasRange, softDb, loudDb,
                        layers, rr, coverage.binsFilled(), CoverageMeter::kNumBins);

        if (isRecording())
        {
            const bool hasProvisional = ! provisionalPeaks_.empty();
            setStatus ("Recording \"" + piece.name + "\" — "
                       + juce::String (totalVisible) + " hit(s)"
                       + (hasProvisional ? " (" + juce::String ((int) provisionalPeaks_.size()) + " live)" : "")
                       + ". Click STOP when coverage looks good.");
        }
    }

    // --- export (the one remaining explicit action besides Record) ---------
    void onExport()
    {
        stopRecording();

        bool anyHits = false;
        for (const auto& p : captures)
            if (! p.hits.empty()) { anyHits = true; break; }
        if (! anyHits) { setStatus ("Nothing to export yet - record some hits first."); return; }

        const juce::String pathText = destPathEditor.getText().trim();
        juce::File destDir = pathText.isNotEmpty() ? juce::File (pathText) : buildDefaultDest();

        if (! destDir.isDirectory() && ! destDir.createDirectory())
        {
            setStatus ("Cannot create folder: " + destDir.getFullPathName());
            return;
        }

        persistDestPath();

        setStatus ("Exporting to " + destDir.getFullPathName() + " ...");
        const ExportResult res = exportKit (kitName, captures, options, destDir, channelLabels);
        setStatus (res.message);

        if (res.ok)
        {
            lastExportedKit = res.kitYaml.getParentDirectory();
            revealBtn.setVisible (true);
        }
    }

    juce::File buildDefaultDest()
    {
        auto musicDir = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (! musicDir.isDirectory())
            musicDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        return musicDir.getChildFile ("FlamForgeKits");
    }

    juce::File loadPersistedDest()
    {
        if (auto* props = appProps.getUserSettings())
        {
            const juce::String last = props->getValue ("lastExportDir");
            if (last.isNotEmpty())
            {
                juce::File f (last);
                if (f.isDirectory())
                    return f;
            }
        }
        return buildDefaultDest();
    }

    void persistDestPath()
    {
        if (auto* props = appProps.getUserSettings())
            props->setValue ("lastExportDir", destPathEditor.getText().trim());
    }

    void onBrowse()
    {
        if (browserVisible)
        {
            hideBrowser();
            return;
        }

        const juce::String pathText = destPathEditor.getText().trim();
        const juce::File startDir = pathText.isNotEmpty() && juce::File (pathText).isDirectory()
                                   ? juce::File (pathText)
                                   : buildDefaultDest();

        if (juce::FileChooser::isPlatformDialogAvailable())
        {
            // Native dialog available (macOS, Windows, Linux with zenity/kdialog).
            chooser = std::make_unique<juce::FileChooser> (
                "Choose export destination folder", startDir, "");

            chooser->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                [this] (const juce::FileChooser& fc)
                {
                    const auto results = fc.getResults();
                    if (! results.isEmpty() && results[0].isDirectory())
                    {
                        destPathEditor.setText (results[0].getFullPathName(), false);
                        persistDestPath();
                    }
                });
            return;
        }

        // No native dialog backend. Show a juce::FileBrowserComponent embedded directly
        // in this window — zero external dependencies, works on any platform.
        if (fileBrowser == nullptr)
        {
            fileBrowser = std::make_unique<juce::FileBrowserComponent> (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                startDir, nullptr, nullptr);
            addAndMakeVisible (*fileBrowser);
        }
        else
        {
            fileBrowser->setRoot (startDir);
            fileBrowser->setVisible (true);
        }

        chooseFolderBtn.setVisible (true);
        cancelBrowseBtn.setVisible (true);
        browseBtn.setButtonText ("Hide Browser");
        browserVisible = true;
        updateBrowserLayout();
    }

    void hideBrowser()
    {
        browserVisible = false;
        if (fileBrowser != nullptr)
            fileBrowser->setVisible (false);
        chooseFolderBtn.setVisible (false);
        cancelBrowseBtn.setVisible (false);
        browseBtn.setButtonText ("Browse...");
        updateBrowserLayout();
    }

    void onChooseFolder()
    {
        if (fileBrowser != nullptr)
        {
            const juce::File chosen = fileBrowser->getRoot();
            if (chosen.isDirectory())
            {
                destPathEditor.setText (chosen.getFullPathName(), false);
                persistDestPath();
                setStatus ("Destination: " + chosen.getFullPathName());
            }
        }
        hideBrowser();
    }

    void updateBrowserLayout()
    {
        // Trigger parent (MainComponent) to resize the viewport content.
        if (auto* parent = getParentComponent())
            parent->resized();
        resized();

        // When opening the browser, scroll the viewport so the browser is visible.
        if (browserVisible)
        {
            if (auto* vp = dynamic_cast<juce::Viewport*> (getParentComponent()))
            {
                // Use the last computed elastic heights so the scroll target
                // tracks the actual layout rather than stale constants.
                const int destRowY = kPad + kTitleH + kSubH + kGap
                                   + m_deviceH + kGap
                                   + kPieceH + kGap
                                   + kRecordH + kGap
                                   + m_statsH + kGap
                                   + m_coverageH + kGap
                                   + kStripsH + kGap;
                vp->setViewPosition (0, destRowY);
            }
        }
    }

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    juce::Label       title, subtitle, pieceLabel, pieceCount, status, destLabel;
    juce::TextEditor  pieceName, destPathEditor;
    juce::TextButton  prevBtn, nextBtn, addBtn, recordBtn, exportBtn, revealBtn,
                      browseBtn, chooseFolderBtn, cancelBrowseBtn;
    StatsPanel        stats;
    CoverageMeter     coverage;
    ChannelStripRow   stripRow;

    // Label model for the per-channel strip row. Survives device re-inits and
    // is the single source of truth for export (FLA-139 will read this vector).
    std::vector<juce::String> channelLabels;
    int                       lastChannelCount = 0;

    CaptureEngine             engine;
    std::vector<PieceCapture> captures;
    int                       currentPieceIndex = 0;
    // Immediate onset peaks not yet matched to an authoritative 600ms window.
    std::vector<float>        provisionalPeaks_;
    SynthOptions              options;
    juce::String              kitName = "FlamForge Kit";

    std::unique_ptr<juce::FileChooser>        chooser;
    std::unique_ptr<juce::FileBrowserComponent> fileBrowser;
    juce::ApplicationProperties               appProps;
    juce::File                                lastExportedKit;
    bool                                      browserVisible = false;

    // Last computed elastic section heights; updated every resized() call so
    // updateBrowserLayout() can produce an accurate scroll offset.
    int m_deviceH   = 160;
    int m_statsH    = 80;
    int m_coverageH = 48;

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
        viewport.setScrollBarsShown (true, true);
        addAndMakeVisible (viewport);
        // Open tall enough to show everything (clamped so it fits typical screens).
        setSize (760, juce::jmin (content.naturalHeight(), 940));
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0f12)); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        const int w = viewport.getMaximumVisibleWidth();
        const int contentW = juce::jmax (640, w);
        content.setSize (contentW, juce::jmax (content.naturalHeight(), viewport.getHeight()));
    }

private:
    juce::Viewport viewport;
    ForgeContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace flamforge
