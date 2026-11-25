/* 4loop Field adapter — input mapping, reconciled to classic reelctl (reelloop-v11/reelctl/reelctl.c).
 * GPL-3.0-or-later + OP-1 firmware permission.
 *
 * Faithful to classic where classic defines the gesture; the only addition is pan (classic is mono, so it had
 * none — pan is the Field-stereo win and lives on a second modifier so it never collides):
 *
 *   encoder turn            -> LC_LEVEL, value = (delta/1024)*1024   (classic resolution: <1024 = no move)
 *   SHIFT + turn            -> LC_SPEED, value =  delta/1024          (classic)
 *   ALT   + turn            -> LC_PAN                                 (NEW, Field only)
 *   SIZE  + turn            -> LC_SIZE: right longer, left shorter (whole take down to 1/16; NEW)
 *   loop trigger press      -> LC_DOWN(value=shift): plain = rec/finish/dub, SHIFT = reverse  (classic)
 *   loop trigger hold >=1s   -> LC_CLEAR   (classic: 400,000,000 cycles ~ 1 s; see FP_HOLD_MS)
 *   loop trigger release    -> LC_RELEASE for a plain short press (core no-op; classic)
 *   SYNC key                -> LC_SYNC (global)
 *   PLAY / STOP keys        -> LC_PAUSE 0 / 1 (global)
 *
 * reelctl drove level/speed off the raw encoder delta and the record/clear state off the four loop
 * keys with a cycle-timestamp hold clock; this reproduces both. The raw delta and the ~1 s hold are
 * the two things the coordinator asked to reconcile — both now come straight from reelctl. */
#include "field_plugin.h"

#define PAN_STEP 2048   /* pan units per 1024 of delta (Field-only; -32768..32768 full scale) */

static void cmd(field_plugin *fp, int kind, int index, int value)
{
    fp->intro_done = 1;                 /* first real interaction dismisses the intro (classic) */
    lc_command(&fp->engine, kind, index, value);
}

static uint32_t now(field_plugin *fp)
{
    return fp->host.now_ms ? fp->host.now_ms(fp->host.ctx) : fp->last_ms;
}

/* Fire CLEAR on any loop whose trigger has been held past the threshold. classic runs this off its
 * SERVICE event so an idle audio thread can't turn a late tap into a clear; we run it off the tick
 * (fp_screen, or a loader timer) for the same reason. */
void fp_tick(field_plugin *fp)
{
    if (!fp->ready) return;
    uint32_t t = now(fp);
    for (int k = 0; k < LC_LOOPS; k++)
        if (fp->held[k] == 1 && (uint32_t)(t - fp->held_since[k]) >= FP_HOLD_MS) {
            cmd(fp, LC_CLEAR, k, 0);
            fp->held[k] = 2;                 /* consumed: release won't re-fire */
        }
}

int fi_map(field_plugin *fp, const field_event *ev)
{
    switch (ev->kind) {
    case FE_KEY:
        switch (ev->a) {
        case FK_SHIFT: fp->shift = ev->b != 0; return 0;
        case FK_ALT:   fp->alt   = ev->b != 0; return 0;
        case FK_SIZE:  fp->size  = ev->b != 0; return 0;
        case FK_INFO:  fp->details = ev->b != 0; if (ev->b) fp->intro_done = 1; return 0;  /* held info-view */
        default: break;
        }
        if (!ev->b) return 0;                 /* act on key-down only */
        switch (ev->a) {
        case FK_PLAY:    cmd(fp, LC_PAUSE, 0, 0); return 1;
        case FK_STOP:    cmd(fp, LC_PAUSE, 0, 1); return 1;
        case FK_SYNC:    cmd(fp, LC_SYNC, 0, 0);  return 1;
        case FK_REC:     cmd(fp, LC_DOWN, fp->sel, fp->shift); return 1;   /* same path as a trigger */
        case FK_REVERSE: cmd(fp, LC_REVERSE, fp->sel, 0); return 1;        /* explicit, for loaders that have it */
        case FK_CLEAR:   cmd(fp, LC_CLEAR,   fp->sel, 0); return 1;
        default: return 0;
        }

    case FE_ENC_PRESS: {                      /* loop trigger down */
        int k = ev->a;
        if ((unsigned)k >= LC_LOOPS) return 0;
        fp->sel = (uint8_t)k;
        if (fp->held[k]) return 1;            /* already down */
        cmd(fp, LC_DOWN, k, fp->shift);       /* plain = press state machine; shift = reverse */
        fp->held[k] = fp->shift ? 3 : 1;
        fp->held_since[k] = now(fp);
        return 1;
    }

    case FE_ENC_RELEASE: {                     /* loop trigger up */
        int k = ev->a;
        if ((unsigned)k >= LC_LOOPS) return 0;
        if (fp->held[k] == 1) cmd(fp, LC_RELEASE, k, 0);   /* plain short press; core no-op */
        fp->held[k] = 0;
        return 1;
    }

    case FE_ENC_TURN: {
        int k = ev->a;
        if ((unsigned)k >= LC_LOOPS) return 0;
        fp->sel = (uint8_t)k;
        int delta = ev->b;
        int steps = delta / 1024;             /* classic quantization */
        if (fp->size)       { if (steps) cmd(fp, LC_SIZE, k, steps > 0 ? 1 : -1); }   /* NEW: one halving a detent */
        else if (fp->alt)   cmd(fp, LC_PAN,   k, steps * PAN_STEP);   /* NEW */
        else if (fp->shift) cmd(fp, LC_SPEED, k, steps);             /* classic */
        else if (fp->level_external) fp->level_made[k] = fp->level_made[k] + steps;   /* the loader sets the level */
        else                cmd(fp, LC_LEVEL, k, steps * 1024);      /* classic */
        return 1;
    }
    default:
        return 0;
    }
}
