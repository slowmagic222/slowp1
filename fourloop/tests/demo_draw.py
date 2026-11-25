#!/usr/bin/env python3
"""Draws tests/demo_sim's frames (one line of shapes per frame) as PNGs, for fourloop/demo.gif.

    python3 tests/demo_draw.py frames.txt outdir
    ffmpeg -framerate 12 -i outdir/f%04d.png -vf "split[a][b];[a]palettegen=max_colors=32:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" demo.gif

Drawn at 960 x 440 (twice the screen), unsmoothed like the unit's own screen, which keeps the GIF small
(SS=8 draws at 8x and scales down, for smooth edges, at about three times the size). Shapes and
colours are the picture's own (tools/make_svg.py); the icons are its sync/free and play/pause.
"""
import os
import sys
from PIL import Image, ImageDraw

W, H = 480, 220
SS, OUT = int(os.environ.get("SS", "2")), 2
LIGHT = (0xCD, 0xD0, 0xC2)


def rgb(h):
    return tuple(int(h[k:k + 2], 16) for k in (0, 2, 4))


def frame(line):
    img = Image.new("RGBA", (W * SS, H * SS), (0, 0, 0, 255))
    d = ImageDraw.Draw(img)
    p = lambda v: float(v) * SS
    for op in line.strip().split(";"):
        f = op.split()
        if not f:
            continue
        if f[0] == "C":
            x, y, r = p(f[1]), p(f[2]), p(f[3])
            fill, op_, stroke, sw = f[4], float(f[5]), f[6], p(f[7])
            box = [x - r, y - r, x + r, y + r]
            if fill != "none":
                if op_ < 1:
                    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
                    ImageDraw.Draw(layer).ellipse(box, fill=rgb(fill) + (int(op_ * 255),))
                    if stroke != "none":
                        ImageDraw.Draw(layer).ellipse([box[0] - sw / 2, box[1] - sw / 2, box[2] + sw / 2, box[3] + sw / 2],
                                                      outline=rgb(stroke) + (int(op_ * 255),), width=max(1, round(sw)))
                    img.alpha_composite(layer)
                    d = ImageDraw.Draw(img)
                else:
                    d.ellipse(box, fill=rgb(fill))
            elif stroke != "none":
                h = sw / 2
                d.ellipse([box[0] - h, box[1] - h, box[2] + h, box[3] + h], outline=rgb(stroke), width=max(1, round(sw)))
        elif f[0] == "L":
            a, b = (p(f[1]), p(f[2])), (p(f[3]), p(f[4]))
            c, w = rgb(f[5]), p(f[6])
            d.line([a, b], fill=c, width=max(1, round(w)))
            for q in (a, b):                                   # round caps
                d.ellipse([q[0] - w / 2, q[1] - w / 2, q[0] + w / 2, q[1] + w / 2], fill=c)
        elif f[0] == "T":
            d.polygon([(p(f[1]), p(f[2])), (p(f[3]), p(f[4])), (p(f[5]), p(f[6]))], fill=rgb(f[7]))
        elif f[0] == "I":
            sync, paused = int(f[1]), int(f[2])
            w = 1.5 * SS
            d.ellipse([p(462 - 6) - w / 2, p(14 - 6) - w / 2, p(462 + 6) + w / 2, p(14 + 6) + w / 2], outline=LIGHT, width=round(w))
            if not sync:
                d.line([(p(455), p(21)), (p(469), p(7))], fill=LIGHT, width=round(w))
            if paused:
                for x in (439, 446):
                    d.line([(p(x), p(9)), (p(x), p(19))], fill=LIGHT, width=round(2.5 * SS))
            else:
                d.polygon([(p(438), p(8)), (p(448), p(14)), (p(438), p(20))], fill=LIGHT)
    return img.convert("RGB").resize((W * OUT, H * OUT), Image.LANCZOS)


src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
for n, line in enumerate(open(src)):
    frame(line).save(os.path.join(dst, "f%04d.png" % n))
