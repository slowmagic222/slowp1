// pendulum.pack: Pendulum, the player of slow magic's OP-1 "S1 Slow1" firmware, as an open-field sequencer (level 5,
// base 30+). It changes no place of the firmware and names no address of it.
//
// What the owner meets: `pendulum` at the end of the sequencer list, for the synth and the drum. Switched on, a key
// does not sound by itself (with shift held it does, played straight by the instrument): it starts a swing that
// fires the key's note each time it crosses the centre, forever, then swings back. The key again retriggers it from
// the extreme. Each key has its own swing, so many run at once and drift in and out of phase. Switching the
// sequencer off, picking another, or changing instrument clears them.
//
// Knobs on its page: blue QUANTIZE (0 free .. 100 locked to the grid of the unit's clock; a press steps the grid
// 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32; shift + press switches between tempo sync and free time), ochre GRAVITY (up is
// faster), grey DRIFT (the pendulum wave: how fast the keys' swings spread apart), red SCALE (turn: the scale, root C;
// press: clear every swing; shift + press: launch every key of the scale at once).
//
// Two kinds of time (v3):
//   SYNC (v4: shift + blue press; FREE is the default): GRAVITY is a note length, 8 bars down to an eighth (1 bar at 50), in pulses of the clock, so
//     the swings follow the tempo. DRIFT sets a cycle of C bars: the middle key (12) crosses n times in it, key k
//     n + (k - 12) times, n = 5000 / DRIFT rounded up so that the cycle is whole bars. Launched together, every swing
//     is back at its extreme together after exactly C bars. Shown top right as SYNC C BARS.
//   FREE (v2, the default from v4): GRAVITY in seconds (0.8 s at the top, 1.6 s at 50, about 8 s at the bottom); key k's frequency is
//     f0 x (1 + D x 0.0002 x (k - 11.5)); back in line after P / (D x 0.0002) seconds, shown as FREE m:ss.
// QUANTIZE blends each crossing's natural time toward the nearest line of the grid: lerp(natural, grid, Q / 100).
// In SYNC the natural times are counted from each swing's anchor, so quantizing never builds up drift. A change of
// GRAVITY or DRIFT (or the tempo, in FREE) re-paces every swing in flight from where it is. A note lasts 85 ms, at the
// key's velocity (110 from the scale launch).
//
// The page (v3): the firmware's own letters in placeholders of the picture pendulum.svg: PENDULUM top left, always;
// the four knobs' names and values along the bottom and the time top right, faded out 2.5 s after the last change.
// Over it: the centre line, two rings rippling out from the pivot at each note with a flash (wider for a louder
// note), thin strings and the balls. v4: the help key held shows every control and setting instead. v5: the look
// (note colours, shadows, trails, bloom on notes landing together, centre line glowing at alignment). v6: flat view
// again, ghost trails of ten frames, ripples to 112 px, and sparks where two rings cross.
//
// Time: all in pulses of the clock in 256ths (q8), counted from the tick's `count`; the grid is the unit's own beat.
//
// Threads: `key` (any thread, the audio thread among them) and the input function (key thread) only put launches and
// clears into a small queue inside op1f_mask sections; `tick` (sequencer thread) owns the swings and makes every note
// call, outside any section; `run` empties the queue and counts the stops so that `tick` and the frame forget the
// swings; the frame function reads a copy of the swings inside a section and paints.
//
// pendulum.bin: "PEN3", quantize, gravity, drift, grid, scale, sync: seven words, 28 bytes, in user/pf/pendulum (not
// shown). v2's "PEN2" (24 bytes) and v1's "PEND" (16 bytes) are read too.
//
// GPL-3.0-or-later with OP-1 firmware permission.

#include "build_r6e_card.h"
#include "build_q_quiet.h"

typedef unsigned int u32;
typedef unsigned char u8;

enum { HOSTS = 2, KEYS = 24, WAITING = 64, FOLDER = 0, ASK = 0 };
enum { KEY_HELP = 0, KNOB_BLUE = 0, KNOB_SECOND = 1, KNOB_THIRD = 2, KNOB_RED = 3, CLICK_BLUE = 54, CLICK_RED = 57 };
enum { FRAME_LEVEL_4 = 40, INPUT_LEVEL_2 = 36, TICK_LEVEL_5 = 32, KEY_LEVEL_5 = 32 };
enum { GRIDS = 6, SCALES = 14, SYNCS = 11, GATE_US = 85000, SCALE_VELOCITY = 110, KEY0_NOTE = 53, VALUES = 6, BAR = 384 };
enum { Q_FIRST = 0, G_FIRST = 50, D_FIRST = 0, GRID_FIRST = 3, SCALE_FIRST = 1, SYNC_FIRST = 0 };
enum { V_Q = 0, V_G = 1, V_D = 2, V_GRID = 3, V_SCALE = 4, V_SYNC = 5 };
enum { FILE_MAGIC = 0x334e4550u /* "PEN3" */, FILE_MAGIC_2 = 0x324e4550u /* "PEN2" */, FILE_MAGIC_1 = 0x444e4550u /* "PEND" */,
       FILE_BYTES = 28, FILE_BYTES_2 = 24, FILE_BYTES_1 = 16, SETTLE_MS = 1000, PERIOD_FRAMES = 256, SAID_MOST = 16, TOTALS = 5 };
enum { DO_LAUNCH = 1, DO_CLEAR = 2, DO_CLEAR_ALL = 3 };

// The screen: 480 x 220 (the canvas is 224 tall).
enum { CY = 110, PIVOT_X0 = 33, PIVOT_DX = 18, SWING = 62, R_MIN = 2, R_MAX = 9, BALL = 2 * R_MAX + 3,
       RIP_MIN = 6, RIP_MAX = 112, RIPPLE_US = 1700000, FLASH_US = 220000, RIPPLES_MOST = 12, TRAIL = 10, BLOOM_US = 900000, DIST_C = RIP_MAX + 6, DIST = DIST_C + 1,
       SPARKS_MOST = 40,
       HELP_LINES = 9, SHOW_MS = 2500, FADE_MS = 1000,
       FADE_Y = 184, FADE_H = 36, SYNC_X = 300, SYNC_Y = 4, SYNC_W = 180, SYNC_H = 22 };

static const char LOG_FILE[] = "op1f/pendulum.cpp";
static const char SAVED[] = "pendulum.bin";

// The Field palette (palette.svg): the knobs' colours and the greys.
static const unsigned BLUE = OP1F_COLOUR(0x4c, 0x7d, 0xa8);
static const unsigned OCHRE = OP1F_COLOUR(0xad, 0x89, 0x59);
static const unsigned GREY = OP1F_COLOUR(0xa2, 0xa4, 0xa6);
static const unsigned LIGHT_GREY = OP1F_COLOUR(0xcd, 0xd0, 0xc2);
static const unsigned DARK_GREY = OP1F_COLOUR(0x37, 0x3d, 0x42);
static const unsigned STRING = OP1F_COLOUR(0x5a, 0x5d, 0x60);
static const unsigned RED = OP1F_COLOUR(0xe5, 0x28, 0x17);

// SYNC's note lengths for GRAVITY, in pulses (96 a quarter), slow to fast, and their names.
static const u32 SYNC_PULSES[SYNCS] = { 3072, 1536, 1152, 768, 576, 384, 288, 192, 144, 96, 48 };
static const char* const SYNC_NAMES[SYNCS] = { "8 BARS", "4 BARS", "3 BARS", "2 BARS", "3/2 BAR", "1 BAR", "3/4", "1/2", "3/8",
                                                "1/4", "1/8" };
// GRAVITY below 50: 1.6 s x 2^((50 - g) x 2.3 / 50), in microseconds. 50 and above: 1.6 s - 16 ms x (g - 50).
static const u32 SLOW_US[50] = {
    7879324, 7632057, 7392549, 7160557, 6935846, 6718187, 6507358, 6303145, 6105341, 5913744, 5728160, 5548400, 5374282,
    5205627, 5042265, 4884029, 4730760, 4582300, 4438499, 4299211, 4164294, 4033611, 3907029, 3784419, 3665657, 3550622,
    3439197, 3331269, 3226728, 3125467, 3027384, 2932380, 2840356, 2751221, 2664882, 2581254, 2500249, 2421787, 2345787,
    2272172, 2200867, 2131800, 2064900, 2000100, 1937333, 1876536, 1817647, 1760606, 1705355, 1651838 };
// QUANTIZE's grids, in pulses (96 a quarter), and their names.
static const u32 GRID_PULSES[GRIDS] = { 96, 48, 32, 24, 16, 12 };
static const char* const GRID_NAMES[GRIDS] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
// The scales, root C: bit n = pitch class n.
static const unsigned short SCALE_BITS[SCALES] = {
    0xfff,                                                     // chromatic
    (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11),         // major
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),         // minor
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<9)|(1<<10),         // dorian
    (1<<0)|(1<<1)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10),         // phrygian
    (1<<0)|(1<<2)|(1<<4)|(1<<6)|(1<<7)|(1<<9)|(1<<11),         // lydian
    (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<10),         // mixolydian
    (1<<0)|(1<<1)|(1<<3)|(1<<5)|(1<<6)|(1<<8)|(1<<10),         // locrian
    (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<11),         // harmonic minor
    (1<<0)|(1<<2)|(1<<4)|(1<<7)|(1<<9),                        // major pentatonic
    (1<<0)|(1<<3)|(1<<5)|(1<<7)|(1<<10),                       // minor pentatonic
    (1<<0)|(1<<3)|(1<<5)|(1<<6)|(1<<7)|(1<<10),                // blues
    (1<<0)|(1<<2)|(1<<4)|(1<<6)|(1<<8)|(1<<10),                // whole tone
    (1<<0)|(1<<2)|(1<<3)|(1<<7)|(1<<8) };                      // hirajoshi
static const char* const SCALE_NAMES[SCALES] = { "CHROMA", "MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYD",
    "LOCRIAN", "HARM MIN", "PENTA", "MIN PENT", "BLUES", "WHOLE", "HIRAJOSI" };
// sin(pi/2 x k/64) in 4096ths.
static const unsigned short SINE[65] = { 0, 101, 201, 301, 401, 501, 601, 700, 799, 897, 995, 1092, 1189, 1285, 1380,
    1474, 1567, 1660, 1751, 1842, 1931, 2019, 2106, 2191, 2276, 2359, 2440, 2520, 2598, 2675, 2751, 2824, 2896, 2967,
    3035, 3102, 3166, 3229, 3290, 3349, 3406, 3461, 3513, 3564, 3612, 3659, 3703, 3745, 3784, 3822, 3857, 3889, 3920,
    3948, 3973, 3996, 4017, 4036, 4052, 4065, 4076, 4085, 4091, 4095, 4096 };


extern "C" const Op1fCard op1f_card_pendulum OP1F_NAMED("op1f_card_pendulum");

// One key's swing, owned by `tick`. Times in q8 pulses of `count`; compared by signed difference (wraps after a day).
struct Swing {
    u32 last, next;          // the crossing before and the one to come (as quantized)
    u32 anchor, j, n;        // SYNC: crossing j comes at anchor + j x cycle / n (n crossings a cycle)
    u32 gate_end;            // when the sounding note ends
    u32 fire_at;             // the last note's time, for the ripple
    u8 used, reverse, note, velocity, sounding, fire_velocity;
};

struct Waiting { u8 what, key, note, velocity; };

struct Host {
    Swing swings[KEYS];
    Waiting waiting[WAITING];
    u32 head, tail;                              // `key` and the input function write at the tail, `tick` reads at the head
    u32 stops, stops_seen;                       // `run` counts; `tick` forgets its swings at a new count
    u32 seen_g, seen_d, seen_sync, paced_us;     // what the swings were paced with
    u32 seen_us;                                 // the last tick's microseconds a pulse, for the frame
    u32 phase, phased;                           // where the tick is in the quarter note (0..95), kept from `pulse`
    int note_of_key0;                            // the note key 0 plays now: learned from the keys and `octave`
};

static Host hosts[HOSTS];

static volatile u32 now_q8[HOSTS];               // the last tick's time, for the frame
static volatile u32 q_now = Q_FIRST, g_now = G_FIRST, d_now = D_FIRST, sync_now = SYNC_FIRST;   // the frame's, read by `tick`
static volatile u32 grid_now = 24, scale_now = 0xab5;                    // the grid in pulses, the scale's bits
static volatile u32 turned[4];                   // the knobs' steps, totals the input function writes
static volatile u32 grid_presses, sync_presses;  // the blue knob's presses (with shift: sync), totals
static volatile u32 shown_where, shown_how;
static volatile u32 help_held;                    // the help key, held while our page is up
static volatile u32 heard, fired, refused, lost, launched;

// ---------------------------------------------------------------- arithmetic

static inline int after(u32 a, u32 b) { return (int)(a - b) >= 0; }          // a at or after b
static u32 clampu(int v, int most) { return v < 0 ? 0u : v > most ? (u32)most : (u32)v; }

// FREE: GRAVITY's period, in microseconds.
static u32 gravity_us(u32 g) {
    return g >= 50u ? (u32)(1600000 - 16000 * (int)(g - 50u)) : SLOW_US[g];
}

// FREE: the time between crossings for key i, in microseconds: f0 x (1 + D x 0.0002 x (i - 11.5)), in 2^20ths.
static u32 period_us(u32 key, u32 g, u32 d) {
    u32 p = gravity_us(g);
    if (d == 0) return p;
    int f = (1 << 20) + (int)d * 105 * (2 * (int)key - 23);
    return (u32)((((unsigned long long)p) << 20) / (u32)f);
}

static u32 gcd(u32 a, u32 b) { while (b) { u32 t = a % b; a = b; b = t; } return a; }

// SYNC: GRAVITY's note length (pulses), and DRIFT's cycle: `cycle` pulses in which the middle key crosses `middle`
// times. DRIFT 0: every key crosses once in a cycle of one note length.
struct Cycle { u32 pulses, cycle, middle; };
static Cycle sync_cycle(u32 g, u32 d) {
    Cycle c;
    c.pulses = SYNC_PULSES[(g * 10u + 50u) / 100u];
    if (d == 0) { c.cycle = c.pulses; c.middle = 1; return c; }
    u32 n = 5000u / d, m = (u32)BAR / gcd((u32)BAR, c.pulses);   // n x pulses a whole number of bars
    n = ((n + m - 1u) / m) * m;
    c.cycle = n * c.pulses;
    c.middle = n;
    return c;
}
static u32 crossings(const Cycle& c, u32 key, u32 d) { return d == 0 ? 1u : c.middle + key - 12u; }

// Microseconds into q8 pulses at `us` a pulse.
static u32 to_q8(u32 microseconds, u32 us) {
    if (us == 0) us = 5208;
    if (microseconds < (1u << 23)) return (microseconds << 8) / us;
    return ((microseconds << 4) / us) << 4;
}

// a / b in 4096ths, 0..4096, for a <= b.
static u32 part12(u32 a, u32 b) {
    if (b == 0 || a >= b) return 4096u;
    while (a >= (1u << 19)) { a >>= 1; b >>= 1; }
    return b ? (a << 12) / b : 4096u;
}

// The next crossing: `natural` (q8) blended toward the nearest line of the grid by Q / 100. `grid0` is a time on the
// grid (q8); `now` is the present (the result is always after it).
static u32 schedule(u32 natural, u32 now, u32 grid0, u32 q) {
    if (q == 0) return natural;
    const u32 step = grid_now << 8;
    u32 off = natural - grid0;                    // natural is after grid0
    u32 g = grid0 + ((off + (step >> 1)) / step) * step;
    while (!after(g, now + 1u)) g += step;
    int diff = (int)(g - natural);
    int moved = diff < 0 ? -(int)(((u32)(-diff) * q) / 100u) : (int)(((u32)diff * q) / 100u);
    u32 t = natural + (u32)moved;
    if (!after(t, now + 1u)) t = now + 1u;
    return t;
}

// What a tick paces with.
struct Pace { u32 now, grid0, us, sync, d; Cycle c; u32 cycle256; };

static u32 period_q8(const Pace& p, u32 key) {
    if (p.sync) return p.cycle256 / crossings(p.c, key, p.d);
    return to_q8(period_us(key, g_now, p.d), p.us);
}

static u32 sync_time(const Swing* s, u32 cycle256) {
    return s->anchor + (u32)(((unsigned long long)s->j * cycle256) / s->n);
}

// A swing set going from `done` q8 past its last crossing: the next natural crossing a period after that.
static void pace_from(Swing* s, const Pace& p, u32 key, u32 done) {
    u32 per = period_q8(p, key);
    if (done > per) done = per;
    s->last = p.now - done;
    u32 natural;
    if (p.sync) {
        s->n = crossings(p.c, key, p.d);
        s->anchor = s->last;
        s->j = 1;
        natural = sync_time(s, p.cycle256);
    } else {
        natural = s->last + per;
    }
    s->next = schedule(natural, p.now, p.grid0, q_now);
}

// ---------------------------------------------------------------- the sequencer's calls

static void queue(Host* h, u32 what, u32 key, u32 note, u32 velocity) {
    u32 was = op1f_mask();
    if (h->tail - h->head >= (u32)WAITING) {
        ++lost;
    } else {
        Waiting* w = &h->waiting[h->tail & (WAITING - 1)];
        w->what = (u8)what; w->key = (u8)key; w->note = (u8)note; w->velocity = (u8)velocity;
        ++h->tail;
    }
    op1f_unmask(was);
}

// A key down launches its swing (through the queue); the instrument plays nothing. Its up is taken and ignored.
extern "C" int pendulum_key(const Op1fKey* e) OP1F_NAMED("pendulum_key");
extern "C" int pendulum_key(const Op1fKey* e) {
    if (e->bytes < (u32)KEY_LEVEL_5 || e->host >= (u32)HOSTS || e->note > 127u) return 0;
    if (e->key < 0 || e->key >= KEYS) return 0;
    if (e->what == (u32)OP1F_UP) return 1;
    if (e->what != (u32)OP1F_DOWN || e->velocity == 0 || e->velocity > 127u) return 0;
    if (e->shift) return 0;                      // shift + key: the instrument plays it straight, past the swings
    Host* h = &hosts[e->host];
    if (e->from == (u32)OP1F_FROM_KEYS) h->note_of_key0 = (int)e->note - e->key;
    ++heard;
    queue(h, DO_LAUNCH, (u32)e->key, e->note, e->velocity);
    return 1;
}

extern "C" void pendulum_run(unsigned host, unsigned how) OP1F_NAMED("pendulum_run");
extern "C" void pendulum_run(unsigned host, unsigned how) {
    (void)how;
    if (host >= (u32)HOSTS) return;
    Host* h = &hosts[host];
    u32 was = op1f_mask();
    h->head = h->tail;
    ++h->stops;
    op1f_unmask(was);
}

extern "C" int pendulum_octave(unsigned host, int steps) OP1F_NAMED("pendulum_octave");
extern "C" int pendulum_octave(unsigned host, int steps) {
    if (host < (u32)HOSTS) hosts[host].note_of_key0 += 12 * steps;
    return 0;
}

static void end_note(Swing* s) {
    s->sounding = 0;
    if (!op1f_note_off(&op1f_card_pendulum, s->note, 0)) ++refused;
}

// Every swing paced again from where it is: its progress kept, the rest of the way at the new period.
static void repace(Host* h, const Pace& p) {
    for (u32 i = 0; i < (u32)KEYS; ++i) {
        Swing* s = &h->swings[i];
        if (!s->used) continue;
        u32 progress = after(p.now, s->last) ? part12(p.now - s->last, s->next - s->last) : 0u;
        u32 per = period_q8(p, i);
        pace_from(s, p, i, (u32)(((per >> 4) * progress) >> 8));
    }
}

static void take_in(Host* h, const Pace& p) {
    for (;;) {
        u32 was = op1f_mask();
        if (h->head == h->tail) { op1f_unmask(was); return; }
        Waiting w = h->waiting[h->head & (WAITING - 1)];
        ++h->head;
        op1f_unmask(was);
        if (w.what == DO_CLEAR_ALL) {
            for (u32 i = 0; i < (u32)KEYS; ++i) {
                if (h->swings[i].sounding) end_note(&h->swings[i]);
                h->swings[i].used = 0;
            }
            continue;
        }
        if (w.key >= (u32)KEYS) continue;
        Swing* s = &h->swings[w.key];
        if (w.what == DO_CLEAR) {
            if (s->sounding) end_note(s);
            s->used = 0;
            continue;
        }
        if (s->sounding && s->note != w.note) end_note(s);
        s->used = 1;
        s->reverse = 0;
        s->note = w.note;
        s->velocity = w.velocity;
        pace_from(s, p, w.key, period_q8(p, w.key) >> 1);     // a launch starts at the extreme: half way
    }
}

static void fire(Host* h, Swing* s, u32 now, u32 us) {
    for (u32 j = 0; j < (u32)KEYS; ++j) {        // the base takes no second note on of a sounding note
        Swing* o = &h->swings[j];
        if (o->sounding && o->note == s->note) end_note(o);
    }
    if (op1f_note_on(&op1f_card_pendulum, s->note, s->velocity, 0)) {
        s->sounding = 1;
        s->gate_end = now + to_q8(GATE_US, us);
        ++fired;
    } else {
        ++refused;
    }
    s->fire_at = now;
    s->fire_velocity = s->velocity;
}

extern "C" void pendulum_tick(const Op1fTick* t) OP1F_NAMED("pendulum_tick");
extern "C" void pendulum_tick(const Op1fTick* t) {
    if (t->bytes < (u32)TICK_LEVEL_5 || t->host >= (u32)HOSTS) return;
    Host* h = &hosts[t->host];
    Pace p;
    p.now = t->count << 8;
    p.us = t->us ? t->us : 5208u;
    p.sync = sync_now;
    p.d = d_now;
    p.c = sync_cycle(g_now, p.d);
    p.cycle256 = p.c.cycle << 8;
    u32 pulse = t->pulse;                        // 0..47: an eighth; the quarter kept by our own count
    if (!h->phased || t->jumped) { h->phase = pulse; h->phased = 1; }
    else {
        h->phase = h->phase + 1u >= 96u ? 0u : h->phase + 1u;
        if ((h->phase >= 48u ? h->phase - 48u : h->phase) != pulse) h->phase = pulse;
    }
    u32 grid = grid_now ? grid_now : 24u;
    p.grid0 = p.now - ((h->phase % grid) << 8);
    if (h->stops_seen != h->stops) {             // a stop since: the base ended our notes; the swings are gone
        h->stops_seen = h->stops;
        for (u32 i = 0; i < (u32)KEYS; ++i) h->swings[i].used = h->swings[i].sounding = 0;
    }
    // SYNC counts in pulses: a new tempo changes nothing. FREE counts in seconds: a new tempo re-paces.
    if (h->seen_g != g_now || h->seen_d != p.d || h->seen_sync != p.sync || (!p.sync && h->paced_us != p.us)) {
        h->seen_g = g_now; h->seen_d = p.d; h->seen_sync = p.sync; h->paced_us = p.us;
        repace(h, p);
    }
    h->seen_us = p.us;
    take_in(h, p);
    for (u32 i = 0; i < (u32)KEYS; ++i) {
        Swing* s = &h->swings[i];
        if (s->sounding && after(p.now, s->gate_end)) end_note(s);
        if (!s->used || !after(p.now, s->next)) continue;
        fire(h, s, p.now, p.us);
        s->reverse ^= 1u;
        s->last = s->next;
        u32 natural;
        if (p.sync) {
            if (++s->j >= s->n) { s->anchor += p.cycle256; s->j -= s->n; }
            natural = sync_time(s, p.cycle256);
        } else {
            natural = s->last + period_q8(p, i);
        }
        s->next = schedule(natural, p.now, p.grid0, q_now);
    }
    now_q8[t->host] = p.now;
}

extern "C" void pendulum_shown(unsigned where, unsigned how) OP1F_NAMED("pendulum_shown");
extern "C" void pendulum_shown(unsigned where, unsigned how) {
    shown_where = where;
    shown_how = how;
}

// ---------------------------------------------------------------- the knobs

// Every key of the scale launched at once, through the queue of the instrument whose page is up. v6: everything
// swinging is cleared first (notes off), so only the scale plays.
static void launch_scale(Host* h) {
    queue(h, DO_CLEAR_ALL, 0, 0, 0);
    u32 bits = scale_now;
    for (u32 k = 0; k < (u32)KEYS; ++k) {
        int note = h->note_of_key0 + (int)k;
        if (note < 0 || note > 127) continue;
        if (!((bits >> (u32)(note % 12)) & 1u)) continue;
        queue(h, DO_LAUNCH, k, (u32)note, SCALE_VELOCITY);
    }
    ++launched;
}

// Turns: the four knobs. Presses (the knob's click: down and up without a turn): blue steps the grid, with shift it
// switches SYNC and FREE; red clears every swing, with shift it launches the scale. Everything else goes on.
extern "C" int pendulum_input(const Op1fInput* e) OP1F_NAMED("pendulum_input");
extern "C" int pendulum_input(const Op1fInput* e) {
    if (e->bytes < (u32)INPUT_LEVEL_2 || !e->mine) return 0;
    if (e->what == (u32)OP1F_TURN) {
        if (e->key > (u32)KNOB_RED) return 0;
        turned[e->key] = turned[e->key] + (u32)(e->turn >> 10);
        return 1;
    }
    if (e->key == (u32)KEY_HELP) {               // held: every control and setting on the page
        if (e->what == (u32)OP1F_UP) help_held = 0;
        else if (e->what == (u32)OP1F_DOWN || e->what == (u32)OP1F_DOWN_SHIFT) help_held = 1;
        return 1;
    }
    if (e->key != (u32)CLICK_BLUE && e->key != (u32)CLICK_RED) return 0;
    if (e->what == (u32)OP1F_UP) return 1;
    if (e->what != (u32)OP1F_DOWN && e->what != (u32)OP1F_DOWN_SHIFT) return 0;
    u32 shift = e->shift || e->what == (u32)OP1F_DOWN_SHIFT;
    if (e->key == (u32)CLICK_BLUE) {
        if (shift) ++sync_presses; else ++grid_presses;
        return 1;
    }
    Host* h = &hosts[shown_where & 1u];
    if (shift) launch_scale(h);
    else queue(h, DO_CLEAR_ALL, 0, 0, 0);
    return 1;
}

// ---------------------------------------------------------------- the frame function's work

static const char TEXT_TITLE[] = "pend_title";
static const char TEXT_SYNC[] = "pend_sync";
static const char TEXT_NAME[4][8] = { "pend_l0", "pend_l1", "pend_l2", "pend_l3" };
static const char TEXT_VALUE[4][8] = { "pend_n0", "pend_n1", "pend_n2", "pend_n3" };
static const char* const NAMES[4] = { "QUANTIZE", "GRAVITY", "DRIFT", "SCALE" };
static const int MIDDLE[4] = { 60, 180, 300, 420 };

static const char TEXT_HELP[HELP_LINES][8] = { "pend_h0", "pend_h1", "pend_h2", "pend_h3", "pend_h4", "pend_h5", "pend_h6",
                                                 "pend_h7", "pend_h8" };

static struct {
    u32 started, stood, refusal_said, document_asked;
    void* document;
    u32 v[VALUES], seen[4], seen_grid, seen_sync, tried[VALUES], last[VALUES], changed_at;
    u32 seen_turns, texts_hidden, help_shown, fade_level, drawn;
    u32 said_how, shows_said, writes, failed;
    u32 printed[TOTALS];
    u32 file[FILE_BYTES >> 2];
    char text[24];
    Swing rows[KEYS];
    u8 ball[R_MAX - R_MIN + 1][BALL * BALL];
    unsigned short dist[DIST * DIST];            // distance from the middle in 16ths of a pixel (one quarter), for the ripples
    u8 cover[2 * DIST + 1 > BALL * BALL ? 2 * DIST + 1 : BALL * BALL];
    int ring_x[2 * RIPPLES_MOST];
    u32 ring_r16[2 * RIPPLES_MOST], ring_w[2 * RIPPLES_MOST];
    unsigned ring_c[2 * RIPPLES_MOST];
    u8 band[480];
    char line[64];
    int trail[KEYS][TRAIL];
    u32 trail_n[KEYS];
    u32 seen_fires, bloom_at, bloom_n, blooms;
    int bloom_x0, bloom_x1, bloom_y;
    u8 veil[480 * FADE_H];
    int countdown;
    QuietGate quiet;
} state;

static const u32 FIRST[VALUES] = { Q_FIRST, G_FIRST, D_FIRST, GRID_FIRST, SCALE_FIRST, SYNC_FIRST };

static u32 isqrt(u32 n) {
    u32 r = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

// Soft discs and thin rings, as covers for op1f_paint_mask: distances in 16ths of a pixel.
static void make_covers() {
    for (u32 r = R_MIN; r <= (u32)R_MAX; ++r) {
        u8* c = state.ball[r - R_MIN];
        for (int y = 0; y < BALL; ++y) for (int x = 0; x < BALL; ++x) {
            int dx = x - BALL / 2, dy = y - BALL / 2;
            int d16 = (int)isqrt((u32)(dx * dx + dy * dy) << 8);
            int edge = (int)(r << 4) + 8 - d16;
            c[y * BALL + x] = (u8)(edge <= 0 ? 0 : edge >= 16 ? 255 : edge * 16);
        }
    }
    for (int y = 0; y < DIST; ++y) for (int x = 0; x < DIST; ++x) {
        state.dist[y * DIST + x] = (unsigned short)isqrt((u32)(x * x + y * y) << 8);
    }
}

static u32 put_number(char* out, u32 n) {
    char digits[12];
    u32 k = 0, at = 0;
    do { digits[k++] = (char)('0' + n % 10u); n /= 10u; } while (n && k < sizeof digits);
    while (k) out[at++] = digits[--k];
    out[at] = 0;
    return at;
}

static u32 put_text(char* out, u32 at, const char* text) {
    while (*text) out[at++] = *text++;
    out[at] = 0;
    return at;
}

// A value's text: what the bottom row shows under knob k.
static const char* value_text(u32 k) {
    char* t = state.text;
    u32 at = 0;
    switch (k) {
    case 0: at = put_number(t, state.v[V_Q]); at = put_text(t, at, "  "); put_text(t, at, GRID_NAMES[state.v[V_GRID]]); break;
    case 1: if (state.v[V_SYNC]) put_text(t, 0, SYNC_NAMES[(state.v[V_G] * 10u + 50u) / 100u]); else put_number(t, state.v[V_G]); break;
    case 2: put_number(t, state.v[V_D]); break;
    default: put_text(t, 0, SCALE_NAMES[state.v[V_SCALE]]); break;
    }
    return t;
}

// The time top right: SYNC n BARS (the cycle) or FREE m:ss (back in line).
static const char* time_text() {
    char* t = state.text;
    u32 d = state.v[V_D];
    if (state.v[V_SYNC]) {
        u32 at = put_text(t, 0, "SYNC");
        if (d) {
            Cycle c = sync_cycle(state.v[V_G], d);
            at = put_text(t, at, " ");
            at += put_number(t + at, c.cycle / (u32)BAR);
            put_text(t, at, " BARS");
        }
        return t;
    }
    u32 at = put_text(t, 0, "FREE");
    if (!d) return t;
    u32 secs = gravity_us(state.v[V_G]) / (200u * d);
    at = put_text(t, at, " ");
    if (secs >= 6000u) { put_text(t, at, ">99 MIN"); return t; }
    at += put_number(t + at, secs / 60u);
    t[at++] = ':';
    u32 ss = secs % 60u;
    t[at++] = (char)('0' + ss / 10u);
    t[at++] = (char)('0' + ss % 10u);
    t[at] = 0;
    return t;
}

// A soft ring: radius r16 and half its width th16 (16ths of a pixel), `weight` 0..256, painted row by row over only
// the pixels it covers.
static void paint_ripple(void* canvas, int x, int y, u32 r16, u32 th16, u32 weight, unsigned colour) {
    if (weight == 0) return;
    int out = (int)((r16 + th16) >> 4) + 1, in = (int)(r16 > th16 + 16u ? ((r16 - th16) >> 4) - 1 : 0);
    if (out > DIST_C) out = DIST_C;
    for (int dy = -out; dy <= out; ++dy) {
        if (y + dy < 0 || y + dy >= 220) continue;
        int xo = (int)isqrt((u32)(out * out - dy * dy));
        int xi = (dy > -in && dy < in) ? (int)isqrt((u32)(in * in - dy * dy)) : -1;
        const unsigned short* row = state.dist + (dy < 0 ? -dy : dy) * DIST;
        for (int side = 0; side < 2; ++side) {
            int x0 = side == 0 ? -xo : (xi < 0 ? xo + 1 : xi), x1 = side == 0 ? (xi < 0 ? xo : -xi) : xo;
            if (x + x0 < 0) x0 = -x;
            if (x + x1 > 479) x1 = 479 - x;
            if (x1 < x0) continue;
            int n = 0;
            for (int dx = x0; dx <= x1; ++dx) {
                int off = (int)row[dx < 0 ? -dx : dx] - (int)r16;
                if (off < 0) off = -off;
                int a = (int)th16 - off;
                u32 c = a <= 0 ? 0u : a >= 16 ? 255u : (u32)a * 16u;
                state.cover[n++] = (u8)((c * weight) >> 8);
            }
            op1f_paint_mask(canvas, x + x0, y + dy, n, 1, state.cover, n, colour);
        }
    }
}

// The help page's lines: every control, with its setting now.
static const char* help_line(u32 k) {
    char* t = state.line;
    u32 at = 0;
    switch (k) {
    case 0: at = put_text(t, 0, "BLUE  QUANTIZE  "); at += put_number(t + at, state.v[V_Q]); break;
    case 1: at = put_text(t, 0, "BLUE PRESS  GRID  "); at = put_text(t, at, GRID_NAMES[state.v[V_GRID]]); break;
    case 2: at = put_text(t, 0, "SHIFT + BLUE PRESS  "); at = put_text(t, at, state.v[V_SYNC] ? "SYNC (OR FREE)" : "FREE (OR SYNC)"); break;
    case 3: at = put_text(t, 0, "OCHRE  GRAVITY  "); at = put_text(t, at, value_text(1)); break;
    case 4: at = put_text(t, 0, "GREY  DRIFT  "); at += put_number(t + at, state.v[V_D]); at = put_text(t, at, "  "); at = put_text(t, at, time_text()); break;
    case 5: at = put_text(t, 0, "RED  SCALE  "); at = put_text(t, at, SCALE_NAMES[state.v[V_SCALE]]); break;
    case 6: put_text(t, 0, "RED PRESS  CLEAR ALL  /  SHIFT  LAUNCH SCALE"); break;
    case 7: put_text(t, 0, "KEY  LAUNCH OR RETRIGGER ITS SWING"); break;
    default: put_text(t, 0, "SHIFT + KEY  PLAY IT STRAIGHT"); break;
    }
    return t;
}

// The words: in the picture's placeholders (or the base's small font where the picture is missing), the bottom row
// and the time faded out SHOW_MS after the last change.
static void paint_words(void* canvas, u32 ms) {
    u32 idle = ms - state.changed_at;
    int shown = idle < (u32)SHOW_MS ? 256 : idle < (u32)(SHOW_MS + FADE_MS) ? 256 - (int)((idle - (u32)SHOW_MS) * 256u / (u32)FADE_MS) : 0;
    static const unsigned colours[4] = { BLUE, OCHRE, LIGHT_GREY, RED };
    void* doc = state.document;
    static const unsigned help_colours[HELP_LINES] = { BLUE, BLUE, BLUE, OCHRE, LIGHT_GREY, RED, RED, GREY, GREY };
    u32 help = help_held;
    if (help) shown = 0;
    if (!doc) {
        op1f_paint_text(canvas, 12, 10, "PENDULUM", LIGHT_GREY, 2);
        for (u32 k = 0; help && k < (u32)HELP_LINES; ++k) op1f_paint_text(canvas, 24, 36 + 19 * (int)k, help_line(k), help_colours[k], 1);
        if (shown == 0) return;
        for (u32 k = 0; k < 4u; ++k) {
            op1f_paint_text(canvas, MIDDLE[k] - 30, 188, NAMES[k], colours[k], 1);
            op1f_paint_text(canvas, MIDDLE[k] - 30, 202, value_text(k), LIGHT_GREY, 1);
        }
        op1f_paint_text(canvas, 380, 10, time_text(), GREY, 1);
        return;
    }
    op1f_text_set(doc, TEXT_TITLE, "PENDULUM", 0, OP1F_LEFT);
    for (u32 k = 0; k < 4u; ++k) {
        op1f_text_set(doc, TEXT_NAME[k], NAMES[k], 0, OP1F_CENTRE);
        op1f_text_set(doc, TEXT_VALUE[k], value_text(k), 0, OP1F_CENTRE);
    }
    op1f_text_set(doc, TEXT_SYNC, time_text(), 0, OP1F_RIGHT);
    for (u32 k = 0; k < (u32)HELP_LINES; ++k) op1f_text_set(doc, TEXT_HELP[k], help ? help_line(k) : "", 0, OP1F_LEFT);
    if (help != state.help_shown) {
        u32 all = 1;
        for (u32 k = 0; k < (u32)HELP_LINES; ++k) { void* n = op1f_text_node(doc, TEXT_HELP[k]); if (n) op1f_show(n, help ? 1 : 0); else all = 0; }
        if (all) state.help_shown = help;
    }
    u32 hide = shown == 0 ? 1u : 0u;
    if (hide != state.texts_hidden) {
        u32 all = 1;
        for (u32 k = 0; k < 4u; ++k) {
            void* a = op1f_text_node(doc, TEXT_NAME[k]);
            void* b = op1f_text_node(doc, TEXT_VALUE[k]);
            if (a) op1f_show(a, hide ? 0 : 1); else all = 0;
            if (b) op1f_show(b, hide ? 0 : 1); else all = 0;
        }
        void* s = op1f_text_node(doc, TEXT_SYNC);
        if (s) op1f_show(s, hide ? 0 : 1); else all = 0;
        if (all) state.texts_hidden = hide;
    }
    if (shown > 0 && shown < 256) {             // fading: a dark veil over the row and the time
        u32 veil = (u32)(255 - shown);
        if (veil != state.fade_level) { state.fade_level = veil; for (u32 i = 0; i < (u32)(480 * FADE_H); ++i) state.veil[i] = (u8)veil; }
        op1f_paint_mask(canvas, 0, FADE_Y, 480, FADE_H, state.veil, 480, 0u);
        op1f_paint_mask(canvas, SYNC_X, SYNC_Y, SYNC_W, SYNC_H, state.veil, 480, 0u);
    }
}

// ---- v5: the look. A slight 3D tilt (the row recedes to the right like a mobile), each note's colour by its pitch
// class around the circle of fifths in the Field palette, a dotted path for each swing (dots at equal times, close at
// the ends), soft shadows, short trails, ripples in the note's colour, a bloom when three or more notes land
// together, and the centre line brightening as the swings come into line (how alike their phases are).

// Colours 0..256 of the way from a to b (RGB565).
static unsigned mix565(unsigned a, unsigned b, u32 w) {
    int ar = (int)(a >> 11) & 31, ag = (int)(a >> 5) & 63, ab = (int)a & 31;
    int br = (int)(b >> 11) & 31, bg = (int)(b >> 5) & 63, bb = (int)b & 31;
    int r = ar + (((br - ar) * (int)w) >> 8), g = ag + (((bg - ag) * (int)w) >> 8), bl = ab + (((bb - ab) * (int)w) >> 8);
    return (unsigned)((r << 11) | (g << 5) | bl);
}

// Pitch class -> colour: its place on the circle of fifths walked through blue, light grey, ochre, red and back.
static unsigned note_colour(u32 note) {
    static const unsigned ANCHORS[4] = { OP1F_COLOUR(0x4c, 0x7d, 0xa8), OP1F_COLOUR(0xcd, 0xd0, 0xc2), OP1F_COLOUR(0xad, 0x89, 0x59),
                                         OP1F_COLOUR(0xe5, 0x28, 0x17) };
    u32 fifths = (note % 12u) * 7u % 12u;        // C 0, G 1, D 2 ...
    u32 a = fifths / 3u, w = (fifths % 3u) * 85u;
    return mix565(ANCHORS[a], ANCHORS[(a + 1u) & 3u], w);
}

static void paint_cover(void* canvas, int x, int y, const u8* c, u32 weight, unsigned colour) {
    for (u32 k = 0; k < (u32)(BALL * BALL); ++k) state.cover[k] = (u8)(((u32)c[k] * weight) >> 8);
    op1f_paint_mask(canvas, x - BALL / 2, y - BALL / 2, BALL, BALL, state.cover, BALL, colour);
}

static void paint_bloom(void* canvas, u32 now, u32 bloom_len) {
    if (!state.bloom_n || !after(now, state.bloom_at) || now - state.bloom_at >= bloom_len) return;
    u32 t = part12(now - state.bloom_at, bloom_len), left = 4096u - t;
    u32 peak = 70u + 14u * (state.bloom_n > 8u ? 8u : state.bloom_n);              // v8: 112 .. 182, under the balls
    u32 strength = (((left * left) >> 12) * peak) >> 12;
    int h = 8 + 3 * (int)(state.bloom_n > 8u ? 8u : state.bloom_n);
    int x0 = state.bloom_x0 - 34, x1 = state.bloom_x1 + 34;
    if (x0 < 0) x0 = 0;
    if (x1 > 479) x1 = 479;
    int w = x1 - x0 + 1;
    for (int dy = -h; dy <= h; ++dy) {                                             // a soft band along the line
        u32 fall = (u32)(h - (dy < 0 ? -dy : dy)) * 256u / (u32)h;
        u32 row = (strength * ((fall * fall) >> 8)) >> 8;
        if (row == 0) continue;
        for (int i = 0; i < w; ++i) {
            int edge = i < 34 ? i : (w - 1 - i < 34 ? w - 1 - i : 34);
            state.band[i] = (u8)((row * (u32)edge) / 34u);
        }
        op1f_paint_mask(canvas, x0, state.bloom_y + dy, w, 1, state.band, w, LIGHT_GREY);
    }
    u32 ease = 4096u - ((left * left) >> 12);
    paint_ripple(canvas, (state.bloom_x0 + state.bloom_x1) / 2, state.bloom_y, (8u << 4) + ((((u32)RIP_MAX - 8u) << 4) * ease >> 12),
                 9u + ((8u * left) >> 12), (strength * 3u) >> 2, LIGHT_GREY);
}

static void paint(void* canvas, u32 where, u32 ms) {
    paint_words(canvas, ms);
    if (help_held) return;                       // the help page: the words alone

    Host* h = &hosts[where & 1u];
    u32 was = op1f_mask();
    for (u32 i = 0; i < (u32)KEYS; ++i) state.rows[i] = h->swings[i];
    u32 now = now_q8[where & 1u], gone = h->stops != h->stops_seen, us = h->seen_us;
    op1f_unmask(was);

    // the row in perspective: key k at scale s (4096 near, left .. 3060 far, right), its pivot higher as it recedes
    int px[KEYS], py[KEYS], bx[KEYS], by[KEYS], br[KEYS];
    u32 sc[KEYS], live[KEYS], sine_of[KEYS];
    int cx = PIVOT_X0 * 16;                      // x in 16ths
    for (u32 i = 0; i < (u32)KEYS; ++i) {
        sc[i] = 4096u;                         // v6: an even view from above again
        px[i] = cx >> 4;
        cx += PIVOT_DX * 16;
        py[i] = CY;
        const Swing* s = &state.rows[i];
        live[i] = s->used && !gone;
        bx[i] = px[i];
        by[i] = py[i];
        br[i] = R_MIN;
        sine_of[i] = 0;
        if (!live[i]) { state.trail_n[i] = 0; continue; }
        u32 p = after(now, s->last) ? part12(now - s->last, s->next - s->last) : 0u;   // 0 at a crossing, 4096 at the next
        u32 k = p > 2048u ? 4096u - p : p;                                             // sin(pi p)
        u32 sine = SINE[(k * 64u) >> 11];
        sine_of[i] = sine;
        int d = (int)(((sine * (u32)SWING) >> 12) * sc[i] >> 12);
        by[i] = s->reverse ? py[i] - d : py[i] + d;
        int r = (int)((((u32)R_MIN << 12) + sine * (u32)(R_MAX - R_MIN)) * sc[i] >> 24);
        br[i] = r < R_MIN ? R_MIN : r > R_MAX ? R_MAX : r;
    }

    // the centre line: v8 dark at rest, flashing up when a note fires and fading in half a second (louder, brighter)
    u32 glow = 0, line_fade = to_q8(500000u, us);
    for (u32 i = 0; i < (u32)KEYS; ++i) {
        const Swing* s = &state.rows[i];
        if (!live[i] || !s->fire_velocity || !after(now, s->fire_at) || now - s->fire_at >= line_fade) continue;
        u32 left = 4096u - part12(now - s->fire_at, line_fade);
        u32 g = (((left * left) >> 12) * (60u + s->fire_velocity)) >> 12;          // up to 187
        if (g > glow) glow = g;
    }
    for (u32 i = 0; i + 1u < (u32)KEYS; ++i) op1f_paint_line(canvas, px[i], py[i], px[i + 1], py[i + 1], 1, mix565(DARK_GREY, LIGHT_GREY, glow));

    // shadows: soft and dark, down and to the right of each ball
    for (u32 i = 0; i < (u32)KEYS; ++i) {
        if (!live[i]) continue;
        paint_cover(canvas, bx[i] + 3, by[i] + 5, state.ball[br[i] - R_MIN], 150u, OP1F_COLOUR(0x22, 0x25, 0x28));
    }

    // trails: where each ball was in the last frames, smaller and fainter
    for (u32 i = 0; i < (u32)KEYS; ++i) {
        if (!live[i]) continue;
        unsigned colour = note_colour(state.rows[i].note);
        for (u32 k = 0; k < state.trail_n[i]; ++k) {
            u32 age = state.trail_n[i] - k;      // 1 newest .. TRAIL oldest
            int r = br[i] - (int)age / 4;       // a ghost: the ball's size, thinning and fading over ten frames
            if (r < R_MIN) r = R_MIN;
            paint_cover(canvas, bx[i], state.trail[i][k], state.ball[r - R_MIN], (u32)(TRAIL + 1 - (int)age) * 9u, colour);
        }
        if (state.trail_n[i] == (u32)TRAIL) for (u32 k = 0; k + 1u < (u32)TRAIL; ++k) state.trail[i][k] = state.trail[i][k + 1];
        else ++state.trail_n[i];
        state.trail[i][state.trail_n[i] - 1u] = by[i];
    }

    // the notes landed since the last frame: three or more within two pulses of each other set off a bloom
    {
        u32 best = 0, best_at = 0;
        for (u32 i = 0; i < (u32)KEYS; ++i) {
            const Swing* s = &state.rows[i];
            if (!live[i] || !s->fire_velocity || !after(s->fire_at, state.seen_fires + 1u)) continue;
            u32 c = 0;
            for (u32 j = 0; j < (u32)KEYS; ++j) {
                const Swing* o = &state.rows[j];
                if (!live[j] || !o->fire_velocity || !after(o->fire_at, state.seen_fires + 1u)) continue;
                int dt = (int)(o->fire_at - s->fire_at);
                if (dt >= -512 && dt <= 512) ++c;
            }
            if (c > best) { best = c; best_at = s->fire_at; }
        }
        if (best >= 3u) {
            int x0 = 479, x1 = 0, ys = 0, m = 0;
            for (u32 j = 0; j < (u32)KEYS; ++j) {
                const Swing* o = &state.rows[j];
                if (!live[j] || !o->fire_velocity) continue;
                int dt = (int)(o->fire_at - best_at);
                if (dt < -512 || dt > 512) continue;
                if (px[j] < x0) x0 = px[j];
                if (px[j] > x1) x1 = px[j];
                ys += py[j];
                ++m;
            }
            state.bloom_at = best_at; state.bloom_n = best; state.bloom_x0 = x0; state.bloom_x1 = x1; state.bloom_y = ys / (m ? m : 1);
            ++state.blooms;
        }
        state.seen_fires = now;
    }
    paint_bloom(canvas, now, to_q8(BLOOM_US, us));

    // ripples: at each note a flash at the pivot and two rings running out from it in the note's colour, the second
    // behind the first and fainter; wider and brighter for a louder note, thinning and fading as they grow; smaller
    // as the row recedes. The most recent twelve.
    u32 ripple = to_q8(RIPPLE_US, us), flash = to_q8(FLASH_US, us);
    u32 drawn = 0, drawn_bits = 0, rings = 0;
    for (u32 pass = 0; pass < (u32)KEYS && drawn < (u32)RIPPLES_MOST; ++pass) {
        int pick = -1;                           // the youngest not yet drawn
        u32 best = 0xffffffffu;
        for (u32 i = 0; i < (u32)KEYS; ++i) {
            const Swing* s = &state.rows[i];
            if (!live[i] || !s->fire_velocity || !after(now, s->fire_at)) continue;
            u32 age = now - s->fire_at;
            if (age < ripple && age < best && !((drawn_bits >> i) & 1u)) { best = age; pick = (int)i; }
        }
        if (pick < 0) break;
        drawn_bits |= 1u << (u32)pick;
        ++drawn;
        const Swing* s = &state.rows[pick];
        unsigned colour = mix565(note_colour(s->note), LIGHT_GREY, 90u);
        u32 loud = 40u + s->fire_velocity;                              // 41..167
        u32 reach16 = (((u32)(RIP_MAX - RIP_MIN) * loud / 167u) << 4) * sc[pick] >> 12;
        for (u32 ring = 0; ring < 1u; ++ring) {             // v7: one thin ring, fading as it grows
            u32 delay = 0u;
            if (best < delay) continue;
            u32 t = part12(best - delay, ripple - delay);               // 0..4096 of this ring's life
            u32 ease = 4096u - (((4096u - t) * (4096u - t)) >> 12);    // fast, then slow
            u32 r16 = ((u32)RIP_MIN << 4) + ((reach16 * ease) >> 12);
            u32 left = 4096u - t;
            u32 weight = ((((left * left) >> 12) + left) >> 5) * 256u / 256u * (96u + loud) / 263u;   // v9: fades later, up to 256
            u32 th16 = 11u + ((6u * left) >> 12);                      // thin: 1.1 px at the start, 0.7 at the end
            paint_ripple(canvas, px[pick], py[pick], r16, th16, weight, colour);
            if (rings < 2u * RIPPLES_MOST) { state.ring_x[rings] = px[pick]; state.ring_r16[rings] = r16; state.ring_w[rings] = weight; state.ring_c[rings] = note_colour(s->note); ++rings; }
        }
        if (best < flash) paint_cover(canvas, px[pick], py[pick], state.ball[R_MAX - R_MIN], (4096u - part12(best, flash)) * 170u >> 12, colour);
    }

    // where two rings meet they spark: a bright soft point at each crossing, the two colours mixed, as bright as the
    // fainter of the two (plus a little); the points run along the rings as they grow
    u32 sparks = 0;
    for (u32 a = 0; a < rings && sparks < (u32)SPARKS_MOST; ++a) {
        for (u32 b = a + 1u; b < rings && sparks < (u32)SPARKS_MOST; ++b) {
            int d = state.ring_x[b] - state.ring_x[a];
            if (d == 0) continue;
            int r1 = (int)(state.ring_r16[a] >> 4), r2 = (int)(state.ring_r16[b] >> 4), ad = d < 0 ? -d : d;
            if (ad > r1 + r2 || ad < (r1 > r2 ? r1 - r2 : r2 - r1)) continue;            // apart, or one inside the other
            int along = (d * d + r1 * r1 - r2 * r2) / (2 * d);                            // from a's centre toward b
            int h2 = r1 * r1 - along * along;
            if (h2 < 0) continue;
            int hy = (int)isqrt((u32)h2);
            u32 w = state.ring_w[a] < state.ring_w[b] ? state.ring_w[a] : state.ring_w[b];
            w = w / 3u;                                          // v9: small and faint, never above 70
            if (w > 70u) w = 70u;
            if (w < 6u) continue;
            unsigned c = mix565(state.ring_c[a], state.ring_c[b], 128u);
            int sxp = state.ring_x[a] + along;
            if (sxp < BALL / 2 || sxp > 479 - BALL / 2) continue;
            if (CY - hy >= BALL / 2) paint_cover(canvas, sxp, CY - hy, state.ball[0], w, c);   // kept on screen
            if (CY + hy <= 219 - BALL / 2) paint_cover(canvas, sxp, CY + hy, state.ball[0], w, c);
            ++sparks;
        }
    }

    // strings, balls, pivots
    for (u32 i = 0; i < (u32)KEYS; ++i) {
        op1f_paint_box(canvas, px[i] - 1, py[i] - 1, 3, 3, live[i] ? LIGHT_GREY : DARK_GREY);
        if (!live[i]) continue;
        if (by[i] != py[i]) op1f_paint_line(canvas, px[i], py[i], bx[i], by[i], 1, STRING);
        // v7: the balls are the brightest thing on the page: the note colour lifted toward light grey, and a light
        // core so each note stands out over the rings, sparks and bloom
        unsigned colour = mix565(note_colour(state.rows[i].note), LIGHT_GREY, 100u + ((sine_of[i] * 60u) >> 12));
        op1f_paint_mask(canvas, bx[i] - BALL / 2, by[i] - BALL / 2, BALL, BALL, state.ball[br[i] - R_MIN], BALL, colour);
        if (br[i] > R_MIN + 1) paint_cover(canvas, bx[i], by[i], state.ball[br[i] / 2 > R_MIN ? br[i] / 2 - R_MIN : 0], 150u, LIGHT_GREY);
    }
}

static void load() {
    int got = op1f_read(&op1f_card_pendulum, FOLDER, SAVED, state.file, sizeof state.file);
    u32 ok3 = state.file[1] <= 100u && state.file[2] <= 100u && state.file[3] <= 100u;
    u32 ok5 = ok3 && state.file[4] < (u32)GRIDS && state.file[5] < (u32)SCALES;
    if (got == (int)FILE_BYTES && state.file[0] == (u32)FILE_MAGIC && ok5 && state.file[6] <= 1u) {
        for (u32 k = 0; k < (u32)VALUES; ++k) state.v[k] = state.file[k + 1];
    } else if (got == (int)FILE_BYTES_2 && state.file[0] == (u32)FILE_MAGIC_2 && ok5) {
        for (u32 k = 0; k < 5u; ++k) state.v[k] = state.file[k + 1];
    } else if (got == (int)FILE_BYTES_1 && state.file[0] == (u32)FILE_MAGIC_1 && ok3) {
        for (u32 k = 0; k < 3u; ++k) state.v[k] = state.file[k + 1];
    } else {
        op1f_log(3, LOG_FILE, 3, "pendulum: no settings read (answer %d): the first ones", got);
        return;
    }
    op1f_log(3, LOG_FILE, 2, "pendulum: pendulum.bin read: quantize %u (%s), gravity %u, drift %u, scale %s, %s", state.v[V_Q],
             GRID_NAMES[state.v[V_GRID]], state.v[V_G], state.v[V_D], SCALE_NAMES[state.v[V_SCALE]], state.v[V_SYNC] ? "sync" : "free");
}

static void save() {
    state.file[0] = (u32)FILE_MAGIC;
    for (u32 k = 0; k < (u32)VALUES; ++k) state.file[k + 1] = state.tried[k] = state.v[k];
    int answer = op1f_write(&op1f_card_pendulum, FOLDER, SAVED, state.file, FILE_BYTES);
    if (answer == (int)FILE_BYTES) ++state.writes;
    else { ++state.failed; op1f_log(2, LOG_FILE, 6, "pendulum: pendulum.bin could not be written (answer %d)", answer); }
}

static void publish() {
    q_now = state.v[V_Q];
    g_now = state.v[V_G];
    d_now = state.v[V_D];
    grid_now = GRID_PULSES[state.v[V_GRID]];
    scale_now = SCALE_BITS[state.v[V_SCALE]];
    sync_now = state.v[V_SYNC];
}

static void say_page(u32 how, u32 where) {
    u32 up = how & (u32)OP1F_SHOWN_UP, off = how & (u32)OP1F_SHOWN_OFF;
    u32 said = up ? up | off | ((where & 1u) << 4) : 0u;
    if (said == state.said_how) return;
    state.said_how = said;
    if (++state.shows_said > (u32)SAID_MOST) return;
    if (up) op1f_log(3, LOG_FILE, 9, "pendulum: its page is on the screen for the %s, its sequencer %s", where ? "drum" : "synth", off ? "off" : "on");
    else op1f_log(3, LOG_FILE, 10, "pendulum: its page is no longer on the screen");
}

static void say_period(const u32* t) {
    op1f_log(3, LOG_FILE, 1, "pendulum: %u key(s), %u note(s), %u refused, %u lost, %u scale launch(es); quantize %u, gravity %u, drift %u, %s",
             t[0] - state.printed[0], t[1] - state.printed[1], t[2] - state.printed[2], t[3] - state.printed[3], t[4] - state.printed[4],
             state.v[V_Q], state.v[V_G], state.v[V_D], state.v[V_SYNC] ? "sync" : "free");
    for (u32 i = 0; i < (u32)TOTALS; ++i) state.printed[i] = t[i];
}

extern "C" void pendulum_frame(const Op1fFrame* now) OP1F_NAMED("pendulum_frame");
extern "C" void pendulum_frame(const Op1fFrame* now) {
    if (now->bytes < (u32)FRAME_LEVEL_4) return;
    if (!state.started) {
        state.started = 1;
        for (u32 k = 0; k < (u32)VALUES; ++k) state.v[k] = FIRST[k];
        load();
        for (u32 k = 0; k < (u32)VALUES; ++k) state.tried[k] = state.last[k] = state.v[k];
        for (u32 k = 0; k < 4u; ++k) state.seen[k] = turned[k];
        state.seen_grid = grid_presses;
        state.seen_sync = sync_presses;
        state.texts_hidden = 2;                  // neither: set at the first frame
        state.help_shown = 2;
        state.changed_at = now->ms;
        for (u32 h = 0; h < (u32)HOSTS; ++h) hosts[h].note_of_key0 = KEY0_NOTE;
        make_covers();
        publish();
    }
    if (!state.document_asked) {
        state.document_asked = 1;
        state.document = op1f_picture(&op1f_card_pendulum, "pendulum.svg");
        if (!state.document) op1f_log(2, LOG_FILE, 11, "pendulum: no picture pendulum.svg; the words in the base's small font");
    }
    if (!state.stood) {
        if (op1f_page_stand_in(&op1f_card_pendulum, ASK, state.document)) state.stood = 1;
        else if (!state.refusal_said) { state.refusal_said = 1; op1f_log(2, LOG_FILE, 8, "pendulum: the base did not take its page"); }
    }
    for (u32 k = 0; k < 3u; ++k) {
        u32 t = turned[k];
        int dk = (int)(t - state.seen[k]);
        state.seen[k] = t;
        state.v[k] = clampu((int)state.v[k] + dk, 100);
    }
    {
        u32 t = turned[3];                       // the scale: turned through the list, round
        int dk = (int)(t - state.seen[3]);
        state.seen[3] = t;
        int v = ((int)state.v[V_SCALE] + dk) % SCALES;
        state.v[V_SCALE] = (u32)(v < 0 ? v + SCALES : v);
        u32 p = grid_presses, dp = p - state.seen_grid;
        state.seen_grid = p;
        state.v[V_GRID] = (state.v[V_GRID] + dp) % (u32)GRIDS;
        u32 s = sync_presses, ds = s - state.seen_sync;
        state.seen_sync = s;
        state.v[V_SYNC] ^= ds & 1u;
    }
    u32 turns = turned[0] + turned[1] + turned[2] + turned[3] + grid_presses + sync_presses;
    u32 moved_v = turns != state.seen_turns;     // a turn at an end shows the words too
    state.seen_turns = turns;
    for (u32 k = 0; k < (u32)VALUES; ++k) if (state.v[k] != state.last[k]) { state.last[k] = state.v[k]; moved_v = 1; }
    if (moved_v) state.changed_at = now->ms;
    publish();
    u32 how = shown_how, where = shown_where;
    say_page(how, where);
    u32 on_page = now->page == &op1f_card_pendulum ? 1u : 0u;
    u32 dirty = 0;
    for (u32 k = 0; k < (u32)VALUES; ++k) dirty |= state.v[k] ^ state.tried[k];
    if (dirty && !now->disk && (now->ms - state.changed_at >= (u32)SETTLE_MS || !on_page)) save();
    if (on_page && now->canvas != 0) paint(now->canvas, where, now->ms);
    u32 totals[TOTALS] = { heard, fired, refused, lost, launched };
    u32 moved = 0;
    for (u32 i = 0; i < (u32)TOTALS; ++i) moved |= totals[i] ^ state.printed[i];
    if (quiet_popup(&state.quiet, now->popup != 0) && moved) say_period(totals);
    if (--state.countdown > 0) return;
    state.countdown = PERIOD_FRAMES;
    u32 sign = quiet_start();
    for (u32 i = 0; i < (u32)TOTALS; ++i) sign = quiet_mix(sign, totals[i]);
    for (u32 k = 0; k < (u32)VALUES; ++k) sign = quiet_mix(sign, state.v[k]);
    if (quiet_period(&state.quiet, sign, moved != 0) && moved) say_period(totals);
}
