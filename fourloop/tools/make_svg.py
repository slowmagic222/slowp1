#!/usr/bin/env python3
"""Writes fourloop/4loop.svg.make: 4loop's page as open-field picture instructions (kind 2).

The frame of the picture (its <svg> root and closing tag) is taken from the unit's own tempo.svg by the same
two stretches Faust's example yoyo uses (tempo.svg: 43996 bytes, CRC-32 eb050950, the same in OS 1.7.0 and 1.7.3), so the pack
carries no byte of Teenage Engineering's. Everything between is ours: per loop a fill disc, a ring, an
empty-loop ring, a playhead, a record disc and 32 radial bars, all drawn at the origin at a unit size, which
the pack moves and scales every frame (fl_circle_matrix: r = 64; fl_line_matrix: (0,0) to (64,0)). Then
the sync and pause glyphs and five text placeholders. Only elements and attributes the firmware's own
pictures use: g, circle, line, path (M, H, V, L, z), fill, stroke, stroke-width, stroke-linecap.

    python3 tools/make_svg.py <out file>
"""
import sys

KNOB = ["#2E4A7D", "#D37E4E", "#A2A4A6", "#E52817"]      # the knobs as they look (navy, copper orange, grey, red), lit enough for the screen


def tone(colour, share):                                 # the colour toward black: share 1 is the colour itself
    c = [int(colour[k:k + 2], 16) for k in (1, 3, 5)]
    return "#%02X%02X%02X" % tuple(round(v * share) for v in c)


FILL_STEPS = 5                                           # fourloop.cpp FILL_STEPS: the disc by volume, a fifth a step
FILL = [[tone(k, 0.55 * (j + 1) / FILL_STEPS) for j in range(FILL_STEPS)] for k in KNOB]
OFF = [tone(k, 0.12) for k in KNOB]                      # the part of a loop that does not play
IDLE_FILL = [tone(k, 0.22) for k in KNOB]                # an empty loop: a faint disc and ring in its knob's colour (classic)
IDLE_RING = [tone(k, 0.5) for k in KNOB]
FILL_OPACITY = 0.75                                      # the unit's own pictures use opacity (ftwo.svg, tape.svg): loops show through each other
LIGHT = "#CDD0C2"                                        # palette.svg lightgrey: loop numbers, logo, icons
# Loop numbers as strokes, about 7 x 10 px round the origin (moved with the loop's centre, never scaled)
DIGIT = ["M0,-5V5M-2.5,-2.5L0,-5", "M-3,-5H3V0H-3V5H3", "M-3,-5H3V5H-3M-3,0H3", "M-3,-5V1H3M3,-5V5"]
SHADES = 16                                              # loop_scene.h LS_SHADE_MAX
RED = "#FF0000"
GREY, DIM = "#5B6670", "#373D42"
BARS = 32

TRI = '<path fill="%s" stroke="none" d="M0,0L64,0L0,64Z"/>'          # moved onto any triangle (fourloop.cpp tri_matrix)
LINE = '<line fill="none" stroke="%s" stroke-width="%s" stroke-linecap="round" x1="0" y1="0" x2="64" y2="0"/>'
body = []
for i in range(4):
    n = i + 1
    body.append('<g id="e%d"><circle fill="%s" opacity="%s" stroke="%s" stroke-width="1.5" cx="0" cy="0" r="64"/></g>'
                % (n, IDLE_FILL[i], FILL_OPACITY, IDLE_RING[i]))
    for j in range(FILL_STEPS):
        body.append('<g id="f%d_%d"><circle fill="%s" opacity="%s" stroke="none" cx="0" cy="0" r="64"/></g>' % (n, j, FILL[i][j], FILL_OPACITY))
    for k in range(SHADES):
        body.append('<g id="s%d_%d">%s</g>' % (n, k, TRI % OFF[i]))
    body.append('<g id="r%d"><circle fill="none" stroke="%s" stroke-width="3" cx="0" cy="0" r="64"/></g>' % (n, KNOB[i]))
    for j in range(BARS):
        body.append('<g id="w%d_%d">%s</g>' % (n, j, LINE % (KNOB[i], 2)))
    body.append('<g id="h%d">%s</g>' % (n, LINE % (KNOB[i], 2)))
    for j in range(3):                                   # playhead: 1 triangle normal, 2 an octave up, 3 two up; turned round in reverse
        body.append('<g id="p%d_%d">%s</g>' % (n, j, TRI % KNOB[i]))
    for j in range(2):                                   # behind the playhead: 1 bar an octave down, 2 two down
        body.append('<g id="b%d_%d">%s</g>' % (n, j, LINE % (KNOB[i], 2.5)))
for i in range(4):   # record discs last, so pure red sits over every loop
    body.append('<g id="d%d"><circle fill="%s" stroke="none" cx="0" cy="0" r="64"/></g>' % (i + 1, RED))
for i in range(4):   # loop numbers, over everything of the loops
    body.append('<g id="n%d"><path fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" d="%s"/></g>' % (i + 1, KNOB[i], DIGIT[i]))
# top right: sync (a ring) or free (the ring crossed out), then play (a triangle) or pause (two bars)
body.append('<g id="g_sync"><circle fill="none" stroke="%s" stroke-width="1.5" cx="462" cy="14" r="6"/></g>' % LIGHT)
body.append('<g id="g_free"><circle fill="none" stroke="%s" stroke-width="1.5" cx="462" cy="14" r="6"/>'
            '<path fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" d="M455,21L469,7"/></g>' % (LIGHT, LIGHT))
body.append('<g id="g_play"><path fill="%s" stroke="none" d="M438,8L448,14L438,20Z"/></g>' % LIGHT)
body.append('<g id="g_pause"><path fill="none" stroke="%s" stroke-width="2.5" stroke-linecap="round" d="M439,9V19M446,9V19"/></g>' % LIGHT)
# the start: "press an encoder", the four knobs' dots, "to record"
body.append('<g id="g_intro">%s</g>' % "".join('<circle fill="%s" stroke="none" cx="%d" cy="118" r="4"/>' % (KNOB[i], 222 + 12 * i) for i in range(4)))
# text placeholders (open-field base, level 3): a group with a stroked upright path; it gives place, height, colour
body.append('<g id="t_logo"><path fill="none" stroke="%s" stroke-width="1.3" stroke-linecap="square" d="M12,20V9"/></g>' % LIGHT)
body.append('<g id="t_status"><path fill="none" stroke="%s" stroke-width="1.3" stroke-linecap="square" d="M240,21V8"/></g>' % GREY)
body.append('<g id="t_in1"><path fill="none" stroke="%s" stroke-width="1.3" stroke-linecap="square" d="M240,107V94"/></g>' % LIGHT)
body.append('<g id="t_in2"><path fill="none" stroke="%s" stroke-width="1.3" stroke-linecap="square" d="M240,142V129"/></g>' % LIGHT)
for i in range(4):
    body.append('<g id="t_%d"><path fill="none" stroke="%s" stroke-width="1.3" stroke-linecap="square" d="M12,%dV%d"/></g>'
                % (i + 1, KNOB[i], 150 + i * 17 + 13, 150 + i * 17))

literal = ("\n".join(body) + "\n").encode("ascii")
HEAD, TAIL = 371, 7          # the two stretches of tempo.svg, as examples/yoyo/yoyo.svg.make takes them
out = b"op1f picture 2\n"
out += b"size %d\n" % (HEAD + len(literal) + TAIL)
out += b"from tempo.svg 43996 eb050950\n"
out += b"c 0 0 %d\n" % HEAD
out += b"a %d\n" % len(literal) + literal + b"\n"
out += b"c 0 43989 %d\n" % TAIL
with open(sys.argv[1], "xb") as f:
    f.write(out)
print("%s: %d bytes of instructions, picture %d bytes" % (sys.argv[1], len(out), HEAD + len(literal) + TAIL))
