/* 4loop portable stereo core. See loop_core.h.
 * Derived from reelloop-v11 (4loop 0.11). GPL-3.0-or-later + OP-1 firmware permission. */
#include "loop_core.h"

static const int32_t lc_rates[5] = { 4096, 8192, 16384, 32768, 65536 };

static int lc_limit(int x, int a, int b) { return x < a ? a : x > b ? b : x; }
static int16_t lc_sat(int32_t x) { return x < -32768 ? -32768 : x > 32767 ? 32767 : (int16_t)x; }
static uint32_t lc_abs(int32_t v) { return v < 0 ? (uint32_t)-v : (uint32_t)v; }
/* Frame `at` of a loop, through its chunk table. */
static int16_t *lc_frame(const lc_engine *e, int index, uint32_t at)
{
    uint32_t chunk = e->table[index][at >> e->chunk_shift];
    return e->audio + (((chunk << e->chunk_shift) + (at & ((1u << e->chunk_shift) - 1))) << 1);
}

/* Keep the first `keep` chunks of a loop and hand the rest back to the pool. */
static void lc_trim(lc_engine *e, int index, uint32_t keep)
{
    while (e->held[index] > keep) e->free_list[e->free_n++] = e->table[index][--e->held[index]];
}

/* Make sure frame `at` has storage. Empty loops keep a reservation the others cannot take. */
static int lc_ensure(lc_engine *e, int index, uint32_t at)
{
    uint32_t chunk = at >> e->chunk_shift, kept = 0;
    if (chunk < e->held[index]) return 1;
    for (int j = 0; j < LC_LOOPS; j++)
        if (j != index && (e->loop[j].state == LC_EMPTY || e->loop[j].state == LC_ARM)) kept += e->reserve;
    if (e->free_n <= kept) return 0;
    e->table[index][e->held[index]++] = e->free_list[--e->free_n];
    return 1;
}

static void reset_loop(lc_loop *l)
{
    l->phase = 0;
    l->sum[0] = l->sum[1] = 0;
    l->length = l->written = l->duration = l->play_start = l->sync_phase = 0;
    l->bin_shift = 9;
    l->gain = l->target_gain = 32767;
    l->pan = 0;
    l->speed = 2;
    l->state = LC_EMPTY;
    l->reverse = l->capture_sync = l->quality = l->rec_phase = 0;
    l->keep = LC_KEEP_WHOLE; l->span = l->dur = l->cycle = 0;
    l->layer_count = 0;
    l->layer_cursor = 2;
    for (int j = 0; j < 3; j++) for (int i = 0; i < LC_DUB_BINS; i++) l->dub_peaks[j][i] = 0;
    for (int c = 0; c < 2; c++) for (int i = 0; i < LC_BINS; i++) l->peaks[c][i] = 0;
}

void lc_init(lc_engine *e, void *memory, uint32_t bytes, const lc_config *cfg)
{
    e->audio = 0; e->stage = 0; e->free_list = 0;
    e->chunk_shift = cfg && cfg->chunk_shift ? cfg->chunk_shift : 9;
    e->chunks = e->free_n = e->reserve = 0; e->loop_max = cfg ? cfg->loop_max : 0; e->stage_mask = 0;
    e->frames = e->timeline = e->master_length = e->master_phase = e->activity = 0;
    e->sync = 1; e->paused = e->error = e->resync = 0;
    e->compact_loop = e->scan_loop = -1;
    e->scan_bin = e->scan_at = 0; e->scan_peak[0] = e->scan_peak[1] = 0;
    e->compact_at = e->compact_pairs = e->stage_n = e->stage_head = e->stage_tail = 0; e->compact_rate = LC_COMPACT_PAIRS;
    for (int i = 0; i < LC_LOOPS; i++) {
        e->trans[i].remaining = 0; e->trans[i].phase = 0; e->trans[i].step = e->trans[i].delta = 0;
        reset_loop(&e->loop[i]); e->held[i] = 0; e->table[i] = 0;
        e->last_wet[i][0] = e->last_wet[i][1] = e->tail[i][0] = e->tail[i][1] = 0;
        e->meter[i] = e->meter_hold[i] = 0;
    }
    if (!memory || e->chunk_shift < 6 || e->chunk_shift > 16) return;   /* render passes dry */
    /* Layout: [audio chunks][stage ring][free list][4 chunk tables].
     * The stage holds what arrives while a take folds itself: one frame per 16 folded pairs. */
    uint32_t stage_frames = LC_STAGE_MIN;
    while (stage_frames < bytes / 4 / 32) stage_frames <<= 1;
    uint32_t chunk_bytes = 4u << e->chunk_shift, per_chunk = chunk_bytes + 2u * (1 + LC_LOOPS);
    if (bytes < stage_frames * 4 + 8 * per_chunk) return;
    uint32_t chunks = (bytes - stage_frames * 4) / per_chunk; if (chunks > 65535) chunks = 65535;
    int16_t *audio = (int16_t *)memory;
    e->stage = audio + (chunks << e->chunk_shift) * 2u; e->stage_mask = stage_frames - 1;
    e->free_list = (uint16_t *)(e->stage + stage_frames * 2u);
    for (int i = 0; i < LC_LOOPS; i++) e->table[i] = e->free_list + chunks * (uint32_t)(1 + i);
    for (uint32_t k = 0; k < chunks; k++) e->free_list[k] = (uint16_t)(chunks - 1 - k);   /* chunk 0 goes out first */
    e->chunks = e->free_n = chunks;
    e->reserve = cfg ? (cfg->reserve_frames + (1u << e->chunk_shift) - 1) >> e->chunk_shift : chunks / 8;
    if (e->reserve > chunks / LC_LOOPS) e->reserve = chunks / LC_LOOPS;
    e->audio = audio;
}

/* Peak bins widen as a take grows, so a short loop keeps a detailed waveform. */
static void peak(lc_loop *l, uint32_t at, int ch, int16_t value)
{
    uint32_t v = lc_abs(value);
    while ((at >> l->bin_shift) >= LC_BINS) {
        for (int c = 0; c < 2; c++) for (int k = 0; k < LC_BINS; k++) {
            uint16_t a = k < LC_BINS / 2 ? l->peaks[c][2 * k] : 0, b = k < LC_BINS / 2 ? l->peaks[c][2 * k + 1] : 0;
            l->peaks[c][k] = a > b ? a : b;
        }
        for (int j = 0; j < 3; j++) for (int k = 0; k < LC_DUB_BINS; k++) {
            uint16_t a = k < LC_DUB_BINS / 2 ? l->dub_peaks[j][2 * k] : 0, b = k < LC_DUB_BINS / 2 ? l->dub_peaks[j][2 * k + 1] : 0;
            l->dub_peaks[j][k] = a > b ? a : b;
        }
        l->bin_shift++;
    }
    uint32_t bin = at >> l->bin_shift;
    if (v > l->peaks[ch][bin]) l->peaks[ch][bin] = (uint16_t)v;
}

static int stored(lc_engine *e, int index, int16_t vl, int16_t vr)
{
    lc_loop *l = &e->loop[index];
    if (!lc_ensure(e, index, l->written)) return 0;
    int16_t *t = lc_frame(e, index, l->written);
    t[0] = vl; t[1] = vr;
    peak(l, l->written, 0, vl); peak(l, l->written, 1, vr);
    l->written++;
    return 1;
}

static void ready_to_play(lc_engine *e, int index)
{
    lc_loop *l = &e->loop[index];
    l->length = l->written;
    if (l->duration < 2205 || l->length < 64) { l->state = LC_EMPTY; l->length = l->written = l->duration = 0; lc_trim(e, index, 0); return; }
    lc_trim(e, index, (l->length + (1u << e->chunk_shift) - 1) >> e->chunk_shift);
    uint64_t period = (uint64_t)l->length << 14;
    uint32_t elapsed = e->timeline - l->play_start;
    uint64_t phase = (((uint64_t)elapsed * (uint32_t)lc_rates[l->speed]) >> l->quality) % period;
    l->sync_phase = l->cycle = elapsed % l->duration;
    l->keep = LC_KEEP_WHOLE; l->span = l->length; l->dur = l->duration;
    l->phase = l->reverse ? (int64_t)(period - 1 - phase) : (int64_t)phase;
    l->state = LC_PLAY;
}

static void finish(lc_engine *e, int index)
{
    lc_loop *l = &e->loop[index];
    if (l->state != LC_REC) return;
    l->play_start = e->timeline;
    if (l->duration >= 2205 && !e->master_length) { e->master_length = l->duration; e->master_phase = 0; }
    if (e->compact_loop == index) { l->state = LC_WORK; return; }
    ready_to_play(e, index);
}

/* Exactly one live writer. Finalizing an earlier compaction does not keep capturing it. */
static void take_writer(lc_engine *e, int index)
{
    for (int i = 0; i < LC_LOOPS; i++) if (i != index) {
        lc_loop *l = &e->loop[i];
        if (l->state == LC_DUB) l->state = LC_PLAY;
        else if (l->state == LC_REC) finish(e, i);
        else if (l->state == LC_ARM) l->state = LC_EMPTY;
    }
}

/* The musical head/clock stay exact. A temporary read head supplies the tape
 * gesture and fades into the exact head, so glides never accumulate timing drift. */
static void start_transition(lc_engine *e, int i, int target)
{
    lc_loop *l = &e->loop[i]; lc_transition *t = &e->trans[i];
    if (l->state != LC_PLAY && l->state != LC_DUB) return;
    int old = t->remaining ? t->step : (lc_rates[l->speed] >> l->quality) * 4096;
    if (!t->remaining) t->phase = l->phase;
    t->step = old; t->delta = (target * 4096 - old) / 10240; t->remaining = 10240;
}

/* The part of a take that plays (set_keep): its first span stored frames, dur real ones. */
static inline uint32_t span_of(const lc_loop *l) { return l->span; }
static inline uint32_t dur_of(const lc_loop *l) { return l->dur; }

/* Interpolated, seam-faded stereo frame at a Q14 phase. */
static void read_frame(lc_engine *e, int i, int64_t phase, int32_t out[2])
{
    lc_loop *l = &e->loop[i];
    uint32_t span = span_of(l), at = (uint32_t)(phase >> 14), next = at + 1; if (next >= span) next = 0;
    const int16_t *a = lc_frame(e, i, at), *b = lc_frame(e, i, next);
    int32_t frac = (int32_t)(phase & 16383);
    int32_t edge = (int32_t)(at < span - at ? at : span - at), seam = 24 >> l->quality; if (seam < 1) seam = 1;
    for (int c = 0; c < 2; c++) {
        int32_t v = a[c] + ((((int32_t)b[c] - a[c]) * frac) >> 14);
        if (edge < seam) v = v * edge * (1 << l->quality) / 24;
        out[c] = v;
    }
}

static void tape_sample(lc_engine *e, int i, int32_t out[2])
{
    lc_loop *l = &e->loop[i]; lc_transition *t = &e->trans[i];
    out[0] = out[1] = 0;
    if (!t->remaining || span_of(l) < 64) return;
    int32_t v[2]; read_frame(e, i, t->phase, v);
    int32_t step = t->step >> 12; int64_t period = (int64_t)span_of(l) << 14;
    t->phase += l->reverse ? -step : step;
    if (t->phase < 0) t->phase += period;
    if (t->phase >= period) t->phase -= period;
    t->step += t->delta; t->remaining--;
    for (int c = 0; c < 2; c++) out[c] = lc_sat((v[c] * l->gain) >> 14);
}

/* Plays the first keep/1024 of a take, at least 64 stored and 2205 real frames; the playhead stays where it is if it fits. */
static void set_keep(lc_engine *e, int index, int keep)
{
    lc_loop *l = &e->loop[index];
    if (l->state != LC_PLAY && l->state != LC_DUB) return;
    while (keep < LC_KEEP_WHOLE && ((uint32_t)(((uint64_t)l->length * (uint32_t)keep) >> 10) < 64 || (uint32_t)(((uint64_t)l->duration * (uint32_t)keep) >> 10) < 2205)) keep *= 2;
    if (keep > LC_KEEP_WHOLE) keep = LC_KEEP_WHOLE;
    if (keep == l->keep) return;
    l->keep = (uint16_t)keep;
    l->span = keep == LC_KEEP_WHOLE ? l->length : (uint32_t)(((uint64_t)l->length * (uint32_t)keep) >> 10);
    l->dur = keep == LC_KEEP_WHOLE ? l->duration : (uint32_t)(((uint64_t)l->duration * (uint32_t)keep) >> 10);
    int64_t period = (int64_t)l->span << 14;
    l->phase %= period;
    e->trans[index].phase %= period;
    l->sync_phase = l->cycle % l->dur;
}

void lc_command(lc_engine *e, int kind, int index, int value)
{
    if ((unsigned)index >= LC_LOOPS || !e->audio) return;
    if (kind == LC_RELEASE || kind == LC_CANCEL) return;
    if (kind == LC_DOWN) { lc_command(e, value ? LC_REVERSE : LC_PRESS, index, 0); return; }
    e->activity++;
    lc_loop *l = &e->loop[index];
    if (kind == LC_PRESS) {
        if (l->state == LC_EMPTY) {
            take_writer(e, index); lc_trim(e, index, 0); l->bin_shift = 9;
            l->written = l->length = l->duration = 0; l->sum[0] = l->sum[1] = 0; l->quality = l->rec_phase = 0; l->keep = LC_KEEP_WHOLE; l->span = l->dur = l->cycle = 0;
            for (int c = 0; c < 2; c++) for (int b = 0; b < LC_BINS; b++) l->peaks[c][b] = 0;
            int first = 1;                       /* Field: the very first take waits for sound (classic 0.11's start) */
            for (int j = 0; j < LC_LOOPS; j++) if (j != index && e->loop[j].state != LC_EMPTY && e->loop[j].state != LC_ARM) first = 0;
            l->capture_sync = e->sync && e->master_length ? 1 : first ? LC_WAIT_SOUND : 0;
            l->state = l->capture_sync ? LC_ARM : LC_REC;
            e->paused = 0;
        }
        else if (l->state == LC_ARM) l->state = LC_EMPTY;
        else if (l->state == LC_REC) { if (l->capture_sync && e->master_phase) l->capture_sync = 2; else finish(e, index); }
        else if (l->state == LC_PLAY) {
            take_writer(e, index); l->state = LC_DUB;
            l->layer_cursor = (l->layer_cursor + 1) % 3; if (l->layer_count < 3) l->layer_count++;
            for (int b = 0; b < LC_DUB_BINS; b++) l->dub_peaks[l->layer_cursor][b] = 0;
        }
        else if (l->state == LC_DUB) l->state = LC_PLAY;
    }
    if (kind == LC_LEVEL) l->target_gain = lc_limit(l->target_gain + value, 0, 32767);
    if (kind == LC_PAN) l->pan = lc_limit(l->pan + value, -32768, 32768);
    if (kind == LC_SPEED) {
        int speed = lc_limit(l->speed + value, 0, 4);
        if (speed != l->speed && !e->paused) start_transition(e, index, lc_rates[speed] >> l->quality);
        l->speed = (int16_t)speed;
    }
    if (kind == LC_SIZE && (l->state == LC_PLAY || l->state == LC_DUB)) {   /* value > 0 longer, < 0 shorter */
        int keep = l->keep;
        if (e->sync) keep = value > 0 ? keep * 2 : keep / 2;                 /* with sync: halves of the take */
        else keep += value > 0 ? LC_KEEP_FREE_STEP : -LC_KEEP_FREE_STEP;      /* free: any share, 1/32 a detent */
        keep = lc_limit(keep, e->sync ? LC_KEEP_SYNC_MIN : LC_KEEP_FREE_STEP, LC_KEEP_WHOLE);
        set_keep(e, index, keep);
    }
    if (kind == LC_REVERSE) l->reverse ^= 1;
    if (kind == LC_SYNC) {
        e->sync ^= 1;
        if (e->sync) {                                                        /* back on the grid: a free size drops to its half below */
            e->resync = 1;
            for (int i = 0; i < LC_LOOPS; i++) { int k = LC_KEEP_WHOLE; while (k > e->loop[i].keep) k /= 2; if (k < LC_KEEP_SYNC_MIN) k = LC_KEEP_SYNC_MIN; set_keep(e, i, k); }
        }
    }
    if (kind == LC_PAUSE) {
        if (value && !e->paused) { for (int i = 0; i < LC_LOOPS; i++) start_transition(e, i, 0); }
        else if (!value) { for (int i = 0; i < LC_LOOPS; i++) e->trans[i].remaining = 0; }
        e->paused = value != 0;
        for (int i = 0; i < LC_LOOPS; i++) {
            if (e->loop[i].state == LC_REC) finish(e, i);
            else if (e->loop[i].state == LC_ARM) e->loop[i].state = LC_EMPTY;
        }
    }
    if (kind == LC_CLEAR) {
        e->trans[index].remaining = 0;
        if (e->scan_loop == index) e->scan_loop = -1;
        if (e->compact_loop == index) { e->compact_loop = -1; e->stage_n = 0; }
        lc_trim(e, index, 0);
        for (int c = 0; c < 2; c++) { e->tail[index][c] = e->last_wet[index][c]; e->last_wet[index][c] = 0; }
        int gain = l->target_gain, pan = l->pan;
        reset_loop(l); l->gain = l->target_gain = gain; l->pan = pan;
        int any = 0; for (int i = 0; i < LC_LOOPS; i++) if (e->loop[i].state != LC_EMPTY) any = 1;
        if (!any) e->master_length = e->master_phase = 0;
    }
}

/* Recording is not played while compacting; other loops continue at their own rates.
 * Incremental in-place frame pairs preserve duration. Incoming new-rate frames use the stage. */
static int start_compact(lc_engine *e, int index)
{
    lc_loop *l = &e->loop[index];
    if (l->quality >= 3 || e->compact_loop >= 0) return 0;
    e->compact_loop = index; e->compact_at = 0; e->compact_pairs = l->written / 2;
    /* Fold fast enough that the frames arriving meanwhile (at most 32 per block) fit the stage. */
    uint32_t blocks = (e->stage_mask + 1) / 32;
    e->compact_rate = (e->compact_pairs + blocks - 1) / blocks;
    if (e->compact_rate < LC_COMPACT_PAIRS) e->compact_rate = LC_COMPACT_PAIRS;
    e->stage_n = e->stage_head = e->stage_tail = 0;
    l->quality++; l->written = e->compact_pairs;
    for (int c = 0; c < 2; c++) for (int b = 0; b < LC_BINS; b++) l->peaks[c][b] = 0;
    return 1;
}

static void compact_step(lc_engine *e)
{
    int index = e->compact_loop; if (index < 0) return;
    lc_loop *l = &e->loop[index];
    uint32_t stop = e->compact_at + e->compact_rate; if (stop > e->compact_pairs) stop = e->compact_pairs;
    for (uint32_t j = e->compact_at; j < stop; j++) {
        const int16_t *a = lc_frame(e, index, j * 2), *b = lc_frame(e, index, j * 2 + 1); int16_t *t = lc_frame(e, index, j);
        for (int c = 0; c < 2; c++) { int16_t v = (int16_t)(((int32_t)a[c] + b[c]) >> 1); t[c] = v; peak(l, j, c, v); }
    }
    if (stop == e->compact_pairs && e->compact_at != stop)   /* folding done: the upper half goes back to the pool */
        lc_trim(e, index, (e->compact_pairs + (1u << e->chunk_shift) - 1) >> e->chunk_shift);
    e->compact_at = stop;
    if (stop == e->compact_pairs) {
        uint32_t count = e->stage_n; if (count > 512) count = 512;
        for (uint32_t j = 0; j < count; j++) {
            if (!stored(e, index, e->stage[e->stage_tail * 2], e->stage[e->stage_tail * 2 + 1])) { e->error = 1; count = e->stage_n; break; }
            e->stage_tail = (e->stage_tail + 1) & e->stage_mask;
        }
        e->stage_n -= count;
        if (!e->stage_n) { e->compact_loop = -1; if (l->state == LC_WORK) ready_to_play(e, index); }
    }
}

/* Refresh one overdub waveform progressively, including cancellation that lowers peaks. */
static void refresh_dub_wave(lc_engine *e)
{
    int index = -1; for (int i = 0; i < LC_LOOPS; i++) if (e->loop[i].state == LC_DUB) index = i;
    if (index < 0) { e->scan_loop = -1; return; }
    lc_loop *l = &e->loop[index]; uint32_t s = l->bin_shift, size = 1u << s;
    if (e->scan_loop != index) { e->scan_loop = index; e->scan_bin = e->scan_at = 0; e->scan_peak[0] = e->scan_peak[1] = 0; }
    uint32_t bins = (l->length + size - 1) >> s; if (e->scan_bin >= bins) e->scan_bin = 0;
    uint32_t start = e->scan_bin << s, end = start + size; if (end > l->length) end = l->length;
    uint32_t stop = start + e->scan_at + 512; if (stop > end) stop = end;
    for (uint32_t at = start + e->scan_at; at < stop; at++) for (int c = 0; c < 2; c++) {
        uint32_t magnitude = lc_abs(lc_frame(e, index, at)[c]); if (magnitude > e->scan_peak[c]) e->scan_peak[c] = magnitude;
    }
    e->scan_at = stop - start;
    if (stop == end) {
        for (int c = 0; c < 2; c++) { l->peaks[c][e->scan_bin] = (uint16_t)e->scan_peak[c]; e->scan_peak[c] = 0; }
        e->scan_bin++; e->scan_at = 0;
    }
}

static void note_meter(lc_engine *e, int i, const int32_t v[2])
{
    uint32_t a = lc_abs(v[0]), b = lc_abs(v[1]); if (b > a) a = b;
    if (a > e->meter[i]) e->meter[i] = a;
}

static void pan_frame(const lc_loop *l, int32_t v[2])
{
    if (l->pan > 0) v[0] = (int32_t)(((int64_t)v[0] * (32768 - l->pan)) >> 15);
    else if (l->pan < 0) v[1] = (int32_t)(((int64_t)v[1] * (32768 + l->pan)) >> 15);
}

static void sample_tick(lc_engine *e, const int16_t in[2], int32_t mix[2])
{
    e->frames++; mix[0] = mix[1] = 0;
    if (e->paused) {
        for (int i = 0; i < LC_LOOPS; i++) {
            lc_transition *t = &e->trans[i]; int32_t gain = ((int32_t)t->remaining * 52429) >> 14, wet[2];
            tape_sample(e, i, wet);
            for (int c = 0; c < 2; c++) wet[c] = (wet[c] * gain) >> 15;
            pan_frame(&e->loop[i], wet);
            mix[0] += wet[0]; mix[1] += wet[1]; note_meter(e, i, wet);
        }
        return;
    }
    if (e->sync && e->resync && !e->master_phase) { for (int i = 0; i < LC_LOOPS; i++) e->loop[i].sync_phase = 0; e->resync = 0; }
    for (int i = 0; i < LC_LOOPS; i++) {
        lc_loop *l = &e->loop[i];
        l->gain += lc_limit(l->target_gain - l->gain, -16, 16);
        for (int c = 0; c < 2; c++) { mix[c] += e->tail[i][c]; e->tail[i][c] = e->tail[i][c] * 63 / 64; }
        if (l->state == LC_ARM) {
            if (l->capture_sync != LC_WAIT_SOUND) { if (e->master_phase == 0) l->state = LC_REC; }
            else if (lc_abs(in[0]) >= LC_SOUND || lc_abs(in[1]) >= LC_SOUND) { l->capture_sync = 0; l->state = LC_REC; }
        }
        if (l->state == LC_REC) {
            int32_t dry[2] = { in[0], in[1] }; note_meter(e, i, dry);
            l->duration++; l->sum[0] += in[0]; l->sum[1] += in[1];
            if (++l->rec_phase == (1u << l->quality)) {
                l->rec_phase = 0;
                int16_t vl = (int16_t)(l->sum[0] >> (l->quality + 1)), vr = (int16_t)(l->sum[1] >> (l->quality + 1));
                l->sum[0] = l->sum[1] = 0;
                if (e->compact_loop == i) {
                    if (e->stage_n <= e->stage_mask) {
                        e->stage[e->stage_head * 2] = vl; e->stage[e->stage_head * 2 + 1] = vr;
                        e->stage_head = (e->stage_head + 1) & e->stage_mask; e->stage_n++;
                    } else { e->error = 1; finish(e, i); }
                }
                else stored(e, i, vl, vr);
                /* No room for the next frame: this take halves its own rate and carries on. */
                if (e->compact_loop != i && l->state == LC_REC && ((e->loop_max && l->written >= e->loop_max) || !lc_ensure(e, i, l->written))) {
                    if (!start_compact(e, i)) finish(e, i);
                }
            }
            if (l->capture_sync == 2 && e->master_phase + 1 >= e->master_length) finish(e, i);
            continue;
        }
        if (l->state != LC_PLAY && l->state != LC_DUB) continue;
        if (e->sync && !l->sync_phase) l->phase = l->reverse ? (int64_t)(span_of(l) - 1) << 14 : 0;
        uint32_t at = (uint32_t)(l->phase >> 14);
        int32_t v[2]; read_frame(e, i, l->phase, v);
        if (e->sync && e->master_length > 48) {
            uint32_t dur = dur_of(l), gap = l->sync_phase < dur - l->sync_phase ? l->sync_phase : dur - l->sync_phase;
            if (gap < 24) { v[0] = v[0] * (int32_t)gap / 24; v[1] = v[1] * (int32_t)gap / 24; }
        }
        for (int c = 0; c < 2; c++) e->last_wet[i][c] = lc_sat((v[c] * l->gain) >> 14);
        lc_transition *tr = &e->trans[i];
        if (tr->remaining) {
            int32_t gain = ((int32_t)tr->remaining * 52429) >> 14, ghost[2]; tape_sample(e, i, ghost);
            for (int c = 0; c < 2; c++) e->last_wet[i][c] += ((ghost[c] - e->last_wet[i][c]) * gain) >> 15;
        }
        pan_frame(l, e->last_wet[i]);
        mix[0] += e->last_wet[i][0]; mix[1] += e->last_wet[i][1]; note_meter(e, i, e->last_wet[i]);
        int32_t step = lc_rates[l->speed] >> l->quality;
        if (l->state == LC_DUB) {
            int32_t gain = step < 16384 ? step : 16384; int16_t *t = lc_frame(e, i, at);
            for (int c = 0; c < 2; c++) {
                int32_t added = ((int32_t)in[c] * gain) >> 16;
                t[c] = lc_sat((int32_t)t[c] + added); peak(l, at, c, t[c]);
                uint32_t magnitude = lc_abs(added), bin = at >> (l->bin_shift + 3);
                if (bin < LC_DUB_BINS && magnitude > l->dub_peaks[l->layer_cursor][bin]) l->dub_peaks[l->layer_cursor][bin] = (uint16_t)magnitude;
            }
        }
        if (++l->sync_phase >= dur_of(l)) l->sync_phase = 0;
        if (++l->cycle >= l->duration) { l->cycle = 0; l->sync_phase = 0; }   /* a shortened loop starts again with the whole take: no drift */
        int64_t phase = l->phase + (l->reverse ? -step : step), period = (int64_t)span_of(l) << 14;
        if (phase < 0) phase += period;
        if (phase >= period) phase -= period;
        l->phase = phase;
    }
    e->timeline++;
    if (e->master_length && ++e->master_phase >= e->master_length) e->master_phase = 0;
}

void lc_render(lc_engine *e, int16_t *left, int16_t *right, int frames)
{
    if (!e->audio) return;
    for (int i = 0; i < LC_LOOPS; i++) e->meter[i] = 0;
    int compacting = e->compact_loop >= 0;
    compact_step(e);
    if (!compacting) refresh_dub_wave(e);
    for (int n = 0; n < frames; n++) {
        int16_t in[2] = { left[n], right[n] }; int32_t mix[2];
        sample_tick(e, in, mix);
        left[n] = lc_sat(in[0] + mix[0]); right[n] = lc_sat(in[1] + mix[1]);
    }
    for (int i = 0; i < LC_LOOPS; i++) {
        e->meter_hold[i] = e->meter_hold[i] * 31 / 32;
        if (e->meter[i] > e->meter_hold[i]) e->meter_hold[i] = e->meter[i];
    }
}

void lc_snapshot(lc_engine *e, lc_view *v)
{
    v->capacity = e->chunks << e->chunk_shift; v->free_frames = e->free_n << e->chunk_shift; v->master_length = e->master_length; v->master_phase = e->master_phase;
    v->activity = e->activity; v->sync = e->sync; v->paused = e->paused; v->error = e->error;
    for (int i = 0; i < LC_LOOPS; i++) {
        lc_loop *l = &e->loop[i]; lc_display *d = &v->loop[i];
        d->phase = (uint32_t)(l->phase >> 14);
        d->length = l->state == LC_REC || l->state == LC_WORK ? l->written + (e->compact_loop == i ? e->stage_n : 0) : l->length;
        d->duration = l->duration; d->bin_shift = l->bin_shift; d->gain = l->target_gain; d->pan = l->pan; d->speed = l->speed;
        d->state = l->state; d->reverse = l->reverse; d->quality = l->quality; d->keep = l->keep;
        d->layer_count = l->layer_count; d->layer_cursor = l->layer_cursor;
        d->output_peak = (uint16_t)lc_limit((int)e->meter_hold[i], 0, 65535);
        for (int k = 0; k < 3; k++) for (int b = 0; b < LC_DUB_BINS; b++) d->layer[k][b] = (uint8_t)lc_limit(l->dub_peaks[k][b] >> 5, 0, 255);
        for (int c = 0; c < 2; c++) for (int b = 0; b < LC_BINS; b++) d->wave[c][b] = (uint8_t)lc_limit(l->peaks[c][b] >> 7, 0, 255);
    }
}
