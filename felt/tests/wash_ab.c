/* wash_block against wash_tick, the frame-at-a-time wash it replaces: the same samples and state, block for block. */
#include <stdio.h>
#include <stdlib.h>
#define MS_WASH_TICK
#include "../core/ms_core.c"
int main(void) {
    static ms_core_t a, b;
    ms_core_init(&a, 44100); ms_core_init(&b, 44100);
    static int32_t in[MS_MAX_BLOCK], oa[MS_MAX_BLOCK], ob[MS_MAX_BLOCK];
    srand(7);
    long frames = 0, bad = 0;
    int32_t amount = 0;
    for (int blk = 0; blk < 200000; blk++) {
        unsigned n = 1u + (unsigned)(rand() % MS_MAX_BLOCK);
        int r = rand() % 10;
        if (r == 0) amount = 0; else if (r == 1) amount = rand() % 32768; else if (r == 2) amount = 32767; else if (r == 3) amount = rand() % 64;
        int loud = rand() % 3;
        for (unsigned k = 0; k < n; k++) { in[k] = (rand() % 65536 - 32768) * (loud == 2 ? 3 : 1) >> (loud == 0 ? 6 : 0); oa[k] = ob[k] = k; }
        for (int ch = 0; ch < 2; ch++) {
            for (unsigned k = 0; k < n; k++) oa[k] += wash_tick(&a.wash[0][ch], in[k], amount);
            wash_block(&b.wash[0][ch], in, b.wash_x, ob, n, amount);
        }
        for (unsigned k = 0; k < n; k++) bad += oa[k] != ob[k];
        for (int ch = 0; ch < 2; ch++) {
            ms_wash_t *p = &a.wash[0][ch], *q = &b.wash[0][ch];
            if (p->wet != q->wet || p->lp != q->lp || p->active != q->active) bad++;
            for (int i = 0; i < 3; i++) if (p->pos[i] != q->pos[i] || p->filled[i] != q->filled[i]) bad++;
            for (unsigned i = 0; i < sizeof p->data / sizeof p->data[0]; i++) if (p->data[i] != q->data[i]) { bad++; break; }
        }
        frames += n;
    }
    printf("wash: %ld frames of random blocks and amounts, %ld differences from the frame-at-a-time wash\n", frames, bad);
    return bad != 0;
}
