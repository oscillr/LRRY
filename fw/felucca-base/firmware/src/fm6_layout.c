/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2012 Google Inc.
 * Copyright 2016-2025 Pascal Gauthier.
 * C port for Felucca (integer only, no float), 2026: Leo Kuroshita (@kurogedelic), Hügelton Instruments
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* FM6 patch layout (was fm6_core.c, msfa ported to C). L/RRY phase 4.4a removed the synthesis (envelopes, pitch envelope, LFO, note set-up, the operator renderer):
 * the L/RRY voice is the only sound source. What is left is what the FM6 pages, the editor and the stores read: the 155-byte patch layout (FP_*) and msfa's 32 algorithm
 * tables with their carriers (the page's algorithm diagram). This part keeps msfa's licence (Apache-2.0, above; LICENSING.md) */

#define FM6_N CTL
#define FM6_LG_N 5
#if (1 << FM6_LG_N) != FM6_N
#error "FM6_LG_N does not match CTL"
#endif

/* the 155-byte patch: 6 x 21 operator bytes (the sixth operator first), then the voice */
enum {
    FP_R1 = 0, FP_L1 = 4, FP_BP = 8, FP_LD, FP_RD, FP_LC, FP_RC, FP_RS, FP_AMS, FP_KVS, FP_OL, FP_MODE, FP_FC, FP_FF,
    FP_DET, FP_OP = 21,
    FP_PR1 = 126, FP_PL1 = 130, FP_ALG = 134, FP_FB, FP_OKS, FP_LFS, FP_LFD, FP_LPMD, FP_LAMD, FP_LKS, FP_LFW,
    FP_LPMS, FP_TRNSP, FP_NAME, FP_SIZE = 155
};
/* sine of a Q24 phase (one cycle = 2^24), Q15, linear between SINE's 1024 points (its 1025th = the 1st) */
static inline int32_t fm6_sin(int32_t ph)
{
    const int16_t *s = &SINE[((uint32_t)ph >> 14) & 1023u];
    return s[0] + (((s[1] - s[0]) * (int32_t)(ph & 0x3FFF)) >> 14);
}

/* ------------------------------------------------------------ envelope --- */
typedef struct {                 /* msfa Env: level in Q24 log2 (2^24 = 6 dB), 16 more bits than the original */
    int32_t level, target, inc, statc;
    int16_t outlevel;            /* microsteps (99 * 32 = full) */
    uint8_t rscale, ix, rising, down;
} fm6_env_t;

/* ------------------------------------------------------ pitch envelope --- */
typedef struct {
    int32_t level, target, inc;
    uint8_t ix, rising, down, rsv;
} fm6_penv_t;

/* ------------------------------------------------------- the algorithms --- */
enum { FM6_OB1 = 1, FM6_OB2 = 2, FM6_OADD = 4, FM6_IB1 = 16, FM6_IB2 = 32, FM6_FBIN = 64, FM6_FBOUT = 128 };
static const uint8_t FM6_ALG[32][6] = {      /* per operator (the sixth first): in bus, out bus, add, feedback */
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14},
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14},
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04},
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}};

/* bit k: operator k (the sixth first) is a carrier of algorithm a (0..31): it writes the output bus (msfa's
 * isCarrier also counts the modulators that add into bus 1 or 2) */
static uint32_t fm6_carriers(uint32_t a)
{
    uint32_t k, c = 0;
    for (k = 0; k < 6u; k++)
        c |= ((FM6_ALG[a & 31u][k] & 3u) ? 0u : 1u) << k;
    return c;
}

/* ------------------------------------------------------- the operators --- */
typedef struct {                 /* msfa FmOpParams */
    int32_t level_in;            /* Q24 log2 */
    int32_t gain_out;            /* Q24 */
    int32_t freq;                /* Q24 cycles per sample */
    int32_t phase;               /* Q24 */
} fm6_op_t;

#define FM6_THRESH 1120          /* msfa kLevelThresh: an operator below it on both block ends is skipped */

/* one operator over the block: modulated by in (0: none), into out (add: += else =). Four loops, so none
 * tests a flag per sample */
#define FM6_OP_LOOP(PH, STORE)                                                    \
    for (i = 0; i < FM6_N; i++) {                                               \
        g += dg;                                                                \
        out[i] STORE (int32_t)(((int64_t)fm6_sin(PH) * g) >> 15);               \
        phase = (int32_t)((uint32_t)phase + (uint32_t)freq);                    \
    }


/* ------------------------------------------------------------ the note --- */
typedef struct {                 /* a sounding note (msfa's note): 252 bytes */
    fm6_env_t env[6];
    fm6_op_t op[6];
    int32_t base[6];             /* the operator's pitch: ratio: log2 offset from the note's (Q24); fixed: log2 Hz */
    int32_t fb[2];
    fm6_penv_t penv;
    uint8_t fixed;               /* bit k: operator k has a fixed frequency */
    uint8_t live, down, rsv;
    uint8_t ol0[6];              /* each operator's OL at the note-on, or when last followed (fm6_note_ol: the morph moves it live) */
    int32_t dc;                  /* (eng_fm6.c) the output's DC blocker */
} fm6_note_t;
#define FM6_LOGF0 50857777       /* log2 of MIDI note 0's frequency, Q24 */

