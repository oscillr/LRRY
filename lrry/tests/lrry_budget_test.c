/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY budget-real: the NOTES budget (budget.c budget_solve2, bd_lr_peak) = the TRUE MAXIMUM of voices without a missed half, for the unit's own measurements. Checks:
 *  1  the six feature scenes: the notes on one core and with two cores (pinned figures; the model is feature based, so these are the sums of the measured feature rows);
 *  2  every measured row of bench_runs/2026-11-01-NOLIM1/runs.csv (the old test patches TEST SAW, STEREO TEST, WIDE PAD, COMB PLUCK: their model rows are pinned below as the part's items; the bank
 *     of the 128 sounds does not hold them): the model's average and peak, the counter, the measured maximum of clean keys; the counter is never above the measured maximum, and the rows where it is below are listed;
 *  3  the fixed cost with LRRY_FX_SENDS 0 against the measured 111.5 us (114 us less the sends stage's 2.5 us, PHASE62 section 8.2);
 *  4  crazy patches: every feature at its dearest option gives a counter of at least 1 and no more than any single feature, and turning any feature on never raises the counter (one core and two cores).
 *  5  cores-pipeline: the pipelined two-core model (rule 2) against the plain one (rule 1) for a synthetic voice of c us (#16 TEST SAW's fixed work and the measured globals): voices at c = 165, 124 and 91 us, the
 *     dearest voice at which eight fit, never fewer voices than the plain model, never more when the voice gets dearer. MODEL values, not measured on the unit.
 * Build (from lrry): ../onepart.sh 1 1 lrry_budget */
#define BD_BOTH_MODELS 1                                /* cores-pipeline: budget.c compiles both two-core models here (the plain runner's, rule 1, and the pipelined one, rule 2) whatever LRRY_PIPELINE is */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "lrry_scn.h"

static int nfail, ncheck;
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_budget: %-110s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}
static void c_basic(track_t *t) { scn_x(t, X_O1LVL, 127); }
static void c_detuned(track_t *t) { scn_x(t, X_O1LVL, 100); scn_x(t, X_O2LVL, 90); scn_x(t, X_O2COARSE, 7); scn_x(t, X_O2FINE, 13); scn_x(t, X_O1PAN, -40); scn_x(t, X_O2PAN, 30); }
static void c_filter_a(track_t *t)
{
    scn_x(t, X_O1LVL, 100); scn_x(t, X_O1MODEL, 5); scn_x(t, X_O1PAN, -30); scn_x(t, X_FAPAN, 35);
    t->p[P_FREEA] = 40; t->p[P_FREEB] = 70; t->p[P_FREEC] = 0; t->p[P_FREED] = 2;
}
static void c_filter_ab_par(track_t *t)
{
    scn_x(t, X_O1LVL, 100); scn_x(t, X_O2LVL, 100); scn_x(t, X_O2COARSE, 7); scn_x(t, X_O1BAL, -30); scn_x(t, X_O2BAL, 40); scn_x(t, X_O1PAN, -20); scn_x(t, X_O2PAN, 20);
    t->p[P_FREEA] = 50; t->p[P_FREEB] = 40; t->p[P_FREEC] = 64; t->p[P_FREED] = 1;   /* (phase 52: the band-pass is 64 of L/RRY's blend, it was 1 of the four-way choice) */
    t->p[P_TWTYPE] = 3; t->p[P_CUT2] = 35; t->p[P_RES2] = 50; scn_x(t, X_FBPAN, -45); scn_x(t, X_FAPAN, 45);
}
static void c_ladder(track_t *t) { scn_x(t, X_O1LVL, 100); scn_x(t, X_O1MODEL, 0); t->p[P_TWTYPE] = 5; t->p[P_CUT2] = 45; t->p[P_RES2] = 90; scn_x(t, X_FBPAN, 25); }
static void c_dirt_par(track_t *t)
{
    scn_x(t, X_O1LVL, 90); scn_x(t, X_O2LVL, 90); scn_x(t, X_O2COARSE, 12); t->p[P_FREEA] = 55; t->p[P_FREEB] = 30; t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 60;
    scn_x(t, X_DATYPE, 1); t->p[P_DRV2] = 80; scn_x(t, X_DAPAN, -35); scn_x(t, X_DALVL, 100);
    scn_x(t, X_DBTYPE, 3); scn_x(t, X_DBDRV, 40); scn_x(t, X_DBPAN, 35); scn_x(t, X_DBLVL, 90);
}
/* the old test patches as the model has them (bd_part items of tools/lrry_sounds.py's #16 TEST SAW, #14 STEREO TEST, #06 WIDE PAD, #07 COMB PLUCK; the bank of the 128 sounds does not hold them), pinned from
 * bd_part on 2026-11-02 (budget-real2: re-pinned with the measured rows): voice (eng), the morph (ext), the rest of the part's fixed work (misc), the COMB burst (bst, bst0) */
static const struct { const char *n; uint32_t eng, ext, misc, bst, bst0; } PT[4] = {
    {"#16 TEST SAW", 146, 0, 20, 0, 0}, {"#14 STEREO TEST", 190, 0, 20, 0, 0}, {"#06 WIDE PAD", 545, 6, 100, 0, 0}, {"#07 COMB PLUCK", 271, 6, 80, 97, 111},
};
/* every measured row of bench_runs/2026-11-01-NOLIM1/runs.csv, repeats joined: patch (PT), cores, keys, average load % (mean), peak load % (the highest), missed halves (the most of the repeats), repeats clean, repeats */
static const struct { uint8_t pat, cores, keys; double avg, peak; int late, nclean, nrun; } MR[] = {
    {0, 1,  1,  15.4,  57.6,   0, 1, 1},
    {0, 1,  2,  24.5,  28.9,   0, 1, 1},
    {0, 1,  4,  33.5,  38.1,   0, 1, 1},
    {0, 1,  6,  42.3,  46.4,   0, 1, 1},
    {0, 1,  8,  51.1,  55.7,   0, 1, 1},
    {0, 1, 10,  59.9,  64.2,   0, 1, 1},
    {0, 1, 11,  64.4,  68.8,   0, 3, 3},
    {0, 1, 12,  68.9,  73.2,   0, 3, 3},
    {1, 1,  1,  20.2,  24.6,   0, 1, 1},
    {1, 1,  2,  26.5,  30.1,   0, 1, 1},
    {1, 1,  4,  38.3,  42.5,   0, 1, 1},
    {1, 1,  6,  50.2,  54.3,   0, 1, 1},
    {1, 1,  8,  62.1,  65.5,   0, 1, 1},
    {1, 1, 10,  74.5,  77.4,   0, 3, 3},
    {1, 1, 11,  80.4,  83.1,   0, 3, 3},
    {1, 1, 12,  85.9,  88.5,   2, 0, 3},
    {2, 1,  1,  33.9,  39.5,   0, 1, 1},
    {2, 1,  2,  51.0,  55.4,   0, 1, 1},
    {2, 1,  3,  68.1,  73.2,   0, 3, 3},
    {2, 1,  4,  84.1,  87.7,   2, 0, 3},
    {2, 1,  5, 103.6, 108.5, 864, 0, 3},
    {3, 1,  1,  21.4,  30.7,   0, 1, 1},
    {3, 1,  2,  27.0,  39.0,   0, 1, 1},
    {3, 1,  4,  38.2,  56.7,   0, 1, 1},
    {3, 1,  6,  49.3,  74.2,   0, 3, 3},
    {3, 1,  7,  54.7,  80.3,   0, 2, 2},
    {3, 1,  8,  59.6,  89.8,   4, 1, 3},
    {3, 1,  9,  64.8, 102.0,  54, 0, 3},
    {0, 2,  1,  20.2,  24.1,   0, 1, 1},
    {0, 2,  2,  27.0,  33.5,   0, 1, 1},
    {0, 2,  4,  32.3,  38.3,   0, 1, 1},
    {0, 2,  6,  37.6,  43.1,   0, 1, 1},
    {0, 2,  8,  43.0,  48.2,   0, 1, 1},
    {0, 2, 10,  48.5,  54.6,   0, 1, 1},
    {0, 2, 11,  52.9,  59.0,   0, 2, 2},
    {0, 2, 12,  54.1,  60.4,   0, 3, 3},
    {1, 2,  1,  20.6,  24.9,   0, 1, 1},
    {1, 2,  2,  27.3,  32.4,   0, 1, 1},
    {1, 2,  4,  33.7,  38.6,   0, 1, 1},
    {1, 2,  6,  40.4,  45.3,   0, 1, 1},
    {1, 2,  8,  47.5,  52.4,   0, 1, 1},
    {1, 2, 10,  54.9,  59.8,   0, 1, 1},
    {1, 2, 11,  61.3,  66.2,   0, 2, 2},
    {1, 2, 12,  62.7,  68.5,   0, 3, 3},
    {2, 2,  1,  34.3,  38.8,   0, 1, 1},
    {2, 2,  2,  40.5,  46.4,   0, 1, 1},
    {2, 2,  3,  57.5,  63.3,   0, 1, 1},
    {2, 2,  4,  59.1,  65.0,   0, 1, 1},
    {2, 2,  5,  75.9,  80.6,   0, 3, 3},
    {2, 2,  6,  77.2,  82.1,   0, 3, 3},
    {2, 2,  7,  96.6, 103.3, 861, 0, 3},
    {2, 2,  8,  98.1, 103.9, 864, 0, 1},
    {3, 2,  1,  21.6,  30.5,   0, 1, 1},
    {3, 2,  2,  26.2,  38.9,   0, 1, 1},
    {3, 2,  4,  32.6,  49.4,   0, 1, 1},
    {3, 2,  6,  39.2,  59.2,   0, 1, 1},
    {3, 2,  8,  45.6,  68.3,   0, 1, 1},
    {3, 2, 10,  52.0,  77.3,   0, 3, 3},
    {3, 2, 11,  57.2,  84.6,   0, 2, 2},
    {3, 2, 12,  57.9,  84.7,   2, 0, 3}
};
/* the counter the model must show for each patch and mode (one core, two cores), pinned */
static const int PIN_COUNTER[4][2] = {{11, 12}, {8, 10}, {2, 3}, {3, 4}};   /* re-pinned 2026-11-13 b: LRRY_MARGIN 5 points (gates 75.6 / 72.5 %); was {{11, 14}, {9, 11}, {2, 4}, {4, 4}} at 2 points */

static void base_scene(void)
{
    scn_fresh();
    trk[0].p[P_ATK] = 0; trk[0].p[P_DEC] = 0; trk[0].p[P_SUS] = 127; trk[0].p[P_REL] = 30; trk[0].p[P_LEVEL] = 112; trk[0].p[P_TRIM] = 112; trk[0].p[P_KTRK] = 0;
    trk[0].p[P_FREEA] = UF_STEPS;
    c_basic(&trk[0]);
}
static uint32_t counter(uint32_t two)
{
    bd_t b;
    uint32_t n[2];
    bd_build(&b);
    budget_solve2(&b, n, two);
    return n[0];
}
/* one feature turned on, as a function on the track (a measured row or an ESTIMATE: budget.c) */
static void f_sine(track_t *t) { scn_x(t, X_O1MODEL, 2); }
static void f_pulse(track_t *t) { scn_x(t, X_O1MODEL, 3); }
static void f_sync(track_t *t) { scn_x(t, X_O1MODEL, 4); }
static void f_fm(track_t *t) { scn_x(t, X_O1MODEL, 6); }
static void f_fold(track_t *t) { scn_x(t, X_O1MODEL, 7); }
static void f_super(track_t *t) { scn_x(t, X_O1MODEL, 5); scn_x(t, X_O1P2, 127); }
static void f_o2(track_t *t) { scn_x(t, X_O2LVL, 100); }
static void f_o2super(track_t *t) { scn_x(t, X_O2LVL, 100); scn_x(t, X_O2MODEL, 5); scn_x(t, X_O2P2, 127); }
static void f_noise(track_t *t) { scn_x(t, X_NZLVL, 90); }
static void f_ring(track_t *t) { scn_x(t, X_RGLVL, 90); }
static void f_fa12(track_t *t) { t->p[P_FREEA] = 40; t->p[P_FREED] = 1; }
static void f_fa24(track_t *t) { t->p[P_FREEA] = 40; t->p[P_FREED] = 2; }
static void f_svf(track_t *t) { t->p[P_TWTYPE] = 1; }
static void f_ladder(track_t *t) { t->p[P_TWTYPE] = 5; }
static void f_comb(track_t *t) { t->p[P_TWTYPE] = 6; }
static void f_fbmod(track_t *t) { scn_x(t, X_ENV2, 60); scn_x(t, X_LFO1, 40); }
static void f_route(track_t *t) { t->p[MSLOT_ID(0)] = MS_LFO; t->p[MSLOT_ID(0) + 1] = MD_CUT2; t->p[MSLOT_ID(0) + 2] = 50; }
static void f_dirta(track_t *t) { scn_x(t, X_DATYPE, 5); scn_x(t, X_DALVL, 100); }
static void f_dirtb(track_t *t) { scn_x(t, X_DBTYPE, 4); scn_x(t, X_DBLVL, 100); }
static void f_sub(track_t *t) { scn_x(t, X_SUBLVL, 100); }
typedef void (*feat_t)(track_t *);
int main(void)
{
    static const struct { const char *n; void (*f)(track_t *); uint32_t one, two; } B[] = {
        /* the notes of six feature scenes (pinned; the sums of the feature rows, budget.c) */
        {"OSC1 SAW alone", c_basic, 12, 14}, {"two oscs, pans", c_detuned, 9, 12}, {"OSC1 SUPER 7 + OSC2 SAW", c_filter_a, 3, 4}, {"A BP + B HP, both oscs", c_filter_ab_par, 3, 4},
        {"B LADDER", c_ladder, 5, 8}, {"two dirt units, A 12 dB, B SVF", c_dirt_par, 3, 4},   /* (re-pinned 2026-11-13 b: LRRY_MARGIN 5 points, so the gates are 75.6 % one core, 72.5 % two cores) */
    };
    static const struct { const char *n; feat_t f; } FT[] = {
        {"O1 SINE", f_sine}, {"O1 PULSE", f_pulse}, {"O1 SYNC", f_sync}, {"O1 FM", f_fm}, {"O1 FOLD", f_fold}, {"O1 SUPER 7", f_super}, {"O2", f_o2}, {"O2 SUPER 7", f_o2super}, {"NOISE", f_noise},
        {"RING", f_ring}, {"A 12 dB", f_fa12}, {"A 24 dB", f_fa24}, {"B SVF", f_svf}, {"B LADDER", f_ladder}, {"B COMB", f_comb}, {"B + KEY/ENV/LFO mod", f_fbmod}, {"matrix CUT2", f_route},
        {"DIRT A", f_dirta}, {"DIRT B", f_dirtb}, {"SUB", f_sub},
    };
    static const feat_t CHAIN[] = {f_super, f_o2, f_o2super, f_noise, f_ring, f_fa12, f_svf, f_fbmod, f_route, f_dirta, f_dirtb, f_sub};   /* each on top of the last, in either order: only items that do not replace each other (24 dB and LADDER are added after) */
    unsigned i, j, r, two;
    printf("lrry_budget: the counter = the largest k with the predicted peak under %u.%02u %% (one core) / %u.%02u %% (two cores): the lowest measured first-miss peak less %u.%02u points\n",
           LRRY_CP_LIM1 / 100u, LRRY_CP_LIM1 % 100u, LRRY_CP_LIM / 100u, LRRY_CP_LIM % 100u, LRRY_MARGIN / 100u, LRRY_MARGIN % 100u);
    /* ---- 1 the scenes */
    for (i = 0; i < sizeof B / sizeof B[0]; i++) {
        bd_t b;
        uint32_t n1[2], n2[2], c, fx, g;
        char w[200];
        base_scene();
        B[i].f(&trk[0]);
        trk[0].p[P_KTRK] = 0;
        bd_build(&b);
        budget_solve2(&b, n1, 0);
        budget_solve2(&b, n2, 1);
        c = BD_U(b.p[0].eng + b.p[0].filt + b.p[0].ext);
        fx = BD_U(b.p[0].ins + b.p[0].sat + b.p[0].eq + b.p[0].misc + b.p[0].base);
        g = BD_U(b.idle + b.fx);
        printf("lrry_budget: %-34s voice %5u (%4.1f %%) fixed %4u globals %4u | notes: one core %u (peak %5.1f %%), two cores %u (peak %5.1f %%)\n", B[i].n, c, c / 100.0, fx, g, n1[0],
               bd_lr_peak(&b.p[0], g, n1[0], 0, 100u) / 100.0, n2[0], bd_lr_peak(&b.p[0], g, n2[0], 1, 100u) / 100.0);
        snprintf(w, sizeof w, "%s: one core %u notes (pinned %u), two cores %u (pinned %u)", B[i].n, n1[0], B[i].one, n2[0], B[i].two);
        ck(w, n1[0] == B[i].one && n2[0] == B[i].two);
        ck("two cores: never fewer notes than one core, at most NVOICE", n2[0] >= n1[0] && n2[0] <= NVOICE);
        ck("the peak at the number fits the gate (or it is the one note every part keeps)", (n1[0] == 1 || bd_lr_peak(&b.p[0], g, n1[0], 0, 100u) <= LRRY_CP_LIM1) && (n2[0] == 1 || bd_lr_peak(&b.p[0], g, n2[0], 1, 100u) <= LRRY_CP_LIM));
        if (n2[0] < b.p[0].cap)
            ck("two cores: one note more would not fit", bd_lr_peak(&b.p[0], g, n2[0] + 1u, 1, 100u) > LRRY_CP_LIM);
        if (n1[0] < b.p[0].cap)
            ck("one core: one note more would not fit", bd_lr_peak(&b.p[0], g, n1[0] + 1u, 0, 100u) > LRRY_CP_LIM1);
    }
    /* ---- 2 every measured row */
    printf("lrry_budget: | patch | cores | keys | measured avg | measured peak | missed halves (* = some repeats) | model avg | model peak | counter | measured max clean keys (+ = no miss up to 12) |\n");
    {
        bd_t b;
        uint32_t g;
        int within = 0, rows = 0;
        base_scene();
        bd_global(&b.idle, &b.fx);
        g = BD_U(b.idle + b.fx);
        for (r = 0; r < sizeof MR / sizeof MR[0]; r++) {
            bd_part_t p = {0};
            uint32_t cnt, n[2], jj = MR[r].cores == 2 ? BD_LR_J2 : BD_LR_J1, k = MR[r].keys, mavg, mpk;
            int maxc = 0, firstmiss = 0, q, off;
            bd_t bb = {0};
            p.eng = PT[MR[r].pat].eng; p.ext = PT[MR[r].pat].ext; p.misc = PT[MR[r].pat].misc; p.base = BD_PART_ON; p.bst = PT[MR[r].pat].bst; p.bst0 = PT[MR[r].pat].bst0; p.on = 1; p.cap = NVOICE;
            bb.idle = b.idle; bb.fx = b.fx; bb.p[0] = p;
            budget_solve2(&bb, n, MR[r].cores == 2);
            cnt = n[0];
            mpk = bd_lr_peak(&p, g, k, MR[r].cores == 2, 100u);
            p.bst = p.bst0 = 0;
            mavg = bd_lr_peak(&p, g, k, MR[r].cores == 2, 100u) - jj;
            for (q = 0; q < (int)(sizeof MR / sizeof MR[0]); q++)   /* the first key count with a missed half in some repeat, then the most clean keys below it */
                if (MR[q].pat == MR[r].pat && MR[q].cores == MR[r].cores && MR[q].nclean < MR[q].nrun && (!firstmiss || (int)MR[q].keys < firstmiss))
                    firstmiss = MR[q].keys;
            for (q = 0; q < (int)(sizeof MR / sizeof MR[0]); q++)
                if (MR[q].pat == MR[r].pat && MR[q].cores == MR[r].cores && MR[q].nclean == MR[q].nrun && (!firstmiss || (int)MR[q].keys < firstmiss) && (int)MR[q].keys > maxc)
                    maxc = MR[q].keys;
            off = mpk / 100.0 - MR[r].peak > 2.0 || MR[r].peak - mpk / 100.0 > 2.0;
            printf("lrry_budget: | %-15s | %u | %2u | %5.1f | %5.1f | %3d%s | %5.1f | %5.1f | %2u | %2d%s |%s\n", PT[MR[r].pat].n, MR[r].cores, k, MR[r].avg, MR[r].peak, MR[r].late, MR[r].nclean < MR[r].nrun ? "*" : " ",
                   mavg / 100.0, mpk / 100.0, cnt, maxc, firstmiss ? " " : "+", off ? " <-- model peak off by more than 2 points" : "");
            rows++;
            within += !off;
            if (r + 1 == sizeof MR / sizeof MR[0] || MR[r + 1].pat != MR[r].pat || MR[r + 1].cores != MR[r].cores) {   /* the last row of a patch and mode: the counter against the measured maximum */
                char w[200];
                snprintf(w, sizeof w, "%s, %u cores: counter %u (pinned %d), measured max clean %d%s: never above a measured first miss", PT[MR[r].pat].n, MR[r].cores, cnt, PIN_COUNTER[MR[r].pat][MR[r].cores - 1], maxc, firstmiss ? "" : "+ (no miss to 12: no upper bound from these rows)");
                ck(w, (int)cnt == PIN_COUNTER[MR[r].pat][MR[r].cores - 1] && (!firstmiss || (int)cnt <= maxc));
            }
        }
        printf("lrry_budget: %d of %d measured rows have the model's peak within 2 points\n", within, rows);
    }
    /* ---- 3 the fixed cost with the buses compiled out */
    {
        bd_t b;
        double us;
        base_scene();
        mods_off |= MODS_SENDS;                         /* (2026-11-13: the sends are compiled in again, core.h LRRY_FX_SENDS 1: SENDS OFF stops the buses, the same as compiled out) */
        bd_build(&b);
        mods_off &= ~MODS_SENDS;
        us = (BD_U(b.idle + b.fx) + BD_U(PT[0].misc + BD_PART_ON)) * 7.25 / 100.0;   /* (#16 TEST SAW's part items: the sound the 114 us was measured with) */
        printf("lrry_budget: LRRY_FX_SENDS %d: globals %u (%.1f us), the part's fixed work %u (%.1f us), together %.1f us a block with #16 TEST SAW's part items (measured 111.5 = 114 less the sends stage's 2.5)\n", LRRY_FX_SENDS, BD_U(b.idle + b.fx),
               BD_U(b.idle + b.fx) * 7.25 / 100.0, BD_U(PT[0].misc + BD_PART_ON), BD_U(PT[0].misc + BD_PART_ON) * 7.25 / 100.0, us);
        ck("fixed cost a block with the sends off (or compiled out): 121.0 us measured (pipelined bench B19: 123.5 us of #03 TEST SAW less the sends stage), within 6 us (not zero, not the old 50 us)", us > 115.0 && us < 127.0);
        ck("the idle mix is the measured BD_LR_GLOBAL, not the old Mac row", b.idle == BD_LR_T(BD_LR_GLOBAL));
    }
    /* ---- 4 crazy patches: the dearest of every feature, and monotonic */
    for (two = 0; two < 2; two++) {
        uint32_t c0, cw, cwc, mn = 99u, cur;
        char w[200];
        base_scene();
        c0 = counter(two);
        for (i = 0; i < sizeof FT / sizeof FT[0]; i++) {
            uint32_t cf;
            base_scene();
            FT[i].f(&trk[0]);
            cf = counter(two);
            mn = cf < mn ? cf : mn;
            snprintf(w, sizeof w, "%s cores: turning on %-20s (%u) never raises the counter (%u)", two ? "two" : "one", FT[i].n, cf, c0);
            ck(w, cf <= c0 && cf >= 1u);
        }
        for (j = 0; j < 2; j++) {                          /* the chain, forward and backward: each independent feature on top of the last never raises the counter */
            unsigned n = sizeof CHAIN / sizeof CHAIN[0], s;
            base_scene();
            cur = counter(two);
            for (s = 0; s < n; s++) {
                uint32_t nx;
                CHAIN[j ? n - 1 - s : s](&trk[0]);
                nx = counter(two);
                if (nx > cur)
                    break;
                cur = nx;
            }
            snprintf(w, sizeof w, "%s cores: the chain of %u features %s never raises the counter (ends at %u)", two ? "two" : "one", n, j ? "backward" : "forward", cur);
            ck(w, s == n);
        }
        base_scene();                                      /* the dearest option of every feature at once, with LADDER and with COMB */
        for (i = 0; i < sizeof CHAIN / sizeof CHAIN[0]; i++)
            CHAIN[i](&trk[0]);
        f_fm(&trk[0]); f_o2super(&trk[0]); f_fa24(&trk[0]); f_ladder(&trk[0]);
        cw = counter(two);
        f_comb(&trk[0]);
        cwc = counter(two);
        printf("lrry_budget: %s cores: every feature at its dearest: LADDER %u notes, COMB %u notes (the cheapest single feature %u, the plain SAW %u)\n", two ? "two" : "one", cw, cwc, mn, c0);
        snprintf(w, sizeof w, "%s cores: the worst case gives at least 1 note and no more than the cheapest single feature (%u)", two ? "two" : "one", mn);
        ck(w, cw >= 1u && cwc >= 1u && cw <= mn && cwc <= mn);
    }
    /* ---- 5 the pipelined two-core model against the plain one, for a synthetic voice of c us */
    {
        bd_t b;
        uint32_t cus, n_p[2], n_q[2], prev_p = 99, prev_q = 99, ge = 1, mono_p = 1, mono_q = 1, top_p = 0, top_q = 0;
        static const struct { uint32_t c, plain, pipe; } PV[] = {{165, 2, 3}, {124, 4, 5}, {91, 4, 7}};   /* (re-pinned 2026-11-13 b: LRRY_MARGIN 5 points) */
        char w[240];
        base_scene();
        bd_global(&b.idle, &b.fx);
        for (cus = 40; cus <= 220; cus++) {
            bd_t bb = {0};
            bd_part_t p = {0};
            p.eng = BD_LR_T(cus * 10u); p.misc = PT[0].misc; p.base = BD_PART_ON; p.on = 1; p.cap = NVOICE;
            bb.idle = b.idle; bb.fx = b.fx; bb.p[0] = p;
            budget_solve2(&bb, n_p, 1);
            budget_solve2(&bb, n_q, 2);
            ge &= n_q[0] >= n_p[0];
            mono_p &= n_p[0] <= prev_p;
            mono_q &= n_q[0] <= prev_q;
            prev_p = n_p[0];
            prev_q = n_q[0];
            if (n_p[0] >= 8u)
                top_p = cus;
            if (n_q[0] >= 8u)
                top_q = cus;
        }
        for (i = 0; i < sizeof PV / sizeof PV[0]; i++) {
            bd_t bb = {0};
            bd_part_t p = {0};
            p.eng = BD_LR_T(PV[i].c * 10u); p.misc = PT[0].misc; p.base = BD_PART_ON; p.on = 1; p.cap = NVOICE;
            bb.idle = b.idle; bb.fx = b.fx; bb.p[0] = p;
            budget_solve2(&bb, n_p, 1);
            budget_solve2(&bb, n_q, 2);
            printf("lrry_budget: MODEL (not measured): a voice of %u us: plain two-core runner %u voices, pipelined %u voices\n", PV[i].c, n_p[0], n_q[0]);
            snprintf(w, sizeof w, "model voices at c = %u us: plain %u (pinned %u), pipelined %u (pinned %u)", PV[i].c, n_p[0], PV[i].plain, n_q[0], PV[i].pipe);
            ck(w, n_p[0] == PV[i].plain && n_q[0] == PV[i].pipe);
        }
        printf("lrry_budget: MODEL (not measured): eight voices fit up to c = %u us (plain two-core runner: %u us)\n", top_q, top_p);
        snprintf(w, sizeof w, "eight voices fit up to c = %u us pipelined (pinned 85), %u us plain (pinned 59)", top_q, top_p);
        ck(w, top_q == 85u && top_p == 59u);   /* (re-pinned 2026-11-13 b: LRRY_MARGIN 5 points; was 89 / 65 at 2 points) */
        ck("the pipelined model never has fewer voices than the plain two-core model, for any c from 40 to 220 us", ge);
        ck("both models: a dearer voice never gives more voices", mono_p && mono_q);
        {   /* what the unit's quick bench would read for the four old test patches (MODEL values: average load % of a 725 us block and the peak, plain runner / pipelined, keys 4, 8, 12) */
            static const uint32_t KEYS[3] = {4, 8, 12};
            uint32_t pi2, ki;
            printf("lrry_budget: MODEL (not measured), two cores, average load / peak (%% of a 725 us block): patch, keys: plain runner | pipelined\n");
            for (pi2 = 0; pi2 < 4; pi2++)
                for (ki = 0; ki < 3; ki++) {
                    bd_part_t p = {0};
                    uint32_t k = KEYS[ki], pk1, pk2;
                    p.eng = PT[pi2].eng; p.ext = PT[pi2].ext; p.misc = PT[pi2].misc; p.base = BD_PART_ON; p.bst = PT[pi2].bst; p.bst0 = PT[pi2].bst0; p.on = 1; p.cap = NVOICE;
                    pk1 = bd_lr_peak(&p, BD_U(b.idle + b.fx), k, 1, 100u);
                    pk2 = bd_lr_peak(&p, BD_U(b.idle + b.fx), k, 2, 100u);
                    p.bst = p.bst0 = 0;
                    printf("lrry_budget:   %-15s %2u keys: %5.1f / %5.1f | %5.1f / %5.1f\n", PT[pi2].n, k, (bd_lr_peak(&p, BD_U(b.idle + b.fx), k, 1, 100u) - BD_LR_J2) / 100.0, pk1 / 100.0,
                           (bd_lr_peak(&p, BD_U(b.idle + b.fx), k, 2, 100u) - BD_LR_J2) / 100.0, pk2 / 100.0);
                }
        }
    }
    printf("lrry_budget: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
