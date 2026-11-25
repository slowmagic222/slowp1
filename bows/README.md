# bows

A string section: contrabass, cellos, violas and violins from real recordings, with a grain stretch. An open-field **synth engine**. 6 voices, stereo.

Demo: _coming soon_

## Install

`bows.pack` into `packs`, `bows.msb` into the `bows` folder. After an update, delete `synth_presets/bows`. Pick it with **shift + T1** on a synth sound. Two sounds come with it: `bows` and `bows ambient`. The bank is big (about 3 MB); if the base has no room, the page says so.

## Knobs

| Knob | Turn | Shift + turn |
| --- | --- | --- |
| blue | bow (attack, up to a 4 s swell) | release |
| ochre | stretch (grains, up to 1/64 speed) | warmth |
| grey | style: as recorded, octave up, two octaves up | slide (a new note near a held one slides to its pitch) |
| red | ambient | width |

**Grey press** steps the style. **Red press** = velocity on/off (saved in `bows.cfg`). On: soft or loud recording by velocity. Off: always loud.

## Files

`bows.cpp` engine and page, `bows_scene*.h` page, `core/` sample core, `recipe.json`, `tools/vsco_to_msb.py` bank builder, `tests/`, `bank/` sample credits.

Samples: VSCO 2 Community Edition by Versilian Studios (Sam Gossner), CC0. Code: GPL-3.0-or-later.
