// GoldenRenderTest.cpp
// CTEST-5 — L2 golden-render harness.
//
// Renders the synthetic fixture kit (Tests/Fixtures/golden-kit/) with a fixed
// seed and event schedule, then null-tests the output against a committed
// reference WAV.  Residual peak must be < -120 dBFS.
//
// To regenerate the golden reference after an intentional DSP change:
//   FLAM_UPDATE_GOLDEN=1 ./flam-tests "[golden_render]"
// Review the diff in Tests/Fixtures/goldens/golden_render.wav before committing.

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "Core/FlamEngine.h"

// ---------------------------------------------------------------------------
// Compile-time fixture paths (set by CMakeLists.txt)
// ---------------------------------------------------------------------------
#ifndef FLAM_TEST_FIXTURES_DIR
#  error "FLAM_TEST_FIXTURES_DIR must be defined by the CMake build system"
#endif

static constexpr const char* FIXTURE_KIT_PATH =
    FLAM_TEST_FIXTURES_DIR "/golden-kit/flamkit.yaml";
static constexpr const char* GOLDEN_WAV_PATH =
    FLAM_TEST_FIXTURES_DIR "/goldens/golden_render.wav";

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
// WAV I/O helpers
// ---------------------------------------------------------------------------
static void writeWav(const juce::AudioBuffer<float>& buf,
                     const juce::String& path,
                     double sampleRate)
{
    juce::WavAudioFormat fmt;
    juce::File outFile(path);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();

    auto* fos = outFile.createOutputStream().release();
    REQUIRE(fos != nullptr);

    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        fmt.createWriterFor(fos, sampleRate,
                            (unsigned)buf.getNumChannels(), 24, {}, 0));
    REQUIRE(writer != nullptr);
    writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
}

static juce::AudioBuffer<float> readWav(const juce::String& path)
{
    juce::WavAudioFormat fmt;
    juce::File inFile(path);
    if (!inFile.existsAsFile())
        return {};

    auto* fis = inFile.createInputStream().release();
    if (!fis)
        return {};

    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        fmt.createReaderFor(fis, true));
    if (!reader)
        return {};

    juce::AudioBuffer<float> result((int)reader->numChannels,
                                    (int)reader->lengthInSamples);
    reader->read(&result, 0, (int)reader->lengthInSamples, 0, true, true);
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

        engine.processBlock(block, midi);

        // Accumulate into the full-length output buffer
        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom(ch, blockStart, block, ch, 0, blockLen);
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
        // Regenerate mode: write the current render as the new reference.
        // Review the resulting WAV diff before committing.
        INFO("FLAM_UPDATE_GOLDEN is set — writing new golden reference to " << GOLDEN_WAV_PATH);
        writeWav(rendered, GOLDEN_WAV_PATH, RENDER_SAMPLE_RATE);
        SUCCEED("Golden reference updated.");
        return;
    }

    // Comparison mode: null-test against committed golden
    INFO("Golden path: " << GOLDEN_WAV_PATH << "  — run with FLAM_UPDATE_GOLDEN=1 to regenerate");
    REQUIRE(juce::File(GOLDEN_WAV_PATH).existsAsFile());

    auto golden = readWav(GOLDEN_WAV_PATH);
    REQUIRE(golden.getNumSamples() > 0);
    INFO("Golden samples: " << golden.getNumSamples()
         << " expected: " << TOTAL_RENDER_SAMPLES);
    REQUIRE(golden.getNumSamples() == TOTAL_RENDER_SAMPLES);

    const float peakDiff = peakResidual(rendered, golden);
    const float peakDiffDB = (peakDiff > 0.0f)
        ? juce::Decibels::gainToDecibels(peakDiff)
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
        ? juce::Decibels::gainToDecibels(peakDiff)
        : -200.0f;

    INFO("Perturbation residual: " << peakDiffDB << " dBFS");
    // A 0.001 perturbation must exceed the -120 dBFS threshold (it's ≈ -60 dBFS)
    REQUIRE(peakDiffDB > -120.0f);
}
