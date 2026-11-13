/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* L/RRY phase 4.4a: DESCRIPTOR ONLY. The engine's DSP (note-on, render and their helpers) is removed: the L/RRY voice (lrry_voice.c) is the only sound source. What stays is what the pages,
 * the editor, the stores and budget.c read (names, the EDIT cards, presets, the voice cap and flags, the page graphs' helpers). The description below is of the engine as L/RRY had it */
/* ANALOG: two band-limited oscillators (saw / square / tri / sine / PWM),
 * noise and drive. No filter of its own since the engine filters left (TWIN_FILTER_DESIGN_2, cloud job J): its VCF is filter 1, the MULTI FILTER of the voice chain (voice.c uf_run;
 * ANALOG's softer RES floor became filter 1's). DRV stays here: it drives the oscillator mix before any filter (the Minimoog's overdriven mixer).
 * WAVE SYNC and SUB (#104, appended: WAVE 0..4 play as they always did) are two more oscillator pairs (analog_osc_x):
 *   SYNC  OSC 2 a saw hard-synced to OSC 1 (a saw): it restarts with OSC 1's every cycle, band-limited at the restart
 *         (polyBLEP scaled by the step the restart makes). DTN (shown SYNC) sets OSC 2 above OSC 1, 3/16 semitone a
 *         step (127: +23.8 semitones), and the SHP routings sweep it (ENV DEST / LFO DEST SHP, the matrix's SHP: 3/16
 *         semitone a step of shape, as PWM takes them for its width): the sync sweep. MIX between the two as for
 *         every pair (OSC 2 alone at 127)
 *   SUB   OSC 1 a saw, OSC 2 a square an octave below it (detuned by DTN in cents as before): MIX (shown SUB) is the
 *         sub oscillator's level */
static const char *const N_ANALOG_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PWM", "SYNC", "SUB"};
enum { AW_SYNC = 5, AW_SUB = 6 };
static const param_desc_t ANALOG_SYNC = {"SYNC", F_INT, 0, 127, 10, 0, 0};   /* DTN's range and default */
static const param_desc_t ANALOG_SUB = {"SUB", F_PCT, 0, 127, 64, 0, 0};      /* MIX's */

static const param_desc_t *analog_desc(const track_t *t, uint32_t k)
{
    if (k == 1u && t->p[P_E0] == AW_SYNC)
        return &ANALOG_SYNC;
    if (k == 2u && t->p[P_E0] == AW_SUB)
        return &ANALOG_SUB;
    return 0;
}

/* {WAVE, DTN, MIX, NOIS, -, -, DRV, -}: the filter's three slots are unused (CUT RES KTR were filter 1's since cloud job J) */
static const preset_t ANALOG_PRESETS[] = {
    {"SAW LEAD", {0, 12, 64, 0, 0, 0, 10, 0}, {4, 70, 100, 50}, 20, 1, FX(0, 10, 45, 30), PAT(4)},
    {"SOFT PAD", {0, 20, 64, 4, 0, 0, 0, 0}, {80, 90, 110, 95}, 10, 0, FX(0, 60, 20, 70), PAT(5)},
    {"SQR BASS", {1, 0, 0, 0, 0, 0, 40, 0}, {0, 60, 40, 30}, 30, 1, FX(5, 0, 10, 10), PAT(2)},
    {"PWM STR", {4, 8, 40, 0, 0, 0, 0, 0}, {60, 80, 110, 85}, 8, 0, FX(0, 50, 20, 60), PAT(5)},
    {"ACID", {0, 0, 0, 0, 0, 0, 25, 0}, {0, 55, 20, 30}, 48, 1, FX(20, 0, 45, 15), PAT(1)},
    {"SINE KEY", {3, 6, 50, 0, 0, 0, 0, 0}, {2, 80, 30, 70}, 0, 0, FX(0, 30, 25, 40), PAT(6)},
    {"RAVE", {4, 30, 64, 0, 0, 0, 30, 0}, {20, 80, 110, 60}, 10, 1, FX(30, 40, 30, 30), PAT(13)},
    {"SUB BASS", {3, 0, 0, 0, 0, 0, 20, 0}, {0, 60, 100, 20}, 0, 1, FX(0, 0, 0, 10), PAT(8)},
    {"PLUCK", {0, 8, 50, 0, 0, 0, 0, 0}, {0, 88, 0, 60}, 55, 0, FX(0, 20, 50, 30), PAT(3)},
    {"BRASS", {0, 10, 64, 0, 0, 0, 10, 0}, {35, 70, 90, 45}, 40, 0, FX(0, 20, 20, 40), PAT(6)},
    {"WIND", {0, 0, 0, 90, 0, 0, 0, 0}, {60, 90, 60, 80}, 50, 0, FX(0, 30, 30, 70), PAT(5)},
    {"STRINGS", {0, 25, 64, 0, 0, 0, 0, 0}, {70, 90, 115, 90}, 5, 0, FX(0, 60, 20, 70), PAT(5)},
};

static const uint8_t ANALOG_UIORD[8] = {0, 1, 2, 3, 6, 4, 5, 7};   /* card position -> E index: ENGINE WAVE DTN MIX; EDIT 1 NOIS DRV (the three unused slots after them, hidden); the stored ids never move */

static const engine_t ENG_ANALOG = {
    .name = "ANALOG",
    .page_title = {"OSC", "FLT"},
    .edit = {
        {"WAVE", F_ENUM, 0, 6, 0, N_ANALOG_WAVE, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was CUT: filter 1 (voice.c uf_run) */
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was RES */
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was KTR (filter 1's KEY TRK) */
    },
    .presets = ANALOG_PRESETS,
    .npresets = NELEM(ANALOG_PRESETS),
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .desc = analog_desc,
    .knob = {P_FREEA, P_FREEB, P_ATK, P_REL},   /* filter 1's CUT and RES (they were the engine's own) */
    .uiord = ANALOG_UIORD,
    .atgt = ATG_ANALOG,          /* ARP TARGET: OSC A, OSC B (SPEC 2.3) */
    .ufilt = 1,                  /* no filter of its own since cloud job J: the multimode filter (voice.c uf_run: CUT RES MODE SLOPE, P_FREEA..D) */
};
