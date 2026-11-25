// felt_scene.h: felt's page, a small 3D scene drawn by the pack itself into a 480x220 block of 16-bit pixels, which
// the frame function hands to op1f_paint_block. An upright piano on a felted applique quilt (framed blocks, flowers,
// running stitches, fading out toward its edges), under a patchwork quilt whose fabric hangs down in soft waves. What
// stands on top tells the style (a flower in a vase, a tape player, an open book); soft red specks of felt fluff drift
// up round it (ambient). The scene swivels from the isometric view (its keys facing the values, right) to the front
// and back, never further, so the faces can be drawn in a fixed order (far to near) with no sorting.
// Knob colours (Field): blue felt (tone) = the felt strip over the hammers; ochre decay (release) = how low the quilt
// hangs (its hem); grey style = the piano's wood and what stands on it; red ambient (wash) = the fluff. With shift: blue
// start, ochre loop in, grey loop out = three marks along the felt strip (the loop between in and out lit); red width =
// how wide the fluff spreads.
// Played notes light keys and make quilt squares pulse.
// No float, no allocation, no calls: plain integer code, the same on the unit and on the PC (tests/scene_preview.cpp).
// GPL-3.0-or-later with OP-1 firmware permission.
#ifndef FELT_SCENE_H
#define FELT_SCENE_H
#include <stdint.h>

namespace scene {

enum { W = 480, H = 220 };

struct State {
    uint32_t angle;        // the turn, 0..65535 a whole turn; swivel() gives it, between 0 (isometric) and -45 degrees (front)
    uint32_t time;         // frames, for the animation
    int tone, release, lofi, wash;   // 0..100
    int stretch;           // 0, 1, 2
    uint32_t keys;         // bit k: white key k (0..14) lit
    uint8_t pulse[12];     // quilt squares on top, 0..255 brightness of the pulse
    int sounding;          // voices sounding
    int start, loop_in, loop_out, width;   // the shift values, 0..100
    int life;              // 0..256: rises with the notes played, falls back slowly; the world wakes with it
    uint32_t seed;         // where the flowers come up: new each time the unit starts
    int garden;            // the flowers: 64 a flower, one opening at a time (0..64 x 64)
    int shift;             // shift held: the shift marks drawn brighter
};

// RGB565 as the base's OP1F_COLOUR
constexpr uint16_t rgb(unsigned r, unsigned g, unsigned b) { return (uint16_t)(((r & 0xf8u) << 8) | ((g & 0xfcu) << 3) | ((b & 0xffu) >> 3)); }

// the Field's palette (content/display/palette.svg)
struct Pal {
    uint16_t offbrown, darkumber, umber, brown, lightbrown, offblue, darkblue, blue, lightblue,
             offred, darkred, red, offgrey, darkgrey2, darkgrey1, grey, lightgrey, slate, wood_side, black;
};
static const Pal PAL = {rgb(0x29,0x22,0x1d), rgb(0x3f,0x39,0x33), rgb(0x5e,0x55,0x4b), rgb(0x8a,0x65,0x3c), rgb(0xad,0x89,0x59),
                          rgb(0x0e,0x1c,0x26), rgb(0x2b,0x42,0x61), rgb(0x3c,0x60,0x87), rgb(0x4c,0x7d,0xa8),
                          rgb(0x33,0x1c,0x29), rgb(0x9e,0x17,0x10), rgb(0xe5,0x28,0x17), rgb(0x1e,0x25,0x2b), rgb(0x37,0x3d,0x42),
                          rgb(0x5b,0x66,0x70), rgb(0xa2,0xa4,0xa6), rgb(0xcd,0xd0,0xc2), rgb(0x54,0x5e,0x69), rgb(0x1b,0x16,0x12), 0};
static inline const Pal& pal() { return PAL; }
// the piano's wood (top, front, side, edge) by the stretch: brown off, blue one octave, grey two (red is wash's)
static const uint16_t WOOD[3][4] = {
    {rgb(0x3f,0x39,0x33), rgb(0x29,0x22,0x1d), rgb(0x1b,0x16,0x12), rgb(0x5e,0x55,0x4b)},
    {rgb(0x2b,0x42,0x61), rgb(0x0e,0x1c,0x26), rgb(0x1e,0x25,0x2b), rgb(0x3c,0x60,0x87)},
    {rgb(0x37,0x3d,0x42), rgb(0x1e,0x25,0x2b), rgb(0x1e,0x25,0x2b), rgb(0x5b,0x66,0x70)},
};

// sine of a turn (0..65535), Q14, from a quarter table
static int sin14(uint32_t a) {
    static const int16_t Q[65] = {0,402,804,1205,1606,2006,2404,2801,3196,3590,3981,4370,4756,5139,5520,5897,6270,6639,7005,7366,
        7723,8076,8423,8765,9102,9434,9760,10080,10394,10702,11003,11297,11585,11866,12140,12406,12665,12916,13160,13395,13623,
        13842,14053,14256,14449,14635,14811,14978,15137,15286,15426,15557,15679,15791,15893,15986,16069,16143,16207,16261,16305,
        16340,16364,16379,16384};
    a &= 65535u;
    uint32_t q = a >> 14, i = (a >> 8) & 63u, f = a & 255u;
    int v;
    if (q == 0) v = Q[i] + (((Q[i+1] - Q[i]) * (int)f) >> 8);
    else if (q == 1) v = Q[64-i] + (((Q[63-i] - Q[64-i]) * (int)f) >> 8);
    else if (q == 2) v = -(Q[i] + (((Q[i+1] - Q[i]) * (int)f) >> 8));
    else v = -(Q[64-i] + (((Q[63-i] - Q[64-i]) * (int)f) >> 8));
    return v;
}
static int cos14(uint32_t a) { return sin14(a + 16384u); }

// the swivel: isometric from the piano's front-left corner at frame 0 (-90 degrees: the keys face right, toward the
// values), to the front (-45 degrees) and back, eased; a round trip in 1456 frames
static inline uint32_t swivel(uint32_t time) {   // the old slow swing, for previews
    uint32_t phase = time * 45u;
    return 49152u + (uint32_t)((8192 * (16384 - cos14(phase))) >> 15);
}

// ------------------------------------------------------------------ drawing into the block

// the mix of two RGB565 colours, a 0..256 of the second
static uint16_t mix(uint16_t d, uint16_t s, int a) {
    int b = 256 - a;
    int r = (((d >> 11) & 31) * b + ((s >> 11) & 31) * a) >> 8;
    int g = (((d >> 5) & 63) * b + ((s >> 5) & 63) * a) >> 8;
    int l = ((d & 31) * b + (s & 31) * a) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | l);
}
static int isqrt(int v) {                       // whole square root
    unsigned r = 0, bit = 1u << 30, n = (unsigned)v;
    while (bit > n) bit >>= 2;
    while (bit) { if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return (int)r;
}
enum { EDGE = 26, THIN = 18 };                  // line widths in 1/16 pixel: outlines, then the floor grid

struct Canvas {
    uint16_t* px;
    void dot(int x, int y, uint16_t c) { if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) px[y * W + x] = c; }
    void line(int x0, int y0, int x1, int y1, uint16_t c) {        // Bresenham, whole pixels
        int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
        int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
        int e = dx + dy;
        for (int n = 0; n < 2048; ++n) {
            dot(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * e;
            if (e2 >= dy) { e += dy; x0 += sx; }
            if (e2 <= dx) { e += dx; y0 += sy; }
        }
    }
    // a convex polygon, vertices in 1/16 pixel; filled, then outlined when `edge` is not 0xffff
    void poly(const int* xs, const int* ys, int n, uint16_t fill, uint16_t edge) {
        int top = ys[0], bot = ys[0];
        for (int i = 1; i < n; ++i) { if (ys[i] < top) top = ys[i]; if (ys[i] > bot) bot = ys[i]; }
        int y0 = (top + 8) >> 4, y1 = (bot + 8) >> 4;
        if (y0 < 0) y0 = 0;
        if (y1 > H) y1 = H;
        for (int y = y0; y < y1; ++y) {
            int yc = (y << 4) + 8, lo = 1 << 30, hi = -(1 << 30);
            for (int i = 0; i < n; ++i) {
                int j = i + 1 == n ? 0 : i + 1;
                int ya = ys[i], yb = ys[j];
                if ((yc < ya) == (yc < yb)) continue;                  // the scanline does not cross this edge
                int x = xs[i] + (xs[j] - xs[i]) * (yc - ya) / (yb - ya);
                if (x < lo) lo = x;
                if (x > hi) hi = x;
            }
            if (lo > hi) continue;
            int a = (lo + 8) >> 4, b = (hi + 8) >> 4;
            if (a < 0) a = 0;
            if (b > W) b = W;
            uint16_t* row = px + y * W;
            for (int x = a; x < b; ++x) row[x] = fill;
        }
        if (edge != 0xffffu)
            for (int i = 0; i < n; ++i) { int j = i + 1 == n ? 0 : i + 1; aline(xs[i], ys[i], xs[j], ys[j], edge, EDGE); }
    }
    // a smooth line, ends in 1/16 pixel, `w` its width in 1/16 pixel: along its long axis, a short run of pixels across
    // it each step, the run's two end pixels blended by how much of them the line covers
    void aline(int x0, int y0, int x1, int y1, uint16_t c, int w, int alpha = 256) {   // alpha 0..256
        int full = alpha >> 4;
        if (full <= 0) return;
        if (x0 < -16000 || x0 > 16000 || x1 < -16000 || x1 > 16000 || y0 < -16000 || y0 > 16000 || y1 < -16000 || y1 > 16000) return;
        int dx = x1 - x0, dy = y1 - y0, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        bool steep = ady > adx;
        if (steep) { int t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; t = dx; dx = dy; dy = t; t = adx; adx = ady; ady = t; }
        if (x0 > x1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        dy = y1 - y0;
        if (adx < 16) { cover(steep, (x0 + x1) >> 5, (y0 + y1 - w) >> 1, (y0 + y1 + w) >> 1, c, full); return; }
        int len = isqrt(adx * adx + ady * ady);                  // up to 32000 a side: within 32 bits
        int hw = w * len / (2 * adx);                            // half the run across, in 1/16 pixel
        int a = (x0 + 8) >> 4, b = (x1 + 8) >> 4, lim = steep ? H : W;
        if (a < 0) a = 0;
        if (b > lim - 1) b = lim - 1;
        int step = dy * 4096 / adx;                              // 1/4096 pixel a pixel along (slope 1 at most)
        int y = (y0 << 8) + step * (((a << 4) + 8) - x0) / 16;
        for (int x = a; x <= b; ++x, y += step) cover(steep, x, (y >> 8) - hw, (y >> 8) + hw, c, full);
    }
    // one pixel's column (or row, steep) across a line, from lo to hi in 1/16 pixel; the end pixels blended
    void cover(bool steep, int x, int lo, int hi, uint16_t c, int full) {
        int p0 = lo >> 4, p1 = (hi - 1) >> 4;
        for (int p = p0; p <= p1; ++p) {
            int top = p << 4, bot = top + 16, a = (hi < bot ? hi : bot) - (lo > top ? lo : top);
            if (a <= 0) continue;
            blend(steep ? p : x, steep ? x : p, c, a * full);
        }
    }
    void blend(int x, int y, uint16_t c, int a) {              // a 0..256
        if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
        uint16_t* d = px + y * W;
        d[x] = a >= 256 ? c : mix(d[x], c, a);
    }
    // a smooth disc, whole-pixel centre, its rim blended
    void disc(int cx, int cy, int r, uint16_t fill, uint16_t edge) {
        int ro = (r + 1) * (r + 1) * 16, rr = r * r * 16, ri = (r - 2) * (r - 2) * 16;
        for (int y = -r - 1; y <= r + 1; ++y)
            for (int x = -r - 1; x <= r + 1; ++x) {
                int d = (x * x + y * y) * 16;
                if (d >= ro) continue;
                if (d <= ri) blend(cx + x, cy + y, fill, 256);
                else if (d <= rr) blend(cx + x, cy + y, edge, 256);
                else blend(cx + x, cy + y, edge, 256 * (ro - d) / (ro - rr));
            }
    }
};

// ------------------------------------------------------------------ the camera: turn about the piano, then isometric

struct Cam {
    int c, s;                                   // Q14 of the turn
    // world in 1/64 unit; a unit is about 9 pixels
    void at(int x, int y, int z, int* sx, int* sy, int* depth) const {
        x -= 352; y -= 192;                     // about the piano's middle (5.5, 3.0)
        int xr = (x * c - y * s) >> 14, yr = (x * s + y * c) >> 14;
        // isometric: sx = (x - y) cos30 U, sy = (x + y) sin30 U - z U, U = 7 px; in 1/16 pixel from 1/64 unit
        *sx = (190 << 4) + (((xr - yr) * 125) >> 6);    // 1/16 pixel: 9 px a unit: 9 * cos30 * 16 / 64 = 125 / 64
        *sy = (142 << 4) + (((xr + yr) * 72) >> 6) - ((z * 144) >> 6);
        *depth = xr + yr + z;                   // toward the viewer
    }
};

enum { MAXQ = 160 };
struct Quad { int xs[4], ys[4], depth; uint16_t fill, edge; };

struct Builder {
    const Cam* cam;
    Quad q[MAXQ];
    int n;
    // a face from four world points; kept only when it faces the viewer
    void face(const int (*p)[3], uint16_t fill, uint16_t edge, bool both = false) {
        if (n >= MAXQ) return;
        Quad& f = q[n];
        int d = 0;
        for (int i = 0; i < 4; ++i) { int dd; cam->at(p[i][0], p[i][1], p[i][2], &f.xs[i], &f.ys[i], &dd); d += dd; }
        int area = 0;
        for (int i = 0; i < 4; ++i) { int j = (i + 1) & 3; area += (f.xs[i] >> 2) * (f.ys[j] >> 2) - (f.xs[j] >> 2) * (f.ys[i] >> 2); }
        if (!both && area <= 0) return;         // turned away
        if (area == 0) return;
        f.depth = d >> 2; f.fill = fill; f.edge = edge;
        ++n;
    }
    // a box of world corners (1/64 unit): only its top, front (+y) and left (-x) faces, the only ones the swivel shows
    void box(int x0, int y0, int z0, int x1, int y1, int z1, uint16_t top, uint16_t front, uint16_t side, uint16_t edge) {
        const int F[3][4][3] = {
            {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}},   // top
            {{x0,y1,z0},{x0,y1,z1},{x1,y1,z1},{x1,y1,z0}},   // +y
            {{x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}},   // -x
        };
        face(F[0], top, edge); face(F[1], front, edge); face(F[2], side, edge);
    }
    void sorted(struct Canvas& cv) {         // far to near (a few faces, insertion sort), then emptied
        for (int i = 1; i < n; ++i) { Quad t = q[i]; int j = i; while (j > 0 && q[j - 1].depth > t.depth) { q[j] = q[j - 1]; --j; } q[j] = t; }
        draw(cv);
    }
    void draw(struct Canvas& cv) {           // in the order given, then emptied
        for (int i = 0; i < n; ++i) cv.poly(q[i].xs, q[i].ys, 4, q[i].fill, q[i].edge);
        n = 0;
    }
};

static uint16_t brighter(uint16_t c) {
    const Pal& P = pal();
    if (c == P.darkblue || c == P.blue) return P.lightblue;
    if (c == P.umber || c == P.brown || c == P.darkumber) return P.lightbrown;
    if (c == P.darkgrey2 || c == P.slate) return P.lightgrey;
    if (c == P.offred) return P.red;
    return P.lightgrey;
}

static uint32_t hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

static Builder builder;   // 160 faces: kept off the stack

// ------------------------------------------------------------------ the scene
// The view runs from -90 degrees (the front and the left, -x, side seen) to -45 degrees (the front seen square): the
// viewer is always on the +y side and never on the +x side, so far to near is: the floor, the body, the right cheek, the
// keys, the left cheek, and the things on top. Faces turned away are dropped by the builder.

// ---- the quilts: patches laid flat in a plane, stitches, applique flowers, all faded toward the floor's edges

// a colour dimmed toward black, alpha 0..256
static uint16_t dim(uint16_t c, int alpha) { return alpha >= 256 ? c : alpha <= 0 ? (uint16_t)0 : mix(0, c, alpha); }

// how much of the floor shows at (x, y): full near the piano, nothing at the quilt's corners
static int floor_boost;                         // 0..256: the rug fading in with life, at most as bright as at v14
static int floor_alpha(int x, int y) {
    int nx = (x > 352 ? x - 352 : 352 - x) * 256 / 800, ny = (y > 288 ? y - 288 : 288 - y) * 256 / 672;
    int d = (nx * nx + ny * ny) >> 8;
    int a = d < 90 ? 170 : 170 - (d - 90) * 170 / 250;   // at rest never brighter than two thirds: the piano stands out
    a = a * floor_boost >> 8;
    return a < 0 ? 0 : a > 256 ? 256 : a;
}

// a patch on the floor (z 0) or on a plane of constant z
static void patch(Canvas& cv, const Cam& cam, int x0, int y0, int x1, int y1, int z, uint16_t c) {
    int xs[4], ys[4], d;
    cam.at(x0, y0, z, &xs[0], &ys[0], &d); cam.at(x1, y0, z, &xs[1], &ys[1], &d);
    cam.at(x1, y1, z, &xs[2], &ys[2], &d); cam.at(x0, y1, z, &xs[3], &ys[3], &d);
    cv.poly(xs, ys, 4, c, 0xffffu);
}

// a running stitch round a rectangle, `in` inside its edge
static void stitches(Canvas& cv, const Cam& cam, int x0, int y0, int x1, int y1, int z, int in, uint16_t c, int alpha) {
    if (alpha < 24) return;
    x0 += in; y0 += in; x1 -= in; y1 -= in;
    const int P[5][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}};
    for (int e = 0; e < 4; ++e) {
        int ax = P[e][0], ay = P[e][1], bx = P[e + 1][0], by = P[e + 1][1];
        int len = (bx > ax ? bx - ax : ax - bx) + (by > ay ? by - ay : ay - by);
        for (int t = 0; t + 14 <= len; t += 26) {
            int sx0, sy0, sx1, sy1, d;
            cam.at(ax + (bx - ax) * t / len, ay + (by - ay) * t / len, z, &sx0, &sy0, &d);
            cam.at(ax + (bx - ax) * (t + 14) / len, ay + (by - ay) * (t + 14) / len, z, &sx1, &sy1, &d);
            cv.aline(sx0, sy0, sx1, sy1, c, 16, alpha);
        }
    }
}

// an applique flower in a plane of constant z: `n` round petals about a round middle, two leaves under it
static void flower(Canvas& cv, const Cam& cam, int cx, int cy, int z, int r, int n, uint16_t petal, uint16_t middle,
                   uint16_t leaf, uint32_t turn) {
    int xs[8], ys[8], d;
    for (int l = 0; l < 2; ++l) {                // the leaves, a rhombus each way down from the middle
        int sx = l ? 1 : -1;
        int L[4][2] = {{cx, cy + r / 2}, {cx + sx * r, cy + r}, {cx + sx * r * 2, cy + r * 3 / 2}, {cx + sx * r / 2, cy + r * 3 / 2}};
        for (int k = 0; k < 4; ++k) cam.at(L[k][0], L[k][1], z, &xs[k], &ys[k], &d);
        cv.poly(xs, ys, 4, leaf, 0xffffu);
    }
    int pr = r * 9 / 16;
    for (int p = 0; p < n; ++p) {
        uint32_t a = turn + (uint32_t)p * 65536u / (uint32_t)n;
        int px = cx + ((r * 2 / 3 * cos14(a)) >> 14), py = cy + ((r * 2 / 3 * sin14(a)) >> 14);
        for (int k = 0; k < 8; ++k) {
            uint32_t b = (uint32_t)k * 8192u;
            cam.at(px + ((pr * cos14(b)) >> 14), py + ((pr * sin14(b)) >> 14), z, &xs[k], &ys[k], &d);
        }
        cv.poly(xs, ys, 8, petal, 0xffffu);
    }
    for (int k = 0; k < 8; ++k) {
        uint32_t b = (uint32_t)k * 8192u;
        cam.at(cx + ((r * 3 / 8 * cos14(b)) >> 14), cy + ((r * 3 / 8 * sin14(b)) >> 14), z, &xs[k], &ys[k], &d);
    }
    cv.poly(xs, ys, 8, middle, 0xffffu);
}

// a round piece of felt in a plane of constant z
static void dot3(Canvas& cv, const Cam& cam, int cx, int cy, int z, int r, uint16_t c) {
    int xs[8], ys[8], d;
    for (int k = 0; k < 8; ++k) {
        uint32_t b = (uint32_t)k * 8192u;
        cam.at(cx + ((r * cos14(b)) >> 14), cy + ((r * sin14(b)) >> 14), z, &xs[k], &ys[k], &d);
    }
    cv.poly(xs, ys, 8, c, 0xffffu);
}
// a pointed flower: `n` kite-shaped petals about a round middle
static void daisy(Canvas& cv, const Cam& cam, int cx, int cy, int z, int r, int n, uint16_t petal, uint16_t middle, uint32_t turn) {
    int xs[4], ys[4], d;
    for (int p = 0; p < n; ++p) {
        uint32_t a = turn + (uint32_t)p * 65536u / (uint32_t)n, w = 65536u / (uint32_t)n / 2u;
        int P[4][2] = {{cx + ((r / 4 * cos14(a)) >> 14), cy + ((r / 4 * sin14(a)) >> 14)},
                       {cx + ((r * 3 / 5 * cos14(a - w)) >> 14), cy + ((r * 3 / 5 * sin14(a - w)) >> 14)},
                       {cx + ((r * cos14(a)) >> 14), cy + ((r * sin14(a)) >> 14)},
                       {cx + ((r * 3 / 5 * cos14(a + w)) >> 14), cy + ((r * 3 / 5 * sin14(a + w)) >> 14)}};
        for (int k = 0; k < 4; ++k) cam.at(P[k][0], P[k][1], z, &xs[k], &ys[k], &d);
        cv.poly(xs, ys, 4, petal, 0xffffu);
    }
    dot3(cv, cam, cx, cy, z, r * 3 / 10, middle);
}
// a stem with a leaf, from (x, y0) up the block to (x, y1)
static void stem(Canvas& cv, const Cam& cam, int x, int y0, int y1, int z, int lean, uint16_t c) {
    int xs[4], ys[4], d;
    int S[4][2] = {{x - 5, y0}, {x + 5, y0}, {x + lean + 5, y1}, {x + lean - 5, y1}};
    for (int k = 0; k < 4; ++k) cam.at(S[k][0], S[k][1], z, &xs[k], &ys[k], &d);
    cv.poly(xs, ys, 4, c, 0xffffu);
    int m = (y0 + y1) >> 1, L[4][2] = {{x + lean / 2, m}, {x + lean / 2 + 22, m - 10}, {x + lean / 2 + 40, m + 4}, {x + lean / 2 + 18, m + 12}};
    for (int k = 0; k < 4; ++k) cam.at(L[k][0], L[k][1], z, &xs[k], &ys[k], &d);
    cv.poly(xs, ys, 4, c, 0xffffu);
}
// a tulip: a cup of three round petals
static void tulip(Canvas& cv, const Cam& cam, int cx, int cy, int z, int r, uint16_t petal, uint16_t inner) {
    dot3(cv, cam, cx - r / 2, cy, z, r * 3 / 5, petal);
    dot3(cv, cam, cx + r / 2, cy, z, r * 3 / 5, petal);
    dot3(cv, cam, cx, cy - r / 4, z, r * 3 / 5, inner);
}
// quarter rounds in a block's four corners, as the reference quilts' scalloped blocks
static void corners(Canvas& cv, const Cam& cam, int x0, int y0, int x1, int y1, int z, int r, uint16_t c) {
    const int C[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    for (int q = 0; q < 4; ++q) {
        int xs[6], ys[6], d;
        cam.at(C[q][0], C[q][1], z, &xs[0], &ys[0], &d);
        for (int k = 0; k < 5; ++k) {
            uint32_t a = (uint32_t)q * 16384u + (uint32_t)k * 4096u;
            cam.at(C[q][0] + ((r * cos14(a)) >> 14), C[q][1] + ((r * sin14(a)) >> 14), z, &xs[k + 1], &ys[k + 1], &d);
        }
        cv.poly(xs, ys, 6, c, 0xffffu);
    }
}

// the floor: a felted applique quilt under the piano, its border of squares, framed blocks with a flower in most,
// running stitches round each, everything fading out toward the edges
enum { QX0 = -448, QX1 = 1152, QY0 = -384, QY1 = 960 };
static void floor_quilt(Canvas& cv, const Cam& cam) {
    const Pal& P = pal();
    const uint16_t border[7] = {P.darkred, P.darkblue, P.brown, P.slate, P.offred, P.blue, P.umber};
    const uint16_t frames[6] = {P.darkblue, P.brown, P.darkred, P.slate, P.blue, P.umber};
    const uint16_t petals[5] = {P.red, P.lightbrown, P.lightblue, P.grey, P.brown};
    // the ground and the border's squares
    int k = 0;
    for (int x = QX0; x < QX1; x += 64, ++k) {
        patch(cv, cam, x, QY0, x + 64, QY0 + 64, 0, dim(border[k % 7], floor_alpha(x + 32, QY0 + 32)));
        patch(cv, cam, x, QY1 - 64, x + 64, QY1, 0, dim(border[(k + 3) % 7], floor_alpha(x + 32, QY1 - 32)));
    }
    for (int y = QY0 + 64; y < QY1 - 64; y += 64, ++k) {
        patch(cv, cam, QX0, y, QX0 + 64, y + 64, 0, dim(border[k % 7], floor_alpha(QX0 + 32, y + 32)));
        patch(cv, cam, QX1 - 64, y, QX1, y + 64, 0, dim(border[(k + 2) % 7], floor_alpha(QX1 - 32, y + 32)));
    }
    // the blocks: five across, four down, a coloured frame round a dark middle
    const int fx0 = QX0 + 96, fy0 = QY0 + 96, fw = QX1 - QX0 - 192, fh = QY1 - QY0 - 192;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 5; ++i) {
            int x0 = fx0 + i * fw / 5 + 12, x1 = fx0 + (i + 1) * fw / 5 - 12;
            int y0 = fy0 + j * fh / 4 + 12, y1 = fy0 + (j + 1) * fh / 4 - 12;
            int a = floor_alpha((x0 + x1) >> 1, (y0 + y1) >> 1);
            if (a <= 8) continue;
            patch(cv, cam, x0 - 12, y0 - 12, x1 + 12, y1 + 12, 0, dim(P.darkumber, a * 3 / 4));   // the sashing round it
            uint32_t h = hash((uint32_t)(i * 7 + j * 13 + 1));
            patch(cv, cam, x0, y0, x1, y1, 0, dim(frames[h % 6u], a));
            patch(cv, cam, x0 + 26, y0 + 26, x1 - 26, y1 - 26, 0, dim((i + j) & 1 ? P.darkumber : P.offgrey, a));
            stitches(cv, cam, x0, y0, x1, y1, 0, 13, dim(P.grey, a), a);
            stitches(cv, cam, x0, y0, x1, y1, 0, 40, dim(P.darkgrey1, a), a);
            int cx = (x0 + x1) >> 1, cy = (y0 + y1) >> 1, side = (x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0);
            uint16_t pc = dim(petals[(h >> 8) % 5u], a), pc2 = dim(petals[(h >> 11) % 5u], a), mid = dim(P.lightgrey, a * 3 / 4);
            uint16_t green = dim(P.darkgrey1, a);
            int scale = 16 + (int)((h >> 14) % 9u);   // each block's flowers their own size: 16 to 24 of 100 of the block
            switch ((h >> 4) % 6u) {
            case 0:                               // one round flower on a stem
                stem(cv, cam, cx, cy + side / 3, cy, 1, 0, green);
                flower(cv, cam, cx, cy - side / 10, 1, side * scale / 100, 5 + (int)((h >> 17) % 4u), pc, mid, green, h);
                break;
            case 1:                               // a pointed daisy, large
                daisy(cv, cam, cx, cy, 1, side * (scale + 8) / 100, 8 + (int)((h >> 17) % 5u), pc, dim(P.lightbrown, a), h);
                break;
            case 2:                               // two tulips leaning apart
                stem(cv, cam, cx - side / 8, cy + side / 3, cy - side / 12, 1, -side / 10, green);
                stem(cv, cam, cx + side / 8, cy + side / 3, cy - side / 12, 1, side / 10, green);
                tulip(cv, cam, cx - side / 4, cy - side / 6, 1, side * scale / 160, pc, pc2);
                tulip(cv, cam, cx + side / 4, cy - side / 6, 1, side * scale / 160, pc2, pc);
                break;
            case 3:                               // four small four-petal flowers
                for (int q = 0; q < 4; ++q)
                    flower(cv, cam, cx + (q & 1 ? side / 5 : -side / 5), cy + (q & 2 ? side / 5 : -side / 5), 1, side / 10, 4,
                           q & 1 ? pc : pc2, mid, dim(P.offgrey, a), (uint32_t)q * 8192u);
                break;
            case 4:                               // scalloped corners round a small daisy
                corners(cv, cam, x0 + 26, y0 + 26, x1 - 26, y1 - 26, 1, side / 5, pc2);
                daisy(cv, cam, cx, cy, 1, side * scale / 110, 10, pc, mid, h);
                break;
            default:                              // a round flower in a ring of dots
                for (int q = 0; q < 10; ++q) {
                    uint32_t t = (uint32_t)q * 6554u;
                    dot3(cv, cam, cx + ((side * 32 / 100 * cos14(t)) >> 14), cy + ((side * 32 / 100 * sin14(t)) >> 14), 1, side / 28, pc2);
                }
                flower(cv, cam, cx, cy, 1, side * scale / 110, 6 + (int)((h >> 17) % 3u), pc, mid, green, h);
                break;
            }
        }
}

// ambient (wash): single red pixels of felt fluff drifting up round the piano, more and higher with ambient, spread
// wider with width; brighter while notes sound. A few dozen small blended dots: next to nothing to draw
static void fluff(Canvas& cv, const Cam& cam, const State& st) {
    const Pal& P = pal();
    int count = st.wash * 24 / 100;
    for (int i = 0; i < count; ++i) {
        uint32_t h = hash((uint32_t)i * 2654435761u + 77u);
        int life = 180 + (int)(h % 120u), t = (int)((st.time + (h >> 8)) % (uint32_t)life);
        uint32_t ang = h >> 4;
        int reach = 300 + (int)((h >> 12) % 260u) + st.width * 2;
        int x = 352 + ((reach * cos14(ang)) >> 14) + ((24 * sin14((uint32_t)t * 300u + h)) >> 14);
        int y = 208 + ((reach * sin14(ang)) >> 14) * 2 / 3;
        int z = t * (300 + st.wash * 6) / life;
        int a = (st.sounding ? 200 : 110) * (life - t) / life;
        if (t < 20) a = a * t / 20;
        int sx, sy, d; cam.at(x, y, z, &sx, &sy, &d);
        cv.blend(sx >> 4, sy >> 4, P.red, a);   // one pixel each
    }
}

// a round piece in an upright plane facing the front (constant y)
static void vdot(Canvas& cv, const Cam& cam, int cx, int y, int cz, int r, uint16_t c) {
    int xs[8], ys[8], d;
    for (int k = 0; k < 8; ++k) {
        uint32_t b = (uint32_t)k * 8192u;
        cam.at(cx + ((r * cos14(b)) >> 14), y, cz + ((r * sin14(b)) >> 14), &xs[k], &ys[k], &d);
    }
    cv.poly(xs, ys, 8, c, 0xffffu);
}

static void reel(Canvas& cv, const Cam& cam, int cx, int cy, int z, int r, uint32_t spin, uint16_t fill, uint16_t edge) {
    int xs[12], ys[12], d;
    for (int k = 0; k < 12; ++k) {
        uint32_t a = (uint32_t)k * 65536u / 12u;
        cam.at(cx + ((r * cos14(a)) >> 14), cy + ((r * sin14(a)) >> 14), z, &xs[k], &ys[k], &d);
    }
    cv.poly(xs, ys, 12, fill, edge);
    int hx, hy; cam.at(cx, cy, z, &hx, &hy, &d);
    for (int k = 0; k < 3; ++k) {                // three spokes, turning in the reel's own plane
        uint32_t a = spin + (uint32_t)k * 21845u;
        int sx, sy; cam.at(cx + ((r * 3 / 4 * cos14(a)) >> 14), cy + ((r * 3 / 4 * sin14(a)) >> 14), z, &sx, &sy, &d);
        cv.aline(hx, hy, sx, sy, edge, THIN);
    }
}

// the hem of the piano's quilt, where the fabric hangs to: lower with release, in soft waves
static int hem(int along, const State& st) {
    return 611 - 64 - st.release * 9 / 10 + ((18 * sin14((uint32_t)along * 160u)) >> 14);
}

// life: flowers grow out of the rug in front of the piano, one after another as the playing goes on, and blossom: each
// its own kind (a daisy, a tulip, a round bloom, a cluster of bells), size and colour; they sink back as it calms
static void bloom(Canvas& cv, int cx, int cy, int kind, int r, uint16_t c, uint16_t c2, uint32_t turn) {
    const Pal& P = pal();
    if (r < 1) { cv.blend(cx, cy, c, 200); return; }
    switch (kind) {
    case 0:                                       // a daisy: petals round a golden middle
        for (int k = 0; k < 7; ++k) {
            uint32_t a = (uint32_t)k * 9362u + turn;
            cv.disc(cx + ((r * 2 * cos14(a)) >> 15), cy + ((r * 2 * sin14(a)) >> 15) * 3 / 4, r / 2 > 1 ? r / 2 : 1, c, c);
        }
        cv.disc(cx, cy, r / 2 > 1 ? r / 2 : 1, P.lightbrown, P.brown);
        break;
    case 1:                                       // a tulip: a cup, its two tips up
        cv.disc(cx, cy, r, c, c2);
        cv.disc(cx - r / 2, cy - r, r / 2 > 1 ? r / 2 : 1, c, c);
        cv.disc(cx + r / 2, cy - r, r / 2 > 1 ? r / 2 : 1, c, c);
        break;
    default:                                      // a round bloom, a darker heart
        cv.disc(cx, cy, r + 1, c, c2);
        cv.disc(cx, cy, r / 3 > 1 ? r / 3 : 1, c2, c2);
        break;
    }
}
static const uint16_t LEAF = rgb(0x5a, 0x7a, 0x3c);   // a muted leaf green, only for the small leaves
static void sprouts(Canvas& cv, const Cam& cam, const State& st) {
    const Pal& P = pal();
    const uint16_t heads[6] = {P.red, P.lightbrown, P.lightblue, P.lightgrey, P.darkred, P.blue};
    const uint16_t hearts[6] = {P.darkred, P.brown, P.blue, P.grey, P.red, P.darkblue};
    // the piano's right edge on the screen (1/16 pixel) in the two outer views, so a spot right of it stays clear of it
    // whichever style's view is shown, and does not move when the view glides
    Cam views[2]; int pr[2], dd;
    for (int v = 0; v < 2; ++v) {
        uint32_t a = v ? 56320u : 50176u;
        views[v].c = cos14(a); views[v].s = sin14(a); pr[v] = -(1 << 30);
        for (int k = 0; k < 8; ++k) {
            int sx, sy;
            views[v].at(k & 1 ? 720 : -16, k & 2 ? 430 : -16, k & 4 ? 640 : 0, &sx, &sy, &dd);
            if (sx > pr[v]) pr[v] = sx;
        }
    }
    int full = st.garden >> 6, part = st.garden & 63;
    int n = full + (part ? 1 : 0);
    for (int i = 0; i < n && i < 64; ++i) {
        // one at a time: the ones before it are open; it grows only once they are
        int grow = i < full ? 64 : part;
        // its spot, new each start: on the rug, left of the piano, in front of it, or right of it where the piano
        // cannot cover it on the screen; never behind it
        uint32_t h = hash((uint32_t)i * 40503u + st.seed);
        int x = 0, y = 0, ok = 0;
        for (int tries = 0; tries < 8 && !ok; ++tries) {
            x = -420 + (int)(h % 1540u); y = -340 + (int)((h >> 11) % 1280u);
            if ((x < -50 && y > 10) || y > 470) { ok = 1; break; }
            if (x > 720) {                        // right of the piano: only clear of it on the screen, flower and all
                int clear = 1;
                for (int v = 0; v < 2; ++v) {
                    int s0, s1, t0, t1;
                    views[v].at(x, y, 0, &s0, &t0, &dd); views[v].at(x, y, 280, &s1, &t1, &dd);
                    if ((s0 < s1 ? s0 : s1) - 10 * 16 <= pr[v]) clear = 0;
                }
                if (clear) { ok = 1; break; }
            }
            h = hash(h + 0x9e3779b9u);
        }
        if (!ok) { x = -200; y = 500 + i * 30; }
        int top = grow * (135 + (int)((h >> 20) % 135u)) / 64;   // three quarters of v18's
        uint32_t sway = st.time * 70u + h;
        int lean = (16 * sin14(sway)) >> 14;
        int ax, ay, bx, by, mx, my, lx, ly, d;
        cam.at(x, y, 0, &ax, &ay, &d); cam.at(x + lean, y, top, &bx, &by, &d);
        cv.aline(ax, ay, bx, by, P.darkgrey1, 24, 240);
        if (grow > 32) {                          // a small green leaf a third of the way up
            cam.at(x + lean / 3, y, top / 3, &mx, &my, &d); cam.at(x + lean / 3 + ((h >> 5) & 1u ? 17 : -17), y, top / 3 + 11, &lx, &ly, &d);
            cv.aline(mx, my, lx, ly, LEAF, 30, 230);
        }
        if (grow > 24) {                          // the head opens, wider as it grows
            int size = 3 + (int)((h >> 9) % 5u);  // each its own: 3 to 7 pixels at full bloom
            int r = (grow - 24) * size / 40;
            if (r > size) r = size;
            int c = (int)((h >> 8) % 6u);
            bloom(cv, bx >> 4, by >> 4, (int)((h >> 14) % 3u), r, heads[c], hearts[c], sway >> 2);
        }
    }
}

// life: petals falling, small tumbling pieces drifting down across the scene
static void petals(Canvas& cv, const State& st) {
    const Pal& P = pal();
    int count = st.life > 176 ? (st.life - 176) / 7 : 0;   // last of all, once the flowers are out
    for (int i = 0; i < count; ++i) {
        uint32_t h = hash((uint32_t)i * 2246822519u + 5u);
        int fall = 300 + (int)(h % 240u), t = (int)((st.time + (h >> 9)) % (uint32_t)fall);
        int x = 10 + (int)((h >> 3) % 320u) + ((14 * sin14((uint32_t)t * 300u + h)) >> 14);
        int y = t * (H + 12) / fall - 6;
        int a = t > fall - 40 ? (fall - t) * 5 : 220;
        const uint16_t heads[6] = {P.red, P.lightbrown, P.lightblue, P.lightgrey, P.darkred, P.blue};   // the flowers' own
        const uint16_t hearts[6] = {P.darkred, P.brown, P.blue, P.grey, P.red, P.darkblue};
        uint16_t c = heads[(h >> 16) % 6u], c2 = hearts[(h >> 16) % 6u];
        int turn = (int)(((uint32_t)t >> 3) & 3u);               // a petal, tumbling: face on, turning, on its edge
        if (turn == 0) {
            for (int oy = -1; oy <= 1; ++oy) for (int ox = -2; ox <= 2; ++ox) if (ox * ox + oy * oy * 4 <= 5) cv.blend(x + ox, y + oy, ox > 0 ? c2 : c, a);
            cv.blend(x, y - 2, c, a / 2); cv.blend(x, y + 2, c2, a / 2);
        } else if (turn == 2) {
            for (int ox = -2; ox <= 2; ++ox) cv.blend(x + ox, y, ox & 1 ? c2 : c, a);
        } else {
            for (int k = -2; k <= 2; ++k) { cv.blend(x + k, y + (turn == 1 ? k : -k) / 2, c, a); cv.blend(x + k, y + 1 + (turn == 1 ? k : -k) / 2, c2, a * 2 / 3); }
        }
    }
}

// the floor is the scene's biggest part: drawn again only when the swivel has moved a step or the life has changed a
// level, else copied from where it was kept (the cache: W x H, as the page)
static uint32_t floor_kept = 0xffffffffu;

// what does not move between notes: the rug, the piano, its quilt and what stands on it
static void base_layer(uint16_t* px, const State& st, uint16_t* floor_cache, uint32_t angle, int level) {
    const Pal& P = pal();
    Canvas cv = {px};
    Cam cam; cam.c = cos14(angle); cam.s = sin14(angle);
    Builder& b = builder; b.cam = &cam; b.n = 0;
    uint32_t key = (angle & 0xffffu) | ((uint32_t)level << 16);
    if (floor_cache && key == floor_kept) {
        for (int i = 0; i < W * H; ++i) px[i] = floor_cache[i];
    } else {
        for (int i = 0; i < W * H; ++i) px[i] = P.black;
        floor_boost = level * 32;                 // the growth: first only the piano, then the rug's patterns fade in
        if (level) floor_quilt(cv, cam);
        if (floor_cache) { for (int i = 0; i < W * H; ++i) floor_cache[i] = px[i]; floor_kept = key; }
    }

    // the piano: body, kneeboard, right cheek, key bed, keys, left cheek; its wood by the stretch (the grey press):
    // brown when off, blue at one octave, grey at two
    const uint16_t* wd = WOOD[st.stretch < 0 ? 0 : st.stretch > 2 ? 2 : st.stretch];
    b.box(0, 0, 0, 704, 256, 608, wd[0], wd[1], wd[2], wd[3]);
    b.box(32, 256, 0, 672, 288, 256, wd[0], wd[1], wd[2], wd[3]);
    b.draw(cv);
    {   // the felt strip over the hammers: thicker and brighter with tone; the shift marks along it
        int th = 26 + 51 * st.tone / 100, z0 = 352, z1 = 352 + th, y = 257;
        int F[4][3] = {{48, y, z0}, {48, y, z1}, {656, y, z1}, {656, y, z0}};
        b.face(F, st.tone > 60 ? P.blue : P.darkblue, P.lightblue);
        int in = 48 + 608 * st.loop_in / 100, out = 48 + 608 * st.loop_out / 100;
        if (out > in + 4) {                       // the loop, lit between its marks
            int L[4][3] = {{in, y, z1}, {in, y, z1 + 13}, {out, y, z1 + 13}, {out, y, z1}};
            b.face(L, st.shift ? P.lightblue : P.slate, 0xffffu);
        }
        b.draw(cv);
        const int at[3] = {st.start, st.loop_in, st.loop_out};
        const uint16_t mc[3] = {P.lightblue, P.lightbrown, P.lightgrey};
        for (int k = 0; k < 3; ++k) {
            int x = 48 + 608 * at[k] / 100, ax, ay, bx2, by2, d;
            cam.at(x, y, z0 - 6, &ax, &ay, &d); cam.at(x, y, z1 + (st.shift ? 26 : 16), &bx2, &by2, &d);
            cv.aline(ax, ay, bx2, by2, mc[k], st.shift ? 40 : EDGE);
        }
    }
    b.box(672, 256, 0, 704, 416, 368, wd[0], wd[1], wd[2], wd[3]);      // right cheek
    b.box(32, 288, 256, 672, 416, 304, wd[0], wd[1], wd[2], wd[3]);     // key bed
    b.draw(cv);
    {   // the keys: fifteen white (two octaves and a C), one slab with soft lines between, the lit ones blue
        const int kx0 = 36, kx1 = 668, ky0 = 296, ky1 = 410, kz0 = 304, kz = 322;
        int F[4][3] = {{kx0, ky1, kz0}, {kx0, ky1, kz}, {kx1, ky1, kz}, {kx1, ky1, kz0}};
        b.face(F, P.grey, 0xffffu);                                    // their front edge
        int T[4][3] = {{kx0, ky0, kz}, {kx1, ky0, kz}, {kx1, ky1, kz}, {kx0, ky1, kz}};
        b.face(T, P.lightgrey, 0xffffu);
        b.draw(cv);
        for (int k = 0; k < 15; ++k) {
            if (!((st.keys >> k) & 1u)) continue;
            int x0 = kx0 + k * (kx1 - kx0) / 15, x1 = kx0 + (k + 1) * (kx1 - kx0) / 15;
            int L[4][3] = {{x0, ky0, kz}, {x1, ky0, kz}, {x1, ky1, kz}, {x0, ky1, kz}};
            b.face(L, P.lightblue, 0xffffu);
        }
        b.draw(cv);
        for (int k = 1; k < 15; ++k) {
            int x = kx0 + k * (kx1 - kx0) / 15, ax, ay, bx2, by2, d;
            cam.at(x, ky0, kz, &ax, &ay, &d); cam.at(x, ky1, kz, &bx2, &by2, &d);
            cv.aline(ax, ay, bx2, by2, P.darkgrey1, 18);
        }
        for (int k = 0; k < 14; ++k) {            // black keys: none after E and B
            if (k % 7 == 2 || k % 7 == 6) continue;
            int x = kx0 + (k + 1) * (kx1 - kx0) / 15, w = (kx1 - kx0) / 15 * 3 / 10;
            int B[4][3] = {{x - w, ky0, kz + 1}, {x + w, ky0, kz + 1}, {x + w, 360, kz + 1}, {x - w, 360, kz + 1}};
            b.face(B, P.offgrey, 0xffffu);
        }
        b.draw(cv);
    }
    b.box(0, 256, 0, 32, 416, 368, wd[0], wd[1], wd[2], wd[3]);         // left cheek
    b.draw(cv);

    // the quilt over the piano: a patchwork top (the squares light up with the notes) whose fabric hangs down the
    // front and the left side in soft waves, its hem stitched in ochre (release: the lower it hangs, the longer)
    const uint16_t cols[8] = {P.darkblue, P.umber, P.darkgrey2, P.offred, P.blue, P.brown, P.slate, P.darkumber};
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 2; ++j) {
            int x0 = 16 + i * 112, x1 = x0 + 112, y0 = -8 + j * 136, y1 = y0 + 136;
            uint16_t c = cols[(i + j * 3) & 7];
            if (st.pulse[i * 2 + j] > 96 - st.life / 4) c = brighter(c);
            patch(cv, cam, x0, y0, x1, y1, 611, c);
            stitches(cv, cam, x0, y0, x1, y1, 611, 12, P.grey, 150);
        }
    {   // the left side's fabric, along y, then the front's, along x: strips down to the hem, darker in the folds
        int ox = 0, oy = 0, hx = 0, hy = 0, d;
        for (int y = -8; y < 264; y += 17) {
            int y1 = y + 17 > 264 ? 264 : y + 17, h0 = hem(y + 700, st), h1 = hem(y1 + 700, st);
            int xs[4], ys[4];
            cam.at(-12, y, 611, &xs[0], &ys[0], &d); cam.at(-12, y1, 611, &xs[1], &ys[1], &d);
            cam.at(-12, y1, h1, &xs[2], &ys[2], &d); cam.at(-12, y, h0, &xs[3], &ys[3], &d);
            uint16_t c = cols[(((y + 8) / 136) * 3) & 7];
            cv.poly(xs, ys, 4, cos14((uint32_t)(y + 700) * 160u) > 0 ? c : dim(c, 170), 0xffffu);
            if (y > -8) cv.aline(ox, oy, xs[3], ys[3], P.lightbrown, EDGE);
            ox = xs[3]; oy = ys[3]; hx = xs[2]; hy = ys[2];
        }
        cv.aline(ox, oy, hx, hy, P.lightbrown, EDGE);
        for (int x = 16; x < 688; x += 16) {
            int x1 = x + 16, h0 = hem(x, st), h1 = hem(x1, st);
            int xs[4], ys[4];
            cam.at(x, 264, 611, &xs[0], &ys[0], &d); cam.at(x1, 264, 611, &xs[1], &ys[1], &d);
            cam.at(x1, 264, h1, &xs[2], &ys[2], &d); cam.at(x, 264, h0, &xs[3], &ys[3], &d);
            uint16_t c = cols[((x - 16) / 112 + 5) & 7];
            cv.poly(xs, ys, 4, cos14((uint32_t)x * 160u) > 0 ? c : dim(c, 170), 0xffffu);
            cv.aline(xs[3], ys[3], xs[2], ys[2], P.lightbrown, EDGE);
            if ((x / 16) % 2 == 0) {              // a running stitch along the hem
                int sx0, sy0, sx1, sy1;
                cam.at(x, 264, h0 + 22, &sx0, &sy0, &d); cam.at(x + 9, 264, hem(x + 9, st) + 22, &sx1, &sy1, &d);
                cv.aline(sx0, sy0, sx1, sy1, P.grey, 16, 170);
            }
        }
        for (int i = 1; i < 6; ++i) {             // the seams between the front's patches, stitched
            int x = 16 + i * 112;
            for (int z = 600; z - 14 > hem(x, st) + 6; z -= 26) {
                int sx0, sy0, sx1, sy1;
                cam.at(x, 264, z, &sx0, &sy0, &d); cam.at(x, 264, z - 14, &sx1, &sy1, &d);
                cv.aline(sx0, sy0, sx1, sy1, P.grey, 16, 150);
            }
        }
    }

    // what stands on the piano tells its style: a flower in a vase (0, the felt piano), a tape player (1, the
    // cassette's lofi stretch), an open book (2, the deeper stretch)
    int d;
    if (st.stretch == 0) {
        {   // the pot: round, wider at its rim, twelve sides of which the builder keeps the ones that face the viewer
            const int cx = 184, cy = 124, z0 = 612, z1 = 740, r0 = 40, r1 = 56;
            {   // its whole outline first, both rings in one span fill, so no face edge can leave a notch at the bottom
                int ox[24], oy[24];
                for (int k = 0; k < 12; ++k) {
                    uint32_t a = (uint32_t)k * 5461u;
                    cam.at(cx + ((r0 * cos14(a)) >> 14), cy + ((r0 * sin14(a)) >> 14), z0, &ox[k], &oy[k], &d);
                    cam.at(cx + ((r1 * cos14(a)) >> 14), cy + ((r1 * sin14(a)) >> 14), z1, &ox[12 + k], &oy[12 + k], &d);
                }
                cv.poly(ox, oy, 24, P.brown, 0xffffu);
            }
            for (int k = 0; k < 12; ++k) {
                uint32_t a0 = (uint32_t)k * 5461u, a1 = a0 + 5461u;
                int F[4][3] = {{cx + ((r0 * cos14(a0)) >> 14), cy + ((r0 * sin14(a0)) >> 14), z0},      // wound so the
                               {cx + ((r0 * cos14(a1)) >> 14), cy + ((r0 * sin14(a1)) >> 14), z0},      // outside faces
                               {cx + ((r1 * cos14(a1)) >> 14), cy + ((r1 * sin14(a1)) >> 14), z1},      // out: the builder
                               {cx + ((r1 * cos14(a0)) >> 14), cy + ((r1 * sin14(a0)) >> 14), z1}};     // keeps the near wall
                int light = cos14(a0 + 2730u - 40960u);         // lit from the front left
                b.face(F, light > 8000 ? P.lightbrown : light > -4000 ? P.brown : P.umber, 0xffffu);
            }
            b.sorted(cv);                                       // the far wall, which leans out, under the near one
            int xs[12], ys[12];
            for (int k = 0; k < 12; ++k) cam.at(cx + ((r1 * cos14((uint32_t)k * 5461u)) >> 14), cy + ((r1 * sin14((uint32_t)k * 5461u)) >> 14), z1, &xs[k], &ys[k], &d);
            cv.poly(xs, ys, 12, P.offbrown, P.lightbrown);               // the rim, earth inside
        }
        int ax, ay, bx2, by2;
        cam.at(184, 124, 740, &ax, &ay, &d); cam.at(194, 124, 880, &bx2, &by2, &d);
        cv.aline(ax, ay, bx2, by2, P.darkgrey1, 30);                   // its stem
        cam.at(188, 124, 800, &ax, &ay, &d); cam.at(240, 124, 832, &bx2, &by2, &d);
        cv.aline(ax, ay, bx2, by2, P.darkgrey1, 40);                   // a leaf
    } else if (st.stretch == 1) {
        const int x0 = 48, y0 = 24, x1 = 304, y1 = 216, z0 = 612, z1 = 660;
        b.box(x0, y0, z0, x1, y1, z1, P.darkgrey2, P.offgrey, P.offgrey, P.darkgrey1);
        b.draw(cv);
    } else {
        // the book: its cover flat on the quilt, two pages rising from the spine (along y, at x 176)
        int xs[4], ys[4];
        const int C[4][3] = {{40, 30, 612}, {332, 30, 612}, {332, 230, 612}, {40, 230, 612}};
        for (int k = 0; k < 4; ++k) cam.at(C[k][0], C[k][1], C[k][2], &xs[k], &ys[k], &d);
        cv.poly(xs, ys, 4, P.darkred, P.offred);
        for (int side = 0; side < 2; ++side) {
            int far = side ? 326 : 46;
            const int G[4][3] = {{186, 36, 618}, {far, 36, 646}, {far, 224, 646}, {186, 224, 618}};
            for (int k = 0; k < 4; ++k) cam.at(G[k][0], G[k][1], G[k][2], &xs[k], &ys[k], &d);
            cv.poly(xs, ys, 4, side ? P.lightgrey : P.grey, P.darkgrey1);
            for (int l = 0; l < 6; ++l) {         // lines of print
                int yy = 64 + l * 28, ax, ay, bx2, by2;
                int xa = side ? 210 : 162, xb = side ? 300 : 72;
                cam.at(xa, yy, 624, &ax, &ay, &d); cam.at(xb, yy, 642, &bx2, &by2, &d);
                cv.aline(ax, ay, bx2, by2, P.darkgrey1, 14, 200);
            }
        }
    }
}

// the whole scene: the still part copied from where it was kept unless something in it changed (the view, the life's
// level, a value, the keys, the lit squares), then what moves on top: the flower turning, the reels, life, fluff
static uint32_t base_kept = 0xffffffffu;
static void render(uint16_t* px, const State& st, uint16_t* floor_cache = 0, uint16_t* base_cache = 0) {
    const Pal& P = pal();
    Canvas cv = {px};
    uint32_t angle = st.angle & ~63u;            // in steps of a third of a degree: the floor and the piano alike
    int level = st.life >> 5;                     // 0..8
    uint32_t lit = 0;
    for (int i = 0; i < 12; ++i) if (st.pulse[i] > 96 - st.life / 4) lit |= 1u << i;
    const uint32_t parts[11] = {angle, (uint32_t)level, (uint32_t)st.tone, (uint32_t)st.release, (uint32_t)st.stretch,
                                (uint32_t)st.start, (uint32_t)st.loop_in, (uint32_t)st.loop_out, (uint32_t)st.shift, st.keys, lit};
    uint32_t key = 0x9e3779b9u;
    for (int i = 0; i < 11; ++i) key = hash(key ^ (parts[i] + (uint32_t)i * 0x45d9f3bu));
    if (base_cache && key == base_kept) {
        for (int i = 0; i < W * H; ++i) px[i] = base_cache[i];
    } else {
        base_layer(px, st, floor_cache, angle, level);
        if (base_cache) { for (int i = 0; i < W * H; ++i) base_cache[i] = px[i]; base_kept = key; }
    }
    Cam cam; cam.c = cos14(angle); cam.s = sin14(angle);
    int d;
    if (st.stretch == 0) {
        for (int k = 0; k < 6; ++k) {                                   // the flower in the pot, turning, facing the front
            uint32_t a = (uint32_t)k * 10923u + st.time * 20u;
            vdot(cv, cam, 194 + ((38 * cos14(a)) >> 14), 150, 900 + ((38 * sin14(a)) >> 14), 25, k & 1 ? P.red : P.darkred);
        }
        vdot(cv, cam, 194, 151, 900, 17, P.lightbrown);
    } else if (st.stretch == 1) {
        const int z1 = 660;
        uint32_t spin = st.time * 300u;
        reel(cv, cam, 112, 112, z1 + 1, 40, spin, P.darkgrey1, P.lightgrey);
        reel(cv, cam, 240, 112, z1 + 1, 32, spin, P.darkgrey1, P.lightgrey);
        int ax, ay, cx, cy;                       // the tape between the reels, along the front
        cam.at(112, 152, z1 + 1, &ax, &ay, &d); cam.at(240, 144, z1 + 1, &cx, &cy, &d);
        cv.aline(ax, ay, cx, cy, P.brown, EDGE);
    }
    sprouts(cv, cam, st);
    petals(cv, st);
    fluff(cv, cam, st);
}

}  // namespace scene
#endif
