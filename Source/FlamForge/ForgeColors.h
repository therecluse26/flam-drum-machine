// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace flamforge
{

// Maps MIDI velocity (1..127) to a perceptually-ordered hue ramp:
//   blue (v=1, hue≈0.66) → cyan → green → yellow → orange → red (v=127, hue=0.0)
//
// Saturation and value are fixed so coverage-tier brightness can be applied by
// callers without shifting hue, e.g. velocityColour(v).withMultipliedBrightness(0.6f)
//
// Single source of truth — all FlamForge velocity-coloured components use this;
// do not duplicate the ramp constants elsewhere.
inline juce::Colour velocityColour (int velocity)
{
    const float t   = juce::jlimit (0.0f, 1.0f, float (velocity - 1) / 126.0f);
    const float hue = 0.66f * (1.0f - t);
    return juce::Colour::fromHSV (hue, 0.85f, 0.90f, 1.0f);
}

} // namespace flamforge
