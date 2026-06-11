# MuldjordKit (FreePats edition)

An acoustic **rock / metal** drum kit — a Tama Superstar kit recorded by **Lars
Muldjord** during the 2009 Sepulchrum sessions and released through the
[DrumGizmo project](https://drumgizmo.org/). This package is the **FreePats
stereo edition** (version 2020-10-18), a 2-channel mixdown of the original
16-channel DrumGizmo kit, assembled for [FreePats](https://freepats.zenvoid.org/)
by Roberto (roberto@zenvoid.org).

It is the second cleared launch kit for FlamKit v1.0, providing genre contrast to
the acoustic-rock/jazz **DRSKit** (`../drs-kit`): MuldjordKit is a heavier,
metal/rock-leaning kit and ships as a lightweight stereo kit (vs. DRSKit's
13-channel multi-mic).

* **License:** Creative Commons Attribution 4.0 International (CC-BY 4.0) —
  attribution **required**. See [`LICENSE.txt`](./LICENSE.txt).
* **Audio:** stereo (2 channels), 44.1 kHz, 24-bit WAV.
* **Source:** https://freepats.zenvoid.org/Percussion/acoustic-drum-kit.html
  · repo: https://github.com/freepats/muldjordkit (CC-BY-4.0)

## Components

2 kickdrums · 1 snare (+ rest / cross-stick hits) · 3 rack toms · 1 floor tom ·
hi-hat (open + closed) · 2 crash cymbals · 2 ride cymbals (+ bells) · 1 china
cymbal. Deep velocity layering (up to 14 layers per piece) with randomized
round-robin, faithfully preserved from the upstream SFZ (777 sample regions).

## MIDI map (General MIDI percussion)

The kit is mapped to the General MIDI drum layout so it plays out-of-the-box with
standard MIDI drum patterns.

| MIDI | Piece                | Source folder | Notes |
|------|----------------------|---------------|-------|
| 35   | Kick 2               | KdrumR        | GM Acoustic Bass Drum (second kick) |
| 36   | Kick                 | KdrumL        | GM Bass Drum 1 (primary kick) |
| 37   | Side Stick           | SnareRest1    | GM Side Stick (snare rest / cross-stick) |
| 38   | Snare                | Snare1        | GM Acoustic Snare (primary) |
| 39   | Snare Rest           | SnareRest2    | bonus articulation — GM 39 slot (Hand Clap) repurposed |
| 40   | Snare 2              | Snare2        | GM Electric Snare (alternate snare) |
| 41   | Floor Tom            | Tom4          | GM Low Floor Tom |
| 42   | Hi-Hat Closed        | HihatClosed   | GM Closed Hi-Hat — choke group 0 |
| 45   | Rack Tom 3 (Low)     | Tom3          | GM Low Tom |
| 46   | Hi-Hat Open          | HihatOpen     | GM Open Hi-Hat — choke group 0 |
| 48   | Rack Tom 2 (Mid)     | Tom2          | GM Hi-Mid Tom |
| 49   | Crash                | CrashL        | GM Crash Cymbal 1 |
| 50   | Rack Tom 1 (High)    | Tom1          | GM High Tom |
| 51   | Ride                 | RideL         | GM Ride Cymbal 1 |
| 52   | China Cymbal         | China         | GM Chinese Cymbal |
| 53   | Ride Bell            | RideLBell     | GM Ride Bell |
| 56   | Ride Bell 2          | RideRBell     | bonus articulation — GM 56 slot (Cowbell) repurposed |
| 57   | Crash 2              | CrashR        | GM Crash Cymbal 2 |
| 59   | Ride 2               | RideR         | GM Ride Cymbal 2 |

The open and closed hi-hats share **choke group 0**, so an open hat is cut off by
a following closed hat (standard hi-hat behaviour). All other pieces are
un-choked. The two "bonus" articulations (MIDI 39, 56) occupy GM slots that an
acoustic kit does not otherwise use; remap them freely in your DAW.

## Samples (not committed to git)

Per the repository's distributed-content model, the WAV sample binaries are
**not** stored in git (`.gitignore` excludes `*.wav`). Only this metadata
(`flamkit.yaml`, `LICENSE.txt`, `README.md`, the source `.sfz`) is tracked. The
audio is distributed via the FlamKit v1.0 website downloads.

To populate the samples locally (for building, testing, or FLA-87 load
verification), download the FreePats **SFZ + WAV** pack and place its `samples/`
tree in this directory:

```sh
# ~223 MiB download (~380 MiB uncompressed), CC-BY 4.0
curl -LO https://freepats.zenvoid.org/Percussion/acoustic-drum-kit.html   # see "SFZ  WAV" link
# extract so that this directory contains:  muldjord-kit/samples/<Instrument>/<n>-<Instrument>.wav
```

`flamkit.yaml` references `samples/<Folder>/<n>-<Folder>.wav` paths that match the
FreePats WAV pack layout.

## Provenance / verification

* License verified at the upstream source: the `freepats/muldjordkit` GitHub repo
  carries a CC-BY-4.0 `LICENSE.txt` (GitHub-detected SPDX `CC-BY-4.0`), and the
  FreePats page states "Creative Commons Attribution 4.0 license."
* Channel layout confirmed by probing an upstream sample: 44.1 kHz, **2 channels
  (stereo)**, 24-bit — meets FlamKit's audio quality bar.
* Catalogued as **CLEARED** in [`../LICENSES.md`](../LICENSES.md).

## Upstream recording notes (original DrumGizmo MuldjordKit, by Lars Muldjord)

> MuldjordKit3 … recordings stem from a drum kit recording session I did back in
> 2009 where I played drums for the now defunct band Sepulchrum. … This should be
> considered a metal or rock kit.

The original 16-channel DrumGizmo version is multi-mic (close mics + overheads +
ambience). The FreePats edition packaged here is the pre-mixed stereo rendering.
See `MuldjordKit-FreePats-20201018.sfz` (upstream SFZ, included for provenance).
