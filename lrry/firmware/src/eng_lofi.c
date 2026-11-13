/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* L/RRY phase 4.4a: DESCRIPTOR ONLY. The engine's DSP (note-on, render and their helpers) is removed: the L/RRY voice (lrry_voice.c) is the only sound source. What stays is what the pages,
 * the editor, the stores and budget.c read (names, the EDIT cards, presets, the voice cap and flags, the page graphs' helpers). The description below is of the engine as L/RRY had it */
/* Chip voice: pulse/triangle/saw/noise/wave RAM, quantised amplitude and a held
 * (downsampled) output; SWEEP bends the pitch down after note-on.
 *
 * WAVE is ONE list (L/RRY 1.1): PLS TRI SAW NOISE, then 11 wave-RAM tables (SIN .. STAIR), each a 32-sample, 4-bit wave,
 * read step-wise (no interpolation). K4 (E2, "PULSE WIDTH") is the pulse width and is shown with WAVE = PLS only
 * (ui_draw.c card_hidden_vp); the table is WAVE - RW_SINE.
 * BITS = STEP: a 4-bit stepped volume envelope with a frame-clock divider replaces the
 * ADSR curve (lofi_amp), the triangle becomes a 32-step 4-bit staircase, the pulse
 * duties are 12.5 / 25 / 50 / 75 %; CRSH then sets the envelope (shown as "DCY").
 * SHP (the track's shape input: ENV / LFO DEST SHP, the matrix's SHP; L/RRY): pulse-width modulation around DUTY (PLS,
 * STEP's pulse), smooth, not in DUTY's steps (+-64 = +-12.5 % of the period, as ANALOG's PW; 6.25 .. 93.75 % at most). Not the
 * WRAM table: a table switch is a step, and the morph fades the SHP depths (tests/morph_test.c: no cliff). Centre
 * (64 << 8): DUTY as set, bit for bit. */
static const char *const N_BITS[] = {"4BIT", "4B/2", "8BIT", "1BIT", "STEP"};
static const char *const N_RWAVE[] = {"PLS", "TRI", "SAW", "NOISE", "SIN", "SAW 2", "BASS 1", "BASS 2",
                                      "ORGAN", "HOLLOW", "AH", "OH", "EE", "BUZZ", "STAIR"};
static const char *const N_RARP[] = {"OFF", "OCT", "MAJ", "MIN"};
enum { BITS_4BIT, BITS_4B2, BITS_8BIT, BITS_1BIT, BITS_STEP };
enum { RW_PLS, RW_TRI, RW_SAW, RW_NOIS, RW_SINE, RW_SAW2, RW_BAS1, RW_BAS2, RW_ORGN, RW_HOLW, RW_VOXA, RW_VOXO, RW_VOXE,
       RW_BUZZ, RW_STAIR, RW_NWAVE };
#define Q4 2184                  /* one 4-bit step (2 * 2184 * 7.5 = 32760) */

/* BITS = STEP: CRSH as the envelope, CRSH / 4 = b: 0 constant volume, 1..16 a one-shot decay
 * (divider period 0..15), 17..31 a looping one (period 1..15) */
static const char *const N_DCY[] = {"CONST", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10",
                                    "D11", "D12", "D13", "D14", "D15", "L1", "L2", "L3", "L4", "L5", "L6", "L7",
                                    "L8", "L9", "L10", "L11", "L12", "L13", "L14", "L15", 0};
static const param_desc_t LOFI_DCY = {"DCY", F_INT, 0, 127, 0, N_DCY, 0};        /* = edit[3] but the names */

static const param_desc_t *lofi_desc(const track_t *t, uint32_t k)
{
    if (k == 3u && t->p[P_E0] == BITS_STEP)
        return &LOFI_DCY;
    return 0;
}

/* the stepped envelope: volume 15..0 in v->s[6] bits 0..3, its divider in bits 4..7, a 240 Hz
 * frame clock (Q16 phase in v->s[7]); each frame the divider counts down and, at 0, reloads the
 * period and takes one step off the volume (a looping one starts again at 15 after 0) */
#define FRAME_Q16 ((240u * CTL * 65536u) / FS)       /* frames per control tick, Q16 */


static const preset_t LOFI_PRESETS[] = {
    {"PULSE LD", {0, 0, 32, 0, 0, 20, 0, 127}, {0, 60, 90, 30}, 0, 1, FX(0, 0, 40, 20), PAT(4)},
    {"WAVE BASS", {1, 1, 0, 0, 0, 0, 0, 90}, {0, 50, 70, 20}, 0, 1, FX(0, 0, 10, 0), PAT(2)},
    {"ARP 8BIT", {2, 0, 96, 0, 0, 0, 2, 110}, {0, 60, 80, 40}, 0, 0, FX(0, 0, 30, 20), PAT(13)},
    /* wave RAM AH (old VOXA), a little vibrato */
    {"WAVE LEAD", {0, 10, 64, 0, 0, 18, 0, 110}, {0, 70, 90, 30}, 0, 1, FX(0, 0, 40, 25), PAT(3)},
    /* STEP: 25 % pulse, DCY 34 = D7 (a 15-step decay over 0.5 s), REL ~54 ms of staircase after note-off */
    {"STEP LEAD", {4, 0, 40, 34, 0, 16, 0, 127}, {0, 64, 127, 55}, 0, 1, FX(0, 0, 40, 20), PAT(4)},
};

static const engine_t ENG_LOFI = {
    .name = "LOFI",
    .page_title = {"LOFI", "MOTN"},
    .edit = {
        {"BITS", F_ENUM, 0, 4, 0, N_BITS, 0},
        {"WAVE", F_ENUM, 0, RW_NWAVE - 1, 0, N_RWAVE, 0},
        {"PULSE WIDTH", F_INT, 0, 127, 64, 0, 0},       /* PLS only (ui_draw.c card_hidden_vp); in STEP 12.5 / 25 / 50 / 75 % */
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},               /* STEP: the envelope (lofi_desc) */
        {"SWP", F_PCT, 0, 127, 0, 0, 0},
        {"VIB", F_PCT, 0, 127, 0, 0, 0},
        {"ARP", F_ENUM, 0, 3, 0, N_RARP, 0},
        {"TONE", F_PCT, 0, 127, 127, 0, 0},
    },
    .presets = LOFI_PRESETS,
    .npresets = NELEM(LOFI_PRESETS),
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_E1, P_E2, P_E3, P_REL},
    .keep = 0x11,                /* the held sample and the low-pass */
    .desc = lofi_desc,
    .ufilt = 1,                 /* no filter of its own: the multimode filter (L/RRY 0.2.9; voice.c uf_run: CUT RES MODE SLOPE, P_FREEA..D) */
};
