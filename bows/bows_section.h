// made by tools/section.py
// x, y, z (the body's bottom), u (up the neck), v (across), n (out of the front), bh, bw, neck, kind, wood, bow_at, pin, bow (1 rising right, -1 rising left, 0 none)
static const Inst SECTION[4] = {
    {-354, 113, 60, {0, 0, 16384}, {3126, -16083, 0}, {16083, 3126, 0}, 420, 130, 300, 0, {rgb(0xb8,0x8a,0x52), rgb(0x94,0x64,0x36), rgb(0x66,0x42,0x24), rgb(0x42,0x2a,0x16)}, 200, 60, 1},   // cello
    {57, -368, 60, {0, 0, 16384}, {15491, -5334, 0}, {5334, 15491, 0}, 520, 170, 380, 1, {rgb(0xad,0x89,0x59), rgb(0x8a,0x65,0x3c), rgb(0x5e,0x45,0x2a), rgb(0x3f,0x2c,0x1a)}, 300, 60, -1},   // bass
    {9, 706, 295, {3974, -15895, 0}, {15895, 3974, 0}, {0, 0, 16384}, 370, 108, 260, 0, {rgb(0xbc,0x84,0x4e), rgb(0x9a,0x5e,0x32), rgb(0x6a,0x3c,0x20), rgb(0x46,0x26,0x14)}, 120, 0, 0},   // viola
    {771, 18, 137, {-16303, 1630, 0}, {-1630, -16303, 0}, {0, 0, 16384}, 325, 94, 228, 0, {rgb(0xc2,0x80,0x4a), rgb(0xa0,0x58,0x2e), rgb(0x70,0x38,0x1c), rgb(0x4a,0x24,0x12)}, 60, 0, 0},   // violin
};
// music stands: x, y, the way the desk faces (Q14)
static const int STANDS[4][4] = {
    {-120, 79, -16207, 2401},
    {38, -93, 1090, -16348},
    {48, 205, 2145, 16243},
    {178, 52, 16377, -482},
};
