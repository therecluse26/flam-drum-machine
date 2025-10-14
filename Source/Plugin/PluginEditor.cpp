#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <thread>

namespace flam {

// Custom file browser window that shows a proper dialog
class FlamAudioProcessorEditor::FileBrowserWindow : public juce::DocumentWindow,
                                                      public juce::FileBrowserListener
{
public:
    FileBrowserWindow(FlamAudioProcessorEditor* editor)
        : DocumentWindow("Select Kit File",
                        juce::Desktop::getInstance().getDefaultLookAndFeel()
                            .findColour(juce::ResizableWindow::backgroundColourId),
                        DocumentWindow::closeButton)
        , owner(editor)
        , wildcard("flamkit.yaml", "*", "FLAM Kit Files")
        , browser(juce::FileBrowserComponent::openMode |
                  juce::FileBrowserComponent::canSelectFiles |
                  juce::FileBrowserComponent::canSelectDirectories,
                  juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                  &wildcard,
                  nullptr)
    {
        browser.addListener(this);
        setContentOwned(&browser, true);
        setResizable(true, true);
        centreWithSize(600, 400);
        setVisible(true);
        setAlwaysOnTop(true);
    }

    ~FileBrowserWindow() override
    {
        browser.removeListener(this);
    }

    void closeButtonPressed() override
    {
        owner->fileBrowserWindow.reset();
    }

    void selectionChanged() override {}

    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}

    void fileDoubleClicked(const juce::File& file) override
    {
        juce::File kitFile = file;

        // If it's a directory, look for flamkit.yaml inside it
        if (file.isDirectory())
        {
            kitFile = file.getChildFile("flamkit.yaml");
        }

        // If the selected file is named flamkit.yaml, use it
        if (kitFile.existsAsFile() && kitFile.getFileName() == "flamkit.yaml")
        {
            owner->loadKitFromPath(kitFile);
            owner->fileBrowserWindow.reset();
        }
    }

    void browserRootChanged(const juce::File&) override {}

private:
    FlamAudioProcessorEditor* owner;
    juce::WildcardFileFilter wildcard;
    juce::FileBrowserComponent browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileBrowserWindow)
};

FlamAudioProcessorEditor::FlamAudioProcessorEditor(FlamAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
{
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

    drumPadsGroup.setText("Drum Pads");
    addAndMakeVisible(drumPadsGroup);

    setupDrumPads();

    setSize(1000, 700);
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

    // Split into left (drum pads) and right (controls)
    auto drumPadArea = contentArea.removeFromLeft(contentArea.getWidth() * 0.5f);
    contentArea.removeFromLeft(10);  // spacing

    // Drum pads on the left
    drumPadsGroup.setBounds(drumPadArea);
    auto padContent = drumPadArea.reduced(15, 25);

    // Layout drum pads in a 4x4 grid
    const int cols = 4;
    const int rows = 4;
    const int padSpacing = 8;
    const int padWidth = (padContent.getWidth() - (cols - 1) * padSpacing) / cols;
    const int padHeight = (padContent.getHeight() - (rows - 1) * padSpacing) / rows;

    for (int i = 0; i < drumPads.size() && i < 16; ++i)
    {
        int row = i / cols;
        int col = i % cols;
        int x = padContent.getX() + col * (padWidth + padSpacing);
        int y = padContent.getY() + row * (padHeight + padSpacing);
        drumPads[i]->setBounds(x, y, padWidth, padHeight);
    }

    // Controls on the right
    auto mixerArea = contentArea.removeFromTop(contentArea.getHeight() / 2 - 5);
    auto performanceArea = contentArea.removeFromTop(contentArea.getHeight() + 10);

    mixerGroup.setBounds(mixerArea);
    auto mixerContent = mixerArea.reduced(15, 25);

    auto sliderHeight = 50;
    masterVolumeLabel.setBounds(mixerContent.removeFromTop(18));
    masterVolumeSlider.setBounds(mixerContent.removeFromTop(sliderHeight));

    closeVolumeLabel.setBounds(mixerContent.removeFromTop(18));
    closeVolumeSlider.setBounds(mixerContent.removeFromTop(sliderHeight));

    overheadVolumeLabel.setBounds(mixerContent.removeFromTop(18));
    overheadVolumeSlider.setBounds(mixerContent.removeFromTop(sliderHeight));

    roomVolumeLabel.setBounds(mixerContent.removeFromTop(18));
    roomVolumeSlider.setBounds(mixerContent.removeFromTop(sliderHeight));

    ambientVolumeLabel.setBounds(mixerContent.removeFromTop(18));
    ambientVolumeSlider.setBounds(mixerContent.removeFromTop(sliderHeight));

    performanceGroup.setBounds(performanceArea);
    auto performanceContent = performanceArea.reduced(15, 25);

    humanizationLabel.setBounds(performanceContent.removeFromTop(18));
    humanizationSlider.setBounds(performanceContent.removeFromTop(sliderHeight));

    bleedAmountLabel.setBounds(performanceContent.removeFromTop(18));
    bleedAmountSlider.setBounds(performanceContent.removeFromTop(sliderHeight));

    polyphonyLabel.setBounds(performanceContent.removeFromTop(18));
    polyphonySlider.setBounds(performanceContent.removeFromTop(35));

    performanceContent.removeFromTop(15);
    roundRobinButton.setBounds(performanceContent.removeFromTop(25));
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

void FlamAudioProcessorEditor::setupDrumPads()
{
    // For now, create pads for the most common General MIDI drum notes
    // Later this will be dynamic based on loaded kit
    struct PadInfo {
        juce::String name;
        int note;
    };

    std::vector<PadInfo> padDefinitions = {
        {"Kick", 36},
        {"Side Stick", 37},
        {"Snare", 38},
        {"Clap", 39},
        {"Snare 2", 40},
        {"Floor Tom", 41},
        {"HH Closed", 42},
        {"Mid Tom", 45},
        {"HH Open", 46},
        {"High Tom", 48},
        {"Crash", 49},
        {"Ride", 51},
        {"Splash", 55},
        {"Cowbell", 56},
        {"Shaker", 70},
        {"Block", 76}
    };

    drumPads.clear();

    for (const auto& padDef : padDefinitions)
    {
        auto* pad = new DrumPad(padDef.name, padDef.note,
            [this](int note, float velocity) { triggerDrumPad(note, velocity); });
        drumPads.add(pad);
        addAndMakeVisible(pad);
    }
}

void FlamAudioProcessorEditor::triggerDrumPad(int midiNote, float velocity)
{
    audioProcessor.getEngine()->triggerNote(midiNote, velocity, 0);
}

void FlamAudioProcessorEditor::loadKitButtonClicked()
{
    // Don't open multiple browsers
    if (fileBrowserWindow != nullptr)
        return;

    // Create and show custom file browser window
    fileBrowserWindow = std::make_unique<FileBrowserWindow>(this);
}

void FlamAudioProcessorEditor::loadKitFromPath(const juce::File& kitFile)
{
    if (kitFile.existsAsFile() && kitFile.getFileName() == "flamkit.yaml")
    {
        // Use parent directory name as kit name
        auto kitName = kitFile.getParentDirectory().getFileName();
        kitNameLabel.setText(kitName, juce::dontSendNotification);

        // Launch kit loading on a background thread
        juce::Thread::launch([this, kitFile]()
        {
            audioProcessor.getEngine()->loadKit(kitFile);
        });
    }
    else
    {
        kitNameLabel.setText("ERROR: Invalid kit file", juce::dontSendNotification);
    }
}

} // namespace flam