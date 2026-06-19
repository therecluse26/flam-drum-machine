// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.
//
// L1 unit test for FlamForge's OfflineTransientDetector (FLA-153 / FLA-158).
//
// Regression guard for the over-segmentation bug (FLA-150): a recording of a
// handful of real strikes over a quiet noise floor must yield ~that many
// segments, NOT dozens of phantom onsets in the silent gaps. Before the
// absolute energy gate (D9) this same signal produced an order of magnitude
// too many breakpoints.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "FlamForge/OfflineTransientDetector.h"

#include <cmath>

namespace
{

// Write a synthetic 2-channel 48 kHz WAV: `numHits` decaying-sine strikes
// spaced `spacingSec` apart, over a constant low-level noise floor. Returns the
// temp file (caller deletes).
juce::File writeSyntheticTake (int numHits, double spacingSec)
{
    constexpr double sr        = 48000.0;
    constexpr int    numCh     = 2;
    const double     totalSec  = spacingSec * (numHits + 1);
    const int64_t    total     = (int64_t) (totalSec * sr);

    juce::AudioBuffer<float> buf (numCh, (int) total);
    buf.clear();

    // Noise floor ~ -72 dBFS (amplitude ~2.5e-4). Deterministic LCG so the test
    // is reproducible — no dependence on Random seeding.
    uint32_t lcg = 0x1234567u;
    auto nextNoise = [&lcg]() noexcept
    {
        lcg = lcg * 1664525u + 1013904223u;
        const float u = (float) (lcg >> 8) / (float) (1u << 24); // [0,1)
        return (u - 0.5f) * 2.0f * 2.5e-4f;
    };

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* d = buf.getWritePointer (ch);
        for (int64_t n = 0; n < total; ++n)
            d[n] += nextNoise();
    }

    // Decaying-sine strikes: fast attack, ~45 ms decay, ~120 Hz, peak ~-4 dBFS.
    const double freq = 120.0;
    const double tau  = 0.045;
    for (int h = 0; h < numHits; ++h)
    {
        const int64_t onset = (int64_t) ((h + 1) * spacingSec * sr);
        for (int64_t k = 0; k + onset < total && k < (int64_t) (0.4 * sr); ++k)
        {
            const double t   = (double) k / sr;
            const double env = std::exp (-t / tau);
            const float  s   = (float) (0.63 * env * std::sin (2.0 * juce::MathConstants<double>::pi * freq * t));
            for (int ch = 0; ch < numCh; ++ch)
                buf.getWritePointer (ch)[onset + k] += s;
        }
    }

    auto file = juce::File::createTempFile (".wav");
    juce::WavAudioFormat fmt;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        std::unique_ptr<juce::AudioFormatWriter> writer (
            fmt.createWriterFor (os.get(), sr, (unsigned) numCh, 24, {}, 0));
        if (writer != nullptr)
        {
            os.release(); // writer owns the stream now
            writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }
    }
    return file;
}

} // namespace

class OfflineTransientDetectorTest : public juce::UnitTest
{
public:
    OfflineTransientDetectorTest()
        : juce::UnitTest ("FlamForge OfflineTransientDetector") {}

    void runTest() override
    {
        beginTest ("six spaced strikes over a noise floor → ~six segments");

        constexpr int    numHits    = 6;
        constexpr double spacingSec = 0.5;
        auto file = writeSyntheticTake (numHits, spacingSec);

        flamforge::OfflineTransientDetector detector;
        detector.setFile (file);
        detector.setSensitivity (0.5f);

        juce::WaitableEvent done;
        flamforge::OfflineTransientDetector::Result result;
        detector.runAsync ([&] (flamforge::OfflineTransientDetector::Result r)
        {
            result = std::move (r);
            done.signal();
        });

        const bool finished = done.wait (10000);
        expect (finished, "detection did not complete within 10 s");
        expect (result.succeeded, "detection failed: " + result.error);

        const int found = (int) result.breakpoints.size();
        logMessage ("detected " + juce::String (found) + " segments (expected ~"
                    + juce::String (numHits) + "); noiseFloor="
                    + juce::String (result.noiseFloorDb, 1) + " dB, gate="
                    + juce::String (result.gateDb, 1) + " dB");

        // Found most of the real strikes ...
        expect (found >= numHits - 1,
                "under-detected: " + juce::String (found) + " < " + juce::String (numHits - 1));
        // ... and did NOT explode into phantom onsets in the silent gaps.
        // Pre-fix this signal produced an order of magnitude too many.
        expect (found <= numHits + 3,
                "OVER-detected: " + juce::String (found) + " segments from "
                + juce::String (numHits) + " strikes (the FLA-150 bug)");

        // Every surviving segment must clear the gate.
        for (auto db : result.segmentPeaksDb)
            expect (db >= result.gateDb - 0.01f, "a kept segment is below the gate");

        file.deleteFile();
    }
};

static OfflineTransientDetectorTest offlineTransientDetectorTest;
