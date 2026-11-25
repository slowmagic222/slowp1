/* 4loop Field screen logic. See loop_scene.h. */
#include "loop_scene.h"

const uint8_t ls_colour[LC_LOOPS][2][3] = {
    { { 0x2E, 0x4A, 0x7D }, { 0x19, 0x29, 0x45 } },   /* 1 navy */
    { { 0xD3, 0x7E, 0x4E }, { 0x74, 0x45, 0x2B } },   /* 2 copper orange */
    { { 0xA2, 0xA4, 0xA6 }, { 0x59, 0x5A, 0x5B } },   /* 3 grey */
    { { 0xE5, 0x28, 0x17 }, { 0x7E, 0x16, 0x0D } },   /* 4 red */
};
const uint8_t ls_record_red[3] = { 0xFF, 0x00, 0x00 };

/* cos(2 pi i / 128) in Q14; sine is the same table a quarter turn on. Index 96 is 12 o'clock. */
static const int16_t ls_cos[128] = {16384,16364,16305,16207,16069,15893,15679,15426,15137,14811,14449,14053,13623,13160,12665,12140,11585,11003,10394,9760,9102,8423,7723,7005,6270,5520,4756,3981,3196,2404,1606,804,0,-804,-1606,-2404,-3196,-3981,-4756,-5520,-6270,-7005,-7723,-8423,-9102,-9760,-10394,-11003,-11585,-12140,-12665,-13160,-13623,-14053,-14449,-14811,-15137,-15426,-15679,-15893,-16069,-16207,-16305,-16364,-16384,-16364,-16305,-16207,-16069,-15893,-15679,-15426,-15137,-14811,-14449,-14053,-13623,-13160,-12665,-12140,-11585,-11003,-10394,-9760,-9102,-8423,-7723,-7005,-6270,-5520,-4756,-3981,-3196,-2404,-1606,-804,0,804,1606,2404,3196,3981,4756,5520,6270,7005,7723,8423,9102,9760,10394,11003,11585,12140,12665,13160,13623,14053,14449,14811,15137,15426,15679,15893,16069,16207,16305,16364};
static int32_t cx(unsigned a) { return ls_cos[a & 127]; }
static int32_t cy(unsigned a) { return ls_cos[(a + 96) & 127]; }
static int32_t lim(int32_t x, int32_t a, int32_t b) { return x < a ? a : x > b ? b : x; }

void ls_init(ls_scene *s)
{
    static const int32_t x0[LC_LOOPS] = { 64, 181, 300, 417 }, y0[LC_LOOPS] = { 104, 137, 91, 132 };
    static const int32_t vx[LC_LOOPS] = { 43, -35, 39, -42 }, vy[LC_LOOPS] = { 26, 32, -29, -24 };
    for (int i = 0; i < LC_LOOPS; i++) {
        s->drift_x[i] = x0[i] * 256; s->drift_y[i] = y0[i] * 256; s->vel_x[i] = vx[i]; s->vel_y[i] = vy[i];
        s->focus[i] = s->level[i] = 0; s->loop[i].state = LC_EMPTY;
    }
    s->camera_x = s->camera_y = 0; s->time_ms = s->age_ms = s->intro_ms = s->last_activity = 0; s->intro_done = 0;
    s->icon_alpha = 255; s->sync = 1; s->paused = 0; s->intro_alpha = 255;
}

/* One closed outline: 32 points round the outside of the mid circle, then 32 back round the
 * inside. Its thickness at each angle is the loop's level there, louder of left and right. */
static void wave_outline(ls_loop *o, const lc_display *d, int32_t x, int32_t y, int32_t radius)
{
    uint32_t size = 1u << d->bin_shift, bins = (d->length + size - 1) >> d->bin_shift; if (bins > LC_BINS) bins = LC_BINS;
    int32_t mid = radius * 2 / 3, amplitude = radius / 5, maximum = 1, amp[32];
    o->wave_on = bins > 0; if (!bins) return;
    for (int j = 0; j < 32; j++) {
        uint32_t a = (uint32_t)j * bins / 32, b = (uint32_t)(j + 1) * bins / 32; if (b <= a) b = a + 1;
        int32_t m = 0;
        for (uint32_t k = a; k < b && k < bins; k++) { if (d->wave[0][k] > m) m = d->wave[0][k]; if (d->wave[1][k] > m) m = d->wave[1][k]; }
        amp[j] = m; if (m > maximum) maximum = m;
    }
    for (int j = 0; j < 32; j++) {
        unsigned angle = (unsigned)(j * 4 + 96); int32_t swing = amp[j] * amplitude / maximum; if (swing < 8) swing = 8;   /* never thinner than half a pixel */
        o->wave[j].x = x + cx(angle) * (mid + swing) / 16384; o->wave[j].y = y + cy(angle) * (mid + swing) / 16384;
        o->wave[63 - j].x = x + cx(angle) * (mid - swing) / 16384; o->wave[63 - j].y = y + cy(angle) * (mid - swing) / 16384;
    }
}

/* A point on a circle at pos 0..32768 of a turn, clockwise from 12 o'clock. */
static ls_pt rim_at(int32_t x, int32_t y, int32_t r, uint32_t pos)
{
    pos &= 32767; unsigned index = pos >> 8; int32_t frac = (int32_t)(pos & 255);
    int32_t nx = cx(index + 96) + (cx(index + 97) - cx(index + 96)) * frac / 256, ny = cy(index + 96) + (cy(index + 97) - cy(index + 96)) * frac / 256;
    ls_pt p = { x + nx * r / 16384, y + ny * r / 16384 };
    return p;
}

/* Loop size: the turn from keep/1024 round to 12 o'clock does not play; a fan of up to 16 triangles covers it. */
static void shade(ls_loop *o, const lc_display *d, int32_t x, int32_t y, int32_t radius)
{
    o->shade_n = 0;
    if (d->keep >= LC_KEEP_WHOLE || (d->state != LC_PLAY && d->state != LC_DUB)) return;
    uint32_t from = (uint32_t)d->keep * 32, arc = 32768 - from;
    int n = (int)((arc + 2047) / 2048); if (n < 1) n = 1; if (n > LS_SHADE_MAX) n = LS_SHADE_MAX;
    for (int k = 0; k <= n; k++) o->shade[k] = rim_at(x, y, radius, from + arc * (uint32_t)k / (uint32_t)n);
    o->shade_n = (uint8_t)n;
}

static void playhead(ls_loop *o, const lc_display *d, int32_t x, int32_t y, int32_t radius, uint32_t time_ms)
{
    uint32_t pos = d->state == LC_REC || !d->length ? 0 : (uint32_t)((uint64_t)d->phase * 32768 / d->length) % 32768;
    unsigned index = pos >> 8; int32_t frac = (int32_t)(pos & 255);
    int32_t nx = cx(index + 96) + (cx(index + 97) - cx(index + 96)) * frac / 256, ny = cy(index + 96) + (cy(index + 97) - cy(index + 96)) * frac / 256;
    int32_t sign = d->state == LC_REC ? 1 : d->reverse ? -1 : 1, dx = -ny * sign, dy = nx * sign;   /* direction of travel */
    ls_pt rim = { x + nx * (radius + 16) / 16384, y + ny * (radius + 16) / 16384 };
    o->head_on = 1; o->scrub[0].x = x; o->scrub[0].y = y; o->scrub[1] = rim;
    o->tri_count = (uint8_t)(d->state == LC_REC ? 1 : 1 + (d->speed > 2 ? d->speed - 2 : 0));
    for (int j = 0; j < o->tri_count; j++) {
        int32_t off = (j * 11 - (o->tri_count - 1) * 11 / 2) * 16, back = off - 6 * 16, tip = off + 8 * 16, half = 7 * 16;
        int32_t bx = rim.x + dx * back / 16384, by = rim.y + dy * back / 16384;
        o->tri[j][0].x = bx + nx * half / 16384; o->tri[j][0].y = by + ny * half / 16384;
        o->tri[j][1].x = bx - nx * half / 16384; o->tri[j][1].y = by - ny * half / 16384;
        o->tri[j][2].x = rim.x + dx * tip / 16384; o->tri[j][2].y = rim.y + dy * tip / 16384;
    }
    o->bar_count = (uint8_t)(d->state != LC_REC && d->speed < 2 ? 2 - d->speed : 0);
    for (int j = 0; j < o->bar_count; j++) {
        int32_t off = (15 + j * 7) * 16, bx = rim.x - dx * off / 16384, by = rim.y - dy * off / 16384;
        o->bar[j][0].x = bx + nx * 64 / 16384; o->bar[j][0].y = by + ny * 64 / 16384;
        o->bar[j][1].x = bx - nx * 64 / 16384; o->bar[j][1].y = by - ny * 64 / 16384;
    }
    if (d->state == LC_REC || d->state == LC_DUB) {   /* pure red, slight pulse once a second */
        int32_t pulse = (int32_t)(time_ms % 1000); if (pulse > 500) pulse = 1000 - pulse;
        o->rec_alpha = (uint8_t)(190 + pulse * 65 / 500); o->rec_radius = 10 * 16 + pulse * 48 / 500;
    }
}

void ls_update(ls_scene *s, const lc_view *v, uint32_t dt_ms)
{
    int32_t delta = (int32_t)(dt_ms <= 100 ? dt_ms : 16);
    s->time_ms += (uint32_t)delta;
    if (v->activity != s->last_activity) { s->last_activity = v->activity; s->age_ms = 0; s->intro_done = 1; }
    s->age_ms = s->age_ms + (uint32_t)delta > 4000 ? 4000 : s->age_ms + (uint32_t)delta;
    s->icon_alpha = (uint8_t)(s->age_ms < 2000 ? 255 : s->age_ms >= 3000 ? 0 : (3000 - s->age_ms) * 255 / 1000);
    s->sync = v->sync; s->paused = v->paused;
    int focus = -1;
    for (int i = 0; i < LC_LOOPS; i++) if (v->loop[i].state == LC_REC || v->loop[i].state == LC_DUB) focus = i;
    int32_t aim_x = focus < 0 ? 0 : (LS_W / 2 * 256 - s->drift_x[focus]) / 3, aim_y = focus < 0 ? 0 : (LS_H / 2 * 256 - s->drift_y[focus]) / 3;
    s->camera_x += (aim_x - s->camera_x) * delta / 800; s->camera_y += (aim_y - s->camera_y) * delta / 800;
    for (int i = 0; i < LC_LOOPS; i++) {
        const lc_display *d = &v->loop[i]; ls_loop *o = &s->loop[i];
        s->focus[i] += ((i == focus ? 256 : 0) - s->focus[i]) * delta / 500;
        if (d->state != LC_EMPTY) s->intro_done = 1;
        int32_t target = lim(d->output_peak * 2, 0, 32767);
        s->level[i] += (target - s->level[i]) / (target > s->level[i] ? 2 : 5);
        int32_t base = 42 + (int32_t)((uint64_t)d->duration * 42 / (44100u * 24u)); if (base > 84) base = 84; if (d->state == LC_EMPTY) base = 39;
        int32_t radius = base * 16 + s->level[i] * 7 * 16 / 32767 + base * 16 * s->focus[i] / 2048;
        s->drift_x[i] += s->vel_x[i] * delta / 16; s->drift_y[i] += s->vel_y[i] * delta / 16;
        if (s->drift_x[i] < 14 * 256) { s->drift_x[i] = 14 * 256; if (s->vel_x[i] < 0) s->vel_x[i] = -s->vel_x[i]; }
        if (s->drift_x[i] > (LS_W - 15) * 256) { s->drift_x[i] = (LS_W - 15) * 256; if (s->vel_x[i] > 0) s->vel_x[i] = -s->vel_x[i]; }
        if (s->drift_y[i] < 14 * 256) { s->drift_y[i] = 14 * 256; if (s->vel_y[i] < 0) s->vel_y[i] = -s->vel_y[i]; }
        if (s->drift_y[i] > (LS_H - 15) * 256) { s->drift_y[i] = (LS_H - 15) * 256; if (s->vel_y[i] > 0) s->vel_y[i] = -s->vel_y[i]; }
        int32_t x = lim((s->drift_x[i] + s->camera_x) >> 4, 17 * 16, (LS_W - 18) * 16), y = lim((s->drift_y[i] + s->camera_y) >> 4, 17 * 16, (LS_H - 18) * 16);
        o->state = d->state; o->centre.x = x; o->centre.y = y; o->radius = radius;
        o->ring_alpha = (uint8_t)lim(d->state == LC_EMPTY ? 145 : d->state == LC_REC ? 255 : 45 + d->gain * 175 / 32767 + s->level[i] * 35 / 32767, 0, 255);
        o->ring_dashed = d->state == LC_ARM; if (o->ring_dashed) o->ring_alpha = (s->time_ms / 240) & 1 ? 80 : 190;
        o->ring_width = 32 + s->level[i] * 24 / 32767;
        o->dotted = d->state == LC_EMPTY; o->fill_alpha = 0;
        if (d->state == LC_EMPTY) o->fill_alpha = 72;
        else if (d->state != LC_ARM) {
            int writing = d->state == LC_REC || d->state == LC_DUB;
            int32_t pulse = writing ? (int32_t)(s->time_ms % 1000) : 0; if (pulse > 500) pulse = 1000 - pulse;
            o->fill_alpha = (uint8_t)lim(35 + d->gain * 45 / 32767 + s->level[i] * 110 / 32767 + (writing ? pulse * 100 / 500 : 0), 0, 190);
        }
        o->wave_on = 0; o->wave_alpha = (uint8_t)lim(o->ring_alpha + 35, 0, 255);
        if (d->state != LC_EMPTY && d->state != LC_ARM) wave_outline(o, d, x, y, radius);
        o->head_on = 0; o->tri_count = o->bar_count = 0; o->rec_alpha = 0; o->rec_radius = 0;
        shade(o, d, x, y, radius);
        if (d->state == LC_PLAY || d->state == LC_DUB || d->state == LC_REC) playhead(o, d, x, y, radius, s->time_ms);
    }
    if (!s->intro_done) { s->intro_ms += (uint32_t)delta; if (s->intro_ms >= 5000) s->intro_done = 1; }
    s->intro_alpha = (uint8_t)(s->intro_done ? 0 : s->intro_ms < 3000 ? 255 : (5000 - s->intro_ms) * 255 / 2000);
}
