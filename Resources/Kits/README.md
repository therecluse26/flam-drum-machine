# FLAM Kit Format

FLAM uses `.flamkit` files (YAML or JSON) to define drum kits. This format is human-readable, version-controllable, and easy to create or modify.

## File Structure

```yaml
name: Kit Name
author: Your Name
version: 1.0.0
description: Kit description

settings:
  masterGain: 1.0
  maxPolyphony: 64
  useRoundRobin: true
  defaultHumanization: 0.0

pieces:
  - name: Kick
    midiNote: 36
    articulations:
      - name: Hit
        chokeGroup: -1
        attackTime: 0.001
        holdTime: 0.0
        decayTime: 0.5
        sustainLevel: 0.0
        releaseTime: 0.1
        layers:
          - sampleFile: Samples/Kick/kick_soft.wav
            velocityMin: 0.0
            velocityMax: 0.4
            gain: 1.0
            roundRobinGroup: 0
```

## Field Descriptions

### Kit Metadata
- `name`: Kit name shown in UI
- `author`: Creator name
- `version`: Semantic version number
- `description`: Kit description

### Settings
- `masterGain`: Overall kit volume (0.0-2.0)
- `maxPolyphony`: Maximum simultaneous voices
- `useRoundRobin`: Enable round-robin sample cycling
- `defaultHumanization`: Default timing/velocity variation (0.0-1.0)

### Drum Pieces
- `name`: Display name for the drum piece
- `midiNote`: MIDI note number (0-127) that triggers this piece

### Articulations
Different playing techniques (center, edge, rim, etc.)

- `name`: Articulation name
- `chokeGroup`: Group ID for mutually exclusive sounds (e.g., open/closed hi-hat). Use -1 for no choking.
- `attackTime`: ADSR attack phase (seconds)
- `holdTime`: ADSR hold phase (seconds)
- `decayTime`: ADSR decay phase (seconds)
- `sustainLevel`: ADSR sustain level (0.0-1.0)
- `releaseTime`: ADSR release phase (seconds)

### Sample Layers
Individual audio files for velocity ranges and round-robin groups

- `sampleFile`: Path to audio file (relative to kit file)
- `velocityMin`: Minimum velocity for this layer (0.0-1.0)
- `velocityMax`: Maximum velocity for this layer (0.0-1.0)
- `gain`: Layer volume multiplier (0.0-2.0)
- `roundRobinGroup`: Round-robin group ID (0-15)

## Sample File Organization

Organize your samples in a logical directory structure:

```
MyKit/
├── MyKit.flamkit
└── Samples/
    ├── Kick/
    │   ├── kick_soft.wav
    │   ├── kick_medium.wav
    │   └── kick_hard.wav
    ├── Snare/
    │   ├── snare_soft_1.wav
    │   ├── snare_soft_2.wav
    │   └── ...
    └── ...
```

## Best Practices

1. **Velocity Layers**: Use 2-4 velocity layers per piece for realistic dynamics
2. **Round Robin**: Provide 2-4 samples per velocity layer to avoid "machine gun" effect
3. **Sample Format**: Use WAV or FLAC, 44.1/48 kHz, 24-bit recommended
4. **Choke Groups**: Assign open/closed hi-hats to the same choke group (e.g., group 0)
5. **Envelope Tuning**: Adjust ADSR to match the natural decay of each drum
6. **Relative Paths**: Always use relative paths for sample files

## MIDI Note Reference (GM Standard)

- 36: Kick
- 38: Snare
- 42: Hi-Hat Closed
- 46: Hi-Hat Open
- 41: Floor Tom
- 43/45: Low/Mid Tom
- 47/48/50: Mid/High Tom
- 49/52/55/57: Crash Cymbals
- 51/53/59: Ride Cymbals

## Example: JSON Format

FLAM also supports JSON for kit definitions:

```json
{
  "name": "Example Kit",
  "author": "FLAM",
  "version": "1.0.0",
  "description": "Example drum kit",
  "settings": {
    "masterGain": 1.0,
    "maxPolyphony": 64,
    "useRoundRobin": true,
    "defaultHumanization": 0.0
  },
  "pieces": [
    {
      "name": "Kick",
      "midiNote": 36,
      "articulations": [
        {
          "name": "Hit",
          "chokeGroup": -1,
          "layers": [
            {
              "sampleFile": "Samples/Kick/kick_soft.wav",
              "velocityMin": 0.0,
              "velocityMax": 0.4,
              "gain": 1.0,
              "roundRobinGroup": 0
            }
          ]
        }
      ]
    }
  ]
}
```
