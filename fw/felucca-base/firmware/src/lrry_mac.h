/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3a: the number format of the L/RRY voice and the ONE place the multiplies live.
 *
 * FORMAT. Inside the voice a signal is an int32 in Q23 (full scale +-8388608, the DAC's 24 bits, 8 bits of headroom in the word); coefficients are Q28 (an a1, a2, a3, k, g, a pan or
 * level gain); the drive gain is Q24; Q15 gains (the amp ramp) come from DUELL's voice.c as they are. Every 32 x 32 product is made in 64 bits and shifted ONCE, here:
 *   MUL_Q28(a, c)   signal x Q28 coefficient           (filters, pans, levels)
 *   MUL_Q23(a, b)   Q23 signal x Q23 signal            (ring modulation, FM index x sine)
 *   MUL_Q24(a, c)   signal x Q24 gain                  (drive)
 *   MUL_Q15(a, c)   signal x Q15 gain                  (the amp ramp, the part's velocity ramp)
 *   MAC2_Q28(a, c, b, d)  a c + b d with one shift     (the two products of an SVF row)
 * To lower the precision later (a 32-bit multiplier on the device is cheaper than a 64-bit one) define LRRY_MUL32 and tune LRRY_PRE_A / LRRY_PRE_C: the operands are shifted right
 * first and the product is made in 32 bits. NOT TUNED and not measured: it is the one edit the brief asked for, the numbers are for the bench to find.
 *
 * THE ONE CONVERSION to the firmware's own sample scale (DUELL's voice scale: Q15, a full-scale oscillator at amp 1.0 is VOICE_FS = 24000): the voice's output is already at that level
 * (lrry_voice.c VGQ = 24000 / 32768 of the Q23 range, the host design's LEVEL PLAN item 2), so the conversion is a shift of 8 bits, in LRRY_TO_FW below. Nothing else in the L/RRY
 * code knows both scales. */
#ifndef LRRY_MAC_H
#define LRRY_MAC_H
#include <stdint.h>

#define LRRY_FS 8388608                 /* Q23 full scale */
#define LRRY_COEF_Q 28

#ifdef LRRY_MUL32
#ifndef LRRY_PRE_A
#define LRRY_PRE_A 10                   /* shift of the signal before a 32-bit product */
#endif
#ifndef LRRY_PRE_C
#define LRRY_PRE_C 10                   /* shift of the coefficient */
#endif
#define LRRY_MUL(a, c, q) ((int32_t)((((int32_t)(a) >> LRRY_PRE_A) * ((int32_t)(c) >> LRRY_PRE_C)) >> ((q) - LRRY_PRE_A - LRRY_PRE_C)))
#define LRRY_MAC2(a, c, b, d, q) (LRRY_MUL(a, c, q) + LRRY_MUL(b, d, q))
#else
#define LRRY_MUL(a, c, q) ((int32_t)(((int64_t)(a) * (int64_t)(c)) >> (q)))
#define LRRY_MAC2(a, c, b, d, q) ((int32_t)(((int64_t)(a) * (int64_t)(c) + (int64_t)(b) * (int64_t)(d)) >> (q)))
#endif
#define MUL_Q28(a, c) LRRY_MUL(a, c, 28)
#define MUL_Q23(a, b) LRRY_MUL(a, b, 23)
#define MUL_Q24(a, c) LRRY_MUL(a, c, 24)
#define MUL_Q15(a, c) LRRY_MUL(a, c, 15)
#define MAC2_Q28(a, c, b, d) LRRY_MAC2(a, c, b, d, 28)

/* Q23 voice signal -> the firmware's Q15 sample scale (and back, for tests) */
#define LRRY_TO_FW(x) ((x) >> 8)
#define LRRY_FROM_FW(x) ((x) * 256)

/* a constant in Q28 / Q24 / Q15 at compile time (initialisers and #defines; no libm, no run-time double) */
#define Q28C(x) ((int32_t)((x) * 268435456.0 + ((x) < 0 ? -0.5 : 0.5)))
#define Q24C(x) ((int32_t)((x) * 16777216.0 + ((x) < 0 ? -0.5 : 0.5)))
#endif
