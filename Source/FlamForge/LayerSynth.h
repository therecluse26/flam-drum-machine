// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include "CaptureTypes.h"
#include "Formats/FlamKitLoader.h"

#include <vector>

namespace flamforge
{

// ---------------------------------------------------------------------------
// LayerSynth — turns a pile of captured hits into a structured DrumPiece.
//
// Given every hit recorded for one drum piece, it:
//   * drops obvious duds (near-silence, or > 40 dB below the loudest hit),
//   * sorts the survivors by MIDI velocity,
//   * splits them into up to `maxLayers` contiguous velocity bands and sets
//     each layer's velocityMin/velocityMax (normalised 0..1),
//   * assigns round-robin groups 0,1,2,... within each band (capped),
//   * wraps everything in a single "default" articulation.
//
// The resulting SampleLayer.sampleFile fields are LEFT EMPTY — KitExporter
// fills in the real .wav paths once it has written the audio to disk.
// ---------------------------------------------------------------------------
struct SynthOptions
{
    int maxLayers            = 6;
    int roundRobinsPerLayer  = 4;
};

// Result of synthesis: the laid-out piece, plus a parallel index telling the
// exporter which captured hit each emitted layer came from. The two vectors
// are aligned with the layers inside the single "default" articulation, in
// order: sourceHitIndex[i] is the index into PieceCapture.hits for the i-th
// layer of pieceOut.articulations[0].layers.
struct SynthResult
{
    flam::DrumPiece     piece;
    std::vector<int>    sourceHitIndex;   // layer i  ->  PieceCapture.hits[idx]
};

// Full synthesis: returns the piece together with the layer->hit mapping so
// the exporter can write each layer's source audio to the right wav file.
SynthResult synthesizePieceWithSources (const PieceCapture& cap, const SynthOptions& opts);

// Convenience overload matching the shared interface contract: discards the
// source-index mapping and returns only the DrumPiece.
flam::DrumPiece synthesizePiece (const PieceCapture& cap, const SynthOptions& opts);

} // namespace flamforge
