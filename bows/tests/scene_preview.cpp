// Renders bows' page scene on the PC into PPM frames (previews only).
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../bows_scene.h"
static uint16_t px[scene::W * scene::H];
static void save(const char* path) {
    FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", scene::W, scene::H);
    for (int i = 0; i < scene::W * scene::H; ++i) { uint16_t c = px[i]; unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}
int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    uint32_t angle = argc > 2 ? (uint32_t)atoi(argv[2]) : 0;
    const int lives[3] = {0, 560, 1023};
    char path[512];
    for (int f = 0; f < 3; ++f) {
        scene::State st; memset(&st, 0, sizeof st);
        st.angle = f == 1 ? scene::swivel(364) : f == 2 ? scene::swivel(1092) : angle; st.time = 37; st.life = lives[f]; st.lit = f ? 0x5u : 0; st.style = f == 2 ? 1 : 0;
        scene::render(px, st);
        snprintf(path, sizeof path, "%s/frame%d.ppm", dir, f); save(path);
    }
    return 0;
}
