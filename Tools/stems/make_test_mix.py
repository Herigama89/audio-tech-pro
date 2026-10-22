#!/usr/bin/env python
"""A 20-second test mix with a part for each stem: a kick and hi-hats, a bass line, chords, a sung-like
melody (harmonics with vibrato and formants), so every source has something the model can find."""
import sys
import numpy as np
import soundfile as sf

sr = 44100
t = np.arange(sr * 20) / sr
rng = np.random.default_rng(1)
beat = 60 / 110
drums = np.zeros_like(t)
for k in range(int(20 / beat * 2)):
    start = int(k * beat / 2 * sr)
    n = np.arange(min(sr // 4, len(t) - start))
    if k % 2 == 0:   # kick
        drums[start:start + len(n)] += 0.8 * np.sin(2 * np.pi * (50 + 120 * np.exp(-n / 900)) * n / sr) * np.exp(-n / 5000)
    hat = rng.standard_normal(len(n)) * np.exp(-n / 600) * 0.15
    drums[start:start + len(n)] += np.diff(np.concatenate([[0], hat]))
notes = [55, 55, 58, 53]
bass = np.zeros_like(t)
chords = np.zeros_like(t)
for bar in range(10):
    s, e = int(bar * 2 * sr), int((bar + 1) * 2 * sr)
    f = 440 * 2 ** ((notes[bar % 4] - 69 - 24) / 12)
    bass[s:e] = 0.35 * np.tanh(2 * np.sin(2 * np.pi * f * t[s:e]))
    for interval in (0, 4, 7):
        g = 440 * 2 ** ((notes[bar % 4] - 69 + interval) / 12)
        chords[s:e] += 0.06 * np.sign(np.sin(2 * np.pi * g * t[s:e])) * (1 - np.exp(-(t[s:e] - t[s]) * 30))
melody = np.zeros_like(t)
for k in range(40):
    s, e = int(k * 0.5 * sr), int((k + 1) * 0.5 * sr)
    f0 = 440 * 2 ** ((notes[(k // 4) % 4] + [12, 14, 16, 19][k % 4] - 69) / 12) * (1 + 0.01 * np.sin(2 * np.pi * 5.5 * t[s:e]))
    phase = 2 * np.pi * np.cumsum(f0) / sr
    for h in range(1, 12):
        formant = np.exp(-((h * f0 - 700) / 400) ** 2) + 0.5 * np.exp(-((h * f0 - 1200) / 500) ** 2) + 0.05
        melody[s:e] += 0.08 * formant / h ** 0.5 * np.sin(h * phase)
    melody[s:e] *= np.minimum(1, (t[s:e] - t[s]) * 20) * np.minimum(1, (t[e - 1] - t[s:e]) * 20 + 0.01)
mix = drums + bass + chords + melody
stereo = np.stack([mix + 0.3 * chords, mix - 0.3 * chords], 1) * 0.6
sf.write(sys.argv[1], stereo.astype(np.float32), sr, subtype="FLOAT")
