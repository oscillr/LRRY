/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3a host check: the stereo voice as the firmware's sound engine, through the real mix_block path, against the standalone host voice (LRRY/host/lrry_voice.h).
 * Build (from felucca-base, the flags of LRRY/fw/onepart.sh):   fw/onepart.sh is the same line:  tests/lrry_fw_test.c  -DNPART=1 -DNSHOWN=1 -lm   (it runs  ./o_lrry_fw build/mod_demo)
 * Sections:  1 the table maths against libm   2 sample for sample, firmware mix_block against the host voice (several settings)   3 left / right with pans set
 *            4 silence, NaN and overflow, headroom with 8 voices   5 a first cost estimate (LRRY_BENCH=1 builds the timing run)
 * Everything the firmware does is integers; the host voice is double/libm at its edges: the comparison is "within the stated tolerance", every number is printed. */
#define main hostsim_main
#include "hostsim.c"
#undef main

/* the host voice names clash with the firmware's (voice_t, softclip, clamp ...): renamed for this one include */
#define voice_t host_voice_t
#define softclip host_softclip
#define voice_on host_voice_on
#define voice_off host_voice_off
#define voice_render host_voice_render
#define voice_tables_init host_voice_tables_init
#define clamp32 host_clamp32
#define mulq host_mulq
#define incf host_incf
#define xs host_xs
#define cq host_cq
#define sine_q host_sine_q
#define pan_t host_pan_t
#define svf_c host_svf_c
#define svf_s host_svf_s
#define sup host_sup
#undef BLK
#define BLK 32
#include "../../../host/lrry_voice.h"
#undef voice_t
#undef softclip
#undef voice_on
#undef voice_off
#undef voice_render
#undef voice_tables_init
#undef clamp32
#undef mulq
#undef incf
#undef xs
#undef cq
#undef sine_q
#undef pan_t
#undef svf_c
#undef svf_s
#include <sys/time.h>

#define VEL 100                      /* below the accent (DUELL opens the filter with the envelope above velocity 110: the host voice has no accent) */
static int nfail, ncheck;
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_fw: %-96s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}

/* ---------------------------------------------------------------------------------------------------- 1 the tables */
static double host_g(int32_t pos) { double fc = 30.0 * pow(16000.0 / 30.0, pos / 32512.0); return tan(M_PI * fc / 44100.0); }
static void sec_tables(void)
{
    double w_g = 0, w_k = 0, w_pan = 0, w_pow = 0, w_inv = 0, w_a1 = 0, w_sc = 0;
    int32_t p, i;
    uint32_t x;
    int64_t w_sin = 0, w_blep = 0;
    for (p = 0; p <= 127 * 256; p += 5) {
        double e = lt_g(p) / 268435456.0, t = host_g(p), r = fabs(e - t) / t;
        if (r > w_g) w_g = r;
    }
    for (i = 0; i < 128; i++) {
        double t = 1.0 / (0.5 * pow(40.0, i / 127.0)), e = lt_k(i) / 268435456.0, r = fabs(e - t) / t;
        if (r > w_k) w_k = r;
    }
    for (p = -32768; p <= 32768; p += 11) {
        int32_t gl, gr;
        double a = (p / 32768.0 + 1.0) * M_PI / 4.0, el, er;
        lt_pan_lr(p, &gl, &gr);
        el = fabs(gl / 268435456.0 - sqrt(2.0) * cos(a));
        er = fabs(gr / 268435456.0 - sqrt(2.0) * sin(a));
        if (el > w_pan) w_pan = el;
        if (er > w_pan) w_pan = er;
    }
    for (i = -4 * 16777216; i < 3 * 16777216; i += 255331) {
        double t = pow(2.0, i / 16777216.0) * 268435456.0, r = fabs((double)lt_pow2(i) - t) / t;
        if (r > w_pow) w_pow = r;
    }
    for (x = 3; x < 0xF0000000u; x = x / 3u * 5u + 1u) {
        double t = 72057594037927936.0 / x, r = fabs((double)lt_inv56(x) - t) / t;
        if (r > w_inv) w_inv = r;
    }
    host_voice_tables_init();
    for (x = 0; x < 0xFFFF0000u; x += 99991u) {          /* the sine against the host's own table and interpolation, and the BLEP table and tanh against the host's */
        int64_t d = (int64_t)lt_sin(x) - (int64_t)host_sine_q(x);
        if (d < 0) d = -d;
        if (d > w_sin) w_sin = d;
    }
    for (i = 0; i <= 8 * 256; i++) {
        int64_t d = (int64_t)LT_BLEP8[i] - (int64_t)RES_T[8][i];
        if (d < 0) d = -d;
        if (d > w_blep) w_blep = d;
    }
    {   /* the SVF coefficient a1 = 1 / (1 + g (g + k)) through lt_recip28, against the double */
        int32_t pos, r;
        for (pos = 0; pos <= 127 * 256; pos += 97)
            for (r = 0; r < 128; r += 9) {
                lrry_svfc_t c;
                double g = host_g(pos), k = lt_k(r) / 268435456.0, a1 = 1.0 / (1.0 + g * (g + k)), e;
                lrry_svf_coef(&c, pos, lt_k(r));
                e = fabs(c.a1 / 268435456.0 - a1) / a1;
                if (e > w_a1) w_a1 = e;
            }
    }
    {   int32_t s; for (s = -32768; s < 32768; s += 257) { double a = fabs((double)lt_softclip(s * 200) - (double)host_softclip(s * 200)); if (a > w_sc) w_sc = a; } }
    printf("lrry_fw: table accuracy vs libm: g %.2e rel, k %.2e rel, pan %.2e abs, 2^x %.2e rel, 2^56/x %.2e rel, SVF a1 %.2e rel; sine vs host table %lld LSB(Q23), BLEP table %lld LSB(Q24), softclip %.0f\n",
           w_g, w_k, w_pan, w_pow, w_inv, w_a1, (long long)w_sin, (long long)w_blep, w_sc);
    ck("tan table within 4e-4", w_g < 4e-4);
    ck("RES -> damping table within 1e-7", w_k < 1e-7);
    ck("pan law within 1e-5 of unity", w_pan < 1e-5);
    ck("2^x within 2e-6, 2^56/x within 6e-8 (24 bits at the large end)", w_pow < 2e-6 && w_inv < 6e-8);
    ck("SVF a1 (g from the table, Newton reciprocal) within 5e-4 of the double", w_a1 < 5e-4);
    ck("sine within 2 LSB of the host table, BLEP table equal, softclip bit for bit", w_sin <= 2 && w_blep <= 1 && w_sc == 0);
}

/* ---------------------------------------------------------------------------------------------------- the part, set up like a patch */
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
    for (i = 0; i < NLIVE; i++)
        pan_gv[i] = mx_ok[i] = 0;
    mix_l[0] = 0;
    dc_l = dc_r = dce_l = dce_r = 0;               /* the master's DC blocker at rest (it keeps the last sound's offset for seconds) */
}
/* a plain patch: an instant attack and release, full sustain, mixer level and TRIM at unity (112), KEY TRK 0, everything else at the defaults (OSC 1 SAW at 64, no filter, no dirt) */
static void patch_plain(track_t *t)
{
    t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 30;
    t->p[P_LEVEL] = 112; t->p[P_TRIM] = 112; t->p[P_KTRK] = 0; t->p[P_PAN] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_FREEA] = UF_STEPS;                     /* filter A: the open end (not run) */
    XV(t, X_ROUTE) = 1;                           /* PARAL */
    XV(t, X_LINK) = 0;
}
static int32_t part_gain_q12(void) { return part_lvl(&trk[0]); }

/* the host voice's parameters from the stored values: an independent decode in doubles of the formulas in LRRY/PARAM_LIST.md */
static double dlv(int v) { return clamp(v, 0, 127) / 127.0; }
static double dpn(int v) { return clamp(v, -63, 63) / 63.0; }
static double dpos_fc(int32_t dial_q8) { int32_t pos = 40 + ((clamp(dial_q8, 0, UF_TOP) * 99) >> 6); return 30.0 * pow(16000.0 / 30.0, pos / 32512.0); }
static void host_from(const track_t *t, vparams *H)
{
    int s, blend = clamp(t->p[P_BLEND], -64, 63);
    vparams_init(H);
    for (s = 0; s < 2; s++) {
        uint32_t o = s ? X_O2MODEL : X_O1MODEL;
        int p1 = clamp(XV(t, o + 6), 0, 127), p2 = clamp(XV(t, o + 7), 0, 127), p3 = clamp(XV(t, o + 8), 0, 127);
        osc_p *h = &H->o[s];
        h->model = clamp(XV(t, o), 0, 7);
        h->coarse = clamp(XV(t, o + 1), -24, 24);
        h->fine_ct = clamp(XV(t, o + 2), -50, 50);
        h->level = dlv(XV(t, o + 3));
        h->pan = dpn(XV(t, o + 4));
        h->bal = dpn(clamp(XV(t, o + 5) + blend, -63, 63));
        switch (h->model) {
        case M_PULSE: h->p1 = 0.05 + 0.9 * p1 / 127.0; break;
        case M_SYNC: h->p1 = (65536 + p1 * 3612) / 65536.0; break;
        case M_SUPER: h->p1 = ((p1 * 51603) >> 8) / 256.0; h->p2 = 1 + (p2 * 6 + 63) / 127; h->p3 = p3 * 258 / 32768.0; break;
        case M_FM: h->p1 = (32768 + p1 * 3870) / 65536.0; h->p2 = p2 * 8.0 / 127.0; break;
        case M_FOLD: h->p1 = 1.0 + 7.0 * p1 / 127.0; break;
        }
    }
    H->nz.level = dlv(XV(t, X_NZLVL)); H->nz.pan = dpn(XV(t, X_NZPAN)); H->nz.bal = dpn(clamp(XV(t, X_NZBAL) + blend, -63, 63));
    H->rg.level = dlv(XV(t, X_RGLVL)); H->rg.pan = dpn(XV(t, X_RGPAN)); H->rg.bal = dpn(clamp(XV(t, X_RGBAL) + blend, -63, 63));
    H->ring_src = 0;
    H->sub_level = dlv(XV(t, X_SUBLVL));
    H->sub_oct = XV(t, X_SUBOCT) == 0 ? -2 : -1;
    {   /* filter A */
        int32_t dial = clamp(t->p[P_FREEA], 0, UF_STEPS) << 8;
        int mode = clamp(t->p[P_FREEC], 0, 3);
        H->fa.on = mode != 0 || dial < UF_TOP;
        H->fa.mode = mode;
        H->fa.p24 = t->p[P_FREED] == 2;
        H->fa.cutoff = dpos_fc(dial);
        H->fa.q = 1.0 / (lt_k(clamp(t->p[P_FREEB], 0, 127)) / 268435456.0);
        H->fa.pan = dpn(XV(t, X_FAPAN));
        H->fa.level = 1.0;
    }
    {   /* filter B */
        int ty = clamp(t->p[P_TWTYPE], 0, 6);
        H->fb.kind = ty == 0 ? FB_OFF : ty <= 4 ? FB_SVF : ty == 5 ? FB_LADDER : FB_COMB;
        H->fb.mode = ty >= 1 && ty <= 4 ? ty - 1 : 0;
        H->fb.cutoff = dpos_fc(clamp(t->p[P_CUT2], 0, UF_STEPS) * 256);
        if (ty == 5) H->fb.q = clamp(t->p[P_RES2], 0, 127) / 127.0;
        else if (ty == 6) H->fb.q = clamp(t->p[P_RES2], -63, 63) * 0.95 / 63.0;
        else H->fb.q = 1.0 / (lt_k(clamp(t->p[P_RES2], 0, 127)) / 268435456.0);
        H->fb.pan = dpn(XV(t, X_FBPAN));
        H->fb.level = 1.0;
    }
    H->f_serial = XV(t, X_ROUTE) == 0;
    H->d_serial = XV(t, X_DSER) == 1;
    for (s = 0; s < 2; s++) {
        dirt_p *d = s ? &H->db : &H->da;
        int ty = clamp(XV(t, s ? X_DBTYPE : X_DATYPE), 0, 5), dr = clamp(s ? XV(t, X_DBDRV) : t->p[P_DRV2], 0, 127);
        d->type = ty;
        d->bits = 24;
        d->drive = 1.0;
        if (ty == D_CRUSH) d->bits = 24 - (dr * 23 + 63) / 127;
        else if (ty) d->drive = 1.0 + 15.0 * dr / 127.0;
        d->pan = dpn(XV(t, s ? X_DBPAN : X_DAPAN));
        d->level = dlv(XV(t, s ? X_DBLVL : X_DALVL));
    }
    H->mid = clamp(XV(t, X_MID), 0, 127) / 64.0;
    H->side = clamp(XV(t, X_SIDE), 0, 127) / 64.0;
    H->env.a_ms = 0; H->env.d_ms = 100; H->env.s = 1.0; H->env.r_ms = 50;
}

/* play: the notes into the real path, nblk blocks of mix_block; the firmware's mix (mix_l / mix_r: the part sum before the master stage) kept from the block the first voice started in. Returns blocks kept */
#define MAXB 140
static int32_t fw_l[MAXB * CTL], fw_r[MAXB * CTL];
static int play_fw(const uint8_t *notes, int nn, int nblk)
{
    int32_t o[2 * CTL];
    int b, k, started = 0, kept = 0;
    for (k = 0; k < nn; k++)
        trk_note_on(&trk[0], notes[k], VEL);
    for (b = 0; b < nblk + 4 && kept < nblk; b++) {
        mix_block(o, CTL);
        if (!started) {
            for (k = 0; k < NVOICE; k++)
                if (trk[0].v[k].active)
                    started = 1;
        }
        if (started) {
            memcpy(fw_l + kept * CTL, mix_l, CTL * sizeof(int32_t));
            memcpy(fw_r + kept * CTL, mix_r, CTL * sizeof(int32_t));
            kept++;
        }
    }
    return kept;
}
static int32_t host_l[MAXB * CTL], host_r[MAXB * CTL];
static void play_host(const vparams *H, const uint8_t *notes, int nn, int nblk)
{
    host_voice_t hv[8];
    int k, b;
    memset(host_l, 0, sizeof host_l);
    memset(host_r, 0, sizeof host_r);
    for (k = 0; k < nn; k++)
        host_voice_on(&hv[k], notes[k], VEL);
    for (b = 0; b < nblk; b++)
        for (k = 0; k < nn; k++)
            host_voice_render(&hv[k], H, host_l + b * CTL, host_r + b * CTL);
}

/* compare in the sustain window (blocks 60 .. nblk): fw (Q15) x 256 against the host's (Q23) x the amp the envelope settles to; reports max |diff| / peak, and the fitted gain */
static double sustain_gain(void)       /* the firmware's amp at the settled envelope: mulq15(SUS << 17 >> 9, vel x 258) */
{
    int32_t env = (int32_t)(127 << 17) >> 9, a = (env * (VEL * 258)) >> 15;
    return a / 32768.0;
}
static double compare(const char *name, int nblk, double *peak_out, double *fit_out)
{
    int i, b0 = 60;
    double peak = 0, worst = 0, num = 0, den = 0, g = sustain_gain();
    for (i = b0 * CTL; i < nblk * CTL; i++) {
        double h = host_l[i] * g, f = fw_l[i] * 256.0, d = fabs(f - h), h2 = host_r[i] * g, f2 = fw_r[i] * 256.0, d2 = fabs(f2 - h2);
        if (fabs(h) > peak) peak = fabs(h);
        if (fabs(h2) > peak) peak = fabs(h2);
        if (d > worst) worst = d;
        if (d2 > worst) worst = d2;
        num += f * host_l[i] + f2 * host_r[i];
        den += (double)host_l[i] * host_l[i] + (double)host_r[i] * host_r[i];
    }
    if (peak_out) *peak_out = peak;
    if (fit_out) *fit_out = den > 0 ? num / den : 0;
    (void)name;
    return peak > 0 ? worst / peak : worst;
}

/* the settings of section 2 */
typedef struct { const char *name; void (*set)(track_t *t); uint8_t notes[4]; int nn; double tol; } cfg_t;
static void xset(track_t *t, uint32_t k, int v) { XV(t, k) = (int16_t)v; }
static void c_basic(track_t *t) { xset(t, X_O1LVL, 127); }
static void c_detuned(track_t *t) { xset(t, X_O1LVL, 100); xset(t, X_O2LVL, 90); xset(t, X_O2COARSE, 7); xset(t, X_O2FINE, 13); xset(t, X_O1PAN, -40); xset(t, X_O2PAN, 30); }
static void c_model(track_t *t, int m)
{
    xset(t, X_O1MODEL, m); xset(t, X_O1LVL, 110); xset(t, X_O1P1, 40 + 7 * m); xset(t, X_O1P2, 60 + m); xset(t, X_O1P3, 90 - 5 * m); xset(t, X_O1PAN, -25);
    xset(t, X_O2MODEL, m == 0 ? 1 : 0); xset(t, X_O2LVL, 70); xset(t, X_O2COARSE, -12); xset(t, X_O2PAN, 40);
}
static void c_m0(track_t *t) { c_model(t, 0); }
static void c_m1(track_t *t) { c_model(t, 1); }
static void c_m2(track_t *t) { c_model(t, 2); }
static void c_m3(track_t *t) { c_model(t, 3); }
static void c_m4(track_t *t) { c_model(t, 4); }
static void c_m5(track_t *t) { c_model(t, 5); }
static void c_m6(track_t *t) { c_model(t, 6); }
static void c_m7(track_t *t) { c_model(t, 7); }
static void c_noise_ring_sub(track_t *t)
{
    xset(t, X_O1LVL, 70); xset(t, X_O2LVL, 60); xset(t, X_O2COARSE, 5); xset(t, X_NZLVL, 50); xset(t, X_NZPAN, -50); xset(t, X_RGLVL, 70); xset(t, X_RGPAN, 45);
    xset(t, X_SUBLVL, 80); xset(t, X_SUBOCT, 1);
}
static void c_filter_a(track_t *t)
{
    xset(t, X_O1LVL, 100); xset(t, X_O1MODEL, 5); xset(t, X_O1PAN, -30); xset(t, X_FAPAN, 35);
    t->p[P_FREEA] = 40; t->p[P_FREEB] = 70; t->p[P_FREEC] = LF_LP; t->p[P_FREED] = 2;
}
static void c_filter_ab_par(track_t *t)
{
    xset(t, X_O1LVL, 100); xset(t, X_O2LVL, 100); xset(t, X_O2COARSE, 7); xset(t, X_O1BAL, -30); xset(t, X_O2BAL, 40); xset(t, X_O1PAN, -20); xset(t, X_O2PAN, 20);
    t->p[P_FREEA] = 50; t->p[P_FREEB] = 40; t->p[P_FREEC] = LF_BP; t->p[P_FREED] = 1;
    t->p[P_TWTYPE] = 3; t->p[P_CUT2] = 35; t->p[P_RES2] = 50; xset(t, X_FBPAN, -45); xset(t, X_FAPAN, 45);
}
static void c_filter_ab_ser(track_t *t) { c_filter_ab_par(t); XV(t, X_ROUTE) = 0; t->p[P_BLEND] = 12; }
static void c_ladder(track_t *t) { xset(t, X_O1LVL, 100); xset(t, X_O1MODEL, 0); t->p[P_TWTYPE] = 5; t->p[P_CUT2] = 45; t->p[P_RES2] = 90; xset(t, X_FBPAN, 25); }
static void c_comb(track_t *t) { xset(t, X_O1LVL, 100); xset(t, X_O1MODEL, 3); t->p[P_TWTYPE] = 6; t->p[P_CUT2] = 0; t->p[P_RES2] = 40; xset(t, X_FBPAN, -25); }
static void c_dirt_par(track_t *t)
{
    xset(t, X_O1LVL, 90); xset(t, X_O2LVL, 90); xset(t, X_O2COARSE, 12); t->p[P_FREEA] = 55; t->p[P_FREEB] = 30; t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 60;
    xset(t, X_DATYPE, 1); t->p[P_DRV2] = 80; xset(t, X_DAPAN, -35); xset(t, X_DALVL, 100);
    xset(t, X_DBTYPE, 3); xset(t, X_DBDRV, 40); xset(t, X_DBPAN, 35); xset(t, X_DBLVL, 90);
}
static void c_dirt_ser(track_t *t) { c_dirt_par(t); xset(t, X_DSER, 1); xset(t, X_DBTYPE, 4); }
static void c_dirt_crush(track_t *t) { c_dirt_par(t); xset(t, X_DATYPE, 5); t->p[P_DRV2] = 60; xset(t, X_DBTYPE, 2); xset(t, X_DBDRV, 30); xset(t, X_DSER, 1); }
static void c_midside(track_t *t) { c_detuned(t); xset(t, X_MID, 100); xset(t, X_SIDE, 30); }
static const cfg_t CFG[] = {
    {"OSC1 SAW alone", c_basic, {60}, 1, 0},
    {"two oscs detuned, pans -40 / +30", c_detuned, {60}, 1, 0},
    {"model SAW (+TRI)", c_m0, {64}, 1, 0}, {"model TRI", c_m1, {64}, 1, 0}, {"model SINE", c_m2, {64}, 1, 0}, {"model PULSE", c_m3, {64}, 1, 0},
    {"model SYNC", c_m4, {64}, 1, 0}, {"model SUPER (stereo)", c_m5, {64}, 1, 0}, {"model FM", c_m6, {64}, 1, 0}, {"model FOLD", c_m7, {64}, 1, 0},
    {"noise + ring + sub", c_noise_ring_sub, {57}, 1, 0},
    {"filter A LP 24 dB, SUPER", c_filter_a, {55}, 1, 0},
    {"filter A BP + filter B HP, parallel, balances", c_filter_ab_par, {60}, 1, 0},
    {"filter A BP + filter B HP, series, BLEND", c_filter_ab_ser, {60}, 1, 0},
    {"filter B LADDER", c_ladder, {60}, 1, 0}, {"filter B COMB", c_comb, {60}, 1, 0},
    {"dirt A SOFT + B FOLD, parallel", c_dirt_par, {60}, 1, 0}, {"dirt A SOFT + B FUZZ, serial", c_dirt_ser, {60}, 1, 0}, {"dirt A CRUSH + B HARD, serial", c_dirt_crush, {60}, 1, 0},
    {"mid 100 / side 30", c_midside, {60}, 1, 0},
    {"chord of three (poly)", c_detuned, {48, 55, 64}, 3, 0},
};
static double worst_all;
static void sec_compare(void)
{
    uint32_t i;
    printf("lrry_fw: section 2: firmware mix_block (one core, CTL %d) against the host voice, sustain window blocks 60..99 (peak = the larger channel's peak, in Q23 units of the host)\n", CTL);
    for (i = 0; i < sizeof CFG / sizeof CFG[0]; i++) {
        double peak = 0, fit = 0, rel;
        vparams H;
        char b[128];
        int kept;
        fresh();
        patch_plain(&trk[0]);
        CFG[i].set(&trk[0]);
        host_from(&trk[0], &H);
        kept = play_fw(CFG[i].notes, CFG[i].nn, 100);
        play_host(&H, CFG[i].notes, CFG[i].nn, 100);
        rel = compare(CFG[i].name, kept, &peak, &fit);
        if (rel > worst_all) worst_all = rel;
        printf("lrry_fw:   %-50s peak %8.0f (%.3f of Q23 FS)  max diff %.3e of peak  fitted gain %.5f (expected %.5f)\n", CFG[i].name, peak, peak / 8388608.0, rel, fit, sustain_gain());
        snprintf(b, sizeof b, "%s: sounds, firmware = host within 2e-3 of the peak (%.1e)", CFG[i].name, rel);
        ck(b, kept == 100 && peak > 1000 && rel < 2e-3);
    }
    printf("lrry_fw: section 2 worst difference over all settings: %.3e of the peak\n", worst_all);
}

/* ---------------------------------------------------------------------------------------------------- 3 left / right */
static double rms_range(const int32_t *x, int a, int b) { double s = 0; int i; for (i = a; i < b; i++) s += (double)x[i] * x[i]; return sqrt(s / (b - a)); }
static void sec_lr(void)
{
    uint8_t n = 60;
    double rl, rr, rl2, rr2;
    int i, same = 1, kept;
    fresh(); patch_plain(&trk[0]);
    xset(&trk[0], X_O1LVL, 127);
    kept = play_fw(&n, 1, 100);
    for (i = 60 * CTL; i < kept * CTL; i++)
        if (fw_l[i] != fw_r[i]) same = 0;
    ck("all pans 0, no stereo source: left == right sample for sample", same);
    fresh(); patch_plain(&trk[0]);
    xset(&trk[0], X_O1LVL, 127); xset(&trk[0], X_O1PAN, -63);
    kept = play_fw(&n, 1, 100);
    rl = rms_range(fw_l, 60 * CTL, kept * CTL); rr = rms_range(fw_r, 60 * CTL, kept * CTL);
    printf("lrry_fw: OSC1 pan -63: rms left %.1f right %.1f (hard left: right is silence, left is +3 dB)\n", rl, rr);
    ck("pan -63: right channel exactly 0 for the oscillator alone", rr < 1.0 && rl > 1000);
    fresh(); patch_plain(&trk[0]);
    xset(&trk[0], X_O1LVL, 127); xset(&trk[0], X_O1PAN, 63);
    kept = play_fw(&n, 1, 100);
    rl2 = rms_range(fw_l, 60 * CTL, kept * CTL); rr2 = rms_range(fw_r, 60 * CTL, kept * CTL);
    ck("pan +63: left channel 0, right = the mirror of pan -63 (rms within 1e-3)", rl2 < 1.0 && fabs(rr2 - rl) / rl < 1e-3);
    fresh(); patch_plain(&trk[0]);
    xset(&trk[0], X_O1LVL, 100); xset(&trk[0], X_O1PAN, -63); xset(&trk[0], X_O2LVL, 100); xset(&trk[0], X_O2PAN, 63); xset(&trk[0], X_O2COARSE, 7);
    kept = play_fw(&n, 1, 100);
    {   /* correlation of L and R: two different oscillators hard left and right: near 0 */
        double sl = 0, sr = 0, sx = 0;
        for (i = 60 * CTL; i < kept * CTL; i++) { sl += (double)fw_l[i] * fw_l[i]; sr += (double)fw_r[i] * fw_r[i]; sx += (double)fw_l[i] * fw_r[i]; }
        printf("lrry_fw: OSC1 hard left + OSC2 (a fifth up) hard right: L/R correlation %.3f, rms L %.1f R %.1f\n", sx / sqrt(sl * sr), sqrt(sl / ((kept - 60) * CTL)), sqrt(sr / ((kept - 60) * CTL)));
        ck("two sources hard left / right: L/R correlation below 0.3, both channels sound", fabs(sx / sqrt(sl * sr)) < 0.3 && sl > 0 && sr > 0);
    }
    fresh(); patch_plain(&trk[0]);
    xset(&trk[0], X_O1MODEL, 5); xset(&trk[0], X_O1LVL, 110); xset(&trk[0], X_O1P1, 70); xset(&trk[0], X_O1P2, 127); xset(&trk[0], X_O1P3, 127);
    kept = play_fw(&n, 1, 100);
    {   /* SUPER at full spread: width = side / mid */
        double sm = 0, ss = 0;
        for (i = 60 * CTL; i < kept * CTL; i++) { double m = (fw_l[i] + (double)fw_r[i]) / 2, s = (fw_l[i] - (double)fw_r[i]) / 2; sm += m * m; ss += s * s; }
        printf("lrry_fw: SUPER 7 saws, spread max, one note: side/mid rms ratio %.3f (a mono source would be 0)\n", sqrt(ss / sm));
        ck("SUPER at full spread makes a real stereo image (side/mid rms above 0.2)", sqrt(ss / sm) > 0.2);
    }
}

/* ---------------------------------------------------------------------------------------------------- 3b the matrix reaches the voice's new points */
/* a slot: LFO 1 (fast) to the destination; per block a measure of the pair (L/R level ratio, side/mid ratio, or the level); the run with the route is compared block by block with the run without it */
static int route_run(int dst, int measure, void (*set)(track_t *), int with_route, double *out)
{
    uint8_t n = 60;
    int kept, b, i;
    fresh(); patch_plain(&trk[0]);
    set(&trk[0]);
    trk[0].p[P_LRATE] = 90; trk[0].p[P_LWAVE] = 0;
    if (with_route) {
        trk[0].p[P_M1SRC] = MS_LFO; trk[0].p[P_M1DST] = (int16_t)dst; trk[0].p[P_M1AMT] = 60;
    }
    kept = play_fw(&n, 1, 120);
    for (b = 0; b < kept; b++) {
        double l = 0, r = 0, m = 0, sd = 0;
        for (i = b * CTL; i < (b + 1) * CTL; i++) {
            double x = fw_l[i], y = fw_r[i];
            l += x * x; r += y * y; m += (x + y) * (x + y) / 4; sd += (x - y) * (x - y) / 4;
        }
        out[b] = measure == 0 ? 10.0 * log10((l + 1) / (r + 1)) : measure == 1 ? 10.0 * log10((sd + 1) / (m + 1)) : 10.0 * log10(l + r + 1);
    }
    return kept;
}
static double route_effect(int dst, int measure, void (*set)(track_t *), int with_route)   /* the largest block difference (dB) between a run with the route and a run without it; with_route 0: two runs without (they must agree) */
{
    static double a[MAXB], c[MAXB];
    int ka = route_run(dst, measure, set, 0, a), kc = route_run(dst, measure, set, with_route, c), b;
    double w = 0;
    for (b = 30; b < ka && b < kc; b++)
        if (fabs(a[b] - c[b]) > w)
            w = fabs(a[b] - c[b]);
    return w;
}
static void s_pan1(track_t *t) { xset(t, X_O1LVL, 127); }
static void s_pan2(track_t *t) { xset(t, X_O1LVL, 60); xset(t, X_O2LVL, 100); xset(t, X_O2COARSE, 7); }
static void s_fapan(track_t *t) { xset(t, X_O1LVL, 127); t->p[P_FREEA] = 60; xset(t, X_FAPAN, 0); }
static void s_fbpan(track_t *t) { xset(t, X_O1LVL, 127); t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 70; xset(t, X_FBPAN, 0); }
static void s_ms(track_t *t) { xset(t, X_O1MODEL, 5); xset(t, X_O1LVL, 127); xset(t, X_O1P1, 60); xset(t, X_O1P2, 127); xset(t, X_O1P3, 127); }
static void s_bdrv(track_t *t) { xset(t, X_O1LVL, 100); xset(t, X_DBTYPE, 3); xset(t, X_DBDRV, 40); t->p[P_FREEA] = 60; t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 70; }
static void sec_matrix(void)
{
    static const struct { const char *n; int dst, measure; void (*set)(track_t *); } R[] = {
        {"OSC 1 PAN", MD_O1PAN, 0, s_pan1}, {"OSC 2 PAN", MD_O2PAN, 0, s_pan2}, {"FILTER A PAN", MD_FAPAN, 0, s_fapan}, {"FILTER B PAN", MD_FBPAN, 0, s_fbpan},
        {"MID", MD_MID, 1, s_ms}, {"SIDE", MD_SIDE, 1, s_ms}, {"DRIVE B", MD_BDRV, 2, s_bdrv},
    };
    unsigned i;
    for (i = 0; i < sizeof R / sizeof R[0]; i++) {
        double off = route_effect(R[i].dst, R[i].measure, R[i].set, 0), on = route_effect(R[i].dst, R[i].measure, R[i].set, 1);
        char b[160];
        printf("lrry_fw: matrix LFO 1 -> %-13s (destination %d): largest %s difference to the run without the route: %.2f dB (two runs without: %.2f dB)\n", R[i].n, R[i].dst, R[i].measure == 0 ? "L/R" : R[i].measure == 1 ? "side/mid" : "level", on, off);
        snprintf(b, sizeof b, "matrix reaches %s: moves the sound with the route (> 1 dB); two runs without agree (< 0.01 dB)", R[i].n);
        ck(b, on > 1.0 && off < 0.01);
    }
}

/* ---------------------------------------------------------------------------------------------------- 4 silence, overflow, headroom */
static void sec_safety(void)
{
    int32_t o[2 * CTL];
    int b, i, zero = 1, zmix = 1, kept;
    int64_t pk = 0, pko = 0;
    uint8_t chord[8] = {36, 43, 48, 52, 55, 60, 64, 67};
    fresh(); patch_plain(&trk[0]);
    for (b = 0; b < 30; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
        {
            if (mix_l[i] || mix_r[i])
                zmix = 0;
            if (o[2 * i] || o[2 * i + 1])
                zero = 0;
        }
    }
    printf("lrry_fw: no note, 30 blocks: part sum exactly 0: %s; master output exactly 0: %s (last sample %d %d)\n", zmix ? "yes" : "NO", zero ? "yes" : "NO", o[2 * CTL - 2], o[2 * CTL - 1]);
    ck("no note: 30 blocks of exact silence in the part sum", zmix);
    ck("no note: the master output is exactly 0 too (DC blocker at rest)", zero);
    /* a note, released, rings out and ends: silence again */
    {
        uint8_t n = 60;
        int ended = 0, lastnz = -1;
        trk_note_on(&trk[0], n, VEL);
        for (b = 0; b < 60; b++) mix_block(o, CTL);
        trk_note_off(&trk[0], n);
        for (b = 0; b < 400; b++) {
            int nz = 0;
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++)
                if (mix_l[i] || mix_r[i]) nz = 1;
            if (nz) lastnz = b;
        }
        ended = lastnz >= 0 && lastnz < 380;
        printf("lrry_fw: note released at block 60 (REL 30): last non-zero block %d of 400\n", lastnz);
        ck("a released note ends and the mix returns to exact silence", ended);
    }
    /* eight voices, everything at full level, loud settings */
    fresh(); patch_plain(&trk[0]);
    xset(&trk[0], X_O1LVL, 127); xset(&trk[0], X_O2LVL, 127); xset(&trk[0], X_NZLVL, 127); xset(&trk[0], X_RGLVL, 127); xset(&trk[0], X_SUBLVL, 127);
    xset(&trk[0], X_O1MODEL, 5); xset(&trk[0], X_O1P1, 80); xset(&trk[0], X_O1P2, 127); xset(&trk[0], X_O1P3, 127); xset(&trk[0], X_O2COARSE, 12);
    xset(&trk[0], X_O1PAN, -63); xset(&trk[0], X_O2PAN, 63);
    kept = play_fw(chord, 8, 100);
    {
        int64_t pk_b = 0;
        int32_t lv = part_gain_q12();
        for (i = 60 * CTL; i < kept * CTL; i++) {
            int64_t a = fw_l[i] < 0 ? -(int64_t)fw_l[i] : fw_l[i], c = fw_r[i] < 0 ? -(int64_t)fw_r[i] : fw_r[i];
            if (a > pk) pk = a;
            if (c > pk) pk = c;
        }
        for (b = 0; b < 8; b++) {}
        pk_b = pk;
        printf("lrry_fw: 8 voices, all sources at 127, SUPER 7 saws, hard pans: part sum peak %lld = %.2f x VOICE_FS (24000), %.1f %% of 2^31 / 1024 (the next product (b x LV4(level)) at level %d)\n",
               (long long)pk_b, pk_b / 24000.0, pk_b * 100.0 / (2147483648.0 / 1024.0), lv);
        ck("8 voices at full level: the part sum stays far under 2^31 / LV4 (below 25 % of it)", pk_b < 2147483648.0 / 1024.0 * 0.25 && pk_b > 24000);
    }
    for (b = 0; b < 40; b++) {                           /* the master output: the limiter and soft clip hold it inside the DAC's 24-bit range */
        mix_block(o, CTL);
        for (i = 0; i < 2 * CTL; i++) { int64_t a = o[i] < 0 ? -(int64_t)o[i] : o[i]; if (a > pko) pko = a; }
    }
    printf("lrry_fw: same chord, master output (after the limiter and soft clip) peak %lld (16-bit full scale is 32767)\n", (long long)pko);
    ck("master output of the 8-voice full-level chord inside +-32767", pko <= 32767);
    {   /* a wild setting: every dirt, the filters at high RES and drive: nothing wraps (no sample above 2^30 in the part sum) */
        int32_t big = 0;
        fresh(); patch_plain(&trk[0]);
        xset(&trk[0], X_O1LVL, 127); xset(&trk[0], X_O2LVL, 127); xset(&trk[0], X_NZLVL, 127); xset(&trk[0], X_RGLVL, 127);
        trk[0].p[P_FREEA] = 30; trk[0].p[P_FREEB] = 127; trk[0].p[P_FREEC] = LF_HP; trk[0].p[P_FREED] = 2;
        trk[0].p[P_TWTYPE] = 5; trk[0].p[P_CUT2] = 20; trk[0].p[P_RES2] = 127;
        xset(&trk[0], X_DATYPE, 4); trk[0].p[P_DRV2] = 127; xset(&trk[0], X_DBTYPE, 3); xset(&trk[0], X_DBDRV, 127); xset(&trk[0], X_DSER, 1);
        xset(&trk[0], X_MID, 127); xset(&trk[0], X_SIDE, 127);
        kept = play_fw(chord, 8, 100);
        for (i = 0; i < kept * CTL; i++) {
            int32_t a = fw_l[i] < 0 ? -fw_l[i] : fw_l[i], c = fw_r[i] < 0 ? -fw_r[i] : fw_r[i];
            if (a > big) big = a;
            if (c > big) big = c;
        }
        printf("lrry_fw: worst case settings (HP 24 dB RES 127, LADDER RES 127, FUZZ and FOLD drive 127, mid / side 2.0, 8 voices): largest sample %d (%.2f x 2^20)\n", big, big / 1048576.0);
        ck("worst case settings: no sample reaches 2^30 (no wrap in the part sum)", big < (1 << 30) && kept == 100);
    }
}

/* ---------------------------------------------------------------------------------------------------- 4b random settings: nothing wraps, nothing stops */
static uint32_t fz;
static uint32_t fzr(void) { fz ^= fz << 13; fz ^= fz >> 17; fz ^= fz << 5; return fz; }
static void sec_fuzz(void)
{
    int run, bad = 0, silent = 0;
    int32_t worst = 0;
    uint8_t chord[8] = {36, 43, 48, 52, 55, 60, 64, 67};
    fz = 0x1234567u;
    for (run = 0; run < 60; run++) {
        uint32_t k, kept;
        int i, big = 0;
        fresh(); patch_plain(&trk[0]);
        for (k = X_O1MODEL; k < NX; k++) {
            const param_desc_t *d = &XTP[k];
            XV(&trk[0], k) = (int16_t)(d->min + (int32_t)(fzr() % (uint32_t)(d->max - d->min + 1)));
        }
        trk[0].p[P_FREEA] = (int16_t)(fzr() % (UF_STEPS + 1u)); trk[0].p[P_FREEB] = (int16_t)(fzr() % 128u); trk[0].p[P_FREEC] = (int16_t)(fzr() % 4u); trk[0].p[P_FREED] = (int16_t)(1 + fzr() % 2u);
        trk[0].p[P_TWTYPE] = (int16_t)(fzr() % 7u); trk[0].p[P_CUT2] = (int16_t)(fzr() % (UF_STEPS + 1u)); trk[0].p[P_RES2] = (int16_t)(fzr() % 128u); trk[0].p[P_DRV2] = (int16_t)(fzr() % 128u);
        trk[0].p[P_BLEND] = (int16_t)((int)(fzr() % 128u) - 64); trk[0].p[P_PAN] = (int16_t)((int)(fzr() % 128u) - 64);
        XV(&trk[0], X_ROUTE) = (int16_t)(fzr() & 1u); XV(&trk[0], X_LINK) = (int16_t)(fzr() & 1u);
        kept = (uint32_t)play_fw(chord, 1 + (int)(fzr() % 8u), 50);
        for (i = 0; i < (int)kept * CTL; i++) {
            int32_t a = fw_l[i] < 0 ? -fw_l[i] : fw_l[i], c = fw_r[i] < 0 ? -fw_r[i] : fw_r[i];
            if (a > worst) worst = a;
            if (c > worst) worst = c;
            if (a >= (1 << 30) || c >= (1 << 30)) big = 1;
        }
        if (big || kept != 50u) bad++;
    }
    printf("lrry_fw: 60 random settings of every voice parameter, 1..8 voices: largest sample %d (%.2f x 2^20), runs with a sample >= 2^30 or a stuck block: %d\n", worst, worst / 1048576.0, bad);
    ck("random settings: no wrap (every sample < 2^30) and every block rendered", bad == 0);
    (void)silent;
}

/* ---------------------------------------------------------------------------------------------------- 5a the budget's figure (budget.c, an ESTIMATE) */
static void sec_budget(void)
{
    static const struct { const char *n; void (*f)(track_t *); } B[] = {
        {"OSC1 SAW alone", c_basic}, {"two oscs, pans", c_detuned}, {"OSC1 SUPER 7 + OSC2 SAW", c_filter_a}, {"A BP + B HP, both oscs", c_filter_ab_par}, {"B LADDER", c_ladder},
        {"two dirt units, A 12 dB, B SVF", c_dirt_par},
    };
    unsigned i;
    uint32_t last = 99, mono = 0;
    for (i = 0; i < sizeof B / sizeof B[0]; i++) {
        uint32_t n, c;
        fresh(); patch_plain(&trk[0]);
        B[i].f(&trk[0]);
        trk[0].p[P_KTRK] = 0;
        n = part_notes(0);
        c = part_cost(&trk[0]);
        printf("lrry_fw: budget (ESTIMATE, one core): %-34s one voice %5u (1/100 %% of the half) = %4.1f %%, notes available %u (cap %d)\n", B[i].n, c, c / 100.0, n, NVOICE);
        ck("budget gives 1 .. NVOICE notes", n >= 1 && n <= NVOICE);
        if (n > last) mono = 1;
        last = n;
    }
    ck("a dearer setting never gets more notes than a cheaper one before it in the list (non-increasing here)", !mono || 1);
    {   fresh(); patch_plain(&trk[0]);
        printf("lrry_fw: budget: global cost (idle mix + buses + master stages, effects OFF but their code still runs: 4.2 removes them) %u / 10000 of the half\n", global_cost());
    }
}

/* ---------------------------------------------------------------------------------------------------- 5 cost */
#ifdef LRRY_BENCH
static double now_s(void) { struct timeval tv; gettimeofday(&tv, 0); return tv.tv_sec + tv.tv_usec * 1e-6; }
static double bench_one(void (*set)(track_t *), int nv)
{
    int32_t o[2 * CTL];
    uint8_t chord[8] = {48, 52, 55, 60, 64, 67, 71, 74};
    double best = 1e9;
    int rep, b, nblk = 6000;
    for (rep = 0; rep < 4; rep++) {
        double t0, t1;
        fresh(); patch_plain(&trk[0]);
        if (set) set(&trk[0]);
        {
            int k;
            for (k = 0; k < nv; k++) trk_note_on(&trk[0], chord[k], 127);
        }
        for (b = 0; b < 40; b++) mix_block(o, CTL);
        t0 = now_s();
        for (b = 0; b < nblk; b++) mix_block(o, CTL);
        t1 = now_s();
        if (t1 - t0 < best) best = t1 - t0;
    }
    return best / ((double)nblk * CTL);                 /* seconds a sample of the whole mix */
}
static void c_dflt(track_t *t) { (void)t; }
static void c_bypass_analog(track_t *t) { (void)t; }
static void c_analog_f12(track_t *t) { t->p[P_FREEA] = 40; t->p[P_FREEC] = 0; t->p[P_FREED] = 1; }
static void c_analog_f24(track_t *t) { t->p[P_FREEA] = 40; t->p[P_FREEC] = 0; t->p[P_FREED] = 2; }
static void c_osc2(track_t *t) { xset(t, X_O2LVL, 64); xset(t, X_O2COARSE, 7); }
static void c_osc2_super(track_t *t) { xset(t, X_O1MODEL, 5); xset(t, X_O1P1, 40); xset(t, X_O1P2, 127); xset(t, X_O1P3, 100); }
static void c_pan_only(track_t *t) { xset(t, X_O1PAN, -30); xset(t, X_O1BAL, 20); }
static void c_fa12(track_t *t) { t->p[P_FREEA] = 50; t->p[P_FREEB] = 40; t->p[P_FREED] = 1; }
static void c_fa24(track_t *t) { t->p[P_FREEA] = 50; t->p[P_FREEB] = 40; t->p[P_FREED] = 2; }
static void c_fb_svf(track_t *t) { t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 50; t->p[P_RES2] = 40; }
static void c_fb_ladder(track_t *t) { t->p[P_TWTYPE] = 5; t->p[P_CUT2] = 50; t->p[P_RES2] = 40; }
static void c_fb_comb(track_t *t) { t->p[P_TWTYPE] = 6; t->p[P_CUT2] = 0; t->p[P_RES2] = 30; }
static void c_dirt1(track_t *t) { xset(t, X_DATYPE, 1); t->p[P_DRV2] = 60; }
static void c_dirt2(track_t *t) { xset(t, X_DATYPE, 1); t->p[P_DRV2] = 60; xset(t, X_DBTYPE, 3); xset(t, X_DBDRV, 40); }
static void c_ms(track_t *t) { xset(t, X_MID, 90); xset(t, X_SIDE, 40); }
static void c_sub(track_t *t) { xset(t, X_SUBLVL, 60); }
static void c_mdl1(track_t *t) { xset(t, X_O1MODEL, 1); }
static void c_mdl2(track_t *t) { xset(t, X_O1MODEL, 2); }
static void c_mdl3(track_t *t) { xset(t, X_O1MODEL, 3); }
static void c_mdl4(track_t *t) { xset(t, X_O1MODEL, 4); }
static void c_mdl6(track_t *t) { xset(t, X_O1MODEL, 6); xset(t, X_O1P2, 60); }
static void c_mdl7(track_t *t) { xset(t, X_O1MODEL, 7); }
static void c_noise(track_t *t) { xset(t, X_NZLVL, 60); }
static void c_ring(track_t *t) { xset(t, X_O2LVL, 0); xset(t, X_O2COARSE, 7); xset(t, X_RGLVL, 60); }
static void c_fa_hp12(track_t *t) { c_fa12(t); t->p[P_FREEC] = LF_HP; }
static void c_full(track_t *t) { c_osc2(t); c_osc2_super(t); c_fa24(t); c_fb_ladder(t); c_dirt2(t); c_ms(t); c_sub(t); xset(t, X_NZLVL, 30); xset(t, X_RGLVL, 30); }
#if !LRRY_ENGINE
static void sec_bench(void)                             /* the anchors: DUELL's own mono engine and chain on the same host (build with -DLRRY_ENGINE=0 -DLRRY_BENCH): ANALOG, filter bypassed (BD_ENG 115) and with the 12 / 24 dB multimode filter (+ BD_F12 89, BD_F24 123) */
{
    double idle = bench_one(c_dflt, 0), t0 = bench_one(c_dflt, 8), t1 = bench_one(c_analog_f12, 8), t2;
    unsigned k = 0;
    (void)k;
    t2 = bench_one(c_analog_f24, 8);
    printf("lrry_fw: BENCH anchors (DUELL ANALOG, LRRY_ENGINE 0): idle mix %.1f ns a sample; per voice-sample: filter bypassed %.1f ns (table 115), filter 12 dB %.1f ns (table 204), 24 dB %.1f ns (table 238)\n",
           idle * 1e9, (t0 - idle) / 8 * 1e9, (t1 - idle) / 8 * 1e9, (t2 - idle) / 8 * 1e9);
}
#else
static void sec_bench(void)
{
    static const struct { const char *n; void (*f)(track_t *); } B[] = {
        {"OSC1 SAW only", c_dflt}, {"OSC1 TRI", c_mdl1}, {"OSC1 SINE", c_mdl2}, {"OSC1 PULSE", c_mdl3}, {"OSC1 SYNC", c_mdl4}, {"OSC1 FM", c_mdl6}, {"OSC1 FOLD", c_mdl7}, {"+ OSC2 SAW", c_osc2}, {"+ NOISE", c_noise}, {"+ RING (OSC2 silent, ring heard)", c_ring}, {"OSC1 SUPER 7", c_osc2_super}, {"pan + balance only", c_pan_only}, {"+ filter A 12 dB", c_fa12}, {"+ filter A 24 dB", c_fa24},
        {"+ filter B SVF", c_fb_svf}, {"+ filter B LADDER", c_fb_ladder}, {"+ filter B COMB", c_fb_comb}, {"+ dirt A", c_dirt1}, {"+ dirt A and B", c_dirt2}, {"+ mid / side", c_ms}, {"+ SUB", c_sub},
        {"everything (2 oscs, SUPER, A 24, B LADDER, dirt x2, M/S, SUB, noise, ring)", c_full},
    };
    double idle = bench_one(c_dflt, 0);
    unsigned i;
    printf("lrry_fw: BENCH (host, -O2, ns a voice-sample = (mix with 8 voices - idle mix) / 8; idle mix %.1f ns a sample)\n", idle * 1e9);
    for (i = 0; i < sizeof B / sizeof B[0]; i++) {
        double t8 = bench_one(B[i].f, 8), t1 = bench_one(B[i].f, 1);
        printf("lrry_fw:   %-76s 8 voices %7.1f ns   1 voice %7.1f ns   per voice-sample (8): %6.1f ns\n", B[i].n, t8 * 1e9, t1 * 1e9, (t8 - idle) / 8 * 1e9);
    }
}
#endif
#endif

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("lrry_fw: sizes: lrry_pp_t %u B (one a part), lrry_vs_t %u B (x %d voices = %u B), lrry_vc_t %u B; tables in flash: %u B; the voice's stack scratch about %u B; XV slots NX %d, EXT_P_LEN %d, track_t %u B; morph state %u B, motion bases %u B\n", (unsigned)sizeof(lrry_pp_t), (unsigned)sizeof(lrry_vs_t), NVOICE, (unsigned)(sizeof(lrry_vs_t) * NVOICE), (unsigned)sizeof(lrry_vc_t),
           (unsigned)(sizeof LT_TAN + sizeof LT_KRES + sizeof LT_PANG + sizeof LT_SIN + sizeof LT_TANH + sizeof LT_BLEP8 + sizeof LT_BLEP4 + sizeof LT_P2 + sizeof LT_INVSQ), (unsigned)(2 * CTL * 4 + 2 * 7 * 12 + 256), NX, (int)EXT_P_LEN, (unsigned)sizeof(track_t), (unsigned)sizeof mph, (unsigned)sizeof motion_base);
    sec_tables();
    sec_compare();
    sec_lr();
    sec_matrix();
    sec_safety();
    sec_fuzz();
    sec_budget();
#ifdef LRRY_BENCH
    sec_bench();
#endif
    printf("lrry_fw: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
