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
// StatsPanel — live, automatically-derived readouts.
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
// SetupHeader — top zone: title + subtitle + audio device selector + channel strips.
//
// Owns title and subtitle; holds a non-owning pointer to the AudioDeviceSelectorComponent
// (owned by ForgeContent via unique_ptr). adoptDeviceSelector() must be called from
// ForgeContent's constructor body after the selector is created.
// ---------------------------------------------------------------------------
class SetupHeader : public juce::Component
{
public:
    ChannelStripRow stripRow;

    static constexpr int kTitleH  = 38;
    static constexpr int kSubH    = 20;
    static constexpr int kStripsH = 90;
    static constexpr int kGap     = 12;

    SetupHeader()
    {
        title.setFont (juce::Font (juce::FontOptions (32.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        title.setText ("FlamForge", juce::dontSendNotification);
        addAndMakeVisible (title);

        subtitle.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        subtitle.setText ("Play each drum from soft to hard - layers build themselves.",
                          juce::dontSendNotification);
        addAndMakeVisible (subtitle);

        addAndMakeVisible (stripRow);
    }

    // Called once from ForgeContent's constructor body after deviceSelector is created.
    // Adds the selector as a non-owned child so it renders inside this zone.
    void adoptDeviceSelector (juce::AudioDeviceSelectorComponent& sel)
    {
        deviceSel = &sel;
        addAndMakeVisible (sel);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        title.setBounds    (b.removeFromTop (kTitleH));
        subtitle.setBounds (b.removeFromTop (kSubH));
        b.removeFromTop (kGap);
        if (deviceSel != nullptr)
        {
            // Remainder minus gap and strips goes to device selector (elastic).
            const int devH = juce::jmax (0, b.getHeight() - kGap - kStripsH);
            deviceSel->setBounds (b.removeFromTop (devH));
            b.removeFromTop (kGap);
        }
        stripRow.setBounds (b.removeFromTop (kStripsH));
    }

private:
    juce::Label title, subtitle;
    juce::AudioDeviceSelectorComponent* deviceSel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetupHeader)
};

// ---------------------------------------------------------------------------
// PieceRail — left rail: drum piece navigation.
//
// Lays out vertically so it works as a narrow left column. Callbacks wired by
// ForgeContent; setDisplay() called on every piece switch or state refresh.
// ---------------------------------------------------------------------------
class PieceRail : public juce::Component
{
public:
    std::function<void (int)>                 onSwitch;      // delta: -1 or +1
    std::function<void()>                     onAdd;
    std::function<void (const juce::String&)> onNameChanged;

    PieceRail()
    {
        pieceLabel.setText ("Drum piece:", juce::dontSendNotification);
        pieceLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (pieceLabel);

        pieceName.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1b1f25));
        pieceName.setColour (juce::TextEditor::textColourId, juce::Colours::white.withAlpha (0.85f));
        pieceName.onTextChange = [this] { if (onNameChanged) onNameChanged (pieceName.getText()); };
        addAndMakeVisible (pieceName);

        configureButton (prevBtn, "<");     prevBtn.onClick = [this] { if (onSwitch) onSwitch (-1); };
        configureButton (nextBtn, ">");     nextBtn.onClick = [this] { if (onSwitch) onSwitch (+1); };
        configureButton (addBtn, "+ Add");  addBtn.onClick  = [this] { if (onAdd) onAdd(); };

        pieceCount.setJustificationType (juce::Justification::centred);
        pieceCount.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (pieceCount);
    }

    void setDisplay (int idx, int total, const juce::String& name)
    {
        pieceName.setText (name, juce::dontSendNotification);
        pieceCount.setText (juce::String (idx + 1) + " / " + juce::String (total),
                            juce::dontSendNotification);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6, 6);
        pieceLabel.setBounds (b.removeFromTop (18));
        b.removeFromTop (4);
        pieceName.setBounds (b.removeFromTop (24));
        b.removeFromTop (10);
        {
            auto nav = b.removeFromTop (28);
            const int navW = juce::jlimit (26, 36, nav.getWidth() / 5);
            prevBtn.setBounds    (nav.removeFromLeft (navW));
            nav.removeFromLeft (2);
            nextBtn.setBounds    (nav.removeFromRight (navW));
            pieceCount.setBounds (nav);
        }
        b.removeFromTop (8);
        addBtn.setBounds (b.removeFromTop (28));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff14171c));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }

private:
    void configureButton (juce::TextButton& b, const juce::String& lbl)
    {
        b.setButtonText (lbl);
        addAndMakeVisible (b);
    }

    juce::Label      pieceLabel, pieceCount;
    juce::TextEditor pieceName;
    juce::TextButton prevBtn, nextBtn, addBtn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PieceRail)
};

// ---------------------------------------------------------------------------
// CapturePanel — center zone: RECORD button + stats readout + coverage meter.
//
// Public members (stats, coverage) are accessed directly by ForgeContent's
// recompute() to update displayed data; no extra indirection needed.
// ---------------------------------------------------------------------------
class CapturePanel : public juce::Component
{
public:
    StatsPanel    stats;
    CoverageMeter coverage;
    std::function<void()> onRecord;

    static constexpr int kRecordH = 56;
    static constexpr int kGap     = 12;

    CapturePanel()
    {
        recordBtn.onClick = [this] { if (onRecord) onRecord(); };
        addAndMakeVisible (recordBtn);
        addAndMakeVisible (stats);
        addAndMakeVisible (coverage);
    }

    void setRecordingState (bool rec)
    {
        recordBtn.setButtonText (rec ? "STOP recording" : "RECORD  -  play this drum");
        recordBtn.setColour (juce::TextButton::buttonColourId,
                             rec ? juce::Colour (0xffd0473f) : juce::Colour (0xff2f7d4f));
    }

    void resized() override
    {
        auto b = getLocalBounds();
        recordBtn.setBounds (b.removeFromTop (kRecordH));
        b.removeFromTop (kGap);
        // Distribute remainder proportionally: stats 60%, gap, coverage 40%.
        const int rem    = b.getHeight();
        const int statsH = juce::jlimit (80, 160, rem * 60 / 100);
        const int coverH = juce::jlimit (48, 100, rem - statsH - kGap);
        stats.setBounds    (b.removeFromTop (statsH));
        b.removeFromTop (kGap);
        coverage.setBounds (b.removeFromTop (coverH));
    }

private:
    juce::TextButton recordBtn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapturePanel)
};

// ---------------------------------------------------------------------------
// ExportBar — bottom zone: destination row + optional embedded browser + export.
//
// Manages its own browser visibility state and FileChooser/FileBrowserComponent
// lifecycle. ForgeContent wires onExportRequested / onRevealRequested /
// onPathCommitted callbacks and calls getDestPath() during export.
// onBrowserVisibilityChanged fires after browser show/hide so ForgeContent
// can scroll the Viewport to reveal the bar.
// ---------------------------------------------------------------------------
class ExportBar : public juce::Component
{
public:
    std::function<void()>                    onExportRequested;
    std::function<void()>                    onRevealRequested;
    std::function<void (const juce::String&)> onPathCommitted;
    std::function<void (bool)>               onBrowserVisibilityChanged;

    static constexpr int kDestH    = 28;
    static constexpr int kBrowserH = 296;  // list 260 + gap 4 + actions 32
    static constexpr int kExportH  = 44;
    static constexpr int kStatusH  = 22;
    static constexpr int kRevealH  = 28;
    static constexpr int kGap      = 12;

    ExportBar()
    {
        destLabel.setText ("Export to:", juce::dontSendNotification);
        destLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
        addAndMakeVisible (destLabel);

        destPathEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1b1f25));
        destPathEditor.setTooltip ("Type or paste the export destination folder path");
        destPathEditor.onReturnKey = [this] { commitPath(); };
        destPathEditor.onFocusLost = [this] { commitPath(); };
        addAndMakeVisible (destPathEditor);

        configureButton (browseBtn, "Browse...");
        browseBtn.onClick = [this] { onBrowse(); };

        configureButton (chooseFolderBtn, "Use This Folder");
        chooseFolderBtn.onClick = [this] { onChooseFolder(); };
        chooseFolderBtn.setVisible (false);

        configureButton (cancelBrowseBtn, "Cancel");
        cancelBrowseBtn.onClick = [this] { hideBrowser(); };
        cancelBrowseBtn.setVisible (false);

        configureButton (exportBtn, "Export kit  (flamkit.yaml + samples)");
        exportBtn.onClick = [this] { if (onExportRequested) onExportRequested(); };

        configureButton (revealBtn, "Reveal in file manager");
        revealBtn.setVisible (false);
        revealBtn.onClick = [this] { if (onRevealRequested) onRevealRequested(); };

        status.setColour (juce::Label::textColourId, juce::Colours::aquamarine.withAlpha (0.85f));
        status.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (status);
    }

    juce::String getDestPath() const     { return destPathEditor.getText(); }
    void setDestPath (const juce::String& p) { destPathEditor.setText (p, false); }
    void setStatus (const juce::String& t)   { status.setText (t, juce::dontSendNotification); }
    bool isBrowserVisible() const            { return browserVisible; }

    void showReveal()
    {
        revealBtn.setVisible (true);
        resized();
    }

    int naturalHeight() const
    {
        return kDestH + kGap
             + (browserVisible ? kBrowserH + kGap : 0)
             + kExportH + kGap
             + kStatusH + kGap
             + kRevealH;
    }

    void resized() override
    {
        auto b = getLocalBounds();
        // Destination row
        {
            auto row = b.removeFromTop (kDestH);
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
        b.removeFromTop (kGap);
        if (browserVisible && fileBrowser != nullptr)
        {
            fileBrowser->setBounds    (b.removeFromTop (kBrowserH - 36));
            b.removeFromTop (4);
            auto actRow = b.removeFromTop (32);
            chooseFolderBtn.setBounds (actRow.removeFromLeft (actRow.getWidth() / 2 - 4));
            actRow.removeFromLeft (8);
            cancelBrowseBtn.setBounds (actRow);
            b.removeFromTop (kGap);
        }
        exportBtn.setBounds (b.removeFromTop (kExportH));
        b.removeFromTop (kGap);
        status.setBounds (b.removeFromTop (kStatusH));
        b.removeFromTop (kGap);
        revealBtn.setBounds (b.removeFromTop (kRevealH));
    }

private:
    void configureButton (juce::TextButton& b, const juce::String& lbl)
    {
        b.setButtonText (lbl);
        addAndMakeVisible (b);
    }

    void commitPath()
    {
        if (onPathCommitted) onPathCommitted (destPathEditor.getText().trim());
    }

    void onBrowse()
    {
        if (browserVisible) { hideBrowser(); return; }

        const juce::String pathText = destPathEditor.getText().trim();
        const juce::File startDir = (pathText.isNotEmpty() && juce::File (pathText).isDirectory())
                                    ? juce::File (pathText) : buildDefaultDir();

        if (juce::FileChooser::isPlatformDialogAvailable())
        {
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
                        commitPath();
                    }
                });
        }
        else
        {
            showEmbeddedBrowser (startDir);
        }
    }

    void showEmbeddedBrowser (const juce::File& startDir)
    {
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
        resized();
        if (auto* parent = getParentComponent()) parent->resized();
        if (onBrowserVisibilityChanged) onBrowserVisibilityChanged (true);
    }

    void hideBrowser()
    {
        browserVisible = false;
        if (fileBrowser != nullptr) fileBrowser->setVisible (false);
        chooseFolderBtn.setVisible (false);
        cancelBrowseBtn.setVisible (false);
        browseBtn.setButtonText ("Browse...");
        resized();
        if (auto* parent = getParentComponent()) parent->resized();
        if (onBrowserVisibilityChanged) onBrowserVisibilityChanged (false);
    }

    void onChooseFolder()
    {
        if (fileBrowser != nullptr)
        {
            const juce::File chosen = fileBrowser->getRoot();
            if (chosen.isDirectory())
            {
                destPathEditor.setText (chosen.getFullPathName(), false);
                commitPath();
            }
        }
        hideBrowser();
    }

    static juce::File buildDefaultDir()
    {
        auto d = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (! d.isDirectory())
            d = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        return d.getChildFile ("FlamForgeKits");
    }

    juce::Label      destLabel, status;
    juce::TextEditor destPathEditor;
    juce::TextButton browseBtn, chooseFolderBtn, cancelBrowseBtn, exportBtn, revealBtn;
    std::unique_ptr<juce::FileBrowserComponent> fileBrowser;
    std::unique_ptr<juce::FileChooser>          chooser;
    bool browserVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportBar)
};

// ---------------------------------------------------------------------------
// ForgeContent — owns the scrollable zone components (header + middle),
// single 30 Hz timer, engine, and all application state.
//
// ExportBar is a sibling component owned by MainComponent and pinned to the
// window bottom outside the scroll area. ForgeContent communicates with it
// via the public callback members below; MainComponent wires them.
//
// Member declaration order is intentional:
//   deviceManager → deviceSelector (unique_ptr) → zone components → state
// Destruction is reverse order, so zones are torn down before deviceSelector,
// and deviceSelector before deviceManager — preventing use-after-free on the
// raw pointer held by SetupHeader::adoptDeviceSelector().
// ---------------------------------------------------------------------------
class ForgeContent : public juce::Component,
                     private juce::Timer
{
public:
    static constexpr int kPad     = 22;
    static constexpr int kGap     = 12;
    static constexpr int kRailW   = 150;  // fixed PieceRail width

    // Callbacks wired by MainComponent to connect ForgeContent ↔ ExportBar.
    std::function<void (const juce::String&)> onStatusChanged;
    std::function<void()>                     onExportSucceeded;
    std::function<juce::String()>             getExportDestPath;

    ForgeContent()
    {
        // Recorder needs inputs only; hiding output + advanced keeps the panel compact.
        deviceManager.initialiseWithDefaultDevices (/*inputs=*/2, /*outputs=*/0);
        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            deviceManager,
            /*minInput=*/1, /*maxInput=*/16,
            /*minOutput=*/0, /*maxOutput=*/0,
            /*showMidiIn=*/false, /*showMidiOut=*/false,
            /*stereoPairs=*/false, /*hideAdvanced=*/true);

        // Wire device selector into the header zone (non-owning).
        header.adoptDeviceSelector (*deviceSelector);

        addAndMakeVisible (header);
        addAndMakeVisible (pieceRail);
        addAndMakeVisible (capturePanel);

        // --- PieceRail callbacks ---
        pieceRail.onSwitch = [this] (int d) { switchPiece (d); };
        pieceRail.onAdd    = [this]        { addPiece(); };
        pieceRail.onNameChanged = [this] (const juce::String& n) { currentPiece().name = n; };

        // --- CapturePanel callbacks ---
        capturePanel.onRecord = [this] { toggleRecord(); };

        // Persist export destination across sessions.
        juce::PropertiesFile::Options propOpts;
        propOpts.applicationName     = "FlamForge";
        propOpts.filenameSuffix      = ".settings";
        propOpts.osxLibrarySubFolder = "Application Support";
        appProps.setStorageParameters (propOpts);

        deviceManager.addAudioCallback (&engine);
        captures.push_back ({});
        captures[0].name = "Kick";

        refreshAll();
        startTimerHz (30);
    }

    ~ForgeContent() override
    {
        stopTimer();
        deviceManager.removeAudioCallback (&engine);
    }

    int naturalHeight() const
    {
        // Lower-bound elastic heights for initial window sizing.
        const int deviceH  = 160;
        const int headerH  = SetupHeader::kTitleH + SetupHeader::kSubH + SetupHeader::kGap
                           + deviceH + SetupHeader::kGap + SetupHeader::kStripsH;
        const int captureH = CapturePanel::kRecordH + CapturePanel::kGap + 80 + CapturePanel::kGap + 48;
        const int midH     = juce::jmax (captureH, 120);
        return 2 * kPad + headerH + kGap + midH;
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0f12)); }

    void resized() override
    {
        // Compute elastic device-panel height from available space (same proportions as before).
        const int fixedV = 2 * kPad
            + SetupHeader::kTitleH + SetupHeader::kSubH + SetupHeader::kGap
            + SetupHeader::kGap    // gap after device
            + SetupHeader::kStripsH + kGap  // strips + gap below header
            + kGap;                          // gap below mid
        const int flexAvail = juce::jmax (0, getHeight() - fixedV);
        const int deviceH   = juce::jlimit (160, 340, 50 + flexAvail * 55 / 100);
        const int midH      = juce::jmax (120, flexAvail - deviceH - kGap);
        const int headerH   = SetupHeader::kTitleH + SetupHeader::kSubH + SetupHeader::kGap
                            + deviceH + SetupHeader::kGap + SetupHeader::kStripsH;

        auto area = getLocalBounds().reduced (kPad, kPad);

        // Top zone: SetupHeader (full width)
        header.setBounds (area.removeFromTop (headerH));
        area.removeFromTop (kGap);

        // Middle: PieceRail (left, fixed) | CapturePanel (center, remaining) via FlexBox
        {
            auto mid = area.removeFromTop (midH);
            juce::FlexBox fb;
            fb.flexDirection = juce::FlexBox::Direction::row;
            fb.items.add (juce::FlexItem (pieceRail)
                              .withWidth ((float) kRailW)
                              .withHeight ((float) midH)
                              .withMargin ({ 0.0f, (float) kGap, 0.0f, 0.0f }));
            fb.items.add (juce::FlexItem (capturePanel)
                              .withFlex (1.0f)
                              .withHeight ((float) midH));
            fb.performLayout (mid.toFloat());
        }
    }

    // --- Public API for MainComponent to call on behalf of ExportBar actions ---

    // Triggered by ExportBar's Export button via MainComponent.
    void onExport()
    {
        stopRecording();
        bool anyHits = false;
        for (const auto& p : captures)
            if (! p.hits.empty()) { anyHits = true; break; }
        if (! anyHits)
        {
            setStatus ("Nothing to export yet - record some hits first.");
            return;
        }

        const juce::String pathText = (getExportDestPath ? getExportDestPath() : juce::String{}).trim();
        juce::File destDir = pathText.isNotEmpty() ? juce::File (pathText) : buildDefaultDest();
        if (! destDir.isDirectory() && ! destDir.createDirectory())
        {
            setStatus ("Cannot create folder: " + destDir.getFullPathName());
            return;
        }
        persistDestPath (destDir.getFullPathName());

        setStatus ("Exporting to " + destDir.getFullPathName() + " ...");
        const ExportResult res = exportKit (kitName, captures, options, destDir, channelLabels);
        setStatus (res.message);
        if (res.ok)
        {
            lastExportedKit = res.kitYaml.getParentDirectory();
            if (onExportSucceeded) onExportSucceeded();
        }
    }

    // Triggered by ExportBar's Reveal button via MainComponent.
    void onReveal()
    {
        if (lastExportedKit != juce::File{})
            lastExportedKit.revealToUser();
    }

    // Called by ExportBar.onPathCommitted (via MainComponent) to persist the path.
    void persistDestPath (const juce::String& path)
    {
        if (auto* props = appProps.getUserSettings())
            props->setValue ("lastExportDir", path);
    }

    // Called by MainComponent at startup to seed ExportBar with the last used path.
    juce::File loadPersistedDest()
    {
        if (auto* props = appProps.getUserSettings())
        {
            const juce::String last = props->getValue ("lastExportDir");
            if (last.isNotEmpty()) { juce::File f (last); if (f.isDirectory()) return f; }
        }
        return buildDefaultDest();
    }

private:
    PieceCapture& currentPiece() { return captures[(size_t) currentPieceIndex]; }

    bool isRecording() const { return engine.getMode() == CaptureEngine::Mode::Recording; }

    void setStatus (const juce::String& s) { if (onStatusChanged) onStatusChanged (s); }

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
        pieceRail.setDisplay (currentPieceIndex, (int) captures.size(), currentPiece().name);
        capturePanel.setRecordingState (isRecording());
        recompute();
    }

    void toggleRecord()
    {
        if (isRecording()) stopRecording();
        else
        {
            engine.setMode (CaptureEngine::Mode::Recording);
            capturePanel.setRecordingState (true);
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
        capturePanel.setRecordingState (false);
    }

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

        // Rebuild strips when device channel count changes; pump levels every tick.
        const int n = engine.channelCount();
        if (n != lastChannelCount)
        {
            while ((int) channelLabels.size() < n)
                channelLabels.push_back ("Mic " + juce::String ((int) channelLabels.size() + 1));
            lastChannelCount = n;
            header.stripRow.rebuild (n, channelLabels,
                [this] (int c, juce::String t)
                {
                    if (c < (int) channelLabels.size())
                        channelLabels[(size_t) c] = t;
                });
            resized();
        }
        header.stripRow.tick (engine);
    }

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
        for (auto& h : piece.hits)
            h.midiVelocity = hasRange ? mapPeakToVelocity (h.peakDb, softDb, loudDb) : 100;

        std::vector<int> vels;
        vels.reserve (piece.hits.size());
        for (const auto& h : piece.hits) vels.push_back (h.midiVelocity);
        capturePanel.coverage.setHits (vels);

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
        capturePanel.stats.setStats ((int) piece.hits.size(), hasRange, softDb, loudDb,
                                     layers, rr, capturePanel.coverage.binsFilled(),
                                     CoverageMeter::kNumBins);

        if (isRecording())
            setStatus ("Recording \"" + piece.name + "\" - "
                       + juce::String ((int) piece.hits.size())
                       + " hits. Click STOP when coverage looks good.");
    }

    juce::File buildDefaultDest()
    {
        auto d = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
        if (! d.isDirectory())
            d = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        return d.getChildFile ("FlamForgeKits");
    }

    // --- member order: device manager and selector before zones ---
    // This ensures zones are destroyed before deviceSelector (and deviceSelector
    // before deviceManager), preventing use-after-free on SetupHeader's raw pointer.
    juce::AudioDeviceManager                             deviceManager;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    SetupHeader  header;
    PieceRail    pieceRail;
    CapturePanel capturePanel;

    std::vector<juce::String> channelLabels;
    int                       lastChannelCount = 0;
    CaptureEngine             engine;
    std::vector<PieceCapture> captures;
    int                       currentPieceIndex = 0;
    SynthOptions              options;
    juce::String              kitName = "FlamForge Kit";
    juce::ApplicationProperties appProps;
    juce::File                lastExportedKit;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeContent)
};

// ---------------------------------------------------------------------------
// MainComponent — two-zone layout:
//   • Viewport (top, elastic): scrolls ForgeContent (header + middle zones).
//   • ExportBar (bottom, persistent): always visible, never scrolls away.
//
// ExportBar expands/shrinks (embedded browser) — this triggers parent->resized()
// inside ExportBar, which reflows the split correctly via resized() here.
// ForgeContent ↔ ExportBar are connected through callback pairs wired here.
// ---------------------------------------------------------------------------
class MainComponent : public juce::Component
{
public:
    // Horizontal padding matches ForgeContent interior margins; vertical gap
    // gives the bar visual breathing room without wasting screen space.
    static constexpr int kBarPadH = ForgeContent::kPad;  // 22 px — matches content margins
    static constexpr int kBarPadV = 8;

    MainComponent()
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, true);
        addAndMakeVisible (viewport);
        addAndMakeVisible (exportBar);

        // ForgeContent → ExportBar: status text + reveal button trigger
        content.onStatusChanged   = [this] (const juce::String& s) { exportBar.setStatus (s); };
        content.onExportSucceeded = [this] { exportBar.showReveal(); };
        content.getExportDestPath = [this] { return exportBar.getDestPath(); };

        // ExportBar → ForgeContent: user actions forwarded to the engine
        exportBar.onExportRequested = [this] { content.onExport(); };
        exportBar.onRevealRequested = [this] { content.onReveal(); };
        exportBar.onPathCommitted   = [this] (const juce::String& p) { content.persistDestPath (p); };
        // Browser expand/collapse changes naturalHeight() — reflow the two-zone split.
        exportBar.onBrowserVisibilityChanged = [this] (bool) { resized(); };

        // Seed ExportBar with the last-used export directory from prefs.
        exportBar.setDestPath (content.loadPersistedDest().getFullPathName());
        exportBar.setStatus ("Ready. Choose your input above, name the drum, then Record.");

        const int initH = content.naturalHeight() + 2 * kBarPadV + exportBar.naturalHeight();
        setSize (760, juce::jmin (initH, 940));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0d0f12));
        // Thin separator between scrollable body and persistent export bar.
        const int sepY = getHeight() - 2 * kBarPadV - exportBar.naturalHeight() - 1;
        g.setColour (juce::Colour (0xff23272e));
        g.fillRect (0, sepY, getWidth(), 1);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        // Pin ExportBar to the bottom; give the Viewport everything above it.
        const int barZoneH = exportBar.naturalHeight() + 2 * kBarPadV;
        exportBar.setBounds (b.removeFromBottom (barZoneH).reduced (kBarPadH, kBarPadV));
        viewport.setBounds (b);

        const int w = viewport.getMaximumVisibleWidth();
        const int contentW = juce::jmax (640, w);
        content.setSize (contentW, juce::jmax (content.naturalHeight(), viewport.getHeight()));
        repaint();  // redraw separator line at updated position
    }

private:
    juce::Viewport viewport;
    ForgeContent   content;
    ExportBar      exportBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace flamforge
