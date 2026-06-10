// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "../UI/LevelMeter.h"
#include "../UI/MixerPanel.h"

namespace flam {

struct DrumKit;

// Simple background panel component for hiding group component borders
class BackgroundPanel : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        g.setColour(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
        g.fillAll();
    }
};

// Custom compressor meter showing output level from bottom and GR from top
class CompressorMeter : public juce::Component, private juce::Timer
{
public:
    CompressorMeter()
    {
        startTimerHz(30);
    }

    ~CompressorMeter() override
    {
        stopTimer();
    }

    void setOutputLevel(float level)
    {
        outputLevel.store(level, std::memory_order_relaxed);
    }

    void setGainReduction(float gr)
    {
        gainReduction.store(gr, std::memory_order_relaxed);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // Background
        g.setColour(juce::Colour(0xff1a1a1a));
        g.fillRoundedRectangle(bounds, 2.0f);

        // Border
        g.setColour(juce::Colours::darkgrey);
        g.drawRoundedRectangle(bounds, 2.0f, 1.0f);

        auto meterBounds = bounds.reduced(2.0f);

        // Draw output level from bottom (green)
        if (displayOutputLevel > 0.0f)
        {
            const float outputDB = juce::Decibels::gainToDecibels(displayOutputLevel);
            const float normalizedOutput = juce::jmap(outputDB, -60.0f, 6.0f, 0.0f, 1.0f);
            const float outputHeight = meterBounds.getHeight() * juce::jlimit(0.0f, 1.0f, normalizedOutput);

            g.setColour(juce::Colours::green);
            auto outputRect = meterBounds.removeFromBottom(outputHeight);
            g.fillRect(outputRect);
        }

        // Draw gain reduction from top (red) - only if GR is active
        if (displayGainReduction > 0.1f)
        {
            const float normalizedGR = juce::jmap(displayGainReduction, 0.0f, 20.0f, 0.0f, 1.0f);
            const float grHeight = meterBounds.getHeight() * juce::jlimit(0.0f, 1.0f, normalizedGR);

            g.setColour(juce::Colours::red.withAlpha(0.8f));
            auto grRect = meterBounds.removeFromTop(grHeight);
            g.fillRect(grRect);
        }

        // Draw labels inside the meter
        g.setFont(9.0f);

        // Draw "Out" label at bottom
        g.setColour(juce::Colours::white);
        auto bottomLabelArea = bounds.removeFromBottom(12).reduced(2.0f, 0.0f);
        g.drawText("Out", bottomLabelArea, juce::Justification::centred);

        // Draw "GR" label at top
        auto topLabelArea = bounds.removeFromTop(12).reduced(2.0f, 0.0f);
        g.drawText("GR", topLabelArea, juce::Justification::centred);

        // Draw GR value in middle if there's gain reduction
        if (displayGainReduction > 0.1f)
        {
            g.setFont(10.0f);
            auto grValueArea = juce::Rectangle<float>(
                bounds.getX(),
                bounds.getCentreY() - 10.0f,
                bounds.getWidth(),
                20.0f
            );

            // Draw background for better readability
            g.setColour(juce::Colour(0xff1a1a1a).withAlpha(0.8f));
            g.fillRoundedRectangle(grValueArea.reduced(2.0f), 2.0f);

            // Draw GR value
            g.setColour(juce::Colours::white);
            juce::String grText = juce::String(displayGainReduction, 1) + " dB";
            g.drawText(grText, grValueArea, juce::Justification::centred);
        }
    }

private:
    void timerCallback() override
    {
        const float newOutput = outputLevel.load(std::memory_order_relaxed);
        const float newGR = gainReduction.load(std::memory_order_relaxed);

        constexpr float decayRate = 0.95f;

        // Output level with decay for visual smoothness
        if (newOutput > displayOutputLevel)
            displayOutputLevel = newOutput;
        else
            displayOutputLevel *= decayRate;

        // GR directly reflects actual compressor behavior - no artificial decay
        displayGainReduction = newGR;

        repaint();
    }

    std::atomic<float> outputLevel{0.0f};
    std::atomic<float> gainReduction{0.0f};
    float displayOutputLevel{0.0f};
    float displayGainReduction{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompressorMeter)
};

// Custom drum pad button component
class DrumPad : public juce::Component
{
public:
    DrumPad(const juce::String& name, int midiNote, std::function<void(int, float)> callback)
        : padName(name), note(midiNote), onTrigger(callback)
    {
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(2.0f);

        // Draw pad background
        if (isMouseOver || isPressed)
            g.setColour(juce::Colours::lightblue);
        else
            g.setColour(juce::Colour(0xff2a2a2a));

        g.fillRoundedRectangle(bounds, 4.0f);

        // Draw border
        g.setColour(isPressed ? juce::Colours::white : juce::Colours::grey);
        g.drawRoundedRectangle(bounds, 4.0f, 2.0f);

        // Draw label
        g.setColour(juce::Colours::white);
        g.setFont(12.0f);
        g.drawText(padName, bounds, juce::Justification::centred);

        // Draw MIDI note number at bottom
        g.setFont(9.0f);
        g.setColour(juce::Colours::grey);
        auto noteText = "(" + juce::String(note) + ")";
        g.drawText(noteText, bounds.removeFromBottom(12), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        isPressed = true;
        // Trigger with velocity based on mouse position
        float velocity = juce::jmap(e.position.y, 0.0f, (float)getHeight(), 1.0f, 0.3f);
        if (onTrigger)
            onTrigger(note, velocity);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        isPressed = false;
        repaint();
    }

    void mouseEnter(const juce::MouseEvent&) override
    {
        isMouseOver = true;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        isMouseOver = false;
        repaint();
    }

private:
    juce::String padName;
    int note;
    std::function<void(int, float)> onTrigger;
    bool isPressed = false;
    bool isMouseOver = false;
};

class FlamAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    FlamAudioProcessorEditor(FlamAudioProcessor&);
    ~FlamAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Forward declaration for friend
    friend class FileBrowserWindow;

private:
    FlamAudioProcessor& audioProcessor;

    juce::Label titleLabel;
    juce::TextButton kitBrowserButton;
    juce::Label kitBrowserLabel;
    juce::Label currentKitLabel;

    juce::GroupComponent drumPadsGroup;
    juce::OwnedArray<DrumPad> drumPads;

    // Main tab component (drum pads + master mixer)
    class MainTabComponent;
    std::unique_ptr<MainTabComponent> mainTab;

    // Mixer panels - use tabs to switch between them
    std::unique_ptr<juce::TabbedComponent> mixerTabs;
    std::unique_ptr<MixerPanel> perChannelMixerPanel;  // Mixer with master channel

    void setupDrumPads();
    void triggerDrumPad(int midiNote, float velocity);

    // Kit management
    void scanForKits();
    void loadKitFromPath(const juce::File& kitFile);
    void openKitBrowser();
    void openFileBrowser();
    void removeKitFromLibrary(const juce::File& kitFile);
    void handleKitRemoval(const juce::File& kitFile);
    void saveLastLoadedKit(const juce::File& kitFile);
    void loadLastLoadedKit();

    // Kit browser window
    class KitBrowserWindow;
    std::unique_ptr<KitBrowserWindow> kitBrowserWindow;

    // File browser window (for adding external kits)
    std::unique_ptr<juce::Component> fileBrowserWindow;

    // Kit library
    std::vector<juce::File> availableKits;
    juce::File currentKitFile;
    std::unique_ptr<DrumKit> currentKit;

    void addKitToLibrary(const juce::File& kitFile);
    void saveKitList();
    void updateDrumPadsFromKit();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlamAudioProcessorEditor)
};

} // namespace flam