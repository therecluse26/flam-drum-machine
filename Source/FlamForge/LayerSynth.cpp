// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "LayerSynth.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace flamforge
{

namespace {

// Sensible default MIDI note for the common piece names, General-MIDI style.
int defaultMidiNoteFor (const juce::String& pieceName)
{
    const auto n = pieceName.trim().toLowerCase();
    if (n.contains ("kick"))                       return 36;
    if (n.contains ("snare"))                      return 38;
    if (n.contains ("hihat") || n.contains ("hi-hat") || n.contains ("hat"))
                                                   return 42;
    return 60;
}

// A hit is a "dud" if it is essentially silent. Captured peaks are dBFS, so
// anything near the noise floor carries no usable sample.
constexpr float kSilenceFloorDb = -80.0f;

// Hits more than this far below the loudest survivor are discarded as duds
// (mis-triggers, bleed, accidental taps).
constexpr float kDynamicDropDb  = 40.0f;

} // namespace

SynthResult synthesizePieceWithSources (const PieceCapture& cap, const SynthOptions& opts)
{
    SynthResult result;
    result.piece.name     = cap.name;
    result.piece.midiNote = defaultMidiNoteFor (cap.name);

    // One articulation always present so the piece validates downstream.
    flam::Articulation art;
    art.name = "default";

    const int maxLayers = juce::jmax (1, opts.maxLayers);
    const int maxRR      = juce::jmax (1, opts.roundRobinsPerLayer);

    // ---- 1. find the loudest hit to anchor the dud threshold --------------
    float maxPeakDb = -1.0e9f;
    for (const auto& h : cap.hits)
        maxPeakDb = juce::jmax (maxPeakDb, h.peakDb);

    // ---- 2. keep survivors (track original index for the source mapping) --
    struct Survivor { int srcIndex; int midiVel; };
    std::vector<Survivor> survivors;
    survivors.reserve (cap.hits.size());

    const float dropBelowDb = maxPeakDb - kDynamicDropDb;
    for (int i = 0; i < (int) cap.hits.size(); ++i)
    {
        const auto& h = cap.hits[(size_t) i];
        if (h.peakDb < kSilenceFloorDb)   continue;   // near-silent
        if (h.peakDb < dropBelowDb)       continue;   // far quieter than the rest
        survivors.push_back ({ i, juce::jlimit (1, 127, h.midiVelocity) });
    }

    // No usable hits -> empty piece (no articulation). Caller decides what to
    // do; the exporter skips pieces with no layers.
    if (survivors.empty())
        return result;

    // ---- 3. sort by velocity (stable so equal velocities keep capture order)
    std::stable_sort (survivors.begin(), survivors.end(),
                      [] (const Survivor& a, const Survivor& b)
                      { return a.midiVel < b.midiVel; });

    // ---- 4. split into up to maxLayers contiguous velocity bands ----------
    const int numHits   = (int) survivors.size();
    const int numBands   = juce::jmin (maxLayers, numHits);

    // Distribute hits across bands as evenly as possible, contiguous in the
    // velocity-sorted order. band b covers survivors[start(b) .. end(b)-1].
    auto bandStart = [numHits, numBands] (int b)
    {
        return (int) ((long long) b * numHits / numBands);
    };

    for (int b = 0; b < numBands; ++b)
    {
        const int start = bandStart (b);
        const int end   = bandStart (b + 1);   // exclusive
        if (end <= start)
            continue;

        // Velocity range for this band, normalised to 0..1. Bands are made
        // contiguous across the whole range: the first band starts at 0.0, the
        // last ends at 1.0, and internal boundaries sit at the midpoint between
        // adjacent bands' touching velocities so there are no gaps/overlaps.
        const float bandLoVel = survivors[(size_t) start].midiVel / 127.0f;
        const float bandHiVel = survivors[(size_t) (end - 1)].midiVel / 127.0f;

        float vMin = bandLoVel;
        float vMax = bandHiVel;

        if (b == 0)
            vMin = 0.0f;
        else
        {
            const float prevHiVel = survivors[(size_t) (start - 1)].midiVel / 127.0f;
            vMin = 0.5f * (prevHiVel + bandLoVel);
        }

        if (b == numBands - 1)
            vMax = 1.0f;
        else
        {
            const float nextLoVel = survivors[(size_t) end].midiVel / 127.0f;
            vMax = 0.5f * (bandHiVel + nextLoVel);
        }

        // Guard against degenerate ordering.
        vMin = juce::jlimit (0.0f, 1.0f, vMin);
        vMax = juce::jlimit (0.0f, 1.0f, vMax);
        if (vMax < vMin)
            std::swap (vMin, vMax);

        // ---- 5. one layer per hit in the band, round-robin 0,1,2,... ------
        int rr = 0;
        for (int i = start; i < end; ++i)
        {
            flam::SampleLayer layer;
            layer.sampleFile        = juce::File();   // exporter fills this in
            layer.velocityMin       = vMin;
            layer.velocityMax       = vMax;
            layer.gain              = 1.0f;
            layer.roundRobinGroup   = juce::jmin (rr, maxRR - 1);
            ++rr;

            art.layers.push_back (layer);
            result.sourceHitIndex.push_back (survivors[(size_t) i].srcIndex);
        }
    }

    result.piece.articulations.push_back (std::move (art));
    return result;
}

flam::DrumPiece synthesizePiece (const PieceCapture& cap, const SynthOptions& opts)
{
    return synthesizePieceWithSources (cap, opts).piece;
}

} // namespace flamforge
