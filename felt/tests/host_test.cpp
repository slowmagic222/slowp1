// Host test of felt.pack's engine: felt.cpp compiled for the PC (OP1F_HOST, 32-bit) with stand-ins for the base.
// Checks the voice render against the FELT core run directly (the reference), the
// pitch from the step, events, the bank file's loading and silence without it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <initializer_list>
#include "../felt.cpp"

extern "C" const Op1fCard op1f_card_felt = {};
static unsigned char memory_block[BANK_ROOM];
static int memory_given = 0;
static const char* bank_path;
static uint16_t page_block[scene::W * scene::H];
static uint16_t floor_block[scene::W * scene::H], base_block[scene::W * scene::H];
static int blocks_painted = 0;
extern "C" void* op1f_memory(const Op1fCard*, unsigned ask) { return !memory_given ? 0 : ask == MEMORY_ASK ? (void*)memory_block : ask == PAGE_ASK ? (void*)page_block : ask == FLOOR_ASK ? (void*)floor_block : ask == BASE_ASK ? (void*)base_block : 0; }
extern "C" void op1f_paint_block(void*, int x, int y, int w, int h, const unsigned short* pixels, int stride, int) { if (x == 0 && y == 0 && w == scene::W && h == scene::H && stride == scene::W && pixels == page_block) ++blocks_painted; }
extern "C" int op1f_read(const Op1fCard*, unsigned folder, const char* name, void* buffer, unsigned room) {
    if (folder != 0 || strcmp(name, "felt.msb") || !bank_path) return OP1F_NO_FILE;
    FILE* f = fopen(bank_path, "rb"); if (!f) return OP1F_NO_FILE;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if ((unsigned long)n > room) { fclose(f); return OP1F_TOO_LARGE; }
    size_t got = fread(buffer, 1, (size_t)n, f); fclose(f); return (int)got;
}
static unsigned cfg_writes; static unsigned cfg_word = 99;
extern "C" int op1f_write(const Op1fCard*, unsigned, const char* name, const void* data, unsigned bytes) { if (!strcmp(name, "felt.cfg") && bytes == 8) { ++cfg_writes; cfg_word = ((const unsigned*)data)[1]; } return (int)bytes; }
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
static int values[8] = {73, 0, 0, 0, 0, 0, 100, 60};

static void frame(unsigned disk) { Op1fFrame f = {}; f.bytes = 40; f.disk = disk; felt_frame(&f); }
static void render(Voice* v, unsigned voice, unsigned step, int velocity, unsigned events) {
    Op1fVoiceBlock b = {}; b.bytes = 52; b.slot = 0; b.voice = voice; b.frames = 128; b.rate = 88200; b.out = out;
    b.values = values; b.step = step; b.velocity = velocity; b.events = events; b.state = v;
    felt_render(&b);
}
static int quiet() { for (int k = 0; k < 256; ++k) if (out[k]) return 0; return 1; }

static int heard(Voice* v, int vel) { (void)vel; const ms_voice_t* m = &v->core.voices[0]; return m->gain_l > m->gain_r ? m->gain_l : m->gain_r; }   // the level, whatever the pan
// The core run directly at 44.1 kHz, as the classic: the reference
struct Ref { ms_core_t c; int32_t l[64], r[64]; };
static Ref* ref;
static void ref_start(int note, int vel) {
    ms_core_init(&ref->c, 44100u);
    ms_part_t* p = &ref->c.parts[0];
    p->bank = &bank_store;
    p->knob[0] = q15(values[0]); p->knob[1] = q15(values[1]); p->knob[2] = 0; p->knob[3] = q15(values[3]);
    p->attack_trim = q15(values[4]); p->loop_trim_start = q15(values[5]); p->loop_trim_end = q15(values[6]); p->width = q15(values[7]);
    p->source_octave = (uint8_t)style_of(values[2]);
    p->wash_width = 0; p->vel_amount = velocity_off ? 0 : 24576;   // as felt_reset and felt_render set them
    ms_note_on(&ref->c, 0, note, vel);
}
static void ref_block() { memset(ref->l, 0, sizeof ref->l); memset(ref->r, 0, sizeof ref->r); ms_render(&ref->c, ref->l, ref->r, 64); }

static int compare_note(Voice* v, int note, int vel, int blocks, int release_at) {
    memset((void*)rr_shared, 0, sizeof rr_shared);
    ref_start(note, vel);
    int bad = 0;
    for (int blk = 0; blk < blocks; ++blk) {
        unsigned ev = blk == 0 ? (unsigned)OP1F_VOICE_BEGINS : blk == release_at ? (unsigned)OP1F_VOICE_ENDS : 0u;
        if (blk == release_at) ms_note_off(&ref->c, 0, note);
        render(v, 0, STEP[note], vel << 24, ev);
        ref_block();
        for (int k = 0; k < 64; ++k) {
            int g = v->vel_gain;                  // the core's own, then our velocity floor's gain
            if (out[4 * k + 2] != to_q31((ref->l[k] * (g >> 2)) >> 10) || out[4 * k + 3] != to_q31((ref->r[k] * (g >> 2)) >> 10)) ++bad;
            int pl = k ? ref->l[k - 1] : (blk ? 0x7fffffff : 0);
            (void)pl;
        }
    }
    return bad;
}

int main(int argc, char** argv) {
    bank_path = argc > 1 ? argv[1] : "felt.msb";
    ref = (Ref*)calloc(1, sizeof(Ref));
    for (int i = 0; i < 6; ++i) voices[i] = (Voice*)calloc(1, VOICE_BYTES);
    printf("voice memory: %u of %u bytes; bank state %u bytes\n", (unsigned)sizeof(Voice), (unsigned)VOICE_BYTES, (unsigned)sizeof(ms_bank_t));

    // silence until reset and bank
    render(voices[0], 0, STEP[60], 100 << 24, OP1F_VOICE_FRESH | OP1F_VOICE_BEGINS);
    CHECK(quiet(), "no reset: silence");
    for (int i = 0; i < 6; ++i) felt_reset(0, (unsigned)i, voices[i]);
    render(voices[0], 0, STEP[60], 100 << 24, OP1F_VOICE_FRESH | OP1F_VOICE_BEGINS);
    CHECK(quiet(), "no bank: silence");
    frame(0); CHECK(!bank_ready && bank_state == BANK_WAITING, "no memory yet: waits");
    memory_given = 1;
    const char* real = bank_path; bank_path = 0;
    frame(0); CHECK(!bank_ready && bank_state == BANK_MISSING, "no file: says so");
    bank_path = real;
    frame(0); CHECK(!bank_ready, "no retry until the disk mode closes");
    frame(1); frame(0); CHECK(bank_ready == &bank_store, "loaded after the disk mode closes");
    printf("bank: '%s', %u zones, %u Hz\n", bank_store.name, (unsigned)bank_store.zone_count, (unsigned)bank_store.rate);

    // pitch: the step of a note gives the core's own rate for it; between notes, the exact ratio
    for (int n = 0; n < 128; ++n) CHECK(note_of(STEP[n]) == n, "note_of(%d)", n);
    CHECK(STEP[69] == 21426141u, "A4");
    ms_core_init(&ref->c, 44100u);
    const ms_zone_t* z = &bank_store.zones[0];
    double worst = 0;
    for (int n = 24; n < 108; ++n) {
        CHECK(rate_for_step(&ref->c, &bank_store, z, STEP[n]) == ms_rate_for(&ref->c, &bank_store, z, n), "rate at note %d", n);
        for (int cents = -50; cents <= 50; cents += 10) {
            double s = STEP[n] * pow(2.0, cents / 1200.0);
            double want = ms_rate_for(&ref->c, &bank_store, z, n) * pow(2.0, cents / 1200.0);
            u32 got = rate_for_step(&ref->c, &bank_store, z, (u32)(s + 0.5));
            if (want >= 300 && ms_rate_for(&ref->c, &bank_store, z, n) < 247000u) { double e = (fabs(got - want) - 1.0) / want; if (e > worst) { worst = e; if (e > 1e-3) printf("  n %d cents %d got %u want %.1f\n", n, cents, got, want); } }
        }
    }
    CHECK(worst < 2e-4, "bend error %g", worst);
    printf("pitch: notes exact, bends within %.1e\n", worst);

    // the voice against the core, frame for frame at 44.1 kHz (odd frames of the output); the midpoints between
    int knobsets[][8] = {{73,0,0,0,0,0,100,0}, {73,0,0,0,0,0,100,60}, {0,50,0,0,0,0,100,60}, {100,30,1,0,0,0,100,30},
                         {73,40,0,70,0,0,100,80}, {50,100,0,100,20,30,70,100}, {73,0,0,0,40,0,100,0}};
    int notes[] = {21, 36, 48, 55, 60, 64, 67, 72, 79, 84, 96, 108};
    long frames = 0; int bad = 0;
    for (auto& ks : knobsets) {
        memcpy(values, ks, sizeof values);
        for (int note : notes) for (int vel : {20, 100, 127}) {
            render(voices[0], 0, STEP[note], 0, OP1F_VOICE_FRESH); CHECK(quiet(), "fresh: silence");
            int b1 = compare_note(voices[0], note, vel, 300, 150);
            if (b1 && bad < 1) printf("  knobs %d %d %d %d note %d vel %d: %d differ\n", ks[0], ks[1], ks[2], ks[3], note, vel, b1);
            bad += b1;
            frames += 300 * 64;
        }
    }
    for (int st = 1; st <= 2; ++st) {                  // the styles: the stretched recordings, as the core plays them
        memcpy(values, knobsets[3], sizeof values);
        values[2] = st;
        for (int note : notes) { render(voices[0], 0, STEP[note], 0, OP1F_VOICE_FRESH); bad += compare_note(voices[0], note, 100, 300, 150); frames += 300 * 64; }
        CHECK(voices[0]->core.voices[0].source_shift == st || voices[0]->core.voices[0].note > 127 - 12 * st, "stretch reached the voice");
    }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = 2; e.turn = 1024; e.mine = 1;
      CHECK(felt_input(&e) == 1, "third knob's turn taken"); frame(0); CHECK(style_set == 1, "one click: style 1 (%d)", style_set); }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = 2; e.turn = -1024; e.mine = 1; style_set = -1;
      felt_input(&e); felt_input(&e); frame(0); CHECK(style_set == 0, "turned down past 0: 0 (%d)", style_set); }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_DOWN; e.key = 56; e.mine = 1; style_set = -1;
      CHECK(felt_input(&e) == 1, "press taken"); frame(0); CHECK(style_set == 1, "press: style 1"); }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = 2; e.turn = 1024; e.mine = 1; e.shift = 1; CHECK(felt_input(&e) == 0, "shift: loop out left to the unit"); }
    { Op1fInput e = {}; e.bytes = 40; e.what = OP1F_TURN; e.key = 2; e.turn = 1024; e.mine = 0; CHECK(felt_input(&e) == 0, "not our page: left alone"); }
    // the velocity switch: on (from the start), soft notes quieter but at least 80% after the unit's v/127; a red-orange press turns it off, kept in felt.cfg
    {
        CHECK(velocity_off == 0, "velocity on to begin with");
        render(voices[1], 1, STEP[60], 0, OP1F_VOICE_FRESH); render(voices[1], 1, STEP[60], 64 << 24, OP1F_VOICE_BEGINS);
        int soft = heard(voices[1], 64);
        render(voices[1], 1, STEP[60], 0, OP1F_VOICE_FRESH); render(voices[1], 1, STEP[60], 127 << 24, OP1F_VOICE_BEGINS);
        int hard = heard(voices[1], 127);
        CHECK(soft < hard, "velocity on: soft %d under hard %d", soft, hard);
        Op1fInput e = {}; e.bytes = 40; e.what = OP1F_DOWN; e.key = 57; e.mine = 1;
        CHECK(felt_input(&e) == 1, "red-orange press taken");
        frame(0); CHECK(velocity_off == 1 && cfg_writes == 1 && cfg_word == 1, "velocity off, written (%u %u)", cfg_writes, cfg_word);
        render(voices[1], 1, STEP[60], 0, OP1F_VOICE_FRESH); render(voices[1], 1, STEP[60], 64 << 24, OP1F_VOICE_BEGINS);
        soft = heard(voices[1], 64);
        render(voices[1], 1, STEP[60], 0, OP1F_VOICE_FRESH); render(voices[1], 1, STEP[60], 127 << 24, OP1F_VOICE_BEGINS);
        hard = heard(voices[1], 127);
        CHECK(soft == hard, "velocity off: one level (%d %d)", soft, hard);
        e.what = OP1F_UP; CHECK(felt_input(&e) == 1, "its up taken");
        e.what = OP1F_DOWN; felt_input(&e); frame(0);
        CHECK(velocity_off == 0 && cfg_writes == 2 && cfg_word == 0, "velocity on again, written");
    }
    CHECK(bad == 0, "%d frames differ from the core", bad);
    printf("core equivalence: %ld frames at 44.1 kHz identical (7 knob sets, 12 notes, 3 velocities, release, stretch 1 and 2)\n", frames);

    // the doubled frames are the midpoints
    memcpy(values, knobsets[1], sizeof values);
    render(voices[1], 1, STEP[60], 0, OP1F_VOICE_FRESH);
    render(voices[1], 1, STEP[60], 100 << 24, OP1F_VOICE_BEGINS);
    int prev_l = 0, mid_bad = 0;
    for (int blk = 0; blk < 20; ++blk) {
        for (int k = 0; k < 64; ++k) {
            int g = voices[1]->vel_gain;
            int l = (voices[1]->left[k] * (g >> 2)) >> 10;
            if (out[4 * k] != to_q31((prev_l + l) >> 1) && blk) ++mid_bad;
            prev_l = l;
        }
        render(voices[1], 1, STEP[60], 100 << 24, 0);
    }
    CHECK(mid_bad == 0, "midpoints %d", mid_bad);

    // stereo: width spreads, width 0 is mono
    memcpy(values, knobsets[1], sizeof values);
    render(voices[2], 2, STEP[40], 0, OP1F_VOICE_FRESH);
    render(voices[2], 2, STEP[40], 100 << 24, OP1F_VOICE_BEGINS);
    int differs = 0; for (int i = 0; i < 10; ++i) { render(voices[2], 2, STEP[40], 100 << 24, 0); for (int k = 0; k < 128; ++k) differs += out[2 * k] != out[2 * k + 1]; }
    CHECK(differs > 0, "width 60: left and right differ");
    memcpy(values, knobsets[0], sizeof values);
    render(voices[2], 2, STEP[40], 0, OP1F_VOICE_FRESH);
    render(voices[2], 2, STEP[40], 100 << 24, OP1F_VOICE_BEGINS);
    differs = 0; for (int i = 0; i < 10; ++i) { render(voices[2], 2, STEP[40], 100 << 24, 0); for (int k = 0; k < 128; ++k) differs += out[2 * k] != out[2 * k + 1]; }
    CHECK(differs == 0, "width 0: mono");

    // release: a let-go note ends, and the voice goes quiet
    memcpy(values, knobsets[1], sizeof values);
    render(voices[3], 3, STEP[60], 0, OP1F_VOICE_FRESH);
    render(voices[3], 3, STEP[60], 127 << 24, OP1F_VOICE_BEGINS | OP1F_VOICE_ENDS);
    int blocks = 0; while (!quiet() && blocks < 2000) { render(voices[3], 3, STEP[60], 127 << 24, 0); ++blocks; }
    CHECK(blocks < 2000 && voices[3]->core.voices[0].active == 0, "short note ends (%d blocks)", blocks);
    printf("release: a short note at release 0 is silent after %d blocks (%.0f ms)\n", blocks, blocks * 128 / 88.2);

    // round robin is shared by the voices: two voices playing the same note take different takes where there are two
    memcpy(values, knobsets[0], sizeof values);
    memset((void*)rr_shared, 0, sizeof rr_shared);
    int zones_seen[2];
    for (int i = 0; i < 2; ++i) { render(voices[4 + i], 4u + (unsigned)i, STEP[60], 0, OP1F_VOICE_FRESH); render(voices[4 + i], 4u + (unsigned)i, STEP[60], 100 << 24, OP1F_VOICE_BEGINS); zones_seen[i] = voices[4 + i]->core.voices[0].zone; }
    int takes = 0; for (unsigned i = 0; i < bank_store.zone_count; ++i) { const ms_zone_t* q = &bank_store.zones[i]; takes += 60 >= q->low && 60 <= q->high && 100 >= q->vel_low && 100 <= q->vel_high; }
    CHECK(takes < 2 || zones_seen[0] != zones_seen[1], "round robin across voices");
    printf("round robin: note 60 has %d take(s), voices 5 and 6 took zones %d and %d\n", takes, zones_seen[0], zones_seen[1]);

    // velocity at the top of the range and at 0
    render(voices[5], 5, STEP[60], 0, OP1F_VOICE_FRESH); render(voices[5], 5, STEP[60], 0x7f000000, OP1F_VOICE_BEGINS);
    CHECK(voices[5]->core.voices[0].velocity == 127, "velocity 127");
    render(voices[5], 5, STEP[60], 0, OP1F_VOICE_FRESH); render(voices[5], 5, STEP[60], 0, OP1F_VOICE_BEGINS);
    CHECK(voices[5]->core.voices[0].velocity == 1, "velocity 0 plays softest");

    // a bad bank is refused
    memory_given = 1; bank_ready = 0; bank_state = BANK_WAITING;
    {   // a copy of the bank with one byte changed
        static char bad_path[512];
        const char* dir = getenv("TMPDIR"); snprintf(bad_path, sizeof bad_path, "%s/felt-bad.msb", dir ? dir : "/tmp");
        FILE* in = fopen(bank_path, "rb"); FILE* o = fopen(bad_path, "wb"); int ch, at = 0;
        while ((ch = fgetc(in)) != EOF) { fputc(at++ == 5000 ? ch ^ 0xff : ch, o); }
        fclose(in); fclose(o); bank_path = bad_path;
    }
    frame(1); frame(0);
    CHECK(!bank_ready && bank_state == BANK_BAD, "a damaged felt.msb is refused");
    {   // the page: shown for a few frames, it paints its scene every frame and draws it every second one
        int canvas_stand_in = 0;
        felt_shown(0, OP1F_SHOWN_UP);
        for (int f = 0; f < 6; ++f) { Op1fFrame fr = {}; fr.bytes = 40; fr.page = &op1f_card_felt; fr.canvas = &canvas_stand_in; felt_frame(&fr); }
        CHECK(blocks_painted == 6, "page painted %d of 6 frames", blocks_painted);
        int lit = 0; for (int i = 0; i < scene::W * scene::H; ++i) lit += page_block[i] != 0;
        CHECK(lit > 10000, "the scene drew %d pixels", lit);
        printf("page: 6 frames painted, scene covers %d pixels\n", lit);
    }
    printf("all %ld checks, %ld failed\n", checks, fails);
    return fails ? 1 : 0;
}
