# Places the section for bows_scene (layout of Nov 25 2026): the cello and the bass stand at the back facing
# the viewer, a little turned, each with its bow leant across it; the viola and the violin lie on the floor in front,
# their scrolls toward each other. Writes bows_section.h (Q14 frames, world 1/64 unit).
import math, sys
def norm(a): l = math.sqrt(sum(x*x for x in a)); return [x/l for x in a]
def add(*vs): return [sum(v[i] for v in vs) for i in range(3)]
def mul(a, k): return [x*k for x in a]
def cross(a, b): return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
Z = [0, 0, 1]
R = norm([1, -1, 0])     # screen right
F = norm([1, 1, 0])      # toward the viewer
def at(r, f): return add(mul(R, r), mul(F, f))
woods = {   # lit, mid, shaded, edge: the Field's browns (cello, bass), a little redder for the viola and the violin
 'bass':   '{rgb(0xad,0x89,0x59), rgb(0x8a,0x65,0x3c), rgb(0x5e,0x45,0x2a), rgb(0x3f,0x2c,0x1a)}',
 'cello':  '{rgb(0xb8,0x8a,0x52), rgb(0x94,0x64,0x36), rgb(0x66,0x42,0x24), rgb(0x42,0x2a,0x16)}',
 'viola':  '{rgb(0xbc,0x84,0x4e), rgb(0x9a,0x5e,0x32), rgb(0x6a,0x3c,0x20), rgb(0x46,0x26,0x14)}',
 'violin': '{rgb(0xc2,0x80,0x4a), rgb(0xa0,0x58,0x2e), rgb(0x70,0x38,0x1c), rgb(0x4a,0x24,0x12)}',
}
out = []
def inst(name, p, u, n, bh, bw, neck, kind, bow_at, pin, bow_side):
    u = norm(u); n = norm(n); v = cross(n, u)
    q = lambda a: ', '.join(str(int(round(x*16384))) for x in a)
    out.append('    {%d, %d, %d, {%s}, {%s}, {%s}, %d, %d, %d, %d, %s, %d, %d, %d},   // %s' % (
        round(p[0]), round(p[1]), round(p[2]), q(u), q(v), q(n), bh, bw, neck, kind, woods[name], bow_at, pin, bow_side, name))
def upright(name, r, f, turn, tilt, bh, bw, neck, kind, bow_at, pin, bow_side):
    t = math.radians(turn)
    n = add(mul(F, math.cos(t)), mul(R, -math.sin(t)))           # its front: the viewer's way, turned left by `turn`
    side = norm(cross(n, Z)); l = math.radians(tilt)
    u = add(mul(Z, math.cos(l)), mul(side, math.sin(l)))         # tipped sideways by `tilt`
    n = norm(add(n, mul(u, -sum(a*b for a, b in zip(n, u)))))
    inst(name, add(at(r, f), [0, 0, pin]), u, n, bh, bw, neck, kind, bow_at, pin, bow_side)
def lying(name, r, f, u, bh, bw, neck, bow_at, lift=0):
    u = norm(u)
    th = bw * 55 // 100 + 6
    p = add(at(r, f), mul(u, -(bh + neck) * 0.45), [0, 0, th + 170 + lift])   # its middle above (r, f): floating in front (v14: lift sets them apart)
    inst(name, p, u, Z, bh, bw, neck, 0, bow_at, 0, 0)
upright('cello', -330, -170, -34, 0, 420, 130, 300, 0, 200, 60, 1)
upright('bass', 300, -220, 26, 0, 520, 170, 380, 1, 300, 60, -1)
lying('viola', -250, 360, [0.25, -1, 0], 370, 108, 260, 120, 60)
lying('violin', 340, 400, [-1, 0.1, 0], 325, 94, 228, 60, -90)
stands = []                                                      # four, in the middle, each desk facing its instrument
M = at(-20, 60)
for r, f in ((-330, -170), (300, -220), (-250, 360), (330, 390)):
    d = norm(add(at(r, f), mul(M, -1)))
    p = add(M, mul(d, 150))
    stands.append('    {%d, %d, %d, %d},' % (round(p[0]), round(p[1]), round(d[0]*16384), round(d[1]*16384)))
open(sys.argv[1], 'w').write('// made by tools/section.py\n// x, y, z (the body\'s bottom), u (up the neck), v (across), n (out of the front), bh, bw, neck, kind, wood, bow_at, pin, bow (1 rising right, -1 rising left, 0 none)\nstatic const Inst SECTION[4] = {\n' + '\n'.join(out) + '\n};\n// music stands: x, y, the way the desk faces (Q14)\nstatic const int STANDS[4][4] = {\n' + '\n'.join(stands) + '\n};\n')
