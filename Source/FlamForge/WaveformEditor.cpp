// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "WaveformEditor.h"
#include <cmath>

namespace flamforge
{

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------
static float ampToDb (float amp) noexcept
{
    return amp > 0.0f ? juce::Decibels::gainToDecibels (amp) : -200.0f;
}

static int mapToVelocity (float db, float softDb, float loudDb)
{
    if (loudDb <= softDb) return 100;
    const float t = juce::jlimit (0.0f, 1.0f, (db - softDb) / (loudDb - softDb));
    return juce::jlimit (1, 127, (int) std::round (1.0f + t * 126.0f));
}

// ===========================================================================
// EnergyScanner
// ===========================================================================
WaveformEditor::EnergyScanner::EnergyScanner (WaveformEditor& o)
    : juce::Thread ("FlamForge_EnvelopeScanner"), owner (o)
{}

void WaveformEditor::EnergyScanner::setSource (const juce::File& f, double sr)
{
    sourceFile = f;
    fileSr     = sr;
}

void WaveformEditor::EnergyScanner::run()
{
    if (! sourceFile.existsAsFile()) return;

    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (mgr.createReaderFor (sourceFile));
    if (! reader) return;

    const int64_t total     = reader->lengthInSamples;
    const int     numCh     = reader->numChannels;
    const int     numFrames = (int) ((total + kHop - 1) / kHop);

    std::vector<float> env;
    env.reserve ((size_t) numFrames);

    juce::AudioBuffer<float> buf (numCh, kHop);

    for (int f = 0; f < numFrames; ++f)
    {
        if (threadShouldExit()) return;

        const int64_t startSamp = (int64_t) f * kHop;
        const int     n         = (int) juce::jmin ((int64_t) kHop, total - startSamp);
        if (n <= 0) break;

        reader->read (&buf, 0, n, startSamp, true, true);

        float peak = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            const float* data = buf.getReadPointer (c);
            for (int i = 0; i < n; ++i)
                peak = juce::jmax (peak, std::fabs (data[i]));
        }
        env.push_back (ampToDb (peak));
    }

    // Post result to message thread; use alive flag to guard the raw owner pointer.
    auto aliveFlag = owner.alive;
    auto* ownerPtr = &owner;
    const double sr = fileSr;

    juce::MessageManager::callAsync (
        [ownerPtr, aliveFlag, env = std::move (env), sr]() mutable
        {
            if (aliveFlag->load (std::memory_order_acquire))
                ownerPtr->onEnvelopeReady (std::move (env), sr);
        });
}

// ===========================================================================
// WaveformEditor
// ===========================================================================
WaveformEditor::WaveformEditor()
    : thumbnail (256, formatManager, thumbnailCache)
{
    formatManager.registerBasicFormats();
    thumbnail.addChangeListener (this);

    expandBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x96\xbe Lanes"));
    expandBtn.setColour (juce::TextButton::buttonColourId,    juce::Colour (0xff1b1f25));
    expandBtn.setColour (juce::TextButton::textColourOffId,   juce::Colours::white.withAlpha (0.55f));
    expandBtn.setColour (juce::TextButton::textColourOnId,    juce::Colours::white.withAlpha (0.80f));
    expandBtn.onClick = [this] { setExpanded (! expanded); };
    addAndMakeVisible (expandBtn);
    setWantsKeyboardFocus (true);
}

WaveformEditor::~WaveformEditor()
{
    alive->store (false, std::memory_order_release);
    scanner.stopThread (2000);
    thumbnail.removeChangeListener (this);
    stopTimer();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void WaveformEditor::setSource (const juce::File& f, double sr, int numCh)
{
    sampleRate  = sr;
    numChannels = numCh;
    currentFile = f;

    // Always reset zoom/pan so a new source starts at the full view.
    zoomFactor     = 1.0f;
    viewOffsetFrac = 0.0f;

    scanner.stopThread (2000);
    energyEnvelope.clear();

    if (f.existsAsFile())
    {
        thumbnail.setSource (new juce::FileInputSource (f));
        scanner.setSource (f, sr);
        scanner.startThread (juce::Thread::Priority::low);
    }
    else
    {
        thumbnail.clear();
        stopTimer();
    }
    repaint();
}

void WaveformEditor::setLiveRecording (bool live)
{
    if (live) startTimerHz (2);
    else      stopTimer();
}

void WaveformEditor::setBreakpoints (const std::vector<int64_t>& bps,
                                     const std::vector<float>&   peaksDb,
                                     const std::vector<int>&     segVels,
                                     int64_t                     totalSmp)
{
    breakpoints        = bps;
    segmentPeaksDb     = peaksDb;
    segmentVelocities  = segVels;
    totalSamples       = totalSmp;
    segmentDisabled.assign (bps.size(), false);

    // Initialise per-segment fade arrays with defaults.
    segmentFadeInMs.assign (bps.size(), 1.0f);
    segmentFadeOutMs.resize (bps.size());
    for (int i = 0; i < (int) bps.size(); ++i)
    {
        const int64_t segEnd = (i + 1 < (int) bps.size()) ? bps[(size_t)(i + 1)] : totalSmp;
        const double  durMs  = (double)(segEnd - bps[(size_t) i]) / sampleRate * 1000.0;
        segmentFadeOutMs[(size_t) i] = (float) juce::jlimit (2.0, 200.0, durMs * 0.05);
    }

    repaint();
}

void WaveformEditor::setChannelLabels (const std::vector<juce::String>& labels)
{
    channelLabels = labels;
    repaint();
}

void WaveformEditor::setSnapToZeroCrossing (bool snap)
{
    snapZeroCrossing = snap;
}

void WaveformEditor::setExpanded (bool e)
{
    expanded = e;
    // ▾ = collapsed (show lanes), ▴ = expanded (hide lanes)
    expandBtn.setButtonText (e ? juce::CharPointer_UTF8 ("\xe2\x96\xb4 Lanes")
                               : juce::CharPointer_UTF8 ("\xe2\x96\xbe Lanes"));
    repaint();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
juce::Rectangle<float> WaveformEditor::waveRect() const
{
    return getLocalBounds().toFloat().withTrimmedTop (kAccBtnH).reduced (2.0f, 2.0f);
}

juce::Rectangle<float> WaveformEditor::minimapRect() const
{
    auto r = waveRect();
    return r.removeFromBottom (kMinimapH);
}

// Both coordinate helpers account for the current zoom/pan state:
//   visibleStart = viewOffsetFrac * totalSamples
//   visibleEnd   = visibleStart + totalSamples / zoomFactor
// Samples outside the visible window map to pixel positions outside waveRect —
// they are naturally clipped by JUCE's painter clipping, so segment fills and
// breakpoints outside the visible window simply do not appear.
float WaveformEditor::sampleToX (int64_t s) const
{
    if (totalSamples <= 0) return 0.0f;
    const auto  r            = waveRect();
    const float visibleFrac  = 1.0f / zoomFactor;
    const float frac         = (float) s / (float) totalSamples;
    return r.getX() + (frac - viewOffsetFrac) / visibleFrac * r.getWidth();
}

int64_t WaveformEditor::xToSample (float x) const
{
    if (totalSamples <= 0) return 0;
    const auto  r           = waveRect();
    const float visibleFrac = 1.0f / zoomFactor;
    const float t           = (x - r.getX()) / r.getWidth();   // 0..1 across visible window
    const float sampleFrac  = juce::jlimit (0.0f, 1.0f, viewOffsetFrac + t * visibleFrac);
    return (int64_t) (sampleFrac * (float) totalSamples);
}

void WaveformEditor::clampViewOffset()
{
    const float maxOffset = juce::jmax (0.0f, 1.0f - 1.0f / zoomFactor);
    viewOffsetFrac = juce::jlimit (0.0f, maxOffset, viewOffsetFrac);
}

// ---------------------------------------------------------------------------
// Zoom API
// ---------------------------------------------------------------------------
void WaveformEditor::resetZoom()
{
    zoomFactor     = 1.0f;
    viewOffsetFrac = 0.0f;
    repaint();
}

void WaveformEditor::setZoom (float factor, float centreXPixel)
{
    const float newZoom = juce::jlimit (1.0f, kMaxZoom, factor);
    if (newZoom == zoomFactor) return;

    const auto  r = waveRect();
    // Compute the sample fraction currently under the cursor (pre-zoom).
    const float relX              = centreXPixel - r.getX();
    const float sampleFracAtCursor = viewOffsetFrac + (relX / r.getWidth()) * (1.0f / zoomFactor);

    // Update zoom and reposition so the same sample fraction stays under the cursor.
    zoomFactor     = newZoom;
    viewOffsetFrac = sampleFracAtCursor - (relX / r.getWidth()) * (1.0f / zoomFactor);
    clampViewOffset();
    repaint();
}

// ---------------------------------------------------------------------------
// Interaction helpers
// ---------------------------------------------------------------------------
int WaveformEditor::findBreakpointNear (float x) const
{
    int   closest = -1;
    float minDist = kMarkerGrabPx;
    for (int i = 0; i < (int) breakpoints.size(); ++i)
    {
        const float dist = std::fabs (sampleToX (breakpoints[i]) - x);
        if (dist < minDist) { minDist = dist; closest = i; }
    }
    return closest;
}

int WaveformEditor::findSegmentAt (float x) const
{
    if (breakpoints.empty() || totalSamples <= 0) return -1;
    const int64_t clickSample = xToSample (x);
    // upper_bound gives the first breakpoint strictly greater than clickSample;
    // stepping back one gives the segment that starts at or before the click.
    auto it = std::upper_bound (breakpoints.begin(), breakpoints.end(), clickSample);
    if (it == breakpoints.begin()) return -1; // click is before the first breakpoint
    --it;
    return (int) (it - breakpoints.begin());
}

void WaveformEditor::insertBreakpoint (int64_t sample)
{
    sample = juce::jlimit ((int64_t) 1, juce::jmax ((int64_t) 1, totalSamples - 1), sample);

    auto it = std::lower_bound (breakpoints.begin(), breakpoints.end(), sample);
    if (it != breakpoints.end() && *it == sample) return; // duplicate

    const int idx = (int) (it - breakpoints.begin());
    breakpoints.insert (it, sample);
    segmentPeaksDb.insert    (segmentPeaksDb.begin()    + idx, -60.0f);
    segmentVelocities.insert (segmentVelocities.begin() + idx, 64);
    segmentDisabled.insert   (segmentDisabled.begin()   + idx, false);

    // Fade defaults for the new segment (proportional fade-out based on new segment length).
    const int64_t segEnd   = (idx + 1 < (int) breakpoints.size())
                           ? breakpoints[(size_t)(idx + 1)] : totalSamples;
    const double  durMs    = (double)(segEnd - sample) / sampleRate * 1000.0;
    const float   defFadeOut = (float) juce::jlimit (2.0, 200.0, durMs * 0.05);
    segmentFadeInMs.insert  (segmentFadeInMs.begin()  + idx, 1.0f);
    segmentFadeOutMs.insert (segmentFadeOutMs.begin() + idx, defFadeOut);

    recomputeVelocities();
}

void WaveformEditor::removeBreakpoint (int idx)
{
    if (idx < 0 || idx >= (int) breakpoints.size()) return;
    breakpoints.erase (breakpoints.begin() + idx);
    if (idx < (int) segmentPeaksDb.size())
        segmentPeaksDb.erase (segmentPeaksDb.begin() + idx);
    if (idx < (int) segmentVelocities.size())
        segmentVelocities.erase (segmentVelocities.begin() + idx);
    if (idx < (int) segmentDisabled.size())
        segmentDisabled.erase (segmentDisabled.begin() + idx);
    if (idx < (int) segmentFadeInMs.size())
        segmentFadeInMs.erase (segmentFadeInMs.begin() + idx);
    if (idx < (int) segmentFadeOutMs.size())
        segmentFadeOutMs.erase (segmentFadeOutMs.begin() + idx);
    if (draggingIdx == idx)
        draggingIdx = -1;
    if (dragFadeSeg == idx)
        dragFadeSeg = -1;
    recomputeVelocities();
}

float WaveformEditor::envelopePeak (int frameA, int frameB) const
{
    if (energyEnvelope.empty() || frameA >= frameB) return -60.0f;
    frameA = juce::jlimit (0, (int) energyEnvelope.size() - 1, frameA);
    frameB = juce::jlimit (0, (int) energyEnvelope.size(),     frameB);
    float peak = -200.0f;
    for (int f = frameA; f < frameB; ++f)
        peak = juce::jmax (peak, energyEnvelope[f]);
    return peak;
}

void WaveformEditor::recomputeVelocities()
{
    if (breakpoints.empty())
    {
        segmentVelocities.clear();
        if (onBreakpointsChanged) onBreakpointsChanged ({}, {});
        repaint();
        return;
    }

    // Update peaks from energy envelope when available; otherwise keep stored peaks.
    if (! energyEnvelope.empty())
    {
        segmentPeaksDb.resize (breakpoints.size());
        for (int i = 0; i < (int) breakpoints.size(); ++i)
        {
            const int64_t segEnd = (i + 1 < (int) breakpoints.size())
                                 ? breakpoints[i + 1] : totalSamples;
            segmentPeaksDb[i] = envelopePeak ((int) (breakpoints[i] / kHop),
                                              (int) ((segEnd + kHop - 1) / kHop));
        }
    }

    // Derive velocity range from measured peaks
    float softDb = 1.0e9f, loudDb = -1.0e9f;
    for (float p : segmentPeaksDb)
    {
        softDb = juce::jmin (softDb, p);
        loudDb = juce::jmax (loudDb, p);
    }
    if (softDb >= loudDb) softDb = loudDb - 1.0f;

    segmentVelocities.resize (breakpoints.size());
    for (int i = 0; i < (int) breakpoints.size(); ++i)
    {
        const float p = (i < (int) segmentPeaksDb.size()) ? segmentPeaksDb[i] : -60.0f;
        segmentVelocities[i] = mapToVelocity (p, softDb, loudDb);
    }

    if (onBreakpointsChanged)
        onBreakpointsChanged (breakpoints, segmentVelocities);

    repaint();
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------
void WaveformEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto wr = waveRect();
    if (! wr.contains (e.position)) return;

    // Minimap click → jump view (higher priority than breakpoint logic).
    const auto mr = minimapRect();
    if (mr.contains (e.position))
    {
        const float clickFrac  = (e.x - mr.getX()) / mr.getWidth();
        const float halfWindow = 0.5f / zoomFactor;
        viewOffsetFrac = juce::jlimit (0.0f,
                                       juce::jmax (0.0f, 1.0f - 1.0f / zoomFactor),
                                       clickFrac - halfWindow);
        repaint();
        return;
    }

    // Pan mode: middle button OR spacebar + left button.
    const bool spaceDown = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey);
    if (e.mods.isMiddleButtonDown() || (e.mods.isLeftButtonDown() && spaceDown))
    {
        panDragging            = true;
        panDragStartX          = e.x;
        panDragStartOffsetFrac = viewOffsetFrac;
        return;
    }

    // Right-click near breakpoint → remove; right-click segment body → toggle disabled.
    if (e.mods.isRightButtonDown())
    {
        const int bpIdx = findBreakpointNear (e.x);
        if (bpIdx >= 0)
        {
            removeBreakpoint (bpIdx);
        }
        else
        {
            const int segIdx = findSegmentAt (e.x);
            if (segIdx >= 0 && segIdx < (int) segmentDisabled.size())
            {
                segmentDisabled[segIdx] = ! segmentDisabled[segIdx];
                if (onDisabledChanged) onDisabledChanged (segmentDisabled);
                repaint();
            }
        }
        return;
    }

    // Ctrl/Cmd + left-click → insert breakpoint (if not landing on an existing one).
    if (e.mods.isLeftButtonDown()
        && (e.mods.isCtrlDown() || e.mods.isCommandDown()))
    {
        if (findBreakpointNear (e.x) < 0)
            insertBreakpoint (xToSample (e.x));
        return;
    }

    // Plain left-click: drag breakpoint if near one; otherwise check fade handles,
    // then defer audition to mouseUp so a horizontal drag can become a pan first.
    draggingIdx   = findBreakpointNear (e.x);
    dragStartX    = e.x;
    dragStartY    = e.y;
    dragOffDelete = false;
    dragFadeSeg   = -1;

    if (draggingIdx < 0)
    {
        // Priority 2: fade handle hit-test.
        // Each handle center is at (x0 + fadeInPx, wr.getY() + kFadeHandleRadius)
        // for fade-in, and (x1 - fadeOutPx, wr.getY() + kFadeHandleRadius) for fade-out.
        const auto  wr      = waveRect();
        const float hitRad  = (float) kFadeHandleRadius * 2.0f; // generous hit radius
        const float hitRad2 = hitRad * hitRad;

        for (int i = 0; i < (int) breakpoints.size() && dragFadeSeg < 0; ++i)
        {
            const int64_t segEnd = (i + 1 < (int) breakpoints.size())
                                 ? breakpoints[(size_t)(i + 1)] : totalSamples;
            const float x0 = sampleToX (breakpoints[i]);
            const float x1 = sampleToX (segEnd);

            // Fade-in handle
            const float thisFadeInMs = (i < (int) segmentFadeInMs.size())
                                     ? segmentFadeInMs[(size_t) i] : 1.0f;
            const int   fadeInSmp = (int) std::round ((double) thisFadeInMs * sampleRate / 1000.0);
            const float hiX = x0 + (sampleToX (breakpoints[i] + fadeInSmp) - x0);
            const float hiY = wr.getY() + (float) kFadeHandleRadius;
            {
                const float dx = e.x - hiX, dy = e.y - hiY;
                if (dx * dx + dy * dy <= hitRad2) { dragFadeSeg = i; dragFadeIsIn = true; }
            }

            // Fade-out handle
            if (dragFadeSeg < 0)
            {
                const float thisFadeOutMs = (i < (int) segmentFadeOutMs.size())
                                          ? segmentFadeOutMs[(size_t) i]
                                          : 1.0f; // fallback ignored — array always populated
                const int   fadeOutSmp = (int) std::round ((double) thisFadeOutMs * sampleRate / 1000.0);
                const float hoX = x1 - (sampleToX (segEnd) - sampleToX (segEnd - fadeOutSmp));
                const float hoY = wr.getY() + (float) kFadeHandleRadius;
                const float dx = e.x - hoX, dy = e.y - hoY;
                if (dx * dx + dy * dy <= hitRad2) { dragFadeSeg = i; dragFadeIsIn = false; }
            }
        }

        if (dragFadeSeg < 0)
        {
            // Priority 4: defer audition (fires on mouseUp unless panned).
            pendingAuditionSegIdx  = findSegmentAt (e.x);
            pendingAuditionPanned  = false;
            panDragStartX          = e.x;
            panDragStartOffsetFrac = viewOffsetFrac;
        }
    }
}

void WaveformEditor::mouseDrag (const juce::MouseEvent& e)
{
    // Pan drag takes full priority over breakpoint operations.
    if (panDragging)
    {
        const float dx = e.x - panDragStartX;
        const auto  r  = waveRect();
        // Dragging right moves the content right (shows earlier samples), so subtract.
        viewOffsetFrac = panDragStartOffsetFrac - dx / r.getWidth() / zoomFactor;
        clampViewOffset();
        repaint();
        return;
    }

    // Deferred-audition: check if drag has exceeded pan threshold yet.
    // Until it does, suppress any other drag action; once exceeded, activate pan mode.
    if (pendingAuditionSegIdx >= 0 && ! pendingAuditionPanned)
    {
        if (std::fabs (e.x - panDragStartX) > kDragPanThreshPx)
        {
            pendingAuditionPanned = true;
            panDragging           = true;
            // panDragStartX / panDragStartOffsetFrac already set in mouseDown
        }
        return;
    }

    // Fade handle drag: adjust per-segment fade-in or fade-out from current x position.
    if (dragFadeSeg >= 0)
    {
        const int64_t bp     = breakpoints[(size_t) dragFadeSeg];
        const int64_t segEnd = (dragFadeSeg + 1 < (int) breakpoints.size())
                             ? breakpoints[(size_t)(dragFadeSeg + 1)] : totalSamples;

        if (dragFadeIsIn)
        {
            const int64_t dragSmp   = juce::jlimit (bp, segEnd, xToSample (e.x));
            const float   newFadeMs = (float)(dragSmp - bp) * 1000.0f / (float) sampleRate;
            segmentFadeInMs[(size_t) dragFadeSeg] = juce::jlimit (0.5f, 500.0f, newFadeMs);
        }
        else
        {
            const int64_t dragSmp   = juce::jlimit (bp, segEnd, xToSample (e.x));
            const float   newFadeMs = (float)(segEnd - dragSmp) * 1000.0f / (float) sampleRate;
            segmentFadeOutMs[(size_t) dragFadeSeg] = juce::jlimit (0.5f, 500.0f, newFadeMs);
        }

        if (onFadesChanged) onFadesChanged (segmentFadeInMs, segmentFadeOutMs);
        repaint();
        return;
    }

    if (draggingIdx >= 0)
    {
        // Vertical drag-off → mark for delete on release
        if (std::fabs (e.y - dragStartY) > kDeleteDragOffPx)
            dragOffDelete = true;

        if (! dragOffDelete)
        {
            // Clamp to the gap between neighbouring breakpoints (1-sample margin)
            const int64_t lo = (draggingIdx > 0)
                             ? breakpoints[draggingIdx - 1] + 1 : 0;
            const int64_t hi = (draggingIdx + 1 < (int) breakpoints.size())
                             ? breakpoints[draggingIdx + 1] - 1 : totalSamples - 1;

            int64_t newSamp = juce::jlimit (lo, hi, xToSample (e.x));
            breakpoints[draggingIdx] = newSamp;
            recomputeVelocities(); // live colour update via energy envelope
        }
        else
        {
            repaint(); // show red delete indicator
        }
    }
}

void WaveformEditor::mouseUp (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);

    // Deferred-audition: fire audition if the click never became a pan, then clean up.
    if (pendingAuditionSegIdx >= 0)
    {
        if (! pendingAuditionPanned && onSegmentAudition)
        {
            const int64_t segEnd = (pendingAuditionSegIdx + 1 < (int) breakpoints.size())
                                 ? breakpoints[(size_t) (pendingAuditionSegIdx + 1)]
                                 : totalSamples;
            onSegmentAudition (breakpoints[(size_t) pendingAuditionSegIdx], segEnd);
        }
        pendingAuditionSegIdx = -1;
        pendingAuditionPanned = false;
        panDragging           = false;
        return;
    }

    if (panDragging)
    {
        panDragging = false;
        return;
    }

    if (dragFadeSeg >= 0)
    {
        dragFadeSeg = -1;
        return;
    }

    if (draggingIdx >= 0)
    {
        if (dragOffDelete)
            removeBreakpoint (draggingIdx);
        // onBreakpointsChanged already fired inside recomputeVelocities()
        draggingIdx   = -1;
        dragOffDelete = false;
        repaint();
    }
}

void WaveformEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-click on the minimap resets zoom to 1×.
    if (minimapRect().contains (e.position))
    {
        resetZoom();
        return;
    }

    const auto wr = waveRect();
    if (! wr.contains (e.position)) return;
    insertBreakpoint (xToSample (e.x));
}

void WaveformEditor::mouseWheelMove (const juce::MouseEvent& e,
                                     const juce::MouseWheelDetails& wheel)
{
    const auto wr = waveRect();
    if (! wr.contains (e.position)) return;

    if (wheel.deltaY != 0.0f)
    {
        // Vertical scroll → zoom centred on cursor
        setZoom (zoomFactor * (1.0f + wheel.deltaY * 0.5f), e.x);
    }
    else if (wheel.deltaX != 0.0f)
    {
        // Horizontal scroll (two-finger swipe on trackpads) → pan
        viewOffsetFrac -= wheel.deltaX * (1.0f / zoomFactor) * 0.2f;
        clampViewOffset();
        repaint();
    }
}

bool WaveformEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onAuditionStop) onAuditionStop();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------
void WaveformEditor::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Component background
    g.setColour (juce::Colour (0xff0d0f12));
    g.fillRoundedRectangle (b, 4.0f);

    // Accordion button bar background
    {
        auto barR = b.removeFromTop (kAccBtnH);
        g.setColour (juce::Colour (0xff14171c));
        g.fillRect (barR);
    }

    const auto wr = waveRect();

    // Placeholder when nothing is loaded yet
    if (totalSamples <= 0 || thumbnail.getTotalLength() <= 0.0)
    {
        g.setColour (juce::Colour (0xff14171c));
        g.fillRect (wr);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("Start recording to see waveform",
                    wr.toNearestInt(), juce::Justification::centred);
        return;
    }

    const double totalSec       = (double) totalSamples / sampleRate;
    const double visibleStartSec = viewOffsetFrac * totalSec;
    const double visibleEndSec   = visibleStartSec + totalSec / (double) zoomFactor;
    const int    numLanes  = (expanded && numChannels > 1) ? numChannels : 1;
    const float  laneH     = wr.getHeight() / (float) numLanes;

    // --- Draw lanes --------------------------------------------------------
    for (int lane = 0; lane < numLanes; ++lane)
    {
        const auto laneR = juce::Rectangle<float> (
            wr.getX(), wr.getY() + (float) lane * laneH, wr.getWidth(), laneH);

        // Lane background
        g.setColour (juce::Colour (0xff14171c));
        g.fillRect (laneR);

        // Waveform (muted blue-grey; segment fills go on top).
        // Pass visibleStartSec/visibleEndSec so JUCE renders only the zoomed window.
        g.setColour (juce::Colour (0xff4a5568));
        const auto waveDrawR = laneR.reduced (0.0f, 2.0f).toNearestInt();

        if (expanded && numLanes > 1 && thumbnail.getNumChannels() > 0)
        {
            const int ch = juce::jlimit (0, thumbnail.getNumChannels() - 1, lane);
            thumbnail.drawChannel (g, waveDrawR, visibleStartSec, visibleEndSec, ch, 1.0f);
        }
        else
        {
            thumbnail.drawChannels (g, waveDrawR, visibleStartSec, visibleEndSec, 1.0f);
        }

        // Channel label (expanded view only)
        if (expanded && lane < (int) channelLabels.size())
        {
            g.setColour (juce::Colours::white.withAlpha (0.42f));
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (channelLabels[lane],
                        laneR.reduced (4.0f, 0.0f).removeFromLeft ((float) kLaneLabelW),
                        juce::Justification::centredLeft);
        }
    }

    // Lane separators (expanded view)
    if (expanded && numLanes > 1)
    {
        g.setColour (juce::Colour (0xff0d0f12).withAlpha (0.75f));
        for (int i = 1; i < numLanes; ++i)
        {
            const float y = wr.getY() + (float) i * laneH;
            g.fillRect (juce::Rectangle<float> (wr.getX(), y - 0.5f, wr.getWidth(), 1.0f));
        }
    }

    // Outer waveform border
    g.setColour (juce::Colour (0xff23272e));
    g.drawRoundedRectangle (wr.expanded (1.0f), 2.0f, 1.0f);

    // --- Segment velocity fills + fade envelope overlay (drawn over waveform) ----
    for (int i = 0; i < (int) breakpoints.size(); ++i)
    {
        const int64_t segEnd = (i + 1 < (int) breakpoints.size())
                             ? breakpoints[i + 1] : totalSamples;
        const float x0 = sampleToX (breakpoints[i]);
        const float x1 = sampleToX (segEnd);

        if (i < (int) segmentVelocities.size())
        {
            g.setColour (velocityColour (segmentVelocities[i]).withAlpha (0.20f));
            g.fillRect (juce::Rectangle<float> (x0, wr.getY(), x1 - x0, wr.getHeight()));

            // Fade-in triangle (left edge): matches per-segment fade-in ms.
            const float thisSegFadeInMs = (i < (int) segmentFadeInMs.size())
                                        ? segmentFadeInMs[(size_t) i] : 1.0f;
            const int   fadeInSmp = (int) std::round ((double) thisSegFadeInMs * sampleRate / 1000.0);
            const float fadeInPx  = sampleToX (breakpoints[i] + fadeInSmp)
                                  - sampleToX (breakpoints[i]);

            juce::Path fadeInPath;
            fadeInPath.startNewSubPath (x0, wr.getBottom());
            fadeInPath.lineTo (x0, wr.getY());
            fadeInPath.lineTo (x0 + fadeInPx, wr.getY());
            fadeInPath.closeSubPath();
            g.setColour (velocityColour (segmentVelocities[i]).withAlpha (0.30f));
            g.fillPath (fadeInPath);

            // Drag handle: small circle at the tip of the fade-in triangle.
            const float hiX = x0 + fadeInPx;
            const float hiY = wr.getY() + (float) kFadeHandleRadius;
            g.setColour (velocityColour (segmentVelocities[i]).brighter (0.4f).withAlpha (0.75f));
            g.fillEllipse (hiX - (float) kFadeHandleRadius, hiY - (float) kFadeHandleRadius,
                           (float) kFadeHandleRadius * 2.0f, (float) kFadeHandleRadius * 2.0f);

            // Fade-out triangle (right edge): matches per-segment fade-out ms.
            const double segDurMs = (double)(segEnd - breakpoints[i]) / sampleRate * 1000.0;
            const float  thisSegFadeOutMs = (i < (int) segmentFadeOutMs.size())
                                          ? segmentFadeOutMs[(size_t) i]
                                          : (float) juce::jlimit (2.0, 200.0, segDurMs * 0.05);
            const int    fadeOutSmp = (int) std::round ((double) thisSegFadeOutMs * sampleRate / 1000.0);
            const float  fadeOutPx  = sampleToX (segEnd) - sampleToX (segEnd - fadeOutSmp);

            juce::Path fadeOutPath;
            fadeOutPath.startNewSubPath (x1, wr.getBottom());
            fadeOutPath.lineTo (x1, wr.getY());
            fadeOutPath.lineTo (x1 - fadeOutPx, wr.getY());
            fadeOutPath.closeSubPath();
            g.setColour (velocityColour (segmentVelocities[i]).withAlpha (0.30f));
            g.fillPath (fadeOutPath);

            // Drag handle: small circle at the tip of the fade-out triangle.
            const float hoX = x1 - fadeOutPx;
            const float hoY = wr.getY() + (float) kFadeHandleRadius;
            g.setColour (velocityColour (segmentVelocities[i]).brighter (0.4f).withAlpha (0.75f));
            g.fillEllipse (hoX - (float) kFadeHandleRadius, hoY - (float) kFadeHandleRadius,
                           (float) kFadeHandleRadius * 2.0f, (float) kFadeHandleRadius * 2.0f);
        }

        if (i < (int) segmentDisabled.size() && segmentDisabled[i])
        {
            g.saveState();
            g.reduceClipRegion (juce::Rectangle<int> ((int) x0, (int) wr.getY(),
                                                      (int) (x1 - x0), (int) wr.getHeight()));
            g.setColour (juce::Colour (0xaa000000));
            g.fillRect (juce::Rectangle<float> (x0, wr.getY(), x1 - x0, wr.getHeight()));
            g.setColour (juce::Colour (0x33ffffff));
            const float stripeSpacing = 10.0f;
            for (float sx = x0 - wr.getHeight(); sx < x1 + stripeSpacing; sx += stripeSpacing)
                g.drawLine (sx, wr.getBottom(), sx + wr.getHeight(), wr.getY(), 1.0f);
            g.restoreState();
        }
    }

    // --- Breakpoint markers — full-height lines spanning all lanes ---------
    for (int i = 0; i < (int) breakpoints.size(); ++i)
    {
        const float x          = sampleToX (breakpoints[i]);
        const bool  dragging   = (i == draggingIdx);
        const bool  willDelete = dragging && dragOffDelete;

        const juce::Colour lineCol = willDelete
            ? juce::Colour (0xffd0473f)
            : (dragging ? juce::Colours::white
                        : juce::Colours::white.withAlpha (0.72f));

        // Vertical line
        g.setColour (lineCol);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, wr.getY(), 2.0f, wr.getHeight()));

        // Diamond handle at top (easy grab target)
        {
            juce::Path diamond;
            const float hx = x, hy = wr.getY() + 7.0f;
            diamond.startNewSubPath (hx, hy - 6.0f); // top
            diamond.lineTo (hx + 5.0f, hy);          // right
            diamond.lineTo (hx, hy + 6.0f);          // bottom
            diamond.lineTo (hx - 5.0f, hy);          // left
            diamond.closeSubPath();

            g.setColour (willDelete ? juce::Colour (0xffd0473f)
                                    : (dragging ? juce::Colour (0xffe0b341)
                                                : juce::Colours::white.withAlpha (0.85f)));
            g.fillPath (diamond);
        }

        // Velocity label — shown just right of the marker
        if (i < (int) segmentVelocities.size())
        {
            const int  vel    = segmentVelocities[i];
            const auto velCol = velocityColour (vel);
            g.setColour (velCol.withAlpha (0.80f));
            g.setFont (juce::Font (juce::FontOptions (9.5f)));
            g.drawText (juce::String (vel),
                        juce::Rectangle<float> (x + 3.0f, wr.getY() + 2.0f, 26.0f, 12.0f),
                        juce::Justification::centredLeft);
        }
    }

    // --- Minimap -----------------------------------------------------------
    paintMinimap (g);
}

void WaveformEditor::resized()
{
    expandBtn.setBounds (getLocalBounds().removeFromTop ((int) kAccBtnH)
                                         .withTrimmedRight (2));
}

// ---------------------------------------------------------------------------
// Minimap
// ---------------------------------------------------------------------------
void WaveformEditor::paintMinimap (juce::Graphics& g)
{
    const auto mr = minimapRect();

    // Dark background so the minimap is always readable over the waveform.
    g.setColour (juce::Colour (0xff0a0c0f).withAlpha (0.88f));
    g.fillRect (mr);

    // Full-duration waveform thumbnail at reduced opacity.
    if (thumbnail.getTotalLength() > 0.0)
    {
        const double totalSec = (double) totalSamples / sampleRate;
        g.setColour (juce::Colour (0xff4a5568).withAlpha (0.55f));
        thumbnail.drawChannels (g, mr.reduced (0.0f, 1.0f).toNearestInt(),
                                0.0, totalSec, 1.0f);
    }

    // Highlight rect showing the current view window.
    {
        const float windowX = mr.getX() + viewOffsetFrac * mr.getWidth();
        const float windowW = mr.getWidth() / zoomFactor;
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (windowX, mr.getY(), windowW, mr.getHeight()));
        // Window border
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.drawRect (juce::Rectangle<float> (windowX, mr.getY(), windowW, mr.getHeight()), 1.0f);
    }

    // Zoom level label (double-click minimap to reset).
    if (zoomFactor > 1.01f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.50f));
        g.setFont (juce::Font (juce::FontOptions (8.5f)));
        g.drawText (juce::String (zoomFactor, 1) + "\xc3\x97",   // UTF-8 ×
                    mr.withTrimmedRight (2.0f),
                    juce::Justification::centredRight);
    }

    // Top border to visually separate minimap from the waveform.
    g.setColour (juce::Colour (0xff23272e));
    g.fillRect (juce::Rectangle<float> (mr.getX(), mr.getY(), mr.getWidth(), 1.0f));
}

// ---------------------------------------------------------------------------
// Timer / thumbnail refresh
// ---------------------------------------------------------------------------
void WaveformEditor::timerCallback()
{
    if (currentFile.existsAsFile())
        thumbnail.setSource (new juce::FileInputSource (currentFile));
}

// ---------------------------------------------------------------------------
// Energy envelope callback
// ---------------------------------------------------------------------------
void WaveformEditor::onEnvelopeReady (std::vector<float> env, double /*sr*/)
{
    energyEnvelope = std::move (env);
    recomputeVelocities(); // refresh segment colours now that we have accurate peaks
}

} // namespace flamforge
