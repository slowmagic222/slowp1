// Renders roads' page scene on the PC into PPM frames (tests/run.sh does not need it; for previews).
#include <stdio.h>
#include <string.h>
#include "../roads_scene.h"
static uint16_t px[scene::W * scene::H];
static void save(const char* path) {
    FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", scene::W, scene::H);
    for (int i = 0; i < scene::W * scene::H; ++i) { uint16_t c = px[i]; unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    int green = argc > 2 ? argv[2][0] == '1' : 0;
    scene::State st; memset(&st, 0, sizeof st);
    st.tone = 30; st.sustain = 55; st.wow = 40; st.ambient = 60; st.sounding = 1; st.green = green;
    st.start = 10; st.loop_in = 50; st.loop_out = 100; st.car_dir = 1;
    char path[512];
    st.target = 3; st.car_x = 0;
    for (int f = 0; f < 8; ++f) {
        st.time = (uint32_t)f * 104u; st.angle = scene::swivel(st.time);   // half a round trip: iso to front
        st.shift = f >= 6;
        st.style = f < 3 ? 0 : (f < 5 ? 1 : 2);
        st.keys = 1u << (f < 4 ? 3 : 11);
        st.target = f < 4 ? 3 : 11;
        for (int k = 0; k < 40; ++k) scene::step(st);
        scene::render(px, st);
        snprintf(path, sizeof path, "%s/frame%d.ppm", dir, f); save(path);
    }
    return 0;
}
