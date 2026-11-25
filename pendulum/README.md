# pendulum

A sequencer of swinging notes. Each key you press starts a pendulum that plays the note every time it crosses the middle, forever. Many keys swing at once and drift in and out of phase. An open-field **sequencer**, for synth and drum.

Demo: _coming soon_

## Install

`pendulum.pack` into `packs`. Pick `pendulum` at the end of the sequencer list. Hold **shift** while playing a key to play it straight (no swing).

## Knobs

| Knob | Turn | Press | Shift + press |
| --- | --- | --- | --- |
| blue | quantize (0 free, 100 on the grid) | grid: 1/4 to 1/32 | sync to tempo / free time |
| ochre | gravity (up = faster) | | |
| grey | drift (how fast swings spread apart) | | |
| red | scale (root C) | clear all swings | launch every note of the scale |

Hold **help** to see every control. Settings are saved in `pendulum.bin`.

## Files

`pendulum.cpp` player and page, `recipe.json`, `pendulum.svg.make`, `tests/`.

Code: GPL-3.0-or-later.
