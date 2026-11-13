/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 57: SUB WAVE. One note, the SUB alone (no oscillator, no filter, centre), through the firmware's mix_block: each of WAVE 0..3 must produce its shape, OCTAVE must still halve the pitch.
 * Shape from the settled samples: crest factor (peak / rms: SINE 1.414, TRIANGLE 1.732, SQUARE 1.000, PULSE 1.732), lowest / highest (-1, -1, -1, -1/3), mean (0 for all, within the window's part cycle). The one-core / two-core
 * bit for bit check is the scene "subwave" of tests/lrry_scn.h in tests/lrry_split.sh (the hashes of lrry_split_test.c and twocore_sim.c).
 * Build (from lrry): ../onepart.sh 1 1 lrry_subwave */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "lrry_scn.h"

static int nfail, ncheck;
static void ck(const char *what, int ok) { ncheck++; printf("lrry_subwave: %-100s %s\n", what, ok ? "ok" : "FAIL"); nfail += !ok; }
#define NB 100
static int32_t rec[NB * CTL];
static void play(int wave, int oct, double *crest, double *lo, double *mean, double *rms, int *zc)
{
    int32_t o[2 * CTL];
    int b, i, n = 0;
    double pk = 0, mx = -1e30, mn = 1e30, sum = 0, sq = 0;
    scn_fresh();
    trk[0].p[P_ATK] = 0; trk[0].p[P_DEC] = 0; trk[0].p[P_SUS] = 127; trk[0].p[P_REL] = 40;
    trk[0].p[P_LEVEL] = 112; trk[0].p[P_TRIM] = 112; trk[0].p[P_VOICE] = V_POLY;
    scn_x(&trk[0], X_O1LVL, 0); scn_x(&trk[0], X_SUBLVL, 100); scn_x(&trk[0], X_SUBOCT, oct); scn_x(&trk[0], X_SUBWAVE, wave);
    trk_note_on(&trk[0], 57, 100);
    for (b = 0; b < NB; b++) { mix_block(o, CTL); memcpy(rec + b * CTL, mix_l, CTL * sizeof(int32_t)); }
    *zc = 0;
    for (i = 40 * CTL; i < NB * CTL; i++) {
        double x = rec[i];
        sum += x; sq += x * x; n++;
        if (x > mx) mx = x;
        if (x < mn) mn = x;
        if (i > 40 * CTL && (rec[i - 1] < 0) != (rec[i] < 0)) (*zc)++;
    }
    pk = mx > -mn ? mx : -mn;
    *rms = sqrt(sq / n); *crest = pk / *rms; *lo = mn / mx; *mean = sum / n / pk;
}
int main(void)
{
    static const char *const nm[4] = {"SINE", "TRIANGLE", "SQUARE", "PULSE"};
    static const double ec[4] = {1.414, 1.732, 1.0, 1.732}, el[4] = {-1, -1, -1, -1.0 / 3};
    double c, l, m, r, rsine = 0;
    int w, zc, zc2, ok;
    char s[160];
    for (w = 0; w < 4; w++) {
        play(w, 1, &c, &l, &m, &r, &zc);
        if (!w) rsine = r;
        snprintf(s, sizeof s, "WAVE %d %s: crest %.3f (want %.3f), low / high %.3f (want %.3f), mean %.3f, rms %.2f x the sine's, sound present", w, nm[w], c, ec[w], l, el[w], m, r / rsine);
        ok = fabs(c - ec[w]) < 0.06 && fabs(l - el[w]) < 0.06 && fabs(m) < 0.06 && r > 1000;
        ck(s, ok);
        play(w, 0, &c, &l, &m, &r, &zc2);               /* OCTAVE stored 0 = -2: half the pitch of stored 1 */
        snprintf(s, sizeof s, "WAVE %d %s: OCTAVE -2 has half the zero crossings of OCTAVE -1 (%d vs %d)", w, nm[w], zc2, zc);
        ck(s, zc > 8 && abs(2 * zc2 - zc) <= 2);
    }
    {   /* different waves are different signals */
        int32_t a[NB * CTL]; int i, same = 0;
        play(0, 1, &c, &l, &m, &r, &zc); memcpy(a, rec, sizeof a);
        play(2, 1, &c, &l, &m, &r, &zc); for (i = 0; i < NB * CTL; i++) same += a[i] == rec[i];
        ck("SINE and SQUARE do not match sample for sample", same < NB * CTL / 2);
    }
    printf("lrry_subwave: %d checks, %d failed\n", ncheck, nfail);
    return nfail != 0;
}
