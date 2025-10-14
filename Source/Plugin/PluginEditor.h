#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace flam {

struct DrumKit;

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