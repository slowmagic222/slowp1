// bows.pack: a string section as an open-field synth engine (levels 9 and 10, base 30+), beside felt and roads.
// GPL-3.0-or-later with OP-1 firmware permission (as felt and roads). Samples: VSCO 2 Community Edition (Versilian
// Studios / Sam Gossner), CC0: see bank/SOURCES.md.
//
// What the owner meets: `bows` in the engine list (shift and T1 on a synth sound). Knobs: bow (how the note is
// drawn: 0 the recording's own attack, up to a 4 s swell), stretch (the real section slowed: from the moment the bow
// has spoken, grains walk through the recording at down to 1/64 of its speed), style (as felt's and roads': 1 the
// section as recorded, 2 the recording an octave up played back down, softer and blurred, 3 two octaves, grainy;
// a turn steps it, a grey press steps it round), ambient (slower, darker, longer). With shift: release, warmth (0 the
// recording as it is, up darker), slide (0 none; 1 to 50: a key pressed within half an octave of a held one starts at the held one's pitch and
// loudness and slides to its own, fading up, over 3 s to 75 ms, the held one sounding on; the players' own vibrato stays), width. A red-orange press is the velocity
// switch: on, velocity picks the soft or loud recording; off, always the loud one. Kept in bows.cfg.
// T1: a string section standing in the dark (bows_scene.h): bass and cello upright, viola and violin floating in
// front, music stands between. Each instrument's bow plays while its register sounds; with playing, ribbon bows are
// tied on, ribbons and confetti gather, confetti falls; it all fades back when playing stops.
//
// How it runs (v7): each of the unit's six voices reads the recording itself (attack, then the sustain looped back
// and forth) and two grains for the stretch, all at 44.1 kHz, drawn up to 88.2 kHz at the end; core/ gives only the
// bank file, the decoder and helpers. (v8: the ensemble, six saws, is gone; style took its knob.) One envelope (bow, release) over both, so the unit's own envelope should stay open (the
// pack's sounds set it so). Pitch from the block's step (glide and bend work); the recording is chosen at the
// note's start. The bank is a file: bows.msb in the pack's folder `bows` (user/pf/bows/), read once into level 6
// working memory by the frame function. Until it is there the engine renders silence and the page says why.
//
// Audio thread rules (docs/level-9.md): render calls nothing of the base, no division, no 64-bit multiply, no float,
// no struct copies, and keeps its stack small (scratch lives in the voice's memory).

#define MS_VOICES 1u
#define MS_PARTS 1u
#include "core/ms_core.c"   // for the bank file, the ADPCM decoder, the sine and the 32-bit helpers only (v7)
#include "build_r6e_card.h"
#include "bows_scene.h"

typedef unsigned int u32;
#include "bows_tables.h"

extern "C" const Op1fCard op1f_card_bows OP1F_NAMED("op1f_card_bows");

enum { VALUES = 8, FRAMES = 128, ENGINE_ASK = 0, MEMORY_ASK = 1, PAGE_ASK = 2, FOLDER = 0, FRAME_LEVEL_4 = 40, BLOCK_LEVEL_9 = 52 };
enum { V_BOW = 0, V_STRETCH, V_STYLE, V_AMBIENT, V_RELEASE, V_WARMTH, V_SLIDE, V_WIDTH };   // v8: style on the third knob; v11: slide in vibrato's place
static int style_of(int v) { return v < 0 ? 0 : v > 2 ? 2 : v; }
enum { BANK_ROOM = 3145728, VOICE_BYTES = 16384, OUT_RATE = 88200, IN_RATE = 44100 };
enum { GRAIN = 32768, HALF_GRAIN = 16384, HALF = 64 };          // a grain: 32,768 inner frames (0.74 s at 44.1 kHz), two half a grain apart
static const u32 MAGIC = 0x31574f42u;               // "BOW1": the voice was set up by bows_reset
static const u32 ENV_FULL = 1u << 30, ENV_END = 1u << 17;   // the envelope, Q30; below ENV_END (-78 dB) the note is over
static const u32 VIB_STEP = 32411575u;              // 5.2 Hz, a block (2^32 a turn)
static const u32 XFADE_STEP = 119u;                 // the stretch takes over in 276 blocks (0.4 s) after the bow

// ---------------------------------------------------------------- shared between the frame function and render

static ms_bank_t bank_store;                       // parsed by the frame function, then only read
static const ms_bank_t* volatile bank_ready;       // published once, never taken back (the file buffer stays)
static volatile u32 velocity_off;                  // the velocity switch: 1 = always the loud recording
static volatile u32 voice_slot[6], voice_note[6], voice_on[6], voice_begins[6], begin_serial;   // for the page
// the slide (v9), between the voices on the audio thread: each voice's age (blocks since its note began) and pitch now,
// and a hand-over: the voice whose note is taken over slides to `hand_to` over `hand_n` blocks while it fades
static u32 voice_age[6], voice_step[6], hand_to[6], hand_n[6], hand_fade[6], voice_vel[6];
enum { SLIDE_AFTER = 100, SLIDE_BASE = 36, SLIDE_PER = 7, SLIDE_MOST = 6 };   // v13: within half an octave   // held 0.15 s; the pitch 52 ms + 10 ms a semitone; within an octave
// v11: the slide knob (shift, grey), 0 none, 1..50: how long the new note slides and fades up (v13: the held one stays),
// v12: 3 s (1) down to 75 ms (50), in blocks; the pitch slides over the whole of that time too (v11's pitch
// moved in some 60 ms whatever the knob, which sounded lightning fast)
static const uint16_t SLIDE_BLOCKS[51] = {0, 2070, 1920, 1781, 1652, 1532, 1421, 1318, 1223, 1134, 1052, 976, 905, 840, 779, 722, 670, 622, 577, 535, 496, 460, 427, 396, 367, 341, 316, 293, 272, 252, 234, 217, 201, 187, 173, 161, 149, 138, 128, 119, 110, 102, 95, 88, 82, 76, 70, 65, 60, 56, 52};
static const int32_t VIBRATO = 4915;      // v11: the vibrato fixed where its knob started (15), the knob being the slide's

// ---------------------------------------------------------------- a voice

struct Grain {             // a reader of the bank: the stretch's grains and (v7) the recording itself
    u32 frame, frac, age, block;
    int dir;
    int16_t cache[MS_FRAMES + 1];
};

// v7: the whole voice runs at 44.1 kHz (the bank is 32 kHz), 64 inner frames a block, drawn up to 88.2 kHz at the end.
// v1..v6 ran felt's full voice path at 88.2 kHz and the layers on top: 60,000 cycles a voice on the unit on average,
// 87,000 at most (the base allows 40,000 and switched it off).
struct Voice {
    u32 magic;
    u32 sounding;          // a note began since the voice was handed over
    u32 gate;              // the key is down
    u32 last_step, base_rate;
    int note, zone;
    u32 env, blocks, vib_phase, vib_env, xfade, vel_gain, heard;
    u32 walker; int walk_dir; u32 noise;
    u32 loop_start, loop_end;
    int32_t gain_l, gain_r, start_gain, start_inc;
    u32 slide_from, slide_to, slide_n, slide_i;   // a slide: from one step to another (slide_to 0: to the key's own)
    u32 held_step, handed, fade_dec, fade_n;      // handed over: the step it slid to, and kept; the fades (v11)
    int32_t lp, grain_lp, prev_l, prev_r;
    int32_t il[HALF], ir[HALF];   // this block at 44.1 kHz
    Grain rec;                    // the recording
    Grain tail;                   // v14: the loop's other side, read while the loop's end crossfades into its start
    u32 xf_shift;                 // v14: the crossfade's length, 2^xf_shift bank frames
    Grain grain[2];               // the stretch
};
static_assert(sizeof(Voice) <= (unsigned)VOICE_BYTES, "a voice fits the memory the card asks");

// The step of MIDI note n at 88,200 Hz, a cycle being 2^32 (A4 = 21,426,141): the same table as roads'.
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

static int note_of(u32 step) {                    // the nearest note
    int lo = 0, hi = 127;
    while (lo < hi) { int mid = (lo + hi + 1) >> 1; if (STEP[mid] <= step) lo = mid; else hi = mid - 1; }
    if (lo < 127 && step > STEP[lo] && step - STEP[lo] > STEP[lo + 1] - step) ++lo;
    return lo;
}

// (a * b) / c, whole, for a quotient below 2^32, without 64-bit arithmetic (roads' mul_div). Once a block at most.
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

// Q16 bank frames an inner frame (44.1 kHz) for this step (a step a frame at 88.2 kHz): step x (bank rate / 44,100) / the root's step.
static u32 rate_for_step(const ms_bank_t* b, const ms_zone_t* z, u32 step) {
    u32 k = ms_udiv(b->rate << 16, (u32)IN_RATE);
    u32 rate = mul_div(step, k, STEP[z->root]);
    if (rate < 256u) rate = 256u;
    if (rate > 262144u) rate = 262144u;
    return rate;
}

static int clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }
static uint16_t q15(int v) { v = clamp100(v); return v >= 100 ? 32767 : (uint16_t)(((u32)v * 1342177u) >> 12); }

// Out: +3 dB, a soft knee above 24,000 and a hard hold at full scale (roads').
static int to_q31(int32_t x) {
    int32_t u = (x * 45) >> 5;
    int32_t a = u < 0 ? -u : u;
    if (a > 24000) a = 24000 + ((a - 24000) >> 2);
    if (a > 32767) a = 32767;
    return (int)((u32)(u < 0 ? -a : a) << 16);
}

static void silence(int* out) { for (u32 k = 0; k < 2u * (u32)FRAMES; ++k) out[k] = 0; }

static void forget(Voice* v) {                     // handed anew: no note, no tail
    v->sounding = 0; v->gate = 0; v->last_step = 0; v->env = 0; v->slide_n = 0; v->handed = 0;
}

static void fill(Grain* g, const ms_bank_t* b, const ms_zone_t* z, u32 block) {   // decode a reader's block
    const uint8_t* bp = b->audio + z->offset + block * MS_BYTES;
    ms_decode(bp, g->cache);
    g->cache[128] = (block + 1u) * 128u < z->frames ? le16s(bp + MS_BYTES) : g->cache[127];
    g->block = block;
}

// The zone for a note and velocity: the first whose ranges hold both (the bank has one soft and one loud a note).
static int zone_for(const ms_bank_t* b, int note, int vel) {
    int any = -1;
    for (int i = 0; i < (int)b->zone_count; ++i) {
        const ms_zone_t* z = &b->zones[i];
        if (note < z->low || note > z->high) continue;
        if (vel >= z->vel_low && vel <= z->vel_high) return i;
        if (any < 0) any = i;
    }
    return any;
}

// ---- the stretch: two grains, half a grain apart, each a sine window over 0.74 s of the recording read at the
// note's pitch, started where the walker is. The walker goes through the zone's loop (the steady part of the bow) at
// the stretch's speed, back and forth. Both in the middle (v5 panned them: a left-right pulse).
static int32_t window(u32 age) { return ms_sine((age >> 3) & 8191u); }   // Q15, age 0..GRAIN: half a turn

static void grain_start(Voice* v, Grain* g, const ms_zone_t* z, u32 rate, u32 speed) {
    u32 lo = z->loop_start, hi = z->loop_end;
    u32 span = (rate >> 1) + 2u;                       // the frames a grain reads (32,768 inner frames)
    hi = hi > lo + span + 2u ? hi - span : lo + 2u;
    u32 adv = ms_mul_shift(rate >> 2, speed, 16);      // the walker's way in half a grain
    if (v->walker < lo) v->walker = lo;
    if (v->walk_dir > 0) {
        v->walker += adv;
        if (v->walker >= hi) { v->walker = hi - 1u; v->walk_dir = -1; }
    } else {
        if (adv >= v->walker - lo) { v->walker = lo; v->walk_dir = 1; } else v->walker -= adv;
    }
    v->noise = v->noise * 1664525u + 1013904223u;
    u32 jitter = ms_mul_shift(v->noise >> 16, span >> 3, 16);   // up to an eighth of a grain, so repeats do not ring
    u32 at = v->walker + jitter;
    if (at >= hi) at = hi - 1u;
    g->frame = at; g->frac = 0; g->age = 0; g->block = 0xffffffffu;
}

extern "C" void bows_reset(unsigned slot, unsigned voice, void* state) OP1F_NAMED("bows_reset");
extern "C" void bows_reset(unsigned slot, unsigned voice, void* state) {   // display thread: the big clear
    (void)slot;
    Voice* v = (Voice*)state;
    if (!v) return;
    v->magic = 0;
    uint8_t* raw = (uint8_t*)v;
    for (u32 i = 0; i < (u32)sizeof(Voice); ++i) raw[i] = 0;
    forget(v);
    v->note = 60;
    v->noise = 0x2545f491u + voice * 977u;
    v->magic = MAGIC;
}

extern "C" void bows_render(const Op1fVoiceBlock* now) OP1F_NAMED("bows_render");
extern "C" void bows_render(const Op1fVoiceBlock* now) {
    int* out = now->out;
    Voice* v = (Voice*)now->state;
    const ms_bank_t* b = bank_ready;
    if (now->bytes < (u32)BLOCK_LEVEL_9 || !v || v->magic != MAGIC || !b) { silence(out); return; }
    if (now->events & (u32)OP1F_VOICE_FRESH) {
        forget(v);
        u32 me = now->voice < 6u ? now->voice : 5u;
        hand_to[me] = 0; voice_age[me] = 0; voice_on[me] = 0; voice_step[me] = 0;   // nothing of what it was
    }

    const int* value = now->values;
    int ambient = clamp100(value[V_AMBIENT]);
    int bow = clamp100(value[V_BOW]); if (bow < ((ambient * 3) >> 2)) bow = (ambient * 3) >> 2;   // ambient bows slower
    int release = clamp100(value[V_RELEASE]);
    int warmth = clamp100(value[V_WARMTH] + (ambient >> 1));                                      // and darker
    int32_t width = (int32_t)q15(value[V_WIDTH]);

    if (now->events & (u32)OP1F_VOICE_BEGINS) {
        int note = note_of(now->step);
        int heard = now->velocity >> 24; if (heard < 0) heard = 0; else if (heard > 127) heard = 127;
        int slide_from_voice = -1, slide_knob = value[V_SLIDE];
        slide_knob = slide_knob < 0 ? 0 : slide_knob > 50 ? 50 : slide_knob;
        if (slide_knob) {
            u32 me = now->voice < 6u ? now->voice : 5u;
            int bestd = 99;
            for (u32 j = 0; j < 6u; ++j) {
                if (j == me || voice_slot[j] != now->slot || !voice_on[j] || voice_age[j] < (u32)SLIDE_AFTER || !voice_step[j]) continue;
                int d = note - (int)voice_note[j]; if (d < 0) d = -d;
                if (d >= 1 && d <= (int)SLIDE_MOST && d < bestd) { bestd = d; slide_from_voice = (int)j; }
            }
            if (slide_from_voice >= 0) heard = (int)voice_vel[slide_from_voice];   // v13: the held note's velocity
        }
        // style: the recording of the note an octave (2) or two (3) up, played back down to the note: slower, softer, blurred
        int source = note + 12 * style_of(value[V_STYLE]); if (source > 127) source = 127;
        int zi = zone_for(b, source, velocity_off ? 110 : (heard < 1 ? 1 : heard));
        if (zi >= 0) {
            const ms_zone_t* z = &b->zones[zi];
            v->note = note; v->zone = zi; v->sounding = 1; v->gate = 1; v->last_step = 0;
            v->env = 0; v->blocks = 0; v->vib_env = 0; v->xfade = 0;
            v->vel_gain = VEL_GAIN[heard]; v->heard = (u32)heard;       // gentle velocity loudness (0.8..1), as heard, whatever the switch picked
            int32_t g = (int32_t)z->gain << 3;   // Q12 -> Q15
            if (z->vel_high < 127u) g = (g * 23170) >> 14;   // the soft recording: 4 dB under the loud, not 7
            if (g > 32767) g = 32767;
            int32_t pan = ((note - 60) * 16384) >> 6; if (pan > 32767) pan = 32767; if (pan < -32767) pan = -32767;
            pan = (pan * width) >> 15;           // low keys left, high right, by width (felt's key spread)
            v->gain_l = (g * (pan > 0 ? 32767 - pan : 32767)) >> 15;
            v->gain_r = (g * (pan < 0 ? 32767 + pan : 32767)) >> 15;
            u32 skip = ms_mul_shift(z->loop_start, (u32)bow * 655u, 16) >> 2;   // a slow bow starts past the first scrape
            v->start_inc = 1024;
            v->slide_n = 0; v->handed = 0; v->fade_n = 0;
            // the slide (v13: added, not a hand-over): a key pressed while another of this sound has been held a
            // moment, within half an octave, begins at the held note's pitch and its loudness and slides to its own,
            // fading up as it goes; the held note keeps on
            if (slide_from_voice >= 0) {
                u32 fade = SLIDE_BLOCKS[slide_knob];
                v->slide_from = voice_step[slide_from_voice]; v->slide_to = 0; v->slide_i = 0;
                v->slide_n = fade; v->fade_n = fade;          // the pitch over the whole fade
                skip = z->loop_start;                         // into the sustain: the bow is already moving
                v->start_inc = (int32_t)ms_udiv(32767u, (fade < 48u ? fade : 48u) * 64u) + 1;   // in from the sustain softly; the envelope fades it up
            }
            v->rec.frame = skip; v->rec.frac = 0; v->rec.dir = 1; v->rec.block = 0xffffffffu;
            v->start_gain = skip ? 0 : 32767;
            v->loop_start = z->loop_start; v->loop_end = z->loop_end;
            {   // v14: the loop crossfades (no more back and forth, whose turns popped): up to 8,192 frames (0.26 s),
                // no longer than the way in before the loop or half the loop
                u32 len = z->loop_end - 1u - z->loop_start, k = 13u;
                while (k > 11u && ((1u << k) > z->loop_start || (1u << k) > (len >> 1))) --k;
                v->xf_shift = k;
            }
            v->tail.block = 0xffffffffu;
            v->lp = 0; v->grain_lp = 0; v->prev_l = v->prev_r = 0;
            v->walker = z->loop_start; v->walk_dir = 1;
            v->grain[0].age = 0; v->grain[1].age = (u32)HALF_GRAIN;
            v->grain[0].block = v->grain[1].block = 0xffffffffu;
            v->grain[0].frame = v->grain[1].frame = v->walker; v->grain[0].frac = v->grain[1].frac = 0;
        }
    }
    if (now->events & (u32)OP1F_VOICE_ENDS) v->gate = 0;
    {
        u32 vi = now->voice < 6u ? now->voice : 5u;
        if ((now->events & (u32)OP1F_VOICE_BEGINS) && v->sounding) { voice_note[vi] = (u32)v->note; voice_vel[vi] = (u32)v->heard; voice_begins[vi] = ++begin_serial; }
        voice_slot[vi] = now->slot;
        voice_on[vi] = v->sounding && v->gate;
    }
    if (!v->sounding || v->zone < 0 || v->zone >= (int)b->zone_count) { silence(out); return; }
    const ms_zone_t* z = &b->zones[v->zone];

    u32 me = now->voice < 6u ? now->voice : 5u;
    if (hand_to[me]) {                       // another voice took this one's note over: slide to it and fade
        v->slide_from = voice_step[me] ? voice_step[me] : now->step; v->slide_to = hand_to[me];
        v->slide_n = hand_n[me]; v->slide_i = 0; v->held_step = hand_to[me]; v->handed = 1; v->gate = 0;
        v->fade_dec = ms_udiv(v->env, hand_fade[me] ? hand_fade[me] : 1u) + 1u;   // fading away, evenly, over the slide
        hand_to[me] = 0;
    }
    // the envelope at the block's start and end: the bow's attack while the key is down, the release after
    u32 env0 = v->env, env1 = env0;
    if (v->gate) {
        u32 inc = ATTACK_INC[bow];
        if (v->fade_n) {                     // arriving on a slide: up over its fade, slowly
            u32 up = ms_udiv(ENV_FULL, v->fade_n) + 1u;
            inc = up < inc ? up : inc;
            if (v->env + inc >= ENV_FULL) v->fade_n = 0;
        }
        env1 = env0 + inc; if (env1 > ENV_FULL || env1 < env0) env1 = ENV_FULL;
    } else {
        int r = release + (ambient >> 1); if (r > 100) r = 100;
        env1 = ms_mul_shift(env0, RELEASE_MUL[r], 30);
        if (v->handed) env1 = env0 > v->fade_dec ? env0 - v->fade_dec : 0;   // handed over: away over the slide
        if (env1 < ENV_END) env1 = 0;
    }
    v->env = env1;
    if (!env0 && !env1 && !v->gate) { v->sounding = 0; v->handed = 0; voice_age[me] = 0; silence(out); return; }

    // pitch: the step (glide, bend), the vibrato on top, fading in over half a second
    u32 step = v->handed ? v->held_step : now->step;
    if (v->slide_n) {                        // sliding: a smooth s from one step to the other (eased in and out)
        u32 to = v->slide_to ? v->slide_to : now->step, from = v->slide_from;
        u32 p = ms_udiv(v->slide_i << 16, v->slide_n);
        u32 p2 = ms_mul_shift(p, p, 16), p3 = ms_mul_shift(p2, p, 16);
        u32 e = 3u * p2 - 2u * p3;           // Q16, 0..65536
        step = to >= from ? from + ms_mul_shift(to - from, e, 16) : from - ms_mul_shift(from - to, e, 16);
        if (++v->slide_i >= v->slide_n) v->slide_n = 0;
    }
    voice_step[me] = step; voice_age[me] = v->gate ? v->blocks : 0u;
    if (step != v->last_step) { v->base_rate = rate_for_step(b, z, step); v->last_step = step; }
    v->vib_phase += VIB_STEP;
    if (v->vib_env < 32767u) { v->vib_env += 95u; if (v->vib_env > 32767u) v->vib_env = 32767u; }
    int32_t depth = (int32_t)((((u32)VIBRATO * 1137u) >> 15) * v->vib_env >> 15);   // up to 30 cents, Q16
    int32_t wobble = (ms_sine(v->vib_phase >> 19) * depth) >> 15;
    u32 bend = (u32)(65536 + wobble);                                    // Q16
    u32 rate = ms_mul_shift(v->base_rate, bend, 16);                     // bank frames an inner frame, Q16

    // stretch 0: the recording alone. Above 0 the grains take the recording's place once the bow has spoken (46 ms
    // in, over 0.4 s); after that the recording is not read at all.
    int32_t stretch = (int32_t)q15(value[V_STRETCH]);
    if (v->blocks >= 32u) { v->xfade += XFADE_STEP; if (v->xfade > 32767u) v->xfade = 32767u; }
    ++v->blocks;
    int32_t grain_w = stretch ? (int32_t)v->xfade : 0, sample_w = 32767 - grain_w;
    int32_t tone = 32767 - ((int32_t)q15(warmth) * 29567 >> 15);       // felt's warmth filter: a one-pole low-pass

    int32_t* il = v->il; int32_t* ir = v->ir;
    // ---- the recording, read through its loop (v14: round, the end crossfaded into the start), low-passed, placed by the key
    if (sample_w > 0) {
        Grain* g = &v->rec;
        u32 frame = g->frame, frac = g->frac, ls = v->loop_start, le = v->loop_end - 1u;
        int32_t lp = v->lp, sg = v->start_gain;
        int32_t gl = (v->gain_l * sample_w) >> 15, gr = (v->gain_r * sample_w) >> 15;
        u32 span = le - ls, xn = 1u << v->xf_shift, xs = le - xn, xq = v->xf_shift - 11u;
        Grain* t = &v->tail;
        for (u32 j = 0; j < (u32)HALF; ++j) {
            if (g->block != (frame >> 7)) fill(g, b, z, frame >> 7);
            const int16_t* at = g->cache + (frame & 127u);
            int32_t x = at[0] + ((((int32_t)at[1] - at[0]) * (int32_t)(frac >> 1)) >> 15);
            if (frame >= xs && span > xn && ls >= xn) {      // v14: the loop's last stretch fades into what comes before its start
                u32 f2 = frame - span;
                if (t->block != (f2 >> 7)) fill(t, b, z, f2 >> 7);
                const int16_t* bt = t->cache + (f2 & 127u);
                int32_t y = bt[0] + ((((int32_t)bt[1] - bt[0]) * (int32_t)(frac >> 1)) >> 15);
                u32 q = (frame - xs) >> xq; if (q > 2048u) q = 2048u;   // a quarter turn over the crossfade
                x = ((x * ms_sine(2048u - q)) >> 15) + ((y * ms_sine(q)) >> 15);   // equal power
            }
            if (sg < 32767) { x = (x * sg) >> 15; sg += v->start_inc; if (sg > 32767) sg = 32767; }
            lp += ((x - lp) * tone) >> 15;
            il[j] = (lp * gl) >> 15; ir[j] = (lp * gr) >> 15;
            frac += rate; frame += frac >> 16; frac &= 65535u;
            if (frame >= le) frame -= span;      // round to the loop's start, where the faded-in side goes on
        }
        g->frame = frame; g->frac = frac; v->lp = lp; v->start_gain = sg;
    } else {
        for (u32 j = 0; j < (u32)HALF; ++j) { il[j] = 0; ir[j] = 0; }
    }

    // ---- the stretch: both grains in one pass, mono into the middle
    if (grain_w > 0) {
        u32 speed = STRETCH_SPEED[clamp100(value[V_STRETCH])];
        Grain* g0 = &v->grain[0]; Grain* g1 = &v->grain[1];
        for (u32 gi = 0; gi < 2u; ++gi) {
            Grain* gr = &v->grain[gi];
            if (gr->age >= (u32)GRAIN) gr->age -= (u32)GRAIN;
            if (gr->age == 0) grain_start(v, gr, z, rate, speed);
        }
        int32_t w0 = window(g0->age), dw0 = (window(g0->age + (u32)HALF) - w0) >> 6;
        int32_t w1 = window(g1->age), dw1 = (window(g1->age + (u32)HALF) - w1) >> 6;
        u32 f0 = g0->frame, q0 = g0->frac, f1 = g1->frame, q1 = g1->frac, last = z->frames - 1u;
        int32_t gm = (((v->gain_l + v->gain_r) >> 1) * grain_w) >> 15;
        int32_t lp = v->grain_lp;
        for (u32 j = 0; j < (u32)HALF; ++j) {
            int32_t x0 = 0, x1 = 0;
            if (f0 < last) {
                if (g0->block != (f0 >> 7)) fill(g0, b, z, f0 >> 7);
                const int16_t* at = g0->cache + (f0 & 127u);
                x0 = at[0] + ((((int32_t)at[1] - at[0]) * (int32_t)(q0 >> 1)) >> 15);
            }
            if (f1 < last) {
                if (g1->block != (f1 >> 7)) fill(g1, b, z, f1 >> 7);
                const int16_t* at = g1->cache + (f1 & 127u);
                x1 = at[0] + ((((int32_t)at[1] - at[0]) * (int32_t)(q1 >> 1)) >> 15);
            }
            q0 += rate; f0 += q0 >> 16; q0 &= 65535u;
            q1 += rate; f1 += q1 >> 16; q1 &= 65535u;
            int32_t x = ((x0 * w0) >> 15) + ((x1 * w1) >> 15);
            w0 += dw0; w1 += dw1;
            lp += ((x - lp) * tone) >> 15;
            int32_t y = (lp * gm) >> 15;
            il[j] += y; ir[j] += y;
        }
        v->grain_lp = lp;
        g0->frame = f0; g0->frac = q0; g0->age += (u32)HALF;
        g1->frame = f1; g1->frac = q1; g1->age += (u32)HALF;
    } else {
        for (u32 gi = 0; gi < 2u; ++gi) { v->grain[gi].age += (u32)HALF; if (v->grain[gi].age >= (u32)GRAIN) v->grain[gi].age -= (u32)GRAIN; }
    }

    // ---- up to 88.2 kHz (a line between inner frames), the envelope and the velocity, out
    int32_t env_q = (int32_t)(env0 >> 16), env_d = ((int32_t)(env1 >> 16) - env_q) >> 7;   // Q14
    int32_t vg = (int32_t)v->vel_gain, pl = v->prev_l, pr = v->prev_r;
    for (u32 j = 0; j < (u32)HALF; ++j) {
        int32_t cl = il[j], cr = ir[j];
        int32_t e0 = env_q + env_d * (int32_t)(2u * j), e1 = e0 + env_d;
        int32_t ml = (pl + cl) >> 1, mr = (pr + cr) >> 1;
        out[4u * j]      = to_q31((((ml * e0) >> 14) * vg) >> 15);
        out[4u * j + 1u] = to_q31((((mr * e0) >> 14) * vg) >> 15);
        out[4u * j + 2u] = to_q31((((cl * e1) >> 14) * vg) >> 15);
        out[4u * j + 3u] = to_q31((((cr * e1) >> 14) * vg) >> 15);
        pl = cl; pr = cr;
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
    void* memory = op1f_memory(&op1f_card_bows, MEMORY_ASK);
    if (!memory) {
        if (++memory_tries > 600u && bank_state == BANK_WAITING) {
            bank_state = BANK_NO_ROOM;
            op1f_log(2, "bows.cpp", 1, "bows: no working memory for the bank (%u bytes)", (unsigned)BANK_ROOM);
        }
        return;
    }
    int got = op1f_read(&op1f_card_bows, FOLDER, "bows.msb", memory, (unsigned)BANK_ROOM);
    if (got == OP1F_NO_FILE || got == 0) { bank_state = BANK_MISSING; op1f_log(2, "bows.cpp", 1, "bows: no bows.msb in the bows folder"); return; }
    if (got == OP1F_TOO_LARGE) { bank_state = BANK_TOO_BIG; op1f_log(2, "bows.cpp", 1, "bows: bows.msb is over %u bytes", (unsigned)BANK_ROOM); return; }
    if (got < 0) { bank_state = BANK_MISSING; op1f_log(2, "bows.cpp", 1, "bows: reading bows.msb answered %d", got); return; }
    int parsed = ms_bank_parse(&bank_store, (const uint8_t*)memory, (uint32_t)got);
    if (parsed != MS_BANK_OK) { bank_state = BANK_BAD; op1f_log(2, "bows.cpp", 1, "bows: bows.msb is not a bank (%d)", parsed); return; }
    bank_state = BANK_LOADED;
    bank_ready = &bank_store;
    op1f_log(3, "bows.cpp", 1, "bows: bank '%s' loaded, %d bytes, %u zones", bank_store.name, got, (unsigned)bank_store.zone_count);
}

// ---------------------------------------------------------------- the page

static const unsigned COLOURS[4] = {OP1F_COLOUR(0x4c, 0x7d, 0xa8), OP1F_COLOUR(0x8a, 0x65, 0x3c), OP1F_COLOUR(0xcd, 0xd0, 0xc2), OP1F_COLOUR(0xe5, 0x28, 0x17)};
static const char NAMES_UP[4][8] = {"bow", "stretch", "style", "ambient"};
static const char NAMES_SHIFT[4][8] = {"release", "warmth", "slide", "width"};

// The values in the firmware's own letters: placeholders of the pack's picture bows.svg (bows.svg.make), the title
// bows_t0..2 (coloured by the style), the names bows_l0..3 in the knobs' colours and the numbers bows_n0..3. The scene
// is painted over that page with the names' column and the title's corner left out (as roads').
enum { KEY = 0x0821, COLUMN_X = 340, TITLE_W = 90, TITLE_H = 32 };
enum { FADE_X = 340, FADE_Y = 8, FADE_W = 140, FADE_H = 210, FADE_STRIP = 10 };   // the veil painted 10 rows at a time: 1.4 KB of the packs' room
static const char TEXT_TITLE[3][8] = {"bows_t0", "bows_t1", "bows_t2"};
static int title_shown = -1;
static const char TEXT_NAME[4][8] = {"bows_l0", "bows_l1", "bows_l2", "bows_l3"};
static const char TEXT_NUMBER[4][8] = {"bows_n0", "bows_n1", "bows_n2", "bows_n3"};
static const char TEXT_VELOCITY[] = "bows_v";
static void* document;
static u32 document_asked, texts_hidden;
static unsigned char fade_cover[FADE_W * FADE_STRIP];
static int fade_level = -1;
static const unsigned LIGHT_GREY = OP1F_COLOUR(0xcd, 0xd0, 0xc2);

static volatile u32 shown_where, shown_how;
static struct { u32 stood, refusal_said; } page;

extern "C" void bows_shown(unsigned where, unsigned how) OP1F_NAMED("bows_shown");
extern "C" void bows_shown(unsigned where, unsigned how) { shown_where = where; shown_how = how; }

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

static int shown_value(const int* value, u32 i, u32 shift) {   // style shows as 1 to 3
    if (shift) return value[i + 4u];
    return i == (u32)V_STYLE ? style_of(value[V_STYLE]) + 1 : value[i];
}

// which instrument a note sounds on (SECTION's order: 0 cello, 1 bass, 2 viola, 3 violin), by the bank's registers
static u32 instrument_of(u32 note) { return note <= 35u ? 1u : note <= 52u ? 0u : note <= 64u ? 2u : 3u; }

// ---- the scene: drawn every second frame into the page's memory, shown every frame
static scene::State look;

// the values fade out when nothing has moved for a while: shown 2.5 s after the last change, then gone over half a second
enum { SHOW_MS = 2500, FADE_MS = 500 };
static int seen_values[VALUES + 4];
static u32 seen_any, changed_ms, frames_up;
static u32 seen_frame_ms, seen_where;   // v12: back on the page after being away (as felt): the scene starts again
static volatile u32 knob_turns;     // v8: every turn of one of our knobs, so a knob at its end (no change) still shows the values

static void paint(void* canvas, u32 where, u32 how, u32 shift, u32 ms) {
    uint16_t* px = (uint16_t*)op1f_memory(&op1f_card_bows, PAGE_ASK);
    int value[VALUES];
    for (u32 i = 0; i < (u32)VALUES; ++i) if (op1f_param_heard(&op1f_card_bows, ENGINE_ASK, where, i, &value[i]) != 0) value[i] = 0;
    if (px) {
        if (seen_frame_ms && (ms - seen_frame_ms > 400u || where != seen_where)) { look.life = 0; look.notes_seen = begin_serial; }
        seen_frame_ms = ms ? ms : 1u; seen_where = where;
        look.time += 1u;
        look.angle = scene::swivel(look.time);   // v10: the swivel, as roads
        look.style = style_of(value[V_STYLE]);
        look.lit = 0;
        for (u32 vi = 0; vi < 6u; ++vi)
            if (voice_slot[vi] == where && voice_on[vi]) look.lit |= 1u << instrument_of(voice_note[vi]);
        look.notes = begin_serial;
        scene::step(look);
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
    // what shows the values: a knob turned (the values as set, not as heard: the heard ones step when notes start and
    // stop), a turn at a knob's end (v8), shift, the velocity switch, another sound
    int now_seen[VALUES + 4];
    for (u32 i = 0; i < (u32)VALUES; ++i) { int set = 0; now_seen[i] = op1f_param_get(&op1f_card_bows, ENGINE_ASK, where, i, &set) == 0 ? set : value[i]; }
    now_seen[VALUES] = shift != 0; now_seen[VALUES + 1] = (int)velocity_off; now_seen[VALUES + 2] = (int)where; now_seen[VALUES + 3] = (int)knob_turns;
    bool moved = !seen_any;
    for (u32 i = 0; i < (u32)VALUES + 4u; ++i) if (now_seen[i] != seen_values[i]) { moved = true; seen_values[i] = now_seen[i]; }
    if (moved) { seen_any = 1; changed_ms = ms; }
    u32 idle = ms - changed_ms;
    int shown = idle < (u32)SHOW_MS ? 256 : idle < (u32)(SHOW_MS + FADE_MS) ? 256 - (int)((idle - (u32)SHOW_MS) * 256u / (u32)FADE_MS) : 0;
    char text[12];
    if (document) {
        int style = style_of(value[V_STYLE]);
        for (u32 k = 0; k < 3u; ++k) op1f_text_set(document, TEXT_TITLE[k], "bows", 0, OP1F_LEFT);
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
                void* nb = op1f_text_node(document, TEXT_NUMBER[i]);
                if (a) op1f_show(a, hide ? 0 : 1);
                if (nb) op1f_show(nb, hide ? 0 : 1);
            }
            void* vn = op1f_text_node(document, TEXT_VELOCITY);
            if (vn) op1f_show(vn, hide ? 0 : 1);
        }
        if (shown > 0 && shown < 256) {
            int veil = 255 - shown;
            if (veil != fade_level) { fade_level = veil; for (u32 k = 0; k < (u32)(FADE_W * FADE_STRIP); ++k) fade_cover[k] = (unsigned char)veil; }
            for (int y = 0; y < (int)FADE_H; y += (int)FADE_STRIP) op1f_paint_mask(canvas, FADE_X, FADE_Y + y, FADE_W, FADE_STRIP, fade_cover, FADE_W, 0u);
        }
    } else {
        op1f_paint_text(canvas, 8, 8, "bows", LIGHT_GREY, 1);
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
        case BANK_MISSING: status = "put bows.msb in the bows folder"; break;
        case BANK_BAD: status = "bows.msb is not a bank"; break;
        case BANK_TOO_BIG: status = "bows.msb is too big"; break;
        case BANK_NO_ROOM: status = "no room for the bank"; break;
        default: break;
    }
    if (!px) status = status ? status : "no room for the page";
    if (status) centred(canvas, 190, 206, status, COLOURS[3], 1);
    if (how & (u32)OP1F_SHOWN_OFF) centred(canvas, 190, 192, "switched off", COLOURS[3], 1);
}

// Style (the third knob, as felt's and roads'): a turn steps it once a click, a press of the grey knob steps it round.
// A press of the red-orange knob is the velocity switch. The input function only counts; the frame function sets the
// value and keeps the switch in bows.cfg (folder bows). Every other turn of our knobs is counted (not taken) so the
// values show even when a knob is already at its end.
enum { INPUT_LEVEL_2 = 36, KNOB_STYLE = 2, KEY_STYLE_CLICK = 56, KEY_VELOCITY_CLICK = 57 };
static volatile int style_turns, style_presses, velocity_presses;
extern "C" int bows_input(const Op1fInput* e) OP1F_NAMED("bows_input");
extern "C" int bows_input(const Op1fInput* e) {
    if (e->bytes < (u32)INPUT_LEVEL_2) return 0;
    if (e->what == (u32)OP1F_UP) return (e->key == (u32)KEY_STYLE_CLICK || e->key == (u32)KEY_VELOCITY_CLICK) && e->mine;   // the up of a click we took
    if (!e->mine) return 0;
    if (e->what == (u32)OP1F_TURN) {
        knob_turns = knob_turns + 1u;
        if (e->key == (u32)KNOB_STYLE && !e->shift) {
            if (e->turn > 0) style_turns = style_turns + 1; else if (e->turn < 0) style_turns = style_turns - 1;
            return 1;
        }
        return 0;
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
    if (op1f_param_get(&op1f_card_bows, ENGINE_ASK, where, V_STYLE, &v) != 0) return;
    v = style_of(v) + turns;
    v = v < 0 ? 0 : v > 2 ? 2 : v;
    v = (v + presses) % 3;
    op1f_param_set(&op1f_card_bows, ENGINE_ASK, where, V_STYLE, v);
}

// "BWV1" and a word, 0 = on (listens), 1 = off
static const u32 CFG_MAGIC = 0x31565742u;
static u32 cfg_read, cfg_to_write;
static void velocity_switch(const Op1fFrame* now) {
    u32 cfg[2];
    if (!cfg_read && !now->disk) {
        cfg_read = 1;
        int got = op1f_read(&op1f_card_bows, FOLDER, "bows.cfg", cfg, sizeof cfg);
        if (got == (int)sizeof cfg && cfg[0] == CFG_MAGIC) velocity_off = cfg[1] ? 1u : 0u;
    }
    int presses = velocity_presses;
    if (presses) {
        velocity_presses = velocity_presses - presses;
        if (presses & 1) { velocity_off = velocity_off ? 0u : 1u; cfg_to_write = 1; }
        op1f_log(3, "bows.cpp", 1, "bows: velocity %s", velocity_off ? "off: always the loud recording" : "on");
    }
    if (cfg_to_write && !now->disk) {
        cfg[0] = CFG_MAGIC; cfg[1] = velocity_off;
        int answer = op1f_write(&op1f_card_bows, FOLDER, "bows.cfg", cfg, sizeof cfg);
        if (answer != OP1F_NOT_NOW) {
            cfg_to_write = 0;
            if (answer != (int)sizeof cfg) op1f_log(2, "bows.cpp", 1, "bows: writing bows.cfg answered %d", answer);
        }
    }
}

extern "C" void bows_frame(const Op1fFrame* now) OP1F_NAMED("bows_frame");
extern "C" void bows_frame(const Op1fFrame* now) {
    if (now->bytes < (u32)FRAME_LEVEL_4) return;
    load_bank(now);
    velocity_switch(now);
    if (!document_asked) {
        document_asked = 1;
        document = op1f_picture(&op1f_card_bows, "bows.svg");
        if (!document) op1f_log(2, "bows.cpp", 1, "bows: no picture bows.svg; the values in the base's small font");
    }
    if (!page.stood) {
        if (op1f_page_stand_in(&op1f_card_bows, ENGINE_ASK, document)) page.stood = 1;
        else if (!page.refusal_said) {
            page.refusal_said = 1;
            op1f_log(2, "bows.cpp", 1, "bows: the base did not take its page; T1 shows the base's blank page with its name");
        }
    }
    u32 how = shown_how, where = shown_where;
    apply_style(where & 7u);
    if (now->page == &op1f_card_bows && now->canvas != 0 && (how & (u32)OP1F_SHOWN_UP)) paint(now->canvas, where & 7u, how, now->shift, now->ms);
}
