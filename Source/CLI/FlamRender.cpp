// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

/**
 * flam-render — headless offline renderer for FlamKit drum kits.
 *
 * Usage:
 *   flam-render --kit <kit.yaml> --seq <seq.json> [options]
 *
 * Sequence JSON format (array of note-on events):
 *   [{"time_ms": 0.0, "note": 36, "velocity": 100}, ...]
 *
 *   time_ms   — event time in milliseconds from start of render
 *   note      — MIDI note number (0-127)
 *   velocity  — MIDI velocity (0-127)
 *
 * Options:
 *   -o / --output   Output WAV path (default: out.wav)
 *   --sr            Sample rate in Hz (default: 48000)
 *   --block         Block size in samples (default: 64)
 *   --seed          Integer seed for deterministic rendering (CTEST-2)
 *   --tail          Extra silence in seconds after last event (default: 2)
 */

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include "Core/FlamEngine.h"
#include "Core/Mixer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

struct RenderConfig
{
    juce::String kitPath;
    juce::String seqPath;
    juce::String outputPath { "out.wav" };
    double sampleRate  { 48000.0 };
    int    blockSize   { 64 };
    double tailSeconds { 2.0 };
    uint64_t seed      { 0 };
    bool     hasSeed   { false };
};

// ---------------------------------------------------------------------------
// Sequence
// ---------------------------------------------------------------------------

struct MidiEvent
{
    double   timeMs;
    int      note;
    int      velocity;   // 0-127
};

static void printUsage()
{
    std::cerr
        << "Usage: flam-render --kit <kit.yaml> --seq <seq.json> [options]\n\n"
        << "Options:\n"
        << "  --kit   <path>   flamkit.yaml file (required)\n"
        << "  --seq   <path>   event sequence JSON file (required)\n"
        << "  -o      <path>   output WAV (default: out.wav)\n"
        << "  --sr    <hz>     sample rate, e.g. 48000 (default: 48000)\n"
        << "  --block <n>      block size in samples (default: 64)\n"
        << "  --seed  <n>      RNG seed for bit-identical renders\n"
        << "  --tail  <sec>    silence after last event (default: 2)\n\n"
        << "Sequence JSON format:\n"
        << "  Array of objects: [{\"time_ms\":0.0,\"note\":36,\"velocity\":100}, ...]\n";
}

static bool parseArgs(int argc, char* argv[], RenderConfig& cfg)
{
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg(argv[i]);
        const bool hasNext = (i + 1 < argc);

        if ((arg == "--kit") && hasNext)        cfg.kitPath    = argv[++i];
        else if ((arg == "--seq") && hasNext)   cfg.seqPath    = argv[++i];
        else if ((arg == "-o" || arg == "--output") && hasNext)
                                                cfg.outputPath = argv[++i];
        else if ((arg == "--sr") && hasNext)    cfg.sampleRate  = std::atof(argv[++i]);
        else if ((arg == "--block") && hasNext) cfg.blockSize   = std::atoi(argv[++i]);
        else if ((arg == "--tail") && hasNext)  cfg.tailSeconds = std::atof(argv[++i]);
        else if ((arg == "--seed") && hasNext)
        {
            cfg.seed    = static_cast<uint64_t>(std::strtoull(argv[++i], nullptr, 10));
            cfg.hasSeed = true;
        }
        else if (arg == "--help" || arg == "-h") { printUsage(); return false; }
        else { std::cerr << "Unknown argument: " << arg << "\n"; printUsage(); return false; }
    }

    if (cfg.kitPath.isEmpty())  { std::cerr << "Error: --kit is required\n";  return false; }
    if (cfg.seqPath.isEmpty())  { std::cerr << "Error: --seq is required\n";  return false; }
    if (cfg.sampleRate <= 0)    { std::cerr << "Error: --sr must be > 0\n";   return false; }
    if (cfg.blockSize <= 0)     { std::cerr << "Error: --block must be > 0\n";return false; }
    return true;
}

static std::vector<MidiEvent> loadSequence(const juce::String& path)
{
    std::vector<MidiEvent> events;

    const juce::File f(path);
    if (!f.existsAsFile())
    {
        std::cerr << "Error: sequence file not found: " << path << "\n";
        return events;
    }

    const auto json = juce::JSON::parse(f.loadFileAsString());
    if (!json.isArray())
    {
        std::cerr << "Error: sequence file must contain a JSON array\n";
        return events;
    }

    for (const auto& item : *json.getArray())
    {
        MidiEvent e;
        e.timeMs   = static_cast<double>(item["time_ms"]);
        e.note     = static_cast<int>(item["note"]);
        e.velocity = juce::jlimit(0, 127, static_cast<int>(item["velocity"]));
        events.push_back(e);
    }

    std::sort(events.begin(), events.end(),
              [](const MidiEvent& a, const MidiEvent& b){ return a.timeMs < b.timeMs; });

    return events;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    RenderConfig cfg;
    if (!parseArgs(argc, argv, cfg))
        return 1;

    const juce::File kitFile(cfg.kitPath);
    if (!kitFile.existsAsFile())
    {
        std::cerr << "Error: kit file not found: " << cfg.kitPath << "\n";
        return 1;
    }

    const auto events = loadSequence(cfg.seqPath);
    if (events.empty())
        std::cerr << "Warning: no events in sequence — output will be silence\n";

    // Total render length = last event + tail
    double lastEventMs = 0.0;
    for (const auto& e : events)
        lastEventMs = std::max(lastEventMs, e.timeMs);

    const double totalSecs    = lastEventMs / 1000.0 + cfg.tailSeconds;
    const int64_t totalSamples = static_cast<int64_t>(totalSecs * cfg.sampleRate) + cfg.blockSize;

    // Convert event times to sample positions once
    struct SampleEvent { int64_t pos; int note; int velocity; };
    std::vector<SampleEvent> sampleEvents;
    sampleEvents.reserve(events.size());
    for (const auto& e : events)
    {
        const int64_t pos = static_cast<int64_t>(e.timeMs * cfg.sampleRate / 1000.0 + 0.5);
        sampleEvents.push_back({ pos, e.note, e.velocity });
    }
    // Already sorted because loadSequence sorted by timeMs

    // Set up engine
    flam::FlamEngine engine;
    engine.setOfflineMode(true);   // load full samples, no streaming races

    if (cfg.hasSeed)
        engine.seedRNG(cfg.seed);

    engine.prepareToPlay(cfg.sampleRate, cfg.blockSize);
    engine.loadKit(kitFile);

    std::cout << "Loading kit: " << cfg.kitPath << " ...\n";
    engine.waitForKitLoaded();
    std::cout << "Kit loaded.\n";

    // Construct and configure the Mixer (mirrors PluginProcessor::prepareToPlay)
    flam::Mixer mixer;
    {
        const int numChannels = engine.getRequiredChannelCount();
        std::vector<juce::String> channelNames;
        channelNames.reserve(static_cast<size_t>(numChannels));
        for (int i = 0; i < numChannels; ++i)
            channelNames.push_back("Channel " + juce::String(i + 1));
        mixer.setNumChannels(numChannels, channelNames);
        mixer.prepareToPlay(cfg.sampleRate, cfg.blockSize);
    }
    // Pre-allocate mixer output: channels 0-1 = Main Mix stereo, channels 2+ = individual buses
    const int totalMixerOutChans = 2 + mixer.getNumChannels();
    juce::AudioBuffer<float> mixerOutputBuffer(totalMixerOutChans, cfg.blockSize);

    // Set up 24-bit WAV writer
    const juce::File outFile(cfg.outputPath);
    outFile.deleteFile();

    auto* outputStream = outFile.createOutputStream().release();
    if (outputStream == nullptr)
    {
        std::cerr << "Error: cannot open output file for writing: " << cfg.outputPath << "\n";
        engine.releaseResources();
        return 1;
    }

    juce::WavAudioFormat wavFormat;
    // Writer takes ownership of outputStream
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(outputStream,
                                  cfg.sampleRate,
                                  2,       // stereo output
                                  24,      // 24-bit
                                  {},      // metadata
                                  0));     // quality index

    if (writer == nullptr)
    {
        delete outputStream;  // not yet owned
        std::cerr << "Error: could not create WAV writer for: " << cfg.outputPath << "\n";
        engine.releaseResources();
        return 1;
    }

    // Render block-by-block
    juce::AudioBuffer<float> buffer(2, cfg.blockSize);
    int eventIndex = 0;
    const int numEvents = static_cast<int>(sampleEvents.size());

    std::cout << "Rendering " << totalSecs << " s  |  sr=" << static_cast<int>(cfg.sampleRate)
              << "  block=" << cfg.blockSize
              << "  events=" << events.size()
              << (cfg.hasSeed ? "  seed=" + juce::String(cfg.seed) : "")
              << "\n";

    for (int64_t blockStart = 0; blockStart < totalSamples; blockStart += cfg.blockSize)
    {
        const int thisBlock = static_cast<int>(
            std::min(static_cast<int64_t>(cfg.blockSize), totalSamples - blockStart));

        buffer.setSize(2, thisBlock, false, false, true);
        buffer.clear();

        // Collect MIDI events that fall within this block
        juce::MidiBuffer midiBuffer;
        while (eventIndex < numEvents
               && sampleEvents[eventIndex].pos < blockStart + thisBlock)
        {
            const auto& se = sampleEvents[eventIndex];
            const int offset = juce::jlimit(0, thisBlock - 1,
                                            static_cast<int>(se.pos - blockStart));
            midiBuffer.addEvent(
                juce::MidiMessage::noteOn(1, se.note, static_cast<juce::uint8>(se.velocity)),
                offset);
            ++eventIndex;
        }

        // Engine fills internalBuffer (multi-channel); buffer is zeroed by the engine.
        engine.processBlock(buffer, midiBuffer);

        // Route multi-channel engine output through Mixer → stereo Main Mix.
        // This mirrors PluginProcessor::processBlock so the CLI hears the same audio.
        const auto& internalBuf = engine.getMultiChannelBuffer();
        mixerOutputBuffer.setSize(totalMixerOutChans, thisBlock, false, false, true);
        mixer.process(internalBuf, mixerOutputBuffer, thisBlock);

        // Copy stereo Main Mix (channels 0-1) back to buffer for the WAV writer
        buffer.copyFrom(0, 0, mixerOutputBuffer, 0, 0, thisBlock);
        buffer.copyFrom(1, 0, mixerOutputBuffer, 1, 0, thisBlock);

        writer->writeFromAudioSampleBuffer(buffer, 0, thisBlock);
    }

    writer->flush();
    writer.reset();   // closes the WAV file

    engine.releaseResources();

    std::cout << "Done: " << cfg.outputPath << "\n";
    return 0;
}
