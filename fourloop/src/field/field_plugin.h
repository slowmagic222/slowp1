/* 4loop Field adapter — the plugin surface a loader drives.
 * GPL-3.0-or-later + OP-1 firmware permission.
 *
 * Lifecycle, all from the loader:
 *   fp_create(fp, host, cfg)        once, on load. Negotiates memory via host->mem_get and inits
 *                                   the core and the scene. Safe to call with host->mem_get
 *                                   returning 0: runs dry (audio passthrough, empty scene).
 *   fp_audio(fp, left, right, n)    once per audio block, from the audio thread. n is frames
 *                                   (the v11 timing assumes 64). In-place stereo; mono loaders
 *                                   pass left==right.
 *   fp_event(fp, &ev)              for every physical control event (encoder turn/press, key).
 *                                   From the audio thread or a control thread — it only enqueues
 *                                   lc_command, which the core already expects from one thread.
 *   fp_screen(fp)                  once per display frame, from the screen thread. Rebuilds the
 *                                   scene from a snapshot and replays it through host->circle/...
 *   fp_destroy(fp)                 on unload. Releases memory via host->mem_release.
 *
 * The adapter holds the engine, the scene, and a tiny input-mode state machine. It does not own a
 * loader format; a loader of any kind (faust .pack, open-xy machine, our splice, the harness)
 * implements field_host and calls these five functions. */
#ifndef FIELD_PLUGIN_H
#define FIELD_PLUGIN_H
#include "field_host.h"
#include "../core/loop_core.h"
#include "../core/loop_scene.h"

/* Physical Field events the loader feeds in. Kept abstract so the exact key matrix / encoder
 * resolution of a given loader doesn't leak into the adapter. The press/turn semantics below are
 * reconciled to classic reelctl (reelloop-v11/reelctl/reelctl.c):
 *   - FE_ENC_TURN b = RAW encoder delta in classic op1_input units (NOT detents). The adapter applies
 *     reelctl's exact formulas: level += (delta/1024)*1024, speed += delta/1024. So a turn under
 *     1024 of accumulated delta moves nothing — that is the classic resolution. A loader with only
 *     coarse detents multiplies each detent by its raw-delta weight before sending.
 *   - FE_ENC_PRESS / FE_ENC_RELEASE are the four loop triggers (classic keys 0x3b..0x3e). The action
 *     fires on PRESS as LC_DOWN(value=shift): plain press = the rec/finish/dub state machine,
 *     SHIFT+press = reverse. Holding a trigger >= FP_HOLD_MS escalates to CLEAR. A plain short
 *     press sends LC_RELEASE on up (core no-op). The hold clock needs fp_tick() or fp_screen()
 *     pumped so a held key clears even with no further events (classic does this in its SERVICE event). */
enum {
    FE_ENC_TURN = 1,   /* a: encoder 0..3   b: raw delta (classic op1_input units) */
    FE_ENC_PRESS,      /* a: encoder 0..3   loop trigger down */
    FE_ENC_RELEASE,    /* a: encoder 0..3   loop trigger up */
    FE_KEY             /* a: fe_key id      b: 1 down / 0 up */
};

/* Hold-to-clear threshold. classic reelctl fires CLEAR at 400,000,000 CYCLES, which is ~1.0 s at the
 * original OP-1's ~400 MHz Blackfin core clock. Expressed in ms here; tune on hardware. */
#define FP_HOLD_MS 1000u

/* fe_key ids: the transport/modifier keys 4loop uses. A loader maps its own scancodes to these.
 * SHIFT/ALT are held modifiers that re-route encoder turns (see field_input.c). */
enum { FK_SHIFT = 1, FK_ALT, FK_PLAY, FK_STOP, FK_REC, FK_SYNC, FK_REVERSE, FK_CLEAR, FK_INFO, FK_SIZE };

typedef struct { int kind, a, b; } field_event;

/* Tuning the loader may override; 0 = defaults. bytes_* bound the memory negotiation. */
typedef struct {
    uint32_t mem_min, mem_want, mem_max;   /* 0 => 256 KiB / 4 MiB / 24 MiB (see field_plugin.c) */
    lc_config core;                        /* forwarded to lc_init; 0 fields => core defaults */
    uint8_t   master_slot;                 /* 1 = this instance is the master (true stereo); else mono */
} field_config;

typedef struct {
    field_host host;
    lc_engine  engine;
    ls_scene   scene;
    void      *block; uint32_t block_bytes;
    uint32_t   last_ms;
    uint8_t    shift, alt, ready, master;  /* modifier state + flags */
    uint8_t    size;                       /* FK_SIZE held: a turn sets the loop's size */
    uint8_t    sel;                        /* loop targeted by per-loop keys: last encoder touched */
    uint8_t    held[LC_LOOPS];             /* 0 idle, 1 plain press pending, 2 consumed by clear, 3 shift/reverse */
    uint32_t   held_since[LC_LOOPS];       /* ms at press, for the hold-to-clear clock */
    uint8_t    details, intro_done;        /* held info-view active; first-open intro finished */
    uint32_t   intro_ms;                   /* elapsed ms of the intro fade */
    uint8_t    level_external;             /* 1: a plain turn is counted in level_made, for a loader that owns the levels */
    volatile int32_t level_made[LC_LOOPS]; /* level steps (1024ths of full) turned so far; written here only */
} field_plugin;

void fp_create (field_plugin *fp, const field_host *host, const field_config *cfg);
void fp_audio  (field_plugin *fp, int16_t *left, int16_t *right, int frames);
void fp_event  (field_plugin *fp, const field_event *ev);
void fp_screen (field_plugin *fp);
void fp_tick   (field_plugin *fp);   /* advance the hold-to-clear clock; called by fp_screen, or from a loader timer */
void fp_destroy(field_plugin *fp);

/* exposed for the input unit (and tests): translate one abstract event to a core command.
 * Returns 1 if it issued a command, 0 if the event was swallowed (e.g. a modifier key). */
int  fi_map(field_plugin *fp, const field_event *ev);

#endif
