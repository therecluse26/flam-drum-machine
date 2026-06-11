# Bundled Kit Licensing & Provenance

This file is the authoritative record of every drum kit bundled with FlamKit:
its source, license, and whether it is cleared for redistribution with the
plugin. **A kit may not ship in a FlamKit release until its row here is marked
`CLEARED`.**

FlamKit itself is GPLv3. Bundled *sample content* is licensed separately, per
kit. We only ship kits whose licenses permit **redistribution and commercial
use** (CC0, CC-BY, or an explicit redistribution grant). We do **not** ship any
kit under a Non-Commercial (NC) or No-Derivatives (ND) restriction, and we do
not ship any kit whose provenance we cannot verify.

## Redistribution status

| Kit | Genre | Channels | License | Provenance verified | Status |
|-----|-------|----------|---------|---------------------|--------|
| `drs-kit` (DRSKit) | Acoustic (rock/jazz) | 13 (multi-mic) | CC-BY 4.0 | ✅ Yes — DrumGizmo / DRSDrums | **CLEARED** |
| `muldjord-kit` (MuldjordKit) | Acoustic (rock/metal) | 2 (stereo) | CC-BY 4.0 | ✅ Yes — FreePats / DrumGizmo (Lars Muldjord) | **CLEARED** |
| `example-kit` | "Rock/Pop" (stereo) | 2 | **Unknown** | ❌ No — see below | **BLOCKED — do not ship** |
| `minimal-kit` | "Rock/Pop" (stereo) | 2 | **Unknown** | ❌ No — see below | **BLOCKED — do not ship** |

## Kit details

### drs-kit — DRSKit (CLEARED ✅)

- **Source:** DrumGizmo project — https://drumgizmo.org/ (kit: https://drumgizmo.org/kits/)
- **Creators:** DrumGizmo team in collaboration with DRSDrums / Jes Eiler
  (http://www.drsdrums.dk). Sampling by Deva & Lars Muldjord.
- **License:** Creative Commons Attribution 4.0 International (CC-BY 4.0).
  Permits redistribution and commercial use **with attribution**.
- **Required attribution (must be shown to end users):**
  > "DRSKit by the DrumGizmo project (https://drumgizmo.org/), licensed under
  > CC-BY 4.0 (https://creativecommons.org/licenses/by/4.0/)."
- **Format:** 13-channel multi-mic WAV, 44.1 kHz, 32-bit IEEE float.
  Demonstrates FlamKit's multi-channel routing capability.
- **Full license text & attribution:** see `drs-kit/LICENSE.txt`.
- **Original kit docs (mic/channel map):** see `drs-kit/README.md`.

### muldjord-kit — MuldjordKit, FreePats edition (CLEARED ✅)

- **Source:** FreePats project —
  https://freepats.zenvoid.org/Percussion/acoustic-drum-kit.html
  (upstream repo: https://github.com/freepats/muldjordkit ; original kit:
  DrumGizmo — https://drumgizmo.org/ , www.muldjord.com).
- **Creator:** Lars Muldjord (Tama Superstar kit, recorded during the 2009
  Sepulchrum sessions). FreePats stereo edition assembled by Roberto
  (roberto@zenvoid.org).
- **License:** Creative Commons Attribution 4.0 International (CC-BY 4.0).
  Permits redistribution and commercial use **with attribution**. Verified at
  source — the upstream GitHub repo carries a CC-BY-4.0 `LICENSE.txt`
  (GitHub-detected SPDX `CC-BY-4.0`).
- **Required attribution (must be shown to end users):** FreePats terms (since
  2021-01) require the credit *"Drum samples provided by DrumGizmo.org."*
  FlamKit displays the fuller credit:
  > "MuldjordKit by Lars Muldjord / the DrumGizmo project (drumgizmo.org),
  > FreePats edition, licensed under CC-BY 4.0
  > (https://creativecommons.org/licenses/by/4.0/)."
- **Format:** stereo (2-channel) WAV, 44.1 kHz, 24-bit. Confirmed by probing an
  upstream sample. A lighter-weight contrast to DRSKit's 13-channel multi-mic
  kit, and a different genre (rock/metal vs. rock/jazz).
- **Full license text & attribution:** see `muldjord-kit/LICENSE.txt`.
- **Kit docs (GM map, components, mic notes):** see `muldjord-kit/README.md`.
- **Provenance reference:** upstream SFZ included as
  `muldjord-kit/MuldjordKit-FreePats-20201018.sfz`.

### example-kit / minimal-kit — provenance UNVERIFIED (BLOCKED ❌)

These two kits share the same `RD_*` sample set. They were added in commit
`c68cc5a "Got samples loading and playing"` purely to get audio playback working
during early engine development — **not** vetted for redistribution licensing.

Provenance investigation findings:
- Embedded WAV metadata reads `comment=Recorded on 12/04/2016 in Edison` and
  `encoder=Edison` (Edison is FL Studio's built-in recorder/editor).
- **No copyright, artist, or license tag** is embedded in the files.
- Sample naming uses an `RD_*` prefix (e.g. `RD_K_1.wav`, `RD_S_1.wav`,
  `RD_C_C_1.wav`) of unknown origin.
- Format: stereo WAV, 44.1 kHz, 32-bit float (meets the audio quality bar), but
  format quality does not establish licensing.

**We cannot certify these samples as CC-licensed or cleared for redistribution.**
Until their origin and license are confirmed in writing, they must not ship in a
public FlamKit release. Resolution paths:
1. The committer confirms the original source + license (and it permits
   redistribution), or
2. The kits are replaced with verified CC0/CC-BY content (see the sourcing plan
   on issue FLA-81), or
3. They are retained as developer-only test fixtures and excluded from release
   packaging.

## How to add a new kit (curation checklist)

Before adding a kit under `Resources/Kits/`:

1. **Verify the license** at the original source. It must be CC0, CC-BY, or carry
   an explicit redistribution + commercial-use grant. Reject anything NC or ND.
2. **Capture provenance:** source URL, creator(s), license, license URL, and
   any modifications you made.
3. **Add a `LICENSE.txt`** in the kit's directory with the full attribution
   string (copy the `drs-kit/LICENSE.txt` pattern).
4. **Record the kit** in the table above with status `CLEARED`.
5. **Confirm the audio bar:** 24-bit minimum (32-bit float OK), 44.1/48 kHz, WAV.
6. **Surface attribution in-app** for CC-BY kits (see the format/UI work tracked
   as a child of FLA-81).
