// Host test of roads.pack's engine: roads.cpp compiled for the PC (OP1F_HOST, 32-bit) with stand-ins for the base.
// Checks the voice render against the TINE core run directly (which tests/test_tine.c holds to the OG engine), the
// pitch from the step (against the OG's own sum), velocity layers, style input, the bank file's loading, the page.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <initializer_list>
#include "../roads.cpp"

extern "C" const Op1fCard op1f_card_roads = {};
static unsigned char memory_block[BANK_ROOM];
static int memory_given = 0;
static const char* bank_path;
static uint16_t page_block[scene::W * scene::H];
static int blocks_painted = 0;
extern "C" void* op1f_memory(const Op1fCard*, unsigned ask) { return !memory_given ? 0 : ask == MEMORY_ASK ? (void*)memory_block : ask == PAGE_ASK ? (void*)page_block : 0; }
extern "C" void op1f_paint_block(void*, int x, int y, int w, int h, const unsigned short* pixels, int stride, int) { if (x == 0 && y == 0 && w == scene::W && h == scene::H && stride == scene::W && pixels == page_block) ++blocks_painted; }
extern "C" int op1f_read(const Op1fCard*, unsigned folder, const char* name, void* buffer, unsigned room) {
    if (folder != 0 || strcmp(name, "roads.msb") || !bank_path) return OP1F_NO_FILE;
    FILE* f = fopen(bank_path, "rb"); if (!f) return OP1F_NO_FILE;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if ((unsigned long)n > room) { fclose(f); return OP1F_TOO_LARGE; }
    size_t got = fread(buffer, 1, (size_t)n, f); fclose(f); return (int)got;
}
static unsigned cfg_writes; static unsigned cfg_word = 99;
extern "C" int op1f_write(const Op1fCard*, unsigned, const char* name, const void* data, unsigned bytes) { if (!strcmp(name, "roads.cfg") && bytes == 8) { ++cfg_writes; cfg_word = ((const unsigned*)data)[1]; } return (int)bytes; }
extern "C" void op1f_log(int, const char*, int, const char* format, ...) { va_list a; va_start(a, format); printf("  log: "); vprintf(format, a); printf("\n"); va_end(a); }
extern "C" int op1f_paint_text(void*, int, int, const char*, unsigned, int) { return 0; }
extern "C" void op1f_paint_box(void*, int, int, int, int, unsigned) {}
extern "C" void* op1f_picture(const Op1fCard*, const char*) { return 0; }
extern "C" int op1f_text_set(void*, const char*, const char*, int, unsigned) { return 0; }
extern "C" void* op1f_text_node(void*, const char*) { return 0; }
extern "C" void op1f_show(void*, int) {}
extern "C" void op1f_move(void*, const int*) {}
extern "C" void op1f_paint_mask(void*, int, int, int, int, const unsigned char*, int, unsigned) {}
extern "C" int op1f_page_stand_in(const Op1fCard*, unsigned, void*) { return 1; }
extern "C" int op1f_param_get(const Op1fCard*, unsigned, unsigned, unsigned, int* out) { *out = 0; return 0; }
static int style_set = -1;
extern "C" int op1f_param_set(const Op1fCard*, unsigned, unsigned, unsigned index, int value) { if (index == 2) style_set = value; return 0; }
extern "C" int op1f_param_heard(const Op1fCard*, unsigned, unsigned, unsigned, int* out) { *out = 0; return 0; }

static long checks = 0, fails = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { if (++fails < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static Voice* voices[6];
static int out[256];
static int values[8] = {0, 55, 0, 0, 0, 50, 100, 0};   // muffle, sustain, style, ambient; start, loop in, loop out, wow

static void frame(unsigned disk) { Op1fFrame f = {}; f.bytes = 40; f.disk = disk; roads_frame(&f); }
static void render(Voice* v, unsigned voice, unsigned step, int velocity, unsigned events) {
    Op1fVoiceBlock b = {}; b.bytes = 52; b.slot = 0; b.voice = voice; b.frames = 128; b.rate = 88200; b.out = out;
    b.values = values; b.step = step; b.velocity = velocity; b.events = events; b.state = v;
    roads_render(&b);
}
static int quiet() { for (int k = 0; k < 256; ++k) if (out[k]) return 0; return 1; }

// The core run directly at 88.2 kHz with the OG's rate: the reference
struct Ref { ms_core_t c; int32_t l[128], r[128]; };
static Ref* ref;
static u32 og_rate(const ms_zone_t* z, u32 step) {
    unsigned long long r = (unsigned long long)step * (12000u * 65536u / 88200u) / STEP[z->root];
    return r < 256 ? 256u : r > 262144 ? 262144u : (u32)r;
}
static void ref_start(int note, int vel) {
    ms_core_init(&ref->c, 88200u);
    ms_part_t* p = &ref->c.parts[0];
    p->bank = &bank_store; p->rr_spread = 0; p->wash_width = 0;
    p->knob[0] = q15(values[0]); p->knob[1] = q15(values[1]); p->knob[2] = q15(values[7]); p->knob[3] = q15(values[3]);
    p->attack_trim = q15(values[4]); p->loop_trim_start = loop_in_word(values[5]); p->loop_trim_end = q15(values[6]); p->width = WIDTH;
    p->source_octave = (uint8_t)style_of(values[2]);
    int vi = ms_note_on(&ref->c, 0, note, vel);
    if (vi >= 0) ref->c.voices[vi].rate = og_rate(&bank_store.zones[ref->c.voices[vi].zone], STEP[note]);
}
static void ref_block() { memset(ref->l, 0, sizeof ref->l); memset(ref->r, 0, sizeof ref->r); ms_render(&ref->c, ref->l, ref->r, 128); }

static int compare_note(Voice* v, int note, int vel, int blocks, int release_at) {
    memset((void*)rr_shared, 0, sizeof rr_shared);
    ref_start(note, vel);
    int bad = 0;
    for (int blk = 0; blk < blocks; ++blk) {
        unsigned ev = blk == 0 ? (unsigned)OP1F_VOICE_BEGINS : blk == release_at ? (unsigned)OP1F_VOICE_ENDS : 0u;
        if (blk == release_at) ms_note_off(&ref->c, 0, note);
        render(v, 0, STEP[note], vel << 24, ev);
        ref_block();
        for (int k = 0; k < 128; ++k) if (out[2 * k] != to_q31(ref->l[k]) || out[2 * k + 1] != to_q31(ref->r[k])) ++bad;
    }
    return bad;
}

int main(int argc, char** argv) {
    bank_path = argc > 1 ? argv[1] : "roads.msb";
    ref = (Ref*)calloc(1, sizeof(Ref));
    for (int i = 0; i < 6; ++i) voices[i] = (Voice*)calloc(1, VOICE_BYTES);
    printf("voice memory: %u of %u bytes; bank state %u bytes\n", (unsigned)sizeof(Voice), (unsigned)VOICE_BYTES, (unsigned)sizeof(ms_bank_t));

    render(voices[0], 0, STEP[60], 100 << 24, OP1F_VOICE_FRESH | OP1F_VOICE_BEGINS);
    CHECK(quiet(), "no reset: silence");
    for (int i = 0; i < 6; ++i) roads_reset(0, (unsigned)i, voices[i]);
    render(voices[0], 0, STEP[60], 100 << 24, OP1F_VOICE_FRESH | OP1F_VOICE_BEGINS);
    CHECK(quiet(), "no bank: silence");
    frame(0); CHECK(!bank_ready && bank_state == BANK_WAITING, "no memory yet: waits");
    memory_given = 1;
    const char* real = bank_path; bank_path = 0;
    frame(0); CHECK(!bank_ready && bank_state == BANK_MISSING, "no file: says so");
    bank_path = real;
    frame(0); CHECK(!bank_ready, "no retry until the disk mode closes");
    frame(1); frame(0); CHECK(bank_ready == &bank_store, "loaded after the disk mode closes");
    CHECK(bank_store.flags & MS_BANK_TINE, "the TINE flag");
    printf("bank: '%s', %u zones, %u Hz, flags %u\n", bank_store.name, (unsigned)bank_store.zone_count, (unsigned)bank_store.rate, (unsigned)bank_store.flags);

    // pitch: the OG's own sum, exactly, at every note, bends and odd steps of every zone
    for (int n = 0; n < 128; ++n) CHECK(note_of(STEP[n]) == n, "note_of(%d)", n);
    CHECK(STEP[69] == 21426141u, "A4");
    long rates = 0; int rate_bad = 0;
    for (unsigned zi = 0; zi < bank_store.zone_count; ++zi) {
        const ms_zone_t* z = &bank_store.zones[zi];
        for (int n = 0; n < 128; ++n) for (int cents = -60; cents <= 60; cents += 7) {
            u32 s = (u32)(STEP[n] * pow(2.0, cents / 1200.0) + 0.5);
            rate_bad += rate_for_step(&bank_store, z, s) != og_rate(z, s); ++rates;
        }
    }
    CHECK(rate_bad == 0, "%d of %ld rates differ from the OG's", rate_bad, rates);
    printf("pitch: %ld steps, every rate the OG's\n", rates);

    // the voice against the core, frame for frame
    int knobsets[][8] = {{0,55,0,0,0,50,100,0}, {0,0,0,0,0,50,100,0}, {60,90,0,0,0,50,100,0}, {20,30,0,0,0,50,100,70},
                         {20,30,0,70,0,50,100,0}, {40,30,1,100,0,50,100,100}, {0,55,2,30,30,20,80,0}, {0,55,0,0,90,50,100,0}};
    int notes[] = {21, 24, 36, 48, 55, 60, 64, 67, 72, 79, 84, 96, 100, 108};
    long frames = 0; int bad = 0;
    for (auto& ks : knobsets) {
        memcpy(values, ks, sizeof values);
        for (int note : notes) for (int vel : {20, 64, 100}) {
            render(voices[0], 0, STEP[note], 0, OP1F_VOICE_FRESH); CHECK(quiet(), "fresh: silence");
            int b1 = compare_note(voices[0], note, vel, 400, 200);
            if (b1 && bad < 1) printf("  knobs %d %d %d %d note %d vel %d: %d differ\n", ks[0], ks[1], ks[2], ks[3], note, vel, b1);
            bad += b1;
            frames += 400 * 128;
        }
    }
    CHECK(bad == 0, "%d frames differ from the core", bad);
    printf("core equivalence: %ld frames at 88.2 kHz identical (8 knob sets, styles 0-2, 14 notes, 3 velocities, release)\n", frames);

    // velocity: soft, middle and hard pick the three dynamics (roads.msb); roads-rr.msb takes turns
    {
        memcpy(values, knobsets[0], sizeof values);
        int take[3];
        int vels[3] = {20, 64, 120};
        for (int i = 0; i < 3; ++i) {
            render(voices[1], 1, STEP[60], 0, OP1F_VOICE_FRESH); render(voices[1], 1, STEP[60], vels[i] << 24, OP1F_VOICE_BEGINS);
            take[i] = bank_store.zones[voices[1]->core.voices[0].zone].take;
        }
        bool layered = bank_store.zones[0].vel_high < 127 || bank_store.zones[0].vel_low > 0;
        if (layered) CHECK(take[0] == 3 && take[1] == 2 && take[2] == 1, "velocity layers (take 1 the hardest): takes %d %d %d", take[0], take[1], take[2]);
        else CHECK(take[0] != take[1] && take[1] != take[2], "round robin: takes %d %d %d", take[0], take[1], take[2]);
        printf("%s: velocity 20/64/120 played takes %d %d %d\n", layered ? "velocity layers" : "round robin", take[0], take[1], take[2]);
    }

    // the velocity switch: a red-orange press; off = every take in turn whatever the velocity; kept in roads.cfg
    {
        Op1fInput e = {}; e.bytes = 40; e.what = OP1F_DOWN; e.key = 57; e.mine = 1;
        CHECK(roads_input(&e) == 1, "red-orange press taken");
        frame(0); CHECK(velocity_off == 1 && cfg_writes == 1 && cfg_word == 1, "velocity off, written (%u %u)", cfg_writes, cfg_word);
        int take[3];
        for (int i = 0; i < 3; ++i) {
            render(voices[1], 1, STEP[60], 0, OP1F_VOICE_FRESH); render(voices[1], 1, STEP[60], 120 << 24, OP1F_VOICE_BEGINS);
            take[i] = bank_store.zones[voices[1]->core.voices[0].zone].take;
        }
        CHECK(take[0] != take[1] && take[1] != take[2] && take[0] != take[2], "velocity off, all hard: takes %d %d %d in turn", take[0], take[1], take[2]);
        printf("velocity off: velocity 120 x3 played takes %d %d %d\n", take[0], take[1], take[2]);
        e.what = OP1F_UP; CHECK(roads_input(&e) == 1, "its up taken");
        e.what = OP1F_DOWN; roads_input(&e); frame(0);
        CHECK(velocity_off == 0 && cfg_writes == 2 && cfg_word == 0, "velocity on again, written");
    }

    // style: the third knob steps it once a click, the grey press steps it round
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = 2; e.turn = 1024; e.mine = 1;
      CHECK(roads_input(&e) == 1, "grey turn taken"); frame(0); CHECK(style_set == 1, "one click: style 1 (%d)", style_set); }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = 2; e.turn = -1024; e.mine = 1; style_set = -1;
      roads_input(&e); roads_input(&e); frame(0); CHECK(style_set == 0, "turned down past 0: 0 (%d)", style_set); }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_DOWN; e.key = 56; e.mine = 1; style_set = -1;
      CHECK(roads_input(&e) == 1, "press taken"); frame(0); CHECK(style_set == 1, "press: style 1"); }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = 3; e.turn = 1024; e.mine = 1; CHECK(roads_input(&e) == 0, "red without shift: ambient, the unit's"); }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = 3; e.turn = 1024; e.mine = 1; e.shift = 1; CHECK(roads_input(&e) == 0, "shift red: wow, the unit's"); }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = 2; e.turn = 1024; e.mine = 1; e.shift = 1; CHECK(roads_input(&e) == 0, "shift grey: loop out, the unit's"); }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = 2; e.turn = 1024; e.mine = 0; CHECK(roads_input(&e) == 0, "not our page: left alone"); }

    // stereo: the keys spread left to right
    memcpy(values, knobsets[0], sizeof values);
    render(voices[2], 2, STEP[40], 0, OP1F_VOICE_FRESH);
    render(voices[2], 2, STEP[40], 100 << 24, OP1F_VOICE_BEGINS);
    long el = 0, er = 0; for (int i = 0; i < 40; ++i) { render(voices[2], 2, STEP[40], 100 << 24, 0); for (int k = 0; k < 128; ++k) { el += labs(out[2 * k] >> 16); er += labs(out[2 * k + 1] >> 16); } }
    CHECK(el > er, "a low note leans left (%ld %ld)", el, er);

    // release: a let-go note ends, and the voice goes quiet
    render(voices[3], 3, STEP[60], 0, OP1F_VOICE_FRESH);
    render(voices[3], 3, STEP[60], 127 << 24, OP1F_VOICE_BEGINS | OP1F_VOICE_ENDS);
    int blocks = 0; while (!quiet() && blocks < 20000) { render(voices[3], 3, STEP[60], 127 << 24, 0); ++blocks; }
    CHECK(blocks < 20000, "a let-go note falls silent (%d blocks)", blocks);   // TINE's natural decay; the unit's envelope ends the voice
    printf("release: a short note at sustain 55 is silent after %d blocks (%.0f ms)\n", blocks, blocks * 128 / 88.2);

    // notes outside TINE's 24..100 are silent, as on the OG
    render(voices[4], 4, STEP[110], 0, OP1F_VOICE_FRESH); render(voices[4], 4, STEP[110], 100 << 24, OP1F_VOICE_BEGINS);
    CHECK(quiet() && voices[4]->sounding == 0, "note 110: silent");

    // a bad bank is refused
    memory_given = 1; bank_ready = 0; bank_state = BANK_WAITING;
    {
        static char bad_path[512];
        const char* dir = getenv("TMPDIR"); snprintf(bad_path, sizeof bad_path, "%s/roads-bad.msb", dir ? dir : "/tmp");
        FILE* in = fopen(bank_path, "rb"); FILE* o = fopen(bad_path, "wb"); int ch, at = 0;
        while ((ch = fgetc(in)) != EOF) { fputc(at++ == 5000 ? ch ^ 0xff : ch, o); }
        fclose(in); fclose(o); bank_path = bad_path;
    }
    frame(1); frame(0);
    CHECK(!bank_ready && bank_state == BANK_BAD, "a damaged roads.msb is refused");
    {   // the page: shown for a few frames, it paints its scene every frame and draws it every second one; the car moves
        int canvas_stand_in = 0;
        roads_shown(0, OP1F_SHOWN_UP);
        voice_slot[0] = 0; voice_note[0] = 72; begin_serial = begin_serial + 1u; voice_begins[0] = begin_serial;
        for (int f = 0; f < 6; ++f) { Op1fFrame fr = {}; fr.bytes = 40; fr.page = &op1f_card_roads; fr.canvas = &canvas_stand_in; roads_frame(&fr); }
        CHECK(blocks_painted == 6, "page painted %d of 6 frames", blocks_painted);
        int lit = 0; for (int i = 0; i < scene::W * scene::H; ++i) lit += page_block[i] != 0;
        CHECK(lit > 10000, "the scene drew %d pixels", lit);
        CHECK(look.target == white_key(72), "the car heads for the last note");
        int was = look.car_x;
        for (int f = 0; f < 600; ++f) { Op1fFrame fr = {}; fr.bytes = 40; fr.page = &op1f_card_roads; fr.canvas = &canvas_stand_in; roads_frame(&fr); }
        CHECK(look.car_x == scene::key_middle(look.target) && was != look.car_x, "the car arrives (%d -> %d)", was, look.car_x);
        printf("page: frames painted, scene covers %d pixels, the car rolled to key %d\n", lit, look.target);
    }
    printf("all %ld checks, %ld failed\n", checks, fails);
    return fails ? 1 : 0;
}
