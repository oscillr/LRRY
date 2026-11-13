/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Modulation matrix: per track eight slots (L/RRY; Felucca had four: slots 5..8 are on ids that were DIGITAL's, core.h PL_*), each SRC -> DST by AMT (-64..63), added to the fixed
 * routings (ENV DEST, LFO DEST). Runs in the audio ISR (fx.c mix_part, voice.c track_render).
 *
 * Sources, Q15:
 *   LFO   the track LFO with its FADE (as LFO DEST), bipolar; with LFO 2 POL UNI 0..1 (1.2)
 *   ENV   the amp envelope of the voice (after the engine's own amp curve, as ENV DEST), 0..1
 *   VEL   the note-on velocity (before UNISON's level scaling), 0..1
 *   KEY   the note relative to C4, bipolar: +-64 semitones = +-1 (an octave = 0.19)
 *   RAND  a new random value per note-on (its own generator: the shared rng is not touched), bipolar
 *   MODW  MIDI CC1, AT channel aftertouch, EXPR CC11 of the track's channel (seq.c), 0..1; until a
 *         controller arrives (and after CC121 RESET ALL CONTROLLERS) MODW and AT are 0, EXPR 1
 *   S&H   (#132) a random value held for one cycle of the track LFO: the value LFO WAVE S&H steps through
 *         (voice.c lfo_rand, drawn as each cycle starts, whatever the WAVE), without the LFO's FADE, POL or
 *         DEPTH; bipolar, 0 until the LFO's first cycle ends. It follows RATE and SYNC (a step per note value)
 *   SLEW  (#132) S&H slewed: it glides linearly from the cycle before's value to this cycle's over the cycle
 *         (a smooth random line, one cycle behind S&H, continuous where S&H steps); bipolar
 *   Both are per track (one value for every voice) and cost no state but the one value kept per cycle.
 *   LFO2  (L/RRY) the track's LFO 2 (voice.c lfo_tick: LFO 1's code, RATE2 WAVE2 PHS2 FADE2 SYNC2 TRIG2 POL2), as LFO;
 *         its own S&H draws (lfo2_rand), no S&H / SLEW source of its own. It runs only while an active slot reads it (mod.l2)
 *   MORPH (L/RRY 4b) the part's MORPH fader (morph.c), 0..1 = fader 0..127, one value per part (in BOTH both faders
 *         move together); read from t->mf, which morph_begin sets before each block of the part. No feedback: the fader is no parameter (no destination reaches it), the morph never rolls a
 *         route from MORPH nor a route to it (morph.c morph_slot), and the morph's blend is written before the matrix
 *         runs (fx.c mix_block: morph_begin, then mix_part's mod_begin), which reads the fader only
 *   ENV2  (L/RRY) the voice's ENV 2 (voice.c env2_tick: ATK2 DEC2 SUS2 REL2, ENV SYNC as ENV's; no engine curve), 0..1.
 *         It runs only while an active slot reads it (mod.e2), so it costs nothing and changes nothing unread
 * Destinations (L/RRY phase 51: the list is the L/RRY voice's, the enum MD_* below says what each number moves; the text that follows is L/RRY's and names some that L/RRY no longer has):
 *   per voice (vmod_t, like the fixed routings):
 *     PITCH  AMT 63 at a full source = +-11.8 semitones (as LFO DEST PIT)
 *     CUT    m->cutoff, SHP m->shape: AMT 63 = +-63 steps (as ENV DEST); the sums are clamped to what
 *            the fixed routings can reach, so the engines see no new range
 *     AMP    a gain <= 1: AMT > 0 the source opens it (AMT 64: silent at 0, full at 1), AMT < 0 closes it
 *            (bipolar sources as 0..1, as LFO DEST AMP); several AMP slots multiply
 *   per track, once per block: PAN, DIST CHO DLY REV (the sends), RATE (LFO rate), VIB (LFO DEST PIT, the
 *     vibrato depth), E1..E8 (the engine's parameters P_E0..P_E7, named by the engine): AMT 64 at a full
 *     source = the parameter's whole range (a bipolar source swings +-half of it around the base value, so its full swing
 *     is the whole range: a sine LFO at AMT 63 sweeps PAN from full left to full right; AMT < 0 inverts the phase); the sum is clamped to its range
 *   DEPTH (#132, per track): the depth of the track LFO, a gain <= 1 on it as AMP is on the voice (AMT > 0 the source
 *     opens it, AMT 64: none at 0, full at 1; AMT < 0 closes it; several DEPTH slots multiply): it scales the LFO
 *     wherever it goes, the four LFO DEST routings and the matrix's LFO source (not S&H / SLEW). With RATE (the LFO's
 *     rate) the matrix modulates the modulator: MODW -> DEPTH is a mod-wheel LFO, ENV -> DEPTH an LFO that
 *     follows the note's envelope. A DEPTH slot whose source is the LFO takes it unscaled
 *   RATE2, DEPTH2 (L/RRY): LFO 2's rate (P_L2RATE, per block as RATE) and depth (as DEPTH, on the LFO2 source). DEPTH and
 *     DEPTH2 read both LFOs unscaled, RATE / RATE2 move the rate from the next tick: LFO to LFO in a fixed order, a block
 *     late, no loop. (No feedback anywhere: every source a per-block destination reads is the value the block before left
 *     (LFO 1 / 2, ENV, ENV 2, DUCK), so a source on its own LFO's RATE / DEPTH or ENV 2 on its own times is heard a block
 *     later, never within the block that computes it)
 *   L/RRY 0.2.7, per block as PAN: LEVEL (the part's), the INSERT's A B C and MIX, the EQ's LOW MID HIGH gains, DUCK DEPTH,
 *     ENV 2's ATK DEC SUS REL and its FLT / SHP depths, RES (the multimode filter's: the engines with no filter of their
 *     own). The bus values CHORUS RATE / DEPTH, DELAY FDBK, the delay modulation's DEPTH / RATE, REVERB SIZE / DAMP: an
 *     offset per part on a global value (mod_bus: in DUEL each part moves its own bus, in STEREO / LAYER the parts add).
 *     An engine slot the matrix cannot move (mod_e_why: unused, WAVE's TABLE, FM6's SLOT, read only at a note-on) does
 *     nothing (TABLE is skipped, the rest is never read in the block); the MATRIX page shows it faded with the reason
 * A per-voice source on a per-block destination takes the latest note-on's value (VEL, KEY, RAND), ENV the
 * envelope of the latest note-on's voice (one block late, 0 when it has ended).
 *
 * Per-block destinations are modulated in place: mod_begin writes the modulated values into t->p for the
 * part's block (its render and its mix in fx.c mix_part) and mod_end puts the stored values back. Both run
 * in the audio ISR, so nothing outside it ever sees a modulated value (the UI, the editor, projects and
 * presets read and write t->p from the main loop, which the ISR preempts; TIMER5 only fills the MIDI rings),
 * and the engines read t->p as before. An engine reads its parameters in note_on (events_block) unmodulated.
 * Nothing active (every slot with SRC, DST or AMT at 0): no work, and the sound is bit-identical. */
enum { MS_OFF, MS_LFO, MS_ENV, MS_VEL, MS_KEY, MS_RAND, MS_MODW, MS_AT, MS_EXPR, MS_SH, MS_SLEW,
       MS_ENV2,                                   /* L/RRY: ENV 2 of the voice (voice.c env2_tick), 0..1 */
       MS_LFO2,                                   /* L/RRY: LFO 2 of the track (voice.c lfo_tick), as MS_LFO */
       MS_MORPH,                                  /* L/RRY 4b: the part's MORPH fader (t->mf), 0..1 */
       /* (L/RRY phase 51: L/RRY's DUCK source, 14, is removed: L/RRY has no ducker (the DUCK pages are hidden and nothing can switch one on), so it read 0 always; CYCLE, BREATH and FOOT moved
        * down by one. No backward compatibility before the alpha: a stored source number of before plays the source now at that number) */
       MS_CYCLE,                                  /* cloud job B: the voice's CYCLE envelope (job F: cycle_value), 0..1; read through the second list (mod_cycle_voice, mod_f2_voice): mod_voice does not know it */
       MS_BREATH, MS_FOOT,                        /* L/RRY 1.1 (owner): MIDI CC2 (BREATH CONTROLLER) and CC4 (FOOT CONTROLLER) of the part's channel, 0..127 as the mod wheel (0..1), appended */
       MS_N };
/* L/RRY phase 51 (owner: "populate the matrix the way L/RRY does; remove the targets that do not exist; pan a major focus"; LRRY/PHASE51_MATRIX_REPORT.md): the destinations of the L/RRY voice.
 * Every number that moved something L/RRY does not have (TIMBRE's engine shape input, the SATURATOR, the three sends, the engine's eight values, the INSERT, the EQ, the DUCK, the effect buses, SPACE,
 * ENV 2's retired TIMBRE depth, the retired SIDE) was freed and given an L/RRY value; the numbers of the destinations L/RRY kept did not move. The stored format stays one byte, 64 destinations (the
 * motion records' virtual targets 192 + the number, core.h MOTION_VBASE; lk_virt). Per voice (vmod_t): PITCH, MULTI CUTOFF, AMP LEVEL (MD_PER_VOICE). Every other one is per block, as PAN: mod_begin writes
 * the part's stored value t->p[id] for the block, lrry_decode (lrry_param.c) reads it once a block, mod_end puts it back. The knob shows them by group (params.c MDST_ORDER, ui_graph.c MDST_GRP).
 * Not offered (mod_dst_removed): L/RRY's retired LFO master depths 20 and 22 (the LFO LEVEL cards replaced them), and the spare numbers 42..45, 56, 62 (a stored route to one does nothing) */
enum { MD_OFF, MD_PITCH, MD_CUT,
       MD_TIMBRE,                                 /*  3 TIMBRE: OSC 1 and OSC 2's first model control (P1: PULSE WIDTH, SYNC RATIO, SUPERSAW DETUNE, FM RATIO, FOLD AMOUNT), per block (was L/RRY's engine shape input) */
       MD_AMP, MD_PAN,                            /*  4 AMP LEVEL (per voice), 5 PAN (the part's) */
       MD_NZPAN, MD_RGPAN, MD_DAPAN, MD_DBPAN,    /*  6..9 NOISE PAN, RING PAN, DIRT A PAN, DIRT B PAN (were SATURATION and the three sends) */
       MD_RATE, MD_VIB,                           /* 10 LFO 1 RATE, 11 LFO 1 DEPTH (the pitch's) */
       MD_O1P1, MD_O1P2, MD_O1P3, MD_O2P1, MD_O2P2, MD_O2P3,   /* 12..17 OSC 1 and OSC 2's model controls P1 P2 P3, named by the MODEL (were the engine's slots E1..E6) */
       MD_O1LVL, MD_O2LVL,                        /* 18, 19 OSC 1 LEVEL, OSC 2 LEVEL (were E7, E8) */
       MD_DEPTH,                                  /* 20 L/RRY's retired LFO 1 master depth (not offered; the code stays in the mod_depth readers) */
       MD_RATE2, MD_DEPTH2,                       /* 21 LFO 2 RATE, 22 L/RRY's retired LFO 2 master depth (not offered) */
       MD_LEVEL,                                  /* 23 PATCH LEVEL (P_TRIM) */
       MD_NZLVL, MD_RGLVL,                        /* 24, 25 NOISE LEVEL, RING LEVEL (were INSERT A, B) */
       MD_O1CRS, MD_O1FIN, MD_O2CRS, MD_O2FIN,    /* 26..29 OSC 1 COARSE, OSC 1 FINE, OSC 2 COARSE, OSC 2 FINE (were INSERT C, INSERT MIX, EQ LOW, EQ MID) */
       MD_DALVL, MD_DBLVL,                        /* 30, 31 DIRT A LEVEL, DIRT B LEVEL (were EQ HIGH, DUCK DEPTH) */
       MD_E2ATK, MD_E2DEC, MD_E2SUS, MD_E2REL, MD_E2FLT,   /* 32..36 FILTER ENV ATTACK DECAY SUSTAIN RELEASE, FILTER ENV MULTI CUTOFF */
       MD_O1BAL,                                  /* 37 OSC 1 BALANCE (the A / B filter balance of the source; was FILTER ENV TIMBRE, retired) */
       MD_RES,                                    /* 38 MULTI RES */
       MD_O2BAL, MD_NZBAL, MD_RGBAL,              /* 39..41 OSC 2 BALANCE, NOISE BALANCE, RING BALANCE (were CHORUS RATE, CHORUS DEPTH, DELAY FEEDBACK) */
       MD_SPARE42, MD_SPARE43, MD_SPARE44, MD_SPARE45,   /* 42..45 spare (were the delay modulation's DEPTH and RATE, REVERB SIZE, REVERB DAMPING): not offered */
       MD_FMODE,                                  /* 46 MULTI MODE (P_FREEC, L/RRY's 0..127 blend: the matrix moves it smoothly) */
       MD_AATK, MD_ADEC, MD_ASUS, MD_AREL,        /* 47..50 AMP ENV ATTACK DECAY SUSTAIN RELEASE: voice.c esync_coef reads the block's values, the envelope stays continuous */
       MD_CUT2,                                   /* 51 TWIN CUTOFF (per note: mod_f2_voice) */
       MD_RES2,                                   /* 52 TWIN RES */
       MD_DRIVE,                                  /* 53 DIRT A DRIVE (P_DRV2: L/RRY's FILTER DRIVE; on CRUSH the bit depth) */
       MD_BLEND,                                  /* 54 FILTER BLEND (added to every source's A / B balance) */
       MD_SUBLVL,                                 /* 55 SUB LEVEL */
       MD_SPARE56,                                /* 56 spare (was SPACE): not offered */
       MD_O1PAN, MD_O2PAN, MD_FAPAN, MD_FBPAN, MD_SUBPAN,   /* 57..61 OSC 1 PAN, OSC 2 PAN, MULTI FILTER PAN, TWIN FILTER PAN, SUB PAN */
       MD_SPARE62,                                /* 62 spare (was SIDE): not offered */
       MD_BDRV,                                   /* 63 DIRT B DRIVE (on CRUSH the bit depth) */
       MD_N };
#define MD_PER_VOICE(d) ((d) == MD_PITCH || (d) == MD_CUT || (d) == MD_AMP)   /* the destinations each voice sums for itself (vmod_t); every other one is per block */
#define MD_SPARE(d) (((d) >= MD_SPARE42 && (d) <= MD_SPARE45) || (d) == MD_SPARE56 || (d) == MD_SPARE62)
#define MD_OSCP(d) ((d) >= MD_O1P1 && (d) <= MD_O2P3)  /* an oscillator's model control: its name and whether it acts follow the MODEL */
#define MD_N_026 (MD_DEPTH2 + 1)                  /* 0.2.6's destinations: the morph's WILD roll picks among these only (morph.c) */
#define MG_NB 7                                   /* L/RRY's bus values in song.g (the effects' CHORUS, DELAY, REVERB values): L/RRY has no destination for them (the effects are hidden), mod_bus has nothing to write */
#define MG_N (MG_NB + 1)
_Static_assert(NELEM(N_MSRC) == MS_N && NELEM(N_MDST) == MD_N, "N_MSRC / N_MDST == MS_* / MD_*");
_Static_assert(MD_TIMBRE == 3 && MD_DBPAN == 9 && MD_O1P1 == 12 && MD_O2LVL == 19 && MD_LEVEL == 23 && MD_DBLVL == 31 && MD_O1BAL == 37 && MD_RES == 38 && MD_RGBAL == 41 && MD_FMODE == 46 && MD_AATK == 47 &&
               MD_SPARE56 == 56 && MD_O1PAN == 57 && MD_BDRV == 63 && MD_N == 64 && MD_N <= 64, "matrix DST: the L/RRY numbers (phase 51); 64 at most (one byte with the motion targets)");
_Static_assert(MD_CUT2 == EXT_MD_CUT2 && MD_RES2 == EXT_MD_RES2 && MD_DRIVE == EXT_MD_DRIVE && MD_BLEND == EXT_MD_BLEND && MD_SUBLVL == EXT_MD_SUBLVL && MS_CYCLE == EXT_MS_CYCLE && MS_BREATH == 15 && MS_FOOT == 16 && MS_N == 17,
               "ext.h: the matrix numbers 51..55 and source 14");
_Static_assert(P_DEC == P_ATK + 1 && P_SUS == P_ATK + 2 && P_REL == P_ATK + 3, "the AMP ENV times in a row");
#define MSLOT_ID(k) (P_M1SRC + 3u * (k))                         /* slot k's SRC; DST + 1, AMT + 2 */
_Static_assert(P_M2SRC == P_M1SRC + 3 && P_M4AMT == P_M1SRC + 11 && P_M5SRC == P_M4AMT + 1 && P_M8AMT == MSLOT_ID(NMSLOT) - 1,
               "matrix slots: 3 ids each, 1..8 in a row");
/* L/RRY 1.1 (owner, the DEST pages): the per-source depth cards are VIRTUAL ROUTES. Each is a stored depth (an extension slot) that mod_begin treats exactly as an active matrix slot with the fixed
 * source and destination below and the depth as its amount (MX_AMT, the same units): the same source value, the same slew (mx_slew), the same smooth per-block write of the destination
 * (t->p[...] and put back by mod_end), the same per-note lists (f2m / cym -> vrm). They do not use a matrix slot. Route index k = NMSLOT + 2 + i (NMSLOT and NMSLOT + 1 are the LFOs' own slews) */
typedef struct { uint8_t src, dst, id; } vr_t;    /* id: the stored depth (a core id or an extension slot's wide id: every one fits the byte) */
static const vr_t VR[] = {
    {MS_ENV2, MD_BLEND, XW_BASE + X_E2BLD}, {MS_ENV2, MD_DRIVE, XW_BASE + X_E2DADRV}, {MS_ENV2, MD_RES, XW_BASE + X_E2RES},   /* FILTER ENV DEST: FILTER BLEND, DIRT A DRIVE (L/RRY's FILTER DRIVE), MULTI RES */
    {MS_ENV, MD_BDRV, XW_BASE + X_EDBDRV},                                                     /* AMP ENV DEST: DIRT B DRIVE (L/RRY phase 51: L/RRY's SATURATION, the last stage, is DIRT B) */
    {MS_CYCLE, MD_CUT2, XW_BASE + X_CYCUT2}, {MS_CYCLE, MD_O1PAN, XW_BASE + X_CYPAN}, {MS_CYCLE, MD_PITCH, XW_BASE + X_CYPIT},   /* CYCLE ENV 2/2: TWIN CUTOFF, OSC 1 PAN (L/RRY: was the part's PAN; the card stays printed PAN), PITCH (the first and the last per note: vrm) */
    {MS_LFO2, MD_RES2, XW_BASE + X_L2RES2}, {MS_LFO2, MD_FAPAN, XW_BASE + X_L2FAPAN}, {MS_LFO2, MD_BLEND, XW_BASE + X_L2BLD},   /* LFO 2 DEST: TWIN RES, MULTI FILTER PAN (phase 51: was MULTI MODE), FILTER BLEND */
    {MS_ENV, MD_TIMBRE, P_ED_SHP}, {MS_LFO, MD_TIMBRE, P_LD_SHP},                               /* L/RRY phase 51: AMP DEST's and LFO 1 DEST's TIMBRE (L/RRY's direct shape depths; the voice has no shape input) */
};
#define VR_N (sizeof VR / sizeof VR[0])
#define MR_N (NMSLOT + 2u + VR_N)                 /* route indices: the 8 slots, 2 reserved for the LFOs' slews, the virtual routes */
_Static_assert(MR_N <= 24u, "mod.c: the active mask is a uint32_t and bits 8, 9 are the LFOs'");
/* route k's source, destination and stored amount (the slot's, or the card's); 1 = active (all three set) */
static __attribute__((noinline)) int mr_get(const int16_t *p, uint32_t k, uint32_t *s, uint32_t *d, int32_t *a)
{
    if (k < NMSLOT) {
        *s = (uint32_t)p[MSLOT_ID(k)];
        *d = (uint32_t)p[MSLOT_ID(k) + 1u];
        *a = p[MSLOT_ID(k) + 2u];
        return *s && *d && *a;
    }
    k -= NMSLOT + 2u;
    *s = VR[k].src;
    *d = VR[k].dst;
    *a = p[VR[k].id];
    return *a != 0;
}
#define MS_VOICE(s) (((s) >= MS_ENV && (s) <= MS_RAND) || (s) == MS_ENV2 || (s) == MS_CYCLE)   /* a value per voice */
#define MS_BIPOLAR(s) ((s) == MS_LFO || (s) == MS_KEY || (s) == MS_RAND || (s) == MS_SH || (s) == MS_SLEW || \
                       (s) == MS_LFO2)                           /* (ENV2 is 0..1, as ENV) */
/* the kind source s gains as (mod_gain): a UNI LFO (POL), 0..1, as ENV does */
#define MS_GAIN(p, s) (((s) == MS_LFO && (p)[P_LPOL]) || ((s) == MS_LFO2 && (p)[P_L2POL]) ? MS_ENV : (s))

/* L/RRY phase 51: what each OSC MODEL has of P1 P2 P3 (params.c LPD, lrry_param.c): SAW TRIANGLE SINE none, PULSE SYNC FOLD P1, SUPERSAW P1 P2 P3, FM P1 P2 */
static const uint8_t MOD_NP[8] = {0, 0, 0, 1, 1, 3, 2, 1};
#define MOD_MODEL(t, o2) ((uint32_t)clamp(XV(t, (o2) ? X_O2MODEL : X_O1MODEL), 0, 7))
#define MOD_SUPER 5u                                    /* the SUPERSAW MODEL (lrry_voice.c LM_SUPER) */

/* the track parameter of a per-block destination (TIMBRE's first; its second, OSC 2's P1, in mod_begin) */
static uint32_t mod_param(uint32_t d)
{
    static const uint8_t ID[MD_N] = {[MD_TIMBRE] = XW_BASE + X_O1P1, [MD_PAN] = P_PAN,
                                       [MD_NZPAN] = XW_BASE + X_NZPAN, [MD_RGPAN] = XW_BASE + X_RGPAN, [MD_DAPAN] = XW_BASE + X_DAPAN, [MD_DBPAN] = XW_BASE + X_DBPAN,
                                       [MD_RATE] = P_LRATE, [MD_VIB] = P_LD_PIT, [MD_RATE2] = P_L2RATE,
                                       [MD_O1P1] = XW_BASE + X_O1P1, [MD_O1P2] = XW_BASE + X_O1P2, [MD_O1P3] = XW_BASE + X_O1P3,
                                       [MD_O2P1] = XW_BASE + X_O2P1, [MD_O2P2] = XW_BASE + X_O2P2, [MD_O2P3] = XW_BASE + X_O2P3,
                                       [MD_O1LVL] = XW_BASE + X_O1LVL, [MD_O2LVL] = XW_BASE + X_O2LVL, [MD_NZLVL] = XW_BASE + X_NZLVL, [MD_RGLVL] = XW_BASE + X_RGLVL,
                                       [MD_LEVEL] = P_TRIM,   /* 0.2.19: the matrix is the sound's: DST TRIM moves the PATCH TRIM, not the player's mixer level */
                                       [MD_O1CRS] = XW_BASE + X_O1COARSE, [MD_O1FIN] = XW_BASE + X_O1FINE, [MD_O2CRS] = XW_BASE + X_O2COARSE, [MD_O2FIN] = XW_BASE + X_O2FINE,
                                       [MD_DALVL] = XW_BASE + X_DALVL, [MD_DBLVL] = XW_BASE + X_DBLVL,
                                       [MD_E2ATK] = P_E2ATK, [MD_E2DEC] = P_E2DEC, [MD_E2SUS] = P_E2SUS, [MD_E2REL] = P_E2REL, [MD_E2FLT] = P_E2FLT,
                                       [MD_O1BAL] = XW_BASE + X_O1BAL, [MD_RES] = P_FREEB, [MD_O2BAL] = XW_BASE + X_O2BAL, [MD_NZBAL] = XW_BASE + X_NZBAL, [MD_RGBAL] = XW_BASE + X_RGBAL,
                                       [MD_FMODE] = P_FREEC, [MD_AATK] = P_ATK, [MD_ADEC] = P_DEC, [MD_ASUS] = P_SUS, [MD_AREL] = P_REL,   /* (T0229: one table, no branches for the newest) */
                                       [MD_CUT2] = P_CUT2, [MD_RES2] = P_RES2, [MD_DRIVE] = P_DRV2, [MD_BLEND] = P_BLEND, [MD_SUBLVL] = XW_BASE + X_SUBLVL,
                                       [MD_O1PAN] = XW_BASE + X_O1PAN, [MD_O2PAN] = XW_BASE + X_O2PAN, [MD_FAPAN] = XW_BASE + X_FAPAN, [MD_FBPAN] = XW_BASE + X_FBPAN,
                                       [MD_SUBPAN] = XW_BASE + X_SUBPAN, [MD_BDRV] = XW_BASE + X_DBDRV};   /* (CUTOFF 2 is per note, mod_begin leaves it to mod_f2_voice; the rest per block; wide ids fit the byte; the spares 0) */
    return d < MD_N ? ID[d] : 0u;
}

/* TIMBRE (L/RRY phase 51): the first model control of OSC 1 and OSC 2. 1 = it acts on neither: both MODELs are SAW, TRIANGLE or SINE (no P1). A value (a LEVEL of 0) is never a reason.
 * The TIMBRE depth cards (AMP DEST, LFO 1 DEST) hide then, and the matrix's TIMBRE says MODEL (L/RRY 0.2.25's hide rule for the shape input, by the model in place of the engine's wave) */
static __attribute__((noinline)) int mod_shp_inert(const track_t *t)
{
    return !MOD_NP[MOD_MODEL(t, 0)] && !MOD_NP[MOD_MODEL(t, 1)];
}
/* an oscillator's model control d (MD_O1P1 .. MD_O2P3): 1 = the MODEL has no such control (its card on the OSC 1/2 page hides) */
static int mod_oscp_inert(const track_t *t, uint32_t d)
{
    uint32_t o2 = d >= MD_O2P1;
    return d - (o2 ? MD_O2P1 : MD_O1P1) >= MOD_NP[MOD_MODEL(t, o2)];
}
/* NOT OFFERED (the DEST knob and the editor skip it; a stored route to one stays where it is). L/RRY's rule (owner, 2026-10-18 and 2026-10-21): the LFO master depths 20 and 22 (the LFO LEVEL cards
 * replaced them; the code stays in the mod_depth readers) and nothing that has no sound; L/RRY phase 51: the spare numbers (a stored route to one does nothing). eng: unused in L/RRY (one sound source) */
static __attribute__((noinline)) int mod_dst_removed(uint32_t eng, int32_t d)
{
    (void)eng;
    return d == MD_DEPTH || d == MD_DEPTH2 || MD_SPARE(d);
}

/* L/RRY phase 51: no destination reaches a bus value or SPACE any more (the effects are hidden; their numbers are L/RRY values now), so nothing writes mod_goff and mgon stays 0: mod_bus,
 * mod_bus_end, mod_space_off and mod_gon_any below are L/RRY's, kept for fx.c's calls, and have nothing to do. L/RRY's text:
 * The bus values (L/RRY 0.2.7): CHORUS RATE / DEPTH, the delay's FDBK and its modulation's DEPTH / RATE, REVERB SIZE / DAMP
 * are global parameters (song.g, one per instrument) while the matrix is a part's. Each part's routes make an OFFSET in the
 * value's own units (AMT 64 at a full source = the whole range, as on the per-block destinations), kept per part
 * (mod_goff, mod_begin). mod_bus writes stored value + the offsets of the parts a bus carries into song.g just before the
 * buses run (fx.c mix_block), clamped to the range, and mod_bus_end puts the stored values back after them: DUEL (SPLIT) has
 * a bus per part, so part 1's routes move bus A's value and part 2's bus B's, never the other's; STEREO and LAYER share one
 * set of buses, so both parts' offsets ADD. As with t->p, all of it runs inside the audio ISR: the UI, the editor and the
 * stores never see a modulated value. Not a bus value: the delay's TIME (a note value: a step moves the read point, a
 * click; the delay modulation's DEPTH is the smooth wobble) */
static const uint8_t MG_ID[MG_NB] = {G_CRATE, G_CDEPTH, G_DFDBK, G_DMDEP, G_DMRATE, G_RSIZE, G_RDAMP};
static int16_t mod_goff[NLIVE][MG_N];    /* per part, its offsets (mod_begin) */
/* per part, in track_t: mgon (0 / 1) an offset is not 0, mdk (0 / 1) an active slot moves DUCK DEPTH (fx.c duck_pump / duck_note let it start) */
static int16_t mod_gkeep[MG_NB];          /* the stored values mod_bus wrote over */
static uint8_t mod_gw;                   /* bit per bus value: written (mod_bus_end puts it back) */
static __attribute__((noinline)) void mod_bus(uint32_t mask)       /* the offsets of the parts in mask (bit per part) onto song.g */
{
    uint32_t g, p;
    for (g = 0; g < MG_NB; g++) {
        int32_t o = 0;
        const param_desc_t *gd = &GP[MG_ID[g]];
        for (p = 0; p < NLIVE; p++)
            o += (int32_t)((mask >> p) & 1u) * mod_goff[p][g];
        if (!((mod_gw >> g) & 1u)) {
            if (!o)
                continue;
            mod_gkeep[g] = song.g[MG_ID[g]];
            mod_gw |= (uint8_t)(1u << g);
        }
        song.g[MG_ID[g]] = (int16_t)clamp(mod_gkeep[g] + o, gd->min, gd->max);
    }
}
static __attribute__((noinline)) void mod_bus_end(void)
{
    uint32_t g;
    for (g = 0; g < MG_NB; g++)
        if ((mod_gw >> g) & 1u)
            song.g[MG_ID[g]] = mod_gkeep[g];
    mod_gw = 0;
}
/* SPACE's offset (L/RRY 1.1): the sum of both parts' routes to SPACE, in SPACE's own units (-100 .. +100 around PERFORM's value); fx.c space_goal adds it to perf_space */
static int32_t mod_space_off(void)
{
    uint32_t p;
    int32_t o = 0;
    for (p = 0; p < NLIVE; p++)
        o += mod_goff[p][MG_NB];
    return clamp(o, -200, 200);
}
/* any part's bus offsets are not 0 (the send stage: it reads both parts', after the parts) */
static uint32_t mod_gon_any(void)
{
    uint32_t p, a = 0;
    for (p = 0; p < NLIVE; p++)
        a |= trk[p].mgon;
    return a;
}
/* L/RRY DUCK (fx.c duck_*): the part's sidechain envelope; dn = how far the gain is down, Q16 (0 = full level, 65536 = silent), ph 0 idle (dn 0), 1 going down, 2 held,
 * 3 coming back, 4 following the PART key (SRC PART); cnt = the hold's samples left; SRC PART: key = this part's level
 * follower (Q: x of mix_part), tg = the reduction it asks of the part now (Q16), on = the other part's key is over THR;
 * SRC PUMP: pos = samples since the last pump, run = it has fired (0: fires at the next tick) */
typedef struct { int32_t dn, key, tg; uint32_t cnt, pos; uint8_t ph, on, run; } duck_t;
static duck_t duck_st[NTRK];
static uint32_t mod_seed = 0x2545F491u;
static int16_t mod_rand(void)    /* RAND of a note-on: xorshift32, bipolar */
{
    mod_seed ^= mod_seed << 13;
    mod_seed ^= mod_seed >> 17;
    mod_seed ^= mod_seed << 5;
    return (int16_t)(mod_seed >> 16);
}

static int32_t mod_key(uint32_t note) { return clamp(((int32_t)note - 60) * 512, -32767, 32767); }

/* S&H: the value of the LFO's cycle (voice.c lfo_rand; 0 before its first one ends), bipolar */
static int32_t mod_sh(const track_t *t) { return t->lfo_rnd ? (int32_t)(t->lfo_rnd >> 16) - 32768 : 0; }

/* L/RRY 1.1: CC2 (BREATH CONTROLLER, [0]) and CC4 (FOOT CONTROLLER, [1]) of each part, 0..127; 0 until a controller comes (mod_midi), CC121 resets them. Outside track_t (its size and offsets stay) */
static uint8_t mod_cc[2][NTRK];
/* source s of the track: the per-track ones, the per-voice ones of the latest note-on */
static int32_t mod_tsrc(const track_t *t, uint32_t s, int32_t lfo)
{
    switch (s) {
    case MS_SH:
        return mod_sh(t);
    case MS_SLEW:                                       /* the cycle before's value .. this one's, by the phase */
        return t->lfo_prev + (((mod_sh(t) - t->lfo_prev) * (int32_t)(t->lfo_ph >> 17)) >> 15);
    case MS_LFO:
        return lfo;
    case MS_LFO2:
        return MODW(t).lfo2;
    case MS_ENV:
        return t->m_env;
    case MS_ENV2:
        return t->m_env2;
    case MS_CYCLE:                                      /* the latest note's CYCLE, last block (mod_cycle_voice) */
        return t->m_cycle;
    case MS_VEL:
        return t->m_vel * 258;
    case MS_KEY:
        return mod_key(t->m_key);
    case MS_RAND:
        return t->m_rnd;
    case MS_MODW:
        return t->mw * 258;
    case MS_AT:
        return t->at * 258;
    case MS_BREATH:                                     /* (L/RRY 1.1: CC2 and CC4, the part's, in mod_cc below) */
        return mod_cc[0][(uint32_t)(t - trk) % NTRK] * 258;
    case MS_FOOT:
        return mod_cc[1][(uint32_t)(t - trk) % NTRK] * 258;
    default:                                            /* EXPR; (4b) MORPH: t->mf, the fader morph_begin set for this block */
        return (s == MS_MORPH ? t->mf : 127 - t->ex_off) * 258;
    }
}

/* MX_AMT (the +63 = 64 rule of the matrix amount) is in core.h: the direct depth cards use it too (voice.c, chain_f2.c) */

/* AMP: the gain (Q15, 0..32767) of source x (bipolar: as 0..1) at amount a */
static int32_t mod_gain(uint32_t s, int32_t x, int32_t a)
{
    int32_t u = MS_BIPOLAR(s) ? (x + 32768) >> 1 : x;
    return clamp(32767 - (a > 0 ? ((32767 - u) * a) >> 6 : (u * -a) >> 6), 0, 32767);
}

/* a per-voice destination d by source value x, amount a */
static void mod_voice_add(uint32_t s, uint32_t d, int32_t x, int32_t a, int32_t *pit, int32_t *cut, int32_t *shp,
                          int32_t *gain)
{
    if (d == MD_PITCH)
        *pit += (x * a * 3) >> 15;
    else if (d == MD_CUT)
        *cut += (x * a) >> 7;
    else                                                /* (AMP: MD_PER_VOICE has only these three; L/RRY phase 51: TIMBRE is per block, the shape sum stays 0) */
        *gain = mulq15(*gain, mod_gain(s, x, a));
}

/* SLEW (L/RRY 0.2.25, owner's matrix round; release_drafts/MATRIX_DESTINATIONS.md): each slot's source value as mod_begin reads it (a per-track source, or a
 * per-voice one's latest note for a per-block destination) moves at most MX_SLEW a block: a jump (an S&H step, a square or saw LFO's edge, an LFO restarted by a
 * note, a MIDI CC step, a new note's VEL / KEY / RAND) becomes a glide of at most 5 blocks (3.6 ms) for a unipolar full swing, 10 (7.3 ms) for a bipolar one,
 * instead of a step at one block boundary (a click on every destination, the filter's cutoff the worst). An LFO or an envelope that moves less than that a
 * block passes unchanged: a full-depth sine up to RATE's top (40 Hz), every synced rate but 32T above 200 BPM; an attack of 4 ms or longer. (The study's 6-block
 * setting rounded LFOs above 36 Hz.) A slot that was not active the block before takes its value at once
 * (nothing to glide from), and so does a slot whose SRC changed. Per part and slot, 80 bytes; flash code (not hot) */
#ifndef MX_SLEW
#define MX_SLEW 6554                              /* 32767 / 5, Q15 of a unipolar full swing a block: LFO RATE's top (40 Hz, a full sine moves 5976 a block) passes */
#endif
typedef struct {
    int32_t v[MR_N];                              /* the slewed value of the block before; [NMSLOT], [NMSLOT + 1]: LFO 1's and LFO 2's value for their own depths (voice.c track_render); [NMSLOT + 2 ..]: the DEST cards' virtual routes */
    uint8_t src[MR_N];                            /* .. of this SRC (another SRC in the slot: its value at once) */
    uint32_t ok;                                  /* bit per slot (and 8, 9 the two LFOs): v holds the block before's value */
} mx_sl_t;
/* (the matrix round's state in a section of its own, .bss.mxr: the compiler's global merge keeps the layout of the variables it had, so the hot RAM
 * code (tools/hot_fns.py) compiles as before) */
#define MXR_BSS __attribute__((section(".bss.mxr")))
static mx_sl_t mx_sl[NLIVE] MXR_BSS;
/* (join0233: the part's state is found from t, not through a global pointer: on two cores part 2's mod_begin runs on core 1 at the same time as part 1's on core 0) */
static __attribute__((noinline)) int32_t mx_slew(const track_t *t, uint32_t k, int32_t x)
{
    uint32_t pi = (uint32_t)(t - trk);
    mx_sl_t *c = pi < NLIVE ? &mx_sl[pi] : 0;
    int32_t v, d;
    if (!c)
        return x;
    if (!((c->ok >> k) & 1u)) {
        c->ok |= 1u << k;
        c->v[k] = x;
        return x;
    }
    v = c->v[k];
    d = x - v;
    v += d > MX_SLEW ? MX_SLEW : d < -MX_SLEW ? -MX_SLEW : d;
    c->v[k] = v;
    return v;
}
/* per part (0 / 1): an active slot moves the part's PAN, PATCH LEVEL or a send this block, or the part's MORPH window is open (hunt1) (fx.c part_mix ramps them inside the block, mix_ramp).
 * A byte for each part, not a bit of one shared byte (join0233): the two parts' mod_begin may run on two cores, and a bit written by a read-modify-write
 * of a shared byte would lose the other core's write */
static uint8_t mod_mixmv[NLIVE] MXR_BSS;
#define MD_MIXED(d) ((d) == MD_PAN || (d) == MD_LEVEL)   /* (L/RRY phase 51: the sends are no destination) */

/* DEPTH: the track LFO's gain (Q15) from the DEPTH slots, 32767 = none (lfo: the LFO unscaled; a per-voice source
 * takes the latest note's value, as on the other per-block destinations). Its own function: mod_begin's loop stays
 * what it was */
static __attribute__((noinline)) int32_t mod_depth(const track_t *t, int32_t lfo, uint32_t dep)   /* dep: the DEPTH slots */
{
    const int16_t *p = t->p;
    int32_t g = 32767;
    for (; dep; dep &= dep - 1u) {                      /* (in slot order) */
        uint32_t k = (uint32_t)__builtin_ctz(dep), s = (uint32_t)p[MSLOT_ID(k)];
        if (s < MS_N)
            g = mulq15(g, mod_gain(MS_GAIN(p, s), mx_slew(t, k, mod_tsrc(t, s, lfo)), MX_AMT(p[MSLOT_ID(k) + 2u])));
    }
    return g;
}

/* L/RRY 0.2.24 (owner): the matrix stays PACKED: a slot is used while its SRC is not OFF; when a SRC is set to OFF (the knob, the editor) or a sound loads with gaps, the used
 * slots move up in their order (a stable shift: mod_begin reads them in slot order, so the sum, the per-voice list and the AMP / DEPTH products come out the same), the slots
 * after them are cleared (SRC DST AMT 0); there is never an empty slot before a used one, and MATRIX 5-8 is in the page round only while slot 4 is used (ui.c page_visible).
 * A slot with a SRC but no DST or AMT yet is being set up: it stays where it is. 1 = something moved. Not hot code */
static int mod_compact(track_t *t)
{
    int16_t *p = t->p;
    uint32_t k, w = 0;
    int moved = 0;
    for (k = 0; k < NMSLOT; k++) {
        if (!p[MSLOT_ID(k)])
            continue;
        if (w != k) {
            p[MSLOT_ID(w)] = p[MSLOT_ID(k)];
            p[MSLOT_ID(w) + 1u] = p[MSLOT_ID(k) + 1u];
            p[MSLOT_ID(w) + 2u] = p[MSLOT_ID(k) + 2u];
            moved = 1;
        }
        w++;
    }
    for (; w < NMSLOT; w++) {
        if (p[MSLOT_ID(w) + 1u] | p[MSLOT_ID(w) + 2u])
            moved |= !p[MSLOT_ID(w)] ? 0 : 1;
        if (p[MSLOT_ID(w)] | p[MSLOT_ID(w) + 1u] | p[MSLOT_ID(w) + 2u])
            moved = 1;
        p[MSLOT_ID(w)] = p[MSLOT_ID(w) + 1u] = p[MSLOT_ID(w) + 2u] = 0;
    }
    return moved;
}
/* is id a slot's SRC (the knob / the editor just wrote it: a 0 compacts) */
static int mod_is_src(uint32_t id) { return id >= P_M1SRC && id <= P_M4AMT ? (id - P_M1SRC) % 3u == 0u : id >= P_M5SRC && id <= P_M8AMT ? (id - P_M5SRC) % 3u == 0u : 0; }

/* L/RRY 1.1 (owner, the new DEST pages): the depth cards that are gone move into the matrix, so an existing sound plays the same. A patch that has a value on one of the retired cards
 * (AMP ENV to MULTI CUTOFF, FILTER ENV to TIMBRE, LFO 2 to PITCH / MULTI CUTOFF / TIMBRE) gets a matrix slot with the same source, destination and amount, and the card is set to 0 (the
 * engine no longer reads it). The units are the same (MX_AMT, the same sums in mod_voice_add and voice.c), so the sound is the same. Done as every sound loads (ui.c load_end) and as a
 * project or multi loads (project.c project_restore_runtime). The first free slot (SRC, DST and AMT all 0, after the packing) is used, in the order of the table. A patch with no free slot keeps
 * the value on its dead card (the sound changes) and is counted: the return is the number of routes not moved (the report lists such patches in PATCH_REVIEW.md). Not hot code */
static __attribute__((noinline)) uint32_t mod_legacy_move(track_t *t)
{
    static const struct { uint8_t id, src, dst; } L[5] = {
        {P_ED_FLT, MS_ENV, MD_CUT}, {P_E2SHP, MS_ENV2, MD_TIMBRE},
        {P_L2PIT, MS_LFO2, MD_PITCH}, {P_L2FLT, MS_LFO2, MD_CUT}, {P_L2SHP, MS_LFO2, MD_TIMBRE}};   /* (L/RRY phase 51: L/RRY's TIMBRE is the L/RRY TIMBRE, MD_TIMBRE) */
    int16_t *p = t->p;
    uint32_t i, k, lost = 0;
    byp_m[PART_OF(t)] = 0;                              /* L/RRY 1.1 BYPASS: a sound loads into this part: its bypass switches go off (the shared effects' stay) */
    for (i = 0; i < 5u; i++) {
        if (!p[L[i].id])
            continue;
        (void)mod_compact(t);
        for (k = 0; k < NMSLOT && (p[MSLOT_ID(k)] | p[MSLOT_ID(k) + 1u] | p[MSLOT_ID(k) + 2u]); k++)
            ;
        if (k == NMSLOT) {
            lost++;
            continue;
        }
        p[MSLOT_ID(k)] = L[i].src;
        p[MSLOT_ID(k) + 1u] = L[i].dst;
        p[MSLOT_ID(k) + 2u] = p[L[i].id];
        p[L[i].id] = 0;
    }
    return lost;
}
/* MATRIX PRESELECTION (L/RRY 1.1, owner; at amount 0, so no sound changes): every EMPTY slot (SRC and DST OFF, AMT 0) of a sound that loads gets the next pair of this list that the sound does
 * not already contain (the same source and destination in any slot, whatever its amount); a used slot stays as it is. Done at LOAD TIME (ui.c load_end for a sound, project.c for a
 * project or multi), after mod_legacy_move, so the moved routes have first claim on the free slots. The visible pair on the matrix page with 0 as its amount is the owner's cue; the owner
 * sets the amount. A slot the user later empties (SRC OFF) is filled again the next time a sound loads. One line a pair; line 3 (LFO 2 to SPACE) is the pair the owner changed last */
/* L/RRY phase 51: L/RRY's pairs 2 (AMP ENV -> REVERB SEND) and 3 (LFO 2 -> SPACE) have no L/RRY destination (no reverb, no SPACE): they are left out, not replaced (the owner picks the pairs:
 * the report's question); pair 1's SATURATION is DIRT B DRIVE, the reading AMP DEST's card has (L/RRY's last stage, the saturator, is DIRT B) */
#define MP_N 6u
static const struct { uint8_t src, dst; } MP_PRE[MP_N] = {
    {MS_AT, MD_BDRV},     /* 1 AFTERTOUCH -> DIRT B DRIVE (L/RRY: SATURATION) */
    {MS_CYCLE, MD_RATE},  /* 4 CYCLE -> LFO 1 RATE */
    {MS_RAND, MD_PAN},    /* 5 RANDOM -> PAN */
    {MS_KEY, MD_AREL},    /* 6 KEY TRACK -> AMP ENV RELEASE */
    {MS_VEL, MD_AATK},    /* 7 VELOCITY -> AMP ENV ATTACK */
    {MS_MODW, MD_VIB},    /* 8 MOD WHEEL -> LFO 1 DEPTH (the pitch's, category PITCH) */
};
static __attribute__((noinline)) void mod_preselect(track_t *t)
{
    int16_t *p = t->p;
    uint32_t k, n = 0, j;
    (void)mod_compact(t);
    for (k = 0; k < NMSLOT; k++) {
        int dup;
        if (p[MSLOT_ID(k)] | p[MSLOT_ID(k) + 1u] | p[MSLOT_ID(k) + 2u])
            continue;
        for (dup = 1; n < MP_N && dup; ) {                /* the next list pair this sound does not hold */
            dup = 0;
            for (j = 0; j < NMSLOT; j++)
                dup |= p[MSLOT_ID(j)] == MP_PRE[n].src && p[MSLOT_ID(j) + 1u] == MP_PRE[n].dst;
            if (dup)
                n++;
        }
        if (n >= MP_N)
            return;
        p[MSLOT_ID(k)] = MP_PRE[n].src;
        p[MSLOT_ID(k) + 1u] = MP_PRE[n].dst;
        p[MSLOT_ID(k) + 2u] = 0;
        n++;
    }
}

/* the descriptor whose range a per-block destination's id moves over: the engine's slot, a part's value, an extension slot or the TWIN FILTER's RES 2 by its TYPE (RES / FDBK) */
static __attribute__((noinline)) const param_desc_t *mod_pd(const track_t *t, uint32_t id)
{
    return id >= XW_BASE || id == P_RES2 ? track_desc(t, id) : id >= P_E0 ? &ENGINES[t->engine]->edit[id - P_E0] : &TP[id];
}

/* before a part's block (fx.c mix_part): the per-track offsets, the per-block destinations into t->p.
 * 0 = no slot active (then nothing changed) */
/* L/RRY 1.1 LOCKS on the destinations that have no stored value (PITCH, TIMBRE, VOICE AMP, the bus values, SPACE): a lock's AMOUNT (-64..+63, motion.c motion_setp) per part and destination number,
 * and how many are not 0. mod_begin adds each as a route from a constant full-size source (MX_AMT: AMT 64 = the whole range), the way a matrix slot does; the bus values and SPACE through the part's
 * bus offsets (both parts' add). The sequencer sets them on the step and puts them back (motion.c) */
static int8_t lk_virt[NLIVE][64];
static uint8_t lk_virt_n[NLIVE];
_Static_assert(MD_N <= 64, "lk_virt: one byte per destination number");
/* L/RRY 1.1 BYPASS: source s belongs to a bypassed module (LFO 1: LFO, S&H, SLEW; LFO 2; FILTER ENV; CYCLE): a route from it sends nothing (skipped, as an unused source) */
static inline uint32_t byp_src(const track_t *t, uint32_t s)
{
    static const uint8_t SRCBIT[MS_N] = {[MS_LFO] = BY_LFO1, [MS_SH] = BY_LFO1, [MS_SLEW] = BY_LFO1, [MS_LFO2] = BY_LFO2, [MS_ENV2] = BY_ENV2, [MS_CYCLE] = BY_CYC};
    return MODW(t).byp & SRCBIT[s];
}
/* L/RRY 1.1: the sequencer's locks on the destinations with no stored value (lk_virt): each is a route from a constant full-size source times the lock's amount (MX_AMT). PITCH and VOICE AMP go into the
 * per-voice sums mod_voice reads (L/RRY phase 51: TIMBRE is per block: mod_begin adds its lock with the routes; no bus value or SPACE is a destination any more). Flash code, only while a lock holds one */
static __attribute__((noinline)) void mod_lock_virt(mod_win_t *mod, uint32_t pi)
{
    uint32_t k;
    for (k = 1; k < MD_N; k++) {
        int32_t a = lk_virt[pi][k];
        if (!a || !MD_PER_VOICE(k))
            continue;
        a = MX_AMT(a);
        mod->amp |= k == MD_AMP;
        mod_voice_add(MS_MODW, k, 32767, a, &mod->pit, &mod->cut, &mod->shp, &mod->gain);
    }
}
/* a per-block destination's id gets the offset xa (the source x amount, already shifted: 64 = the whole range of the id's own descriptor) for this block: its slot in the part's held list
 * (mod->kid, keep, off), made on its first route. 0 = the list is full (MOD_NK: cannot happen with 8 slots and the DEST cards, mod.c VR; kept as a guard) */
static __attribute__((noinline)) int mod_blk_add(track_t *t, mod_win_t *mod, int32_t *off, uint32_t id, int32_t xa)
{
    const param_desc_t *pd = mod_pd(t, id);
    uint32_t j;
    for (j = 0; j < mod->nk && mod->kid[j] != id; j++)
        ;
    if (j == mod->nk) {
        if (j >= MOD_NK)
            return 0;
        mod->kid[j] = (uint8_t)id;
        mod->keep[j] = t->p[id];
        off[j] = 0;
        mod->nk++;
    }
    off[j] += (xa * (pd->max - pd->min)) >> 15;
    return 1;
}
static __attribute__((noinline)) int mod_begin(track_t *t)
{
    const int16_t *p = t->p;
    mod_win_t *mod = &MODW(t);
    int32_t lfo, off[MOD_NK];
    uint32_t k, j, act = 0, dep = 0, dep2 = 0, mx = 0, pi = (uint32_t)(t - trk), vrm = 0;
    mx_sl_t *mc = pi < NLIVE ? &mx_sl[pi] : 0;      /* (the slew's state of this part: 0 for a track that does not render) */
    uint32_t f2m = 0, cym = 0, cy = 0;                  /* the second list (cloud job B): slots to CUTOFF 2 from a per-voice source, CYCLE slots to a per-note destination, CYCLE read */
    mod->on = 0;
    mod->e2 = mod->l2 = 0;
    mod->byp = (uint8_t)(byp_m[pi & 1u] & 0x3Fu);                      /* L/RRY 1.1 BYPASS: this block's copy of the part's switches */
    t->f2add = 0;
    for (k = 0; k < MR_N; k++) {                        /* the active routes (the slots with SRC, DST and AMT set, and the DEST cards off 0), once a block */
        uint32_t qs, qd;
        int32_t qa;
        if (k == NMSLOT || k == NMSLOT + 1u)
            continue;                                   /* (the LFOs' own slews) */
        if (mr_get(p, k, &qs, &qd, &qa) && !byp_src(t, qs)) {   /* (L/RRY 1.1 BYPASS: a bypassed module's routes send nothing) */
            if (mc && mc->src[k] != (uint8_t)qs) {     /* (the slew: another source in the slot starts from its own value) */
                mc->src[k] = (uint8_t)qs;
                mc->ok &= ~(1u << k);
            }
            act |= 1u << k;
            dep |= (uint32_t)(qd == MD_DEPTH) << k;
            dep2 |= (uint32_t)(qd == MD_DEPTH2) << k;
            mx |= MD_MIXED(qd);
            mod->e2 |= qs == MS_ENV2;
            mod->l2 |= qs == MS_LFO2;
            cy |= qs == MS_CYCLE;
        }
    }
    t->f2m = t->cym = t->vrm = 0;                                /* (the second list is made below, with the other slots; nothing active: it stays empty) */
    t->cy = (uint8_t)(cy != 0);
    if (mc) {
        mc->ok &= act | 0x300u;                  /* (the slew: a slot not active now starts from its value when it comes back; bits 8, 9: the LFOs, always fed) */
        mod_mixmv[pi] = (uint8_t)(mx != 0 || t->mwon);   /* (hunt1: an open MORPH window counts too: the fader moves PAN and PATCH LEVEL by whole steps, as a route does) */
    }
    if (!act && !(pi < NLIVE && lk_virt_n[pi]))
        return 0;
    lfo = mulq15(t->lfo_val, t->lfo_fade);              /* (the LFO of the block before: track_render ticks it) */
    mod->lfo2 = mod->l2 ? mulq15(t->lfo2.val, t->lfo2.fade) : 0;   /* (LFO 2's of the block before, the same way) */
    mod->on = 1;
    mod->amp = 0;
    mod->nv = mod->nk = 0;
    mod->pit = mod->cut = mod->shp = 0;
    mod->gain = 32767;
    mod->ldep = dep ? mod_depth(t, lfo, dep) : 32767;    /* DEPTH first: the other slots see the LFO it leaves */
    mod->ldep2 = dep2 ? mod_depth(t, lfo, dep2) : 32767; /* L/RRY: DEPTH2 too; both read either LFO unscaled (no loop: the */
    if (mod->ldep < 32767)                               /* LFOs a block before, RATE / RATE2 heard from the next tick) */
        lfo = mulq15(lfo, mod->ldep);
    if (mod->ldep2 < 32767)
        mod->lfo2 = mulq15(mod->lfo2, mod->ldep2);
    for (act &= ~(dep | dep2); act; act &= act - 1u) {   /* the other active slots, in slot order */
        uint32_t s, d, id;
        int32_t a, x, xa;
        k = (uint32_t)__builtin_ctz(act);
        (void)mr_get(p, k, &s, &d, &a);
        a = MX_AMT(a);
        if (s >= MS_N || d >= MD_N || MD_SPARE(d))      /* (L/RRY phase 51: a spare number does nothing) */
            continue;
        if (d == MD_CUT2) {                             /* filter 2's cutoff, per note (cloud job B): a per-voice source is read by mod_f2_voice (the slot's bit), a per-track one is summed here, as mod_voice_add sums CUT */
            if (k >= NMSLOT)
                vrm |= 1u;                              /* (L/RRY 1.1: CYCLE's TWIN CUTOFF card: mod_f2_voice adds it) */
            else if (MS_VOICE(s))
                f2m |= 1u << k;
            else
                t->f2add += (mx_slew(t, k, mod_tsrc(t, s, lfo)) * a) >> 7;
            continue;
        }
        if (MD_PER_VOICE(d) && s == MS_CYCLE) {         /* CYCLE to a per-note destination: mod_cycle_voice, after mod_voice (which does not know the source) */
            if (k >= NMSLOT)
                vrm |= 2u;                              /* (L/RRY 1.1: CYCLE's PITCH card: mod_cycle_voice adds it) */
            else
                cym |= 1u << k;
            continue;
        }
        if (MD_PER_VOICE(d)) {                          /* per voice */
            mod->amp |= d == MD_AMP;
            if (MS_VOICE(s)) {
                mod->vsrc[mod->nv] = (uint8_t)s;
                mod->vdst[mod->nv] = (uint8_t)d;
                mod->vamt[mod->nv++] = (int8_t)a;
            } else {                                    /* (a UNI LFO, 0..1, takes AMP as ENV does) */
                mod_voice_add(MS_GAIN(p, s), d, mx_slew(t, k, mod_tsrc(t, s, lfo)), a, &mod->pit, &mod->cut, &mod->shp, &mod->gain);
            }
            continue;
        }
        x = mx_slew(t, k, mod_tsrc(t, s, lfo));
        id = mod_param(d);
        if (!id)
            continue;
        /* per block: AMT 64 = the whole range. A unipolar source (ENV, VEL, MODW, UNI LFO ..) at AMT 64 moves the parameter by its whole range one way; a
         * bipolar one (LFO, S&H, KEY, RAND ..) swings +-half the range, so its full swing spans the whole range around the base value (PAN: full left to
         * full right at AMT 63 / -64; a negative AMT is the inverted phase). TIMBRE moves OSC 1's P1 and OSC 2's P1 by the same share of their ranges */
        xa = (x * a) >> (MS_BIPOLAR(MS_GAIN(p, s)) ? 7 : 6);
        (void)mod_blk_add(t, mod, off, id, xa);
        if (d == MD_TIMBRE)
            (void)mod_blk_add(t, mod, off, XW_BASE + X_O2P1, xa);
    }
    if (pi < NLIVE && lk_virt_n[pi]) {
        mod_lock_virt(mod, pi);                         /* L/RRY 1.1: the sequencer's locks on the destinations with no stored value (flash) */
        if (lk_virt[pi][MD_TIMBRE]) {                   /* (TIMBRE's lock: per block, as a route from a constant full source) */
            int32_t xa = (32767 * MX_AMT(lk_virt[pi][MD_TIMBRE])) >> 6;
            (void)mod_blk_add(t, mod, off, XW_BASE + X_O1P1, xa);
            (void)mod_blk_add(t, mod, off, XW_BASE + X_O2P1, xa);
        }
    }
    for (j = 0; j < mod->nk; j++) {
        uint32_t id = mod->kid[j];
        const param_desc_t *pd = mod_pd(t, id);
        t->p[id] = (int16_t)clamp(mod->keep[j] + off[j], pd->min, pd->max);
    }
    t->f2m = (uint8_t)f2m;
    t->cym = (uint8_t)cym;
    t->vrm = (uint8_t)vrm;
    return 1;
}

/* after the part's block (when mod.on): the stored values back */
static __attribute__((noinline)) void mod_end(track_t *t)
{
    mod_win_t *mod = &MODW(t);
    uint32_t j;
    for (j = 0; j < mod->nk; j++)
        t->p[mod->kid[j]] = mod->keep[j];
    mod->on = 0;
}

/* a voice's modulation (voice.c track_render, when the part's window mod is on), onto its vmod_t m as track_render made it:
 * pitch (the increment again, with the fine factor fine as there), cutoff and shape (clamped to what the
 * fixed routings can reach), the amplitude (and v->env_out, the next block's start). env: its envelope */
static __attribute__((noinline)) void mod_voice(track_t *t, voice_t *v, vmod_t *m, int32_t fine)
{
    const mod_win_t *mod = &MODW(t);
    int32_t pit = mod->pit, cut = mod->cut, shp = mod->shp, gain = mod->gain, env = m->envq15;
    uint32_t k;
    if (v == &t->v[t->m_vi]) {
        t->m_env = env;                                 /* ENV of the latest note, for per-block destinations */
        t->m_env2 = ((mod->byp & BY_ENV2) ? 0 : v->env2 >> 9);                       /* .. and its ENV 2 */
    }
    for (k = 0; k < mod->nv; k++) {
        uint32_t s = mod->vsrc[k];
        int32_t x = s == MS_ENV ? env : s == MS_ENV2 ? ((mod->byp & BY_ENV2) ? 0 : v->env2 >> 9) : s == MS_VEL ? v->mvel * 258 : s == MS_KEY ?
                    mod_key(v->note) : v->mrnd;
        mod_voice_add(s, mod->vdst[k], x, mod->vamt[k], &pit, &cut, &shp, &gain);
    }
    if (pit) {
        m->pitch16 = clamp(m->pitch16 + pit, 0, 2047);
        m->inc = pitch_inc(m->pitch16);
        if (fine)
            m->inc += (uint32_t)((int32_t)(m->inc >> 12) * fine);
    }
    m->cutoff = clamp(m->cutoff + cut, -150 * 256, 150 * 256);
    m->shape = clamp(m->shape + shp, -62 * 256, 190 * 256);
    if (mod->amp) {
        m->amp1 = mulq15(m->amp1, gain);
        v->env_out = m->amp1;
    }
}

/* THE SECOND LIST (cloud job B, EXTENSION_BLOCK_PLAN 3.4): what mod_voice and mod_voice_add (hot RAM, closed) do not know, in two cold functions that voice.c track_render and the chain call.
 * mod_begin took the slots whose destination is CUTOFF 2, or whose source is CYCLE with a per-note destination, out of the per-voice list (t->f2m, t->cym, t->f2add, t->cy); with the
 * second list empty they do nothing and mod_voice runs as it always did.
 *
 * mod_f2_voice: the matrix's sum for filter 2's cutoff for voice v, in 1/256 dial steps as mod_voice's cut is (job E adds it to filter 2's own cutoff sum): the per-track sources' sum
 * made in mod_begin, and each CUTOFF 2 slot of a per-voice source (ENV, ENV 2, VEL, KEY, RAND, CYCLE) by its amount, x * AMT >> 7 as mod_voice_add's CUT. ENV is the voice's envelope as
 * track_render has it (t->venv, set before the call: the engine's amp curve applied; 0 for an engine with its own envelopes, as mod_voice). Call it after mod_voice, in track_render's
 * loop of voice v; cycle_value must give the same value for v on every call in a block (it is read here and in mod_cycle_voice) */
static __attribute__((noinline)) int32_t mod_f2_voice(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    int32_t sum = t->f2add;
    uint32_t m;
    for (m = t->f2m; m; m &= m - 1u) {
        uint32_t k = (uint32_t)__builtin_ctz(m), s = (uint32_t)p[MSLOT_ID(k)];
        int32_t x = s == MS_ENV ? t->venv : s == MS_ENV2 ? E2Q(t, v) : s == MS_VEL ? v->mvel * 258 : s == MS_KEY ? mod_key(v->note) : s == MS_CYCLE ? cycle_value(t, v) : v->mrnd;
        sum += (x * MX_AMT(p[MSLOT_ID(k) + 2u])) >> 7;
    }
    if (t->vrm & 1u)                                    /* L/RRY 1.1: CYCLE ENV's TWIN CUTOFF card, the same sum as a CYCLE to TWIN CUTOFF slot */
        sum += (cycle_value(t, v) * MX_AMT(XV(t, X_CYCUT2))) >> 7;
    return sum;
}
/* mod_cycle_voice: the CYCLE-sourced slots with a per-note destination (PITCH, CUT, SHP, AMP) applied to voice v's vmod_t m after mod_voice has made its own, with the same sums and the same
 * re-computations (the pitch increment again with the fine factor, cutoff and shape clamped to what the fixed routings reach, the amplitude with v->env_out), and the latest note's CYCLE kept
 * for the per-block destinations (t->m_cycle, as ENV 2's m_env2). Called when an active slot reads CYCLE (t->cy), whatever its destination */
static __attribute__((noinline)) void mod_cycle_voice(track_t *t, voice_t *v, vmod_t *m, int32_t fine)
{
    const int16_t *p = t->p;
    int32_t pit = 0, cut = 0, shp = 0, gain = 32767, x = cycle_value(t, v);
    uint32_t cm, amp = 0;
    if (v == &t->v[t->m_vi])
        t->m_cycle = x;
    for (cm = t->cym; cm; cm &= cm - 1u) {
        uint32_t k = (uint32_t)__builtin_ctz(cm), d = (uint32_t)p[MSLOT_ID(k) + 1u];
        amp |= d == MD_AMP;
        mod_voice_add(MS_CYCLE, d, x, MX_AMT(p[MSLOT_ID(k) + 2u]), &pit, &cut, &shp, &gain);
    }
    if (t->vrm & 2u)                                    /* L/RRY 1.1: CYCLE ENV's PITCH card, as a CYCLE to PITCH slot */
        mod_voice_add(MS_CYCLE, MD_PITCH, x, MX_AMT(XV(t, X_CYPIT)), &pit, &cut, &shp, &gain);
    if (!t->cym && !(t->vrm & 2u))
        return;
    if (pit) {
        m->pitch16 = clamp(m->pitch16 + pit, 0, 2047);
        m->inc = pitch_inc(m->pitch16);
        if (fine)
            m->inc += (uint32_t)((int32_t)(m->inc >> 12) * fine);
    }
    m->cutoff = clamp(m->cutoff + cut, -150 * 256, 150 * 256);
    m->shape = clamp(m->shape + shp, -62 * 256, 190 * 256);
    if (amp) {
        m->amp1 = mulq15(m->amp1, gain);
        v->env_out = m->amp1;
    }
}
/* a note-on of track t (voice.c trk_note_on): the per-voice sources of the latest note */
static void mod_note(track_t *t, uint32_t note, uint32_t vel)
{
    t->m_vel = (uint8_t)vel;
    t->m_key = (uint8_t)note;
    t->m_rnd = mod_rand();
}

/* a MIDI controller for track t (seq.c events_block): CC1 / CC2 / CC4 / CC11 / channel aftertouch; CC121 resets them */
static __attribute__((noinline)) void mod_midi(track_t *t, uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st == 0xD0u) {
        t->at = (uint8_t)d1;
    } else if (st == 0xB0u) {
        if (d1 == 1u)
            t->mw = (uint8_t)d2;
        else if (d1 == 11u)
            t->ex_off = (uint8_t)(127u - d2);
        else if (d1 == 2u)
            mod_cc[0][(uint32_t)(t - trk) % NTRK] = (uint8_t)d2;      /* (L/RRY 1.1: BREATH CONTROLLER) */
        else if (d1 == 4u)
            mod_cc[1][(uint32_t)(t - trk) % NTRK] = (uint8_t)d2;      /* (FOOT CONTROLLER) */
        else if (d1 == 121u) {
            t->mw = t->at = t->ex_off = 0;
            mod_cc[0][(uint32_t)(t - trk) % NTRK] = mod_cc[1][(uint32_t)(t - trk) % NTRK] = 0;
        }
    }
}

/* the name of destination value v for track t (UI: the MATRIX page's DEST card and rows, two lines where a card breaks it; the DEST line makes one line of it). L/RRY phase 51: every destination in full
 * words (N_MDST keeps the stored keys: the factory sound tool, the editor). OSC 1 / OSC 2's model controls are named by the MODEL, with the words of the OSC 1/2 page's cards (params.c LPD: WIDTH, RATIO,
 * DETUNE, SAWS, SPREAD, INDEX, AMOUNT); a control the MODEL does not have says where its knob is (L/RRY's form for an unused engine slot: OSC 1 1/2 KNOB 3; phase 53: K2 .. K4 of the first page). What a card cannot hold in 49 px is
 * fitted or cut by the card (ui_draw.c cv_text_fit), never shortened here */
static __attribute__((noinline)) const char *mod_dst_name(const track_t *t, int32_t v)
{
    static char oscp[6][24];
    v = clamp(v, 0, MD_N - 1);
    if (MD_OSCP(v)) {
        uint32_t k = (uint32_t)(v - MD_O1P1), o2 = k >= 3u, i = k % 3u;
        const param_desc_t *pd = track_desc(t, XW_BASE + (o2 ? X_O2P1 : X_O1P1) + i);
        char *b = oscp[k];
        str_cpy(b, o2 ? "OSC 2\n" : "OSC 1\n", sizeof oscp[0]);
        if (mod_oscp_inert(t, (uint32_t)v)) {           /* "OSC 1" over "KNOB 2" (PHASE58: the page has no "1/2" now; phase 53: the model's controls are K2 .. K4 of the oscillator's first page) */
            str_cpy(b, o2 ? "OSC 2\nKNOB 2" : "OSC 1\nKNOB 2", sizeof oscp[0]);
            b[15] = (char)('2' + i);
        } else
            str_cpy(b + 6, desc_shown(pd), sizeof oscp[0] - 6u);
        return b;
    }
    if (t->p[P_TWTYPE] == TT_COMB && (v == MD_CUT2 || v == MD_RES2))   /* (on COMB the names of the type: TUNE, FEEDBACK) */
        return v == MD_CUT2 ? "TWIN\nTUNE" : "TWIN\nFEEDBACK";
    switch (v) {
    case MD_OFF:    return "NONE";
    case MD_PITCH:  return "PITCH";
    case MD_CUT:    return "MULTI\nCUTOFF";            /* (1.0, owner 2026-10-21: the filters are MULTI and TWIN; N_MDST keeps CUT) */
    case MD_TIMBRE: return "TIMBRE";                   /* (L/RRY phase 51: OSC 1 and OSC 2's first model control, both; N_MDST keeps SHP) */
    case MD_AMP:    return "AMP\nLEVEL";
    case MD_PAN:    return "PAN";
    case MD_NZPAN:  return "NOISE\nPAN";
    case MD_RGPAN:  return "RING\nPAN";
    case MD_DAPAN:  return "DIRT A\nPAN";
    case MD_DBPAN:  return "DIRT B\nPAN";
    case MD_RATE:   return "LFO 1\nRATE";
    case MD_VIB:    return "LFO 1\nDEPTH";              /* (change list 2026-10-28 D: under the category PITCH the item is LFO 1 DEPTH) */
    case MD_O1LVL:  return "OSC 1\nLEVEL";
    case MD_O2LVL:  return "OSC 2\nLEVEL";
    case MD_DEPTH:  return "LFO 1\nLEVEL";              /* (L/RRY's retired master depth: not offered) */
    case MD_RATE2:  return "LFO 2\nRATE";
    case MD_DEPTH2: return "LFO 2\nLEVEL";
    case MD_LEVEL:  return "PATCH\nLEVEL";              /* (0.2.25: TRIM is PATCH LEVEL everywhere; N_MDST keeps TRIM) */
    case MD_NZLVL:  return "NOISE\nLEVEL";
    case MD_RGLVL:  return "RING\nLEVEL";
    case MD_O1CRS:  return "OSC 1\nCOARSE";
    case MD_O1FIN:  return "OSC 1\nFINE";
    case MD_O2CRS:  return "OSC 2\nCOARSE";
    case MD_O2FIN:  return "OSC 2\nFINE";
    case MD_DALVL:  return "DIRT A\nLEVEL";
    case MD_DBLVL:  return "DIRT B\nLEVEL";
    case MD_E2ATK:  return "FILTER ENV\nATTACK";
    case MD_E2DEC:  return "FILTER ENV\nDECAY";
    case MD_E2SUS:  return "FILTER ENV\nSUSTAIN";
    case MD_E2REL:  return "FILTER ENV\nRELEASE";
    case MD_E2FLT:  return "FILTER ENV\nMULTI CUTOFF";
    case MD_O1BAL:  return "OSC 1\nBALANCE";
    case MD_RES:    return "MULTI\nRES";
    case MD_O2BAL:  return "OSC 2\nBALANCE";
    case MD_NZBAL:  return "NOISE\nBALANCE";
    case MD_RGBAL:  return "RING\nBALANCE";
    case MD_FMODE:  return "MULTI\nMODE";
    case MD_AATK:   return "AMP ENV\nATTACK";            /* (owner 2026-10-25: no short row forms; the category and the item in full) */
    case MD_ADEC:   return "AMP ENV\nDECAY";
    case MD_ASUS:   return "AMP ENV\nSUSTAIN";
    case MD_AREL:   return "AMP ENV\nRELEASE";
    case MD_CUT2:   return "TWIN\nCUTOFF";
    case MD_RES2:   return "TWIN\nRES";
    case MD_DRIVE:  return "DIRT A\nDRIVE";             /* (L/RRY: P_DRV2, L/RRY's FILTER DRIVE, is DIRT A's DRIVE; N_MDST keeps FDRV) */
    case MD_BLEND:  return "FILTER\nBLEND";
    case MD_SUBLVL: return "SUB\nLEVEL";
    case MD_O1PAN:  return "OSC 1\nPAN";
    case MD_O2PAN:  return "OSC 2\nPAN";
    case MD_FAPAN:  return "MULTI FILTER\nPAN";         /* (the STAGE PAN page's MULTI FILTER, TWIN FILTER) */
    case MD_FBPAN:  return "TWIN FILTER\nPAN";
    case MD_SUBPAN: return "SUB\nPAN";
    case MD_BDRV:   return "DIRT B\nDRIVE";
    default:        return "SPARE";                     /* (42 .. 45, 56, 62: not offered) */
    }
}

/* why destination v does nothing for track t now, 0 = it works (a card's reason, <= 8 characters). Only what a selection makes impossible (owner rule), never a value at 0. L/RRY phase 51:
 * MODEL (an oscillator's MODEL has no such control; TIMBRE: neither MODEL has a P1), TWIN OFF (the TWIN FILTER's values and pan and the A / B balances while its TYPE is OFF, as their cards hide),
 * DIRT OFF (a dirt unit's DRIVE, LEVEL, PAN while its TYPE is OFF, as their cards hide) */
static __attribute__((noinline)) const char *mod_dst_why(const track_t *t, int32_t v)
{
    v = clamp(v, 0, MD_N - 1);
    if ((MD_OSCP(v) && mod_oscp_inert(t, (uint32_t)v)) || (v == MD_TIMBRE && mod_shp_inert(t)))
        return "MODEL";
    if ((v == MD_CUT2 || v == MD_RES2 || v == MD_BLEND || v == MD_FBPAN || v == MD_O1BAL || v == MD_O2BAL || v == MD_NZBAL || v == MD_RGBAL) && !t->p[P_TWTYPE])
        return "TWIN OFF";                              /* (a stored route keeps playing) */
    if ((v == MD_DRIVE || v == MD_DALVL || v == MD_DAPAN) && !XV(t, X_DATYPE))
        return "DIRT OFF";
    if ((v == MD_BDRV || v == MD_DBLVL || v == MD_DBPAN) && !XV(t, X_DBTYPE))
        return "DIRT OFF";
    return 0;
}

/* the matrix's DEST knob and the editor offer destination v for t (0.2.25): not a removed one (mod_dst_removed), none the current selection makes do nothing (mod_dst_why), and (L/RRY phase 51,
 * L/RRY's rule of 2026-10-18 for the engine slots that are a list: every change a hard switch) not SUPERSAW's SAWS, a count of saws that steps. OFF is always offered */
static __attribute__((noinline)) int mod_dst_offered(const track_t *t, int32_t v)
{
    if (v <= 0)
        return 1;
    if (v >= MD_N || mod_dst_removed(0, v))
        return 0;
    if ((v == MD_O1P2 && MOD_MODEL(t, 0) == MOD_SUPER) || (v == MD_O2P2 && MOD_MODEL(t, 1) == MOD_SUPER))
        return 0;
    return !mod_dst_why(t, v);
}

/* L/RRY 1.1 (owner, HANDOFF_V2 item 34): the PARAMETER LOCKS page's parameter list IS the matrix destination list: the same entries, in the same order (MDST_ORDER), under the same names, offered by the
 * same rule (mod_dst_offered). Destination -> the stored parameter a lock or an automation event records: its own id (the extension slots by their wide id: motion.c's extended id space); MULTI CUTOFF
 * records filter A's dial; PITCH, AMP LEVEL and TIMBRE (two values) have none: virtual targets 192 + the number, an AMOUNT the way a matrix slot adds it. 0: none (OFF, the spares) */
static __attribute__((noinline)) uint32_t mod_lock_id(uint32_t d)
{
    if (d == 0u || d >= MD_N)
        return 0;
    if (MD_PER_VOICE(d) && d != MD_CUT)
        return MOTION_VBASE + d;
    if (d == MD_TIMBRE)
        return MOTION_VBASE + d;
    return d == MD_CUT ? P_FREEA : mod_param(d);
}
/* the destination whose stored value is target id (0 = none: the id is not in the matrix list, so it cannot be locked) */
static __attribute__((noinline)) uint32_t mod_lock_dst(uint32_t id)
{
    uint32_t d;
    if (id == 0u || id > 255u)
        return 0;
    for (d = 1; d < MD_N; d++)
        if (mod_lock_id(d) == id)
            return d;
    return 0;
}
/* a virtual target's descriptor: an AMOUNT -64..+63 (0 = nothing), shown like a matrix amount. The other targets' are the parameters' own */
static const param_desc_t LOCK_VD = PD("AMT", F_BIPCT, -64, 63, 0);
static const param_desc_t *lock_desc_of(uint32_t e, uint32_t tid) { return tid >= MOTION_VBASE ? &LOCK_VD : param_desc_of(e, tid); }
static const param_desc_t *lock_desc(const track_t *t, uint32_t tid) { return tid >= MOTION_VBASE ? &LOCK_VD : track_desc(t, tid); }
/* parameter id can be locked / recorded on t now: its destination is offered by the matrix's own rule */
static __attribute__((noinline)) int mod_lock_ok(const track_t *t, uint32_t id)
{
    uint32_t d = mod_lock_dst(id);
    return d && mod_dst_offered(t, (int32_t)d);
}

/* the MATRIX page's DEST line for destination v of track t: "DEST: OSC 1 PAN" (b: 32 bytes), and in more (0: nothing to add) what the card's reason means in words */
static __attribute__((noinline)) void mod_dst_line(const track_t *t, int32_t v, char *b, const char **more)
{
    const char *why;
    uint32_t n;
    v = clamp(v, 0, MD_N - 1);
    why = mod_dst_why(t, v);
    str_cpy(b, "DEST: ", 32);
    n = str_len(b);
    str_cpy(b + n, mod_dst_name(t, v), 32 - n);
    for (; b[n]; n++)                                   /* (a two-line card name is one line here) */
        if (b[n] == '\n')
            b[n] = ' ';
    *more = !why ? 0 : why[0] == 'M' ? "NOT USED BY THIS MODEL" : why[0] == 'T' ? "THE TWIN FILTER IS OFF" : "THIS DIRT UNIT IS OFF";
}
