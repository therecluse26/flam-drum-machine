#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/LimiterProcessor.h"
#include "DSP/CompressorProcessor.h"
#include "DSP/TenBandGraphicEQ.h"
#include "DSP/SaturationProcessor.h"
#include "Core/Mixer.h"

namespace flam {

// ============================================================================
// Utility

static juce::AudioBuffer<float> makeSilentBuffer(int channels, int samples)
{
    juce::AudioBuffer<float> buf(channels, samples);
    buf.clear();
    return buf;
}

// Fill buffer channels with a constant value
static void fillConstant(juce::AudioBuffer<float>& buf, float value)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        juce::FloatVectorOperations::fill(buf.getWritePointer(ch), value, buf.getNumSamples());
}

// Check that two buffers are identical within tolerance
static bool buffersEqual(const juce::AudioBuffer<float>& a,
                         const juce::AudioBuffer<float>& b,
                         float tol = 1e-6f)
{
    if (a.getNumChannels() != b.getNumChannels()) return false;
    if (a.getNumSamples()  != b.getNumSamples())  return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int s = 0; s < a.getNumSamples(); ++s)
            if (std::abs(a.getSample(ch, s) - b.getSample(ch, s)) > tol)
                return false;
    return true;
}

// Check that all samples in buffer are below |threshold|
static bool bufferBelowThreshold(const juce::AudioBuffer<float>& buf, float threshold)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        for (int s = 0; s < buf.getNumSamples(); ++s)
            if (std::abs(buf.getSample(ch, s)) > threshold)
                return false;
    return true;
}

// Check that a buffer is silent (all zeros within tolerance)
static bool isSilent(const juce::AudioBuffer<float>& buf, float tol = 1e-9f)
{
    return bufferBelowThreshold(buf, tol);
}

// ============================================================================

class LimiterProcessorTest : public juce::UnitTest
{
public:
    LimiterProcessorTest() : juce::UnitTest("LimiterProcessor", "DSP") {}

    void runTest() override
    {
        testBypassIsDefaultOff();
        testBypassPassthrough();
        testSilenceInSilenceOut();
        testNeverExceedsThreshold();
        testThresholdRoundTrip();
        testReleaseRoundTrip();
    }

private:
    void testBypassIsDefaultOff()
    {
        beginTest("Default state is bypassed (disabled)");
        LimiterProcessor lim;
        expect(!lim.isEnabled());
    }

    void testBypassPassthrough()
    {
        beginTest("Bypassed limiter — bit-exact passthrough");
        LimiterProcessor lim;
        lim.prepareToPlay(44100.0);
        // enabled defaults to false

        const int N = 512;
        auto original = makeSilentBuffer(2, N);
        // Fill with arbitrary ramp
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < N; ++s)
                original.setSample(ch, s, (float)s / N - 0.5f);

        juce::AudioBuffer<float> processed(2, N);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(
                processed.getWritePointer(ch), original.getReadPointer(ch), N);

        lim.process(processed, N);

        expect(buffersEqual(processed, original),
               "Bypassed limiter must not modify samples");
    }

    void testSilenceInSilenceOut()
    {
        beginTest("Enabled limiter: silence in → silence out");
        LimiterProcessor lim;
        lim.prepareToPlay(44100.0);
        lim.setEnabled(true);
        lim.setThreshold(-0.1f);

        auto buf = makeSilentBuffer(2, 512);
        lim.process(buf, 512);

        expect(isSilent(buf), "Silence in must produce silence out");
    }

    void testNeverExceedsThreshold()
    {
        beginTest("Enabled limiter: output never exceeds threshold on hot input");
        LimiterProcessor lim;
        lim.prepareToPlay(44100.0);
        lim.setEnabled(true);
        const float threshDb = -0.1f;
        lim.setThreshold(threshDb);
        lim.setRelease(10.0f);  // Fast release for clean test

        // Warm up the envelope with a pre-roll so we test steady-state limiting
        {
            juce::AudioBuffer<float> warmup(2, 512);
            fillConstant(warmup, 10.0f);
            lim.process(warmup, 512);
        }

        juce::AudioBuffer<float> buf(2, 512);
        fillConstant(buf, 10.0f);  // Hot signal: +20 dBFS
        lim.process(buf, 512);

        const float threshLinear = juce::Decibels::decibelsToGain(threshDb) + 1e-3f;
        expect(bufferBelowThreshold(buf, threshLinear),
               "All samples must be ≤ threshold after limiting");
    }

    void testThresholdRoundTrip()
    {
        beginTest("setThreshold / getThreshold round-trips within [-1, 0] clamp");
        LimiterProcessor lim;
        lim.setThreshold(-0.5f);
        expectWithinAbsoluteError(lim.getThreshold(), -0.5f, 1e-4f);

        lim.setThreshold(5.0f);   // Clamped to 0
        expectWithinAbsoluteError(lim.getThreshold(), 0.0f, 1e-4f);

        lim.setThreshold(-5.0f);  // Clamped to -1
        expectWithinAbsoluteError(lim.getThreshold(), -1.0f, 1e-4f);
    }

    void testReleaseRoundTrip()
    {
        beginTest("setRelease / getRelease round-trips within [10, 500] clamp");
        LimiterProcessor lim;
        lim.setRelease(100.0f);
        expectWithinAbsoluteError(lim.getRelease(), 100.0f, 1e-4f);

        lim.setRelease(1.0f);    // Clamped to 10
        expectWithinAbsoluteError(lim.getRelease(), 10.0f, 1e-4f);

        lim.setRelease(9999.0f); // Clamped to 500
        expectWithinAbsoluteError(lim.getRelease(), 500.0f, 1e-4f);
    }
};

// ============================================================================

class CompressorProcessorTest : public juce::UnitTest
{
public:
    CompressorProcessorTest() : juce::UnitTest("CompressorProcessor", "DSP") {}

    void runTest() override
    {
        testBypassPassthrough();
        testSilenceInSilenceOut();
        testCompressorReducesGainAboveThreshold();
        testCompressorHasNoEffectBelowThreshold();
    }

private:
    void testBypassPassthrough()
    {
        beginTest("Bypassed compressor — bit-exact passthrough");
        CompressorProcessor comp;
        comp.prepareToPlay(44100.0);
        // enabled defaults to false

        const int N = 512;
        auto original = makeSilentBuffer(2, N);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < N; ++s)
                original.setSample(ch, s, (float)s / N - 0.5f);

        juce::AudioBuffer<float> processed(2, N);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(
                processed.getWritePointer(ch), original.getReadPointer(ch), N);

        comp.process(processed, N);
        expect(buffersEqual(processed, original),
               "Bypassed compressor must not modify samples");
    }

    void testSilenceInSilenceOut()
    {
        beginTest("Enabled compressor: silence in → silence out");
        CompressorProcessor comp;
        comp.prepareToPlay(44100.0);
        comp.setEnabled(true);
        comp.setThreshold(-10.0f);
        comp.setRatio(4.0f);

        auto buf = makeSilentBuffer(2, 512);
        comp.process(buf, 512);
        expect(isSilent(buf), "Silence in must produce silence out");
    }

    void testCompressorReducesGainAboveThreshold()
    {
        beginTest("Compressor reduces gain above threshold");
        CompressorProcessor comp;
        comp.prepareToPlay(44100.0);
        comp.setEnabled(true);
        comp.setThreshold(-10.0f);
        comp.setRatio(4.0f);
        comp.setAttack(1.0f);
        comp.setRelease(50.0f);
        comp.setMakeupGain(0.0f);

        const float inputLevel = 0.5f;  // ≈ -6 dBFS, above threshold of -10 dBFS

        // Warm up envelope
        {
            juce::AudioBuffer<float> warmup(2, 1024);
            fillConstant(warmup, inputLevel);
            comp.process(warmup, 1024);
        }

        juce::AudioBuffer<float> buf(2, 512);
        fillConstant(buf, inputLevel);
        comp.process(buf, 512);

        // After warm-up, the steady-state output should be below the un-compressed input
        const float lastSample = std::abs(buf.getSample(0, 511));
        expect(lastSample < inputLevel,
               "Compressor must reduce gain for signal above threshold");
    }

    void testCompressorHasNoEffectBelowThreshold()
    {
        beginTest("Compressor: signal below threshold passes unchanged after warm-up");
        CompressorProcessor comp;
        comp.prepareToPlay(44100.0);
        comp.setEnabled(true);
        comp.setThreshold(-10.0f);
        comp.setRatio(4.0f);
        comp.setRelease(100.0f);
        comp.setMakeupGain(0.0f);

        const float inputLevel = 0.01f;  // -40 dBFS, well below -10 dBFS threshold

        // The envelope starts at 0 dBFS and must decay below threshold (-10 dB)
        // before the compressor is inactive. With 100ms release, the envelope reaches
        // ~-40 dBFS after ~5 time constants ≈ 500ms (≈22,050 samples). Warm up with
        // 60 blocks of 512 samples = 30,720 samples ≈ 700ms to safely exceed that.
        for (int b = 0; b < 60; ++b)
        {
            juce::AudioBuffer<float> warm(2, 512);
            fillConstant(warm, inputLevel);
            comp.process(warm, 512);
        }

        // After warm-up the envelope is well below threshold; check steady-state output
        juce::AudioBuffer<float> buf(2, 512);
        fillConstant(buf, inputLevel);
        comp.process(buf, 512);

        const float out = std::abs(buf.getSample(0, 511));
        expectWithinAbsoluteError(out, inputLevel, inputLevel * 0.05f,
            "Below-threshold signal should pass with <5% change after envelope settles");
    }
};

// ============================================================================

class TenBandGraphicEQTest : public juce::UnitTest
{
public:
    TenBandGraphicEQTest() : juce::UnitTest("TenBandGraphicEQ", "DSP") {}

    void runTest() override
    {
        testBypassPassthrough();
        testSilenceInSilenceOut();
        testBandGainRoundTrip();
    }

private:
    void testBypassPassthrough()
    {
        beginTest("Bypassed EQ — bit-exact passthrough");
        TenBandGraphicEQ eq;
        eq.prepareToPlay(44100.0, 512);
        // enabled defaults to false

        const int N = 512;
        auto original = makeSilentBuffer(2, N);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < N; ++s)
                original.setSample(ch, s, (float)s / N - 0.5f);

        juce::AudioBuffer<float> processed(2, N);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(
                processed.getWritePointer(ch), original.getReadPointer(ch), N);

        eq.process(processed, N);
        expect(buffersEqual(processed, original),
               "Bypassed EQ must not modify samples");
    }

    void testSilenceInSilenceOut()
    {
        beginTest("Enabled EQ: silence in → silence out");
        TenBandGraphicEQ eq;
        eq.prepareToPlay(44100.0, 512);
        eq.setEnabled(true);

        // Set some band gains — shouldn't matter, silence stays silent
        eq.setBandGain(3, 6.0f);
        eq.setBandGain(7, -3.0f);

        auto buf = makeSilentBuffer(2, 512);
        eq.process(buf, 512);
        expect(isSilent(buf), "Silence in must produce silence out");
    }

    void testBandGainRoundTrip()
    {
        beginTest("setBandGain / getBandGain round-trips for all 10 bands");
        TenBandGraphicEQ eq;
        eq.prepareToPlay(44100.0, 512);

        for (int i = 0; i < TenBandGraphicEQ::NUM_BANDS; ++i)
        {
            const float gain = (float)(i - 5);  // -5 dB to +4 dB
            eq.setBandGain(i, gain);
            expectWithinAbsoluteError(eq.getBandGain(i), gain, 1e-4f,
                "Band " + juce::String(i) + " gain round-trip");
        }
    }
};

// ============================================================================

class SaturationProcessorTest : public juce::UnitTest
{
public:
    SaturationProcessorTest() : juce::UnitTest("SaturationProcessor", "DSP") {}

    void runTest() override
    {
        testBypassPassthrough();
        testSilenceInSilenceOut();
    }

private:
    void testBypassPassthrough()
    {
        beginTest("Bypassed saturation — bit-exact passthrough");
        SaturationProcessor sat;
        sat.prepareToPlay(44100.0);
        // enabled defaults to false

        const int N = 512;
        auto original = makeSilentBuffer(2, N);
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < N; ++s)
                original.setSample(ch, s, (float)s / N - 0.5f);

        juce::AudioBuffer<float> processed(2, N);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::copy(
                processed.getWritePointer(ch), original.getReadPointer(ch), N);

        sat.process(processed, N);
        expect(buffersEqual(processed, original),
               "Bypassed saturation must not modify samples");
    }

    void testSilenceInSilenceOut()
    {
        beginTest("Enabled saturation: silence in → silence out");
        SaturationProcessor sat;
        sat.prepareToPlay(44100.0);
        sat.setEnabled(true);
        sat.setAmount(1.0f);

        for (int mode = 0; mode < 3; ++mode)
        {
            sat.setMode(static_cast<SaturationProcessor::Mode>(mode));
            auto buf = makeSilentBuffer(2, 512);
            sat.process(buf, 512);
            expect(isSilent(buf),
                   "Silence in must produce silence out (mode " + juce::String(mode) + ")");
        }
    }
};

// ============================================================================

class MixerTest : public juce::UnitTest
{
public:
    MixerTest() : juce::UnitTest("Mixer", "Core") {}

    void runTest() override
    {
        testZeroDB();
        testMute();
        testPanFullLeft();
        testPanFullRight();
        testPanCenter();
        testSoloPassesOnlySoloed();
    }

private:
    // Helper: single mono channel routed to MainMix, constant input amplitude
    // Returns output main-mix buffer (2 ch) for inspection.
    juce::AudioBuffer<float> processWithMixer(
        float inputAmplitude,
        float volumeDb,
        float pan,
        bool mute,
        bool solo)
    {
        const int N = 512;
        Mixer mixer;
        mixer.setNumChannels(1, {"Test"});
        mixer.prepareToPlay(44100.0, N);

        mixer.setChannelOutput(0, Mixer::OutputDestination::MainMix);
        mixer.setChannelVolume(0, volumeDb);
        mixer.setChannelPan(0, pan);
        mixer.setChannelMute(0, mute);
        mixer.setChannelSolo(0, solo);
        mixer.setMasterVolume(0.0f);  // 0 dB master

        // Input: 1 mono channel
        juce::AudioBuffer<float> input(1, N);
        fillConstant(input, inputAmplitude);

        // Output: main mix (2 ch) + 1 individual bus ch = 3 ch minimum
        juce::AudioBuffer<float> output(3, N);
        output.clear();

        mixer.process(input, output, N);

        // Return just the main mix stereo pair
        juce::AudioBuffer<float> mainMix(2, N);
        mainMix.copyFrom(0, 0, output, 0, 0, N);
        mainMix.copyFrom(1, 0, output, 1, 0, N);
        return mainMix;
    }

    void testZeroDB()
    {
        beginTest("0 dB volume, center pan — output amplitude follows constant-power curve");
        const float amp = 0.8f;
        auto out = processWithMixer(amp, 0.0f, 0.0f, false, false);

        // Center pan: panAngle = π/4 → leftGain = rightGain = cos(π/4) ≈ 0.7071
        const float expected = amp * std::cos(juce::MathConstants<float>::pi / 4.0f);
        expectWithinAbsoluteError(out.getSample(0, 256), expected, 0.005f, "Left channel at 0 dB");
        expectWithinAbsoluteError(out.getSample(1, 256), expected, 0.005f, "Right channel at 0 dB");
    }

    void testMute()
    {
        beginTest("Muted channel → silence in main mix");
        auto out = processWithMixer(0.8f, 0.0f, 0.0f, /*mute=*/true, false);
        expect(isSilent(out), "Muted channel must produce silence");
    }

    void testPanFullLeft()
    {
        beginTest("Pan = -1 → all audio on left channel, silence on right");
        const float amp = 0.8f;
        auto out = processWithMixer(amp, 0.0f, -1.0f, false, false);

        // panAngle = 0 → leftGain = cos(0) = 1, rightGain = sin(0) = 0
        expectWithinAbsoluteError(out.getSample(0, 256), amp, 0.005f, "Left should have signal");
        expectWithinAbsoluteError(out.getSample(1, 256), 0.0f, 0.005f, "Right should be silent");
    }

    void testPanFullRight()
    {
        beginTest("Pan = +1 → all audio on right channel, silence on left");
        const float amp = 0.8f;
        auto out = processWithMixer(amp, 0.0f, 1.0f, false, false);

        // panAngle = π/2 → leftGain = cos(π/2) = 0, rightGain = sin(π/2) = 1
        expectWithinAbsoluteError(out.getSample(0, 256), 0.0f, 0.005f, "Left should be silent");
        expectWithinAbsoluteError(out.getSample(1, 256), amp, 0.005f, "Right should have signal");
    }

    void testPanCenter()
    {
        beginTest("Pan = 0 → equal-power split on both channels");
        const float amp = 1.0f;
        auto out = processWithMixer(amp, 0.0f, 0.0f, false, false);

        const float l = out.getSample(0, 256);
        const float r = out.getSample(1, 256);

        // Both channels must be equal
        expectWithinAbsoluteError(l, r, 0.001f, "L and R must be equal at center pan");

        // Power law: l² + r² ≈ amp²  (constant-power panning)
        const float power = l * l + r * r;
        expectWithinAbsoluteError(power, amp * amp, 0.01f,
            "Total power must be conserved (l² + r² ≈ input²)");
    }

    void testSoloPassesOnlySoloed()
    {
        beginTest("Solo'd channel passes; non-solo'd channel is silenced");
        const int N = 512;
        Mixer mixer;
        mixer.setNumChannels(2, {"A", "B"});
        mixer.prepareToPlay(44100.0, N);

        mixer.setChannelOutput(0, Mixer::OutputDestination::MainMix);
        mixer.setChannelOutput(1, Mixer::OutputDestination::MainMix);
        mixer.setChannelVolume(0, 0.0f);
        mixer.setChannelVolume(1, 0.0f);
        mixer.setChannelPan(0, -1.0f);   // Channel A hard-left
        mixer.setChannelPan(1,  1.0f);   // Channel B hard-right
        mixer.setChannelSolo(0, true);    // Solo channel A only
        mixer.setMasterVolume(0.0f);

        // Input: 2 channels, both filled with 0.5
        juce::AudioBuffer<float> input(2, N);
        fillConstant(input, 0.5f);

        juce::AudioBuffer<float> output(4, N);  // 2 main mix + 2 buses
        output.clear();
        mixer.process(input, output, N);

        // Solo'd ch A is hard-left → L should have signal, R should be silent
        // (ch B is not solo'd → silenced even though it routes to R)
        const float left  = output.getSample(0, 256);
        const float right = output.getSample(1, 256);

        expect(std::abs(left) > 0.01f, "Solo'd left channel must produce output");
        expectWithinAbsoluteError(right, 0.0f, 0.01f, "Non-solo'd right channel must be silent");
    }
};

// ============================================================================
// L2: Mixer master param write-through test
// Asserts that every automatable master setter/getter round-trips through the
// Mixer master state — the same path that updateEngineParameters() now drives.

class MixerMasterParamTest : public juce::UnitTest
{
public:
    MixerMasterParamTest() : juce::UnitTest("MixerMasterParam", "Core") {}

    void runTest() override
    {
        testMasterVolume();
        testMasterEQEnabled();
        testMasterEQBandGains();
        testMasterCompressorEnabled();
        testMasterCompressorThreshold();
        testMasterCompressorRatio();
        testMasterCompressorAttack();
        testMasterCompressorRelease();
        testMasterCompressorMakeupGain();
    }

private:
    void testMasterVolume()
    {
        beginTest("setMasterVolume / getMasterVolume round-trip");
        Mixer mixer;
        mixer.setNumChannels(1, {"Test"});
        mixer.prepareToPlay(44100.0, 512);

        mixer.setMasterVolume(-6.0f);
        expectWithinAbsoluteError(mixer.getMasterVolume(), -6.0f, 1e-4f,
            "Master volume must round-trip");
    }

    void testMasterEQEnabled()
    {
        beginTest("setMasterEQEnabled / isMasterEQEnabled round-trip");
        Mixer mixer;
        mixer.setNumChannels(1, {"Test"});
        mixer.prepareToPlay(44100.0, 512);

        mixer.setMasterEQEnabled(true);
        expect(mixer.isMasterEQEnabled(), "Master EQ must report enabled after set");

        mixer.setMasterEQEnabled(false);
        expect(!mixer.isMasterEQEnabled(), "Master EQ must report disabled after clear");
    }

    void testMasterEQBandGains()
    {
        beginTest("setMasterEQBandGain / getMasterEQBandGain round-trips for all 10 bands");
        Mixer mixer;
        mixer.setNumChannels(1, {"Test"});
        mixer.prepareToPlay(44100.0, 512);

        for (int i = 0; i < 10; ++i)
        {
            const float gain = static_cast<float>(i - 5);
            mixer.setMasterEQBandGain(i, gain);
            expectWithinAbsoluteError(mixer.getMasterEQBandGain(i), gain, 1e-4f,
                "Band " + juce::String(i) + " gain must round-trip");
        }
    }

    void testMasterCompressorEnabled()
    {
        beginTest("setMasterCompressorEnabled / isMasterCompressorEnabled round-trip");
        Mixer mixer;
        mixer.setNumChannels(1, {"Test"});
        mixer.prepareToPlay(44100.0, 512);

        mixer.setMasterCompressorEnabled(true);
        expect(mixer.isMasterCompressorEnabled(), "Master compressor must report enabled");

        mixer.setMasterCompressorEnabled(false);
        expect(!mixer.isMasterCompressorEnabled(), "Master compressor must report disabled");
    }

    void testMasterCompressorThreshold()
    {
        beginTest("setMasterCompressorThreshold / getMasterCompressorThreshold round-trip");
        Mixer mixer;
        mixer.setNumChannels(1, {"Test"});
        mixer.prepareToPlay(44100.0, 512);

        mixer.setMasterCompressorThreshold(-18.0f);
        expectWithinAbsoluteError(mixer.getMasterCompressorThreshold(), -18.0f, 1e-4f);
    }

    void testMasterCompressorRatio()
    {
        beginTest("setMasterCompressorRatio / getMasterCompressorRatio round-trip");
        Mixer mixer;
        mixer.setNumChannels(1, {"Test"});
        mixer.prepareToPlay(44100.0, 512);

        mixer.setMasterCompressorRatio(4.0f);
        expectWithinAbsoluteError(mixer.getMasterCompressorRatio(), 4.0f, 1e-4f);
    }

    void testMasterCompressorAttack()
    {
        beginTest("setMasterCompressorAttack / getMasterCompressorAttack round-trip");
        Mixer mixer;
        mixer.setNumChannels(1, {"Test"});
        mixer.prepareToPlay(44100.0, 512);

        mixer.setMasterCompressorAttack(10.0f);
        expectWithinAbsoluteError(mixer.getMasterCompressorAttack(), 10.0f, 1e-4f);
    }

    void testMasterCompressorRelease()
    {
        beginTest("setMasterCompressorRelease / getMasterCompressorRelease round-trip");
        Mixer mixer;
        mixer.setNumChannels(1, {"Test"});
        mixer.prepareToPlay(44100.0, 512);

        mixer.setMasterCompressorRelease(100.0f);
        expectWithinAbsoluteError(mixer.getMasterCompressorRelease(), 100.0f, 1e-4f);
    }

    void testMasterCompressorMakeupGain()
    {
        beginTest("setMasterCompressorMakeupGain / getMasterCompressorMakeupGain round-trip");
        Mixer mixer;
        mixer.setNumChannels(1, {"Test"});
        mixer.prepareToPlay(44100.0, 512);

        mixer.setMasterCompressorMakeupGain(6.0f);
        expectWithinAbsoluteError(mixer.getMasterCompressorMakeupGain(), 6.0f, 1e-4f);
    }
};

// ============================================================================
// Static registration

static LimiterProcessorTest    limiterTest;
static CompressorProcessorTest compressorTest;
static TenBandGraphicEQTest    eqTest;
static SaturationProcessorTest satTest;
static MixerTest               mixerTest;
static MixerMasterParamTest    mixerMasterParamTest;

} // namespace flam
