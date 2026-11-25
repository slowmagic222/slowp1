/* felt.pack: the runtime helpers GCC may call on its own (a pack links no libgcc and no C library): memcpy, memmove
 * and memset for a clear or a copy, and division and 64-bit helpers in case the compiler turns a loop into one.
 * The FELT core itself divides nothing by a variable and multiplies nothing in 64 bits. Taken unchanged from 4loop's
 * fourloop_rt.cpp, which builds and runs on the unit. Each is written with 32-bit operations only.
 * GPL-3.0-or-later + OP-1 firmware permission. */
#include <stdint.h>
#include <stddef.h>

#ifdef FL_RT_TEST
#define RT(name) t_##name
#else
#define RT(name) name
#endif

typedef union { uint64_t v; struct { uint32_t lo, hi; } w; } u64w;   /* the unit and the host are little-endian */
typedef union { int64_t v; struct { uint32_t lo, hi; } w; } s64w;

extern "C" {
uint32_t RT(__udivsi3)(uint32_t n, uint32_t d);
uint32_t RT(__umodsi3)(uint32_t n, uint32_t d);
int32_t  RT(__divsi3)(int32_t n, int32_t d);
int32_t  RT(__modsi3)(int32_t n, int32_t d);
uint64_t RT(__udivdi3)(uint64_t n, uint64_t d);
uint64_t RT(__umoddi3)(uint64_t n, uint64_t d);
int64_t  RT(__divdi3)(int64_t n, int64_t d);
int64_t  RT(__moddi3)(int64_t n, int64_t d);
int64_t  RT(__muldi3)(int64_t a, int64_t b);
int64_t  RT(__ashldi3)(int64_t a, int b);
int64_t  RT(__ashrdi3)(int64_t a, int b);
uint64_t RT(__lshrdi3)(uint64_t a, int b);
void    *RT(memcpy)(void *dst, const void *src, size_t n);
void    *RT(memmove)(void *dst, const void *src, size_t n);
void    *RT(memset)(void *dst, int c, size_t n);
}

/* ---- 32-bit ---- */

static uint32_t udivmod32(uint32_t n, uint32_t d, uint32_t *rem)
{
    if (!d) { *rem = 0; return 0; }                    /* never hang the audio thread on a bad divisor */
    uint32_t q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= 1u << i; }
    }
    *rem = r;
    return q;
}

uint32_t RT(__udivsi3)(uint32_t n, uint32_t d) { uint32_t r; return udivmod32(n, d, &r); }
uint32_t RT(__umodsi3)(uint32_t n, uint32_t d) { uint32_t r; udivmod32(n, d, &r); return r; }

int32_t RT(__divsi3)(int32_t n, int32_t d)
{
    uint32_t un = n < 0 ? 0u - (uint32_t)n : (uint32_t)n, ud = d < 0 ? 0u - (uint32_t)d : (uint32_t)d, r;
    uint32_t q = udivmod32(un, ud, &r);
    return (int32_t)(((n < 0) != (d < 0)) ? 0u - q : q);
}

int32_t RT(__modsi3)(int32_t n, int32_t d)
{
    uint32_t un = n < 0 ? 0u - (uint32_t)n : (uint32_t)n, ud = d < 0 ? 0u - (uint32_t)d : (uint32_t)d, r;
    udivmod32(un, ud, &r);
    return (int32_t)(n < 0 ? 0u - r : r);               /* the remainder takes the dividend's sign */
}

/* ---- 64-bit, on words ---- */

static void shl1(uint32_t *lo, uint32_t *hi, uint32_t in) { *hi = (*hi << 1) | (*lo >> 31); *lo = (*lo << 1) | in; }
static int ge(uint32_t alo, uint32_t ahi, uint32_t blo, uint32_t bhi) { return ahi != bhi ? ahi > bhi : alo >= blo; }
static void sub(uint32_t *alo, uint32_t *ahi, uint32_t blo, uint32_t bhi) { uint32_t borrow = *alo < blo; *alo -= blo; *ahi -= bhi + borrow; }
static void neg(uint32_t *lo, uint32_t *hi) { *lo = ~*lo; *hi = ~*hi; if (++*lo == 0) ++*hi; }

static void udivmod64(u64w n, u64w d, u64w *q, u64w *r)
{
    q->w.lo = q->w.hi = r->w.lo = r->w.hi = 0;
    if (!d.w.lo && !d.w.hi) return;
    for (int i = 63; i >= 0; i--) {
        uint32_t bit = i >= 32 ? (n.w.hi >> (i - 32)) & 1u : (n.w.lo >> i) & 1u;
        shl1(&r->w.lo, &r->w.hi, bit);
        if (ge(r->w.lo, r->w.hi, d.w.lo, d.w.hi)) {
            sub(&r->w.lo, &r->w.hi, d.w.lo, d.w.hi);
            if (i >= 32) q->w.hi |= 1u << (i - 32); else q->w.lo |= 1u << i;
        }
    }
}

uint64_t RT(__udivdi3)(uint64_t n, uint64_t d) { u64w a, b, q, r; a.v = n; b.v = d; udivmod64(a, b, &q, &r); return q.v; }
uint64_t RT(__umoddi3)(uint64_t n, uint64_t d) { u64w a, b, q, r; a.v = n; b.v = d; udivmod64(a, b, &q, &r); return r.v; }

static int abs64(u64w *x) { if (x->w.hi >> 31) { neg(&x->w.lo, &x->w.hi); return 1; } return 0; }

int64_t RT(__divdi3)(int64_t n, int64_t d)
{
    u64w a, b, q, r; s64w out; a.v = (uint64_t)n; b.v = (uint64_t)d;
    int sn = abs64(&a), sd = abs64(&b);
    udivmod64(a, b, &q, &r);
    if (sn != sd) neg(&q.w.lo, &q.w.hi);
    out.w.lo = q.w.lo; out.w.hi = q.w.hi; return out.v;
}

int64_t RT(__moddi3)(int64_t n, int64_t d)
{
    u64w a, b, q, r; s64w out; a.v = (uint64_t)n; b.v = (uint64_t)d;
    int sn = abs64(&a); abs64(&b);
    udivmod64(a, b, &q, &r);
    if (sn) neg(&r.w.lo, &r.w.hi);
    out.w.lo = r.w.lo; out.w.hi = r.w.hi; return out.v;
}

static void mul32x32(uint32_t x, uint32_t y, uint32_t *lo, uint32_t *hi)   /* 16-bit halves: no product over 32 bits */
{
    uint32_t x0 = x & 0xffffu, x1 = x >> 16, y0 = y & 0xffffu, y1 = y >> 16;
    uint32_t p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
    uint32_t mid = (p00 >> 16) + (p01 & 0xffffu) + (p10 & 0xffffu);
    *lo = (p00 & 0xffffu) | (mid << 16);
    *hi = p11 + (p01 >> 16) + (p10 >> 16) + (mid >> 16);
}

int64_t RT(__muldi3)(int64_t a, int64_t b)
{
    s64w x, y, out; x.v = a; y.v = b;
    uint32_t lo, hi;
    mul32x32(x.w.lo, y.w.lo, &lo, &hi);
    hi += x.w.lo * y.w.hi + x.w.hi * y.w.lo;              /* the rest only reaches the high word (mod 2^64) */
    out.w.lo = lo; out.w.hi = hi; return out.v;
}

int64_t RT(__ashldi3)(int64_t a, int b)
{
    s64w x; x.v = a; b &= 63;
    if (b >= 32) { x.w.hi = x.w.lo << (b - 32); x.w.lo = 0; }
    else if (b) { x.w.hi = (x.w.hi << b) | (x.w.lo >> (32 - b)); x.w.lo <<= b; }
    return x.v;
}

uint64_t RT(__lshrdi3)(uint64_t a, int b)
{
    u64w x; x.v = a; b &= 63;
    if (b >= 32) { x.w.lo = x.w.hi >> (b - 32); x.w.hi = 0; }
    else if (b) { x.w.lo = (x.w.lo >> b) | (x.w.hi << (32 - b)); x.w.hi >>= b; }
    return x.v;
}

int64_t RT(__ashrdi3)(int64_t a, int b)
{
    s64w x; x.v = a; b &= 63;
    uint32_t fill = (x.w.hi >> 31) ? 0xffffffffu : 0u;
    if (b >= 32) { x.w.lo = (uint32_t)((int32_t)x.w.hi >> (b - 32)); x.w.hi = fill; }
    else if (b) { x.w.lo = (x.w.lo >> b) | (x.w.hi << (32 - b)); x.w.hi = (uint32_t)((int32_t)x.w.hi >> b); }
    return x.v;
}

/* ---- memory ---- byte loops, volatile so the compiler cannot turn them back into calls to themselves. */

void *RT(memcpy)(void *dst, const void *src, size_t n)
{
    volatile uint8_t *d = (volatile uint8_t *)dst; const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *RT(memmove)(void *dst, const void *src, size_t n)
{
    volatile uint8_t *d = (volatile uint8_t *)dst; const uint8_t *s = (const uint8_t *)src;
    if (d < s) while (n--) *d++ = *s++;
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

void *RT(memset)(void *dst, int c, size_t n)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}
