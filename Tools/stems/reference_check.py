#!/usr/bin/env python
"""Checks the app's stem separator (StemCheck, C++ with ONNX Runtime) against Demucs itself (PyTorch).

Both run the same audio with the same half-second silent margins and no random shift, so they chunk it the
same way; then the only differences left are the STFT, the padding, the segments' blending and the exported
network - which must agree to well past what anyone could hear.

    reference_check.py <model name: htdemucs | htdemucs_6s> <stemcheck output folder> <in.wav>
"""
import sys
import numpy as np
import torch
import soundfile as sf
from demucs.pretrained import get_model
from demucs.apply import apply_model

name, folder, wav = sys.argv[1], sys.argv[2], sys.argv[3]
mix, rate = sf.read(wav, dtype="float32", always_2d=True)
assert rate == 44100
mix = torch.from_numpy(mix.T.copy())
ref = mix.mean(0)
mean, std = ref.mean(), ref.std()
normalized = (mix - mean) / std
margin = 44100 // 2
padded = torch.nn.functional.pad(normalized, (margin, margin))

model = get_model(name)
model.eval()
with torch.no_grad():
    out = apply_model(model, padded[None], shifts=0, split=True, overlap=0.25)[0]
out = out[..., margin:margin + mix.shape[-1]] * std + mean

names = ["drums", "bass", "other", "vocals", "guitar", "piano"][: out.shape[0]]
worst = 1e9
for i, n in enumerate(names):
    ours, _ = sf.read(f"{folder}/{n}.wav", dtype="float32", always_2d=True)
    ours = torch.from_numpy(ours.T.copy())
    reference = out[i]
    error = reference - ours
    sdr = 10 * np.log10(float((reference ** 2).sum()) / max(1e-20, float((error ** 2).sum())))
    level = 10 * np.log10(max(1e-20, float((reference ** 2).mean())))
    print(f"{n:7s}  level {level:7.1f} dB   agreement {sdr:6.1f} dB")
    if level > -60:   # (a source the model found nothing of can't be compared by ratio)
        worst = min(worst, sdr)
print(f"worst agreement: {worst:.1f} dB")
sys.exit(0 if worst > 30 else 1)
