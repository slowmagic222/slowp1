/* Host tests for ms_core with the real FELT bank. The OG v16 engine (tests/og, unchanged ms_engine.h) is the
 * reference: with width 0, velocity amount 0 and damp 0, every sample must match it exactly. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/ms_core.h"

void og_set_audio(const uint8_t *a);
void og_set_pitch(uint32_t inc);
uint32_t og_note_inc(int note);
void og_on(int v, const int16_t *p);
void og_off(int v);
void og_render(int v, int16_t *out, const int16_t *p);
int og_sample(int v);
uint32_t og_rate(int v);
uint32_t og_amp(int v);
void og_set_octave(int o);
void og_reset(void);
int16_t og_control(int16_t raw);

static uint8_t *bank_file; static uint32_t bank_bytes;
static ms_bank_t bank;
static ms_core_t core, core2;
static int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static void load(const char *path) {
    FILE *f = fopen(path, "rb"); CHECK(f);
    fseek(f, 0, SEEK_END); bank_bytes = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    bank_file = malloc(bank_bytes); CHECK(fread(bank_file, 1, bank_bytes, f) == bank_bytes); fclose(f);
}

static void test_bank(void) {
    CHECK(ms_bank_parse(&bank, bank_file, bank_bytes) == MS_BANK_OK);
    CHECK(bank.zone_count == 15 && bank.rate == 16000 && bank.audio_bytes == 484568);
    CHECK(!strcmp(bank.name, "felt") && bank.source_makeup && bank.peaks);
    ms_bank_t tmp;
    CHECK(ms_bank_parse(&tmp, bank_file, 40) == MS_BANK_SHORT);
    CHECK(ms_bank_parse(&tmp, bank_file, bank_bytes - 1) == MS_BANK_SHORT);
    bank_file[5000] ^= 1; CHECK(ms_bank_parse(&tmp, bank_file, bank_bytes) == MS_BANK_CRC); bank_file[5000] ^= 1;
    bank_file[0] = 'X'; CHECK(ms_bank_parse(&tmp, bank_file, bank_bytes) == MS_BANK_MAGIC); bank_file[0] = 'M';
    CHECK(ms_bank_parse(&tmp, bank_file, bank_bytes) == MS_BANK_OK);
}

/* Pitch: the semitone table against the OG's step table, and at 48 kHz against the exact ratio. */
static void test_pitch(void) {
    ms_core_init(&core, 44100);
    double worst = 0;
    for (unsigned zi = 0; zi < bank.zone_count; zi++)
        for (int note = 0; note < 128; note++) {
            const ms_zone_t *z = &bank.zones[zi];
            double og = (double)og_note_inc(note) * (16000u * 65536u / 44100u) / og_note_inc(z->root);
            if (og < 256 || og > 262144) continue;
            double diff = fabs(ms_rate_for(&core, &bank, z, note) - og);
            if (diff <= 1) continue;          /* one step of Q16: the OG's own resolution at the lowest notes */
            if (diff / og > worst) worst = diff / og;
        }
    CHECK(worst < 1e-4);                     /* the OG itself truncates 16000*65536/44100: about 3e-5 */
    ms_core_init(&core, 48000);
    worst = 0;
    for (int note = 30; note < 100; note++) {
        double want = pow(2, (note - 62) / 12.0) * 16000 / 48000 * 65536;
        double diff = fabs(ms_rate_for(&core, &bank, &bank.zones[5], note) - want);
        if (diff > 1 && diff / want > worst) worst = diff / want;
    }
    CHECK(worst < 1e-4);
    printf("pitch: semitone table within %.1e of exact at 48 kHz\n", worst);
}

/* The OG voice path, sample for sample. */
static unsigned compare(int note, const int16_t raw[8], int octave, int off_block, int blocks) {
    int16_t p[8], a[128], b[128];
    memcpy(p, raw, sizeof p);
    og_reset(); og_set_octave(octave); og_set_pitch(og_note_inc(note));
    ms_core_init(&core, 44100);
    core.silent_amp = 0;
    ms_part_t *part = &core.parts[0];
    ms_part_defaults(part, &bank);
    part->width = 0; part->damp = 0; part->vel_amount = 0; part->source_octave = (uint8_t)octave;
    for (int k = 0; k < 4; k++) part->knob[k] = (uint16_t)og_control(raw[k]);
    if (raw[7]) { part->attack_trim = (uint16_t)raw[5]; part->loop_trim_start = (uint16_t)raw[6]; part->loop_trim_end = (uint16_t)raw[7]; }
    else { part->attack_trim = 0; part->loop_trim_start = (uint16_t)raw[5]; part->loop_trim_end = (uint16_t)raw[6]; }
    unsigned compared = 0;
    for (int hit = 0; hit < 3; hit++) {
        og_on(0, p);
        int vi = ms_note_on(&core, 0, note, 100);
        CHECK(vi == 0 || hit > 0);
        ms_voice_t *v = &core.voices[vi];
        for (int blk = 0; blk < blocks; blk++) {
            if (blk == off_block) { og_off(0); ms_note_off(&core, 0, note); }
            og_render(0, a, p);
            if (blk == 0) {
                CHECK(og_sample(0) == v->zone);
                CHECK(fabs((double)v->rate - og_rate(0)) <= 1 || fabs((double)v->rate / og_rate(0) - 1) < 1e-4);
                v->rate = og_rate(0);           /* the OG's own rounding, so the comparison is exact */
            }
            if (v->active) ms_voice_render_mono(&core, v, b, 128); else memset(b, 0, sizeof b);
            for (int i = 0; i < 128; i++) {
                if (a[i] != b[i]) {
                    fprintf(stderr, "note %d hit %d block %d frame %d: og %d core %d (k %d %d %d %d)\n",
                            note, hit, blk, i, a[i], b[i], raw[0], raw[1], raw[2], raw[3]);
                    exit(1);
                }
                compared++;
            }
        }
        /* a fresh voice each hit, as the OG's ms_on restarts voice 0 */
        v->active = 0;
    }
    return compared;
}

static void test_og(void) {
    static const int16_t sets[][8] = {
        {0, 0, 0, 0, 0, 0, 32767, 0},                 /* clear, one-shot */
        {24256, 0, 0, 0, 0, 0, 32767, 0},             /* felt */
        {32767, 32767, 0, 0, 0, 0, 32767, 0},         /* felt + long release, loops */
        {16000, 9000, 32767, 0, 0, 0, 32767, 0},      /* lofi */
        {20000, 0, 0, 32767, 0, 0, 32767, 0},         /* wash, looping through green 0 */
        {32767, 32767, 20000, 32767, 0, 0, 32767, 0}, /* everything */
        {24000, 16000, 0, 0, 0, 9000, 22000, 0},      /* two-marker loop edit */
        {24000, 16000, 0, 8000, 0, 6000, 9000, 22000},/* three markers: sample start, loop in, loop out */
    };
    unsigned total = 0;
    for (unsigned s = 0; s < sizeof sets / sizeof sets[0]; s++)
        for (int note = 0; note < 128; note += 5)
            for (int oct = 0; oct < 3; oct += (s & 1) ? 1 : 2)
                total += compare(note, sets[s], oct, 300, 420);
    printf("OG equivalence: %u samples identical (8 knob sets, 26 notes, octaves, 3 round-robin hits)\n", total);
}

/* Block size: without per-block ramps (no wash, no release) 64-frame blocks give the 128-frame result. */
static void test_blocks(void) {
    int32_t l1[128], r1[128], l2[128], r2[128];
    ms_core_init(&core, 44100); ms_core_init(&core2, 44100);
    ms_part_defaults(&core.parts[0], &bank); ms_part_defaults(&core2.parts[0], &bank);
    core.parts[0].width = core2.parts[0].width = 0;
    for (int n = 48; n < 72; n += 7) { ms_note_on(&core, 0, n, 90); ms_note_on(&core2, 0, n, 90); }
    for (int blk = 0; blk < 300; blk++) {
        memset(l1, 0, sizeof l1); memset(r1, 0, sizeof r1); memset(l2, 0, sizeof l2); memset(r2, 0, sizeof r2);
        ms_render(&core, l1, r1, 128);
        ms_render(&core2, l2, r2, 64); ms_render(&core2, l2 + 64, r2 + 64, 64);
        for (int i = 0; i < 128; i++) CHECK(l1[i] == l2[i] && r1[i] == r2[i] && l1[i] == r1[i]);
    }
    printf("block size: 64-frame blocks match 128-frame blocks; width 0 is mono\n");
}

static double side_energy(ms_core_t *c, int blocks, double *mid) {
    int32_t l[64], r[64]; double side = 0; *mid = 0;
    for (int blk = 0; blk < blocks; blk++) {
        memset(l, 0, sizeof l); memset(r, 0, sizeof r); ms_render(c, l, r, 64);
        for (int i = 0; i < 64; i++) {
            CHECK(abs(l[i]) < 6 * 32768 && abs(r[i]) < 6 * 32768);
            side += (double)(l[i] - r[i]) * (l[i] - r[i]); *mid += (double)(l[i] + r[i]) * (l[i] + r[i]);
        }
    }
    return side;
}

static void test_stereo(void) {
    double mid, side;
    /* round-robin takes land on opposite sides */
    ms_core_init(&core, 48000); ms_part_defaults(&core.parts[0], &bank);
    core.parts[0].width = 32767; core.parts[0].key_spread = 0;
    int a = ms_note_on(&core, 0, 60, 100), b = ms_note_on(&core, 0, 60, 100);
    CHECK(bank.zones[core.voices[a].zone].take != bank.zones[core.voices[b].zone].take);
    CHECK((core.voices[a].gain_l < core.voices[a].gain_r) != (core.voices[b].gain_l < core.voices[b].gain_r));
    /* keyboard spread: low keys left, high keys right */
    ms_core_init(&core, 48000); ms_part_defaults(&core.parts[0], &bank);
    core.parts[0].width = 32767; core.parts[0].rr_spread = 0;
    a = ms_note_on(&core, 0, 36, 100); b = ms_note_on(&core, 0, 90, 100);
    CHECK(core.voices[a].gain_l > core.voices[a].gain_r && core.voices[b].gain_r > core.voices[b].gain_l);
    side = side_energy(&core, 200, &mid);
    CHECK(side > mid * 0.01);
    /* the stereo wash: a decorrelated tail after the keys are let go */
    ms_core_init(&core, 48000); ms_part_defaults(&core.parts[0], &bank);
    core.parts[0].width = 32767; core.parts[0].rr_spread = core.parts[0].key_spread = 0;
    core.parts[0].knob[MS_WASH] = 32767; core.parts[0].knob[MS_RELEASE] = 8000;
    ms_note_on(&core, 0, 60, 100); ms_note_on(&core, 0, 64, 100); ms_note_on(&core, 0, 67, 100);
    side = side_energy(&core, 1500, &mid);
    CHECK(side > mid * 0.02);
    ms_all_off(&core, 0);
    side_energy(&core, 40000, &mid);                 /* wash at full: about 45 s to -60 dB */
    for (unsigned i = 0; i < MS_VOICES; i++) CHECK(!core.voices[i].active);
    /* and it empties out once wash is turned down */
    core.parts[0].knob[MS_WASH] = 0;
    side_energy(&core, 2000, &mid);
    CHECK(!core.wash[0][0].active && !core.wash[0][1].active);
    printf("stereo: takes left/right, keyboard spread, wash tail decorrelated and drained\n");
}

static void test_parts(void) {
    ms_core_init(&core, 48000);
    for (unsigned p = 0; p < MS_PARTS; p++) ms_part_defaults(&core.parts[p], &bank);
    core.parts[1].transpose = 12; core.parts[1].knob[MS_RELEASE] = 20000;
    int a = ms_note_on(&core, 0, 60, 100), b = ms_note_on(&core, 1, 60, 100);
    CHECK(a != b && core.voices[a].note == 60 && core.voices[b].note == 72);
    ms_note_off(&core, 0, 60);
    CHECK(!core.voices[a].gate && core.voices[b].gate);
    ms_note_off(&core, 1, 60);
    CHECK(!core.voices[b].gate);
    /* round-robin counters are a part's own */
    ms_core_init(&core, 48000);
    ms_part_defaults(&core.parts[0], &bank); ms_part_defaults(&core.parts[1], &bank);
    a = ms_note_on(&core, 0, 60, 100); b = ms_note_on(&core, 1, 60, 100);
    CHECK(core.voices[a].zone == core.voices[b].zone);
    /* stealing: the oldest released voice goes first, then the oldest held */
    ms_core_init(&core, 48000); ms_part_defaults(&core.parts[0], &bank);
    for (int n = 0; n < (int)MS_VOICES; n++) ms_note_on(&core, 0, 40 + n, 100);
    ms_note_off(&core, 0, 45);
    int v = ms_note_on(&core, 0, 80, 100);
    CHECK(core.voices[v].note == 80 && v == 5);
    v = ms_note_on(&core, 0, 81, 100);
    CHECK(v == 0);
    /* damp: a one-shot part (green and wash at 0) still stops after the key, within about 0.3 s */
    ms_core_init(&core, 48000); ms_part_defaults(&core.parts[0], &bank);
    v = ms_note_on(&core, 0, 60, 100);
    double mid; side_energy(&core, 100, &mid); ms_note_off(&core, 0, 60);
    side_energy(&core, (int)(0.3 * 48000 / 64), &mid);
    CHECK(!core.voices[v].active);
    printf("parts: separate notes, transposes, round-robin and releases; voice stealing; damp\n");
}

/* Velocity layers and the velocity amount, on a bank built here with two layers over one recording. */
static void test_velocity(void) {
    static ms_bank_t layered;
    layered = bank;
    layered.zone_count = 2;
    layered.zones[0] = bank.zones[5]; layered.zones[0].vel_low = 0; layered.zones[0].vel_high = 63;
    layered.zones[1] = bank.zones[6]; layered.zones[1].vel_low = 64; layered.zones[1].vel_high = 127;
    ms_core_init(&core, 48000); ms_part_defaults(&core.parts[0], &layered);
    CHECK(core.voices[ms_note_on(&core, 0, 62, 30)].zone == 0);
    CHECK(core.voices[ms_note_on(&core, 0, 62, 100)].zone == 1);
    core.parts[0].vel_amount = 32767;
    int soft = ms_note_on(&core, 0, 62, 20), loud = ms_note_on(&core, 0, 62, 127);
    CHECK(core.voices[soft].gain_l < core.voices[loud].gain_l / 4 && core.voices[loud].gain_l >= 32700);
    printf("velocity: layers by range, gain by amount\n");
}

int main(int argc, char **argv) {
    load(argc > 1 ? argv[1] : "felt.msb");
    test_bank();
    og_set_audio(bank.audio);
    test_pitch();
    test_og();
    test_blocks();
    test_stereo();
    test_parts();
    test_velocity();
    printf("all %d checks passed\n", checks);
    return 0;
}
