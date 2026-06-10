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
        kit = parseYamlKit(content);
#else
        lastError = "YAML support not available. Build with yaml-cpp library to enable YAML parsing.\n"
                    "On NixOS: run 'nix-shell' before building to include yaml-cpp dependency.";
        return nullptr;
#endif
    }
    else if (extension == ".json")
    {
        kit = parseJsonKit(content);
    }
    else
    {
        lastError = "Unsupported file format (expected .yaml, .yml, or .json): " + extension;
        return nullptr;
    }

    if (!kit)
    {
        lastError = "Failed to parse kit file";
        return nullptr;
    }

    // Resolve relative file paths (samples and cover image)
    const auto kitDirectory = kitFile.getParentDirectory();

    // Resolve cover image path
    if (kit->coverImageFile != juce::File())
    {
        auto pathString = kit->coverImageFile.getFullPathName();
        if (!juce::File::isAbsolutePath(pathString))
        {
            kit->coverImageFile = kitDirectory.getChildFile(pathString);
        }
        else
        {
            auto cwdPrefix = juce::File::getCurrentWorkingDirectory().getFullPathName() + "/";
            if (pathString.startsWith(cwdPrefix))
            {
                auto relativePart = pathString.substring(cwdPrefix.length());
                kit->coverImageFile = kitDirectory.getChildFile(relativePart);
            }
        }
    }

    // Resolve sample file paths
    for (auto& piece : kit->pieces)
    {
        for (auto& articulation : piece.articulations)
        {
            for (auto& layer : articulation.layers)
            {
                auto pathString = layer.sampleFile.getFullPathName();

                if (!juce::File::isAbsolutePath(pathString))
                {
                    layer.sampleFile = kitDirectory.getChildFile(pathString);
                }
                else
                {
                    // Check if it looks like it was incorrectly resolved from CWD
                    auto cwdPrefix = juce::File::getCurrentWorkingDirectory().getFullPathName() + "/";
                    if (pathString.startsWith(cwdPrefix))
                    {
                        auto relativePart = pathString.substring(cwdPrefix.length());
                        layer.sampleFile = kitDirectory.getChildFile(relativePart);
                    }
                }
            }
        }
    }

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
    else if (extension == ".json")
    {
        content = serializeKitToJson(kit);
    }
    else
    {
        lastError = "Unsupported file format (expected .yaml, .yml, or .json): " + extension;
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

std::unique_ptr<DrumKit> FlamKitLoader::parseYamlKit(const juce::String& content)
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
            kit->coverImageFile = juce::File(root["coverImage"].as<std::string>());
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

                                // DON'T create juce::File yet - store as empty and resolve later
                                // We'll store the path string temporarily elsewhere
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

                                // Store the path string in the layer we just added
                                if (!sampleFilePath.empty())
                                    art.layers.back().sampleFile = juce::File(sampleFilePath);
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

std::unique_ptr<DrumKit> FlamKitLoader::parseJsonKit(const juce::String& content)
{
    auto kit = std::make_unique<DrumKit>();
    
    auto json = juce::JSON::parse(content);
    if (!json.isObject())
    {
        lastError = "Invalid JSON format";
        return nullptr;
    }
    
    kit->name = json["name"].toString();
    kit->author = json["author"].toString();
    kit->version = json["version"].toString();
    kit->description = json["description"].toString();

    auto channels = json["channels"];
    if (channels.isArray())
    {
        for (const auto& channelJson : *channels.getArray())
            kit->channelNames.push_back(channelJson["name"].toString());
    }

    auto settings = json["settings"];
    if (settings.isObject())
    {
        kit->settings.masterGain = (float)settings["masterGain"];
        kit->settings.maxPolyphony = (int)settings["maxPolyphony"];
        kit->settings.useRoundRobin = (bool)settings["useRoundRobin"];
        kit->settings.defaultHumanization = (float)settings["defaultHumanization"];
    }
    
    auto pieces = json["pieces"];
    if (pieces.isArray())
    {
        for (const auto& pieceJson : *pieces.getArray())
        {
            DrumPiece piece;
            piece.name = pieceJson["name"].toString();
            piece.midiNote = (int)pieceJson["midiNote"];
            
            auto articulations = pieceJson["articulations"];
            if (articulations.isArray())
            {
                for (const auto& artJson : *articulations.getArray())
                {
                    Articulation art;
                    art.name = artJson["name"].toString();
                    art.chokeGroup = (int)artJson["chokeGroup"];
                    
                    auto layers = artJson["layers"];
                    if (layers.isArray())
                    {
                        for (const auto& layerJson : *layers.getArray())
                        {
                            SampleLayer layer;
                            layer.velocityMin = (float)layerJson["velocityMin"];
                            layer.velocityMax = (float)layerJson["velocityMax"];
                            layer.gain = (float)layerJson["gain"];
                            layer.roundRobinGroup = (int)layerJson["roundRobinGroup"];
                            
                            // Note: sample file paths would need to be resolved relative to kit file
                            art.layers.push_back(layer);
                        }
                    }
                    
                    piece.articulations.push_back(art);
                }
            }
            
            kit->pieces.push_back(piece);
        }
    }
    
    return kit;
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

juce::String FlamKitLoader::serializeKitToJson(const DrumKit& kit)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    
    root->setProperty("name", kit.name);
    root->setProperty("author", kit.author);
    root->setProperty("version", kit.version);
    root->setProperty("description", kit.description);
    
    juce::DynamicObject::Ptr settings = new juce::DynamicObject();
    settings->setProperty("masterGain", kit.settings.masterGain);
    settings->setProperty("maxPolyphony", kit.settings.maxPolyphony);
    settings->setProperty("useRoundRobin", kit.settings.useRoundRobin);
    settings->setProperty("defaultHumanization", kit.settings.defaultHumanization);
    root->setProperty("settings", settings.get());
    
    juce::Array<juce::var> piecesArray;
    for (const auto& piece : kit.pieces)
    {
        juce::DynamicObject::Ptr pieceObj = new juce::DynamicObject();
        pieceObj->setProperty("name", piece.name);
        pieceObj->setProperty("midiNote", piece.midiNote);
        
        juce::Array<juce::var> artArray;
        for (const auto& art : piece.articulations)
        {
            juce::DynamicObject::Ptr artObj = new juce::DynamicObject();
            artObj->setProperty("name", art.name);
            artObj->setProperty("chokeGroup", art.chokeGroup);
            
            juce::Array<juce::var> layersArray;
            for (const auto& layer : art.layers)
            {
                juce::DynamicObject::Ptr layerObj = new juce::DynamicObject();
                layerObj->setProperty("velocityMin", layer.velocityMin);
                layerObj->setProperty("velocityMax", layer.velocityMax);
                layerObj->setProperty("gain", layer.gain);
                layerObj->setProperty("roundRobinGroup", layer.roundRobinGroup);
                layersArray.add(layerObj.get());
            }
            artObj->setProperty("layers", layersArray);
            
            artArray.add(artObj.get());
        }
        pieceObj->setProperty("articulations", artArray);
        
        piecesArray.add(pieceObj.get());
    }
    root->setProperty("pieces", piecesArray);
    
    return juce::JSON::toString(juce::var(root.get()), true);
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