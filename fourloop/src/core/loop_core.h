/* 4loop portable stereo core. No firmware calls, no heap, no static state.
 * Derived from reelloop-v11 (4loop 0.11). GPL-3.0-or-later + OP-1 firmware permission.
 *
 * The host adapter owns memory, input and drawing:
 *   - lc_init() with one block of memory (4-byte aligned). The core carves it into a
 *     shared pool of fixed-size chunks, a staging ring and small chunk tables.
 *   - lc_command() for every control event, from the audio thread only.
 *   - lc_render() once per audio block (the v11 timing assumes 64 frames).
 *   - lc_snapshot() to fill a view for the screen.
 *
 * Memory policy: all four loops draw chunks from one pool. Short loops stay at 44.1 kHz.
 * When the pool has no chunk left for the loop being recorded, that loop halves its own
 * stored rate (44.1 > 22.05 > 11.025 > 5.5 kHz), which hands half its chunks back, and keeps
 * recording. Only at the lowest rate with the pool still empty does recording end. Each
 * empty loop keeps a small reservation so a long take cannot starve the other three.
 * Loops that are already playing are never degraded.
 *
 * With L == R input, centred pan and lc_config {9, 130560, 0} the output equals v11
 * sample for sample (tests/diff_v11.c). */
#ifndef LOOP_CORE_H
#define LOOP_CORE_H
#include <stdint.h>

#define LC_LOOPS 4
#define LC_STAGE_MIN 4096      /* smallest staging ring, frames */
#define LC_COMPACT_PAIRS 512   /* frame pairs folded per block, at least */
#define LC_BINS 256            /* waveform peak bins per loop and channel */
#define LC_DUB_BINS 32

enum { LC_EMPTY, LC_REC, LC_PLAY, LC_DUB, LC_ARM, LC_WORK };
enum { LC_PRESS = 1, LC_LEVEL, LC_SPEED, LC_REVERSE, LC_SYNC, LC_CLEAR, LC_PAUSE,
       LC_DOWN, LC_RELEASE, LC_CANCEL, LC_PAN, LC_SIZE };
#define LC_WAIT_SOUND 3       /* capture_sync of an armed first take: it starts at the first frame at or over LC_SOUND */
#define LC_SOUND 330u         /* about -40 dBFS in 16 bits */
#define LC_KEEP_WHOLE 1024    /* loop size, Q10 share of the take that plays */
#define LC_KEEP_SYNC_MIN 64   /* with sync: whole, 1/2, 1/4, 1/8, 1/16 */
#define LC_KEEP_FREE_STEP 32  /* without sync: 1/32 of the take a detent */

typedef struct { int64_t phase; int32_t step, delta; uint16_t remaining; } lc_transition;

typedef struct {
    int64_t  phase;                       /* Q14 frames */
    int32_t  gain, target_gain;           /* 0..32767 */
    int32_t  sum[2];
    int32_t  pan;                         /* -32768 left .. 0 centre .. 32768 right */
    uint32_t length, written, duration, play_start, sync_phase;
    uint32_t bin_shift;                   /* frames per peak bin = 1 << bin_shift, grows with the take */
    uint16_t peaks[2][LC_BINS];
    uint16_t dub_peaks[3][LC_DUB_BINS];
    int16_t  speed;                       /* 0..4 = -2..+2 octaves */
    uint8_t  state, reverse, capture_sync, quality, rec_phase, layer_count, layer_cursor;
    uint16_t keep;                        /* loop size: Q10 share of the take that plays, from its start */
    uint32_t span, dur, cycle;            /* stored / real frames that play; real frames into the whole take */
} lc_loop;

typedef struct {
    uint32_t chunk_shift;                 /* frames per chunk = 1 << chunk_shift */
    uint32_t loop_max;                    /* stored frames per loop before it halves its rate, 0 = no limit */
    uint32_t reserve_frames;              /* pool kept back for each empty loop */
} lc_config;

typedef struct {
    int16_t *audio, *stage;               /* audio == 0 means no memory: render passes dry */
    uint16_t *free_list, *table[LC_LOOPS];
    uint32_t chunk_shift, chunks, free_n, reserve, loop_max, stage_mask;
    uint32_t held[LC_LOOPS];              /* chunks owned by each loop */
    uint32_t frames, timeline, master_length, master_phase, activity;
    lc_loop  loop[LC_LOOPS];
    lc_transition trans[LC_LOOPS];
    int32_t  last_wet[LC_LOOPS][2], tail[LC_LOOPS][2];
    uint32_t meter[LC_LOOPS], meter_hold[LC_LOOPS];
    uint32_t compact_at, compact_pairs, compact_rate, stage_n, stage_head, stage_tail;
    int      compact_loop, scan_loop;
    uint32_t scan_bin, scan_at, scan_peak[2];
    uint8_t  sync, paused, error, resync;
} lc_engine;

typedef struct {
    uint32_t phase, length, duration, bin_shift;
    int32_t  gain, pan;
    int16_t  speed;
    uint8_t  state, reverse, quality, layer_count, layer_cursor;
    uint16_t keep;
    uint16_t output_peak;
    uint8_t  wave[2][LC_BINS], layer[3][LC_DUB_BINS];
} lc_display;

typedef struct {
    uint32_t capacity, free_frames;       /* pool size and free space, in stored frames */
    uint32_t master_length, master_phase, activity;
    uint8_t  sync, paused, error;
    lc_display loop[LC_LOOPS];
} lc_view;

/* cfg may be 0: 512-frame chunks, no per-loop limit, one eighth of the pool reserved per empty loop. */
void lc_init(lc_engine *e, void *memory, uint32_t bytes, const lc_config *cfg);
void lc_command(lc_engine *e, int kind, int index, int value);
void lc_render(lc_engine *e, int16_t *left, int16_t *right, int frames);
void lc_snapshot(lc_engine *e, lc_view *v);
#endif
