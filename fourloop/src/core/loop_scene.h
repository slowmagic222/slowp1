/* 4loop Field screen logic: turns an lc_view into a small retained scene for a 480 x 220 canvas.
 * Look taken from 4loop 0.11 (reelloop-v11/4loop/ui.h), scaled about 1.4x. Integer only, no
 * firmware calls, no drawing: the adapter (or tools/preview) maps the scene onto SVG nodes.
 * GPL-3.0-or-later + OP-1 firmware permission.
 *
 * Coordinates are Q4 pixels (1/16 px). Per loop the scene holds: one filled circle, one ring,
 * one closed 64-point waveform outline inside the circle (overdubs grow the same wave), a radial
 * scrub line, up to three rim triangles, up to two slow-speed bars, and a pulsing record disc. */
#ifndef LOOP_SCENE_H
#define LOOP_SCENE_H
#include "loop_core.h"

#define LS_W 480
#define LS_H 220
#define LS_WAVE_POINTS 64
#define LS_SHADE_MAX 16    /* the fan's triangles: one per 1/16 turn at most */

typedef struct { int32_t x, y; } ls_pt;

typedef struct {
    uint8_t  state;                    /* LC_EMPTY ... */
    ls_pt    centre; int32_t radius;   /* Q4 */
    uint8_t  fill_alpha, dotted;       /* dotted: empty loop, draw the stipple texture */
    uint8_t  ring_alpha, ring_dashed; int32_t ring_width;
    uint8_t  wave_on, wave_alpha; ls_pt wave[LS_WAVE_POINTS];
    uint8_t  head_on; ls_pt scrub[2];
    uint8_t  tri_count; ls_pt tri[3][3];
    uint8_t  bar_count; ls_pt bar[2][2];
    uint8_t  rec_alpha; int32_t rec_radius;   /* rec_alpha 0 = not writing. Pure red, drawn last. */
    uint8_t  shade_n; ls_pt shade[LS_SHADE_MAX + 1];   /* loop size: the part that does not play, a fan of shade_n triangles from the centre */
} ls_loop;

typedef struct {
    ls_loop  loop[LC_LOOPS];
    uint8_t  icon_alpha, sync, paused, intro_alpha;
    /* animation state, private */
    int32_t  drift_x[LC_LOOPS], drift_y[LC_LOOPS], vel_x[LC_LOOPS], vel_y[LC_LOOPS];   /* Q8 px */
    int32_t  camera_x, camera_y, focus[LC_LOOPS], level[LC_LOOPS];
    uint32_t time_ms, age_ms, intro_ms, last_activity; uint8_t intro_done;
} ls_scene;

/* The knobs as they look on the unit (navy, copper orange, grey, red), lit enough for the screen: [0] the knob, [1] the fill at full volume. */
extern const uint8_t ls_colour[LC_LOOPS][2][3];
extern const uint8_t ls_record_red[3];

void ls_init(ls_scene *s);
void ls_update(ls_scene *s, const lc_view *v, uint32_t dt_ms);
#endif
