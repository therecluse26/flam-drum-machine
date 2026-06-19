// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <memory>

namespace flamforge
{

// ---------------------------------------------------------------------------
// SegmentPlayer — RT-safe AudioSource that auditions a [start, end) region
// from a shared AudioFormatReader. Designed for click-to-audition in
// WaveformEditor (FLA-163).
//
// Thread model:
//   Message thread  — setReader(), playRegion(), stop()
//   Audio thread    — getNextAudioBlock()
//
// Reader swap safety: reader_ is std::atomic<shared_ptr<>> so the audio
// thread takes a local copy at the top of each block; the shared_ptr keeps
// the reader alive for that block even if the message thread replaces it
// concurrently. No heap allocation occurs in the audio path.
// ---------------------------------------------------------------------------
class SegmentPlayer : public juce::AudioSource
{
public:
    SegmentPlayer() = default;

    // Replace the backing reader. Stops any in-progress playback first.
    // Call from the message thread only (e.g. when the take WAV is finalised).
    void setReader (std::shared_ptr<juce::AudioFormatReader> r)
    {
        stop();
        reader_.store (std::move (r));
    }

    // Begin playing [startSample, endSample) immediately.
    // Safe to call from the message thread while audio thread is running.
    // Noop if no reader has been set.
    void playRegion (int64_t startSample, int64_t endSample)
    {
        if (reader_.load() == nullptr) return;
        regionEnd_.store (endSample,   std::memory_order_release);
        playhead_.store  (startSample, std::memory_order_release);
        playing_.store   (true,        std::memory_order_release);
    }

    void stop() noexcept { playing_.store (false, std::memory_order_release); }

    // --- juce::AudioSource --------------------------------------------------
    void prepareToPlay (int /*samplesPerBlock*/, double /*sampleRate*/) override {}
    void releaseResources() override { stop(); }

    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override
    {
        if (! playing_.load (std::memory_order_acquire))
        {
            info.clearActiveBufferRegion();
            return;
        }

        // Local copy: keeps the reader alive for this entire block even if the
        // message thread calls setReader() during the callback.
        const auto r = reader_.load();
        if (r == nullptr)
        {
            playing_.store (false, std::memory_order_release);
            info.clearActiveBufferRegion();
            return;
        }

        const int64_t head  = playhead_.load  (std::memory_order_acquire);
        const int64_t end   = regionEnd_.load (std::memory_order_acquire);
        const int     n     = info.numSamples;
        const int64_t avail = end - head;

        if (avail <= 0)
        {
            playing_.store (false, std::memory_order_release);
            info.clearActiveBufferRegion();
            return;
        }

        const int toRead = (int) juce::jmin ((int64_t) n, avail);
        r->read (info.buffer, info.startSample, toRead, head, true, true);

        // Zero-fill any remaining tail samples when the region ends mid-block.
        if (toRead < n)
        {
            for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
                info.buffer->clear (ch, info.startSample + toRead, n - toRead);
            playing_.store (false, std::memory_order_release);
        }

        playhead_.store (head + toRead, std::memory_order_release);
    }

private:
    std::atomic<std::shared_ptr<juce::AudioFormatReader>> reader_;
    std::atomic<int64_t> playhead_  { 0 };
    std::atomic<int64_t> regionEnd_ { 0 };
    std::atomic<bool>    playing_   { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentPlayer)
};

} // namespace flamforge
