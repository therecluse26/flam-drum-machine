#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <atomic>
#include <queue>

namespace flam {

struct SampleLayer;
class SampleVoice;

/**
 * Background thread for loading audio samples asynchronously.
 * Ensures real-time audio thread is never blocked by I/O operations.
 */
class SampleLoader : private juce::Thread
{
public:
    SampleLoader();
    ~SampleLoader() override;

    /**
     * Start the background loading thread
     */
    void start();

    /**
     * Stop the background loading thread and wait for completion
     */
    void stop();

    /**
     * Queue a sample for loading in the background
     * @param layer Sample layer containing the file path
     * @param targetVoice Voice that will receive the loaded sample
     */
    void queueSampleLoad(const SampleLayer* layer, SampleVoice* targetVoice);

    /**
     * Load all samples for a drum kit (called from background thread)
     * @param kitDirectory Base directory for the kit
     * @param layers Vector of sample layers to load
     */
    void loadKitSamples(const juce::File& kitDirectory,
                       const std::vector<SampleLayer>& layers);

    /**
     * Check if loading is currently in progress
     */
    bool isLoading() const { return loading.load(); }

    /**
     * Get the number of samples in the load queue
     */
    int getQueueSize() const;

private:
    struct LoadRequest
    {
        juce::File sampleFile;
        SampleVoice* targetVoice{nullptr};
        double sourceSampleRate{44100.0};
    };

    juce::CriticalSection queueLock;
    std::queue<LoadRequest> loadQueue;

    std::atomic<bool> loading{false};
    juce::AudioFormatManager formatManager;

    // Thread implementation
    void run() override;

    // Load a single sample file
    std::shared_ptr<juce::AudioBuffer<float>> loadSampleFile(const juce::File& file,
                                                              double& outSampleRate);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleLoader)
};

} // namespace flam
