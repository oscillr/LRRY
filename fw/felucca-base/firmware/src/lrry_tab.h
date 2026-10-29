/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3a: coefficient maths WITHOUT libm (host and device compile the same code). The host voice (LRRY/host/lrry_*.h) calls tan, cos, sin, exp, pow once per control tick;
 * here each of them is a table (lrry_tab_data.h, made by tools/gen_lrry_tables.py) with linear interpolation, and the two divides (the SVF's 1 / (1 + g (g + k)), the BLEP's p / inc)
 * are a Newton reciprocal in integers. Accuracy against the libm functions (measured by tests/lrry_fw_test.c section 1, the numbers are in LRRY/PHASE43A_REPORT.md):
 *   lt_g       tan(pi fc / 44100), fc on DUELL's cutoff axis (30 Hz .. 16 kHz, 508 table intervals): relative error < 4e-4 (largest near 16 kHz, where tan bends)
 *   lt_k       the 128 damping values, exact to the table's rounding (1e-8)
 *   lt_pan_lr  the width preserving pan law, 256 intervals: absolute error < 1e-5 of unity
 *   lt_sin     the sine, a quarter-wave table of 1024 intervals mirrored, linear interpolation: the host's 4096 point table (same interpolation) to 1 LSB of Q23
 *   lt_pow2    2^x, 256 intervals of the octave: relative error < 2e-6
 *   lt_inv56   2^56 / x: relative error < 2e-8
 *   lt_softclip  the host's integer softclip, bit for bit (its divisions were shifts)
 * Q formats: see lrry_mac.h. Everything here is a pure function of its arguments (no state, no globals but the read-only tables): any core may call it. */
#ifndef LRRY_TAB_H
#define LRRY_TAB_H
#include "lrry_mac.h"
#include "lrry_tab_data.h"

#ifndef LRRY_BLEP_M
#define LRRY_BLEP_M 8                   /* BLEP kernel width in samples: 8 (default, the step 2b measurement) or 4 (half the table, the cheaper edge) */
#endif
#if LRRY_BLEP_M == 8
#define LT_BLEP LT_BLEP8
#elif LRRY_BLEP_M == 4
#define LT_BLEP LT_BLEP4
#else
#error "LRRY_BLEP_M: 4 or 8 (the tables lrry_tab_data.h holds)"
#endif

static inline int32_t lt_clamp32(int64_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : (int32_t)v; }

/* 2^56 / x for any x > 0 (uint32): Newton on the normalised x, four rounds, 64-bit products only; no divide */
static inline uint64_t lt_inv56(uint32_t x)
{
    uint32_t m, s = 0, y;
    uint64_t e;
    int i;
    if (!x)
        return 0xFFFFFFFFFFFFFFFFull;
    while (!(x & 0x80000000u)) {                       /* (a loop of at most 31: __builtin_clz is not on every target) */
        x <<= 1;
        s++;
    }
    m = x;                                             /* d = m / 2^32 in [0.5, 1) */
    y = (uint32_t)(3031741621u - (uint32_t)(((uint64_t)2021161080u * m) >> 32));   /* 48/17 - 32/17 d, Q30: first guess, 4 bits */
    for (i = 0; i < 4; i++) {
        e = ((uint64_t)m * y) >> 32;                   /* d y, Q30, about 1.0 */
        y = (uint32_t)(((uint64_t)y * ((1ull << 31) - e)) >> 30);
    }
    return s >= 6u ? (uint64_t)y << (s - 6u) : (uint64_t)y >> (6u - s);
}
/* 1 / x for x in Q28 (x >= 1.0): the result in Q28 */
static inline uint32_t lt_recip28(uint32_t x_q28) { return (uint32_t)lt_inv56(x_q28); }

/* g = tan(pi fc / SR), Q28, at the cutoff position pos_q8 (Q8 of DUELL's 0..127 cutoff table index: voice.c's idx = 40 + (uc * 99 >> 6); 0 .. 127 << 8) */
static inline uint32_t lt_g(int32_t pos_q8)
{
    uint32_t j, f, a, b;
    if (pos_q8 < 0)
        pos_q8 = 0;
    if (pos_q8 > (127 << 8))
        pos_q8 = 127 << 8;
    j = (uint32_t)pos_q8 >> 6;
    f = (uint32_t)pos_q8 & 63u;
    a = LT_TAN[j];
    b = LT_TAN[j + 1u];
    return a + (uint32_t)(((uint64_t)(b - a) * f) >> 6);
}
/* damping k = 1 / q for RES 0..127 (2.0 down to 0.05), Q28 */
static inline int32_t lt_k(int32_t res) { return (int32_t)LT_KRES[res < 0 ? 0 : res > 127 ? 127 : res]; }

/* pan -1..+1 as Q15 (-32768..32768): the width preserving law's gains of one mono source, L = gl s, R = gr s (unity at the centre, 1.414 on the panned side), Q28 */
static inline void lt_pan_lr(int32_t pan_q15, int32_t *gl, int32_t *gr)
{
    int32_t u = (pan_q15 < -32768 ? -32768 : pan_q15 > 32768 ? 32768 : pan_q15) + 32768;   /* 0 .. 65536 */
    uint32_t i = (uint32_t)u >> 8, f = (uint32_t)u & 255u;
    int32_t a = LT_PANG[i], b = LT_PANG[i + (i < 256u ? 1u : 0u)], c = LT_PANG[256u - i], d = LT_PANG[256u - i - (i < 256u ? 1u : 0u)];
    *gl = a + (int32_t)(((int64_t)(b - a) * (int32_t)f) >> 8);
    *gr = c + (int32_t)(((int64_t)(d - c) * (int32_t)f) >> 8);
}

/* the sine of a 32-bit phase, Q23, +-(FS - 1): the host's sine_q (4096 points, linear interpolation) from a quarter wave */
static inline int32_t lt_sin_pt(uint32_t n)           /* the table point n, 0 .. 4096 */
{
    uint32_t q = n >> 10, r = n & 1023u;
    int32_t v = (q & 1u) ? LT_SIN[1024u - r] : LT_SIN[r];
    return (q & 2u) ? -v : v;
}
static inline int32_t lt_sin(uint32_t p)
{
    uint32_t i = p >> 20, f = (p >> 4) & 0xFFFFu;
    int32_t a = lt_sin_pt(i), b = lt_sin_pt(i + 1u);
    return a + (int32_t)(((int64_t)(b - a) * f) >> 16);
}

/* tanh(x / FS) * FS: the host's softclip with its two divisions as the shifts they were (4 FS = 2^25, TANH_N = 2^11) */
static inline int32_t lt_softclip(int32_t x)
{
    int64_t a = x < 0 ? -(int64_t)x : x;
    int32_t y;
    uint32_t pos = (uint32_t)(a >> 14);
    if (pos >= 2048u)
        y = LT_TANH[2048];
    else
        y = LT_TANH[pos] + (int32_t)(((int64_t)(LT_TANH[pos + 1u] - LT_TANH[pos]) * (int32_t)(a & 16383)) >> 14);
    return x < 0 ? -y : y;
}

/* 2^x for x in octaves, Q24 (-4 .. +3), result Q28 (256 table steps an octave, linear interpolation inside a step on 16 bits) */
static inline uint32_t lt_pow2(int32_t x_q24)
{
    int32_t e = x_q24 >> 24;
    uint32_t f = (uint32_t)x_q24 & 0xFFFFFFu, j = f >> 16, r = f & 0xFFFFu, a = LT_P2[j], b = LT_P2[j + 1u], v = a + (uint32_t)(((uint64_t)(b - a) * r) >> 16);
    if (e > 3)
        e = 3;
    if (e < -4)
        e = -4;
    return e >= 0 ? v << e : v >> -e;
}
/* a phase increment times a Q28 factor */
static inline uint32_t lt_scale_inc(uint32_t inc, uint32_t f_q28) { return (uint32_t)(((uint64_t)inc * f_q28) >> 28); }
#endif
