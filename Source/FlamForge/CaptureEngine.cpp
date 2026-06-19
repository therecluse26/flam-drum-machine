// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "CaptureEngine.h"

#include <cmath>

namespace flamforge
{

namespace
{
    // Linear amplitude (0..1) -> dBFS, floored so silence is finite.
    inline float ampToDb (float amp) noexcept
    {
        return amp > 1.0e-6f ? juce::Decibels::gainToDecibels (amp, -100.0f) : -100.0f;
    }
}

CaptureEngine::CaptureEngine()
{
    // std::atomic<float> zero-inits to 0 dBFS; we want -100 dBFS as the silence floor.
    for (auto& p : channelPeak)
        p.store (-100.0f, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// control (message thread)
// ---------------------------------------------------------------------------
void CaptureEngine::setMode (Mode m)
{
    // Reset per-mode transient state so a fresh pass starts clean. Plain stores;
    // the audio thread reads `mode` atomically and re-derives the rest.
    calibArmed     = false;
    calibRunPeakDb = -100.0f;
    calibSilence   = 0;
    resetRecordingState();
    lastPeakDb.store (-100.0f);
    mode.store (m);
}

CaptureEngine::Mode CaptureEngine::getMode() const
{
    return mode.load();
}

float CaptureEngine::lastCalibratedDb() const
{
    return lastPeakDb.load();
}

float CaptureEngine::channelLevelDb (int c) const
{
    if (c < 0 || c >= kMaxChannels) return -100.0f;
    return channelPeak[(size_t) c].load (std::memory_order_relaxed);
}

int CaptureEngine::channelCount() const
{
    return activeChannels.load (std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// drainProvisionalOnsets: immediate peakDb events (message thread).
// ---------------------------------------------------------------------------
std::vector<float> CaptureEngine::drainProvisionalOnsets()
{
    std::vector<float> out;
    int s1, n1, s2, n2;
    onsetFifo.prepareToRead (onsetFifo.getNumReady(), s1, n1, s2, n2);
    out.reserve ((size_t) (n1 + n2));
    for (int i = 0; i < n1; ++i) out.push_back (onsetBuf[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) out.push_back (onsetBuf[(size_t) (s2 + i)]);
    onsetFifo.finishedRead (n1 + n2);
    return out;
}

// ---------------------------------------------------------------------------
// drain (message thread): copy finished hits out of the slot pool.
// ---------------------------------------------------------------------------
std::vector<CapturedHit> CaptureEngine::drainNewHits()
{
    std::vector<CapturedHit> out;

    int start1, size1, start2, size2;
    fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

    auto copySlot = [&out] (Slot& slot)
    {
        const int ch  = slot.channels.load();
        const int len = slot.length.load();
        if (ch <= 0 || len <= 0)
            return;

        CapturedHit hit;
        hit.sampleRate   = slot.sr.load();
        hit.peakDb       = slot.peakDb.load();
        hit.audio.setSize (ch, len, false, true, false);
        for (int c = 0; c < ch; ++c)
            hit.audio.copyFrom (c, 0, slot.audio, c, 0, len);
        out.push_back (std::move (hit));
    };

    for (int i = 0; i < size1; ++i) copySlot (slots[(size_t) (start1 + i)]);
    for (int i = 0; i < size2; ++i) copySlot (slots[(size_t) (start2 + i)]);

    fifo.finishedRead (size1 + size2);

    // Velocity is assigned now (message thread) using the current calibration,
    // so a re-calibrate before draining still applies sensibly.
    for (auto& h : out)
        h.midiVelocity = calib.velocityFor (h.peakDb);

    return out;
}

// ---------------------------------------------------------------------------
// device lifecycle
// ---------------------------------------------------------------------------
void CaptureEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate  = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    if (sampleRate <= 0.0)
        sampleRate = 48000.0;

    numChannels = device != nullptr
                    ? juce::jlimit (0, kMaxChannels,
                                    device->getActiveInputChannels().countNumberOfSetBits())
                    : 0;
    if (numChannels <= 0)
        numChannels = juce::jmin (1, kMaxChannels);

    activeChannels.store (numChannels, std::memory_order_relaxed);

    windowSamples  = (int) std::ceil (kWindowMs  * 0.001 * sampleRate);
    preRollSamples = (int) std::ceil (kPreRollMs * 0.001 * sampleRate);
    releaseSamples = (int) std::ceil (kReleaseMs * 0.001 * sampleRate);

    ringSize = windowSamples + preRollSamples + 1;

    // Preallocate everything the audio thread will ever touch.
    ring.setSize   (kMaxChannels, ringSize,      false, true, false);
    recBuf.setSize (kMaxChannels, windowSamples, false, true, false);
    for (auto& s : slots)
        s.audio.setSize (kMaxChannels, windowSamples, false, true, false);

    ring.clear();
    recBuf.clear();
    ringWrite = 0;

    resetRecordingState();
    calibArmed     = false;
    calibRunPeakDb = -100.0f;
    calibSilence   = 0;
}

void CaptureEngine::audioDeviceStopped()
{
    resetRecordingState();
    calibArmed     = false;
    calibRunPeakDb = -100.0f;
    calibSilence   = 0;
}

// ---------------------------------------------------------------------------
// audio thread helpers
// ---------------------------------------------------------------------------
void CaptureEngine::resetRecordingState()
{
    recActive  = false;
    recWritten = 0;
    recSilence = 0;
    recPeakDb  = -100.0f;
}

float CaptureEngine::blockPeakDb (const float* const* in, int numCh, int numSamples) const
{
    float peak = 0.0f;
    for (int c = 0; c < numCh; ++c)
        if (in[c] != nullptr)
            for (int n = 0; n < numSamples; ++n)
                peak = juce::jmax (peak, std::abs (in[c][n]));
    return ampToDb (peak);
}

void CaptureEngine::publishHit (int channels, int length, double sr)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    const int idx = (n1 > 0) ? s1 : s2;
    if (n1 + n2 < 1)
        return; // pool full: drop this hit rather than block the audio thread

    auto& slot = slots[(size_t) idx];

    const int ch  = juce::jlimit (1, kMaxChannels, channels);
    const int len = juce::jlimit (1, windowSamples, length);
    for (int c = 0; c < ch; ++c)
        slot.audio.copyFrom (c, 0, recBuf, c, 0, len);

    slot.channels.store (ch);
    slot.length.store (len);
    slot.sr.store (sr);
    slot.peakDb.store (recPeakDb);

    fifo.finishedWrite (1);
}

// ---------------------------------------------------------------------------
// the real-time callback
// ---------------------------------------------------------------------------
void CaptureEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                      int numInputChannels,
                                                      float* const* outputChannelData,
                                                      int numOutputChannels,
                                                      int numSamples,
                                                      const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused (context);

    // Always present silent output — FlamForge does not pass audio through.
    for (int c = 0; c < numOutputChannels; ++c)
        if (outputChannelData[c] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[c], numSamples);

    const int ch = juce::jlimit (0, kMaxChannels, numInputChannels);

    // Per-channel instantaneous block peak — updated unconditionally so the UI
    // meter is live in every mode (including Idle). Relaxed stores: this is
    // telemetry, not synchronisation.
    if (ch > 0 && numSamples > 0)
    {
        activeChannels.store (ch, std::memory_order_relaxed);
        for (int c = 0; c < ch; ++c)
        {
            float peak = 0.0f;
            if (inputChannelData[c] != nullptr)
                for (int n = 0; n < numSamples; ++n)
                    peak = juce::jmax (peak, std::abs (inputChannelData[c][n]));
            channelPeak[(size_t) c].store (ampToDb (peak), std::memory_order_relaxed);
        }
    }

    const Mode m = mode.load();
    if (m == Mode::Idle || ch <= 0 || numSamples <= 0)
        return;

    const float blkDb = blockPeakDb (inputChannelData, ch, numSamples);
    lastPeakDb.store (blkDb);

    // --- calibrate passes: track the peak of a deliberate hit --------------
    if (m == Mode::CalibrateSoft || m == Mode::CalibrateLoud)
    {
        if (blkDb > kCalibrateDb)
        {
            calibArmed     = true;
            calibSilence   = 0;
            calibRunPeakDb = juce::jmax (calibRunPeakDb, blkDb);
        }
        else if (calibArmed)
        {
            calibSilence += numSamples;
            if (calibSilence >= releaseSamples)
            {
                // Hit finished — commit its peak as the calibration value.
                if (m == Mode::CalibrateSoft) calib.softestDb = calibRunPeakDb;
                else                          calib.loudestDb = calibRunPeakDb;

                calib.valid = (calib.loudestDb > calib.softestDb);

                calibArmed     = false;
                calibRunPeakDb = -100.0f;
                calibSilence   = 0;
            }
        }
        return;
    }

    // --- Recording: ring buffer + onset detection --------------------------
    // 1) Always keep the most recent audio in the ring (for pre-roll).
    for (int n = 0; n < numSamples; ++n)
    {
        const int w = ringWrite;
        for (int c = 0; c < ch; ++c)
            ring.setSample (c, w, inputChannelData[c] != nullptr ? inputChannelData[c][n] : 0.0f);
        ringWrite = (w + 1) % ringSize;
    }

    // 2) Onset / capture state machine, evaluated per block.
    if (! recActive)
    {
        if (blkDb > kOnsetDb)
        {
            // Immediate onset event: push peakDb to the provisional FIFO now,
            // before the 600ms window assembles. The message thread drains this
            // via drainProvisionalOnsets() within one 30 Hz tick (~33ms) so the
            // coverage meter updates live. RT-safe: prepareToWrite + array write
            // + finishedWrite — no allocation, no lock, no I/O.
            {
                int s1, n1, s2, n2;
                onsetFifo.prepareToWrite (1, s1, n1, s2, n2);
                if (n1 > 0)      onsetBuf[(size_t) s1] = blkDb;
                else if (n2 > 0) onsetBuf[(size_t) s2] = blkDb;
                onsetFifo.finishedWrite (n1 > 0 ? 1 : (n2 > 0 ? 1 : 0));
            }

            // New hit: seed recBuf with the pre-roll already sitting in the ring.
            recActive  = true;
            recWritten = 0;
            recSilence = 0;
            recPeakDb  = blkDb;

            const int pre = juce::jmin (preRollSamples, windowSamples);
            int rPos = ringWrite - numSamples - pre;
            rPos %= ringSize;
            if (rPos < 0) rPos += ringSize;

            for (int n = 0; n < pre && recWritten < windowSamples; ++n)
            {
                for (int c = 0; c < ch; ++c)
                    recBuf.setSample (c, recWritten, ring.getSample (c, rPos));
                rPos = (rPos + 1) % ringSize;
                ++recWritten;
            }
        }
    }

    if (recActive)
    {
        recPeakDb = juce::jmax (recPeakDb, blkDb);

        // Append this block into recBuf (clamped to the window length).
        for (int n = 0; n < numSamples && recWritten < windowSamples; ++n)
        {
            for (int c = 0; c < ch; ++c)
                recBuf.setSample (c, recWritten, inputChannelData[c] != nullptr ? inputChannelData[c][n] : 0.0f);
            ++recWritten;
        }

        // Track silence for end-of-hit detection.
        if (blkDb > kOnsetDb) recSilence = 0;
        else                  recSilence += numSamples;

        const bool windowFull = (recWritten >= windowSamples);
        const bool decayed     = (recSilence >= releaseSamples);

        if (windowFull || decayed)
        {
            publishHit (ch, recWritten, sampleRate);
            resetRecordingState();
        }
    }
}

} // namespace flamforge
