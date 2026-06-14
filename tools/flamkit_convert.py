#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2024-2026 FLAM Contributors
"""
flamkit_convert.py — FlamKit Kit Conversion Pipeline

Converts DrumGizmo instrument XML kits or SFZ kits to flamkit.yaml format.
Also provides normalize/trim/loudness-match and lite-variant generation.

USAGE:
  # DrumGizmo kit → flamkit.yaml
  python flamkit_convert.py convert \\
      --drumgizmo Resources/Kits/drs-kit/DRSKit_basic.xml \\
      --midimap   Resources/Kits/drs-kit/Midimap_basic.xml \\
      -o /tmp/drs-converted/

  # SFZ kit → flamkit.yaml
  python flamkit_convert.py convert --sfz path/to/kit.sfz -o /tmp/sfz-converted/

  # Normalize all samples in a kit (in-place)
  python flamkit_convert.py normalize --kit /tmp/drs-converted/flamkit.yaml --target-lufs -23

  # Generate lite variant (fewer velocity layers / round-robins)
  python flamkit_convert.py lite \\
      --kit  /tmp/drs-converted/flamkit.yaml \\
      -o     /tmp/drs-lite/ \\
      --max-layers 8 --max-rr 3

DEPENDENCIES:
  pip install pyyaml soundfile numpy
  (soundfile/numpy only needed for the 'normalize' subcommand)
"""

import argparse
import copy
import os
import re
import sys
from pathlib import Path
from xml.etree import ElementTree as ET

try:
    import yaml
    _YAML_OK = True
except ImportError:
    _YAML_OK = False

try:
    import soundfile as sf
    import numpy as np
    _AUDIO_OK = True
except ImportError:
    _AUDIO_OK = False


# ---------------------------------------------------------------------------
# Human-readable channel name mapping for DrumGizmo channel names
# ---------------------------------------------------------------------------
_DG_CHANNEL_NAMES = {
    "AmbL":         "Ambient-L",
    "AmbR":         "Ambient-R",
    "Kdrum_back":   "Kick Out",
    "Kdrum_front":  "Kick In",
    "Hihat":        "HH Close",
    "OHL":          "OH-L",
    "OHR":          "OH-R",
    "Ride":         "Ride",
    "Snare_bottom": "Snare Btm",
    "Snare_top":    "Snare Top",
    "Tom1":         "Tom 1",
    "Tom2":         "Tom 2",
    "Tom3":         "Tom 3",
    "CrashL":       "Crash-L",
    "CrashR":       "Crash-R",
    "Room_L":       "Room-L",
    "Room_R":       "Room-R",
}

# ADSR defaults by instrument name keyword
_ADSR_TABLE = [
    (["kick", "kdrum", "bass_drum"],       0.0,  0.0,  0.5,  0.0, 0.20),
    (["snare"],                             0.0,  0.0,  0.4,  0.0, 0.15),
    (["hihat_open", "hh_open"],             0.0,  0.0,  1.2,  0.0, 0.40),
    (["hihat", "hh"],                       0.0,  0.0,  0.1,  0.0, 0.05),
    (["tom"],                               0.0,  0.0,  0.65, 0.0, 0.22),
    (["crash"],                             0.0,  0.0,  3.5,  0.0, 1.50),
    (["ride_bell"],                         0.0,  0.0,  3.0,  0.0, 1.20),
    (["ride"],                              0.0,  0.0,  2.5,  0.0, 1.00),
    (["cymbal"],                            0.0,  0.0,  3.0,  0.0, 1.20),
]

def _adsr_for(name: str):
    n = name.lower()
    for keywords, att, hld, dcy, sus, rel in _ADSR_TABLE:
        if any(k in n for k in keywords):
            return att, hld, dcy, sus, rel
    return 0.0, 0.0, 0.3, 0.0, 0.10


def _pretty_name(raw: str) -> str:
    """Convert underscore/camel drum identifiers to human-readable names."""
    s = raw.replace("_", " ").strip()
    # Title-case with some abbreviation fixes
    fixes = {
        "Kdrum": "Kick",
        "With Contact": "",
        "Without Contact": "No Contact",
        "Hihat": "Hi-Hat",
        "Hh": "Hi-Hat",
    }
    s = s.title()
    for old, new in fixes.items():
        s = s.replace(old, new)
    return s.strip()


# ---------------------------------------------------------------------------
# flamkit.yaml building helpers
# ---------------------------------------------------------------------------

def _layer(sample_file, vel_min, vel_max, gain=1.0, rr_group=0):
    return {
        "sampleFile":     str(sample_file),
        "velocityMin":    round(float(vel_min), 6),
        "velocityMax":    round(float(vel_max), 6),
        "gain":           round(float(gain), 4),
        "roundRobinGroup": int(rr_group),
    }

def _articulation(name, layers, choke=-1, att=0.0, hld=0.0, dcy=0.3, sus=0.0, rel=0.1):
    return {
        "name":         name,
        "chokeGroup":   choke,
        "attackTime":   round(att, 4),
        "holdTime":     round(hld, 4),
        "decayTime":    round(dcy, 4),
        "sustainLevel": round(sus, 4),
        "releaseTime":  round(rel, 4),
        "layers":       layers,
    }

def _piece(name, midi_note, articulations):
    return {"name": name, "midiNote": midi_note, "articulations": articulations}


# ---------------------------------------------------------------------------
# DrumGizmo XML → flamkit.yaml
# ---------------------------------------------------------------------------

def _drumgizmo_channel_order(first_instr_xml: Path) -> list:
    """Return list of (channel_name, 0-based-index) sorted by filechannel."""
    try:
        tree = ET.parse(first_instr_xml)
    except Exception:
        return []
    # Collect channel -> filechannel from first <sample>
    ch_map = {}
    for sample in tree.getroot().findall("./samples/sample"):
        for af in sample.findall("audiofile"):
            ch = af.get("channel")
            fc = af.get("filechannel")
            if ch and fc and ch not in ch_map:
                ch_map[ch] = int(fc)
        break  # one sample is enough
    # Sort by filechannel value
    ordered = sorted(ch_map.items(), key=lambda x: x[1])
    return [{"name": _DG_CHANNEL_NAMES.get(n, n), "index": fc - 1} for n, fc in ordered]


def _drumgizmo_instrument(instr_xml: Path, instr_dir_rel: str):
    """Parse a DrumGizmo instrument XML; return sorted samples list."""
    try:
        tree = ET.parse(instr_xml)
    except Exception as e:
        print(f"  Warning: cannot parse {instr_xml}: {e}", file=sys.stderr)
        return []

    samples = []
    for sample in tree.getroot().findall("./samples/sample"):
        power = float(sample.get("power", 0.5))
        audiofiles = sample.findall("audiofile")
        if not audiofiles:
            continue
        # All audiofiles in a sample share the same .wav; use the first entry for the path.
        rel_file = audiofiles[0].get("file", "")
        # Path is relative to the instrument XML directory
        kit_rel_path = str(Path(instr_dir_rel) / rel_file).replace("\\", "/")
        samples.append({"power": power, "file": kit_rel_path})

    samples.sort(key=lambda s: s["power"])
    return samples


def _build_velocity_layers(samples: list) -> list:
    """Convert sorted [{power, file}] list into flamkit velocity layer dicts.

    Samples with identical power values are treated as round-robins at the
    same velocity layer.  All other layers are evenly spaced across [0, 1].
    """
    if not samples:
        return []

    # Group by power (round to 5dp to merge near-identical power values)
    groups: dict = {}
    for s in samples:
        key = round(s["power"], 5)
        groups.setdefault(key, []).append(s["file"])

    sorted_keys = sorted(groups)
    n = len(sorted_keys)
    layers = []
    for i, key in enumerate(sorted_keys):
        vel_min = round(i / n, 6)
        vel_max = round((i + 1) / n, 6) if i < n - 1 else 1.0
        for rr, path in enumerate(groups[key]):
            layers.append(_layer(path, vel_min, vel_max, rr_group=rr))
    return layers


def parse_drumgizmo(kit_xml: str, midimap_xml: str | None = None) -> dict:
    """Convert a DrumGizmo kit XML (+ optional midimap) to intermediate repr."""
    kit_xml = Path(kit_xml)
    kit_dir = kit_xml.parent

    tree = ET.parse(kit_xml)
    root = tree.getroot()

    kit_name = root.get("name", kit_xml.stem)
    kit_desc = root.get("description", "")

    # Collect all instruments and their optional choke group
    instruments: dict = {}
    for instr in root.findall("./instruments/instrument"):
        name = instr.get("name", "")
        file = instr.get("file", "")
        group = instr.get("group")  # e.g. "hihat" → maps to choke group
        instruments[name] = {"file": file, "group": group}

    # Assign choke group IDs (one per unique DrumGizmo group name)
    group_choke: dict = {}
    choke_id = 0
    for info in instruments.values():
        g = info.get("group")
        if g and g not in group_choke:
            group_choke[g] = choke_id
            choke_id += 1

    # Parse midimap
    midi_map: dict = {}
    if midimap_xml and Path(midimap_xml).exists():
        mm = ET.parse(midimap_xml)
        for mapping in mm.getroot().findall("map"):
            note = int(mapping.get("note", 60))
            instr = mapping.get("instr", "")
            midi_map[instr] = note

    # Derive channel list from the first parseable instrument XML
    channels = []
    for info in instruments.values():
        if info["file"]:
            instr_path = kit_dir / info["file"]
            if instr_path.exists():
                channels = _drumgizmo_channel_order(instr_path)
                break

    # Build pieces
    pieces = []
    for instr_name, info in instruments.items():
        if not info["file"]:
            continue
        instr_path = kit_dir / info["file"]
        if not instr_path.exists():
            print(f"  Warning: not found: {instr_path}", file=sys.stderr)
            continue

        instr_dir_rel = str(Path(info["file"]).parent)
        samples = _drumgizmo_instrument(instr_path, instr_dir_rel)
        if not samples:
            continue

        midi_note = midi_map.get(instr_name, 60)
        choke = group_choke.get(info.get("group"), -1) if info.get("group") else -1

        att, hld, dcy, sus, rel = _adsr_for(instr_name)
        layers = _build_velocity_layers(samples)
        art = _articulation("Main", layers, choke=choke, att=att, hld=hld, dcy=dcy, sus=sus, rel=rel)
        pieces.append(_piece(_pretty_name(instr_name), midi_note, [art]))

    pieces.sort(key=lambda p: p["midiNote"])

    return {
        "kit_name": kit_name,
        "kit_desc": kit_desc,
        "channels":  channels,
        "pieces":    pieces,
    }


# ---------------------------------------------------------------------------
# SFZ → flamkit.yaml
# ---------------------------------------------------------------------------

_NOTE_SEMITONES = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}

def _sfz_note(s: str) -> int:
    """Parse 'C3', 'Bb2', '60', etc. to MIDI note number."""
    s = str(s).strip()
    if re.fullmatch(r"\d+", s):
        return int(s)
    m = re.fullmatch(r"([a-gA-G])(b|#)?(-?\d+)", s)
    if m:
        note = _NOTE_SEMITONES[m.group(1).lower()]
        if m.group(2) == "#":
            note += 1
        elif m.group(2) == "b":
            note -= 1
        return max(0, min(127, (int(m.group(3)) + 1) * 12 + note))
    return 60


def _sfz_preprocess(
    sfz_path: Path,
    root_dir: Path | None = None,
    _visited: set | None = None,
    _defines: dict | None = None,
) -> str:
    """Recursively expand #include directives; collect #define macros in _defines.

    SFZ preprocessor rules:
    - #include "rel/path" — resolved first relative to the current file's dir,
      then falling back to root_dir (the top-level SFZ's dir). Many kits write
      all includes relative to the top-level Programs/ directory.
    - #define $VAR value — stored in the shared _defines dict; substitution is
      applied at the TOP-LEVEL call only, after the full tree is expanded.
    - sample= paths are always relative to the top-level SFZ; do not rebase them.

    _defines is a SHARED mutable dict threaded through all recursive calls so
    that defines from any included file are visible to the entire include tree.
    """
    is_root = _defines is None
    if _visited is None:
        _visited = set()
    if _defines is None:
        _defines = {}
    if root_dir is None:
        root_dir = sfz_path.parent

    canonical = sfz_path.resolve()
    if canonical in _visited:
        return ""
    _visited.add(canonical)

    try:
        raw = sfz_path.read_text(encoding="utf-8", errors="replace")
    except OSError as e:
        print(f"  Warning: cannot read {sfz_path}: {e}", file=sys.stderr)
        return ""

    lines_out: list[str] = []

    for line in raw.splitlines(keepends=True):
        stripped = line.strip()

        # Collect #define $VAR value into shared dict; emit nothing (strip directive).
        m = re.match(r"#define\s+(\$\w+)\s+(\S+)", stripped)
        if m:
            _defines[m.group(1)] = m.group(2)
            continue

        # Expand #include "relative/path"
        m = re.match(r'#include\s+"([^"]+)"', stripped)
        if m:
            rel = m.group(1)
            # Try relative to the current file's dir first, then root_dir.
            inc_path = sfz_path.parent / rel
            if not inc_path.exists():
                inc_path = root_dir / rel
            expanded = _sfz_preprocess(inc_path, root_dir, _visited, _defines)
            if not expanded.endswith("\n"):
                expanded += "\n"
            lines_out.append(expanded)
            continue

        lines_out.append(line)

    text = "".join(lines_out)

    # Apply substitutions only at root level, once the full tree is expanded.
    if is_root:
        for var in sorted(_defines, key=len, reverse=True):
            text = text.replace(var, _defines[var])

    return text


def _sfz_parse_opcodes(text: str) -> dict:
    """Parse key=value pairs from a text fragment."""
    return {m.group(1): m.group(2) for m in re.finditer(r"(\w+)\s*=\s*(\S+)", text)}


def parse_sfz(
    sfz_path: str,
    replace_ext: str | None = None,
    kit_root: str | None = None,
) -> dict:
    """Convert an SFZ file to intermediate repr.

    Args:
        sfz_path:    Path to the top-level SFZ file.
        replace_ext: If set (e.g. ".wav"), rewrite sample file extensions.
                     Useful when FLAC sources have been converted to WAV.
        kit_root:    If set, rebase all sample paths to be relative to this
                     directory instead of relative to the SFZ's parent.
                     E.g. set to the kit root when the SFZ lives in Programs/.
    """
    sfz_path = Path(sfz_path)
    sfz_dir = sfz_path.parent
    kit_root_path = Path(kit_root).resolve() if kit_root else None

    # Full preprocessing: resolve #include and #define
    raw = _sfz_preprocess(sfz_path)

    # Strip line comments
    raw = re.sub(r"//[^\n]*", "", raw)
    # Strip block comments
    raw = re.sub(r"/\*.*?\*/", "", raw, flags=re.DOTALL)

    regions = []
    current_header = None
    current_ops: dict = {}
    group_ops: dict = {}

    def flush_region():
        nonlocal current_ops
        merged = {**group_ops, **current_ops}
        regions.append(merged)

    for line in raw.splitlines():
        line = line.strip()
        if not line:
            continue

        # Header detection — may have opcodes on the same line
        header_match = re.match(r"<(\w+)>", line)
        if header_match:
            if current_header == "region":
                flush_region()
            elif current_header == "group":
                group_ops = dict(current_ops)
            current_header = header_match.group(1)
            current_ops = {}
            rest = line[header_match.end():]
            current_ops.update(_sfz_parse_opcodes(rest))
        else:
            current_ops.update(_sfz_parse_opcodes(line))

    if current_header == "region":
        flush_region()

    # Build pieces keyed by (MIDI note, sample group) — one piece per note per
    # unique instrument sub-folder so multi-mic kits collapse to the primary mic only.
    # We deduplicate by keeping ONLY the first mic group seen per note.
    note_first_group: dict[int, str] = {}
    pieces_map: dict[int, dict] = {}
    for ops in regions:
        sample = ops.get("sample", "").replace("\\", "/")
        if not sample:
            continue

        # Optional: rebase path relative to kit_root instead of SFZ dir.
        if kit_root_path is not None:
            abs_sample = (sfz_dir / sample).resolve()
            try:
                sample = str(abs_sample.relative_to(kit_root_path)).replace("\\", "/")
            except ValueError:
                pass  # keep original if outside kit_root

        # Optional: rewrite file extension (e.g. .flac → .wav)
        if replace_ext:
            sample = str(Path(sample).with_suffix(replace_ext))

        lovel = int(ops.get("lovel", 0))
        hivel = int(ops.get("hivel", 127))
        vel_min = lovel / 127.0
        vel_max = hivel / 127.0

        key_str = ops.get("key") or ops.get("pitch_keycenter") or ops.get("lokey") or "60"
        midi_note = _sfz_note(key_str)

        # Derive the instrument sub-folder (second-to-last path component)
        # so we can group regions by instrument rather than mic position.
        parts = Path(sample).parts
        # For Virtuosity-style paths like "Samples/kickmic/kick/file.flac",
        # the instrument group is parts[-2] (e.g. "kick").
        sample_group = parts[-2] if len(parts) >= 2 else "unknown"

        # Accept only the first mic group encountered for this MIDI note
        # to avoid duplicating across mic positions.
        if midi_note not in note_first_group:
            note_first_group[midi_note] = sample_group
        elif note_first_group[midi_note] != sample_group:
            continue

        seq_pos = int(ops.get("seq_position", 1))
        rr_group = seq_pos - 1

        gain_db = float(ops.get("volume", 0.0))
        gain = 10 ** (gain_db / 20.0)

        att  = float(ops.get("ampeg_attack",  0.0))
        dcy  = float(ops.get("ampeg_decay",   0.3))
        rel  = float(ops.get("ampeg_release", 0.1))

        layer = _layer(sample, vel_min, vel_max, gain=gain, rr_group=rr_group)

        if midi_note not in pieces_map:
            # Derive piece name from the instrument sub-folder
            piece_name = _pretty_name(sample_group)
            pieces_map[midi_note] = {
                "name":    piece_name,
                "layers":  [],
                "att": att, "dcy": dcy, "rel": rel,
            }
        pieces_map[midi_note]["layers"].append(layer)

    pieces = []
    for midi_note, info in sorted(pieces_map.items()):
        att, hld, dcy, sus, rel = _adsr_for(info["name"])
        # Override with SFZ envelope if present (non-zero)
        if info["att"] > 0:
            att = info["att"]
        if info["dcy"] > 0:
            dcy = info["dcy"]
        if info["rel"] > 0:
            rel = info["rel"]
        art = _articulation("Main", info["layers"], att=att, hld=hld, dcy=dcy, sus=sus, rel=rel)
        pieces.append(_piece(info["name"], midi_note, [art]))

    return {
        "kit_name": sfz_path.stem.replace("_", " ").replace("-", " ").title(),
        "kit_desc": f"Converted from SFZ: {sfz_path.name}",
        "channels": [],
        "pieces":   pieces,
    }


# ---------------------------------------------------------------------------
# flamkit.yaml emitter
# ---------------------------------------------------------------------------

class _FloatDumper(yaml.Dumper):
    """YAML dumper that emits floats without trailing zeros."""
    pass

def _represent_float(dumper, data):
    if data != data:  # NaN
        return dumper.represent_scalar("tag:yaml.org,2002:float", ".nan")
    s = f"{data:.6g}"
    if "." not in s and "e" not in s:
        s += ".0"
    return dumper.represent_scalar("tag:yaml.org,2002:float", s)

_FloatDumper.add_representer(float, _represent_float)


def emit_flamkit_yaml(
    kit_data: dict,
    output_dir: str,
    kit_name: str | None = None,
    author: str = "",
    version: str = "1.0",
    tags: list | None = None,
) -> Path:
    """Write a flamkit.yaml to output_dir from intermediate repr."""
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    doc = {
        "name":        kit_name or kit_data["kit_name"],
        "author":      author,
        "description": kit_data.get("kit_desc", ""),
        "version":     version,
        "tags":        tags or ["acoustic"],
    }
    if kit_data.get("channels"):
        doc["channels"] = kit_data["channels"]
    doc["settings"] = {"maxPolyphony": 64, "useRoundRobin": True}
    doc["pieces"]   = kit_data["pieces"]

    out_path = out / "flamkit.yaml"
    with open(out_path, "w", encoding="utf-8") as f:
        yaml.dump(doc, f, Dumper=_FloatDumper, default_flow_style=False,
                  allow_unicode=True, sort_keys=False, width=120)
    return out_path


# ---------------------------------------------------------------------------
# Audio normalization
# ---------------------------------------------------------------------------

def normalize_kit(
    kit_yaml_path: str,
    target_lufs: float = -23.0,
    trim_silence: bool = True,
    silence_db: float = -60.0,
) -> bool:
    """Normalize and trim-silence all samples referenced in a flamkit.yaml.

    Processes WAV files in-place.  Uses RMS-based normalization as a fast
    proxy for integrated loudness (accurate enough for internal kit consistency;
    use ffmpeg-normalize for broadcast-grade EBU R128 compliance).
    """
    if not _AUDIO_OK:
        print("Error: pip install soundfile numpy", file=sys.stderr)
        return False

    with open(kit_yaml_path) as f:
        kit = yaml.safe_load(f)

    kit_dir = Path(kit_yaml_path).parent
    seen: set = set()
    for piece in kit.get("pieces", []):
        for art in piece.get("articulations", []):
            for layer in art.get("layers", []):
                p = (kit_dir / layer["sampleFile"]).resolve()
                if p.exists():
                    seen.add(p)

    silence_lin = 10 ** (silence_db / 20.0)
    # Target RMS mapped from LUFS (approximate; +10 dB offset for typical audio)
    target_rms = 10 ** ((target_lufs + 10.0) / 20.0)

    print(f"Normalizing {len(seen)} sample files (target {target_lufs} LUFS approx)...")
    for i, path in enumerate(sorted(seen), 1):
        try:
            data, sr = sf.read(str(path))
        except Exception as e:
            print(f"  [{i}] SKIP {path.name}: {e}", file=sys.stderr)
            continue

        # Trim leading silence
        if trim_silence and len(data) > 0:
            amp = np.max(np.abs(data), axis=-1) if data.ndim > 1 else np.abs(data)
            start = 0
            for j, a in enumerate(amp):
                if a > silence_lin:
                    start = max(0, j - int(0.001 * sr))  # 1 ms pre-roll
                    break
            data = data[start:]

        if len(data) == 0:
            print(f"  [{i}] SKIP {path.name}: empty after trim")
            continue

        rms = float(np.sqrt(np.mean(data.astype(np.float64) ** 2)))
        if rms > 1e-9:
            gain = min(target_rms / rms, 20.0)  # cap at +26 dB
            data = (data * gain).clip(-1.0, 1.0)

        # True-peak limit to −0.5 dBTP
        peak = float(np.max(np.abs(data)))
        if peak > 0.944:  # 10^(-0.5/20)
            data = data * (0.944 / peak)

        sf.write(str(path), data, sr)
        print(f"  [{i}/{len(seen)}] {path.name}  gain={gain:.2f}×  dur={len(data)/sr:.2f}s")

    print("Done.")
    return True


# ---------------------------------------------------------------------------
# Lite-variant generator
# ---------------------------------------------------------------------------

def generate_lite(
    kit_yaml_path: str,
    output_dir: str,
    max_layers: int = 8,
    max_rr: int = 3,
) -> Path:
    """Produce a velocity/RR-reduced flamkit.yaml for lighter distribution.

    Sample files are NOT copied — the lite YAML references the same paths,
    which must exist alongside the output.  Copy samples separately if needed.
    """
    with open(kit_yaml_path) as f:
        kit = yaml.safe_load(f)

    kit_lite = copy.deepcopy(kit)
    kit_lite["name"] = kit_lite.get("name", "Kit") + " Lite"

    removed_total = 0
    for piece in kit_lite.get("pieces", []):
        for art in piece.get("articulations", []):
            layers = art.get("layers", [])

            # Group layers by velocity midpoint key (vel_min, vel_max)
            vel_groups: dict = {}
            for layer in layers:
                vk = (layer["velocityMin"], layer["velocityMax"])
                vel_groups.setdefault(vk, []).append(layer)

            sorted_keys = sorted(vel_groups, key=lambda k: (k[0] + k[1]) / 2)

            # Downsample velocity layers if needed
            if len(sorted_keys) > max_layers:
                step = (len(sorted_keys) - 1) / (max_layers - 1)
                kept = [sorted_keys[round(i * step)] for i in range(max_layers)]
            else:
                kept = sorted_keys

            # Rebuild with clamped round-robins and re-ranged velocity spans
            new_layers = []
            n = len(kept)
            for i, vk in enumerate(kept):
                rrs = vel_groups[vk][:max_rr]
                new_vmin = round(i / n, 6)
                new_vmax = round((i + 1) / n, 6) if i < n - 1 else 1.0
                for j, layer in enumerate(rrs):
                    nl = dict(layer)
                    nl["velocityMin"]    = new_vmin
                    nl["velocityMax"]    = new_vmax
                    nl["roundRobinGroup"] = j
                    new_layers.append(nl)

            removed_total += len(layers) - len(new_layers)
            art["layers"] = new_layers

    out_dir = Path(output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "flamkit.yaml"
    with open(out_path, "w", encoding="utf-8") as f:
        yaml.dump(kit_lite, f, Dumper=_FloatDumper, default_flow_style=False,
                  allow_unicode=True, sort_keys=False, width=120)

    orig_layers = sum(
        len(art.get("layers", []))
        for p in kit.get("pieces", [])
        for art in p.get("articulations", [])
    )
    print(f"Lite: {orig_layers} → {orig_layers - removed_total} layers "
          f"({removed_total} removed, {max_layers} max/art, {max_rr} max RR)")
    print(f"Output: {out_path}")
    return out_path


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _cmd_convert(args):
    if not _YAML_OK:
        sys.exit("Error: pip install pyyaml")

    if args.drumgizmo:
        print(f"DrumGizmo: {args.drumgizmo}")
        kit_data = parse_drumgizmo(args.drumgizmo, getattr(args, "midimap", None))
    else:
        print(f"SFZ: {args.sfz}")
        replace_ext = getattr(args, "replace_ext", None) or None
        kit_root = getattr(args, "kit_root", None) or None
        kit_data = parse_sfz(args.sfz, replace_ext=replace_ext, kit_root=kit_root)

    tags = [t.strip() for t in args.tags.split(",")] if args.tags else None
    out = emit_flamkit_yaml(
        kit_data, args.output,
        kit_name=args.name, author=args.author,
        version=args.version, tags=tags,
    )

    total_layers = sum(
        len(art["layers"])
        for p in kit_data["pieces"]
        for art in p["articulations"]
    )
    print(f"Written: {out}")
    print(f"  Pieces:   {len(kit_data['pieces'])}")
    print(f"  Channels: {len(kit_data['channels'])}")
    print(f"  Layers:   {total_layers}")


def _cmd_normalize(args):
    normalize_kit(args.kit, target_lufs=args.target_lufs,
                  trim_silence=not args.no_trim)


def _cmd_lite(args):
    generate_lite(args.kit, args.output,
                  max_layers=args.max_layers, max_rr=args.max_rr)


def main():
    p = argparse.ArgumentParser(
        description="FlamKit conversion pipeline: DrumGizmo/SFZ → flamkit.yaml"
    )
    sub = p.add_subparsers(dest="command", required=True)

    # -- convert --
    pc = sub.add_parser("convert", help="Convert DrumGizmo XML or SFZ → flamkit.yaml")
    src = pc.add_mutually_exclusive_group(required=True)
    src.add_argument("--drumgizmo", metavar="KIT_XML",
                     help="Path to DrumGizmo kit XML (e.g. DRSKit_basic.xml)")
    src.add_argument("--sfz", metavar="SFZ_FILE",
                     help="Path to SFZ file")
    pc.add_argument("--midimap", metavar="MIDIMAP_XML",
                    help="DrumGizmo midimap XML (recommended with --drumgizmo)")
    pc.add_argument("-o", "--output", metavar="DIR", required=True)
    pc.add_argument("--name",        help="Override kit name")
    pc.add_argument("--author",      default="", help="Kit author")
    pc.add_argument("--version",     default="1.0")
    pc.add_argument("--tags",        help="Comma-separated tags")
    pc.add_argument("--replace-ext", metavar="EXT",
                    help="Rewrite sample file extension in output (e.g. .wav). "
                         "Use after converting FLAC sources to WAV.")
    pc.add_argument("--kit-root", metavar="DIR",
                    help="Rebase sample paths relative to this directory (e.g. the "
                         "kit root when the SFZ lives in a Programs/ subdirectory).")

    # -- normalize --
    pn = sub.add_parser("normalize", help="Normalize + trim-silence samples in-place")
    pn.add_argument("--kit",         required=True, metavar="flamkit.yaml")
    pn.add_argument("--target-lufs", type=float, default=-23.0,
                    help="Target loudness in LUFS approx (default: -23)")
    pn.add_argument("--no-trim",     action="store_true",
                    help="Skip leading silence trimming")

    # -- lite --
    pl = sub.add_parser("lite", help="Generate lite (layer/RR-reduced) variant")
    pl.add_argument("--kit",        required=True, metavar="flamkit.yaml")
    pl.add_argument("-o", "--output", required=True, metavar="DIR")
    pl.add_argument("--max-layers", type=int, default=8,
                    help="Max velocity layers per articulation (default: 8)")
    pl.add_argument("--max-rr",     type=int, default=3,
                    help="Max round-robins per velocity layer (default: 3)")

    args = p.parse_args()
    {"convert": _cmd_convert, "normalize": _cmd_normalize, "lite": _cmd_lite}[args.command](args)


if __name__ == "__main__":
    main()
