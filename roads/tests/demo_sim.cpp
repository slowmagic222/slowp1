// Simulates roads' page on the PC over a scripted performance (no text), writing PPM frames for roads/demo.gif.
// g++ -O2 -std=gnu++11 tests/demo_sim.cpp -o demo_sim && ./demo_sim frames 4
// ffmpeg -framerate 15 -i frames/f%04d.ppm -vf "fps=12,scale=960:440:flags=neighbor,split[a][b];[a]palettegen=max_colors=64:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle" demo.gif
// gifsicle -O3 --lossy=60 demo.gif -o demo.gif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../roads_scene.h"
static uint16_t px[scene::W * scene::H];
static void save(const char* p) {
    FILE* f = fopen(p, "wb"); fprintf(f, "P6\n%d %d\n255\n", scene::W, scene::H);
    for (int i = 0; i < scene::W * scene::H; ++i) { uint16_t c = px[i]; unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
static int white_key(unsigned note) {   // as roads.cpp: C3..C5 on the 15 white keys, wrapped
    static const unsigned char W12[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    int k = (int)((note / 12) * 7 + W12[note % 12]) - 28; while (k < 0) k += 15; while (k >= 15) k -= 15; return k;
}
static int ramp(double s, double a, double b, int from, int to) { return s <= a ? from : s >= b ? to : from + (int)((to - from) * (s - a) / (b - a)); }
struct Voice { unsigned note; int on_until; };
int main(int argc, char** argv) {
    const char* dir = argv[1]; int step = atoi(argv[2]);   // keep every step-th paint frame (60 fps paint)
    scene::State st; memset(&st, 0, sizeof st);
    st.car_dir = 1; st.target = 7;
    Voice v[6] = {}; int vn = 0, pos = 7;
    srand(11);
    static const unsigned SCALE[] = {48, 50, 52, 55, 57, 60, 62, 64, 67, 69, 72, 74, 76};   // pentatonic, C3..E5
    const int T = 60 * 18; int out = 0;
    for (int f = 0; f < T; ++f) {
        double s = f / 60.0;
        st.time += 1; st.angle = scene::swivel(st.time);
        // knobs: the smoke (ambient) and the car's wobble (wow) come up as the town wakes; the style swaps the case
        st.tone = ramp(s, 11, 14, 40, 85); st.sustain = ramp(s, 7, 9, 50, 80);
        st.ambient = ramp(s, 2, 7, 15, 90); st.wow = ramp(s, 4, 8, 20, 75);
        st.style = s < 9.5 ? 0 : s < 13.5 ? 1 : 2;
        st.start = 10; st.loop_in = 40; st.loop_out = 85;
        st.shift = s > 8 && s < 9.2;
        // notes: busy from the first frame, runs up and down with jumps and the odd chord
        double rate = 7.0;
        if ((rand() % 10000) < (int)(rate / 60.0 * 10000)) {
            int r = rand() % 10;
            pos += r < 4 ? 1 : r < 8 ? -1 : (rand() % 9) - 4;
            if (pos < 0) pos = 2;
            if (pos > 12) pos = 10;
            int n = rand() % 6 == 0 ? 2 : 1;
            for (int c = 0; c < n; ++c) {
                Voice& x = v[vn]; vn = (vn + 1) % 6;
                x.note = SCALE[c ? (pos + 2 > 12 ? pos - 2 : pos + 2) : pos]; x.on_until = f + 15 + rand() % 40;
                st.notes++; st.target = white_key(x.note);
            }
        }
        st.keys = 0; st.sounding = 0;
        for (int i = 0; i < 6; ++i) if (v[i].note && f < v[i].on_until) { st.keys |= 1u << white_key(v[i].note); ++st.sounding; }
        scene::step(st);
        if (f % 60 == 0 && getenv("LIFE")) fprintf(stderr, "%d:%d ", f / 60, st.life);
        if (f % 2 == 0) scene::render(px, st);
        if (f % step == 0) { char p[600]; snprintf(p, sizeof p, "%s/f%04d.ppm", dir, out++); save(p); }
    }
    fprintf(stderr, "%d frames, life %d notes %u\n", out, st.life, st.notes);
}
