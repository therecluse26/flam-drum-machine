#include <juce_core/juce_core.h>
#include "Formats/FlamKitLoader.h"

namespace flam {

// ============================================================================
// YAML fixture helpers
//
// FLA-80 dropped JSON kit support; all test fixtures now use the flamkit.yaml
// (YAML) format that is the sole supported format in v1.0.

static juce::String makeKitYaml(
    const juce::String& name = "Test Kit",
    const juce::String& author = "Test Author",
    int midiNote = 36,
    int chokeGroup = -1,
    int numLayers = 1,
    int numChannels = 0)
{
    juce::String yaml;
    yaml << "name: \"" << name << "\"\n";
    yaml << "author: \"" << author << "\"\n";
    yaml << "version: \"1.0\"\n";
    yaml << "description: \"Unit test fixture\"\n";

    if (numChannels > 0)
    {
        yaml << "channels:\n";
        for (int c = 0; c < numChannels; ++c)
            yaml << "  - name: \"Ch" << juce::String(c + 1) << "\"\n";
    }

    yaml << "settings:\n";
    yaml << "  masterGain: 1.0\n";
    yaml << "  maxPolyphony: 64\n";
    yaml << "  useRoundRobin: true\n";
    yaml << "  defaultHumanization: 0.0\n";

    yaml << "pieces:\n";
    yaml << "  - name: \"Kick\"\n";
    yaml << "    midiNote: " << juce::String(midiNote) << "\n";
    yaml << "    articulations:\n";
    yaml << "      - name: \"Center\"\n";
    yaml << "        chokeGroup: " << juce::String(chokeGroup) << "\n";
    yaml << "        layers:\n";

    for (int i = 0; i < numLayers; ++i)
    {
        const float vMin = (float)i / numLayers;
        const float vMax = (float)(i + 1) / numLayers;
        yaml << "          - velocityMin: " << juce::String(vMin, 4) << "\n";
        yaml << "            velocityMax: " << juce::String(vMax, 4) << "\n";
        yaml << "            gain: 1.0\n";
        yaml << "            roundRobinGroup: " << juce::String(i + 1) << "\n";
    }

    return yaml;
}

// RAII wrapper for a temp .yaml file.
// juce::TemporaryFile is non-copyable and non-movable, so it must be
// constructed in-place — this struct is never returned by value.
struct YamlFixture
{
    juce::TemporaryFile file;
    explicit YamlFixture(const juce::String& content) : file(".yaml")
    {
        file.getFile().replaceWithText(content);
    }
    juce::File get() const { return file.getFile(); }
    JUCE_DECLARE_NON_COPYABLE (YamlFixture)
};

// ============================================================================

class FlamKitLoaderTest : public juce::UnitTest
{
public:
    FlamKitLoaderTest() : juce::UnitTest("FlamKitLoader", "Formats") {}

    void runTest() override
    {
        testMissingFile();
        testUnsupportedExtension();
        testEmptyFile();
        testMalformedYaml();
        testMissingNameFails();
        testNoPiecesFails();
        testEmptyArticulationFails();
        testValidMinimalKit();
        testMidiNoteMapping();
        testVelocityRanges();
        testChokeGroupParsed();
        testRoundRobinGroups();
        testChannelNamesDetection();
        testKitSettings();
    }

private:
    // -------------------------------------------------------------------------
    // Error / edge-case tests
    // -------------------------------------------------------------------------

    void testMissingFile()
    {
        beginTest("Missing file -> nullptr + error message");
        FlamKitLoader loader;
        auto kit = loader.loadKit(juce::File("/nonexistent/path/to/kit.yaml"));
        expect(kit == nullptr, "Should fail for non-existent file");
        expect(!loader.getLastError().isEmpty(), "Should set error message");
    }

    void testUnsupportedExtension()
    {
        beginTest("Unsupported extension -> nullptr + error");
        YamlFixture f(makeKitYaml());
        juce::File badFile = f.get().getSiblingFile("kit.xml");
        f.get().copyFileTo(badFile);

        FlamKitLoader loader;
        auto kit = loader.loadKit(badFile);
        expect(kit == nullptr);
        expect(loader.getLastError().contains("Unsupported"));
        badFile.deleteFile();
    }

    void testEmptyFile()
    {
        beginTest("Empty file -> nullptr");
        juce::TemporaryFile tmp(".yaml");
        tmp.getFile().replaceWithText("");
        FlamKitLoader loader;
        auto kit = loader.loadKit(tmp.getFile());
        expect(kit == nullptr);
    }

    void testMalformedYaml()
    {
        beginTest("Malformed YAML -> nullptr");
        juce::TemporaryFile tmp(".yaml");
        // YAML that triggers a parse exception (unbalanced brackets / invalid flow)
        tmp.getFile().replaceWithText("name: [unclosed\npieces: }{invalid");
        FlamKitLoader loader;
        auto kit = loader.loadKit(tmp.getFile());
        expect(kit == nullptr, "Should reject malformed YAML");
    }

    void testMissingNameFails()
    {
        beginTest("Kit without name -> validateKit fails");
        juce::TemporaryFile tmp(".yaml");
        tmp.getFile().replaceWithText(
            "name: \"\"\n"
            "author: \"A\"\n"
            "version: \"1\"\n"
            "description: \"\"\n"
            "pieces:\n"
            "  - name: \"Kick\"\n"
            "    midiNote: 36\n"
            "    articulations:\n"
            "      - name: \"A\"\n"
            "        chokeGroup: -1\n"
            "        layers:\n"
            "          - velocityMin: 0.0\n"
            "            velocityMax: 1.0\n"
            "            gain: 1.0\n"
            "            roundRobinGroup: 0\n"
        );
        FlamKitLoader loader;
        auto kit = loader.loadKit(tmp.getFile());
        expect(kit == nullptr, "Empty name should fail validation");
    }

    void testNoPiecesFails()
    {
        beginTest("Kit with no pieces -> validateKit fails");
        juce::TemporaryFile tmp(".yaml");
        tmp.getFile().replaceWithText(
            "name: \"Kit\"\n"
            "author: \"A\"\n"
            "version: \"1\"\n"
            "description: \"\"\n"
            "pieces: []\n"
        );
        FlamKitLoader loader;
        auto kit = loader.loadKit(tmp.getFile());
        expect(kit == nullptr, "No pieces should fail validation");
    }

    void testEmptyArticulationFails()
    {
        beginTest("Articulation with no layers -> validateKit fails");
        juce::TemporaryFile tmp(".yaml");
        tmp.getFile().replaceWithText(
            "name: \"Kit\"\n"
            "author: \"A\"\n"
            "version: \"1\"\n"
            "description: \"\"\n"
            "pieces:\n"
            "  - name: \"Kick\"\n"
            "    midiNote: 36\n"
            "    articulations:\n"
            "      - name: \"A\"\n"
            "        chokeGroup: -1\n"
            "        layers: []\n"
        );
        FlamKitLoader loader;
        auto kit = loader.loadKit(tmp.getFile());
        expect(kit == nullptr, "Empty layers should fail validation");
    }

    // -------------------------------------------------------------------------
    // Successful parsing tests
    // -------------------------------------------------------------------------

    void testValidMinimalKit()
    {
        beginTest("Valid YAML kit parses successfully");
        YamlFixture f(makeKitYaml());
        FlamKitLoader loader;
        auto kit = loader.loadKit(f.get());
        expect(kit != nullptr, "Minimal valid kit should parse");
        if (!kit) return;

        expectEquals(kit->name, juce::String("Test Kit"));
        expectEquals(kit->author, juce::String("Test Author"));
        expectEquals(kit->version, juce::String("1.0"));
        expectEquals(kit->getDrumPieceCount(), 1);
        expectEquals(kit->getTotalSampleCount(), 1);
    }

    void testMidiNoteMapping()
    {
        beginTest("MIDI note parsed correctly");
        YamlFixture f(makeKitYaml("Kit", "A", 42));
        FlamKitLoader loader;
        auto kit = loader.loadKit(f.get());
        expect(kit != nullptr);
        if (!kit) return;
        expectEquals(kit->pieces[0].midiNote, 42);
    }

    void testVelocityRanges()
    {
        beginTest("Velocity ranges parsed: 3 layers cover [0,1]");
        YamlFixture f(makeKitYaml("Kit", "A", 36, -1, 3));
        FlamKitLoader loader;
        auto kit = loader.loadKit(f.get());
        expect(kit != nullptr);
        if (!kit) return;

        const auto& layers = kit->pieces[0].articulations[0].layers;
        expectEquals((int)layers.size(), 3);

        expectWithinAbsoluteError(layers.front().velocityMin, 0.0f, 0.001f);
        expectWithinAbsoluteError(layers.back().velocityMax,  1.0f, 0.001f);

        for (size_t i = 1; i < layers.size(); ++i)
            expectWithinAbsoluteError(layers[i].velocityMin,
                                      layers[i - 1].velocityMax, 0.001f);
    }

    void testChokeGroupParsed()
    {
        beginTest("chokeGroup field parsed correctly");
        YamlFixture f(makeKitYaml("Kit", "A", 36, 0));
        FlamKitLoader loader;
        auto kit = loader.loadKit(f.get());
        expect(kit != nullptr);
        if (!kit) return;
        expectEquals(kit->pieces[0].articulations[0].chokeGroup, 0);
    }

    void testRoundRobinGroups()
    {
        beginTest("Round-robin group indices parsed per layer");
        YamlFixture f(makeKitYaml("Kit", "A", 36, -1, 3));
        FlamKitLoader loader;
        auto kit = loader.loadKit(f.get());
        expect(kit != nullptr);
        if (!kit) return;

        const auto& layers = kit->pieces[0].articulations[0].layers;
        for (int i = 0; i < (int)layers.size(); ++i)
            expectEquals(layers[i].roundRobinGroup, i + 1,
                         "Layer " + juce::String(i) + " roundRobinGroup");
    }

    void testChannelNamesDetection()
    {
        beginTest("Channel names parsed from YAML channels array");
        YamlFixture f(makeKitYaml("Kit", "A", 36, -1, 1, 3));
        FlamKitLoader loader;
        auto kit = loader.loadKit(f.get());
        expect(kit != nullptr);
        if (!kit) return;

        expectEquals((int)kit->channelNames.size(), 3);
        expectEquals(kit->channelNames[0], juce::String("Ch1"));
        expectEquals(kit->channelNames[1], juce::String("Ch2"));
        expectEquals(kit->channelNames[2], juce::String("Ch3"));
    }

    void testKitSettings()
    {
        beginTest("GlobalSettings parsed correctly");
        YamlFixture f(makeKitYaml());
        FlamKitLoader loader;
        auto kit = loader.loadKit(f.get());
        expect(kit != nullptr);
        if (!kit) return;
        expectWithinAbsoluteError(kit->settings.masterGain, 1.0f, 0.001f);
        expectEquals(kit->settings.maxPolyphony, 64);
        expect(kit->settings.useRoundRobin);
    }
};

static FlamKitLoaderTest flamKitLoaderTest;

} // namespace flam
