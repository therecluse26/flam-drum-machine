// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include "CaptureTypes.h"
#include "LayerSynth.h"

#include <juce_core/juce_core.h>
#include <vector>

namespace flamforge
{

// ---------------------------------------------------------------------------
// KitExporter — writes a complete, loadable kit to disk.
//
// For every PieceCapture it runs LayerSynth to lay out velocity layers and
// round-robins, writes each layer's source hit to a 24-bit WAV under
//   <destDir>/<kitName>/Samples/<piece>/<piece>_v<vel>_rr<rr>.wav
// builds a flam::DrumKit pointing at those files, serialises it to
//   <destDir>/<kitName>/flamkit.yaml
// via flam::FlamKitLoader::saveKit, then reloads the yaml to validate the
// round-trip.
// ---------------------------------------------------------------------------
struct ExportResult
{
    bool         ok       = false;
    juce::String message;
    juce::File   kitYaml;
    int          wavCount = 0;
};

ExportResult exportKit (const juce::String& kitName,
                        const std::vector<PieceCapture>& captures,
                        const SynthOptions& opts,
                        const juce::File& destDir,
                        const std::vector<juce::String>& channelLabels = {});

} // namespace flamforge
