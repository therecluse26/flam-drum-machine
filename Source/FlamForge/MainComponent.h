// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "CaptureEngine.h"
#include "CaptureTypes.h"
#include "ForgeColors.h"
#include "LayerSynth.h"
#include "KitExporter.h"
#include "OfflineTransientDetector.h"
#include "SegmentExtractor.h"
#include "SegmentPlayer.h"
#include "WaveformEditor.h"

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
// CaptureProgress -- unified capture-feedback view.
//
// Replaces the former separate StatsPanel + CoverageMeter. The bin count is
// shown inline on the same header row as "VELOCITY COVERAGE" (text beside the
// visual it describes, not a separate stacked block). Session stats (hits,
// range, layers) are rendered beneath the bins inside the same rounded panel.
//
// setAll() is called from ForgeContent::recompute() -- no internal timer.
// ---------------------------------------------------------------------------
class CaptureProgress : public juce::Component
{
public:
    static constexpr int kNumBins = 16;

    CaptureProgress() = default;

    void setAll (int hits, bool hasRange, float softDb, float loudDb,
                 int layers, int rr, const std::vector<int>& velocities)
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

        hitsCount    = hits;
        hasRangeFlag = hasRange;
        softDbVal    = softDb;
        loudDbVal    = loudDb;
        layersVal    = layers;
        rrVal        = rr;

        repaint();
    }

    int binsFilled() const { return filled; }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff14171c));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

        auto area = getLocalBounds().reduced (12, 8);

        // Coverage header row: label + inline bin count
        {
            auto row = area.removeFromTop (16);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            g.drawText ("VELOCITY COVERAGE", row.toFloat(), juce::Justification::centredLeft);

            const juce::String cnt = juce::String (filled) + " / " + juce::String (kNumBins) + " bins"
                                   + (filled >= kNumBins ? "  (full)" : "");
            g.setColour (filled >= kNumBins ? juce::Colour (0xff3ecf6b) : juce::Colours::white.withAlpha (0.50f));
            g.drawText (cnt, row.toFloat(), juce::Justification::centredRight);
        }
        area.removeFromTop (5);

        // Bin tiles: hue encodes velocity position (blue=soft → red=loud via velocityColour),
        // brightness encodes coverage tier (full=ideal, dim=thin, near-black=empty).
        {
            const int binsH = juce::jlimit (26, 46, area.getHeight() / 3);
            auto binsR = area.removeFromTop (binsH).toFloat();

            const float gap  = 3.0f;
            const float binW = (binsR.getWidth() - gap * (kNumBins - 1)) / (float) kNumBins;

            for (int i = 0; i < kNumBins; ++i)
            {
                const auto bin = juce::Rectangle<float> (
                    binsR.getX() + i * (binW + gap), binsR.getY(), binW, binsR.getHeight());
                const int n   = counts[(size_t) i];

                // Centre velocity of this bin's range, 1..127 monotonically left to right
                const float t   = float (2 * i + 1) / float (2 * kNumBins);
                const int   vel = 1 + juce::roundToInt (t * 126.0f);

                juce::Colour c;
                if      (n >= 6) c = velocityColour (vel);
                else if (n >= 2) c = velocityColour (vel).withMultipliedBrightness (0.80f);
                else if (n >= 1) c = velocityColour (vel).withMultipliedBrightness (0.60f);
                else             c = velocityColour (vel).withMultipliedSaturation (0.25f)
                                                         .withMultipliedBrightness (0.28f);
                g.setColour (c);
                g.fillRoundedRectangle (bin, 2.0f);

                if (n > 0 && binW > 12.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (n >= 6 ? 0.90f : 0.75f));
                    g.setFont (juce::Font (juce::FontOptions (12.0f)));
                    g.drawText (juce::String (n), bin, juce::Justification::centred, false);
                }
            }

            if (hitsCount == 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.28f));
                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                g.drawText ("soft  <  velocity coverage  >  hard   (play hits to fill)",
                            binsR.toType<int>(), juce::Justification::centred);
            }
        }
        area.removeFromTop (8);

        // Session stats beneath the bins
        g.setFont (juce::Font (juce::FontOptions (12.5f).withStyle ("Monospaced")));
        const int lh = juce::jlimit (14, 20, juce::jmax (14, area.getHeight() / 4));

        struct StatRow { const char* label; juce::String value; };
        const StatRow rows[3] = {
            { "Hits:",   juce::String (hitsCount) },
            { "Range:",  hasRangeFlag
                             ? juce::String (softDbVal, 1) + " to " + juce::String (loudDbVal, 1) + " dBFS"
                             : juce::String ("play 2+ hits to detect") },
            { "Layers:", layersVal > 0
                             ? juce::String (layersVal) + " x up to " + juce::String (rrVal) + " rr"
                             : juce::String ("--") },
        };

        for (const auto& row : rows)
        {
            if (area.getHeight() < lh) break;
            auto r = area.removeFromTop (lh).toFloat();
            area.removeFromTop (3);
            g.setColour (juce::Colours::white.withAlpha (0.42f));
            g.drawText (row.label, r.withWidth (64.0f), juce::Justification::centredLeft);
            g.setColour (juce::Colours::white.withAlpha (0.82f));
            g.drawText (row.value, r.withLeft (r.getX() + 66.0f), juce::Justification::centredLeft);
        }
    }

private:
    std::array<int, kNumBins> counts{{}};
    int   filled      = 0;
    int   hitsCount   = 0;
    bool  hasRangeFlag = false;
    float softDbVal   = 0.0f;
    float loudDbVal   = 0.0f;
    int   layersVal   = 0;
    int   rrVal       = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureProgress)
};

// ---------------------------------------------------------------------------
// ForgeMeter -- timer-free segmented LED input level meter.
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

        // Segmented LED bars -- ported from Source/UI/PeakMeter.h
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
// ForgeChannelStrip -- editable label + vertical meter for one input channel.
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
// ChannelStripRow -- horizontal, scrollable row of up to 16 ForgeChannelStrip.
//
// ForgeContent owns the label model (std::vector<juce::String> channelLabels).
// rebuild() seeds strips from that vector and wires onLabelChanged back so
// edits keep the model current. tick() is called at 30 Hz from ForgeContent.
// ---------------------------------------------------------------------------
class ChannelStripRow : public juce::Component
{
public:
    static constexpr int kStripW = 76;  // fixed strip width; 16 strips = 1216 px -> horizontal scroll

    ChannelStripRow()
    {
        // inner declared before viewport -> viewport destroyed first (see member order below).
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

    // Declare inner before viewport -- C++ reverse-destruction order ensures
    // the viewport (and its raw contentComp pointer) is destroyed first.
    juce::Component              inner;
    juce::OwnedArray<ForgeChannelStrip> strips;
    juce::Viewport               viewport;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripRow)
};

// ---------------------------------------------------------------------------
// SetupHeader -- top zone: title + subtitle + audio device selector.
//
// Collapsible: click the title/subtitle area to collapse to a one-line summary;
// click the summary bar to expand again. Auto-collapses once ForgeContent detects
// a valid input device via AudioDeviceManager::ChangeListener -- no timer added.
//
// Per-channel meter strips have moved into CapturePanel (directly under RECORD)
// where the user watches levels while playing -- this zone is device setup only.
//
// Owns title and subtitle; holds a non-owning pointer to the AudioDeviceSelectorComponent
// (owned by ForgeContent via unique_ptr). adoptDeviceSelector() must be called from
// ForgeContent's constructor body after the selector is created.
// ---------------------------------------------------------------------------
class SetupHeader : public juce::Component
{
public:
    std::function<void()> onCollapseToggled;  // fired after state flips; caller should relayout

    static constexpr int kTitleH  = 38;
    static constexpr int kSubH    = 20;
    static constexpr int kGap     = 12;
    static constexpr int kBarH    = 32;  // height of the collapsed summary bar

    SetupHeader()
    {
        title.setFont (juce::Font (juce::FontOptions (32.0f).withStyle ("Bold")));
        title.setColour (juce::Label::textColourId, juce::Colours::white);
        title.setText ("FlamForge", juce::dontSendNotification);
        title.setInterceptsMouseClicks (false, false);  // let clicks fall through to SetupHeader
        addAndMakeVisible (title);

        subtitle.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        subtitle.setText ("Play each drum from soft to hard - layers build themselves.",
                          juce::dontSendNotification);
        subtitle.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (subtitle);

        // Shown only when collapsed; summarises device + channel count + export path.
        summaryBar.setFont (juce::Font (juce::FontOptions (13.0f)));
        summaryBar.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.8f));
        summaryBar.setText ("> Setup", juce::dontSendNotification);
        summaryBar.setInterceptsMouseClicks (false, false);
        summaryBar.setVisible (false);
        addAndMakeVisible (summaryBar);
    }

    // Called once from ForgeContent's constructor body after deviceSelector is created.
    // Adds the selector as a non-owned child so it renders inside this zone.
    void adoptDeviceSelector (juce::AudioDeviceSelectorComponent& sel)
    {
        deviceSel = &sel;
        addAndMakeVisible (sel);
    }

    bool isExpanded() const { return expanded; }

    void setExpanded (bool e)
    {
        if (expanded == e) return;
        expanded = e;
        applyVisibility();
        if (onCollapseToggled) onCollapseToggled();
    }

    // Update the one-line summary shown when collapsed, e.g. "Default ALSA * 2 in * ~/Music/..."
    void setSummaryText (const juce::String& t)
    {
        summaryBar.setText ("> " + t, juce::dontSendNotification);
    }

    // Returns pixel height needed: kBarH when collapsed; full expanded height otherwise.
    int naturalHeight (int deviceH) const
    {
        if (! expanded) return kBarH;
        return kTitleH + kSubH + kGap + deviceH;
    }

    // Collapsed: any click expands. Expanded: click in the title+subtitle zone collapses.
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! expanded || e.getPosition().getY() <= kTitleH + kSubH)
            setExpanded (! expanded);
    }

    void paint (juce::Graphics& g) override
    {
        if (! expanded)
        {
            g.setColour (juce::Colour (0xff1b1f25));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        if (! expanded)
        {
            summaryBar.setBounds (b.reduced (12, 0));
            return;
        }
        title.setBounds    (b.removeFromTop (kTitleH));
        subtitle.setBounds (b.removeFromTop (kSubH));
        b.removeFromTop (kGap);
        if (deviceSel != nullptr)
            deviceSel->setBounds (b);   // all remaining height to device selector
    }

private:
    void applyVisibility()
    {
        title.setVisible (expanded);
        subtitle.setVisible (expanded);
        if (deviceSel != nullptr) deviceSel->setVisible (expanded);
        summaryBar.setVisible (! expanded);
    }

    bool expanded = true;
    juce::Label title, subtitle, summaryBar;
    juce::AudioDeviceSelectorComponent* deviceSel = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SetupHeader)
};

// ---------------------------------------------------------------------------
// PieceRail -- left rail: all drum pieces shown as a scrollable clickable list.
//
// Layout (top to bottom):
//   [Drum pieces ▸ scrollable list, each row clickable]
//   [Name TextEditor — edits the selected piece's name]
//   [+ Add Piece button]
//
// rebuild() is called by ForgeContent::refreshAll() whenever the piece list or
// selection changes; it recreates the row components from the names vector.
// onSelectPiece(int index) replaces the old onSwitch(delta) and fires an
// absolute index rather than a +/-1 delta.
// ---------------------------------------------------------------------------
class PieceRail : public juce::Component
{
public:
    std::function<void (int)>                  onSelectPiece;  // absolute index
    std::function<void()>                      onAdd;
    std::function<void (const juce::String&)>  onNameChanged;

    static constexpr int kRowH    = 30;
    static constexpr int kNameH   = 24;
    static constexpr int kAddH    = 28;
    static constexpr int kGap     = 4;

    PieceRail()
    {
        // inner declared before viewport -- reverse-destruction order is safe.
        viewport.setViewedComponent (&inner, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1b1f25));
        nameEditor.setColour (juce::TextEditor::textColourId, juce::Colours::white.withAlpha (0.85f));
        nameEditor.setFont (juce::Font (juce::FontOptions (12.0f)));
        nameEditor.setTextToShowWhenEmpty ("Piece name...", juce::Colours::white.withAlpha (0.3f));
        nameEditor.onTextChange = [this]
        {
            if (onNameChanged) onNameChanged (nameEditor.getText());
            // Keep the selected row label in sync without a full rebuild.
            if (selectedIdx >= 0 && selectedIdx < rows.size())
            {
                rows[selectedIdx]->name = nameEditor.getText();
                rows[selectedIdx]->repaint();
            }
        };
        addAndMakeVisible (nameEditor);

        addBtn.setButtonText ("+ Add Piece");
        addBtn.onClick = [this] { if (onAdd) onAdd(); };
        addAndMakeVisible (addBtn);
    }

    // Rebuild the list from the current names vector. selectedIdx is the piece
    // currently selected in ForgeContent; its name seeds the name editor.
    void rebuild (int selectedIndex, const std::vector<juce::String>& names)
    {
        selectedIdx = selectedIndex;
        rows.clear();
        for (int i = 0; i < (int) names.size(); ++i)
        {
            auto* row = rows.add (new PieceRow());
            row->name     = names[(size_t) i];
            row->selected = (i == selectedIndex);
            const int idx = i;
            row->onClicked = [this, idx] { selectRow (idx); };
            inner.addAndMakeVisible (row);
        }
        layoutInner();
        if (selectedIndex >= 0 && selectedIndex < (int) names.size())
            nameEditor.setText (names[(size_t) selectedIndex], juce::dontSendNotification);
        scrollToSelected();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (4, 4);
        addBtn.setBounds  (b.removeFromBottom (kAddH));
        b.removeFromBottom (kGap);
        nameEditor.setBounds (b.removeFromBottom (kNameH));
        b.removeFromBottom (kGap);
        viewport.setBounds (b);
        layoutInner();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff14171c));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

        // Section label above the list
        g.setColour (juce::Colours::white.withAlpha (0.40f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("DRUM PIECES", getLocalBounds().reduced (6, 0).withHeight (14),
                    juce::Justification::centredLeft);
    }

private:
    // ---------------------------------------------------------------------------
    // PieceRow -- one clickable row in the piece list.
    // ---------------------------------------------------------------------------
    struct PieceRow : juce::Component
    {
        std::function<void()> onClicked;
        juce::String name;
        bool         selected = false;
        bool         hovered  = false;

        void paint (juce::Graphics& g) override
        {
            if (selected)
            {
                g.setColour (juce::Colour (0xff2f5c3a));
                g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f, 1.0f), 4.0f);
            }
            else if (hovered)
            {
                g.setColour (juce::Colour (0xff1e2329));
                g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f, 1.0f), 4.0f);
            }
            g.setColour (selected ? juce::Colours::white : juce::Colours::white.withAlpha (0.75f));
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (name, getLocalBounds().reduced (8, 0), juce::Justification::centredLeft);
        }

        void mouseUp    (const juce::MouseEvent&) override { if (onClicked) onClicked(); }
        void mouseEnter (const juce::MouseEvent&) override { hovered = true;  repaint(); }
        void mouseExit  (const juce::MouseEvent&) override { hovered = false; repaint(); }
    };

    void selectRow (int idx)
    {
        if (selectedIdx >= 0 && selectedIdx < rows.size())
            rows[selectedIdx]->selected = false;
        selectedIdx = idx;
        if (selectedIdx >= 0 && selectedIdx < rows.size())
        {
            rows[selectedIdx]->selected = true;
            nameEditor.setText (rows[selectedIdx]->name, juce::dontSendNotification);
        }
        inner.repaint();
        if (onSelectPiece) onSelectPiece (idx);
    }

    void layoutInner()
    {
        const int n      = rows.size();
        const int innerH = juce::jmax (viewport.getHeight(), n * kRowH);
        inner.setSize (viewport.getWidth(), innerH);
        for (int i = 0; i < n; ++i)
            rows[i]->setBounds (0, i * kRowH, inner.getWidth(), kRowH);
    }

    void scrollToSelected()
    {
        if (selectedIdx >= 0 && selectedIdx < rows.size())
            viewport.setViewPosition (0, selectedIdx * kRowH);
    }

    // inner before viewport -- safe reverse-destruction order.
    juce::Component         inner;
    juce::OwnedArray<PieceRow> rows;
    juce::Viewport          viewport;
    juce::TextEditor        nameEditor;
    juce::TextButton        addBtn;
    int                     selectedIdx = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PieceRail)
};

// ---------------------------------------------------------------------------
// CapturePanel -- center zone: RECORD button + per-channel meters + unified
// capture-progress view (coverage bins + session stats in one panel).
//
// Layout (top to bottom):
//   [RECORD button -- full width]
//   [ChannelStripRow -- per-channel input meters, full width]
//   [CaptureProgress -- velocity bins + inline stats, fills remainder]
//
// The ChannelStripRow is driven by ForgeContent's single 30 Hz Timer via
// tickMeters(); no timer is created inside this panel. The progress view is
// updated by ForgeContent::recompute() via progress.setAll().
//
// TODO (FLA-123): insert audition/preview controls between RECORD and meters
// once the CaptureEngine playback path lands.
// ---------------------------------------------------------------------------
class CapturePanel : public juce::Component
{
public:
    CaptureProgress progress;
    WaveformEditor  waveEditor;  // C4 — interactive waveform + breakpoint editor
    std::function<void()> onRecord;

    static constexpr int kRecordH   = 56;
    static constexpr int kMeterH    = 90;
    static constexpr int kProgressH = 150;  // header + bins + 3 stat rows
    static constexpr int kGap       = 12;

    CapturePanel()
    {
        recordBtn.onClick = [this] { if (onRecord) onRecord(); };
        addAndMakeVisible (recordBtn);
        addAndMakeVisible (meterRow);
        addAndMakeVisible (waveEditor);
        addAndMakeVisible (progress);
    }

    void setRecordingState (bool rec)
    {
        recordBtn.setButtonText (rec ? "STOP recording" : "RECORD  -  play this drum");
        recordBtn.setColour (juce::TextButton::buttonColourId,
                             rec ? juce::Colour (0xffd0473f) : juce::Colour (0xff2f7d4f));
    }

    // Called at 30 Hz from ForgeContent::timerCallback() -- no timer inside this panel.
    void tickMeters (CaptureEngine& eng) { meterRow.tick (eng); }

    // Rebuild strips when device channel count changes.
    void rebuild (int n,
                  const std::vector<juce::String>& labels,
                  std::function<void (int, juce::String)> onChanged)
    {
        meterRow.rebuild (n, labels, std::move (onChanged));
        resized();
    }

    void resized() override
    {
        auto b = getLocalBounds();
        recordBtn.setBounds (b.removeFromTop (kRecordH));
        b.removeFromTop (kGap);
        meterRow.setBounds (b.removeFromTop (kMeterH));
        b.removeFromTop (kGap);

        // CaptureProgress pinned to bottom at a fixed height.
        // Guard: never shrink waveform below its minimum usable size.
        const int progH = juce::jmin (kProgressH,
                                      juce::jmax (0, b.getHeight() - WaveformEditor::kMinHeight - kGap));
        if (progH >= 40)
        {
            progress.setBounds (b.removeFromBottom (progH));
            b.removeFromBottom (kGap);
        }

        // WaveformEditor gets all remaining height.
        waveEditor.setBounds (b.withHeight (juce::jmax (WaveformEditor::kMinHeight, b.getHeight())));
    }

private:
    juce::TextButton recordBtn;
    ChannelStripRow  meterRow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CapturePanel)
};

// ---------------------------------------------------------------------------
// ExportBar -- bottom zone: destination row + optional embedded browser + export.
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
// ForgeContent -- owns the scrollable zone components (header + middle),
// single 30 Hz timer, engine, and all application state.
//
// ExportBar is a sibling component owned by MainComponent and pinned to the
// window bottom outside the scroll area. ForgeContent communicates with it
// via the public callback members below; MainComponent wires them.
//
// Member declaration order is intentional:
//   deviceManager -> deviceSelector (unique_ptr) -> zone components -> state
// Destruction is reverse order, so zones are torn down before deviceSelector,
// and deviceSelector before deviceManager -- preventing use-after-free on the
// raw pointer held by SetupHeader::adoptDeviceSelector().
// ---------------------------------------------------------------------------
class ForgeContent : public juce::Component,
                     private juce::Timer,
                     public juce::ChangeListener
{
public:
    static constexpr int kPad     = 22;
    static constexpr int kGap     = 12;
    static constexpr int kRailW   = 150;  // fixed PieceRail width

    // Callbacks wired by MainComponent to connect ForgeContent <-> ExportBar.
    std::function<void (const juce::String&)> onStatusChanged;
    std::function<void()>                     onExportSucceeded;
    std::function<juce::String()>             getExportDestPath;

    ForgeContent()
    {
        // Inputs for recording, outputs for segment audition playback (FLA-163).
        deviceManager.initialiseWithDefaultDevices (/*inputs=*/2, /*outputs=*/2);
        deviceSelector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            deviceManager,
            /*minInput=*/1, /*maxInput=*/16,
            /*minOutput=*/0, /*maxOutput=*/2,
            /*showMidiIn=*/false, /*showMidiOut=*/false,
            /*stereoPairs=*/false, /*hideAdvanced=*/true);

        // Wire device selector into the header zone (non-owning).
        header.adoptDeviceSelector (*deviceSelector);

        // Re-layout when the header collapses/expands, propagating up to MainComponent
        // so the viewport content height is updated to match the new naturalHeight().
        header.onCollapseToggled = [this]
        {
            resized();
            if (auto* vp = getParentComponent())
                if (auto* mc = vp->getParentComponent())
                    mc->resized();
        };

        addAndMakeVisible (header);
        addAndMakeVisible (pieceRail);
        addAndMakeVisible (capturePanel);

        // --- PieceRail callbacks ---
        pieceRail.onSelectPiece = [this] (int idx) { switchPieceAbsolute (idx); };
        pieceRail.onAdd         = [this]            { addPiece(); };
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
        deviceManager.addChangeListener (this);  // auto-collapse on valid device selection
        captures.push_back ({});
        captures[0].name = "Kick";

        // SegmentPlayer — click-to-audition wiring (FLA-163).
        segmentFormatManager.registerBasicFormats();
        audioSourcePlayer.setSource (&segmentPlayer);
        deviceManager.addAudioCallback (&audioSourcePlayer);

        capturePanel.waveEditor.onSegmentAudition = [this] (int64_t start, int64_t end)
        {
            segmentPlayer.playRegion (start, end);
        };
        capturePanel.waveEditor.onAuditionStop = [this]
        {
            segmentPlayer.stop();
        };

        // WaveformEditor callback — user edited breakpoints; store and update status.
        // Full re-extraction deferred to export; velocities reflect new boundaries.
        capturePanel.waveEditor.onBreakpointsChanged =
            [this] (const std::vector<int64_t>& bps, const std::vector<int>& /*vels*/)
            {
                setStatus ("Breakpoints adjusted: " + juce::String ((int) bps.size()) + " hit(s).");
            };

        refreshAll();
        updateSetupSummary();
        startTimerHz (30);
    }

    ~ForgeContent() override
    {
        stopTimer();
        detector.cancel();  // stop background thread before members are torn down
        segmentPlayer.stop();
        deviceManager.removeAudioCallback (&audioSourcePlayer);
        deviceManager.removeChangeListener (this);
        deviceManager.removeAudioCallback (&engine);
        // Delete the continuous take WAV now that the editor is about to be destroyed.
        if (currentContinuousWav.existsAsFile())
            currentContinuousWav.deleteFile();
    }

    int naturalHeight() const
    {
        // Lower-bound elastic heights for initial window sizing.
        const int deviceH  = 160;
        const int headerH  = header.naturalHeight (deviceH);
        const int captureH = CapturePanel::kRecordH      + CapturePanel::kGap
                           + CapturePanel::kMeterH      + CapturePanel::kGap
                           + WaveformEditor::kMinHeight + CapturePanel::kGap
                           + CapturePanel::kProgressH;
        const int midH     = juce::jmax (captureH, 120);
        return 2 * kPad + headerH + kGap + midH;
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0f12)); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (kPad, kPad);

        int headerH, midH;
        if (header.isExpanded())
        {
            // fixedV: non-elastic height (strips now in CapturePanel, not header).
            const int fixedV = 2 * kPad
                + SetupHeader::kTitleH + SetupHeader::kSubH + SetupHeader::kGap
                + kGap    // gap between header and mid
                + kGap;   // gap below mid
            const int flexAvail = juce::jmax (0, getHeight() - fixedV);
            const int deviceH   = juce::jlimit (160, 340, 50 + flexAvail * 55 / 100);
            midH    = juce::jmax (120, flexAvail - deviceH - kGap);
            headerH = SetupHeader::kTitleH + SetupHeader::kSubH + SetupHeader::kGap + deviceH;
        }
        else
        {
            // Collapsed: header is a thin bar; give the freed space to the mid zone.
            const int fixedVCollapsed = 2 * kPad + SetupHeader::kBarH + kGap + kGap;
            const int flexAvailCollapsed = juce::jmax (0, getHeight() - fixedVCollapsed);
            headerH = SetupHeader::kBarH;
            midH    = juce::jmax (120, flexAvailCollapsed - kGap);
        }

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
        updateSetupSummary();  // keep collapsed summary in sync with new path
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

    // Open a new reader from wav and hand it to SegmentPlayer for audition.
    // Pass an invalid File() to stop playback and clear the reader.
    void updateSegmentPlayerReader (const juce::File& wav)
    {
        segmentPlayer.stop();
        if (wav.existsAsFile())
        {
            auto reader = std::shared_ptr<juce::AudioFormatReader> (
                segmentFormatManager.createReaderFor (wav));
            segmentPlayer.setReader (std::move (reader));
        }
        else
        {
            segmentPlayer.setReader (nullptr);
        }
    }

    void setStatus (const juce::String& s) { if (onStatusChanged) onStatusChanged (s); }

    // Rebuild the one-line summary shown in the collapsed SetupHeader bar.
    void updateSetupSummary()
    {
        juce::String summary;
        if (auto* dev = deviceManager.getCurrentAudioDevice())
        {
            summary = dev->getName();
            const int chCount = dev->getActiveInputChannels().countNumberOfSetBits();
            if (chCount > 0)
                summary += " \xc2\xb7 " + juce::String (chCount) + " in";  // U+00B7 middle dot
        }
        else
        {
            summary = "No device";
        }
        if (getExportDestPath)
        {
            const juce::String dest = getExportDestPath().trim();
            if (dest.isNotEmpty())
            {
                // Shorten path to ~/... where possible.
                const juce::String home =
                    juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
                juce::String dispDest = dest;
                if (dest.startsWith (home))
                    dispDest = "~" + dest.substring (home.length());
                summary += " \xc2\xb7 " + dispDest;
            }
        }
        header.setSummaryText (summary);
    }

    // Auto-collapse header once a valid input device is selected (fires at most once).
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        updateSetupSummary();
        if (header.isExpanded() && ! hasAutoCollapsed)
        {
            if (auto* dev = deviceManager.getCurrentAudioDevice())
            {
                if (dev->getActiveInputChannels().countNumberOfSetBits() > 0)
                {
                    hasAutoCollapsed = true;
                    header.setExpanded (false);
                }
            }
        }
    }

    void addPiece()
    {
        stopRecording();
        captures.push_back ({});
        captures.back().name = "Piece " + juce::String ((int) captures.size());
        currentPieceIndex = (int) captures.size() - 1;
        refreshAll();
    }

    void switchPieceAbsolute (int index)
    {
        stopRecording();
        // Drop the current piece's WAV when switching — the new piece has its own take.
        if (currentContinuousWav.existsAsFile())
        {
            capturePanel.waveEditor.setSource ({}, 0.0, 0);
            updateSegmentPlayerReader ({});  // stop audition before deleting the WAV
            currentContinuousWav.deleteFile();
            currentContinuousWav = juce::File {};
        }
        currentPieceIndex = juce::jlimit (0, (int) captures.size() - 1, index);
        refreshAll();
    }

    void refreshAll()
    {
        std::vector<juce::String> names;
        names.reserve (captures.size());
        for (const auto& p : captures)
            names.push_back (p.name);
        pieceRail.rebuild (currentPieceIndex, names);
        capturePanel.setRecordingState (isRecording());
        recompute();
    }

    void toggleRecord()
    {
        if (isRecording()) { stopRecording(); return; }

        // Clear any existing WAV from a previous take before starting a new one.
        if (currentContinuousWav.existsAsFile())
        {
            capturePanel.waveEditor.setSource ({}, 0.0, 0);
            updateSegmentPlayerReader ({});  // stop audition before deleting the WAV
            currentContinuousWav.deleteFile();
            currentContinuousWav = juce::File {};
        }

        // Fresh take: clear provisional realtime estimates from any prior take
        // so the live coverage meter starts empty.
        provisionalPeaksDb.clear();

        engine.setMode (CaptureEngine::Mode::Recording);
        capturePanel.setRecordingState (true);
        setStatus ("Recording \"" + currentPiece().name + "\" - play it soft to hard.");

        // Wire live thumbnail refresh from the growing temp WAV.
        const juce::File liveWav = engine.getContinuousTempFile();
        if (liveWav != juce::File{})
        {
            double sr    = 48000.0;
            int    numCh = 1;
            if (auto* dev = deviceManager.getCurrentAudioDevice())
            {
                sr    = dev->getCurrentSampleRate();
                numCh = juce::jmax (1, dev->getActiveInputChannels().countNumberOfSetBits());
            }
            capturePanel.waveEditor.setChannelLabels (channelLabels);
            capturePanel.waveEditor.setSource (liveWav, sr, numCh);
            capturePanel.waveEditor.setLiveRecording (true);
        }
    }

    void stopRecording()
    {
        if (! isRecording())
        {
            capturePanel.setRecordingState (false);
            return;
        }

        // Finalise the WAV before setMode(Idle), which would otherwise discard it.
        const juce::File tempWav = engine.stopContinuousRecording();
        engine.setMode (CaptureEngine::Mode::Idle);
        capturePanel.setRecordingState (false);

        if (! tempWav.existsAsFile())
        {
            setStatus ("Stopped — no audio captured.");
            return;
        }

        // Stop the live thumbnail refresh — the recording is done.
        capturePanel.waveEditor.setLiveRecording (false);

        setStatus ("Analysing transients...");

        // Capture the piece index now; the user may switch pieces before the
        // async detection callback fires.
        const int pieceIndex = currentPieceIndex;

        detector.setFile (tempWav);

        // SafePointer goes null when ForgeContent is destroyed, preventing
        // use-after-free in the async callback chain.
        juce::Component::SafePointer<ForgeContent> safe (this);

        detector.runAsync ([safe, tempWav, pieceIndex] (OfflineTransientDetector::Result r) mutable
        {
            juce::MessageManager::callAsync ([safe, r = std::move (r), tempWav, pieceIndex] () mutable
            {
                if (safe == nullptr)
                {
                    tempWav.deleteFile();
                    return;
                }

                auto* self = safe.getComponent();

                if (! r.succeeded)
                {
                    self->setStatus ("Transient detection failed: " + r.error);
                    tempWav.deleteFile();
                    return;
                }

                // Guard: if the user switched pieces while detection was running, discard.
                if (pieceIndex != self->currentPieceIndex || pieceIndex >= (int) self->captures.size())
                {
                    tempWav.deleteFile();
                    return;
                }

                // Feed WAV + breakpoints to the waveform editor BEFORE extraction so
                // the thumbnail and energy scanner can start from the complete file.
                self->capturePanel.waveEditor.setLiveRecording (false);
                self->capturePanel.waveEditor.setChannelLabels (self->channelLabels);
                self->capturePanel.waveEditor.setSource (tempWav, r.sampleRate, r.numChannels);
                {
                    const std::vector<int> initVels (r.breakpoints.size(), 64);
                    self->capturePanel.waveEditor.setBreakpoints (r.breakpoints, r.segmentPeaksDb,
                                                                  initVels, r.totalSamples);
                }

                auto seg = extractSegments (tempWav, r);

                if (! seg.ok)
                {
                    self->setStatus ("Segment extraction failed: " + seg.error);
                    // Keep the WAV alive — editor is still useful for visual review.
                    self->currentContinuousWav = tempWav;
                    self->updateSegmentPlayerReader (tempWav);
                    return;
                }

                auto& piece = self->captures[(size_t) pieceIndex];
                piece.hits.clear();
                for (auto& h : seg.hits)
                    piece.hits.push_back (std::move (h));

                // Reconcile: authoritative hits now drive the meter; drop the
                // provisional realtime estimates so counts aren't double-shown.
                self->provisionalPeaksDb.clear();

                // Store WAV (don't delete) — WaveformEditor EnergyScanner is still reading it.
                // Deleted when piece resets or new recording starts (see toggleRecord / destructor).
                self->currentContinuousWav = tempWav;
                self->updateSegmentPlayerReader (tempWav);

                self->recompute();
                self->setStatus ("\"" + piece.name + "\" — "
                               + juce::String ((int) piece.hits.size()) + " hit(s) detected.");
            });
        });
    }

    void timerCallback() override
    {
        // Drain realtime onset events (FLA-157 / D10) into the provisional peak
        // list that drives the live coverage meter while recording. These are
        // advisory; the offline detector reconciles them to authoritative
        // velocities once recording stops (see the detection callback below,
        // which clears provisionalPeaksDb).
        if (isRecording())
        {
            OnsetEvent ev[64];
            int got = engine.drainOnsets (ev, 64);
            for (int i = 0; i < got; ++i)
                provisionalPeaksDb.push_back (ev[i].peakDb);
        }
        recompute();

        // Rebuild strips when device channel count changes; pump levels every tick.
        const int n = engine.channelCount();
        if (n != lastChannelCount)
        {
            while ((int) channelLabels.size() < n)
                channelLabels.push_back ("Mic " + juce::String ((int) channelLabels.size() + 1));
            lastChannelCount = n;
            capturePanel.rebuild (n, channelLabels,
                [this] (int c, juce::String t)
                {
                    if (c < (int) channelLabels.size())
                        channelLabels[(size_t) c] = t;
                });
            resized();
        }
        capturePanel.tickMeters (engine);
    }

    void recompute()
    {
        auto& piece = currentPiece();
        const bool rec = isRecording();

        // While recording, the meter is driven by the provisional realtime peaks
        // (advisory, no audio). Once stopped, it reflects the authoritative
        // audio-backed hits the offline detector produced.
        std::vector<float> peaks;
        if (rec)
        {
            peaks = provisionalPeaksDb;
        }
        else
        {
            peaks.reserve (piece.hits.size());
            for (const auto& h : piece.hits) peaks.push_back (h.peakDb);
        }

        float softDb = 1.0e9f, loudDb = -1.0e9f;
        for (float p : peaks)
        {
            softDb = juce::jmin (softDb, p);
            loudDb = juce::jmax (loudDb, p);
        }
        const bool hasRange = peaks.size() >= 2 && loudDb > softDb;

        std::vector<int> vels;
        vels.reserve (peaks.size());
        for (float p : peaks)
            vels.push_back (hasRange ? mapPeakToVelocity (p, softDb, loudDb) : 100);

        // Keep authoritative hit velocities in sync for the export/synth path
        // (peaks and piece.hits are parallel only when not recording).
        if (! rec)
            for (size_t i = 0; i < piece.hits.size() && i < vels.size(); ++i)
                piece.hits[i].midiVelocity = vels[i];

        int layers = 0, rr = 0;
        if (! rec && ! piece.hits.empty())
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

        capturePanel.progress.setAll ((int) peaks.size(), hasRange, softDb, loudDb,
                                      layers, rr, vels);

        if (rec)
            setStatus ("Recording \"" + piece.name + "\" - "
                       + juce::String ((int) peaks.size())
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
    // SegmentPlayer / AudioSourcePlayer are removed from the device manager
    // explicitly in the destructor before member destructors run.
    juce::AudioDeviceManager                             deviceManager;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;

    SetupHeader  header;
    PieceRail    pieceRail;
    CapturePanel capturePanel;

    std::vector<juce::String> channelLabels;
    int                       lastChannelCount = 0;
    CaptureEngine             engine;
    OfflineTransientDetector  detector;

    // Segment audition (FLA-163): format manager + player + JUCE source adapter.
    juce::AudioFormatManager  segmentFormatManager;
    SegmentPlayer             segmentPlayer;
    juce::AudioSourcePlayer   audioSourcePlayer;
    std::vector<PieceCapture> captures;
    std::vector<float>        provisionalPeaksDb;   // live realtime onset peaks (FLA-157)
    int                       currentPieceIndex = 0;
    SynthOptions              options;
    juce::String              kitName = "FlamForge Kit";
    juce::ApplicationProperties appProps;
    juce::File                lastExportedKit;
    bool                      hasAutoCollapsed = false;
    // Temp WAV from the most recent continuous recording — kept alive while
    // WaveformEditor's EnergyScanner and AudioThumbnail still reference it.
    // Deleted on new recording start, piece switch, or app teardown.
    juce::File                currentContinuousWav;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForgeContent)
};

// ---------------------------------------------------------------------------
// MainComponent -- hosts ForgeContent (scrollable viewport) pinned above
// ExportBar (fixed bottom strip). ExportBar lives outside the viewport so it
// is always visible even when the device-selector area requires scrolling.
//
// Callback wiring between ForgeContent and ExportBar is done here so neither
// zone owns the other directly.
// ---------------------------------------------------------------------------
class MainComponent : public juce::Component
{
public:
    MainComponent()
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, true);
        addAndMakeVisible (viewport);
        addAndMakeVisible (exportBar);

        // --- ForgeContent -> ExportBar ---
        content.onStatusChanged   = [this] (const juce::String& s) { exportBar.setStatus (s); };
        content.onExportSucceeded = [this] { exportBar.showReveal(); };
        content.getExportDestPath = [this] { return exportBar.getDestPath(); };

        // --- ExportBar -> ForgeContent ---
        exportBar.onExportRequested = [this] { content.onExport(); };
        exportBar.onRevealRequested = [this] { content.onReveal(); };
        exportBar.onPathCommitted   = [this] (const juce::String& p) { content.persistDestPath (p); };
        exportBar.onBrowserVisibilityChanged = [this] (bool) { resized(); };

        exportBar.setDestPath (content.loadPersistedDest().getFullPathName());
        exportBar.setStatus ("Ready. Choose your input above, name the drum, then Record.");

        setSize (760, juce::jmin (content.naturalHeight() + exportBar.naturalHeight(), 940));
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0f12)); }

    void resized() override
    {
        auto b = getLocalBounds();
        const int exportH = exportBar.naturalHeight();
        exportBar.setBounds (b.removeFromBottom (exportH));
        viewport.setBounds (b);
        const int w = viewport.getMaximumVisibleWidth();
        const int contentW = juce::jmax (640, w);
        content.setSize (contentW, juce::jmax (content.naturalHeight(), viewport.getHeight()));
    }

private:
    juce::Viewport viewport;
    ForgeContent   content;
    ExportBar      exportBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

} // namespace flamforge
