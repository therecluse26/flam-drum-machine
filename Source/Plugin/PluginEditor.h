#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace flam {

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

private:
    FlamAudioProcessor& audioProcessor;
    
    juce::Label titleLabel;
    
    juce::GroupComponent mixerGroup;
    juce::Slider masterVolumeSlider;
    juce::Label masterVolumeLabel;
    juce::Slider closeVolumeSlider;
    juce::Label closeVolumeLabel;
    juce::Slider overheadVolumeSlider;
    juce::Label overheadVolumeLabel;
    juce::Slider roomVolumeSlider;
    juce::Label roomVolumeLabel;
    juce::Slider ambientVolumeSlider;
    juce::Label ambientVolumeLabel;
    
    juce::GroupComponent performanceGroup;
    juce::Slider humanizationSlider;
    juce::Label humanizationLabel;
    juce::Slider bleedAmountSlider;
    juce::Label bleedAmountLabel;
    juce::Slider polyphonySlider;
    juce::Label polyphonyLabel;
    juce::ToggleButton roundRobinButton;
    
    juce::TextButton kitBrowserButton;
    juce::Label kitBrowserLabel;
    juce::Label currentKitLabel;

    juce::GroupComponent drumPadsGroup;
    juce::OwnedArray<DrumPad> drumPads;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> closeVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> overheadVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> roomVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ambientVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humanizationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bleedAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> polyphonyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> roundRobinAttachment;
    
    void setupSlider(juce::Slider& slider, juce::Label& label,
                     const juce::String& labelText, const juce::String& suffix = "");
    void setupDrumPads();
    void triggerDrumPad(int midiNote, float velocity);

    // Kit management
    void scanForKits();
    void loadKitFromPath(const juce::File& kitFile);
    void openKitBrowser();
    void saveLastLoadedKit(const juce::File& kitFile);
    void loadLastLoadedKit();

    // Kit browser window
    class KitBrowserWindow;
    std::unique_ptr<KitBrowserWindow> kitBrowserWindow;

    // Kit library
    std::vector<juce::File> availableKits;
    juce::File currentKitFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlamAudioProcessorEditor)
};

} // namespace flam