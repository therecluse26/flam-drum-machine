// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.
//
// L1 unit test for FlamForge's realtime onset estimator (FLA-157 / D10).
//
// Regression guard for the "velocity coverage doesn't update in realtime" bug
// (FLA-150): while recording, the audio thread must emit one OnsetEvent per
// strike through the lock-free FIFO so the live coverage meter can update as
// the player plays — *before* offline detection runs. Previously the producer
// did not exist (drainNewHits() was an empty stub) and the meter only filled
// after recording stopped.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include "FlamForge/CaptureEngine.h"

#include <vector>

namespace
{

// Feed one constant-amplitude block of `numSamples` mono samples through the
// engine's audio callback. A constant value v means block peak == |v|.
void feedBlock (flamforge::CaptureEngine& eng, float amp, int numSamples)
{
    std::vector<float> chan ((size_t) numSamples, amp);
    const float* in[1]  = { chan.data() };
    juce::AudioIODeviceCallbackContext ctx;
    eng.audioDeviceIOCallbackWithContext (in, 1, nullptr, 0, numSamples, ctx);
}

} // namespace

class CaptureEngineOnsetTest : public juce::UnitTest
{
public:
    CaptureEngineOnsetTest()
        : juce::UnitTest ("FlamForge CaptureEngine realtime onsets") {}

    void runTest() override
    {
        beginTest ("six strikes over a LOUD noise floor still emit six onsets");

        flamforge::CaptureEngine eng;
        eng.audioDeviceAboutToStart (nullptr); // 48 kHz, 1 ch, onset windows set
        eng.setMode (flamforge::CaptureEngine::Mode::Recording);

        constexpr int   kBlock     = 512;
        constexpr int   kNumHits   = 6;
        constexpr float kHitAmp    = 0.5f;    // ~ -6 dBFS strikes
        // Noise floor ~ -42 dBFS — deliberately ABOVE the old -45 dBFS arm
        // threshold. The previous level-gate detector never "released" here and
        // emitted ZERO events (the real-app bug, FLA-150). The rise-based
        // detector must still find every strike.
        constexpr float kNoiseAmp  = 0.0079f; // ~ -42 dBFS
        constexpr int   kLoudBlks  = 4;       // strike body
        constexpr int   kQuietBlks = 16;      // gap (noise) between strikes

        // Lead-in noise so the detector is primed at the noise floor, not silence.
        for (int i = 0; i < 6; ++i) feedBlock (eng, kNoiseAmp, kBlock);

        for (int h = 0; h < kNumHits; ++h)
        {
            for (int i = 0; i < kLoudBlks;  ++i) feedBlock (eng, kHitAmp,   kBlock);
            for (int i = 0; i < kQuietBlks; ++i) feedBlock (eng, kNoiseAmp, kBlock);
        }

        flamforge::OnsetEvent events[64];
        const int got = eng.drainOnsets (events, 64);

        logMessage ("drained " + juce::String (got) + " onset events over a -42 dBFS"
                    + " noise floor (expected " + juce::String (kNumHits) + ")");

        expectEquals (got, kNumHits, "wrong number of realtime onset events");

        const float expectedDb = juce::Decibels::gainToDecibels (kHitAmp);
        int64_t lastPos = -1;
        for (int i = 0; i < got; ++i)
        {
            expectWithinAbsoluteError (events[i].peakDb, expectedDb, 1.0f,
                                       "onset peak dB off from the strike level");
            expect (events[i].samplePos > lastPos, "onset positions not increasing");
            lastPos = events[i].samplePos;
        }

        // A second drain must be empty — the FIFO was consumed.
        expectEquals (eng.drainOnsets (events, 64), 0, "events lingered after drain");

        eng.resetContinuousRecording(); // delete the temp WAV
    }
};

static CaptureEngineOnsetTest captureEngineOnsetTest;
