# Velocity Mapping & Round-Robin Feature Specification

**Status:** v1.0 MVP Core Feature
**Priority:** Critical
**Dependencies:** `FlamKitLoader`, `VoiceManager`

---

## Overview

Implement sophisticated velocity layer selection and round-robin sample cycling for realistic drum playback. Map incoming MIDI velocity (0-127) to appropriate sample layers, select round-robin variants to eliminate "machine gun" effect, and apply humanization (timing/velocity variation) for natural feel.

---

## Technical Requirements

### Core Functionality

1. **Velocity Layer Mapping**
   - Parse velocity ranges from `flamkit.yaml` (e.g., [0-40], [41-80], [81-127])
   - Map MIDI velocity to appropriate sample layer using binary search
   - Support unlimited velocity layers per drum piece
   - Crossfade between adjacent layers for smooth transitions (future enhancement)

2. **Round-Robin Sample Cycling**
   - Rotate through round-robin samples within selected velocity layer
   - Support unlimited round-robins per layer
   - Multiple round-robin modes:
     - **Sequential**: Cycle 1 → 2 → 3 → 1
     - **Random**: Pick random sample each time
     - **True Random**: Random without immediate repeats
     - **Velocity-Weighted**: Prefer certain samples based on velocity micro-variations

3. **Humanization**
   - **Timing Variation**: ±5-50ms random offset per note
   - **Velocity Variation**: ±1-10 MIDI velocity units
   - **Round-Robin Randomness**: Occasional "off-sequence" selection
   - User-adjustable humanization intensity (0-100%)

4. **Sample Preloading**
   - Preload all samples for currently loaded kit
   - Organize samples by piece → velocity layer → round-robin
   - Integrate with `HybridStreamingLoader` for memory efficiency

---

## Implementation Details

### Data Structures

```cpp
// Source/Core/VelocityMapper.h
class VelocityMapper
{
public:
    VelocityMapper();

    // Load kit structure
    void loadKit(const KitMetadata& metadata);

    // Get sample for playback (audio thread)
    const SampleInfo* getSampleForNote(
        int midiNote,
        int midiVelocity,
        float humanizationAmount
    );

    // Round-robin configuration
    enum class RoundRobinMode
    {
        Sequential,
        Random,
        TrueRandom,
        VelocityWeighted
    };

    void setRoundRobinMode(RoundRobinMode mode);

    // Humanization settings
    void setHumanizationAmount(float amount);  // 0.0 to 1.0
    void setTimingVariationMs(float maxMs);
    void setVelocityVariation(int maxUnits);

private:
    struct VelocityLayer
    {
        int minVelocity;
        int maxVelocity;
        std::vector<SampleInfo> roundRobins;
        int currentRoundRobinIndex{0};
        int previousRoundRobinIndex{-1};  // For TrueRandom mode
    };

    struct DrumPiece
    {
        int midiNote;
        juce::String name;
        std::vector<VelocityLayer> velocityLayers;
    };

    struct SampleInfo
    {
        juce::File filePath;
        int velocityLayer;
        int roundRobinIndex;
        const AudioBuffer<float>* preloadedData;  // Pointer to HybridStreamingLoader data
    };

    std::map<int, DrumPiece> pieces;  // Keyed by MIDI note

    // Round-robin state
    RoundRobinMode roundRobinMode = RoundRobinMode::Sequential;

    // Humanization parameters
    std::atomic<float> humanizationAmount{0.3f};  // Default 30%
    std::atomic<float> timingVariationMs{10.0f};
    std::atomic<int> velocityVariation{5};

    // Random number generator (lock-free)
    juce::Random random;

    // Helper functions
    int findVelocityLayer(const DrumPiece& piece, int velocity) const;
    int selectRoundRobin(VelocityLayer& layer, int originalVelocity);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VelocityMapper)
};
```

### Kit Loading

```cpp
void VelocityMapper::loadKit(const KitMetadata& metadata)
{
    pieces.clear();

    for (const auto& pieceMetadata : metadata.pieces)
    {
        DrumPiece piece;
        piece.midiNote = pieceMetadata.midiNote;
        piece.name = pieceMetadata.name;

        // Parse velocity layers
        for (const auto& velLayerMetadata : pieceMetadata.velocityLayers)
        {
            VelocityLayer layer;
            layer.minVelocity = velLayerMetadata.minVelocity;
            layer.maxVelocity = velLayerMetadata.maxVelocity;

            // Parse round-robin samples
            for (const auto& rrMetadata : velLayerMetadata.roundRobins)
            {
                SampleInfo sampleInfo;
                sampleInfo.filePath = rrMetadata.filePath;
                sampleInfo.velocityLayer = piece.velocityLayers.size();
                sampleInfo.roundRobinIndex = layer.roundRobins.size();
                sampleInfo.preloadedData = nullptr;  // Will be set by HybridStreamingLoader

                layer.roundRobins.push_back(sampleInfo);
            }

            piece.velocityLayers.push_back(std::move(layer));
        }

        pieces[piece.midiNote] = std::move(piece);
    }
}
```

### Sample Selection (Audio Thread)

```cpp
const SampleInfo* VelocityMapper::getSampleForNote(
    int midiNote,
    int midiVelocity,
    float humanizationAmount)
{
    // Find drum piece
    auto pieceIt = pieces.find(midiNote);
    if (pieceIt == pieces.end())
        return nullptr;

    DrumPiece& piece = pieceIt->second;

    // Apply velocity humanization
    int adjustedVelocity = midiVelocity;
    if (humanizationAmount > 0.0f)
    {
        const int variation = static_cast<int>(velocityVariation.load() * humanizationAmount);
        const int randomOffset = random.nextInt(variation * 2 + 1) - variation;
        adjustedVelocity = juce::jlimit(0, 127, midiVelocity + randomOffset);
    }

    // Find appropriate velocity layer
    int layerIndex = findVelocityLayer(piece, adjustedVelocity);
    if (layerIndex == -1)
        return nullptr;

    VelocityLayer& layer = piece.velocityLayers[layerIndex];

    if (layer.roundRobins.empty())
        return nullptr;

    // Select round-robin sample
    int rrIndex = selectRoundRobin(layer, adjustedVelocity);

    return &layer.roundRobins[rrIndex];
}

int VelocityMapper::findVelocityLayer(const DrumPiece& piece, int velocity) const
{
    // Binary search for velocity layer
    for (size_t i = 0; i < piece.velocityLayers.size(); ++i)
    {
        const auto& layer = piece.velocityLayers[i];
        if (velocity >= layer.minVelocity && velocity <= layer.maxVelocity)
        {
            return static_cast<int>(i);
        }
    }

    return -1;  // No matching layer found
}

int VelocityMapper::selectRoundRobin(VelocityLayer& layer, int originalVelocity)
{
    const int numRoundRobins = layer.roundRobins.size();

    if (numRoundRobins == 1)
        return 0;

    switch (roundRobinMode)
    {
        case RoundRobinMode::Sequential:
        {
            int index = layer.currentRoundRobinIndex;
            layer.currentRoundRobinIndex = (layer.currentRoundRobinIndex + 1) % numRoundRobins;
            return index;
        }

        case RoundRobinMode::Random:
        {
            return random.nextInt(numRoundRobins);
        }

        case RoundRobinMode::TrueRandom:
        {
            // Random, but avoid immediate repeats
            int index = random.nextInt(numRoundRobins);

            if (numRoundRobins > 1 && index == layer.previousRoundRobinIndex)
            {
                index = (index + 1) % numRoundRobins;  // Pick next one
            }

            layer.previousRoundRobinIndex = index;
            return index;
        }

        case RoundRobinMode::VelocityWeighted:
        {
            // Use micro-variations in velocity to influence round-robin selection
            // Higher velocities slightly favor later round-robins
            const int velocityMod = (originalVelocity % numRoundRobins);
            const float bias = humanizationAmount.load();

            int baseIndex = layer.currentRoundRobinIndex;
            int targetIndex = static_cast<int>(baseIndex + velocityMod * bias) % numRoundRobins;

            layer.currentRoundRobinIndex = (layer.currentRoundRobinIndex + 1) % numRoundRobins;
            return targetIndex;
        }

        default:
            return 0;
    }
}
```

### Humanization: Timing Variation

Timing variation is handled in `VoiceManager` when triggering voices:

```cpp
// Source/Core/VoiceManager.cpp
void VoiceManager::handleNoteOn(int midiNote, int midiVelocity, int timestamp)
{
    // Get sample info from velocity mapper
    const SampleInfo* sampleInfo = velocityMapper->getSampleForNote(
        midiNote,
        midiVelocity,
        humanizationAmount
    );

    if (sampleInfo == nullptr)
        return;

    // Apply timing humanization
    int adjustedTimestamp = timestamp;
    if (humanizationAmount > 0.0f)
    {
        const float maxVariationMs = velocityMapper->getTimingVariationMs();
        const float variationSamples = (maxVariationMs / 1000.0f) * sampleRate * humanizationAmount;
        const int randomOffset = random.nextInt(static_cast<int>(variationSamples * 2)) - static_cast<int>(variationSamples);
        adjustedTimestamp = std::max(0, timestamp + randomOffset);
    }

    // Find available voice
    SampleVoice* voice = allocateVoice();
    if (voice == nullptr)
        return;  // No available voices (voice stealing would go here)

    // Start voice
    voice->startNote(sampleInfo, midiVelocity, adjustedTimestamp);
}
```

---

## Kit Metadata Format (flamkit.yaml)

```yaml
pieces:
  - name: Kick
    midiNote: 36
    velocityLayers:
      - velocityRange: [0, 40]
        roundRobins:
          - file: Samples/Kick/kick_v001_rr0.wav
          - file: Samples/Kick/kick_v001_rr1.wav
          - file: Samples/Kick/kick_v001_rr2.wav
      - velocityRange: [41, 80]
        roundRobins:
          - file: Samples/Kick/kick_v064_rr0.wav
          - file: Samples/Kick/kick_v064_rr1.wav
      - velocityRange: [81, 127]
        roundRobins:
          - file: Samples/Kick/kick_v127_rr0.wav
          - file: Samples/Kick/kick_v127_rr1.wav
          - file: Samples/Kick/kick_v127_rr2.wav
          - file: Samples/Kick/kick_v127_rr3.wav
```

---

## Testing Requirements

1. **Velocity Layer Selection**
   - Load kit with multiple velocity layers
   - Send MIDI notes at velocities 1, 64, 127
   - Verify correct samples triggered for each velocity

2. **Round-Robin Cycling**
   - Trigger same note 10 times at same velocity
   - **Sequential mode**: Verify samples cycle 0→1→2→0
   - **Random mode**: Verify varied selection
   - **TrueRandom mode**: Verify no immediate repeats

3. **Humanization**
   - Set humanization to 50%
   - Trigger 100 notes at velocity 64
   - Measure actual triggered velocities → verify spread around 64
   - Measure timing offsets → verify variation within expected range

4. **Edge Cases**
   - MIDI velocity 0 (should map to layer 1)
   - MIDI velocity 127 (should map to highest layer)
   - Single velocity layer → verify always selected
   - Single round-robin → verify always selected
   - Gaps in velocity coverage (e.g., [0-40], [60-127]) → handle gracefully

---

## Real-Time Safety Constraints

- **Audio Thread:**
  - ✅ `getSampleForNote()` - lock-free, allocation-free
  - ✅ `findVelocityLayer()` - simple array lookup
  - ✅ `selectRoundRobin()` - atomic reads/writes, lock-free RNG
  - ❌ **No allocations, locks, or blocking operations**

- **Non-Audio Thread:**
  - `loadKit()` - can allocate, rebuild data structures
  - `setRoundRobinMode()` - atomic write

---

## User-Adjustable Parameters (UI)

```cpp
// Parameters exposed to DAW for automation
- Humanization Amount: 0-100% (default 30%)
- Timing Variation: 0-50ms (default 10ms)
- Velocity Variation: 0-20 units (default 5)
- Round-Robin Mode: Sequential / Random / TrueRandom / VelocityWeighted
```

---

## Performance Optimizations

1. **Velocity Layer Lookup**
   - Use binary search for kits with many velocity layers (>10)
   - Cache last-used layer per piece (temporal locality)

2. **Round-Robin Indexing**
   - Store round-robin indices per piece (avoid global state)
   - Use lock-free atomics for thread safety

3. **Random Number Generation**
   - Use fast PRNG (juce::Random) instead of std::random
   - Pre-generate random values if needed (future optimization)

---

## Future Enhancements (Post-v1.0)

- Velocity curve editor (remap MIDI velocity to custom curve)
- Crossfade between adjacent velocity layers
- Round-robin "reset" on time gap (reset sequence after 1 second of silence)
- Per-piece humanization settings
- Machine learning velocity prediction (analyze drummer's patterns)
