#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Formats/FlamKitLoader.h"
#include <thread>

namespace flam {

// Kit tile component that displays kit information
class KitTileComponent : public juce::Component
{
public:
    KitTileComponent(const juce::File& kitFile, std::function<void(juce::File)> onSelect)
        : file(kitFile), selectCallback(onSelect)
    {
        // Load kit metadata
        auto loader = std::make_unique<FlamKitLoader>();
        kit = loader->loadKit(kitFile);

        // Load cover image if available
        if (kit && kit->coverImageFile.existsAsFile())
        {
            coverImage = juce::ImageFileFormat::loadFrom(kit->coverImageFile);
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(8.0f);

        // Draw background
        if (isMouseOver)
            g.setColour(juce::Colour(0xff3a3a3a));
        else
            g.setColour(juce::Colour(0xff2a2a2a));
        g.fillRoundedRectangle(bounds, 6.0f);

        // Draw border
        g.setColour(isMouseOver ? juce::Colours::lightblue : juce::Colours::grey);
        g.drawRoundedRectangle(bounds, 6.0f, 2.0f);

        auto contentBounds = bounds.reduced(8.0f);

        // Draw cover image
        auto imageBounds = contentBounds.removeFromTop(contentBounds.getHeight() * 0.5f);
        if (coverImage.isValid())
        {
            g.drawImage(coverImage, imageBounds, juce::RectanglePlacement::centred);
        }
        else
        {
            // Placeholder if no image
            g.setColour(juce::Colour(0xff1a1a1a));
            g.fillRect(imageBounds);
            g.setColour(juce::Colours::grey);
            g.setFont(24.0f);
            g.drawText("?", imageBounds, juce::Justification::centred);
        }

        contentBounds.removeFromTop(8.0f);

        // Draw kit name
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        auto nameArea = contentBounds.removeFromTop(18.0f);
        g.drawText(kit ? kit->name : "Unknown Kit", nameArea, juce::Justification::centredLeft);

        // Draw author
        g.setColour(juce::Colours::lightgrey);
        g.setFont(11.0f);
        auto authorArea = contentBounds.removeFromTop(14.0f);
        g.drawText(kit && kit->author.isNotEmpty() ? kit->author : "",
                   authorArea, juce::Justification::centredLeft);

        // Draw version
        auto versionArea = contentBounds.removeFromTop(14.0f);
        g.drawText(kit && kit->version.isNotEmpty() ? ("v" + kit->version) : "",
                   versionArea, juce::Justification::centredLeft);

        contentBounds.removeFromTop(4.0f);

        // Draw metadata (drums and samples count)
        g.setColour(juce::Colours::grey);
        g.setFont(10.0f);
        if (kit)
        {
            auto statsArea = contentBounds.removeFromTop(12.0f);
            juce::String stats = juce::String(kit->getDrumPieceCount()) + " drums, " +
                                juce::String(kit->getTotalSampleCount()) + " samples";
            g.drawText(stats, statsArea, juce::Justification::centredLeft);
        }
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

    void mouseUp(const juce::MouseEvent&) override
    {
        if (selectCallback)
            selectCallback(file);
    }

private:
    juce::File file;
    std::function<void(juce::File)> selectCallback;
    std::unique_ptr<DrumKit> kit;
    juce::Image coverImage;
    bool isMouseOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitTileComponent)
};

// Kit browser window showing grid of kits
class FlamAudioProcessorEditor::KitBrowserWindow : public juce::DocumentWindow
{
public:
    KitBrowserWindow(FlamAudioProcessorEditor* editor, const std::vector<juce::File>& kits)
        : DocumentWindow("Select Drum Kit",
                        juce::Desktop::getInstance().getDefaultLookAndFeel()
                            .findColour(juce::ResizableWindow::backgroundColourId),
                        DocumentWindow::closeButton)
        , owner(editor)
    {
        viewport = std::make_unique<juce::Viewport>();
        content = std::make_unique<juce::Component>();

        // Create kit tiles
        const int tileWidth = 200;
        const int tileHeight = 280;
        const int spacing = 16;
        const int cols = 3;

        int row = 0;
        int col = 0;

        for (const auto& kitFile : kits)
        {
            auto* tile = new KitTileComponent(kitFile, [this](juce::File selectedFile) {
                owner->loadKitFromPath(selectedFile);
                owner->kitBrowserWindow.reset();
            });

            int x = col * (tileWidth + spacing) + spacing;
            int y = row * (tileHeight + spacing) + spacing;
            tile->setBounds(x, y, tileWidth, tileHeight);

            content->addAndMakeVisible(tile);
            tiles.add(tile);

            col++;
            if (col >= cols)
            {
                col = 0;
                row++;
            }
        }

        // Set content size
        int rows = (int)std::ceil((float)kits.size() / cols);
        int contentHeight = rows * (tileHeight + spacing) + spacing;
        int contentWidth = cols * (tileWidth + spacing) + spacing;
        content->setSize(contentWidth, contentHeight);

        viewport->setViewedComponent(content.get(), false);
        viewport->setScrollBarsShown(true, false);

        setContentNonOwned(viewport.get(), true);
        setResizable(true, true);
        centreWithSize(cols * (tileWidth + spacing) + spacing + 20, 600);
        setVisible(true);
        setAlwaysOnTop(true);
    }

    ~KitBrowserWindow() override
    {
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        owner->kitBrowserWindow.reset();
    }

private:
    FlamAudioProcessorEditor* owner;
    std::unique_ptr<juce::Viewport> viewport;
    std::unique_ptr<juce::Component> content;
    juce::OwnedArray<KitTileComponent> tiles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitBrowserWindow)
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

    kitBrowserLabel.setText("Kit:", juce::dontSendNotification);
    kitBrowserLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(kitBrowserLabel);

    currentKitLabel.setText("No kit loaded", juce::dontSendNotification);
    currentKitLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(currentKitLabel);

    kitBrowserButton.setButtonText("Browse...");
    kitBrowserButton.onClick = [this] { openKitBrowser(); };
    addAndMakeVisible(kitBrowserButton);
    
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

    // Scan for available kits and load the last used one
    scanForKits();
    loadLastLoadedKit();
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

    kitBrowserLabel.setBounds(kitArea.removeFromLeft(40));
    kitArea.removeFromLeft(5);
    kitBrowserButton.setBounds(kitArea.removeFromRight(100));
    kitArea.removeFromRight(5);
    currentKitLabel.setBounds(kitArea);

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

void FlamAudioProcessorEditor::scanForKits()
{
    availableKits.clear();

    // Scan Resources/Kits directory for kits
    auto kitsDir = juce::File::getCurrentWorkingDirectory().getChildFile("Resources/Kits");

    if (!kitsDir.isDirectory())
    {
        // Try relative to executable
        kitsDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory()
            .getChildFile("Resources/Kits");
    }

    if (kitsDir.isDirectory())
    {
        for (auto& dir : kitsDir.findChildFiles(juce::File::findDirectories, false))
        {
            auto flamkitFile = dir.getChildFile("flamkit.yaml");
            if (flamkitFile.existsAsFile())
            {
                availableKits.push_back(flamkitFile);
            }
        }
    }
}

void FlamAudioProcessorEditor::openKitBrowser()
{
    // Don't open multiple browsers
    if (kitBrowserWindow != nullptr)
        return;

    // Create and show kit browser window
    kitBrowserWindow = std::make_unique<KitBrowserWindow>(this, availableKits);
}

void FlamAudioProcessorEditor::loadKitFromPath(const juce::File& kitFile)
{
    if (kitFile.existsAsFile() && kitFile.getFileName() == "flamkit.yaml")
    {
        // Store current kit file
        currentKitFile = kitFile;

        // Update UI label - load kit metadata to get name
        auto loader = std::make_unique<FlamKitLoader>();
        auto kit = loader->loadKit(kitFile);
        if (kit)
        {
            currentKitLabel.setText(kit->name, juce::dontSendNotification);
        }
        else
        {
            currentKitLabel.setText(kitFile.getParentDirectory().getFileName(), juce::dontSendNotification);
        }

        // Launch kit loading on a background thread
        juce::Thread::launch([this, kitFile]()
        {
            audioProcessor.getEngine()->loadKit(kitFile);
        });

        // Save as last loaded kit
        saveLastLoadedKit(kitFile);
    }
}

void FlamAudioProcessorEditor::saveLastLoadedKit(const juce::File& kitFile)
{
    auto options = juce::PropertiesFile::Options();
    options.applicationName = "FLAM";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";

    juce::PropertiesFile props(options);
    props.setValue("lastLoadedKit", kitFile.getFullPathName());
    props.save();
}

void FlamAudioProcessorEditor::loadLastLoadedKit()
{
    auto options = juce::PropertiesFile::Options();
    options.applicationName = "FLAM";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";

    juce::PropertiesFile props(options);
    auto lastKitPath = props.getValue("lastLoadedKit");

    if (lastKitPath.isNotEmpty())
    {
        juce::File lastKit(lastKitPath);

        // Find this kit in the available kits list
        for (const auto& kitFile : availableKits)
        {
            if (kitFile == lastKit)
            {
                loadKitFromPath(lastKit);
                return;
            }
        }
    }

    // If no last kit or not found, load the first available kit
    if (!availableKits.empty())
    {
        loadKitFromPath(availableKits[0]);
    }
}

} // namespace flam