// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_cryptography/juce_cryptography.h>
#include <juce_graphics/juce_graphics.h>
#include "KitIndexEntry.h"
#include <atomic>
#include <vector>

namespace flam {

/**
 * Manages remote kit repository subscriptions, index fetching, kit downloads,
 * and cover art caching. All network I/O runs on an internal ThreadPool.
 *
 * Caller contract:
 *   - addListener / removeListener must be called from the JUCE message thread.
 *   - All Listener callbacks are dispatched on the JUCE message thread.
 *   - refreshAllIndices / downloadKit / getCoverImage are safe to call from any
 *     non-audio thread (they enqueue jobs immediately and return).
 */
class RepositoryManager
{
public:
    // -------------------------------------------------------------------------
    // Listener interface
    // -------------------------------------------------------------------------
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void repositoryRefreshed() = 0;
        virtual void repositoryFetchFailed (const juce::String& url, const juce::String& error) = 0;
        virtual void kitDownloadProgress (const juce::String& kitId, float progress) = 0;
        virtual void kitDownloadComplete (const juce::String& kitId, const juce::File& kitYaml) = 0;
        virtual void kitDownloadFailed   (const juce::String& kitId, const juce::String& error) = 0;
        virtual void coverImageReady     (const juce::String& kitId, const juce::Image& img) = 0;
    };

    // -------------------------------------------------------------------------
    RepositoryManager();
    ~RepositoryManager();

    void addListener    (Listener* l);
    void removeListener (Listener* l);

    // -------------------------------------------------------------------------
    // Repository URL management
    // -------------------------------------------------------------------------

    /** Add a user-supplied repo URL. Persists across restarts. */
    void addRepo    (const juce::String& url);
    /** Remove a repo URL. Has no effect if the URL is not in the list. */
    void removeRepo (const juce::String& url);
    /** Returns the current ordered list of repo URLs (official first). */
    juce::StringArray getRepoUrls() const;

    // -------------------------------------------------------------------------
    // Index management
    // -------------------------------------------------------------------------

    /**
     * Fetch all registered repo indices on a background thread, merge the
     * results, and fire Listener::repositoryRefreshed() on the message thread.
     *
     * Must NOT be called from the audio thread — this method submits work to
     * the internal ThreadPool and returns immediately, but network I/O on a
     * real-time callback would violate plugin host contracts.
     *   jassert(!juce::MessageManager::getInstance()->isThisTheMessageThread())
     * would catch accidental message-thread blocking; audio-thread misuse is
     * guarded by convention (no juce::ThreadPool access is RT-safe).
     */
    void refreshAllIndices();

    /** Returns a snapshot of the merged kit list. Thread-safe. */
    std::vector<KitIndexEntry> getKits() const;

    // -------------------------------------------------------------------------
    // Download & cover art
    // -------------------------------------------------------------------------

    /**
     * Download, verify (SHA-256), and extract the kit with the given ID.
     * Fires kitDownloadProgress / kitDownloadComplete / kitDownloadFailed on
     * the message thread. The kit is extracted to:
     *   <userAppData>/FlamKit/kits/<id>/
     */
    void downloadKit (const juce::String& kitId);

    /**
     * Async fetch/cache of cover art for an entry. If the cover is already
     * cached on disk it is loaded directly without a network request.
     * Fires coverImageReady on the message thread.
     */
    void getCoverImage (const KitIndexEntry& entry);

private:
    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------
    static constexpr const char* kOfficialRepoUrl =
        "https://raw.githubusercontent.com/therecluse26/flam-drum-machine/main/repo/index.json";
    static constexpr const char* kSettingsKey = "repoUrls";

    // -------------------------------------------------------------------------
    // Settings helpers (match PluginEditor PropertiesFile pattern)
    // -------------------------------------------------------------------------
    static juce::PropertiesFile::Options settingsOptions();
    void loadSettings();
    void saveSettings();

    // -------------------------------------------------------------------------
    // Path helpers
    // -------------------------------------------------------------------------
    juce::File kitInstallDir  (const juce::String& kitId) const;
    juce::File coverCacheFile (const juce::String& kitId) const;
    bool       isKitInstalled (const juce::String& kitId) const;

    // -------------------------------------------------------------------------
    // JSON parsing
    // -------------------------------------------------------------------------
    void parseIndexJson (const juce::String& json,
                        const juce::String& repoName,
                        std::vector<KitIndexEntry>& out) const;

    // -------------------------------------------------------------------------
    // Listener dispatch (always posted to the message thread)
    // -------------------------------------------------------------------------
    void notifyRefreshed    ();
    void notifyFetchFailed  (const juce::String& url, const juce::String& error);
    void notifyProgress     (const juce::String& id, float progress);
    void notifyComplete     (const juce::String& id, const juce::File& yaml);
    void notifyFailed       (const juce::String& id, const juce::String& error);
    void notifyCoverReady   (const juce::String& id, const juce::Image& img);

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------
    juce::StringArray repoUrls_;            // guarded by message-thread access

    mutable juce::ReadWriteLock kitsLock_;
    std::vector<KitIndexEntry>  kits_;

    juce::ListenerList<Listener> listeners_;  // message-thread only

    // Lifetime guard: set to false in destructor; callAsync lambdas check before
    // touching `this`.  Shared ownership keeps the atomic alive past destruction.
    std::shared_ptr<std::atomic<bool>> aliveFlag_{
        std::make_shared<std::atomic<bool>>(true)};

    std::atomic<bool> shuttingDown_{false};

    // Declared last so its destructor (which drains pending jobs) runs before
    // any other members are torn down.
    juce::ThreadPool threadPool_{2};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RepositoryManager)
};

} // namespace flam
