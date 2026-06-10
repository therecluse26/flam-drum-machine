#!/usr/bin/env python3
"""
Generates Tests/Fixtures/goldens/golden_render.wav by precisely simulating
the FlamKit C++ rendering pipeline so the golden can be committed without
requiring a full C++ build.

The simulation replicates bit-for-bit:
  - JUCE Random (48-bit LCG): seed=(seed*0x5DEECE66D+11)&0xFFFFFFFFFFFF
  - JUCE ADSR (linear attack/decay/sustain/release rates)
  - VoiceManager round-robin + history-filtering
  - SampleVoice.renderNextBlock with linear interpolation
  - FlamEngine.processBlock: all MIDI events processed before render,
    so notes start from sample 0 of the block regardless of sampleOffset

Run after any change to the fixture WAVs or render parameters.
The output should be verified with: ctest -R golden_render (or with
FLAM_UPDATE_GOLDEN=1 to let the C++ harness regenerate it).
"""

import struct
import wave
import math
import os

# ── Parameters matching GoldenRenderTest.cpp ───────────────────────────────
SAMPLE_RATE   = 44100
BLOCK_SIZE    = 512
SEED          = 42
MIDI_NOTE     = 36
VELOCITY_BYTE = 102        # uint8 MIDI velocity
VELOCITY_FLOAT = VELOCITY_BYTE / 127.0   # ≈ 0.80315
HIT_SAMPLES   = [0, 11025, 22050, 33075]   # every 250 ms
TOTAL_SAMPLES = 44100      # 1 second render

# ADSR from fixture flamkit.yaml (applied by SampleVoice.setEnvelopeParameters)
ATTACK_TIME   = 0.001   # 1 ms
DECAY_TIME    = 0.01    # 10 ms
SUSTAIN_LEVEL = 0.0
RELEASE_TIME  = 0.005   # 5 ms  (not used in practice: sample ends first)

# Layer gain from YAML
LAYER_GAIN = 1.0

# ── JUCE Random (48-bit LCG) ────────────────────────────────────────────────
class JuceRandom:
    def __init__(self, seed: int):
        # Replicate: int64 seed stored in a 64-bit field, but the LCG mask
        # is 48-bit (& 0xFFFFFFFFFFFF).  Internally treated as unsigned.
        self.seed = int(seed) & 0xFFFF_FFFF_FFFF_FFFF

    def _next_int_raw(self) -> int:
        """juce::Random::nextInt() → returns unsigned 32-bit value."""
        self.seed = (self.seed * 0x5DEECE66D + 11) & 0xFFFF_FFFF_FFFF
        return (self.seed >> 16) & 0xFFFF_FFFF   # 32-bit unsigned

    def next_int(self, max_value: int) -> int:
        """juce::Random::nextInt(int maxValue).  Returns [0, max_value)."""
        if max_value <= 1:
            self._next_int_raw()   # always consume an RNG call
            return 0
        raw = self._next_int_raw()
        return int((raw * max_value) >> 32)


# ── JUCE ADSR (linear rates, state-machine) ──────────────────────────────
IDLE = 0; ATTACK = 1; DECAY = 2; SUSTAIN = 3; RELEASE = 4

def _adsr_rate(distance, time_s, sr):
    return distance / (time_s * sr) if time_s > 0.0 else -1.0

ATTACK_RATE  = _adsr_rate(1.0,                  ATTACK_TIME,  SAMPLE_RATE)
DECAY_RATE   = _adsr_rate(1.0 - SUSTAIN_LEVEL,  DECAY_TIME,   SAMPLE_RATE)

class ADSR:
    def __init__(self):
        self.env_val     = 0.0
        self.state       = IDLE
        self.release_rate = 0.0

    def note_on(self):
        if ATTACK_RATE > 0.0:
            self.state = ATTACK
        elif DECAY_RATE > 0.0:
            self.env_val = 1.0
            self.state = DECAY
        else:
            self.env_val = SUSTAIN_LEVEL
            self.state = SUSTAIN

    def get_next_sample(self) -> float:
        if self.state == IDLE:
            return 0.0
        elif self.state == ATTACK:
            self.env_val += ATTACK_RATE
            if self.env_val >= 1.0:
                self.env_val = 1.0
                self.state = DECAY if DECAY_RATE > 0.0 else SUSTAIN
        elif self.state == DECAY:
            self.env_val -= DECAY_RATE
            if self.env_val <= SUSTAIN_LEVEL:
                self.env_val = SUSTAIN_LEVEL
                self.state = SUSTAIN
        elif self.state == SUSTAIN:
            self.env_val = SUSTAIN_LEVEL
        elif self.state == RELEASE:
            self.env_val -= self.release_rate
            if self.env_val <= 0.0:
                self.env_val = 0.0
                self.state = IDLE
        return self.env_val

    def is_active(self) -> bool:
        return self.state != IDLE


# ── WAV I/O helpers ─────────────────────────────────────────────────────────
def read_wav_float(path):
    """Returns list of (left, right) float pairs."""
    with wave.open(path, 'r') as wf:
        n_ch   = wf.getnchannels()
        n_fr   = wf.getnframes()
        sw     = wf.getsampwidth()
        raw    = wf.readframes(n_fr)
    if sw == 2:
        count  = n_fr * n_ch
        ints   = struct.unpack(f"<{count}h", raw)
        scale  = 32768.0
    elif sw == 3:
        ints = []
        for i in range(0, len(raw), 3):
            v = int.from_bytes(raw[i:i+3], 'little', signed=True)
            ints.append(v)
        scale = 8388608.0
    else:
        raise ValueError(f"Unsupported sample width {sw}")
    floats = [v / scale for v in ints]
    if n_ch == 2:
        return list(zip(floats[0::2], floats[1::2]))
    else:
        return list(zip(floats, floats))


def write_wav_24bit(path, samples_lr, sr):
    """Write list of (left, right) float tuples as 24-bit stereo WAV."""
    n = len(samples_lr)
    with wave.open(path, 'w') as wf:
        wf.setnchannels(2)
        wf.setsampwidth(3)
        wf.setframerate(sr)
        frames = bytearray()
        for l, r in samples_lr:
            for v in (l, r):
                v = max(-1.0, min(1.0, v))
                i = max(-8388608, min(8388607, round(v * 8388607.0)))
                frames += i.to_bytes(3, 'little', signed=True)
        wf.writeframes(bytes(frames))
    print(f"  Wrote {n} frames → {path}")


# ── Core rendering simulation ────────────────────────────────────────────────
def render():
    fixtures_root = os.path.dirname(__file__)
    samples_dir   = os.path.join(fixtures_root, "golden-kit", "samples")

    layer_data = [
        read_wav_float(os.path.join(samples_dir, "kick_a.wav")),  # idx 0
        read_wav_float(os.path.join(samples_dir, "kick_b.wav")),  # idx 1
    ]
    n_layers = len(layer_data)

    rng = JuceRandom(SEED)

    # Simulates RecentSampleHistory (MAX_HISTORY=10, historySize=min(3,n-1)=1)
    history_size = min(3, n_layers - 1)  # = 1
    recent = []   # circular list of recently played layer indices (newest last)

    # Output buffer
    out = [(0.0, 0.0)] * TOTAL_SAMPLES

    # All active voices: (adsr, layer_idx, playback_pos, samples)
    active_voices = []

    hit_idx = 0
    n_hits  = len(HIT_SAMPLES)

    for block_start in range(0, TOTAL_SAMPLES, BLOCK_SIZE):
        block_end = min(block_start + BLOCK_SIZE, TOTAL_SAMPLES)
        block_len = block_end - block_start

        # ── 1. Process MIDI events (all at once, before render — mirrors C++) ──
        while hit_idx < n_hits and HIT_SAMPLES[hit_idx] < block_end:
            # Filter layers by recent history
            avoid = set(recent[-history_size:]) if history_size > 0 else set()
            available = [i for i in range(n_layers) if i not in avoid]
            if not available:
                available = list(range(n_layers))

            # RNG pick (even if len==1, one RNG call is consumed)
            chosen = available[rng.next_int(len(available))]
            recent.append(chosen)
            if len(recent) > 10:  # MAX_HISTORY
                recent.pop(0)

            adsr = ADSR()
            adsr.note_on()
            active_voices.append([adsr, chosen, 0.0])  # [adsr, layer_idx, pos]
            hit_idx += 1

        # ── 2. Render all active voices into block ─────────────────────────────
        block_buf = [0.0] * (block_len * 2)  # interleaved L/R

        for voice in active_voices:
            adsr, layer_idx, pos = voice
            data = layer_data[layer_idx]
            total_len = len(data)

            for s in range(block_len):
                pos_int = int(pos)
                if pos_int >= total_len:
                    adsr.env_val = 0.0
                    adsr.state = IDLE
                    break

                env = adsr.get_next_sample()
                if not adsr.is_active():
                    break

                final_gain = LAYER_GAIN * VELOCITY_FLOAT * env
                frac = pos - pos_int

                l0, r0 = data[pos_int]
                if pos_int + 1 < total_len:
                    l1, r1 = data[pos_int + 1]
                else:
                    l1, r1 = l0, r0

                sl = (l0 + frac * (l1 - l0)) * final_gain
                sr = (r0 + frac * (r1 - r0)) * final_gain

                block_buf[s * 2]     += sl
                block_buf[s * 2 + 1] += sr
                pos += 1.0   # playback_rate = src_SR / target_SR = 1.0

            voice[2] = pos  # update position

        # ── 3. Accumulate block into output ────────────────────────────────────
        for s in range(block_len):
            out[block_start + s] = (
                out[block_start + s][0] + block_buf[s * 2],
                out[block_start + s][1] + block_buf[s * 2 + 1],
            )

        # ── 4. Remove finished voices ──────────────────────────────────────────
        active_voices = [v for v in active_voices if v[0].is_active()]

    return out


if __name__ == "__main__":
    print("Simulating FlamKit render pipeline to generate golden reference...")
    output = render()

    golden_dir  = os.path.join(os.path.dirname(__file__), "goldens")
    os.makedirs(golden_dir, exist_ok=True)
    golden_path = os.path.join(golden_dir, "golden_render.wav")

    write_wav_24bit(golden_path, output, SAMPLE_RATE)

    nonzero = sum(1 for l, r in output if abs(l) > 1e-9 or abs(r) > 1e-9)
    print(f"  Non-zero frames: {nonzero} / {TOTAL_SAMPLES}")
    print("Done. Run: ctest -R golden_render  (or FLAM_UPDATE_GOLDEN=1 ./flam-tests)")
