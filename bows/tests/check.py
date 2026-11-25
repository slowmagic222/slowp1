#!/usr/bin/env python3
"""Reads host_test's takes: the pitch heard against the note played, the largest jump between frames (clicks), the
level while held. usage: python3 -I check.py <out dir>"""
import glob, os, sys, re
import numpy as np
from scipy.io import wavfile

def pitch(x, sr):
    seg = x[:16384] - x[:16384].mean(); n = len(seg); M = min(4000, n // 2)
    d = np.array([np.sum((seg[:n - t] - seg[t:]) ** 2) for t in range(1, M)])
    cm = d * np.arange(1, M) / np.maximum(np.cumsum(d), 1e-12)
    best = next((i for i in range(10, len(cm) - 1) if cm[i] < 0.15 and cm[i] <= cm[i - 1] and cm[i] <= cm[i + 1]), int(np.argmin(cm[10:-1])) + 10)
    a, b, c = cm[best - 1], cm[best], cm[best + 1]
    lag = best + 1 + (0.5 * (a - c) / (a - 2 * b + c) if a - 2 * b + c > 0 else 0)
    return 69 + 12 * np.log2(sr / lag / 440)

bad = 0
for f in sorted(glob.glob(os.path.join(sys.argv[1], '*.wav'))):
    name = os.path.basename(f)[:-4]
    sr, x = wavfile.read(f); x = x.astype(float)
    m = x.mean(axis=1)
    note = int(re.findall(r'-(\d+)', name)[-1]) if re.search(r'-\d+', name) else None
    held = m[int(0.5 * sr):int(0.6 * sr) + 16384]
    if name.startswith('slide'): held = m[-int(0.45 * sr):]   # v12: slides take up to 3 s: heard at the take's end
    rms = np.sqrt(np.mean(held ** 2)) if len(held) else 0
    jump = np.abs(np.diff(x, axis=0)).max()
    line = f'{name:16s} rms {rms:7.0f} biggest step {jump:6.0f}'
    if note is not None and 28 <= note <= 100 and not name.startswith(('ends', 'bend', 'swell')) and rms > 200:
        heard = pitch(held, sr); cents = (heard - note) * 100
        line += f'  pitch {cents:+6.1f} c'
        if abs(cents) > 12: line += '  <-- off'; bad += 1
    print(line)
sys.exit(1 if bad else 0)
