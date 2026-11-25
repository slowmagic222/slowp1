#!/usr/bin/env python3
"""Write bows_tables.h (the envelope, stretch and filter tables bows.cpp reads; no float on the unit)."""
import math, textwrap
B = 88200 / 128
def arr(name, vals):
    body = ', '.join(f'{v}u' for v in vals)
    return f'static const u32 {name}[{len(vals)}] = {{\n    ' + '\n    '.join(textwrap.wrap(body, 116)) + '\n};\n'
att = [min(1 << 30, round((1 << 30) / (0.004 * 1000 ** (v / 100) * B))) for v in range(101)]
rel = [round((1 << 30) * 10 ** (-3 / (0.12 * 200 ** (v / 100) * B))) for v in range(101)]
spd = [round(65536 * 2 ** (-6 * v / 100)) for v in range(101)]
cut = [round(32767 * (1 - math.exp(-2 * math.pi * min(440 * 2 ** ((n - 69) / 12), 16000) / 88200))) for n in range(128)]
out = '// Made by tools/tables.py (python3 -I tools/tables.py > bows_tables.h); do not edit by hand.\n'
out += '// The envelope a block (128 frames at 88.2 kHz): attack from 4 ms (bow 0) to 4 s (bow 100), Q30 a block.\n' + arr('ATTACK_INC', att)
out += '// Release to -60 dB from 0.12 s (0) to 24 s (100): the level times this a block, Q30.\n' + arr('RELEASE_MUL', rel)
out += '// Stretch: how fast the grains walk through the recording, Q16 of its own speed: 1 (0) to 1/64 (100).\n' + arr('STRETCH_SPEED', spd)
out += '// A one-pole low-pass at the pitch of note n (16 kHz at most), Q15, for the ensemble.\n' + arr('CUT', cut)
print(out, end='')
# Velocity: the unit does NOT lower our engines by velocity (v4's boost on that guess overdrove), so the engine makes a
# gentle velocity loudness itself: 0.8 + 0.2 v/127, never above unity (as a velocity LFO on amp at 20).
DEPTH = 0.20
vel = [round(32767 * (1 - DEPTH * (1 - v / 127))) for v in range(128)]
print('// Velocity loudness, Q15: 0.8 + 0.2 v/127 (DEPTH 20 %), never above unity.\n' + arr('VEL_GAIN', vel), end='')
