#include "SampleLoader.h"
#include "SampleVoice.h"
#include "../Formats/FlamKitLoader.h"

namespace flam {

SampleLoader::SampleLoader()
    : juce::Thread("FlamSampleLoader")
{
    formatManager.registerBasicFormats();
}

SampleLoader::~SampleLoader()
{
    stop();
}

void SampleLoader::start()
{
    startThread(juce::Thread::Priority::normal);
}

void SampleLoader::stop()
{
    signalThreadShouldExit();
    notify();
    stopThread(5000); // Wait up to 5 seconds for thread to exit

    // Clear any remaining queue items
    const juce::ScopedLock lock(queueLock);
    while (!loadQueue.empty())
        loadQueue.pop();
}

void SampleLoader::queueSampleLoad(const SampleLayer* layer, SampleVoice* targetVoice)
{
    if (!layer || !targetVoice)
        return;

    LoadRequest request;
    request.sampleFile = layer->sampleFile;
    request.targetVoice = targetVoice;
    request.sourceSampleRate = 44100.0; // Default, will be updated from file

    {
        const juce::ScopedLock lock(queueLock);
        loadQueue.push(request);
    }

    notify();
}

void SampleLoader::loadKitSamples(const juce::File& kitDirectory,
                                  const std::vector<SampleLayer>& layers)
{
    juce::ignoreUnused(kitDirectory);

    for (const auto& layer : layers)
    {
        if (threadShouldExit())
            break;

        // Queue each layer for loading
        // In a full implementation, we'd batch these and track progress
        LoadRequest request;
        request.sampleFile = layer.sampleFile;
        request.targetVoice = nullptr; // Will be assigned when voices are allocated
        request.sourceSampleRate = 44100.0;

        {
            const juce::ScopedLock lock(queueLock);
            loadQueue.push(request);
        }
    }
}

int SampleLoader::getQueueSize() const
{
    const juce::ScopedLock lock(queueLock);
    return static_cast<int>(loadQueue.size());
}

void SampleLoader::run()
{
    while (!threadShouldExit())
    {
        LoadRequest request;
        bool hasRequest = false;

        {
            const juce::ScopedLock lock(queueLock);
            if (!loadQueue.empty())
            {
                request = loadQueue.front();
                loadQueue.pop();
                hasRequest = true;
            }
        }

        if (hasRequest)
        {
            loading.store(true);

            // Load the sample file from disk
            double sampleRate = 44100.0;
            auto buffer = loadSampleFile(request.sampleFile, sampleRate);

            if (buffer && request.targetVoice)
            {
                // Pass the loaded buffer to the target voice (thread-safe atomic operation)
                request.targetVoice->loadSampleData(buffer, sampleRate);
            }

            loading.store(false);
        }
        else
        {
            // No work to do, wait for notification
            loading.store(false);
            wait(500); // Wake up every 500ms to check if thread should exit
        }
    }
}

std::shared_ptr<juce::AudioBuffer<float>> SampleLoader::loadSampleFile(
    const juce::File& file, double& outSampleRate)
{
    if (!file.existsAsFile())
    {
        juce::Logger::writeToLog("Sample file not found: " + file.getFullPathName());
        return nullptr;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

    if (!reader)
    {
        juce::Logger::writeToLog("Could not create reader for: " + file.getFullPathName());
        return nullptr;
    }

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);

    outSampleRate = reader->sampleRate;

    // Allocate buffer and read samples
    auto buffer = std::make_shared<juce::AudioBuffer<float>>(numChannels, numSamples);

    if (!reader->read(buffer.get(), 0, numSamples, 0, true, true))
    {
        juce::Logger::writeToLog("Failed to read samples from: " + file.getFullPathName());
        return nullptr;
    }

    juce::Logger::writeToLog("Loaded sample: " + file.getFileName() +
                            " (" + juce::String(numSamples) + " samples, " +
                            juce::String(outSampleRate) + " Hz)");

    return buffer;
}

} // namespace flam
