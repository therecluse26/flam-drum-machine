#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Formats/FlamKitLoader.h"
#include "../DSP/SimpleCompressor.h"
#include <thread>
#include <cmath>

namespace flam {

// MixerPanel implementation
MixerPanel::MixerPanel(juce::AudioProcessorValueTreeState& vts, FlamAudioProcessor& proc)
    : valueTreeState(vts)
    , processor(proc)
{
    // Level meters
    inputMeterLabel.setText("In", juce::dontSendNotification);
    inputMeterLabel.setJustificationType(juce::Justification::centred);
    inputMeterLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    addAndMakeVisible(inputMeterLabel);
    addAndMakeVisible(inputMeter);

    outputMeterLabel.setText("Out", juce::dontSendNotification);
    outputMeterLabel.setJustificationType(juce::Justification::centred);
    outputMeterLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
    addAndMakeVisible(outputMeterLabel);
    addAndMakeVisible(outputMeter);

    // Start timer to update meters from audio thread
    startTimerHz(30);

    // Input gain
    inputGainLabel.setText("Input Gain", juce::dontSendNotification);
    inputGainLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(inputGainLabel);

    inputGainSlider.setSliderStyle(juce::Slider::LinearVertical);
    inputGainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    inputGainSlider.setTextValueSuffix(" dB");
    addAndMakeVisible(inputGainSlider);
    inputGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        valueTreeState, "input_gain", inputGainSlider);

    // Master volume
    masterVolumeLabel.setText("Master", juce::dontSendNotification);
    masterVolumeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(masterVolumeLabel);

    masterVolumeSlider.setSliderStyle(juce::Slider::LinearVertical);
    masterVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    masterVolumeSlider.setTextValueSuffix(" dB");
    addAndMakeVisible(masterVolumeSlider);
    masterVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        valueTreeState, "master_volume", masterVolumeSlider);

    // Create viewport and content for scrollable FX section
    fxViewport = std::make_unique<juce::Viewport>();
    fxContent = std::make_unique<juce::Component>();
    addAndMakeVisible(fxViewport.get());
    fxViewport->setViewedComponent(fxContent.get(), false);
    fxViewport->setScrollBarsShown(true, false);

    // EQ group
    eqGroup.setText("10-Band Equalizer");
    fxContent->addAndMakeVisible(eqGroup);

    // EQ enable toggle background and button
    fxContent->addAndMakeVisible(eqButtonBackground);
    eqBypassButton.setButtonText("Enable");
    eqBypassButton.setClickingTogglesState(true);
    fxContent->addAndMakeVisible(eqBypassButton);
    eqBypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        valueTreeState, "eq_enabled", eqBypassButton);

    // Create EQ band sliders
    setupEQBand(eq31HzSlider, eq31HzLabel, eq31HzAttachment, "31Hz", "eq_31hz");
    setupEQBand(eq62HzSlider, eq62HzLabel, eq62HzAttachment, "62Hz", "eq_62hz");
    setupEQBand(eq125HzSlider, eq125HzLabel, eq125HzAttachment, "125Hz", "eq_125hz");
    setupEQBand(eq250HzSlider, eq250HzLabel, eq250HzAttachment, "250Hz", "eq_250hz");
    setupEQBand(eq500HzSlider, eq500HzLabel, eq500HzAttachment, "500Hz", "eq_500hz");
    setupEQBand(eq1kHzSlider, eq1kHzLabel, eq1kHzAttachment, "1kHz", "eq_1khz");
    setupEQBand(eq2kHzSlider, eq2kHzLabel, eq2kHzAttachment, "2kHz", "eq_2khz");
    setupEQBand(eq4kHzSlider, eq4kHzLabel, eq4kHzAttachment, "4kHz", "eq_4khz");
    setupEQBand(eq8kHzSlider, eq8kHzLabel, eq8kHzAttachment, "8kHz", "eq_8khz");
    setupEQBand(eq16kHzSlider, eq16kHzLabel, eq16kHzAttachment, "16kHz", "eq_16khz");

    // Compressor group
    compressorGroup.setText("Compressor");
    fxContent->addAndMakeVisible(compressorGroup);

    // Compressor enable toggle background and button
    fxContent->addAndMakeVisible(compButtonBackground);
    compBypassButton.setButtonText("Enable");
    compBypassButton.setClickingTogglesState(true);
    fxContent->addAndMakeVisible(compBypassButton);
    compBypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        valueTreeState, "comp_enabled", compBypassButton);

    // Compressor controls
    setupCompControl(compAttackSlider, compAttackLabel, compAttackAttachment,
                    "Attack", "comp_attack", juce::Slider::Rotary);
    setupCompControl(compReleaseSlider, compReleaseLabel, compReleaseAttachment,
                    "Release", "comp_release", juce::Slider::Rotary);
    setupCompControl(compHoldSlider, compHoldLabel, compHoldAttachment,
                    "Hold", "comp_hold", juce::Slider::Rotary);
    setupCompControl(compThresholdSlider, compThresholdLabel, compThresholdAttachment,
                    "Threshold", "comp_threshold", juce::Slider::Rotary);
    setupCompControl(compRatioSlider, compRatioLabel, compRatioAttachment,
                    "Ratio", "comp_ratio", juce::Slider::Rotary);
    setupCompControl(compLookaheadSlider, compLookaheadLabel, compLookaheadAttachment,
                    "Lookahead", "comp_lookahead", juce::Slider::Rotary);
    setupCompControl(compMakeupGainSlider, compMakeupGainLabel, compMakeupGainAttachment,
                    "Makeup", "comp_makeup_gain", juce::Slider::LinearHorizontal);

    // Compressor meter with built-in labels
    fxContent->addAndMakeVisible(compMeter);
}

MixerPanel::~MixerPanel()
{
    stopTimer();
}

void MixerPanel::paint(juce::Graphics& g)
{
    // Paint background for button backgrounds to match main background
    g.setColour(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.fillAll();
}

void MixerPanel::timerCallback()
{
    // Update level meters from audio engine
    if (auto* engine = processor.getEngine())
    {
        inputMeter.setLevel(engine->getInputLevel());
        outputMeter.setLevel(engine->getOutputLevel());

        // Update compressor meter
        if (auto* compressor = engine->getCompressor())
        {
            compMeter.setOutputLevel(compressor->getOutputLevel());

            // Gain reduction is negative (dB), so display absolute value
            float gr = std::abs(compressor->getGainReduction());
            compMeter.setGainReduction(gr);
        }
    }
}

void MixerPanel::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    const int panelWidth = bounds.getWidth();

    // Gain controls and meters at top (fixed height)
    auto gainArea = bounds.removeFromTop(120);

    // Level meters on the left (narrow strip)
    const int meterWidth = 30;
    auto meterArea = gainArea.removeFromLeft(meterWidth * 2 + 5);

    auto inputMeterArea = meterArea.removeFromLeft(meterWidth);
    inputMeterLabel.setBounds(inputMeterArea.removeFromTop(16));
    inputMeter.setBounds(inputMeterArea);

    meterArea.removeFromLeft(5); // spacing

    auto outputMeterArea = meterArea.removeFromLeft(meterWidth);
    outputMeterLabel.setBounds(outputMeterArea.removeFromTop(16));
    outputMeter.setBounds(outputMeterArea);

    gainArea.removeFromLeft(5); // spacing

    // Gain sliders on the right
    const int gainWidth = gainArea.getWidth() / 2;

    auto inputGainArea = gainArea.removeFromLeft(gainWidth);
    inputGainLabel.setBounds(inputGainArea.removeFromTop(20));
    inputGainSlider.setBounds(inputGainArea.reduced(5));

    auto masterGainArea = gainArea;
    masterVolumeLabel.setBounds(masterGainArea.removeFromTop(20));
    masterVolumeSlider.setBounds(masterGainArea.reduced(5));

    bounds.removeFromTop(10);

    // FX section with viewport for scrolling
    fxViewport->setBounds(bounds);

    // Layout FX content with fixed heights
    const int eqFixedHeight = 200;
    const int compressorFixedHeight = 320;
    const int spacing = 10;
    const int totalContentHeight = eqFixedHeight + spacing + compressorFixedHeight;

    // Set FX content size
    fxContent->setSize(panelWidth - 20, totalContentHeight);

    auto fxBounds = juce::Rectangle<int>(0, 0, panelWidth - 20, totalContentHeight);

    // EQ section (fixed height)
    auto eqArea = fxBounds.removeFromTop(eqFixedHeight);
    eqGroup.setBounds(eqArea);

    // Position background and enable button on the title line
    auto eqHeader = eqArea.removeFromTop(25);
    auto eqButtonArea = eqHeader.removeFromRight(70).reduced(5, 3);
    eqButtonBackground.setBounds(eqButtonArea);
    eqBypassButton.setBounds(eqButtonArea);

    auto eqContent = eqArea.reduced(8, 2);

    // Single row layout for EQ bands
    const int numBands = 10;
    juce::Slider* eqSliders[] = {&eq31HzSlider, &eq62HzSlider, &eq125HzSlider,
                                  &eq250HzSlider, &eq500HzSlider, &eq1kHzSlider,
                                  &eq2kHzSlider, &eq4kHzSlider, &eq8kHzSlider, &eq16kHzSlider};
    juce::Label* eqLabels[] = {&eq31HzLabel, &eq62HzLabel, &eq125HzLabel,
                                &eq250HzLabel, &eq500HzLabel, &eq1kHzLabel,
                                &eq2kHzLabel, &eq4kHzLabel, &eq8kHzLabel, &eq16kHzLabel};

    const int bandWidth = eqContent.getWidth() / numBands;
    for (int i = 0; i < numBands; ++i)
    {
        auto bandArea = eqContent.removeFromLeft(bandWidth);
        eqLabels[i]->setBounds(bandArea.removeFromBottom(18));
        eqSliders[i]->setBounds(bandArea.reduced(1));
    }

    fxBounds.removeFromTop(spacing);

    // Compressor section (fixed height)
    auto compArea = fxBounds.removeFromTop(compressorFixedHeight);
    compressorGroup.setBounds(compArea);

    // Position background and enable button on the title line
    auto compHeader = compArea.removeFromTop(25);
    auto compButtonArea = compHeader.removeFromRight(70).reduced(5, 3);
    compButtonBackground.setBounds(compButtonArea);
    compBypassButton.setBounds(compButtonArea);

    auto compContent = compArea.reduced(8, 2);

    // Meter on the left side (with built-in labels)
    const int compMeterWidth = 40;
    auto compMeterArea = compContent.removeFromLeft(compMeterWidth);
    compMeter.setBounds(compMeterArea);

    compContent.removeFromLeft(5); // spacing

    // Reserve space for the makeup gain slider at the bottom (full-width)
    const int makeupGainHeight = 50;
    auto makeupArea = compContent.removeFromBottom(makeupGainHeight);
    compContent.removeFromBottom(5); // spacing between grid and makeup slider

    // Layout compressor controls in 2x3 grid (without makeup gain)
    juce::Slider* compSliders[] = {&compAttackSlider, &compReleaseSlider, &compHoldSlider,
                                    &compThresholdSlider, &compRatioSlider, &compLookaheadSlider};
    juce::Label* compLabels[] = {&compAttackLabel, &compReleaseLabel, &compHoldLabel,
                                  &compThresholdLabel, &compRatioLabel, &compLookaheadLabel};

    const int compCols = 3;
    const int compRows = 2;
    const int compWidth = compContent.getWidth() / compCols;
    const int compHeight = compContent.getHeight() / compRows;

    for (int i = 0; i < 6; ++i)
    {
        int row = i / compCols;
        int col = i % compCols;

        int x = compContent.getX() + (col * compWidth);
        int y = compContent.getY() + (row * compHeight);
        auto controlArea = juce::Rectangle<int>(x, y, compWidth, compHeight).reduced(3);

        compLabels[i]->setBounds(controlArea.removeFromTop(18));
        compSliders[i]->setBounds(controlArea.reduced(2));
    }

    // Layout makeup gain slider full-width at the bottom
    compMakeupGainLabel.setBounds(makeupArea.removeFromTop(18));
    compMakeupGainSlider.setBounds(makeupArea.reduced(5, 0));
}

void MixerPanel::setupEQBand(juce::Slider& slider, juce::Label& label,
                             std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
                             const juce::String& labelText, const juce::String& paramID)
{
    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(juce::FontOptions(10.0f)));
    fxContent->addAndMakeVisible(label);

    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fxContent->addAndMakeVisible(slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        valueTreeState, paramID, slider);
}

void MixerPanel::setupCompControl(juce::Slider& slider, juce::Label& label,
                                  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
                                  const juce::String& labelText, const juce::String& paramID,
                                  juce::Slider::SliderStyle style)
{
    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(juce::FontOptions(11.0f)));
    fxContent->addAndMakeVisible(label);

    slider.setSliderStyle(style);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
    fxContent->addAndMakeVisible(slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        valueTreeState, paramID, slider);
}

// Kit tile component that displays kit information
class KitTileComponent : public juce::Component
{
public:
    KitTileComponent(const juce::File& kitFile, std::function<void(juce::File)> onSelect, std::function<void(juce::File)> onDelete)
        : file(kitFile), selectCallback(onSelect), deleteCallback(onDelete)
    {
        // Load kit metadata
        auto loader = std::make_unique<FlamKitLoader>();
        kit = loader->loadKit(kitFile);

        // Load cover image if available
        if (kit && kit->coverImageFile.existsAsFile())
        {
            coverImage = juce::ImageFileFormat::loadFrom(kit->coverImageFile);
        }

        // Create delete button
        deleteButton = std::make_unique<juce::TextButton>("X");
        deleteButton->setSize(24, 24);
        deleteButton->onClick = [this]() {
            if (deleteCallback)
                deleteCallback(file);
        };
        deleteButton->setAlpha(0.0f); // Hidden by default
        deleteButton->setMouseCursor(juce::MouseCursor::PointingHandCursor);
        deleteButton->addMouseListener(this, false); // Don't consume events
        addAndMakeVisible(deleteButton.get());
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

    void resized() override
    {
        // Position delete button in top-right corner
        deleteButton->setBounds(getWidth() - 32, 8, 24, 24);
    }

    void mouseEnter(const juce::MouseEvent& e) override
    {
        isMouseOver = true;
        updateDeleteButtonVisibility();
        repaint();
    }

    void mouseExit(const juce::MouseEvent& e) override
    {
        // Only hide if we're actually leaving the component entirely
        // Don't hide when entering a child component (like the delete button)
        if (!getLocalBounds().contains(e.getPosition()))
        {
            isMouseOver = false;
            updateDeleteButtonVisibility();
            repaint();
        }
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        // Keep button visible if mouse is within bounds
        if (getLocalBounds().contains(e.getPosition()))
        {
            isMouseOver = true;
            updateDeleteButtonVisibility();
        }
    }

    void updateDeleteButtonVisibility()
    {
        deleteButton->setAlpha(isMouseOver ? 1.0f : 0.0f);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        // Don't select if clicking on delete button
        if (!deleteButton->getBounds().contains(e.getPosition()))
        {
            if (selectCallback)
                selectCallback(file);
        }
    }

private:
    juce::File file;
    std::function<void(juce::File)> selectCallback;
    std::function<void(juce::File)> deleteCallback;
    std::unique_ptr<DrumKit> kit;
    juce::Image coverImage;
    std::unique_ptr<juce::TextButton> deleteButton;
    bool isMouseOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitTileComponent)
};

// File browser for adding external kits
class FileBrowserWindow : public juce::DocumentWindow,
                          public juce::FileBrowserListener
{
public:
    FileBrowserWindow(FlamAudioProcessorEditor* editor)
        : DocumentWindow("Add Kit from File System",
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
            kitFile = file.getChildFile("flamkit.yaml");

        // If the selected file is named flamkit.yaml, use it
        if (kitFile.existsAsFile() && kitFile.getFileName() == "flamkit.yaml")
        {
            // Add to kit library (persists to settings)
            owner->addKitToLibrary(kitFile);

            // Load the kit
            owner->loadKitFromPath(kitFile);

            // Refresh the kit list
            owner->scanForKits();
            owner->fileBrowserWindow.reset();

            // Reopen kit browser with updated list
            owner->openKitBrowser();
        }
    }

    void browserRootChanged(const juce::File&) override {}

private:
    FlamAudioProcessorEditor* owner;
    juce::WildcardFileFilter wildcard;
    juce::FileBrowserComponent browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileBrowserWindow)
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
        mainContent = std::make_unique<juce::Component>();

        // Create "Add Kit" button at the top
        addKitButton = std::make_unique<juce::TextButton>("Add Kit from File System");
        addKitButton->onClick = [this]() {
            owner->kitBrowserWindow.reset();
            owner->openFileBrowser();
        };
        mainContent->addAndMakeVisible(addKitButton.get());

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
            auto* tile = new KitTileComponent(kitFile,
                [this](juce::File selectedFile) {
                    // Select callback
                    owner->loadKitFromPath(selectedFile);
                    owner->kitBrowserWindow.reset();
                },
                [this](juce::File fileToDelete) {
                    // Delete callback
                    removeKit(fileToDelete);
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
        mainContent->addAndMakeVisible(viewport.get());

        // Layout: button at top, viewport below
        int buttonHeight = 40;
        addKitButton->setBounds(spacing, spacing, contentWidth - spacing * 2, buttonHeight);
        viewport->setBounds(0, buttonHeight + spacing * 2, contentWidth, 600 - buttonHeight - spacing * 2);
        mainContent->setSize(contentWidth, 600);

        setContentNonOwned(mainContent.get(), true);
        setResizable(true, true);
        centreWithSize(cols * (tileWidth + spacing) + spacing + 20, 640);
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

    void removeKit(const juce::File& kitFile)
    {
        // Capture owner pointer safely
        auto* ownerPtr = owner;

        // Show confirmation dialog
        auto options = juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon)
            .withTitle("Remove Kit")
            .withMessage("Are you sure you want to remove this kit from the library?\n\n"
                        "This will only remove it from the FlamKit library list.\n"
                        "The kit files will NOT be deleted from your computer.")
            .withButton("Remove")
            .withButton("Cancel");

        juce::AlertWindow::showAsync(options, [ownerPtr, kitFile](int result) {
            if (result == 1) // "Remove" button
            {
                ownerPtr->handleKitRemoval(kitFile);
            }
        });
    }

private:
    FlamAudioProcessorEditor* owner;
    std::unique_ptr<juce::Component> mainContent;
    std::unique_ptr<juce::TextButton> addKitButton;
    std::unique_ptr<juce::Viewport> viewport;
    std::unique_ptr<juce::Component> content;
    juce::OwnedArray<KitTileComponent> tiles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitBrowserWindow)
};

FlamAudioProcessorEditor::FlamAudioProcessorEditor(FlamAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
{
    titleLabel.setText("FlamKit", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredTop);
    addAndMakeVisible(titleLabel);

    kitBrowserLabel.setText("Kit:", juce::dontSendNotification);
    kitBrowserLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(kitBrowserLabel);

    currentKitLabel.setText("No kit loaded", juce::dontSendNotification);
    currentKitLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(currentKitLabel);

    kitBrowserButton.setButtonText("Browse...");
    kitBrowserButton.onClick = [this] { openKitBrowser(); };
    addAndMakeVisible(kitBrowserButton);

    drumPadsGroup.setText("Drum Pads");
    addAndMakeVisible(drumPadsGroup);

    // Create mixer panel
    mixerPanel = std::make_unique<MixerPanel>(audioProcessor.getValueTreeState(), audioProcessor);
    addAndMakeVisible(mixerPanel.get());

    setupDrumPads();

    setSize(1200, 600);

    // Scan for available kits and load last one
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

    // Mixer panel on the right side (300px wide)
    auto mixerArea = contentArea.removeFromRight(300);
    if (mixerPanel)
        mixerPanel->setBounds(mixerArea);

    contentArea.removeFromRight(10);  // Spacing

    // Kit browser area
    auto kitArea = contentArea.removeFromTop(40);
    kitBrowserLabel.setBounds(kitArea.removeFromLeft(40));
    kitArea.removeFromLeft(5);
    kitBrowserButton.setBounds(kitArea.removeFromRight(100));
    kitArea.removeFromRight(5);
    currentKitLabel.setBounds(kitArea);

    contentArea.removeFromTop(10);

    // Drum pads fill the remaining space
    drumPadsGroup.setBounds(contentArea);
    auto padContent = contentArea.reduced(15, 25);

    // Calculate grid dimensions based on number of pads
    const int numPads = drumPads.size();
    if (numPads > 0)
    {
        // Calculate optimal grid size (try to keep it roughly square)
        const int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(numPads))));
        const int rows = static_cast<int>(std::ceil(static_cast<float>(numPads) / cols));

        const int padSpacing = 8;
        const int padWidth = (padContent.getWidth() - (cols - 1) * padSpacing) / cols;
        const int padHeight = (padContent.getHeight() - (rows - 1) * padSpacing) / rows;

        for (int i = 0; i < numPads; ++i)
        {
            int row = i / cols;
            int col = i % cols;
            int x = padContent.getX() + col * (padWidth + padSpacing);
            int y = padContent.getY() + row * (padHeight + padSpacing);
            drumPads[i]->setBounds(x, y, padWidth, padHeight);
        }
    }
}

void FlamAudioProcessorEditor::setupDrumPads()
{
    // Initial setup - will be populated when a kit is loaded
    updateDrumPadsFromKit();
}

void FlamAudioProcessorEditor::updateDrumPadsFromKit()
{
    // Clear existing pads
    drumPads.clear();

    // If we have a loaded kit, create pads from its drum pieces
    if (currentKit && !currentKit->pieces.empty())
    {
        for (const auto& piece : currentKit->pieces)
        {
            auto* pad = new DrumPad(piece.name, piece.midiNote,
                [this](int note, float velocity) { triggerDrumPad(note, velocity); });
            drumPads.add(pad);
            addAndMakeVisible(pad);
        }
    }
    else
    {
        // No kit loaded - show placeholder message or empty grid
        // For now, we'll just leave it empty
    }

    // Trigger a relayout
    resized();
}

void FlamAudioProcessorEditor::triggerDrumPad(int midiNote, float velocity)
{
    audioProcessor.getEngine()->triggerNote(midiNote, velocity, 0);
}

void FlamAudioProcessorEditor::scanForKits()
{
    availableKits.clear();

    // Load kit list from settings file (no directory scanning)
    auto options = juce::PropertiesFile::Options();
    options.applicationName = "FLAM";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";

    juce::PropertiesFile props(options);
    auto kitList = props.getValue("kitPaths");

    if (kitList.isNotEmpty())
    {
        // Parse pipe-separated list of kit paths
        auto tokens = juce::StringArray::fromTokens(kitList, "|", "");
        for (const auto& token : tokens)
        {
            if (token.isNotEmpty())
            {
                juce::File kitFile(token);
                // Verify the file still exists before adding
                if (kitFile.existsAsFile())
                {
                    availableKits.push_back(kitFile);
                }
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

void FlamAudioProcessorEditor::openFileBrowser()
{
    // Don't open multiple browsers
    if (fileBrowserWindow != nullptr)
        return;

    // Create and show file browser window
    fileBrowserWindow = std::make_unique<FileBrowserWindow>(this);
}

void FlamAudioProcessorEditor::handleKitRemoval(const juce::File& kitFile)
{
    // Use MessageManager to safely defer the window destruction
    juce::MessageManager::callAsync([this, kitFile]() {
        // Close the browser window
        kitBrowserWindow.reset();

        // Remove from library
        removeKitFromLibrary(kitFile);

        // Rescan and reopen
        scanForKits();
        openKitBrowser();
    });
}

void FlamAudioProcessorEditor::addKitToLibrary(const juce::File& kitFile)
{
    auto kitPath = kitFile.getFullPathName();

    // Check if already in library
    for (const auto& existingKit : availableKits)
    {
        if (existingKit.getFullPathName() == kitPath)
            return; // Already exists
    }

    // Add to in-memory list
    availableKits.push_back(kitFile);

    // Persist to settings
    saveKitList();
}

void FlamAudioProcessorEditor::removeKitFromLibrary(const juce::File& kitFile)
{
    // Remove from available kits list
    availableKits.erase(
        std::remove(availableKits.begin(), availableKits.end(), kitFile),
        availableKits.end()
    );

    // Persist to settings
    saveKitList();

    // If the removed kit was the currently loaded kit, clear it
    if (currentKitFile == kitFile)
    {
        currentKitFile = juce::File();
        currentKitLabel.setText("No kit loaded", juce::dontSendNotification);

        // Load the first available kit if any exist
        if (!availableKits.empty())
        {
            loadKitFromPath(availableKits[0]);
        }
    }
}

void FlamAudioProcessorEditor::saveKitList()
{
    auto options = juce::PropertiesFile::Options();
    options.applicationName = "FLAM";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";

    juce::PropertiesFile props(options);

    // Join paths with pipe separator
    juce::String kitList;
    for (size_t i = 0; i < availableKits.size(); ++i)
    {
        if (i > 0)
            kitList += "|";
        kitList += availableKits[i].getFullPathName();
    }

    props.setValue("kitPaths", kitList);
    props.save();
}

void FlamAudioProcessorEditor::loadKitFromPath(const juce::File& kitFile)
{
    if (kitFile.existsAsFile() && kitFile.getFileName() == "flamkit.yaml")
    {
        // Store current kit file
        currentKitFile = kitFile;

        // Load kit metadata
        auto loader = std::make_unique<FlamKitLoader>();
        auto kit = loader->loadKit(kitFile);
        if (kit)
        {
            // Store the loaded kit
            currentKit = std::move(kit);

            // Update UI label
            currentKitLabel.setText(currentKit->name, juce::dontSendNotification);

            // Update drum pads to match the kit
            updateDrumPadsFromKit();
        }
        else
        {
            currentKit.reset();
            currentKitLabel.setText(kitFile.getParentDirectory().getFileName(), juce::dontSendNotification);
        }

        // Launch kit loading on a background thread for the audio engine
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