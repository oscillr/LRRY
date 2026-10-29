/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* L/RRY phase 4.4a: DESCRIPTOR ONLY. The engine's DSP (note-on, render and their helpers) is removed: the L/RRY voice (lrry_voice.c) is the only sound source. What stays is what the pages,
 * the editor, the stores and budget.c read (names, the EDIT cards, presets, the voice cap and flags, the page graphs' helpers). The description below is of the engine as DUELL had it */
/* Phase distortion. The waveforms are a C port of the oscillator of
 * CrispyZebra (Leo Kuroshita, GPL-3.0, github.com/hugelton/CrispyZebra), the
 * author's own phase-distortion core: a -cos table read through a bent phase whose
 * bend is DCW. WAVE2 alternates with WAVE every other cycle. A second
 * line (DTN) can be mixed or ring-modulated (its own SUB, a sine an octave down, went in cloud job J: the global SUB LAYER, chain_sub.c, replaces it).
 * Envelopes are Felucca's own (ADSR, ENV -> DCW).
 *
 * The render is built for a core with no branch prediction (pi32v2): per voice and block the bends are set up once
 * (pd_setup: break points and Q16 slopes; a division only where a slope needs one), each line is then rendered into a
 * block buffer by a loop with no call, no switch and no data-dependent branch (a sample's segment is counted from three
 * compares), and one more loop mixes the lines and adds the amplitude. The numbers are the same as the
 * one-sample-at-a-time original's (tests/regress.c's goldens). */
static const char *const N_PD_WAVE[] = {"SAW", "SQUARE", "PULSE", "DOUBLE\nSINE", "SAW\nPULSE", "RESONANT\nSAW", "RESONANT\nTRIANGLE", "RESONANT\nTRAPEZOID"};
static const char *const N_PD_WAVE2[] = {"OFF", "SAW", "SQUARE", "PULSE", "DOUBLE\nSINE", "SAW\nPULSE", "RESONANT\nSAW", "RESONANT\nTRIANGLE", "RESONANT\nTRAPEZOID"};
static const char *const N_PD_LINE[] = {"MIX", "RING"};

/* -cos(2 pi a16 / 65536), Q15, from a 16-bit phase: the table (1024 points, a 1/4 turn in: sine_i of (a16 << 16) +
 * 0x40000000) read at the point (a16 + 0x4000) >> 6 and interpolated by the 6 bits below it (SINE[1024] = SINE[0] = 0) */
static inline int32_t pd_cos(uint32_t a16)
{
    uint32_t t = a16 + 0x4000u, i = (t >> 6) & 1023u;
    int32_t a = SINE[i], b = SINE[i + 1u];
    return -(a + (((b - a) * (int32_t)(t & 63u)) >> 6));
}

/* one wave's bend for a block. The bent phase of a 16-bit phase p is piecewise linear: up to four segments, told apart by
 * three break points b[] (a phase p is in segment (p >= b[0]) + (p >= b[1]) + (p >= b[2]); unused ones sit at 65536,
 * never reached), and in a segment s: off + ((p - st) * k >> 16), k the Q16 slope (0: a flat piece). A division only
 * for a slope of the block (the bent phase comes out the same or 1 lower in 65536 than a division per sample) */
typedef struct {
    uint32_t st, off, k, pad;
} pds_t;
typedef struct {
    uint32_t b[3];
    uint32_t w, rez, pad;                                    /* the wave; RESONANCE waves: the core's pitch rez */
    pds_t s[4];
} pdw_t;

static inline uint32_t pd_slope(uint32_t span, uint32_t len) { return (span << 16) / (len ? len : 1u); }
static inline void pd_seg(pdw_t *b, uint32_t i, uint32_t st, uint32_t off, uint32_t k)
{
    b->s[i].st = st;
    b->s[i].off = off;
    b->s[i].k = k;
}

static void pd_setup(pdw_t *b, uint32_t w, uint32_t dcw)
{
    uint32_t x, pk;
    b->w = w;
    b->b[0] = b->b[1] = b->b[2] = 65536u;
    switch (w) {
    case 0:                                                  /* SAW */
        x = 32768u + ((dcw * 32767u) >> 16);
        b->b[0] = x;
        pd_seg(b, 0, 0, 0, pd_slope(32768u, x));
        pd_seg(b, 1, x, 32768u, pd_slope(32767u, 65535u - x));
        break;
    case 1:                                                  /* SQUARE */
        x = 32768u - ((dcw * 31120u) >> 16);
        b->b[0] = x;
        b->b[1] = 32768u;
        b->b[2] = 32768u + x;
        pd_seg(b, 0, 0, 0, pd_slope(32768u, x));
        pd_seg(b, 1, 0, 32768u, 0);
        pd_seg(b, 2, 32768u, 32768u, pd_slope(32767u, x));
        pd_seg(b, 3, 0, 65535u, 0);
        break;
    case 2:                                                  /* PULSE */
        x = 65535u - ((dcw * 63487u) >> 16);
        b->b[0] = x;
        pd_seg(b, 0, 0, 0, pd_slope(65535u, x));
        pd_seg(b, 1, 0, 65535u, 0);
        break;
    case 3:                                                  /* DOUBLE SINE */
        x = 65535u - ((dcw * 49151u) >> 16);
        b->b[0] = x;
        pd_seg(b, 0, 0, 0, pd_slope(65535u, x));
        pd_seg(b, 1, x, 0, pd_slope(65535u, 65535u - x));
        break;
    case 4:                                                  /* SAW-PULSE */
        x = 65535u - ((dcw * 32767u) >> 16);
        pk = (x * (32768u + ((dcw * 29491u) >> 16))) >> 16;
        if (!pk)
            pk = 1;
        b->b[0] = pk;
        b->b[1] = x;
        pd_seg(b, 0, 0, 0, pd_slope(32768u, pk));
        pd_seg(b, 1, pk, 32768u, pd_slope(32767u, x - pk));
        pd_seg(b, 2, 0, 65535u, 0);
        break;
    default:                                                 /* RESONANCE */
        b->rez = (dcw * 44000u) >> 16;
        break;
    }
}
static inline uint32_t pd_bend(const pdw_t *b, uint32_t p)
{
    const pds_t *s = &b->s[(p >= b->b[0]) + (p >= b->b[1]) + (p >= b->b[2])];
    return s->off + (((p - s->st) * s->k) >> 16);
}

/* RESONANCE: a windowed sine, rez = the pitch of the core; the window by wave (5 saw, 6 triangle, 7 trapezoid) */
static inline uint32_t pd_win(uint32_t w, uint32_t p)       /* (selects, no branches: all three are cheap) */
{
    uint32_t a = 65535u - p, m = p < a ? p : a, c = p << 2, r;
    c = p > 49152u ? a << 2 : c < 65535u ? c : 65535u;
    r = w == 6u ? m << 1 : c;
    return w == 5u ? a : r;
}
static inline int32_t pd_rez(const pdw_t *b, uint32_t p, uint32_t win)
{
    int32_t core = (pd_cos((p + ((p * b->rez) >> 16) * 7u) & 0xFFFFu) * 5) >> 3;
    return (core * (int32_t)win) >> 16;                      /* |core| <= 20479: fits 32 bits */
}

/* one sample at phase p (16 bit): bipolar Q15 (any wave: the generic one, used where the loops below do not apply) */
static inline __attribute__((always_inline)) int32_t pd_wave_i(const pdw_t *b, uint32_t p)
{
    if (b->w < 5u)
        return pd_cos(pd_bend(b, p));
    return pd_rez(b, p, pd_win(b->w, p));
}
static int32_t pd_wave(const pdw_t *b, uint32_t p) { return pd_wave_i(b, p); }
#ifndef PD_BRIDGE_MIN
#define PD_BRIDGE_MIN 128                                    /* of DCW's 0..56000: the attack of #31 moves it by 1,700 a block */
#endif


/* One line (a phase accumulator ph, its step inc, the toggle tg that picks wp[0] / wp[1] every other cycle) into dst[n].
 * Branch-free per sample: the toggle flips when the phase wraps. Four loops by the kind of the two waves */
#define PD_LINE_BEGIN                                                                                              \
    uint32_t ph = *php, tg = *tgp, i;                                                                              \
    for (i = 0; i < n; i++) {                                                                                      \
        const pdw_t *w = wp[tg];                                                                                   \
        uint32_t p = ph >> 16, old = ph;
#define PD_LINE_END                                                                                                \
        ph += inc;                                                                                                 \
        tg ^= ph < old;                                                                                            \
    }                                                                                                              \
    *php = ph;                                                                                                     \
    *tgp = tg;


#define PD_LINE_REZ(NAME, WIN)                                                                                     \
    static void NAME(const pdw_t *const *wp, uint32_t *php, uint32_t inc, uint32_t *tgp, int32_t *dst, uint32_t n)  \
    {                                                                                                              \
        PD_LINE_BEGIN                                                                                              \
        dst[i] = pd_rez(w, p, WIN);                                                                                \
        PD_LINE_END                                                                                                \
    }
PD_LINE_REZ(pd_line_r5, 65535u - p)
PD_LINE_REZ(pd_line_r6, (p < 32768u ? p : 65535u - p) << 1)
PD_LINE_REZ(pd_line_r7, p > 49152u ? (65535u - p) << 2 : p < 16384u ? p << 2 : 65535u)
PD_LINE_REZ(pd_line_rx, pd_win(w->w, p))                    /* two RESONANCE waves with windows of their own */

static void pd_line_gen(const pdw_t *const *wp, uint32_t *php, uint32_t inc, uint32_t *tgp, int32_t *dst, uint32_t n)
{
    PD_LINE_BEGIN
    dst[i] = pd_wave_i(w, p);                                /* one bent, one RESONANCE */
    PD_LINE_END
}

/* the lines (s1, s2) mixed (L 1) or ring-modulated (L 2) or line 1 alone (L 0), and the amplitude ramp (dsp.c amp_at, voice_amp, by adding the step instead of multiplying) */
#define PD_MIX(L)                                                                                                  \
    for (i = 0; i < n; i++) {                                                                                      \
        int32_t s = s1[i], amp;                                                                                    \
        if (L == 1)                                                                                                \
            s = (s + s2[i]) >> 1;                         /* both within +-32767 */                                \
        if (L == 2)                                                                                                \
            s = mulq15(s, s2[i]);                                                                                  \
        amp = m->amp0 + ((x + ((x >> 31) & (CTL - 1))) >> CTL_LOG2);                                              \
        x = (int32_t)((uint32_t)x + (uint32_t)d);                                                                  \
        out[i] += mulq15(mulq15(s, amp), VOICE_FS) << 1;                                                           \
    }


static const preset_t PHASE_PRESETS[] = {
    /* name, {WAVE, WAVE2, DCW, ENV, DTN, LINE, -, -}, {A D S R}, fenv, mono (E7 was the engine's own SUB until cloud job J) */
    {"BRASS", {0, 0, 30, 90, 0, 0, 0, 0}, {8, 70, 90, 40}, 0, 0, FX(0, 20, 20, 40), PAT(6)},
    {"ORGAN", {3, 0, 40, 0, 0, 0, 0, 0}, {0, 127, 127, 30}, 0, 0, FX(0, 40, 0, 30), PAT(6)},
    {"STRING", {0, 4, 50, 40, 12, 0, 0, 0}, {40, 90, 100, 70}, 0, 0, FX(0, 50, 20, 60), PAT(5)},
    {"RESO", {5, 0, 60, 60, 0, 0, 0, 0}, {0, 70, 30, 60}, 0, 0, FX(0, 0, 40, 40), PAT(1)},
    {"BELL", {6, 0, 80, 50, 0, 0, 0, 0}, {0, 95, 0, 90}, 0, 0, FX(0, 0, 30, 70), PAT(7)},
    {"WIRE", {4, 7, 70, 40, 7, 0, 0, 0}, {10, 80, 80, 60}, 0, 0, FX(15, 30, 30, 40), PAT(4)},
};

static const engine_t ENG_PHASE = {
    .name = "PHASE",
    .page_title = {"PHS", "LINE"},
    .edit = {
        {"WAVE 1", F_ENUM, 0, 7, 0, N_PD_WAVE, 0},
        {"WAVE 2", F_ENUM, 0, 8, 0, N_PD_WAVE2, 0},
        {"DCW", F_PCT, 0, 127, 60, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"DTN", F_INT, 0, 127, 0, 0, "ct"},
        {"LINE", F_ENUM, 0, 1, 0, N_PD_LINE, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was SUB: the global SUB LAYER (chain_sub.c) replaces it */
        {"-", F_INT, 0, 0, 0, 0, 0},
    },
    .presets = PHASE_PRESETS,
    .npresets = NELEM(PHASE_PRESETS),
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_E2, P_E3, P_E4, P_REL},
    .ufilt = 1,                 /* no filter of its own: the multimode filter (DUELL 0.2.9; voice.c uf_run: CUT RES MODE SLOPE, P_FREEA..D) */
};
