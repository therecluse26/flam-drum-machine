#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace flam {

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
    
    juce::TextButton loadKitButton;
    juce::Label kitNameLabel;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> closeVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> overheadVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> roomVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ambientVolumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humanizationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bleedAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> polyphonyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> roundRobinAttachment;
    
    void loadKitButtonClicked();
    void setupSlider(juce::Slider& slider, juce::Label& label, 
                     const juce::String& labelText, const juce::String& suffix = "");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlamAudioProcessorEditor)
};

} // namespace flam