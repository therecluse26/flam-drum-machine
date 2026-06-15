// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../Formats/FlamKitLoader.h"
#include "../Repository/RepositoryManager.h"
#include "../UI/RepoSourcesComponent.h"
#include <thread>
#include <cmath>

namespace flam {

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

        g.setColour(isMouseOver ? juce::Colour(FlamColors::Interactive)
                                : juce::Colour(FlamColors::Elevated));
        g.fillRoundedRectangle(bounds, 8.0f);

        g.setColour(isMouseOver ? juce::Colour(FlamColors::AccentBlue).withAlpha(0.7f)
                                : juce::Colour(FlamColors::BorderSubtle));
        g.drawRoundedRectangle(bounds, 8.0f, 1.5f);

        auto contentBounds = bounds.reduced(8.0f);

        // Cover image or placeholder
        auto imageBounds = contentBounds.removeFromTop(contentBounds.getHeight() * 0.5f);
        if (coverImage.isValid())
        {
            g.drawImage(coverImage, imageBounds, juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour(juce::Colour(FlamColors::Surface));
            g.fillRoundedRectangle(imageBounds, 4.0f);
            g.setColour(juce::Colour(FlamColors::TextDisabled));
            g.setFont(20.0f);
            g.drawText("FLAM", imageBounds, juce::Justification::centred);
        }

        contentBounds.removeFromTop(8.0f);

        g.setColour(juce::Colour(FlamColors::TextPrimary));
        g.setFont(juce::Font(13.0f, juce::Font::bold));
        g.drawText(kit ? kit->name : "Unknown Kit",
                   contentBounds.removeFromTop(16.0f), juce::Justification::centredLeft);

        g.setColour(juce::Colour(FlamColors::TextSecondary));
        g.setFont(juce::Font(11.0f));
        g.drawText(kit && kit->author.isNotEmpty() ? kit->author : "",
                   contentBounds.removeFromTop(13.0f), juce::Justification::centredLeft);

        g.drawText(kit && kit->version.isNotEmpty() ? ("v" + kit->version) : "",
                   contentBounds.removeFromTop(13.0f), juce::Justification::centredLeft);

        contentBounds.removeFromTop(4.0f);

        g.setColour(juce::Colour(FlamColors::TextDisabled));
        g.setFont(juce::Font(10.0f));
        if (kit)
        {
            juce::String stats = juce::String(kit->getDrumPieceCount()) + " drums · " +
                                 juce::String(kit->getTotalSampleCount()) + " samples";
            g.drawText(stats, contentBounds.removeFromTop(12.0f), juce::Justification::centredLeft);
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

// Main tab component containing drum pads only (mixer moved to Mixer tab)
class FlamAudioProcessorEditor::MainTabComponent : public juce::Component
{
public:
    MainTabComponent(juce::GroupComponent* padsGroup, juce::OwnedArray<DrumPad>* pads)
        : drumPadsGroup(padsGroup)
        , drumPads(pads)
    {
        addAndMakeVisible(drumPadsGroup);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(10);

        // Drum pads fill the entire area
        drumPadsGroup->setBounds(bounds);
        auto padContent = bounds.reduced(15, 25);

        // Calculate grid dimensions based on number of pads
        const int numPads = drumPads->size();
        if (numPads > 0)
        {
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
                (*drumPads)[i]->setBounds(x, y, padWidth, padHeight);
            }
        }
    }

private:
    juce::GroupComponent* drumPadsGroup;
    juce::OwnedArray<DrumPad>* drumPads;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainTabComponent)
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

// Kit browser window — Library tab (local kit grid) + Sources tab (repo URL management)
class FlamAudioProcessorEditor::KitBrowserWindow : public juce::DocumentWindow
{
public:
    KitBrowserWindow (FlamAudioProcessorEditor* editor,
                      const std::vector<juce::File>& kits,
                      RepositoryManager* repoMgr)
        : DocumentWindow ("Select Drum Kit",
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                              .findColour (juce::ResizableWindow::backgroundColourId),
                          DocumentWindow::closeButton)
        , owner (editor)
    {
        libraryPanel_ = std::make_unique<LibraryPanel> (editor, kits);

        if (repoMgr != nullptr)
            sourcesPanel_ = std::make_unique<RepoSourcesComponent> (*repoMgr);

        tabs_ = std::make_unique<juce::TabbedComponent> (juce::TabbedButtonBar::TabsAtTop);
        tabs_->addTab ("Library", juce::Colour (FlamColors::Surface), libraryPanel_.get(),  false);
        if (sourcesPanel_)
            tabs_->addTab ("Sources", juce::Colour (FlamColors::Surface), sourcesPanel_.get(), false);

        setContentNonOwned (tabs_.get(), false);
        setResizable (true, true);
        centreWithSize (700, 650);
        setVisible (true);
        setAlwaysOnTop (true);
    }

    ~KitBrowserWindow() override
    {
        // Detach before unique_ptr members are destroyed.
        clearContentComponent();
    }

    void closeButtonPressed() override
    {
        owner->kitBrowserWindow.reset();
    }

private:
    // -------------------------------------------------------------------------
    // Library tab — scrollable grid of local kit tiles
    // -------------------------------------------------------------------------
    class LibraryPanel : public juce::Component
    {
    public:
        LibraryPanel (FlamAudioProcessorEditor* editor, const std::vector<juce::File>& kits)
            : owner_ (editor)
        {
            addKitButton_ = std::make_unique<juce::TextButton> ("Add Kit from File System");
            addKitButton_->onClick = [this] {
                owner_->kitBrowserWindow.reset();
                owner_->openFileBrowser();
            };
            addAndMakeVisible (addKitButton_.get());

            viewport_ = std::make_unique<juce::Viewport>();
            content_  = std::make_unique<juce::Component>();

            constexpr int tileW   = 200;
            constexpr int tileH   = 280;
            constexpr int spacing = 16;
            constexpr int cols    = 3;

            int row = 0, col = 0;
            for (const auto& kitFile : kits)
            {
                auto* tile = new KitTileComponent (kitFile,
                    [this] (juce::File f) {
                        owner_->loadKitFromPath (f);
                        owner_->kitBrowserWindow.reset();
                    },
                    [this] (juce::File f) { confirmRemoveKit_ (f); });

                tile->setBounds (col * (tileW + spacing) + spacing,
                                 row * (tileH + spacing) + spacing,
                                 tileW, tileH);
                content_->addAndMakeVisible (tile);
                tiles_.add (tile);

                if (++col >= cols) { col = 0; ++row; }
            }

            const int rows = (int) std::ceil ((float) kits.size() / cols);
            content_->setSize (cols * (tileW + spacing) + spacing,
                               rows * (tileH + spacing) + spacing);
            viewport_->setViewedComponent (content_.get(), false);
            viewport_->setScrollBarsShown (true, false);
            addAndMakeVisible (viewport_.get());
        }

        void resized() override
        {
            auto area = getLocalBounds();
            addKitButton_->setBounds (area.removeFromTop (44).reduced (8, 6));
            viewport_->setBounds (area);
        }

    private:
        void confirmRemoveKit_ (const juce::File& kitFile)
        {
            auto* ownerPtr = owner_;
            auto options = juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Remove Kit")
                .withMessage ("Are you sure you want to remove this kit from the library?\n\n"
                              "This will only remove it from the FlamKit library list.\n"
                              "The kit files will NOT be deleted from your computer.")
                .withButton ("Remove")
                .withButton ("Cancel");

            juce::AlertWindow::showAsync (options, [ownerPtr, kitFile] (int result) {
                if (result == 1)
                    ownerPtr->handleKitRemoval (kitFile);
            });
        }

        FlamAudioProcessorEditor* owner_;
        std::unique_ptr<juce::TextButton>  addKitButton_;
        std::unique_ptr<juce::Viewport>    viewport_;
        std::unique_ptr<juce::Component>   content_;
        juce::OwnedArray<KitTileComponent> tiles_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPanel)
    };

    // -------------------------------------------------------------------------
    // Members — declared so that tabs_ is destroyed before its tab panels
    // -------------------------------------------------------------------------
    FlamAudioProcessorEditor*              owner;
    std::unique_ptr<LibraryPanel>          libraryPanel_;
    std::unique_ptr<RepoSourcesComponent>  sourcesPanel_;
    std::unique_ptr<juce::TabbedComponent> tabs_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitBrowserWindow)
};

FlamAudioProcessorEditor::FlamAudioProcessorEditor(FlamAudioProcessor& p)
    : AudioProcessorEditor(&p)
    , audioProcessor(p)
{
    repositoryManager = std::make_unique<RepositoryManager>();

    // Apply the design system to the editor — cascades to all child components.
    setLookAndFeel(&flamLaf);

    // Title — left-aligned wordmark
    titleLabel.setText("FLAMKIT", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(18.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextPrimary));
    addAndMakeVisible(titleLabel);

    // "Kit:" prefix — subdued
    kitBrowserLabel.setText("Kit:", juce::dontSendNotification);
    kitBrowserLabel.setJustificationType(juce::Justification::centredRight);
    kitBrowserLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextSecondary));
    addAndMakeVisible(kitBrowserLabel);

    // Currently loaded kit name — center of header
    currentKitLabel.setText("No kit loaded", juce::dontSendNotification);
    currentKitLabel.setJustificationType(juce::Justification::centredLeft);
    currentKitLabel.setColour(juce::Label::textColourId, juce::Colour(FlamColors::TextPrimary));
    addAndMakeVisible(currentKitLabel);

    kitBrowserButton.setButtonText("Browse");
    kitBrowserButton.onClick = [this] { openKitBrowser(); };
    addAndMakeVisible(kitBrowserButton);

    drumPadsGroup.setText("Drum Pads");

    // Create full-width tabbed interface
    mixerTabs = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);

    // Tab 1: Drum Pads (full width)
    mainTab = std::make_unique<MainTabComponent>(&drumPadsGroup, &drumPads);
    mixerTabs->addTab("Main", juce::Colours::darkgrey, mainTab.get(), false);

    // Tab 2: Mixer (full width)
    if (auto* mixer = audioProcessor.getMixer())
    {
        perChannelMixerPanel = std::make_unique<MixerPanel>(*mixer);
        mixerTabs->addTab("Mixer", juce::Colours::darkgrey, perChannelMixerPanel.get(), false);
    }

    addAndMakeVisible(mixerTabs.get());

    setupDrumPads();

    setSize(1200, 700);
    setResizable(true, true);
    setResizeLimits(800, 500, 2400, 1400);

    // Scan for available kits and load last one
    scanForKits();
    loadLastLoadedKit();
}

FlamAudioProcessorEditor::~FlamAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void FlamAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Base background
    g.fillAll(juce::Colour(FlamColors::Background));

    // Header bar — Surface tone with accent bottom border
    auto headerBounds = getLocalBounds().removeFromTop(100).toFloat();
    g.setColour(juce::Colour(FlamColors::Surface));
    g.fillRect(headerBounds);

    // Accent border at the bottom of the header
    g.setColour(juce::Colour(FlamColors::AccentBlue).withAlpha(0.5f));
    g.fillRect(headerBounds.removeFromBottom(1.0f));
}

void FlamAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    // 100px header: first 50px = title row, next 50px = kit row
    auto headerArea = bounds.removeFromTop(100).reduced(16, 0);

    // Title row: FLAMKIT wordmark on left
    auto titleRow = headerArea.removeFromTop(50);
    titleLabel.setBounds(titleRow.removeFromLeft(120));

    // Kit row: "Kit:" | kit name | [Browse] button
    auto kitRow = headerArea.removeFromTop(44);
    kitBrowserLabel.setBounds(kitRow.removeFromLeft(36));
    kitRow.removeFromLeft(6);
    kitBrowserButton.setBounds(kitRow.removeFromRight(80).withSizeKeepingCentre(80, 28));
    kitRow.removeFromRight(8);
    currentKitLabel.setBounds(kitRow);

    // Remaining area: content / tabs
    bounds.removeFromTop(2); // gap below header border
    if (mixerTabs)
        mixerTabs->setBounds(bounds.reduced(0));
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
            if (mainTab)
                mainTab->addAndMakeVisible(pad);
        }
    }
    else
    {
        // No kit loaded - show placeholder message or empty grid
        // For now, we'll just leave it empty
    }

    // Trigger a relayout
    if (mainTab)
        mainTab->resized();
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
    if (kitBrowserWindow != nullptr)
        return;

    kitBrowserWindow = std::make_unique<KitBrowserWindow>(this, availableKits, repositoryManager.get());
}

void FlamAudioProcessorEditor::onRemoteKitInstalled(const juce::File& kitYaml)
{
    addKitToLibrary(kitYaml);
    scanForKits();
    loadKitFromPath(kitYaml);
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

            // Configure mixer with kit's channel count. Goes through the processor so the
            // channel rebuild + FX-buffer reallocation happen with audio processing suspended
            // (calling mixer->setNumChannels() directly here raced the audio thread and left
            // channelFXBuffers undersized → out-of-bounds crash on the next processBlock).
            audioProcessor.configureMixerForChannels(currentKit->channelNames);

            // Refresh the mixer UI
            if (perChannelMixerPanel)
                perChannelMixerPanel->refreshChannels();
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