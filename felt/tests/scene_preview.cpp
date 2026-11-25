// Renders felt's page scene on the PC into PPM frames (tests/run.sh does not need it; for previews).
#include <stdio.h>
#include <string.h>
#include "../felt_scene.h"
static uint16_t px[scene::W * scene::H];
static void save(const char* path) {
    FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", scene::W, scene::H);
    for (int i = 0; i < scene::W * scene::H; ++i) { uint16_t c = px[i]; unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    scene::State st; memset(&st, 0, sizeof st);
    st.tone = 73; st.release = 40; st.lofi = 20; st.wash = 100; st.stretch = 1; st.sounding = 1;
    st.keys = (1u << 7) | (1u << 9) | (1u << 11);
    st.pulse[1] = 200; st.pulse[4] = 200; st.pulse[9] = 200;
    char path[512];
    st.start = 10; st.loop_in = 40; st.loop_out = 85; st.width = 50;
    for (int f = 0; f < 8; ++f) {
        st.time = (uint32_t)f * 104u; st.angle = scene::swivel(st.time);   // half a round trip: iso to front
        st.shift = f >= 6;
        st.stretch = f < 3 ? 0 : (f < 5 ? 1 : 2); st.lofi = 0;
        st.life = f % 3 == 0 ? 0 : f % 3 == 1 ? 140 : 256;   // asleep, waking, alive
        scene::render(px, st);
        snprintf(path, sizeof path, "%s/frame%d.ppm", dir, f); save(path);
    }
    return 0;
}
