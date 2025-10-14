#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace flam {

FlamAudioProcessorEditor::FlamAudioProcessorEditor(FlamAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
{
    std::cout << "[FLAM] FlamAudioProcessorEditor constructor called" << std::endl;
    titleLabel.setText("FLAM - Free Layered Audio Machine", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredTop);
    addAndMakeVisible(titleLabel);
    
    mixerGroup.setText("Mixer");
    addAndMakeVisible(mixerGroup);
    
    setupSlider(masterVolumeSlider, masterVolumeLabel, "Master", " dB");
    setupSlider(closeVolumeSlider, closeVolumeLabel, "Close", " dB");
    setupSlider(overheadVolumeSlider, overheadVolumeLabel, "Overhead", " dB");
    setupSlider(roomVolumeSlider, roomVolumeLabel, "Room", " dB");
    setupSlider(ambientVolumeSlider, ambientVolumeLabel, "Ambient", " dB");
    
    performanceGroup.setText("Performance");
    addAndMakeVisible(performanceGroup);
    
    setupSlider(humanizationSlider, humanizationLabel, "Humanize", "");
    setupSlider(bleedAmountSlider, bleedAmountLabel, "Mic Bleed", "");
    setupSlider(polyphonySlider, polyphonyLabel, "Polyphony", "");
    polyphonySlider.setSliderStyle(juce::Slider::IncDecButtons);
    polyphonySlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 20);
    
    roundRobinButton.setButtonText("Round Robin");
    addAndMakeVisible(roundRobinButton);
    
    loadKitButton.setButtonText("Load Kit");
    loadKitButton.onClick = [this] { loadKitButtonClicked(); };
    addAndMakeVisible(loadKitButton);
    
    kitNameLabel.setText("No kit loaded", juce::dontSendNotification);
    kitNameLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(kitNameLabel);
    
    auto& vts = audioProcessor.getValueTreeState();
    
    masterVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        vts, "master_volume", masterVolumeSlider);
    closeVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        vts, "close_volume", closeVolumeSlider);
    overheadVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        vts, "overhead_volume", overheadVolumeSlider);
    roomVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        vts, "room_volume", roomVolumeSlider);
    ambientVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        vts, "ambient_volume", ambientVolumeSlider);
    humanizationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        vts, "humanization", humanizationSlider);
    bleedAmountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        vts, "bleed_amount", bleedAmountSlider);
    polyphonyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        vts, "polyphony", polyphonySlider);
    roundRobinAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        vts, "round_robin", roundRobinButton);
    
    setSize(800, 600);
    std::cout << "[FLAM] FlamAudioProcessorEditor initialized, size set to 800x600" << std::endl;
}

FlamAudioProcessorEditor::~FlamAudioProcessorEditor() = default;

void FlamAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    
    g.setColour(juce::Colours::white);
    g.setFont(12.0f);
}

void FlamAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    auto topArea = bounds.removeFromTop(50);
    
    titleLabel.setBounds(topArea);
    
    auto contentArea = bounds.reduced(10);
    auto kitArea = contentArea.removeFromTop(40);
    
    loadKitButton.setBounds(kitArea.removeFromLeft(100));
    kitArea.removeFromLeft(10);
    kitNameLabel.setBounds(kitArea);
    
    contentArea.removeFromTop(10);
    
    auto mixerArea = contentArea.removeFromLeft(contentArea.getWidth() / 2 - 5);
    auto performanceArea = contentArea.removeFromRight(contentArea.getWidth() - 5);
    
    mixerGroup.setBounds(mixerArea);
    auto mixerContent = mixerArea.reduced(15, 25);
    
    auto sliderHeight = 60;
    masterVolumeLabel.setBounds(mixerContent.removeFromTop(20));
    masterVolumeSlider.setBounds(mixerContent.removeFromTop(sliderHeight));
    
    closeVolumeLabel.setBounds(mixerContent.removeFromTop(20));
    closeVolumeSlider.setBounds(mixerContent.removeFromTop(sliderHeight));
    
    overheadVolumeLabel.setBounds(mixerContent.removeFromTop(20));
    overheadVolumeSlider.setBounds(mixerContent.removeFromTop(sliderHeight));
    
    roomVolumeLabel.setBounds(mixerContent.removeFromTop(20));
    roomVolumeSlider.setBounds(mixerContent.removeFromTop(sliderHeight));
    
    ambientVolumeLabel.setBounds(mixerContent.removeFromTop(20));
    ambientVolumeSlider.setBounds(mixerContent.removeFromTop(sliderHeight));
    
    performanceGroup.setBounds(performanceArea);
    auto performanceContent = performanceArea.reduced(15, 25);
    
    humanizationLabel.setBounds(performanceContent.removeFromTop(20));
    humanizationSlider.setBounds(performanceContent.removeFromTop(sliderHeight));
    
    bleedAmountLabel.setBounds(performanceContent.removeFromTop(20));
    bleedAmountSlider.setBounds(performanceContent.removeFromTop(sliderHeight));
    
    polyphonyLabel.setBounds(performanceContent.removeFromTop(20));
    polyphonySlider.setBounds(performanceContent.removeFromTop(40));
    
    performanceContent.removeFromTop(20);
    roundRobinButton.setBounds(performanceContent.removeFromTop(30));
}

void FlamAudioProcessorEditor::setupSlider(juce::Slider& slider, juce::Label& label,
                                           const juce::String& labelText, const juce::String& suffix)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    slider.setTextValueSuffix(suffix);
    addAndMakeVisible(slider);
    
    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label);
}

void FlamAudioProcessorEditor::loadKitButtonClicked()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select a FlamKit file...",
        juce::File{},
        "*.flamkit;*.yaml;*.yml;*.json");
    
    chooser->launchAsync(juce::FileBrowserComponent::openMode | 
                        juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file.existsAsFile())
            {
                audioProcessor.getEngine()->loadKit(file);
                kitNameLabel.setText(file.getFileNameWithoutExtension(), 
                                   juce::dontSendNotification);
            }
        });
}

} // namespace flam