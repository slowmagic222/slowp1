// Host tests for 4loop.pack's glue: the master-effect hook, the Q31 audio path through 4loop, the event queue,
// the page geometry and the runtime helpers. Build and run: tests/run.sh
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "fourloop_glue.h"

extern "C" {
uint32_t t___udivsi3(uint32_t, uint32_t); uint32_t t___umodsi3(uint32_t, uint32_t);
int32_t t___divsi3(int32_t, int32_t); int32_t t___modsi3(int32_t, int32_t);
uint64_t t___udivdi3(uint64_t, uint64_t); uint64_t t___umoddi3(uint64_t, uint64_t);
int64_t t___divdi3(int64_t, int64_t); int64_t t___moddi3(int64_t, int64_t);
int64_t t___muldi3(int64_t, int64_t); int64_t t___ashldi3(int64_t, int); int64_t t___ashrdi3(int64_t, int);
uint64_t t___lshrdi3(uint64_t, int);
void *t_memmove(void *, const void *, size_t);
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t rng = 88172645463325252ull;
static uint64_t r64() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static uint64_t rnd_any() { int k = (int)(r64() % 4); uint64_t v = r64(); return k == 0 ? v & 0xff : k == 1 ? v & 0xffffffffull : k == 2 ? v >> (r64() % 64) : v; }

static void test_rt()
{
    for (int i = 0; i < 200000; i++) {
        uint32_t a = (uint32_t)rnd_any(), b = (uint32_t)rnd_any(); if (!b) b = 1;
        CHECK(t___udivsi3(a, b) == a / b && t___umodsi3(a, b) == a % b, "u32 %u %u", a, b);
        int32_t sa = (int32_t)a, sb = (int32_t)b;
        if (!(sa == INT32_MIN && sb == -1)) CHECK(t___divsi3(sa, sb) == sa / sb && t___modsi3(sa, sb) == sa % sb, "s32 %d %d", sa, sb);
        uint64_t x = rnd_any(), y = rnd_any(); if (!y) y = 3;
        CHECK(t___udivdi3(x, y) == x / y && t___umoddi3(x, y) == x % y, "u64");
        int64_t sx = (int64_t)x, sy = (int64_t)y;
        if (!(sx == INT64_MIN && sy == -1)) CHECK(t___divdi3(sx, sy) == sx / sy && t___moddi3(sx, sy) == sx % sy, "s64 %lld %lld", (long long)sx, (long long)sy);
        CHECK((uint64_t)t___muldi3(sx, sy) == (uint64_t)sx * (uint64_t)sy, "mul");
        int s = (int)(r64() % 64);
        CHECK((uint64_t)t___ashldi3(sx, s) == ((uint64_t)sx << s), "shl %d", s);
        CHECK(t___lshrdi3(x, s) == (x >> s), "lshr %d", s);
        CHECK(t___ashrdi3(sx, s) == (sx >> s), "ashr %d", s);
    }
    CHECK(t___udivsi3(5, 0) == 0 && t___udivdi3(5, 0) == 0, "division by zero answers 0");
    char buf[16] = "abcdefghij"; t_memmove(buf + 2, buf, 5); CHECK(!memcmp(buf, "ababcdehij", 10), "memmove overlap");
}

// ---- a stand-in for the firmware's master effect object
static uintptr_t stock_vt_storage[FL_VT_BEFORE + FL_VT_AFTER];   // the hook copies 32 entries; on the unit they are rodata
static uintptr_t *stock_vt = &stock_vt_storage[2];
static uintptr_t object[4];                    // object[0] = vtable
static volatile uintptr_t slot;
static volatile uint8_t active = 1;
static int stock_calls, setup_calls;
static void stock_process(void *self, int32_t *l, int32_t *r, void *) {
    stock_calls++; (void)self;
    for (int i = 0; i < FL_BLOCK; i++) { l[i] = l[i] / 2; r[i] = r[i] / 2; }    // a "master effect": -6 dB
}
static void stock_setup(void *, int) { setup_calls++; }
static void stock_dtor(void *) {}

static fl_env env_any() {
    fl_env e; e.slot = &slot; e.active = &active;
    e.obj_lo = 0; e.obj_hi = UINTPTR_MAX; e.vt_lo = 0; e.vt_hi = UINTPTR_MAX;
    e.code_lo[0] = 0; e.code_hi[0] = UINTPTR_MAX; e.code_lo[1] = e.code_hi[1] = 0;
    return e;
}
static void build_stock() {
    stock_vt_storage[0] = 0; stock_vt_storage[1] = 0x1234;                       // offset-to-top, typeinfo
    stock_vt[0] = (uintptr_t)&stock_dtor; stock_vt[1] = (uintptr_t)&stock_dtor;
    stock_vt[2] = (uintptr_t)&stock_process; stock_vt[3] = (uintptr_t)&stock_setup;
    object[0] = (uintptr_t)stock_vt; slot = (uintptr_t)object;
}
static void call_vt(int32_t *l, int32_t *r) {            // what the master stage does
    uintptr_t *vt = (uintptr_t *)object[0];
    ((fl_process_fn)vt[2])(object, l, r, 0);
}

static void test_hook()
{
    fl_env env = env_any();
    memset(&fl, 0, sizeof fl);
    slot = 0;
    CHECK(fl_hook_step(&fl.hook, &env, 1) == FL_HOOK_NO_FX, "no object: no fx");
    build_stock();
    CHECK(fl_hook_step(&fl.hook, &env, 1) == FL_HOOK_ON, "wraps");
    uintptr_t *vt = (uintptr_t *)object[0];
    CHECK(vt != stock_vt, "object's vtable word is ours");
    CHECK(vt[-1] == 0x1234 && vt[0] == stock_vt[0] && vt[3] == stock_vt[3], "the other entries and RTTI are stock's");
    CHECK(vt[2] == (uintptr_t)&fl_process, "entry 2 is fl_process");
    CHECK(fl_hook_step(&fl.hook, &env, 1) == FL_HOOK_ON && fl.hook.wraps == 1, "a second step changes nothing");
    ((void (*)(void *, int))vt[3])(object, 0); CHECK(setup_calls == 1, "setup still reaches stock");

    int32_t l[FL_BLOCK], r[FL_BLOCK];
    for (int i = 0; i < FL_BLOCK; i++) l[i] = r[i] = 1 << 28;
    stock_calls = 0; call_vt(l, r);
    CHECK(stock_calls == 1 && l[0] == 1 << 27, "stock's effect runs first (not created yet: 4loop passes)");
    fl.hook.replace = 1; for (int i = 0; i < FL_BLOCK; i++) l[i] = r[i] = 1 << 28;
    call_vt(l, r); CHECK(stock_calls == 1 && l[0] == 1 << 28, "replace skips stock");
    fl.hook.replace = 0;

    object[0] = (uintptr_t)stock_vt;                      // the firmware rebuilds the master effect
    CHECK(fl_hook_step(&fl.hook, &env, 1) == FL_HOOK_ON && fl.hook.wraps == 2, "rewraps after a rebuild");
    CHECK(object[0] == (uintptr_t)&fl.hook.copy[1][FL_VT_BEFORE], "with the other copy");
    stock_calls = 0; call_vt(l, r); CHECK(stock_calls == 1, "second copy reaches stock's process");

    active = 0; CHECK(fl_hook_step(&fl.hook, &env, 1) == FL_HOOK_NO_FX, "fx off reported"); active = 1;
    CHECK(fl_hook_step(&fl.hook, &env, 0) == FL_HOOK_OFF && object[0] == (uintptr_t)stock_vt, "disable restores stock");

    fl_env narrow = env; narrow.code_hi[0] = 1;            // stock's process outside the code ranges
    CHECK(fl_hook_step(&fl.hook, &narrow, 1) == FL_HOOK_REFUSED && object[0] == (uintptr_t)stock_vt, "refuses, writes nothing");
    narrow = env; narrow.obj_hi = 1;
    CHECK(fl_hook_step(&fl.hook, &narrow, 1) == FL_HOOK_REFUSED && object[0] == (uintptr_t)stock_vt, "refuses an odd object");
}

static uint32_t clock_ms;
static void *mem_get(void *, uint32_t, uint32_t want, uint32_t, uint32_t *got) { *got = want; return calloc(1, want); }
static void mem_release(void *, void *b) { free(b); }
static uint32_t now(void *) { return clock_ms; }

static void block(int32_t *l, int32_t *r, int32_t level)
{
    for (int i = 0; i < FL_BLOCK; i++) { l[i] = level ? (int32_t)((i & 8) ? level : -level) : 0; r[i] = l[i] / 2; }
    call_vt(l, r);
    clock_ms += 1;                                         // 64 samples is 1.45 ms; near enough for the hold clock
}

static void test_audio()
{
    fl_env env = env_any();
    memset(&fl, 0, sizeof fl);
    build_stock();
    stock_vt[2] = (uintptr_t)0; stock_vt[2] = (uintptr_t)&stock_process;
    field_host host; memset(&host, 0, sizeof host);
    host.mem_get = mem_get; host.mem_release = mem_release; host.now_ms = now;
    field_config cfg; memset(&cfg, 0, sizeof cfg);
    cfg.mem_min = 1 << 20; cfg.mem_want = 4 << 20; cfg.mem_max = 4 << 20; cfg.master_slot = 1;
    fp_create(&fl.fp, &host, &cfg); fl.created = 1;
    fl.hook.replace = 1;                                   // 4loop alone, so the dry check is exact
    CHECK(fl_hook_step(&fl.hook, &env, 1) == FL_HOOK_ON, "wrapped");

    int32_t l[FL_BLOCK], r[FL_BLOCK], ref[FL_BLOCK];
    for (int i = 0; i < FL_BLOCK; i++) l[i] = r[i] = ref[i] = (int32_t)(0x12345 * (i + 1)) ^ (i << 20);
    call_vt(l, r);
    CHECK(!memcmp(l, ref, sizeof l), "no loops: the Q31 dry signal comes back bit for bit");

    fl_push(&fl.q, FE_ENC_PRESS, 0, 0); fl_push(&fl.q, FE_ENC_RELEASE, 0, 0);   // record loop 1
    for (int b = 0; b < 400; b++) block(l, r, 0x20000000);
    fl_push(&fl.q, FE_ENC_PRESS, 0, 0); fl_push(&fl.q, FE_ENC_RELEASE, 0, 0);   // finish: it plays
    for (int b = 0; b < 50; b++) block(l, r, 0);
    lc_view v; lc_snapshot(&fl.fp.engine, &v);
    CHECK(v.loop[0].state == LC_PLAY, "loop 1 plays (state %d)", v.loop[0].state);
    int32_t pk = 0, pr = 0;
    for (int b = 0; b < 200; b++) { block(l, r, 0); for (int i = 0; i < FL_BLOCK; i++) { if (abs(l[i]) > pk) pk = abs(l[i]); if (abs(r[i]) > pr) pr = abs(r[i]); } }
    CHECK(pk > 0x08000000 && pr > 0x04000000 && pk > pr, "the loop plays back into silence, in stereo (L %08x, R %08x)", pk, pr);
    printf("  loop playback peak L %08x R %08x\n", pk, pr);

    for (int i = 0; i < 100; i++) fl_push(&fl.q, FE_ENC_TURN, 1, 1024);
    CHECK(fl.q.dropped == 100 - FL_QUEUE, "queue drops beyond %d (%u)", FL_QUEUE, fl.q.dropped);
    block(l, r, 0);
    CHECK(fl.q.head == fl.q.tail, "audio thread drained the queue");

    // hold to clear: press, hold > 1 s of blocks, the loop empties
    fl_push(&fl.q, FE_ENC_PRESS, 0, 0);
    for (int b = 0; b < 1200; b++) block(l, r, 0);
    fl_push(&fl.q, FE_ENC_RELEASE, 0, 0); block(l, r, 0);
    for (int b = 0; b < 100; b++) block(l, r, 0);
    lc_snapshot(&fl.fp.engine, &v);
    CHECK(v.loop[0].state == LC_EMPTY, "held 1 s: cleared (state %d)", v.loop[0].state);
    fp_destroy(&fl.fp);
}

// Level 8: the effect path. Values are the loops' levels; plain turns go to level_made, not to the core.
static int values[4] = { 32, 32, 32, 32 };
static void fx_block(int32_t *l, int32_t *r, int32_t level)
{
    for (int i = 0; i < FL_BLOCK; i++) { l[i] = level ? (int32_t)((i & 8) ? level : -level) : 0; r[i] = l[i] / 2; }
    fl_effect(l, r, FL_BLOCK, values, 4);
    clock_ms += 1;
}

static void test_effect()
{
    memset(&fl, 0, sizeof fl);
    field_host host; memset(&host, 0, sizeof host);
    host.mem_get = mem_get; host.mem_release = mem_release; host.now_ms = now;
    field_config cfg; memset(&cfg, 0, sizeof cfg);
    cfg.mem_min = 1 << 20; cfg.mem_want = 4 << 20; cfg.mem_max = 4 << 20; cfg.master_slot = 1;
    fp_create(&fl.fp, &host, &cfg); fl.fp.level_external = 1; fl.created = 1;

    int32_t l[FL_BLOCK], r[FL_BLOCK], ref[FL_BLOCK];
    for (int i = 0; i < FL_BLOCK; i++) l[i] = r[i] = ref[i] = (int32_t)(0x12345 * (i + 1)) ^ (i << 20);
    fl_effect(l, r, FL_BLOCK, values, 4);
    CHECK(!memcmp(l, ref, sizeof l), "effect, no loops: dry bit for bit");
    CHECK(fl_level_gain(0) == 0 && fl_level_gain(16) == 16384 && fl_level_gain(32) == 32767 && fl_level_gain(99) == 32767 && fl_level_gain(-3) == 0, "level gains");

    fl_push(&fl.q, FE_ENC_PRESS, 0, 0); fl_push(&fl.q, FE_ENC_RELEASE, 0, 0);
    for (int b = 0; b < 50; b++) fx_block(l, r, 0);
    CHECK(fl.fp.engine.loop[0].state == LC_ARM && fl.fp.engine.loop[0].duration == 0, "first take armed, waits in silence (state %d)", fl.fp.engine.loop[0].state);
    for (int b = 0; b < 400; b++) fx_block(l, r, 0x20000000);
    CHECK(fl.fp.engine.loop[0].state == LC_REC, "first take records once sound comes");
    fl_push(&fl.q, FE_ENC_PRESS, 0, 0); fl_push(&fl.q, FE_ENC_RELEASE, 0, 0);
    for (int b = 0; b < 50; b++) fx_block(l, r, 0);
    int32_t full = 0;
    for (int b = 0; b < 200; b++) { fx_block(l, r, 0); for (int i = 0; i < FL_BLOCK; i++) if (abs(l[i]) > full) full = abs(l[i]); }
    CHECK(full > 0x08000000, "effect: loop 1 plays at value 32 (%08x)", full);

    values[0] = 8;                                          // a quarter: the knob, an LFO or a loaded save
    int32_t quarter = 0;
    for (int b = 0; b < 400; b++) { fx_block(l, r, 0); if (b > 200) for (int i = 0; i < FL_BLOCK; i++) if (abs(l[i]) > quarter) quarter = abs(l[i]); }
    CHECK(quarter < full / 3 && quarter > full / 6, "effect: value 8 plays loop 1 near a quarter (%08x of %08x)", quarter, full);
    CHECK(fl.fp.engine.loop[0].target_gain == 8192, "value written into the core");

    int before = fl.fp.engine.loop[1].target_gain;
    fl_push(&fl.q, FE_ENC_TURN, 1, 3 * 1024); fl_push(&fl.q, FE_ENC_TURN, 1, -1024);
    fx_block(l, r, 0);
    CHECK(fl.fp.level_made[1] == 2 && fl.fp.engine.loop[1].target_gain == before, "plain turns counted for the values, core untouched (%d)", fl.fp.level_made[1]);
    values[0] = 32;
    fp_destroy(&fl.fp);
}

static void test_geometry()
{
    int m[9];
    fl_line_matrix(100 * 16, 50 * 16, 130 * 16, 90 * 16, m);   // a 30 x 40 px line
    // where the node's end (64, 0) and a point (0, 1) land, in px
    double ex = (m[0] * 64.0 + m[6]) / 1024, ey = (m[1] * 64.0 + m[7]) / 1024;
    double px = (m[3] * 1.0 + m[6]) / 1024 - 100, py = (m[4] * 1.0 + m[7]) / 1024 - 50;
    CHECK(ex == 130 && ey == 90 && m[6] / 1024 == 100 && m[7] / 1024 == 50, "line ends at (%g, %g)", ex, ey);
    CHECK(px * px + py * py > 0.98 && px * px + py * py < 1.02 && (px * 30 + py * 40) * (px * 30 + py * 40) < 0.01, "unit perpendicular (%g, %g)", px, py);
    fl_circle_matrix(240 * 16, 110 * 16, 32 * 16, m);
    CHECK((m[0] * 64.0) / 1024 == 32 && m[6] / 1024 == 240 && m[7] / 1024 == 110, "circle r 32 at (240, 110)");
    for (uint32_t v = 0; v < 2000000; v += 7) { uint32_t s = fl_isqrt(v); CHECK(s * s <= v && (s + 1) * (s + 1) > v, "isqrt %u", v); if (fails > 5) break; }
}

int main()
{
    test_rt(); printf("runtime helpers: done\n");
    test_hook(); printf("hook: done\n");
    test_audio(); printf("audio: done\n");
    test_effect(); printf("effect: done\n");
    test_geometry(); printf("geometry: done\n");
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails != 0;
}
