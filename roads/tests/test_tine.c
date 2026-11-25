/* Host tests for ms_core's TINE character (bank flag MS_BANK_TINE) with the real TINE bank as roads-rr.msb. The OG
 * TINE 0.2 engine (tests/og-tine, unchanged ms_engine.h) is the reference: with width 0, velocity amount 0 and
 * damp 0, every sample must match it exactly, at 88.2 kHz in blocks of 128. */
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
void og_set_mode(int o);
void og_reset(void);
int16_t og_control(int16_t raw);
extern int16_t og_sine[8192];

static uint8_t *file; static uint32_t bytes;
static ms_bank_t bank;
static ms_core_t core;
static long checks, samples;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static uint32_t og_way_rate(uint32_t inc, const ms_zone_t *z) {   /* the OG's rate: what roads.cpp works out */
    return (uint32_t)(((uint64_t)inc * (12000u * 65536u / 88200u)) / og_note_inc(z->root));
}

/* One note through both, `blocks` blocks of 128, released at block `off` (or never). */
static void one(int note, const int16_t *raw, int mode, int blocks, int off) {
    int16_t p[8]; for (int i = 0; i < 8; i++) p[i] = i < 4 ? og_control(raw[i]) : raw[i];
    ms_part_t *part = &core.parts[0];
    for (int k = 0; k < 4; k++) part->knob[k] = (uint16_t)p[k];
    part->attack_trim = (uint16_t)p[5]; part->loop_trim_start = (uint16_t)p[6]; part->loop_trim_end = (uint16_t)p[7];
    part->source_octave = (uint8_t)mode; og_set_mode(mode);
    og_set_pitch(og_note_inc(note));
    og_on(0, raw);
    int vi = ms_note_on(&core, 0, note, 100);
    int16_t a[128], b[128];
    og_render(0, a, raw);                                          /* the OG picks its sample in the first block */
    if (og_sample(0) < 0) { CHECK(vi < 0); return; }
    CHECK(vi >= 0);
    ms_voice_t *v = &core.voices[vi];
    CHECK(core.parts[0].bank->zones[v->zone].offset == bank.zones[og_sample(0)].offset);
    v->rate = og_way_rate(og_note_inc(note), &bank.zones[v->zone]);
    if (v->rate < 256) v->rate = 256;
    if (v->rate > 262144) v->rate = 262144;
    CHECK(v->rate == og_rate(0));
    for (int blk = 0; blk < blocks; blk++) {
        if (blk == off) { og_off(0); ms_note_off(&core, 0, note); }
        if (blk) og_render(0, a, raw);
        if (!v->active) memset(b, 0, sizeof b); else ms_voice_render_mono(&core, v, b, 128);
        for (int k = 0; k < 128; k++) {
            if (a[k] != b[k]) { fprintf(stderr, "note %d mode %d block %d frame %d: og %d core %d (knobs %d %d %d %d / %d %d %d)\n",
                                        note, mode, blk, k, a[k], b[k], raw[0], raw[1], raw[2], raw[3], raw[5], raw[6], raw[7]); exit(1); }
            samples++;
        }
    }
    v->active = 0;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "bank/roads-rr.msb";
    FILE *f = fopen(path, "rb"); CHECK(f);
    fseek(f, 0, SEEK_END); bytes = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    file = malloc(bytes); CHECK(fread(file, 1, bytes, f) == bytes); fclose(f);
    CHECK(ms_bank_parse(&bank, file, bytes) == MS_BANK_OK);
    CHECK(bank.zone_count == 45 && bank.rate == 12000 && (bank.flags & MS_BANK_TINE) && bank.audio_bytes == 498168);
    for (int i = 0; i < 8192; i++) og_sine[i] = (int16_t)ms_sine((uint32_t)i);
    og_set_audio(bank.audio);
    /* the sine against the real one */
    double worst = 0;
    for (int i = 0; i < 8192; i++) { double e = ms_sine((uint32_t)i) / 32767.0 - __builtin_sin(i * 6.283185307179586 / 8192); if (e < 0) e = -e; if (e > worst) worst = e; }
    CHECK(worst < 4e-3);                     /* a wow and flutter shape: 0.3% is plenty */
    static const int16_t sets[][8] = {
        {256, 18256, 256, 256, 0, 0, 16384, 32767},     /* plugin.json defaults */
        {256, 256, 256, 256, 0, 0, 16384, 32767},       /* all off: one-shot */
        {20000, 30000, 256, 256, 0, 0, 16384, 32767},   /* dark, long */
        {8000, 9000, 20000, 256, 0, 0, 16384, 32767},   /* wow and flutter */
        {8000, 9000, 256, 24000, 0, 0, 16384, 32767},   /* ambient */
        {12000, 9000, 32767, 32767, 0, 0, 16384, 32767},/* all up */
        {256, 18256, 256, 256, 9000, 3000, 26000, 20000},/* the editor */
        {256, 18256, 256, 9000, 30000, 0, 16384, 32767},/* start past the loop end: a one-shot */
    };
    int notes[] = {20, 24, 29, 36, 45, 52, 60, 61, 67, 72, 84, 96, 100, 101, 110};
    for (unsigned s = 0; s < sizeof sets / sizeof sets[0]; s++)
        for (int mode = 0; mode < 3; mode++) {
            ms_core_init(&core, 88200u);
            ms_part_t *part = &core.parts[0];
            part->bank = &bank; part->damp = 0; part->width = 0; part->level = 32768;
            og_reset();
            for (unsigned n = 0; n < sizeof notes / sizeof notes[0]; n++)
                for (int take = 0; take < 3; take++) one(notes[n], sets[s], mode, 1400, 700);
        }
    printf("tine: %ld samples identical to TINE 0.2, %ld checks\n", samples, checks);
    return 0;
}
