/* 4loop Field adapter — the host interface a loader must provide.
 * GPL-3.0-or-later + OP-1 firmware permission.
 *
 * This is the ONLY thing the adapter knows about its environment. Anything that can fill in
 * this struct can run 4loop: faust's .pack loader, an open-xy-style static machine, our own
 * os.ldr splice, or the desktop preview/test harness. The adapter never calls firmware, never
 * allocates, and never draws directly — it goes through here. Keeping this surface tiny is what
 * makes the port loader-agnostic: when a real Field loader lands we implement field_host once and
 * nothing in core/ or the rest of field/ changes.
 *
 * Three concerns, matching the three things any loader exposes (see INTEGRATION.md §2, §6, §8):
 *   1. memory  — the loader hands us one aligned block from its heap (the "CCES heap 1" pool on
 *                the Field, an fx lease on OG, or malloc in the harness).
 *   2. screen  — the loader turns our draw ops into whatever its renderer wants (SVG nodes via the
 *                content/display loader at 0x8FC8AB7C, open-xy's retained scene, or an SVG file).
 *   3. timing  — a millisecond clock for the scene's drift/pulse animation.
 * Audio and raw input are pushed *into* the adapter by the loader (see field_plugin.h); they are
 * not callbacks here, because the loader owns those buffers and the audio thread. */
#ifndef FIELD_HOST_H
#define FIELD_HOST_H
#include <stdint.h>

/* Screen draw ops. The scene is retained (rebuilt each frame from an lc_view), so the host just
 * replays these between fh_screen_begin/fh_screen_end. Coordinates are whole pixels on a 480x220
 * canvas (the adapter converts the scene's Q4 pixels down for the host). Colours are 0..255 RGB.
 * alpha 0..255. A host that can only draw filled polylines (open-xy: <=4 outlines of <=64 points)
 * approximates circles as polylines; a full SVG host draws them as <circle>. Both are valid — the
 * adapter emits intent, the host picks fidelity. */
typedef struct field_host field_host;

struct field_host {
    void *ctx;   /* loader-private; passed back to every call */

    /* --- memory --- the adapter asks once at create time. Return the largest aligned block you
     * can spare between min_bytes and max_bytes (4-byte aligned), or 0 to run audio-passthrough
     * with no loops. want_bytes is our preferred size; honour it if you can. */
    void *(*mem_get)(void *ctx, uint32_t min_bytes, uint32_t want_bytes, uint32_t max_bytes,
                     uint32_t *granted_bytes);
    void  (*mem_release)(void *ctx, void *block);

    /* --- timing --- monotonic milliseconds, any epoch. */
    uint32_t (*now_ms)(void *ctx);

    /* --- screen --- called in order: begin, then a run of ops, then end. The host may skip a
     * frame by making begin return 0. All ops are no-ops if begin returned 0. */
    int  (*screen_begin)(void *ctx);
    void (*screen_end)(void *ctx);
    /* filled circle; fill alpha; optional dotted/stipple texture for an empty loop (dotted!=0). */
    void (*circle)(void *ctx, int x, int y, int r, const uint8_t rgb[3], uint8_t alpha, uint8_t dotted);
    /* stroked ring (circle outline); dashed!=0 for an empty/paused loop. width in px. */
    void (*ring)(void *ctx, int x, int y, int r, int width, const uint8_t rgb[3], uint8_t alpha, uint8_t dashed);
    /* closed polyline (the waveform outline / transport shapes). n points, x/y interleaved px. */
    void (*poly)(void *ctx, const int16_t *xy, int n, int closed, int width,
                 const uint8_t rgb[3], uint8_t alpha, uint8_t fill);
    /* short line (scrub/playhead). */
    void (*line)(void *ctx, int x0, int y0, int x1, int y1, int width, const uint8_t rgb[3], uint8_t alpha);
    /* optional centred glyph/icon by id (sync, pause). A host without glyphs ignores it. */
    void (*glyph)(void *ctx, int id, int x, int y, const uint8_t rgb[3], uint8_t alpha);
    /* optional text at (x,y) top-left, px. For the held info-view, help header and first-open
     * intro. A host with no text layer (e.g. a pure-polyline renderer) ignores it; the geometry
     * scene still renders. s is NUL-terminated, uppercase ASCII + space + a few symbols. */
    void (*text)(void *ctx, int x, int y, const char *s, const uint8_t rgb[3], uint8_t alpha);
};

/* glyph ids (host may ignore any it doesn't have). */
enum { FG_SYNC = 1, FG_PAUSE, FG_INTRO };

#endif
