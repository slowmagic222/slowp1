// 4loop.pack v2: four stereo loopers as an effect of level 8 in the unit's own list of master effects.
// GPL-3.0-or-later + OP-1 firmware permission.
//
// What the pack is:
//   - Audio: one ask of kind `effect`, named `4loop`, riding stock `delay` (a unit without the pack plays the
//     master dry). The base calls fourloop_process once a block of 64 frames on the master's Q31 sums, in place of
//     the unit's own master effect, while it is switched on (no tails: off = the loops are silent and frozen, kept).
//     4loop works on the master only: picked on a synth or drum sound, it passes the sound by untouched.
//   - Values: the effect's four values are the four loops' levels, 0..32 (the OG's 32 steps). The knobs, the LFOs
//     and the base's save of the master keep them. The pack takes the plain turns itself (OG resolution) and sets
//     the value with op1f_param_set from its frame; the block writes the value into the core.
//   - Page: T3 on the mixer shows 4loop's page in place of the master effect's (op1f_page_stand_in). Shift +
//     metronome still opens the same page in any mode. The page is retained SVG: per loop a filled disc, a ring,
//     32 radial waveform bars, a playhead and the red record disc, moved and scaled each frame from the 4loop scene.
//   - Keys while the page shows:
//       knob turn            loop level             shift + turn       speed (octaves)
//       T1 held + turn       pan                    knob press         record / finish / overdub
//       shift + knob press   reverse                hold a knob 1 s    clear that loop
//       T4 or shift + T1     sync                   shift + T2         pause / play all
//       T3                   pause / play           shift + T3         the unit's list of master effects
//       < >                  taken, so the mixer does not move the tape
//       help (held)          loop details
//   - Memory: one block from the firmware's larger heap at the first frame (heap_malloc), kept for good, so loops
//     outlive picking another master effect and coming back. No working memory of level 6 is asked.
//   - Folder: `4loop` in the disk mode (reserved for saved loops, not written yet).
//
// Threads: the frame function (display thread) makes the page, the memory and the hook; the input function
// (key thread) only queues events; the audio thread drains the queue, so the core is only ever touched
// from one thread, as it expects. Addresses of the firmware: fourloop_fw.h only.

#include "build_r6e_card.h"
#include "fourloop_fw.h"
#include "fourloop_glue.h"

typedef unsigned int u32;

extern "C" const Op1fCard op1f_card_fourloop OP1F_NAMED("op1f_card_fourloop");
static const char LOG_FILE[] = "op1f/4loop.cpp";

enum { POP_MS = 1500 };                                              // sync / free / pause pops up this long
enum { KEY_HELP = 0, KEY_METRONOME = 1, KEY_T1 = 6, KEY_T2 = 7, KEY_T3 = 8, KEY_T4 = 9, KEY_SHIFT = 38, KEY_OCTAVE_DOWN = 36, KEY_OCTAVE_UP = 37,
       KEY_CLICK0 = 54, KEY_PRESS0 = 61, FRAME_L1 = 28, FRAME_L2 = 32, INPUT_L2 = 36, BLOCK_L8 = 44 };
enum { ASK = 0, MASTER = OP1F_PLACE_MASTER, LIVE_FRAMES = 4 };
enum { MEM_MIN = 512u * 1024u, MEM_WANT = 8u * 1024u * 1024u, MEM_MAX = 16u * 1024u * 1024u,
       HEAP_KEEP = 4u * 1024u * 1024u, LOG_EVERY = 240, BARS = 32 };

typedef void* (*HeapMallocFn)(int heap, u32 bytes);
typedef u32 (*HeapFreeFn)(int heap);

// ---------------------------------------------------------------- memory and time for the adapter

static struct { int heap; u32 got, free0, free1; } mem;

static void* mem_get(void*, uint32_t min_bytes, uint32_t, uint32_t max_bytes, uint32_t* granted)
{
    HeapFreeFn heap_free = (HeapFreeFn)FL_HEAP_FREE;
    mem.free0 = heap_free(0); mem.free1 = heap_free(1);
    int heap = mem.free1 > mem.free0 ? 1 : 0;
    u32 room = heap ? mem.free1 : mem.free0;
    if (room < HEAP_KEEP + min_bytes) return 0;                        // leave the other packs their share
    u32 take = room - HEAP_KEEP;
    if (take > max_bytes) take = max_bytes;
    take &= ~4095u;
    void* p = ((HeapMallocFn)FL_HEAP_MALLOC)(heap, take);
    if (!p) return 0;
    mem.heap = heap; mem.got = take; *granted = take;
    return p;
}

static uint32_t now_ms(void*) { return op1f_ms(); }

// ---------------------------------------------------------------- the effect (audio thread) and its page's word

static volatile u32 shown_where, shown_how;                          // what the base last said of the stand-in page
static volatile u32 other_calls;                                     // blocks handed for a place that is not the master

extern "C" void fourloop_process(const Op1fEffectBlock* now) OP1F_NAMED("fourloop_process");
extern "C" void fourloop_process(const Op1fEffectBlock* now)
{
    if (now->bytes < (u32)BLOCK_L8) return;
    if (now->place != (u32)MASTER) { other_calls = other_calls + 1; return; }   // the sound passes by untouched
    fl_effect((int32_t*)now->left, (int32_t*)now->right, (int)now->frames, now->values, LC_LOOPS);   // int and int32_t (long on the Blackfin) are both 32 bits
}

extern "C" void fourloop_shown(unsigned where, unsigned how) OP1F_NAMED("fourloop_shown");
extern "C" void fourloop_shown(unsigned where, unsigned how) { shown_where = where; shown_how = how; }

static int stand_in_up() { return (shown_how & (u32)OP1F_SHOWN_UP) != 0; }

// Live: the base called the effect on the master within the last few frames. Written by the frame only.
static volatile u32 live;

// ---------------------------------------------------------------- the page's parts

struct Part { void* p; int m[6]; signed char shown; };
enum { FILL_STEPS = 5 };                                             // tools/make_svg.py: the disc in five tones, by volume
struct LoopParts { Part fill[FILL_STEPS], ring, empty, head, rec, bar[BARS], shade[LS_SHADE_MAX], tri[3], slow[2], num; };

static void* document;                                                // single word, shared with the input function
static volatile u32 touches;                                         // counted by the input function, timed by the frame (the status line fades)
static struct {
    u32 asked, last_ms, countdown, frames_shown;
    LoopParts loop[LC_LOOPS];
    Part sync, free, play, pause, intro;
    u32 pop_ms, pop_sync, pop_paused, pop_seen;
    const char* pop_word;
    u32 last_calls, periods, stood, own_open, frame_calls, quiet_frames;
    int32_t level_used[LC_LOOPS];
    char text[8][48];
    u32 seen_touches, touch_ms;
} page;

static void find(Part* part, void* doc, const char* name) { part->p = op1f_node(doc, name); part->shown = -1; }

static void put(Part* part, const int* m)
{
    if (!part->p) return;
    int same = 1;
    for (int i = 0; i < 6; i++) if (part->m[i] != m[(i / 2) * 3 + (i & 1)]) same = 0;   // a, b, c, d, e, f
    if (same) return;
    for (int i = 0; i < 6; i++) part->m[i] = m[(i / 2) * 3 + (i & 1)];
    op1f_move(part->p, m);
}

static void show(Part* part, int on)
{
    if (!part->p || part->shown == (on ? 1 : 0)) return;
    op1f_show(part->p, on);
    part->shown = (signed char)(on ? 1 : 0);
}

static void name2(char* out, char a, int i, const char* tail)        // "f1", "w1_17": names in the picture
{
    int n = 0;
    out[n++] = a; out[n++] = (char)('1' + i);
    while (*tail) out[n++] = *tail++;
    out[n] = 0;
}

static void make_page()
{
    void* doc = op1f_picture(&op1f_card_fourloop, "4loop.svg");
    if (!doc) return;
    char name[12];
    for (int i = 0; i < LC_LOOPS; i++) {
        LoopParts* L = &page.loop[i];
        for (int k = 0; k < FILL_STEPS; k++) { char tail[3] = { '_', (char)('0' + k), 0 }; name2(name, 'f', i, tail); find(&L->fill[k], doc, name); show(&L->fill[k], 0); }
        name2(name, 'r', i, ""); find(&L->ring, doc, name);
        name2(name, 'e', i, ""); find(&L->empty, doc, name);
        name2(name, 'h', i, ""); find(&L->head, doc, name);
        name2(name, 'd', i, ""); find(&L->rec, doc, name);
        for (int j = 0; j < BARS; j++) {
            char tail[5]; tail[0] = '_';
            if (j < 10) { tail[1] = (char)('0' + j); tail[2] = 0; }
            else { tail[1] = (char)('0' + j / 10); tail[2] = (char)('0' + j % 10); tail[3] = 0; }
            name2(name, 'w', i, tail); find(&L->bar[j], doc, name);
        }
        show(&L->ring, 0); show(&L->empty, 0); show(&L->head, 0); show(&L->rec, 0);
        for (int j = 0; j < BARS; j++) show(&L->bar[j], 0);
        for (int k = 0; k < LS_SHADE_MAX; k++) {
            char tail[5]; tail[0] = '_';
            if (k < 10) { tail[1] = (char)('0' + k); tail[2] = 0; } else { tail[1] = '1'; tail[2] = (char)('0' + k - 10); tail[3] = 0; }
            name2(name, 's', i, tail); find(&L->shade[k], doc, name); show(&L->shade[k], 0);
        }
        for (int j = 0; j < 3; j++) { char tail[3] = { '_', (char)('0' + j), 0 }; name2(name, 'p', i, tail); find(&L->tri[j], doc, name); show(&L->tri[j], 0); }
        for (int j = 0; j < 2; j++) { char tail[3] = { '_', (char)('0' + j), 0 }; name2(name, 'b', i, tail); find(&L->slow[j], doc, name); show(&L->slow[j], 0); }
        name2(name, 'n', i, ""); find(&L->num, doc, name);
    }
    find(&page.sync, doc, "g_sync"); find(&page.free, doc, "g_free"); find(&page.play, doc, "g_play"); find(&page.pause, doc, "g_pause");
    find(&page.intro, doc, "g_intro");
    show(&page.sync, 0); show(&page.free, 0); show(&page.play, 0); show(&page.pause, 0); show(&page.intro, 0);
    for (int i = 0; i < 8; i++) page.text[i][0] = 1;                  // nothing set yet: the first set_text always writes (an unset placeholder draws its stroke)
    document = doc;
}

// ---------------------------------------------------------------- text

static char* put_s(char* o, const char* s) { while (*s) *o++ = *s++; return o; }
static char* put_u(char* o, u32 v) { char d[10]; int n = 0; do { d[n++] = (char)('0' + v % 10u); v /= 10u; } while (v); while (n) *o++ = d[--n]; return o; }

static const char* state_word(int s)
{
    return s == LC_REC ? "rec" : s == LC_DUB ? "dub" : s == LC_ARM ? "arm" : s == LC_WORK ? "finish" : s == LC_PLAY ? "play" : "empty";
}

static void set_text(int slot, const char* part, const char* text, unsigned align = OP1F_LEFT)
{
    char* had = page.text[slot];
    int same = 1;
    for (int i = 0; ; i++) { if (had[i] != text[i]) { same = 0; break; } if (!text[i]) break; }
    if (same) return;
    if (op1f_text_set(document, part, text, 0, align)) {
        int i = 0; for (; text[i] && i < 47; i++) had[i] = text[i]; had[i] = 0;
    }
}

static void dress_text(const lc_view* v)
{
    char line[48]; char* o = line;
    u32 ms = op1f_ms();
    if (!page.pop_seen) { page.pop_seen = 1; page.pop_sync = v->sync; page.pop_paused = v->paused; }   // no pop at the start
    if (v->sync != page.pop_sync) { page.pop_sync = v->sync; page.pop_word = v->sync ? "sync" : "free"; page.pop_ms = ms ? ms : 1; }
    if (v->paused != page.pop_paused) { page.pop_paused = v->paused; page.pop_word = v->paused ? "pause" : "play"; page.pop_ms = ms ? ms : 1; }
    const ls_scene* s = &fl.fp.scene;
    u32 how = shown_how, where = shown_where;
    if (!fl.fp.block) o = put_s(o, "4loop: no memory");
    else if ((how & (u32)OP1F_SHOWN_UP) && where != (u32)MASTER) o = put_s(o, "4loop works on the master only");
    else if ((how & (u32)OP1F_SHOWN_UP) && (how & (u32)OP1F_SHOWN_OFF)) o = put_s(o, "off  (T3 = on)");
    else if (!(op1f_riding(&op1f_card_fourloop, ASK) & (1u << MASTER))) o = put_s(o, "pick 4loop as the master fx");
    else if (!live) o = put_s(o, "master fx off");
    else if (fl.fp.size) {                                            // T2 held: the touched loop's size
        const lc_display* l = &v->loop[fl.fp.sel & 3];
        o = put_s(o, "loop "); *o++ = (char)('1' + (fl.fp.sel & 3)); o = put_s(o, " size ");
        if (l->state == LC_EMPTY) o = put_s(o, "-");
        else if (l->keep >= LC_KEEP_WHOLE) o = put_s(o, "whole");
        else if (v->sync) { o = put_s(o, "1/"); o = put_u(o, LC_KEEP_WHOLE / l->keep); }
        else { o = put_u(o, (u32)l->keep * 100u / LC_KEEP_WHOLE); *o++ = '%'; }
    }
    else if (page.pop_ms && (u32)(ms - page.pop_ms) < (u32)POP_MS) o = put_s(o, page.pop_word);
    *o = 0;
    int warning = o != line && !(page.pop_ms && (u32)(ms - page.pop_ms) < (u32)POP_MS) && !fl.fp.size;
    set_text(0, "t_status", line, OP1F_CENTRE);
    int intro = s->intro_alpha && !warning;                          // OG start: press an encoder (four dots) to record
    set_text(5, "t_logo", "4loop");
    set_text(6, "t_in1", intro ? "press an encoder" : "", OP1F_CENTRE);
    set_text(7, "t_in2", intro ? "to record" : "", OP1F_CENTRE);
    show(&page.intro, intro);
    static const char* const parts[LC_LOOPS] = { "t_1", "t_2", "t_3", "t_4" };
    for (int i = 0; i < LC_LOOPS; i++) {
        const lc_display* l = &v->loop[i];
        o = line;
        if (fl.fp.details && l->state != LC_EMPTY) {
            static const char* const qual[4] = { "44k", "22k", "11k", "5k" };
            *o++ = (char)('1' + i); *o++ = ' ';
            o = put_s(o, state_word(l->state)); *o++ = ' ';
            o = put_s(o, qual[l->quality & 3]); *o++ = ' ';
            o = put_u(o, l->duration / 44100u); *o++ = 's'; *o++ = ' ';
            int oct = (int)l->speed - 2; *o++ = oct < 0 ? '-' : '+'; *o++ = (char)('0' + (oct < 0 ? -oct : oct));
            o = put_s(o, " vol "); o = put_u(o, (u32)l->gain * 100u / 32767u);
            if (l->reverse) o = put_s(o, " rev");
            if (l->keep < LC_KEEP_WHOLE) { o = put_s(o, " size "); o = put_u(o, (u32)l->keep * 100u / LC_KEEP_WHOLE); *o++ = '%'; }
        }
        *o = 0;
        set_text(1 + i, parts[i], line);
    }
}

// ---------------------------------------------------------------- the scene onto the parts

// The picture's triangle (0,0) (64,0) (0,64) onto three points, Q4 px.
static void tri_matrix(ls_pt a, ls_pt b, ls_pt c, int* m)
{
    m[0] = b.x - a.x; m[1] = b.y - a.y; m[2] = 0;
    m[3] = c.x - a.x; m[4] = c.y - a.y; m[5] = 0;
    m[6] = a.x * 64; m[7] = a.y * 64; m[8] = 1024;
}


static void dress_page(u32 dt)
{
    field_plugin* fp = &fl.fp;
    lc_view v;
    lc_snapshot(&fp->engine, &v);
    ls_update(&fp->scene, &v, dt);
    const ls_scene* s = &fp->scene;
    int m[9];
    for (int i = 0; i < LC_LOOPS; i++) {
        const ls_loop* o = &s->loop[i];
        LoopParts* L = &page.loop[i];
        int empty = o->state == LC_EMPTY || (o->state == LC_ARM && o->ring_alpha < 150);
        int lit = o->state != LC_EMPTY && o->state != LC_ARM;
        fl_circle_matrix(o->centre.x, o->centre.y, o->radius, m);
        if (empty) { put(&L->empty, m); show(&L->empty, 1); } else show(&L->empty, 0);
        int tone = lit ? (int)(((u32)v.loop[i].gain * FILL_STEPS + 32766u) / 32767u) : 0;   // 0 silent .. 5 full
        for (int k = 0; k < FILL_STEPS; k++) {
            int on = tone == k + 1;
            if (on) put(&L->fill[k], m);
            show(&L->fill[k], on);
        }
        if (lit) put(&L->ring, m);
 show(&L->ring, lit || o->state == LC_ARM);
        if (o->state == LC_ARM && o->ring_alpha >= 150) put(&L->ring, m);
        for (int j = 0; j < BARS; j++) {
            if (o->wave_on) {
                fl_line_matrix(o->wave[63 - j].x, o->wave[63 - j].y, o->wave[j].x, o->wave[j].y, m);
                put(&L->bar[j], m);
            }
            show(&L->bar[j], o->wave_on);
        }
        if (o->head_on) { fl_line_matrix(o->scrub[0].x, o->scrub[0].y, o->scrub[1].x, o->scrub[1].y, m); put(&L->head, m); }
        show(&L->head, o->head_on);
        for (int k = 0; k < LS_SHADE_MAX; k++) {                       // loop size: the part that does not play, dimmed
            int on = k < o->shade_n;
            if (on) { tri_matrix(o->centre, o->shade[k], o->shade[k + 1], m); put(&L->shade[k], m); }
            show(&L->shade[k], on);
        }
        for (int j = 0; j < 3; j++) {                                  // playhead: direction and octaves up
            int on = j < o->tri_count;
            if (on) { tri_matrix(o->tri[j][0], o->tri[j][1], o->tri[j][2], m); put(&L->tri[j], m); }
            show(&L->tri[j], on);
        }
        for (int j = 0; j < 2; j++) {                                  // octaves down
            int on = j < o->bar_count;
            if (on) { fl_line_matrix(o->bar[j][0].x, o->bar[j][0].y, o->bar[j][1].x, o->bar[j][1].y, m); put(&L->slow[j], m); }
            show(&L->slow[j], on);
        }
        if (o->rec_alpha) { fl_circle_matrix(o->centre.x, o->centre.y, o->rec_radius, m); put(&L->rec, m); }
        show(&L->rec, o->rec_alpha != 0);
        fl_circle_matrix(o->centre.x, o->centre.y, 64 * 16, m);       // the loop's number at its centre, never scaled
        put(&L->num, m); show(&L->num, 1);
    }
    show(&page.sync, s->sync); show(&page.free, !s->sync);
    show(&page.play, !s->paused); show(&page.pause, s->paused);
    dress_text(&v);
}

// ---------------------------------------------------------------- the frame

static void say(const Op1fFrame* now)
{
    if (--page.countdown > 0 && !now->popup) return;
    page.countdown = LOG_EVERY;                                       // every four seconds or so
    u32 calls = fl.hook.calls;
    int changed = (calls != 0) != (page.last_calls != 0);
    if (!changed && ++page.periods < 15 && !now->popup) return;      // otherwise once a minute
    page.periods = 0;
    page.last_calls = calls;
    int32_t peak = fl.hook.peak; fl.hook.peak = 0;
    op1f_log(3, LOG_FILE, 1, "4loop: %u master calls, live %u, riding %x, %u calls elsewhere, stand-in %u, "
             "bus peak %08x, heap %d got %u (free %u/%u), queue dropped %u",
             calls, (u32)live, op1f_riding(&op1f_card_fourloop, ASK), (u32)other_calls, page.stood,
             (u32)peak, mem.heap, mem.got, mem.free0, mem.free1, fl.q.dropped);
}

// Plain turns, counted by the core's adapter on the audio thread, become the master's values here.
static void levels_to_values()
{
    for (int k = 0; k < LC_LOOPS; k++) {
        int32_t made = fl.fp.level_made[k], d = made - page.level_used[k];
        if (!d) continue;
        page.level_used[k] = made;
        int v = 0;
        if (op1f_param_get(&op1f_card_fourloop, ASK, MASTER, (unsigned)k, &v) == 0)
            op1f_param_set(&op1f_card_fourloop, ASK, MASTER, (unsigned)k, v + d);
    }
}

extern "C" void fourloop_frame(const Op1fFrame* now) OP1F_NAMED("fourloop_frame");
extern "C" void fourloop_frame(const Op1fFrame* now)
{
    if (now->bytes < (u32)FRAME_L1) return;
    u32 ms = op1f_ms();
    if (!page.asked) {                                                // the first frame: memory, core, page
        page.asked = 1;
        field_host host;
        for (u32 i = 0; i < sizeof host; i++) ((volatile char*)&host)[i] = 0;
        host.mem_get = mem_get; host.now_ms = now_ms;
        field_config cfg;
        for (u32 i = 0; i < sizeof cfg; i++) ((volatile char*)&cfg)[i] = 0;
        cfg.mem_min = MEM_MIN; cfg.mem_want = MEM_WANT; cfg.mem_max = MEM_MAX; cfg.master_slot = 1;
        fp_create(&fl.fp, &host, &cfg);
        fl.fp.level_external = 1;                                     // plain turns become the effect's values
        fl.created = 1;                                               // from here the audio thread may run the core
        make_page();
        page.last_ms = ms;
        page.countdown = 1;
        op1f_log(3, LOG_FILE, 0, "4loop: effect v2 built for %s, page %s, memory %u bytes from heap %d", FL_FW_NAME,
                 document ? "made" : "NOT made", mem.got, mem.heap);
    }
    if (!page.stood && document && op1f_page_stand_in(&op1f_card_fourloop, ASK, document)) page.stood = 1;
    u32 calls = fl.hook.calls;                                        // live: the master's blocks keep coming
    if (calls != page.frame_calls) { page.frame_calls = calls; page.quiet_frames = 0; live = 1; }
    else if (page.quiet_frames < (u32)LIVE_FRAMES) { if (++page.quiet_frames == (u32)LIVE_FRAMES) live = 0; }
    levels_to_values();

    u32 dt = ms - page.last_ms; page.last_ms = ms;
    int mine = now->bytes >= (u32)FRAME_L2 && now->page == &op1f_card_fourloop;
    if (mine && document) { ++page.frames_shown; dress_page(dt); }
    say(now);
}

// ---------------------------------------------------------------- keys (key thread: queue only)

static volatile u32 last_shift;
static volatile u32 own_shift;
static volatile u32 shift_seen;                                      // shift's down went on to the unit too (OP1F_SEEN)                                       // shift as we saw it go down and up while the page showed

extern "C" int fourloop_input(const Op1fInput* e) OP1F_NAMED("fourloop_input");
extern "C" int fourloop_input(const Op1fInput* e)
{
    if (e->bytes < (u32)INPUT_L2) return 0;
    fl_queue* q = &fl.q;
    u32 k = e->key;
    int down = e->what == (u32)OP1F_DOWN || e->what == (u32)OP1F_DOWN_SHIFT;
    u32 sh = (e->what == (u32)OP1F_DOWN_SHIFT || e->shift || own_shift) ? 1u : 0u;
    if (down && k == (u32)KEY_METRONOME && sh) {      // shift + metronome: open / close, in any mode
        void* doc = document;
        if (!doc) return 0;
        if (e->mine && stand_in_up()) return 1;                                 // T3's page is up already: leave it
        touches = touches + 1;                                                  // the status line shows on opening
        if (e->mine) op1f_page_close(&op1f_card_fourloop);
        else op1f_page_open(&op1f_card_fourloop, doc);
        return 1;
    }
    if (e->what == (u32)OP1F_UP) {                                              // ups of downs we took
        if (k == (u32)KEY_SHIFT) {
            own_shift = 0; if (last_shift) { last_shift = 0; fl_push(q, FE_KEY, FK_SHIFT, 0); }
            if (shift_seen) { shift_seen = 0; return 0; }                       // its down went on to the unit: so does its up
        }
        else if (k >= (u32)KEY_PRESS0 && k < (u32)KEY_PRESS0 + 4) fl_push(q, FE_ENC_RELEASE, (int)(k - KEY_PRESS0), 0);
        else if (k == (u32)KEY_T1) fl_push(q, FE_KEY, FK_ALT, 0);
        else if (k == (u32)KEY_T2) fl_push(q, FE_KEY, FK_SIZE, 0);
        else if (k == (u32)KEY_HELP) fl_push(q, FE_KEY, FK_INFO, 0);
        return 1;
    }
    if (!e->mine) return 0;
    touches = touches + 1;
    if (down && k == (u32)KEY_SHIFT) {                                         // its up comes to us, so shift is ours to know
        own_shift = 1;
        if (!last_shift) { last_shift = 1; fl_push(q, FE_KEY, FK_SHIFT, 1); }
        if (stand_in_up()) { shift_seen = 1; return OP1F_SEEN; }               // T3's page: the unit sees shift too (shift + T3 = its effect list)
        return 1;
    }
    if (sh != last_shift) { last_shift = sh; fl_push(q, FE_KEY, FK_SHIFT, (int)sh); }
    if (e->what == (u32)OP1F_TURN) {
        if (k > 3u) return 0;
        if (!live) return stand_in_up() ? 0 : 1;                               // off: T3's page lets the unit turn the values
        fl_push(q, FE_ENC_TURN, (int)k, e->turn);
        return 1;
    }
    if (!down) return 0;
    if (k == (u32)KEY_OCTAVE_DOWN || k == (u32)KEY_OCTAVE_UP) return 1;      // < >: kept from the mixer's tape moves (no octave helper in the base)
    if (k == (u32)KEY_T3) {
        if (sh) return stand_in_up() ? 0 : 1;                                   // shift + T3: the unit's list of master effects
        if (live) { fl_push(q, FE_KEY, fl.fp.engine.paused ? FK_PLAY : FK_STOP, 1); return 1; }   // T3: pause / play the loops
        return stand_in_up() ? 0 : 1;                                           // switched off: T3 is the unit's on
    }
    if (!live && k != (u32)KEY_HELP) return 1;                                  // nothing runs the core now: drop it
    if (k >= (u32)KEY_PRESS0 && k < (u32)KEY_PRESS0 + 4) { fl_push(q, FE_ENC_PRESS, (int)(k - KEY_PRESS0), 0); return 1; }
    if (k >= (u32)KEY_CLICK0 && k < (u32)KEY_CLICK0 + 4) { fl_push(q, FE_ENC_RELEASE, (int)(k - KEY_CLICK0), 0); return 1; }   // a press that came up unturned
    switch (k) {
    case KEY_T1: fl_push(q, FE_KEY, sh ? FK_SYNC : FK_ALT, 1); return 1;     // held: turn = pan; shift + T1 = sync
    case KEY_T2:                                                               // held: turn = loop size; shift + T2 = pause/play
        if (sh) fl_push(q, FE_KEY, fl.fp.engine.paused ? FK_PLAY : FK_STOP, 1);
        else fl_push(q, FE_KEY, FK_SIZE, 1);
        return 1;
    case KEY_T4: fl_push(q, FE_KEY, FK_SYNC, 1); return 1;                   // sync on/off
    case KEY_HELP: fl_push(q, FE_KEY, FK_INFO, 1); return 1;
    default: return 0;                                                          // the mode keys and the keyboard stay the firmware's
    }
}
