/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* L/RRY phase 4.4a: DESCRIPTOR ONLY. The engine's DSP (note-on, render and their helpers) is removed: the L/RRY voice (lrry_voice.c) is the only sound source. What stays is what the pages,
 * the editor, the stores and budget.c read (names, the EDIT cards, presets, the voice cap and flags, the page graphs' helpers). The description below is of the engine as L/RRY had it */
/* NOISE: from analog-style noise to shift-register noise. No filter of its own since the engine filters left (TWIN_FILTER_DESIGN_2, cloud job J): its FREQ / RES / TRK are filter 1, the MULTI FILTER
 * (voice.c uf_run; RES as MODE LP > BP and the damping). What stays here is the loudness make-up, which now reads filter 1's effective cutoff WITHOUT the velocity accent (vmod_t.ucn, set by the voice path; 1.1: it fell as the accent opened the filter):
 * the source is made louder the lower the filter will close, BEFORE the filter (the amp ramp is applied to the signal scaled down by 2^sh, sh 0..4, and the shift put back, so the
 * x16 of a low cutoff cannot overflow mulq15). DRFT's wander of the cutoff is handed to filter 1 one block late (v->s[0], voice.c track_render adds it to the voice's cutoff routes while
 * filter 1 is on). CRSH is the last thing the engine does, so it runs before filter 1 and the filter smooths its steps.
 *
 * MODE picks the source:
 *   ANLG  xorshift white noise, plus random impulses (DENS: their density, 0 = none)
 *   DUST  the impulses only: crackle, drops (through a resonant filter)
 *   LFSR  a long shift register (LEN: 23 down to 7 bits) clocked CLK semitones above the key
 *   META  one cycle of a short register sequence per period of the key (LEN: which one): a metallic tone
 * COLR tilts the source from white (0) through pink (64, -3 dB/oct) to brown (127, -6 dB/oct).
 * DRFT: a slow random wander of filter 1's cutoff (and the clock), the level following it. CRSH (ANLG, DUST): sample-and-hold and fewer bits.
 * The track's SHP moves COLR, PIT the key (so the clock).
 *
 * State: s[0] the cutoff wander handed to filter 1 (Q8 dial steps), s[2] the random generator (seeded by the note and the unison detune: a
 * note from silence sounds the same each time), s[3] the drift target and its countdown, s[4..6] the pink
 * poles, s[7] the brown pole; ph[0] the clock phase (LFSR, META) or the hold count (CRSH), ph[1] the
 * register, the held sample or META's DC blocker, ph[2] the drift. */
static const char *const N_NOISE_MODE[] = {"ANALOG", "DUST", "DIGITAL", "METAL"};
enum { NZ_ANLG, NZ_DUST, NZ_LFSR, NZ_META };
static const char *const N_NOISE_LONG[] = {"23 BIT", "20 BIT", "17 BIT", "15 BIT", "13 BIT", "11 BIT", "9 BIT", "7 BIT", 0};
static const char *const N_NOISE_META[] = {"127 A", "127 B", "93", "63 A", "63 B", "31", "15", "7", 0};
static const param_desc_t NOISE_LONG = {"LENGTH", F_INT, 0, 127, 0, N_NOISE_LONG, 0};   /* = edit[5] but the names */
static const param_desc_t NOISE_META = {"LENGTH", F_INT, 0, 127, 0, N_NOISE_META, 0};
static const param_desc_t NOISE_CLK = {"PITCH", F_INT, 0, 127, 0, 0, "st"};

/* META: one period of a register's output bit (bit i of the words, from the state 1), its length,
 * and the levels of a 1 and a 0 (no DC, the larger at 16384). 7-bit taps 0x60 and 0x78, the 15-bit
 * register with its feedback from bits 0 and 6 (period 93), 6-bit 0x30 and 0x2D, 5-bit, 4-bit, 3-bit */
static const struct { uint32_t w[4]; uint8_t len; int16_t hi, lo; } NOISE_SEQ[8] = {
    {{0xD13C50C1u, 0x91C2F95Cu, 0xA58DED6Cu, 0x01FD533Bu}, 127, 16128, -16384},
    {{0xDBD165F1u, 0x9E46A1FDu, 0x84C15692u, 0x019B1CEBu}, 127, 16128, -16384},
    {{0x41008001u, 0x00492402u, 0x00824824u, 0x00000000u}, 93, 16384, -3404},
    {{0x27179461u, 0x03F566EDu, 0x00000000u, 0x00000000u}, 63, 15872, -16384},
    {{0x75A6C487u, 0x02FCA8CFu, 0x00000000u, 0x00000000u}, 63, 15872, -16384},
    {{0x05763E69u, 0x00000000u, 0x00000000u, 0x00000000u}, 31, 15360, -16384},
    {{0x00000F59u, 0x00000000u, 0x00000000u, 0x00000000u}, 15, 14336, -16384},
    {{0x0000001Du, 0x00000000u, 0x00000000u, 0x00000000u}, 7, 12288, -16384},
};

static const param_desc_t *noise_desc(const track_t *t, uint32_t k)
{
    int32_t mode = t->p[P_E0];
    if (k == 5u && mode >= NZ_LFSR)
        return mode == NZ_LFSR ? &NOISE_LONG : &NOISE_META;
    if (k == 7u && mode >= NZ_LFSR)
        return &NOISE_CLK;
    return 0;
}

/* {MODE, COLR, -, -, -, DENS, DRFT, CRSH / CLK} (FREQ RES TRK are filter 1's since cloud job J) */
static const preset_t NOISE_PRESETS[] = {
    {"WIND", {NZ_ANLG, 64, 0, 0, 0, 0, 70, 0}, {90, 90, 110, 90}, 0, 0, FX(0, 30, 20, 80), PAT(5)},
    {"RAIN", {NZ_DUST, 30, 0, 0, 0, 105, 40, 0}, {10, 90, 127, 70}, 0, 0, FX(0, 0, 20, 70), PAT(5)},
    /* a 7-bit register (127 steps) clocked 84 semitones (7 octaves) above the key: one period per key period,
     * a pitched chip-style buzz */
    {"ARCADE", {NZ_LFSR, 0, 0, 0, 0, 112, 0, 84}, {0, 80, 0, 50}, 0, 0, FX(0, 0, 10, 20), PAT(3)},
    {"METAL", {NZ_META, 0, 0, 0, 0, 32, 0, 0}, {0, 75, 40, 60}, 10, 0, FX(0, 20, 30, 45), PAT(7)},
};

static const uint8_t NOISE_UIORD[8] = {0, 1, 5, 6, 7, 2, 3, 4};   /* card position -> E index: ENGINE MODE COLR DENS; EDIT 1 DRFT CRSH (FREQ RES TRK are unused slots, hidden); the stored ids never move */

static const engine_t ENG_NOISE = {
    .name = "NOISE",
    .page_title = {"SRC", "MOVE"},
    .edit = {
        {"MODE", F_ENUM, 0, 3, 0, N_NOISE_MODE, 0},
        {"COLR", F_PCT, 0, 127, 64, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was FREQ: filter 1's CUT */
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was RES: filter 1's RES and MODE */
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was TRK: filter 1's KEY TRK */
        {"DENS", F_PCT, 0, 127, 0, 0, 0},           /* LFSR, META: the register (noise_desc) */
        {"DRFT", F_PCT, 0, 127, 0, 0, 0},
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},           /* LFSR, META: the clock above the key */
    },
    .presets = NOISE_PRESETS,
    .npresets = NELEM(NOISE_PRESETS),
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_FREEA, P_FREEB, P_E1, P_REL},   /* filter 1's CUT and RES (FREQ and RES were here) */
    .keep = 0xF1,                /* the wander handed to filter 1, the colour poles */
    .desc = noise_desc,
    .uiord = NOISE_UIORD,
    .ufilt = 1,                  /* no filter of its own since cloud job J: the multimode filter (voice.c uf_run) */
};
