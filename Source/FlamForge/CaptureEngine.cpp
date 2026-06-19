// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "CaptureEngine.h"

#include <cmath>

namespace flamforge
{

namespace
{
    inline float ampToDb (float amp) noexcept
    {
        return amp > 1.0e-6f ? juce::Decibels::gainToDecibels (amp, -100.0f) : -100.0f;
    }
}

CaptureEngine::CaptureEngine()
{
    for (auto& p : channelPeak)
        p.store (-100.0f, std::memory_order_relaxed);
}

CaptureEngine::~CaptureEngine()
{
    // Abort any active recording and clean up the temp file.
    stopAndFlushWriter (true);
    writerThread.stopThread (2000);
}

// ---------------------------------------------------------------------------
// control (message thread)
// ---------------------------------------------------------------------------
void CaptureEngine::setMode (Mode m)
{
    calibArmed     = false;
    calibRunPeakDb = -100.0f;
    calibSilence   = 0;
    lastPeakDb.store (-100.0f);

    const Mode prev = mode.load();

    if (m == Mode::Recording)
    {
        startContinuousRecording();
    }
    else if (prev == Mode::Recording)
    {
        // Leaving Recording without an explicit stopContinuousRecording() call
        // (e.g. aborting a take mid-stream). Discard the temp WAV.
        stopAndFlushWriter (true);
    }

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

// --- continuous-capture API ------------------------------------------------

juce::File CaptureEngine::getContinuousTempFile() const
{
    return currentTempFile;
}

int64_t CaptureEngine::getRecordedSamples() const noexcept
{
    return continuousRecordedSamples.load (std::memory_order_relaxed);
}

juce::File CaptureEngine::stopContinuousRecording()
{
    return stopAndFlushWriter (false); // finalise WAV, return path, don't delete
}

void CaptureEngine::resetContinuousRecording()
{
    stopAndFlushWriter (true); // abort and delete temp file
}

// ---------------------------------------------------------------------------
// device lifecycle
// ---------------------------------------------------------------------------
void CaptureEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device != nullptr ? device->getCurrentSampleRate() : 48000.0;
    if (sampleRate <= 0.0)
        sampleRate = 48000.0;

    numChannels = device != nullptr
                    ? juce::jlimit (0, kMaxChannels,
                                    device->getActiveInputChannels().countNumberOfSetBits())
                    : 0;
    if (numChannels <= 0)
        numChannels = juce::jmin (1, kMaxChannels);

    activeChannels.store (numChannels, std::memory_order_relaxed);
    releaseSamples = (int) std::ceil (kReleaseMs * 0.001 * sampleRate);

    // Realtime onset estimator windows, in samples at the current rate.
    rtRefractorySamples = juce::jmax (1, (int) std::ceil (kRefractoryMs * 0.001 * sampleRate));
    rtPeakHoldSamples   = juce::jmax (1, (int) std::ceil (kPeakHoldMs   * 0.001 * sampleRate));

    calibArmed     = false;
    calibRunPeakDb = -100.0f;
    calibSilence   = 0;
}

void CaptureEngine::audioDeviceStopped()
{
    // Device stopped: abort any active continuous recording so the temp file
    // is not left open on a device that no longer exists.
    if (continuousWriterPtr.load (std::memory_order_acquire) != nullptr)
        stopAndFlushWriter (true);

    calibArmed     = false;
    calibRunPeakDb = -100.0f;
    calibSilence   = 0;
}

// ---------------------------------------------------------------------------
// private helpers
// ---------------------------------------------------------------------------
float CaptureEngine::blockPeakDb (const float* const* in, int numCh, int numSamples) const
{
    float peak = 0.0f;
    for (int c = 0; c < numCh; ++c)
        if (in[c] != nullptr)
            for (int n = 0; n < numSamples; ++n)
                peak = juce::jmax (peak, std::abs (in[c][n]));
    return ampToDb (peak);
}

// --- realtime onset FIFO ---------------------------------------------------

void CaptureEngine::pushOnset (int64_t samplePos, float peakDb) noexcept
{
    int s1, n1, s2, n2;
    onsetFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)      onsetBuf[(size_t) s1] = { samplePos, peakDb };
    else if (n2 > 0) onsetBuf[(size_t) s2] = { samplePos, peakDb };
    onsetFifo.finishedWrite (n1 + n2 >= 1 ? 1 : 0); // 0 if full → drop
}

int CaptureEngine::drainOnsets (OnsetEvent* dst, int maxCount) noexcept
{
    if (dst == nullptr || maxCount <= 0)
        return 0;

    int s1, n1, s2, n2;
    const int want = juce::jmin (maxCount, onsetFifo.getNumReady());
    onsetFifo.prepareToRead (want, s1, n1, s2, n2);

    int k = 0;
    for (int i = 0; i < n1; ++i) dst[k++] = onsetBuf[(size_t) (s1 + i)];
    for (int i = 0; i < n2; ++i) dst[k++] = onsetBuf[(size_t) (s2 + i)];
    onsetFifo.finishedRead (n1 + n2);
    return k;
}

void CaptureEngine::startContinuousRecording()
{
    stopAndFlushWriter (true); // discard any previous take

    // Reset the realtime onset estimator for the new take. The FIFO is drained
    // and the audio-thread tracking state cleared before continuousWriterPtr is
    // published, so the audio thread starts clean. rtLastOnsetSample starts well
    // below 0 so the very first strike is never suppressed by the refractory.
    onsetFifo.reset();
    rtPrimed          = false;
    rtPrevBlkDb       = -100.0f;
    rtOnsetHold       = false;
    rtOnsetPeakDb     = -100.0f;
    rtOnsetHoldLeft   = 0;
    rtOnsetStart      = 0;
    rtLastOnsetSample = -(int64_t) rtRefractorySamples - 1;
    rtSamplePos       = 0;

    if (numChannels <= 0)
        return;

    auto tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("flamforge_" + juce::Uuid().toString().replace ("-", "").substring (0, 12) + ".wav");

    auto* outputStream = new juce::FileOutputStream (tempFile);
    if (! outputStream->openedOk())
    {
        delete outputStream;
        return;
    }

    // createWriterFor takes ownership of outputStream on success; on failure the
    // caller must delete the stream.
    auto* formatWriter = wavFormat.createWriterFor (
        outputStream,
        sampleRate,
        (unsigned int) juce::jmax (1, numChannels),
        24,   // 24-bit PCM — matches flamkit.yaml pipeline
        {},
        0);

    if (formatWriter == nullptr)
    {
        delete outputStream;
        tempFile.deleteFile();
        return;
    }

    // Ring buffer: 4 seconds at current sample rate — generous headroom for
    // any background-thread scheduling jitter on Linux/Windows.
    const int ringBufferSamples = (int) (sampleRate * 4.0);

    // ThreadedWriter takes ownership of formatWriter; the TimeSliceThread is
    // shared across recordings so it doesn't need to restart each time.
    continuousWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
        formatWriter, writerThread, ringBufferSamples);

    currentTempFile = tempFile;
    continuousRecordedSamples.store (0, std::memory_order_relaxed);

    if (! writerThread.isThreadRunning())
        writerThread.startThread (juce::Thread::Priority::background);

    // Release-store: audio thread must see the fully-constructed writer before
    // it starts writing into it.
    continuousWriterPtr.store (continuousWriter.get(), std::memory_order_release);
}

juce::File CaptureEngine::stopAndFlushWriter (bool deleteFile)
{
    // 1. Signal the audio thread to stop writing — any callback that starts
    //    after this store sees null and skips the write block.
    continuousWriterPtr.store (nullptr, std::memory_order_seq_cst);

    // 2. Acquire writerLock. If an audio callback already loaded the old (non-null)
    //    pointer and is currently inside write(), it holds this lock. We block until
    //    that write() finishes — then no further writes to the object are possible.
    {
        const juce::ScopedLock sl (writerLock);

        // 3. Destroy the ThreadedWriter: flushes the ring buffer to disk, finalises
        //    the WAV header, closes the file, and removes itself from writerThread.
        continuousWriter.reset();
    }

    const juce::File result = currentTempFile;
    if (deleteFile)
        currentTempFile.deleteFile();
    currentTempFile = juce::File();

    continuousRecordedSamples.store (0, std::memory_order_relaxed);
    return result;
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

    // Per-channel instantaneous block peak — live in every mode. Relaxed stores:
    // this is telemetry, not synchronisation.
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

    // --- realtime onset estimator (advisory; feeds the live coverage meter) -
    // Rise-based: a strike is a sharp jump in the block-peak envelope, which the
    // noise floor cannot sustain — so this fires regardless of how loud the room
    // is. On onset we peak-hold briefly to capture the true strike level, then
    // emit one OnsetEvent. RT-safe: reads only blkDb, pushes a 16-byte event
    // into a lock-free FIFO — never copies, windows, or splits audio. Runs
    // before the writer push so it ticks even if the writer is momentarily null.
    {
        if (rtPrimed && ! rtOnsetHold)
        {
            // Looking for a strike: a sharp rise above the previous block.
            const float rise = blkDb - rtPrevBlkDb;
            if (rise >= kOnsetRiseDb
                && blkDb >= kOnsetFloorDb
                && (rtSamplePos - rtLastOnsetSample) >= rtRefractorySamples)
            {
                rtOnsetHold     = true;
                rtOnsetStart    = rtSamplePos;
                rtOnsetPeakDb   = blkDb;
                rtOnsetHoldLeft = rtPeakHoldSamples;
            }
        }
        else if (rtOnsetHold)
        {
            // Capturing the strike peak, then emit.
            rtOnsetPeakDb   = juce::jmax (rtOnsetPeakDb, blkDb);
            rtOnsetHoldLeft -= numSamples;
            if (rtOnsetHoldLeft <= 0)
            {
                pushOnset (rtOnsetStart, rtOnsetPeakDb);
                rtLastOnsetSample = rtOnsetStart;
                rtOnsetHold       = false;
                rtOnsetPeakDb     = -100.0f;
            }
        }

        rtPrimed     = true;     // first block only seeds rtPrevBlkDb (no rise)
        rtPrevBlkDb  = blkDb;
        rtSamplePos += numSamples;
    }

    // --- Recording: continuous write via lock-free ring buffer -------------
    //
    // Fast path: load the pointer with acquire semantics. If null (idle or
    // tearing down), return immediately — no locking, no allocation.
    auto* w = continuousWriterPtr.load (std::memory_order_acquire);
    if (w == nullptr)
        return;

    // Acquire writerLock for the duration of write(). During steady-state
    // recording this lock is always uncontended — just an atomic CAS, ~10 ns.
    // It only blocks on the rare teardown event, allowing the message thread
    // to safely destroy the writer after we release it.
    {
        const juce::ScopedLock sl (writerLock);

        // Double-check: teardown could have stored null between our load and
        // our lock acquisition. If so, the writer is already destroyed — skip.
        if (continuousWriterPtr.load (std::memory_order_seq_cst) == nullptr)
            return;

        // write() is lock-free internally (AbstractFifo into pre-allocated AudioBuffer).
        // Returns false if the ring overflows; drop the block rather than blocking.
        if (w->write (inputChannelData, numSamples))
            continuousRecordedSamples.fetch_add (numSamples, std::memory_order_relaxed);
    }
}

} // namespace flamforge
