# FlamForge: Kit Recording Tool - Feature Specification

**Status:** v1.1 Feature (Basic), v1.2 (Advanced)
**Priority:** High
**Dependencies:** Audio I/O, `FlamKitLoader` (for YAML generation)

---

## Overview

FlamForge is a standalone application that guides users through recording professional multi-channel drum kits. It uses real-time velocity detection and visual feedback (inspired by fingerprint registration UX) to ensure comprehensive sampling across the dynamic range. Output is a ready-to-use `flamkit.yaml` file with organized multi-channel WAV samples.

---

## Technical Requirements

### Core Functionality (v1.0)

1. **Audio Interface Configuration**
   - Detect available audio interfaces
   - Select input channels (1-16)
   - Configure sample rate (44.1/48 kHz)
   - Set bit depth (24-bit)
   - Monitor input levels with peak meters

2. **Microphone Setup Wizard**
   - Visual guides showing optimal mic placement for each drum piece
   - Channel assignment UI (map interface inputs to mic positions)
   - **Microphone naming**: User assigns name to each input channel (e.g., "Kick Close", "OH-L", "Room-R")
   - These names are stored in `flamkit.yaml` and used for mixer channel labels in FlamKit
   - Test recording to verify all channels working

3. **Dynamic Range Calibration**
   - User strikes drum at softest audible level → record minimum amplitude
   - User strikes drum at hardest level → record maximum amplitude
   - Calculate velocity mapping curve (amplitude → MIDI 0-127)

4. **Velocity Mapping Recording**
   - Real-time amplitude analysis of incoming hits
   - Map each hit to velocity bin (0-127) using calibration curve
   - Visual "completeness meter" showing coverage of velocity spectrum
   - Color coding: Red (0-1 samples), Yellow (2-5), Green (6+)

5. **Per-Drum Workflow**
   - Iterate through drum pieces: Kick → Snare → Toms → Hi-Hat → Cymbals
   - For each piece:
     - Calibrate dynamic range
     - Record hits until sufficient coverage
     - Move to next piece

6. **Automatic Export**
   - Generate `flamkit.yaml` with metadata
   - Organize samples into folder structure
   - Name files: `{piece}_v{velocity}_rr{roundrobin}.wav`
   - Each WAV file contains N channels (one per mic)

### Advanced Features (v1.1)

- Articulation switching (center, edge, rim, choke)
- Choke group configuration
- Mic bleed recording and analysis
- Preview/playback of recorded samples
- Built-in test mode (load kit in FlamForge before export)

---

## Implementation Details

### Class Structure

```cpp
// Source/FlamForge/RecordingEngine.h
class RecordingEngine : public juce::AudioIODeviceCallback
{
public:
    RecordingEngine();

    // Audio device management
    void setAudioDevice(juce::AudioIODevice* device);
    void startMonitoring();
    void stopMonitoring();

    // Dynamic range calibration
    void startCalibration(CalibrationPhase phase);  // Softest or Hardest
    void finishCalibration();
    std::pair<float, float> getCalibrationRange() const;  // min, max amplitude

    // Recording session
    void startRecording(const DrumPiece& piece);
    void stopRecording();
    bool isRecording() const;

    // Velocity analysis
    int getCurrentVelocityBin() const;  // Real-time MIDI velocity (0-127)
    std::vector<int> getVelocityCoverage() const;  // Sample count per bin

    // Sample storage
    std::vector<RecordedSample> getRecordedSamples() const;

    // Export
    bool exportKit(const juce::File& outputDirectory, const KitMetadata& metadata);

private:
    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // Hit detection
    struct HitDetector
    {
        float threshold = 0.01f;  // Onset detection threshold
        float previousPeak = 0.0f;
        int samplesSincePeak = 0;
        static constexpr int COOLDOWN_SAMPLES = 2205;  // 50ms @ 44.1kHz

        bool detectHit(float currentPeak);
    };

    HitDetector hitDetector;

    // Calibration
    enum class CalibrationPhase { Softest, Hardest, Complete };
    CalibrationPhase calibrationPhase = CalibrationPhase::Complete;
    float calibrationMin = 0.0f;
    float calibrationMax = 1.0f;

    // Recording state
    std::atomic<bool> recording{false};
    DrumPiece currentPiece;
    int numChannels = 0;
    int sampleRate = 44100;
    std::vector<juce::String> microphoneChannelNames;  // User-defined names for each mic channel

    // Recorded samples
    struct RecordedSample
    {
        AudioBuffer<float> multiChannelBuffer;
        float amplitude;
        int velocityBin;
        juce::Time timestamp;
    };

    std::vector<RecordedSample> recordedSamples;
    juce::CriticalSection sampleLock;

    // Temporary recording buffer
    AudioBuffer<float> tempRecordingBuffer;
    std::atomic<int> tempBufferWritePos{0};
    static constexpr int TEMP_BUFFER_SIZE = 88200;  // 2 seconds @ 44.1kHz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingEngine)
};
```

### Audio Callback: Hit Detection & Recording

```cpp
void RecordingEngine::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData,
    int numInputChannels,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext& context)
{
    // Calculate peak amplitude across all channels
    float peakAmplitude = 0.0f;
    for (int ch = 0; ch < numInputChannels; ++ch)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            peakAmplitude = std::max(peakAmplitude, std::abs(inputChannelData[ch][i]));
        }
    }

    // Calibration mode
    if (calibrationPhase == CalibrationPhase::Softest)
    {
        if (hitDetector.detectHit(peakAmplitude))
        {
            calibrationMin = std::max(0.001f, peakAmplitude * 0.9f);  // 10% margin
        }
        return;
    }
    else if (calibrationPhase == CalibrationPhase::Hardest)
    {
        if (hitDetector.detectHit(peakAmplitude))
        {
            calibrationMax = std::min(1.0f, peakAmplitude * 1.1f);  // 10% margin
        }
        return;
    }

    // Recording mode
    if (!recording.load())
        return;

    // Detect hit
    if (hitDetector.detectHit(peakAmplitude))
    {
        // Map amplitude to velocity (0-127)
        float normalizedAmp = (peakAmplitude - calibrationMin) / (calibrationMax - calibrationMin);
        normalizedAmp = juce::jlimit(0.0f, 1.0f, normalizedAmp);
        int velocity = static_cast<int>(normalizedAmp * 127.0f);

        // Start recording this hit
        tempBufferWritePos.store(0);

        // Copy incoming audio to temp buffer
        for (int ch = 0; ch < numInputChannels; ++ch)
        {
            tempRecordingBuffer.copyFrom(ch, 0, inputChannelData[ch], numSamples);
        }

        tempBufferWritePos.store(numSamples);

        // In real implementation, would continue recording for ~500ms to capture full decay
        // For now, just store what we have
        RecordedSample sample;
        sample.multiChannelBuffer = AudioBuffer<float>(numInputChannels, numSamples);
        for (int ch = 0; ch < numInputChannels; ++ch)
        {
            sample.multiChannelBuffer.copyFrom(ch, 0, inputChannelData[ch], numSamples);
        }
        sample.amplitude = peakAmplitude;
        sample.velocityBin = velocity;
        sample.timestamp = juce::Time::getCurrentTime();

        {
            juce::ScopedLock lock(sampleLock);
            recordedSamples.push_back(std::move(sample));
        }
    }
    else if (tempBufferWritePos.load() > 0)
    {
        // Continue recording after hit detected (capture decay tail)
        int writePos = tempBufferWritePos.load();

        if (writePos + numSamples < TEMP_BUFFER_SIZE)
        {
            for (int ch = 0; ch < numInputChannels; ++ch)
            {
                tempRecordingBuffer.copyFrom(ch, writePos, inputChannelData[ch], numSamples);
            }

            tempBufferWritePos.store(writePos + numSamples);
        }
        else
        {
            // Finished recording this hit (reached max duration)
            tempBufferWritePos.store(0);

            // Move temp buffer to recorded samples
            // (In real implementation, trim silence from end)
        }
    }
}

bool RecordingEngine::HitDetector::detectHit(float currentPeak)
{
    samplesSincePeak++;

    bool hitDetected = false;

    // Onset detection: rising edge above threshold
    if (currentPeak > threshold && currentPeak > previousPeak * 1.5f)
    {
        if (samplesSincePeak > COOLDOWN_SAMPLES)
        {
            hitDetected = true;
            samplesSincePeak = 0;
        }
    }

    previousPeak = currentPeak;

    return hitDetected;
}
```

### Velocity Coverage Calculation

```cpp
std::vector<int> RecordingEngine::getVelocityCoverage() const
{
    std::vector<int> coverage(128, 0);  // Count per velocity bin

    juce::ScopedLock lock(sampleLock);
    for (const auto& sample : recordedSamples)
    {
        if (sample.velocityBin >= 0 && sample.velocityBin < 128)
        {
            coverage[sample.velocityBin]++;
        }
    }

    return coverage;
}
```

### Export: Generate flamkit.yaml and Samples

```cpp
bool RecordingEngine::exportKit(const juce::File& outputDirectory, const KitMetadata& metadata)
{
    if (!outputDirectory.exists())
        outputDirectory.createDirectory();

    // Create Samples subdirectory
    juce::File samplesDir = outputDirectory.getChildFile("Samples");
    samplesDir.createDirectory();

    // Organize samples by velocity bin, assign round-robin indices
    std::map<int, std::vector<RecordedSample*>> samplesByVelocity;

    for (auto& sample : recordedSamples)
    {
        samplesByVelocity[sample.velocityBin].push_back(&sample);
    }

    // Write WAV files
    juce::WavAudioFormat wavFormat;
    int globalSampleIndex = 0;

    for (auto& [velocity, samples] : samplesByVelocity)
    {
        for (size_t rrIndex = 0; rrIndex < samples.size(); ++rrIndex)
        {
            RecordedSample* sample = samples[rrIndex];

            juce::String filename = juce::String::formatted(
                "%s_v%03d_rr%d.wav",
                currentPiece.name.toRawUTF8(),
                velocity,
                static_cast<int>(rrIndex)
            );

            juce::File outputFile = samplesDir
                .getChildFile(currentPiece.name)
                .getChildFile(filename);

            outputFile.getParentDirectory().createDirectory();

            std::unique_ptr<juce::FileOutputStream> stream(outputFile.createOutputStream());
            if (stream == nullptr)
                return false;

            std::unique_ptr<juce::AudioFormatWriter> writer(
                wavFormat.createWriterFor(
                    stream.get(),
                    sampleRate,
                    sample->multiChannelBuffer.getNumChannels(),
                    24,  // 24-bit
                    {},
                    0
                )
            );

            if (writer == nullptr)
                return false;

            stream.release();  // Writer takes ownership

            writer->writeFromAudioSampleBuffer(
                sample->multiChannelBuffer,
                0,
                sample->multiChannelBuffer.getNumSamples()
            );
        }
    }

    // Generate flamkit.yaml
    juce::File yamlFile = outputDirectory.getChildFile("flamkit.yaml");
    return generateYAML(yamlFile, metadata, samplesByVelocity);
}

bool RecordingEngine::generateYAML(
    const juce::File& yamlFile,
    const KitMetadata& metadata,
    const std::map<int, std::vector<RecordedSample*>>& samplesByVelocity)
{
    juce::String yaml;

    yaml << "name: " << metadata.name << "\n";
    yaml << "version: 1.0.0\n";
    yaml << "sampleRate: " << sampleRate << "\n";
    yaml << "\n# Microphone channel names (one per channel in WAV files)\n";
    yaml << "channels:\n";

    for (size_t i = 0; i < microphoneChannelNames.size(); ++i)
    {
        yaml << "  - name: \"" << microphoneChannelNames[i] << "\"\n";
        yaml << "    index: " << i << "\n";
    }

    yaml << "\npieces:\n";
    yaml << "  - name: " << currentPiece.name << "\n";
    yaml << "    midiNote: " << currentPiece.midiNote << "\n";
    yaml << "    velocityLayers:\n";

    for (const auto& [velocity, samples] : samplesByVelocity)
    {
        yaml << "      - velocityRange: [" << velocity << ", " << (velocity + 1) << "]\n";
        yaml << "        roundRobins:\n";

        for (size_t rrIndex = 0; rrIndex < samples.size(); ++rrIndex)
        {
            juce::String filename = juce::String::formatted(
                "Samples/%s/%s_v%03d_rr%d.wav",
                currentPiece.name.toRawUTF8(),
                currentPiece.name.toRawUTF8(),
                velocity,
                static_cast<int>(rrIndex)
            );

            yaml << "          - file: " << filename << "\n";
        }
    }

    return yamlFile.replaceWithText(yaml);
}
```

---

## UI Design

### Main Window Layout

```
┌─────────────────────────────────────────────────┐
│  FlamForge - Kit Recording Tool                │
├─────────────────────────────────────────────────┤
│                                                  │
│  Step 1/6: Audio Setup                          │
│                                                  │
│  Audio Interface: [Focusrite Scarlett 18i20 ▼] │
│  Sample Rate:     [44.1 kHz ▼]                  │
│  Input Channels:  [8]                           │
│                                                  │
│  Channel Mapping & Naming:                       │
│    Input 1: [Kick Close      ] [Custom Name...] │
│    Input 2: [Kick Sub        ] [Custom Name...] │
│    Input 3: [Snare Top       ] [Custom Name...] │
│    Input 4: [Snare Bottom    ] [Custom Name...] │
│    Input 5: [Hi-Hat Close    ] [Custom Name...] │
│    Input 6: [OH-L            ] [Custom Name...] │
│    Input 7: [OH-R            ] [Custom Name...] │
│    Input 8: [Room            ] [Custom Name...] │
│                                                  │
│  ℹ️ These names will label mixer channels        │
│     in FlamKit when this kit is loaded.         │
│                                                  │
│  [Test Input] [Next Step →]                     │
└─────────────────────────────────────────────────┘
```

### Recording Session UI

```
┌─────────────────────────────────────────────────┐
│  Recording: Kick Drum                           │
├─────────────────────────────────────────────────┤
│                                                  │
│  Velocity Coverage Meter (0-127):               │
│  ████████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │
│  [Green: 6+] [Yellow: 2-5] [Red: 0-1]          │
│                                                  │
│  Current Velocity: 87                           │
│  Samples Recorded: 124                          │
│                                                  │
│  Instructions:                                   │
│  Strike the kick drum at varying velocities.    │
│  Try to fill all velocity ranges (0-127).       │
│                                                  │
│  [Pause] [Next Drum →]                          │
└─────────────────────────────────────────────────┘
```

---

## Testing Requirements

1. **Audio I/O**
   - Test with various audio interfaces (USB, Thunderbolt, PCIe)
   - Verify all input channels record correctly
   - Test latency/sync across channels

2. **Hit Detection**
   - Test with quiet hits (threshold sensitivity)
   - Test with rapid hits (cooldown period)
   - Test with different drum pieces (varied attack characteristics)

3. **Velocity Mapping**
   - Record known soft/medium/hard hits
   - Verify velocity bins match expected values
   - Test calibration accuracy

4. **Export**
   - Verify WAV files contain all channels
   - Verify flamkit.yaml parses correctly in FlamKit
   - Test with 1, 4, 8, 16 channel setups

---

## User Experience Flow

1. **Launch FlamForge**
2. **Audio Setup**: Select interface, configure channels
3. **Mic Placement Guide**: Visual diagrams show where to place mics
4. **Calibrate Kick Drum**:
   - Hit softest → system records minimum
   - Hit hardest → system records maximum
5. **Record Kick Samples**:
   - Strike kick at varying velocities
   - Watch completeness meter fill up
   - Green bars = good coverage, red bars = need more samples
6. **Repeat for Each Drum**: Snare, toms, hi-hat, cymbals
7. **Export Kit**:
   - Enter kit name and metadata
   - Click "Export"
   - FlamForge generates flamkit.yaml and organized samples
8. **Test in FlamKit**: Load kit in FlamKit plugin to verify

---

## Future Enhancements

- Auto-detect drum type from frequency content
- Machine learning velocity prediction (improve calibration)
- Cloud backup/sync of recorded kits
- Collaborative recording sessions (multi-user)
- Integration with FlamKit plugin (one-click import)
