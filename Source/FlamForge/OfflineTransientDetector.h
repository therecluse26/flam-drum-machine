// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <vector>

namespace flamforge
{

// ---------------------------------------------------------------------------
// OfflineTransientDetector — background-thread onset detector for FlamForge.
//
// Scans the multi-channel temp WAV produced by CaptureEngine (C1) and returns
// an ordered list of sample-position segment boundaries, one shared set for
// the whole recording group (never per-mic — invariant D3a of FLA-150).
//
// Algorithm (all offline, no RT-safety constraints):
//   1. Build frame-by-frame peak-energy envelope (one value per kHopSize
//      samples, across ALL channels via JUCE's readMaxLevels).
//   2. Compute the ODF: half-wave rectified first difference of the dBFS
//      envelope — fires on energy *increases* only, ignoring decay tails.
//   3. Compute an adaptive threshold via sliding-window median of the ODF,
//      scaled by a sensitivity-driven multiplier (log-linear: sensitivity=0
//      → multiplier≈8×, sensitivity=1 → multiplier≈1.5×).
//   4. Peak-pick local ODF maxima above threshold, enforcing a minimum
//      inter-onset interval of kMinInterOnsetMs.
//   5. Back-search from each ODF peak to the true onset frame (local energy
//      minimum before the peak) for tight boundary alignment.
//   6. Measure peak dBFS across all channels for each resulting segment
//      (feeds mapPeakToVelocity in C4/C5).
//
// Usage:
//   auto detector = std::make_unique<OfflineTransientDetector>();
//   detector->setFile(tempWav);
//   detector->setSensitivity(0.5f);
//   detector->runAsync([this](OfflineTransientDetector::Result r) {
//       // fires on detector thread — use callAsync to update UI
//       juce::MessageManager::callAsync([r = std::move(r), this] { ... });
//   });
//
// Re-runnable: call runAsync() again to cancel any running detection and
// restart with the current file/sensitivity settings.
// ---------------------------------------------------------------------------
class OfflineTransientDetector : private juce::Thread
{
public:
    // Result of one detection run.
    struct Result
    {
        // Sorted sample positions where each detected onset begins.
        // segmentPeaksDb[i] covers [breakpoints[i], breakpoints[i+1]) for
        // i < breakpoints.size()-1, and [breakpoints.back(), totalSamples)
        // for the last segment.
        std::vector<int64_t> breakpoints;

        // Peak dBFS per segment measured across all channels.
        // Always the same size as breakpoints.
        std::vector<float> segmentPeaksDb;

        int64_t      totalSamples = 0;
        double       sampleRate   = 48000.0;
        int          numChannels  = 0;
        bool         succeeded    = false;
        juce::String error;

        // Diagnostics from the absolute energy gate (FLA-158 / D9).
        float        noiseFloorDb = -100.0f; // estimated noise floor (dBFS)
        float        gateDb       = -100.0f; // onset keep-threshold actually applied
    };

    using CompletionFn = std::function<void (Result)>;

    OfflineTransientDetector();
    ~OfflineTransientDetector() override;

    // --- configuration (call before or between runs) -----------------------

    // Set the WAV file to analyse (must exist and be readable).
    void setFile (const juce::File& wavFile);

    // Sensitivity [0.0, 1.0].  Higher = lower threshold = more onsets.
    // Default 0.5.  Safe to set while idle; calling during a run takes effect
    // on the *next* run (current run reads the value once at start).
    void setSensitivity (float s) noexcept;
    float getSensitivity() const noexcept { return sensitivity.load(); }

    // --- execution ---------------------------------------------------------

    // Launch background detection.  Cancels any currently-running detection
    // and starts fresh.  onDone fires from the detector thread — use
    // juce::MessageManager::callAsync() in the callback to update UI safely.
    void runAsync (CompletionFn onDone);

    // Abort any running detection and block until the thread exits.
    void cancel();

    bool isRunning() const { return isThreadRunning(); }

    // --- shared onset gating (FLA-158 / D9) --------------------------------
    // The absolute dBFS threshold a candidate onset must clear to be treated as
    // a real strike, given an estimated noise floor. Single source of truth so
    // the realtime estimator (FLA-157) gates identically. Pure, header-inline,
    // RT-safe (no allocation) — safe to call from the audio thread.
    static float onsetGateDb (float noiseFloorDb) noexcept
    {
        return juce::jmax (kAbsoluteFloorDb, noiseFloorDb + kNoiseMarginDb);
    }

private:
    // juce::Thread entry point — runs the detection algorithm.
    void run() override;

    // --- algorithm tunables ------------------------------------------------
    static constexpr int   kHopSize               = 256;   // energy frame in samples
    static constexpr int   kMedianHalfWindowFrames = 20;   // 41-frame (~0.22 s) sliding window
    static constexpr float kMinInterOnsetMs        = 80.0f; // suppress double-triggers < 80 ms
    static constexpr int   kBackSearchFrames       = 40;   // ≈213 ms at 48 kHz — covers slow attacks
    static constexpr float kOnsetRiseDb            = 6.0f; // dB above noise floor = "signal has started"

    // --- absolute energy gate (FLA-158 / D9) -------------------------------
    // The relative ODF threshold alone over-segments: in the near-silent gaps
    // between real strikes the dBFS envelope is jumpy and the local median
    // threshold collapses, so room noise registers as onsets (94 hits from ~6
    // strikes). The gate below is an *absolute* reference the noise floor
    // cannot move: a detected onset is kept only if the segment it begins peaks
    // above max(noiseFloor + margin, kAbsoluteFloorDb). Rejected onsets merge
    // into the preceding (louder) segment, so surviving velocities are
    // unchanged. These constants are the single source of truth for onset
    // gating — the realtime estimator (FLA-157) consumes the same values.
    static constexpr float kAbsoluteFloorDb   = -50.0f; // never treat sub-this as a hit
    static constexpr float kNoiseMarginDb     = 20.0f;  // a hit must clear the noise floor by this (raised from 12 to suppress reverb-tail spikes — FLA-166)
    static constexpr float kNoiseFloorPercent = 0.10f;  // noise floor = 10th-percentile frame energy

    // --- Phase 7b: minimum sustained energy (FLA-166) -----------------------
    // After Phase 7, each candidate is additionally required to sustain energy
    // above gateDb - 3 dB for at least kMinSustainFrames consecutive frames.
    // kHopSize=256 samples @ 48 kHz ≈ 5.3 ms/frame → 3 frames ≈ 16 ms, which
    // comfortably passes real hits (≥15 ms) and rejects reverb spikes (<10 ms).
    static constexpr int kMinSustainFrames = 3;

    // --- helpers -----------------------------------------------------------
    static float ampToDb (float amp) noexcept;
    static std::vector<float> slidingMedian (const std::vector<float>& v, int halfW);

    // --- state -------------------------------------------------------------
    juce::File         sourceFile;
    std::atomic<float> sensitivity { 0.5f };
    CompletionFn       completionFn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OfflineTransientDetector)
};

} // namespace flamforge
