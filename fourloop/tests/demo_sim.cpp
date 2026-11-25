// Simulates 4loop's page on a computer over a scripted performance, for fourloop/demo.gif.
// Runs the pack's own core, scene and input code on made-up audio, then maps the scene onto the
// picture's parts exactly as fourloop.cpp dress_page does, and writes one line of drawing per frame.
// Text and the loop numbers are left out. tests/demo_draw.py turns the lines into the GIF.
//   g++ -O2 -std=gnu++11 -Isrc tests/demo_sim.cpp -o /tmp/demo_sim && /tmp/demo_sim > /tmp/frames.txt
//   python3 tests/demo_draw.py /tmp/frames.txt /tmp/frames && ffmpeg ... (see demo_draw.py)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fourloop_core.cpp"

enum { RATE = 44100, BLOCK = 64 };
static uint32_t clock_ms;
static uint32_t now_ms(void*) { return clock_ms; }
static void* mem_get(void*, uint32_t, uint32_t want, uint32_t, uint32_t* got) { *got = want; return calloc(1, want); }

static field_plugin fp;
static void ev(int kind, int a, int b = 0) { field_event e = { kind, a, b }; fp_event(&fp, &e); }

// ---------------------------------------------------------------- what is played into 4loop
// Each take gets its own kind of sound, so the four waveforms look different.
static int voice = -1;          // which sound is playing now, -1 none
static double vt;               // seconds into it
static double rnd() { return rand() / (double)RAND_MAX * 2 - 1; }
static void sound(int16_t* l, int16_t* r)
{
    for (int i = 0; i < BLOCK; i++, vt += 1.0 / RATE) {
        double s = 0;
        if (voice == 0) {                                     // drums: kick on the beat, hats between
            double b = fmod(vt, 0.5), h = fmod(vt + 0.25, 0.5);
            s = sin(2 * M_PI * 55 * b * (1 + 3 * exp(-b * 30))) * exp(-b * 9) * 0.9 + rnd() * exp(-h * 60) * 0.35;
            if (fmod(vt, 1.0) > 0.5 && fmod(vt, 1.0) < 0.62) s += rnd() * exp(-(fmod(vt, 1.0) - 0.5) * 25) * 0.5;
        } else if (voice == 1) {                              // bass: four notes, plucked
            static const double n[4] = { 55, 55, 65.4, 49 };
            double b = fmod(vt, 0.5); int k = (int)(vt / 0.5) & 3;
            s = tanh(3 * sin(2 * M_PI * n[k] * vt)) * exp(-b * 4) * 0.6;
        } else if (voice == 2) {                              // pad: slow swell
            double env = 0.5 - 0.5 * cos(2 * M_PI * fmod(vt, 2.0) / 2.0);
            s = (sin(2 * M_PI * 220 * vt) + sin(2 * M_PI * 277 * vt) + sin(2 * M_PI * 330 * vt)) / 3 * env * 0.7;
        } else if (voice == 3) {                              // plucks: an arpeggio, uneven
            static const double n[8] = { 440, 660, 523, 784, 440, 587, 880, 659 };
            double step = 0.125, b = fmod(vt, step); int k = (int)(vt / step) & 7;
            s = sin(2 * M_PI * n[k] * vt) * exp(-b * 18) * (k % 3 == 0 ? 0.8 : 0.4);
        }
        int v = (int)(s * 20000);
        l[i] = r[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

// ---------------------------------------------------------------- the picture's parts, as dress_page sets them
// Colours and sizes from tools/make_svg.py.
static const char* KNOB[4] = { "2E4A7D", "D37E4E", "A2A4A6", "E52817" };
static void tone(const char* c, double share, char* out)
{
    int v[3]; for (int k = 0; k < 3; k++) { char h[3] = { c[2 * k], c[2 * k + 1], 0 }; v[k] = (int)lround(strtol(h, 0, 16) * share); }
    sprintf(out, "%02X%02X%02X", v[0], v[1], v[2]);
}
enum { FILL_STEPS = 5, BARS = 32 };
static FILE* out;
static void circle(const ls_pt& c, int32_t r, const char* fill, double op, const char* stroke, double sw)
{   // a circle node of r 64 scaled by r/64: its stroke scales too
    double s = r / 16.0 / 64.0;
    fprintf(out, "C %.2f %.2f %.2f %s %.2f %s %.2f;", c.x / 16.0, c.y / 16.0, r / 16.0, fill, op, stroke, sw * s);
}
static void line(const ls_pt& a, const ls_pt& b, const char* col, double w)
{ fprintf(out, "L %.2f %.2f %.2f %.2f %s %.2f;", a.x / 16.0, a.y / 16.0, b.x / 16.0, b.y / 16.0, col, w); }
static void tri(const ls_pt& a, const ls_pt& b, const ls_pt& c, const char* col)
{ fprintf(out, "T %.2f %.2f %.2f %.2f %.2f %.2f %s;", a.x / 16.0, a.y / 16.0, b.x / 16.0, b.y / 16.0, c.x / 16.0, c.y / 16.0, col); }

static void draw(const lc_view& v)
{
    const ls_scene* s = &fp.scene;
    for (int i = 0; i < LC_LOOPS; i++) {                      // in the picture's order: loop by loop
        const ls_loop* o = &s->loop[i];
        char idle_fill[8], idle_ring[8], fill[8], off[8];
        tone(KNOB[i], 0.22, idle_fill); tone(KNOB[i], 0.5, idle_ring); tone(KNOB[i], 0.12, off);
        int empty = o->state == LC_EMPTY || (o->state == LC_ARM && o->ring_alpha < 150);
        int lit = o->state != LC_EMPTY && o->state != LC_ARM;
        if (empty) circle(o->centre, o->radius, idle_fill, 0.75, idle_ring, 1.5);
        int t = lit ? (int)(((uint32_t)v.loop[i].gain * FILL_STEPS + 32766u) / 32767u) : 0;
        if (t) { tone(KNOB[i], 0.55 * t / FILL_STEPS, fill); circle(o->centre, o->radius, fill, 0.75, "none", 0); }
        for (int k = 0; k < o->shade_n; k++) tri(o->centre, o->shade[k], o->shade[k + 1], off);
        if (lit || o->state == LC_ARM) circle(o->centre, o->radius, "none", 1, KNOB[i], 3);
        if (o->wave_on) for (int j = 0; j < BARS; j++) line(o->wave[63 - j], o->wave[j], KNOB[i], 2);
        if (o->head_on) line(o->scrub[0], o->scrub[1], KNOB[i], 2);
        for (int j = 0; j < o->tri_count; j++) tri(o->tri[j][0], o->tri[j][1], o->tri[j][2], KNOB[i]);
        for (int j = 0; j < o->bar_count; j++) line(o->bar[j][0], o->bar[j][1], KNOB[i], 2.5);
    }
    for (int i = 0; i < LC_LOOPS; i++) {                      // record discs over every loop
        const ls_loop* o = &s->loop[i];
        if (o->rec_alpha) circle(o->centre, o->rec_radius, "FF0000", 1, "none", 0);
    }
    fprintf(out, "I %d %d", s->sync, s->paused);              // the sync and play icons, top right
    fputc('\n', out);
}

// ---------------------------------------------------------------- the performance
// Times in seconds of simulated play. Between `fast` marks the GIF takes frames further apart.
struct Step { double t; const char* what; int a, b; };
static const Step script[] = {
    { 0.2, "sound", 0 },      { 0.3, "press", 0 },                      // drums: armed, starts on the sound
    { 2.3, "press", 0 },      { 2.35, "sound", -1 },                    // a 2 s loop, now the grid
    { 2.6, "turn", 0, -6 },   { 3.2, "turn", 0, 4 },                    // its level down and back: the disc's tone
    { 3.6, "sound", 1 },      { 3.7, "press", 1 },                      // bass: waits for the grid, then records
    { 6.6, "press", 1 },      { 6.7, "sound", -1 },
    { 7.0, "shift", 1 },      { 7.1, "turn", 1, -1 },  { 7.4, "shift", 0 },   // bass an octave down: bars behind the head
    { 7.8, "sound", 2 },      { 7.9, "press", 2 },                      // pad, 4 s
    { 12.1, "press", 2 },     { 12.2, "sound", -1 },
    { 12.4, "shift", 1 },     { 12.5, "turn", 2, 1 },  { 12.9, "turn", 2, 1 }, { 13.2, "shift", 0 },   // pad up two octaves: three heads
    { 13.6, "sound", 3 },     { 13.7, "press", 3 },                     // plucks
    { 16.1, "press", 3 },     { 16.2, "sound", -1 },
    { 16.6, "shift", 1 },     { 16.7, "press", 3 },  { 16.8, "release", 3 }, { 16.9, "shift", 0 },   // plucks reversed
    { 17.4, "sound", 3 },     { 17.5, "press", 0 },                     // overdub plucks onto the drums
    { 19.5, "press", 0 },     { 19.6, "sound", -1 },
    { 19.9, "size", 1 },      { 20.0, "turn", 1, -1 }, { 20.4, "turn", 1, -1 }, { 20.9, "size", 0 },   // bass to a quarter: the fan
    { 21.4, "turn", 2, -3 },  { 21.8, "turn", 3, -2 },                  // levels
    { 22.4, "sync", 0 },      { 23.2, "sync", 0 },                      // sync off and on: the icon
    { 24.2, "stop", 0 },      { 25.6, "play", 0 },                      // pause, play
    { 26.4, "press", 2 },     { 27.5, "release", 2 },                   // hold: the pad cleared
    { 27.9, "sound", 1 },     { 28.0, "press", 2 },                     // a new take on loop 3
    { 30.4, "press", 2 },     { 30.5, "sound", -1 },
    { 31.0, "end", 0 },
};
// Stretches with little happening (a take running on): the GIF runs four times as fast there.
enum { FPS = 12, FAST = 4 };
static const double fast[][2] = { { 0.9, 2.0 }, { 4.4, 6.4 }, { 8.4, 11.9 }, { 14.2, 15.9 }, { 17.8, 19.3 }, { 24.5, 25.4 }, { 28.4, 30.2 } };

int main()
{
    out = stdout;
    field_host h = {}; h.mem_get = mem_get; h.now_ms = now_ms;
    field_config cfg = {}; cfg.master_slot = 1;
    fp_create(&fp, &h, &cfg);
    srand(3);
    int16_t l[BLOCK], r[BLOCK];
    size_t next = 0; double t = 0, shot = 0; uint32_t last_screen = 0; int frames = 0;
    for (long b = 0; ; b++) {
        t = b * (double)BLOCK / RATE;
        clock_ms = (uint32_t)(t * 1000);
        while (script[next].t <= t) {
            const Step& s = script[next++];
            if (!strcmp(s.what, "end")) { fprintf(stderr, "%d frames\n", frames); return 0; }
            if (!strcmp(s.what, "sound")) { voice = s.a; vt = 0; }
            if (!strcmp(s.what, "press")) ev(FE_ENC_PRESS, s.a);
            if (!strcmp(s.what, "release")) ev(FE_ENC_RELEASE, s.a);
            if (!strcmp(s.what, "turn")) ev(FE_ENC_TURN, s.a, s.b * 1024);
            if (!strcmp(s.what, "shift")) ev(FE_KEY, FK_SHIFT, s.a);
            if (!strcmp(s.what, "size")) ev(FE_KEY, FK_SIZE, s.a);
            if (!strcmp(s.what, "sync")) ev(FE_KEY, FK_SYNC, 1);
            if (!strcmp(s.what, "stop")) ev(FE_KEY, FK_STOP, 1);
            if (!strcmp(s.what, "play")) ev(FE_KEY, FK_PLAY, 1);
        }
        int holding = clock_ms >= 26400 && clock_ms < 27500;  // loop 3 held down to clear it
        for (int k = 0; k < LC_LOOPS; k++)
            if (fp.held[k] == 1 && clock_ms - fp.held_since[k] > 150 && !(k == 2 && holding)) ev(FE_ENC_RELEASE, k);   // the rest are short taps
        sound(l, r);
        fp_audio(&fp, l, r, BLOCK);
        if (clock_ms - last_screen >= 16) {                    // the page at about 60 frames a second
            fp_tick(&fp);
            lc_view v; lc_snapshot(&fp.engine, &v);
            ls_update(&fp.scene, &v, clock_ms - last_screen);
            last_screen = clock_ms;
            double gap = 1.0 / FPS;
            for (auto& f : fast) if (t >= f[0] && t < f[1]) gap = (double)FAST / FPS;
            if (t >= shot) { draw(v); frames++; shot += gap; }
        }
    }
}
