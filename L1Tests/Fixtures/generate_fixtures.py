#!/usr/bin/env python3
"""
Generate synthetic WAV fixtures for the golden-render harness (CTEST-5).

Produces tiny 2-channel 44100Hz WAVs containing sine waves.
These are committed to the repo so the test harness has a known,
deterministic input that doesn't depend on real drum samples.

Re-run this script only when you intentionally want to change the
fixture samples (which will also require regenerating the golden
reference with --update-golden).
"""

import wave
import struct
import math
import os

SAMPLE_RATE = 44100
DURATION_MS = 5           # 5ms fits entirely within VoiceManager's 5ms preload window
NUM_CHANNELS = 2
BIT_DEPTH = 16
MAX_AMP = 32767 * 0.5     # 50% amplitude to leave headroom

OUT_DIR = os.path.join(os.path.dirname(__file__), "golden-kit", "samples")
os.makedirs(OUT_DIR, exist_ok=True)

def write_sine_wav(filename, freq_hz):
    """Write a stereo sine wave WAV at the given frequency."""
    num_samples = int(SAMPLE_RATE * DURATION_MS / 1000)
    path = os.path.join(OUT_DIR, filename)
    with wave.open(path, "w") as wf:
        wf.setnchannels(NUM_CHANNELS)
        wf.setsampwidth(BIT_DEPTH // 8)
        wf.setframerate(SAMPLE_RATE)
        frames = bytearray()
        for i in range(num_samples):
            t = i / SAMPLE_RATE
            sample = int(math.sin(2 * math.pi * freq_hz * t) * MAX_AMP)
            sample = max(-32768, min(32767, sample))
            # Interleave L and R (same signal for simplicity)
            frames += struct.pack("<hh", sample, sample)
        wf.writeframes(bytes(frames))
    print(f"  wrote {path}  ({num_samples} samples @ {freq_hz}Hz)")

print("Generating golden-kit fixture WAVs...")
write_sine_wav("kick_a.wav", 440)   # 440 Hz sine — layer A
write_sine_wav("kick_b.wav", 660)   # 660 Hz sine — layer B (different pitch for distinguishability)
print("Done. Commit the generated .wav files.")
