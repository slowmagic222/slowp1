// roads.pack: Roads (an electric piano multisampler, "Roads" like Rhodes) as an open-field synth
// engine (levels 9 and 10, base 30+). GPL-3.0-or-later with OP-1 firmware permission.
// Samples: jRhodes (Jeffrey Learman, 1977 Rhodes Mark I Stage 73), CC BY-NC 4.0: see bank/SOURCES.md.
//
// What the owner meets: `roads` in the engine list (shift and T1 on a synth sound). Knobs: tone, sustain, wow,
// ambient; with shift: start, loop in, loop out (TINE's three-point loop editor) and style (OG, SLOW, LOFI: the
// recording played, 0, 12 or 24 semitones up slowed back). A press of the grey knob also steps the style.
// T1 shows a suitcase electric piano on a kids' road rug; a toy car on its lid rolls to above the last note played.
//
// How it runs: each of the unit's six voices owns a one-voice, one-part ms_core (core/) in its working memory, with
// the bank flagged TINE: the TINE voice path, at
// the classic own rate and block (88,200 Hz, 128 frames: the engine's), so nothing is resampled. Pitch comes from the
// block's step, worked out as classic (step x bank rate / 88,200 / the root's step), so glide and bend work; the
// recording is chosen at the note's start. Velocity chooses among TINE's three dynamics when the bank says so
// (roads.msb); roads-rr.msb takes them in turn as classic.
// The bank is a file: roads.msb in the pack's folder `roads` (user/pf/roads/), read once into level 6 working memory
// by the frame function. Until it is there the engine renders silence and the page says why.
//
// Audio thread rules (docs/level-9.md): render calls nothing of the base, no division, no 64-bit multiply, no float,
// no struct copies, and keeps its stack small (scratch lives in the voice's memory).

#define MS_VOICES 1u
#define MS_PARTS 1u
#include "core/ms_core.c"
#include "build_r6e_card.h"
#include "roads_scene.h"

typedef unsigned int u32;

extern "C" const Op1fCard op1f_card_roads OP1F_NAMED("op1f_card_roads");

enum { VALUES = 8, FRAMES = 128, ENGINE_ASK = 0, MEMORY_ASK = 1, PAGE_ASK = 2, FOLDER = 0, FRAME_LEVEL_4 = 40, BLOCK_LEVEL_9 = 52 };
enum { V_TONE = 0, V_SUSTAIN, V_STYLE, V_AMBIENT, V_START, V_LOOP_IN, V_LOOP_OUT, V_WOW };   // v4: style on the third knob, as felt's; V_TONE shows as muffle
static int style_of(int v) { return v < 0 ? 0 : v > 2 ? 2 : v; }
enum { BANK_ROOM = 786432, VOICE_BYTES = 32768, OUT_RATE = 88200 };
static const u32 MAGIC = 0x524f4431u;   // "ROD1": the voice's core was set up by roads_reset
static const uint16_t WIDTH = 19660;    // the stereo the core spreads by: 60 % (keys low left, high right; the wash)

// ---------------------------------------------------------------- shared between the frame function and render

static ms_bank_t bank_store;                       // parsed by the frame function, then only read
static const ms_bank_t* volatile bank_ready;       // published once, never taken back (the file buffer stays)
static volatile unsigned char rr_shared[128];      // round-robin by played note, shared by the six voices (classic ms_rr)
static volatile u32 voice_slot[6], voice_note[6], voice_on[6], voice_begins[6];   // for the page: what each voice plays
static volatile u32 velocity_off;                  // the velocity switch (a red-orange knob press): 1 = every take in turn, velocity unheard
static volatile u32 begin_serial, begin_velocity[6];   // velocity: as render got it, for the log                  // counts the notes begun; voice_begins holds it at each voice's last

// ---------------------------------------------------------------- a voice

struct Voice {
    u32 magic;
    u32 sounding;          // a note began since the voice was handed over
    u32 last_step;         // the step the rate was worked out for
    int note;              // the MIDI note picked at the start
    int32_t left[FRAMES], right[FRAMES];
    ms_core_t core;
};
static_assert(sizeof(Voice) <= (unsigned)VOICE_BYTES, "a voice fits the memory the card asks");

// The step of MIDI note n at 88,200 Hz, a cycle being 2^32 (A4 = 21,426,141, as docs/level-9.md says): TINE's own
// ms_note_inc, the same numbers.
static const u32 STEP[128] = {
    398127u, 421801u, 446882u, 473455u, 501608u, 531436u, 563036u, 596516u, 631987u, 669567u, 709381u, 751563u, 796254u,
    843601u, 893765u, 946911u, 1003217u, 1062871u, 1126073u, 1193033u, 1263974u, 1339134u, 1418763u, 1503127u, 1592507u,
    1687203u, 1787529u, 1893821u, 2006434u, 2125742u, 2252146u, 2386065u, 2527948u, 2678268u, 2837526u, 3006254u,
    3185015u, 3374406u, 3575058u, 3787642u, 4012867u, 4251485u, 4504291u, 4772130u, 5055896u, 5356535u, 5675051u,
    6012507u, 6370030u, 6748811u, 7150117u, 7575285u, 8025735u, 8502970u, 9008582u, 9544261u, 10111792u, 10713070u,
    11350103u, 12025015u, 12740059u, 13497623u, 14300233u, 15150569u, 16051469u, 17005939u, 18017165u, 19088521u,
    20223584u, 21426141u, 22700205u, 24050030u, 25480119u, 26995246u, 28600467u, 30301139u, 32102938u, 34011878u,
    36034330u, 38177043u, 40447168u, 42852281u, 45400411u, 48100060u, 50960238u, 53990491u, 57200933u, 60602278u,
    64205876u, 68023757u, 72068660u, 76354085u, 80894335u, 85704563u, 90800821u, 96200119u, 101920476u, 107980983u,
    114401866u, 121204555u, 128411753u, 136047513u, 144137319u, 152708170u, 161788671u, 171409126u, 181601643u,
    192400238u, 203840952u, 215961966u, 228803732u, 242409110u, 256823506u, 272095026u, 288274639u, 305416341u,
    323577341u, 342818251u, 363203285u, 384800477u, 407681904u, 431923931u, 457607465u, 484818220u, 513647012u,
    544190053u, 576549277u, 610832681u
};

static int note_below(u32 step);
static int note_of(u32 step) {                    // the nearest note
    int lo = note_below(step);
    if (lo < 127 && step > STEP[lo] && step - STEP[lo] > STEP[lo + 1] - step) ++lo;
    return lo;
}

static int note_below(u32 step) {                 // the highest note whose step is at most this one (0 below note 0)
    int lo = 0, hi = 127;
    while (lo < hi) { int mid = (lo + hi + 1) >> 1; if (STEP[mid] <= step) lo = mid; else hi = mid - 1; }
    return lo;
}

// (a * b) / c, whole, for a, b, c below 2^32 and a quotient below 2^32, without 64-bit arithmetic: the product in two
// words, then long division a bit at a time. Once a block at most (when the step moves).
static u32 mul_div(u32 a, u32 b, u32 c) {
    u32 al = a & 65535u, ah = a >> 16, bl = b & 65535u, bh = b >> 16;
    u32 p0 = al * bl, p1 = al * bh, p2 = ah * bl, p3 = ah * bh;
    u32 mid = (p0 >> 16) + (p1 & 65535u) + (p2 & 65535u);
    u32 lo = (p0 & 65535u) | (mid << 16), hi = p3 + (p1 >> 16) + (p2 >> 16) + (mid >> 16);
    u32 q = 0, r = 0;
    for (int i = 63; i >= 0; --i) {
        u32 top = r >> 31;
        r = (r << 1) | ((i >= 32 ? hi >> (i - 32) : lo >> i) & 1u);
        q <<= 1;
        if (top || r >= c) { r -= c; q |= 1u; }
    }
    return q;
}

// The rate (Q16 bank frames an output frame) the classic worked out: inc x (MS_RATE x 65536 / 88200) / the root's inc.
static u32 rate_for_step(const ms_bank_t* b, const ms_zone_t* z, u32 step) {
    u32 k = ms_udiv(b->rate << 16, (u32)OUT_RATE);
    u32 rate = mul_div(step, k, STEP[z->root]);
    if (rate < 256u) rate = 256u;
    if (rate > 262144u) rate = 262144u;
    return rate;
}

static uint16_t q15(int v) {                       // a value of 0..100 as Q15 0..32767
    if (v <= 0) return 0;
    if (v >= 100) return 32767;
    return (uint16_t)(((u32)v * 1342177u) >> 12);
}
static uint16_t loop_in_word(int v) {              // 50 is TINE's 16384: the bank's own loop start
    if (v <= 0) return 0;
    if (v >= 100) return 32767;
    if (v <= 50) return (uint16_t)(((u32)v * 335544u + 512u) >> 10);
    return (uint16_t)(16384u + (((u32)(v - 50) * 335524u + 512u) >> 10));
}

// Output: TINE's voice is already 6 dB down (its classic headroom). +3 dB here, as felt, with a soft knee above 24,000
// and a hard hold at full scale.
static int to_q31(int32_t x) {
    int32_t u = (x * 45) >> 5;
    int32_t a = u < 0 ? -u : u;
    if (a > 24000) a = 24000 + ((a - 24000) >> 2);
    if (a > 32767) a = 32767;
    return (int)((u32)(u < 0 ? -a : a) << 16);
}

static void silence(int* out) { for (u32 k = 0; k < 2u * (u32)FRAMES; ++k) out[k] = 0; }

static void forget(Voice* v) {                     // handed anew: no note, no tail
    ms_voice_t* mv = &v->core.voices[0];
    mv->active = 0; mv->gate = 0;
    for (int ch = 0; ch < 2; ++ch) {
        ms_wash_t* w = &v->core.wash[0][ch];
        for (int i = 0; i < MS_WASH_TAPS; ++i) { w->pos[i] = 0; w->filled[i] = 0; }
        w->wet = 0; w->lp = 0; w->active = 0;
    }
    v->sounding = 0; v->last_step = 0;
}

extern "C" void roads_reset(unsigned slot, unsigned voice, void* state) OP1F_NAMED("roads_reset");
extern "C" void roads_reset(unsigned slot, unsigned voice, void* state) {   // display thread: the big clear
    (void)slot; (void)voice;
    Voice* v = (Voice*)state;
    if (!v) return;
    v->magic = 0;
    ms_core_init(&v->core, (u32)OUT_RATE);        // TINE's own rate: the classic path unchanged
    v->core.parts[0].rr_spread = 0;               // TINE's three recordings are dynamics, not takes: no left/right
    v->core.parts[0].wash_width = 0;              // no stereo diffuser: at 88.2 kHz it cost too much (the unit switched
                                                  // roads off with ambient up); TINE's ambient is its own, in the voice
    forget(v);
    v->note = 60;
    v->magic = MAGIC;
}

extern "C" void roads_render(const Op1fVoiceBlock* now) OP1F_NAMED("roads_render");
extern "C" void roads_render(const Op1fVoiceBlock* now) {
    int* out = now->out;
    Voice* v = (Voice*)now->state;
    const ms_bank_t* b = bank_ready;
    if (now->bytes < (u32)BLOCK_LEVEL_9 || !v || v->magic != MAGIC || !b) { silence(out); return; }
    ms_core_t* c = &v->core;
    ms_part_t* p = &c->parts[0];
    ms_voice_t* mv = &c->voices[0];
    if (now->events & (u32)OP1F_VOICE_FRESH) forget(v);

    const int* value = now->values;
    p->bank = b;
    p->knob[MS_TONE] = q15(value[V_TONE]);
    p->knob[MS_RELEASE] = q15(value[V_SUSTAIN]);
    p->knob[MS_LOFI] = q15(value[V_WOW]);
    p->knob[MS_WASH] = q15(value[V_AMBIENT]);
    p->attack_trim = q15(value[V_START]);
    p->loop_trim_start = loop_in_word(value[V_LOOP_IN]);
    p->loop_trim_end = q15(value[V_LOOP_OUT]);
    p->width = WIDTH;
    p->source_octave = (uint8_t)style_of(value[V_STYLE]);
    p->any_velocity = velocity_off ? 1u : 0u;

    if (now->events & (u32)OP1F_VOICE_BEGINS) {
        int note = note_of(now->step);
        int vel = now->velocity >> 24;
        if (vel < 1) vel = 1; else if (vel > 127) vel = 127;
        p->rr[note] = rr_shared[note];            // TINE turns the counter of the played note
        p->rr[60] = rr_shared[60];                // the classic seeds the lofi hiss from it
        if (ms_note_on(c, 0, note, vel) >= 0) { v->note = note; v->sounding = 1; v->last_step = 0; }
        rr_shared[note] = p->rr[note];
    }
    if (now->events & (u32)OP1F_VOICE_ENDS) ms_note_off(c, 0, v->note);
    {
        u32 vi = now->voice < 6u ? now->voice : 5u;
        if ((now->events & (u32)OP1F_VOICE_BEGINS) && v->sounding) { voice_note[vi] = (u32)v->note; begin_velocity[vi] = (u32)(now->velocity >> 24); begin_serial = begin_serial + 1u; voice_begins[vi] = begin_serial; }
        voice_slot[vi] = now->slot;
        voice_on[vi] = v->sounding && mv->active && mv->gate;
    }

    if (!v->sounding) { silence(out); return; }   // nothing played since the voice was handed over

    if (mv->active && mv->zone < b->zone_count && now->step != v->last_step) {
        mv->rate = rate_for_step(b, &b->zones[mv->zone], now->step);
        v->last_step = now->step;
    }

    for (u32 k = 0; k < (u32)FRAMES; ++k) { v->left[k] = 0; v->right[k] = 0; }
    ms_render(c, v->left, v->right, FRAMES);
    for (u32 k = 0; k < (u32)FRAMES; ++k) {
        out[2u * k] = to_q31(v->left[k]);
        out[2u * k + 1u] = to_q31(v->right[k]);
    }
}

// ---------------------------------------------------------------- the bank file

enum { BANK_WAITING = 0, BANK_LOADED = 1, BANK_MISSING = 2, BANK_BAD = 3, BANK_TOO_BIG = 4, BANK_NO_ROOM = 5 };
static u32 bank_state, disk_was, memory_tries;

static void load_bank(const Op1fFrame* now) {
    if (bank_ready) return;
    u32 disk_closed = disk_was && !now->disk;
    disk_was = now->disk;
    if (now->disk) return;                        // the computer owns the folder
    if (bank_state != BANK_WAITING && !disk_closed) return;   // tried: again only after the disk mode closes
    void* memory = op1f_memory(&op1f_card_roads, MEMORY_ASK);
    if (!memory) {
        if (++memory_tries > 600u && bank_state == BANK_WAITING) {   // some ten seconds: refused, not late
            bank_state = BANK_NO_ROOM;
            op1f_log(2, "roads.cpp", 1, "roads: no working memory for the bank (%u bytes)", (unsigned)BANK_ROOM);
        }
        return;
    }
    int got = op1f_read(&op1f_card_roads, FOLDER, "roads.msb", memory, (unsigned)BANK_ROOM);
    if (got == OP1F_NO_FILE || got == 0) { bank_state = BANK_MISSING; op1f_log(2, "roads.cpp", 1, "roads: no roads.msb in the roads folder"); return; }
    if (got == OP1F_TOO_LARGE) { bank_state = BANK_TOO_BIG; op1f_log(2, "roads.cpp", 1, "roads: roads.msb is over %u bytes", (unsigned)BANK_ROOM); return; }
    if (got < 0) { bank_state = BANK_MISSING; op1f_log(2, "roads.cpp", 1, "roads: reading roads.msb answered %d", got); return; }
    int parsed = ms_bank_parse(&bank_store, (const uint8_t*)memory, (uint32_t)got);
    if (parsed != MS_BANK_OK) { bank_state = BANK_BAD; op1f_log(2, "roads.cpp", 1, "roads: roads.msb is not a bank (%d)", parsed); return; }
    if (!(bank_store.flags & MS_BANK_TINE)) op1f_log(2, "roads.cpp", 1, "roads: roads.msb has no TINE flag: it plays with FELT's character at 88.2 kHz");
    bank_state = BANK_LOADED;
    bank_ready = &bank_store;
    op1f_log(3, "roads.cpp", 1, "roads: bank '%s' loaded, %d bytes, %u zones", bank_store.name, got, (unsigned)bank_store.zone_count);
}

// ---------------------------------------------------------------- the page

static const unsigned COLOURS[4] = {OP1F_COLOUR(0x4c, 0x7d, 0xa8), OP1F_COLOUR(0x8a, 0x65, 0x3c), OP1F_COLOUR(0xcd, 0xd0, 0xc2), OP1F_COLOUR(0xe5, 0x28, 0x17)};
static const char NAMES_UP[4][8] = {"muffle", "sustain", "style", "ambient"};
static const char NAMES_SHIFT[4][9] = {"start", "loop in", "loop out", "wow"};

// The values in the firmware's own letters: placeholders of the pack's picture roads.svg (roads.svg.make), the
// title roads_t0..2 (coloured by the style), the names roads_l0..3 in the knobs' colours and the numbers roads_n0..3,
// set to the right. The scene is painted over that page with the names' column (and the title's corner) left out.
// Where the picture cannot be had, the base's blank page and its small font.
enum { KEY = 0x0821, COLUMN_X = 340, TITLE_W = 90, TITLE_H = 32, FADE_X = 340, FADE_Y = 8, FADE_W = 140, FADE_H = 210 };
static const char TEXT_TITLE[3][9] = {"roads_t0", "roads_t1", "roads_t2"};
static int title_shown = -1;
static const char TEXT_NAME[4][9] = {"roads_l0", "roads_l1", "roads_l2", "roads_l3"};
static const char TEXT_NUMBER[4][9] = {"roads_n0", "roads_n1", "roads_n2", "roads_n3"};
static const char TEXT_VELOCITY[] = "roads_v";   // VELOCITY ON / OFF, under the values, while they show
static void* document;
static int shown_value(const int* value, u32 i, u32 shift) {   // style shows as 1 to 3
    if (shift) return value[i + 4u];
    return i == 2u ? style_of(value[V_STYLE]) + 1 : value[i];
}
static u32 document_asked, texts_hidden;
static unsigned char fade_cover[FADE_W * FADE_H];
static int fade_level = -1;
static const unsigned LIGHT_GREY = OP1F_COLOUR(0xcd, 0xd0, 0xc2);

static volatile u32 shown_where, shown_how;
static struct { u32 stood, refusal_said; } page;

extern "C" void roads_shown(unsigned where, unsigned how) OP1F_NAMED("roads_shown");
extern "C" void roads_shown(unsigned where, unsigned how) { shown_where = where; shown_how = how; }

static void put_number(char* out, int n) {
    u32 at = 0;
    if (n < 0) { out[at++] = '-'; n = -n; }
    char digits[12];
    u32 d = 0, u = (u32)n;
    do { u32 tens = 0; while (u >= 10u) { u -= 10u; ++tens; } digits[d++] = (char)('0' + u); u = tens; } while (u && d < 11u);
    while (d) out[at++] = digits[--d];
    out[at] = 0;
}

static void centred(void* canvas, int middle, int y, const char* text, unsigned colour, int scale) {
    int n = 0;
    while (text[n]) ++n;
    op1f_paint_text(canvas, middle - (((6 * n - 1) * scale) >> 1), y, text, colour, scale);
}

// ---- the scene: turned a little every frame, drawn every second frame into the page's memory, shown every frame
static scene::State look;
static u32 begin_seen, frames_up, velocity_logs, page_was_up;

static int white_key(u32 note) {                 // the note's white key (or the white key under a black one), 0..14
    static const unsigned char W12[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    u32 octave = 0, n = note;
    while (n >= 12u) { n -= 12u; ++octave; }
    int k = (int)(octave * 7u + W12[n]) - 28;   // C4 (60) on the eighth key: the span C3 to C5, wrapped
    while (k < 0) k += 15;
    while (k >= 15) k -= 15;
    return k;
}

// the values fade out when nothing has moved for a while: shown 2.5 s after the last change, then gone over half a second
enum { SHOW_MS = 2500, FADE_MS = 500 };
static int seen_values[VALUES + 3];
static volatile u32 knob_turns;   // v9: every turn on our page, taken or not: shows the values even at a knob's end
static u32 seen_any, changed_ms, turns_seen;

static void paint(void* canvas, u32 where, u32 how, u32 shift, u32 ms) {
    uint16_t* px = (uint16_t*)op1f_memory(&op1f_card_roads, PAGE_ASK);
    int value[VALUES];
    for (u32 i = 0; i < (u32)VALUES; ++i) if (op1f_param_heard(&op1f_card_roads, ENGINE_ASK, where, i, &value[i]) != 0) value[i] = 0;
    if (px) {
        look.time += 1u;
        look.angle = scene::swivel(look.time);   // isometric to the front and back
        look.tone = value[V_TONE]; look.sustain = value[V_SUSTAIN]; look.wow = value[V_WOW]; look.ambient = value[V_AMBIENT];
        look.style = style_of(value[V_STYLE]);
        look.start = value[V_START]; look.loop_in = value[V_LOOP_IN]; look.loop_out = value[V_LOOP_OUT];
        look.shift = shift != 0;
        look.keys = 0; look.sounding = 0;
        u32 latest = begin_seen;
        for (u32 vi = 0; vi < 6u; ++vi) {
            if (voice_slot[vi] != where) continue;
            if (voice_on[vi]) { look.keys |= 1u << white_key(voice_note[vi]); ++look.sounding; }
            u32 b = voice_begins[vi];
            if ((int)(b - latest) > 0) {         // the car's next stop: the newest note
                latest = b; look.target = white_key(voice_note[vi]);
                if (velocity_logs < 64u) {          // what the unit hands the engine, for a look with the unit's velocity off
                    ++velocity_logs;
                    op1f_log(3, "roads.cpp", 1, "roads: note %u velocity %u", (unsigned)voice_note[vi], (unsigned)begin_velocity[vi]);
                }
            }
        }
        begin_seen = latest;
        look.notes = begin_serial;               // v5: the notes played wake the town
        scene::step(look);                       // the car rolls on every frame
        if ((frames_up++ & 1u) == 0u) {
            scene::render(px, look);
            if (document) {                      // the names' column and the title's corner left to the page
                for (int y = 0; y < scene::H; ++y) {
                    uint16_t* row = px + y * scene::W;
                    for (int x = COLUMN_X; x < scene::W; ++x) row[x] = (uint16_t)KEY;
                    if (y < TITLE_H) for (int x = 0; x < TITLE_W; ++x) row[x] = (uint16_t)KEY;
                }
            }
        }
        op1f_paint_block(canvas, 0, 0, scene::W, scene::H, px, scene::W, document ? (int)KEY : -1);
    }
    int now_seen[VALUES + 3];
    // v8: what shows the values is a knob turned: the values as set, not as heard (the heard ones step between the set
    // value and the played one when notes start and stop, which popped the values up untouched)
    for (u32 i = 0; i < (u32)VALUES; ++i) { int set = 0; now_seen[i] = op1f_param_get(&op1f_card_roads, ENGINE_ASK, where, i, &set) == 0 ? set : value[i]; }
    now_seen[VALUES] = shift != 0; now_seen[VALUES + 1] = (int)velocity_off;   // a switch shows the values now_seen[VALUES + 2] = (int)where;
    bool moved = !seen_any;
    for (u32 i = 0; i < (u32)VALUES + 3u; ++i) if (now_seen[i] != seen_values[i]) { moved = true; seen_values[i] = now_seen[i]; }
    if (knob_turns != turns_seen) { turns_seen = knob_turns; moved = true; }   // a turn at a value's end changes nothing
    if (moved) { seen_any = 1; changed_ms = ms; }
    u32 idle = ms - changed_ms;
    int shown = idle < (u32)SHOW_MS ? 256 : idle < (u32)(SHOW_MS + FADE_MS) ? 256 - (int)((idle - (u32)SHOW_MS) * 256u / (u32)FADE_MS) : 0;
    char text[12];
    if (document) {
        int style = style_of(value[V_STYLE]);
        for (u32 k = 0; k < 3u; ++k) op1f_text_set(document, TEXT_TITLE[k], "roads", 0, OP1F_LEFT);
        if (style != title_shown) {
            u32 all = 1;
            for (u32 k = 0; k < 3u; ++k) { void* t = op1f_text_node(document, TEXT_TITLE[k]); if (t) op1f_show(t, (int)k == style); else all = 0; }
            if (all) title_shown = style;
        }
        for (u32 i = 0; i < 4u; ++i) {
            op1f_text_set(document, TEXT_NAME[i], shift ? NAMES_SHIFT[i] : NAMES_UP[i], 0, OP1F_RIGHT);
            put_number(text, shown_value(value, i, shift));
            op1f_text_set(document, TEXT_NUMBER[i], text, 0, OP1F_RIGHT);
        }
        op1f_text_set(document, TEXT_VELOCITY, velocity_off ? "velocity off" : "velocity on", 0, OP1F_RIGHT);
        u32 hide = shown == 0 ? 1u : 0u;
        if (hide != texts_hidden) {
            texts_hidden = hide;
            for (u32 i = 0; i < 4u; ++i) {
                void* a = op1f_text_node(document, TEXT_NAME[i]);
                void* b = op1f_text_node(document, TEXT_NUMBER[i]);
                if (a) op1f_show(a, hide ? 0 : 1);
                if (b) op1f_show(b, hide ? 0 : 1);
            }
            void* v = op1f_text_node(document, TEXT_VELOCITY);
            if (v) op1f_show(v, hide ? 0 : 1);
        }
        if (shown > 0 && shown < 256) {
            int veil = 255 - shown;
            if (veil > 255) veil = 255;
            if (veil != fade_level) { fade_level = veil; for (u32 k = 0; k < (u32)(FADE_W * FADE_H); ++k) fade_cover[k] = (unsigned char)veil; }
            op1f_paint_mask(canvas, FADE_X, FADE_Y, FADE_W, FADE_H, fade_cover, FADE_W, 0u);
        }
    } else {
        op1f_paint_text(canvas, 8, 8, "roads", LIGHT_GREY, 1);
        for (u32 i = 0; shown > 0 && i < 4u; ++i) {
            int y = 20 + (int)i * 48;
            op1f_paint_text(canvas, 400, y, shift ? NAMES_SHIFT[i] : NAMES_UP[i], scene::mix(0, (uint16_t)COLOURS[i], shown), 1);
            put_number(text, shown_value(value, i, shift));
            op1f_paint_text(canvas, 400, y + 12, text, scene::mix(0, (uint16_t)LIGHT_GREY, shown), 3);
        }
        if (shown > 0) op1f_paint_text(canvas, 400, 210, velocity_off ? "velocity off" : "velocity on", scene::mix(0, (uint16_t)COLOURS[3], shown), 1);
    }
    const char* status = 0;
    switch (bank_state) {
        case BANK_MISSING: status = "put roads.msb in the roads folder"; break;
        case BANK_BAD: status = "roads.msb is not a bank"; break;
        case BANK_TOO_BIG: status = "roads.msb is too big"; break;
        case BANK_NO_ROOM: status = "no room for the bank"; break;
        default: break;
    }
    if (!px) status = status ? status : "no room for the page";
    if (status) centred(canvas, 190, 206, status, COLOURS[3], 1);
    if (how & (u32)OP1F_SHOWN_OFF) centred(canvas, 190, 192, "switched off", COLOURS[3], 1);
}

// Style (the third knob, as felt's): a turn steps it once a click (no long spin), and a press of the grey knob steps it
// round, OG, SLOW, LOFI, OG (as a white press). A press of the red-orange knob is the velocity switch. The input
// function only counts; the frame function sets the value and keeps the switch in the file roads.cfg (folder roads).
enum { INPUT_LEVEL_2 = 36, KNOB_STYLE = 2, KEY_STYLE_CLICK = 56, KEY_VELOCITY_CLICK = 57 };
static volatile int style_turns, style_presses, velocity_presses;
extern "C" int roads_input(const Op1fInput* e) OP1F_NAMED("roads_input");
extern "C" int roads_input(const Op1fInput* e) {
    if (e->bytes < (u32)INPUT_LEVEL_2) return 0;
    if (e->what == (u32)OP1F_UP) return (e->key == (u32)KEY_STYLE_CLICK || e->key == (u32)KEY_VELOCITY_CLICK) && e->mine;   // the up of a click we took
    if (!e->mine) return 0;
    if (e->what == (u32)OP1F_TURN) knob_turns = knob_turns + 1u;
    if (e->what == (u32)OP1F_TURN && e->key == (u32)KNOB_STYLE && !e->shift) {
        if (e->turn > 0) style_turns = style_turns + 1; else if (e->turn < 0) style_turns = style_turns - 1;
        return 1;
    }
    if (e->what == (u32)OP1F_DOWN && e->key == (u32)KEY_STYLE_CLICK) { style_presses = style_presses + 1; return 1; }
    if (e->what == (u32)OP1F_DOWN && e->key == (u32)KEY_VELOCITY_CLICK) { velocity_presses = velocity_presses + 1; return 1; }
    return 0;
}
static void apply_style(u32 where) {
    int turns = style_turns, presses = style_presses;
    if (!turns && !presses) return;
    style_turns = style_turns - turns; style_presses = style_presses - presses;
    int v = 0;
    if (op1f_param_get(&op1f_card_roads, ENGINE_ASK, where, V_STYLE, &v) != 0) return;
    v = style_of(v) + turns;
    v = v < 0 ? 0 : v > 2 ? 2 : v;
    v = (v + presses) % 3;
    op1f_param_set(&op1f_card_roads, ENGINE_ASK, where, V_STYLE, v);
}

// the velocity switch, kept for all roads sounds in roads.cfg: "RDV1" and a word, 0 = on (listens), 1 = off
static const u32 CFG_MAGIC = 0x31564452u;
static u32 cfg_read, cfg_to_write;
static void velocity_switch(const Op1fFrame* now) {
    u32 cfg[2];
    if (!cfg_read && !now->disk) {
        cfg_read = 1;
        int got = op1f_read(&op1f_card_roads, FOLDER, "roads.cfg", cfg, sizeof cfg);
        if (got == (int)sizeof cfg && cfg[0] == CFG_MAGIC) velocity_off = cfg[1] ? 1u : 0u;
    }
    int presses = velocity_presses;
    if (presses) {
        velocity_presses = velocity_presses - presses;
        if (presses & 1) { velocity_off = velocity_off ? 0u : 1u; cfg_to_write = 1; }
        op1f_log(3, "roads.cpp", 1, "roads: velocity %s", velocity_off ? "off: every take in turn" : "on");
    }
    if (cfg_to_write && !now->disk) {
        cfg[0] = CFG_MAGIC; cfg[1] = velocity_off;
        int answer = op1f_write(&op1f_card_roads, FOLDER, "roads.cfg", cfg, sizeof cfg);
        if (answer != OP1F_NOT_NOW) {
            cfg_to_write = 0;
            if (answer != (int)sizeof cfg) op1f_log(2, "roads.cpp", 1, "roads: writing roads.cfg answered %d", answer);
        }
    }
}

extern "C" void roads_frame(const Op1fFrame* now) OP1F_NAMED("roads_frame");
extern "C" void roads_frame(const Op1fFrame* now) {
    if (now->bytes < (u32)FRAME_LEVEL_4) return;
    load_bank(now);
    velocity_switch(now);
    if (!document_asked) {
        document_asked = 1;
        document = op1f_picture(&op1f_card_roads, "roads.svg");
        if (!document) op1f_log(2, "roads.cpp", 1, "roads: no picture roads.svg; the values in the base's small font");
    }
    if (!page.stood) {
        if (op1f_page_stand_in(&op1f_card_roads, ENGINE_ASK, document)) page.stood = 1;
        else if (!page.refusal_said) {
            page.refusal_said = 1;
            op1f_log(2, "roads.cpp", 1, "roads: the base did not take its page; T1 shows the base's blank page with its name");
        }
    }
    u32 how = shown_how, where = shown_where;
    apply_style(where & 7u);
    u32 up = now->page == &op1f_card_roads && now->canvas != 0 && (how & (u32)OP1F_SHOWN_UP);
    if (up && !page_was_up) {                    // v9: back on the page: the world starts from nothing again
        look.life = 0;
        look.notes = look.notes_seen = begin_serial;
        turns_seen = knob_turns;
    }
    page_was_up = up;
    if (up) paint(now->canvas, where, how, now->shift, now->ms);
}
