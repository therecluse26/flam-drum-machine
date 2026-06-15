// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

// Custom JUCE standalone application for FLAM.
//
// Why this file exists: JUCE's stock standalone wrapper leaves the audio buffer size at whatever
// the device offers (commonly 512 frames ≈ 10.7 ms @ 48 kHz), which is audible as latency when
// triggering drums live. JUCE's StandaloneFilterApp is declared `final`, so it can't be
// subclassed — instead we set JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1 (see CMakeLists.txt), which
// tells the wrapper to call OUR juce_CreateApplication() instead of defining its own app. The
// wrapper still supplies main() (JUCE_MAIN_FUNCTION_DEFINITION).
//
// This is a faithful copy of juce::StandaloneFilterApp with exactly one change: createPluginHolder()
// hands the device a preferred buffer size (see kDefaultBufferSize) instead of nullptr.

#include <juce_core/system/juce_TargetPlatform.h>

#if JucePlugin_Build_Standalone

// Mirror the include sequence of JUCE's own juce_audio_plugin_client_Standalone.cpp so this
// custom app sees exactly the same module/define setup the stock StandaloneFilterApp would.
#include <juce_audio_plugin_client/detail/juce_CheckSettingMacros.h>
#include <juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h>
#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>
#include <juce_gui_basics/native/juce_WindowsHooks_windows.h>
#include <juce_audio_plugin_client/detail/juce_PluginUtilities.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace flam {

// Preferred audio buffer size (frames), requested on FIRST launch only. The device clamps this to
// its nearest supported size. Once the user changes the buffer in Options → Audio/MIDI Settings,
// their saved DEVICESETUP XML overrides this on every later launch (JUCE merges the saved XML over
// preferredSetupOptions in AudioDeviceManager::initialiseFromXML), so this is a sane default, NOT a
// lock-in. 128 frames ≈ 2.7 ms @ 48 kHz — imperceptible, with low xrun risk.
constexpr int kDefaultBufferSize = 128;

class FlamStandaloneApp : public juce::JUCEApplication
{
public:
    FlamStandaloneApp()
    {
        juce::PropertiesFile::Options options;

        options.applicationName     = juce::CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override           { return juce::CharPointer_UTF8 (JucePlugin_Name); }
    const juce::String getApplicationVersion() override        { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override                 { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    virtual juce::StandaloneFilterWindow* createWindow()
    {
        if (juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            // No displays are available, so no window will be created!
            jassertfalse;
            return nullptr;
        }

        return new juce::StandaloneFilterWindow (
            getApplicationName(),
            juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
            createPluginHolder());
    }

    virtual std::unique_ptr<juce::StandalonePluginHolder> createPluginHolder()
    {
        constexpr bool autoOpenMidiDevices = false;  // desktop targets (Linux/macOS/Windows)

       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr juce::StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig (channels, juce::numElementsInArray (channels));
       #else
        const juce::Array<juce::StandalonePluginHolder::PluginInOuts> channelConfig;
       #endif

        // The ONLY deviation from JUCE's stock app: request a low default buffer. Held static so
        // the pointer outlives this call (StandalonePluginHolder copies it during construction).
        static const juce::AudioDeviceManager::AudioDeviceSetup preferredSetup = []
        {
            juce::AudioDeviceManager::AudioDeviceSetup s;  // device/sampleRate left at defaults
            s.bufferSize = kDefaultBufferSize;
            return s;
        }();

        return std::make_unique<juce::StandalonePluginHolder> (
            appProperties.getUserSettings(),
            false,
            juce::String{},
            &preferredSetup,          // was nullptr in juce::StandaloneFilterApp
            channelConfig,
            autoOpenMidiDevices);
    }

    void initialise (const juce::String&) override
    {
        mainWindow = juce::rawToUniquePtr (createWindow());

        if (mainWindow != nullptr)
        {
           #if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
            juce::Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
           #endif

            mainWindow->setVisible (true);
        }
        else
        {
            pluginHolder = createPluginHolder();
        }
    }

    void shutdown() override
    {
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();

        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []()
            {
                if (auto app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

protected:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> mainWindow;

private:
    std::unique_ptr<juce::StandalonePluginHolder> pluginHolder;
};

} // namespace flam

// The factory the JUCE standalone wrapper declares `extern` and calls from its main() when
// JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP is set.
juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new flam::FlamStandaloneApp(); }

#endif // JucePlugin_Build_Standalone
