/* 4loop.pack glue that does not depend on the unit. See fourloop_glue.h.
 * GPL-3.0-or-later + OP-1 firmware permission. */
#include "fourloop_glue.h"

#define FL_BARRIER() __asm__ __volatile__("" ::: "memory")

fl_state fl;

static int fl_in(uintptr_t v, uintptr_t lo, uintptr_t hi) { return v >= lo && v < hi; }

int fl_hook_step(fl_hook *h, const fl_env *env, int enable)
{
    uintptr_t ours[2] = { (uintptr_t)&h->copy[0][FL_VT_BEFORE], (uintptr_t)&h->copy[1][FL_VT_BEFORE] };
    uintptr_t obj = *env->slot;

    if (!enable) {                                            /* put stock's vtable back where we wrapped it */
        if (h->obj && obj == h->obj) {
            volatile uintptr_t *o = (volatile uintptr_t *)obj;
            uintptr_t vt = *o;
            if (vt == ours[0] || vt == ours[1]) *o = h->stock_vt;
        }
        h->obj = 0;
        return h->state = FL_HOOK_OFF;
    }
    if (!obj) return h->state = FL_HOOK_NO_FX;
    if (!fl_in(obj, env->obj_lo, env->obj_hi) || (obj & 3u)) { h->refusals++; return h->state = FL_HOOK_REFUSED; }

    volatile uintptr_t *o = (volatile uintptr_t *)obj;
    uintptr_t vt = *o;
    if (vt == ours[0] || vt == ours[1])                       /* still ours: nothing to do */
        return h->state = (env->active && !*env->active) ? FL_HOOK_NO_FX : FL_HOOK_ON;

    if (!fl_in(vt, env->vt_lo, env->vt_hi) || (vt & 3u)) { h->refusals++; return h->state = FL_HOOK_REFUSED; }
    const uintptr_t *v = (const uintptr_t *)vt;
    uintptr_t proc = v[2];
    if (!fl_in(proc, env->code_lo[0], env->code_hi[0]) && !fl_in(proc, env->code_lo[1], env->code_hi[1])) {
        h->refusals++; return h->state = FL_HOOK_REFUSED;
    }

    int k = h->next & 1; h->next = (uint8_t)(k ^ 1);
    for (int i = 0; i < FL_VT_BEFORE + FL_VT_AFTER; i++) h->copy[k][i] = v[i - FL_VT_BEFORE];
    h->copy[k][FL_VT_BEFORE + 2] = (uintptr_t)&fl_process;
    h->orig_process[k] = proc;
    h->stock_vt = vt;
    h->obj = obj;
    FL_BARRIER();
    *o = ours[k];                                             /* one aligned word: the audio thread sees old or new */
    h->wraps++;
    return h->state = (env->active && !*env->active) ? FL_HOOK_NO_FX : FL_HOOK_ON;
}

void fl_process(void *self, int32_t *left, int32_t *right, void *ctx)
{
    fl_hook *h = &fl.hook;
    h->calls++;
    uintptr_t vt = *(volatile uintptr_t *)self;
    int k = vt == (uintptr_t)&h->copy[1][FL_VT_BEFORE] ? 1 : 0;
    fl_process_fn stock = (fl_process_fn)h->orig_process[k];
    if (!h->replace && stock) stock(self, left, right, ctx);
    fl_audio(left, right, FL_BLOCK);
}

static int32_t sat_add(int32_t a, int32_t b)
{
    int32_t s = (int32_t)((uint32_t)a + (uint32_t)b);
    if (a >= 0 && b >= 0 && s < 0) return 0x7fffffff;
    if (a < 0 && b < 0 && s >= 0) return (int32_t)0x80000000u;
    return s;
}

void fl_audio(int32_t *left, int32_t *right, int frames)
{
    fl_queue *q = &fl.q;
    if (frames > FL_BLOCK) frames = FL_BLOCK;
    while (q->tail != q->head) {                              /* the key thread's events, on the core's thread */
        FL_BARRIER();
        if (fl.created) fp_event(&fl.fp, &q->ev[q->tail % FL_QUEUE]);
        q->tail = q->tail + 1;
    }
    int32_t peak = fl.hook.peak;
    for (int i = 0; i < frames; i++) { int32_t a = left[i] < 0 ? -(left[i] + 1) : left[i]; if (a > peak) peak = a; }
    fl.hook.peak = peak;
    if (!fl.created) return;

    fp_tick(&fl.fp);
    for (int i = 0; i < frames; i++) {                        /* Q31 -> 16 bits for the core */
        fl.in_l[i] = fl.out_l[i] = (int16_t)(left[i] >> 16);
        fl.in_r[i] = fl.out_r[i] = (int16_t)(right[i] >> 16);
    }
    fp_audio(&fl.fp, fl.out_l, fl.out_r, frames);
    for (int i = 0; i < frames; i++) {                        /* add back only what 4loop changed */
        int32_t dl = (int32_t)fl.out_l[i] - fl.in_l[i], dr = (int32_t)fl.out_r[i] - fl.in_r[i];
        if (dl) { left[i]  = sat_add(sat_add(left[i],  dl * 32768), dl * 32768); }
        if (dr) { right[i] = sat_add(sat_add(right[i], dr * 32768), dr * 32768); }
    }
}

int32_t fl_level_gain(int value)
{
    if (value <= 0) return 0;
    if (value >= FL_LEVEL_TOP) return 32767;
    return value * 1024;
}

void fl_effect(int32_t *left, int32_t *right, int frames, const int *values, int count)
{
    fl.hook.calls++;
    if (fl.created && fl.fp.ready && values)
        for (int i = 0; i < count && i < LC_LOOPS; i++) fl.fp.engine.loop[i].target_gain = fl_level_gain(values[i]);
    fl_audio(left, right, frames);
}

int fl_push(fl_queue *q, int kind, int a, int b)
{
    uint32_t head = q->head;
    if (head - q->tail >= FL_QUEUE) { q->dropped++; return 0; }
    field_event *e = &q->ev[head % FL_QUEUE];
    e->kind = kind; e->a = a; e->b = b;
    FL_BARRIER();
    q->head = head + 1;
    return 1;
}

uint32_t fl_isqrt(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

void fl_line_matrix(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int *m)
{
    int32_t dx = x1 - x0, dy = y1 - y0;                       /* Q4; the node is 64 px long */
    uint32_t len = fl_isqrt((uint32_t)(dx * dx) + (uint32_t)(dy * dy));
    m[0] = dx; m[1] = dy; m[2] = 0;                           /* x axis: 64 px -> (dx, dy) px, i.e. dx_q4 1024ths */
    if (len) { m[3] = -dy * 1024 / (int32_t)len; m[4] = dx * 1024 / (int32_t)len; }
    else     { m[3] = 0; m[4] = 1024; }                       /* y axis: unit perpendicular, so the stroke keeps its width */
    m[5] = 0; m[6] = x0 * 64; m[7] = y0 * 64; m[8] = 1024;
}

void fl_circle_matrix(int32_t cx, int32_t cy, int32_t radius, int *m)
{
    m[0] = radius; m[1] = 0; m[2] = 0;                        /* r = 64 px node: scale radius_px / 64 = radius_q4 / 1024 */
    m[3] = 0; m[4] = radius; m[5] = 0;
    m[6] = cx * 64; m[7] = cy * 64; m[8] = 1024;
}
