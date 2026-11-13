/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 52 host check: filter A's MODE is L/RRY's smooth blend again (0 LP .. 64 BP .. 127 HP, the kill zones at the dial's ends), on L/RRY's stereo SVF (lrry_voice.c lrry_fa_weights, lrry_fa_tick).
 * Build (from lrry, the flags of LRRY/fw/onepart.sh):  ../onepart.sh 1 1 lrry_mode
 * Sections:  1 the weights: equal to L/RRY's (voice.c uf_run, copied here in its own form) for every MODE at many cutoffs; the pure points; continuity; the kill zones
 *            2 the kernel at the pure points: LP, BP, HP equal the plain outputs (LP and HP bit for bit the old filter's, BP the normalised band-pass k v1)
 *            3 the response at MODE 64 is a band-pass (and 0 a low-pass, 127 a high-pass), at 12 and 24 dB
 *            4 through the voice: a MODE sweep and a MODE jump leave no step (the weights ramp over the block)
 *            5 no level jump at the 12 / 24 dB switch (LP, BP, HP with the note in the pass band), both ways
 *            6 stereo: left and right independent at every MODE and slope (a hard-panned source on each side)
 *            7 the matrix: MULTI MODE moves the blend smoothly, the stored value back after the block
 *            8 MORPH: MODE is smooth, L/RRY's windows (morph_uf_safe) keep every rolled end audible */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int nfail, ncheck;
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_mode: %-120s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}
static void fresh(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(lrry_vs, 0, sizeof lrry_vs);
    memset(lrry_pp, 0, sizeof lrry_pp);                 /* (2026-11-12: the pan slew's memory: a fresh part starts at its own pans) */
    memset(lrry_key_ok, 0, sizeof lrry_key_ok);
    memset(&pscr, 0, sizeof pscr);
    for (i = 0; i < NLIVE; i++)
        pan_gv[i] = mx_ok[i] = 0;
    mix_l[0] = 0;
    dc_l = dc_r = dce_l = dce_r = 0;
}
static void patch_plain(track_t *t)       /* as lrry_pan_test.c: instant attack, full sustain, unity level and TRIM, PARALLEL */
{
    t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 30;
    t->p[P_LEVEL] = 112; t->p[P_TRIM] = 112; t->p[P_KTRK] = 0; t->p[P_PAN] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_FREEA] = UF_STEPS;
    XV(t, X_ROUTE) = 1;
    XV(t, X_LINK) = 0;
}
static void xset(track_t *t, uint32_t k, int v) { XV(t, k) = (int16_t)v; }
static double rms(const int32_t *x, int a, int b) { double s = 0; int i; for (i = a; i < b; i++) s += (double)x[i] * x[i]; return sqrt(s / (b - a)); }
static uint32_t rnd(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---------------------------------------------------------------------------------------------------- 1 the weights */
/* L/RRY's, from voice.c uf_run (the test's own copy, in the form it has there; uc in Q8 dial steps, UF_TOP = 82 x 256, the kill zone 4 x 256) */
static void lrry_w(int32_t md, int32_t uc, int32_t *cx, int32_t *cb, int32_t *cl)
{
    const int32_t UF_HPx = 127, top = 82 << 8, kz = 4 << 8;
    int32_t wb = md <= 64 ? md << 4 : ((UF_HPx - md) * 65) >> 2, wl = md < 64 ? 1024 - wb : 0, wh = md > 64 ? 1024 - wb : 0;
    int32_t gl = uc < kz ? uc : kz, gt = top - uc < kz ? top - uc : kz;
    *cx = (wh * gt) >> 10; *cb = ((((wb * gl) >> 10) * gt) >> 10) - *cx; *cl = ((wl * gl) >> 10) - *cx;
}
static void sec_weights(void)
{
    static const int32_t UC[] = {0, 1, 100, 255, 256, 700, 1023, 1024, 1025, 4000, 10000, 20000, 20992 - 1025, 20992 - 1024, 20992 - 700, 20992 - 100, 20992 - 1, 20992};
    int32_t md, k, same = 1, cx, cb, cl, dx, db, dl, px, pb, pl, maxd = 0;
    uint32_t n = 0;
    for (md = 0; md <= 127; md++)
        for (k = 0; k < (int32_t)(sizeof UC / sizeof UC[0]); k++) {
            lrry_fa_weights(md, UC[k], &cx, &cb, &cl);
            lrry_w(md, UC[k], &dx, &db, &dl);
            same &= cx == dx && cb == db && cl == dl;
            n++;
        }
    printf("lrry_mode: %u (MODE, cutoff) pairs compared with L/RRY's weights\n", n);
    ck("the weights are L/RRY's, equal for every MODE 0..127 at 18 cutoffs including the kill zones", same);
    lrry_fa_weights(0, 10000, &cx, &cb, &cl);
    same = cx == 0 && cb == 0 && cl == 1024;
    lrry_fa_weights(64, 10000, &cx, &cb, &cl);
    same &= cx == 0 && cb == 1024 && cl == 0;
    lrry_fa_weights(127, 10000, &cx, &cb, &cl);
    same &= cx == 1024 && cb == -1024 && cl == -1024;
    ck("the pure points: 0 = low-pass (0, 0, 1024), 64 = band-pass (0, 1024, 0), 127 = high-pass (x - band-pass - low-pass: 1024, -1024, -1024)", same);
    px = pb = pl = 0;
    for (md = 0; md <= 127; md++) {
        lrry_fa_weights(md, 10000, &cx, &cb, &cl);
        if (md) {
            int32_t d1 = abs(cx - px), d2 = abs(cb - pb), d3 = abs(cl - pl);
            maxd = d1 > maxd ? d1 : maxd; maxd = d2 > maxd ? d2 : maxd; maxd = d3 > maxd ? d3 : maxd;
        }
        px = cx; pb = cb; pl = cl;
    }
    printf("lrry_mode:   largest weight step between neighbouring MODE values: %d of 1024\n", maxd);
    ck("continuous: no weight moves more than 34 of 1024 (3.3 %) from one MODE value to the next (L/RRY's own largest step, at the band-pass: the high-pass share starts)", maxd <= 34);
    lrry_fa_weights(0, 0, &cx, &cb, &cl);
    same = !cx && !cb && !cl;
    lrry_fa_weights(64, 0, &cx, &cb, &cl);
    same &= !cx && !cb && !cl;
    lrry_fa_weights(127, 82 << 8, &cx, &cb, &cl);
    same &= !cx && !cb && !cl;
    lrry_fa_weights(64, 82 << 8, &cx, &cb, &cl);
    same &= !cx && !cb && !cl;
    ck("kill zones: low-pass and band-pass at CUT 0 and high-pass and band-pass at the open end are silent (all weights 0)", same);
}

/* ---------------------------------------------------------------------------------------------------- 2 the kernel at the pure points */
static void sec_kernel(void)
{
    uint32_t seed = 12345u, i, c;
    int bad_lp = 0, bad_hp = 0, bad_bp = 0, bad_lp24 = 0;
    static const int32_t POS[3] = {60, 140, 260}, RES[3] = {0, 70, 127};
    for (c = 0; c < 3; c++) {
        lrry_svfc_t co;
        lrry_svfs_t sl = {0, 0}, sh = {0, 0}, sb = {0, 0};      /* the new sum at the three pure points */
        lrry_svfs_t ol = {0, 0}, oh = {0, 0}, ob = {0, 0};      /* the references: the old low-pass and high-pass, an own band-pass */
        lrry_svfs_t s1 = {0, 0}, s2 = {0, 0}, t1 = {0, 0}, t2 = {0, 0};
        lrry_svf_coef(&co, POS[c] * 8, lt_k(RES[c]));
        for (i = 0; i < 20000; i++) {
            int32_t in = (int32_t)(rnd(&seed) % 6000000u) - 3000000, x, v3, v1, v2, y1, y2, z1, z2;
            /* weights (0, 0, 1024) with the steady flag off: the weighted sum itself */
            bad_lp += lrry_fa_tick(&co, &sl, in, 0, 0, 0, 1024) != lrry_svf_tick(&co, &ol, in, LF_LP);
            bad_hp += lrry_fa_tick(&co, &sh, in, 0, 1024, -1024, -1024) != lrry_svf_tick(&co, &oh, in, LF_HP);
            x = in * 8;                                          /* the plain band-pass k v1, from the filter's own update */
            v3 = x - ob.ic2;
            v1 = (LRRY_MULH(co.a1, ob.ic1) + LRRY_MULH(co.a2, v3)) * 2;
            v2 = ob.ic2 + (LRRY_MULH(co.a2, ob.ic1) + LRRY_MULH(co.a3, v3)) * 2;
            ob.ic1 = lt_clamp32(2 * (int64_t)v1 - ob.ic1, -8 * 8 * LRRY_FS, 8 * 8 * LRRY_FS);
            ob.ic2 = lt_clamp32(2 * (int64_t)v2 - ob.ic2, -8 * 8 * LRRY_FS, 8 * 8 * LRRY_FS);
            bad_bp += lrry_fa_tick(&co, &sb, in, 0, 0, 1024, 0) != ((LRRY_MULH(v1, co.k) * 4) >> 3);
            /* the 24 dB low-pass: the steady path, two stages, against two old low-pass stages */
            y1 = lrry_fa_tick(&co, &s1, in, 1, 0, 0, 1024); y2 = lrry_fa_tick(&co, &s2, y1, 1, 0, 0, 1024);
            z1 = lrry_svf_tick(&co, &t1, in, LF_LP); z2 = lrry_svf_tick(&co, &t2, z1, LF_LP);
            bad_lp24 += y2 != z2;
        }
    }
    ck("MODE 0 (weights 0, 0, 1024): the sum equals the old low-pass bit for bit, 3 cutoffs x RES 0 / 70 / 127, 20000 random samples each", bad_lp == 0);
    ck("MODE 127 (weights 1024, -1024, -1024): the sum equals the old high-pass (lrry_svf_tick LF_HP) bit for bit, the same samples", bad_hp == 0);
    ck("MODE 64 (weights 0, 1024, 0): the sum equals the plain band-pass k v1 (unity at the cutoff) bit for bit, the same samples", bad_bp == 0);
    ck("the 24 dB low-pass (two stages): the steady-low-pass path equals two old lrry_svf_tick low-pass stages bit for bit", bad_lp24 == 0);
}

/* ---------------------------------------------------------------------------------------------------- 3 the response */
/* gain (dB) of the kernel, a sine at f x the cutoff, steady weights of md at a cutoff mid dial, two stages when s24; the cutoff in Hz from the table's g */
static double resp(int32_t pos, int32_t res, int md, int s24, double fratio, double *fc_out)
{
    lrry_svfc_t co;
    lrry_svfs_t a = {0, 0}, b = {0, 0};
    int32_t cx, cb, cl, n = 48000, i;
    double g, fc, w, s = 0, sin_s = 0;
    lrry_svf_coef(&co, pos, lt_k(res));
    g = lt_g(pos) / 268435456.0;
    fc = 48000.0 * atan(g) / M_PI;
    if (fc_out)
        *fc_out = fc;
    lrry_fa_weights(md, 10000, &cx, &cb, &cl);
    w = 2.0 * M_PI * fc * fratio / 48000.0;
    for (i = 0; i < n; i++) {
        int32_t in = (int32_t)(sin(w * i) * 2000000.0), y = lrry_fa_tick(&co, &a, in, 0, cx, cb, cl);
        if (s24)
            y = lrry_fa_tick(&co, &b, y, 0, cx, cb, cl);
        if (i >= n / 2) {
            s += (double)y * y;
            sin_s += (double)in * in;
        }
    }
    return 10.0 * log10(s / sin_s + 1e-30);
}
static void sec_response(void)
{
    int s24, ok = 1;
    double fc;
    int32_t pos = 40 + ((40 * 256 * 99) >> 6);
    for (s24 = 0; s24 < 2; s24++) {
        double lo = resp(pos, 0, 64, s24, 0.125, &fc), mid = resp(pos, 0, 64, s24, 1.0, 0), hi = resp(pos, 0, 64, s24, 8.0, 0);
        double l0 = resp(pos, 0, 0, s24, 0.125, 0), l1 = resp(pos, 0, 0, s24, 8.0, 0), h0 = resp(pos, 0, 127, s24, 0.125, 0), h1 = resp(pos, 0, 127, s24, 8.0, 0);
        printf("lrry_mode:   %s dB, cutoff %.0f Hz, RES 0: MODE 64 at 1/8 fc / fc / 8 fc = %.1f / %.1f / %.1f dB; MODE 0 at 1/8 fc, 8 fc = %.1f / %.1f; MODE 127 = %.1f / %.1f\n", s24 ? "24" : "12", fc, lo, mid, hi, l0, l1, h0, h1);
        ok &= mid > -1.5 && mid < 1.5 && mid - lo > 12.0 && mid - hi > 12.0;       /* unity at the centre, both skirts well down */
        ok &= l0 > -1.5 && l0 < 1.5 && l0 - l1 > 12.0;                              /* low-pass: passes below, falls above */
        ok &= h1 > -1.5 && h1 < 1.5 && h1 - h0 > 12.0;                              /* high-pass: the reverse */
    }
    ck("MODE 64 is a band-pass (about 0 dB at the cutoff, 12 dB or more down at 1/8 and 8 times it), MODE 0 a low-pass, MODE 127 a high-pass, at 12 and 24 dB", ok);
    {   /* a blend in between has both: MODE 32 passes low frequencies and the centre, falls above; MODE 96 the centre and above */
        double a = resp(pos, 0, 32, 0, 0.125, 0), b = resp(pos, 0, 32, 0, 1.0, 0), c = resp(pos, 0, 32, 0, 8.0, 0);
        double d = resp(pos, 0, 96, 0, 0.125, 0), e = resp(pos, 0, 96, 0, 1.0, 0), f = resp(pos, 0, 96, 0, 8.0, 0);
        printf("lrry_mode:   MODE 32 at 1/8 fc / fc / 8 fc = %.1f / %.1f / %.1f dB; MODE 96 = %.1f / %.1f / %.1f dB\n", a, b, c, d, e, f);
        ck("MODE 32 is between low-pass and band-pass (1/8 fc and fc within 8 dB of 0, 8 fc more than 10 dB lower), MODE 96 between band-pass and high-pass (the reverse)", a > -8 && b > -8 && c < a - 10 && f > -8 && e > -8 && d < f - 10);
    }
}

/* ---------------------------------------------------------------------------------------------------- the voice runs */
#define NBLK 160
static int32_t out_l[NBLK * CTL], out_r[NBLK * CTL];
typedef void (*blk_fn)(int b);
static void run(blk_fn fn, int note)    /* one note held, NBLK blocks, fn(b) before each block; the part sum in out_l / out_r */
{
    int32_t o[2 * CTL];
    int b;
    trk_note_on(&trk[0], (uint8_t)note, 100);
    for (b = 0; b < NBLK; b++) {
        if (fn)
            fn(b);
        mix_block(o, CTL);
        memcpy(out_l + b * CTL, mix_l, CTL * sizeof(int32_t));
        memcpy(out_r + b * CTL, mix_r, CTL * sizeof(int32_t));
    }
}
static void sine_patch(void)
{
    fresh(); patch_plain(&trk[0]);
    xset(&trk[0], X_O1MODEL, 2); xset(&trk[0], X_O1LVL, 100);
}
static int32_t maxstep(const int32_t *x, int a, int b) { int32_t m = 0; int i; for (i = a + 1; i < b; i++) { int32_t d = abs(x[i] - x[i - 1]); m = d > m ? d : m; } return m; }

static int sweep_dir, sweep_start, jump_at, jump_to;
static void fn_sweep(int b)
{
    if (b >= sweep_start && b < sweep_start + 128)
        trk[0].p[P_FREEC] = (int16_t)(sweep_dir ? 127 - (b - sweep_start) : b - sweep_start);
}
static void fn_jump(int b) { if (b == jump_at) trk[0].p[P_FREEC] = (int16_t)jump_to; }
static void sec_voice(void)
{
    int32_t steady[3], st_max = 0, sw, sw2, jm;
    int md, k;
    static const int M3[3] = {0, 64, 127};
    for (k = 0; k < 3; k++) {       /* the filter set where the note is in the passband of each pure MODE: the steps of a sine through it */
        sine_patch();
        trk[0].p[P_FREEA] = 40; trk[0].p[P_FREEB] = 30; trk[0].p[P_FREED] = 1; trk[0].p[P_FREEC] = (int16_t)M3[k];
        run(0, 72);
        steady[k] = maxstep(out_l, 60 * CTL, NBLK * CTL);
        st_max = steady[k] > st_max ? steady[k] : st_max;
    }
    sine_patch();
    trk[0].p[P_FREEA] = 40; trk[0].p[P_FREEB] = 30; trk[0].p[P_FREED] = 1; trk[0].p[P_FREEC] = 0;
    sweep_dir = 0; sweep_start = 20;
    run(fn_sweep, 72);
    sw = maxstep(out_l, 22 * CTL, NBLK * CTL);
    sine_patch();
    trk[0].p[P_FREEA] = 40; trk[0].p[P_FREEB] = 30; trk[0].p[P_FREED] = 1; trk[0].p[P_FREEC] = 127;
    sweep_dir = 1; sweep_start = 20;
    run(fn_sweep, 72);
    sw2 = maxstep(out_l, 22 * CTL, NBLK * CTL);
    printf("lrry_mode:   largest sample step: steady LP / BP / HP %d / %d / %d, MODE swept 0 -> 127 (1 per block) %d, 127 -> 0 %d (full scale 8388608)\n", steady[0], steady[1], steady[2], sw, sw2);
    ck("a MODE sweep 0 -> 127 and 127 -> 0 (one step a block) leaves no sample step beyond 1.25 x the largest of the steady modes", sw <= st_max * 5 / 4 && sw2 <= st_max * 5 / 4);
    jm = 0;
    for (md = 0; md < 3; md++) {
        int to;
        for (to = 0; to < 3; to++) {
            int32_t m;
            if (md == to)
                continue;
            sine_patch();
            trk[0].p[P_FREEA] = 40; trk[0].p[P_FREEB] = 30; trk[0].p[P_FREED] = 1; trk[0].p[P_FREEC] = (int16_t)M3[md];
            jump_at = 70; jump_to = M3[to];
            run(fn_jump, 72);
            m = maxstep(out_l, 60 * CTL, NBLK * CTL);
            jm = m > jm ? m : jm;
        }
    }
    printf("lrry_mode:   largest sample step with a jump of MODE (0 / 64 / 127, all six ways) in one block: %d\n", jm);
    ck("a jump of MODE between LP, BP and HP in one block: the weights ramp over it, no sample step beyond 1.5 x the largest of the steady modes (a click would be about 25 x: the sine's whole swing; the ramp adds the phase differences of the three outputs)", jm <= st_max * 3 / 2);
}

/* ---------------------------------------------------------------------------------------------------- 5 the slope switch */
static double pk(const int32_t *x, int a, int b) { int32_t m = 0; int i; for (i = a; i < b; i++) m = abs(x[i]) > m ? abs(x[i]) : m; return (double)m; }
static int sl_at, sl_to;
static void fn_slope(int b) { if (b == sl_at) trk[0].p[P_FREED] = (int16_t)sl_to; }
static int pass_dial(int mode)     /* the dial where the note (72) is in the pass band of the MODE: LP well above it, HP well below, BP at the peak of the 12 dB response */
{
    int d, best = 0;
    double bv = -1;
    if (mode == 0) return 70;
    if (mode == 127) return 8;
    for (d = 15; d < 60; d++) {
        double r;
        sine_patch();
        trk[0].p[P_FREEA] = (int16_t)d; trk[0].p[P_FREEB] = 0; trk[0].p[P_FREED] = 1; trk[0].p[P_FREEC] = (int16_t)mode;
        run(0, 72);
        r = rms(out_l, 100 * CTL, NBLK * CTL);
        if (r > bv) { bv = r; best = d; }
    }
    return best;
}
static void sec_slope(void)
{
    static const int M3[3] = {0, 64, 127};
    int k, dir, ok = 1;
    double worst = 0;
    int32_t worst_step = 0;
    for (k = 0; k < 3; k++) {
        int dial = pass_dial(M3[k]);
        for (dir = 0; dir < 2; dir++) {
            double before, mx = 0;
            int b;
            int32_t st_steady, st_sw;
            sine_patch();
            trk[0].p[P_FREEA] = (int16_t)dial; trk[0].p[P_FREEB] = 0; trk[0].p[P_FREEC] = (int16_t)M3[k]; trk[0].p[P_FREED] = (int16_t)(dir ? 2 : 1);
            sl_at = 80; sl_to = dir ? 1 : 2;
            run(fn_slope, 72);
            before = pk(out_l, 70 * CTL, 80 * CTL);                  /* the sine's peak (a block alone is shorter than the sine's period: its RMS ripples with the phase) */
            for (b = 78; b < 100; b++) {
                double r = pk(out_l, b * CTL, (b + 3) * CTL), d = fabs(20.0 * log10(r / before));   /* the peak over 3 blocks (96 samples, a period of the note) */
                mx = d > mx ? d : mx;
            }
            st_steady = maxstep(out_l, 60 * CTL, 80 * CTL);
            st_sw = maxstep(out_l, 80 * CTL - 1, 100 * CTL);      /* (from the last sample before the switch: the step AT the switch counts) */
            printf("lrry_mode:   MODE %3d at dial %2d, %s: peak level around the switch moves at most %.2f dB, largest step %d (steady %d)\n", M3[k], dial, dir ? "24 -> 12 dB" : "12 -> 24 dB", mx, st_sw, st_steady);
            worst = mx > worst ? mx : worst;
            worst_step = st_sw > st_steady * 5 / 4 ? st_sw : worst_step;
            ok &= mx < 0.5 && st_sw <= st_steady * 5 / 4 + 1;
        }
    }
    ck("no level jump at the 12 / 24 dB switch: the note in the pass band of LP, BP and HP, both ways: the peak level within 0.5 dB and no step beyond 1.25 x the steady one", ok && worst < 0.5 && !worst_step);
}

/* ---------------------------------------------------------------------------------------------------- 6 stereo */
static int32_t sl_ref[2][NBLK * CTL];
static void st_patch(int a, int b, int md, int s24)
{
    fresh(); patch_plain(&trk[0]);
    xset(&trk[0], X_O1MODEL, 2); xset(&trk[0], X_O1LVL, a ? 100 : 0); xset(&trk[0], X_O1PAN, -63);
    xset(&trk[0], X_O2MODEL, 2); xset(&trk[0], X_O2LVL, b ? 100 : 0); xset(&trk[0], X_O2PAN, 63); xset(&trk[0], X_O2COARSE, 7);
    trk[0].p[P_FREEA] = 36; trk[0].p[P_FREEB] = 50; trk[0].p[P_FREEC] = (int16_t)md; trk[0].p[P_FREED] = (int16_t)(s24 ? 2 : 1);
}
static void sec_stereo(void)
{
    static const int MD[] = {0, 20, 64, 100, 127};
    int i, s24, same_l = 1, same_r = 1, far0 = 1, alive = 1;
    for (s24 = 0; s24 < 2; s24++)
        for (i = 0; i < 5; i++) {
            int n;
            st_patch(1, 1, MD[i], s24); run(0, 60);
            memcpy(sl_ref[0], out_l, sizeof out_l); memcpy(sl_ref[1], out_r, sizeof out_r);
            st_patch(1, 0, MD[i], s24); run(0, 60);
            same_l &= !memcmp(sl_ref[0], out_l, sizeof out_l);
            for (n = 0; n < NBLK * CTL; n++) far0 &= out_r[n] == 0;
            alive &= rms(out_l, 100 * CTL, NBLK * CTL) > 1000.0;
            st_patch(0, 1, MD[i], s24); run(0, 60);
            same_r &= !memcmp(sl_ref[1], out_r, sizeof out_r);
            for (n = 0; n < NBLK * CTL; n++) far0 &= out_l[n] == 0;
            alive &= rms(out_r, 100 * CTL, NBLK * CTL) > 1000.0;
        }
    ck("stereo: a source hard left and one hard right, MODE 0 / 20 / 64 / 100 / 127 at 12 and 24 dB: the left output bit for bit the left source alone, the right the right alone", same_l && same_r && alive);
    ck("stereo: the other side of a one-sided source stays exactly 0 through the blend", far0);
}

/* ---------------------------------------------------------------------------------------------------- 7 the matrix */
static void sec_matrix(void)
{
    int32_t v, prev = -1, maxs = 0, lo, hi, amt;
    int ok_back = 1, mono = 1;
    track_t *t;
    fresh(); patch_plain(&trk[0]);
    t = &trk[0];
    lo = 1000; hi = -1;
    for (amt = 0; amt <= 63; amt++) {
        t->p[P_FREEC] = 0;
        t->mw = 127;
        t->p[P_M1SRC] = MS_MODW; t->p[P_M1DST] = MD_FMODE; t->p[P_M1AMT] = (int16_t)amt;
        for (v = 0; v < (int32_t)NLIVE; v++) mx_sl[v].ok = 0;
        (void)mod_begin(t);
        v = t->p[P_FREEC];
        mod_end(t);
        ok_back &= t->p[P_FREEC] == 0;
        if (prev >= 0) {
            if (v < prev || v - prev > 4)
                printf("lrry_mode:   AMT %d: MODE %d after %d\n", amt, v, prev);
            mono &= v >= prev;
            maxs = v - prev > maxs ? v - prev : maxs;
        }
        prev = v;
        lo = v < lo ? v : lo; hi = v > hi ? v : hi;
        if (amt == 63)
            printf("lrry_mode:   MODW -> MULTI MODE at AMT +63: the block's MODE from 0 to %d (L/RRY's rule: AMT 64 = the whole range 0..127, L/RRY's own test expects %d)\n", v, (((127 * 258) * 64) >> 6) * 127 >> 15);
    }
    ck("matrix MULTI MODE (MD_FMODE): AMT +63 moves MODE from 0 to L/RRY's value (the whole range, near HP), by steps of at most 4 (126 over 63 amounts) and never down, not four choices", hi == ((((127 * 258) * 64) >> 6) * 127 >> 15) && lo == 0 && mono && maxs <= 4);
    ck("the stored MODE is back after the block (mod_end) at every amount", ok_back);
    t->p[P_M1SRC] = 0; t->p[P_M1DST] = 0; t->p[P_M1AMT] = 0;
    t->p[P_FREEC] = 90;
    ck("the descriptor is L/RRY's: MODE 0..127, default 0, shown LP, LP>BP, BP, BP>HP, HP", TP[P_FREEC].min == 0 && TP[P_FREEC].max == 127 && TP[P_FREEC].def == 0 && TP[P_FREEC].fmt == F_FMODE && !strcmp(TP[P_FREEC].label, "MODE"));
}

/* ---------------------------------------------------------------------------------------------------- 8 MORPH */
static void sec_morph(void)
{
    int8_t o[P_COUNT + NX], end[P_COUNT + NX];
    uint32_t seed = 777u, i, bad = 0, moved = 0, hp = 0, lp = 0, bp = 0;
    int ok_tag;
    mtag_t a, b, c;
    memset(o, 0, sizeof o);
    mrc = MC_STEREO; a = morph_tag(o, P_FREEC, 1);
    mrc = MC_STEREO | MC_PIT; b = morph_tag(o, P_FREEC, 1);
    mrc = MC_STEREO | MC_PIT | MC_WILD; c = morph_tag(o, P_FREEC, 1);
    ok_tag = a.kind == MK_CONT && b.kind == MK_CONT && c.kind == MK_CONT;
    ck("MORPH: MODE is smooth (MK_CONT) in SMART, SMART + PITCH and WILD + PITCH, as L/RRY's", ok_tag);
    for (i = 0; i < 20000; i++) {
        int32_t lo, hi, bpx;
        memset(o, 0, sizeof o);
        o[P_FREEA] = (int8_t)(rnd(&seed) % 83u); o[P_FREEB] = (int8_t)(rnd(&seed) % 128u); o[P_FREEC] = (int8_t)(rnd(&seed) % 128u); o[P_FREED] = (int8_t)(1 + rnd(&seed) % 2u);
        memcpy(end, o, sizeof end);
        end[P_FREEA] = (int8_t)(rnd(&seed) % 83u); end[P_FREEB] = (int8_t)(rnd(&seed) % 128u); end[P_FREEC] = (int8_t)(rnd(&seed) % 128u);
        mrc = MC_STEREO;
        morph_uf_safe(o, o, end, 10u + rnd(&seed) % 91u);
        morph_uf_range(o, end[P_FREEC], &lo, &hi, &bpx);
        bad += end[P_FREEA] < lo || end[P_FREEA] > hi;
        moved += end[P_FREEC] != o[P_FREEC];
        hp += end[P_FREEC] > 64; lp += end[P_FREEC] < 64; bp += end[P_FREEC] == 64;
    }
    printf("lrry_mode:   20000 SMART ends through morph_uf_safe: MODE moved in %u, ends LP side %u, BP %u, HP side %u, CUT outside the window of its MODE: %u\n", moved, lp, bp, hp, bad);
    ck("MORPH: L/RRY's windows on the blend (morph_uf_safe): every rolled end has its CUT inside the window of its MODE's shares; MODE reaches both sides", bad == 0 && moved > 1000 && hp > 1000 && lp > 1000);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    sec_weights();
    sec_kernel();
    sec_response();
    sec_voice();
    sec_slope();
    sec_stereo();
    sec_matrix();
    sec_morph();
    printf("lrry_mode: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
