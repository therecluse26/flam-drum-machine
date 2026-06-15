// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Repository/RepositoryManager.h"
#include "FlamLookAndFeel.h"

namespace flam {

/**
 * Sources tab content — lists configured repository URLs, lets the user
 * add / remove user-supplied repos, and triggers a full index refresh.
 *
 * This component must be created and destroyed on the JUCE message thread.
 * It registers itself as a RepositoryManager::Listener on construction and
 * removes itself on destruction, so the manager outlives it safely.
 */
class RepoSourcesComponent : public juce::Component,
                              public juce::ListBoxModel,
                              public RepositoryManager::Listener
{
public:
    // -------------------------------------------------------------------------
    // Construction / destruction
    // -------------------------------------------------------------------------
    explicit RepoSourcesComponent (RepositoryManager& mgr)
        : manager_ (mgr)
    {
        // ── Header labels ─────────────────────────────────────────────────────
        headerLabel_.setText ("Repository Sources", juce::dontSendNotification);
        headerLabel_.setFont (juce::Font (14.0f, juce::Font::bold));
        headerLabel_.setColour (juce::Label::textColourId, juce::Colour (FlamColors::TextPrimary));
        addAndMakeVisible (headerLabel_);

        // ── Refresh All button ────────────────────────────────────────────────
        refreshButton_.setButtonText ("Refresh All");
        refreshButton_.onClick = [this] { doRefreshAll(); };
        addAndMakeVisible (refreshButton_);

        // ── URL list ─────────────────────────────────────────────────────────
        urlList_.setModel (this);
        urlList_.setRowHeight (36);
        urlList_.setColour (juce::ListBox::backgroundColourId,  juce::Colour (FlamColors::Surface));
        urlList_.setColour (juce::ListBox::outlineColourId,     juce::Colour (FlamColors::BorderSubtle));
        urlList_.setOutlineThickness (1);
        addAndMakeVisible (urlList_);

        // ── URL input + Add button ────────────────────────────────────────────
        urlInput_.setFont (juce::Font (12.0f));
        urlInput_.setColour (juce::TextEditor::backgroundColourId,  juce::Colour (FlamColors::Elevated));
        urlInput_.setColour (juce::TextEditor::textColourId,        juce::Colour (FlamColors::TextPrimary));
        urlInput_.setColour (juce::TextEditor::outlineColourId,     juce::Colour (FlamColors::BorderSubtle));
        urlInput_.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (FlamColors::AccentBlue));
        urlInput_.setTextToShowWhenEmpty ("https://example.com/repo/index.json",
                                          juce::Colour (FlamColors::TextDisabled));
        urlInput_.onReturnKey = [this] { doAddUrl(); };
        addAndMakeVisible (urlInput_);

        addButton_.setButtonText ("Add");
        addButton_.onClick = [this] { doAddUrl(); };
        addAndMakeVisible (addButton_);

        // ── Inline error label ────────────────────────────────────────────────
        errorLabel_.setText ("", juce::dontSendNotification);
        errorLabel_.setFont (juce::Font (11.0f));
        errorLabel_.setColour (juce::Label::textColourId, juce::Colour (FlamColors::AccentRed));
        addAndMakeVisible (errorLabel_);

        // ── Status label ──────────────────────────────────────────────────────
        statusLabel_.setText ("Not yet refreshed", juce::dontSendNotification);
        statusLabel_.setFont (juce::Font (11.0f));
        statusLabel_.setColour (juce::Label::textColourId, juce::Colour (FlamColors::TextSecondary));
        addAndMakeVisible (statusLabel_);

        // Initial URL list load
        reloadUrls();

        manager_.addListener (this);
    }

    ~RepoSourcesComponent() override
    {
        manager_.removeListener (this);
        urlList_.setModel (nullptr);
    }

    // -------------------------------------------------------------------------
    // juce::Component overrides
    // -------------------------------------------------------------------------
    void resized() override
    {
        auto area = getLocalBounds().reduced (12);

        // Header row
        auto headerRow = area.removeFromTop (32);
        headerLabel_.setBounds (headerRow.removeFromLeft (200));
        refreshButton_.setBounds (headerRow.removeFromRight (90).withSizeKeepingCentre (90, 26));

        area.removeFromTop (6);

        // Bottom controls: input row + error + status = ~72px
        auto bottomArea = area.removeFromBottom (72);
        auto inputRow = bottomArea.removeFromTop (30);
        addButton_.setBounds (inputRow.removeFromRight (60).withSizeKeepingCentre (60, 26));
        inputRow.removeFromRight (6);
        urlInput_.setBounds (inputRow.reduced (0, 2));

        bottomArea.removeFromTop (4);
        errorLabel_.setBounds (bottomArea.removeFromTop (18));
        statusLabel_.setBounds (bottomArea.removeFromTop (18));

        area.removeFromBottom (4); // gap above bottom controls

        // List box fills remaining space
        urlList_.setBounds (area);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (FlamColors::Background));
    }

    // -------------------------------------------------------------------------
    // juce::ListBoxModel overrides
    // -------------------------------------------------------------------------
    int getNumRows() override
    {
        return urls_.size();
    }

    void paintListBoxItem (int /*row*/, juce::Graphics& /*g*/,
                           int /*width*/, int /*height*/, bool /*selected*/) override
    {
        // All painting is done by the custom RowComponent returned below.
    }

    juce::Component* refreshComponentForRow (int row, bool /*isSelected*/,
                                              juce::Component* existing) override
    {
        auto* rowComp = dynamic_cast<RowComponent*> (existing);
        if (rowComp == nullptr)
            rowComp = new RowComponent (*this);

        if (row < urls_.size())
        {
            const bool isOfficial = (urls_[row] == kOfficialRepoUrl);
            rowComp->update (urls_[row], row, isOfficial);
        }

        return rowComp;
    }

    // -------------------------------------------------------------------------
    // RepositoryManager::Listener overrides
    // -------------------------------------------------------------------------
    void repositoryRefreshed() override
    {
        isRefreshing_ = false;
        lastRefreshTime_ = juce::Time::getCurrentTime();

        refreshButton_.setEnabled (true);
        statusLabel_.setText ("Last refreshed: " + lastRefreshTime_.toString (false, true, false, true),
                               juce::dontSendNotification);

        // Reload URLs in case a refresh caused a change
        reloadUrls();
    }

    void repositoryFetchFailed (const juce::String& url, const juce::String&) override
    {
        // Show the failing URL so the user can identify or remove it from Sources.
        statusLabel_.setText ("Could not reach: " + url, juce::dontSendNotification);
    }

    void kitDownloadProgress (const juce::String&, float) override {}
    void kitDownloadComplete (const juce::String&, const juce::File&) override {}
    void kitDownloadFailed   (const juce::String&, const juce::String&) override {}
    void coverImageReady     (const juce::String&, const juce::Image&) override {}

    // -------------------------------------------------------------------------
    // Public callbacks invoked by RowComponent
    // -------------------------------------------------------------------------
    void removeUrl (int row)
    {
        if (row < 0 || row >= urls_.size())
            return;

        const auto url = urls_[row];
        if (url == kOfficialRepoUrl)
            return; // guard: never remove official

        manager_.removeRepo (url);
        reloadUrls();
    }

private:
    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------
    static constexpr const char* kOfficialRepoUrl =
        "https://raw.githubusercontent.com/flam-drum-machine/flam-drum-machine/main/repo/index.json";

    void reloadUrls()
    {
        urls_ = manager_.getRepoUrls();
        urlList_.updateContent();
        urlList_.repaint();
    }

    void doAddUrl()
    {
        const auto url = urlInput_.getText().trim();

        if (url.isEmpty())
        {
            showError ("Please enter a URL.");
            return;
        }

        if (!url.startsWithIgnoreCase ("https://"))
        {
            showError ("URL must start with https://");
            return;
        }

        // Basic sanity: must contain at least one dot after the scheme
        const auto withoutScheme = url.fromFirstOccurrenceOf ("://", false, false);
        if (!withoutScheme.contains ("."))
        {
            showError ("URL appears malformed.");
            return;
        }

        clearError();
        manager_.addRepo (url);
        urlInput_.clear();
        reloadUrls();

        // Kick off a refresh so the new repo's index is fetched immediately
        doRefreshAll();
    }

    void doRefreshAll()
    {
        if (isRefreshing_)
            return;

        isRefreshing_ = true;
        refreshButton_.setEnabled (false);
        statusLabel_.setText (juce::String (juce::CharPointer_UTF8 ("Refreshing\xe2\x80\xa6")), juce::dontSendNotification); // "Refreshing…"
        clearError();

        // This enqueues background I/O and returns immediately.
        // repositoryRefreshed() fires on the message thread when done.
        manager_.refreshAllIndices();
    }

    void showError (const juce::String& msg)
    {
        errorLabel_.setText (msg, juce::dontSendNotification);
    }

    void clearError()
    {
        errorLabel_.setText ("", juce::dontSendNotification);
    }

    // -------------------------------------------------------------------------
    // Inner row component — one instance per visible list row, recycled by JUCE
    // -------------------------------------------------------------------------
    class RowComponent : public juce::Component
    {
    public:
        explicit RowComponent (RepoSourcesComponent& owner) : owner_ (owner)
        {
            urlLabel_.setFont (juce::Font (12.0f));
            urlLabel_.setColour (juce::Label::textColourId, juce::Colour (FlamColors::TextPrimary));
            addAndMakeVisible (urlLabel_);

            suffixLabel_.setFont (juce::Font (11.0f));
            suffixLabel_.setColour (juce::Label::textColourId, juce::Colour (FlamColors::TextSecondary));
            addAndMakeVisible (suffixLabel_);

            removeButton_.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95"))); // ✕
            removeButton_.onClick = [this] { owner_.removeUrl (row_); };
            removeButton_.setColour (juce::TextButton::buttonColourId,  juce::Colour (FlamColors::Elevated));
            removeButton_.setColour (juce::TextButton::textColourOffId, juce::Colour (FlamColors::AccentRed));
            addChildComponent (removeButton_); // visibility controlled by update()
        }

        void update (const juce::String& url, int row, bool isOfficial)
        {
            row_        = row;
            isOfficial_ = isOfficial;

            urlLabel_.setText (url, juce::dontSendNotification);
            suffixLabel_.setText (isOfficial ? "(official)" : "", juce::dontSendNotification);
            removeButton_.setVisible (!isOfficial);
            repaint();
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (8, 4);

            if (!isOfficial_)
            {
                removeButton_.setBounds (area.removeFromRight (28).withSizeKeepingCentre (28, 22));
                area.removeFromRight (4);
            }

            if (isOfficial_)
            {
                suffixLabel_.setBounds (area.removeFromRight (80));
                area.removeFromRight (4);
            }

            urlLabel_.setBounds (area);
        }

        void paint (juce::Graphics& g) override
        {
            const auto bg = isOfficial_ ? juce::Colour (FlamColors::Surface)
                                        : juce::Colour (FlamColors::Background);
            g.fillAll (bg);

            // Subtle divider at bottom
            g.setColour (juce::Colour (FlamColors::BorderSubtle));
            g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        }

    private:
        RepoSourcesComponent& owner_;
        juce::Label     urlLabel_;
        juce::Label     suffixLabel_;
        juce::TextButton removeButton_ { juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95")) };
        int  row_        = -1;
        bool isOfficial_ = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RowComponent)
    };

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    RepositoryManager& manager_;
    juce::StringArray  urls_;
    bool               isRefreshing_   = false;
    juce::Time         lastRefreshTime_;

    // ── Widgets ───────────────────────────────────────────────────────────────
    juce::Label      headerLabel_;
    juce::TextButton refreshButton_;
    juce::ListBox    urlList_ { "repo-url-list", nullptr };
    juce::TextEditor urlInput_;
    juce::TextButton addButton_;
    juce::Label      errorLabel_;
    juce::Label      statusLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RepoSourcesComponent)
};

} // namespace flam
