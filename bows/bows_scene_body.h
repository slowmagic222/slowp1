// The scene is built the way felt's and roads' are: solids of flat faces (bodies, necks, boxes), each face shaded by
// how it turns to the light, outlined, sorted far to near (Builder), in the Field's palette; thin things (strings, bow
// sticks, stand legs) as smooth lines.

// the ribbons' colours by the style: ribbon, its folds, its edge; (the rug's three rings, unused since v11)
struct Tint { uint16_t ribbon, fold, edge, rug0, rug1, rug2; };
static const Tint TINTS[3] = {
    {rgb(0xe5,0x28,0x17), rgb(0x9e,0x17,0x10), rgb(0x33,0x1c,0x29), rgb(0x33,0x1c,0x29), rgb(0x5a,0x14,0x12), rgb(0x46,0x12,0x14)},
    {rgb(0x4c,0x7d,0xa8), rgb(0x2b,0x42,0x61), rgb(0x0e,0x1c,0x26), rgb(0x0e,0x1c,0x26), rgb(0x2b,0x42,0x61), rgb(0x1e,0x30,0x46)},
    {rgb(0xcd,0xd0,0xc2), rgb(0xa2,0xa4,0xa6), rgb(0x37,0x3d,0x42), rgb(0x1e,0x25,0x2b), rgb(0x37,0x3d,0x42), rgb(0x2a,0x31,0x36)},
};
static const Tint& tint(const State& st) { return TINTS[st.style < 0 ? 0 : st.style > 2 ? 2 : st.style]; }

// a material: lit, mid, shaded, its edge
struct Mat { uint16_t lit, mid, dark, edge; };
static const Mat EBONY = {rgb(0x5b,0x66,0x70), rgb(0x37,0x3d,0x42), rgb(0x1e,0x25,0x2b), rgb(0x5b,0x66,0x70)};
static const Mat METAL = {rgb(0x5b,0x66,0x70), rgb(0x37,0x3d,0x42), rgb(0x1e,0x25,0x2b), rgb(0x5b,0x66,0x70)};
static const Mat PAPER = {rgb(0xcd,0xd0,0xc2), rgb(0xa2,0xa4,0xa6), rgb(0x5b,0x66,0x70), rgb(0xa2,0xa4,0xa6)};
static const Mat MAPLE = {rgb(0xcd,0xd0,0xc2), rgb(0xad,0x89,0x59), rgb(0x8a,0x65,0x3c), rgb(0x5e,0x55,0x4b)};

struct Wood { uint16_t lit, mid, dark, edge; };
struct Inst {
    int x, y, z;       // the body's bottom, in the middle (world 1/64 unit)
    int u[3], v[3], n[3];   // Q14: up the neck, across, out of the front
    int bh, bw, neck;  // the body's height and half its widest width; from the body's top to the scroll's top
    int sloped;        // 1: the bass's sloped shoulders
    Wood wood;
    int bow_at;        // the life at which its ribbon bow is tied
    int pin;           // the end pin's length (0: floating)
    int bow_side;      // its bow at rest: 1 leant rising right, -1 rising left, 0 lying beside it
};

// the body's outline: 12 points bottom to top, (height Q8 of bh, half width Q8 of bw); straight facets, pointed corners
enum { NP = 12 };
static const uint16_t VIOLIN_P[NP][2] = {{0,110},{15,200},{46,256},{82,240},{97,204},{110,150},{148,150},{160,194},{184,210},{220,192},{246,140},{256,74}};
static const uint16_t BASS_P[NP][2]   = {{0,110},{15,200},{46,256},{82,244},{97,210},{112,162},{146,158},{158,186},{180,198},{210,184},{238,140},{256,70}};

#include "bows_section.h"

// ---- a frame: an origin and three axes (Q14); things are made in its a (u), b (v), c (n) and turned into the world
struct Frame { int o[3], u[3], v[3], n[3]; };
static Frame frame_of(const Inst& in) {
    Frame f;
    f.o[0] = in.x; f.o[1] = in.y; f.o[2] = in.z;
    for (int i = 0; i < 3; ++i) { f.u[i] = in.u[i]; f.v[i] = in.v[i]; f.n[i] = in.n[i]; }
    return f;
}
static void world(const Frame& f, int a, int b, int c, int* p) {
    for (int i = 0; i < 3; ++i) p[i] = f.o[i] + ((a * f.u[i] + b * f.v[i] + c * f.n[i]) >> 14);
}
static void lp(const Cam& cam, const Frame& f, int a, int b, int c, int* sx, int* sy) {
    int p[3], d; world(f, a, b, c, p); cam.at(p[0], p[1], p[2], sx, sy, &d);
}
static void lline(Canvas& cv, const Cam& cam, const Frame& f, int a0, int b0, int c0, int a1, int b1, int c1, uint16_t col, int w, int alpha = 256) {
    int ax, ay, bx, by;
    lp(cam, f, a0, b0, c0, &ax, &ay); lp(cam, f, a1, b1, c1, &bx, &by);
    cv.aline(ax, ay, bx, by, col, w, alpha);
}

// ---- faces: shaded by their turn to the light (from above, the viewer's left and front), kept when they face the viewer
static int toward_viewer(const Cam& cam, const int* d) {   // a direction's depth toward the viewer
    return ((d[0] * cam.c - d[1] * cam.s + d[0] * cam.s + d[1] * cam.c) >> 14) + d[2];
}
static int lit_by(const int* nrm) {                  // Q10, the light (0, 0.64, 0.77) as world
    return (nrm[1] * 655 + nrm[2] * 788) >> 10;
}
// a face from four world points; `out` a direction outside the solid there (its normal is turned to it)
static void face4(Builder& b, const Cam& cam, const int (*p)[3], const int* out, const Mat& m, bool edged = true) {
    int e1[3] = {p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]};
    int e2[3] = {p[3][0] - p[0][0], p[3][1] - p[0][1], p[3][2] - p[0][2]};
    if (e1[0] == 0 && e1[1] == 0 && e1[2] == 0) { for (int i = 0; i < 3; ++i) e1[i] = p[2][i] - p[1][i]; }
    if (e2[0] == 0 && e2[1] == 0 && e2[2] == 0) { for (int i = 0; i < 3; ++i) e2[i] = p[2][i] - p[3][i]; }
    for (int i = 0; i < 3; ++i) { while (e1[i] > 2000 || e1[i] < -2000 || e2[i] > 2000 || e2[i] < -2000) { for (int k = 0; k < 3; ++k) { e1[k] >>= 1; e2[k] >>= 1; } } }
    int nr[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
    int big = 0;
    for (int i = 0; i < 3; ++i) { int a = nr[i] < 0 ? -nr[i] : nr[i]; if (a > big) big = a; }
    while (big > 16000) { for (int i = 0; i < 3; ++i) nr[i] >>= 1; big >>= 1; }
    if (nr[0] * (out[0] >> 4) + nr[1] * (out[1] >> 4) + nr[2] * (out[2] >> 4) < 0) for (int i = 0; i < 3; ++i) nr[i] = -nr[i];
    if (toward_viewer(cam, nr) <= 0) return;           // turned away
    int len = isqrt(nr[0] * nr[0] + nr[1] * nr[1] + nr[2] * nr[2]);
    if (len < 1) return;
    int unit[3] = {nr[0] * 1024 / len, nr[1] * 1024 / len, nr[2] * 1024 / len};
    int light = lit_by(unit);
    uint16_t col = light > 560 ? m.lit : light > 250 ? m.mid : m.dark;
    b.face(p, col, edged ? m.edge : (uint16_t)0xffffu, true);
}
// a face in a frame's a, b, c
static void fface(Builder& b, const Cam& cam, const Frame& f, const int (*q)[3], int oa, int ob, int oc, const Mat& m, bool edged = true) {
    int p[4][3], o[3];
    for (int k = 0; k < 4; ++k) world(f, q[k][0], q[k][1], q[k][2], p[k]);
    for (int i = 0; i < 3; ++i) o[i] = ((oa >> 4) * (f.u[i] >> 4) + (ob >> 4) * (f.v[i] >> 4) + (oc >> 4) * (f.n[i] >> 4)) >> 6;   // Q14-ish, no 64-bit multiply
    face4(b, cam, p, o, m, edged);
}
// a box in a frame: a0..a1 along, b0..b1 across, c0..c1 out
static void fbox(Builder& b, const Cam& cam, const Frame& f, int a0, int a1, int b0, int b1, int c0, int c1, const Mat& m) {
    const int F[6][4][3] = {
        {{a0,b0,c1},{a1,b0,c1},{a1,b1,c1},{a0,b1,c1}}, {{a0,b0,c0},{a0,b1,c0},{a1,b1,c0},{a1,b0,c0}},
        {{a1,b0,c0},{a1,b1,c0},{a1,b1,c1},{a1,b0,c1}}, {{a0,b0,c0},{a0,b0,c1},{a0,b1,c1},{a0,b1,c0}},
        {{a0,b1,c0},{a0,b1,c1},{a1,b1,c1},{a1,b1,c0}}, {{a0,b0,c0},{a1,b0,c0},{a1,b0,c1},{a0,b0,c1}},
    };
    const int O[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    for (int i = 0; i < 6; ++i) fface(b, cam, f, F[i], O[i][0] * 16384, O[i][1] * 16384, O[i][2] * 16384, m);
}

// ---- the instrument
static const uint16_t (*profile(const Inst& in))[2] { return in.sloped ? BASS_P : VIOLIN_P; }
static Mat wood_mat(const Inst& in) { Mat m = {in.wood.lit, in.wood.mid, in.wood.dark, in.wood.edge}; return m; }

// the body: an arched front (two facets a row, rising to a ridge down the middle), ribs, a flat back
static void body(Builder& b, const Cam& cam, const Frame& f, const Inst& in, int th) {
    const uint16_t (*P)[2] = profile(in);
    const Mat m = wood_mat(in);
    const Mat rib = {in.wood.mid, in.wood.dark, mix(0, in.wood.dark, 170), in.wood.edge};
    const int arch = th / 2;
    for (int i = 0; i + 1 < NP; ++i) {
        int a0 = in.bh * P[i][0] >> 8, a1 = in.bh * P[i + 1][0] >> 8;
        int w0 = in.bw * P[i][1] >> 8, w1 = in.bw * P[i + 1][1] >> 8;
        int am = (a0 + a1) >> 1;
        for (int side = -1; side <= 1; side += 2) {
            const int Fr[4][3] = {{a0, 0, arch}, {a0, side * w0, 0}, {a1, side * w1, 0}, {a1, 0, arch}};
            fface(b, cam, f, Fr, 0, side * 6000, 16384, m, false);
            const int Rb[4][3] = {{a0, side * w0, 0}, {a1, side * w1, 0}, {a1, side * w1, -th}, {a0, side * w0, -th}};
            int out_a = (w0 - w1) * 16384 / (a1 - a0 + 1);   // the rib's outward lean along the height
            if (out_a > 16384) out_a = 16384;
            if (out_a < -16384) out_a = -16384;
            fface(b, cam, f, Rb, side * 0 + out_a * 0, side * 16384, 0, rib, false);
            (void)am;
        }
        const int Bk[4][3] = {{a0, -w0, -th}, {a1, -w1, -th}, {a1, w1, -th}, {a0, w0, -th}};
        fface(b, cam, f, Bk, 0, 0, -16384, m, false);
    }
    int wb = in.bw * P[0][1] >> 8, wt = in.bw * P[NP - 1][1] >> 8;
    const int Bo[4][3] = {{0, -wb, 0}, {0, wb, 0}, {0, wb, -th}, {0, -wb, -th}};
    fface(b, cam, f, Bo, -16384, 0, 0, rib, false);
    const int To[4][3] = {{in.bh, -wt, 0}, {in.bh, -wt, -th}, {in.bh, wt, -th}, {in.bh, wt, 0}};
    fface(b, cam, f, To, 16384, 0, 0, rib, false);
}
static int faces_viewer(const Cam& cam, const Frame& f) { int d[3] = {f.n[0], f.n[1], f.n[2]}; return toward_viewer(cam, d) > 0; }
static void rim(Canvas& cv, const Cam& cam, const Frame& f, const Inst& in, int c) {
    const uint16_t (*P)[2] = profile(in);
    for (int side = -1; side <= 1; side += 2)
        for (int i = 0; i + 1 < NP; ++i)
            lline(cv, cam, f, in.bh * P[i][0] >> 8, side * (in.bw * P[i][1] >> 8), c, in.bh * P[i + 1][0] >> 8, side * (in.bw * P[i + 1][1] >> 8), c, in.wood.edge, 18);
}

// an f-hole: a long s with a round eye at each end and a nick at its waist, cut in the front
static void f_hole(Canvas& cv, const Cam& cam, const Frame& f, const Inst& in, int side, int c, uint16_t col) {
    const int A[5] = {61, 56, 49, 42, 37}, B[5] = {27, 35, 37, 35, 41};
    int xs[5], ys[5];
    for (int k = 0; k < 5; ++k) lp(cam, f, in.bh * A[k] / 100, side * in.bw * B[k] / 100, c, &xs[k], &ys[k]);
    for (int k = 0; k + 1 < 5; ++k) cv.aline(xs[k], ys[k], xs[k + 1], ys[k + 1], col, 16);
    int r = in.bw / 150;
    cv.disc((xs[0] + 8) >> 4, (ys[0] + 8) >> 4, r, col, col);
    cv.disc((xs[4] + 8) >> 4, (ys[4] + 8) >> 4, r, col, col);
    lline(cv, cam, f, in.bh * 49 / 100, side * in.bw * 33 / 100, c, in.bh * 49 / 100, side * in.bw * 41 / 100, c, col, 12);
}

// an eight-sided disc (a volute, the scroll's curl) as a short prism across b, at b0..b1
static void volute(Builder& b, const Cam& cam, const Frame& f, int ca, int cc, int r, int b0, int b1, const Mat& m) {
    static const int8_t OA[8] = {10, 7, 0, -7, -10, -7, 0, 7}, OC[8] = {0, 7, 10, 7, 0, -7, -10, -7};
    for (int k = 0; k < 8; ++k) {                                // its rim
        int j = (k + 1) & 7;
        const int Q[4][3] = {{ca + r * OA[k] / 10, b0, cc + r * OC[k] / 10}, {ca + r * OA[j] / 10, b0, cc + r * OC[j] / 10},
                             {ca + r * OA[j] / 10, b1, cc + r * OC[j] / 10}, {ca + r * OA[k] / 10, b1, cc + r * OC[k] / 10}};
        fface(b, cam, f, Q, (OA[k] + OA[j]) * 800, 0, (OC[k] + OC[j]) * 800, m);
    }
    for (int s = 0; s < 2; ++s) {                                // its two faces: the octagon as three quads
        int bb = s ? b1 : b0, ob = s ? 16384 : -16384;
        const int Q1[4][3] = {{ca + r * OA[0] / 10, bb, cc}, {ca + r * OA[1] / 10, bb, cc + r * OC[1] / 10}, {ca + r * OA[3] / 10, bb, cc + r * OC[3] / 10}, {ca + r * OA[4] / 10, bb, cc}};
        const int Q2[4][3] = {{ca + r * OA[4] / 10, bb, cc}, {ca + r * OA[5] / 10, bb, cc + r * OC[5] / 10}, {ca + r * OA[7] / 10, bb, cc + r * OC[7] / 10}, {ca + r * OA[0] / 10, bb, cc}};
        const int Q3[4][3] = {{ca + r * OA[1] / 10, bb, cc + r * OC[1] / 10}, {ca, bb, cc + r}, {ca, bb, cc + r}, {ca + r * OA[3] / 10, bb, cc + r * OC[3] / 10}};
        const int Q4[4][3] = {{ca + r * OA[5] / 10, bb, cc + r * OC[5] / 10}, {ca, bb, cc - r}, {ca, bb, cc - r}, {ca + r * OA[7] / 10, bb, cc + r * OC[7] / 10}};
        fface(b, cam, f, Q1, 0, ob, 0, m, false); fface(b, cam, f, Q2, 0, ob, 0, m, false);
        fface(b, cam, f, Q3, 0, ob, 0, m, false); fface(b, cam, f, Q4, 0, ob, 0, m, false);
    }
}

// a ribbon bow tied round the neck: two loops (each two faces, folded), the knot, two notched tails; `grow` 0..256
static void ribbon_bow(Builder& b, const Cam& cam, const Frame& f, int ka, int kc, int L, int grow, const Tint& t) {
    if (grow <= 0) return;
    L = L * grow >> 8;
    if (L < 4) return;
    const Mat m = {t.ribbon, mix(t.ribbon, t.fold, 128), t.fold, mix(t.fold, t.edge, 110)};   // v12: a softer, thinner-looking edge
    for (int s = -1; s <= 1; s += 2) {
        const int T[4][3] = {{ka, s * L / 8, kc}, {ka - L * 11 / 10, s * L * 6 / 10, kc + L / 6}, {ka - L * 9 / 10, s * L * 4 / 10, kc + L / 5}, {ka - L / 10, 0, kc + L / 8}};
        fface(b, cam, f, T, 0, 0, 16384, m);                                       // a tail
        const int A[4][3] = {{ka, s * L / 8, kc + L / 10}, {ka + L * 6 / 10, s * L * 7 / 10, kc + L / 4}, {ka + L / 5, s * L * 13 / 10, kc + L / 3}, {ka - L / 4, s * L, kc + L / 5}};
        fface(b, cam, f, A, L * 4, 0, 16384, m);                                   // the loop's upper face
        const int B2[4][3] = {{ka, s * L / 8, kc + L / 10}, {ka - L / 4, s * L, kc + L / 5}, {ka - L * 5 / 10, s * L * 8 / 10, kc + L / 10}, {ka - L / 5, s * L / 4, kc + L / 12}};
        fface(b, cam, f, B2, -L * 4, 0, 16384, m);                                 // its lower, folded under
    }
    fbox(b, cam, f, ka - L / 5, ka + L / 5, -L / 5, L / 5, kc, kc + L / 3, m); // the knot
}

static void instrument(Canvas& cv, const Cam& cam, const Inst& in, const State& st, int lit) {
    const Pal& P = pal();
    Builder& b = builder;
    const Frame f = frame_of(in);
    const int th = in.bw * 55 / 100 + 6;                         // v10: deeper bodies
    const int top = in.bh, n = in.neck, scroll = in.bh + in.neck;
    const int fb = th / 3 + 6;                                   // the fingerboard's height over the front
    if (in.pin) lline(cv, cam, f, -in.pin, 0, -th / 2, 0, 0, -th / 2, P.grey, 22);   // the end pin, down to the floor
    body(b, cam, f, in, th);
    const Mat wm = wood_mat(in);
    // the neck, the pegbox, the scroll
    const int p0 = top + n * 60 / 100, p1 = top + n * 84 / 100, hw = in.bw * 24 / 100 + 5;   // v12: heads clearly wider than the neck
    fbox(b, cam, f, top - in.bh / 12, p0, -in.bw * 9 / 100, in.bw * 9 / 100, -th / 2, th / 6, wm);
    fbox(b, cam, f, p0, p1, -hw, hw, -th / 2, th / 5, wm);
    volute(b, cam, f, p1 + (scroll - p1) / 2, -th / 6, (scroll - p1) * 7 / 10 + 4, -hw * 9 / 10, hw * 9 / 10, wm);
    b.sorted(cv);
    bool front = faces_viewer(cam, f);
    if (front) {
        rim(cv, cam, f, in, 0);
        f_hole(cv, cam, f, in, -1, th / 6, P.offbrown); f_hole(cv, cam, f, in, 1, th / 6, P.offbrown);
    }
    // the fingerboard, the tailpiece, the bridge, the pegs: on the front
    fbox(b, cam, f, in.bh * 52 / 100, scroll - n * 38 / 100, -in.bw * 11 / 100, in.bw * 11 / 100, th / 4, fb, EBONY);
    fbox(b, cam, f, in.bh / 14, in.bh * 34 / 100, -in.bw * 12 / 100, in.bw * 12 / 100, th / 4, th / 4 + 6, EBONY);
    fbox(b, cam, f, in.bh * 41 / 100, in.bh * 43 / 100, -in.bw * 22 / 100, in.bw * 22 / 100, th / 4, fb + 4, MAPLE);
    b.sorted(cv);
    for (int k = 0; k < 4; ++k) {                                // the pegs, two a side
        int a = p0 + (p1 - p0) * (2 * k + 1) / 8, sd = (k & 1) ? 1 : -1;
        lline(cv, cam, f, a, 0, 0, a, sd * hw * 2, 0, P.offbrown, 30);
    }
    if (front)
        for (int s = 0; s < 4; ++s)
            lline(cv, cam, f, in.bh * 33 / 100, (s * 2 - 3) * in.bw * 4 / 100, fb + 5, scroll - n * 38 / 100, (s * 2 - 3) * in.bw * 2 / 100, fb + 2,
                  lit ? P.lightgrey : P.grey, 8, lit ? 240 : 160);
    // the ribbon bow, round the neck just above the body
    int grow = (st.life - in.bow_at) * 3;
    if (grow > 256) grow = 256;
    if (grow > 0) {
        ribbon_bow(b, cam, f, top + n / 7, fb + 4, in.bw * 7 / 10, grow, tint(st));
        b.sorted(cv);
    }
}

// the instrument's bow: at rest leant across it (or lying beside it); while it sounds, floating across the strings
// between the bridge and the fingerboard, drawn back and forth
static void string_bow(Canvas& cv, const Cam& cam, const Inst& in, const State& st, int lit) {
    const Frame fr = frame_of(in);
    const Pal& P = pal();
    int a0, b0, c0, a1, b1, c1;                                  // the frog's end and the tip's, on the instrument
    int len = in.bh + in.neck / 2;
    if (lit) {                                                   // across the strings, a little tilted, sliding
        int slide = (sin14(st.time * 1200u + (uint32_t)in.bh * 977u) * (len / 3)) >> 14;
        int a = in.bh * 47 / 100, c = 26;
        a0 = a - len / 9; b0 = -len / 2 - slide; c0 = c; a1 = a + len / 9; b1 = len / 2 - slide; c1 = c;
        if (in.bow_side < 0) { b0 = -b0; b1 = -b1; }
    } else if (in.bow_side) {                                    // leant: the frog low by the body, the tip past the shoulder
        int sd = in.bow_side;
        a0 = in.bh / 20; b0 = -sd * in.bw * 13 / 10; c0 = in.bw / 2;
        a1 = in.bh * 118 / 100; b1 = sd * in.bw * 11 / 10; c1 = in.bw / 2;
    } else {                                                     // lying beside it on the floor
        a0 = -in.bh / 10; b0 = -in.bw * 14 / 10; c0 = -in.bw / 3 - 2;
        a1 = in.bh + in.neck * 7 / 10; b1 = -in.bw * 11 / 10; c1 = -in.bw / 3 - 2;
    }
    const int hair = in.bw / 9 + 3;                              // the hair runs under the stick
    lline(cv, cam, fr, a0, b0, c0 - hair, a1, b1, c1 - hair / 3, P.lightgrey, 8, 200);     // v12: thinner
    lline(cv, cam, fr, a0, b0, c0, a1, b1, c1, rgb(0x6a, 0x34, 0x12), 16);
    int fx, fy, gx, gy;                                          // the frog, black, and the tip
    lp(cam, fr, a0 + (a1 - a0) / 14, b0 + (b1 - b0) / 14, c0 - hair / 2, &fx, &fy);
    lp(cam, fr, a0, b0, c0, &gx, &gy);
    cv.aline(gx, gy, fx, fy, rgb(0x14, 0x12, 0x12), 48);
    lp(cam, fr, a1, b1, c1, &gx, &gy);
    cv.disc((gx + 8) >> 4, (gy + 8) >> 4, 1, P.lightgrey, P.grey);
}

// a music stand: three legs, a post (lines); the desk a thin box leant back from its reader; the sheet on it
static void music_stand(Canvas& cv, const Cam& cam, const int* s) {
    const Pal& P = pal();
    Builder& b = builder;
    const int x = s[0], y = s[1], fx = s[2], fy = s[3];
    for (int k = 0; k < 3; ++k) {
        uint32_t a = 5461u + (uint32_t)k * 21845u;
        line3(cv, cam, x, y, 70, x + ((80 * cos14(a)) >> 14), y + ((80 * sin14(a)) >> 14), 0, P.darkgrey1, 22);
    }
    line3(cv, cam, x, y, 0, x, y, 400, P.darkgrey1, 26);
    // the desk's frame: a along its height (leant 30 degrees back), b across, c toward the reader
    Frame f;
    f.o[0] = x; f.o[1] = y; f.o[2] = 450;
    f.u[0] = (-fx * 8192) >> 14; f.u[1] = (-fy * 8192) >> 14; f.u[2] = 14189;
    f.v[0] = -fy; f.v[1] = fx; f.v[2] = 0;
    f.n[0] = (fx * 14189) >> 14; f.n[1] = (fy * 14189) >> 14; f.n[2] = 8192;
    fbox(b, cam, f, -64, 64, -92, 92, -6, 6, METAL);
    fbox(b, cam, f, -74, -60, -92, 92, 6, 22, METAL);            // the ledge
    b.sorted(cv);
    if (faces_viewer(cam, f)) {
        const int Q[4][3] = {{-54, -76, 8}, {-54, 76, 8}, {54, 76, 8}, {54, -76, 8}};
        fface(b, cam, f, Q, 0, 0, 16384, PAPER, false);
        b.sorted(cv);
        for (int l = 0; l < 4; ++l) lline(cv, cam, f, 36 - l * 22, -62, 9, 36 - l * 22, 62, 9, P.grey, 10, 220);   // the staves
    }
}

// ---- the floor: none drawn (the scene floats on black); ribbons and confetti gather on it with life
enum { FX0 = -680, FX1 = 680, FY0 = -480, FY1 = 920 };   // v11: wider
enum { CX0 = -700, CX1 = 1000, CY0 = -640, CY1 = 960 };   // v13: the confetti's, reaching further right
static void floor(Canvas& cv, const Cam& cam, const State& st) {
    const Pal& P = pal();
    // the floating instruments' shadows
    for (int i = 0; i < 4; ++i) {
        const Inst& in = SECTION[i];
        if (in.pin) continue;
        int mx = in.x + (((in.bh + in.neck) / 2 * in.u[0]) >> 14), my = in.y + (((in.bh + in.neck) / 2 * in.u[1]) >> 14);
        disc3(cv, cam, mx, my, 0, in.bw * 7 / 10, rgb(0x10, 0x0a, 0x08), 12);
    }
    // ribbons lying curled on the floor, more with life
    const uint16_t rc[4] = {P.red, P.lightblue, P.lightbrown, P.lightgrey};
    for (int r = 0; r < 7; ++r) {
        int fade = (st.life - 420 - r * 50) * 4;              // v11: each one fades in, and out again as life goes
        if (fade <= 0) break;
        if (fade > 256) fade = 256;
        uint32_t h = hash(0xb0b5u + (uint32_t)r * 77u);
        int x = FX0 + 120 + (int)(h % (uint32_t)(FX1 - FX0 - 240)), yy = FY0 + 120 + (int)((h >> 12) % (uint32_t)(FY1 - FY0 - 240));
        int len = 140 + (int)((h >> 20) & 63u);
        uint32_t a = h >> 8;
        int px = x, py = yy;
        for (int k = 1; k <= 10; ++k) {
            int t = len * k / 10;
            int nx = x + ((t * cos14(a)) >> 14) + ((22 * sin14((uint32_t)k * 14000u + h)) >> 14);
            int ny = yy + ((t * sin14(a)) >> 14) + ((22 * cos14((uint32_t)k * 14000u + h)) >> 14);
            line3(cv, cam, px, py, 1, nx, ny, 1, mix(0, rc[r & 3], fade), 30);
            px = nx; py = ny;
        }
    }
    // confetti settled on the floor: paper squares and strips lying flat, each turned its own way
    for (int i = 0; i < 130; ++i) {                          // v13: more, wider, more to the right, dimmer
        int fade = (st.life - 380 - i * 4) * 6;               // v11: each piece fades in, and out again as life goes
        if (fade <= 0) break;
        if (fade > 100) fade = 100;                           // v14: at most 40% bright, under the instruments
        uint32_t h = hash(0xc0ffu + (uint32_t)i * 131u);
        int x = CX0 + (int)(h % (uint32_t)(CX1 - CX0)), yy = CY0 + (int)((h >> 11) % (uint32_t)(CY1 - CY0));
        uint32_t turn = h << 5;
        int len = (h & 0x100000u) ? 26 : 13, wid = 11;           // a strip or a square
        int ca = cos14(turn), sa = sin14(turn), xs[4], ys[4], d;
        const int C[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int k = 0; k < 4; ++k) {
            int lx = C[k][0] * len, ly = C[k][1] * wid;
            cam.at(x + ((lx * ca - ly * sa) >> 14), yy + ((lx * sa + ly * ca) >> 14), 1, &xs[k], &ys[k], &d);
        }
        cv.poly(xs, ys, 4, mix(0, rc[(h >> 24) & 3u], fade), 0xffffu);
    }
}

// confetti falling over the scene, from life 750: paper pieces turning as they fall (wide, then edge on)
static void falling(Canvas& cv, const State& st) {
    const Pal& P = pal();
    const uint16_t rc[4] = {P.red, P.lightblue, P.lightbrown, P.lightgrey};
    int n = (st.life - 750) / 8;
    if (n > 30) n = 30;
    for (int i = 0; i < n; ++i) {
        uint32_t h = hash(0xfa11u + (uint32_t)i * 977u);
        int speed = 1 + (int)((h >> 9) & 1u);
        int x = 20 + (int)(h % 300u) + ((10 * sin14(st.time * 700u + h)) >> 14);
        int yy = (int)((st.time * (uint32_t)speed + (h >> 13)) % 250u) - 15;
        uint32_t spin = st.time * (500u + ((h >> 3) & 511u)) + h;
        int len = (h & 0x40u) ? 5 : 3, wid = 3;                   // in pixels: a strip or a square
        int ca = cos14(spin), sa = sin14(spin), flat = cos14(spin * 2u + h);   // turning, and its flutter edge on
        int xs[4], ys[4];
        const int C[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int k = 0; k < 4; ++k) {
            int lx = C[k][0] * len * 16, ly = (C[k][1] * wid * 16 * (flat < 0 ? -flat : flat)) >> 14;
            if (ly == 0) ly = C[k][1] * 6;
            xs[k] = (x << 4) + ((lx * ca - ly * sa) >> 14); ys[k] = (yy << 4) + ((lx * sa + ly * ca) >> 14);
        }
        uint16_t c = rc[(h >> 24) & 3u];
        cv.poly(xs, ys, 4, (flat < 0 ? -flat : flat) > 8000 ? c : mix(0, c, 150), 0xffffu);
    }
}

enum { WITH_STANDS = 0 };   // v10: the music stands left out, for a look without them
static void render(uint16_t* px, const State& st) {
    const Pal& P = pal();
    Canvas cv = {px};
    for (int i = 0; i < W * H; ++i) px[i] = P.black;
    Cam cam; cam.c = cos14(st.angle); cam.s = sin14(st.angle);
    builder.cam = &cam; builder.n = 0;
    cam.cx = 0; cam.cy = 0; cam.ox = 165; cam.oy = 150; cam.k = 860;
    floor(cv, cam, st);
    int order[8], depth[8];
    for (int i = 0; i < 8; ++i) {
        int sx, sy;
        if (i < 4) cam.at(SECTION[i].x, SECTION[i].y, 0, &sx, &sy, &depth[i]); else cam.at(STANDS[i - 4][0], STANDS[i - 4][1], 0, &sx, &sy, &depth[i]);
        order[i] = i;
    }
    for (int i = 1; i < 8; ++i) { int t = order[i], j = i; while (j > 0 && depth[order[j - 1]] > depth[t]) { order[j] = order[j - 1]; --j; } order[j] = t; }
    for (int k = 0; k < 8; ++k) {
        int i = order[k];
        if (i >= 4) { if (WITH_STANDS) music_stand(cv, cam, STANDS[i - 4]); continue; }
        int lit = (int)((st.lit >> i) & 1u);
        if (!lit && !SECTION[i].bow_side) string_bow(cv, cam, SECTION[i], st, 0);   // lying beside: under it
        instrument(cv, cam, SECTION[i], st, lit);
        if (lit || SECTION[i].bow_side) string_bow(cv, cam, SECTION[i], st, lit);
    }
    falling(cv, st);
}
