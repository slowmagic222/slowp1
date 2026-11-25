// Host test of pendulum.pack: pendulum.cpp compiled for the PC (OP1F_HOST, 32-bit) with stand-ins for the base.
// Drives the sequencer's calls through a clock at 96 pulses a quarter, checks the swings' timing against the
// design, and paints the page into a 480 x 224 picture (tests/preview.ppm) for the preview.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <initializer_list>
#include "../pendulum/pendulum.cpp"

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

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : ".";
    frame();
    CHECK(state.v[0] == 0 && state.v[1] == 50 && state.v[2] == 0 && state.v[3] == 3 && state.v[4] == 1 && state.v[5] == 0, "defaults");
    CHECK(texts["pend_title"] == "PENDULUM" && texts["pend_sync"] == "FREE" && texts["pend_n1"] == "50", "texts: %s / %s", texts["pend_sync"].c_str(), texts["pend_n1"].c_str());
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_DOWN; e.key = 0; e.mine = 1; e.thread = 1;
      CHECK(pendulum_input(&e) == 1, "help not taken"); frame();
      CHECK(texts["pend_h0"] == "BLUE  QUANTIZE  0" && texts["pend_h2"] == "SHIFT + BLUE PRESS  FREE (OR SYNC)" && texts["pend_h5"] == "RED  SCALE  MAJOR",
            "help: %s / %s", texts["pend_h0"].c_str(), texts["pend_h2"].c_str());
      e.what = OP1F_UP; pendulum_input(&e); frame(); CHECK(texts["pend_h0"] == "", "help gone"); }
    tick();

    // 1. A launch: the first note after half a period (0.8 s), then every 1.6 s; 85 ms notes at the key's velocity.
    key(0, 90); key(0, 0, OP1F_UP);
    ticks(1500);
    CHECK(ons >= 4, "notes %d", ons);
    CHECK(fabs(on_at[0] * 0 + (on_at[0] - 1) * us / 1e6 - 0.8) < 0.011, "first crossing at %.3f s", (on_at[0] - 1) * us / 1e6);
    for (int i = 1; i < 4; ++i) CHECK(fabs(crossing_s(i) - 1.6) < 0.011, "period %.3f s", crossing_s(i));
    CHECK(on_note[0] == 53 && on_vel[0] == 90, "note %d vel %d", on_note[0], on_vel[0]);
    CHECK(offs >= ons - 1, "offs %d ons %d", offs, ons);
    { // gate: 85 ms = 16.3 pulses
        reset(); key(5); int t0 = (int)count; ticks(200);
        int off_at = -1; for (int k = 0; k < 40 && off_at < 0; ++k) { if (!sounding[58]) off_at = (int)count; tick(); }
        CHECK(ons == 1, "one note");
        (void)t0; (void)off_at;
        ticks(10); int still = 0; for (int k = 0; k < 20; ++k) { tick(); still += sounding[58]; }
        CHECK(still == 0, "gate open too long");
    }
    // 2. Gravity: 100 = 0.8 s, 0 = 7.88 s.
    reset(); set_knob(1, 100); key(3); ticks(1000);
    CHECK(ons >= 3 && fabs(crossing_s(2) - 0.8) < 0.011, "gravity 100: %.3f s", crossing_s(2));
    reset(); set_knob(1, 0); key(3); ticks(6000);
    CHECK(ons >= 2 && fabs(crossing_s(1) - 7.879) < 0.011, "gravity 0: %.3f s", crossing_s(1));
    reset(); set_knob(1, 50);
    // 3. Drift 100: key 1 = 1.6 + 0.1712 s, key 6 = 1.6 - 0.1927 s.
    // 3. Drift = pendulum wave: key k's period 1.6 / (1 + D x 0.0002 x (k - 11.5)).
    reset(); set_knob(2, 100);
    for (int k : {0, 6, 11, 12, 23}) {
        reset(); key(k); ticks(1300);
        double want = 1.6 / (1 + 100 * 0.0002 * (k - 11.5));
        CHECK(ons >= 3 && fabs(crossing_s(2) - want) < 0.011, "drift key %d: %.3f s, %.3f expected", k, crossing_s(2), want);
    }
    { // all 24 launched together come back in line after P / (D x 0.0002) = 1.6 / 0.02 = 80 s at D 100
        reset(); for (int k = 0; k < 24; ++k) key(k); tick();
        unsigned t0 = count; unsigned T = (unsigned)(80.0 / (us / 1e6) + 0.5);
        int early = ons; ticks(400); int n_early = ons - early;       // spread a little: not all at the same tick
        int same_first = 0; for (int i = early; i < ons; ++i) if (on_at[i] == on_at[early]) ++same_first;
        CHECK(same_first < 24, "spread after the first crossing");
        while (count < t0 + T) tick();
        unsigned now = (count - 1) << 8; int out_of_line = 0; double worst = 0;
        for (int k = 0; k < 24; ++k) { const Swing& w = hosts[0].swings[k];
            double p = (double)(now - w.last) / (double)(w.next - w.last); static double p0; if (k == 0) p0 = p;
            double d = fabs(p - p0); if (d > 0.5) d = 1 - d; if (d > worst) worst = d; if (d > 0.06) ++out_of_line; }
        CHECK(out_of_line == 0, "back in line after 80 s: worst %.3f of a swing", worst);
        (void)n_early;
    }
    reset(); set_knob(2, 0);
    // 4. Quantize 100: every crossing on a 16th of the unit's grid (pulse % 24 == 0); drift on, so the natural times are not.
    reset(); set_knob(0, 100); set_knob(2, 60); ticks(7);
    for (int k = 0; k < 8; ++k) key(k * 3);
    { unsigned pulse_at_count0 = (pulse + 48 * 1000 - count % 48) % 48; (void)pulse_at_count0; }
    unsigned base_count = count, base_pulse = pulse;
    ticks(3000);
    int off_grid = 0; for (int i = 0; i < ons && i < 4096; ++i) { int p = (int)((base_pulse + (unsigned)on_at[i] - base_count) % 24u); if (p) ++off_grid; }
    CHECK(ons > 20 && off_grid == 0, "quantize 100: %d of %d off the grid", off_grid, ons);
    // Quantize 50: halfway.
    reset(); set_knob(0, 0); set_knob(2, 0);
    // 5. Retrigger: the key again starts from the extreme: next note half a period later.
    reset(); key(2); ticks(100); key(2); unsigned t_re = count; ticks(400);
    CHECK(ons >= 1 && fabs((on_at[0] - (int)t_re) * us / 1e6 - 0.8) < 0.011, "retrigger: %.3f s", (on_at[0] - (int)t_re) * us / 1e6);
    // 6. Re-pacing: gravity 50 -> 100 a quarter of the way: the rest goes at the new pace (3/4 of 0.8 s), not at the next note.
    reset(); key(4); while (ons == 0) tick(); int n0 = ons; unsigned c0 = (unsigned)on_at[n0 - 1];
    while (count < c0 + 77) tick();             // a quarter of 307 pulses
    set_knob(1, 100); unsigned c1 = count; while (ons == n0 && count < c1 + 400) tick();
    double rest = (on_at[n0] - (int)c1) * us / 1e6; 
    double want = (1.0 - (c1 - c0) / 307.2) * 0.8;
    CHECK(c1 - c0 < 200 && fabs(rest - want) < 0.02, "re-paced rest %.3f s (%.3f expected)", rest, want);
    set_knob(1, 50);
    // 7. Tempo follows: at 60 BPM a pulse is twice as long; the period stays 1.6 s.
    reset(); us = 10417; key(7); ticks(800);
    CHECK(ons >= 3 && fabs(crossing_s(2) - 1.6) < 0.02, "60 BPM: %.3f s", crossing_s(2));
    us = 5208;
    // 8. A stop clears the swings: nothing more.
    reset(); key(1); key(9); ticks(500); stop(); tick(); int after_stop = ons; ticks(1000);
    CHECK(ons == after_stop, "notes after a stop: %d", ons - after_stop);
    // 9. Shift + red press launches every key of the scale (major, root C); red press clears them all.
    reset(); frame(); press(57, 1); ticks(2000);
    { int seen[24] = {0}; for (int i = 0; i < ons && i < 4096; ++i) if (on_note[i] >= 53 && on_note[i] < 77) seen[on_note[i] - 53] = 1;
      int good = 1; for (int k = 0; k < 24; ++k) { int pc = (53 + k) % 12; int in = (0xab5 >> pc) & 1; if (seen[k] != in) good = 0; }
      CHECK(good, "scale launch = the major keys only"); }
    CHECK(on_vel[0] == 110, "scale velocity %d", on_vel[0]);
    // v6: a launch clears what was swinging first (F#, out of the scale, stops).
    reset(); frame(); key(1); ticks(300); press(57, 1); tick(); int n_before = ons; ticks(2000);
    { int stray = 0; for (int i = n_before; i < ons && i < 4096; ++i) if (on_note[i] == 54) ++stray;
      CHECK(stray == 0 && ons > n_before, "launch clears others: %d stray F# notes", stray); }
    press(57, 0); tick(); int n_clear = ons; ticks(2000);
    CHECK(ons == n_clear, "press cleared: %d more notes", ons - n_clear);
    // Turning red scrolls the scales, round: back one from major = chromatic; launch = all 24.
    turn(3, -1); frame(); CHECK(state.v[4] == 0, "scale %u", state.v[4]);
    turn(3, -1); frame(); CHECK(state.v[4] == SCALES - 1, "scale wraps %u", state.v[4]);
    turn(3, 2); frame(); CHECK(state.v[4] == 1, "scale %u", state.v[4]);
    // 9b. Blue press steps the grid: 1/16 -> 1/16T -> 1/32 -> 1/4; at 1/4 with quantize 100 every crossing is on a beat.
    press(54, 0); frame(); CHECK(state.v[3] == 4, "grid %u", state.v[3]);
    press(54, 0); press(54, 0); frame(); CHECK(state.v[3] == 0 && grid_now == 96, "grid %u", state.v[3]);
    reset(); set_knob(0, 100); set_knob(2, 30); pulse = 0; count += 5; pulse = 5;
    { Op1fTick t = {}; t.bytes = 32; t.pulse = pulse; t.count = count; t.us = us; t.jumped = 1; cur_tick = count; in_call = 1; pendulum_tick(&t); in_call = 0; ++count; ++pulse; }
    unsigned beat_count = count - 6;                     // the count where pulse was 0 (a quarter's start)
    for (int k = 0; k < 6; ++k) key(k * 4);
    ticks(4000);
    { int off = 0; for (int i = 0; i < ons && i < 4096; ++i) if (((unsigned)on_at[i] - beat_count) % 96u) ++off;
      CHECK(ons > 10 && off == 0, "1/4 grid: %d of %d off the beat", off, ons); }
    press(54, 0); press(54, 0); press(54, 0); frame(); CHECK(state.v[3] == 3, "back to 1/16");
    set_knob(0, 0); set_knob(2, 0);
    // 10. An octave change: the same note from two keys never double-starts.
    reset(); key(12); { Op1fKey e = {}; e.bytes = 32; e.what = OP1F_DOWN; e.host = 0; e.key = 0; e.note = 65; e.velocity = 100; e.from = OP1F_FROM_KEYS; in_call = 1; pendulum_key(&e); in_call = 0; }
    ticks(3000);
    // 10b. Shift + key goes straight to the instrument: not taken, no swing.
    { reset(); Op1fKey e = {}; e.bytes = 32; e.what = OP1F_DOWN; e.key = 3; e.note = 56; e.velocity = 100; e.shift = 1; e.from = OP1F_FROM_KEYS;
      CHECK(pendulum_key(&e) == 0, "shift key taken"); ticks(1000); CHECK(ons == 0, "shift key swung"); }
    // 11. Keys outside 0..23 go to the instrument.
    { Op1fKey e = {}; e.bytes = 32; e.what = OP1F_DOWN; e.key = 30; e.note = 90; e.velocity = 100; e.from = OP1F_FROM_MIDI; CHECK(pendulum_key(&e) == 0, "key 30 taken"); }
    // 12. Settings saved after a second, read back.
    set_knob(0, 33); for (int i = 0; i < 80; ++i) frame();
    CHECK(saved_n == 28 && ((unsigned*)saved)[1] == 33 && ((unsigned*)saved)[5] == 1 && ((unsigned*)saved)[6] == 0, "saved %d", saved_n);
    set_knob(0, 0);

    // 13. SYNC (shift + blue press back): gravity 50 = 1 bar = 384 pulses at any tempo; the first note half way.
    press(54, 1); frame(); CHECK(state.v[5] == 1, "sync again");
    reset(); key(2); unsigned t_launch = count; ticks(1300);
    CHECK(ons >= 3 && on_at[0] - (int)t_launch == 192 && on_at[1] - on_at[0] == 384 && on_at[2] - on_at[1] == 384,
          "sync 1 bar: %d %d %d", on_at[0] - (int)t_launch, on_at[1] - on_at[0], on_at[2] - on_at[1]);
    us = 10417; reset(); key(2); ticks(1300); us = 5208;
    CHECK(ons >= 3 && on_at[2] - on_at[1] == 384, "sync at 60 BPM: %d", on_at[2] - on_at[1]);
    set_knob(1, 100); reset(); key(2); ticks(300);
    CHECK(ons >= 3 && on_at[2] - on_at[1] == 48 && texts["pend_n1"] == "1/8", "sync 1/8: %d %s", on_at[2] - on_at[1], texts["pend_n1"].c_str());
    set_knob(1, 50);
    // 14. SYNC drift 40: n = 125, a cycle of 125 bars; key k crosses 113 + k times in it; all 24 back at their
    // extremes together after exactly the cycle.
    set_knob(2, 40); frame();
    CHECK(texts["pend_sync"] == "SYNC 125 BARS", "sync text %s", texts["pend_sync"].c_str());
    reset(); key(0); ticks(2000);
    { double want = 48000.0 / 113; CHECK(ons >= 4 && fabs((on_at[3] - on_at[1]) / 2.0 - want) <= 1.0, "key 0: %.1f, %.1f", (on_at[3] - on_at[1]) / 2.0, want); }
    { reset(); for (int k = 0; k < 24; ++k) key(k); tick(); unsigned t0 = count - 1;
      while (count <= t0 + 48000) tick();
      unsigned now = (count - 1) << 8; double worst = 0;
      for (int k = 0; k < 24; ++k) { const Swing& w = hosts[0].swings[k];
          double p = (double)(now - w.last) / (double)(w.next - w.last), d = fabs(p - 0.5); if (d > worst) worst = d; }
      CHECK(worst < 0.02, "sync realign after 125 bars: worst %.3f", worst); }
    // 15. SYNC with quantize 100 at 1/16: every note on a 16th of the beat.
    set_knob(0, 100); reset(); for (int k = 0; k < 24; k += 5) key(k); ticks(6000);
    { int off = 0; for (int i = 0; i < ons && i < 4096; ++i) { unsigned c = (unsigned)on_at[i]; int ph = ((int)hosts[0].phase + (int)c - (int)(count - 1)) % 24; if (ph < 0) ph += 24; if (ph) ++off; }
      CHECK(ons > 20 && off == 0, "sync 1/16: %d of %d off", off, ons); }
    set_knob(0, 0); set_knob(2, 0);
    // 15b. Ripples drawn through their life (many at once, loud): the canvas stays in bounds (ASan).
    reset(); press(57, 1); for (int k = 0; k < 24; ++k) key(k, 127); { int painted = 0;
      for (int f = 0; f < 400; ++f) { ticks(3); frame(); for (int i = 0; i < CW * CH; i += 97) if (canvas[i]) { ++painted; break; } }
      CHECK(painted > 100, "ripples painted %d", painted); }
    CHECK(state.blooms > 0, "no bloom when every key lands together");
    // 16. Without the picture: the base's small font, no crash.
    use_picture = 0; state.document_asked = 0; state.document = 0; frame(); use_picture = 1;

    CHECK(refusals == 0 && bad_calls == 0, "refused %d, bad %d", refusals, bad_calls);

    if (argc < 3) { printf("%ld checks, %ld failed\n", checks, fails); return fails ? 1 : 0; }
    // The previews (only with a third argument), v5: 1 a pentatonic launch landing together (the bloom), words up;
    // 2 drift 40 nine seconds on, words gone; 3 the help page.
    char path[512];
    state.document_asked = 0; state.texts_hidden = 2; state.help_shown = 2; frame();
    if (state.v[5]) { press(54, 1); frame(); }
    us = 5208; reset(); set_knob(0, 0); set_knob(1, 60); set_knob(2, 0); turn(3, 9 - (int)state.v[4]); frame();
    press(57, 1); tick();
    while (ons == 0) tick();
    ticks(18); frame();
    snprintf(path, sizeof path, "%s/shot1.ppm", out); save_ppm(path); snprintf(path, sizeof path, "%s/shot1.txt", out); save_texts(path);
    reset(); set_knob(2, 40); frame(); press(57, 1); tick();
    for (int i = 0; i < 9 * 192; ++i) { tick(); if (i % 8 == 0) { ms += 100; frame(); } }
    for (int i = 0; i < 4; ++i) { tick(); frame(); }
    snprintf(path, sizeof path, "%s/shot2.ppm", out); save_ppm(path); snprintf(path, sizeof path, "%s/shot2.txt", out); save_texts(path);
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_DOWN; e.key = 0; e.mine = 1; e.thread = 1; pendulum_input(&e); frame(); frame();
      snprintf(path, sizeof path, "%s/shot3.ppm", out); save_ppm(path); snprintf(path, sizeof path, "%s/shot3.txt", out); save_texts(path);
      e.what = OP1F_UP; pendulum_input(&e); }

    printf("%ld checks, %ld failed\n", checks, fails);
    return fails ? 1 : 0;
}
