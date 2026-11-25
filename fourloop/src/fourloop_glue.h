/* 4loop.pack glue that does not depend on the unit: the master-effect hook, the audio block, the
 * event queue and the screen geometry. Compiled into the pack and, unchanged, into the host tests.
 * GPL-3.0-or-later + OP-1 firmware permission.
 *
 * How 4loop gets audio, with no change to the firmware's code:
 *   The master effect is a C++ object the firmware builds into fixed storage and points to from one
 *   data word (FL_MASTER_SLOT). Its first word is its vtable; the master stage calls vtable[2](self, L, R,
 *   ctx) on the stereo sums every block while the master effect is on. Once a frame, from the display
 *   thread, fl_hook_step() copies that vtable (two words in front for RTTI, 32 behind) into one of
 *   two buffers of ours, puts fl_process into entry 2, and writes our copy's address into the object's
 *   first word. Every other entry (destructors, setup, the UI's own calls) still goes to stock. When the
 *   firmware rebuilds the master effect (a type change) it constructs a new object with stock's vtable,
 *   and the next frame wraps it again. Disabling puts stock's vtable back.
 *   fl_process calls stock's own process first (unless "replace" is on), then runs 4loop on L and R.
 */
#ifndef FOURLOOP_GLUE_H
#define FOURLOOP_GLUE_H
#include <stdint.h>
#include "field/field_plugin.h"

#define FL_VT_BEFORE 2      /* offset-to-top and typeinfo in front of a C++ vtable */
#define FL_VT_AFTER  32     /* entries copied behind it; stock's master producers use 4 to 16 */
#define FL_QUEUE     64     /* key/knob events between the key thread and the audio thread */

typedef void (*fl_process_fn)(void *self, int32_t *left, int32_t *right, void *ctx);

/* Where things are and what counts as a sane pointer. The unit fills it from fourloop_fw.h; the tests fill
 * it with fakes. All words are uintptr_t: on the unit that is 32 bits. */
typedef struct {
    volatile uintptr_t *slot;            /* the master producer pointer word */
    volatile uint8_t   *active;          /* the master fx on byte, or 0 if unknown */
    uintptr_t obj_lo, obj_hi;            /* where a producer object may live */
    uintptr_t vt_lo, vt_hi;              /* where its vtable may live */
    uintptr_t code_lo[2], code_hi[2];    /* where its process function may live (two ranges) */
} fl_env;

enum { FL_HOOK_OFF, FL_HOOK_NO_FX, FL_HOOK_ON, FL_HOOK_REFUSED };

typedef struct {
    uintptr_t copy[2][FL_VT_BEFORE + FL_VT_AFTER];
    volatile uintptr_t orig_process[2];  /* stock's process behind each copy */
    uintptr_t stock_vt, obj;             /* what we replaced, and in which object */
    uint8_t   next;                      /* the copy to use next time */
    volatile uint8_t replace;            /* 1: skip stock's effect, 4loop only */
    volatile uint32_t calls, wraps, refusals;
    volatile int32_t peak;               /* largest |sample| seen on L since the frame last read it */
    uint8_t   state;                     /* FL_HOOK_* as of the last step */
} fl_hook;

typedef struct {
    field_event ev[FL_QUEUE];
    volatile uint32_t head, tail;        /* head: written by the key thread; tail: by the audio thread */
    volatile uint32_t dropped;
} fl_queue;

#ifndef FL_BLOCK
#define FL_BLOCK 64
#endif

/* Everything the pack keeps. One instance, in the pack's zeroed data. */
typedef struct {
    field_plugin fp;
    fl_hook      hook;
    fl_queue     q;
    int16_t      in_l[FL_BLOCK], in_r[FL_BLOCK], out_l[FL_BLOCK], out_r[FL_BLOCK];
    uint8_t      created;
} fl_state;

extern fl_state fl;

/* Display thread, once a frame. enable 0 restores stock. Answers FL_HOOK_*. */
int  fl_hook_step(fl_hook *h, const fl_env *env, int enable);

/* The vtable entry 2 we install. Audio thread. */
void fl_process(void *self, int32_t *left, int32_t *right, void *ctx);

/* One audio block on the Q31 stereo sums: drains the queue, ticks the hold clock, runs 4loop.
 * The dry signal keeps its full resolution: only 4loop's difference is added back. */
void fl_audio(int32_t *left, int32_t *right, int frames);

/* Level 8: one block of the 4loop effect on the master's Q31 sums. values[i] (0..FL_LEVEL_TOP) is loop i's level as
 * the knob, an LFO or the save left it; it is written into the core before the block. Audio thread. */
#define FL_LEVEL_TOP 32                  /* 32 steps of 1024, as the classic level knob */
void fl_effect(int32_t *left, int32_t *right, int frames, const int *values, int count);
int32_t fl_level_gain(int value);        /* 0..FL_LEVEL_TOP -> 0..32767 */

/* Key thread: queue one event for the audio thread. Answers 0 when the queue is full. */
int  fl_push(fl_queue *q, int kind, int a, int b);

/* Geometry for the page. A line node in the picture runs from (0,0) to (64,0); a circle node has r = 64
 * at (0,0). These give the 9-word matrix (1024ths) that puts one where the scene wants it. Points in Q4. */
void fl_line_matrix(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int *m);
void fl_circle_matrix(int32_t cx, int32_t cy, int32_t radius, int *m);
uint32_t fl_isqrt(uint32_t v);

#endif
