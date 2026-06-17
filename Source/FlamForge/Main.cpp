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
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : juce::DocumentWindow (name,
                                    juce::Colour (0xff0d0f12),
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace flamforge

START_JUCE_APPLICATION (flamforge::FlamForgeApplication)
