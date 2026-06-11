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

// Custom drum pad button component — animated, velocity-sensitive, type-coloured.
class DrumPad : public juce::Component, private juce::Timer
{
public:
    DrumPad(const juce::String& name, int midiNote, std::function<void(int, float)> callback)
        : padName(name), note(midiNote), onTrigger(callback)
        , typeColour(drumTypeColour(name))
    {
        startTimerHz(60);
    }

    ~DrumPad() override { stopTimer(); }

    // Thread-safe: may be called from the audio callback or the UI thread.
    void notifyHit(float velocity) noexcept
    {
        pendingHitVelocity.store(velocity, std::memory_order_release);
    }

    void paint(juce::Graphics& g) override
    {
        const auto fullBounds = getLocalBounds().toFloat().reduced(2.0f);
        const float flash = hitAlpha;

        // ── Background ──────────────────────────────────────────
        const auto idleBg  = juce::Colour(0xff1e1e1e);
        const auto flashBg = typeColour.withMultipliedBrightness(0.35f + 0.65f * lastVelocity);
        g.setColour(isPressed ? typeColour.darker(0.25f)
                              : idleBg.interpolatedWith(flashBg, flash));
        g.fillRoundedRectangle(fullBounds, 6.0f);

        if (isMouseOver && flash < 0.1f && !isPressed)
        {
            g.setColour(juce::Colours::white.withAlpha(0.04f));
            g.fillRoundedRectangle(fullBounds, 6.0f);
        }

        // ── Border — glows at hit intensity ────────────────────
        g.setColour(typeColour.withAlpha(0.30f + 0.70f * flash));
        g.drawRoundedRectangle(fullBounds, 6.0f, flash > 0.05f ? 2.0f : 1.0f);

        // ── Velocity bar (thin strip at bottom) ─────────────────
        auto textBounds = fullBounds.reduced(4.0f, 2.0f);

        if (lastVelocity > 0.0f)
        {
            auto velStrip = textBounds.removeFromBottom(4.0f);
            textBounds.removeFromBottom(2.0f); // gap

            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.fillRoundedRectangle(velStrip, 2.0f);

            g.setColour(typeColour.withMultipliedBrightness(0.7f + 0.3f * flash));
            g.fillRoundedRectangle(velStrip.withWidth(velStrip.getWidth() * lastVelocity), 2.0f);
        }

        // ── Pad name (upper 60% of text area) ──────────────────
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.90f));
        g.drawText(padName,
                   textBounds.withTrimmedBottom(textBounds.getHeight() * 0.40f),
                   juce::Justification::centred);

        // ── MIDI note + velocity hint (lower 40%) ───────────────
        juce::String subLabel = "(" + juce::String(note) + ")";
        if (flash > 0.08f)
            subLabel += "  v" + juce::String(juce::roundToInt(lastVelocity * 127.0f));
        g.setFont(juce::Font(9.0f));
        g.setColour(juce::Colours::white.withAlpha(0.40f + 0.35f * flash));
        g.drawText(subLabel,
                   textBounds.withTrimmedTop(textBounds.getHeight() * 0.60f),
                   juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        isPressed = true;
        const float velocity = juce::jmap(e.position.y, 0.0f, (float)getHeight(), 1.0f, 0.3f);
        notifyHit(velocity);
        if (onTrigger)
            onTrigger(note, velocity);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override   { isPressed  = false; repaint(); }
    void mouseEnter(const juce::MouseEvent&) override { isMouseOver = true;  repaint(); }
    void mouseExit(const juce::MouseEvent&) override  { isMouseOver = false; repaint(); }

private:
    // Infer a colour from the drum piece name for visual identity.
    static juce::Colour drumTypeColour(const juce::String& name) noexcept
    {
        const auto n = name.toLowerCase();
        if (n.contains("kick") || n.contains("bass"))  return juce::Colour(0xffC0392B); // red
        if (n.contains("snare") || n.contains("snr"))  return juce::Colour(0xff2980B9); // blue
        if (n.contains("hihat") || n.contains("hi-hat") || n.contains(" hh"))
                                                        return juce::Colour(0xff27AE60); // green
        if (n.contains("crash"))                        return juce::Colour(0xffD4AC0D); // gold
        if (n.contains("ride"))                         return juce::Colour(0xff8E44AD); // purple
        if (n.contains("tom"))                          return juce::Colour(0xffD35400); // orange
        if (n.contains("room") || n.contains("amb"))    return juce::Colour(0xff16A085); // teal
        if (n.contains("overhead") || n.contains(" oh")) return juce::Colour(0xff5D8AA8); // steel blue
        return juce::Colour(0xff5D6D7E); // default slate
    }

    void timerCallback() override
    {
        // Drain the atomic flag written by any thread (audio, UI, MIDI).
        const float pending = pendingHitVelocity.exchange(-1.0f, std::memory_order_acq_rel);
        if (pending >= 0.0f)
        {
            lastVelocity = pending;
            hitAlpha     = 1.0f;
            repaint();
            return;
        }

        // Decay animation at ~0.82 per frame → ≈150 ms to 10% at 60 Hz.
        if (hitAlpha > 0.001f)
        {
            hitAlpha *= 0.82f;
            repaint();
        }
        else if (hitAlpha > 0.0f)
        {
            hitAlpha = 0.0f;
            repaint();
        }
    }

    juce::String padName;
    int note;
    std::function<void(int, float)> onTrigger;
    juce::Colour typeColour;

    bool isPressed   = false;
    bool isMouseOver = false;

    // Written by any thread; read and reset by the UI timer.
    std::atomic<float> pendingHitVelocity { -1.0f };
    // UI-thread only — driven exclusively by timerCallback.
    float hitAlpha    = 0.0f;
    float lastVelocity = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPad)
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