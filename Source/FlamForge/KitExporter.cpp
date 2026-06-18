// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "KitExporter.h"

#include "Formats/FlamKitLoader.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace flamforge
{

namespace {

// Filesystem-safe slug for a piece/folder name.
juce::String slug (const juce::String& s)
{
    juce::String out;
    for (auto c : s)
    {
        if (juce::CharacterFunctions::isLetterOrDigit (c) || c == '-' || c == '_')
            out << c;
        else if (c == ' ')
            out << '_';
        // drop anything else
    }
    if (out.isEmpty())
        out = "Piece";
    return out;
}

// Write a single multi-channel buffer as a 24-bit WAV. Returns true on success.
bool writeWav24 (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return false;

    file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(),
                             sampleRate <= 0.0 ? 48000.0 : sampleRate,
                             (unsigned int) buffer.getNumChannels(),
                             24,                       // bit depth
                             {},                       // metadata
                             0));                      // quality (unused for WAV)

    if (writer == nullptr)
        return false;       // stream is NOT released; FileOutputStream destructs here

    // Writer now owns the stream.
    stream.release();

    const bool wrote = writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    writer.reset();         // flush + close before we read the file back later
    return wrote;
}

} // namespace

ExportResult exportKit (const juce::String& kitName,
                        const std::vector<PieceCapture>& captures,
                        const SynthOptions& opts,
                        const juce::File& destDir,
                        const std::vector<juce::String>& channelLabels)
{
    ExportResult result;

    const juce::String safeKitName = kitName.trim().isEmpty() ? juce::String ("Untitled Kit")
                                                              : kitName.trim();

    // --- prepare output tree ----------------------------------------------
    const juce::File kitRoot     = destDir.getChildFile (safeKitName);
    const juce::File samplesRoot = kitRoot.getChildFile ("Samples");

    if (! samplesRoot.createDirectory())
    {
        result.message = "Could not create output directory: " + samplesRoot.getFullPathName();
        return result;
    }

    flam::DrumKit kit;
    kit.name        = safeKitName;
    kit.author      = "FlamForge";
    kit.version     = "1.0";
    kit.description = "Recorded with FlamForge.";

    int channelCount = 0;   // inferred from the first written hit
    int wavCount     = 0;

    // --- per piece ---------------------------------------------------------
    for (const auto& cap : captures)
    {
        SynthResult synth = synthesizePieceWithSources (cap, opts);

        auto& piece = synth.piece;
        if (piece.articulations.empty() || piece.articulations[0].layers.empty())
            continue;   // no usable hits for this piece — skip it

        auto& layers = piece.articulations[0].layers;

        const juce::String pieceSlug = slug (piece.name);
        const juce::File   pieceDir  = samplesRoot.getChildFile (pieceSlug);
        if (! pieceDir.createDirectory())
        {
            result.message = "Could not create piece directory: " + pieceDir.getFullPathName();
            return result;
        }

        // Write each layer's source hit and point the layer at the wav.
        for (int li = 0; li < (int) layers.size(); ++li)
        {
            const int srcIndex = synth.sourceHitIndex[(size_t) li];
            if (srcIndex < 0 || srcIndex >= (int) cap.hits.size())
                continue;

            const CapturedHit& hit = cap.hits[(size_t) srcIndex];
            auto& layer = layers[(size_t) li];

            if (channelCount == 0)
                channelCount = hit.audio.getNumChannels();

            const int vel = juce::jlimit (1, 127, hit.midiVelocity);
            const int rr  = layer.roundRobinGroup;

            // <piece>_v<vel>_rr<rr>.wav, zero-padded for stable sorting.
            juce::String base;
            base << pieceSlug << "_v" << juce::String (vel).paddedLeft ('0', 3)
                 << "_rr" << juce::String (rr).paddedLeft ('0', 2);

            juce::File wavFile = pieceDir.getChildFile (base + ".wav");

            // Guarantee uniqueness if (vel, rr) repeats across bands.
            int dedupe = 1;
            while (wavFile.existsAsFile())
                wavFile = pieceDir.getChildFile (base + "_" + juce::String (dedupe++) + ".wav");

            if (! writeWav24 (wavFile, hit.audio, hit.sampleRate))
            {
                result.message = "Failed to write WAV: " + wavFile.getFullPathName();
                return result;
            }

            layer.sampleFile = wavFile;   // absolute path; loader handles abs paths
            ++wavCount;
        }

        kit.pieces.push_back (std::move (piece));
    }

    if (kit.pieces.empty())
    {
        result.message = "No usable hits to export (all captures were empty or duds).";
        return result;
    }

    // --- channel names -----------------------------------------------------
    // Use user-provided labels where available; fall back to "Mic N" for blank or missing entries.
    if (channelCount <= 0)
        channelCount = 1;
    for (int c = 0; c < channelCount; ++c)
    {
        const bool hasLabel = (c < (int) channelLabels.size() && channelLabels[(size_t) c].isNotEmpty());
        kit.channelNames.push_back (hasLabel ? channelLabels[(size_t) c]
                                             : "Mic " + juce::String (c + 1));
    }

    // --- write flamkit.yaml ------------------------------------------------
    const juce::File kitYaml = kitRoot.getChildFile ("flamkit.yaml");
    flam::FlamKitLoader loader;

    if (! loader.saveKit (kit, kitYaml))
    {
        result.message = "saveKit failed: " + loader.getLastError();
        result.kitYaml = kitYaml;
        result.wavCount = wavCount;
        return result;
    }

    // --- reload to validate the round-trip ---------------------------------
    auto reloaded = loader.loadKit (kitYaml);
    if (reloaded == nullptr)
    {
        result.message = "Round-trip validation failed: " + loader.getLastError();
        result.kitYaml = kitYaml;
        result.wavCount = wavCount;
        return result;
    }

    result.ok       = true;
    result.kitYaml  = kitYaml;
    result.wavCount = wavCount;
    result.message  = "Exported \"" + safeKitName + "\": "
                    + juce::String (reloaded->getDrumPieceCount()) + " piece(s), "
                    + juce::String (wavCount) + " sample(s), "
                    + juce::String (reloaded->getTotalSampleCount()) + " layer(s).";
    return result;
}

} // namespace flamforge
