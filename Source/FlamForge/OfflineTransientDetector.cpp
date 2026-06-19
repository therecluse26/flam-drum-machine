// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "OfflineTransientDetector.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace flamforge
{

OfflineTransientDetector::OfflineTransientDetector()
    : juce::Thread ("FlamForge Transient Detector")
{
}

OfflineTransientDetector::~OfflineTransientDetector()
{
    stopThread (3000);
}

void OfflineTransientDetector::setFile (const juce::File& wavFile)
{
    jassert (! isRunning());
    sourceFile = wavFile;
}

void OfflineTransientDetector::setSensitivity (float s) noexcept
{
    sensitivity.store (juce::jlimit (0.0f, 1.0f, s));
}

void OfflineTransientDetector::runAsync (CompletionFn onDone)
{
    stopThread (3000);          // cancel any previous run (signals threadShouldExit)
    completionFn = std::move (onDone);
    startThread (juce::Thread::Priority::background);
}

void OfflineTransientDetector::cancel()
{
    stopThread (3000);
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

float OfflineTransientDetector::ampToDb (float amp) noexcept
{
    return amp > 1.0e-6f ? juce::Decibels::gainToDecibels (amp, -100.0f) : -100.0f;
}

// Compute sliding-window median of `v` with half-window `halfW`.
// Returns a vector the same size as `v`.  O(N * W * log W).
std::vector<float> OfflineTransientDetector::slidingMedian (const std::vector<float>& v, int halfW)
{
    const int n = (int) v.size();
    std::vector<float> result ((size_t) n, 0.0f);
    std::vector<float> window;
    window.reserve ((size_t) (2 * halfW + 1));

    for (int i = 0; i < n; ++i)
    {
        const int lo = std::max (0, i - halfW);
        const int hi = std::min (n, i + halfW + 1);
        window.assign (v.begin() + lo, v.begin() + hi);
        const size_t mid = window.size() / 2;
        std::nth_element (window.begin(), window.begin() + (ptrdiff_t) mid, window.end());
        result[(size_t) i] = window[mid];
    }

    return result;
}

// ---------------------------------------------------------------------------
// Detection algorithm — runs on the background thread
// ---------------------------------------------------------------------------
void OfflineTransientDetector::run()
{
    Result result;

    // --- Open WAV file ------------------------------------------------------
    juce::WavAudioFormat fmt;
    auto* rawStream = new juce::FileInputStream (sourceFile);

    // createReaderFor owns the stream on success; deletes it on failure when
    // deleteStreamIfOpeningFails=true.
    std::unique_ptr<juce::AudioFormatReader> reader (
        fmt.createReaderFor (rawStream, /*deleteStreamIfOpeningFails=*/true));

    if (reader == nullptr)
    {
        result.error = "Cannot open WAV for transient detection: "
                       + sourceFile.getFullPathName();
        if (completionFn) completionFn (std::move (result));
        return;
    }

    const int64_t totalSamples = reader->lengthInSamples;
    const double  sampleRate   = reader->sampleRate;
    const int     numCh        = juce::jlimit (1, 16, (int) reader->numChannels);

    result.totalSamples = totalSamples;
    result.sampleRate   = sampleRate;
    result.numChannels  = numCh;

    if (totalSamples <= 0)
    {
        result.succeeded = true;
        if (completionFn) completionFn (std::move (result));
        return;
    }

    // --- Phase 1: energy envelope (peak dBFS per kHopSize-sample frame) -----
    // readMaxLevels reads the WAV data internally and returns Range<float> min/max
    // per channel — no need to allocate a full-file audio buffer.

    const int64_t numFrames = (totalSamples + kHopSize - 1) / kHopSize;
    std::vector<float> energyDb;
    energyDb.reserve ((size_t) numFrames);

    std::vector<juce::Range<float>> levels ((size_t) numCh);

    for (int64_t frame = 0; frame < numFrames; ++frame)
    {
        if (threadShouldExit()) { if (completionFn) completionFn ({}); return; }

        const int64_t start = frame * kHopSize;
        const int64_t count = std::min ((int64_t) kHopSize, totalSamples - start);
        if (count <= 0) break;

        reader->readMaxLevels (start, count, levels.data(), numCh);

        // Combine all channels into a single peak amplitude.
        float peakAmp = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            peakAmp = std::max (peakAmp,  levels[(size_t) c].getEnd());
            peakAmp = std::max (peakAmp, -levels[(size_t) c].getStart());
        }
        energyDb.push_back (ampToDb (peakAmp));
    }

    if (threadShouldExit()) { if (completionFn) completionFn ({}); return; }

    const int N = (int) energyDb.size();
    if (N < 2)
    {
        result.succeeded = true;
        if (completionFn) completionFn (std::move (result));
        return;
    }

    // --- Noise-floor estimate (for the absolute energy gate, Phase 7) -------
    // Low percentile of the per-frame energy envelope: a robust stand-in for
    // the room/preamp noise floor that ignores the loud strike frames.
    float noiseFloorDb = -100.0f;
    {
        std::vector<float> sorted (energyDb);
        std::sort (sorted.begin(), sorted.end());
        const int idx = juce::jlimit (0, N - 1, (int) std::floor (kNoiseFloorPercent * (N - 1)));
        noiseFloorDb = sorted[(size_t) idx];
    }
    const float gateDb = onsetGateDb (noiseFloorDb);

    // --- Phase 2: onset detection function (ODF) ----------------------------
    // Half-wave rectified first difference of the dBFS envelope.
    // Positive values mark energy *increases*; decay tails are suppressed.

    std::vector<float> odf ((size_t) N, 0.0f);
    for (int i = 1; i < N; ++i)
        odf[(size_t) i] = std::max (0.0f, energyDb[(size_t) i] - energyDb[(size_t) (i - 1)]);

    // --- Phase 3: adaptive threshold ----------------------------------------
    // Sliding median of the ODF * sensitivity-driven multiplier.
    // sensitivity=0 → lambda≈8 (only loud hits), =1 → lambda≈1.5 (near-noise).
    // Log-linear interpolation produces perceptually uniform slider response.

    const std::vector<float> odfMedian = slidingMedian (odf, kMedianHalfWindowFrames);

    const float s      = juce::jlimit (0.0f, 1.0f, sensitivity.load());
    const float lambda = std::exp (std::log (8.0f) + (std::log (1.5f) - std::log (8.0f)) * s);

    std::vector<float> threshold ((size_t) N);
    for (int i = 0; i < N; ++i)
        threshold[(size_t) i] = lambda * std::max (odfMedian[(size_t) i], 1.0e-6f);

    // --- Phase 4: peak picking ----------------------------------------------
    // Local maxima of ODF that exceed the adaptive threshold and are separated
    // by at least kMinInterOnsetMs.

    const int minInterOnsetFrames = juce::jmax (1, (int) std::round (
        kMinInterOnsetMs * 0.001 * sampleRate / kHopSize));

    std::vector<int> peakFrames;
    int lastPeakFrame = -minInterOnsetFrames * 2;

    for (int i = 1; i < N - 1; ++i)
    {
        if (odf[(size_t) i] > threshold[(size_t) i]
            && odf[(size_t) i] >= odf[(size_t) (i - 1)]
            && odf[(size_t) i] >= odf[(size_t) (i + 1)]
            && (i - lastPeakFrame) >= minInterOnsetFrames)
        {
            peakFrames.push_back (i);
            lastPeakFrame = i;
        }
    }

    // --- Phase 5: onset back-search -----------------------------------------
    // Walk backward from each ODF peak to find the local energy minimum, which
    // marks the true onset frame (the drum stick impact start, not the peak).

    auto& breakpoints = result.breakpoints;
    breakpoints.reserve (peakFrames.size());

    for (int peakFrame : peakFrames)
    {
        int   onsetFrame = peakFrame;
        float minEnergy  = energyDb[(size_t) peakFrame];

        for (int j = peakFrame - 1; j >= std::max (0, peakFrame - kBackSearchFrames); --j)
        {
            if (energyDb[(size_t) j] < minEnergy)
            {
                minEnergy  = energyDb[(size_t) j];
                onsetFrame = j + 1; // onset = first frame after local minimum
            }
        }

        breakpoints.push_back ((int64_t) onsetFrame * kHopSize);
    }

    // Sort and deduplicate (back-search could occasionally collapse adjacent peaks).
    std::sort (breakpoints.begin(), breakpoints.end());
    breakpoints.erase (std::unique (breakpoints.begin(), breakpoints.end()),
                       breakpoints.end());

    // --- Phase 6: segment peak measurement ----------------------------------
    // For each segment [breakpoints[i], breakpoints[i+1]) (last segment extends
    // to totalSamples), measure the peak dBFS across all channels.

    const int numSegments = (int) breakpoints.size();
    result.segmentPeaksDb.resize ((size_t) numSegments, -100.0f);

    constexpr int64_t kMeasureChunk = 4096;

    for (int seg = 0; seg < numSegments; ++seg)
    {
        if (threadShouldExit()) { if (completionFn) completionFn ({}); return; }

        const int64_t segStart = breakpoints[(size_t) seg];
        const int64_t segEnd   = (seg + 1 < numSegments)
                                     ? breakpoints[(size_t) (seg + 1)]
                                     : totalSamples;

        float segPeak = 0.0f;

        for (int64_t pos = segStart; pos < segEnd; pos += kMeasureChunk)
        {
            const int64_t n = std::min (kMeasureChunk, segEnd - pos);
            if (n <= 0) break;

            reader->readMaxLevels (pos, n, levels.data(), numCh);

            for (int c = 0; c < numCh; ++c)
            {
                segPeak = std::max (segPeak,  levels[(size_t) c].getEnd());
                segPeak = std::max (segPeak, -levels[(size_t) c].getStart());
            }
        }

        result.segmentPeaksDb[(size_t) seg] = ampToDb (segPeak);
    }

    // --- Phase 7: absolute energy gate (FLA-158 / D9) -----------------------
    // Drop onsets whose segment never rises above the gate — these are
    // noise-floor flutter in the silent gaps between real strikes, not hits.
    // A dropped onset's span merges into the preceding (louder) segment; that
    // segment's peak is unchanged because the dropped span is, by definition,
    // quieter than the gate (and the preceding segment is louder still), so we
    // can prune both vectors in lockstep without re-measuring.
    {
        std::vector<int64_t> keptBreakpoints;
        std::vector<float>   keptPeaks;
        keptBreakpoints.reserve (breakpoints.size());
        keptPeaks.reserve (breakpoints.size());

        for (int seg = 0; seg < numSegments; ++seg)
        {
            if (result.segmentPeaksDb[(size_t) seg] >= gateDb)
            {
                keptBreakpoints.push_back (breakpoints[(size_t) seg]);
                keptPeaks.push_back (result.segmentPeaksDb[(size_t) seg]);
            }
        }

        breakpoints.swap (keptBreakpoints);
        result.segmentPeaksDb.swap (keptPeaks);
    }

    result.noiseFloorDb = noiseFloorDb;
    result.gateDb       = gateDb;
    result.succeeded    = true;
    if (completionFn) completionFn (std::move (result));
}

} // namespace flamforge
