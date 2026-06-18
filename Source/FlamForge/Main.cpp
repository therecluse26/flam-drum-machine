// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

// FlamForge — standalone kit-recording companion to FLAM.
//
// This is the application skeleton (FLA-119): a real, launchable JUCE gui_app
// with live audio-device selection and the full fingerprint-registration
// workflow laid out. The functional stages — capture (FLA-120), coverage
// scoring (FLA-121), layer synthesis (FLA-122) and YAML export (FLA-124) —
// are dispatched separately and wired into this shell as they land.
//
// FlamForge is deliberately its OWN executable (juce_add_gui_app), not a mode
// of the FLAM plugin: it is a recording/authoring tool, has no DAW host, and
// must own the audio device directly. It shares engine/format code with FLAM
// through the forthcoming FlamCore library (FLA-116).

#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.h"
#include "CaptureEngine.h"
#include "CaptureTypes.h"
#include "LayerSynth.h"
#include "KitExporter.h"
#include "Formats/FlamKitLoader.h"

#include <cmath>
#include <iostream>

namespace flamforge
{

// ---------------------------------------------------------------------------
// Headless self-test (run with `FlamForge --selftest`).
//
// The GUI smoke test can't exercise the data pipeline because Xvfb exposes no
// audio input. This drives synth -> export -> reload directly with synthetic
// hits and asserts the kit round-trips through FlamKitLoader, so the
// load-bearing logic is proven by an actual run, not just by compiling.
// Returns 0 on PASS, non-zero on FAIL.
// ---------------------------------------------------------------------------
static int runSelfTest()
{
    auto say = [] (const juce::String& s) { std::cout << "SELFTEST " << s << std::endl; };

    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("flamforge_selftest");
    tmp.deleteRecursively();
    tmp.createDirectory();

    auto makePiece = [] (const juce::String& name)
    {
        PieceCapture pc;
        pc.name = name;
        for (int i = 0; i < 12; ++i)
        {
            CapturedHit h;
            h.sampleRate   = 48000.0;
            h.midiVelocity = juce::jlimit (1, 127, 10 + i * 10);
            const float amp = juce::jmap ((float) h.midiVelocity, 1.0f, 127.0f, 0.05f, 0.9f);
            h.peakDb = juce::Decibels::gainToDecibels (amp);

            juce::AudioBuffer<float> buf (2, 4800);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < 4800; ++s)
                    buf.setSample (ch, s, amp * std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * s / 48000.0)
                                              * std::exp (-3.0 * s / 4800.0));
            h.audio = std::move (buf);
            pc.hits.push_back (std::move (h));
        }
        return pc;
    };

    std::vector<PieceCapture> captures { makePiece ("Kick"), makePiece ("Snare") };
    SynthOptions opts;

    // 1) synthesis
    auto piece = synthesizePiece (captures[0], opts);
    const int nLayers = piece.articulations.empty() ? 0 : (int) piece.articulations[0].layers.size();
    say ("synth: piece='" + piece.name + "' layers=" + juce::String (nLayers));
    if (piece.articulations.empty() || nLayers == 0) { say ("FAIL: no layers synthesized"); return 1; }

    // 2) export
    auto res = exportKit ("FlamForge SelfTest Kit", captures, opts, tmp);
    say ("export: ok=" + juce::String ((int) res.ok) + " wavs=" + juce::String (res.wavCount)
         + " msg=" + res.message);
    if (! res.ok || res.wavCount <= 0 || ! res.kitYaml.existsAsFile()) { say ("FAIL: export"); return 1; }

    // 3) reload + validate round-trip
    flam::FlamKitLoader loader;
    auto kit = loader.loadKit (res.kitYaml);
    if (kit == nullptr) { say ("FAIL: reload null: " + loader.getLastError()); return 1; }
    const int pieces = (int) kit->pieces.size();
    const int totalLayers = kit->getTotalSampleCount();
    say ("reload: pieces=" + juce::String (pieces) + " layers=" + juce::String (totalLayers));
    if (pieces < 2 || totalLayers <= 0) { say ("FAIL: reloaded kit empty"); return 1; }

    // 4) every referenced wav must exist on disk
    int missing = 0;
    for (auto& p : kit->pieces)
        for (auto& a : p.articulations)
            for (auto& l : a.layers)
                if (! l.sampleFile.existsAsFile()) ++missing;
    say ("wav-refs missing=" + juce::String (missing));
    if (missing > 0) { say ("FAIL: reloaded layers reference missing wavs"); return 1; }

    // 5) label round-trip: user channel labels survive export → YAML → reload (FLA-139)
    {
        auto tmp2 = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("flamforge_labeltest");
        tmp2.deleteRecursively();
        tmp2.createDirectory();

        // "Left" and "" (blank): blank should fall back to "Mic 2".
        const std::vector<juce::String> testLabels { "Left", "" };
        auto res2 = exportKit ("Label Test Kit", captures, opts, tmp2, testLabels);
        say ("label-export: ok=" + juce::String ((int) res2.ok));
        if (! res2.ok) { say ("FAIL: label export: " + res2.message); return 1; }

        flam::FlamKitLoader ldr;
        auto kit2 = ldr.loadKit (res2.kitYaml);
        if (kit2 == nullptr) { say ("FAIL: label reload"); return 1; }

        const bool ch0ok = kit2->channelNames.size() > 0 && kit2->channelNames[0] == "Left";
        const bool ch1ok = kit2->channelNames.size() > 1 && kit2->channelNames[1] == "Mic 2";
        say ("label round-trip: ch0=" + (kit2->channelNames.size() > 0 ? kit2->channelNames[0] : "?")
             + " ch1=" + (kit2->channelNames.size() > 1 ? kit2->channelNames[1] : "?"));
        if (! ch0ok) { say ("FAIL: ch0 label expected 'Left'"); return 1; }
        if (! ch1ok) { say ("FAIL: ch1 blank label expected 'Mic 2' fallback"); return 1; }

        tmp2.deleteRecursively();
    }

    // 6) per-channel metering atomics (FLA-137)
    // Feed 3-channel synthetic blocks: ch0 hot, ch1 silent, ch2 mid.
    // Assert channelLevelDb(c) reflects each channel independently.
    {
        CaptureEngine engine;
        engine.audioDeviceAboutToStart (nullptr);   // init buffers; null → 48 kHz / 1 ch fallback

        constexpr int kTestCh   = 3;
        constexpr int kBlockSz  = 256;

        const float ampHot = 0.9f;    // ch0 — hot, ~-0.9 dBFS
        const float ampSil = 0.0f;    // ch1 — silence, expect -100 dBFS
        const float ampMid = 0.1f;    // ch2 — mid, ~-20 dBFS

        std::vector<float> buf0 (kBlockSz, ampHot);
        std::vector<float> buf1 (kBlockSz, ampSil);
        std::vector<float> buf2 (kBlockSz, ampMid);

        const float* inPtrs[kTestCh] = { buf0.data(), buf1.data(), buf2.data() };

        juce::AudioIODeviceCallbackContext ctx{};
        engine.audioDeviceIOCallbackWithContext (inPtrs, kTestCh,
                                                 nullptr, 0, kBlockSz, ctx);

        const float dbHot = engine.channelLevelDb (0);
        const float dbSil = engine.channelLevelDb (1);
        const float dbMid = engine.channelLevelDb (2);
        const int   count = engine.channelCount();

        say ("metering: ch0=" + juce::String (dbHot, 1)
             + " ch1=" + juce::String (dbSil, 1)
             + " ch2=" + juce::String (dbMid, 1)
             + " count=" + juce::String (count));

        if (dbHot < -3.0f)
        { say ("FAIL: ch0 expected hot (>-3 dBFS), got " + juce::String (dbHot, 1)); return 1; }
        if (dbSil > -90.0f)
        { say ("FAIL: ch1 expected silent (<-90 dBFS), got " + juce::String (dbSil, 1)); return 1; }
        if (dbMid > -15.0f || dbMid < -30.0f)
        { say ("FAIL: ch2 expected mid [-30,-15] dBFS, got " + juce::String (dbMid, 1)); return 1; }
        if (count != kTestCh)
        { say ("FAIL: channelCount expected " + juce::String (kTestCh) + ", got " + juce::String (count)); return 1; }
    }

    say ("PASS");
    return 0;
}


class FlamForgeApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "FlamForge"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.contains ("--selftest")
            || juce::JUCEApplicationBase::getCommandLineParameters().contains ("--selftest"))
        {
            setApplicationReturnValue (runSelfTest());
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override { mainWindow = nullptr; }

    void systemRequestedQuit() override { quit(); }

    // A resizable document window hosting the FlamForge MainComponent.
    // Inherits Timer so we can fire a delayed geometry re-assert after the WM
    // has finished applying its own show-time restore heuristics.
    class MainWindow : public juce::DocumentWindow,
                       private juce::Timer
    {
    public:
        explicit MainWindow (const juce::String& name)
            : juce::DocumentWindow (name,
                                    juce::Colour (0xff0d0f12),
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);

            auto* mc = new MainComponent();
            enforcedW = mc->getWidth();
            enforcedH = mc->getHeight();

            std::cerr << "[MainWindow] mc=" << enforcedW << "x" << enforcedH
                      << " scale=" << juce::Desktop::getInstance().getGlobalScaleFactor()
                      << "\n";

            setContentOwned (mc, true);
            setResizable (true, true);
            setResizeLimits (640, 700, 99999, 99999);

            // Restore our saved window size, enforcing the content minimum.
            initWindowProps();
            if (auto* s = windowProps.getUserSettings())
            {
                enforcedW = juce::jmax (enforcedW, s->getIntValue ("windowW", enforcedW));
                enforcedH = juce::jmax (enforcedH, s->getIntValue ("windowH", enforcedH));
            }

            centreWithSize (enforcedW, enforcedH);
            setVisible (true);

            // The WM applies its stored geometry at show-time, overriding the
            // centreWithSize above. Fire at 50 ms and 200 ms to re-assert AFTER
            // the WM has finished. 50 ms catches most WMs; 200 ms catches KDE
            // Plasma which has a second geometry-restore pass.
            startTimer (50);
        }

        void closeButtonPressed() override
        {
            initWindowProps();
            if (auto* s = windowProps.getUserSettings())
            {
                s->setValue ("windowW", getWidth());
                s->setValue ("windowH", getHeight());
                s->saveIfNeeded();
            }
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        void timerCallback() override
        {
            const float scale = juce::Desktop::getInstance().getGlobalScaleFactor();
            std::cerr << "[FlamForge] timer pass " << timerCount
                      << " scale=" << scale
                      << " requestedW=" << enforcedW << " requestedH=" << enforcedH
                      << " currentW=" << getWidth() << " currentH=" << getHeight() << "\n";
            centreWithSize (enforcedW, enforcedH);
            std::cerr << "[FlamForge] after centreWithSize: "
                      << getWidth() << "x" << getHeight() << "\n";

            if (timerCount == 0) { timerCount = 1; startTimer (500); }   // 500ms for KDE
            else                 { stopTimer(); }
        }

        void initWindowProps()
        {
            if (windowPropsReady) return;
            windowPropsReady = true;
            juce::PropertiesFile::Options opts;
            opts.applicationName     = "FlamForge";
            opts.filenameSuffix      = ".settings";
            opts.osxLibrarySubFolder = "Application Support";
            windowProps.setStorageParameters (opts);
        }

        juce::ApplicationProperties windowProps;
        bool                        windowPropsReady = false;
        int                         enforcedW = 760;
        int                         enforcedH = 714;
        int                         timerCount = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace flamforge

START_JUCE_APPLICATION (flamforge::FlamForgeApplication)
