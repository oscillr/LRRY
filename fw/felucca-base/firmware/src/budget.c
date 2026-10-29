/* SPDX-License-Identifier: GPL-3.0-only */
/* DUELL NOTES budget (docs/CPU_PLAN.md section 5, MIXSPLIT job 4; the API and the units: budget.h). The number of notes each part may play at
 * once, from what is switched on in the settings, so that every note the budget allows can sound together without the audio running out of
 * time: the idle mix + the sends and master stages + each part's own stages + the voices of both parts <= BUDGET_LIMIT of the time budget.
 *
 *   notes(part) = floor((limit - global - other part's stages - this part's stages - other part's notes x their cost) / this part's cost)
 *
 * Here the "other part's notes" are the same number n (or its own limit, if lower): a part's number is the most notes it can play while the other part
 * plays as many (budget_solve). The worst case is taken over every way the shared voice pool can be split (a + b over NVOICE cannot all sound at once). That
 * makes the numbers depend on the settings only, not on the order of the calls, on what plays or on a half-solved other number, and switching an item on
 * can only lower them (an earlier version that gave the cheaper part the leftover notes raised one part's number when the other got dearer). A part that
 * is muted costs nothing. Never below 1 a part.
 * DUELL 1.1 (HANDOFF_V2 item 90) THE TWO-CORE RULE (release_drafts/TWO_CORE_DESIGN.md 3.4): while core 1 plays a part (bd_two_now), each part has its own core and its number
 * no longer depends on the other part: notes(part) = floor((limit - global - this part's stages) / this part's cost) (item 132: the hand-over BUDGET_HO no longer charged). The globals are counted on BOTH cores:
 * core 0 runs them after it has waited for core 1's part (fx.c mix_block: post, part 1, wait, gather, sends, master), so they are on core 1's path too. The moment core 1
 * pauses or stops, the one-core numbers (bnotes1, always worked out) are in force again (voice.c trk_nvoice reads them while bd_one_now is set by the runner, which fades
 * the notes above them at once: twocore_run.c).
 * The one-core rule: one core runs everything. Nothing here runs per sample: flash code, called when a setting changes
 * (budget_poll, once a UI frame) and by the tests; the note-on path (voice.c trk_nvoice) reads the numbers it left in bnotes[].
 *
 * The cost table is ONE table, in host instructions a sample (the load bench's scale, tests/cpu_bench.c: 3281.25 = 100 % of a half); BD_U turns them into 1/100 % and
 * applies BUDGET_MARGIN. DUELL 1.1 item 90 re-measured it on the 1.1 sources (2026-10-25):
 *   (cal)    tests/poly_calib.c: the cost of ONE MORE note (5 notes less 1 note, over 4), every factory sound of the engine cleaned of TWIN / SUB / matrix, the dearest one;
 *            the filter and TWIN rows are the dearest paired difference (the same sound with and without). The part's own work that runs once while it sounds is BD_PART_ON
 *            (before item 90 every voice paid it, and the TWIN rows were estimates: the old table read 1.2 to 1.4 x the measured note);
 *   (bench)  tests/cpu_bench.c on the same sources (inserts, SAT, EQ, DUCK, the buses, the master stages);
 *   (lin)    measured on Linux with valgrind in the bench's way (a slot, an LFO, the arp): only the ratios were used;
 *   (old)    the VOICE CAP's figures of 0.2.11 (voice.c VC_MORPH, VC_FADE).
 * The unit: the one-core bench B2 of 2026-10-18 on the owner's unit read 0.90 .. 1.08 x the same host model for every engine's voice rows (release_drafts/bench_results/
 * bench_B2_run1_core1off_2026-10-18.csv); BUDGET_MARGIN (1.15) stays. DUELL 1.1 item 132 (owner): BUDGET_LIMIT 80 -> 90 % (both rules), the hand-over no longer charged; the
 * two-core runner's deadline (twocore_run.c tcr_deadline: part_fixed, part_cost, global_cost, i.e. BD_U with the margin) reads neither and is unchanged (tests/deadline_test.c).
 * DUELL 1.1 item 137 (owner): BUDGET_MARGIN 1.15 -> 1.00 (the caps charge the measured cost) and BUDGET_LIMIT 88 -> 81 (model + 6 points of peak fits core 0 and core 1: budget.h); the runner's
 * deadline and wait limit read the _dl figures (BD_UDL, BUDGET_DL_MARGIN 1.15) and stay bit for bit (tests/deadline_test.c).
 * DUELL 1.1 item 142 (owner-approved, HANDOFF_V2 items 139 / 140): a part's MORPH ends are charged as they SOUND (bd_end: morph_apply on the live values), not as stored, so an edit
 * after a load (TWIN OFF, 24 -> 12 dB) is no longer charged at both ends; every factory patch and multi as loaded gives the same items as before (tests/budget_test.c). BUDGET_LIMIT
 * 81 -> 84 with the runner's wait limit at true cost (twocore_run.c tcr_deadline, budget.h).
 * DUELL 1.1 item 147 (owner): SAT is charged like the filters and the TWIN, from its played value at the morph's ends (live on, or on at either end as it sounds), not from the live value only;
 * the cap may differ for a patch whose SAT is on at End B only (tests/budget_test.c item 147 (f)). */

#include "budget.h"

#define BD_UM(i, m) ((((uint32_t)(i)) * 64u * (m) + 2099u) / 2100u)   /* host instructions a sample -> 1/100 % (3281.25 = 100 %), margin m (percent) applied, rounded up */
#define BD_U(i) BD_UM(i, BUDGET_MARGIN)                 /* the NOTES caps (item 137: margin 1.00) */
#define BD_UDL(i) BD_UM(i, BUDGET_DL_MARGIN)            /* the two-core runner's deadline and wait limit (item 137: the 1.15 they always had) */

/* ----------------------------------------------------------------- the table (host instructions a sample) --- */
/* one more note of an engine with the multimode filter bypassed (cal); the filter (MULTI) is added by the slope the part really has. BD_F12 holds the chain path too: with
 * CHAIN_ALL every note whose filter 1 runs goes through the voice chain (voice_chain.c), and the calibration measured them together */
#define BD_F12 89u              /* MULTI 12 dB a note, the chain path included (cal: the dearest paired difference, all engines 83 .. 89) */
#define BD_F24 123u             /* .. 24 dB (cal: 115 .. 123) */
/* DUELL 1.1 item 116: COST-CAL (bench_runs/cost_merged/cost_fit/report.txt, 625 + 124 runs on the unit's own counter, 2026-10-26): per engine the DEARER of the one-core row and the two-core
 * critical-path row, rounded up (the model charges one row on both paths; only WAVE differs by more than 3 %: 93 one core, 119 on the critical path) */
static const struct { const engine_t *e; uint16_t c; } BD_ENG[] = {   /* (cal) */
    {&ENG_ANALOG, 115}, {&ENG_FM6, 152}, {&ENG_PHASE, 99}, {&ENG_LOFI, 53}, {&ENG_FORMANT, 173},   /* (FORMANT is the engine VOICE) */
    {&ENG_TRIO, 105}, {&ENG_WHEEL, 154}, {&ENG_PHYS, 406}, {&ENG_NOISE, 87}, {&ENG_WAVE, 119},
};
#define BD_ENG_DEF 200u         /* an engine not in the table: above the dearest of the normal ones */
#define BD_PART_ON 64u          /* the part's own work while it sounds, once whatever the number of notes (cal: the first note's extra over the next ones, 39 .. 64) */
#define BD_MORPH 16u            /* the MORPH a voice (old: 0.5 %) */
#define BD_FADE 66u             /* .. an engine choice cross-faded, both shapes made (old: 2 %) */
/* the part's own stages */
static const uint8_t BD_INS[] = {0, 69, 65, 65, 70, 52, 68, 62, 65, 75, 88};   /* COST-CAL item 116 (rounded up; HARD CLIP .. VINYL slope -2 a voice: the mean over the key counts),  P_ITYPE order (N_ITYPE): OFF SOFT CLIP, HARD CLIP, FOLD, FUZZ, BIT CRUSHER, PHASER, FLANGER, CHORUS TAPE VINYL */
#define BD_INS_MAX 88u          /* a type out of the table: the dearest */
#define BD_SAT 29u              /* COST-CAL item 116: SAT 64 a part 28.3 (OK) */
#define BD_EQ_BASE 11u          /* COST-CAL item 116: EQ 1, 2, 3 bands measured 35.2, 61.9, 83.6 (GOOD): this + n x BD_EQ_BAND = 36, 61, 86 */
#define BD_EQ_BAND 25u          /* .. a band whose gain is not 0 (a flat band is not run) */
#define BD_DUCK 31u             /* COST-CAL item 116: DUCK a part 30.5 (OK) */
#define BD_KTRK 1u              /* KEY TRK with the filter on: uf_test.c "under 1 instruction a sample" */
#define BD_MX_SLOT 8u           /* a matrix slot with SRC, DST and AMT set (lin: 7 a slot, 4 voices; the voice share is in the margin) */
#define BD_LFO 5u               /* an LFO that a slot reads (lin: under 5; LFO 1 / LFO 2) */
#define BD_ARP 18u              /* the arp's own step work a part (bench scene fixed, STEREO / DUEL: 36 for two parts) */
/* the mix with nothing playing, by MODE [0 STEREO and LAYER, 1 DUEL]: BD_IDLE is the mix with SENDS OFF (bench idle 455 less bench bus/STEREO/nothing_fed 221;
 * DUEL: CPU_PLAN section 1, 21.7 % less the bench's 492), then the buses (bench bus/<MODE>/<row>: the whole bus section against SENDS OFF with only that effect fed;
 * the delay and the reverb run whenever SENDS is ON, the chorus only while some part sends to it) */
/* (DUELL 1.1 item 90: re-measured on the 1.1 sources, tests/cpu_bench.c and tests/poly_calib.c: idle with the power-on sends 501 STEREO, 745 DUEL, less the bus row) */
static const uint16_t BD_IDLE[2] = {317, 319};                                 /* COST-CAL item 116 (OK: one run spreads 21 %), rounded up */
static const uint16_t BD_BUS[2] = {238, 496};                                   /* nothing fed, HALL + DIGITAL (the power-on) */
static const uint16_t BD_CHO[2] = {305, 553};                                   /* + the chorus fed */
static const uint16_t BD_DLY[2][4] = {{295, 320, 365, 413}, {539, 537, 673, 712}};   /* DIGITAL PING-PONG RANDOM OCT UP, fed */
static const uint16_t BD_REV[2][6] = {{246, 272, 295, 289, 299, 312}, {463, 516, 535, 590, 555, 565}};   /* ROOM SPRING HALL SHIM(UP) RESO PLATE, fed */
static const uint16_t BD_SHD[2] = {386, 752};                                   /* SHIM PITCH DOWN */
static const uint16_t BD_SHM[2] = {390, 758};                                   /* SHIM PITCH UP + MOD 50 */
#define BD_SHDM 387u            /* SHIM PITCH DOWN + MOD 50, STEREO / LAYER (measured 386.8, GOOD; the DUEL row was not measured: its two extras are added) */
#define BD_REV_BYP 121u         /* the bus section (STEREO / LAYER) with the REVERB BYPASS on: HALL fed 294.9 less the 173.9 the bypass saved (item 116; DUEL not measured) */
#define BD_COMP 54u             /* bench master/COMP, WIDTH, BASSMONO (none runs in DUEL) */
#define BD_WIDTH 39u
#define BD_BMONO 38u

/* ----------------------------------------------------------------- the parts --- */
typedef struct {                /* one part's items, host instructions a sample (the ablations of budget_reason zero one) */
    uint32_t eng, filt, ext;    /* a voice: the engine, the multimode filter's slope, the morph's extras */
    uint32_t ins, sat, eq, misc;/* fixed: INSERT, SAT, EQ, the rest (ducker, KEY TRK, matrix, LFOs, arp) */
    uint32_t base;              /* fixed: the part's own work while it sounds (BD_PART_ON; no ablation removes it) */
    uint32_t cap;               /* the most notes it can have: NOTES_MAX, the pool, the engine's own limit */
    uint32_t on;                /* sounds at all (not muted) */
} bd_part_t;
typedef struct { uint32_t idle, fx; bd_part_t p[2]; } bd_t;   /* the global items too */
#define BD_N ((uint32_t)NSHOWN)  /* the parts the budget covers (the shown ones; L/RRY: was a fixed 2, now follows NSHOWN so one part builds) */
_Static_assert(NLIVE >= BD_N && BD_N <= 2u, "budget.c: at most two parts (bd_t holds two)");
#include "budget_feat.h"        /* the new features' items: TWIN FILTER, SUB, CYCLE (cloud job B's stub; jobs E, F fill budget_feat.c) */

static uint32_t bd_slope(const engine_t *e, const int16_t *p)  /* filter 1, the multimode filter, on every engine since cloud job J: 0 bypassed or none, 1 12 dB, 2 24 dB */
{
    uint32_t i, mc = 0;
    for (i = 0; i < NMSLOT; i++)
        mc |= p[MSLOT_ID(i)] && p[MSLOT_ID(i) + 1] == MD_CUT && p[MSLOT_ID(i) + 2];
    if (!e->ufilt || (p[P_FREEA] >= (int16_t)UF_STEPS && p[P_FREEC] <= 0 && !p[P_KTRK] &&   /* (item 90: as voice.c's real bypass, the effective cut at the open end: */
                      !p[P_LD_FLT] && !p[P_E2FLT] && !p[P_ED_FLT] && !p[P_L2FLT] && !mc && e != &ENG_NOISE))   /* no depth or route that moves the cut; NOISE's DRFT moves it) */
        return 0;
    return p[P_FREED] == 2 ? 2u : 1u;
}
/* DUELL 1.1 item 142 (HANDOFF_V2 item 140): end e (0 = A, 1 = B) of the part's MORPH as it SOUNDS: morph.c morph_apply on a copy of the live values with the fader at that end
 * and no ramp (= morph_effective_x). A continuous value is live + (end - Original); a choice the player changed (live != Original, not pinned at End B) stays the player's and is never
 * faded; a value End B holds by hand (pinned) is End B's. Before item 142 the budget read the ends' STORED values (morph_cur): a loaded patch edited to TWIN OFF / 12 dB was
 * still charged its Original's TWIN and 24 dB at both ends, which can never sound. q: EXT_P_LEN values */
static void bd_end(const track_t *t, const morph_part_t *mp, uint32_t e, int16_t *q)
{
    mxf_t xf;
    memcpy(q, t->p, EXT_P_LEN * sizeof q[0]);
    xf.on = 0;
    morph_apply(mp, q, e ? 32768 : 0, 0, &xf);          /* (at the ends nothing is cross-faded: xf is never written) */
}

#include "budget_feat.c"
#if LRRY_ENGINE
/* ------------------------------------------------------------------------------------------------- L/RRY (phase 4.3a): the stereo voice's cost. AN ESTIMATE, NOT MEASURED ON THE UNIT.
 * How: tests/lrry_fw_test.c (built with -DLRRY_BENCH) times the real mix_block on the Mac: 8 voices of each setting less the idle mix, per voice-sample, best of 4 runs, three
 * runs averaged; the same build with -DLRRY_ENGINE=0 times DUELL's own ANALOG voice (the table's 115 for the filter bypassed, +89 for 12 dB, +123 for 24 dB): the three anchors read 21.4 .. 25.8
 * host instructions a nanosecond, 22.5 is used. The rows below are those nanoseconds x 22.5 (host instructions a sample and voice, the unit of the rows above), rounded up to 5, then x 1.15
 * (BD_LR_MARGIN: no unit measurement and no instruction counter behind them; DUELL's own rows carry margin 1.00 because they are measured on the unit). Replace with the bench's numbers. The
 * measured nanoseconds (Mac, -O2): OSC1 SAW alone 12.2; models TRI -0.5, SINE +0.1, PULSE +0.8, SYNC +1.1, FM +1.5, FOLD +0.1, SUPER 7 saws +12.9; OSC 2 SAW +4.5; NOISE +1.0; RING +3.0;
 * filter A 12 dB +5.3, 24 dB +15.2; filter B SVF +4.3, LADDER +15.3, COMB +4.8; dirt unit (SOFT) +3.6 each; MID / SIDE +0.9; SUB +1.0. The costs add (all on at once: 73 measured, 74 summed) */
#define BD_LR_MARGIN 115u
#define BD_LR(i) ((((i) * BD_LR_MARGIN) + 99u) / 100u)
static const uint8_t BD_LR_MODEL[8] = {0, 0, 5, 20, 25, 0 /* SUPER: below */, 35, 5};   /* SAW TRI SINE PULSE SYNC SUPER FM FOLD, over SAW */
#define BD_LR_BASE 280u         /* one source (OSC 1 SAW), the stereo pans and balances, the amp, no filter, no dirt (12.2 ns) */
#define BD_LR_OSC2 100u         /* a second oscillator rendered (4.5 ns) */
#define BD_LR_NOISE 25u
#define BD_LR_RING 70u
#define BD_LR_FA12 120u
#define BD_LR_FA24 345u
#define BD_LR_FB_SVF 100u
#define BD_LR_FB_LAD 345u
#define BD_LR_FB_COMB 110u
#define BD_LR_DIRT 85u          /* a dirt unit (drive, shape, level, pan) */
#define BD_LR_MS 20u
#define BD_LR_SUB 25u
/* L/RRY 4.3b, the split by voice: two ESTIMATES, nothing measured on the unit.
 * BD_LR_CTL_PCT: the share of a voice's cost that is control work (envelope, glide, pitch, the matrix, lrry_voice_ctl) and stays on core 0 when the voice's audio goes to core 1. Measured on the Mac with the
 * render stage switched off (8 voices, 3000 blocks, best of 5): 5.5 % for OSC 1 SAW alone, 2.3 % for LADDER + dirt, 2.2 % for the SUPER patch; 6 is used.
 * BD_LR_HO0 / HO1: the hand-over a block, host instructions a sample: core 0 copies the 888-byte job context and hashes it (222 words), core 1 hashes it again and clears and reads the 256-byte accumulator;
 * counted from the code at about 4 instructions a word and 32 samples a block (222 x 6 / 32 = 41 on core 0, 222 x 4 / 32 + 16 = 44 on core 1), rounded to 40 and 45. */
#define BD_LR_CTL_PCT 6u
#define BD_LR_HO0 40u
#define BD_LR_HO1 45u
#define BD_LR_FBMOD 10u         /* filter B's modulation (KEY TRK 2, ENV 2, LFO 1, LFO 2, a matrix route): the cutoff sum, once a block */
static uint32_t bd_lr_osc(const int16_t *x, uint32_t o)   /* oscillator o (0, 1): its model's cost over a SAW */
{
    uint32_t m = (uint32_t)clamp(x[o ? X_O2MODEL : X_O1MODEL], 0, 7), n;
    if (m != 5u)
        return BD_LR_MODEL[m];
    n = 1u + (((uint32_t)clamp(x[(o ? X_O2P2 : X_O1P2)], 0, 127) * 6u + 63u) / 127u);
    return 45u * n - 25u;                               /* SUPER: 7 saws +290 */
}
/* one voice of the part from its values p (the live ones: the L/RRY has no MORPH tags yet, ext.h); sl: filter A's slope as bd_slope has it (0 not run, 1 12 dB, 2 24 dB) */
static uint32_t bd_lrry_voice(const int16_t *p, uint32_t sl)
{
    const int16_t *x = &p[XW_BASE];
    uint32_t c = BD_LR_BASE, o2 = x[X_O2LVL] > 0 || x[X_RGLVL] > 0, ty = (uint32_t)clamp(p[P_TWTYPE], 0, 6), i;
    c += bd_lr_osc(x, 0);
    if (o2)
        c += BD_LR_OSC2 + bd_lr_osc(x, 1);
    c += x[X_NZLVL] > 0 ? BD_LR_NOISE : 0u;
    c += x[X_RGLVL] > 0 ? BD_LR_RING : 0u;
    c += sl == 2u ? BD_LR_FA24 : sl ? BD_LR_FA12 : 0u;
    if (ty) {
        c += ty == 5u ? BD_LR_FB_LAD : ty == 6u ? BD_LR_FB_COMB : BD_LR_FB_SVF;
        if (x[X_KTRK2] || x[X_ENV2] || x[X_LFO1] || x[X_LFO2])
            c += BD_LR_FBMOD;
        for (i = 0; i < NMSLOT; i++)
            if (p[MSLOT_ID(i)] && p[MSLOT_ID(i) + 1u] == MD_CUT2 && p[MSLOT_ID(i) + 2u]) {
                c += BD_LR_FBMOD;
                break;
            }
    }
    c += (x[X_DATYPE] ? BD_LR_DIRT : 0u) + (x[X_DBTYPE] ? BD_LR_DIRT : 0u);
    c += x[X_MID] != 64 || x[X_SIDE] != 64 ? BD_LR_MS : 0u;
    c += x[X_SUBLVL] > 0 ? BD_LR_SUB : 0u;
    return BD_LR(c);
}
#endif
static void bd_part(const track_t *t, bd_part_t *r)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];   /* (the sound's engine: a switch fades first, eng_req leads) */
    const int16_t *p = t->p;
    uint32_t i, sl, bands, pi = (uint32_t)(t - trk), lf = 0, by = pi < 2u ? byp_m[pi] : 0u;   /* by: the BYPASS switches of the part (item 116: SAT, INSERT and EQ bypassed measured as not running) */
    uint32_t sat = p[P_DIST] != 0;                      /* SAT on in the live values or at either morph end as it sounds (item 147) */
    bd_tw_t w = {0, 0, 0, 0};                           /* item 142: the TWIN FILTER of the morph's two ends as they sound (budget_feat.c adds the live one) */
    memset(r, 0, sizeof *r);
    r->cap = vc_eng_cap(e);
    r->cap = r->cap < NOTES_MAX ? r->cap : NOTES_MAX;
    r->cap = r->cap < NVOICE ? r->cap : NVOICE;
    if (p[P_MUTE])
        return;                                         /* a muted part costs nothing */
    r->on = 1;
    r->eng = BD_ENG_DEF;
    for (i = 0; i < NELEM(BD_ENG); i++)
        if (BD_ENG[i].e == e)
            r->eng = BD_ENG[i].c;
    sl = bd_slope(e, p);
    if (pi < NSHOWN && (mph[pi].nid || mph[pi].fnid) && !morph_stale(t, &mph[pi])) {   /* the MORPH on this part: each end as morph.c morph_apply plays it (item 142) */
        const morph_part_t *mp = &mph[pi];
        int16_t q[EXT_P_LEN], d0 = 0, c0[8] = {0};      /* (one end at a time: End A's slope, SLOPE and engine choices are kept for the comparison with End B) */
        uint32_t s0 = 0, s1, fade = 0, k;
        for (k = 0; k < 2u; k++) {
            const int16_t *v = q;
            if (!k && !memcmp(morph_cur(mp, 0), mp->base, P_COUNT) && !memcmp(morph_curx(mp, 0), mp->base + XW_BASE, NX))
                v = p;                                  /* End A is the Original (every load; a ROLL of B only): at the A end morph_apply gives the live values back (each kind at w 0 with A = the
                                                         * Original; only its range clamp could differ, for a live value already out of range). One blend less: tcr_deadline runs this at block 0 */
            else
                bd_end(t, mp, k, q);
            s1 = bd_slope(e, v);
            sat |= v[P_DIST] != 0;                      /* (item 147: SAT is charged from the played ends like the filters and the TWIN) */
            if (v != p)
                bd_tw_of(&w, e, v);                     /* (the live TWIN: bd_feat) */
            if (!k) {
                s0 = s1, d0 = v[P_FREED];
                memcpy(c0, &v[P_E0], sizeof c0);
                continue;
            }
            for (i = 0; i < mp->nid; i++)
                if ((mp->ent[i].kind & 0x7Fu) == MK_FADE && mp->ent[i].id >= P_E0 && mp->ent[i].id < P_E0 + 8u && q[mp->ent[i].id] != c0[mp->ent[i].id - P_E0])
                    fade = 1;                           /* an engine choice cross-faded between the ends as they sound: both shapes (a choice the player changed is his at both ends: none) */
        }
        sl = s0 > sl ? s0 : sl;
        sl = s1 > sl ? s1 : sl;
        if (s0 && s1 && d0 != q[P_FREED])
            sl = 2;                                     /* (the slopes cross-fade: both stages run) */
        r->ext += (fade ? BD_FADE : 0u) + BD_MORPH;
    }
    r->filt = sl == 2u ? BD_F24 : sl ? BD_F12 : 0;
    if (mods_off & MODS_PFX)                            /* (item 125: GLO > MODULES, PART FX OFF: the part's DIST, INSERT, EQ and DUCK are skipped, fx.c: they cost nothing; was charged as if on) */
        by |= BY_INS | BY_SAT | BY_EQ;
    i = (uint32_t)p[P_ITYPE];
    r->ins = i == 0 || (by & BY_INS) ? 0 : i < NELEM(BD_INS) ? BD_INS[i] : BD_INS_MAX;
    r->sat = sat && !(by & BY_SAT) ? BD_SAT : 0;
    bands = (p[P_EQLG] != 0) + (p[P_EQMG] != 0) + (p[P_EQHG] != 0);
    r->eq = bands && !(by & BY_EQ) ? BD_EQ_BASE + bands * BD_EQ_BAND : 0;
    r->misc = p[P_DKDEP] > 0 && !(mods_off & MODS_PFX) ? BD_DUCK : 0;
    r->misc += sl && p[P_KTRK] ? BD_KTRK : 0;
    for (i = 0; i < NMSLOT; i++) {
        const int16_t *q = &p[MSLOT_ID(i)];
        if (q[0] && q[1] && q[2]) {
            r->misc += BD_MX_SLOT;
            lf |= (q[0] == MS_LFO ? 1u : 0u) | (q[0] == MS_LFO2 ? 2u : 0u);
        }
    }
    r->misc += ((lf & 1u) + (lf >> 1)) * BD_LFO;
    r->misc += arp_mode(t) ? BD_ARP : 0;                /* (item 125: the arp as it plays now: MODULES' ARP OFF costs nothing; was p[P_AMODE]) */
    r->base = BD_PART_ON;
#if LRRY_ENGINE
    /* L/RRY 4.3a: the stereo voice is the one source; the old engine, TWIN, SUB and the part's effects (SAT, INSERT, EQ, DUCK: OFF, not run) are not charged. CYCLE still is */
    r->eng = bd_lrry_voice(p, bd_slope(&ENG_ANALOG, p));
    r->filt = 0;
    r->ext = pi < NSHOWN && (mph[pi].nid || mph[pi].fnid) ? BD_MORPH : 0u;
    r->ins = r->sat = r->eq = 0;
    if (r->misc >= BD_DUCK && p[P_DKDEP] > 0 && !(mods_off & MODS_PFX))
        r->misc -= BD_DUCK;
    for (i = 0; i < NMSLOT; i++)
        if (p[MSLOT_ID(i)] == MS_CYCLE && p[MSLOT_ID(i) + 1u] && p[MSLOT_ID(i) + 2u]) {
            r->ext += BD_CYCLE;
            break;
        }
#else
    bd_feat(t, r, &w);                                  /* TWIN FILTER (live, and the morph ends' from above), SUB, CYCLE (budget_feat.c) */
#endif
}

static uint32_t part_cost(const track_t *t)
{
    bd_part_t r;
    bd_part(t, &r);
    return BD_U(r.eng + r.filt + r.ext);
}
static uint32_t part_fixed(const track_t *t)
{
    bd_part_t r;
    bd_part(t, &r);
    return BD_U(r.ins + r.sat + r.eq + r.misc + r.base);
}
static void part_dl(const track_t *t, uint32_t *fixed, uint32_t *voice)   /* item 137: the deadline's figures, BUDGET_DL_MARGIN (= part_fixed / part_cost of before item 137 bit for bit);
                                                         * item 142: both from ONE bd_part (it blends each morph end once: tcr_deadline asks it for both parts at every half's block 0) */
{
    bd_part_t r;
    bd_part(t, &r);
    *fixed = BD_UDL(r.ins + r.sat + r.eq + r.misc + r.base);
    *voice = BD_UDL(r.eng + r.filt + r.ext);
}

/* ----------------------------------------------------------------- the globals --- */
static void bd_global(uint32_t *idle, uint32_t *fx)     /* host instructions a sample: the idle mix, and what the sends and the master stages add */
{
    uint32_t m = 0u /* (L/RRY: always the STEREO column) */, dm = (uint32_t)song.g[G_DMODE], rt = (uint32_t)song.g[G_RTYPE], p, cho = 0, mod;
    int32_t t = 0;
    *idle = BD_IDLE[m];
    for (p = 0; p < BD_N; p++)
        cho |= !trk[p].p[P_MUTE] && trk[p].p[P_CHOR] != 0;
    if (!(mods_off & MODS_SENDS)) {                     /* (SENDS OFF: the buses do not run) */
        dm = dm < 4u ? dm : 0u;                         /* (a value past OCT UP plays as DIGITAL, fx.c) */
        rt = rt < 6u ? rt : 2u;
        mod = (uint32_t)clamp(song.g[G_SHMOD], 0, 100);
        t = (int32_t)BD_BUS[m] + ((int32_t)BD_DLY[m][dm] - (int32_t)BD_BUS[m]);
        if (!m && (byp_sh & BYS_REV))                   /* item 116: the REVERB BYPASS skips the reverb (measured -173.9 of 294.9, STEREO / LAYER): the bus section is the rest; the chorus and delay BYPASS saved nothing (+16, +26) and stay charged. DUEL: not measured, charged as before */
            t += (int32_t)BD_REV_BYP - (int32_t)BD_BUS[m];
        else if (rt == 3u && song.g[G_SHPITCH] == SHP_DOWN && mod && !m)
            t += (int32_t)BD_SHDM - (int32_t)BD_BUS[m];   /* (item 116: SHIM DOWN + MOD measured) */
        else if (rt == 3u && song.g[G_SHPITCH] == SHP_DOWN)
            t += (int32_t)BD_SHD[m] - (int32_t)BD_BUS[m] + (mod ? (int32_t)BD_SHM[m] - (int32_t)BD_REV[m][3] : 0);
        else if (rt == 3u && mod)
            t += (int32_t)BD_SHM[m] - (int32_t)BD_BUS[m];
        else
            t += (int32_t)BD_REV[m][rt] - (int32_t)BD_BUS[m];
        t += cho ? (int32_t)BD_CHO[m] - (int32_t)BD_BUS[m] : 0;
    }
    if (!m) {                                           /* the master stages (none runs in DUEL) */
        t += (cmp_cfg & 0xF00u) && !(byp_sh & BYS_CMP) ? (int32_t)BD_COMP : 0;   /* (item 116: the master and compressor BYPASS measured as skipping all three, -146) */
        t += wd_sel != 100u && !(byp_sh & BYS_MST) ? (int32_t)BD_WIDTH : 0;
        t += bm_sel && !(byp_sh & BYS_MST) ? (int32_t)BD_BMONO : 0;
    }
#if DUELL_VELO
    t += sense_running() ? 57 : 0;                      /* KEY SENSE (after Velo's method; est.: the key scan is not on the CPU bar): a fixed core-0 load of 2 % after the margin, 57 host instructions a sample, not measured */
#endif
    *fx = (uint32_t)t;
}
static uint32_t global_cost(void)
{
    uint32_t i, f;
    bd_global(&i, &f);
    return BD_U(i + f);
}
static uint32_t global_cost_dl(void)
{
    uint32_t i, f;
    bd_global(&i, &f);
    return BD_UDL(i + f);
}

/* ----------------------------------------------------------------- the pair --- */
static void bd_build(bd_t *b)
{
    uint32_t p;
    bd_global(&b->idle, &b->fx);
    b->p[0] = b->p[1] = (bd_part_t){0};                 /* L/RRY: a part that does not exist is all zero (budget_solve2 reads both slots) */
    for (p = 0; p < BD_N; p++)
        bd_part(&trk[p], &b->p[p]);
}
static uint32_t bd_worst(const uint32_t *c, const uint32_t *n)   /* both parts' notes (DUELL 1.1 item 90: each part has its own NVOICE voices, no shared pool any more) */
{
    return n[0] * c[0] + n[1] * c[1];
}
/* The numbers of both parts. A part's number is the most notes it can play while the other part plays the same number (or its own limit, when that is
 * lower): the worst case of that pair, whatever way the shared pool is split, stays under the limit. So a pair (a, b) from here always fits: with a >= b,
 * the pair (a, min(a, cap of the other)) is the one a was checked with and b is not above it. Each number depends on the settings only (never on what
 * plays, on the order of the calls or on the other number), and switching an item on in a part can only lower the numbers: its stages and voices
 * make the pair dearer. Never below 1. */
static void budget_solve2(const bd_t *b, uint32_t *out, uint32_t two)
{
    uint32_t c[2] = {0, 0}, fx[2] = {0, 0}, g = BD_U(b->idle + b->fx), lim = BUDGET_LIMIT * 100u, p;
    for (p = 0; p < BD_N; p++) {
        const bd_part_t *r = &b->p[p];
        c[p] = r->on ? BD_U(r->eng + r->filt + r->ext) : 0;
        fx[p] = r->on ? BD_U(r->ins + r->sat + r->eq + r->misc + r->base) : 0;
    }
#if LRRY_ENGINE
    if (two) {                                          /* L/RRY 4.3b, the two-core rule BY VOICE (an ESTIMATE, nothing of it measured on the unit). The split is dynamic (voice.c lrry_split_choose picks the voices for core 1 every
                                                         * block from what sounds), so the best case is taken: k sounding voices sit ceil(k / 2) on core 0, floor(k / 2) on core 1 (equal voices; the chooser does as well or better with unequal
                                                         * ones). Core 0 plays: the globals, the part's fixed work, its ceil(k / 2) voices whole, and of the other floor(k / 2) the control
                                                         * work that stays on it (BD_LR_CTL_PCT of a voice) and the hand-over (BD_LR_HO0). Core 1 plays its floor(k / 2) voices (a voice less that control work) and BD_LR_HO1. Both loads must fit
                                                         * the limit. The one-core number (bnotes1) is untouched. */
        for (p = 0; p < BD_N; p++) {
            uint32_t k, n0, n1, cc;
            if (!b->p[p].on) {
                out[p] = b->p[p].cap;
                continue;
            }
            cc = (c[p] * BD_LR_CTL_PCT + 99u) / 100u;   /* the control work of a voice, kept on core 0 */
            for (k = b->p[p].cap; k > 1u; k--) {
                n1 = k / 2u;
                n0 = k - n1;
                if (g + fx[p] + n0 * c[p] + n1 * cc + (n1 ? BD_U(BD_LR_HO0) : 0u) <= lim && n1 * (c[p] - cc) + (n1 ? BD_U(BD_LR_HO1) : 0u) <= lim)
                    break;
            }
            out[p] = k;
        }
        return;
    }
#else
    if (two) {                                          /* the two-core rule: each part on its own core, the globals on both paths (item 132, owner: the hand-over BUDGET_HO is not charged:
                                                         * COST-CAL's estimate of it was -28, nothing) */
        for (p = 0; p < BD_N; p++) {
            uint32_t k;
            for (k = b->p[p].cap; k > 1u; k--)
                if (g + fx[p] + k * c[p] <= lim)
                    break;
            out[p] = k;
        }
        return;
    }
#endif
    for (p = 0; p < BD_N; p++) {
        uint32_t n[2], k;
        for (k = b->p[p].cap; k > 1u; k--) {            /* (the most that fits: a pair with more notes costs more) */
            n[p] = k;
            n[!p] = k < b->p[!p].cap ? k : b->p[!p].cap;
            if (g + fx[0] + fx[1] + bd_worst(c, n) <= lim)
                break;
        }
        out[p] = k;
    }
}
static void budget_solve(const bd_t *b, uint32_t *out) { budget_solve2(b, out, bd_two_now()); }   /* the rule in force */

/* the two-core rule is in force: core 1 answered at power-on, has not failed, its jobs are not paused (a flash park is short: the runner trims to the one-core numbers for it) */
static uint32_t bd_two_now(void)
{
#if DUELL_TWOCORE
    return tc.up && !tc.failed && !tcr.paused TCR_AND_NOT_NOCORE1;   /* (the bench word `core1 off`; the macro is empty outside the bench) */
#else
    return 0;
#endif
}

static uint32_t part_notes(int part)
{
    bd_t b;
    uint32_t n[2];
    if (part < 0 || part >= (int)BD_N)
        return 0;
    bd_build(&b);
    budget_solve(&b, n);
    return n[part];
}
static uint8_t part_notes_shown(int part)
{
    return (uint8_t)part_notes(part);
}

/* what limits part p: the item whose removal gives it the most notes (ties: the dearer item) */
static uint8_t part_notes_reason(int part)
{
    bd_t b, q;
    uint32_t n[2], m[2], k, best = 0, gain = 0, bcost = 0, o = part == 0;
    static const uint8_t WHY[7] = {NR_FILTER, NR_INSERT, NR_SAT, NR_EQ, NR_FX, NR_OTHER, NR_MISC};
    if (part < 0 || part >= (int)BD_N)
        return NR_NONE;
    bd_build(&b);
    budget_solve(&b, n);
    for (k = 0; k < 7u; k++) {
        uint32_t cost = 0;
        q = b;
        switch (k) {
        case 0: cost = q.p[part].filt > BD_F12 ? q.p[part].filt - BD_F12 : 0, q.p[part].filt = BD_F12 * (q.p[part].filt != 0); break;   /* (the 24 dB slope: down to 12 dB) */
        case 1: cost = q.p[part].ins, q.p[part].ins = 0; break;
        case 2: cost = q.p[part].sat, q.p[part].sat = 0; break;
        case 3: cost = q.p[part].eq, q.p[part].eq = 0; break;
        case 4: cost = q.fx, q.fx = 0; break;
        case 5: cost = q.p[o].eng + q.p[o].filt + q.p[o].ext, q.p[o].eng = q.p[o].filt = q.p[o].ext = 0; break;
        default: cost = q.p[part].misc + q.p[part].ext, q.p[part].misc = q.p[part].ext = 0; break;
        }
        if (!cost)
            continue;
        budget_solve(&q, m);
        if (m[part] > n[part] && (m[part] - n[part] > gain || (m[part] - n[part] == gain && cost > bcost))) {
            gain = m[part] - n[part];
            bcost = cost;
            best = WHY[k];
        }
    }
    if (best)
        return (uint8_t)best;
    if (n[part] >= b.p[part].cap)                       /* it has all it can have: the pool, NOTES_MAX, or the engine's own limit */
        return b.p[part].cap < NOTES_MAX && b.p[part].cap < NVOICE ? NR_ENGINE : NR_NONE;
    return NR_ENGINE;                                   /* the engine's own voices are what is left */
}

static void budget_poll(void)                           /* main loop, every frame (main.c, beside vcap_poll) */
{
    bd_t b;
    uint32_t n[2], p;
#if DUELL_BENCH
    if (bench_bopen) {                                 /* (bench builds: a cost row of src/bench.c holds the budget open: no numbers, every voice the
                                                        * VOICE CAP and the pool give, as the bench of 0.2.25 measured) */
        for (p = 0; p < BD_N; p++)
            bnotes[p] = bnotes1[p] = 0;
        return;
    }
#endif
    bd_build(&b);
    budget_solve2(&b, n, 0);
    for (p = 0; p < BD_N; p++)
        bnotes1[p] = (uint8_t)n[p];                     /* the one-core numbers, always (the runner falls back to them at once) */
    if (bd_two_now())
        budget_solve2(&b, n, 1);
    for (p = 0; p < BD_N; p++)
        bnotes[p] = (uint8_t)n[p];
}
