#include <juce_core/juce_core.h>
#include "Formats/FlamKitLoader.h"

namespace flam {

// ============================================================================
// JSON fixture helpers

static juce::String makeKitJson(
    const juce::String& name = "Test Kit",
    const juce::String& author = "Test Author",
    int midiNote = 36,
    int chokeGroup = -1,
    int numLayers = 1,
    int numChannels = 0)
{
    juce::String layers;
    for (int i = 0; i < numLayers; ++i)
    {
        if (i > 0) layers += ",";
        const float vMin = (float)i / numLayers;
        const float vMax = (float)(i + 1) / numLayers;
        layers += "{\"velocityMin\":" + juce::String(vMin, 4)
               + ",\"velocityMax\":" + juce::String(vMax, 4)
               + ",\"gain\":1.0"
               + ",\"roundRobinGroup\":" + juce::String(i + 1)
               + "}";
    }

    juce::String channelsStr;
    if (numChannels > 0)
    {
        channelsStr = ",\"channels\":[";
        for (int c = 0; c < numChannels; ++c)
        {
            if (c > 0) channelsStr += ",";
            channelsStr += "{\"name\":\"Ch" + juce::String(c + 1) + "\"}";
        }
        channelsStr += "]";
    }

    return "{"
           "\"name\":\"" + name + "\","
           "\"author\":\"" + author + "\","
           "\"version\":\"1.0\","
           "\"description\":\"Unit test fixture\""
           + channelsStr +
           ",\"settings\":{\"masterGain\":1.0,\"maxPolyphony\":64,"
           "\"useRoundRobin\":true,\"defaultHumanization\":0.0},"
           "\"pieces\":[{\"name\":\"Kick\",\"midiNote\":" + juce::String(midiNote) + ","
           "\"articulations\":[{\"name\":\"Center\","
           "\"chokeGroup\":" + juce::String(chokeGroup) + ","
           "\"layers\":[" + layers + "]}]}]}";
}

// RAII wrapper for a temp .json file.
// juce::TemporaryFile is non-copyable and non-movable, so it must be
// constructed in-place — this struct is never returned by value.
struct JsonFixture
{
    juce::TemporaryFile file;
    explicit JsonFixture(const juce::String& content) : file(".json")
    {
        file.getFile().replaceWithText(content);
    }
    juce::File get() const { return file.getFile(); }
    JUCE_DECLARE_NON_COPYABLE (JsonFixture)
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
        testMalformedJson();
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
        auto kit = loader.loadKit(juce::File("/nonexistent/path/to/kit.json"));
        expect(kit == nullptr, "Should fail for non-existent file");
        expect(!loader.getLastError().isEmpty(), "Should set error message");
    }

    void testUnsupportedExtension()
    {
        beginTest("Unsupported extension -> nullptr + error");
        JsonFixture f(makeKitJson());
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
        juce::TemporaryFile tmp(".json");
        tmp.getFile().replaceWithText("");
        FlamKitLoader loader;
        auto kit = loader.loadKit(tmp.getFile());
        expect(kit == nullptr);
    }

    void testMalformedJson()
    {
        beginTest("Malformed JSON -> nullptr");
        juce::TemporaryFile tmp(".json");
        tmp.getFile().replaceWithText("{ this is definitely not valid json }}}");
        FlamKitLoader loader;
        auto kit = loader.loadKit(tmp.getFile());
        expect(kit == nullptr, "Should reject malformed JSON");
    }

    void testMissingNameFails()
    {
        beginTest("Kit without name -> validateKit fails");
        juce::TemporaryFile tmp(".json");
        tmp.getFile().replaceWithText(
            "{\"name\":\"\",\"author\":\"A\",\"version\":\"1\",\"description\":\"\","
            "\"settings\":{\"masterGain\":1.0,\"maxPolyphony\":64,"
            "\"useRoundRobin\":true,\"defaultHumanization\":0.0},"
            "\"pieces\":[{\"name\":\"Kick\",\"midiNote\":36,"
            "\"articulations\":[{\"name\":\"A\",\"chokeGroup\":-1,"
            "\"layers\":[{\"velocityMin\":0,\"velocityMax\":1,"
            "\"gain\":1,\"roundRobinGroup\":0}]}]}]}"
        );
        FlamKitLoader loader;
        auto kit = loader.loadKit(tmp.getFile());
        expect(kit == nullptr, "Empty name should fail validation");
    }

    void testNoPiecesFails()
    {
        beginTest("Kit with no pieces -> validateKit fails");
        juce::TemporaryFile tmp(".json");
        tmp.getFile().replaceWithText(
            "{\"name\":\"Kit\",\"author\":\"A\",\"version\":\"1\",\"description\":\"\","
            "\"settings\":{\"masterGain\":1.0,\"maxPolyphony\":64,"
            "\"useRoundRobin\":true,\"defaultHumanization\":0.0},"
            "\"pieces\":[]}"
        );
        FlamKitLoader loader;
        auto kit = loader.loadKit(tmp.getFile());
        expect(kit == nullptr, "No pieces should fail validation");
    }

    void testEmptyArticulationFails()
    {
        beginTest("Articulation with no layers -> validateKit fails");
        juce::TemporaryFile tmp(".json");
        tmp.getFile().replaceWithText(
            "{\"name\":\"Kit\",\"author\":\"A\",\"version\":\"1\",\"description\":\"\","
            "\"settings\":{\"masterGain\":1.0,\"maxPolyphony\":64,"
            "\"useRoundRobin\":true,\"defaultHumanization\":0.0},"
            "\"pieces\":[{\"name\":\"Kick\",\"midiNote\":36,"
            "\"articulations\":[{\"name\":\"A\",\"chokeGroup\":-1,"
            "\"layers\":[]}]}]}"
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
        beginTest("Valid JSON kit parses successfully");
        JsonFixture f(makeKitJson());
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
        JsonFixture f(makeKitJson("Kit", "A", 42));
        FlamKitLoader loader;
        auto kit = loader.loadKit(f.get());
        expect(kit != nullptr);
        if (!kit) return;
        expectEquals(kit->pieces[0].midiNote, 42);
    }

    void testVelocityRanges()
    {
        beginTest("Velocity ranges parsed: 3 layers cover [0,1]");
        JsonFixture f(makeKitJson("Kit", "A", 36, -1, 3));
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
        JsonFixture f(makeKitJson("Kit", "A", 36, 0));
        FlamKitLoader loader;
        auto kit = loader.loadKit(f.get());
        expect(kit != nullptr);
        if (!kit) return;
        expectEquals(kit->pieces[0].articulations[0].chokeGroup, 0);
    }

    void testRoundRobinGroups()
    {
        beginTest("Round-robin group indices parsed per layer");
        JsonFixture f(makeKitJson("Kit", "A", 36, -1, 3));
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
        beginTest("Channel names parsed from JSON channels array");
        JsonFixture f(makeKitJson("Kit", "A", 36, -1, 1, 3));
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
        JsonFixture f(makeKitJson());
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
