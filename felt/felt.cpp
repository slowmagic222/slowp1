// felt.pack: FELT (a felt piano multisampler) as an open-field synth engine (levels 9 and 10, base 30+).
// GPL-3.0-or-later with OP-1 firmware permission. Samples: CC0 (see the bank's SOURCES.md).
//
// What the owner meets: `felt` in the engine list (shift and T1 on a synth sound). Knobs: felt, decay, style, ambient;
// with shift: start, loop in, loop out (the classic three-marker loop editor) and width (stereo). T1 shows the values.
//
// How it runs: each of the unit's six voices owns a one-voice, one-part ms_core (core/, the FELT sample core) in its working memory. It renders the classic path at 44,100 Hz, 64 frames,
// exactly as classic, and doubles it to the engine's 88,200 Hz, 128 frames by linear interpolation. Pitch comes from
// the block's step, so glide and bend work; the zone (and its round-robin take) is chosen at the note's start.
// The bank is a file: felt.msb in the pack's folder `felt` (user/pf/felt/), read once into level 6 working memory by
// the frame function. Until it is there the engine renders silence and the page says why.
//
// Audio thread rules (docs/level-9.md): render calls nothing of the base, no division, no 64-bit multiply, no float,
// no struct copies, and keeps its stack small (scratch lives in the voice's memory).

#define MS_VOICES 1u
#define MS_PARTS 1u
#include "core/ms_core.c"
#include "build_r6e_card.h"
#include "felt_scene.h"

typedef unsigned int u32;

extern "C" const Op1fCard op1f_card_felt OP1F_NAMED("op1f_card_felt");

enum { VALUES = 8, FRAMES = 128, HALF = 64, ENGINE_ASK = 0, MEMORY_ASK = 1, PAGE_ASK = 2, FLOOR_ASK = 3, BASE_ASK = 4, FOLDER = 0, FRAME_LEVEL_4 = 40, BLOCK_LEVEL_9 = 52 };
enum { V_TONE = 0, V_RELEASE, V_STYLE, V_WASH, V_START, V_LOOP_IN, V_LOOP_OUT, V_WIDTH };

// The third knob, style, picks the piano: 0 the plain felt piano (brown), 1 the classic lofi stretch (blue: the recording
// an octave up played slowed back), 2 the classic second stretch (grey: two octaves). The classic lofi grit is gone: always 0.
static int style_of(int v) { return v < 0 ? 0 : v > 2 ? 2 : v; }
enum { BANK_ROOM = 786432, VOICE_BYTES = 32768 };
static const u32 MAGIC = 0x464c5431u;   // "FLT1": the voice's core was set up by felt_reset

// ---------------------------------------------------------------- shared between the frame function and render

static ms_bank_t bank_store;                       // parsed by the frame function, then only read
static const ms_bank_t* volatile bank_ready;       // published once, never taken back (the file buffer stays)
static volatile unsigned char rr_shared[128];      // round-robin by note, shared by the six voices (one renders at a time)
static volatile u32 voice_slot[6], voice_note[6], voice_on[6], voice_begins[6], voice_velocity[6];   // for the page: what each voice plays
static volatile u32 velocity_off;                  // the velocity switch (a red-orange knob press): 1 = every note at one level
static volatile u32 knob_turns;                    // any knob turned on our page: shows the values even at a limit

// v21: velocity back as in v17 (the core's vel_amount); the output gain stays x1. The log gives each note's velocity as
// the unit hands it, to measure the unit's own curve.

// ---------------------------------------------------------------- a voice

struct Voice {
    u32 magic;
    u32 sounding;          // a note began since the voice was handed over
    u32 last_step;         // the step the rate was worked out for
    int note;              // the MIDI note picked at the start
    int prev_l, prev_r;    // the last 44.1 kHz frame, for the doubling
    int vel_gain;          // Q12: the velocity floor for the note sounding
    int32_t left[HALF], right[HALF];
    ms_core_t core;
};
static_assert(sizeof(Voice) <= (unsigned)VOICE_BYTES, "a voice fits the memory the card asks");

// The step of MIDI note n at 88,200 Hz, a cycle being 2^32 (A4 = 21,426,141, as docs/level-9.md says).
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

static u32 frac16(u32 a, u32 b) {                  // a * 2^16 / b for a < b < 2^31, by long division
    u32 q = 0, r = a;
    for (int i = 0; i < 16; ++i) { r <<= 1; q <<= 1; if (r >= b) { r -= b; q |= 1u; } }
    return q;
}

// Rate (Q16 bank frames per 44.1 kHz frame) for the zone at the step's exact pitch: the rate of the note at or below
// (the classic table) times the step's distance above that note. Exactly the classic rate when the step is a note's own.
static u32 rate_for_step(const ms_core_t* c, const ms_bank_t* b, const ms_zone_t* z, u32 step) {
    int n = note_below(step);
    u32 t = STEP[n];
    u32 fine = step >= t ? 65536u + frac16(step - t, t) : 65536u - frac16(t - step, t);
    u32 rate = fine == 65536u ? ms_rate_for(c, b, z, n) : mul_shift(ms_rate_for(c, b, z, n), fine, 16);
    if (rate < 256u) rate = 256u;
    if (rate > 262144u) rate = 262144u;
    return rate;
}

static uint16_t q15(int v) {                       // a value of 0..100 as Q15 0..32767
    if (v <= 0) return 0;
    if (v >= 100) return 32767;
    return (uint16_t)(((u32)v * 1342177u) >> 12);
}

// Output gain: FELT's voice peaks near -3 dBFS with an RMS near -16 dBFS. +8 dB (x2.5) overdrove on the unit, so
// +3 dB (x1.41), with the core's own soft knee above 24,000 and a hard hold at full scale.
static int to_q31(int32_t x) {                     // the core's 16-bit scale, raised and held, to Q1.31
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
    v->sounding = 0; v->last_step = 0; v->prev_l = 0; v->prev_r = 0;
}

extern "C" void felt_reset(unsigned slot, unsigned voice, void* state) OP1F_NAMED("felt_reset");
extern "C" void felt_reset(unsigned slot, unsigned voice, void* state) {   // display thread: the big clear
    (void)slot; (void)voice;
    Voice* v = (Voice*)state;
    if (!v) return;
    v->magic = 0;
    ms_core_init(&v->core, 44100u);               // FELT's own rate: the classic path unchanged
    v->core.parts[0].wash_width = 0;              // no stereo diffuser: at 88.2 kHz it cost too much (the unit switched
                                                  // felt off with ambient up); the classic own mono wash stays, in the voice
    forget(v);
    v->note = 60;
    v->magic = MAGIC;
}

extern "C" void felt_render(const Op1fVoiceBlock* now) OP1F_NAMED("felt_render");
extern "C" void felt_render(const Op1fVoiceBlock* now) {
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
    p->knob[MS_RELEASE] = q15(value[V_RELEASE]);
    int style = style_of(value[V_STYLE]);
    p->knob[MS_LOFI] = 0;
    p->knob[MS_WASH] = q15(value[V_WASH]);
    p->attack_trim = q15(value[V_START]);
    p->loop_trim_start = q15(value[V_LOOP_IN]);
    p->loop_trim_end = q15(value[V_LOOP_OUT]);
    p->width = q15(value[V_WIDTH]);
    p->source_octave = (uint8_t)style;
    p->vel_amount = velocity_off ? 0 : 24576;      // as v17: on, the core's own velocity (soft notes down to a quarter); off, all alike

    if (now->events & (u32)OP1F_VOICE_BEGINS) {
        int note = note_of(now->step);
        int vel = now->velocity >> 24;
        if (vel < 1) vel = 1; else if (vel > 127) vel = 127;
        int source = note + 12 * (int)p->source_octave;   // the recording the core picks the take for
        if (source > 127) source = 127;
        p->rr[source] = rr_shared[source];
        p->rr[60] = rr_shared[60];                // the classic seeds the lofi hiss from it
        if (velocity_off) vel = 64;               // off: one level, and only the soft recordings (a bank with layers)
        if (ms_note_on(c, 0, note, vel) >= 0) { v->note = note; v->sounding = 1; v->last_step = 0; v->vel_gain = 4096; }
        rr_shared[source] = p->rr[source];
    }
    if (now->events & (u32)OP1F_VOICE_ENDS) ms_note_off(c, 0, v->note);
    {
        u32 vi = now->voice < 6u ? now->voice : 5u;
        if (now->events & (u32)OP1F_VOICE_BEGINS) { voice_note[vi] = (u32)v->note; voice_velocity[vi] = (u32)(now->velocity >> 24); voice_begins[vi] = voice_begins[vi] + 1u; }
        voice_slot[vi] = now->slot;
        voice_on[vi] = v->sounding && mv->active && mv->gate;
    }

    if (!v->sounding) { silence(out); return; }   // nothing played since the voice was handed over

    if (mv->active && mv->zone < b->zone_count && now->step != v->last_step) {
        mv->rate = rate_for_step(c, b, &b->zones[mv->zone], now->step);
        v->last_step = now->step;
    }

    for (u32 k = 0; k < (u32)HALF; ++k) { v->left[k] = 0; v->right[k] = 0; }
    ms_render(c, v->left, v->right, HALF);

    int pl = v->prev_l, pr = v->prev_r;            // 44.1 -> 88.2 kHz: the midpoint, then the frame
    int g = v->vel_gain ? v->vel_gain : 4096;
    for (u32 k = 0; k < (u32)HALF; ++k) {
        int l = (v->left[k] * (g >> 2)) >> 10, r = (v->right[k] * (g >> 2)) >> 10;
        out[4u * k] = to_q31((pl + l) >> 1);
        out[4u * k + 1u] = to_q31((pr + r) >> 1);
        out[4u * k + 2u] = to_q31(l);
        out[4u * k + 3u] = to_q31(r);
        pl = l; pr = r;
    }
    v->prev_l = pl; v->prev_r = pr;
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
    void* memory = op1f_memory(&op1f_card_felt, MEMORY_ASK);
    if (!memory) {
        if (++memory_tries > 600u && bank_state == BANK_WAITING) {   // some ten seconds: refused, not late
            bank_state = BANK_NO_ROOM;
            op1f_log(2, "felt.cpp", 1, "felt: no working memory for the bank (%u bytes)", (unsigned)BANK_ROOM);
        }
        return;
    }
    int got = op1f_read(&op1f_card_felt, FOLDER, "felt.msb", memory, (unsigned)BANK_ROOM);
    if (got == OP1F_NO_FILE || got == 0) { bank_state = BANK_MISSING; op1f_log(2, "felt.cpp", 1, "felt: no felt.msb in the felt folder"); return; }
    if (got == OP1F_TOO_LARGE) { bank_state = BANK_TOO_BIG; op1f_log(2, "felt.cpp", 1, "felt: felt.msb is over %u bytes", (unsigned)BANK_ROOM); return; }
    if (got < 0) { bank_state = BANK_MISSING; op1f_log(2, "felt.cpp", 1, "felt: reading felt.msb answered %d", got); return; }
    int parsed = ms_bank_parse(&bank_store, (const uint8_t*)memory, (uint32_t)got);
    if (parsed != MS_BANK_OK) { bank_state = BANK_BAD; op1f_log(2, "felt.cpp", 1, "felt: felt.msb is not a bank (%d)", parsed); return; }
    bank_state = BANK_LOADED;
    bank_ready = &bank_store;
    op1f_log(3, "felt.cpp", 1, "felt: bank '%s' loaded, %d bytes, %u zones", bank_store.name, got, (unsigned)bank_store.zone_count);
}

// ---------------------------------------------------------------- the page

static const unsigned COLOURS[4] = {OP1F_COLOUR(0x4c, 0x7d, 0xa8), OP1F_COLOUR(0x8a, 0x65, 0x3c), OP1F_COLOUR(0xcd, 0xd0, 0xc2), OP1F_COLOUR(0xe5, 0x28, 0x17)};
static const char NAMES_UP[4][8] = {"felt", "decay", "style", "ambient"};
static const char NAMES_SHIFT[4][9] = {"start", "loop in", "loop out", "width"};

// The values in the firmware's own letters: placeholders of the pack's picture felt.svg (felt.svg.make), the title
// felt_title, the names felt_l0..3 in the knobs' colours and the numbers felt_n0..3, set to the right. The scene is
// painted over that page with the names' column (and the title's corner) left out, so the letters show. Where the
// picture cannot be had, the base's blank page and its small font, as before.
enum { KEY = 0x0821, COLUMN_X = 340, TITLE_W = 90, TITLE_H = 32, FADE_X = 340, FADE_Y = 8, FADE_W = 140, FADE_H = 210 };
static const char TEXT_TITLE[3][8] = {"felt_t0", "felt_t1", "felt_t2"};   // brown, blue, grey: by the style
static int title_shown = -1;
static const char TEXT_NAME[4][8] = {"felt_l0", "felt_l1", "felt_l2", "felt_l3"};
static const char TEXT_NUMBER[4][8] = {"felt_n0", "felt_n1", "felt_n2", "felt_n3"};
static const char TEXT_VELOCITY[] = "felt_v";     // VELOCITY ON / OFF, under the values, while they show
static void* document;
static int shown_value(const int* value, u32 i, u32 shift) {   // style shows as 1 to 3
    if (shift) return value[i + 4u];
    return i == (u32)V_STYLE ? style_of(value[i]) + 1 : value[i];
}
static u32 document_asked, texts_hidden;
static unsigned char fade_cover[FADE_W * FADE_H];
static int fade_level = -1;
static const unsigned LIGHT_GREY = OP1F_COLOUR(0xcd, 0xd0, 0xc2);

static volatile u32 shown_where, shown_how;
static struct { u32 stood, refusal_said; } page;

extern "C" void felt_shown(unsigned where, unsigned how) OP1F_NAMED("felt_shown");
extern "C" void felt_shown(unsigned where, unsigned how) { shown_where = where; shown_how = how; }

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
static u32 begins_seen[6], frames_up, life, velocity_logs, garden, garden_quiet, seen_frame_ms, seen_where;   // life: 0..65535, the notes' liveliness

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
static int seen_values[VALUES + 4];
static u32 seen_any, changed_ms;

static void paint(void* canvas, u32 where, u32 how, u32 shift, u32 ms) {
    uint16_t* px = (uint16_t*)op1f_memory(&op1f_card_felt, PAGE_ASK);
    int value[VALUES];
    for (u32 i = 0; i < (u32)VALUES; ++i) if (op1f_param_heard(&op1f_card_felt, ENGINE_ASK, where, i, &value[i]) != 0) value[i] = 0;
    if (px) {
        // back on felt's page after being away (another page, another sound): the scene starts again, just the piano,
        // with new flower spots
        if (seen_frame_ms && (ms - seen_frame_ms > 400u || where != seen_where)) {
            life = 0; garden = 0; garden_quiet = 0; look.seed = 0;
            for (u32 i = 0; i < 12u; ++i) look.pulse[i] = 0;
        }
        seen_frame_ms = ms ? ms : 1u; seen_where = where;
        look.time += 1u;
        {   // the view holds still, and glides to the style's own angle when the style changes: still, the rug and
            // the piano are kept and only what moves is drawn
            static const int VIEW[3] = {50176, 53248, 56320};   // between isometric (49152) and the front (57344)
            int target = VIEW[style_of(value[V_STYLE])], at = look.angle ? (int)look.angle : target;
            int gap = target - at, step = gap / 10;
            if (gap && step == 0) step = gap > 0 ? 1 : -1;
            if (step > 0 && step < 64 && gap >= 64) step = 64;
            if (step < 0 && step > -64 && gap <= -64) step = -64;
            look.angle = (u32)(at + step);
        }
        look.tone = value[V_TONE]; look.release = value[V_RELEASE]; look.lofi = 0; look.wash = value[V_WASH];
        look.stretch = style_of(value[V_STYLE]);
        look.start = value[V_START]; look.loop_in = value[V_LOOP_IN]; look.loop_out = value[V_LOOP_OUT]; look.width = value[V_WIDTH];
        look.shift = shift != 0;
        look.keys = 0; look.sounding = 0;
        for (u32 vi = 0; vi < 6u; ++vi) {
            if (voice_slot[vi] != where) continue;
            if (voice_on[vi]) { look.keys |= 1u << white_key(voice_note[vi]); ++look.sounding; }
            u32 b = voice_begins[vi];
            if (b != begins_seen[vi]) {
                begins_seen[vi] = b;
                if (velocity_logs < 64u) {          // what the unit hands the engine: to measure its velocity curve
                    ++velocity_logs;
                    op1f_log(3, "felt.cpp", 1, "felt: note %u velocity %u", (unsigned)voice_note[vi], (unsigned)voice_velocity[vi]);
                }
                u32 h = scene::hash(voice_note[vi] * 2654435761u + b);
                look.pulse[h % 12u] = 255;
                if (life > 24000u) look.pulse[(h >> 8) % 12u] = 255;   // a lively world: two squares a note
                life = life + 2400u > 65535u ? 65535u : life + 2400u;   // each note wakes the world a little
                garden = garden + 16u > 64u * 64u ? 64u * 64u : garden + 16u;   // and grows the flower opening: four notes a flower
                garden_quiet = 0;
            }
        }
        life -= life >> 10;                      // and it falls back asleep: about half in twelve seconds without notes
        if (++garden_quiet > 180u && garden && (garden_quiet & 1u)) garden = garden - 1u;   // after some silence the flowers sink, last first
        look.garden = (int)garden;
        look.life = (int)(life >> 8);
        if (!look.seed) look.seed = scene::hash(ms * 2654435761u + 1u) | 1u;   // the flowers' spots: new each start
        int fade = 10 - look.life / 32;          // the quilt's squares stay lit longer the livelier it is
        for (u32 i = 0; i < 12u; ++i) look.pulse[i] = look.pulse[i] > fade ? (unsigned char)(look.pulse[i] - fade) : 0;
        if ((frames_up++ & 1u) == 0u) {
            scene::render(px, look, (uint16_t*)op1f_memory(&op1f_card_felt, FLOOR_ASK), (uint16_t*)op1f_memory(&op1f_card_felt, BASE_ASK));
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
    // the four values, right, in the knobs' colours; with shift held, the four shift values. Any change (a value, shift,
    // the stretch, another sound) shows them; they fade when nothing has moved
    int now_seen[VALUES + 4];
    // the set values, not the heard ones: those step between the knob's setting and the playing value as notes start
    // and stop, and would bring the values up untouched
    for (u32 i = 0; i < (u32)VALUES; ++i) { int set = 0; now_seen[i] = op1f_param_get(&op1f_card_felt, ENGINE_ASK, where, i, &set) == 0 ? set : value[i]; }
    now_seen[VALUES] = shift != 0; now_seen[VALUES + 1] = (int)velocity_off; now_seen[VALUES + 2] = (int)where;
    bool moved = !seen_any;
    now_seen[VALUES + 3] = (int)knob_turns;   // a turn at 0 or 100 changes nothing, but shows them too
    for (u32 i = 0; i < (u32)VALUES + 4u; ++i) if (now_seen[i] != seen_values[i]) { moved = true; seen_values[i] = now_seen[i]; }
    if (moved) { seen_any = 1; changed_ms = ms; }
    u32 idle = ms - changed_ms;
    int shown = idle < (u32)SHOW_MS ? 256 : idle < (u32)(SHOW_MS + FADE_MS) ? 256 - (int)((idle - (u32)SHOW_MS) * 256u / (u32)FADE_MS) : 0;
    char text[12];
    if (document) {
        int style = style_of(value[V_STYLE]);
        for (u32 k = 0; k < 3u; ++k) op1f_text_set(document, TEXT_TITLE[k], "felt", 0, OP1F_LEFT);
        if (style != title_shown) {              // the name in the style's colour
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
        u32 hide = shown == 0 ? 1u : 0u;         // gone: hidden; fading: a dark veil over the column
        if (hide != texts_hidden) {
            texts_hidden = hide;
            for (u32 i = 0; i < 4u; ++i) {
                void* a = op1f_text_node(document, TEXT_NAME[i]);
                void* b = op1f_text_node(document, TEXT_NUMBER[i]);
                if (a) op1f_show(a, hide ? 0 : 1);
                if (b) op1f_show(b, hide ? 0 : 1);
            }
            void* vt = op1f_text_node(document, TEXT_VELOCITY);
            if (vt) op1f_show(vt, hide ? 0 : 1);
        }
        if (shown > 0 && shown < 256) {
            int veil = 255 - shown;
            if (veil > 255) veil = 255;
            if (veil != fade_level) { fade_level = veil; for (u32 k = 0; k < (u32)(FADE_W * FADE_H); ++k) fade_cover[k] = (unsigned char)veil; }
            op1f_paint_mask(canvas, FADE_X, FADE_Y, FADE_W, FADE_H, fade_cover, FADE_W, 0u);
        }
    } else {
        op1f_paint_text(canvas, 8, 8, "felt", LIGHT_GREY, 1);
        for (u32 i = 0; shown > 0 && i < 4u; ++i) {
            int y = 20 + (int)i * 48;
            op1f_paint_text(canvas, 400, y, shift ? NAMES_SHIFT[i] : NAMES_UP[i], scene::mix(0, (uint16_t)COLOURS[i], shown), 1);
            put_number(text, shown_value(value, i, shift));
            op1f_paint_text(canvas, 400, y + 12, text, scene::mix(0, (uint16_t)LIGHT_GREY, shown), 3);
        }
        if (shown > 0) op1f_paint_text(canvas, 400, 210, velocity_off ? "velocity off" : "velocity on", scene::mix(0, (uint16_t)COLOURS[3], shown), 1);
    }
    const char* status = 0;                      // said only when something is wrong
    switch (bank_state) {
        case BANK_MISSING: status = "put felt.msb in the felt folder"; break;
        case BANK_BAD: status = "felt.msb is not a bank"; break;
        case BANK_TOO_BIG: status = "felt.msb is too big"; break;
        case BANK_NO_ROOM: status = "no room for the bank"; break;
        default: break;
    }
    if (!px) status = status ? status : "no room for the page";
    if (status) centred(canvas, 190, 206, status, COLOURS[3], 1);
    if (how & (u32)OP1F_SHOWN_OFF) centred(canvas, 190, 192, "switched off", COLOURS[3], 1);
}

// Style on felt's page: each click of the third knob steps it once (no long spin), and its press steps it round,
// 1 2 3 1. The input function only counts; the frame function sets the value, as the base wants.
enum { INPUT_LEVEL_2 = 36, KNOB_STYLE = 2, KEY_STYLE_CLICK = 56, KEY_VELOCITY_CLICK = 57 };
static volatile int style_turns, style_presses, velocity_presses;
extern "C" int felt_input(const Op1fInput* e) OP1F_NAMED("felt_input");
extern "C" int felt_input(const Op1fInput* e) {
    if (e->bytes < (u32)INPUT_LEVEL_2) return 0;
    if (e->what == (u32)OP1F_UP) return (e->key == (u32)KEY_STYLE_CLICK || e->key == (u32)KEY_VELOCITY_CLICK) && e->mine;   // the up of the click we took
    if (!e->mine) return 0;
    if (e->what == (u32)OP1F_TURN) knob_turns = knob_turns + 1u;   // counted, not taken (the unit still turns it)
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
    if (op1f_param_get(&op1f_card_felt, ENGINE_ASK, where, V_STYLE, &v) != 0) return;
    v = style_of(v) + turns;
    v = v < 0 ? 0 : v > 2 ? 2 : v;
    v = (v + presses) % 3;
    op1f_param_set(&op1f_card_felt, ENGINE_ASK, where, V_STYLE, v);
}

// the velocity switch, kept for all felt sounds in felt.cfg (folder felt): "FTV1" and a word, 0 = on (listens), 1 = off
static const u32 CFG_MAGIC = 0x31565446u;
static u32 cfg_read, cfg_to_write;
static void velocity_switch(const Op1fFrame* now) {
    u32 cfg[2];
    if (!cfg_read && !now->disk) {
        cfg_read = 1;
        int got = op1f_read(&op1f_card_felt, FOLDER, "felt.cfg", cfg, sizeof cfg);
        if (got == (int)sizeof cfg && cfg[0] == CFG_MAGIC) velocity_off = cfg[1] ? 1u : 0u;
    }
    int presses = velocity_presses;
    if (presses) {
        velocity_presses = velocity_presses - presses;
        if (presses & 1) { velocity_off = velocity_off ? 0u : 1u; cfg_to_write = 1; }
    }
    if (cfg_to_write && !now->disk) {
        cfg[0] = CFG_MAGIC; cfg[1] = velocity_off;
        int answer = op1f_write(&op1f_card_felt, FOLDER, "felt.cfg", cfg, sizeof cfg);
        if (answer != OP1F_NOT_NOW) {
            cfg_to_write = 0;
            if (answer != (int)sizeof cfg) op1f_log(2, "felt.cpp", 1, "felt: writing felt.cfg answered %d", answer);
        }
    }
}

extern "C" void felt_frame(const Op1fFrame* now) OP1F_NAMED("felt_frame");
extern "C" void felt_frame(const Op1fFrame* now) {
    if (now->bytes < (u32)FRAME_LEVEL_4) return;
    load_bank(now);
    velocity_switch(now);
    if (!document_asked) {                       // once, whatever comes of it
        document_asked = 1;
        document = op1f_picture(&op1f_card_felt, "felt.svg");
        if (!document) op1f_log(2, "felt.cpp", 1, "felt: no picture felt.svg; the values in the base's small font");
    }
    if (!page.stood) {
        if (op1f_page_stand_in(&op1f_card_felt, ENGINE_ASK, document)) page.stood = 1;
        else if (!page.refusal_said) {
            page.refusal_said = 1;
            op1f_log(2, "felt.cpp", 1, "felt: the base did not take its page; T1 shows the base's blank page with its name");
        }
    }
    u32 how = shown_how, where = shown_where;
    apply_style(where & 7u);
    if (now->page == &op1f_card_felt && now->canvas != 0 && (how & (u32)OP1F_SHOWN_UP)) paint(now->canvas, where, how, now->shift, now->ms);
}
