/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY PHASE58: NOISE TYPE (X_NZTYPE, X63): WHITE, PINK, BROWN. The NOISE source alone (no oscillator, no SUB, no filter, centre) through the firmware's mix_block, one held note.
 * The kernel alone (lrry_nz_color on the voice's generator): WHITE's first difference has 1.414 x the RMS of the signal (flat spectrum); PINK and BROWN tilt the spectrum down (the first difference's RMS
 * over the signal's falls) and keep WHITE's loudness (RMS within 12 %, so the type switch is no jump in level). Through the voice: all three sound, the tilt keeps its order. A value above 2 plays BROWN's neighbour: clamped to BROWN (the format's range is 0..2 by the card; the voice clamps what it reads).
 * Build (from lrry): ../onepart.sh 1 1 lrry_noise */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "lrry_scn.h"

static int nfail, ncheck;
static void ck(const char *what, int ok) { ncheck++; printf("lrry_noise: %-110s %s\n", what, ok ? "ok" : "FAIL"); nfail += !ok; }
#define NB 200
static int32_t rec[NB * CTL];
static void play(int type, double *rms, double *tilt, double *mean)
{
    int32_t o[2 * CTL];
    int b, i, n = 0;
    double sq = 0, sd = 0, sum = 0;
    scn_fresh();
    trk[0].p[P_ATK] = 0; trk[0].p[P_DEC] = 0; trk[0].p[P_SUS] = 127; trk[0].p[P_REL] = 40;
    trk[0].p[P_LEVEL] = 112; trk[0].p[P_TRIM] = 112; trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_FREEA] = UF_STEPS; trk[0].p[P_FREEC] = 0;                      /* filter A open (bypassed): the source as the voice makes it */
    scn_x(&trk[0], X_O1LVL, 0); scn_x(&trk[0], X_SUBLVL, 0); scn_x(&trk[0], X_NZLVL, 100); scn_x(&trk[0], X_NZTYPE, type);
    trk_note_on(&trk[0], 57, 100);
    for (b = 0; b < NB; b++) { mix_block(o, CTL); memcpy(rec + b * CTL, mix_l, CTL * sizeof(int32_t)); }
    for (i = 60 * CTL; i < NB * CTL; i++) {
        double x = rec[i], d = rec[i] - rec[i - 1];
        sq += x * x; sd += d * d; sum += x; n++;
    }
    *rms = sqrt(sq / n); *tilt = sqrt(sd / sq); *mean = sum / n;
}
/* the kernel alone (lrry_voice.c lrry_nz_color on the voice's own generator lrry_xs): the source before the filters and the output chain */
static void kernel(int type, double *rms, double *tilt, double *mean)
{
    lrry_vs_t V;
    uint32_t st = 0x9E3779B9u * 58u;
    int i, n = 0;
    double sq = 0, sd = 0, sum = 0, prev = 0;
    memset(&V, 0, sizeof V);
    for (i = 0; i < 400000; i++) {
        int32_t w = (int32_t)lrry_xs(&st) >> 8, x = type ? lrry_nz_color(&V, w, (uint32_t)type) : w;
        if (i > 1000) { sq += (double)x * x; sd += ((double)x - prev) * ((double)x - prev); sum += x; n++; }
        prev = x;
    }
    *rms = sqrt(sq / n); *tilt = sqrt(sd / sq); *mean = sum / n;
}
int main(void)
{
    double r0, t0, m0, r1, t1, m1, r2, t2, m2, r3, t3, m3, k0, kt0, km0, k1, kt1, km1, k2, kt2, km2;
    char s[220];
    kernel(0, &k0, &kt0, &km0); kernel(1, &k1, &kt1, &km1); kernel(2, &k2, &kt2, &km2);
    snprintf(s, sizeof s, "kernel WHITE: first difference / signal = %.3f (flat spectrum, want 1.414)", kt0);
    ck(s, fabs(kt0 - 1.414) < 0.03);
    snprintf(s, sizeof s, "kernel PINK: tilted down (%.3f), loudness %.3f x white", kt1, k1 / k0);
    ck(s, kt1 < 0.6 * kt0 && fabs(k1 / k0 - 1.0) < 0.12);
    snprintf(s, sizeof s, "kernel BROWN: tilted further down (%.3f), loudness %.3f x white", kt2, k2 / k0);
    ck(s, kt2 < 0.4 * kt1 && fabs(k2 / k0 - 1.0) < 0.12);
    snprintf(s, sizeof s, "kernel: no DC beyond the slow wander of a coloured noise (mean / rms: white %.4f, pink %.4f, brown %.4f, all under 0.05)", km0 / k0, km1 / k1, km2 / k2);
    ck(s, fabs(km0) < 0.05 * k0 && fabs(km1) < 0.05 * k1 && fabs(km2) < 0.05 * k2);
    play(0, &r0, &t0, &m0); play(1, &r1, &t1, &m1); play(2, &r2, &t2, &m2); play(7, &r3, &t3, &m3);
    snprintf(s, sizeof s, "through the voice: WHITE sounds (rms %.0f); PINK and BROWN sound and tilt down (%.3f, %.3f, %.3f)", r0, t0, t1, t2);
    ck(s, r0 > 1000 && r1 > 1000 && r2 > 1000 && t1 < t0 && t2 < t1);
    ck("a stored type above 2 plays BROWN (clamped), as the card's range", fabs(r3 - r2) < 1e-6 * r2 && fabs(t3 - t2) < 1e-9);
    {   /* a new note starts every type from silence in the filters: two notes in a row sound the same */
        double ra, ta, ma, rb, tb, mb;
        play(1, &ra, &ta, &ma); play(1, &rb, &tb, &mb);
        ck("the same note twice gives the same PINK noise (the filter state is the voice's own, cleared at note-on)", ra == rb && ta == tb);
    }
    printf("lrry_noise: %d checks, %d failed\n", ncheck, nfail);
    return nfail != 0;
}
