/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3c: the number format of the L/RRY voice and the ONE place the multiplies live. 32-bit (owner decision): no 64-bit multiply in the sample loop, the 64-bit filter path is gone.
 *
 * FORMAT. A signal is an int32 in Q23 (full scale +-8388608, the DAC's 24 bits, 8 bits of headroom in the word). Stored gains and coefficients stay Q28 (a pan, a level, mid / side), the drive gain
 * is Q24, the amp ramp's gain Q15 (L/RRY's). The SVF, LADDER and COMB have their own formats (lrry_voice.c, from the host's variant 4, LRRY/PREC_32_vs_64.md): Q31 coefficients, Q26 states.
 * EVERY product goes through LRRY_MULH below, a 32 x 32 -> high 32 multiply, and one of these wrappers:
 *   MUL_Q28(a, c)   signal x Q28 gain (|c| < 2.0)   = MULH(a, c << 2) x 4        (levels, pans, mid / side)
 *   MAC2_Q28(a, c, b, d)  a c + b d                 = (MULH(a, c << 2) + MULH(b, d << 2)) x 4   (the pan matrix: two products, each cut once)
 *   MUL_Q23(a, b)   Q23 signal x Q23 signal         = MULH(a x 64, b) x 8         (ring; a under 4 FS: a x 64 must fit 32 bits, LRRY_RING_PRE)
 *   MUL_Q24(a, c)   signal x Q24 gain               = MULH(a x 64, c) x 4         (drive, FOLD amount; a under 4 FS: the callers clamp)
 *   MUL_Q15(a, c)   signal x Q15 gain (c < 1.0)     = MULH(a, c << 16) x 2        (the amp ramp)
 * WHAT MULTIPLY-HIGH COSTS ON A 32-BIT CORE IS UNKNOWN until a bench on the unit. The host cannot say (a 64-bit multiply costs the same as a 32-bit one on arm64). On the unit it may be one instruction
 * (a "multiply, keep the high word" such as ARM SMMUL) or the compiler's library call for a 64-bit product (slower than the int64 code it replaces). Nothing here ran on the unit. If the device has a
 * better instruction or the compiler a better intrinsic, change LRRY_MULH in this one place; nothing else in L/RRY writes a (int64_t) product. Each product truncates toward minus infinity (about -0.5 LSB bias).
 * What stays 64-bit on purpose (not a product of two signals, not in PREC_32_vs_64.md's list): the coefficient maths once a block (lrry_voice.c, lrry_tab.h), the source gain ramp's accumulator (adds), the
 * clamp of a filter state (one add), the FM phase and the oscillators' phase / BLEP maths.
 *
 * THE ONE CONVERSION to the firmware's own sample scale (L/RRY's voice scale: Q15, a full-scale oscillator at amp 1.0 is VOICE_FS = 24000): the voice's output is already at that level
 * (lrry_voice.c VGQ = 24000 / 32768 of the Q23 range, the host design's LEVEL PLAN item 2), so the conversion is a shift of 8 bits, in LRRY_TO_FW below. Nothing else in the L/RRY
 * code knows both scales. */
#ifndef LRRY_MAC_H
#define LRRY_MAC_H
#include <stdint.h>

#define LRRY_FS 8388608                 /* Q23 full scale */
#define LRRY_COEF_Q 28

#define LRRY_MULH(a, b) ((int32_t)(((int64_t)(int32_t)(a) * (int64_t)(int32_t)(b)) >> 32))   /* THE one multiply-high */
#define LRRY_RING_PRE 64                /* the ring's pre-multiplier of the first operand (2^6: 4 FS of headroom); the result factor is 2^(32 - 23 - 6) = 8 */
#define MUL_Q28(a, c) (LRRY_MULH((a), (c) * 4) * 4)
#define MAC2_Q28(a, c, b, d) ((LRRY_MULH((a), (c) * 4) + LRRY_MULH((b), (d) * 4)) * 4)
#define MUL_Q23(a, b) (LRRY_MULH((a) * LRRY_RING_PRE, (b)) * 8)
#define MUL_Q24(a, c) (LRRY_MULH((a) * 64, (c)) * 4)
#define MUL_Q15(a, c) (LRRY_MULH((a), (c) * 65536) * 2)

/* Q23 voice signal -> the firmware's Q15 sample scale (and back, for tests) */
#define LRRY_TO_FW(x) ((x) >> 8)
#define LRRY_FROM_FW(x) ((x) * 256)

/* a constant in Q28 / Q24 / Q31 at compile time (initialisers and #defines; no libm, no run-time double) */
#define Q28C(x) ((int32_t)((x) * 268435456.0 + ((x) < 0 ? -0.5 : 0.5)))
#define Q24C(x) ((int32_t)((x) * 16777216.0 + ((x) < 0 ? -0.5 : 0.5)))
#define Q31C(x) ((int32_t)((x) >= 1.0 ? 2147483647.0 : (x) * 2147483648.0 + ((x) < 0 ? -0.5 : 0.5)))
#endif
