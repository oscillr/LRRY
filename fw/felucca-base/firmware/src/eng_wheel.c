/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* L/RRY phase 4.4a: DESCRIPTOR ONLY. The engine's DSP (note-on, render and their helpers) is removed: the L/RRY voice (lrry_voice.c) is the only sound source. What stays is what the pages,
 * the editor, the stores and budget.c read (names, the EDIT cards, presets, the voice cap and flags, the page graphs' helpers). The description below is of the engine as DUELL had it */
/* WHEEL: a tonewheel-style additive organ. Felucca's own design.
 *
 * Partials: nine sines per voice at the classic drawbar footages, 16' 5 1/3' 8' 4' 2 2/3' 2'
 * 1 3/5' 1 1/3' 1' (0.5, 1.5, 1, 2, 3, 4, 5, 6, 8 times the note), each with a level 0..8
 * (about 3 dB a step). One shared sine table (plain lookup), one free-running integer phase
 * per partial: a note starts wherever its wheels are, as on the real generators. A partial
 * above ~6 kHz folds back an octave (as the top of a tonewheel set does), so nothing aliases.
 * Partials at level 0 are skipped (their phase still runs).
 *
 * Registration: 8 knobs cannot hold 9 drawbars, so REG picks one of 16 drawbar settings
 * (DRW_REG: our list of common registrations) and BODY / TOP add -8..+8 to its body (8', 4') and
 * its upper bars (2 2/3' .. 1'); the sub bars (16', 5 1/3') are the registration's alone (the SUB knob went in
 * cloud job J: more sub is the global SUB LAYER's, chain_sub.c). The louder the
 * registration, the less each bar gets (a soft normalisation: full bars are louder than one
 * bar, but they do not stack up to clipping).
 *
 * PERC: a decaying partial on the 2nd (4') or 3rd (2 2/3') harmonic, normal or soft, fast
 * or slow. Single trigger: it only strikes on a note that starts while no other key of the
 * part is held (legato playing gets none). With PERC on the 1' bar is silent and, at normal
 * level, the bars drop by ~3 dB, as on the real circuit.
 * CLICK: a short burst of high-passed noise at the key contact (half as loud at the release).
 * DRIVE: a per-voice soft overdrive (tanh); for chord growl add the track's DIST.
 * ROTR: a cheap rotary speaker effect: two rotors (a low one and a high one, split by partial
 * frequency at ~800 Hz) that spin SLOW or FAST and ramp between the speeds (the high rotor in
 * ~1 s, the low one in ~4 s); each gives its partials an amplitude and a pitch (Doppler)
 * modulation whose depth follows its speed. All of it is folded into the per-block partial
 * gains and increments: no delay line, no per-sample cost. OFF fades it out and stops them.
 *
 * SHP (DUELL; the track's shape input: ENV / LFO DEST SHP, the matrix's SHP): the voice's brightness, per block. Up, it
 * pulls the upper bars (2 2/3' .. 1'; not the 1' with PERC on) toward full and the sub bars (16', 5 1/3') toward silence,
 * down the reverse; the body (8', 4') stays; then the registration's normalisation again (drw_tilt). Smooth (not in
 * bar steps), so an envelope or LFO sweeps it; the centre (64 << 8) skips it: bit for bit as before.
 *
 * Envelope: an organ gate (the presets set A 0, S 127, a short R); the track ADSR still applies.
 * Cost: per sample and partial one table lookup and a multiply-add (a gain ramp only for the
 * partials whose gain moved: percussion, rotary, knobs); the bar levels, the rotor and the
 * percussion run once per block. State: the per-track rotor and bar gains (drw_trk) and the
 * per-voice phases / gains (drw_vc) live here, indexed by part and voice; voice_t is unused. */
#define DRW_NP 9                 /* partials */
#define DRW_FOLD 584366000u      /* ~6 kHz as a phase increment: partials above fold down an octave */
#define DRW_XOVER 77915000u      /* ~800 Hz: below the low rotor, above the high rotor */


/* the 16 registrations, one digit per bar: 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' */
static const char *const N_DRW_REG[] = {"FLUTE", "MELLOW", "HOLLOW", "SMOOTH", "3 BARS", "BLUES", "GOSPEL", "ROCK",
                                        "TOPS", "CLARI", "REED", "STRING", "CHAPL", "BRITE", "BASS", "FULL"};
static const char DRW_REG[16][DRW_NP + 1] = {
    "008000000", /* FLUTE  one soft flute */
    "008400000", /* MELLO  flute and octave */
    "800800000", /* HOLLW  sub and octave, hollow */
    "838000000", /* SMOOT  smooth comping */
    "888000000", /* 3BAR   the first three bars out */
    "888800000", /* BLUES  three bars and the octave */
    "888600006", /* GOSPL  body with a little shimmer */
    "888000888", /* ROCK   body and the top three */
    "800000888", /* TOPS   sub and the top only */
    "008080800", /* CLARI  odd harmonics, reedy-hollow */
    "008765432", /* REED   a falling slope, brassy */
    "006876540", /* STRNG  bright and thin */
    "004432000", /* CHAPL  soft, churchy */
    "008888000", /* BRITE  even bars, bright */
    "858000000", /* BASS   for the left hand */
    "888888888", /* FULL   everything */
};
static const char *const N_DRW_PERC[] = {"OFF", "2ND", "3RD", "2SOFT", "3SOFT", "2SLOW", "3SLOW"};
static const char *const N_DRW_ROTR[] = {"OFF", "SLOW", "FAST"};

/* rotor speeds as phase increments per block (CTL samples): Hz * CTL / FS * 2^32 */
#define DRW_HZ(h) ((int32_t)((h) * 3116457.0))

static int32_t drw_level(const int16_t *p, uint32_t k)
{
    int32_t d = DRW_REG[(uint32_t)p[P_E0] & 15u][k] - '0';
    d += k < 2u ? 0 : k < 4u ? p[P_E2] : p[P_E3];
    return clamp(d, 0, 8);
}

static const preset_t WHEEL_PRESETS[] = {
    /* name, {REG, -, BODY, TOP, PERC, CLICK, DRIVE, ROTR}, {A D S R}, fenv, mono (E2 was SUB until cloud job J) */
    {"FULL ORGAN", {15, 0, 0, 0, 0, 30, 20, 1}, {0, 64, 127, 45}, 0, 0, FX(0, 0, 10, 35), PAT(5)},
    {"JAZZ PERC", {4, 0, 0, 0, 2, 50, 8, 1}, {0, 64, 127, 40}, 0, 0, FX(0, 0, 0, 25), PAT(3)},
    {"GOSPEL", {6, 0, 0, 0, 1, 60, 40, 2}, {0, 64, 127, 45}, 0, 0, FX(0, 0, 0, 40), PAT(6)},
    {"SOFT FLUTE", {1, 0, -2, 0, 0, 10, 0, 1}, {0, 64, 127, 55}, 0, 0, FX(0, 20, 15, 55), PAT(5)},
    {"ROCK DRIVE", {7, 0, 0, 0, 0, 70, 100, 2}, {0, 64, 127, 40}, 0, 0, FX(35, 0, 10, 25), PAT(4)},
};

static const uint8_t WHEEL_UIORD[8] = {0, 2, 3, 4, 5, 6, 7, 1};   /* card position -> E index: ENGINE REG BODY TOP; EDIT 1 PERC CLICK DRV ROTR; the unused SUB slot last (EDIT 2 is not shown); the stored ids never move */

static const engine_t ENG_WHEEL = {
    .name = "WHEEL",
    .page_title = {"BARS", "TONE"},
    .edit = {
        {"REG", F_ENUM, 0, 15, 4, N_DRW_REG, 0},
        {"-", F_INT, 0, 0, 0, 0, 0},                 /* was SUB: the registration's sub bars are not raised or lowered any more; the global SUB LAYER (chain_sub.c) adds sub */
        {"BODY", F_INT, -8, 8, 0, 0, 0},
        {"TOP", F_INT, -8, 8, 0, 0, 0},
        {"PERC", F_ENUM, 0, 6, 0, N_DRW_PERC, 0},
        {"CLICK", F_PCT, 0, 127, 40, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"ROTR", F_ENUM, 0, 2, 1, N_DRW_ROTR, 0},
    },
    .presets = WHEEL_PRESETS,
    .npresets = NELEM(WHEEL_PRESETS),
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_E0, P_E4, P_E6, P_E7},
    .uiord = WHEEL_UIORD,
    .ufilt = 1,                 /* no filter of its own: the multimode filter (DUELL 0.2.9; voice.c uf_run: CUT RES MODE SLOPE, P_FREEA..D) */
};
