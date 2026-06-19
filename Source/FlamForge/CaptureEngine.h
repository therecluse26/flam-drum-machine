// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include "CaptureTypes.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
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
//   Idle           — pass the audio through untouched, do nothing.
//   CalibrateSoft  — measure incoming peaks; on a clear hit, record the peak
//                    as the player's softest dynamic (calibration().softestDb).
//   CalibrateLoud  — same, but stores it as loudestDb. Calibration becomes
//                    valid once both passes have produced a value.
//   Recording      — detect onsets and capture a ~600ms multi-channel window
//                    per hit, map peak -> MIDI velocity, publish to the message
//                    thread.
//
// REAL-TIME SAFETY: the audio callback never allocates, locks, or touches the
// filesystem. All buffers are preallocated; finished hits are handed to the
// message thread through a juce::AbstractFifo carrying only slot indices.
// drainNewHits() (message thread) copies the staged audio out.
// ---------------------------------------------------------------------------
class CaptureEngine : public juce::AudioIODeviceCallback
{
public:
    enum class Mode { Idle, CalibrateSoft, CalibrateLoud, Recording };

    CaptureEngine();
    ~CaptureEngine() override = default;

    // --- control (message thread) ------------------------------------------
    void setMode (Mode m);
    Mode getMode() const;

    Calibration& calibration()             { return calib; }
    const Calibration& calibration() const { return calib; }

    // Most recent measured peak (dBFS). Updated continuously in calibrate
    // modes so the UI can show a live meter / "got it" feedback.
    float lastCalibratedDb() const;

    // Per-channel instantaneous block peak (dBFS). Relaxed-atomic read; safe to
    // call from the message thread at any time. Returns -100 dBFS for indices
    // outside [0, channelCount()).
    float channelLevelDb (int c) const;

    // Number of active input channels reported by the device (or last callback).
    int channelCount() const;

    // Drain all hits captured since the previous call. MESSAGE THREAD ONLY.
    std::vector<CapturedHit> drainNewHits();

    // Drain immediate onset-peak events (one float peakDb per strike) that fire
    // at the moment of onset detection — before the 600ms window assembles.
    // Use these for provisional coverage meter updates; authoritative counts
    // come later via drainNewHits(). MESSAGE THREAD ONLY.
    std::vector<float> drainProvisionalOnsets();

    // The energy gate used to arm onset detection in Recording mode.
    // Both the RT estimator and the offline detector use this value so they
    // agree on what constitutes a real hit.  Equivalent to
    // OfflineTransientDetector::onsetGateDb() once that class is available.
    static constexpr float onsetGateDb() noexcept { return kOnsetDb; }

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
    static constexpr int   kMaxChannels   = 16;
    static constexpr double kWindowMs      = 600.0;   // captured length per hit
    static constexpr double kPreRollMs     = 5.0;     // grab slightly before onset
    static constexpr float  kOnsetDb       = -40.0f;  // arm threshold (Recording)
    static constexpr float  kCalibrateDb   = -45.0f;  // "this is a real hit" floor
    static constexpr double kReleaseMs     = 120.0;   // silence to end a hit
    static constexpr int    kNumSlots      = 64;      // finished-hit pool / fifo

    // --- helpers (audio thread) --------------------------------------------
    void  resetRecordingState();
    float blockPeakDb (const float* const* in, int numCh, int numSamples) const;
    void  publishHit (int channels, int length, double sr); // copies window -> a free slot

    // --- mode / feedback ---------------------------------------------------
    std::atomic<Mode>  mode       { Mode::Idle };
    std::atomic<float> lastPeakDb { -100.0f };
    Calibration        calib;                       // message thread owns logic; values plain floats

    // Per-channel peak updated every callback (relaxed stores, audio→UI telemetry).
    // Initialised to -100 dBFS in the constructor (zero-init leaves them at 0 dBFS).
    std::array<std::atomic<float>, kMaxChannels> channelPeak {};
    std::atomic<int> activeChannels { 0 };

    // calibrate-pass state (audio thread)
    bool  calibArmed     = false;   // currently inside a calibrate hit
    float calibRunPeakDb = -100.0f; // peak of the hit in progress
    int   calibSilence   = 0;       // samples below floor since last loud sample

    // --- device geometry ---------------------------------------------------
    double sampleRate     = 48000.0;
    int    numChannels    = 0;
    int    windowSamples  = 0;      // kWindowMs in samples
    int    preRollSamples = 0;
    int    releaseSamples = 0;

    // --- ring buffer for pre-roll (audio thread) ---------------------------
    // Continuous capture of the most recent audio so a hit window can start a
    // few ms before the detected onset.
    juce::AudioBuffer<float> ring;   // [kMaxChannels][windowSamples + preRoll]
    int  ringWrite = 0;
    int  ringSize  = 0;

    // --- in-progress recorded hit (audio thread) ---------------------------
    juce::AudioBuffer<float> recBuf; // [kMaxChannels][windowSamples]
    bool recActive   = false;
    int  recWritten  = 0;
    int  recSilence  = 0;
    float recPeakDb  = -100.0f;

    // --- finished-hit pool + fifo (audio -> message) -----------------------
    struct Slot
    {
        juce::AudioBuffer<float> audio; // [kMaxChannels][windowSamples]
        std::atomic<int> channels { 0 };
        std::atomic<int> length   { 0 };
        std::atomic<double> sr    { 48000.0 };
        std::atomic<float>  peakDb { -100.0f };
    };
    std::array<Slot, kNumSlots> slots;
    juce::AbstractFifo fifo { kNumSlots };

    // --- immediate onset event FIFO (audio -> message, no audio copy) --------
    // One peakDb float per onset, fired the instant the hit window opens.
    // RT-safe: preallocated array + AbstractFifo, zero heap/lock on audio thread.
    static constexpr int kOnsetSlots = 128;
    std::array<float, kOnsetSlots> onsetBuf {};
    juce::AbstractFifo              onsetFifo { kOnsetSlots };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CaptureEngine)
};

} // namespace flamforge
