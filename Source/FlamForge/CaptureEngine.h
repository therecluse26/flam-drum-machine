// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include "CaptureTypes.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <array>
#include <vector>

namespace flamforge
{

// ---------------------------------------------------------------------------
// CaptureEngine — the real-time input side of FlamForge.
//
// Plugged into a juce::AudioDeviceManager as an AudioIODeviceCallback. It runs
// in one of four modes:
//
//   Idle           — pass audio through (meters still active), do nothing.
//   CalibrateSoft  — measure incoming peaks; on a clear hit, record the peak
//                    as the player's softest dynamic (calibration().softestDb).
//   CalibrateLoud  — same, but stores it as loudestDb. Calibration becomes
//                    valid once both passes have produced a value.
//   Recording      — continuous capture: all incoming audio is written into a
//                    pre-allocated lock-free ring buffer; a background writer
//                    thread drains it to a 24-bit temp WAV file on disk.
//                    Call stopContinuousRecording() to finalise the WAV and
//                    retrieve the file; call resetContinuousRecording() to
//                    abort and delete it.
//
// REAL-TIME SAFETY (Recording mode): the audio callback never allocates or
// touches the filesystem. It writes audio blocks into ThreadedWriter's
// pre-allocated internal ring buffer (AbstractFifo + AudioBuffer) — no heap
// alloc. The CriticalSection is an uncontended thin-mutex; it blocks only
// during the rare teardown event, not during steady-state recording.
// The background TimeSliceThread drains the ring and writes to the WAV file.
// ---------------------------------------------------------------------------
class CaptureEngine : public juce::AudioIODeviceCallback
{
public:
    enum class Mode { Idle, CalibrateSoft, CalibrateLoud, Recording };

    CaptureEngine();
    ~CaptureEngine() override;

    // --- control (message thread) ------------------------------------------
    void setMode (Mode m);
    Mode getMode() const;

    Calibration& calibration()             { return calib; }
    const Calibration& calibration() const { return calib; }

    // Most recent measured peak (dBFS). Updated continuously in calibrate
    // modes so the UI can show a live meter / "got it" feedback.
    float lastCalibratedDb() const;

    // Per-channel instantaneous block peak (dBFS). Relaxed-atomic read; safe
    // to call from the message thread at any time. Returns -100 for out-of-range.
    float channelLevelDb (int c) const;

    // Number of active input channels reported by the device.
    int channelCount() const;

    // Legacy stub — returns empty. Kept for code compiled against old API.
    std::vector<CapturedHit> drainNewHits() { return {}; }

    // --- continuous-capture API (Recording mode) ----------------------------

    // Path to the active temp WAV being written. Invalid File() when idle.
    juce::File getContinuousTempFile() const;

    // Samples written to the temp WAV so far. 0 when not recording.
    // Safe to read from message thread (relaxed atomic).
    int64_t getRecordedSamples() const noexcept;

    // Finalise the WAV and return the file — caller takes ownership and is
    // responsible for deleting it when done.
    // Returns an invalid File() if no recording is active.
    juce::File stopContinuousRecording();

    // Abort the current recording and delete the temp file.
    // Call on piece reset or before the object is destroyed.
    void resetContinuousRecording();

    // --- juce::AudioIODeviceCallback ---------------------------------------
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    // --- tunables ----------------------------------------------------------
    static constexpr int   kMaxChannels = 16;
    static constexpr float kCalibrateDb = -45.0f; // "real hit" floor for calibration
    static constexpr double kReleaseMs  = 120.0;   // calibration silence threshold

    // --- helpers -----------------------------------------------------------
    float blockPeakDb (const float* const* in, int numCh, int numSamples) const;

    // Internal: stop and flush the writer. If deleteFile=true, removes the temp
    // WAV. Returns the file path regardless (caller may ignore it).
    juce::File stopAndFlushWriter (bool deleteFile);

    void startContinuousRecording();

    // --- mode / feedback ---------------------------------------------------
    std::atomic<Mode>  mode       { Mode::Idle };
    std::atomic<float> lastPeakDb { -100.0f };
    Calibration        calib;

    // Per-channel peak — relaxed-atomic telemetry for UI meters.
    std::array<std::atomic<float>, kMaxChannels> channelPeak {};
    std::atomic<int> activeChannels { 0 };

    // calibrate-pass state (audio thread)
    bool  calibArmed     = false;
    float calibRunPeakDb = -100.0f;
    int   calibSilence   = 0;

    // --- device geometry ---------------------------------------------------
    double sampleRate    = 48000.0;
    int    numChannels   = 0;
    int    releaseSamples = 0;

    // --- continuous capture ------------------------------------------------
    // ThreadedWriter uses an internal pre-allocated ring buffer (AbstractFifo).
    // Audio thread calls write() → lock-free ring push.
    // writerThread (TimeSliceThread) drains the ring → 24-bit WAV on disk.
    //
    // Teardown protocol (prevents use-after-free without allocating on audio thread):
    //   1. Store null to continuousWriterPtr (seq_cst) — audio thread sees null on
    //      next callback and skips the write block.
    //   2. Acquire writerLock — blocks until any in-progress write() finishes.
    //   3. Reset continuousWriter — ThreadedWriter destructor flushes the ring,
    //      closes the WAV, and removes itself from writerThread.
    //
    // The audio callback acquires writerLock for the duration of write(). During
    // normal recording this is always uncontended (tens of nanoseconds). It only
    // blocks on the rare teardown event.
    juce::WavAudioFormat wavFormat;
    juce::TimeSliceThread writerThread { "FlamForge Writer" };

    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> continuousWriterPtr { nullptr };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> continuousWriter;
    juce::File currentTempFile;

    // Incremented on audio thread; read on message thread.
    std::atomic<int64_t> continuousRecordedSamples { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureEngine)
};

} // namespace flamforge
