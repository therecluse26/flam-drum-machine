// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace flamforge
{

// ---------------------------------------------------------------------------
// CapturedHit — one detected drum strike, lifted off the audio thread.
//
// `audio` holds the full multi-channel window for the hit (all input channels
// the device exposed). `peakDb` is the loudest sample in the window; it drives
// velocity mapping. `midiVelocity` is the calibrated 1..127 result.
// ---------------------------------------------------------------------------
struct CapturedHit
{
    juce::AudioBuffer<float> audio;
    double sampleRate    = 48000.0;
    float  peakDb        = -100.0f;
    int    midiVelocity  = 1;
};

// ---------------------------------------------------------------------------
// OnsetEvent — one realtime strike estimate, lifted off the audio thread via a
// lock-free FIFO (FLA-157 / D10). It carries no audio: just where the strike
// began and how loud it peaked. The message thread maps `peakDb` to a velocity
// bin to drive the live Velocity Coverage meter while the player is recording.
// This is advisory/provisional — authoritative velocities come from the offline
// detector once recording stops.
// ---------------------------------------------------------------------------
struct OnsetEvent
{
    int64_t samplePos = 0;        // strike onset, in samples from recording start
    float   peakDb     = -100.0f; // loudest block dBFS across the strike
};

// ---------------------------------------------------------------------------
// Calibration — maps a measured peak (dBFS) to a MIDI velocity (1..127).
//
// `softestDb` is the player's quietest deliberate hit, `loudestDb` the hardest.
// Both are filled during the calibrate passes; `valid` is set once both exist.
// ---------------------------------------------------------------------------
struct Calibration
{
    float softestDb = -48.0f;
    float loudestDb = -6.0f;
    bool  valid     = false;

    // Linear-in-dB mapping from peak loudness to MIDI velocity.
    int velocityFor (float db) const
    {
        if (! valid || loudestDb <= softestDb)
            return 100;

        const float t = juce::jlimit (0.0f, 1.0f,
                                      (db - softestDb) / (loudestDb - softestDb));
        return juce::jlimit (1, 127, (int) std::round (1.0f + t * 126.0f));
    }
};

// ---------------------------------------------------------------------------
// PieceCapture — every hit recorded for a single drum piece, plus its name.
// ---------------------------------------------------------------------------
struct PieceCapture
{
    juce::String           name = "Kick";
    std::vector<CapturedHit> hits;
};

} // namespace flamforge
