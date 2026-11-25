// Host test of bows.pack's engine: bows.cpp compiled for the PC (OP1F_HOST, 32-bit) with stand-ins for the base.
// Loads the bank as the frame function does, plays notes through the render with several knob sets, checks
// silence/sound/ending/overflow, and writes the takes as WAV files (out dir) for tests/check.py (pitch, clicks, level).
// usage: host_test <bows.msb> <out dir>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <initializer_list>
#include "../bows.cpp"

extern "C" const Op1fCard op1f_card_bows = {};
static unsigned char memory_block[BANK_ROOM];
static const char* bank_path;
extern "C" void* op1f_memory(const Op1fCard*, unsigned ask) { return ask == MEMORY_ASK ? (void*)memory_block : 0; }
extern "C" int op1f_read(const Op1fCard*, unsigned folder, const char* name, void* buffer, unsigned room) {
    if (folder != 0 || strcmp(name, "bows.msb") || !bank_path) return OP1F_NO_FILE;
    FILE* f = fopen(bank_path, "rb"); if (!f) return OP1F_NO_FILE;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if ((unsigned long)n > room) { fclose(f); return OP1F_TOO_LARGE; }
    size_t got = fread(buffer, 1, (size_t)n, f); fclose(f); return (int)got;
}
extern "C" int op1f_write(const Op1fCard*, unsigned, const char*, const void*, unsigned bytes) { return (int)bytes; }
extern "C" void op1f_log(int, const char*, int, const char* format, ...) { va_list a; va_start(a, format); printf("  log: "); vprintf(format, a); printf("\n"); va_end(a); }
extern "C" int op1f_paint_text(void*, int, int, const char*, unsigned, int) { return 0; }
extern "C" void* op1f_picture(const Op1fCard*, const char*) { return 0; }
extern "C" int op1f_text_set(void*, const char*, const char*, int, unsigned) { return 0; }
extern "C" void* op1f_text_node(void*, const char*) { return 0; }
extern "C" void op1f_show(void*, int) {}
extern "C" void op1f_paint_mask(void*, int, int, int, int, const unsigned char*, int, unsigned) {}
extern "C" int op1f_page_stand_in(const Op1fCard*, unsigned, void*) { return 1; }
extern "C" int op1f_param_heard(const Op1fCard*, unsigned, unsigned, unsigned, int* out) { *out = 0; return 0; }
extern "C" int op1f_param_get(const Op1fCard*, unsigned, unsigned, unsigned, int* out) { *out = 0; return 0; }
extern "C" int op1f_param_set(const Op1fCard*, unsigned, unsigned, unsigned, int) { return 0; }

static long checks = 0, fails = 0;
#define CHECK(c, ...) do { ++checks; if (!(c)) { if (++fails < 20) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static int out[256];
static Voice* voice;
static void render(int* values, unsigned step, int velocity, unsigned events) {
    Op1fVoiceBlock b = {}; b.bytes = 52; b.slot = 0; b.voice = 0; b.frames = 128; b.rate = 88200; b.out = out;
    b.values = values; b.step = step; b.velocity = velocity; b.events = events; b.state = voice;
    bows_render(&b);
}

static void put32(FILE* f, unsigned v) { fwrite(&v, 4, 1, f); }
static void put16(FILE* f, unsigned v) { fwrite(&v, 2, 1, f); }
static void wav(const char* path, const short* s, unsigned frames) {
    FILE* f = fopen(path, "wb"); if (!f) { printf("cannot write %s\n", path); exit(2); }
    fwrite("RIFF", 1, 4, f); put32(f, 36 + frames * 4); fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1); put16(f, 2);
    put32(f, 88200); put32(f, 88200 * 4); put16(f, 4); put16(f, 16); fwrite("data", 1, 4, f); put32(f, frames * 4);
    fwrite(s, 2, frames * 2, f); fclose(f);
}

// plays a note `hold` blocks, lets go, renders `tail` blocks more; returns the blocks until silence (or -1)
enum { MOST = 4000 };
static short take[2 * 128 * MOST];
static int play(const char* dir, const char* name, int* values, int note, int velocity, int hold, int tail, int bend_cents) {
    bows_reset(0, 0, voice);
    unsigned events = OP1F_VOICE_FRESH | OP1F_VOICE_BEGINS;
    int total = hold + tail, quiet_from = -1;
    long peak = 0;
    for (int blk = 0; blk < total; ++blk) {
        unsigned step = STEP[note];
        if (bend_cents && blk > hold / 2) step = (unsigned)((double)step * pow(2.0, bend_cents / 1200.0));
        if (blk == hold) events |= OP1F_VOICE_ENDS;
        int was_dir = ((Voice*)voice)->rec.dir;
        render(values, step, velocity << 24, events);
        if (getenv("TRACE_TURNS") && blk && ((Voice*)voice)->rec.dir != was_dir) printf("  %s turns at %.3f s (frame %u)\n", name, blk * 128 / 88200.0, ((Voice*)voice)->rec.frame);
        events = 0;
        int silent = 1;
        for (int k = 0; k < 256; ++k) {
            int s = out[k] >> 16; if (s) silent = 0;
            long a = s < 0 ? -s : s; if (a > peak) peak = a;
            if (blk < MOST) take[blk * 256 + k] = (short)s;
            CHECK((out[k] & 0xffff) == 0, "out word low bits");
        }
        if (blk >= hold && silent && quiet_from < 0) quiet_from = blk;
        if (!silent) quiet_from = -1;
    }
    char path[512]; snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    wav(path, take, (unsigned)(total < MOST ? total : MOST) * 128u);
    printf("%-22s note %3d vel %3d: peak %6ld, silent %s\n", name, note, velocity, peak, quiet_from >= 0 ? "after release" : "NOT by the end");
    return quiet_from;
}

// v9: the slide: one voice holds `from`, a second begins `to` after `after` blocks; the two mixed. The held one should
// slide and fade, the new one come in on the move
static int legato(const char* dir, const char* name, int* values, int from, int to, int after, int total) {
    Voice* second = (Voice*)aligned_alloc(32, VOICE_BYTES);
    bows_reset(0, 0, voice); bows_reset(0, 1, second);
    static int out2[256];
    long peak = 0; int biggest = 0, prev = 0;
    for (int blk = 0; blk < total; ++blk) {
        Op1fVoiceBlock b = {}; b.bytes = 52; b.slot = 0; b.voice = 0; b.frames = 128; b.rate = 88200; b.out = out;
        b.values = values; b.step = STEP[from]; b.velocity = 100 << 24; b.events = blk == 0 ? (OP1F_VOICE_FRESH | OP1F_VOICE_BEGINS) : 0; b.state = voice;
        if (blk == after + 20) b.events |= OP1F_VOICE_ENDS;        // the first key let go a little after
        bows_render(&b);
        Op1fVoiceBlock c = b; c.voice = 1; c.out = out2; c.state = second; c.step = STEP[to];
        c.events = blk == after ? (OP1F_VOICE_FRESH | OP1F_VOICE_BEGINS) : 0;
        if (blk < after) { for (int k = 0; k < 256; ++k) out2[k] = 0; } else bows_render(&c);
        for (int k = 0; k < 256; ++k) {
            int s = (out[k] >> 17) + (out2[k] >> 17);
            long a = s < 0 ? -s : s; if (a > peak) peak = a;
            if (blk < MOST) take[blk * 256 + k] = (short)s;
            if ((k & 1) == 0) { int d = s - prev; if (d < 0) d = -d; if (blk > 2 && d > biggest) biggest = d; prev = s; }
        }
    }
    char path[512]; snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    wav(path, take, (unsigned)(total < MOST ? total : MOST) * 128u);
    printf("%-22s %d -> %d: peak %6ld, biggest step %d\n", name, from, to, peak, biggest);
    free(second);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: host_test <bows.msb> <out dir>\n"); return 2; }
    bank_path = argv[1];
    const char* dir = argv[2];
    voice = (Voice*)aligned_alloc(32, VOICE_BYTES);
    printf("voice %u bytes of %u\n", (unsigned)sizeof(Voice), (unsigned)VOICE_BYTES);

    // before the bank: silence
    bows_reset(0, 0, voice);
    int plain[8] = {20, 0, 0, 0, 45, 25, 15, 60};
    render(plain, STEP[60], 100 << 24, OP1F_VOICE_FRESH | OP1F_VOICE_BEGINS);
    { int q = 1; for (int k = 0; k < 256; ++k) if (out[k]) q = 0; CHECK(q, "silent before the bank"); }
    Op1fFrame f = {}; f.bytes = 40; bows_frame(&f);
    CHECK(bank_ready != 0, "bank loaded");
    if (!bank_ready) { printf("no bank\n"); return 1; }

    int dry[8] = {0, 0, 0, 0, 30, 0, 0, 60};
    int ens[8] = {20, 0, 2, 0, 30, 25, 15, 100};   // v8: style 3 (two octaves)
    int str[8] = {30, 100, 0, 0, 60, 25, 0, 100};
    int amb[8] = {55, 70, 0, 60, 75, 45, 10, 90};
    int swell[8] = {100, 0, 0, 0, 40, 25, 15, 60};
    const int notes[] = {30, 36, 43, 48, 55, 60, 67, 72, 79, 86, 96};
    char name[64];
    for (int n : notes) { snprintf(name, sizeof name, "dry-%d-loud", n); CHECK(play(dir, name, dry, n, 120, 600, 900, 0) >= 0, "dry %d ends", n); }
    for (int n : {48, 72}) { snprintf(name, sizeof name, "dry-%d-soft", n); play(dir, name, dry, n, 30, 600, 400, 0); }
    for (int n : {48, 72}) { snprintf(name, sizeof name, "plain-%d", n); CHECK(play(dir, name, plain, n, 100, 600, 1200, 0) >= 0, "plain ends"); }
    for (int n : {36, 60, 84}) { snprintf(name, sizeof name, "style3-%d", n); play(dir, name, ens, n, 100, 600, 600, 0); }
    for (int n : {36, 60, 84}) { snprintf(name, sizeof name, "stretch-%d", n); CHECK(play(dir, name, str, n, 100, 1600, 3000, 0) >= 0, "stretch ends"); }
    for (int n : {48, 67}) { snprintf(name, sizeof name, "ambient-%d", n); play(dir, name, amb, n, 90, 1600, 2300, 0); }
    play(dir, "swell-60", swell, 60, 100, 3000, 1000, 0);
    play(dir, "bend-60", plain, 60, 100, 600, 300, 200);
    // every knob at its ends: no overflow traps (UBSan) and the out words stay whole
    int ends[2][8] = {{0, 0, 0, 0, 0, 0, 0, 0}, {100, 100, 100, 100, 100, 100, 100, 100}};
    for (auto& e : ends) for (int n : {0, 21, 60, 108, 127}) { snprintf(name, sizeof name, "ends-%d-%d", e[0], n); play(dir, name, e, n, 127, 300, 3500, 0); }
    for (int n : {36, 48, 60, 72, 84}) { snprintf(name, sizeof name, "hold-%d", n); play(dir, name, dry, n, 100, 3990, 10, 0); snprintf(name, sizeof name, "hold-amb-%d", n); play(dir, name, amb, n, 100, 3990, 10, 0); snprintf(name, sizeof name, "hold-def-%d", n); play(dir, name, plain, n, 100, 3990, 10, 0); }   // v14: long holds, for loop pops
    legato(dir, "slide-60-64", plain, 60, 64, 300, 1700);
    legato(dir, "slide-60-55", plain, 60, 55, 300, 1700);
    legato(dir, "slide-48-60", plain, 48, 60, 300, 1700);
    printf("%ld checks, %ld failed\n", checks, fails);
    return fails ? 1 : 0;
}
