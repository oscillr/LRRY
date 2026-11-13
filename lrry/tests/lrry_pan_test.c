/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 50 host check: the voice's pans (owner, test1 feedback: a pan on a stereo signal is a BALANCE, SUB PAN, pans as modulation targets), through the real firmware path (mix_block).
 * Build (from lrry, the flags of LRRY/fw/onepart.sh):  ../onepart.sh 1 1 lrry_pan
 * Sections:  1 the balance kernel (lrry_pan_set / lrry_pan_pair) at every stored pan -64..+63 on random pairs
 *            2 SUPER with SPREAD at OSC 1 PAN -63..+63: the near channel bit for bit, the far one as the balance, silent at +-63
 *            3 the stage pans (FILTER A, FILTER B, DIRT A, DIRT B) the same on a SUPER pair
 *            4 SUPER with SPREAD 0 (mono: the pan law) and the step to the smallest SPREAD; SUB PAN (the pan law)
 *            6 (phase 56) a stage pan that moves between blocks ramps over the block, sample by sample (no zipper step)
 *            5 every pan at 0: the output is bit for bit the output before phase 50 (hashes of commit 0071e42, made by building THIS file in that tree with -DLRRY_PAN_OLD=1)
 * With -DLRRY_PAN_OLD=1 only section 5 is built and it prints the hashes (the old tree has none of the new names). */
#define VS_MAX 3u     /* (2026-11-12: the firmware starts 1 new voice a block, voice.c; the hashes below are of a chord that starts at once) */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int nfail, ncheck;
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_pan: %-110s %s\n", what, ok ? "ok" : "FAIL");
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
    memset(&pscr, 0, sizeof pscr);
    memset(lrry_pp, 0, sizeof lrry_pp);                 /* (2026-11-12: the pan slew's memory, lrry_param.c lrry_pan_slew: a new case starts at its own pans, as a part's first decode) */
    memset(lrry_key_ok, 0, sizeof lrry_key_ok);
    memset(lrry_ran, 0, sizeof lrry_ran);               /* (phase 56: the voices are gone: no stage pan of a block before to ramp from, as on the unit after silence) */
    for (i = 0; i < NLIVE; i++)
        pan_gv[i] = mx_ok[i] = 0;
    mix_l[0] = 0;
    dc_l = dc_r = dce_l = dce_r = 0;
}
static void patch_plain(track_t *t)       /* as lrry_fw_test.c: instant attack, full sustain, unity level and TRIM, no filter, PARALLEL */
{
    t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 30;
    t->p[P_LEVEL] = 112; t->p[P_TRIM] = 112; t->p[P_KTRK] = 0; t->p[P_PAN] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_FREEA] = UF_STEPS;
    XV(t, X_ROUTE) = 1;
    XV(t, X_LINK) = 0;
}
static void xset(track_t *t, uint32_t k, int v) { XV(t, k) = (int16_t)v; }
#define NB 80
static int32_t fw_l[NB * CTL], fw_r[NB * CTL];
static int play(const uint8_t *notes, int nn)   /* the part sum (mix_l / mix_r) from the block the first voice starts in, NB blocks */
{
    int32_t o[2 * CTL];
    int b, k, started = 0, kept = 0;
    for (k = 0; k < nn; k++)
        trk_note_on(&trk[0], notes[k], 100);
    for (b = 0; b < NB + 4 && kept < NB; b++) {
        mix_block(o, CTL);
        if (!started)
            for (k = 0; k < NVOICE; k++)
                if (trk[0].v[k].active)
                    started = 1;
        if (started) {
            memcpy(fw_l + kept * CTL, mix_l, CTL * sizeof(int32_t));
            memcpy(fw_r + kept * CTL, mix_r, CTL * sizeof(int32_t));
            kept++;
        }
    }
    return kept;
}
static uint32_t fnv(uint32_t h, const int32_t *x, int n) { int i; for (i = 0; i < n; i++) { h ^= (uint32_t)x[i]; h *= 16777619u; } return h; }

/* ---------------------------------------------------------------------------------------------------- 5 every pan at 0: today's output (no new names here) */
static void s_saw(track_t *t) { xset(t, X_O1LVL, 127); }
static void s_two(track_t *t) { xset(t, X_O1LVL, 100); xset(t, X_O2LVL, 90); xset(t, X_O2COARSE, 7); xset(t, X_O2FINE, 13); }
static void s_super(track_t *t) { xset(t, X_O1MODEL, 5); xset(t, X_O1LVL, 110); xset(t, X_O1P1, 70); xset(t, X_O1P2, 127); xset(t, X_O1P3, 127); }
static void s_super0(track_t *t) { s_super(t); xset(t, X_O1P3, 0); }
static void s_sources(track_t *t) { s_two(t); xset(t, X_NZLVL, 50); xset(t, X_RGLVL, 70); xset(t, X_SUBLVL, 80); xset(t, X_SUBOCT, 1); xset(t, X_O1BAL, -20); xset(t, X_O2BAL, 30); }
static void s_filters(track_t *t) { s_super(t); xset(t, X_O2LVL, 80); xset(t, X_O2COARSE, -12); t->p[P_FREEA] = 50; t->p[P_FREEB] = 40; t->p[P_FREED] = 2; t->p[P_TWTYPE] = 5; t->p[P_CUT2] = 45; t->p[P_RES2] = 70; xset(t, X_SUBLVL, 60); }
static void s_series(track_t *t) { s_filters(t); XV(t, X_ROUTE) = 0; t->p[P_BLEND] = 12; t->p[P_TWTYPE] = 3; }
static void s_dirt(track_t *t) { s_two(t); t->p[P_FREEA] = 55; t->p[P_FREEB] = 30; t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 60; xset(t, X_DATYPE, 1); t->p[P_DRV2] = 80; xset(t, X_DBTYPE, 3); xset(t, X_DBDRV, 40); xset(t, X_DBLVL, 90); }
static void s_dirt_ser(track_t *t) { s_dirt(t); xset(t, X_DSER, 1); xset(t, X_DBTYPE, 4); s_super(t); }
static void s_comb(track_t *t) { xset(t, X_O1LVL, 100); xset(t, X_O1MODEL, 3); t->p[P_TWTYPE] = 6; t->p[P_CUT2] = 0; t->p[P_RES2] = 40; xset(t, X_SUBLVL, 40); }
static const struct { const char *n; void (*f)(track_t *); uint8_t notes[4]; int nn; } SC[] = {
    {"SAW", s_saw, {60}, 1}, {"two oscs", s_two, {60}, 1}, {"SUPER 7 saws, SPREAD 127", s_super, {57}, 1}, {"SUPER SPREAD 0", s_super0, {57}, 1},
    {"OSC 1 + 2, NOISE, RING, SUB, balances", s_sources, {57}, 1}, {"SUPER + OSC 2, A 24 dB, B LADDER, SUB", s_filters, {48, 55, 64}, 3},
    {"the same in series, BLEND, B HP", s_series, {60}, 1}, {"dirt A SOFT + B FOLD, parallel", s_dirt, {60}, 1}, {"dirt series on SUPER", s_dirt_ser, {52, 59}, 2},
    {"COMB + SUB", s_comb, {45}, 1},
};
#define NSC (sizeof SC / sizeof SC[0])
/* the hashes of commit 0071e42 (L/RRY 0.1.0-test1's voice code), this file built in that tree with -DLRRY_PAN_OLD=1.
 * RE-BASED (voice-cheaper 2, owner's rule "perfect firmware first"): pass 6 is now ONE gain (LRRY_PASS6_ONEGAIN is the default: 1 LSB at Q15, -107.7 dB against the old two-product line), so every hash below is the new kernel's own
 * (tests/lrry_kern_equiv_test.c still proves the kernel against the reference: bit for bit with -DLRRY_PASS6_LEGACY, within 2 LSB by default). With -DLRRY_PASS6_LEGACY the original 0071e42 hashes (c2783d45 8f59bc6d 22f33955 55130ecd 3ea63c89 16dedd26 dfbc7cd5 cdf95e9d ecb88ae4, COMB: f8589fd5 with 8 voices, db89372d with 12) hold again */
#if !LRRY_PAN_OLD
static const uint32_t OLD_HASH[NSC] = {
    0x7d6a8901u,   /* SAW */
    0xb3dfb055u,   /* two oscs */
    0x0777cb85u,   /* SUPER 7 saws, SPREAD 127 */
    0x7c43b6fdu,   /* SUPER SPREAD 0 */
    0xabf2cfc5u,   /* OSC 1 + 2, NOISE, RING, SUB, balances */
    0xa8650bd6u,   /* SUPER + OSC 2, A 24 dB, B LADDER, SUB */
    0x05d4d6e8u,   /* the same in series, BLEND, B HP */
    0x45cf2f41u,   /* dirt A SOFT + B FOLD, parallel */
    0x74463c88u,   /* dirt series on SUPER */
#if NVOICE == 8
    0xc09d6e2du,   /* COMB + SUB */
#else
    0xa0c2a9c5u,   /* COMB + SUB: with 12 voices a COMB line is 268 samples (402 with 8), the comb sounds different by design (owner, 2026-11-01); the other seven scenes stay equal */
#endif
};
#endif
static void sec_centre(void)
{
    uint32_t i, same = 0;
    for (i = 0; i < NSC; i++) {
        uint32_t h = 2166136261u;
        int kept;
        fresh(); patch_plain(&trk[0]);
        SC[i].f(&trk[0]);
        kept = play(SC[i].notes, SC[i].nn);
        h = fnv(fnv(h, fw_l, kept * CTL), fw_r, kept * CTL);
#if LRRY_PAN_OLD
        printf("    0x%08xu,   /* %s */\n", h, SC[i].n);
#else
        printf("lrry_pan:   every pan 0, %-44s hash %08x, before phase 50 %08x %s\n", SC[i].n, h, OLD_HASH[i], h == OLD_HASH[i] ? "equal" : "DIFFERENT");
        same += h == OLD_HASH[i] && kept == NB;
#endif
    }
#if !LRRY_PAN_OLD
    ck("every pan at 0: the part's output bit for bit as before phase 50, in every scene", same == NSC);
#endif
}

#if !LRRY_PAN_OLD
/* ---------------------------------------------------------------------------------------------------- 1 the balance kernel */
static uint32_t rs = 777;
static int32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return (int32_t)(rs >> 8) - (1 << 23); }
static void sec_kernel(void)
{
    int v, i, bad_c = 0, bad_near = 0, bad_far = 0, bad_end = 0, bad_mono = 0;
    double worst = 0;
    for (v = -64; v <= 63; v++) {
        lrry_pan_t p;
        int32_t q = LRRY_PAN_Q15(v), f, gl, gr;
        double want = v == 0 ? 1.0 : fabs(v) >= 63 ? 0.0 : M_SQRT2 * cos((1.0 + fabs(LRRY_PAN_Q15(v) / 32768.0)) * M_PI / 4.0);
        lrry_pan_set(&p, q);
        f = p.g;
        if (v && fabs(f / 268435456.0 - want) > worst)
            worst = fabs(f / 268435456.0 - want);
        if (v && v > -63 && v < 63) {                   /* the mono law's far side is the same curve */
            lt_pan_lr(q, &gl, &gr);
            if ((v > 0 ? gl : gr) != f)
                bad_mono++;
        }
        for (i = 0; i < 300; i++) {
            int32_t a = rnd(), b = rnd(), x = a, y = b;
            lrry_pan_pair(&p, &x, &y);
            if (v == 0) { bad_c += x != a || y != b; continue; }
            bad_near += v > 0 ? y != b : x != a;
            bad_far += fabs((double)(v > 0 ? x : y) - (double)(v > 0 ? a : b) * f / 268435456.0) > 8.0;
            if (v >= 63 || v <= -63)
                bad_end += (v > 0 ? x : y) != 0;
        }
    }
    printf("lrry_pan: balance far gain vs sqrt2 cos((1 + |pan|) pi / 4): worst %.2e; at +32 (half right) %.4f (%.2f dB)\n", worst, lrry_pan_far(LRRY_PAN_Q15(32)) / 268435456.0, 20 * log10(lrry_pan_far(LRRY_PAN_Q15(32)) / 268435456.0));
    ck("balance at 0: identity bit for bit", bad_c == 0);
    ck("balance -64..+63: the near channel untouched bit for bit (pan right: R, pan left: L)", bad_near == 0);
    ck("balance: the far channel = x far gain (within 8 Q23 units), the far gain the curve within 1e-5", bad_far == 0 && worst < 1e-5);
    ck("balance at -64, -63 and +63: the far channel exactly 0", bad_end == 0);
    ck("balance far gain = the mono law's far side (lt_pan_lr) at every pan between", bad_mono == 0);
}

/* ---------------------------------------------------------------------------------------------------- 2, 3 a pair: near channel bit for bit, far as the balance */
static int32_t ref_l[NB * CTL], ref_r[NB * CTL];
static double rms(const int32_t *x, int a, int b) { double s = 0; int i; for (i = a; i < b; i++) s += (double)x[i] * x[i]; return sqrt(s / (b - a)); }
static void sweep(const char *what, void (*set)(track_t *), uint32_t slot, int step, int far_exact)
{
    int v, bad_near = 0, bad_end = 0, kept;
    uint8_t n = 57;
    double worst = 0;
    char b[200];
    fresh(); patch_plain(&trk[0]); set(&trk[0]);
    kept = play(&n, 1);
    memcpy(ref_l, fw_l, sizeof ref_l); memcpy(ref_r, fw_r, sizeof ref_r);
    for (v = -63; v <= 63; v++) {
        int i, k2;
        if (v != 63 && (v + 63) % step)
            continue;
        fresh(); patch_plain(&trk[0]); set(&trk[0]); xset(&trk[0], slot, v);
        k2 = play(&n, 1);
        if (k2 != kept) { bad_near++; continue; }
        if (memcmp(v >= 0 ? fw_r : fw_l, v >= 0 ? ref_r : ref_l, sizeof fw_l))
            bad_near++;
        if (v == 63 || v == -63)
            for (i = 0; i < kept * CTL; i++)
                bad_end += (v > 0 ? fw_l[i] : fw_r[i]) != 0;
        if (far_exact && v) {
            double r = rms(v > 0 ? fw_l : fw_r, 20 * CTL, kept * CTL) / rms(v > 0 ? ref_l : ref_r, 20 * CTL, kept * CTL), want = lrry_pan_far(LRRY_PAN_Q15(v)) / 268435456.0;
            if (fabs(r - want) > worst) worst = fabs(r - want);
        }
    }
    if (far_exact)
        snprintf(b, sizeof b, "%s -63..+63: near channel bit for bit, far channel silent at +-63, far rms = the balance gain (worst %.1e)", what, worst);
    else
        snprintf(b, sizeof b, "%s -63..+63: near channel bit for bit, far channel silent at +-63", what);
    ck(b, bad_near == 0 && bad_end == 0 && (!far_exact || worst < 2e-3));
}
static void p_super(track_t *t) { xset(t, X_O1MODEL, 5); xset(t, X_O1LVL, 110); xset(t, X_O1P1, 70); xset(t, X_O1P2, 127); xset(t, X_O1P3, 100); }
static void p_fa(track_t *t) { p_super(t); t->p[P_FREEA] = 60; t->p[P_FREEB] = 50; XV(t, X_ROUTE) = 0; }   /* (SERIES: all of it through A; in PARALLEL with B OFF bus B adds the unfiltered pair) */
static void p_fb(track_t *t) { p_super(t); t->p[P_TWTYPE] = 5; t->p[P_CUT2] = 60; t->p[P_RES2] = 60; XV(t, X_ROUTE) = 0; }
static void p_da(track_t *t) { p_fa(t); xset(t, X_DATYPE, 1); t->p[P_DRV2] = 70; xset(t, X_DSER, 1); xset(t, X_DBTYPE, 3); xset(t, X_DBDRV, 40); }
static void p_db(track_t *t) { p_fa(t); xset(t, X_DATYPE, 4); t->p[P_DRV2] = 50; xset(t, X_DSER, 1); xset(t, X_DBTYPE, 1); xset(t, X_DBDRV, 60); }
static void sec_pairs(void)
{
    sweep("SUPER (7 saws, SPREAD 100), OSC 1 PAN", p_super, X_O1PAN, 1, 1);
    sweep("FILTER A PAN (SUPER through A 12 dB, series)", p_fa, X_FAPAN, 3, 1);
    sweep("FILTER B PAN (SUPER through B LADDER, series)", p_fb, X_FBPAN, 3, 1);
    sweep("DIRT A PAN (SUPER, A, dirt A SOFT then B FOLD, series)", p_da, X_DAPAN, 3, 0);
    sweep("DIRT B PAN (SUPER, A, dirt A FUZZ then B SOFT, series: the last unit)", p_db, X_DBPAN, 3, 1);
}

/* ---------------------------------------------------------------------------------------------------- 4 the mono sources: SUPER at SPREAD 0, SUB PAN */
static void p_super0(track_t *t) { p_super(t); xset(t, X_O1P3, 0); }
static void p_sub(track_t *t) { xset(t, X_O1LVL, 0); xset(t, X_SUBLVL, 110); xset(t, X_SUBOCT, 1); }
static void sec_mono(void)
{
    uint8_t n = 57;
    int kept, i, far0;
    double near, step1, rl, rr;
    fresh(); patch_plain(&trk[0]); p_super0(&trk[0]); kept = play(&n, 1);
    memcpy(ref_l, fw_l, sizeof ref_l); memcpy(ref_r, fw_r, sizeof ref_r);
    ck("SUPER SPREAD 0 at the centre: L == R (a mono source)", !memcmp(ref_l, ref_r, sizeof ref_l));
    fresh(); patch_plain(&trk[0]); p_super0(&trk[0]); xset(&trk[0], X_O1PAN, 63); kept = play(&n, 1);
    near = rms(fw_r, 20 * CTL, kept * CTL) / rms(ref_r, 20 * CTL, kept * CTL);
    far0 = 1;
    for (i = 0; i < kept * CTL; i++) far0 &= fw_l[i] == 0;
    fresh(); patch_plain(&trk[0]); p_super0(&trk[0]); xset(&trk[0], X_O1PAN, 63); xset(&trk[0], X_O1P3, 1); kept = play(&n, 1);
    step1 = rms(fw_r, 20 * CTL, kept * CTL) / rms(ref_r, 20 * CTL, kept * CTL);
    printf("lrry_pan: SUPER SPREAD 0, OSC 1 PAN +63: right x %.4f (the pan law +3 dB), left silent %s; SPREAD 1: right x %.4f: the step SPREAD 0 -> 1 at a hard pan is %.2f dB on the near side only\n",
           near, far0 ? "yes" : "NO", step1, 20 * log10(step1 / near));
    ck("SUPER SPREAD 0 is mono: hard right = the pan law (near x 1.414 within 1e-3, far exactly 0)", fabs(near - M_SQRT2) < 1e-3 && far0);
    for (i = -48; i <= 48; i += 48) {
        double a, c;
        int32_t gl, gr;
        if (!i) continue;
        fresh(); patch_plain(&trk[0]); p_super0(&trk[0]); xset(&trk[0], X_O1PAN, i); kept = play(&n, 1);
        a = rms(i > 0 ? fw_r : fw_l, 20 * CTL, kept * CTL) / rms(ref_r, 20 * CTL, kept * CTL);
        fresh(); patch_plain(&trk[0]); p_super0(&trk[0]); xset(&trk[0], X_O1PAN, i); xset(&trk[0], X_O1P3, 1); kept = play(&n, 1);
        c = rms(i > 0 ? fw_r : fw_l, 20 * CTL, kept * CTL) / rms(ref_r, 20 * CTL, kept * CTL);
        lt_pan_lr(LRRY_PAN_Q15(i), &gl, &gr);
        printf("lrry_pan:   OSC 1 PAN %+d: near side SPREAD 0 x %.4f (law %.4f), SPREAD 1 x %.4f: step %.2f dB\n", i, a, (i > 0 ? gr : gl) / 268435456.0, c, 20 * log10(c / a));
    }
    fresh(); patch_plain(&trk[0]); p_sub(&trk[0]); kept = play(&n, 1);
    memcpy(ref_l, fw_l, sizeof ref_l); memcpy(ref_r, fw_r, sizeof ref_r);
    fresh(); patch_plain(&trk[0]); p_sub(&trk[0]); xset(&trk[0], X_SUBPAN, -63); kept = play(&n, 1);
    rl = rms(fw_l, 20 * CTL, kept * CTL) / rms(ref_l, 20 * CTL, kept * CTL);
    far0 = 1;
    for (i = 0; i < kept * CTL; i++) far0 &= fw_r[i] == 0;
    printf("lrry_pan: SUB PAN -63: left x %.4f, right silent %s\n", rl, far0 ? "yes" : "NO");
    ck("SUB PAN -63: the pan law (left x 1.414 within 1e-3, right exactly 0)", fabs(rl - M_SQRT2) < 1e-3 && far0);
    fresh(); patch_plain(&trk[0]); p_sub(&trk[0]); xset(&trk[0], X_SUBPAN, 30); kept = play(&n, 1);
    {
        int32_t gl, gr;
        lt_pan_lr(LRRY_PAN_Q15(30), &gl, &gr);
        rl = rms(fw_l, 20 * CTL, kept * CTL) / rms(ref_l, 20 * CTL, kept * CTL);
        rr = rms(fw_r, 20 * CTL, kept * CTL) / rms(ref_r, 20 * CTL, kept * CTL);
        ck("SUB PAN +30: left / right = the pan law's gains (within 1e-3)", fabs(rl - gl / 268435456.0) < 1e-3 && fabs(rr - gr / 268435456.0) < 1e-3);
    }
}
#endif

/* ---------------------------------------------------------------------------------------------------- 6 a stage pan that moves ramps (phase 56) */
/* Phase 56 (owner, test2: crackle in the stereo field on the arp): a stage pan moved from one block to the next (the matrix, a DEST card, CYCLE, an envelope route with a fast attack) was a gain
 * step at the block's first sample. A sine through FILTER A (SERIES), MULTI FILTER PAN 0 for 30 blocks, then +40 (or -40, or to +40 from -40 across the centre); the far channel against the same render
 * at pan 0 is the pan's gain sample by sample: it must go from the old gain to the new over one block in even steps (no step bigger than 1.5 x (the change / 32)), and land exactly on the new one */
static int32_t rr_l[NB * CTL], rr_r[NB * CTL];
static int32_t ramp_run(int from, int to, int32_t *L, int32_t *R)
{
    int32_t o[2 * CTL];
    int b, i, n = 0;
    fresh(); patch_plain(&trk[0]);
    xset(&trk[0], X_O1MODEL, 2); xset(&trk[0], X_O1LVL, 110); trk[0].p[P_FREEA] = 70; XV(&trk[0], X_ROUTE) = 0;
    xset(&trk[0], X_FAPAN, from);
    trk_note_on(&trk[0], 69, 100);
    for (b = 0; b < NB; b++) {
        if (b == 30)
            xset(&trk[0], X_FAPAN, to);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++, n++) {
            L[n] = mix_l[i];
            R[n] = mix_r[i];
        }
    }
    return n;
}
static void sec_ramp(void)
{
    static const int CASE[3][2] = {{0, 40}, {0, -40}, {-40, 40}};
    uint32_t c;
    int ok = 1;
    for (c = 0; c < 3u; c++) {
        int from = CASE[c][0], to = CASE[c][1], n = ramp_run(0, 0, rr_l, rr_r), i, ch;
        double worst = 0;
        (void)n;
        n = ramp_run(from, to, fw_l, fw_r);
        for (ch = 0; ch < 2; ch++) {
            const int32_t *x = ch ? fw_r : fw_l, *r = ch ? rr_r : rr_l;
            int pf = ch ? from < 0 : from > 0, pt = ch ? to < 0 : to > 0;   /* (pan right attenuates the left: L is the far side of a positive pan) */
            double g0 = pf ? lrry_pan_far(LRRY_PAN_Q15(from)) / 268435456.0 : 1.0, g1 = pt ? lrry_pan_far(LRRY_PAN_Q15(to)) / 268435456.0 : 1.0, lim = fabs(g1 - g0) / 32.0 * 1.5 + 2e-3, prev = g0;
            for (i = 30 * CTL - 4; i < 42 * CTL; i++) {   /* (2026-11-12: the pan slew spreads a move over up to 7 blocks, LRRY_PAN_SLEW: the whole slide, then the landing) */
                double g;
                if (abs(r[i]) < 2000)
                    continue;                           /* (near a zero crossing the ratio says nothing) */
                g = (double)x[i] / r[i];
                if (fabs(g - prev) > lim + 1e-9 && fabs(g - prev) > worst)
                    worst = fabs(g - prev);
                prev = g;
            }
            if (fabs(prev - g1) > 2e-3)
                ok = 0, printf("lrry_pan:   ramp %+d -> %+d channel %c ends at %.4f, not %.4f\n", from, to, ch ? 'R' : 'L', prev, g1);
        }
        printf("lrry_pan: MULTI FILTER PAN %+d -> %+d between two blocks: the largest step of the gain over one sample above the even ramp: %.4f (0 = none)\n", from, to, worst);
        ok &= worst == 0;
    }
    ck("a stage pan that moves ramps sample by sample (2026-11-12: over up to 7 blocks, the slew) (MULTI FILTER PAN 0 -> +40, 0 -> -40, -40 -> +40): no step, it lands on the new gain", ok);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
#if LRRY_PAN_OLD
    sec_centre();
    return 0;
#else
    sec_kernel();
    sec_pairs();
    sec_mono();
    sec_centre();
    sec_ramp();
    printf("lrry_pan: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
#endif
}
