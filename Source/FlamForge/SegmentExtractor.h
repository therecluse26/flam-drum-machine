// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include "CaptureTypes.h"
#include "OfflineTransientDetector.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace flamforge
{

// ---------------------------------------------------------------------------
// SegmentExtractor — FLA-154 segment-at-export pipeline (decision D6).
//
// Takes the multi-channel temp WAV written by CaptureEngine (C1) and the
// breakpoint list from OfflineTransientDetector (C2) and slices the file
// into per-hit CapturedHits ready for LayerSynth / KitExporter — no
// changes to downstream code required.
//
// Each hit covers [breakpoints[i], breakpoints[i+1]) from the source WAV;
// the last hit covers [breakpoints.back(), detection.totalSamples).  All N
// input channels are preserved intact in a single N-channel AudioBuffer per
// hit, maintaining inter-mic phase coherence (invariant D3a of FLA-150).
//
// A short linear fade-in (default 1 ms) is applied at the leading edge of every cut
// to suppress transient clicking from hard sample boundaries.
//
// hit.peakDb is taken directly from detection.segmentPeaksDb[i] (already
// measured by the detector — no second pass, no requantization).
// hit.midiVelocity is set to 1; ForgeContent::recompute() remaps all
// velocities retroactively once the full set of hits is loaded.
// ---------------------------------------------------------------------------
struct SegmentResult
{
    std::vector<CapturedHit> hits;
    bool         ok    = false;
    juce::String error;
};

// Slice wavFile at the breakpoints in `detection` into per-hit CapturedHits.
// disabledSegments: optional parallel vector; segments whose entry is `true` are skipped.
// fadeInMsPerSeg:  per-segment fade-in duration in ms (fallback 1.0 ms — short enough
//                  to be inaudible on transients while still suppressing cut-edge clicks).
// fadeOutMsPerSeg: per-segment fade-out duration in ms (fallback: 5% of segment duration,
//                  clamped [2 ms, 200 ms]).
// Safe to call from any thread (reads the WAV file synchronously).
SegmentResult extractSegments (const juce::File&                      wavFile,
                               const OfflineTransientDetector::Result& detection,
                               const std::vector<bool>&                disabledSegments = {},
                               const std::vector<float>&               fadeInMsPerSeg   = {},
                               const std::vector<float>&               fadeOutMsPerSeg  = {});

} // namespace flamforge
