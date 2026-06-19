// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include "ForgeColors.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <memory>
#include <vector>

namespace flamforge
{

// ---------------------------------------------------------------------------
// WaveformEditor — interactive waveform display for the FlamForge continuous take.
//
// Decisions D2 / D3a / D4 / D5 of FLA-150:
//
//   D2  — single shared time axis; per-mic lanes phase-aligned in x.
//   D3a — one set of breakpoints for the whole recording group (never per-mic).
//         Markers span full height across all lanes.
//   D4  — drag to move, click/double-click to add, drag-off/right-click to delete.
//   D5  — segments filled by velocityColour(); recompute live from energy envelope.
//
// Default view: single summed overview lane. Accordion button expands to per-mic
// lanes stacked on one shared time axis. Both views share identical x-pixel mapping
// so phase alignment is preserved visually.
//
// For live recording: call setSource() when recording starts, then setLiveRecording(true);
// setLiveRecording(false) after the WAV is finalised. The 2 Hz timer refreshes the
// AudioThumbnail from the growing file, giving ~0.5 s latency which is acceptable
// for a recording tool (real-time monitoring is the ForgeMeter's job).
//
// Energy envelope: built on a low-priority background thread (EnergyScanner)
// whenever setSource() is called. Once available, segment-peak recomputation
// during drag is O(N_frames) in RAM — no per-drag disk I/O.
// ---------------------------------------------------------------------------
class WaveformEditor : public juce::Component,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    // Fired after every user interaction that changes the breakpoint list.
    // Both vectors are the same size: breakpoints[i] is the start sample of
    // segment i, segmentVelocities[i] is the MIDI velocity for that segment.
    std::function<void (const std::vector<int64_t>& breakpoints,
                        const std::vector<int>&     segmentVelocities)> onBreakpointsChanged;

    // Fired when the user single-clicks inside a segment (FLA-163).
    // Arguments are the [startSample, endSample) of the clicked segment.
    std::function<void (int64_t startSample, int64_t endSample)> onSegmentAudition;

    // Fired when the user presses Escape to stop in-progress audition.
    std::function<void()> onAuditionStop;

    // Fired when a segment's disabled state changes (right-click on segment body).
    std::function<void (const std::vector<bool>& disabled)> onDisabledChanged;

    WaveformEditor();
    ~WaveformEditor() override;

    // Load a WAV file as the waveform source. Pass an invalid File() to clear.
    // numCh is used to allocate per-lane slots for the accordion view.
    void setSource (const juce::File& f, double sr, int numCh);

    // Start (live=true) or stop (live=false) the 2 Hz thumbnail-refresh timer
    // used while the WAV is still being written by CaptureEngine.
    void setLiveRecording (bool live);

    // Replace the breakpoint + velocity state — typically called after
    // OfflineTransientDetector completes. All three vectors must be the same size.
    void setBreakpoints (const std::vector<int64_t>& bps,
                         const std::vector<float>&   peaksDb,
                         const std::vector<int>&     segVels,
                         int64_t                     totalSmp);

    // Labels for the per-mic accordion lanes ("OH-L", "Kick In", …).
    void setChannelLabels (const std::vector<juce::String>& labels);

    // When enabled, dragged breakpoints snap to the nearest zero crossing
    // within ±kSnapWindowSamples of the raw drag position.
    void setSnapToZeroCrossing (bool snap);

    // Toggle summed ↔ per-mic accordion view.
    void setExpanded (bool e);
    bool isExpanded() const { return expanded; }

    // Returns a copy of the current disabled-segment state (parallel to breakpoints).
    std::vector<bool> getDisabledSegments() const { return segmentDisabled; }

    // Minimum sensible height so the waveform + controls are readable.
    static constexpr int kMinHeight = 140;

    // Zoom control (zoomFactor 1.0 = full view, max ~32×).
    // setZoom keeps the sample under centreXPixel fixed during the change.
    void resetZoom();
    void setZoom (float factor, float centreXPixel);

    // --- juce::Component --------------------------------------------------
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    // -----------------------------------------------------------------------
    // EnergyScanner — builds a frame-level dBFS envelope (kHop-sample hops,
    // all channels peak) in a low-priority background thread. Fires a
    // juce::MessageManager::callAsync back to the owning WaveformEditor.
    //
    // The alive flag (shared_ptr<atomic<bool>>) prevents the callAsync lambda
    // from dereferencing the owner pointer after destruction.
    // -----------------------------------------------------------------------
    class EnergyScanner : public juce::Thread
    {
    public:
        explicit EnergyScanner (WaveformEditor& owner);
        void setSource (const juce::File& f, double sr);
        void run() override;

    private:
        WaveformEditor& owner;
        juce::File      sourceFile;
        double          fileSr = 48000.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnergyScanner)
    };

    // --- geometry ---------------------------------------------------------
    // Waveform drawing rectangle (below accordion button, with 2-px padding).
    juce::Rectangle<float> waveRect() const;
    // Thin strip at the bottom of waveRect showing the minimap.
    juce::Rectangle<float> minimapRect() const;
    float   sampleToX (int64_t s) const;
    int64_t xToSample (float x) const;
    void    clampViewOffset();
    void    paintMinimap (juce::Graphics& g);

    // --- interaction helpers ----------------------------------------------
    // Returns the breakpoint index whose marker is within kMarkerGrabPx of x,
    // or -1 if none qualify.
    int  findBreakpointNear (float x) const;
    // Returns the segment index that contains pixel x (i.e. breakpoints[i] ≤ xToSample(x)
    // < breakpoints[i+1]), or -1 if x falls before the first breakpoint or out of bounds.
    int  findSegmentAt (float x) const;
    void insertBreakpoint (int64_t sample);
    void removeBreakpoint (int idx);

    // Recompute segmentPeaksDb from energy envelope (if available) and
    // derive segmentVelocities via mapPeakToVelocity. Fires onBreakpointsChanged.
    void recomputeVelocities();

    // Peak dBFS across energyEnvelope frames [frameA, frameB).
    float envelopePeak (int frameA, int frameB) const;

    // --- juce::ChangeListener (AudioThumbnail done loading) ---------------
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

    // --- juce::Timer (2 Hz live refresh during recording) -----------------
    void timerCallback() override;

    // Called from EnergyScanner via callAsync when the envelope is ready.
    void onEnvelopeReady (std::vector<float> env, double sr);

    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
    static constexpr int   kHop              = 256;   // matches OfflineTransientDetector
    static constexpr float kMarkerGrabPx     = 8.0f;  // ±px from line that counts as grab
    static constexpr float kDeleteDragOffPx  = 40.0f; // vertical drag-off triggers delete
    static constexpr float kAccBtnH          = 22.0f; // height of the accordion toggle row
    static constexpr int   kLaneLabelW       = 52;    // px reserved for channel label
    static constexpr int   kSnapWindowSamples = 512;  // zero-crossing search radius
    static constexpr float kMaxZoom          = 32.0f; // maximum zoom factor
    static constexpr float kMinimapH         = 12.0f; // minimap strip height in px
    static constexpr float kDragPanThreshPx  = 5.0f;  // horizontal drag threshold before converting click to pan

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------
    juce::AudioFormatManager  formatManager;
    juce::AudioThumbnailCache thumbnailCache { 4 };
    juce::AudioThumbnail      thumbnail;
    juce::File                currentFile;

    std::vector<int64_t> breakpoints;
    std::vector<float>   segmentPeaksDb;    // always same size as breakpoints
    std::vector<int>     segmentVelocities; // always same size as breakpoints
    std::vector<bool>    segmentDisabled;   // always same size as breakpoints
    int64_t              totalSamples = 0;
    double               sampleRate   = 48000.0;
    int                  numChannels  = 1;

    std::vector<juce::String> channelLabels;
    std::vector<float>        energyEnvelope;  // kHop-resolution dBFS, all-ch peak

    bool expanded          = false;
    bool snapZeroCrossing  = false;

    // Zoom / pan state
    float zoomFactor     = 1.0f;  // 1.0 = full view, >1.0 = zoomed in
    float viewOffsetFrac = 0.0f;  // normalised left edge of visible window [0..1]

    // Pan drag state (middle-button, spacebar+left-drag, or deferred-audition left-drag)
    bool  panDragging            = false;
    float panDragStartX          = 0.0f;
    float panDragStartOffsetFrac = 0.0f;

    // Deferred-audition state (plain left-click on a segment; fires on mouseUp unless panned)
    int  pendingAuditionSegIdx = -1;
    bool pendingAuditionPanned = false;

    // Breakpoint drag state
    int   draggingIdx  = -1;
    float dragStartX   = 0.0f;
    float dragStartY   = 0.0f;
    bool  dragOffDelete = false;

    juce::TextButton expandBtn;

    // Alive flag: set to false in destructor before scanner.stopThread() so any
    // queued callAsync lambda skips the owner pointer rather than dereferencing it.
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };
    EnergyScanner scanner { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformEditor)
};

} // namespace flamforge
