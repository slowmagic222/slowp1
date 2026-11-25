// roads_scene.h: roads' page, a small 3D scene drawn by the pack itself into a 480x220 block of 16-bit pixels, which
// the frame function hands to op1f_paint_block. A suitcase electric piano (the keys on top of their speaker cabinet,
// silver trim, the speaker cloth in front) stands on a kids' road rug: grass, grey roads with dashed middle lines, a
// roundabout, a zebra crossing, little houses and trees, fading out toward the edges. A toy car on the piano's lid rolls,
// slowly, along it to above the last note played. The scene swivels from the isometric view (the keys facing the values,
// right) to the front and back, never further, so the faces are drawn in a fixed order (far to near), no sorting.
// Knob colours (Field): the four little knobs on the piano's panel, left of the keys, are the four values (blue
// muffle, ochre sustain, grey style, red ambient), each pointing at its value. Wow (shift, red) makes the car wobble on
// its springs; red ambient = soft puffs from its exhaust. Style = the tolex: black (OG), blue (SLOW), grey (LOFI), and
// the car's paint (light grey, blue, grey). With
// shift: blue start, ochre loop in, grey loop out = three marks along the silver name rail (the loop between lit).
// Played notes light keys. No float, no allocation, no calls: plain integer code, the same on the unit and on the PC
// (tests/scene_preview.cpp). Code from felt_scene.h (felt.pack). GPL-3.0-or-later with OP-1 firmware permission.
#ifndef ROADS_SCENE_H
#define ROADS_SCENE_H
#include <stdint.h>

namespace scene {

enum { W = 480, H = 220 };

struct State {
    uint32_t angle;        // the turn, 0..65535 a whole turn; swivel() gives it, between 0 (isometric) and -45 degrees (front)
    uint32_t time;         // frames, for the animation
    int tone, sustain, wow, ambient;  // 0..100
    int style;             // 0 OG, 1 SLOW, 2 LOFI
    uint32_t keys;         // bit k: white key k (0..14) lit
    int sounding;          // voices sounding
    int start, loop_in, loop_out;     // the shift values, 0..100
    int shift;             // shift held: the shift marks drawn brighter
    int target;            // the white key (0..14) of the last note played: where the car rolls to
    int car_x;             // the car's place along the keys, world 1/64 units (step() moves it)
    uint32_t wheel;        // the wheels' turn, 0..65535
    int car_dir;           // 1 heading right (+x), -1 left
    int green;             // the rug's grass: 0 the Field's palette (umber), 1 a muted green
    uint32_t notes;        // notes begun so far (the page sets it); step() turns each new one into life
    uint32_t notes_seen;
    int life;              // 0..1023: the town wakes with playing and sleeps without (step() keeps it)
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
// the case's tolex (top, front, side, edge) by the style: black (OG), blue (SLOW), grey (LOFI); red is ambient's
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

// the swivel: isometric from the piano's front-left corner at frame 0 (-90 degrees: the keys face right, toward the
// values), to the front (-45 degrees) and back, eased; a round trip in 1456 frames
static uint32_t swivel(uint32_t time) {
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
    int k = 1024;                               // Q10 scale about the piano's middle: the town is drawn at 0.85 (v8)
    // world in 1/64 unit; a unit is about 9 pixels
    void at(int x, int y, int z, int* sx, int* sy, int* depth) const {
        x -= 352; y -= 192;                     // about the piano's middle (5.5, 3.0)
        if (k != 1024) { x = (x * k) >> 10; y = (y * k) >> 10; z = (z * k) >> 10; }
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

static inline uint16_t brighter(uint16_t c) {
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

static uint16_t dim(uint16_t c, int alpha) { return alpha >= 256 ? c : alpha <= 0 ? (uint16_t)0 : mix(0, c, alpha); }

// how much of the rug shows at (x, y): full near the piano, nothing at its corners
enum { RX0 = -416, RX1 = 1104, RY0 = -368, RY1 = 768 };   // the rug's edge (v8: fits at 0.85)
static int floor_alpha(int x, int y) {
    // v6: the rug no longer fades, so neither does what stands on it: one level on the rug, nothing off it
    return x > RX0 && x < RX1 && y > RY0 && y < RY1 ? 190 : 0;
}

static void patch(Canvas& cv, const Cam& cam, int x0, int y0, int x1, int y1, int z, uint16_t c) {
    int xs[4], ys[4], d;
    cam.at(x0, y0, z, &xs[0], &ys[0], &d); cam.at(x1, y0, z, &xs[1], &ys[1], &d);
    cam.at(x1, y1, z, &xs[2], &ys[2], &d); cam.at(x0, y1, z, &xs[3], &ys[3], &d);
    cv.poly(xs, ys, 4, c, 0xffffu);
}
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
// a round piece in an upright plane facing the front (constant y)
static void vdisc(Canvas& cv, const Cam& cam, int cx, int y, int cz, int r, uint16_t c, uint16_t edge = 0xffffu) {
    int xs[10], ys[10], d;
    for (int k = 0; k < 10; ++k) {
        uint32_t b = (uint32_t)k * 6554u;
        cam.at(cx + ((r * cos14(b)) >> 14), y, cz + ((r * sin14(b)) >> 14), &xs[k], &ys[k], &d);
    }
    cv.poly(xs, ys, 10, c, edge);
}
static void line3(Canvas& cv, const Cam& cam, int x0, int y0, int z0, int x1, int y1, int z1, uint16_t c, int w, int alpha = 256) {
    int ax, ay, bx, by, d;
    cam.at(x0, y0, z0, &ax, &ay, &d); cam.at(x1, y1, z1, &bx, &by, &d);
    cv.aline(ax, ay, bx, by, c, w, alpha);
}

// ---- the rug: a 64-unit grid of tiles, each grass or road, faded by its place; then the markings and the town
enum { QX0 = -448, QX1 = 1152, QY0 = -384, QY1 = 960, T = 64 };
// the roads, as bands two tiles wide: a ring round the piano and a street off its front, down to the rug's edge
enum { BACK_Y = -256, FRONT_Y = 448, LEFT_X = -320, RIGHT_X = 896, SIDE_X = 320, ROAD = 128 };
struct RugColours { uint16_t grass, grass2, road, line, tree, trunk; };
static RugColours rug_colours(const State& st) {
    const Pal& P = pal();
    if (st.green) return {P.umber, P.brown, P.darkgrey2, P.lightgrey, P.darkumber, P.offbrown};   // green 1: the old umber
    return {rgb(0x1f,0x2b,0x19), rgb(0x2c,0x3d,0x22), P.darkgrey2, P.lightgrey, rgb(0x1e,0x36,0x18), P.umber};   // v7: a subtle green rug
}

static void dashes(Canvas& cv, const Cam& cam, int x0, int y0, int x1, int y1, uint16_t c) {   // a dashed middle line
    int len = (x1 > x0 ? x1 - x0 : x0 - x1) + (y1 > y0 ? y1 - y0 : y0 - y1);
    for (int t = 8; t + 22 <= len; t += 44) {
        int ax = x0 + (x1 - x0) * t / len, ay = y0 + (y1 - y0) * t / len;
        int bx = x0 + (x1 - x0) * (t + 22) / len, by = y0 + (y1 - y0) * (t + 22) / len;
        int a = floor_alpha((ax + bx) >> 1, (ay + by) >> 1);
        if (a > 30) line3(cv, cam, ax, ay, 1, bx, by, 1, c, 18, a + 40 > 256 ? 256 : a + 40);
    }
}

// how awake one thing is that wakes at `at` (0..1023): 0 asleep, 256 fully, a soft ramp between
static int awake(const State& st, int at) {
    int a = (st.life - at) * 4;
    return a <= 0 ? 0 : a >= 256 ? 256 : a;
}
static void rug(Canvas& cv, const Cam& cam, const State& st) {
    const RugColours R = rug_colours(st);
    // v6: a flat green rug with rounded corners and a darker hem, one polygon each and no fade over it: no seams to
    // shimmer as the view turns, and far fewer sums a pixel than v3's faded grass. The roads stop at the hem.
    {
        // v7: it rolls out from under the piano as the town wakes (life 0..250), and back in as it sleeps
        int g = st.life * 256 / 250;
        if (g <= 0) return;
        if (g > 256) g = 256;
        const int x0 = 352 + (RX0 - 352) * g / 256, x1 = 352 + (RX1 - 352) * g / 256;
        const int y0 = 192 + (RY0 - 192) * g / 256, y1 = 192 + (RY1 - 192) * g / 256;
        const int c = 72 * g / 256, h = 20;                 // the corners' cut, the hem's width
        const int X[8] = {x0 + c, x1 - c, x1, x1, x1 - c, x0 + c, x0, x0};
        const int Y[8] = {y0, y0, y0 + c, y1 - c, y1, y1, y1 - c, y0 + c};
        const int IX[8] = {x0 + c, x1 - c, x1 - h, x1 - h, x1 - c, x0 + c, x0 + h, x0 + h};
        const int IY[8] = {y0 + h, y0 + h, y0 + c, y1 - c, y1 - h, y1 - h, y1 - c, y0 + c};
        int xs[8], ys[8], d;
        for (int k = 0; k < 8; ++k) cam.at(X[k], Y[k], 0, &xs[k], &ys[k], &d);
        cv.poly(xs, ys, 8, R.tree, 0xffffu);
        for (int k = 0; k < 8; ++k) cam.at(IX[k], IY[k], 0, &xs[k], &ys[k], &d);
        cv.poly(xs, ys, 8, R.grass, 0xffffu);
    }
    // the roads come next (life 250..350), out of the grass; then their markings
    const int ra = awake(st, 250);
    if (!ra) return;
    const RugColours Rg = R;
    const uint16_t road = mix(Rg.grass, Rg.road, ra);
    const int E0 = RX0 + 20, E1 = RX1 - 20, F0 = RY0 + 20, F1 = RY1 - 20;   // the hem's inner edge
    patch(cv, cam, E0, BACK_Y, E1, BACK_Y + ROAD, 1, road);
    patch(cv, cam, E0, FRONT_Y, E1, FRONT_Y + ROAD, 1, road);
    patch(cv, cam, LEFT_X, BACK_Y, LEFT_X + ROAD, FRONT_Y + ROAD, 1, road);
    patch(cv, cam, RIGHT_X, F0, RIGHT_X + ROAD, F1, 1, road);
    patch(cv, cam, SIDE_X, FRONT_Y, SIDE_X + ROAD, F1, 1, road);
    {   // the roundabout where the right road meets the front one: a grey ring, a grass island
        int cx = RIGHT_X + ROAD / 2, cy = FRONT_Y + ROAD / 2;
        disc3(cv, cam, cx, cy, 0, 120, road, 16);
        disc3(cv, cam, cx, cy, 1, 44, R.grass2, 12);
    }
    if (ra < 256) return;                                  // the markings once the roads are down
    {
        int cx = RIGHT_X + ROAD / 2, cy = FRONT_Y + ROAD / 2, a = floor_alpha(cx, cy);
        for (int k = 0; k < 12; k += 2) {                 // the ring's dashes
            uint32_t a0 = (uint32_t)k * 5461u, a1 = a0 + 3000u;
            line3(cv, cam, cx + ((82 * cos14(a0)) >> 14), cy + ((82 * sin14(a0)) >> 14), 2,
                  cx + ((82 * cos14(a1)) >> 14), cy + ((82 * sin14(a1)) >> 14), 2, R.line, 16, a);
        }
    }
    // the middle lines
    dashes(cv, cam, E0, BACK_Y + ROAD / 2, E1, BACK_Y + ROAD / 2, R.line);
    dashes(cv, cam, E0, FRONT_Y + ROAD / 2, RIGHT_X - 40, FRONT_Y + ROAD / 2, R.line);
    dashes(cv, cam, LEFT_X + ROAD / 2, BACK_Y + ROAD, LEFT_X + ROAD / 2, FRONT_Y, R.line);
    dashes(cv, cam, RIGHT_X + ROAD / 2, F0, RIGHT_X + ROAD / 2, FRONT_Y - 40, R.line);
    dashes(cv, cam, RIGHT_X + ROAD / 2, FRONT_Y + ROAD + 40, RIGHT_X + ROAD / 2, F1, R.line);
    dashes(cv, cam, SIDE_X + ROAD / 2, FRONT_Y + ROAD, SIDE_X + ROAD / 2, F1, R.line);
    {   // a zebra crossing on the street off the front road
        int y0 = FRONT_Y + ROAD + 40, a = floor_alpha(SIDE_X + 64, y0);
        for (int k = 0; k < 5; ++k) {
            int x = SIDE_X + 10 + k * 24;
            patch(cv, cam, x, y0, x + 12, y0 + 40, 1, dim(R.line, a));
        }
    }
}

// the town round the roads: little houses (a wall box, a two-faced roof) and round trees. `near`: only those on the
// viewer's side of the piano (in front, +y, or left, -x), drawn after it; otherwise the far ones, before it
struct House { int x, y, w, d, h, roof; };
static const House HOUSES[] = {
    {-184, -96, 88, 72, 64, 0}, {64, -128 - 80, 112, 64, 56, 1}, {512, -128 - 80, 80, 64, 72, 2}, {800, -96, 80, 80, 60, 0},
    {1056, 160, 64, 96, 64, 1}, {768, 640, 96, 80, 56, 2}, {96, 640, 112, 80, 64, 0}, {-288, 640, 80, 64, 72, 1},
    {-224, 192, 64, 96, 56, 2}, {512, 704, 64, 64, 60, 1},
};
struct Tree { int x, y, r; };
static const Tree TREES[] = {
    {-136, 48, 26}, {-104, 320, 30}, {800, 96, 28}, {800, 320, 24}, {-176, -360 + 60, 24}, {1064, -192, 28}, {640, 730, 26},
    {-40, 700, 28}, {1040, 700, 24}, {224, -170, 22},
};
static bool near_side(int x, int y) { return y > 288 || x < 0; }
static void town(Canvas& cv, const Cam& cam, Builder& b, const State& st, bool near) {
    const Pal& P = pal();
    const RugColours R = rug_colours(st);
    const uint16_t roofs[3][2] = {{P.red, P.darkred}, {P.blue, P.darkblue}, {P.lightbrown, P.brown}};
    for (unsigned i = 0; i < sizeof TREES / sizeof TREES[0]; ++i) {
        const Tree& t = TREES[i];
        if (near_side(t.x, t.y) != near) continue;
        int a = floor_alpha(t.x, t.y);
        int grow = awake(st, 300 + (int)i * 25);              // v7: the trees come up one by one
        if (a < 40 || !grow) continue;
        int show = grow * 220 / 256;                          // v8: faded in from the rug
        int th = 30 * grow / 256, tr = t.r * grow / 256;
        line3(cv, cam, t.x, t.y, 0, t.x, t.y, th, mix(R.grass, R.trunk, show), 40);
        int sx, sy, d; cam.at(t.x, t.y, th + tr, &sx, &sy, &d);
        int r = (tr * 9 / 64 + 2) * cam.k >> 10;
        cv.disc(sx >> 4, sy >> 4, r, mix(R.grass, R.tree, show), mix(R.grass, R.grass2, show));
    }
    for (unsigned i = 0; i < sizeof HOUSES / sizeof HOUSES[0]; ++i) {
        House h = HOUSES[i];
        if (near_side(h.x + h.w / 2, h.y + h.d / 2) != near) continue;
        int a = floor_alpha(h.x + h.w / 2, h.y + h.d / 2);
        int rise = awake(st, 400 + (int)i * 40);              // v7: the houses rise out of the rug one by one
        if (a < 40 || !rise) continue;
        h.h = h.h * (128 + rise / 2) / 256;
        // v8: faded in from the rug's green as they rise (and out again), and never full bright: they sit back
        const int show = 170 * rise / 256;
        auto fade = [&](uint16_t c) { return mix(R.grass, c, show); };
        b.box(h.x, h.y, 0, h.x + h.w, h.y + h.d, h.h, fade(P.grey), fade(P.lightgrey), fade(P.grey), 0xffffu);
        b.draw(cv);
        // the roof: a ridge along x, two slopes (the far one first), the gable on the left end
        int mid = h.y + h.d / 2, top = h.h + h.d / 2;
        int F[4][3] = {{h.x - 4, h.y - 4, h.h}, {h.x + h.w + 4, h.y - 4, h.h}, {h.x + h.w + 4, mid, top}, {h.x - 4, mid, top}};
        int N[4][3] = {{h.x - 4, mid, top}, {h.x + h.w + 4, mid, top}, {h.x + h.w + 4, h.y + h.d + 4, h.h}, {h.x - 4, h.y + h.d + 4, h.h}};
        b.face(F, fade(roofs[h.roof][1]), 0xffffu);
        b.face(N, fade(roofs[h.roof][0]), 0xffffu);
        b.draw(cv);
        int G[4][3] = {{h.x, h.y, h.h}, {h.x, mid, top}, {h.x, mid, top}, {h.x, h.y + h.d, h.h}};
        int xs[3], ys[3], d;
        cam.at(G[0][0], G[0][1], G[0][2], &xs[0], &ys[0], &d); cam.at(G[1][0], G[1][1], G[1][2], &xs[1], &ys[1], &d);
        cam.at(G[3][0], G[3][1], G[3][2], &xs[2], &ys[2], &d);
        cv.poly(xs, ys, 3, fade(P.grey), 0xffffu);
        // a door and a window on its front
        int D[4][3] = {{h.x + h.w / 2 - 8, h.y + h.d + 1, 0}, {h.x + h.w / 2 - 8, h.y + h.d + 1, 26}, {h.x + h.w / 2 + 8, h.y + h.d + 1, 26}, {h.x + h.w / 2 + 8, h.y + h.d + 1, 0}};
        b.face(D, fade(roofs[(h.roof + 1) % 3][1]), 0xffffu);
        int Wn[4][3] = {{h.x + 6, h.y + h.d + 1, 28}, {h.x + 6, h.y + h.d + 1, 50}, {h.x + 28, h.y + h.d + 1, 50}, {h.x + 28, h.y + h.d + 1, 28}};
        int Ws[4][3] = {{h.x - 1, h.y + h.d / 2 - 12, 26}, {h.x - 1, h.y + h.d / 2 - 12, 48}, {h.x - 1, h.y + h.d / 2 + 12, 48}, {h.x - 1, h.y + h.d / 2 + 12, 26}};
        int lit = rise < 256 ? 0 : awake(st, 500 + (int)i * 50);              // v5: the windows light up as the town wakes, one house at a time
        uint16_t glass = P.darkblue;
        b.face(Wn, lit ? mix(fade(P.darkblue), P.lightbrown, lit * 220 / 256) : fade(glass), 0xffffu);   // lit: a lamp in the dark
        b.face(Ws, lit ? mix(fade(P.darkblue), P.lightbrown, lit * 220 / 256) : fade(glass), 0xffffu, true);
        int cx = h.x + h.w - 22, cy = mid - 6;                // a chimney on the far slope
        b.box(cx, cy - 10, top - 14, cx + 12, cy + 2, top + 10, fade(P.lightgrey), fade(P.grey), fade(P.darkgrey1), 0xffffu);
        b.draw(cv);
        int smoke = rise < 256 ? 0 : awake(st, 600 + (int)i * 40);            // and their chimneys smoke a little later
        if (smoke) {
            for (int k = 0; k < 4; ++k) {
                uint32_t hh = hash(i * 977u + (uint32_t)k * 131u);
                int life = 80 + (int)(hh % 40u), t = (int)((st.time + (hh >> 8)) % (uint32_t)life);
                int sx, sy, d;
                cam.at(cx + 6 + t / 4 + ((6 * sin14((uint32_t)t * 700u + hh)) >> 14), cy - 4, top + 12 + t, &sx, &sy, &d);
                int a2 = smoke * 3 / 4 * (life - t) / life;
                if (t < 8) a2 = a2 * t / 8;
                int rr = 1 + t * 3 / life;
                for (int oy = -rr; oy <= rr; ++oy)
                    for (int ox = -rr; ox <= rr; ++ox)
                        if (ox * ox + oy * oy <= rr * rr) cv.blend((sx >> 4) + ox, (sy >> 4) + oy, P.lightgrey, a2);
            }
        }
    }
}

// v5: little cars on the rug's ring road, one more as the town wakes (four in all), each in its lane, round and round
static void traffic(Canvas& cv, Builder& b, const State& st, bool near) {
    const Pal& P = pal();
    const int x0 = LEFT_X + ROAD / 2, x1 = RIGHT_X + ROAD / 2, y0 = BACK_Y + ROAD / 2, y1 = FRONT_Y + ROAD / 2;
    const int lap = 2 * (x1 - x0) + 2 * (y1 - y0);
    const uint16_t paint[4][2] = {{P.red, P.darkred}, {P.lightbrown, P.brown}, {P.lightgrey, P.grey}, {P.lightblue, P.blue}};
    for (int i = 0; i < 4; ++i) {
        int on = awake(st, 550 + i * 140);
        if (!on) continue;
        int dir = (i & 1) ? -1 : 1;                           // two each way
        int s = (int)((st.time * (uint32_t)(5 + i) + (uint32_t)i * (uint32_t)lap / 4u) % (uint32_t)lap);
        if (dir < 0) s = lap - s;
        int x, y, ax;                                          // the place round the ring, clockwise seen from above
        if (s < x1 - x0) { x = x0 + s; y = y0; ax = 1; }
        else if ((s -= x1 - x0) < y1 - y0) { x = x1; y = y0 + s; ax = 0; }
        else if ((s -= y1 - y0) < x1 - x0) { x = x1 - s; y = y1; ax = 1; }
        else { s -= x1 - x0; x = x0; y = y1 - s; ax = 0; }
        if (ax) y += dir * 24; else x -= dir * 24;             // keep to its lane
        if (near_side(x, y) != near) continue;
        int a = floor_alpha(x, y);
        if (a < 40) continue;
        const RugColours R = rug_colours(st);
        const int show = 200 * on / 256;                       // v8: faded in from the rug
        auto fade = [&](uint16_t c) { return mix(R.grass, c, show); };
        int hx = ax ? 34 : 18, hy = ax ? 18 : 34;
        b.box(x - hx, y - hy, 2, x + hx, y + hy, 20, fade(paint[i][0]), fade(paint[i][0]), fade(paint[i][1]), 0xffffu);
        b.box(x - hx / 2, y - hy / 2, 20, x + hx / 2, y + hy / 2, 34, fade(paint[i][0]), fade(P.darkblue), fade(P.darkblue), 0xffffu);
        b.draw(cv);
    }
}

// ---- the piano: geometry (world 1/64 units)
enum { KX0 = 208, KX1 = 664, KY0 = 164, KY1 = 274, KZ = 400, TOP = 478 };   // TOP: the harp cover's lid, where the car drives
static int key_middle(int k) { return KX0 + (2 * k + 1) * (KX1 - KX0) / 30; }

// the car: moves toward its key a little every frame, slowing as it arrives; the wheels turn by the distance
static void step(State& st) {
    int want = key_middle(st.target < 0 ? 0 : st.target > 14 ? 14 : st.target);
    if (st.car_x < KX0 || st.car_x > KX1) st.car_x = key_middle(7);
    int d = want - st.car_x, m = d / 8;      // v4: some three times quicker than v3
    if (m == 0 && d) m = d > 0 ? 1 : -1;
    if (m > 16) m = 16;
    if (m < -16) m = -16;
    st.car_x += m;
    if (m) st.car_dir = m > 0 ? 1 : -1;
    st.wheel -= (uint32_t)(m * 1300);
    // life: each new note wakes the town a little (some 40 notes from asleep to all awake), and it falls asleep
    // again over half a minute or so without playing
    uint32_t fresh = st.notes - st.notes_seen;
    st.notes_seen = st.notes;
    if (fresh > 64u) fresh = 64u;
    st.life += (int)fresh * 26;
    if (st.life > 1023) st.life = 1023;
    if (st.life > 0 && (st.time & 1u)) st.life -= 1;
}


// the car's paint by the style, as the title's colour: light grey (OG), blue (SLOW), grey (LOFI); lit, shaded
static const uint16_t CAR[3][2] = {
    {rgb(0xcd,0xd0,0xc2), rgb(0xa2,0xa4,0xa6)},
    {rgb(0x4c,0x7d,0xa8), rgb(0x3c,0x60,0x87)},
    {rgb(0xa2,0xa4,0xa6), rgb(0x5b,0x66,0x70)},
};

// the toy car on the piano's lid, at st.car_x: body, cabin with windows, four wheels, headlights; it bobs with wow
static void car(Canvas& cv, const Cam& cam, Builder& b, const State& st) {
    const Pal& P = pal();
    const uint16_t* paint = CAR[st.style < 0 ? 0 : st.style > 2 ? 2 : st.style];
    int cx = st.car_x, f = st.car_dir < 0 ? -1 : 1;
    int bob = (st.wow * 7 * sin14(st.time * 900u)) / (100 << 14);
    int lean = (st.wow * 5 * sin14(st.time * 900u + 9000u)) / (100 << 14);
    const int y0 = 11, y1 = 79, wz = TOP + 15, wr = 15;   // v9: on the deeper lid's crown
    for (int side = 0; side < 2; ++side) {            // the far wheels, then (after the body) the near ones
        if (side == 1) {
            b.box(cx - 56, y0, TOP + 12 + bob, cx + 56, y1, TOP + 40 + bob, paint[0], paint[0], paint[1], paint[1]);
            b.draw(cv);
            int c0 = cx - 30 * f, c1 = cx + 18 * f;       // the cabin, set back from the bonnet
            if (c0 > c1) { int t = c0; c0 = c1; c1 = t; }
            b.box(c0 + lean, y0 + 8, TOP + 40 + bob, c1 + lean, y1 - 8, TOP + 70 + bob, paint[0], P.darkblue, P.darkblue, paint[1]);
            b.draw(cv);
            int wm = (c0 + c1) / 2 + lean;                 // the near side's window pillar
            line3(cv, cam, wm, y1 - 7, TOP + 41 + bob, wm, y1 - 7, TOP + 69 + bob, paint[0], 24);
            int hx = cx + 57 * f, sx, sy, d;               // a headlight on the front
            cam.at(hx, y1 - 14, TOP + 28 + bob, &sx, &sy, &d); cv.disc(sx >> 4, sy >> 4, 1, P.lightbrown, P.lightbrown);
        }
        int y = side ? y1 + 1 : y0 - 1;
        for (int w = -1; w <= 1; w += 2) {
            int wx = cx + w * 34;
            vdisc(cv, cam, wx, y, wz, wr, P.offgrey, P.darkgrey1);
            vdisc(cv, cam, wx, y + (side ? 1 : -1), wz, 5, P.grey);
            if (side) {                                    // a spoke: the wheel turns
                uint32_t a = st.wheel;
                line3(cv, cam, wx, y + 1, wz, wx + ((11 * cos14(a)) >> 14), y + 1, wz + ((11 * sin14(a)) >> 14), P.lightgrey, 14);
            }
        }
    }
}

// ambient: soft puffs from the car's exhaust, drifting up and back, more and bigger with ambient
static void puffs(Canvas& cv, const Cam& cam, const State& st) {
    const Pal& P = pal();
    int count = st.ambient * 14 / 100;
    int ex = st.car_x - 58 * (st.car_dir < 0 ? -1 : 1);
    for (int i = 0; i < count; ++i) {
        uint32_t h = hash((uint32_t)i * 2654435761u + 31u);
        int life = 90 + (int)(h % 70u), t = (int)((st.time + (h >> 8)) % (uint32_t)life);
        int x = ex - (st.car_dir < 0 ? -1 : 1) * t * 2 / 3 + ((10 * sin14((uint32_t)t * 500u + h)) >> 14);
        int z = TOP + 20 + t * (120 + st.ambient * 3) / life;
        int a = (st.sounding ? 190 : 120) * (life - t) / life;
        if (t < 10) a = a * t / 10;
        int sx, sy, d; cam.at(x, 45, z, &sx, &sy, &d);
        int r = 1 + t * (2 + st.ambient / 34) / life;
        for (int oy = -r - 1; oy <= r + 1; ++oy)
            for (int ox = -r - 1; ox <= r + 1; ++ox) {
                int r2 = ox * ox + oy * oy, rr = (r + 1) * (r + 1);
                if (r2 <= rr) cv.blend((sx >> 4) + ox, (sy >> 4) + oy, r2 * 3 < rr ? P.lightgrey : P.red, r2 * 3 < rr ? a : a / 2);
            }
    }
}

// a small knob on the panel, its top a disc in the case's top plane, a pointer from its middle: 0 at seven o'clock
// round to 100 at five o'clock
static void knob(Canvas& cv, const Cam& cam, int cx, int cy, int value, uint16_t c, uint16_t dark) {
    const Pal& P = pal();
    disc3(cv, cam, cx, cy, KZ - 18, 22, dark, 12);
    disc3(cv, cam, cx, cy, KZ - 6, 20, c, 12);
    uint32_t a = 16384u + 8192u * 3u / 2u + (uint32_t)(value < 0 ? 0 : value > 100 ? 100 : value) * 49152u / 100u;   // round in the plane
    line3(cv, cam, cx, cy, KZ - 5, cx + ((17 * cos14(a)) >> 14), cy + ((17 * sin14(a)) >> 14), KZ - 5, P.lightgrey, 18);
}

static void render(uint16_t* px, const State& st) {
    const Pal& P = pal();
    Canvas cv = {px};
    for (int i = 0; i < W * H; ++i) px[i] = P.black;
    Cam cam; cam.c = cos14(st.angle); cam.s = sin14(st.angle);
    Builder& b = builder; b.cam = &cam; b.n = 0;

    cam.k = 870;                                             // v7: the town at 0.7, so it fits left of the values
    rug(cv, cam, st);
    traffic(cv, b, st, false);
    town(cv, cam, b, st, false);
    cam.k = 1024;

    const uint16_t* tx = TOLEX[st.style < 0 ? 0 : st.style > 2 ? 2 : st.style];
    // the speaker cabinet, its cloth and two speakers showing through
    b.box(12, -48, 0, 692, 276, 280, tx[0], tx[1], tx[2], tx[3]);   // v9: 60 deeper at the back
    b.draw(cv);
    {
        int F[4][3] = {{56, 277, 32}, {56, 277, 248}, {648, 277, 248}, {648, 277, 32}};
        b.face(F, P.darkgrey2, P.darkgrey1);
        b.draw(cv);
        for (int z = 48; z < 248; z += 16) line3(cv, cam, 60, 278, z, 644, 278, z, P.offgrey, 10, 120);   // the weave
        vdisc(cv, cam, 224, 278, 140, 72, P.offgrey, P.darkgrey1);
        vdisc(cv, cam, 480, 278, 140, 72, P.offgrey, P.darkgrey1);
        vdisc(cv, cam, 224, 279, 140, 26, P.darkgrey2);
        vdisc(cv, cam, 480, 279, 140, 26, P.darkgrey2);
    }
    // the silver band where the piano sits on the cabinet, then the piano's lower case and its harp cover
    b.box(0, -56, 280, 704, 284, 296, P.grey, P.lightgrey, P.grey, P.darkgrey1);
    b.box(0, -60, 296, 704, 288, 372, tx[0], tx[1], tx[2], tx[3]);
    b.draw(cv);
    {   // the harp cover: a lid rounded front to back, as the electric piano's. Its profile across y, back to front;
        // strips along x, far to near; the front a short upright face (the name rail's); the left end cut to the profile
        const int n = 9;
        const int Y[n] = {-60, -49, -29, 2, 45, 88, 119, 139, 150};   // v9: the lid 210 deep (was 150), the same curve
        const int Z[n] = {372 + 52, 372 + 82, 372 + 98, 372 + 105, 372 + 106, 372 + 104, 372 + 96, 372 + 80, 372 + 58};
        for (int i = 0; i + 1 < n; ++i) {
            int F[4][3] = {{0, Y[i], Z[i]}, {704, Y[i], Z[i]}, {704, Y[i + 1], Z[i + 1]}, {0, Y[i + 1], Z[i + 1]}};
            int slope = Z[i + 1] - Z[i];                     // rising toward the front: lit; falling: the front's shade
            uint16_t col = slope > 4 ? tx[2] : slope < -10 ? tx[1] : slope < -3 ? mix(tx[0], tx[1], 128) : tx[0];
            b.face(F, col, 0xffffu, true);
        }
        b.draw(cv);
        int Fr[4][3] = {{0, 150, 372}, {0, 150, Z[n - 1]}, {704, 150, Z[n - 1]}, {704, 150, 372}};
        b.face(Fr, tx[1], tx[3]);
        b.draw(cv);
        int xs[n + 2], ys[n + 2], d;                         // the left end, the profile closed along the bottom
        for (int i = 0; i < n; ++i) cam.at(0, Y[i], Z[i], &xs[i], &ys[i], &d);
        cam.at(0, 150, 372, &xs[n], &ys[n], &d); cam.at(0, -60, 372, &xs[n + 1], &ys[n + 1], &d);
        if (cam.s < 0 || cam.c > 0) cv.poly(xs, ys, n + 2, tx[2], 0xffffu);
        for (int i = 0; i + 1 < n; ++i) cv.aline(xs[i], ys[i], xs[i + 1], ys[i + 1], tx[3], EDGE);   // its rim
        cv.aline(xs[0], ys[0], xs[n + 1], ys[n + 1], tx[3], EDGE);   // v4: and down the back, so the rim closes
        int ax, ay, bx2, by2;                                // the lid's ridge lines along x: its rounded edges
        cam.at(0, 150, Z[n - 1], &ax, &ay, &d); cam.at(704, 150, Z[n - 1], &bx2, &by2, &d);
        cv.aline(ax, ay, bx2, by2, tx[3], EDGE);
    }
    {   // the name rail along the harp cover's front: silver; the shift marks along it, the loop lit between
        const int y = 151, z0 = 394, z1 = 420, x0 = 40, x1 = 664;
        int F[4][3] = {{x0, y, z0}, {x0, y, z1}, {x1, y, z1}, {x1, y, z0}};
        b.face(F, P.grey, P.lightgrey);
        int in = x0 + (x1 - x0) * st.loop_in / 100, out = x0 + (x1 - x0) * st.loop_out / 100;
        if (out > in + 4) {
            int L[4][3] = {{in, y + 1, z0 + 6}, {in, y + 1, z1 - 6}, {out, y + 1, z1 - 6}, {out, y + 1, z0 + 6}};
            b.face(L, st.shift ? P.lightblue : P.slate, 0xffffu);
        }
        b.draw(cv);
        const int at[3] = {st.start, st.loop_in, st.loop_out};
        const uint16_t mc[3] = {P.lightblue, P.lightbrown, P.lightgrey};
        for (int k = 0; k < 3; ++k) {
            int x = x0 + (x1 - x0) * at[k] / 100;
            line3(cv, cam, x, y + 2, z0 - 4, x, y + 2, z1 + (st.shift ? 20 : 10), mc[k], st.shift ? 40 : EDGE);
        }
    }
    b.box(0, 150, 372, 704, 288, 384, P.offgrey, tx[1], tx[2], tx[3]);    // the key bed's floor (v4: no end cheeks)
    b.draw(cv);
    {   // the panel left of the keys: four small knobs in the Field's knob colours, each pointing at its value
        knob(cv, cam, 92, 192, st.tone, P.blue, P.darkblue);
        knob(cv, cam, 152, 192, st.sustain, P.brown, P.darkumber);
        knob(cv, cam, 92, 248, st.style * 50, P.grey, P.darkgrey1);   // v4: the third knob is the style
        knob(cv, cam, 152, 248, st.ambient, P.red, P.darkred);
    }
    {   // the keys: fifteen white, one slab with soft lines between, the lit ones blue; the black ones
        int F[4][3] = {{KX0, KY1, KZ - 16}, {KX0, KY1, KZ}, {KX1, KY1, KZ}, {KX1, KY1, KZ - 16}};
        b.face(F, P.grey, 0xffffu);
        int Tt[4][3] = {{KX0, KY0, KZ}, {KX1, KY0, KZ}, {KX1, KY1, KZ}, {KX0, KY1, KZ}};
        b.face(Tt, P.lightgrey, 0xffffu);
        b.draw(cv);
        for (int k = 0; k < 15; ++k) {
            if (!((st.keys >> k) & 1u)) continue;
            int x0 = KX0 + k * (KX1 - KX0) / 15, x1 = KX0 + (k + 1) * (KX1 - KX0) / 15;
            int L[4][3] = {{x0, KY0, KZ}, {x1, KY0, KZ}, {x1, KY1, KZ}, {x0, KY1, KZ}};
            b.face(L, P.lightblue, 0xffffu);
        }
        b.draw(cv);
        for (int k = 1; k < 15; ++k) {
            int x = KX0 + k * (KX1 - KX0) / 15;
            line3(cv, cam, x, KY0, KZ, x, KY1, KZ, P.darkgrey1, 16);
        }
        for (int k = 0; k < 14; ++k) {
            if (k % 7 == 2 || k % 7 == 6) continue;
            int x = KX0 + (k + 1) * (KX1 - KX0) / 15, w = (KX1 - KX0) / 15 * 3 / 10;
            int B[4][3] = {{x - w, KY0, KZ + 1}, {x + w, KY0, KZ + 1}, {x + w, 224, KZ + 1}, {x - w, 224, KZ + 1}};
            b.face(B, P.offgrey, 0xffffu);
        }
        b.draw(cv);
        // the silver strip along the keys' front
        b.box(0, 276, 372, 704, 290, 392, P.grey, P.lightgrey, P.grey, 0xffffu);
        b.draw(cv);
    }

    car(cv, cam, b, st);
    puffs(cv, cam, st);
    cam.k = 870;
    traffic(cv, b, st, true);
    town(cv, cam, b, st, true);
    cam.k = 1024;
}

}  // namespace scene
#endif
