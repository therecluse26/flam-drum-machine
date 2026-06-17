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

namespace flamforge
{

class FlamForgeApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "FlamForge"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
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
