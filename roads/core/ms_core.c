/* ms_core: see ms_core.h. GPL-3.0-or-later with OP-1 firmware permission.
 * The mono voice path (ms_voice_render_mono) is the plain mono sample path.
 * Everything stereo, multitimbral or velocity happens after that path, so width 0,
 * velocity amount 0 and damp 0 reproduce the plain mono path exactly. */
#include "ms_core.h"

static int32_t sat16(int32_t x) { return x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }
static int16_t le16s(const uint8_t *p) { return (int16_t)(uint16_t)rd16(p); }

uint32_t ms_udiv(uint32_t num, uint32_t den) {
    if (!den) return 0xffffffffu;
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((num >> i) & 1u);
        if (r >= den) { r -= den; q |= 1u << i; }
    }
    return q;
}

uint32_t ms_crc32(uint32_t crc, const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc;
}

/* ---- the bank file ---- */
enum { HEADER = 64, ZONE = 48 };
int ms_bank_parse(ms_bank_t *b, const uint8_t *f, uint32_t bytes) {
    if (bytes < HEADER) return MS_BANK_SHORT;
    if (f[0] != 'M' || f[1] != 'S' || f[2] != 'B' || f[3] != '1' || rd16(f + 4) != 1 || rd16(f + 6) != HEADER)
        return MS_BANK_MAGIC;
    uint32_t total = rd32(f + 8);
    if (total > bytes) return MS_BANK_SHORT;
    if (rd16(f + 44) != 0) return MS_BANK_LAYOUT;                       /* codec 0: IMA 4-bit, 128/68 */
    if ((ms_crc32(0xffffffffu, f + HEADER, total - HEADER) ^ 0xffffffffu) != rd32(f + 12)) return MS_BANK_CRC;
    uint32_t rate = rd32(f + 16), count = rd16(f + 20), zone_bytes = rd16(f + 22), zones_at = rd32(f + 24);
    uint32_t makeup_at = rd32(f + 28), peaks_at = rd32(f + 32), audio_at = rd32(f + 36), audio_bytes = rd32(f + 40);
    if (!count || count > MS_MAX_ZONES || zone_bytes < 40 || rate < 4000 || rate > 65535) return MS_BANK_LAYOUT;
    if (zones_at < HEADER || zones_at + count * zone_bytes > total) return MS_BANK_LAYOUT;
    if (audio_at < HEADER || audio_at + audio_bytes > total) return MS_BANK_LAYOUT;
    if (makeup_at && makeup_at + 128 * 3 * 2 > total) return MS_BANK_LAYOUT;
    if (peaks_at && peaks_at + count * 64 * 2 > total) return MS_BANK_LAYOUT;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *z = f + zones_at + i * zone_bytes;
        ms_zone_t *d = &b->zones[i];
        d->offset = rd32(z); d->frames = rd32(z + 4);
        d->loop_start = rd32(z + 8); d->loop_end = rd32(z + 12); d->loop_limit = rd32(z + 16);
        d->root = z[20]; d->low = z[21]; d->high = z[22]; d->take = z[23];
        d->vel_low = z[24]; d->vel_high = z[25]; d->loop_enabled = z[26]; d->pan = z[27];
        for (int k = 0; k < 5; k++) d->makeup[k] = (uint16_t)rd16(z + 28 + 2 * k);
        d->gain = (uint16_t)rd16(z + 38);
        d->decay = zone_bytes >= 44 ? rd32(z + 40) : 0;
        uint32_t blocks = (d->frames + MS_FRAMES - 1) >> 7;
        if (!d->frames || d->frames > 0x00ffffffu || d->offset + blocks * MS_BYTES > audio_bytes) return MS_BANK_LAYOUT;
        if (d->low > d->high || d->high > 127 || d->root > 127 || d->vel_low > d->vel_high) return MS_BANK_LAYOUT;
        if (d->loop_start >= d->loop_end || d->loop_end > d->frames || d->loop_limit > d->frames) return MS_BANK_LAYOUT;
    }
    b->audio = f + audio_at; b->audio_bytes = audio_bytes; b->rate = rate;
    b->zone_count = (uint16_t)count; b->flags = (uint16_t)rd16(f + 46);
    b->source_makeup = makeup_at ? f + makeup_at : 0;
    b->peaks = peaks_at ? f + peaks_at : 0;
    for (int k = 0; k < 16; k++) b->name[k] = (char)f[48 + k];
    b->name[16] = 0;
    return MS_BANK_OK;
}

/* ---- lifecycle ---- */
static const uint16_t WASH_L_LEN[3] = {733, 1999, 4127}, WASH_L_OFF[3] = {0, 733, 2732};
static const uint16_t WASH_R_LEN[3] = {719, 2003, 4099}, WASH_R_OFF[3] = {0, 719, 2722};   /* decorrelated */
static void wash_reset(ms_wash_t *s) { for (int i = 0; i < 3; i++) s->pos[i] = s->filled[i] = 0; s->wet = s->lp = 0; s->active = 0; }

void ms_part_defaults(ms_part_t *p, const ms_bank_t *bank) {
    uint8_t *raw = (uint8_t *)p;
    for (unsigned i = 0; i < sizeof *p; i++) raw[i] = 0;
    p->bank = bank;
    p->knob[MS_TONE] = 24000;                 /* FELT's own starting patch: felt on, green/lofi/wash off */
    p->loop_trim_end = 32767;
    p->damp = 1;
    p->level = 32768;
    p->rr_spread = 12000; p->key_spread = 20000; p->wash_width = 32767;
}

void ms_core_init(ms_core_t *c, uint32_t out_rate) {
    uint8_t *raw = (uint8_t *)c;
    for (unsigned i = 0; i < sizeof *c; i++) raw[i] = 0;
    c->out_rate = out_rate ? out_rate : 44100;
    /* classic: release_coef = 657000 / release_ms at 44.1 kHz; scaled here for other output rates. */
    uint32_t ratio = ms_udiv(44100u << 12, c->out_rate);              /* Q12 */
    c->release_k = ((657000u >> 3) * ratio) >> 9;
    c->silent_amp = 16;
    for (unsigned p = 0; p < MS_PARTS; p++) {
        ms_part_defaults(&c->parts[p], 0);
        c->wash[p][0].len = WASH_L_LEN; c->wash[p][0].off = WASH_L_OFF; wash_reset(&c->wash[p][0]);
        c->wash[p][1].len = WASH_R_LEN; c->wash[p][1].off = WASH_R_OFF; wash_reset(&c->wash[p][1]);
    }
}

/* ---- ADPCM (classic verbatim) ---- */
static const int16_t steps[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,
    130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,
    2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,
    18500,20350,22385,24623,27086,29794,32767};
static const int8_t indices[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
void ms_decode(const uint8_t *p, int16_t *out) {
    int pred = le16s(p), idx = p[2]; if (idx > 88) idx = 88;
    out[0] = (int16_t)pred;
    for (unsigned i = 0; i < 127; i++) {
        int code = (p[4 + (i >> 1)] >> ((i & 1) * 4)) & 15;
        int step = steps[idx], diff = step >> 3;
        if (code & 4) diff += step;
        if (code & 2) diff += step >> 1;
        if (code & 1) diff += step >> 2;
        pred += (code & 8) ? -diff : diff;
        if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
        idx += indices[code & 7]; if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
        out[i + 1] = (int16_t)pred;
    }
}

/* ---- pitch: 2^((note - root) / 12) * bank rate / output rate, Q16 frames a sample ---- */
static const uint32_t SEMI[12] = {65536,69433,73562,77936,82570,87480,92682,98193,104032,110218,116772,123715};   /* Q16 */
/* (a * b) >> s, exact, without a 64-bit multiply */
static uint32_t mul_shift(uint32_t a, uint32_t b, unsigned s) {
    uint32_t al = a & 65535u, ah = a >> 16, bl = b & 65535u, bh = b >> 16;
    uint32_t p0 = al * bl, p1 = al * bh, p2 = ah * bl, p3 = ah * bh;
    uint32_t mid = (p0 >> 16) + (p1 & 65535u) + (p2 & 65535u);
    uint32_t lo = (p0 & 65535u) | (mid << 16), hi = p3 + (p1 >> 16) + (p2 >> 16) + (mid >> 16);
    if (!s) return lo;
    return s < 32 ? (lo >> s) | (hi << (32 - s)) : hi >> (s - 32);
}
uint32_t ms_mul_shift(uint32_t a, uint32_t b, unsigned s) { return mul_shift(a, b, s); }
uint32_t ms_rate_for(const ms_core_t *c, const ms_bank_t *b, const ms_zone_t *z, int note) {
    uint32_t q = ms_udiv(b->rate << 16, c->out_rate), rem = (b->rate << 16) - q * c->out_rate;
    uint32_t base = (q << 8) + ms_udiv(rem << 8, c->out_rate);      /* bank rate / output rate, Q24 */
    int d = note - (int)z->root, oct = 0;
    while (d < 0) { d += 12; oct--; }
    while (d >= 12) { d -= 12; oct++; }
    if (oct > 6) oct = 6;
    if (oct < -40) oct = -40;
    uint32_t rate = (mul_shift(base, SEMI[d], (unsigned)(23 - oct)) + 1u) >> 1;   /* Q24 x Q16 -> Q16, rounded */
    if (rate < 256) rate = 256;                                      /* classic clamp */
    if (rate > 262144) rate = 262144;
    return rate;
}

/* ---- zone choice: classic ms_select, plus velocity ranges ---- */
/* counter: the note whose round-robin counter turns (FELT: the source note; TINE: the played note, ms_select_for) */
static int select_zone(ms_part_t *p, int note, int vel, int counter) {
    const ms_bank_t *b = p->bank;
    if (!b) return -1;
    unsigned n = 0;
    for (unsigned i = 0; i < b->zone_count; i++) {
        const ms_zone_t *z = &b->zones[i];
        if (note >= z->low && note <= z->high && (p->any_velocity || (vel >= z->vel_low && vel <= z->vel_high))) n++;
    }
    if (!n) return -1;
    unsigned wanted = p->rr[counter];
    while (wanted >= n) wanted -= n;
    p->rr[counter] = (uint8_t)(wanted + 1 >= n ? 0 : wanted + 1);
    for (unsigned i = 0; i < b->zone_count; i++) {
        const ms_zone_t *z = &b->zones[i];
        if (note >= z->low && note <= z->high && (p->any_velocity || (vel >= z->vel_low && vel <= z->vel_high)) && wanted-- == 0) return (int)i;
    }
    return -1;
}
int ms_select_zone(ms_part_t *p, int note, int vel) { return select_zone(p, note, vel, note); }

/* ---- loop bounds: classic ms_bounds_preferred (moving_end 0) and ms_sample_start ---- */
static void bounds(const ms_bank_t *b, const ms_zone_t *z, unsigned start_trim, unsigned end_trim, uint32_t *lo, uint32_t *hi) {
    uint32_t guard = b->rate / 4u, limit = z->loop_limit;
    if (limit <= guard + b->rate) { *lo = z->loop_start; *hi = z->loop_end; return; }   /* too short to edit */
    if (start_trim > 32767) start_trim = 32767;
    if (end_trim > 32767) end_trim = 32767;
    uint32_t room = limit - guard - b->rate;
    uint32_t a = start_trim ? guard + ((room * start_trim) >> 15) : z->loop_start;
    uint32_t e = end_trim == 32767u ? z->loop_end : guard + b->rate + ((room * end_trim) >> 15);
    if (a + b->rate > e) a = e - b->rate;
    *lo = a; *hi = e;
}
static void editor(const ms_part_t *p, unsigned *attack, unsigned *start, unsigned *end) {
    *attack = p->attack_trim; *start = p->loop_trim_start; *end = p->loop_trim_end;
    if (*attack <= 1) *attack = 0;
    if (*start <= 1) *start = 0;
}

/* TINE ms_bounds: loop in 16384 = the bank's own start, below it a share of it, above it up to the top; loop out
 * 32767 = the bank's end; the window never shorter than 350 ms. The classic 64-bit products fit 32 bits here. */
static void tine_bounds(const ms_bank_t *b, const ms_zone_t *z, unsigned start_trim, unsigned end_trim, uint32_t *lo, uint32_t *hi) {
    uint32_t limit = z->loop_limit, minimum = (b->rate * 7u * 3277u) >> 16;   /* 35 % of a second: 4200 at 12 kHz, no division */
    if (start_trim > 32767) start_trim = 32767;
    if (end_trim > 32767) end_trim = 32767;
    if (limit <= minimum) { *lo = z->loop_start; *hi = z->loop_end; return; }
    uint32_t automatic = z->loop_start, top = limit - minimum;
    if (automatic > top) automatic = top;
    uint32_t a = start_trim <= 16384u ? (automatic * start_trim) >> 14
        : automatic + ms_udiv((top - automatic) * (start_trim - 16384u), 16383u);
    uint32_t e = end_trim == 32767u ? z->loop_end : minimum + ms_udiv((limit - minimum) * end_trim, 32767u);
    if (e > limit) e = limit;
    if (e < minimum) e = minimum;
    if (a + minimum > e) a = e - minimum;
    *lo = a; *hi = e;
}
static int tine_loop_allowed(const ms_part_t *p, const ms_zone_t *z) {
    return z->loop_enabled || p->loop_trim_start != 16384u || p->loop_trim_end < 32767u;
}

/* ---- note on / off ---- */
static int pick_voice(ms_core_t *c) {
    int best = -1; uint32_t best_age = 0; int best_rank = -1;
    for (unsigned i = 0; i < MS_VOICES; i++) {
        ms_voice_t *v = &c->voices[i];
        if (!v->active) return (int)i;
        int rank = v->gate ? 0 : 1;                                       /* prefer stealing released voices */
        uint32_t age = c->serial - v->age;
        if (rank > best_rank || (rank == best_rank && age > best_age)) { best = (int)i; best_rank = rank; best_age = age; }
    }
    return best;
}

int ms_note_on(ms_core_t *c, unsigned part, int note, int vel) {
    if (part >= MS_PARTS) return -1;
    if (vel <= 0) { ms_note_off(c, part, note); return -1; }
    if (vel > 127) vel = 127;
    ms_part_t *p = &c->parts[part];
    if (!p->bank || note < 0 || note > 127) return -1;
    int played = note + p->transpose;
    if (played < 0) played = 0; else if (played > 127) played = 127;
    int vi = pick_voice(c);
    ms_voice_t *v = &c->voices[vi];
    /* classic ms_on */
    v->frame = 0; v->frac = 0; v->amp = 32767;
    v->release_level = 32767u << 16; v->wash_env = 0;
    v->noise = 0x1234567u + p->rr[60] + (uint32_t)vi * 1234567u;
    v->cache_block = 0xffffffffu;
    v->direction = 1; v->loop_valid = 0; v->edit_gain = 32767; v->edit_phase = 0;
    v->pad_gain = 8192; v->lofi_held = 0; v->lofi_count = 0;
    v->start_gain = 32767; v->gate = 1; v->lp = 0;
    /* classic pending block: choose the recording (source octave up), tune it to the played note */
    v->note = (uint8_t)played;
    v->source_shift = p->source_octave > 2 ? 2 : p->source_octave;
    int tine = (p->bank->flags & MS_BANK_TINE) != 0;
    int source = played + 12 * (int)v->source_shift; if (source > (tine ? 100 : 127)) source = tine ? 100 : 127;
    int zi = tine && (played < 24 || played > 100) ? -1 : select_zone(p, source, vel, tine ? played : source);
    if (zi < 0) { v->active = 0; return -1; }
    const ms_zone_t *z = &p->bank->zones[zi];
    v->zone = (uint8_t)zi; v->part = (uint8_t)part; v->velocity = (uint8_t)vel;
    if (tine) {                                                           /* TINE ms_on and its pending block */
        v->natural_level = 1u << 30; v->phase = 0;
        tine_bounds(p->bank, z, p->loop_trim_start, p->loop_trim_end, &v->loop_start, &v->loop_end);
        v->target_start = v->loop_start; v->target_end = v->loop_end;
        unsigned at = p->attack_trim > 32767u ? 32767u : p->attack_trim;
        v->play_start = ms_udiv((z->frames - 2u) * at, 32767u);         /* < 2^30: fits */
        v->loop_valid = (uint8_t)(tine_loop_allowed(p, z) && v->play_start < v->loop_end - 1u);
        v->frame = v->play_start;
        v->start_gain = v->frame ? 0 : 32767;
    } else {
    unsigned attack, start, end; editor(p, &attack, &start, &end);
    bounds(p->bank, z, start, end, &v->loop_start, &v->loop_end);
    v->target_start = v->loop_start; v->target_end = v->loop_end;
    v->loop_valid = (uint8_t)(z->loop_enabled || start != 0 || end < 32767u);
    uint32_t frame = ((z->frames >> 15) * attack) + (((z->frames & 32767u) * attack) >> 15);
    v->frame = frame > v->loop_start ? v->loop_start : frame;
    v->start_gain = v->frame ? 0 : 32767;
    }
    v->rate = ms_rate_for(c, p->bank, z, played);
    /* Field: stereo place and velocity, outside the classic path */
    int32_t pan = 0;
    if (p->width) {
        pan = (z->take & 1) ? -(int32_t)p->rr_spread : (int32_t)p->rr_spread;
        if (z->take == 0) pan = 0;
        pan = (pan * 3) >> 3;                                             /* takes: up to 3/8 of the width */
        pan += ((played - 60) * (int32_t)p->key_spread) >> 6;            /* about +-1 over five octaves */
        pan += ((int32_t)z->pan - 128) << 8;
        if (pan > 32767) pan = 32767; else if (pan < -32767) pan = -32767;
        pan = (pan * (int32_t)p->width) >> 15;
    }
    int32_t gl = pan > 0 ? 32768 - pan : 32768, gr = pan < 0 ? 32768 + pan : 32768;
    int32_t g = 32768 - (((int32_t)p->vel_amount * ((127 - vel) * 258)) >> 15);
    if (z->gain && z->gain != 4096) g = (g * (int32_t)z->gain) >> 12;
    if (g > 65534) g = 65534;
    v->gain_l = (gl * g) >> 15; v->gain_r = (gr * g) >> 15;
    v->age = ++c->serial;
    v->active = 1;
    return vi;
}

void ms_note_off(ms_core_t *c, unsigned part, int note) {
    if (part >= MS_PARTS) return;
    int played = note + c->parts[part].transpose;
    if (played < 0) played = 0; else if (played > 127) played = 127;
    for (unsigned i = 0; i < MS_VOICES; i++) {
        ms_voice_t *v = &c->voices[i];
        if (v->active && v->gate && v->part == part && v->note == played) v->gate = 0;
    }
}
void ms_all_off(ms_core_t *c, unsigned part) {
    for (unsigned i = 0; i < MS_VOICES; i++) if (c->voices[i].part == part) c->voices[i].gate = 0;
}

/* ---- the classic voice path ---- */
static int32_t read_frame(ms_voice_t *v, const ms_bank_t *b, const ms_zone_t *z, uint32_t frame, uint32_t frac) {
    if (frame >= z->frames) return 0;
    uint32_t block = frame >> 7;
    if (v->cache_block != block) {
        const uint8_t *p = b->audio + z->offset + block * MS_BYTES;
        ms_decode(p, v->cache);
        v->cache[128] = (block + 1) * 128 < z->frames ? le16s(p + MS_BYTES) : v->cache[127];
        v->cache_block = block;
    }
    int32_t a = v->cache[frame & 127u];
    return a + (((int32_t)v->cache[(frame & 127u) + 1] - a) * (int32_t)(frac >> 1) >> 15);
}

static void advance(ms_voice_t *v, uint32_t rate, int looping) {          /* classic ms_advance */
    uint32_t whole = rate >> 16, part = rate & 65535u;
    if (!looping) v->direction = 1;
    if (v->direction > 0) {
        v->frac += rate; v->frame += v->frac >> 16; v->frac &= 65535u;
        if (v->edit_phase == 1) return;
        if (looping && v->loop_valid && v->frame >= v->loop_end - 1) {
            uint32_t edge = v->loop_end - 1;
            if (v->frame - edge >= v->loop_end - v->loop_start) { v->frame = edge; v->frac = 0; }
            else v->frame = edge - (v->frame - edge) - (v->frac ? 1u : 0u);
            if (v->frac) v->frac = 65536u - v->frac;
            v->direction = -1;
        }
    } else {
        if (v->frac < part) { v->frame -= whole + 1; v->frac += 65536u - part; }
        else { v->frame -= whole; v->frac -= part; }
        if (v->edit_phase == 1) return;
        if (v->frame < v->loop_start) {
            uint32_t edge = v->loop_start;
            if (edge - v->frame >= v->loop_end - v->loop_start) { v->frame = edge; v->frac = 0; }
            else v->frame = edge + (edge - v->frame) - (v->frac ? 1u : 0u);
            if (v->frac) v->frac = 65536u - v->frac;
            v->direction = 1;
        }
    }
}

static void edit_tick(ms_voice_t *v) {                                    /* classic ms_edit_tick */
    if (v->edit_phase == 1) {
        v->edit_gain -= 512;
        if (v->edit_gain <= 0) {
            v->edit_gain = 0; v->loop_start = v->target_start; v->loop_end = v->target_end;
            if (v->frame >= v->loop_end - 1) { v->frame = v->loop_end - 1; v->frac = 0; v->direction = -1; }
            else if (v->direction < 0 && v->frame < v->loop_start) { v->frame = v->loop_start; v->frac = 0; v->direction = 1; }
            v->edit_phase = 2;
        }
    } else {
        v->edit_gain += 512; if (v->edit_gain >= 32767) { v->edit_gain = 32767; v->edit_phase = 0; }
    }
}

/* TINE's sine: Q15, 8192 steps a turn (the classic OP1_SINE_TABLE[phase >> 19]). A quarter by a fifth-order odd
 * polynomial, good to about 0.3%: only the wow and flutter use it. */
int32_t ms_sine(uint32_t index) {
    index &= 8191u;
    uint32_t q = index >> 11, x = index & 2047u;                          /* quarter, place in it */
    if (q & 1u) x = 2048u - x;
    /* sin(pi/2 * t), t = x / 2048: t * (1.5707963 - t^2 * (0.6459640 - t^2 * 0.0794927)) */
    uint32_t t = x << 4;                                                  /* Q15 */
    uint32_t t2 = (t * t) >> 15;
    uint32_t inner = 21167u - ((t2 * 2605u) >> 15);                       /* Q15 */
    uint32_t poly = 51472u - ((t2 * inner) >> 15);                        /* Q15 */
    int32_t s = (int32_t)((t * poly) >> 15);
    if (s > 32767) s = 32767;
    return q >= 2u ? -s : s;
}

/* The TINE voice path (a block of 128 at 88.2 kHz). */
static void render_tine(ms_core_t *c, ms_voice_t *v, int16_t *out, unsigned frames) {
    const ms_part_t *p = &c->parts[v->part];
    const ms_bank_t *b = p->bank;
    const ms_zone_t *z = &b->zones[v->zone];
    const int32_t dark = (int32_t)p->knob[0], lofi = (int32_t)p->knob[2], wash = (int32_t)p->knob[3];
    int looping = p->knob[1] != 0 || wash != 0;
    if ((!looping || !v->loop_valid) && v->frame >= z->frames) v->amp = 0;
    if (!v->gate && v->amp < c->silent_amp) v->amp = 0;
    if (!v->amp) { for (unsigned i = 0; i < frames; i++) out[i] = 0; v->active = 0; return; }
    uint32_t next_start, next_end;
    tine_bounds(b, z, p->loop_trim_start, p->loop_trim_end, &next_start, &next_end);
    v->target_start = next_start; v->target_end = next_end;
    v->loop_valid = (uint8_t)(tine_loop_allowed(p, z) && v->play_start < next_end - 1u);
    if (next_start != v->loop_start || next_end != v->loop_end) {
        if (looping && v->loop_valid) { if (!v->edit_phase) v->edit_phase = 1; }
        else { v->loop_start = next_start; v->loop_end = next_end; }
    }
    int32_t tone = 32767 - ((dark * 30000) >> 15);
    tone -= ((tone - 1800) * wash) >> 15;
    uint32_t release_ms = 150u + (((uint32_t)p->knob[1] * 14850u) >> 15) + (((uint32_t)wash * 30000u) >> 15);
    int32_t wow = ms_sine(v->phase >> 19), flutter = ms_sine((v->phase * 13u) >> 19);
    int32_t warble = ((((wow * 64) >> 15) + ((flutter * 16) >> 15)) * lofi) >> 15;
    uint32_t rate = mul_shift(v->rate, (uint32_t)(32768 + warble), 15);
    v->phase += frames == 128u ? 1350949u : (1350949u >> 7) * frames;  /* ~0.217 Hz at 88.2 kHz */
    unsigned crush = (unsigned)(lofi * 3 + 16383) >> 15;
    uint32_t attack_step = ms_udiv(32767u, 1u + (((uint32_t)wash * 60000u) >> 15)); if (!attack_step) attack_step = 1;
    uint32_t decay_coef = z->decay - mul_shift(z->decay, (uint32_t)wash, 16);
    uint32_t release_coef = ms_udiv(c->release_k, release_ms);
    uint32_t release_drop = ((v->release_level >> 15) * release_coef) >> 7; if (!release_drop) release_drop = 1;
    int32_t makeup = 4096;
    if (b->source_makeup) makeup = (int32_t)rd16(b->source_makeup + ((unsigned)v->note * 3u + v->source_shift) * 2u);
    int releasing = looping || p->damp;                                   /* Field: damp releases one-shots too */
    for (unsigned i = 0; i < frames; i++) {
        if (v->frame >= z->frames) { out[i] = 0; if (v->edit_phase) edit_tick(v); continue; }
        int32_t value = read_frame(v, b, z, v->frame, v->frac);
        if (!dark && !wash) v->lp = value; else v->lp += ((value - v->lp) * tone) >> 15;
        value = (v->lp * (int32_t)v->amp) >> 15;
        if (looping && v->frame >= v->loop_start) {
            value = (value * (int32_t)(v->natural_level >> 15)) >> 15;
            uint32_t drop = ((v->natural_level >> 15) * decay_coef) >> 15;
            v->natural_level = v->natural_level > drop ? v->natural_level - drop : 0;
        }
        if (wash) {
            if (v->wash_env < 32767u) { v->wash_env += attack_step; if (v->wash_env > 32767u) v->wash_env = 32767u; }
            int32_t envelope = 32767 - ((wash * (32767 - (int32_t)v->wash_env)) >> 15);
            value = (value * envelope) >> 15;
        }
        if (lofi) {
            value = (value >> crush) * (1 << crush);
            v->noise = v->noise * 1664525u + 1013904223u;
            int32_t hiss = (int32_t)(v->noise >> 16) - 32768;
            value += (((hiss * lofi) >> 15) * ((int32_t)v->amp >> 9)) >> 15;
        }
        if (v->start_gain < 32767) { v->start_gain += 128; if (v->start_gain > 32767) v->start_gain = 32767; value = (value * v->start_gain) >> 15; }
        if (v->edit_phase) { value = (value * v->edit_gain) >> 15; edit_tick(v); }
        value = (value * makeup) >> 12;
        value = (value * 16384) >> 15;                                    /* -6 dB, as TINE */
        if (value > 24000) value = 24000 + ((value - 24000) >> 2);
        if (value < -24000) value = -24000 + ((value + 24000) >> 2);
        out[i] = (int16_t)sat16(value);
        if (!v->gate && releasing) {
            if (v->release_level > release_drop) v->release_level -= release_drop;
            else v->release_level = 0;
            if (v->release_level < 65536u) v->release_level = 0;
            v->amp = v->release_level >> 16;
        }
        advance(v, rate, looping);
    }
}

void ms_voice_render_mono(ms_core_t *c, ms_voice_t *v, int16_t *out, unsigned frames) {
    const ms_part_t *p = &c->parts[v->part];
    const ms_bank_t *b = p->bank;
    if (!v->active || !b || v->zone >= b->zone_count) { for (unsigned i = 0; i < frames; i++) out[i] = 0; v->active = 0; return; }
    if (b->flags & MS_BANK_TINE) { render_tine(c, v, out, frames); return; }
    const ms_zone_t *z = &b->zones[v->zone];
    const uint32_t k0 = p->knob[0], k1 = p->knob[1], k2 = p->knob[2], k3 = p->knob[3];
    int looping = k1 != 0 || k3 != 0;
    if ((!looping || !v->loop_valid) && v->frame >= z->frames) v->amp = 0;
    if (!v->gate && v->amp < c->silent_amp) v->amp = 0;              /* Field: end inaudible tails, free the voice */
    if (!v->amp) { for (unsigned i = 0; i < frames; i++) out[i] = 0; v->active = 0; return; }
    unsigned attack, start, end; editor(p, &attack, &start, &end);
    uint32_t next_start, next_end;
    bounds(b, z, start, end, &next_start, &next_end);
    v->target_start = next_start; v->target_end = next_end;
    v->loop_valid = (uint8_t)(z->loop_enabled || start != 0 || end < 32767u);
    if (next_start != v->loop_start || next_end != v->loop_end) {
        if (looping && v->loop_valid) { if (!v->edit_phase) v->edit_phase = 1; }
        else { v->loop_start = next_start; v->loop_end = next_end; }
    }
    uint32_t rate = v->rate;
    unsigned felt_index = k0 >> 13, blend = k0 & 8191u;
    int32_t ga = z->makeup[felt_index], gb = z->makeup[felt_index + 1];
    int32_t felt_gain = ga + (((gb - ga) * (int32_t)blend) >> 13);
    int32_t felt = (int32_t)k0;
    int32_t tone = 32767 - ((felt * 29567) >> 15);
    int32_t lofi = (int32_t)k2, wash = (int32_t)k3;
    unsigned crush = (unsigned)(lofi * 6 + 16383) >> 15;
    unsigned hold = 1u + ((unsigned)(lofi * 5 + 16383) >> 15);
    tone -= ((tone * wash) >> 16);
    uint32_t release_ms = 200u + ((k1 * 19800u) >> 15) + ((k3 * 40000u) >> 15);
    uint32_t release_coef = ms_udiv(c->release_k, release_ms);
    uint32_t release_drop = ((v->release_level >> 15) * release_coef) >> 7;
    if (!release_drop) release_drop = 1;
    int32_t pad_target = 8192;
    if (wash && v->frame >= v->loop_start) pad_target += ((wash * 12288) >> 15);
    v->pad_gain += (pad_target - v->pad_gain) >> 4;
    int32_t makeup = 4096;
    if (b->source_makeup) makeup = (int32_t)rd16(b->source_makeup + ((unsigned)v->note * 3u + v->source_shift) * 2u);
    int releasing = looping || p->damp;                                   /* Field: damp releases one-shots too */
    for (unsigned i = 0; i < frames; i++) {
        if (v->frame >= z->frames) { out[i] = 0; if (v->edit_phase) edit_tick(v); continue; }
        int32_t value = read_frame(v, b, z, v->frame, v->frac);
        if (v->start_gain < 32767) { value = (value * v->start_gain) >> 15; v->start_gain += 512; if (v->start_gain > 32767) v->start_gain = 32767; }
        if (wash) {
            if (v->wash_env < 32767u) v->wash_env++;
            int32_t att = 32767 - ((wash * (32767 - (int32_t)v->wash_env)) >> 15);
            value = (value * att) >> 15;
        }
        if (!felt && !wash) v->lp = value; else v->lp += ((value - v->lp) * tone) >> 15;
        value = (v->lp * (int32_t)v->amp) >> 15;
        if (wash) value = sat16((value * v->pad_gain) >> 13);
        if (lofi) {
            if (!v->lofi_count) { v->lofi_held = (value >> crush) * (1 << crush); v->lofi_count = (uint8_t)hold; }
            v->lofi_count--; value = v->lofi_held;
            v->noise = v->noise * 1664525u + 1013904223u;
            int32_t hiss = ((int32_t)(v->noise >> 16) - 32768);
            value += ((((hiss * lofi) >> 15) * ((int32_t)v->amp >> 8)) >> 15);
        } else v->lofi_count = 0;
        if (v->edit_phase) { value = (value * v->edit_gain) >> 15; edit_tick(v); }
        value = (value * felt_gain) >> 12;
        value = (value * makeup) >> 12;
        if (value > 24000) value = 24000 + ((value - 24000) >> 2);
        if (value < -24000) value = -24000 + ((value + 24000) >> 2);
        out[i] = (int16_t)sat16(value);
        if (!v->gate && releasing) {
            if (v->release_level > release_drop) v->release_level -= release_drop;
            else v->release_level = 0;
            if (v->release_level < 65536u) v->release_level = 0;
            v->amp = v->release_level >> 16;
        }
        advance(v, rate, looping);
    }
}

/* ---- stereo wash: the stereo diffuser, returning the wet part only ---- */
static uint32_t qsin(uint32_t x) {                                         /* sin(x * pi/2), Q15 in and out */
    if (x > 32767) x = 32767;
    uint32_t x2 = (x * x) >> 15;
    return (x * (98304u - x2)) >> 16;
}
#ifdef MS_WASH_TICK   /* the frame-at-a-time wash, kept as the reference for tests/wash_ab.c */
static int32_t wash_tick(ms_wash_t *s, int32_t in, int32_t amount) {
    if (s->wet < amount) { s->wet += 32; if (s->wet > amount) s->wet = amount; }
    else if (s->wet > amount) { s->wet -= 32; if (s->wet < amount) s->wet = amount; }
    if (!s->wet) { if (s->active) wash_reset(s); return 0; }
    s->active = 1;
    int32_t x = sat16(in) >> 1;
    for (int i = 0; i < 3; i++) {
        unsigned at = s->off[i] + s->pos[i];
        int32_t delayed = s->filled[i] >= s->len[i] ? s->data[at] : 0;
        int32_t y = delayed - ((x * 22938) >> 15);
        s->data[at] = (int16_t)sat16(x + ((y * 22938) >> 15)); x = sat16(y);
        if (s->filled[i] < s->len[i]) s->filled[i]++;
        if (++s->pos[i] >= s->len[i]) s->pos[i] = 0;
    }
    x = sat16(-x * 2);
    int32_t tone = 30000 - ((s->wet * 18000) >> 15);
    s->lp += ((x - s->lp) * tone) >> 15;
    return (s->lp * (int32_t)qsin((uint32_t)s->wet)) >> 15;
}
#endif

/* The same wash a block at a time, stage by stage, with the state in locals: the samples wash_tick gives, for a
 * fraction of the cost (the amount is one value a block, so the wet part reaches 0 only by ramping down to an
 * amount of 0, and from then on stays there: the frames washed are a prefix of the block). Adds into out. */
static void wash_block(ms_wash_t *s, const int32_t *in, int32_t *x, int32_t *out, unsigned frames, int32_t amount) {
    int32_t wet = s->wet;
    unsigned n = 0;
    for (; n < frames; n++) {
        if (wet < amount) { wet += 32; if (wet > amount) wet = amount; }
        else if (wet > amount) { wet -= 32; if (wet < amount) wet = amount; }
        if (!wet) break;
    }
    if (n) {
        for (unsigned k = 0; k < n; k++) x[k] = sat16(in[k]) >> 1;
        for (int i = 0; i < 3; i++) {
            int16_t *d = s->data + s->off[i];
            unsigned pos = s->pos[i], filled = s->filled[i], len = s->len[i];
            for (unsigned k = 0; k < n; k++) {
                int32_t delayed = filled >= len ? d[pos] : 0;
                int32_t y = delayed - ((x[k] * 22938) >> 15);
                d[pos] = (int16_t)sat16(x[k] + ((y * 22938) >> 15));
                x[k] = sat16(y);
                if (filled < len) filled++;
                if (++pos >= len) pos = 0;
            }
            s->pos[i] = (uint16_t)pos; s->filled[i] = (uint16_t)filled;
        }
        int32_t lp = s->lp, w = s->wet, gain = -1, tone = 0;
        for (unsigned k = 0; k < n; k++) {
            int32_t before = w;
            if (w < amount) { w += 32; if (w > amount) w = amount; }
            else if (w > amount) { w -= 32; if (w < amount) w = amount; }
            if (w != before || gain < 0) { tone = 30000 - ((w * 18000) >> 15); gain = (int32_t)qsin((uint32_t)w); }
            int32_t v = sat16(-x[k] * 2);
            lp += ((v - lp) * tone) >> 15;
            out[k] += (lp * gain) >> 15;
        }
        s->lp = lp; s->wet = w; s->active = 1;
    }
    if (n < frames) { s->wet = 0; if (s->active) wash_reset(s); }
}

void ms_render(ms_core_t *c, int32_t *left, int32_t *right, unsigned frames) {
    int16_t *mono = c->mono;
    if (frames > MS_MAX_BLOCK) frames = MS_MAX_BLOCK;
    for (unsigned pi = 0; pi < MS_PARTS; pi++) {
        ms_part_t *p = &c->parts[pi];
        int32_t amount = (int32_t)(((uint32_t)p->knob[MS_WASH] * p->wash_width) >> 15);
        if (!p->width) amount = 0;
        int sending = amount || c->wash[pi][0].active;
        if (sending) for (unsigned i = 0; i < frames; i++) c->send[i] = 0;
        int32_t level = p->level;
        for (unsigned vi = 0; vi < MS_VOICES; vi++) {
            ms_voice_t *v = &c->voices[vi];
            if (!v->active || v->part != pi) continue;
            ms_voice_render_mono(c, v, mono, frames);
            int32_t gl = (v->gain_l * level) >> 15, gr = (v->gain_r * level) >> 15;
            if (gl == 32768 && gr == 32768) {
                for (unsigned i = 0; i < frames; i++) { left[i] += mono[i]; right[i] += mono[i]; }
            } else {
                for (unsigned i = 0; i < frames; i++) { left[i] += (mono[i] * gl) >> 15; right[i] += (mono[i] * gr) >> 15; }
            }
            if (sending) for (unsigned i = 0; i < frames; i++) c->send[i] += (mono[i] * level) >> 15;
        }
        if (sending) {
            wash_block(&c->wash[pi][0], c->send, c->wash_x, left, frames, amount);
            wash_block(&c->wash[pi][1], c->send, c->wash_x, right, frames, amount);
        }
    }
}
