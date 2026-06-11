// GoldenRenderTest.cpp
// CTEST-5 — L2 golden-render harness.
//
// Renders the synthetic fixture kit (L1Tests/Fixtures/golden-kit/) with a fixed
// seed and event schedule, then null-tests the output against a committed
// reference WAV.  Residual peak must be < -120 dBFS.
//
// To regenerate the golden reference after an intentional DSP change:
//   FLAM_UPDATE_GOLDEN=1 ./flam-tests "[golden_render]"
// Review the diff in L1Tests/Fixtures/goldens/golden_render.wav before committing.

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Core/FlamEngine.h"
#include "Core/Mixer.h"

// ---------------------------------------------------------------------------
// Compile-time fixture paths (set by CMakeLists.txt)
// ---------------------------------------------------------------------------
#ifndef FLAM_TEST_FIXTURES_DIR
#  error "FLAM_TEST_FIXTURES_DIR must be defined by the CMake build system"
#endif

static constexpr const char* FIXTURE_KIT_PATH =
    FLAM_TEST_FIXTURES_DIR "/golden-kit/flamkit.yaml";
// Golden stored as raw 32-bit float (interleaved L/R) to avoid any PCM
// quantization round-trip error.  The .f32 extension is unconventional but
// makes the format self-documenting.
static constexpr const char* GOLDEN_PATH =
    FLAM_TEST_FIXTURES_DIR "/goldens/golden_render.f32";

// ---------------------------------------------------------------------------
// Render parameters — must stay constant across all platforms for the golden
// ---------------------------------------------------------------------------
static constexpr double   RENDER_SAMPLE_RATE = 44100.0;
static constexpr int      RENDER_BLOCK_SIZE  = 512;
static constexpr uint64_t RENDER_SEED        = 42;
static constexpr int      MIDI_NOTE          = 36;   // "Test Kick" in fixture kit
static constexpr uint8_t  MIDI_VELOCITY      = 102;  // ≈ 0.8 * 127

// Note-on positions in samples (250 ms apart @ 44100 Hz)
static const int HIT_SAMPLES[] = { 0, 11025, 22050, 33075 };
static constexpr int TOTAL_RENDER_SAMPLES = 44100;  // 1 second

// ---------------------------------------------------------------------------
// Golden I/O — raw 32-bit float, interleaved channels.
// Using raw float avoids WAV PCM quantization (which would add ~-138 dBFS
// error and could mask near-threshold changes).
// ---------------------------------------------------------------------------
static void writeGolden(const juce::AudioBuffer<float>& buf,
                        const juce::String& path)
{
    juce::File outFile(path);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();

    auto fos = outFile.createOutputStream();
    REQUIRE(fos != nullptr);

    const int nCh = buf.getNumChannels();
    const int nSamples = buf.getNumSamples();
    for (int i = 0; i < nSamples; ++i)
        for (int ch = 0; ch < nCh; ++ch)
            fos->writeFloat(buf.getSample(ch, i));
}

static juce::AudioBuffer<float> readGolden(const juce::String& path,
                                           int numChannels, int numSamples)
{
    juce::File inFile(path);
    if (!inFile.existsAsFile())
        return {};

    auto fis = inFile.createInputStream();
    if (!fis)
        return {};

    juce::AudioBuffer<float> result(numChannels, numSamples);
    for (int i = 0; i < numSamples; ++i)
        for (int ch = 0; ch < numChannels; ++ch)
            result.setSample(ch, i, fis->readFloat());
    return result;
}

// ---------------------------------------------------------------------------
// Core render function — deterministic given fixed seed + events
// ---------------------------------------------------------------------------
static juce::AudioBuffer<float> renderFixtureKit()
{
    // Set up the engine in offline/test mode
    flam::FlamEngine engine;
    engine.setOfflineMode(true);   // load full samples, no 5ms-preload truncation
    engine.seedRNG(RENDER_SEED);   // deterministic round-robin + humanization

    // Load the synthetic fixture kit
    juce::File kitFile(FIXTURE_KIT_PATH);
    REQUIRE(kitFile.existsAsFile());
    engine.loadKit(kitFile);
    engine.waitForKitLoaded();     // block until background preload thread finishes

    // Prepare for headless rendering
    engine.prepareToPlay(RENDER_SAMPLE_RATE, RENDER_BLOCK_SIZE);

    // Configure Mixer — mirrors PluginProcessor::prepareToPlay so golden validates the
    // same signal path the plugin uses.  All settings at default (unity gain, no FX).
    flam::Mixer mixer;
    {
        const int numCh = engine.getRequiredChannelCount();
        std::vector<juce::String> names;
        names.reserve(static_cast<size_t>(numCh));
        for (int i = 0; i < numCh; ++i)
            names.push_back("Channel " + juce::String(i + 1));
        mixer.setNumChannels(numCh, names);
        mixer.prepareToPlay(RENDER_SAMPLE_RATE, RENDER_BLOCK_SIZE);
    }
    const int mixerOutChans = 2 + mixer.getNumChannels();
    juce::AudioBuffer<float> mixerOut(mixerOutChans, RENDER_BLOCK_SIZE);

    // Render TOTAL_RENDER_SAMPLES into a stereo output buffer, block by block
    juce::AudioBuffer<float> output(2, TOTAL_RENDER_SAMPLES);
    output.clear();

    int nextHitIdx = 0;
    const int numHits = (int)(sizeof(HIT_SAMPLES) / sizeof(HIT_SAMPLES[0]));

    for (int blockStart = 0; blockStart < TOTAL_RENDER_SAMPLES;
         blockStart += RENDER_BLOCK_SIZE)
    {
        const int blockLen = juce::jmin(RENDER_BLOCK_SIZE,
                                        TOTAL_RENDER_SAMPLES - blockStart);
        const int blockEnd = blockStart + blockLen;

        juce::AudioBuffer<float> block(2, blockLen);
        block.clear();

        // Schedule note-on events that fall within this block
        juce::MidiBuffer midi;
        while (nextHitIdx < numHits && HIT_SAMPLES[nextHitIdx] < blockEnd)
        {
            const int offset = HIT_SAMPLES[nextHitIdx] - blockStart;
            midi.addEvent(juce::MidiMessage::noteOn(1, MIDI_NOTE, MIDI_VELOCITY),
                          offset);
            ++nextHitIdx;
        }

        // Engine fills internalBuffer; the legacy stereo `block` is zeroed (FLA-70).
        engine.processBlock(block, midi);

        // Route multi-channel engine output through Mixer → stereo Main Mix.
        const auto& internalBuf = engine.getMultiChannelBuffer();
        mixerOut.setSize(mixerOutChans, blockLen, false, false, true);
        mixer.process(internalBuf, mixerOut, blockLen);

        // Accumulate stereo Main Mix (channels 0-1) into the output buffer
        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom(ch, blockStart, mixerOut, ch, 0, blockLen);
    }

    return output;
}

// ---------------------------------------------------------------------------
// Peak residual between two buffers (linear scale, 0..1)
// ---------------------------------------------------------------------------
static float peakResidual(const juce::AudioBuffer<float>& a,
                          const juce::AudioBuffer<float>& b)
{
    float peak = 0.0f;
    const int numCh = juce::jmin(a.getNumChannels(), b.getNumChannels());
    const int numSamples = juce::jmin(a.getNumSamples(), b.getNumSamples());
    for (int ch = 0; ch < numCh; ++ch)
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak,
                            std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
    return peak;
}

// ---------------------------------------------------------------------------
// TEST CASES
// ---------------------------------------------------------------------------

TEST_CASE("Golden render — output matches committed reference", "[golden_render]")
{
    auto rendered = renderFixtureKit();
    REQUIRE(rendered.getNumSamples() == TOTAL_RENDER_SAMPLES);

    const bool updateGolden = (std::getenv("FLAM_UPDATE_GOLDEN") != nullptr);

    if (updateGolden)
    {
        INFO("FLAM_UPDATE_GOLDEN is set — writing new golden reference to " << GOLDEN_PATH);
        writeGolden(rendered, GOLDEN_PATH);
        SUCCEED("Golden reference updated.");
        return;
    }

    // Comparison mode: null-test against committed golden
    INFO("Golden path: " << GOLDEN_PATH << "  — run with FLAM_UPDATE_GOLDEN=1 to regenerate");
    REQUIRE(juce::File(GOLDEN_PATH).existsAsFile());

    auto golden = readGolden(GOLDEN_PATH, rendered.getNumChannels(), TOTAL_RENDER_SAMPLES);
    REQUIRE(golden.getNumSamples() > 0);
    INFO("Golden samples: " << golden.getNumSamples()
         << " expected: " << TOTAL_RENDER_SAMPLES);
    REQUIRE(golden.getNumSamples() == TOTAL_RENDER_SAMPLES);

    const float peakDiff = peakResidual(rendered, golden);
    const float peakDiffDB = (peakDiff > 0.0f)
        ? juce::Decibels::gainToDecibels(peakDiff, -200.0f)
        : -200.0f;

    INFO("Peak residual: " << peakDiffDB << " dBFS  (pass threshold: < -120 dBFS)");
    REQUIRE(peakDiffDB < -120.0f);
}

TEST_CASE("Golden render — perturbation is detectable above -120 dBFS", "[golden_render]")
{
    // This test proves the harness actually catches audio drift:
    // adding 0.001 linear ≈ -60 dBFS, which is 60 dB above the -120 dBFS pass threshold.
    auto rendered = renderFixtureKit();

    juce::AudioBuffer<float> perturbed(rendered.getNumChannels(),
                                       rendered.getNumSamples());
    for (int ch = 0; ch < rendered.getNumChannels(); ++ch)
        perturbed.copyFrom(ch, 0, rendered, ch, 0, rendered.getNumSamples());

    // Inject a deliberate 0.001-amplitude error on sample 0, channel 0
    perturbed.addSample(0, 0, 0.001f);

    const float peakDiff = peakResidual(rendered, perturbed);
    const float peakDiffDB = (peakDiff > 0.0f)
        ? juce::Decibels::gainToDecibels(peakDiff, -200.0f)
        : -200.0f;

    INFO("Perturbation residual: " << peakDiffDB << " dBFS");
    // A 0.001 perturbation must exceed the -120 dBFS threshold (it's ≈ -60 dBFS)
    REQUIRE(peakDiffDB > -120.0f);
}
