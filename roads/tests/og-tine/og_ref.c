/* The OG TINE 0.2 voice path, compiled unchanged on the host as the reference for ms_core's TINE character. */
#include <string.h>
#include "op1_plugin.h"
int32_t og_pitch_inc[OP1_BLOCK];
int16_t og_sine[8192];
#define MS_ENGINE OG_ENGINE
#define MS_SCREEN OG_SCREEN
#define MS_TINE 1
#include "ms_engine.h"
void og_set_audio(const uint8_t *a) { ms_audio = a; }
void og_set_pitch(uint32_t inc) { for (int i = 0; i < OP1_BLOCK; i++) og_pitch_inc[i] = (int32_t)inc; }
uint32_t og_note_inc(int note) { return ms_note_inc[note]; }
void og_on(int v, const int16_t *p) { ms_on(OP1_VOICE(v), p); }
void og_off(int v) { ms_off(OP1_VOICE(v)); }
void og_render(int v, int16_t *out, const int16_t *p) { ms_render(OP1_VOICE(v), out, p); }
int og_sample(int v) { return ms_voices[v].sample; }
uint32_t og_rate(int v) { return ms_voices[v].cached_rate; }
void og_set_mode(int o) { ms_source_mode[0] = (uint8_t)o; }
void og_reset(void) { memset(ms_rr, 0, sizeof ms_rr); }
int16_t og_control(int16_t raw) { return ms_control(raw); }
