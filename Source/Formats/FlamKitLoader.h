#pragma once

#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace flam {

struct SampleLayer
{
    juce::File sampleFile;
    float velocityMin{0.0f};
    float velocityMax{1.0f};
    float gain{1.0f};
    int roundRobinGroup{0};
};

struct Articulation
{
    juce::String name;
    std::vector<SampleLayer> layers;
    int chokeGroup{-1};
    float attackTime{0.001f};
    float holdTime{0.0f};
    float decayTime{0.1f};
    float sustainLevel{1.0f};
    float releaseTime{0.05f};
};

struct DrumPiece
{
    juce::String name;
    int midiNote{60};
    std::vector<Articulation> articulations;
    
    struct MicChannel
    {
        juce::String name;
        float gain{1.0f};
        float pan{0.0f};
        float delay{0.0f};
    };
    
    std::vector<MicChannel> micChannels;
};

struct DrumKit
{
    juce::String name;
    juce::String author;
    juce::String version;
    juce::String description;
    
    std::vector<DrumPiece> pieces;
    
    struct GlobalSettings
    {
        float masterGain{1.0f};
        int maxPolyphony{64};
        bool useRoundRobin{true};
        float defaultHumanization{0.0f};
    } settings;
};

class FlamKitLoader
{
public:
    FlamKitLoader();
    ~FlamKitLoader();
    
    std::unique_ptr<DrumKit> loadKit(const juce::File& kitFile);
    bool saveKit(const DrumKit& kit, const juce::File& kitFile);
    
    juce::String getLastError() const { return lastError; }
    
private:
    juce::String lastError;
    
    std::unique_ptr<DrumKit> parseYamlKit(const juce::String& content);
    std::unique_ptr<DrumKit> parseJsonKit(const juce::String& content);
    
    juce::String serializeKitToYaml(const DrumKit& kit);
    juce::String serializeKitToJson(const DrumKit& kit);
    
    bool validateKit(const DrumKit& kit);
    juce::File resolveRelativePath(const juce::File& kitFile, const juce::String& relativePath);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlamKitLoader)
};

} // namespace flam