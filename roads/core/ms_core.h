/* ms_core: the FELT / TINE multisampler as an engine-agnostic core, for the OP-1 Field.
 * GPL-3.0-or-later with OP-1 firmware permission. Samples: see the bank's SOURCES.md.
 *
 * What it adds to the plain mono sample path:
 *   - banks are data: a bank file (MSB1, see bank-format.md) carries the zones, loops, makeup tables and ADPCM,
 *     so FELT, TINE or any later instrument is a file in user/pf, not a rebuild;
 *   - multitimbral: up to MS_PARTS parts, each with its own bank, knobs and source octave, sharing one voice pool;
 *   - stereo: per-voice pan (round-robin takes left/right, keyboard spread) and a decorrelated stereo wash per part
 *     (the stereo diffuser). Width 0 gives the classic mono sample path exactly;
 *   - velocity: optional, by zone velocity range (layers) and/or a gain amount; 0 = classic behaviour;
 *   - pitch from the note number (semitone table), any output rate, any block size up to MS_MAX_BLOCK;
 *   - its own release when the part is not looping (a bus engine has no host envelope).
 *
 * Rules kept from the classic and from open-field packs: no allocation, no floating point, no file access, no 64-bit
 * multiply and no division in the core (a pack has no copy of the compiler's ___muldi3 or division; ms_udiv is a
 * shift-subtract loop used once a block at most). Threads: ms_note_* may be called from any thread between
 * blocks; the adapter serialises them with render (open-field's engine note() may arrive on the audio thread).
 */
#ifndef MS_CORE_H
#define MS_CORE_H
#include <stdint.h>

#define MS_FRAMES 128u          /* ADPCM block: 128 frames */
#define MS_BYTES 68u            /* 2 predictor + 1 index + 1 pad + 64 code bytes */
#define MS_MAX_ZONES 96u
#ifndef MS_VOICES
#define MS_VOICES 16u
#endif
#ifndef MS_PARTS
#define MS_PARTS 4u
#endif
#define MS_MAX_BLOCK 128u
#define MS_WASH_TAPS 3

/* ---- bank ---- */
typedef struct {
    uint32_t offset, frames;                 /* in the bank's audio, bytes / frames */
    uint32_t loop_start, loop_end, loop_limit;
    uint8_t root, low, high, take;
    uint8_t vel_low, vel_high, loop_enabled, pan;   /* pan 0..255, 128 = centre (a stereo pair = two zones) */
    uint16_t makeup[5];                      /* Q12 by tone knob (classic ms_felt_makeup) */
    uint16_t gain;                           /* Q12, 4096 = unity */
    uint32_t decay;                          /* TINE: natural tail decay a frame at 88.2 kHz, Q30 (0 = none) */
} ms_zone_t;

/* Bank flags (header byte 46). MS_BANK_TINE: the TINE character in place of
 * FELT's: wow and flutter lofi, the fitted natural decay, the ambient soft attack, -6 dB, the TINE loop editor
 * (loop in 16384 = the bank's loop, 350 ms shortest), notes 24..100. Its rates are a frame at 88.2 kHz: run the
 * core at out_rate 88200 for it. */
#define MS_BANK_TINE 1u

typedef struct {
    const uint8_t *audio;                    /* ADPCM blocks, not copied: the file buffer must stay */
    uint32_t audio_bytes, rate;              /* the bank's sample rate */
    uint16_t zone_count, flags;
    ms_zone_t zones[MS_MAX_ZONES];
    const uint8_t *source_makeup;            /* 128 x 3 le16 Q12, or 0 = unity */
    const uint8_t *peaks;                    /* zone_count x 64 le16, for faces, or 0 */
    char name[17];
} ms_bank_t;

enum { MS_BANK_OK = 0, MS_BANK_SHORT = -1, MS_BANK_MAGIC = -2, MS_BANK_CRC = -3, MS_BANK_LAYOUT = -4 };
/* Checks and reads an MSB1 file held whole in memory (from op1f_read into a heap buffer). */
int ms_bank_parse(ms_bank_t *bank, const uint8_t *file, uint32_t bytes);

/* ---- parts ---- */
enum { MS_TONE, MS_RELEASE, MS_LOFI, MS_WASH };   /* the four classic knobs, Q15 0..32767 */
typedef struct {
    const ms_bank_t *bank;                   /* 0 = silent part */
    uint16_t knob[4];                        /* FELT: tone, release (green), lofi, wash */
    uint16_t attack_trim, loop_trim_start, loop_trim_end;   /* classic editor words 5/6/7: 0, 0, 32767 = bank loops */
    uint8_t source_octave;                   /* 0..2: play a recording 0/12/24 semitones up, retuned (classic LOFI octaves) */
    uint8_t damp;                            /* 1: key release fades even when not looping (bus engine) */
    uint16_t width;                          /* Q15 stereo width: 0 = mono classic path */
    uint16_t rr_spread, key_spread;          /* Q15 shares of width: takes left/right, low keys left / high right */
    uint16_t wash_width;                     /* Q15: stereo diffuser amount at full WASH knob */
    uint16_t vel_amount;                     /* Q15: 0 = velocity ignored (classic) */
    uint16_t level;                          /* Q15 part level, 32768 = unity */
    int8_t transpose;
    uint8_t any_velocity;                    /* 1: the zones' velocity ranges ignored, every take in turn (roads' switch) */
    uint8_t rr[128];                         /* round-robin counter per note */
} ms_part_t;

/* ---- voices ---- */
typedef struct {
    uint8_t active, gate, part, note, zone, source_shift, lofi_count, edit_phase, loop_valid, velocity;
    int8_t direction;
    uint32_t frame, frac, rate, amp, release_level, wash_env, noise, age, cache_block;
    uint32_t loop_start, loop_end, target_start, target_end;
    int32_t lp, start_gain, pad_gain, lofi_held, edit_gain, gain_l, gain_r, vel_gain;
    uint32_t natural_level, play_start, phase;   /* TINE */
    int16_t cache[MS_FRAMES + 1];
} ms_voice_t;

typedef struct {
    int16_t data[4127 + 1999 + 733 + 8];
    uint16_t pos[MS_WASH_TAPS], filled[MS_WASH_TAPS];
    const uint16_t *len, *off;
    int32_t wet, lp; uint8_t active;
} ms_wash_t;

typedef struct {
    uint32_t out_rate, release_k, serial;
    uint32_t silent_amp;                      /* a released voice below this amp (Q15) ends: 16 = -66 dB; 0 = classic */
    ms_part_t parts[MS_PARTS];
    ms_voice_t voices[MS_VOICES];
    ms_wash_t wash[MS_PARTS][2];
    int32_t send[MS_MAX_BLOCK];               /* scratch: one part's wash send */
    int32_t wash_x[MS_MAX_BLOCK];             /* scratch: the wash's stages, a block at a time */
    int16_t mono[MS_MAX_BLOCK];               /* scratch: one voice (kept off the stack: a pack render has 1 KiB) */
} ms_core_t;

void ms_core_init(ms_core_t *c, uint32_t out_rate);
void ms_part_defaults(ms_part_t *p, const ms_bank_t *bank);
/* note on/off for one part; velocity 1..127 (0 = note off). Returns the voice used, or -1. */
int ms_note_on(ms_core_t *c, unsigned part, int note, int velocity);
void ms_note_off(ms_core_t *c, unsigned part, int note);
void ms_all_off(ms_core_t *c, unsigned part);
/* Adds `frames` (<= MS_MAX_BLOCK) of stereo into left/right, int32 at 16-bit scale (the adapter shifts to Q31). */
void ms_render(ms_core_t *c, int32_t *left, int32_t *right, unsigned frames);
/* One voice's mono classic path only, into out (frames <= MS_MAX_BLOCK). For the reference test and for a voice engine. */
void ms_voice_render_mono(ms_core_t *c, ms_voice_t *v, int16_t *out, unsigned frames);

/* helpers shared with adapters and tests */
uint32_t ms_udiv(uint32_t num, uint32_t den);
uint32_t ms_rate_for(const ms_core_t *c, const ms_bank_t *b, const ms_zone_t *z, int note);
int ms_select_zone(ms_part_t *p, int note, int velocity);
void ms_decode(const uint8_t *block, int16_t *out);
uint32_t ms_crc32(uint32_t crc, const uint8_t *p, uint32_t n);
uint32_t ms_mul_shift(uint32_t a, uint32_t b, unsigned s);   /* (a * b) >> s without a 64-bit multiply */
/* TINE's sine (Q15, 8192 steps a turn), for the wow and flutter */
int32_t ms_sine(uint32_t index);
#endif
