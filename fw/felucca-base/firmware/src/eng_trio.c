/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* L/RRY phase 4.4a: DESCRIPTOR ONLY. The engine's DSP (note-on, render and their helpers) is removed: the L/RRY voice (lrry_voice.c) is the only sound source. What stays is what the pages,
 * the editor, the stores and budget.c read (names, the EDIT cards, presets, the voice cap and flags, the page graphs' helpers). The description below is of the engine as DUELL had it */
/* TRIO: three oscillators with ring modulation and hard sync, in the style of the sound chips of early 8-bit home computers. Felucca's own design.
 * No filter of its own since the engine filters left (TWIN_FILTER_DESIGN_2, cloud job J): its filter lives on as filter 2's LP, BP, HP and NOTCH (chain_f2.c f2_svf: the same SVF, the
 * same knees and damping, the slow drift from a generator of its own); filter 1 is the MULTI FILTER (voice.c uf_run).
 *
 * Oscillators: triangle, saw, pulse (PW), pitched noise (a new random value 16 times a cycle)
 * and two combined waves (saw AND triangle, pulse AND saw: the bitwise AND of the 16-bit
 * waves, as such chips mix two waveforms). WAVE picks one of 16 sets: the three waves, their
 * levels and the interactions:
 *   RING  osc 1's output flips sign with osc 3's top phase bit (the XOR ring of the triangle;
 *         osc 3 may be silent and only modulate),
 *   SYNC  osc 2 restarts whenever osc 1 starts a cycle.
 * EDIT: WAVE, INT2 / INT3 (osc 2 / osc 3 intervals, semitones), DTN (osc 2 up, osc 3
 * down, cents), PW. The track's ENV / LFO -> SHP move PW.
 *
 * Band limiting: every step of a saw or a pulse, the sync restarts of osc 2 and the ring
 * flips of osc 1 get a polyBLEP correction. The naive waves are computed and each step adds
 * its residual to the sample before it and the one after it (the voice runs one sample late
 * for that). Triangle and noise have no step correction; the combined waves alias like the
 * originals.
 */
enum { TW_OFF, TW_TRI, TW_SAW, TW_PLS, TW_NOI, TW_ST, TW_PS };
#define TRIO_RING 1u
#define TRIO_SYNC 2u
#define TRIO_G3 11000            /* osc level with three oscillators sounding */
#define TRIO_G2 15000            /* two */
#define TRIO_G1 24000            /* one */

typedef struct {
    uint8_t w[3];                /* TW_* of osc 1..3 */
    uint8_t flags;               /* TRIO_RING | TRIO_SYNC */
    int16_t g[3];                /* Q15 levels (0: silent; osc 3 still runs for RING) */
} trio_set_t;

static const char *const N_TRIO_WAVE[] = {"3xSAW", "3xPULSE", "2xPULSE+TRI", "2xSAW+TRI", "3xTRI", "SAW&TRI", "PULSE&SAW", "PULSE&NOISE",
                                          "NOISE", "SAW\nSYNC", "PULSE\nSYNC", "SAW+TRI\nSYNC", "TRI\nRING", "RING+2xTRI", "TRI+SAW\nRING SYNC", "TRI+PULSE\nRING SYNC"};
static const trio_set_t TRIO_SETS[16] = {
    {{TW_SAW, TW_SAW, TW_SAW}, 0, {TRIO_G3, TRIO_G3, TRIO_G3}},                 /* SAW3 */
    {{TW_PLS, TW_PLS, TW_PLS}, 0, {TRIO_G3, TRIO_G3, TRIO_G3}},                 /* PLS3 */
    {{TW_PLS, TW_PLS, TW_TRI}, 0, {TRIO_G3, TRIO_G3, TRIO_G3 + 3000}},          /* PPT */
    {{TW_SAW, TW_SAW, TW_TRI}, 0, {TRIO_G3, TRIO_G3, TRIO_G3 + 3000}},          /* SST */
    {{TW_TRI, TW_TRI, TW_TRI}, 0, {TRIO_G3 + 2000, TRIO_G3 + 2000, TRIO_G3 + 2000}},   /* TRI3 */
    {{TW_ST, TW_ST, TW_TRI}, 0, {TRIO_G3 + 3000, TRIO_G3 + 3000, TRIO_G3}},     /* S&T */
    {{TW_PS, TW_PS, TW_TRI}, 0, {TRIO_G3 + 3000, TRIO_G3 + 3000, TRIO_G3}},     /* P&S */
    {{TW_PLS, TW_OFF, TW_NOI}, 0, {TRIO_G2, 0, TRIO_G2 - 4000}},                /* P+N */
    {{TW_NOI, TW_OFF, TW_OFF}, 0, {TRIO_G1, 0, 0}},                             /* NOIS */
    {{TW_SAW, TW_SAW, TW_OFF}, TRIO_SYNC, {TRIO_G2 - 5000, TRIO_G2 + 5000, 0}}, /* SYNC */
    {{TW_PLS, TW_PLS, TW_OFF}, TRIO_SYNC, {TRIO_G2 - 5000, TRIO_G2 + 5000, 0}}, /* SYNCP */
    {{TW_SAW, TW_SAW, TW_TRI}, TRIO_SYNC, {TRIO_G3 - 3000, TRIO_G3 + 4000, TRIO_G3}},  /* SYNC3 */
    {{TW_TRI, TW_OFF, TW_OFF}, TRIO_RING, {TRIO_G1, 0, 0}},                     /* RING */
    {{TW_TRI, TW_OFF, TW_TRI}, TRIO_RING, {TRIO_G2 + 2000, 0, TRIO_G2 - 3000}}, /* RING3 */
    {{TW_TRI, TW_SAW, TW_OFF}, TRIO_RING | TRIO_SYNC, {TRIO_G2 + 2000, TRIO_G2, 0}},   /* R+S */
    {{TW_TRI, TW_PLS, TW_OFF}, TRIO_RING | TRIO_SYNC, {TRIO_G2 + 2000, TRIO_G2, 0}},   /* R+SP */
};
static const char *const N_TRIO_MODE[] = {"LP", "BP", "HP", "NOT"};   /* (the names of filter 2's LP BP HP NOTCH: icons.c, params.c) */
/* filter 2's output = (in * a + bp * b + lp * c) >> 10 (chain_f2.c f2_svf); HP and notch take k * bp off (k: per block) */
static const int16_t TRIO_MIX[4][3] = {{0, 0, 1024}, {0, 1400, 0}, {1024, 0, -1024}, {1024, 0, 0}};

static const preset_t TRIO_PRESETS[] = {
    /* name, {WAVE, INT2, INT3, DTN, -, -, -, PW} (MODE CUT RES are filter 2's since cloud job J), {A D S R}, fenv, mono */
    {"FAT BASS", {0, 0, -12, 9, 0, 0, 0, 64}, {0, 62, 60, 25}, 40, 1, FX(10, 0, 10, 10), PAT(2)},
    {"ARP LEAD", {2, 12, 0, 4, 0, 0, 0, 32}, {0, 60, 90, 30}, 8, 0, FX(0, 0, 35, 20), PAT(13)},
    {"SYNC LEAD", {9, 9, 0, 0, 0, 0, 0, 64}, {2, 70, 100, 40}, 18, 1, FX(0, 10, 40, 25), PAT(4)},
    {"RING BELL", {13, 0, 18, 6, 0, 0, 0, 64}, {0, 92, 0, 80}, 0, 0, FX(0, 20, 35, 55), PAT(7)},
    {"CHIP CHOIR", {1, 0, 12, 7, 0, 0, 0, 40}, {70, 90, 110, 85}, 30, 0, FX(0, 50, 20, 65), PAT(5)},
};

static const uint8_t TRIO_UIORD[8] = {0, 1, 2, 3, 7, 4, 5, 6};   /* card position -> E index: ENGINE WAVE INT2 INT3; EDIT 1 DTN PW (MODE CUT RES are unused slots, hidden); the stored ids never move */

static const engine_t ENG_TRIO = {
    .name = "TRIO",
    .page_title = {"OSC", "TONE"},
    .edit = {
        {"WAVE", F_ENUM, 0, 15, 0, N_TRIO_WAVE, 0},
        {"INT2", F_SEMI, -24, 24, 0, 0, 0},
        {"INT3", F_SEMI, -24, 24, -12, 0, 0},
        {"DTN", F_INT, 0, 50, 6, 0, "ct"},
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was MODE: filter 2's TYPE */
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was CUT: filter 2's CUTOFF */
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was RES: filter 2's RES */
        {"PW", F_PCT, 0, 127, 64, 0, 0},
    },
    .presets = TRIO_PRESETS,
    .npresets = NELEM(TRIO_PRESETS),
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_FREEA, P_FREEB, P_E7, P_E1},   /* filter 1's CUT and RES (the engine's own CUT and RES were here) */
    .uiord = TRIO_UIORD,
    .atgt = ATG_TRIO,            /* ARP TARGET: OSC1..OSC3 (SPEC 2.3) */
    .ufilt = 1,                  /* no filter of its own since cloud job J: the multimode filter (voice.c uf_run), and filter 2 (TWIN) */
};
