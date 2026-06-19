// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "SegmentExtractor.h"

#include <cmath>
#include <vector>

namespace flamforge
{

namespace
{

// Read numSamples from reader beginning at readerStart, returning a float
// buffer with ALL reader channels populated.  Uses the int** overload so it
// works for any channel count (the AudioBuffer<float> two-channel overload of
// AudioFormatReader::read only fills channels 0 and 1 from left/right).
//
// JUCE stores samples left-justified in 32-bit ints: the 24-bit PCM value
// occupies bits [31:8].  Dividing by 2^31 gives a value in [-1.0, 1.0].
static juce::AudioBuffer<float> readBlock (juce::AudioFormatReader& reader,
                                           int64_t readerStart,
                                           int     numSamples)
{
    const int numCh = (int) reader.numChannels;
    if (numCh <= 0 || numSamples <= 0)
        return {};

    // Flat storage, one row per channel.
    std::vector<int> storage ((size_t)(numCh * numSamples), 0);
    std::vector<int*> ptrs ((size_t) numCh);
    for (int ch = 0; ch < numCh; ++ch)
        ptrs[(size_t) ch] = storage.data() + ch * numSamples;

    if (! reader.read (ptrs.data(), numCh, readerStart, numSamples,
                       /*fillLeftover=*/false))
        return {};

    // Convert left-justified int32 → float [-1, 1].
    constexpr float kScale = 1.0f / static_cast<float> (static_cast<unsigned int> (1) << 31);

    juce::AudioBuffer<float> out (numCh, numSamples);
    for (int ch = 0; ch < numCh; ++ch)
    {
        const int*  src  = ptrs[(size_t) ch];
        float*      dst  = out.getWritePointer (ch);
        for (int s = 0; s < numSamples; ++s)
            dst[s] = static_cast<float> (src[s]) * kScale;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------

SegmentResult extractSegments (const juce::File&                      wavFile,
                               const OfflineTransientDetector::Result& detection,
                               const std::vector<bool>&                disabledSegments,
                               const std::vector<float>&               fadeInMsPerSeg,
                               const std::vector<float>&               fadeOutMsPerSeg)
{
    SegmentResult out;

    if (! detection.succeeded)
    {
        out.error = "Detection did not succeed.";
        return out;
    }

    if (detection.breakpoints.empty())
    {
        out.error = "No breakpoints to extract.";
        return out;
    }

    juce::WavAudioFormat wavFmt;
    std::unique_ptr<juce::InputStream> stream (wavFile.createInputStream());
    if (stream == nullptr)
    {
        out.error = "Cannot open temp WAV: " + wavFile.getFullPathName();
        return out;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (
        wavFmt.createReaderFor (stream.get(), /*deleteStreamIfOpeningFails=*/true));
    if (reader == nullptr)
    {
        // createReaderFor consumed the stream on failure when the flag is true
        stream.release();
        out.error = "Cannot decode WAV: " + wavFile.getFullPathName();
        return out;
    }
    stream.release();  // reader owns the stream on success

    const int64_t totalSamples = juce::jmin ((int64_t) reader->lengthInSamples,
                                             detection.totalSamples > 0
                                                 ? detection.totalSamples
                                                 : (int64_t) reader->lengthInSamples);
    const double  sampleRate   = reader->sampleRate;
    const int     numCh        = (int) reader->numChannels;

    const auto& bp = detection.breakpoints;
    const int   n  = (int) bp.size();

    out.hits.reserve ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        if (i < (int) disabledSegments.size() && disabledSegments[i])
            continue;

        const int64_t start = bp[(size_t) i];
        const int64_t end   = (i + 1 < n) ? bp[(size_t)(i + 1)] : totalSamples;
        const int64_t len   = end - start;

        if (len <= 0 || start < 0 || start >= totalSamples)
            continue;

        const int safeLen = (int) juce::jmin (len, totalSamples - start);
        juce::AudioBuffer<float> buf = readBlock (*reader, start, safeLen);

        if (buf.getNumChannels() == 0)
        {
            out.error = "Read failed at segment " + juce::String (i)
                      + " (start=" + juce::String (start) + ")";
            return out;
        }

        // Per-segment fade-in; fallback 1 ms (inaudible on transients, suppresses click).
        const float segFadeInMs = (i < (int) fadeInMsPerSeg.size())
                                ? fadeInMsPerSeg[(size_t) i] : 1.0f;
        const int fadeInSmp = (int) std::round ((double) segFadeInMs * sampleRate / 1000.0);

        // Linear fade-in: 0 → 1 over the first fadeInSmp samples.
        const int fade = juce::jmin (fadeInSmp, safeLen);
        for (int s = 0; s < fade; ++s)
        {
            const float g = static_cast<float> (s) / static_cast<float> (fade);
            for (int ch = 0; ch < numCh; ++ch)
                buf.setSample (ch, s, buf.getSample (ch, s) * g);
        }

        // Per-segment fade-out; fallback proportional (5% of duration, clamped [2 ms, 200 ms]).
        const double segDurMs = (double) safeLen / sampleRate * 1000.0;
        const double fadeOutMs = (i < (int) fadeOutMsPerSeg.size())
                               ? (double) fadeOutMsPerSeg[(size_t) i]
                               : juce::jlimit (2.0, 200.0, segDurMs * 0.05);
        const int    fadeOutSmp = (int) std::round (fadeOutMs * sampleRate / 1000.0);
        // Ensure fade-out does not overlap fade-in region.
        const int fadeOut = juce::jmin (fadeOutSmp, safeLen - fade);

        for (int s = 0; s < fadeOut; ++s)
        {
            const float gain = static_cast<float> (fadeOut - 1 - s)
                             / static_cast<float> (fadeOut);
            const int   idx  = safeLen - fadeOut + s;
            for (int ch = 0; ch < numCh; ++ch)
                buf.setSample (ch, idx, buf.getSample (ch, idx) * gain);
        }

        CapturedHit hit;
        hit.audio        = std::move (buf);
        hit.sampleRate   = sampleRate;
        hit.peakDb       = detection.segmentPeaksDb[(size_t) i];
        hit.midiVelocity = 1;   // retroactively remapped by ForgeContent::recompute()

        out.hits.push_back (std::move (hit));
    }

    out.ok = true;
    return out;
}

} // namespace flamforge
