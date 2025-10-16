# Distributed Repositories Feature Specification

**Status:** v1.1 Feature
**Priority:** High
**Dependencies:** `FlamKitLoader`, Network layer

---

## Overview

Implement decentralized kit distribution system inspired by Linux package managers (APT/PPA). Users can browse, download, and auto-update kits from official and community-hosted repositories without leaving the plugin. v1.0 ships with direct download from website only.

---

## Technical Requirements

### Core Functionality

1. **Repository Management**
   - Default official FlamKit repository pre-configured
   - User-configurable repository URLs (add/remove/enable/disable)
   - Repository metadata caching for offline browsing
   - Automatic repository index updates (check on launch)

2. **Kit Discovery**
   - Fetch repository index (JSON manifest)
   - Parse available kits with metadata (name, description, size, channels, thumbnail)
   - Display in browsable UI with filtering/search
   - Show installed vs. available kits

3. **Kit Download**
   - Background download with progress indication
   - Verify checksums (SHA256) for integrity
   - Resume support for interrupted downloads
   - Unzip/extract kits to local library directory

4. **Update System**
   - Check for kit updates on repository sync
   - Notify user of available updates
   - One-click update with automatic backup of old version

---

## Implementation Details

### Repository Index Format (JSON)

```json
{
  "repository": {
    "name": "FlamKit Official Repository",
    "url": "https://kits.flamkit.org",
    "version": "1.0",
    "lastUpdated": "2025-10-15T12:00:00Z"
  },
  "kits": [
    {
      "id": "modern-rock-studio",
      "name": "Modern Rock Studio",
      "description": "Professional 10-piece rock kit recorded in Abbey Road Studio 2",
      "version": "1.2.0",
      "author": "FlamKit Team",
      "license": "CC-BY-4.0",
      "downloadUrl": "https://kits.flamkit.org/modern-rock-studio-v1.2.0.zip",
      "checksum": "sha256:abc123...",
      "size": 1.2e9,
      "channels": [
        "Kick Close",
        "Kick Sub",
        "Snare Top",
        "Snare Bottom",
        "OH-L",
        "OH-R",
        "Room",
        "Ambient"
      ],
      "thumbnailUrl": "https://kits.flamkit.org/thumbs/modern-rock-studio.jpg",
      "tags": ["rock", "studio", "multi-mic", "8-channel"],
      "sampleRate": 44100,
      "bitDepth": 24,
      "numSamples": 1247,
      "minVersion": "1.0.0"
    }
  ]
}
```

### Class Structure

```cpp
// Source/Core/RepositoryManager.h
class RepositoryManager
{
public:
    RepositoryManager();

    // Repository management
    void addRepository(const juce::URL& repositoryUrl);
    void removeRepository(const juce::String& repositoryId);
    void enableRepository(const juce::String& repositoryId, bool enabled);
    std::vector<Repository> getRepositories() const;

    // Sync repositories (background thread)
    void syncAllRepositories(std::function<void(float progress)> progressCallback);

    // Kit discovery
    std::vector<KitInfo> getAvailableKits() const;
    std::vector<KitInfo> getInstalledKits() const;
    std::vector<KitInfo> searchKits(const juce::String& query) const;

    // Kit download
    void downloadKit(
        const juce::String& kitId,
        std::function<void(float progress)> progressCallback,
        std::function<void(bool success, juce::String error)> completionCallback
    );

    // Update checks
    std::vector<KitUpdateInfo> checkForUpdates();
    void updateKit(const juce::String& kitId);

private:
    struct Repository
    {
        juce::String id;
        juce::URL url;
        bool enabled;
        juce::var indexJson;  // Cached index
        juce::Time lastSync;
    };

    struct KitInfo
    {
        juce::String id;
        juce::String name;
        juce::String description;
        juce::String version;
        juce::String author;
        juce::URL downloadUrl;
        juce::String checksum;
        int64 size;
        std::vector<juce::String> channels;
        juce::URL thumbnailUrl;
        std::vector<juce::String> tags;
        juce::String repositoryId;
        bool isInstalled;
    };

    std::vector<Repository> repositories;
    juce::File localKitDirectory;
    juce::File cacheDirectory;

    // Network operations
    bool fetchRepositoryIndex(Repository& repo);
    bool downloadFile(const juce::URL& url, const juce::File& destination,
                     std::function<void(float)> progressCallback);
    bool verifyChecksum(const juce::File& file, const juce::String& expectedChecksum);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RepositoryManager)
};
```

### Repository Syncing

```cpp
void RepositoryManager::syncAllRepositories(std::function<void(float)> progressCallback)
{
    // Run on background thread
    juce::Thread::launch([this, progressCallback]()
    {
        int repoCount = 0;
        for (auto& repo : repositories)
        {
            if (!repo.enabled)
                continue;

            if (progressCallback)
                progressCallback(static_cast<float>(repoCount) / repositories.size());

            fetchRepositoryIndex(repo);
            repoCount++;
        }

        if (progressCallback)
            progressCallback(1.0f);

        // Save cached indices to disk
        saveCachedIndices();
    });
}

bool RepositoryManager::fetchRepositoryIndex(Repository& repo)
{
    juce::URL indexUrl = repo.url.getChildURL("index.json");

    // Create HTTP request
    juce::URL::InputStreamOptions options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(10000)
        .withNumRedirectsToFollow(5);

    std::unique_ptr<juce::InputStream> stream(indexUrl.createInputStream(options));

    if (stream == nullptr)
    {
        DBG("Failed to fetch repository index: " + repo.url.toString(true));
        return false;
    }

    // Parse JSON
    juce::String jsonString = stream->readEntireStreamAsString();
    juce::var parsedJson = juce::JSON::parse(jsonString);

    if (!parsedJson.isObject())
    {
        DBG("Invalid JSON in repository index");
        return false;
    }

    // Cache the index
    repo.indexJson = parsedJson;
    repo.lastSync = juce::Time::getCurrentTime();

    return true;
}
```

### Kit Downloading

```cpp
void RepositoryManager::downloadKit(
    const juce::String& kitId,
    std::function<void(float)> progressCallback,
    std::function<void(bool, juce::String)> completionCallback)
{
    // Find kit info
    KitInfo kitInfo;
    bool found = false;

    for (const auto& repo : repositories)
    {
        const auto kits = repo.indexJson["kits"];
        if (!kits.isArray())
            continue;

        for (const auto& kit : *kits.getArray())
        {
            if (kit["id"].toString() == kitId)
            {
                kitInfo.id = kit["id"];
                kitInfo.downloadUrl = juce::URL(kit["downloadUrl"].toString());
                kitInfo.checksum = kit["checksum"].toString();
                found = true;
                break;
            }
        }

        if (found)
            break;
    }

    if (!found)
    {
        if (completionCallback)
            completionCallback(false, "Kit not found");
        return;
    }

    // Download on background thread
    juce::Thread::launch([this, kitInfo, progressCallback, completionCallback]()
    {
        // Create temp download file
        juce::File tempFile = cacheDirectory.getChildFile(kitInfo.id + ".zip.tmp");

        // Download
        bool success = downloadFile(kitInfo.downloadUrl, tempFile, progressCallback);

        if (!success)
        {
            if (completionCallback)
                completionCallback(false, "Download failed");
            return;
        }

        // Verify checksum
        if (!verifyChecksum(tempFile, kitInfo.checksum))
        {
            tempFile.deleteFile();
            if (completionCallback)
                completionCallback(false, "Checksum verification failed");
            return;
        }

        // Extract ZIP
        juce::File extractDir = localKitDirectory.getChildFile(kitInfo.id);
        extractDir.createDirectory();

        juce::ZipFile zipFile(tempFile);
        if (zipFile.uncompressTo(extractDir).failed())
        {
            tempFile.deleteFile();
            if (completionCallback)
                completionCallback(false, "Extraction failed");
            return;
        }

        // Cleanup
        tempFile.deleteFile();

        if (completionCallback)
            completionCallback(true, "");
    });
}

bool RepositoryManager::downloadFile(
    const juce::URL& url,
    const juce::File& destination,
    std::function<void(float)> progressCallback)
{
    juce::URL::InputStreamOptions options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(30000)
        .withNumRedirectsToFollow(5);

    std::unique_ptr<juce::InputStream> stream(url.createInputStream(options));

    if (stream == nullptr)
        return false;

    juce::FileOutputStream output(destination);

    if (!output.openedOk())
        return false;

    const int64 totalBytes = stream->getTotalLength();
    int64 bytesRead = 0;
    const int bufferSize = 32768;
    juce::MemoryBlock buffer(bufferSize);

    while (!stream->isExhausted())
    {
        int bytesThisTime = stream->read(buffer.getData(), bufferSize);

        if (bytesThisTime > 0)
        {
            output.write(buffer.getData(), bytesThisTime);
            bytesRead += bytesThisTime;

            if (progressCallback && totalBytes > 0)
                progressCallback(static_cast<float>(bytesRead) / totalBytes);
        }
    }

    output.flush();

    return true;
}

bool RepositoryManager::verifyChecksum(const juce::File& file, const juce::String& expectedChecksum)
{
    // Parse checksum format: "sha256:abc123..."
    if (!expectedChecksum.startsWith("sha256:"))
        return false;

    juce::String expectedHash = expectedChecksum.substring(7);

    // Calculate SHA256
    juce::FileInputStream stream(file);
    if (!stream.openedOk())
        return false;

    juce::SHA256 sha256;
    const int bufferSize = 32768;
    juce::MemoryBlock buffer(bufferSize);

    while (!stream.isExhausted())
    {
        int bytesRead = stream.read(buffer.getData(), bufferSize);
        if (bytesRead > 0)
            sha256.process(buffer.getData(), bytesRead);
    }

    juce::String calculatedHash = sha256.toHexString();

    return calculatedHash.equalsIgnoreCase(expectedHash);
}
```

### UI Integration

```cpp
// Source/UI/RepositoryBrowser.h
class RepositoryBrowser : public juce::Component
{
public:
    RepositoryBrowser(RepositoryManager& repoManager);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void refreshKitList();
    void onKitSelected(const juce::String& kitId);
    void onDownloadClicked(const juce::String& kitId);

    RepositoryManager& repositoryManager;

    std::unique_ptr<juce::ListBox> kitListBox;
    std::unique_ptr<juce::TextEditor> searchBox;
    std::unique_ptr<juce::TextButton> refreshButton;
    std::unique_ptr<juce::ProgressBar> downloadProgress;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RepositoryBrowser)
};
```

---

## Configuration File Format

Store repository configuration in `~/.flamkit/repositories.json`:

```json
{
  "repositories": [
    {
      "id": "official",
      "url": "https://kits.flamkit.org",
      "enabled": true
    },
    {
      "id": "community-rock",
      "url": "https://rockkits.example.com/flamkit",
      "enabled": true
    }
  ],
  "settings": {
    "autoCheckUpdates": true,
    "downloadDirectory": "~/Documents/FlamKit/Kits"
  }
}
```

---

## Security Considerations

1. **HTTPS Only**
   - Reject HTTP repository URLs
   - Verify SSL certificates (JUCE handles this)

2. **Checksum Verification**
   - Always verify SHA256 checksums after download
   - Reject kits with mismatched checksums

3. **Sandboxing**
   - Extract kits to dedicated directory
   - Validate kit structure before loading

4. **User Consent**
   - Require explicit user action for downloads
   - Show repository source before downloading

---

## Testing Requirements

1. **Repository Operations**
   - Add/remove/enable/disable repositories
   - Sync with mock repository server
   - Handle network failures gracefully

2. **Download Operations**
   - Download small kit (100MB)
   - Download large kit (2GB+)
   - Resume interrupted download
   - Verify checksum validation works

3. **UI Flow**
   - Browse kits from multiple repositories
   - Search/filter kits
   - Download progress indication
   - Install and load downloaded kit

4. **Edge Cases**
   - Offline mode (use cached indices)
   - Corrupted download (checksum fail)
   - Repository server down
   - Conflicting kit IDs from different repos

---

## User Experience

### v1.0 (Website Download)

1. User visits flamkit.org/kits
2. Browses available kits
3. Downloads ZIP file
4. Manually extracts to Documents/FlamKit/Kits
5. Opens plugin, clicks "Load Kit", selects flamkit.yaml

### v1.1 (In-Plugin Browser)

1. User opens FlamKit plugin
2. Clicks "Browse Kits" button
3. Sees list of available kits from all enabled repositories
4. Filters by tags (e.g., "rock", "8-channel")
5. Clicks kit thumbnail to preview metadata
6. Clicks "Download" button
7. Progress bar shows download/extraction progress
8. On completion, kit appears in "Installed" tab
9. Double-click kit to load instantly

---

## Future Enhancements (Post-v1.1)

- Kit ratings and reviews
- Dependency management (e.g., shared IR files)
- Differential updates (only download changed samples)
- Torrent/P2P distribution for large kits
- Community kit submission workflow
- Kit versioning and rollback
