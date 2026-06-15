// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 FLAM Contributors
// See LICENSE in the repository root for full license text.

#include "RepositoryManager.h"

namespace flam {

// =============================================================================
// Internal helpers
// =============================================================================

namespace {

/** Chunk size for streaming downloads. */
constexpr int kDownloadChunkBytes = 65536;
/** Minimum percentage-point change before a progress notification is posted. */
constexpr float kProgressThreshold = 0.01f;

/** Parse a juce::StringArray from a JSON array of strings. */
juce::StringArray parseStringArray (const juce::var& v)
{
    juce::StringArray result;
    if (auto* arr = v.getArray())
        for (const auto& item : *arr)
            result.add (item.toString());
    return result;
}

} // namespace

// =============================================================================
// RepositoryManager
// =============================================================================

RepositoryManager::RepositoryManager()
{
    loadSettings();
}

RepositoryManager::~RepositoryManager()
{
    // Signal jobs to stop posting callbacks before draining the pool.
    aliveFlag_->store (false);
    shuttingDown_.store (true);
    // threadPool_ is destroyed last (declared last), its destructor waits for
    // all pending jobs.  We also call removeAllJobs() explicitly to honour the
    // 5-second timeout rather than blocking indefinitely.
    threadPool_.removeAllJobs (true, 5000);
}

// -------------------------------------------------------------------------
// Listener management
// -------------------------------------------------------------------------

void RepositoryManager::addListener (Listener* l)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    listeners_.add (l);
}

void RepositoryManager::removeListener (Listener* l)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    listeners_.remove (l);
}

// -------------------------------------------------------------------------
// Repo URL management
// -------------------------------------------------------------------------

void RepositoryManager::addRepo (const juce::String& url)
{
    if (url.isEmpty() || repoUrls_.contains (url))
        return;
    repoUrls_.add (url);
    saveSettings();
}

void RepositoryManager::removeRepo (const juce::String& url)
{
    if (!repoUrls_.contains (url))
        return;
    repoUrls_.removeString (url);
    saveSettings();
}

juce::StringArray RepositoryManager::getRepoUrls() const
{
    return repoUrls_;
}

// -------------------------------------------------------------------------
// Index refresh
// -------------------------------------------------------------------------

void RepositoryManager::refreshAllIndices()
{
    // Must not be called from the audio thread — network I/O would block the
    // callback and violate real-time constraints.  isThisTheMessageThread()
    // guards against accidental call from UI code that intends to block; audio
    // threads are never the message thread, so both are caught.
    jassert (!juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Snapshot the URL list before handing off to the background thread.
    auto urls = repoUrls_;

    threadPool_.addJob ([this, urls]()
    {
        std::vector<KitIndexEntry> merged;

        for (const auto& url : urls)
        {
            if (shuttingDown_.load()) return;

            juce::URL juceUrl (url);
            auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                            .withConnectionTimeoutMs (10000);
            auto stream = juceUrl.createInputStream (opts);
            if (!stream)
                continue;  // network unavailable or URL unreachable — skip silently

            auto json = stream->readEntireStreamAsString();
            if (json.isEmpty())
                continue;

            // Extract a display name from the URL (last path component before /index.json).
            auto repoName = juce::URL (url).getDomain();

            parseIndexJson (json, repoName, merged);
        }

        if (shuttingDown_.load()) return;

        // Cross-reference with installed kits.
        for (auto& entry : merged)
            entry.isInstalled = isKitInstalled (entry.id);

        {
            juce::ScopedWriteLock lock (kitsLock_);
            kits_ = std::move (merged);
        }

        notifyRefreshed();
    });
}

std::vector<KitIndexEntry> RepositoryManager::getKits() const
{
    juce::ScopedReadLock lock (kitsLock_);
    return kits_;
}

// -------------------------------------------------------------------------
// Kit download
// -------------------------------------------------------------------------

void RepositoryManager::downloadKit (const juce::String& kitId)
{
    // Find the entry to get the download URL and expected checksum.
    KitIndexEntry entry;
    {
        juce::ScopedReadLock lock (kitsLock_);
        for (const auto& k : kits_)
        {
            if (k.id == kitId)
            {
                entry = k;
                break;
            }
        }
    }

    if (entry.id.isEmpty())
    {
        notifyFailed (kitId, "Kit not found in index");
        return;
    }

    if (entry.downloadUrl.isEmpty())
    {
        notifyFailed (kitId, "No download URL for kit: " + kitId);
        return;
    }

    threadPool_.addJob ([this, entry]()
    {
        if (shuttingDown_.load()) return;

        // Capture id from entry to avoid shadowing the outer kitId parameter.
        const auto id = entry.id;

        // Temp file for the ZIP download.
        auto tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("flamkit_" + id + ".zip");
        tempFile.deleteFile();

        // ---- Download ----
        juce::URL juceUrl (entry.downloadUrl);
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (30000);
        auto stream = juceUrl.createInputStream (opts);

        if (!stream)
        {
            notifyFailed (id, "Download failed: could not connect to " + entry.downloadUrl);
            return;
        }

        {
            juce::FileOutputStream fos (tempFile);
            if (!fos.openedOk())
            {
                notifyFailed (id, "Cannot create temp file for download");
                return;
            }

            const int64_t total = stream->getTotalLength();
            int64_t downloaded = 0;
            float lastNotifiedProgress = -1.0f;
            juce::HeapBlock<char> chunk (kDownloadChunkBytes);

            while (!stream->isExhausted() && !shuttingDown_.load())
            {
                auto n = stream->read (chunk.getData(), kDownloadChunkBytes);
                if (n <= 0) break;

                fos.write (chunk.getData(), (size_t) n);
                downloaded += n;

                if (total > 0)
                {
                    auto progress = float (downloaded) / float (total);
                    if (progress - lastNotifiedProgress >= kProgressThreshold)
                    {
                        lastNotifiedProgress = progress;
                        notifyProgress (id, progress);
                    }
                }
            }
        }

        if (shuttingDown_.load())
        {
            tempFile.deleteFile();
            return;
        }

        // ---- SHA-256 verification ----
        if (entry.checksumSha256.isNotEmpty())
        {
            juce::FileInputStream fis (tempFile);
            if (!fis.openedOk())
            {
                tempFile.deleteFile();
                notifyFailed (id, "Cannot re-open downloaded file for checksum");
                return;
            }

            juce::SHA256 sha (fis);
            if (sha.toHexString() != entry.checksumSha256.toLowerCase())
            {
                tempFile.deleteFile();
                notifyFailed (id, "SHA-256 mismatch — download may be corrupt");
                return;
            }
        }

        // ---- Extract ----
        auto kitDir = kitInstallDir (id);
        kitDir.createDirectory();

        juce::ZipFile zip (tempFile);
        auto result = zip.uncompressTo (kitDir, true);
        tempFile.deleteFile();

        if (result.failed())
        {
            notifyFailed (id, "ZIP extraction failed: " + result.getErrorMessage());
            return;
        }

        // Locate flamkit.yaml — may be at root or inside a single sub-directory.
        auto yamlFile = kitDir.getChildFile ("flamkit.yaml");
        if (!yamlFile.existsAsFile())
        {
            juce::Array<juce::File> found;
            kitDir.findChildFiles (found, juce::File::findFiles, true, "flamkit.yaml");
            if (!found.isEmpty())
                yamlFile = found[0];
        }

        if (!yamlFile.existsAsFile())
        {
            notifyFailed (id, "flamkit.yaml not found after extraction");
            return;
        }

        // Mark installed in the cached index.
        {
            juce::ScopedWriteLock lock (kitsLock_);
            for (auto& k : kits_)
                if (k.id == id)
                    k.isInstalled = true;
        }

        notifyProgress (id, 1.0f);
        notifyComplete (id, yamlFile);
    });
}

// -------------------------------------------------------------------------
// Cover image
// -------------------------------------------------------------------------

void RepositoryManager::getCoverImage (const KitIndexEntry& entry)
{
    if (entry.id.isEmpty() || entry.coverImageUrl.isEmpty())
        return;

    const auto kitId  = entry.id;
    const auto imgUrl = entry.coverImageUrl;

    threadPool_.addJob ([this, kitId, imgUrl]()
    {
        if (shuttingDown_.load()) return;

        auto cacheFile = coverCacheFile (kitId);

        // Serve from disk cache if already downloaded.
        if (cacheFile.existsAsFile())
        {
            auto img = juce::ImageFileFormat::loadFrom (cacheFile);
            if (img.isValid())
            {
                notifyCoverReady (kitId, img);
                return;
            }
            // Cache file is corrupt — delete and re-fetch.
            cacheFile.deleteFile();
        }

        if (shuttingDown_.load()) return;

        juce::URL juceUrl (imgUrl);
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (10000);
        auto stream = juceUrl.createInputStream (opts);
        if (!stream) return;

        auto img = juce::ImageFileFormat::loadFrom (*stream);
        if (!img.isValid() || shuttingDown_.load()) return;

        // Write PNG to the covers cache directory.
        cacheFile.getParentDirectory().createDirectory();
        juce::FileOutputStream fos (cacheFile);
        if (fos.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (img, fos);
        }

        notifyCoverReady (kitId, img);
    });
}

// -------------------------------------------------------------------------
// Settings helpers
// -------------------------------------------------------------------------

juce::PropertiesFile::Options RepositoryManager::settingsOptions()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "FLAM";
    opts.filenameSuffix      = ".settings";
    opts.osxLibrarySubFolder = "Application Support";
   #if JUCE_LINUX || JUCE_BSD
    opts.folderName          = "~/.config";
   #else
    opts.folderName          = "";
   #endif
    return opts;
}

void RepositoryManager::loadSettings()
{
    juce::PropertiesFile props (settingsOptions());
    auto stored = props.getValue (kSettingsKey);

    repoUrls_.clear();

    if (stored.isNotEmpty())
    {
        repoUrls_ = juce::StringArray::fromTokens (stored, "|", "");
        repoUrls_.removeEmptyStrings();
    }

    // Official repo is always present, and always first.
    if (!repoUrls_.contains (kOfficialRepoUrl))
        repoUrls_.insert (0, kOfficialRepoUrl);
}

void RepositoryManager::saveSettings()
{
    juce::PropertiesFile props (settingsOptions());
    props.setValue (kSettingsKey, repoUrls_.joinIntoString ("|"));
    props.save();
}

// -------------------------------------------------------------------------
// Path helpers
// -------------------------------------------------------------------------

juce::File RepositoryManager::kitInstallDir (const juce::String& kitId) const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("FlamKit/kits")
               .getChildFile (kitId);
}

juce::File RepositoryManager::coverCacheFile (const juce::String& kitId) const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("FlamKit/cache/covers")
               .getChildFile (kitId + ".png");
}

bool RepositoryManager::isKitInstalled (const juce::String& kitId) const
{
    return kitInstallDir (kitId).getChildFile ("flamkit.yaml").existsAsFile();
}

// -------------------------------------------------------------------------
// JSON index parsing
// -------------------------------------------------------------------------

void RepositoryManager::parseIndexJson (const juce::String& json,
                                        const juce::String& repoName,
                                        std::vector<KitIndexEntry>& out) const
{
    juce::var parsed;
    if (juce::JSON::parse (json, parsed).failed())
        return;

    auto* root = parsed.getDynamicObject();
    if (!root) return;

    auto kitsVar = root->getProperty ("kits");
    auto* arr = kitsVar.getArray();
    if (!arr) return;

    for (const auto& kitVar : *arr)
    {
        auto* k = kitVar.getDynamicObject();
        if (!k) continue;

        KitIndexEntry entry;
        entry.id             = k->getProperty ("id").toString();
        entry.name           = k->getProperty ("name").toString();
        entry.author         = k->getProperty ("author").toString();
        entry.version        = k->getProperty ("version").toString();
        entry.description    = k->getProperty ("description").toString();
        entry.license        = k->getProperty ("license").toString();
        entry.licenseUrl     = k->getProperty ("licenseUrl").toString();
        entry.downloadUrl    = k->getProperty ("downloadUrl").toString();
        entry.coverImageUrl  = k->getProperty ("coverImageUrl").toString();
        entry.sizeBytes      = (int64_t)(double) k->getProperty ("sizeBytes");
        entry.drumPieceCount = (int) k->getProperty ("drumPieceCount");
        entry.sampleCount    = (int) k->getProperty ("sampleCount");
        entry.checksumSha256 = k->getProperty ("checksumSha256").toString().toLowerCase();
        entry.tags           = parseStringArray (k->getProperty ("tags"));
        entry.channels       = parseStringArray (k->getProperty ("channels"));
        entry.repoName       = repoName;

        if (entry.id.isNotEmpty())
            out.push_back (entry);
    }
}

// -------------------------------------------------------------------------
// Listener notification (always dispatched on the message thread)
// -------------------------------------------------------------------------

void RepositoryManager::notifyRefreshed()
{
    auto alive = aliveFlag_;
    juce::MessageManager::callAsync ([this, alive]()
    {
        if (alive->load())
            listeners_.call ([] (Listener& l) { l.repositoryRefreshed(); });
    });
}

void RepositoryManager::notifyProgress (const juce::String& id, float progress)
{
    auto alive = aliveFlag_;
    juce::MessageManager::callAsync ([this, alive, id, progress]()
    {
        if (alive->load())
            listeners_.call ([&] (Listener& l) { l.kitDownloadProgress (id, progress); });
    });
}

void RepositoryManager::notifyComplete (const juce::String& id, const juce::File& yaml)
{
    auto alive = aliveFlag_;
    juce::MessageManager::callAsync ([this, alive, id, yaml]()
    {
        if (alive->load())
            listeners_.call ([&] (Listener& l) { l.kitDownloadComplete (id, yaml); });
    });
}

void RepositoryManager::notifyFailed (const juce::String& id, const juce::String& error)
{
    auto alive = aliveFlag_;
    juce::MessageManager::callAsync ([this, alive, id, error]()
    {
        if (alive->load())
            listeners_.call ([&] (Listener& l) { l.kitDownloadFailed (id, error); });
    });
}

void RepositoryManager::notifyCoverReady (const juce::String& id, const juce::Image& img)
{
    auto alive = aliveFlag_;
    juce::MessageManager::callAsync ([this, alive, id, img]()
    {
        if (alive->load())
            listeners_.call ([&] (Listener& l) { l.coverImageReady (id, img); });
    });
}

} // namespace flam
