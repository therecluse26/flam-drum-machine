#include "FlamKitLoader.h"

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
    if (extension == ".yaml" || extension == ".yml" || extension == ".flamkit")
    {
        kit = parseYamlKit(content);
    }
    else if (extension == ".json")
    {
        kit = parseJsonKit(content);
    }
    else
    {
        lastError = "Unsupported file format: " + extension;
        return nullptr;
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
    if (extension == ".yaml" || extension == ".yml" || extension == ".flamkit")
    {
        content = serializeKitToYaml(kit);
    }
    else if (extension == ".json")
    {
        content = serializeKitToJson(kit);
    }
    else
    {
        lastError = "Unsupported file format: " + extension;
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
    // Placeholder YAML parsing implementation
    // Would use a YAML library like yaml-cpp
    auto kit = std::make_unique<DrumKit>();
    
    // Basic stub implementation
    kit->name = "Example Kit";
    kit->author = "FLAM";
    kit->version = "1.0";
    kit->description = "Example drum kit";
    
    return kit;
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