/* 4loop Field adapter — lifecycle. Wires the core and the scene to a field_host.
 * GPL-3.0-or-later + OP-1 firmware permission. See field_plugin.h for the contract. */
#include "field_plugin.h"

/* Memory defaults (overridable via field_config). Start modest, ask for the "CCES heap 1" pool as
 * the ceiling; the loader grants what it can and we validate the real figure on-device with a RAM
 * diff (INTEGRATION.md §8). 24 MiB is the Discord estimate for that free pool, not yet proven. */
#define MEM_MIN_DEFAULT  (256u * 1024u)
#define MEM_WANT_DEFAULT (4u   * 1024u * 1024u)
#define MEM_MAX_DEFAULT  (24u  * 1024u * 1024u)

void fp_create(field_plugin *fp, const field_host *host, const field_config *cfg)
{
    field_config def = {0};
    const field_config *c = cfg ? cfg : &def;

    fp->host  = *host;
    fp->shift = fp->alt = fp->size = 0;
    fp->sel   = 0;
    for (int k = 0; k < LC_LOOPS; k++) { fp->held[k] = 0; fp->held_since[k] = 0; }
    fp->details = fp->intro_done = 0; fp->intro_ms = 0;
    fp->master = c->master_slot ? 1 : 0;
    fp->block = 0; fp->block_bytes = 0;
    fp->ready = 0;

    uint32_t mn = c->mem_min  ? c->mem_min  : MEM_MIN_DEFAULT;
    uint32_t mw = c->mem_want ? c->mem_want : MEM_WANT_DEFAULT;
    uint32_t mx = c->mem_max  ? c->mem_max  : MEM_MAX_DEFAULT;
    if (mw < mn) mw = mn;
    if (mx < mw) mx = mw;

    uint32_t got = 0;
    if (fp->host.mem_get) fp->block = fp->host.mem_get(fp->host.ctx, mn, mw, mx, &got);
    fp->block_bytes = fp->block ? got : 0;

    /* lc_init tolerates block==0 / bytes==0 (audio passthrough, empty scene). */
    lc_init(&fp->engine, fp->block, fp->block_bytes, (c->core.chunk_shift || c->core.loop_max || c->core.reserve_frames) ? &c->core : 0);
    ls_init(&fp->scene);

    fp->last_ms = fp->host.now_ms ? fp->host.now_ms(fp->host.ctx) : 0;
    fp->ready = 1;
}

void fp_audio(field_plugin *fp, int16_t *left, int16_t *right, int frames)
{
    if (!fp->ready) return;
    lc_render(&fp->engine, left, right, frames);
}

void fp_event(field_plugin *fp, const field_event *ev)
{
    if (!fp->ready || !ev) return;
    fi_map(fp, ev);
}

void fp_destroy(field_plugin *fp)
{
    if (fp->block && fp->host.mem_release) fp->host.mem_release(fp->host.ctx, fp->block);
    fp->block = 0; fp->block_bytes = 0; fp->ready = 0;
}
