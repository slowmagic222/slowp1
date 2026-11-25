// Simulates bows' page on the PC over a scripted performance (no text), writing PPM frames for bows/demo.gif.
// g++ -O2 -std=gnu++11 tests/demo_sim.cpp -o demo_sim && ./demo_sim frames 5
// ffmpeg -framerate 12 -i frames/f%04d.ppm -vf "scale=960:440:flags=neighbor,split[a][b];[a]palettegen=max_colors=40:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" demo.gif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../bows_scene.h"
static uint16_t px[scene::W * scene::H];
static void save(const char* p) {
    FILE* f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", scene::W, scene::H);
    for (int i = 0; i < scene::W * scene::H; ++i) { uint16_t c = px[i]; unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
// as bows.cpp: 0 cello, 1 bass, 2 viola, 3 violin
static unsigned instrument_of(unsigned note) { return note <= 35u ? 1u : note <= 52u ? 0u : note <= 64u ? 2u : 3u; }
struct Voice { unsigned note; int until; };
int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "."; int step = argc > 2 ? atoi(argv[2]) : 5;   // render every step-th paint (60 fps)
    scene::State st; memset(&st, 0, sizeof st);
    Voice v[6] = {}; int vn = 0, out = 0;
    srand(11);
    // D dorian across all four registers
    static const unsigned low[] = {26, 28, 31, 33, 35}, cello[] = {38, 41, 43, 45, 48, 50}, viola[] = {53, 55, 57, 60, 62, 64},
                          violin[] = {65, 67, 69, 72, 74, 76, 77, 79, 81};
    const int T = 60 * 22;
    for (int f = 0; f < T; ++f) {
        double s = f / 60.0;
        st.time += 1u; st.angle = scene::swivel(st.time);
        st.style = s < 8 ? 0 : s < 14 ? 1 : s < 19 ? 2 : 0;
        // busy all through: a violin line, with the low strings answering and chords on the style changes
        double rate = s < 1.5 ? 9.0 : 7.0;
        bool chord = (f == 60 * 8 || f == 60 * 14 || f == 60 * 19 || f == 60 * 4 || f == 60 * 11);
        int count = chord ? 4 : ((rand() % 10000) < (int)(rate / 60.0 * 10000) ? 1 : 0);
        for (int k = 0; k < count; ++k) {
            int which = chord ? k : (s < 1.5 ? 3 : rand() % 10 < 4 ? 3 : rand() % 3 == 0 ? 2 : rand() % 2 ? 0 : 1);
            unsigned n = which == 3 ? violin[rand() % 9] : which == 2 ? viola[rand() % 6] : which == 0 ? cello[rand() % 6] : low[rand() % 5];
            v[vn].note = n; v[vn].until = f + (chord ? 70 : 18 + rand() % 50); vn = (vn + 1) % 6; st.notes++;
        }
        st.lit = 0;
        for (int i = 0; i < 6; ++i) if (f < v[i].until) st.lit |= 1u << instrument_of(v[i].note);
        scene::step(st);
        if (f % 2 == 0) scene::render(px, st);
        if (f % step == 0) { char p[600]; snprintf(p, sizeof p, "%s/f%04d.ppm", dir, out++); save(p); }
    }
    fprintf(stderr, "%d frames, life %d, notes %u\n", out, st.life, st.notes);
    return 0;
}
