// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "FlamKitLoader.h"

#ifdef FLAM_YAML_SUPPORT
#include <yaml-cpp/yaml.h>
#endif

namespace flam {

FlamKitLoader::FlamKitLoader() = default;

FlamKitLoader::~FlamKitLoader() = default;

std::unique_ptr<DrumKit> FlamKitLoader::loadKit(const juce::File& kitFile)
{
    if (!kitFile.existsAsFile())
    {
        lastError = "Kit file does not exist: " + kitFile.getFullPathName();
        return nullptr;
    }

    auto content = kitFile.loadFileAsString();
    if (content.isEmpty())
    {
        lastError = "Could not read kit file: " + kitFile.getFullPathName();
        return nullptr;
    }

    std::unique_ptr<DrumKit> kit;

    auto extension = kitFile.getFileExtension().toLowerCase();
    if (extension == ".yaml" || extension == ".yml")
    {
#ifdef FLAM_YAML_SUPPORT
        kit = parseYamlKit(content, kitFile);
#else
        lastError = "YAML support not available. Build with yaml-cpp library to enable YAML parsing.\n"
                    "On NixOS: run 'nix-shell' before building to include yaml-cpp dependency.";
        return nullptr;
#endif
    }
    else
    {
        lastError = "Unsupported file format (expected .yaml or .yml): " + extension;
        return nullptr;
    }

    if (!kit)
    {
        lastError = "Failed to parse kit file";
        return nullptr;
    }

    // Relative sample/cover paths are resolved against the kit file during
    // parsing (see parseYamlKit + resolveRelativePath), so they are already
    // absolute here. No post-pass needed.

    if (kit && validateKit(*kit))
    {
        return kit;
    }

    return nullptr;
}

bool FlamKitLoader::saveKit(const DrumKit& kit, const juce::File& kitFile)
{
    if (!validateKit(kit))
    {
        lastError = "Invalid kit data";
        return false;
    }
    
    juce::String content;

    auto extension = kitFile.getFileExtension().toLowerCase();
    if (extension == ".yaml" || extension == ".yml")
    {
#ifdef FLAM_YAML_SUPPORT
        content = serializeKitToYaml(kit);
#else
        lastError = "YAML support not available. Build with yaml-cpp library to enable YAML serialization.\n"
                    "On NixOS: run 'nix-shell' before building to include yaml-cpp dependency.";
        return false;
#endif
    }
    else
    {
        lastError = "Unsupported file format (expected .yaml or .yml): " + extension;
        return false;
    }
    
    if (content.isEmpty())
    {
        lastError = "Failed to serialize kit data";
        return false;
    }
    
    if (!kitFile.replaceWithText(content))
    {
        lastError = "Failed to write kit file: " + kitFile.getFullPathName();
        return false;
    }
    
    return true;
}

std::unique_ptr<DrumKit> FlamKitLoader::parseYamlKit(const juce::String& content, const juce::File& kitFile)
{
#ifdef FLAM_YAML_SUPPORT
    auto kit = std::make_unique<DrumKit>();

    try
    {
        YAML::Node root = YAML::Load(content.toStdString());

        // Parse root level fields
        if (root["name"])
            kit->name = root["name"].as<std::string>();
        if (root["author"])
            kit->author = root["author"].as<std::string>();
        if (root["version"])
            kit->version = root["version"].as<std::string>();
        if (root["description"])
            kit->description = root["description"].as<std::string>();
        if (root["coverImage"])
            kit->coverImageFile = resolveRelativePath(kitFile, juce::String(root["coverImage"].as<std::string>()));
        if (root["tags"] && root["tags"].IsSequence())
        {
            for (const auto& tagNode : root["tags"])
                kit->tags.push_back(tagNode.as<std::string>());
        }

        // Parse channels
        if (root["channels"] && root["channels"].IsSequence())
        {
            for (const auto& channelNode : root["channels"])
            {
                if (channelNode["name"])
                {
                    kit->channelNames.push_back(channelNode["name"].as<std::string>());
                }
            }
        }

        // Parse settings
        if (root["settings"])
        {
            auto settings = root["settings"];
            if (settings["masterGain"])
                kit->settings.masterGain = settings["masterGain"].as<float>();
            if (settings["maxPolyphony"])
                kit->settings.maxPolyphony = settings["maxPolyphony"].as<int>();
            if (settings["useRoundRobin"])
                kit->settings.useRoundRobin = settings["useRoundRobin"].as<bool>();
            if (settings["defaultHumanization"])
                kit->settings.defaultHumanization = settings["defaultHumanization"].as<float>();
        }

        // Parse pieces
        if (root["pieces"] && root["pieces"].IsSequence())
        {
            for (const auto& pieceNode : root["pieces"])
            {
                DrumPiece piece;

                if (pieceNode["name"])
                    piece.name = pieceNode["name"].as<std::string>();
                if (pieceNode["midiNote"])
                    piece.midiNote = pieceNode["midiNote"].as<int>();

                // Parse articulations
                if (pieceNode["articulations"] && pieceNode["articulations"].IsSequence())
                {
                    for (const auto& artNode : pieceNode["articulations"])
                    {
                        Articulation art;

                        if (artNode["name"])
                            art.name = artNode["name"].as<std::string>();
                        if (artNode["chokeGroup"])
                            art.chokeGroup = artNode["chokeGroup"].as<int>();
                        if (artNode["attackTime"])
                            art.attackTime = artNode["attackTime"].as<float>();
                        if (artNode["holdTime"])
                            art.holdTime = artNode["holdTime"].as<float>();
                        if (artNode["decayTime"])
                            art.decayTime = artNode["decayTime"].as<float>();
                        if (artNode["sustainLevel"])
                            art.sustainLevel = artNode["sustainLevel"].as<float>();
                        if (artNode["releaseTime"])
                            art.releaseTime = artNode["releaseTime"].as<float>();

                        // Parse layers
                        if (artNode["layers"] && artNode["layers"].IsSequence())
                        {
                            for (const auto& layerNode : artNode["layers"])
                            {
                                SampleLayer layer;

                                // Resolve the sample path against the kit file's
                                // directory immediately. resolveRelativePath never
                                // builds a juce::File from a bare relative string, so
                                // it won't trip JUCE's absolute-path assertion.
                                std::string sampleFilePath;
                                if (layerNode["sampleFile"])
                                    sampleFilePath = layerNode["sampleFile"].as<std::string>();

                                if (layerNode["velocityMin"])
                                    layer.velocityMin = layerNode["velocityMin"].as<float>();
                                if (layerNode["velocityMax"])
                                    layer.velocityMax = layerNode["velocityMax"].as<float>();
                                if (layerNode["gain"])
                                    layer.gain = layerNode["gain"].as<float>();
                                if (layerNode["roundRobinGroup"])
                                    layer.roundRobinGroup = layerNode["roundRobinGroup"].as<int>();

                                art.layers.push_back(layer);

                                // Store the resolved (absolute) file in the layer we just added
                                if (!sampleFilePath.empty())
                                    art.layers.back().sampleFile = resolveRelativePath(kitFile, juce::String(sampleFilePath));
                            }
                        }

                        piece.articulations.push_back(art);
                    }
                }

                kit->pieces.push_back(piece);
            }
        }
    }
    catch (const YAML::Exception& e)
    {
        lastError = "YAML parsing error: " + juce::String(e.what());
        return nullptr;
    }

    return kit;
#else
    (void)content;  // Suppress unused parameter warning
    lastError = "YAML support not compiled in";
    return nullptr;
#endif
}

juce::String FlamKitLoader::serializeKitToYaml(const DrumKit& kit)
{
    // Placeholder YAML serialization
    juce::String yaml;
    yaml << "name: " << kit.name << "\n";
    yaml << "author: " << kit.author << "\n";
    yaml << "version: " << kit.version << "\n";
    yaml << "description: " << kit.description << "\n";
    
    return yaml;
}

bool FlamKitLoader::validateKit(const DrumKit& kit)
{
    if (kit.name.isEmpty())
    {
        lastError = "Kit must have a name";
        return false;
    }
    
    if (kit.pieces.empty())
    {
        lastError = "Kit must have at least one drum piece";
        return false;
    }
    
    for (const auto& piece : kit.pieces)
    {
        if (piece.articulations.empty())
        {
            lastError = "Drum piece '" + piece.name + "' must have at least one articulation";
            return false;
        }
        
        for (const auto& art : piece.articulations)
        {
            if (art.layers.empty())
            {
                lastError = "Articulation '" + art.name + "' must have at least one sample layer";
                return false;
            }
        }
    }
    
    return true;
}

juce::File FlamKitLoader::resolveRelativePath(const juce::File& kitFile, const juce::String& relativePath)
{
    if (juce::File::isAbsolutePath(relativePath))
    {
        return juce::File(relativePath);
    }
    
    return kitFile.getParentDirectory().getChildFile(relativePath);
}

} // namespace flam