/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* L/RRY phase 4.4a: DESCRIPTOR ONLY. The engine's DSP (note-on, render and their helpers) is removed: the L/RRY voice (lrry_voice.c) is the only sound source. What stays is what the pages,
 * the editor, the stores and budget.c read (names, the EDIT cards, presets, the voice cap and flags, the page graphs' helpers). The description below is of the engine as DUELL had it */
/* PHYS: physical models. The DSP is ported to fixed point from DaisySP (Electrosmith, Emilie Gillet;
 * MIT: phys_dsp.c) and Rings' sympathetic strings (Emilie Gillet; MIT: phys_symp.c); this file is the
 * Felucca engine around them.
 *
 * MODEL picks one of four (the values are stored: new ones are appended):
 *   MODAL   a bank of 12 band-pass modes (bells, bars, plates): STRC stretches the partials
 *           (below 0.25 compressed, 0.25..0.3 harmonic, above more and more inharmonic), BRIT
 *           keeps the high modes ringing and opens the strike, DAMP the decay (127 rings on),
 *           POS the strike position (the mode amplitudes follow cos(2 pi pos i), 0..1/4).
 *           Struck by an impulse into a low-pass; with BOW > 0 sustained by random hits (bowed /
 *           blown) at that level instead
 *   STRNG   an extended Karplus-Strong string, plucked by a noise burst one period long: STRC
 *           below ~0.25 a curved bridge (buzzy, sitar-like), above it dispersion (stiff, piano-
 *           like), BRIT the burst's tone and the loop filter, DAMP the decay, POS the pluck
 *           position (a comb on the burst, 0 = none); BOW > 0 again sustains it by random hits
 *   MEMB    a struck drum head: the modal bank with the modes of a circular membrane. HARM blends an
 *           ideal head (inharmonic: toms, timpani) into a loaded one (harmonic overtones: a tabla-like
 *           hand drum), POS goes from the centre (round, only the circular modes) to the rim, BEND
 *           drops the pitch after the strike (up to 12 semitones); BRIT, DAMP as MODAL
 *   SYMP    the plucked string with three sympathetic strings that ring along (a sitar's, a
 *           harp's): CHRD tunes them (OCT 5TH 4TH MAJ MIN SUS 7TH above the note, ROOT: the track's
 *           scale root, its fifth and octave around C3), SYMP how much they take from the string, BUZZ
 *           the curved bridge; BRIT, DAMP as STRNG
 * ACC is the accent at full velocity (brighter, longer, louder strike; velocity scales it), EXC
 * mixes the exciter itself into the output (the mallet / pick). The track's ENV / LFO -> FLT move BRIT, -> SHP (DUELL)
 * STRC, the structure (SYMP: BUZZ, the structure it takes there; CHRD stays); centre = as set, bit for bit.
 *
 * MODEL 4 was DRUM before 1.0 (a DRUM engine took it over, and DUELL 0.2.14 removed that engine): there is no MODEL 4 now.
 *
 * DUST (resonant particles, value 2 before 1.0) was dropped: MODEL 2 is MEMB now (stores of before 1.0 are no longer read).
 *
 * Polyphony 3 per part (engine_t.poly): each part's voices 0..2 own a state slot in the part's engine
 * memory (engines.c eng_mem; phys_slot_of: SYMP's, the largest: the string's 512 + 128 sample lines, Q20, and three
 * 256-sample sympathetic lines, 16-bit), 12 slots in all. Voice amplitude: the track's ADSR as for every
 * engine; the presets hold SUS at 127 so the model's own decay is heard and REL damps it after the key.
 * Cost (host, 8 notes asked = 3 voices): see the README and cpu_baseline.txt. */

enum { PM_MODAL, PM_STRING, PM_MEMB, PM_SYMP, PM_COUNT };
#define PHYS_POLY 3




static const char *const N_PHYS_MODEL[] = {"MODAL", "STRING", "MEMB", "SYMP"};
static const char *const N_PHYS_CHORD[] = {"OCT", "5TH", "4TH", "MAJ", "MIN", "SUS", "7TH", "ROOT", 0};
/* per model: STRC .. EXC under their own names (phys_desc; range and default as edit[], 0 = edit[]) */
static const param_desc_t PHYS_D_HARM = {"HARM", F_PCT, 0, 127, 64, 0, 0};
static const param_desc_t PHYS_D_BEND = {"BEND", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t PHYS_D_CHRD = {"CHRD", F_INT, 0, 127, 64, N_PHYS_CHORD, 0};
static const param_desc_t PHYS_D_SYMP = {"SYMP", F_PCT, 0, 127, 20, 0, 0};
static const param_desc_t PHYS_D_BUZZ = {"BUZZ", F_PCT, 0, 127, 0, 0, 0};
static const param_desc_t *const PHYS_DESC[PM_COUNT][8] = {
    [PM_MEMB] = {0, &PHYS_D_HARM, 0, 0, 0, 0, &PHYS_D_BEND, 0},
    [PM_SYMP] = {0, &PHYS_D_CHRD, 0, 0, &PHYS_D_SYMP, 0, &PHYS_D_BUZZ, 0},
};

static const param_desc_t *phys_desc(const track_t *t, uint32_t k)
{
    return k < 8u ? PHYS_DESC[(uint32_t)t->p[P_E0] % PM_COUNT][k] : 0;
}

/* MODAL partials by STRC: 34 harmonic, 70 1 : 2.83 : 5.48 : 8.90 (a free bar, a chime: 1 : 2.76 : 5.40 : 8.93),
 * 98 1 : 3.57 : 7.65, 116 1 : 4.0 : 9.0 (a tuned bar: 1 : 4 : 10) */
static const preset_t PHYS_PRESETS[] = {
    /* name, {MODEL, STRC, BRIT, DAMP, POS, ACC, BOW, EXC}, {A D S R}, fenv, mono */
    {"BELL TREE", {PM_MODAL, 70, 100, 106, 12, 100, 0, 6}, {0, 100, 127, 96}, 0, 0, FX(0, 20, 30, 60), PAT(7)},
    {"MARIMBA", {PM_MODAL, 116, 38, 40, 24, 90, 0, 30}, {0, 90, 127, 52}, 0, 0, FX(0, 0, 15, 30), PAT(6)},
    {"PLUCK", {PM_STRING, 56, 72, 84, 40, 96, 0, 0}, {0, 100, 127, 58}, 0, 0, FX(0, 10, 30, 30), PAT(3)},
    {"BOWED METAL", {PM_MODAL, 98, 70, 112, 10, 80, 90, 0}, {60, 90, 127, 90}, 0, 0, FX(0, 40, 20, 70), PAT(5)},
    {"KALIMBA", {PM_MODAL, 34, 16, 56, 100, 90, 0, 48}, {0, 100, 127, 60}, 0, 0, FX(0, 10, 20, 40), PAT(7)},
    /* MEMB: {MODEL, HARM, BRIT, DAMP, POS, ACC, BEND, EXC} */
    {"HAND DRUM", {PM_MEMB, 112, 76, 56, 44, 100, 14, 24}, {0, 100, 127, 64}, 0, 0, FX(0, 0, 10, 30), PAT(6)},
    {"TOMS", {PM_MEMB, 0, 52, 40, 30, 100, 40, 30}, {0, 100, 127, 70}, 0, 0, FX(0, 0, 0, 40), PAT(2)},
    /* SYMP: {MODEL, CHRD, BRIT, DAMP, SYMP, ACC, BUZZ, EXC} */
    {"DRONE STRING", {PM_SYMP, 127, 80, 88, 64, 100, 92, 0}, {0, 100, 127, 90}, 0, 0, FX(0, 10, 20, 50), PAT(3)},
    {"HARP", {PM_SYMP, 8, 62, 74, 60, 90, 0, 0}, {0, 100, 127, 80}, 0, 0, FX(0, 20, 20, 60), PAT(3)},
};

static const engine_t ENG_PHYS = {
    .name = "PHYS",
    .page_title = {"BODY", "EXCT"},
    .edit = {
        {"MODEL", F_ENUM, 0, PM_COUNT - 1, 0, N_PHYS_MODEL, 0},
        {"STRC", F_PCT, 0, 127, 64, 0, 0},
        {"BRIT", F_PCT, 0, 127, 80, 0, 0},
        {"DAMP", F_PCT, 0, 127, 80, 0, 0},
        {"POS", F_PCT, 0, 127, 20, 0, 0},
        {"ACC", F_PCT, 0, 127, 90, 0, 0},
        {"BOW", F_PCT, 0, 127, 0, 0, 0},
        {"EXC", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = PHYS_PRESETS,
    .npresets = NELEM(PHYS_PRESETS),
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .poly = PHYS_POLY,
    .desc = phys_desc,
    .mem = 12888u,               /* (4.4a: the bytes its state had, sizeof(phys_slot_t) x PHYS_POLY: what a claim clears stays as it was; the state itself is gone) */
    .ufilt = 1,                 /* no filter of its own: the multimode filter (DUELL 0.2.9; voice.c uf_run: CUT RES MODE SLOPE, P_FREEA..D) */
};
