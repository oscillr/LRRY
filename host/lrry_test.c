/* L/RRY step 1 host tests: pan, stereo filter pair, series stereo, dirt, mid/side, accuracy against double precision. cc -O2 lrry_test.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lrry_filt2.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static double db(double x) { return 20.0 * log10(x > 1e-30 ? x : 1e-30); }
static uint32_t rs = 12345;
static int32_t rnd(int32_t amp) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (int32_t)(((int64_t)(int32_t)rs * amp) >> 31); }

static void t_pan(void)
{
    int i; pan_t p; int32_t l, r, gl, gr;
    pan_set(&p, 0.0);
    for (i = 0; i < 1000; i++) { l = rnd(FS_Q23); r = rnd(FS_Q23); int32_t a = l, b = r; pan_pair(&p, &a, &b); CHECK(a == l && b == r, "pan 0 not identity"); }
    pan_set(&p, -1.0);   /* hard left: R folds into L at 0.414, R' = 0 */
    l = 4000000; r = 2000000; pan_pair(&p, &l, &r);
    CHECK(r == 0, "hard left R' = %d", r);
    CHECK(labs(l - (int32_t)(4000000 + 0.41421356 * 2000000)) < 8, "hard left L' = %d", l);
    pan_set(&p, 1.0);
    l = 4000000; r = 2000000; pan_pair(&p, &l, &r);
    CHECK(l == 0, "hard right L' = %d", l);
    CHECK(labs(r - (int32_t)(2000000 + 0.41421356 * 4000000)) < 8, "hard right R' = %d", r);
    /* mono source into the field: power constant over the pan range (sqrt2 law: gl^2 + gr^2 = 2) */
    for (i = -10; i <= 10; i++) {
        pan_mono(i / 10.0, &gl, &gr);
        double e = ((double)gl / (1 << COEF_Q)) * ((double)gl / (1 << COEF_Q)) + ((double)gr / (1 << COEF_Q)) * ((double)gr / (1 << COEF_Q));
        CHECK(fabs(e - 2.0) < 1e-6, "pan law power %f at %d", e, i);
    }
    /* centred content (L = R) keeps its power at every pan (that is what the law is for); anti-phase content loses some when folded (Teensy law, measured below) */
    for (i = -10; i <= 10; i++) { pan_set(&p, i / 10.0); l = r = 3000000; pan_pair(&p, &l, &r); double e = ((double)l * l + (double)r * r) / (2.0 * 3e6 * 3e6); CHECK(fabs(e - 1.0) < 1e-6 + (i == 0 ? 0 : 1e-6), "centre power %f at pan %d", e, i); }
    pan_set(&p, -0.5); l = 3000000; r = -3000000; pan_pair(&p, &l, &r);
    printf("  anti-phase content at pan -0.5: %.1f dB (Teensy law)\n", db(sqrt(((double)l * l + (double)r * r) / (2.0 * 3e6 * 3e6))));
}

static double ref_svf_lp(double *x, double *y, int n, double fc, double q, int mode)
{   /* double precision Simper SVF, same equations */
    double g = tan(M_PI * fc / SR), k = 1.0 / q, a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2, ic1 = 0, ic2 = 0; int i;
    for (i = 0; i < n; i++) {
        double v3 = x[i] - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        y[i] = mode == F_LP ? v2 : mode == F_BP ? v1 : mode == F_HP ? x[i] - k * v1 - v2 : x[i] - k * v1;
    }
    return 0;
}

static void t_filter_accuracy(void)
{
    enum { N = 44100 }; static double x[N], y[N]; static int32_t in[N]; int m, i;
    for (m = 0; m < 4; m++) {
        svf_c c; svf_s s = {0, 0}; double err = 0, sig = 0, xin = 0;
        svf_coef(&c, 3000.0, 2.0);
        for (i = 0; i < N; i++) { x[i] = sin(2 * M_PI * 1000.0 * i / SR) * 0.5 * FS_Q23; in[i] = (int32_t)lrint(x[i]); x[i] = in[i]; }
        ref_svf_lp(x, y, N, 3000.0, 2.0, m);
        for (i = 0; i < N; i++) { double o = svf_tick(&c, &s, in[i], m); if (i > 2000) { err += (o - y[i]) * (o - y[i]); sig += y[i] * y[i]; xin += x[i] * x[i]; } }
        double snr = db(sqrt(err / (xin + 1e-9)));
        printf("  SVF mode %d vs double: error %.1f dB below the input\n", m, -snr);
        CHECK(snr < -110.0, "SVF mode %d error %.1f dB", m, snr);
    }
}

static void t_filter_cutoff(void)
{   /* LP at fc: gain at fc with q = 0.7071 is -3.01 dB; at fc/10 ~0 dB; at 10 fc ~ -40 dB */
    double fc = 2000.0, gain[3]; double fr[3] = {200.0, 2000.0, 20000.0}; int f, i;
    for (f = 0; f < 3; f++) {
        svf_c c; svf_s s = {0, 0}; double peak = 0; svf_coef(&c, fc, 0.70710678);
        for (i = 0; i < 44100; i++) { int32_t o = svf_tick(&c, &s, (int32_t)lrint(sin(2 * M_PI * fr[f] * i / SR) * 0.25 * FS_Q23), F_LP); if (i > 20000 && fabs((double)o) > peak) peak = fabs((double)o); }
        gain[f] = db(peak / (0.25 * FS_Q23));
    }
    printf("  LP 2 kHz gains: 200 Hz %.2f dB, 2 kHz %.2f dB, 20 kHz %.2f dB\n", gain[0], gain[1], gain[2]);
    CHECK(fabs(gain[0]) < 0.1, "passband %f", gain[0]);
    CHECK(fabs(gain[1] + 3.01) < 0.3, "cutoff gain %f", gain[1]);
    CHECK(gain[2] < -30.0, "stopband %f", gain[2]);
}

static void t_stability(void)
{   /* full scale noise and the highest resonance, every mode: finite and bounded */
    int m, i; for (m = 0; m < 4; m++) {
        svf_c c; svf_s s = {0, 0}; int32_t mx = 0; svf_coef(&c, 18000.0, 20.0);
        for (i = 0; i < 200000; i++) { int32_t o = svf_tick(&c, &s, rnd(FS_Q23), m); if (labs(o) > mx) mx = labs(o); }
        CHECK(mx < 64 * FS_Q23, "mode %d peak %d", m, mx);
    }
    lad_c lc; lad_s ls; memset(&ls, 0, sizeof ls); int32_t mx = 0; lad_coef(&lc, 5000.0, 1.0);
    for (i = 0; i < 200000; i++) { int32_t o = lad_tick(&lc, &ls, rnd(FS_Q23)); if (labs(o) > mx) mx = labs(o); }
    CHECK(mx <= FS_Q23, "ladder peak %d", mx);
    static comb_s cs; comb_c cc; comb_coef(&cc, 220.0, 0.94, 0.3); mx = 0;
    for (i = 0; i < 200000; i++) { int32_t o = comb_tick(&cc, &cs, rnd(FS_Q23)); if (labs(o) > mx) mx = labs(o); }
    CHECK(mx <= FS_Q23, "comb peak %d", mx);
}

static void t_pair_independence(void)
{   /* the pair is exactly two mono units: L alone gives R = 0; different inputs equal separate runs, bit for bit */
    int i; svf_c c; svf_pair_s sp; svf_s sl = {0, 0}, sr = {0, 0}; memset(&sp, 0, sizeof sp); svf_coef(&c, 1500.0, 4.0);
    for (i = 0; i < 20000; i++) {
        int32_t l = rnd(FS_Q23), r = 0, ml = l, mr = 0;
        svf_pair(&c, &sp, &l, &r, F_LP); ml = svf_tick(&c, &sl, ml, F_LP);
        CHECK(r == 0 && l == ml, "L only leaked at %d", i); if (r != 0) break;
    }
    memset(&sp, 0, sizeof sp); sl = sr = (svf_s){0, 0};
    for (i = 0; i < 20000; i++) {
        int32_t l = rnd(FS_Q23), r = rnd(FS_Q23), ml = svf_tick(&c, &sl, l, F_BP), mr = svf_tick(&c, &sr, r, F_BP);
        svf_pair(&c, &sp, &l, &r, F_BP);
        CHECK(l == ml && r == mr, "pair differs from two mono runs at %d", i); if (l != ml || r != mr) break;
    }
}

static void t_series_stereo(void)
{   /* A then B in series, each with its own pan: nothing collapses to mono. Source panned hard left stays left until a pan moves it. */
    int i; svf_c ca, cb; svf_pair_s sa, sb; pan_t pa, pb; memset(&sa, 0, sizeof sa); memset(&sb, 0, sizeof sb);
    svf_coef(&ca, 4000.0, 1.0); svf_coef(&cb, 900.0, 3.0); pan_set(&pa, 0.0); pan_set(&pb, 0.0);
    double el = 0, er = 0;
    for (i = 0; i < 30000; i++) {
        int32_t l = (int32_t)lrint(sin(i * 0.05) * 3000000), r = 0;
        svf_pair(&ca, &sa, &l, &r, F_LP); pan_pair(&pa, &l, &r);
        svf_pair(&cb, &sb, &l, &r, F_HP); pan_pair(&pb, &l, &r);
        el += (double)l * l; er += (double)r * r;
    }
    CHECK(er == 0.0 && el > 0.0, "series pan 0 pan 0: right energy %g", er);
    /* unit B pan right: L-only signal now appears on the right through the fold (width kept) */
    memset(&sa, 0, sizeof sa); memset(&sb, 0, sizeof sb); pan_set(&pb, 0.8); el = er = 0;
    for (i = 0; i < 30000; i++) {
        int32_t l = (int32_t)lrint(sin(i * 0.05) * 3000000), r = 0;
        svf_pair(&ca, &sa, &l, &r, F_LP); svf_pair(&cb, &sb, &l, &r, F_HP); pan_pair(&pb, &l, &r);
        el += (double)l * l; er += (double)r * r;
    }
    CHECK(er > 0.0 && el > 0.0 && er / el > 0.3, "B pan right: L %g R %g", el, er);
    printf("  series, B pan +0.8, left-only source: right/left energy %.2f\n", er / el);
}

static void t_dirt(void)
{
    int i, ty; tanh_init();
    /* softclip: small signal gain 1, accuracy against tanh, odd symmetry, bounded */
    double worst = 0; for (i = -4 * 1024; i <= 4 * 1024; i++) { int32_t x = (int32_t)((int64_t)i * FS_Q23 / 1024), y = softclip(x); double ref = tanh((double)x / FS_Q23) * (FS_Q23 - 1); double e = fabs(y - ref) / FS_Q23; if (e > worst) worst = e; CHECK(softclip(-x) == -y, "softclip odd at %d", i); CHECK(labs(y) < FS_Q23, "softclip bound"); }
    printf("  softclip worst error %.2e of full scale (%.1f dB)\n", worst, db(worst));
    CHECK(worst < 1e-6, "softclip error %g", worst);
    CHECK(labs(softclip(1000) - 1000) < 2, "softclip small signal %d", softclip(1000));
    /* shapers bounded for any input; pair independent (R unchanged by L) */
    for (ty = D_SOFT; ty <= D_CRUSH; ty++) {
        int32_t mx = 0; for (i = 0; i < 100000; i++) { int32_t l = rnd(4 * FS_Q23), r = 0; dirt_pair(ty, 1 << 24, 8, &l, &r); if (labs(l) > mx) mx = labs(l); CHECK(r == 0, "dirt %d leaked to R", ty); }
        CHECK(mx <= 2 * FS_Q23, "dirt %d peak %d", ty, mx);
    }
    /* crush: 8 bits gives at most 256 levels over full scale; 24 bits is clean */
    { int32_t seen[1 << 12]; int n = 0, j; memset(seen, 0, sizeof seen);
      for (i = -FS_Q23; i < FS_Q23; i += 997) { int32_t v = dirt_shape(D_CRUSH, i, 8); for (j = 0; j < n && seen[j] != v; j++) { } if (j == n && n < 4096) seen[n++] = v; }
      CHECK(n <= 257, "crush 8 bit gives %d levels", n); printf("  crush 8 bits: %d levels\n", n); }
    CHECK(dirt_shape(D_CRUSH, 123457, 24) == 123457, "crush 24 clean");
    /* fold stays inside +-1.5 * 16384 * 256 */
    for (i = -4 * FS_Q23; i < 4 * FS_Q23; i += 99991) CHECK(labs(dirt_shape(D_FOLD, i, 8)) <= 24576 * 256, "fold range");
}

static void t_midside(void)
{
    int i; ms_t m; int32_t l, r;
    ms_set(&m, 1.0, 1.0); for (i = 0; i < 1000; i++) { int32_t a = rnd(FS_Q23), b = rnd(FS_Q23); l = a; r = b; ms_pair(&m, &l, &r); CHECK(l == a && r == b, "mid/side default not identity"); }
    ms_set(&m, 1.0, 0.0); l = 3000000; r = 1000000; ms_pair(&m, &l, &r); CHECK(labs(l - r) <= 2 && labs(l - 2000000) <= 2, "side 0: %d %d", l, r);   /* mono */
    ms_set(&m, 0.0, 1.0); l = 3000000; r = 1000000; ms_pair(&m, &l, &r); CHECK(labs(l - 1000000) <= 2 && labs(r + 1000000) <= 2, "mid 0: %d %d", l, r);   /* pure side */
    ms_set(&m, 1.0, 2.0); l = 3000000; r = 1000000; ms_pair(&m, &l, &r); CHECK(labs(l - 4000000) <= 2 && labs(r) <= 2, "side 2: %d %d", l, r);   /* wider */
}

int main(void)
{
    tanh_init();
    t_pan(); t_filter_accuracy(); t_filter_cutoff(); t_stability(); t_pair_independence(); t_series_stereo(); t_dirt(); t_midside();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
