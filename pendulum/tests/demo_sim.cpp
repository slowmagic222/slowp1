// Simulates pendulum's page on the PC over a scripted performance (no text: the words live in the picture's
// placeholders, which this canvas does not draw), writing PPM frames for pendulum/demo.gif.
// Run through tests/demo.sh, which builds this, keeps the busy frames and writes the GIF.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "../pendulum.cpp"

extern "C" const Op1fCard op1f_card_pendulum = {};

static long checks = 0, fails = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { if (++fails < 30) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

// ---- the base's stand-ins
static int sounding[128];
static int ons = 0, offs = 0, refusals = 0, bad_calls = 0;
static int on_at[4096], on_note[4096], on_vel[4096];
static unsigned cur_tick = 0;
static int in_call = 0;                          // note helpers allowed only inside tick/key/run/input
extern "C" unsigned op1f_mask() { return 1; }
extern "C" void op1f_unmask(unsigned) {}
extern "C" int op1f_note_on(const Op1fCard*, unsigned note, unsigned velocity, unsigned) {
    if (!in_call) { ++bad_calls; return 0; }
    if (note > 127 || velocity == 0 || velocity > 127 || sounding[note]) { ++refusals; return 0; }
    sounding[note] = 1;
    if (ons < 4096) { on_at[ons] = (int)cur_tick; on_note[ons] = (int)note; on_vel[ons] = (int)velocity; }
    ++ons;
    return 1;
}
extern "C" int op1f_note_off(const Op1fCard*, unsigned note, unsigned) {
    if (!in_call) { ++bad_calls; return 0; }
    if (note > 127 || !sounding[note]) { ++refusals; return 0; }
    sounding[note] = 0; ++offs; return 1;
}
static unsigned char saved[64]; static int saved_n = -1;
extern "C" int op1f_read(const Op1fCard*, unsigned, const char*, void* b, unsigned room) {
    if (saved_n < 0) return OP1F_NO_FILE;
    if ((unsigned)saved_n > room) return OP1F_TOO_LARGE; memcpy(b, saved, (size_t)saved_n); return saved_n;
}
extern "C" int op1f_write(const Op1fCard*, unsigned, const char*, const void* d, unsigned n) { memcpy(saved, d, n); saved_n = (int)n; return (int)n; }
extern "C" void op1f_log(int, const char*, int, const char* format, ...) { va_list a; va_start(a, format); printf("  log: "); vprintf(format, a); printf("\n"); va_end(a); }
extern "C" int op1f_page_stand_in(const Op1fCard*, unsigned, void*) { return 1; }
#include <string>
#include <map>
static std::map<std::string, std::string> texts;
static int fake_doc, fake_node;
static int use_picture = 1;
extern "C" void* op1f_picture(const Op1fCard*, const char* name) { return use_picture && !strcmp(name, "pendulum.svg") ? &fake_doc : 0; }
extern "C" int op1f_text_set(void*, const char* part, const char* text, int, unsigned) { texts[part] = text; return 1; }
static std::map<std::string, int> visible;
extern "C" void* op1f_text_node(void*, const char* part) { return texts.count(part) ? (void*)&texts.find(part)->first : 0; }
extern "C" void op1f_show(void* node, int on) { visible[*(const std::string*)node] = on; }
static void save_texts(const char* path) {
    FILE* f = fopen(path, "w");
    for (auto& kv : texts) { auto v = visible.find(kv.first); fprintf(f, "%s|%s|%d\n", kv.first.c_str(), kv.second.c_str(), v == visible.end() ? 1 : v->second); }
    fclose(f);
}

// ---- a canvas
enum { CW = 480, CH = 224 };
static unsigned short canvas[CW * CH];
static void put(int x, int y, unsigned c) { if (x >= 0 && y >= 0 && x < CW && y < CH) canvas[y * CW + x] = (unsigned short)c; }
extern "C" void op1f_paint_box(void*, int x, int y, int w, int h, unsigned c) { for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) put(x + i, y + j, c); }
extern "C" void op1f_paint_line(void*, int x0, int y0, int x1, int y1, int w, unsigned c) {
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    for (int k = 0; k <= n; ++k) {
        int x = n ? x0 + (x1 - x0) * k / n : x0, y = n ? y0 + (y1 - y0) * k / n : y0;
        op1f_paint_box(0, x - (w - 1) / 2, y - (w - 1) / 2, w, w, c);
    }
}
extern "C" void op1f_paint_mask(void*, int x, int y, int w, int h, const unsigned char* cover, int stride, unsigned c) {
    int cr = (c >> 11) & 31, cg = (c >> 5) & 63, cb = c & 31;
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) {
        int px = x + i, py = y + j; if (px < 0 || py < 0 || px >= CW || py >= CH) continue;
        int wgt = cover[j * stride + i]; wgt += wgt >> 7;
        unsigned short o = canvas[py * CW + px];
        int r = (o >> 11) & 31, g = (o >> 5) & 63, b = o & 31;
        r += ((cr - r) * wgt) >> 8; g += ((cg - g) * wgt) >> 8; b += ((cb - b) * wgt) >> 8;
        canvas[py * CW + px] = (unsigned short)((r << 11) | (g << 5) | b);
    }
}
// A 5x7 font for what the page writes (digits and the labels' capitals).
static const char* glyph(char ch) {
    switch (ch) {
    case '0': return "01110100011001110101110011000101110"; case '1': return "00100011000010000100001000010001110";
    case '2': return "01110100010000100010001000100011111"; case '3': return "11111000100010000010000011000101110";
    case '4': return "00010001100101010010111110001000010"; case '5': return "11111100001111000001000011000101110";
    case '6': return "00110010001000011110100011000101110"; case '7': return "11111000010001000100010000100001000";
    case '8': return "01110100011000101110100011000101110"; case '9': return "01110100011000101111000010001001100";
    case 'A': return "01110100011000111111100011000110001"; case 'D': return "11100100101000110001100011001011100";
    case 'E': return "11111100001000011110100001000011111"; case 'F': return "11111100001000011110100001000010000";
    case 'G': return "01110100011000010111100011000101111"; case 'I': return "01110001000010000100001000010001110";
    case 'N': return "10001100011100110101100111000110001"; case 'P': return "11110100011000111110100001000010000";
    case 'Q': return "01110100011000110001101011001001101"; case 'R': return "11110100011000111110101001001010001";
    case 'S': return "01111100001000001110000010000111110"; case 'T': return "11111001000010000100001000010000100";
    case 'U': return "10001100011000110001100011000101110"; case 'V': return "10001100011000110001100010101000100";
    case 'W': return "10001100011000110101101011010101010"; case 'Y': return "10001100011000101010001000010000100";
    case 'Z': return "11111000010001000100010001000011111";
    case 'B': return "11110100011000111110100011000111110"; case 'C': return "01110100011000010000100001000101110";
    case 'H': return "10001100011000111111100011000110001"; case 'J': return "00111000100001000010000101001001100";
    case 'K': return "10001100101010011000101001001010001"; case 'L': return "10000100001000010000100001000011111";
    case 'M': return "10001110111010110101100011000110001"; case 'O': return "01110100011000110001100011000101110";
    case 'X': return "10001100010101000100010101000110001"; case '/': return "00001000010001000100010001000010000";
    case ':': return "00000011000110000000011000110000000"; case '>': return "10000010000010000010001000100010000";
    default: return "00000000000000000000000000000000000";
    }
}
extern "C" int op1f_paint_text(void*, int x, int y, const char* t, unsigned c, int s) {
    int n = 0;
    for (; t[n]; ++n) { const char* g = glyph(t[n]); for (int r = 0; r < 7; ++r) for (int q = 0; q < 5; ++q) if (g[r * 5 + q] == '1') op1f_paint_box(0, x + (n * 6 + q) * s, y + r * s, s, s, c); }
    return (6 * n - 1) * s;
}

// ---- driving
static unsigned count = 0, pulse = 0, us = 5208;   // 120 BPM
static void tick(unsigned host = 0) {
    Op1fTick t = {}; t.bytes = 32; t.host = host; t.pulse = pulse; t.count = count; t.tempo = 12000; t.us = us;
    cur_tick = count; in_call = 1; pendulum_tick(&t); in_call = 0;
    ++count; if (++pulse >= 48) pulse = 0;
}
static void ticks(int n) { while (n-- > 0) tick(); }
static void key(int k, int vel = 100, unsigned what = OP1F_DOWN, unsigned host = 0) {
    Op1fKey e = {}; e.bytes = 32; e.what = what; e.host = host; e.key = k; e.note = (unsigned)(53 + k); e.velocity = what == OP1F_DOWN ? (unsigned)vel : 0u; e.from = OP1F_FROM_KEYS;
    in_call = 1; int took = pendulum_key(&e); in_call = 0;
    CHECK(took == 1, "key %d not taken", k);
}
static void turn(unsigned knob, int steps) {
    Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = knob; e.turn = steps * 1024; e.mine = 1; e.mode = 7;
    in_call = 1; pendulum_input(&e); in_call = 0;
}
static void press(unsigned k, int shift) {          // a knob's click: down, up
    Op1fInput e = {}; e.bytes = 40; e.what = OP1F_DOWN; e.key = k; e.shift = (unsigned)shift; e.mine = 1; e.mode = 7; e.thread = 1;
    in_call = 1; CHECK(pendulum_input(&e) == 1, "click %u not taken", k); e.what = OP1F_UP; pendulum_input(&e); in_call = 0;
}
static unsigned ms = 0;
static void frame(int on_page = 1) {
    Op1fFrame f = {}; f.bytes = 40; f.ms = ms; ms += 16; f.page = on_page ? &op1f_card_pendulum : 0; f.canvas = on_page ? canvas : 0;
    memset(canvas, 0, sizeof canvas);
    pendulum_frame(&f);
}
static void stop(unsigned host = 0) { in_call = 1; pendulum_run(host, OP1F_RUN_STOP); in_call = 0; for (int i = 0; i < 128; ++i) sounding[i] = 0; }
static void reset() { stop(); tick(); ons = offs = 0; }
static void set_knob(unsigned k, int v) {           // from wherever it is to v
    frame(); int now = (int)state.v[k]; turn(k, v - now); frame();
}
static double crossing_s(int i) { return (on_at[i] - on_at[i - 1]) * us / 1e6; }

static void save_ppm(const char* path) {
    FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", CW, CH);
    for (int i = 0; i < CW * CH; ++i) { unsigned c = canvas[i]; unsigned char rgb[3] = {(unsigned char)(((c >> 11) & 31) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63), (unsigned char)((c & 31) * 255 / 31)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}

// ---- the performance: one paint frame each 1/60 s, the clock at 120 BPM (192 pulses a second), every 4th frame kept.
static const char* out_dir; static int out_n = 0, paint_n = 0, ons_seen = 0; static FILE* activity;
static double tick_debt = 0;
static void step(int n = 1) {                       // n paint frames of time
    while (n-- > 0) {
        tick_debt += 192.0 / 60.0; while (tick_debt >= 1) { tick(); tick_debt -= 1; }
        frame();
        if (paint_n++ % 4 == 0) {
            char p[600]; snprintf(p, sizeof p, "%s/f%04d.ppm", out_dir, out_n++); save_ppm(p);
            int lit = 0; for (int i = 0; i < CW * CH; ++i) if (canvas[i]) ++lit;
            fprintf(activity, "%d %d %d\n", ons - ons_seen, (int)state.blooms, lit); ons_seen = ons;
        }
    }
}
static void secs(double s) { step((int)(s * 60 + 0.5)); }
static void knob_to(unsigned k, int v, double over) {   // a knob turned smoothly to v
    int from = (int)state.v[k == 3 ? 4 : k], n = (int)(over * 60); if (n < 1) n = 1;
    int done = 0;
    for (int i = 1; i <= n; ++i) { int want = from + (v - from) * i / n; if (want != from + done) { turn(k, want - from - done); done = want - from; } step(); }
}
static void tap(int k, int vel) { key(k, vel); }

int main(int argc, char** argv) {
    out_dir = argc > 1 ? argv[1] : "."; char p[600]; snprintf(p, sizeof p, "%s/activity.txt", out_dir); activity = fopen(p, "w");
    srand(11);
    frame(); state.texts_hidden = 2;
    set_knob(1, 62);                                 // gravity a little quick
    // 1. a few keys, one by one: a slow melody that starts to interlock
    int mel[] = { 12, 16, 19, 14, 21, 9 };
    for (int i = 0; i < 6; ++i) { tap(mel[i], 70 + 9 * i); secs(0.3); }
    secs(0.5);
    // 2. drift up: the swings fan out into a wave
    knob_to(2, 45, 1.0); secs(0.8);
    // 3. pentatonic launched at once: everything lands together (bloom), then spreads with the drift
    turn(3, 9 - (int)state.v[4]); press(57, 1); secs(2.6);
    // 4. scale to dorian, launched again, gravity quicker
    turn(3, 3 - (int)state.v[4]); knob_to(1, 72, 0.6); press(57, 1); secs(2.2);
    // 5. quantize on, grid steps: the notes snap together
    knob_to(0, 100, 0.8); secs(1.2); press(54, 0); secs(1.2); press(54, 0); secs(1.0);
    knob_to(0, 0, 0.5);
    // 6. clear, then loud chords by hand, gravity slow
    press(57, 0); knob_to(1, 55, 0.4); knob_to(2, 20, 0.3);
    int ch[4][3] = { {0, 7, 16}, {5, 12, 21}, {3, 10, 19}, {8, 15, 23} };
    for (int c = 0; c < 4; ++c) { for (int j = 0; j < 3; ++j) tap(ch[c][j], 120); secs(0.6); }
    secs(0.6);
    // 7. hirajoshi launch with high drift and quick gravity: a busy finish
    turn(3, 13 - (int)state.v[4]); knob_to(2, 80, 0.4); knob_to(1, 80, 0.4); press(57, 1); secs(3.0);
    // 8. whole-tone launch slows into a last bloom, then a clear
    turn(3, 12 - (int)state.v[4]); knob_to(1, 60, 0.3); knob_to(2, 10, 0.3); press(57, 1); secs(2.5);
    press(57, 0); secs(0.3);
    fclose(activity);
    fprintf(stderr, "%d frames, %d notes, %u blooms, refused %d\n", out_n, ons, state.blooms, refusals);
    return 0;
}
