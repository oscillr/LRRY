/* SPDX-License-Identifier: GPL-3.0-only */
/* L/RRY NOTES budget (docs/CPU_PLAN.md section 5, MIXSPLIT job 4; the API and the units: budget.h). The number of notes each part may play at
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
 * L/RRY 1.1 (HANDOFF_V2 item 90) THE TWO-CORE RULE (release_drafts/TWO_CORE_DESIGN.md 3.4): while core 1 plays a part (bd_two_now), each part has its own core and its number
 * no longer depends on the other part: notes(part) = floor((limit - global - this part's stages) / this part's cost) (item 132: the hand-over BUDGET_HO no longer charged). The globals are counted on BOTH cores:
 * core 0 runs them after it has waited for core 1's part (fx.c mix_block: post, part 1, wait, gather, sends, master), so they are on core 1's path too. The moment core 1
 * pauses or stops, the one-core numbers (bnotes1, always worked out) are in force again (voice.c trk_nvoice reads them while bd_one_now is set by the runner, which fades
 * the notes above them at once: twocore_run.c).
 * The one-core rule: one core runs everything. Nothing here runs per sample: flash code, called when a setting changes
 * (budget_poll, once a UI frame) and by the tests; the note-on path (voice.c trk_nvoice) reads the numbers it left in bnotes[].
 *
 * The cost table is ONE table, in host instructions a sample (the load bench's scale, tests/cpu_bench.c: 3281.25 = 100 % of a half); BD_U turns them into 1/100 % and
 * applies BUDGET_MARGIN. L/RRY 1.1 item 90 re-measured it on the 1.1 sources (2026-10-25):
 *   (cal)    tests/poly_calib.c: the cost of ONE MORE note (5 notes less 1 note, over 4), every factory sound of the engine cleaned of TWIN / SUB / matrix, the dearest one;
 *            the filter and TWIN rows are the dearest paired difference (the same sound with and without). The part's own work that runs once while it sounds is BD_PART_ON
 *            (before item 90 every voice paid it, and the TWIN rows were estimates: the old table read 1.2 to 1.4 x the measured note);
 *   (bench)  tests/cpu_bench.c on the same sources (inserts, SAT, EQ, DUCK, the buses, the master stages);
 *   (lin)    measured on Linux with valgrind in the bench's way (a slot, an LFO, the arp): only the ratios were used;
 *   (old)    the VOICE CAP's figures of 0.2.11 (voice.c VC_MORPH, VC_FADE).
 * The unit: the one-core bench B2 of 2026-10-18 on the owner's unit read 0.90 .. 1.08 x the same host model for every engine's voice rows (release_drafts/bench_results/
 * bench_B2_run1_core1off_2026-10-18.csv); BUDGET_MARGIN (1.15) stays. L/RRY 1.1 item 132 (owner): BUDGET_LIMIT 80 -> 90 % (both rules), the hand-over no longer charged; the
 * two-core runner's deadline (twocore_run.c tcr_deadline: part_fixed, part_cost, global_cost, i.e. BD_U with the margin) reads neither and is unchanged (tests/deadline_test.c).
 * L/RRY 1.1 item 137 (owner): BUDGET_MARGIN 1.15 -> 1.00 (the caps charge the measured cost) and BUDGET_LIMIT 88 -> 81 (model + 6 points of peak fits core 0 and core 1: budget.h); the runner's
 * deadline and wait limit read the _dl figures (BD_UDL, BUDGET_DL_MARGIN 1.15) and stay bit for bit (tests/deadline_test.c).
 * L/RRY 1.1 item 142 (owner-approved, HANDOFF_V2 items 139 / 140): a part's MORPH ends are charged as they SOUND (bd_end: morph_apply on the live values), not as stored, so an edit
 * after a load (TWIN OFF, 24 -> 12 dB) is no longer charged at both ends; every factory patch and multi as loaded gives the same items as before (tests/budget_test.c). BUDGET_LIMIT
 * 81 -> 84 with the runner's wait limit at true cost (twocore_run.c tcr_deadline, budget.h).
 * L/RRY 1.1 item 147 (owner): SAT is charged like the filters and the TWIN, from its played value at the morph's ends (live on, or on at either end as it sounds), not from the live value only;
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
/* L/RRY 1.1 item 116: COST-CAL (bench_runs/cost_merged/cost_fit/report.txt, 625 + 124 runs on the unit's own counter, 2026-10-26): per engine the DEARER of the one-core row and the two-core
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
#define BD_MX_SLOT 20u /* budget-real2: measured the part's own work 1.5 us a slot (extra.csv 'A12 + 4 slots' fixed +5.8 us for 4); was 8 */ /* old:           (a matrix slot with SRC, DST and AMT set (lin: 7 a slot, 4 voices; the voice share is in the margin)) */
#define BD_LFO 0u /* budget-real2: measured 0 (extra.csv 'A12 + LFO1>CUT' equals 'A12 + VEL>CUT'); was 5 */ /* old:               (an LFO that a slot reads (lin: under 5; LFO 1 / LFO 2)) */
#define BD_ARP 303u /* budget-real2: measured +22 us a block (feat.csv 'ARP UP' fixed 126.1 against 104.1); was 18 */ /* old:              (the arp's own step work a part (bench scene fixed, STEREO / DUEL: 36 for two parts)) */
/* the mix with nothing playing, by MODE [0 STEREO and LAYER, 1 DUEL]: BD_IDLE is the mix with SENDS OFF (bench idle 455 less bench bus/STEREO/nothing_fed 221;
 * DUEL: CPU_PLAN section 1, 21.7 % less the bench's 492), then the buses (bench bus/<MODE>/<row>: the whole bus section against SENDS OFF with only that effect fed;
 * the delay and the reverb run whenever SENDS is ON, the chorus only while some part sends to it) */
/* (L/RRY 1.1 item 90: re-measured on the 1.1 sources, tests/cpu_bench.c and tests/poly_calib.c: idle with the power-on sends 501 STEREO, 745 DUEL, less the bus row) */
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
#define BD_LR_WIDTH 46u         /* L/RRY 2026-11-12, MEASURED on the unit (bench 1112I, MASTER2: MULTI-filter saw, 12 keys, two cores, core 0 average): WIDTH 150 +1.4 points = 46 host instructions a sample */
#define BD_LR_BMONO 46u         /* .. BASS MONO 200 Hz +1.4 points (bench 1112J, MASTER). COMP measured +1.6 (removed: core.h LRRY_COMP). Were the Mac rows at 71 % (0.9, 0.8): too low */
/* L/RRY 2026-11-12, the effect buses back (core.h LRRY_FX_SENDS 1), MEASURED on the unit (bench 1112L, FX-COST: SAW BASS with MULTI 12 dB, 8 keys, two cores, core 0 average, sends 100;
 * 1 point = 32.8 host instructions a sample). SENDS OFF measured +0.0 (nothing charged). Each row is what the bus adds over SENDS OFF:
 *   BD_LR_REV   the REVERB fed by type (ROOM SPRING HALL SHIM RESO PLATE: +3.1 +3.3 +4.6 +4.7 +4.3 +4.2), with the delay running idle; nothing fed (SENDS ON, sends 0, HALL) measured +3.7:
 *               BD_LR_REVIDLE (0.9) less when no part sends to the reverb
 *   BD_LR_DLY   the DELAY fed by MODE over the reverb row (DIGITAL +0.8, PING-PONG +1.5, RANDOM and OCT UP +7.1, their peak 0.3 more over the average than the others: 7.4); RANDOM and OCT UP charged fed or not
 *   BD_LR_CHO   the CHORUS fed (+2.5)
 * The rows add up to more than measured together (all three, HALL, DIGITAL: 7.9 charged, 6.2 measured): the screen errs low, never high. NOT measured: SHIM DOWN / MOD (the Mac rows' extra at 71 %), the REVERB BYPASS (charged as running) */
static const uint16_t BD_LR_REV[6] = {102, 108, 151, 154, 141, 138};
#define BD_LR_REVIDLE 30u
static const uint16_t BD_LR_DLY[4] = {26, 49, 243, 243};
#define BD_LR_CHO 82u

/* ----------------------------------------------------------------- the parts --- */
typedef struct {                /* one part's items, host instructions a sample (the ablations of budget_reason zero one) */
    uint32_t eng, filt, ext;    /* a voice: the engine, the multimode filter's slope, the morph's extras */
    uint32_t ins, sat, eq, misc;/* fixed: INSERT, SAT, EQ, the rest (ducker, KEY TRK, matrix, LFOs, arp) */
    uint32_t base;              /* fixed: the part's own work while it sounds (BD_PART_ON; no ablation removes it) */
    uint32_t cap;               /* the most notes it can have: NOTES_MAX, the pool, the engine's own limit */
    uint32_t on;                /* sounds at all (not muted) */
    uint32_t bst, bst0;         /* L/RRY: the note-on burst (PHASE62 section 10): extra peak a voice that has it, and once for the sound (host instructions a sample; 0 = none) */
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
/* L/RRY 1.1 item 142 (HANDOFF_V2 item 140): end e (0 = A, 1 = B) of the part's MORPH as it SOUNDS: morph.c morph_apply on a copy of the live values with the fader at that end
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
/* ------------------------------------------------------------------------------------------------- L/RRY: the stereo voice's cost, FROM THE UNIT'S OWN MEASUREMENTS.
 * Every row below is in TENTHS OF A MICROSECOND of one 725 us block (BD_LR_T turns them into the table's host instructions a sample: 1 us a block = 4.526). THE ROWS COME FROM
 * PHASE62_VOICE_COST_REPORT.md (bench12, unit FM-1_901, 2026-11-01); update them there first, here second, each time speed work changes a cost:
 *   BD_LR_BASE      a plain SAW voice as the quick bench measures it, all-in (kernel 22 us + envelope, matrix, control, mix): the straight-line slope of the clean one-core tests of #16 TEST SAW,
 *                   bench_runs/quick/2026-11-01_bench12/results.csv, 1 to 12 keys: (500 - 146) / 11 = 32 us. (report section 8.2: voices 250 us at 12 keys)
 *   the extras      section 1 "Remaining per-voice cost (kernel bench, extra us per voice per block over a plain SAW)": SUPERSAW +56 (7 saws: 8 us a saw), LADDER +53, 24 dB +44 (the table uses
 *                   57: the dearer of this and the 24 dB row of the ticket of 2026-11-01, so the number errs low), filter MODE blend +37, SUB +27, second oscillator +8, dirt A+B +22 (11 each)
 *   checked against the quick bench's slopes (results.csv, one core, us a block a key): #06 WIDE PAD 121 (model 133), #14 STEREO TEST 43 (43), #07 COMB PLUCK 41 (41), #12 FULL STRESS about 270 (280)
 *   budget-real2: the former ESTIMATES (oscillator models, NOISE, RING, SUB, filter A 12 / 24 dB, filter B types, dirt types, modulation, matrix, LFO, arp) are now MEASURED, one core, isolated, see each row below
 *   (bench_runs/2026-11-02-B17). Pan routes (ramping pans) measured +0 (feat.csv 'PAN route LFO1'), SAT +0 (not run), CYCLE +0.4 us a voice (BD_CYCLE kept). NOT measured: filter A 24 dB with the MODE blend, the COMB note-on burst of a real sound.
 * L/RRY budget-real (owner: the counter shows the true maximum): the fixed work of a block is BD_LR_GLOBAL (below), measured; BD_LR_FIXED_PCT (71 %) scales only what is left of the old Mac rows (buses with
 * LRRY_FX_SENDS 1, COMP, WIDTH, BASS MONO, KEY SENSE: ESTIMATES). With the sends compiled out the measured saving is the 2.5 us of the sends stage (report section 8.2), so nothing else is taken off.
 * THE SHOWN NUMBER (budget_solve2): the largest k whose predicted PEAK (bd_lr_peak: average + steady peak J + the note-on burst) stays under the lowest measured first-miss peak less 2 points, one core and
 * two cores separately (LRRY_CP_LIM1, LRRY_CP_LIM). Misses follow the peak, not the average (PHASE62 section 10.2, 5). */
#define BD_LR_T(t10) (((t10) * 4526u + 5000u) / 10000u)  /* tenths of us a block -> host instructions a sample, rounded */
#define BD_LR_FIXED_PCT 71u
/* budget-real2: every row below that was an ESTIMATE is now MEASURED on the unit FM-1_901 (bench17, tag B17, one core, 1 / 4 / 8 keys, slope of the average load over the keys, TEST SAW base): bench_runs/2026-11-02-B17/feat.csv, runs 'O1 TRI' .. 'ARP UP'; and bench_runs/2026-11-02-B17/extra.csv ('A12 + ..'). PHASE62_VOICE_COST_REPORT.md section 11 has the table. */
static const uint16_t BD_LR_MODEL[8] = {0, 0, 4, 14, 31, 0 /* SUPER: below */, 61, 0};   /* SAW TRI SINE PULSE SYNC SUPER FM FOLD, over SAW: measured +0 (TRI -2.8 and FOLD -0.3 us, charged 0), +0.4, +1.4, +3.1, +6.1 us (feat.csv 'O1 TRI' 'O1 SINE' 'O1 PULSE' 'O1 SYNC' 'O1 FM' 'O1 FOLD') */
#define BD_LR_BASE 283u         /* one plain SAW voice, all-in: 2026-11-12 measured 3.9 % = 28.3 us a voice (bench 1112H, LIGHT-FIT one core, keys 1 / 4 / 8, the slope of the average); was 320 (measured before the decode cache) */
#define BD_LR_OSC2 83u          /* a second oscillator rendered: measured +8.3 us SAW, +9.1 SINE (feat.csv 'O2 SAW', 'O2 SINE'; was 80 from the report) */
#define BD_LR_NOISE 53u         /* 2026-11-11: measured +5.3 us (2026-11-11 refit: bench 1111C, LRRY-COST one core, keys 1 / 4 / 8, the slope of the average over the clean keys only, scratch run cost/); was 72: measured +7.2 us (feat.csv 'NOISE'; was an estimate, 20) */
#define BD_LR_RING 59u          /* 2026-11-11: RING +14.2 us in all with O2 (2026-11-11 refit: bench 1111C, LRRY-COST one core, keys 1 / 4 / 8, the slope of the average over the clean keys only, scratch run cost/), less BD_LR_OSC2 8.3 = 5.9; was 35: RING measured +11.8 us in all with O2 level 0 (feat.csv 'RING'), of which BD_LR_OSC2 83 is charged with it (o2 below), so 35 here; was an estimate, 140 */
#define BD_LR_FA12 183u         /* 2026-11-12: +18.3 us (bench 1112H, LIGHT-FIT one core: cut 50 6.43 % a voice against the open filter 3.90 %); was 176. 2026-11-11: +17.6 us (2026-11-11 refit: bench 1111C, LRRY-COST one core, keys 1 / 4 / 8, the slope of the average over the clean keys only, scratch run cost/); was 217: filter A 12 dB at pure LP (MODE 0): measured +21.7 us (feat.csv 'A 12dB LP'; was 370, the MODE blend row) */
#define BD_LR_FAMIX 110u        /* filter A with the MODE blend away from pure LP (BAND-PASS 64): measured +32.7 us against +21.7 (feat.csv 'A 12dB BP'); also charged at 24 dB (NOT measured there) */
#define BD_LR_FA24 350u         /* 2026-11-11: +35.0 us, keys 1 and 4 (8 keys engaged the overload guard) (2026-11-11 refit: bench 1111C, LRRY-COST one core, keys 1 / 4 / 8, the slope of the average over the clean keys only, scratch run cost/); was 428: filter A 24 dB at pure LP: measured +42.8 us (feat.csv 'A 24dB LP'; was 570) */
static const uint16_t BD_LR_FB_SVF[7] = {0, 172, 261, 274, 271, 0, 0};   /* 2026-11-11: LP +17.2 us (TWIN LP +34.8 over the base with filter A 12 dB +17.6 on, keys 1 and 4) (2026-11-11 refit: bench 1111C, LRRY-COST one core, keys 1 / 4 / 8, the slope of the average over the clean keys only, scratch run cost/); BP HP NOTCH not re-measured, kept. Was: filter B SVF by P_TWTYPE (1 LP, 2 BP, 3 HP, 4 NOTCH): measured +22.1, +26.1, +27.4, +27.1 us (feat.csv 'B LP' 'B BP' 'B HP' 'B NOTCH'; was 370 for all) */
#define BD_LR_FB_LAD 326u       /* 2026-11-11: +32.6 us (TWIN LADDER +50.2 over the base, less filter A 12 dB 17.6, keys 1 and 4) (2026-11-11 refit: bench 1111C, LRRY-COST one core, keys 1 / 4 / 8, the slope of the average over the clean keys only, scratch run cost/); was 506: filter B LADDER: measured +50.6 us (feat.csv 'B LADDER'; was 530) */
#define BD_LR_FB_COMB 199u      /* filter B COMB: measured +19.9 us in steady state (feat.csv 'B COMB'; was 70, a fit). The note-on burst (BD_LR_BURST) is NOT re-measured: the isolated tests (extra.csv 'burst B COMB', window 0.05 to 0.4 s) show none on a sustained TEST SAW + COMB */
static const uint16_t BD_LR_DIRT[6] = {0, 138, 122, 114, 156, 172};   /* a dirt unit by type (1 SOFT, 2 HARD, 3 FOLD, 4 FUZZ, 5 CRUSH): measured +13.8, +12.2, +11.4, +15.6, +17.2 us, dirt A and B alike (feat.csv 'DIRT A ..', 'DIRT B SOFT', 'DIRT B CRUSH'; was 110 for all) */
#define BD_LR_SLOTV 2u          /* a matrix slot, per voice: measured +0.9 us a voice for 4 slots (extra.csv 'A12 + 4 slots' against 'A12 + LFO1>CUT'..; the part's own work is BD_MX_SLOT) */
#define BD_LR_MS 20u
#define BD_LR_SUB 67u           /* 2026-11-11: +6.7 us (2026-11-11 refit: bench 1111C, LRRY-COST one core, keys 1 / 4 / 8, the slope of the average over the clean keys only, scratch run cost/); was 106, SUB: measured +10.6 us (feat.csv 'SUB'; was 270, report +27) */
/* the split by voice. BD_LR_CTL: the control work of a voice that stays on core 0 when the voice's audio goes to core 1 (envelope, glide, pitch, matrix: lrry_voice_ctl), a constant, from the
 * two-core slopes of the quick bench (a key adds 22 us on TEST SAW, 27.6 on STEREO TEST, 60 on WIDE PAD: ctl + (voice - ctl) / 2, ctl 0 .. 12; 12 is used, the dearest).
 * BD_LR_HO0 / HO1: the hand-over a block, host instructions a sample (core 0 copies and hashes the 888-byte job context, core 1 hashes it again and clears and reads the accumulator), counted
 * from the code in L/RRY 4.3b (about 9 and 10 us); not measured on its own: the two-core fixed figure above holds it */
#define BD_LR_CTL 85u           /* tenths of us a block. L/RRY budget-real: DERIVED from the two-core rows of #16 TEST SAW (bench_runs/2026-11-01-NOLIM1/runs.csv, cores 2, keys 1 to 12, 8 rows: least squares on the
                                 * critical-path model below, c 32 us, fixed 111.5 us: ctl 7.1 us, HO0 42.4 us, HO1 1.6 us, largest error 0.14 points); was 12 us (a guess: "0 .. 12") */
#define BD_LR_HO0 235u          /* core 0's two-core overhead a block, host instructions a sample (42.4 us; same derivation. It is the whole hand-over AND the split's fixed work: 10 to 11 keys of #16 step by 4.4 points
                                 * on the odd key, 1.2 on the even one, the first pair costs 49 us against 32 for a lone key). Was 40 (8.8 us, counted from the code, not measured) */
#define BD_LR_HO1 5u            /* core 1's head of its job (1.6 us, same derivation; was 45 = 10 us, counted from the code) */
#define BD_LR_GLOBAL 1048u      /* tenths of us a block: the idle mix that always runs, WITHOUT the part's own fixed work (that is bd_part's base, misc ...). Measured fixed work of one block with a plain SAW:
                                 * 114 us (#16 TEST SAW one key: bench_runs/quick/2026-11-01_bench12/results.csv Q16_1C_N01 146 us less the voice 32; the intercept of the runs.csv one-core line of #16 is 113 us;
                                 * report section 8.2 stage timers, one key: 108 us without the voice and without the sends) less 2.5 us for the sends stage (report section 8.2), which LRRY_FX_SENDS 0 compiles out =
                                 * 111.5 us, less the model's own fixed work of #16 (17.0 us: budget_solve's F) = 94.5 us. The old figure was 49.7 us (317 host instructions at 71 %), 62 us too low */
#define BD_LR_BURST 215u        /* tenths of us: the note-on burst of a COMB voice (filter B COMB), extra PEAK over the average in the first 0.5 s, per voice on the core that plays it: runs.csv #07 COMB PLUCK, peak less
                                 * average less the steady peak, least squares over both modes (one core 1..9 keys: 3.16 points = 22.9 us, two cores 1..12 keys, core 0's voices: 2.91 points = 21.1 us; joint 2.96 points =
                                 * 21.5 us). Report 8.1 gives 69 us against 41 (+28) from the 12-key one-core run */
#define BD_LR_BURST0 246u       /* tenths of us: the same fit's constant, once for a sound that has a COMB voice (3.4 points = 24.6 us; one core 1.6, two cores 4.1) */
#define BD_LR_J1 710u           /* 1/100 %: the steady peak over the average, ONE core: mean of peak less average of the clean runs of #16 / #14 / #06 with 2 or more keys (25 rows, runs.csv): 3.77 points, 2.1 .. 5.1 */
#define BD_LR_J2 820u           /* .. two cores (29 rows, runs.csv): 5.36 points, 4.4 .. 6.5 */
#define BD_LR_FBMOD 5u          /* filter B's modulation (KEY TRK 2, ENV 2, LFO 1, LFO 2, a matrix route): measured +0.5 us, +0.6 for the route (feat.csv 'B LP + mod ENV2+LFO1', 'B LP + route CUT2'; was 20) */
static uint32_t bd_lr_osc(const int16_t *x, uint32_t o)   /* oscillator o (0, 1): its model's cost over a SAW */
{
    uint32_t m = (uint32_t)clamp(x[o ? X_O2MODEL : X_O1MODEL], 0, 7), n;
    if (m != 5u)
        return BD_LR_MODEL[m];
    n = 1u + (((uint32_t)clamp(x[(o ? X_O2P2 : X_O1P2)], 0, 127) * 6u + 63u) / 127u);
    return 85u * n - 20u;                               /* SUPER: measured 3 saws +23.5 us, 7 saws +56.8 (feat.csv 'O1 SUPER3', 'O1 SUPER7'): 85 a saw less 20 (was 90 a saw less 70) */
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
    c += sl && p[P_FREEC] != 0 ? BD_LR_FAMIX : 0u;
    if (ty) {
        c += ty == 5u ? BD_LR_FB_LAD : ty == 6u ? BD_LR_FB_COMB : BD_LR_FB_SVF[ty];
        if (x[X_KTRK2] || x[X_ENV2] || x[X_LFO1] || x[X_LFO2])
            c += BD_LR_FBMOD;
        for (i = 0; i < NMSLOT; i++)
            if (p[MSLOT_ID(i)] && p[MSLOT_ID(i) + 1u] == MD_CUT2 && p[MSLOT_ID(i) + 2u]) {
                c += BD_LR_FBMOD;
                break;
            }
    }
    c += BD_LR_DIRT[clamp(x[X_DATYPE], 0, 5)] + BD_LR_DIRT[clamp(x[X_DBTYPE], 0, 5)];
    for (i = 0; i < NMSLOT; i++)
        if (p[MSLOT_ID(i)] && p[MSLOT_ID(i) + 1u] && p[MSLOT_ID(i) + 2u])
            c += BD_LR_SLOTV;
    c += x[X_SUBLVL] > 0 ? BD_LR_SUB : 0u;
    return BD_LR_T(c);
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
    if (p[P_VOICE] == V_MONO || p[P_VOICE] == V_LEGATO)
        r->cap = 1;                                     /* MONO and LEGATO play one voice (measured: #28, #11, #03 mono, no miss to 12 keys on either core mode) */
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
    r->bst = clamp(p[P_TWTYPE], 0, 6) == 6 ? BD_LR_T(BD_LR_BURST) : 0u;   /* COMB on filter B: the note-on burst (BD_LR_BURST) */
    r->bst0 = r->bst ? BD_LR_T(BD_LR_BURST0) : 0u;
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
static uint8_t bd_lr_sends;     /* (L/RRY: the send effects run and a part sends to one of them: budget_solve2's LRRY_FXCAP) */
static void bd_global(uint32_t *idle, uint32_t *fx)     /* host instructions a sample: the idle mix, and what the sends and the master stages add */
{
    uint32_t m = 0u /* (L/RRY: always the STEREO column) */, dm = (uint32_t)song.g[G_DMODE], rt = (uint32_t)song.g[G_RTYPE], p, cho = 0, dly = 0, rev = 0, mod;
    int32_t t = 0, lrb = 0;
    *idle = BD_IDLE[m];
    for (p = 0; p < BD_N; p++)
        cho |= !trk[p].p[P_MUTE] && trk[p].p[P_CHOR] != 0, dly |= !trk[p].p[P_MUTE] && trk[p].p[P_DLY] != 0, rev |= !trk[p].p[P_MUTE] && trk[p].p[P_REV] != 0;
    bd_lr_sends = LRRY_FX_SENDS && !(mods_off & MODS_SENDS) && (cho | dly | rev);
    if (LRRY_FX_SENDS && !(mods_off & MODS_SENDS)) {   /* (core.h LRRY_FX_SENDS 0: no buses compiled in, nothing charged: the idle mix and the master stages below only) */                     /* (SENDS OFF: the buses do not run) */
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
#if LRRY_ENGINE
        {   /* (L/RRY: the unit's measured rows, BD_LR_REV above, in place of the Mac rows; only SHIM DOWN / MOD's extra is still the Mac rows' at 71 %) */
            int32_t sx = 0;
            if (rt == 3u && song.g[G_SHPITCH] == SHP_DOWN)
                sx = (mod ? (int32_t)BD_SHDM : (int32_t)BD_SHD[0]) - (int32_t)BD_REV[0][3];
            else if (rt == 3u && mod)
                sx = (int32_t)BD_SHM[0] - (int32_t)BD_REV[0][3];
            lrb = (int32_t)BD_LR_REV[rt] - (rev ? 0 : (int32_t)BD_LR_REVIDLE) + (dly || dm >= 2u ? (int32_t)BD_LR_DLY[dm] : 0) + (cho ? (int32_t)BD_LR_CHO : 0) +
                  (sx > 0 ? (sx * (int32_t)BD_LR_FIXED_PCT + 50) / 100 : 0);
            t = 0;
        }
#endif
    }
    if (!m) {                                           /* the master stages (none runs in DUEL) */
        t += (cmp_cfg & 0xF00u) && !(byp_sh & BYS_CMP) ? (int32_t)BD_COMP : 0;   /* (item 116: the master and compressor BYPASS measured as skipping all three, -146) */
        t += wd_sel != 100u && !(byp_sh & BYS_MST) ? (int32_t)BD_WIDTH : 0;
        t += bm_sel && !(byp_sh & BYS_MST) ? (int32_t)BD_BMONO : 0;
    }
#if LRRY_VELO
    t += sense_running() ? 57 : 0;                      /* KEY SENSE (after Velo's method; est.: the key scan is not on the CPU bar): a fixed core-0 load of 2 % after the margin, 57 host instructions a sample, not measured */
#endif
#if LRRY_ENGINE
    *idle = BD_LR_T(BD_LR_GLOBAL);                      /* the unit's measured fixed work a block (BD_LR_GLOBAL: 94.5 us + the part's 17.0 = 111.5 us with the sends compiled out; the old Mac idle row is not used) */
    t = (t * (int32_t)BD_LR_FIXED_PCT + 50) / 100 + lrb;   /* (lrb: the buses, measured, already in host instructions) */
    if (!m)                                             /* (2026-11-12: WIDTH and BASS MONO at their unit measurement, in place of the Mac rows at 71 %; COMP only when LRRY_COMP builds it back) */
        t += (wd_sel != 100u && !(byp_sh & BYS_MST) ? (int32_t)BD_LR_WIDTH - ((int32_t)BD_WIDTH * (int32_t)BD_LR_FIXED_PCT + 50) / 100 : 0) +
             (bm_sel && !(byp_sh & BYS_MST) ? (int32_t)BD_LR_BMONO - ((int32_t)BD_BMONO * (int32_t)BD_LR_FIXED_PCT + 50) / 100 : 0);       /* what is left of t: the buses (only with LRRY_FX_SENDS 1) and COMP / WIDTH / BASS MONO / KEY SENSE, the old Mac rows at 71 %: ESTIMATES, not measured on the unit */
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
static uint32_t bd_worst(const uint32_t *c, const uint32_t *n)   /* both parts' notes (L/RRY 1.1 item 90: each part has its own NVOICE voices, no shared pool any more) */
{
    return n[0] * c[0] + n[1] * c[1];
}
/* L/RRY 4.9 (owner, lrry-0.1.0-test1: two voices shown, the second held note cut after a second or two), reworked by budget-real (owner: "show the maximum": the counter is the TRUE MAXIMUM of voices the unit
 * plays without a missed half, one core and two cores). Fitted to PHASE62_VOICE_COST_REPORT.md section 10 and bench_runs/2026-11-01-NOLIM1/runs.csv (the load of a 725 us block, average and peak, missed halves).
 * What the data say: misses follow the PEAK load, not the average (clean runs peaked at 83.1 % or less, first misses at 87.1 % or more on one core, 83.1 % on two cores).
 *   average   two cores  A(k) = G + F + min(k x c, HO0 + k x cc + max(n0 x a, HO1 + n1 x a))     n1 = floor(k / 2) voices on core 1, n0 = k - n1 (core 0 waits for core 1: its half is the critical path); k = 1: one core's rule
 *             (cores-balance: the runner splits only when the split costs less than one core, voice.c lrry_cost: the min. Cheap voices stay on core 0 up to 3 plain SAW voices; from 4 on, and always for a typical
 *             128 voice, the split is the smaller term, so the counter's numbers at the limit are those of the formula as before. The shares: balanced by cost, floor(k / 2) of equal voices on core 1)
 *             one core   A(k) = G + F + k x c
 *   peak      P(k) = A(k) + J + (the sound has a COMB voice ? BURST0 + BURST x (voices the core plays: k on one core, n0 on two) : 0)
 * G the globals (BD_LR_GLOBAL), F the part's fixed work, c a voice (bd_lrry_voice: the measured feature rows, tenths of us, BD_LR_T), cc its control share (BD_LR_CTL, core 0's before the post), a = c - cc its audio;
 * c, cc, a, HO0, HO1 and the burst taken at the unit's dev (bd_dev_pct, 1.00 until the measured voice says dearer). J = LRRY_J1 / LRRY_J2 (BD_LR_J1, BD_LR_J2: the steady peak over the average).
 * THE GATE: the shown number is the largest k (never below 1: one note always plays) with
 *   one core   P(k) <= LRRY_CP_LIM1 = 85.1 %   = the lowest first-miss peak of one core in runs.csv (87.1 %: #07 COMB PLUCK 8 keys, first sweep, 3 missed halves) less LRRY_MARGIN
 *   two cores  P(k) <= LRRY_CP_LIM  = 81.1 %   = the lowest first-miss peak of two cores in runs.csv (83.1 %: #07 COMB PLUCK 12 keys, repeat 2, 1 missed half) less LRRY_MARGIN
 * LRRY_MARGIN = 2 points: the report (section 10.2, 2) says the average repeats within 0.1 point and the PEAK varies by up to 2 points between repeats of the same step (#07 one core 8 keys: 87.1, 89.8, 88.1,
 * of which two missed and one was clean); nothing else is added, so the counter is as high as the data allow. Not yet verified on the unit with the 128 sounds */
/* cores-pipeline (LRRY_PIPELINE, twocore_run.c): the two cores work side by side the whole block, so the two-core model changes (the rule value TWO = 2 in bd_lr_peak / budget_solve2 / bd_measure; 1 is the plain
 * runner above, kept for LRRY_PIPELINE 0 and compared by tests/lrry_budget_test.c, which compiles both with BD_BOTH_MODELS):
 *   core 0 a block   W0 = G + F + HO0 + k x cc + n0 x a      (the idle mix and the part's fixed work, the hand-over, every voice's control, its own voices' audio: the mix, master and DAC write of the block
 *                                                             before run here while core 1 renders this block's voices, and the control of the next block follows)
 *   core 1 a block   W1 = HO1 + n1 x a
 *   the block        max(W0, W1)      (the longer core is the cycle; there is no wait inside it: core 1's job runs through core 0's mix, master and DAC write and the control of the next block, the chooser,
 *                                      voice.c lrry_split_choose, takes n1 as large as balances them, at most LRRY_C1_MAX)
 *   the runner splits only when that is below one core (G + F + k x c); k = 1: one core's rule. The peak adds J2 and the COMB burst as before. Not refitted to single patches: the rows are those of the plain model.
 * Predicted voices (this model, NOT measured on the unit): c = 165 us 5 (plain 4), c = 124 us 6 (plain 5), c = 91 us 9 (plain 8); eight voices fit up to c = 116 us (plain: 91) (tests/lrry_budget_test.c 5) */
#if !defined(BD_BOTH_MODELS)
#define BD_BOTH_MODELS 0
#endif
#define BD_PIPE_CODE (LRRY_PIPELINE || BD_BOTH_MODELS)
#if LRRY_PIPELINE
#define BD_TWO_NOW() (bd_two_now() ? 2u : 0u)            /* the rule the runner is in: 2 = two cores, pipelined */
#else
#define BD_TWO_NOW() bd_two_now()                       /* (the plain build: as it was, byte for byte) */
#endif
#define LRRY_DEV_PCT 100u       /* the rows are the unit's own measurement (PHASE62_VOICE_COST_REPORT.md), so no extra margin (was 125 on the old Mac estimate); a unit dearer than the rows still raises it (bd_measure) */
#define LRRY_MARGIN 500u        /* 1/100 %: 5 points. 2026-11-13 (owner: the users "can go to the wildest fucking settings and it still should work"): was 2 points, taken from the spread of
                                 * the runs that fitted the gates. Two runs of the same eleven tests on the same build (bench 1113C and 1113D) moved a peak by up to 2.9 points (#047 MOLTEN PAD 83.0 then
                                 * 85.9), which is more than the margin covered, and a voice is shed from about 84 % up: MOLTEN PAD and SAW BASS shed in the run where they crossed it and stayed clean in the
                                 * other. 5 points covers that spread and keeps the heaviest patches' measured peak under 84 % */
#define LRRY_CP_LIM (7750u - LRRY_MARGIN)    /* 2026-11-11 (owner yes): two cores: 77.5 % = the lowest two-core peak at which the overload guard engaged (LRRY-RANDOM seed 1771, 5 keys, bench 1111H; the
                                     * highest clean one 78.5 %) less the margin = 75.5 %. Was 8640 - margin (84.4 %): the screen showed 6 where 4 played clean. Before: budget-real2: two cores: 80.1 % = the lowest first-miss peak of two cores in bench_runs/2026-11-02-B17/sounds.csv (#66 WARM BLANKET 3 keys, 1 late half: 74.1 / 80.1) less the margin = 78.1 %; was 83.1 (NOLIM1 #07) */
#define LRRY_CP_LIM_OLD2 (8310u - LRRY_MARGIN)    /* two cores: 83.1 % (runs.csv, #07 two cores 12 keys, repeat 2) less the margin = 81.1 % */
#define LRRY_CP_LIM1 (8060u - LRRY_MARGIN)   /* 2026-11-11: one core: 80.6 % = the lowest peak at which the overload guard engaged in the refit runs (DIRT A+B SERIAL 8 keys, scratch run clk2; the same
                                     * row stayed clean at 80.3 % in run cost) less the margin = 78.6 %. Was 7500 - margin: budget-real2: one core: 79.5 % = the lowest first-miss peak of one core in bench_runs/2026-11-02-B17/sounds.csv (#117 RISER SAW 2 keys, 1 late half: 75.8 / 79.5) less the margin = 77.5 %; was 87.1 (NOLIM1 #07) */
#define LRRY_CP_LIM1_OLD (8710u - LRRY_MARGIN)   /* one core: 87.1 % (runs.csv, #07 one core 8 keys, first sweep) less the margin = 85.1 % */
/* L/RRY phase 56 (owner, lrry-0.1.0-test2: "#08 CLAVINET: one note held, a second note cuts the first off, the screen says voices are still free"). The host model holds two CLAVINET notes up to
 * a unit 1.45 x dearer than the estimate; a dearer unit sheds the first one (audio.c shed_voice: two halves over 85 %), and the screen's number, the estimate's, stayed 2. THE MEASURED VOICE: while the
 * same sound plays (the key: its estimated voice and fixed costs) with the same number of voices for MEAS_MS, budget_poll reads the audio meter (song.cpu_q8, the render's share of a half,
 * core 0's wait for core 1 included) and works out how much dearer the unit is than this model: dev = (measured - G - F) / (the voices' share at 1.00), the same formula as below. The largest
 * dev seen for the sound is kept (it only rises; another sound starts again) and the numbers use it in place of LRRY_DEV_PCT when it is higher. So after one note has been held for MEAS_MS the
 * number is the unit's own, and a second note the unit cannot hold is not offered (the allocator gives it the first note's voice, as at any number). Nothing is stored */
#define LRRY_MEAS_MS 200u       /* a steady voice count this long before the meter is read (the meter averages ~93 ms, audio.c CPU_AVG) */
static uint32_t bd_meas_key, bd_meas_t;
static uint16_t bd_meas_pct;    /* the measured dev of the sound, percent; 0 = none yet */
static uint8_t bd_meas_n;       /* the voices sounding at bd_meas_t */
/* (2026-11-11, owner: SPACE CHORD showed 2 voices, "a stupid low number for a chord patch". The dev only rose and kept readings taken in an overload, so one bad moment
 * held the number down for the whole sound. Now: a reading is taken only from a clean window (the meter under LRRY_MEAS_OK, the overload guard neither engaged nor
 * tripped during it), the last LRRY_MEAS_N clean readings are kept and the dev is the largest of them: it follows the sound up at once and comes down again after
 * LRRY_MEAS_N clean windows that say it is cheaper) */
#define LRRY_MEAS_N 4u
#define LRRY_MEAS_OK 8500u      /* 1/100 %: 85 %, the overload rule's line (audio.c shed_check): a window at or above it measures the overload, not the voices */
static uint16_t bd_meas_ring[LRRY_MEAS_N];
static uint8_t bd_meas_w;
static uint32_t bd_meas_ovl;    /* the overload guard's count at bd_meas_t (fx.c ovl_count) */
/* 2026-11-12 (bench 1112C, the limit ON): the measured dev OFF for the numbers. It charges the part's whole fixed work (decode, control, the pipeline's hand-over, all left out of
 * G + F) to the voices, so with 2 voices it read the unit as about 2 x dearer than the rows: the number fell to the one-core size (random patches: the screen 2 where 4 play
 * clean, DIRT FOLD 6 where 10 do) and, measured again at that low count, never came back. The rows and gates alone were checked on the unit the same night (LRRY-CHECK,
 * 6 new random patches, everything on: the number never above what plays clean, 5 exact, 1 one under); the overload rule (audio.c shed_check) stays as the safety net.
 * LRRY_MEAS_GATE 1 brings the measured dev back */
#ifndef LRRY_MEAS_GATE
#define LRRY_MEAS_GATE 0
#endif
static uint32_t bd_dev_pct(void) { return LRRY_MEAS_GATE && bd_meas_pct > LRRY_DEV_PCT ? bd_meas_pct : LRRY_DEV_PCT; }
#if LRRY_ENGINE
/* budget-real: the predicted peak load (1/100 % of a 725 us block) of k voices of the part r, at dev percent (bd_dev_pct): the model of the comment above. g: the globals (BD_U(idle + fx)). two: the two-core rule */
static uint32_t bd_lr_peak(const bd_part_t *r, uint32_t g, uint32_t k, uint32_t two, uint32_t dev)
{
    uint32_t c = BD_U(r->eng + r->filt + r->ext), fx = BD_U(r->ins + r->sat + r->eq + r->misc + r->base);
    uint32_t cu = (c * dev + 99u) / 100u, ccu = BD_U(BD_LR_T(BD_LR_CTL)) < cu ? BD_U(BD_LR_T(BD_LR_CTL)) : cu, au = cu - ccu;
    uint32_t bu = (BD_U(r->bst) * dev + 99u) / 100u, b0 = (BD_U(r->bst0) * dev + 99u) / 100u, cp, nb = k;
    uint32_t n1 = k / 2u, n0 = k - n1, h0 = (BD_U(BD_LR_HO0) * dev + 99u) / 100u, h1 = (BD_U(BD_LR_HO1) * dev + 99u) / 100u;
    uint32_t cs = h0 + k * ccu + (n0 * au > h1 + n1 * au ? n0 * au : h1 + n1 * au);   /* the split: hand-over, every voice's control, then the longer side (core 0 waits for core 1 before the part's rest and the globals) */
#if BD_PIPE_CODE
    if (two == 2u && k >= 2u) {                          /* the pipelined runner (above): the longer core's work is the block; n1 as large as balances the cores */
        uint32_t best = 0xFFFFFFFFu, n, w0, w1, m, f0 = g + fx + h0 + k * ccu, nbest = k, lim1 = k < LRRY_C1_MAX ? k : LRRY_C1_MAX;
        for (n = 1; n <= lim1; n++) {
            w0 = f0 + (k - n) * au;
            w1 = h1 + n * au;
            m = w0 > w1 ? w0 : w1;
            if (m < best) {
                best = m;
                nbest = k - n;
            }
        }
        if (best < g + fx + k * cu) {
            cp = best + BD_LR_J2;
            nb = nbest;
        } else
            cp = g + fx + k * cu + BD_LR_J2;
        return cp + (r->bst ? b0 + nb * bu : 0u);
    }
#endif
    if (two && k >= 2u && cs < k * cu) {                 /* (one voice: the split posts nothing, lrry_sp_t min_total. cores-balance: and the runner splits only when that costs less than one core: voice.c lrry_cost) */
        cp = g + fx + cs + BD_LR_J2;
        nb = n0;
    } else
        cp = g + fx + k * cu + (two ? BD_LR_J2 : BD_LR_J1);
    return cp + (r->bst ? b0 + nb * bu : 0u);
}
/* 2026-11-12 (owner: "Yes, fit and check"; bench 1112N MEASURE-FIT, 1112O RECHECK-O): with the send effects running, the voices' total audio work k x au (au: bd_lr_peak's, a voice less its
 * control share) decides the chord start on two cores: every sound that played its number clean was at or under 72.6 points (M1 8 keys 7264, M2 5 keys 7040, SBX 3 keys 6876, A12 11, OPEN 13), the ones that
 * shed a voice at the chord start over it (#03 SAW BASS poly 4 keys 7400: one run of two, M1 9 keys 8172: both). No split helps them: core 0's 15 points outside core 1's window (voice.c LRRY_SP ho1).
 * Effects OFF the same sounds played clean up to 9080 (M1 10 keys), so the cap is for the sends only */
#define LRRY_FXCAP 7300u
static uint32_t bd_lr_au(const bd_part_t *r)
{
    uint32_t cu = (BD_U(r->eng + r->filt + r->ext) * bd_dev_pct() + 99u) / 100u, ccu = BD_U(BD_LR_T(BD_LR_CTL));
    return cu > ccu ? cu - ccu : 0u;
}
#endif
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
    for (p = 0; p < BD_N; p++) {                        /* L/RRY budget-real: the shown number is the most notes whose predicted peak stays under the gate (LRRY_CP_LIM / LRRY_CP_LIM1, above) */
        uint32_t k;
        if (!b->p[p].on) {
            out[p] = b->p[p].cap;
            continue;
        }
        for (k = b->p[p].cap; k > 1u; k--)
            if (bd_lr_peak(&b->p[p], g, k, two, bd_dev_pct()) <= (two ? LRRY_CP_LIM : LRRY_CP_LIM1) && !(two && bd_lr_sends && k * bd_lr_au(&b->p[p]) > LRRY_FXCAP))
                break;
        out[p] = k;
    }
    return;
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
static void budget_solve(const bd_t *b, uint32_t *out) { budget_solve2(b, out, BD_TWO_NOW()); }   /* the rule in force (2 for the pipelined runner) */

/* the two-core rule is in force: core 1 answered at power-on, has not failed, its jobs are not paused (a flash park is short: the runner trims to the one-core numbers for it) */
static uint32_t bd_two_now(void)
{
#if LRRY_TWOCORE
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
static uint8_t part_notes_shown(int part)             /* the number the header shows (2026-11-12: the gate's own, bnotes, which budget_poll left; part_notes is the model without the runner's state) */
{
    if (part >= 0 && part < (int)BD_N && bnotes[part])
        return bnotes[part];
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

#if LRRY_ENGINE
/* phase 56: the measured voice (above budget_solve2). n voices of part 0 sound (active, not fading out); two: the two-core rule is in force. dev at 1.00 x the rows:
 *   one core (or fewer than two voices: the split posts nothing, lrry_sp_t min_total)  m = G + F + dev x n x c
 *   two cores                                                                         m = G + F + dev x (HO0 + n x cc + max(n0 x a, HO1 + n1 x a))    n1 = floor(n / 2) */
static void bd_measure(const bd_t *b, uint32_t two)
{
    const bd_part_t *r = &b->p[0];
    uint32_t key, n = 0, i, c, cc, a, g, fx, x, m, dev;
    if (!r->on)
        return;
    c = BD_U(r->eng + r->filt + r->ext);
    fx = BD_U(r->ins + r->sat + r->eq + r->misc + r->base);
    g = BD_U(b->idle + b->fx);
    key = c * 2654435761u ^ fx * 40503u ^ g;
    if (key != bd_meas_key) {                           /* another sound (or a change of its cost): start again */
        bd_meas_key = key;
        bd_meas_pct = 0;
        bd_meas_n = 0xFFu;
        memset(bd_meas_ring, 0, sizeof bd_meas_ring);
        bd_meas_w = 0;
    }
    for (i = 0; i < NVOICE; i++)
        n += trk[0].v[i].active && trk[0].v[i].stage != 4u;
    if (n != bd_meas_n || ovl_lite || ovl_count != bd_meas_ovl) {   /* (a new count, or the overload guard engaged / tripped: the window starts again) */
        bd_meas_n = (uint8_t)n;
        bd_meas_t = fm1_ms;
        bd_meas_ovl = ovl_count;
        return;
    }
    if (!n || fm1_ms - bd_meas_t < LRRY_MEAS_MS)
        return;
    bd_meas_t = fm1_ms;                                 /* (the next reading is a new window of LRRY_MEAS_MS) */
    cc = BD_U(BD_LR_T(BD_LR_CTL)) < c ? BD_U(BD_LR_T(BD_LR_CTL)) : c;   /* (budget-real: in the table's unit, 1/100 %: the host-instruction figure was used as if it were that, 3.5 x too small) */
    a = c - cc;
    if (two && n >= 2u) {
        uint32_t n1 = n / 2u, n0 = n - n1;
        x = BD_U(BD_LR_HO0) + n * cc + (n0 * a > BD_U(BD_LR_HO1) + n1 * a ? n0 * a : BD_U(BD_LR_HO1) + n1 * a);
#if BD_PIPE_CODE
        if (two == 2u) {                                /* the pipelined runner: the longer core's work less the globals and the part's fixed work, as bd_lr_peak */
            uint32_t best = 0xFFFFFFFFu, nn, w0, w1, m, lim1 = n < LRRY_C1_MAX ? n : LRRY_C1_MAX;
            for (nn = 1; nn <= lim1; nn++) {
                w0 = g + fx + BD_U(BD_LR_HO0) + n * cc + (n - nn) * a;
                w1 = BD_U(BD_LR_HO1) + nn * a;
                m = w0 > w1 ? w0 : w1;
                best = m < best ? m : best;
            }
            x = best > g + fx ? best - g - fx : 0u;
        }
#endif
        if (x > n * c)
            x = n * c;                                  /* (cores-balance: the runner does not split when that costs more than one core) */
    } else
        x = n * c;
    m = song.cpu_q8 * 10000u / 256u;                    /* (1/100 % of a half, the rows' unit) */
    if (!x || m <= g + fx || m >= LRRY_MEAS_OK)
        return;                                         /* (an overloaded window measures the overload: not taken) */
    dev = (m - g - fx) * 100u / x;
    if (dev > 400u)
        dev = 400u;                                     /* (a meter spike, a flash write's wait: at most 4 x) */
    bd_meas_ring[bd_meas_w] = (uint16_t)dev;            /* the last LRRY_MEAS_N clean readings; the dev is their largest */
    bd_meas_w = (uint8_t)((bd_meas_w + 1u) % LRRY_MEAS_N);
    for (i = 0, dev = 0; i < LRRY_MEAS_N; i++)
        dev = bd_meas_ring[i] > dev ? bd_meas_ring[i] : dev;
    bd_meas_pct = (uint16_t)dev;
}
#endif
static void budget_poll(void)                           /* main loop, every frame (main.c, beside vcap_poll) */
{
    bd_t b;
    uint32_t n[2], p;
    /* (2026-11-11, owner: the screen said 12 on a bench build while 3 voices overloaded the unit. LRRY_NOLIMIT keeps the gate open, voice.c trk_nvoice ignores these numbers,
     * but they are worked out as in every build, so the header shows the real number: voice.c trk_nvoice_shown) */
#if LRRY_BENCH
    if (bench_bopen) {                                 /* (bench builds: a cost row of src/bench.c holds the budget open: no numbers, every voice the
                                                        * VOICE CAP and the pool give, as the bench of 0.2.25 measured) */
        for (p = 0; p < BD_N; p++)
            bnotes[p] = bnotes1[p] = 0;
        return;
    }
#endif
    bd_build(&b);
    lrry_sp_gx = (uint16_t)(b.fx < 65535u ? b.fx : 65535u);   /* (the voice split's view of core 0's effects and master stages: voice.c lrry_sp_gx) */
#if LRRY_ENGINE
    bd_measure(&b, BD_TWO_NOW());                       /* (phase 56: the unit's own voice cost, before the numbers) */
#endif
    budget_solve2(&b, n, 0);
    for (p = 0; p < BD_N; p++)
        bnotes1[p] = (uint8_t)n[p];                     /* the one-core numbers, always (the runner falls back to them at once) */
    if (bd_two_now())
        budget_solve2(&b, n, BD_TWO_NOW());             /* (2026-11-12, bench 1112D: this said 1, the old non-pipelined split with its 42 us hand-over and a half-and-half split, while the runner is
                                                         * pipelined (2) and the header's model (part_notes) used 2: the gate gave 2 notes where the pipelined model, checked on the unit (LRRY-CHECK), gives 4) */
    for (p = 0; p < BD_N; p++)
        bnotes[p] = (uint8_t)n[p];
}
