/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* L/RRY phase 4.4a: DESCRIPTOR ONLY. The engine's DSP (note-on, render and their helpers) is removed: the L/RRY voice (lrry_voice.c) is the only sound source. What stays is what the pages,
 * the editor, the stores and budget.c read (names, the EDIT cards, presets, the voice cap and flags, the page graphs' helpers). The description below is of the engine as L/RRY had it */
/* VOICE: formant oscillator, sung vowels from the keyboard. 4 voices.
 * After D. H. Klatt, "Software for a cascade/parallel formant synthesizer",
 * JASA 67 (1980): a glottal source (derivative of a Rosenberg (1971) pulse,
 * no DC) into four two-pole resonators in cascade, F1 -> F2 -> F3 -> F4.
 * Design follows klattsch by Tony Gies (MIT, design reference only, no code
 * copied). Formant data from Klatt (1980) and Hillenbrand et al. (1995).
 *
 *   source  rising half-sine while opening (Tp), falling quarter-sine while
 *           closing (Tn), 0 while closed; the closure step goes through blep()
 *   BUZZ    open quotient 0.9 .. 0.35 and a one-pole spectral tilt
 *   BREATH  noise, louder while the glottis is open, mixed before the
 *           resonators; at the top the voicing fades out (whisper)
 *   F1..F4  y = A x + B y1 + C y2, C = -r^2, B = 2 r cos(2 pi F / FS),
 *           A = 1 - B - C (unity gain at DC), Q30 coefficients per block;
 *           F4 is fixed at 3300 Hz and moves only with SHIFT / key tracking
 *   PRES    out = y + k (y - y[-1]), a first-difference shelf; k follows BUZZ
 *
 * Voice state: ph[0] glottal phase, ph[1..2] F4 y1 / y2, s[0..5] F1..F3
 * y1 / y2, s[6] tilt, s[7] TALK progress (Q24, bits 0..24) and the note's
 * random vowel (bits 25..31). Formant key tracking is fixed at 30 / 127. */
#define FMT_FOLOW 30
#define FMT_TALK_MASK 0x01FFFFFF

/* formants (Hz) of A E I O U: adult male means, Hillenbrand et al., JASA 97
 * (1995) (/a/ hod, /E/ head, /i/ heed, /o/ hoed, /u/ who'd); bandwidths (Hz)
 * in the range of Klatt (1980) and of measured vowel bandwidths */
static const uint16_t VOWEL_F[5][3] = {
    {768, 1333, 2522}, {580, 1799, 2605}, {342, 2322, 3000}, {497, 910, 2459}, {378, 997, 2343},
};
static const uint8_t VOWEL_B[5][3] = {
    {90, 100, 160}, {70, 100, 170}, {50, 120, 200}, {70, 80, 150}, {60, 80, 150},
};
/* 2^(k/12), Q16, k = 0..12 */
static const uint32_t SEMI_Q16[13] = {
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072,
};

/* 2^(st16 / 192) in Q16 (st16 in 1/16 semitone), clamped to 1/16 .. 16.
 * Divisions by constants are written as multiplies: -Os keeps them as divides */
static uint32_t fmt_ratio(int32_t st16)
{
    int32_t o, k;
    uint32_t r;
    st16 = clamp(st16, -4 * 192, 4 * 192 - 1) + 4 * 192;   /* 0 .. 8 octaves */
    o = (st16 * 2731) >> 19;                            /* / 192, exact for 0 .. 1535 */
    k = st16 - o * 192;
    r = SEMI_Q16[k >> 4] + (((SEMI_Q16[(k >> 4) + 1] - SEMI_Q16[k >> 4]) * (uint32_t)(k & 15)) >> 4);
    return o >= 4 ? r << (o - 4) : r >> (4 - o);
}

/* the three formants of vowel position pos (0..127 << 8, A E I O U), Hz * 16 */
static void vowel_at(int32_t pos, int32_t *f, int32_t *b)
{
    int32_t x = (clamp(pos, 0, 127 << 8) * 33027) >> 20, i = x >> 8, fr = x & 255, j;   /* * 4 / 127 */
    if (i >= 4) {
        i = 3;
        fr = 256;
    }
    for (j = 0; j < 3; j++) {
        f[j] = (VOWEL_F[i][j] << 4) + (((VOWEL_F[i + 1][j] - VOWEL_F[i][j]) * fr) >> 4);
        b[j] = (VOWEL_B[i][j] << 4) + (((VOWEL_B[i + 1][j] - VOWEL_B[i][j]) * fr) >> 4);
    }
}

typedef struct { int32_t a, b, c; } fres_t;
/* breath noise: one xorshift per PART (job 3; before, one for all voices of both parts, so two FORMANT parts drew from each other's sequence and the parts' order mattered).
 * A part's generator starts at its first note-on (the event stage, in a fixed order): the FIRST part to play FORMANT starts at the seed the one shared generator always
 * had, so a scene with FORMANT on one part is bit for bit what it was; a second part starts at another seed (its draws are its own from then on). 0 = not started */
#define FORMANT_SEED_A 0x2545F491
#define FORMANT_SEED_B 0x6C8E9CF5
                            /* a part has started its generator (event stage only) */

/* Klatt resonator coefficients (Q30) for F, BW in Hz * 16 */
static void fres_coef(fres_t *r, uint32_t f16, uint32_t bw16)
{
    int32_t s = sine_i(f16 * 3044u);                    /* sin(pi F / FS), Q15 (2^32 / (2 FS) / 16) */
    int32_t x = (int32_t)(bw16 * 4781u);                /* pi BW / FS, Q30 (<= 0.2) */
    int32_t x2 = (int32_t)(((int64_t)x * x) >> 30), x3 = (int32_t)(((int64_t)x2 * x) >> 30);
    int32_t x4 = (int32_t)(((int64_t)x3 * x) >> 30);
    int32_t e = (1 << 30) - x + (x2 >> 1) - (int32_t)(((int64_t)x3 * 178956971) >> 30)   /* exp(-x): x^3 / 6, */
                + (int32_t)(((int64_t)x4 * 44739243) >> 30);                            /* x^4 / 24 */
    int32_t cs = (1 << 30) - 2 * s * s;                 /* cos(2 pi F / FS) = 1 - 2 sin^2, Q30 */
    r->b = (int32_t)(((int64_t)e * cs) >> 29);          /* 2 r cos */
    r->c = -(int32_t)(((int64_t)e * e) >> 30);          /* -r^2 */
    r->a = (1 << 30) - r->b - r->c;
}

static const preset_t FORMANT_PRESETS[] = {
    /* VOWEL VOWL2 TALK SHIFT | BUZZ BREATH Q RAND */
    {"CHOIR AAH", {0, 0, 0, 0, 40, 22, 60, 0}, {85, 90, 115, 95}, 0, 0, FX(0, 70, 15, 90), PAT(5)},
    {"VOX LEAD", {32, 0, 0, 2, 90, 8, 72, 0}, {6, 70, 105, 55}, 0, 1, FX(0, 20, 45, 40), PAT(4)},
    {"WOW BASS", {95, 0, 68, 0, 100, 0, 80, 0}, {0, 70, 70, 30}, 0, 1, FX(10, 0, 10, 10), PAT(8)},
    {"WHISPER", {0, 95, 88, 3, 50, 120, 50, 0}, {50, 90, 110, 90}, 0, 0, FX(0, 40, 30, 70), PAT(5)},
};

static const engine_t ENG_FORMANT = {
    .name = "VOICE",
    .page_title = {"VOWL", "TONE"},
    .edit = {
        {"VOWL 1", F_INT, 0, 127, 0, 0, 0},
        {"VOWL 2", F_INT, 0, 127, 64, 0, 0},
        {"TALK", F_TIME, 0, 127, 0, 0, 0},
        {"SHIFT", F_SEMI, -12, 12, 0, 0, 0},
        {"BUZZ", F_PCT, 0, 127, 64, 0, 0},
        {"BRTH", F_PCT, 0, 127, 10, 0, 0},
        {"Q", F_PCT, 0, 127, 64, 0, 0},
        {"RAND", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = FORMANT_PRESETS,
    .npresets = NELEM(FORMANT_PRESETS),
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_E0, P_E4, P_E5, P_E2},
    .poly = 4,
    .ufilt = 1,                 /* no filter of its own: the multimode filter (L/RRY 0.2.9; voice.c uf_run: CUT RES MODE SLOPE, P_FREEA..D) */
};
