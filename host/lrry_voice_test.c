/* L/RRY step 2 host tests: the whole stereo voice. cc -O2 lrry_voice_test.c -o t -lm */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "lrry_voice.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static double db(double x) { return 20.0 * log10(x > 1e-30 ? x : 1e-30); }

#define NS (BLK * 256)                         /* 32768 samples */
static int32_t L[NS], R[NS];
static void render(vparams *P, int note, int blocks, int release_at)
{
    voice_t v; int b; voice_on(&v, note, 100); memset(L, 0, sizeof L); memset(R, 0, sizeof R);
    for (b = 0; b < blocks; b++) { if (b == release_at) voice_off(&v); voice_render(&v, P, L + b * BLK, R + b * BLK); }
}
static double peak(const int32_t *x, int a, int n) { double m = 0; int i; for (i = a; i < a + n; i++) if (fabs((double)x[i]) > m) m = fabs((double)x[i]); return m; }
static double rms(const int32_t *x, int a, int n) { double s = 0; int i; for (i = a; i < a + n; i++) s += (double)x[i] * x[i]; return sqrt(s / n); }

/* fundamental by rising zero crossings with linear interpolation */
static double measure_hz(const int32_t *x, int a, int n)
{
    double first = -1, last = -1; int cnt = 0, i;
    for (i = a + 1; i < a + n; i++) if (x[i - 1] < 0 && x[i] >= 0) { double t = i - 1 + (double)(-x[i - 1]) / (x[i] - x[i - 1]); if (first < 0) first = t; last = t; cnt++; }
    return cnt > 1 ? (cnt - 1) * SR / (last - first) : 0.0;
}

static void t_silence(void)
{
    vparams P; vparams_init(&P); P.o[0].level = 0; voice_t v; int32_t l[BLK] = {0}, r[BLK] = {0}, i;
    voice_on(&v, 60, 100); voice_render(&v, &P, l, r);
    for (i = 0; i < BLK; i++) CHECK(l[i] == 0 && r[i] == 0, "level 0 not silent");
    /* a released voice ends exactly: becomes inactive and renders zeros */
    vparams_init(&P); render(&P, 60, 40, 10);
    voice_t w; voice_on(&w, 60, 100); int b, act = 1; memset(L, 0, sizeof L);
    for (b = 0; b < 200 && act; b++) { if (b == 5) voice_off(&w); act = voice_render(&w, &P, L + (b % 200) * 0, R + 0); }
    CHECK(!act, "voice never ended"); printf("  released voice ended after %d blocks (%.0f ms)\n", b, b * BLK / SR * 1000.0);
}

static void t_pitch(void)
{
    static const int models[] = {M_SAW, M_TRI, M_SINE, M_PULSE, M_FM, M_SUPER, M_SYNC, M_FOLD}; static const char *nm[] = {"SAW", "TRI", "SINE", "PULSE", "FM", "SUPER", "SYNC", "FOLD"};
    int m, notes[3] = {45, 69, 93}, ni;
    for (m = 0; m < 8; m++) for (ni = 0; ni < 3; ni++) {
        vparams P; vparams_init(&P); P.o[0].model = models[m]; P.o[0].p1 = models[m] == M_PULSE ? 0.3 : models[m] == M_SYNC ? 1.0 : models[m] == M_FM ? 1.0 : models[m] == M_SUPER ? 0.0 : 1.0; P.o[0].p2 = models[m] == M_FM ? 0.0 : 1; P.o[0].p3 = 0.0; P.o[0].level = 0.8;
        render(&P, notes[ni], 100, -1);
        double want = note_hz(notes[ni]), got = measure_hz(L, 6000, 6000);
        double cents = got > 0 ? 1200.0 * log2(got / want) : 999.0;
        if (models[m] == M_SYNC || models[m] == M_FOLD) { CHECK(fabs(cents) < 25.0, "%s note %d: %.2f ct", nm[m], notes[ni], cents); }
        else CHECK(fabs(cents) < 2.0, "%s note %d: %.2f ct", nm[m], notes[ni], cents);
        if (ni == 1) printf("  %-5s A4: %.3f Hz (%+.2f ct)\n", nm[m], got, cents);
    }
}

static void t_level(void)
{
    vparams P; vparams_init(&P); P.o[0].model = M_SINE; P.o[0].level = 0.5; render(&P, 69, 100, -1);
    double p = peak(L, 8000, 4000) / FS_Q23;
    CHECK(fabs(p - 0.5 * MG[M_SINE] * VOICE_GAIN) < 0.01, "sine level 0.5 peak %.3f", p); CHECK(peak(L, 8000, 4000) == peak(R, 8000, 4000), "centre not equal on L and R");
    printf("  sine level 0.5, pan 0: peak %.3f on both channels\n", p);
    vparams_init(&P); P.o[0].model = M_SINE; P.o[0].level = 1.0; P.o[0].pan = -1.0; render(&P, 69, 100, -1);
    printf("  sine level 1.0, hard left: L peak %.3f, R peak %.0f\n", peak(L, 8000, 4000) / FS_Q23, peak(R, 8000, 4000));
}

static uint32_t rs = 4242;
static void t_levels(void)
{
    static const int models[] = {M_SAW, M_TRI, M_SINE, M_PULSE, M_SYNC, M_SUPER, M_FM, M_FOLD}; static const char *nm[] = {"SAW", "TRI", "SINE", "PULSE", "SYNC", "SUPER", "FM", "FOLD"};
    int m, ni, notes[2] = {45, 69}; double target = 0.56 * VOICE_GAIN, lo = 1e9, hi = 0;
    printf("  level plan: RMS of each source at level 1.0 (target %.3f of DAC full scale at the voice, %.3f after DUELL's 0.5 master):\n", target, target * 0.5);
    for (m = 0; m < 8; m++) {
        printf("    %-6s", nm[m]);
        for (ni = 0; ni < 2; ni++) {
            vparams P; vparams_init(&P); P.o[0].model = models[m]; P.o[0].level = 1.0; P.env.a_ms = 0.1;
            P.o[0].p1 = models[m] == M_PULSE ? 0.5 : models[m] == M_SYNC ? 1.5 : models[m] == M_FM ? 1.0 : models[m] == M_FOLD ? 3.0 : models[m] == M_SUPER ? 20.0 : 0.5; P.o[0].p2 = models[m] == M_FM ? 1.5 : models[m] == M_SUPER ? 5 : 1; P.o[0].p3 = 0.0;
            render(&P, notes[ni], 250, -1);
            double r = rms(L, 2000, 28000) / FS_Q23; double d = db(r / target); if (r < lo) lo = r; if (r > hi) hi = r;
            printf("  n%d %.3f (%+.1f dB)", notes[ni], r, d); CHECK(fabs(d) < 2.0, "%s note %d is %+.1f dB from the target", nm[m], notes[ni], d);
        }
        printf("\n");
    }
    { vparams P; double r;
      vparams_init(&P); P.o[0].level = 0; P.nz.level = 1.0; render(&P, 69, 100, -1); r = rms(L, 6000, 6000) / FS_Q23; printf("    NOISE  %.3f (%+.1f dB)\n", r, db(r / target)); CHECK(fabs(db(r / target)) < 2.0, "noise level");
      vparams_init(&P); P.o[0].level = 0; P.sub_level = 1.0; P.sub_oct = -1; render(&P, 69, 100, -1); r = rms(L, 6000, 6000) / FS_Q23; printf("    SUB    %.3f (%+.1f dB)\n", r, db(r / target)); CHECK(fabs(db(r / target)) < 2.0, "sub level");
      vparams_init(&P); P.o[0].level = 0; P.o[1].model = M_SAW; P.o[1].level = 0; P.rg.level = 1.0; render(&P, 69, 100, -1); r = rms(L, 6000, 6000) / FS_Q23; printf("    RING   %.3f (%+.1f dB)\n", r, db(r / target)); CHECK(fabs(db(r / target)) < 2.0, "ring level"); }
    /* headroom: k voices of one saw at source level lv, random notes C2..C6 (5 random sets each), then DUELL's master gain 0.5; its limiter threshold is 0.549 of full scale */
    { int ks[3] = {1, 4, 8}, ki, li; double lvs[4] = {1.0, 0.5, 0.35, 0.25};
      printf("    headroom (peak / %% of samples above the limiter threshold 0.549, after the master gain 0.5), 5 random note sets:\n      voices:");
      for (ki = 0; ki < 3; ki++) printf("        %d       ", ks[ki]); printf("\n");
      for (li = 0; li < 4; li++) { printf("      level %.2f", lvs[li]);
        for (ki = 0; ki < 3; ki++) { int k = ks[ki], set; double pkm = 0, ov = 0;
          for (set = 0; set < 5; set++) { int n = 16384, i, j; static int32_t AL[16384], AR[16384]; memset(AL, 0, sizeof AL); memset(AR, 0, sizeof AR); vparams P; vparams_init(&P); P.o[0].level = lvs[li]; P.env.a_ms = 0.1; double pk = 0; int over = 0;
            for (j = 0; j < k; j++) { voice_t v; rs = rs * 1664525u + 1013904223u; voice_on(&v, 36 + (int)((rs >> 8) % 49), 100); int b; for (b = 0; b < n / BLK; b++) voice_render(&v, &P, AL + b * BLK, AR + b * BLK); }
            for (i = 4000; i < n - BLK; i++) { double a = fabs((double)AL[i]) * 0.5 / FS_Q23; if (a > pk) pk = a; if (a > 0.549) over++; }
            if (pk > pkm) pkm = pk; ov += 100.0 * over / (n - BLK - 4000); }
          printf("   %.2f / %4.1f%%", pkm, ov / 5); }
        printf("\n"); } }
}

static void t_peak_sweep(void)
{   /* no sample of a saw may exceed its own full scale (a BLEP table bug once overshot at note 78): every note 24..108; pulse allowed the usual BLEP overshoot */
    int n, worst_n = 0; double worst = 0, wp = 0;
    for (n = 24; n <= 108; n++) {
        vparams P; vparams_init(&P); P.o[0].level = 1.0; P.env.a_ms = 0.1; P.o[0].model = M_SAW; render(&P, n, 100, -1);
        double pk = peak(L, 3000, 9000) / FS_Q23 / VOICE_GAIN / MG[M_SAW]; if (pk > worst) { worst = pk; worst_n = n; }
        P.o[0].model = M_PULSE; P.o[0].p1 = 0.3; render(&P, n, 100, -1); pk = peak(L, 3000, 9000) / FS_Q23 / VOICE_GAIN / MG[M_PULSE]; if (pk > wp) wp = pk;
    }
    CHECK(worst <= 1.001, "saw peak %.3f at note %d", worst, worst_n); CHECK(wp <= 1.12, "pulse peak %.3f", wp);
    printf("  peak sweep notes 24..108: saw %.3f, pulse %.3f of its full scale\n", worst, wp);
}

static void t_stereo(void)
{
    vparams P; int i;
    /* osc 1 hard left, osc 2 hard right; either alone leaves the other channel exactly 0 through every unit */
    vparams_init(&P); P.o[0].model = M_SAW; P.o[0].level = 0.5; P.o[0].pan = -1.0; P.o[1].level = 0.0;
    P.fa.on = 1; P.fa.mode = F_LP; P.fa.cutoff = 3000; P.fa.q = 2; P.fa.p24 = 1; P.fb.kind = FB_SVF; P.fb.mode = F_HP; P.fb.cutoff = 300; P.fb.q = 1;
    P.da.type = D_SOFT; P.da.drive = 3.0; P.db.type = D_FOLD; P.db.drive = 2.0; P.d_serial = 1; P.f_serial = 1; P.mid = 1.0; P.side = 1.0;
    render(&P, 57, 60, -1);
    CHECK(peak(R, 0, 60 * BLK) == 0.0 && peak(L, 0, 60 * BLK) > 0, "left only, series everything: R peak %.0f", peak(R, 0, 60 * BLK));
    P.f_serial = 0; P.d_serial = 0; render(&P, 57, 60, -1);
    CHECK(peak(R, 0, 60 * BLK) == 0.0 && peak(L, 0, 60 * BLK) > 0, "left only, parallel everything: R peak %.0f", peak(R, 0, 60 * BLK));
    vparams_init(&P); P.o[0].level = 0.0; P.o[1].model = M_SAW; P.o[1].level = 0.5; P.o[1].pan = 1.0; P.fa.on = 1; P.fa.cutoff = 2000; P.fa.q = 1; P.fb.kind = FB_LADDER; P.fb.cutoff = 3000; P.fb.q = 0.5; P.da.type = D_FUZZ; P.da.drive = 2.0;
    render(&P, 57, 60, -1); CHECK(peak(L, 0, 60 * BLK) == 0.0 && peak(R, 0, 60 * BLK) > 0, "right only: L peak %.0f", peak(L, 0, 60 * BLK));
    /* filter B pan moves a left-only source to the right (width kept): both channels carry signal */
    vparams_init(&P); P.o[0].model = M_SAW; P.o[0].level = 0.5; P.o[0].pan = -1.0; P.fb.kind = FB_SVF; P.fb.mode = F_LP; P.fb.cutoff = 5000; P.fb.q = 1; P.fb.pan = 0.9;
    render(&P, 57, 60, -1); { double r = rms(R, 4000, 8000) / rms(L, 4000, 8000); CHECK(r > 0.3, "filter B pan did not move signal: %.3f", r); printf("  filter B pan +0.9 on a left-only source: R/L = %.2f\n", r); }
    /* mid/side: side 0 makes a hard-panned pair mono */
    vparams_init(&P); P.o[0].model = M_SAW; P.o[0].level = 0.5; P.o[0].pan = -1.0; P.side = 0.0; render(&P, 57, 60, -1);
    for (i = 8000; i < 12000; i++) { CHECK(labs(L[i] - R[i]) <= 2, "side 0 not mono at %d", i); if (labs(L[i] - R[i]) > 2) break; }
    /* a supersaw is a stereo source: both channels carry signal with spread */
    vparams_init(&P); P.o[0].model = M_SUPER; P.o[0].p1 = 25.0; P.o[0].p2 = 7; P.o[0].p3 = 1.0; P.o[0].level = 0.6; render(&P, 57, 100, -1);
    { double c = 0; int n = 8000; for (i = 8000; i < 8000 + n; i++) c += (double)L[i] * R[i]; c /= (rms(L, 8000, n) * rms(R, 8000, n) * n); CHECK(c < 0.98 && rms(L, 8000, n) > 0 && rms(R, 8000, n) > 0, "supersaw not wide: correlation %.3f", c); printf("  supersaw 7 saws spread 1.0: L/R correlation %.3f\n", c); }
}

static void fft(double *re, double *im, int n)
{
    int i, j = 0, k, len;
    for (i = 1; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (len = 2; len <= n; len <<= 1) { double ang = -2 * M_PI / len, wr = cos(ang), wi = sin(ang); for (i = 0; i < n; i += len) { double cr = 1, ci = 0; for (k = 0; k < len / 2; k++) { double ur = re[i + k], ui = im[i + k], vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr; re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi; double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr; } } }
}
/* alias level: power outside +-5 bins of the harmonics (below Nyquist), against the harmonic power; Blackman-Harris window, 8192 points, bin-centred fundamental */
static double alias_db(const int32_t *x, int model_naive, int n, double f0)
{
    static double re[8192], im[8192]; int i, h; double tot = 0, harm = 0;
    for (i = 0; i < n; i++) { double w = 0.35875 - 0.48829 * cos(2 * M_PI * i / n) + 0.14128 * cos(4 * M_PI * i / n) - 0.01168 * cos(6 * M_PI * i / n); re[i] = x[i] * w; im[i] = 0; }
    fft(re, im, n);
    for (i = 6; i < n / 2; i++) tot += re[i] * re[i] + im[i] * im[i];   /* bins 0..5: DC and its window leakage, not counted */
    for (h = 1; h * f0 < SR / 2 - 100; h++) { int c = (int)lrint(h * f0 / SR * n); for (i = c - 5; i <= c + 5; i++) if (i > 0 && i < n / 2) harm += re[i] * re[i] + im[i] * im[i]; }
    (void)model_naive; return 10 * log10((tot - harm) / harm + 1e-30);
}
static void t_alias(void)
{
    int notes[3] = {72, 84, 96}, ni; vparams P;
    for (ni = 0; ni < 3; ni++) {
        double f0 = note_hz(notes[ni]);
        vparams_init(&P); P.o[0].model = M_SAW; P.o[0].level = 0.8; P.env.a_ms = 0.1; render(&P, notes[ni], 250, -1);
        double a_saw = alias_db(L + 12000, 0, 8192, f0);
        /* a naive saw (no BLEP) of the same note for comparison */
        { uint32_t ph = 0, inc = incf(f0); static int32_t nv[8192]; int i; for (i = 0; i < 8192; i++) { nv[i] = (int32_t)ph / 256 * 4 / 5; ph += inc; } double a_naive = alias_db(nv, 1, 8192, f0);
          printf("  SAW note %d (%.0f Hz): inharmonic energy %.1f dB with BLEP, %.1f dB naive\n", notes[ni], f0, a_saw, a_naive); CHECK(a_saw < a_naive - 10.0 || a_saw < -55.0, "saw alias %.1f vs naive %.1f", a_saw, a_naive); }
    }
}

static double saw_alias(int M, double f0)
{
    static int32_t x[8192]; uint32_t ph = 0, inc = incf(f0); int i;
    for (i = 0; i < 8192; i++) { x[i] = (int32_t)(ph + 0x80000000u) / 256 - (M ? blep_m_q(M, ph, inc) : 0); ph += inc; }
    return alias_db(x, 0, 8192, f0);
}
static void t_alias_orders(void)
{
    int notes[4] = {60, 72, 84, 96}, ni, M;
    printf("  saw aliasing (inharmonic energy vs harmonics, dB), by kernel width M (0 = naive, 2 = old 2-point):\n");
    for (ni = 0; ni < 4; ni++) {
        double f0 = note_hz(notes[ni]);
        printf("    note %d (%5.0f Hz):", notes[ni], f0);
        for (M = 0; M <= 8; M += 2) { double a = saw_alias(M, f0); printf("  M=%d %6.1f", M, a); if (M == BLEP_M) CHECK(a < (ni < 3 ? -50.0 : -45.0), "M=%d alias %.1f at note %d", M, a, notes[ni]); }
        printf("\n");
    }
}
static void t_alias_models(void)
{
    static const int models[] = {M_SAW, M_PULSE, M_SYNC, M_TRI, M_FM, M_FOLD, M_SINE}; static const char *nm[] = {"SAW", "PULSE", "SYNC", "TRI", "FM", "FOLD", "SINE"};
    int notes[2] = {72, 84}, m, ni; vparams P;
    printf("  aliasing by model through the voice (dB, inharmonic vs harmonic energy), BLEP width %d:\n", BLEP_M);
    for (m = 0; m < 7; m++) {
        printf("    %-5s", nm[m]);
        for (ni = 0; ni < 2; ni++) {
            vparams_init(&P); P.o[0].model = models[m]; P.o[0].level = 0.8; P.env.a_ms = 0.1;
            P.o[0].p1 = models[m] == M_PULSE ? 0.3 : models[m] == M_SYNC ? 1.5 : models[m] == M_FM ? 1.0 : models[m] == M_FOLD ? 3.0 : 0.5; P.o[0].p2 = models[m] == M_FM ? 1.5 : 1;
            render(&P, notes[ni], 250, -1);
            double a = alias_db(L + 12000, 0, 8192, note_hz(notes[ni])); printf("  note %d: %6.1f", notes[ni], a);
        }
        printf("\n");
    }
}
static void t_blocks(void)
{   /* same voice and parameters, rendered twice: identical (the voice is deterministic, nothing global) */
    vparams P; vparams_init(&P); static int32_t L2[NS], R2[NS]; P.o[0].model = M_SUPER; P.o[0].p1 = 20; P.o[0].p2 = 5; P.o[0].p3 = 0.7; P.o[1].model = M_SINE; P.o[1].level = 0.3; P.o[1].pan = 0.5; P.nz.level = 0.05; P.nz.pan = -0.4;
    P.fa.on = 1; P.fa.p24 = 1; P.fa.cutoff = 4000; P.fa.q = 3; P.fb.kind = FB_COMB; P.fb.q = 0.8; P.fb.pan = 0.3; P.da.type = D_SOFT; P.da.drive = 2; P.da.pan = -0.5; P.sub_level = 0.3; P.sub_oct = -1; P.mid = 1.0; P.side = 1.3;
    render(&P, 60, 100, 70); memcpy(L2, L, sizeof L); memcpy(R2, R, sizeof R); render(&P, 60, 100, 70);
    CHECK(memcmp(L, L2, sizeof L) == 0 && memcmp(R, R2, sizeof R) == 0, "voice not deterministic");
    CHECK(peak(L, 0, 100 * BLK) < 8 * FS_Q23 && peak(R, 0, 100 * BLK) < 8 * FS_Q23, "full voice peak too high"); printf("  full voice (everything on) peak L %.2f R %.2f of full scale\n", peak(L, 0, 100 * BLK) / FS_Q23, peak(R, 0, 100 * BLK) / FS_Q23);
}

static void t_cost(void)
{   /* relative cost on the Mac (NOT device cycles): ns per voice-sample for an INIT-like voice, the full stereo voice, and a mono-everything reference */
    vparams I, F; voice_t v; int b, reps = 400; int32_t l[BLK], r[BLK]; clock_t t0; double ti, tf;
    vparams_init(&I); I.fa.on = 0; vparams_init(&F);
    F.o[0].model = M_SUPER; F.o[0].p1 = 20; F.o[0].p2 = 5; F.o[0].p3 = 0.7; F.o[1].model = M_SAW; F.o[1].level = 0.3; F.nz.level = 0.05; F.fa.on = 1; F.fa.p24 = 1; F.fa.cutoff = 4000; F.fa.q = 3; F.fb.kind = FB_SVF; F.fb.mode = F_HP; F.fb.cutoff = 200; F.fb.q = 1; F.fb.pan = 0.3;
    F.da.type = D_SOFT; F.da.drive = 2; F.da.pan = -0.5; F.db.type = D_FOLD; F.db.drive = 1.5; F.sub_level = 0.3; F.sub_oct = -1; F.side = 1.2; F.env.s = 1.0;
    voice_on(&v, 57, 100); t0 = clock(); for (b = 0; b < reps; b++) { memset(l, 0, sizeof l); memset(r, 0, sizeof r); voice_render(&v, &I, l, r); } ti = (double)(clock() - t0) / CLOCKS_PER_SEC / (reps * BLK) * 1e9;
    voice_on(&v, 57, 100); t0 = clock(); for (b = 0; b < reps; b++) { memset(l, 0, sizeof l); memset(r, 0, sizeof r); voice_render(&v, &F, l, r); } tf = (double)(clock() - t0) / CLOCKS_PER_SEC / (reps * BLK) * 1e9;
    printf("  Mac time per voice sample: INIT-like %.0f ns, full stereo voice %.0f ns (ratio %.1f). Not device cycles.\n", ti, tf, tf / ti);
}

int main(void)
{
    voice_tables_init();
    t_silence(); t_pitch(); t_level(); t_levels(); t_peak_sweep(); t_stereo(); t_alias(); t_alias_orders(); t_alias_models(); t_blocks(); t_cost();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
