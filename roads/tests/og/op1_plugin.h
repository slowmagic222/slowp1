/* Host shim for the OG FELT engine, test reference only: the few OG plugin names ms_engine.h uses. */
#pragma once
#include <stdint.h>
#define OP1_VOICES 6
#define OP1_BLOCK 128
#define OP1_PRESS_PAGE 1
extern int32_t og_pitch_inc[OP1_BLOCK];
#define OP1_PITCH_INC og_pitch_inc
#define OP1_VOICE(i) ((void*)(intptr_t)((i) + 1))
static inline int op1_voice_index(void *self) { return (int)(intptr_t)self - 1; }
static inline int op1_current_slot(void) { return 0; }
static inline int16_t op1_sat16(int32_t x) { return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x); }
typedef struct { void (*on)(void*, const int16_t*); void (*render)(void*, int16_t*, const int16_t*); void (*off)(void*); uint8_t press[4]; } op1_engine_t;
