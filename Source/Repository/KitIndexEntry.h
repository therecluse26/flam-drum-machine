// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_core/juce_core.h>

namespace flam {

/** One kit entry from a remote repository index. */
struct KitIndexEntry
{
    juce::String id, name, author, version, description;
    juce::String license, licenseUrl;
    juce::String downloadUrl, coverImageUrl;
    juce::String repoName;     // display name of the repo this entry came from
    int64_t sizeBytes{0};
    int drumPieceCount{0}, sampleCount{0};
    juce::String checksumSha256;
    juce::StringArray tags, channels;

    bool isInstalled{false};   // set by RepositoryManager after cross-referencing local kitPaths
};

} // namespace flam
