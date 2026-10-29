/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DUELL cloud feature plan, wave 0: the INTERFACE COMMIT (docs/design/CLOUD_FEAT_PLAN.md section 3, docs/design/EXTENSION_BLOCK_PLAN.md
 * section 2A). Names and sizes only: nothing here is read or written by the firmware yet, every extension value is 0, and the rendered
 * samples are bit for bit those of cloud/base3. Jobs A (store), B (values), C (MIDI map) and D (velocity) work on top of these names.
 *
 * Included twice by core.h, on purpose: the first time (before the parameter ids) it gives the constants track_t needs; the second time
 * (after the types) it declares the hooks, which need track_t, voice_t and vmod_t. Job B owns the hook declarations: the argument lists
 * are provisional (CLOUD_FEAT_B_BRIEF task 6) and may change there. */
#ifndef DUELL_EXT_H_CONST
#define DUELL_EXT_H_CONST

/* --- extension slots: 15 stored values that have no old parameter id (EXTENSION_BLOCK_PLAN 2A) --------------------------------- */
#define NX 64                   /* slots in a part's value list: X0 .. X27 as DUELL 1.1 (X15 .. X26 the DEST cards and the LFO LEVELs, X27 the chord keys' STRUM), X28 .. X63 the L/RRY voice (phase 4.3a, below) */
#define XW_BASE 128             /* wide id of slot 0: wide ids 0..126 are the old ids, 127 is reserved (the motion escape), 128.. the slots */
#define XWID(k) (XW_BASE + (k)) /* the wide id of slot k (X_ROUTE 128 .. X_LFO2LVL 154) */
#define EXT_P_LEN (XW_BASE + NX)    /* length of track_t.p[]: 156 (index 127 stays empty); P_COUNT stays 127, every store and format counts that */
#define XV(t, k) ((t)->p[XW_BASE + (k)])    /* extension value k of part t */
/* MX_AMT (1.0, owner 2026-10-21; since 0.2.32 also the direct depth cards ENV / LFO to PIT, FLT, SHP and TWIN ENV 2 / LFO 1 / LFO 2): a depth at the top of its knob is a true 100 %: +63 acts as 64 (the whole range, as the shifts take 64), so the +100 % shown is what happens.
 * Below 0 and at 0 nothing changes (-64 was already 100 %); +32 .. +63 grow by one step (63 -> 64 is +1.6 %, 32 -> 33 +3.1 %), +1 .. +31 are unchanged */
#define MX_AMT(a) ((a) > 0 ? (a) + (((a) + 32) >> 6) : (a))   /* (x 1.0156, rounded: 63 -> 64, 50 -> 51; one shift and one add) */
#define DEP_AMT(a) MX_AMT(a)                        /* the direct depth cards (voice.c, chain_f2.c): the same rule */

enum {                          /* slot numbers (the wide id is 128 + slot) */
    X_ROUTE,                    /* X0  128  TWIN ROUTE: SERIES, PARALLEL = 0..1 (SERIES) */
    X_LINK,                     /* X1  129  TWIN LINK: OFF, ON = 0..1 (ON) */
    X_KTRK2,                    /* X2  130  TWIN KEY TRK 2: -63..+63 (0) */
    X_ENV2,                     /* X3  131  TWIN ENV 2 (to CUTOFF 2): -64..+63 (0) */
    X_LFO1,                     /* X4  132  TWIN LFO 1 (to CUTOFF 2): -64..+63 (0) */
    X_LFO2,                     /* X5  133  TWIN LFO 2 (to CUTOFF 2): -64..+63 (0) */
    X_SUBLVL,                   /* X6  134  SUB LEVEL: 0..127 (0 = not run) */
    X_SUBOCT,                   /* X7  135  SUB OCTAVE: -2, -1, 0 stored 0..2 (-1) */
    X_SUBWAVE,                  /* X8  136  SUB WAVE: SINE, TRI, SQUARE, PULSE = 0..3 (SINE) */
    X_SUBHARM,                  /* X9  137  SUB HARMONICS: 0..127 (0 = not run) */
    X_CYMODE,                   /* X10 138  CYCLE MODE: LOOP, ONCE = 0..1 */
    X_CYRISE,                   /* X11 139  CYCLE RISE: 0..127 */
    X_CYFALL,                   /* X12 140  CYCLE FALL: 0..127 */
    X_CYCURVE,                  /* X13 141  CYCLE CURVE: -64..+63 (0 = straight) */
    X_CYSYNC,                   /* X14 142  CYCLE SYNC: OFF, ON = 0..1 (OFF) */
    /* DUELL 1.1 (owner, DEST pages): per-source depth cards that run through the matrix's smooth path (mod.c mod_vr, the virtual routes), and the LFOs' LEVEL. All depths -64..+63 (0) */
    X_ESAT,                     /* X15 143  AMP ENV to SATURATION */
    X_E2BLD,                    /* X16 144  FILTER ENV to FILTER BLEND */
    X_E2DRV,                    /* X17 145  FILTER ENV to FILTER DRIVE */
    X_E2RES,                    /* X18 146  FILTER ENV to MULTI RES */
    X_CYCUT2,                   /* X19 147  CYCLE to TWIN CUTOFF */
    X_CYPAN,                    /* X20 148  CYCLE to PAN */
    X_CYPIT,                    /* X21 149  CYCLE to PITCH */
    X_L2RES2,                   /* X22 150  LFO 2 to TWIN RES */
    X_L2FMODE,                  /* X23 151  LFO 2 to MULTI MODE */
    X_L2BLD,                    /* X24 152  LFO 2 to FILTER BLEND */
    X_LFO1LVL,                  /* X25 153  LFO 1 LEVEL (its master depth): 0..127, default 127 = as before the card existed */
    X_LFO2LVL,                  /* X26 154  LFO 2 LEVEL: 0..127, default 127 */
    /* X27: STRUM (DUELL 1.1 item 144, owner 2026-10-27): the CHORD page's K3, the time between the tones of a chord one key (or one MIDI note) plays, low to high: 0 = OFF (all
     * tones at once, as before), 1..100 ms (seq.c strum_block). STORED WITH THE PATCH (no core id is free: P_COUNT is 127 and every store counts it; this slot was already in every record,
     * multi, project and the editor protocol at its default 0, so no format changes). DUELL 1.1 had the patch's KEY OFFSET here for a while (removed 2026-10-27, item 141) and then a spare;
     * an older save that left a value here plays it as a STRUM time (no-backcompat rule) */
    X_STRUM,                    /* X27 155  STRUM: 0..100 ms (0 OFF) */
    /* L/RRY phase 4.3a (docs: LRRY/PARAM_LIST.md): the stereo voice's parameters that have no old id. Stored -64..+127 like every slot. The old ids that the voice keeps (cutoff, RES, MODE, SLOPE of
     * filter A: P_FREEA..D; filter B: P_TWTYPE, P_CUT2, P_RES2, X_LINK; DRIVE A: P_DRV2; ROUTE; the global BLEND; SUB LEVEL / OCTAVE; ENV, LFO, CYCLE, matrix, level, pan) are listed in lrry_param.c */
    X_O1MODEL,                  /* X28 156  OSC 1 MODEL: SAW TRI SINE PULSE SYNC SUPER FM FOLD (0) */
    X_O1COARSE,                 /* X29 157  OSC 1 COARSE: -24..+24 semitones (0) */
    X_O1FINE,                   /* X30 158  OSC 1 FINE: -50..+50 cents (0) */
    X_O1LVL,                    /* X31 159  OSC 1 LEVEL: 0..127 (64) */
    X_O1PAN,                    /* X32 160  OSC 1 PAN: -64..+63 (0) */
    X_O1BAL,                    /* X33 161  OSC 1 BAL: the A / B filter balance of the source, -64..+63 (0) */
    X_O1P1, X_O1P2, X_O1P3,     /* X34..X36 162..164  OSC 1 P1 P2 P3: 0..127 (64), what they mean follows MODEL (lrry_param.c) */
    X_O2MODEL, X_O2COARSE, X_O2FINE, X_O2LVL, X_O2PAN, X_O2BAL, X_O2P1, X_O2P2, X_O2P3,   /* X37..X45 165..173  OSC 2, the same; LEVEL 0 */
    X_NZLVL, X_NZPAN, X_NZBAL,  /* X46..X48 174..176  NOISE: LEVEL 0..127 (0), PAN, BAL */
    X_RGLVL, X_RGPAN, X_RGBAL,  /* X49..X51 177..179  RING (OSC 1 x OSC 2): LEVEL (0), PAN, BAL */
    X_FAPAN, X_FBPAN,           /* X52 X53 180 181  FILTER A PAN, FILTER B PAN (the units' levels are fixed at unity: the slot budget, see below) */
    X_DATYPE, X_DAPAN, X_DALVL, /* X54..X56 182..184  DIRT A: TYPE (OFF SOFT HARD FOLD FUZZ CRUSH), PAN, LEVEL (127); its DRIVE is P_DRV2 (on CRUSH: the bit depth) */
    X_DBTYPE, X_DBDRV, X_DBPAN, X_DBLVL,   /* X57..X60 185..188  DIRT B: TYPE, DRIVE 0..127 (0), PAN, LEVEL */
    X_DSER,                     /* X61 189  DIRT ROUTE: PARAL / SERIES (PARAL) */
    X_MID, X_SIDE               /* X62 X63 190 191  MID and SIDE of the voice's output: 0..127 (64 = unity) */
};
/* NX is 64 at the most: the motion records name a target in one byte, the slots' wide ids 128 .. 128 + NX - 1 and the virtual targets 192 + the destination number (core.h MOTION_VBASE, 64 destinations,
 * mod.c MD_N 64) share it. So the voice has no slot for: the ring source (fixed OSC 1 x OSC 2), the levels of filter A / B, and the bit depth of the two dirt units (CRUSH's DRIVE is its depth);
 * widening that format is the owner's choice (PHASE43A_REPORT.md) */
_Static_assert(X_CYSYNC == 14 && X_LFO2LVL == 26 && X_STRUM == 27 && X_SIDE == NX - 1 && XWID(X_ROUTE) == 128 && XWID(X_STRUM) == 155 && XWID(X_SIDE) == 191, "ext slots: X0..X27 as DUELL 1.1, X28..X63 the L/RRY voice, wide ids 128..191");

/* --- the matrix: new destinations 51..55 and source 15 (mod.c keeps its own lists until job B appends them; append-only) --------- */
#define EXT_MD_CUT2   51        /* destination CUTOFF 2 (per note) */
#define EXT_MD_RES2   52        /* destination RES 2 */
#define EXT_MD_DRIVE  53        /* destination DRIVE */
#define EXT_MD_BLEND  54        /* destination BLEND */
#define EXT_MD_SUBLVL 55        /* destination SUB LEVEL */
#define EXT_MS_CYCLE  15        /* source CYCLE (the CYCLE envelope) */
/* L/RRY phase 4.3a: matrix destinations 57..63 (mod.c MD_O1PAN .. MD_DRVB; MD_N 64 is the format's limit: the virtual locks are one byte 192 + destination) */
#endif /* DUELL_EXT_H_CONST */

/* --- hooks (names declared; nothing defines them yet: job B stubs, jobs E and F fill) -------------------------------------------- */
#if defined(DUELL_EXT_TYPES_DONE) && !defined(DUELL_EXT_H_HOOKS)
#define DUELL_EXT_H_HOOKS

/* --- the five old ids that the TWIN FILTER takes over (TWIN_FILTER_DESIGN_2 3.1) ---
 * The plan and the designs write the numbers 25, 26, 42, 106, 113. In the source (core.h, counted from the enum) the first, second and
 * fourth are right: ROOT 25, SCALE 26, DKNOTE 106 (retired, free). The designs' "42 ALLOC" is ALLOC = 43 (42 is PRIO, in use) and their
 * "113 ANALOG filter TYPE" is FTYPE = 101 (113 is DKTHR, the DUCK THRESHOLD, in use). The names are what the designs mean. Job B (cloud/feat-values)
 * gave the ids their new names in the P_ enum itself (core.h): P_TWTYPE 43, P_CUT2 106, P_RES2 25, P_DRV2 26, P_BLEND 101; the old names
 * (P_ALLOC, P_DKNOTE, P_ROOT, P_SCALE, P_FTYPE) no longer exist, so nothing can read a retired meaning by mistake. */
_Static_assert(P_TWTYPE == 43 && P_CUT2 == 106 && P_RES2 == 25 && P_DRV2 == 26 && P_BLEND == 101, "ext.h: the five TWIN ids");
int32_t cycle_value(track_t *t, voice_t *v);   /* the CYCLE source for voice v (job F fills it; job B's stub returns 0) */
int twin_on(const track_t *t);                                   /* TWIN FILTER (job E, chain_f2.c) for the pages: TYPE is not OFF */
void twin_link_set(track_t *t, int on);                          /* LINK on / off, keeping the sound (CUTOFF 2 converted between OFFSET and CUTOFF with filter 1's knob) */
void twin_type_set(track_t *t, int type);                        /* TYPE (TT_*): refuses COMB on WAVE / PHYS; resets RES 2 and CUTOFF 2 to the new kind's defaults on entering or leaving COMB */
int32_t twin_graph_db(const track_t *t, int which, uint32_t hz); /* the graph: 1/16 dB of filter 1 (0), filter 2 (1) or the whole chain (2) at hz (twin_graph.c) */
void chain_sub(track_t *t, voice_t *v, int32_t *tmp, uint32_t n, const vmod_t *m);                 /* chain stage: SUB (job F) */
void chain_f2_pre(track_t *t, voice_t *v, int32_t *tmp, int32_t *dry, uint32_t n);                 /* chain stage: before filter 1, DRIVE in PARALLEL and the unfiltered signal kept in dry (job E) */
void chain_f2(track_t *t, voice_t *v, int32_t *tmp, int32_t *dry, uint32_t n, const vmod_t *m, int32_t uc);     /* chain stage: filter 2 and DRIVE (job E; dry is chain_f2_pre's) */
#endif
