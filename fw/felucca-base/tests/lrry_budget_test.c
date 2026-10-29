/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3b: the NOTES budget with two cores (budget.c budget_solve2, the by-voice rule; an ESTIMATE). For the six patches of tests/lrry_fw_test.c (section 5a) it prints the notes available on one core (the 4.3a
 * figures, which must not change: 6 4 2 3 2 2 in PHASE43A_REPORT.md) and with two cores, the cost of a voice, and the load each core carries at that number. Checks: one core unchanged; two cores never fewer than one core,
 * never more than NVOICE; both cores' loads at the two-core number fit the limit; one note more would not fit (unless it is the cap).
 * Build (from felucca-base): ../onepart.sh 1 1 lrry_budget */
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
    t->p[P_FREEA] = 50; t->p[P_FREEB] = 40; t->p[P_FREEC] = 1; t->p[P_FREED] = 1;
    t->p[P_TWTYPE] = 3; t->p[P_CUT2] = 35; t->p[P_RES2] = 50; scn_x(t, X_FBPAN, -45); scn_x(t, X_FAPAN, 45);
}
static void c_ladder(track_t *t) { scn_x(t, X_O1LVL, 100); scn_x(t, X_O1MODEL, 0); t->p[P_TWTYPE] = 5; t->p[P_CUT2] = 45; t->p[P_RES2] = 90; scn_x(t, X_FBPAN, 25); }
static void c_dirt_par(track_t *t)
{
    scn_x(t, X_O1LVL, 90); scn_x(t, X_O2LVL, 90); scn_x(t, X_O2COARSE, 12); t->p[P_FREEA] = 55; t->p[P_FREEB] = 30; t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 60;
    scn_x(t, X_DATYPE, 1); t->p[P_DRV2] = 80; scn_x(t, X_DAPAN, -35); scn_x(t, X_DALVL, 100);
    scn_x(t, X_DBTYPE, 3); scn_x(t, X_DBDRV, 40); scn_x(t, X_DBPAN, 35); scn_x(t, X_DBLVL, 90);
}
int main(void)
{
    static const struct { const char *n; void (*f)(track_t *); uint32_t one; } B[] = {
        {"OSC1 SAW alone", c_basic, 6}, {"two oscs, pans", c_detuned, 4}, {"OSC1 SUPER 7 + OSC2 SAW", c_filter_a, 2}, {"A BP + B HP, both oscs", c_filter_ab_par, 3},
        {"B LADDER", c_ladder, 2}, {"two dirt units, A 12 dB, B SVF", c_dirt_par, 2},
    };
    unsigned i;
    printf("lrry_budget: ESTIMATE, not measured on the unit. limit %u %% of the half; cost of a voice and of the part's fixed work in 1/100 %% of the half\n", BUDGET_LIMIT);
    for (i = 0; i < sizeof B / sizeof B[0]; i++) {
        bd_t b;
        uint32_t n1[2], n2[2], c, fx, g, k, n0, nn1, cc, l0, l1;
        scn_fresh();
        trk[0].p[P_ATK] = 0; trk[0].p[P_DEC] = 0; trk[0].p[P_SUS] = 127; trk[0].p[P_REL] = 30; trk[0].p[P_LEVEL] = 112; trk[0].p[P_TRIM] = 112; trk[0].p[P_KTRK] = 0;
        trk[0].p[P_FREEA] = UF_STEPS;
        B[i].f(&trk[0]);
        trk[0].p[P_KTRK] = 0;
        bd_build(&b);
        budget_solve2(&b, n1, 0);
        budget_solve2(&b, n2, 1);
        c = BD_U(b.p[0].eng + b.p[0].filt + b.p[0].ext);
        fx = BD_U(b.p[0].ins + b.p[0].sat + b.p[0].eq + b.p[0].misc + b.p[0].base);
        g = BD_U(b.idle + b.fx);
        k = n2[0];
        nn1 = k / 2u;
        n0 = k - nn1;
        cc = (c * BD_LR_CTL_PCT + 99u) / 100u;
        l0 = g + fx + n0 * c + nn1 * cc + (nn1 ? BD_U(BD_LR_HO0) : 0u);
        l1 = nn1 * (c - cc) + (nn1 ? BD_U(BD_LR_HO1) : 0u);
        printf("lrry_budget: %-34s voice %5u (%4.1f %%) fixed %4u globals %4u | notes: one core %u, two cores %u (core 0 carries %u voices, load %5.1f %%; core 1 %u voices, load %5.1f %%)\n",
               B[i].n, c, c / 100.0, fx, g, n1[0], n2[0], n0, l0 / 100.0, nn1, l1 / 100.0);
        {
            char w[200];
            snprintf(w, sizeof w, "%s: one core still %u notes (the 4.3a figure)", B[i].n, B[i].one);
            ck(w, n1[0] == B[i].one);
        }
        ck("two cores: never fewer notes than one core, at most NVOICE", n2[0] >= n1[0] && n2[0] <= NVOICE);
        ck("two cores: both loads fit the limit at that number (or it is the one note every part keeps)", n2[0] == 1 || (l0 <= BUDGET_LIMIT * 100u && l1 <= BUDGET_LIMIT * 100u));
        if (n2[0] < b.p[0].cap && n2[0] >= 1) {
            uint32_t k1 = n2[0] + 1u, a1 = k1 / 2u, a0 = k1 - a1;
            ck("two cores: one note more would not fit", !(g + fx + a0 * c + a1 * cc + BD_U(BD_LR_HO0) <= BUDGET_LIMIT * 100u && a1 * (c - cc) + BD_U(BD_LR_HO1) <= BUDGET_LIMIT * 100u));
        }
    }
    printf("lrry_budget: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
