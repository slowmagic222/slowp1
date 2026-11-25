#!/usr/bin/env python3
"""Make bows.msb (MSB1, see felt-field-v1/bank-format.md) from VSCO 2 Community Edition's section sustains (CC0).

usage: python3 -I vsco_to_msb.py <VSCO-2-CE dir> <out.msb>   (needs numpy, scipy)

Orchestral split by register: contrabass below C2, cello section C2..E3, viola section F3..E4, violin section F4 up.
Each note: two dynamics (the softest and the loudest recording), velocity 0..71 soft, 72..127 loud. VSCO names its
notes an octave low (its "A2" sounds A3), so roots come from the file name plus 12 and are checked by pitch.
Mono (L+R), retuned to the root, 32 kHz, 4.6 s from the bow's start, the loop (ping-pong in the core) from 1.3 s to 4.4 s. Every recording
is stored near full scale; the zone gain sets the level (loud layers matched, soft 7 dB under).
"""
import os, re, struct, sys, zlib
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly, butter, sosfiltfilt

RATE, SECS, LOOP_IN, LOOP_OUT = 32000, 4.6, 1.3, 4.4
SOFT_DB = -7.0
SECTIONS = [  # folder, file regex (note, dynamic), lowest, highest note it serves
    ("Solo Contrabass/SusVib", r"_SusVib_([A-G]#?)(\d)_v(\d)_", 0, 35),
    ("Cello Section/susvib", r"susvib_([A-G]#?)(\d)_v(\d)_1", 36, 52),
    ("Viola Section/susvib", r"_susvib_([A-G]#?)(\d)_v(\d)_1", 53, 64),
    ("Violin Section/susVib", r"_susVib_([A-G]#?)(\d)_v(\d)", 65, 127),
]
NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
STEPS = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,
    230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,
    3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,
    24623,27086,29794,32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def encode(pcm):
    """IMA 4-bit, 128 frames a 68-byte block, the decoder (ms_decode) in the loop. Each block restarts at its first
    sample with the running step index."""
    out = bytearray(); idx = 0
    pcm = list(int(v) for v in pcm) + [0] * (-len(pcm) % 128)
    for b in range(0, len(pcm), 128):
        blk = pcm[b:b + 128]; pred = blk[0]
        codes = []
        for s in blk[1:]:
            step = STEPS[idx]; d = s - pred; code = 0
            if d < 0: code = 8; d = -d
            diff = step >> 3
            if d >= step: code |= 4; d -= step; diff += step
            if d >= step >> 1: code |= 2; d -= step >> 1; diff += step >> 1
            if d >= step >> 2: code |= 1; diff += step >> 2
            pred = pred - diff if code & 8 else pred + diff
            pred = max(-32768, min(32767, pred))
            idx = max(0, min(88, idx + IDX[code & 7])); codes.append(code)
        codes.append(0)
        out += struct.pack("<hBB", blk[0], start_idx[0], 0)
        out += bytes((codes[2 * i] | (codes[2 * i + 1] << 4)) for i in range(64))
        start_idx[0] = idx
    return bytes(out)

start_idx = [0]


def read(path):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1: x = x.mean(axis=1)
    peak = np.abs(x).max()
    return sr, x / (peak if peak else 1)


def pitch(x, sr):
    """The median over windows from 1.0 to 4.0 s (vibrato moves any one window)."""
    return float(np.median([pitch_at(x, sr, t) for t in np.arange(1.0, 4.0, 0.25)]))


def pitch_at(x, sr, t):
    seg = x[int(sr * t):int(sr * t) + 8192]; seg = seg - seg.mean(); n = len(seg)
    d = np.array([np.sum((seg[:n - t] - seg[t:]) ** 2) for t in range(1, 2400)])
    cm = d * np.arange(1, 2400) / np.maximum(np.cumsum(d), 1e-12)
    best = None
    for i in range(10, len(cm) - 1):
        if cm[i] < 0.15 and cm[i] <= cm[i - 1] and cm[i] <= cm[i + 1]: best = i; break
    if best is None: best = int(np.argmin(cm[10:-1])) + 10
    a, b, c = cm[best - 1], cm[best], cm[best + 1]           # the lag between samples: a parabola through the dip
    lag = best + 1 + (0.5 * (a - c) / (a - 2 * b + c) if a - 2 * b + c > 0 else 0.0)
    return 69 + 12 * np.log2(sr / lag / 440)


def prepare(path, root):
    """Mono at 32 kHz, retuned to the root (the recordings sit up to 20 cents off), from the bow's start."""
    sr, x = read(path)
    assert sr == 44100, (path, sr)
    x = sosfiltfilt(butter(2, 28, "hp", fs=sr, output="sos"), x)
    off = pitch(x, sr) - root
    assert abs(off) < 0.6, (path, root, off)
    down = int(round(4410 * 2 ** (-off / 12)))     # sharp: fewer source frames an output frame, pitch down
    a = resample_poly(x, 3200, down)
    env = np.abs(a); hot = np.flatnonzero(env > 0.02)
    start = max(0, int(hot[0]) - int(0.012 * RATE)) if len(hot) else 0
    a = a[start:start + int(SECS * RATE)].copy()
    assert len(a) == int(SECS * RATE), (path, len(a))
    a[:16] *= np.linspace(0, 1, 16); f = int(0.05 * RATE); a[-f:] *= np.linspace(1, 0, f)
    sus = np.sqrt(np.mean(a[int(LOOP_IN * RATE):int(LOOP_OUT * RATE)] ** 2))
    a *= 0.89 / np.abs(a).max()
    return np.rint(a * 32767).astype(np.int16), sus * 0.89 / 1.0


def main():
    if len(sys.argv) < 3: sys.exit(__doc__)
    root_dir, out = sys.argv[1], sys.argv[2]
    zones = []  # (root, low, high, dyn, pcm, sustain rms, name)
    for folder, rx, low, high in SECTIONS:
        notes = {}
        for f in sorted(os.listdir(os.path.join(root_dir, "Strings", folder))):
            m = re.search(rx, f)
            if not m: continue
            note = 12 * (int(m.group(2)) + 1) + NOTE[m.group(1)] + 12
            notes.setdefault(note, {}).setdefault(int(m.group(3)), os.path.join(root_dir, "Strings", folder, f))
        roots = sorted(notes)
        # keep the recordings that serve this register: the nearest root on each side of it and all inside
        keep = [r for r in roots if low <= r <= high]
        below = [r for r in roots if r < low]; above = [r for r in roots if r > high]
        if not keep or keep[0] - low > 1 and below: keep = [below[-1]] + keep if below and low - below[-1] < 4 else keep
        if high < 127 and above and (not keep or high - keep[-1] > 2): keep = keep + [above[0]]
        if low == 0: keep = [r for r in roots if r <= high + 2][:4]
        for i, r in enumerate(keep):
            lo = low if i == 0 else (keep[i - 1] + r) // 2 + 1
            hi = high if i == len(keep) - 1 else (r + keep[i + 1]) // 2
            lo, hi = max(lo, low), min(hi, high)
            if lo > hi: continue
            dyns = sorted(notes[r])
            for take, dyn in ((1, dyns[0]), (2, dyns[-1])):
                path = notes[r][dyn]
                pcm, sus = prepare(path, r)
                heard = pitch(pcm.astype(np.float64) / 32767, RATE)
                assert abs(heard - r) < 0.6, (path, r, heard)
                zones.append(dict(root=r, low=lo, high=hi, take=take, pcm=pcm, sus=sus, file=os.path.relpath(path, root_dir), cents=round((heard - r) * 100)))
    loud = min(z["sus"] for z in zones if z["take"] == 2)
    audio = bytearray(); recs = b""
    for z in zones:
        target = loud * (10 ** (SOFT_DB / 20) if z["take"] == 1 else 1.0)
        gain = int(round(4096 * target / z["sus"]))
        start_idx[0] = 0
        enc = encode(z["pcm"]); off = len(audio); audio += enc
        n = len(z["pcm"]); vlo, vhi = (0, 71) if z["take"] == 1 else (72, 127)
        rec = struct.pack("<5I8B5HH", off, n, int(LOOP_IN * RATE), int(LOOP_OUT * RATE), n,
                          z["root"], z["low"], z["high"], 0, vlo, vhi, 1, 128, 4096, 4096, 4096, 4096, 4096, gain)
        recs += rec + bytes(48 - len(rec))
        print(f'{z["root"]:3d} {z["low"]:3d}-{z["high"]:3d} {"soft" if z["take"] == 1 else "loud"} gain {gain / 4096:.2f} {z["cents"]:+d}c  {z["file"]}')
    head_size = 64; audio_at = (head_size + len(recs) + 3) & ~3
    body = recs + bytes(audio_at - head_size - len(recs)) + bytes(audio)
    total = head_size + len(body)
    head = struct.pack("<4sHHIIIHHIIIIIHH16s", b"MSB1", 1, head_size, total, zlib.crc32(body), RATE, len(zones), 48,
                       head_size, 0, 0, audio_at, len(audio), 0, 0, b"bows".ljust(16, b"\0"))
    open(out, "wb").write(head + body)
    print(f"{out}: {total} bytes, {len(zones)} zones, {RATE} Hz")


if __name__ == "__main__":
    main()
