// bows_scene.h: bows' page, a small isometric scene drawn by the pack itself into a 480x220 block of 16-bit pixels,
// which the frame function hands to op1f_paint_block. A string section in the dark: the cello and the double bass stand
// at the back, their bows leant on them; the viola and the violin float in front; four music stands between them.
// While an instrument's register sounds, its bow floats over its strings and draws back and forth. With playing the
// scene comes alive: ribbon bows are tied on the instruments, ribbons and confetti gather on the floor, then
// confetti falls; without playing it fades back to the instruments alone. The ribbons'
// colour follows the style: red (1), blue (2), grey (3). Layout from tools/section.py (bows_section.h).
// Framework (canvas, camera, faces) from roads_scene.h. No float, no allocation, no calls: plain integer code, the
// same on the unit and on the PC (tests/scene_preview.cpp). GPL-3.0-or-later with OP-1 firmware permission.
#ifndef BOWS_SCENE_H
#define BOWS_SCENE_H
#include <stdint.h>

namespace scene {

enum { W = 480, H = 220 };

struct State {
    uint32_t angle, time;
    int life;              // 0..1023: 0 the instruments alone; ribbon bows from 60, floor 380+, falling 750+
    uint32_t lit;          // bit i: SECTION[i] sounding (0 cello, 1 bass, 2 viola, 3 violin)
    int style;             // 0..2: the ribbons' colour
    uint32_t notes, notes_seen;   // notes begun so far (the page sets it); step() turns each new one into life
};

// life: each new note wakes the scene a little (some 40 notes from bare to all alive), and it fades back over half a
// minute or so without playing (as roads' town)
static void step(State& st) {
    uint32_t fresh = st.notes - st.notes_seen;
    st.notes_seen = st.notes;
    if (fresh > 64u) fresh = 64u;
    st.life += (int)fresh * 26;
    if (st.life > 1023) st.life = 1023;
    if (st.life > 0 && (st.time & 1u)) st.life -= 1;
}

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
// the case's tolex (top, front, side, edge) by the style: black, blue (SLOW), grey (LOFI); red is ambient's
static const uint16_t TOLEX[3][4] = {
    {rgb(0x37,0x3d,0x42), rgb(0x1e,0x25,0x2b), rgb(0x16,0x1b,0x20), rgb(0x5b,0x66,0x70)},
    {rgb(0x2b,0x42,0x61), rgb(0x0e,0x1c,0x26), rgb(0x1e,0x25,0x2b), rgb(0x3c,0x60,0x87)},
    {rgb(0x5b,0x66,0x70), rgb(0x37,0x3d,0x42), rgb(0x1e,0x25,0x2b), rgb(0xa2,0xa4,0xa6)},
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

// the swivel (as roads'): the view turns slowly to one side of the isometric view and to the other, some 12 degrees
// each way (further shows the upright instruments edge on), eased; a round trip in 1456 frames
static uint32_t swivel(uint32_t time) {
    return (uint32_t)((2200 * sin14(time * 45u)) >> 14);
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
    int k = 1024;                               // Q10 scale about the middle
    int cx = 352, cy = 192, ox = 190, oy = 142; // the middle (world) and where it lands (pixels)
    // world in 1/64 unit; a unit is about 9 pixels
    void at(int x, int y, int z, int* sx, int* sy, int* depth) const {
        x -= cx; y -= cy;                       // about the section's middle
        if (k != 1024) { x = (x * k) >> 10; y = (y * k) >> 10; z = (z * k) >> 10; }
        int xr = (x * c - y * s) >> 14, yr = (x * s + y * c) >> 14;
        // isometric: sx = (x - y) cos30 U, sy = (x + y) sin30 U - z U, U = 7 px; in 1/16 pixel from 1/64 unit
        *sx = (ox << 4) + (((xr - yr) * 125) >> 6);    // 1/16 pixel: 9 px a unit: 9 * cos30 * 16 / 64 = 125 / 64
        *sy = (oy << 4) + (((xr + yr) * 72) >> 6) - ((z * 144) >> 6);
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


static Builder builder;   // 160 faces: kept off the stack
static uint32_t hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

// ------------------------------------------------------------------ the scene



// a round piece in a plane of constant z, `n` sides
static void disc3(Canvas& cv, const Cam& cam, int cx, int cy, int z, int r, uint16_t c, int n = 12) {
    int xs[16], ys[16], d;
    if (n > 16) n = 16;
    for (int k = 0; k < n; ++k) {
        uint32_t b = (uint32_t)k * 65536u / (uint32_t)n;
        cam.at(cx + ((r * cos14(b)) >> 14), cy + ((r * sin14(b)) >> 14), z, &xs[k], &ys[k], &d);
    }
    cv.poly(xs, ys, n, c, 0xffffu);
}
static void line3(Canvas& cv, const Cam& cam, int x0, int y0, int z0, int x1, int y1, int z1, uint16_t c, int w, int alpha = 256) {
    int ax, ay, bx, by, d;
    cam.at(x0, y0, z0, &ax, &ay, &d); cam.at(x1, y1, z1, &bx, &by, &d);
    cv.aline(ax, ay, bx, by, c, w, alpha);
}

#include "bows_scene_body.h"

}  // namespace scene
#endif
