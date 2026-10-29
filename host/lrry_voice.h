/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY stereo voice, step 2 (host first): the Teensy architecture (sources with own pan and A/B balance, filter A and B, dirt A and B, amp, mid/side)
 * on the step 1 kernels. Design: DUELL/release_drafts/LRRY_STEREO_VOICE_DESIGN.md. One voice = one job: own state, own scratch, no globals except tables
 * (sine and tanh tables are read-only after init). Oscillator models here are the BASIC ones; DUELL's WAVE / NOISE / FORMANT / PHASE / LOFI cores come in a later step. */
#ifndef LRRY_VOICE_H
#define LRRY_VOICE_H
#include <string.h>
#include "lrry_filt2.h"

#ifndef BLK
#define BLK 128                   /* (the control block: the firmware test builds it with 32 = CTL, LRRY/fw/felucca-base/tests/lrry_fw_test.c) */
#endif
enum { M_SAW, M_TRI, M_SINE, M_PULSE, M_SYNC, M_SUPER, M_FM, M_FOLD, M_N };
enum { S_O1, S_O2, S_NOISE, S_RING, NSRC };
enum { FB_OFF, FB_SVF, FB_LADDER, FB_COMB };           /* unit B kinds; the SVF mode is in .mode */

/* ---- LEVEL PLAN (owner asked 2026-10-24; numbers measured by lrry_voice_test.c t_levels) ----
 * 1. Every source at level 1.0, centre, has about the same loudness (RMS about 0.56 of its own full scale = a saw's), whatever the model: MG[] equalises the models (at their default
 *    P1..P3; FM index and fold amount change the loudness, that is part of the sound). NOISE, SUB and RING have their own gains (NG, SG, RG).
 * 2. The voice works on DUELL's voice scale: a full-scale oscillator is VOICE_FS = 24000 / 32768 = 0.732 of the DAC's range, because DUELL's master then makes the mix at gain 0.5
 *    (MASTER_USB_REF) before its limiter (threshold 0.549) and soft clip. So one saw at level 1.0 peaks at about 0.73 here and about 0.36 at the DAC: the same as a DUELL voice.
 * 3. Pans: a source's pan law has unity at the centre on both channels and 1.414 on the panned side (constant power); a hard-panned source is 3 dB louder in its channel. Units' pans fold, no gain.
 * 4. Nothing clips inside the voice (32-bit words, 8 bits of headroom); the master stage (limiter, soft clip) is the only place that limits. */
#define VOICE_GAIN 0.7324          /* 24000 / 32768 */
static const double MG[8] = {1.00, 0.97, 0.79, 0.57, 1.01, 1.0, 1.10, 1.27};   /* SAW TRI SINE PULSE SYNC SUPER FM FOLD (SUPER: see the 1/sqrt(n) below) */
#define NG 0.97                    /* noise */
#define SG 0.79                    /* SUB (a sine) */
#define RG 1.33                    /* ring mod (osc1 x osc2) */
static int32_t MGQ[8], INVSQ_Q[8], VGQ, NGQ, SGQ, RGQ;

/* ---- tables ---- */
static int32_t SIN_T[4097];
static void blep_init(void);
static void voice_tables_init(void) { int i; blep_init(); tanh_init(); for (i = 0; i < 8; i++) { MGQ[i] = cq(MG[i]); INVSQ_Q[i] = i ? cq(1.0 / sqrt((double)i)) : 0; } VGQ = cq(VOICE_GAIN); NGQ = cq(NG); SGQ = cq(SG); RGQ = cq(RG); for (i = 0; i < 4097; i++) SIN_T[i] = (int32_t)lrint(sin(2.0 * M_PI * i / 4096.0) * (FS_Q23 - 1)); }
static inline int32_t sine_q(uint32_t p) { uint32_t i = p >> 20, f = (p >> 4) & 0xFFFFu; return SIN_T[i] + (int32_t)(((int64_t)(SIN_T[i + 1] - SIN_T[i]) * f) >> 16); }

/* (fixed 2026-10-24: the table once held S - step, so samples within 1/256 of the jump interpolated across it and overshot by up to 0.45 FS) */
/* ---- BLEP (integer, table driven): residual for a jump of 2 FS at phase 0, in Q23. Kernel: the cardinal B-spline of order BLEP_M (M samples wide: 2 = the old 2-point polyBLEP,
 * 4 = cubic, 6 = quintic, 8; its spectrum has M-th order zeros at every multiple of the sample rate). res(x) = S(x) - step(x), x = samples since the jump (negative before it),
 * S = the integrated kernel, from the truncated-power formula S(x) = 1/M! sum_k (-1)^k C(M,k) (x + M/2 - k)_+^M. Table: 256 steps per sample, linear interpolation. ---- */
#ifndef SYNC_CORR
#define SYNC_CORR 0
#endif
#ifndef FOLD_IN
#define FOLD_IN 1                  /* what the wavefolder folds: 0 saw, 1 triangle (default: measured best), 2 sine */
#endif
#ifndef BLEP_M
#define BLEP_M 8
#endif
static int32_t RES_T[9][8 * 256 + 1];                  /* [M][...]: Q24 of res */
static int blep_ready;
static void blep_init(void)
{
    int M, i, k;
    if (blep_ready) return; blep_ready = 1;
    for (M = 2; M <= 8; M += 2) {
        double fact = 1; for (i = 2; i <= M; i++) fact *= i;
        for (i = 0; i <= M * 256; i++) {
            double x = (double)i / 256.0 - M / 2.0, S = 0, c = 1;
            for (k = 0; k <= M; k++) { double t = x + M / 2.0 - k; if (t > 0) S += (k & 1 ? -1.0 : 1.0) * c * pow(t, M); c = c * (M - k) / (k + 1); }
            S /= fact; RES_T[M][i] = (int32_t)lrint(S * 16777216.0);     /* S only (smooth): the step is subtracted in res_q_m, so no interpolation across the jump */
        }
    }
}
static inline int32_t res_q_m(int M, int64_t x)        /* x in Q24 samples */
{
    int64_t lim = (int64_t)(M / 2) << 24, u; int32_t *T = RES_T[M]; int idx, fr;
    if (x >= lim || x <= -lim) return 0;
    u = x + lim; idx = (int)(u >> 16); fr = (int)(u & 0xFFFF);
    { int32_t v = T[idx] + (int32_t)(((int64_t)(T[idx + 1] - T[idx]) * fr) >> 16); return x >= 0 ? v - (1 << 24) : v; }
}
static inline int32_t blep_m_q(int M, uint32_t p, uint32_t inc)
{
    uint32_t w = (uint32_t)(M / 2) * inc;
    if (inc > (1u << 29)) return 0;
    if (p < w) return res_q_m(M, ((int64_t)p << 24) / inc);
    if (p > (uint32_t)(0u - w)) return res_q_m(M, -(((int64_t)(0u - p) << 24) / inc));
    return 0;
}
static inline int32_t res_q(int64_t x) { return res_q_m(BLEP_M, x); }
static inline int32_t blep_q(uint32_t p, uint32_t inc) { return blep_m_q(BLEP_M, p, inc); }
/* the old 2-point version, kept to measure the difference */
static inline int32_t blep2_q(uint32_t p, uint32_t inc)
{
    int64_t t;
    if (p < inc) { t = ((int64_t)p << 24) / inc; return (int32_t)(((t * 2 - ((t * t) >> 24)) - (1 << 24)) >> 1); }
    if (p > (uint32_t)(0u - inc)) { t = -(((int64_t)(0u - p) << 24) / inc); return (int32_t)((((t * t) >> 24) + t * 2 + (1 << 24)) >> 1); }
    return 0;
}
static inline int32_t osc_saw_q(uint32_t p, uint32_t inc) { return (int32_t)(p + 0x80000000u) / 256 - blep_q(p, inc); }   /* the jump sits at phase 0, where blep_q is centred */
static inline int32_t osc_pulse_q(uint32_t p, uint32_t inc, uint32_t pw) { return (p < pw ? FS_Q23 - 1 : -FS_Q23) + blep_q(p, inc) - blep_q(p - pw, inc); }
static inline int32_t osc_tri_q(uint32_t p) { int32_t x = (int32_t)(p + 0x40000000u); x = x < 0 ? -x - 1 : x; return (x >> 7) - FS_Q23 + 1; }   /* +-FS, peak at p = 1/4 */

/* ---- parameters (what a patch sets; matrix and LFOs will add to them per block) ---- */
typedef struct { int model; double coarse, fine_ct, level, pan, p1, p2, p3, bal; } osc_p;   /* p1..p3 as Teensy: pulse width, sync ratio, supersaw detune/count/spread, FM ratio/index/fb, fold amount */
typedef struct { double level, pan, bal; } src_p;                                             /* noise, ring */
typedef struct { int kind, mode; double cutoff, q, pan, level; } fb_p;                         /* unit B: kind + SVF mode; (unit A is an SVF cascade: .kind unused, .mode LP/BP/HP, .p24) */
typedef struct { int on, mode, p24; double cutoff, q, pan, level; } fa_p;
typedef struct { int type, bits; double drive, pan, level; } dirt_p;
typedef struct { double a_ms, d_ms, s, r_ms; } env_p;
typedef struct {
    osc_p o[2]; src_p nz, rg; int ring_src;
    double sub_level; int sub_oct;                       /* SUB LAYER: mono, centre (octave -1 or -2), sine */
    fa_p fa; fb_p fb; int f_serial;
    dirt_p da, db; int d_serial;
    env_p env; double mid, side;
} vparams;

/* ---- state ---- */
typedef struct {
    uint32_t ph[2], ph2[2], sync_sb[2], sub_ph, sup_ph[7], nst; int32_t fm_y[2];
    svf_pair_s fa1, fa2, fb; lad_s lad[2]; comb_s comb[2];
    int32_t gprev[NSRC][2][2];                    /* last block's source gains [source][bus][L/R] (per-sample ramp: no zipper) */
    int32_t env; int stage; int active; int32_t ref_gain;
    int note, vel;
} voice_t;

static inline void voice_on(voice_t *v, int note, int vel) { memset(v, 0, sizeof *v); v->nst = 0x9E3779B9u * (uint32_t)(note + 1); v->note = note; v->vel = vel; v->active = 1; v->stage = 1; }
static inline void voice_off(voice_t *v) { if (v->active && v->stage != 3) v->stage = 3; }

static inline uint32_t incf(double hz) { return (uint32_t)(hz / SR * 4294967296.0); }
static inline double note_hz(double n) { return 440.0 * pow(2.0, (n - 69.0) / 12.0); }
static inline uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* one mono sample of oscillator model m, or for SUPER a pair via *r */
static inline int32_t osc_tick(voice_t *v, int i, const osc_p *o, uint32_t inc, int32_t *sup_r, int32_t sup_pan_l[7], int32_t sup_pan_r[7], uint32_t sup_inc[7], int sup_n)
{
    uint32_t *p = &v->ph[i];
    int32_t y = 0;
    switch (o->model) {
    case M_SAW: y = osc_saw_q(*p, inc); break;
    case M_TRI: y = osc_tri_q(*p); break;
    case M_SINE: y = sine_q(*p); break;
    case M_PULSE: { double pwd = o->p1 < 0.05 ? 0.05 : o->p1 > 0.95 ? 0.95 : o->p1; uint32_t pw = (uint32_t)(pwd * 4294967296.0); y = osc_pulse_q(*p, inc, pw); break; }
    case M_SYNC: {                                      /* master phase *p (before this sample's advance); slave phase v->ph2[i]; the reset is placed at its fractional time and corrected */
        uint32_t sp = v->ph2[i], sinc = (uint32_t)((uint64_t)inc * (uint32_t)(o->p1 * 65536.0) >> 16), pn = *p + inc;
        int64_t x = 0; int near = 0;
        y = osc_saw_q(sp, sinc);
        if (*p < (uint32_t)(BLEP_M / 2) * inc) { x = ((int64_t)*p << 24) / inc; near = 1; }                              /* the reset was x samples ago */
        else if (*p > (uint32_t)(0u - (uint32_t)(BLEP_M / 2) * inc)) { x = -(((int64_t)(0u - *p) << 24) / inc); near = 1; } /* the reset comes in -x samples */
        if (near && SYNC_CORR) {                         /* jump of the slave at the reset: from its phase just before to phase 0 */
            uint32_t sb = v->sync_sb[i];                 /* after the reset: the phase it had just before (stored when the reset happened) */
            if (x <= 0) sb = sp + (uint32_t)((double)(-x) / 16777216.0 * (double)sinc);   /* before the reset: the free running slave, -x samples on */
            { int64_t J = (int64_t)(-FS_Q23) - (int64_t)((int32_t)(sb + 0x80000000u) / 256);      /* naive saw(0) - naive saw(sb) */
              y += (int32_t)((J * (int64_t)res_q(x)) >> 24); }                                    /* J x res(x): res is Q24 of a unit jump, J in Q23 */
        }
        if (pn < *p) {                                   /* the master wraps between this sample and the next: place the reset in time */
            double tr = (double)(inc - pn) / (double)inc;                                  /* samples from now to the reset */
            v->sync_sb[i] = sp + (uint32_t)(tr * (double)sinc);
            v->ph2[i] = (uint32_t)(((uint64_t)pn * sinc) / inc);                           /* the slave restarts, already (1 - tr) samples on */
        } else v->ph2[i] = sp + sinc;
        break; }
    case M_FM: { uint32_t mi = incf(note_hz(v->note + o->coarse) * o->p1); (void)mi;
        int32_t mod = sine_q(v->ph2[i]); uint32_t idx = (uint32_t)(o->p2 / (2.0 * M_PI) * 4294967296.0 / 2.0);   /* index in radians: phase units of 2^32 / 2pi */
        uint32_t ph = *p + (uint32_t)(((int64_t)mod * (int64_t)(o->p2 * (4294967296.0 / (2.0 * M_PI)))) >> 23); (void)idx;
        y = sine_q(ph); v->ph2[i] += (uint32_t)((uint64_t)inc * (uint32_t)(o->p1 * 65536.0) >> 16); break; }
    case M_FOLD: { int32_t s = FOLD_IN == 2 ? sine_q(*p) : FOLD_IN == 1 ? osc_tri_q(*p) : osc_saw_q(*p, inc); y = dirt_shape(D_FOLD, (int32_t)(((int64_t)s * (int64_t)(o->p1 * 16777216.0)) >> 24), 24); y = clamp32(y, -FS_Q23, FS_Q23); break; }
    case M_SUPER: {
        int k; int64_t l = 0, r = 0; for (k = 0; k < sup_n; k++) { int32_t s = osc_saw_q(v->sup_ph[k], sup_inc[k]); v->sup_ph[k] += sup_inc[k]; l += (int64_t)s * sup_pan_l[k] >> COEF_Q; r += (int64_t)s * sup_pan_r[k] >> COEF_Q; }
        *sup_r = (int32_t)(r * INVSQ_Q[sup_n] >> COEF_Q); y = (int32_t)(l * INVSQ_Q[sup_n] >> COEF_Q); break; }
    }
    *p += inc;
    return y;
}

/* render one block: ADDS the voice's stereo output to outL / outR. Returns 1 while the voice sounds. */
static int voice_render(voice_t *v, const vparams *P, int32_t *outL, int32_t *outR)
{
    int i, s, k;
    int32_t bufL[BLK], bufR[BLK];                         /* the voice's pair after the amp */
    if (!v->active) return 0;
    {
        /* ---- block control ---- */
        double hz0 = note_hz(v->note + P->o[0].coarse + P->o[0].fine_ct / 100.0), hz1 = note_hz(v->note + P->o[1].coarse + P->o[1].fine_ct / 100.0);
        uint32_t inc0 = incf(hz0), inc1 = incf(hz1), incs = incf(note_hz(v->note) / (P->sub_oct == -2 ? 4.0 : 2.0));
        uint32_t incs_[2] = {inc0, inc1};
        int32_t tg[NSRC][2][2];         /* target gains [source][bus][L/R] */
        double bal_a[NSRC], bal_b[NSRC];
        int32_t sup_l[7], sup_r_[7]; uint32_t sup_inc[7]; int sup_n[2] = {0, 0};
        pan_t pa, pb, pda, pdb; ms_t ms; svf_c ca, cb; lad_c lc; comb_c cc; int sup_idx = -1;
        double lev[NSRC];
        const osc_p *oo[2] = {&P->o[0], &P->o[1]}; const double *pans[NSRC] = {&P->o[0].pan, &P->o[1].pan, &P->nz.pan, &P->rg.pan}; const double *bals[NSRC] = {&P->o[0].bal, &P->o[1].bal, &P->nz.bal, &P->rg.bal};
        (void)incs_; (void)sup_idx;
        lev[0] = P->o[0].level; lev[1] = P->o[1].level; lev[2] = P->nz.level; lev[3] = P->rg.level;
        for (s = 0; s < NSRC; s++) {
            double b = *bals[s]; int32_t gl, gr;
            if (P->f_serial) { bal_a[s] = b <= 0 ? 1.0 : 1.0 - b; bal_b[s] = b > 0 ? b : 0.0; }
            else { bal_a[s] = b <= 0 ? 1.0 : 1.0 - b; bal_b[s] = b >= 0 ? 1.0 : 1.0 + b; }
            if (P->fa.on == 0 && P->fb.kind == FB_OFF) { bal_a[s] = 1.0; bal_b[s] = 0.0; }
            pan_mono(*pans[s], &gl, &gr);
            for (k = 0; k < 2; k++) { double bb = k ? bal_b[s] : bal_a[s]; tg[s][k][0] = (int32_t)lrint((double)gl * lev[s] * bb); tg[s][k][1] = (int32_t)lrint((double)gr * lev[s] * bb); }
        }
        for (s = 0; s < 2; s++) if (oo[s]->model == M_SUPER) {
            int n = (int)lrint(oo[s]->p2 * 1.0); if (n < 1) n = 1; if (n > 7) n = 7; sup_n[s] = n;
        }
        for (k = 0; k < 7; k++) { sup_l[k] = sup_r_[k] = 0; sup_inc[k] = 0; }
        pan_set(&pa, P->fa.pan); pan_set(&pb, P->fb.pan); pan_set(&pda, P->da.pan); pan_set(&pdb, P->db.pan); ms_set(&ms, P->mid, P->side);
        svf_coef(&ca, P->fa.cutoff, P->fa.q); svf_coef(&cb, P->fb.cutoff, P->fb.q);
        lad_coef(&lc, P->fb.cutoff, P->fb.q); comb_coef(&cc, note_hz(v->note), P->fb.q, 0.3);
        /* ---- per-sample ---- */
        {
            int32_t drA = (int32_t)lrint(P->da.drive * 16777216.0), drB = (int32_t)lrint(P->db.drive * 16777216.0);
            int32_t sub_g = (int32_t)lrint(P->sub_level * 268435456.0);
            int32_t lvA = cq(P->fa.level), lvB = cq(P->fb.level), ldA = cq(P->da.level), ldB = cq(P->db.level);
            int32_t aenvgoal; double atk = P->env.a_ms > 0 ? 1.0 - exp(-1.0 / (P->env.a_ms * 0.001 * SR / 5.0)) : 1.0, dec = 1.0 - exp(-1.0 / (P->env.d_ms * 0.001 * SR / 5.0)), rel = 1.0 - exp(-1.0 / (P->env.r_ms * 0.001 * SR / 5.0));
            int32_t sus = (int32_t)lrint(P->env.s * FS_Q23);
            (void)aenvgoal;
            for (i = 0; i < BLK; i++) {
                int32_t bus[2][2] = {{0, 0}, {0, 0}};           /* [bus A/B][L,R] */
                int32_t os[2], ors[2] = {0, 0};
                int32_t nzv = (int32_t)xs(&v->nst) >> 8;         /* white noise, +-FS */
                float ramp = (float)(i + 1) / BLK;
                /* oscillators (mono sources; SUPER gives its own pair) */
                for (s = 0; s < 2; s++) {
                    int32_t sr_ = 0; uint32_t inc = s ? inc1 : inc0;
                    if (oo[s]->model == M_SUPER) {
                        int n = sup_n[s]; double det = oo[s]->p1, spread = oo[s]->p3; int kk; int32_t pl[7], pr[7]; uint32_t sinc[7];
                        for (kk = 0; kk < n; kk++) { double off = n > 1 ? (2.0 * kk / (n - 1) - 1.0) : 0.0; sinc[kk] = incf((s ? hz1 : hz0) * pow(2.0, off * det / 1200.0)); int32_t gl, gr; pan_mono(off * spread, &gl, &gr); pl[kk] = gl; pr[kk] = gr; }
                        os[s] = mulq(osc_tick(v, s, oo[s], inc, &sr_, pl, pr, sinc, n), MGQ[oo[s]->model]); ors[s] = mulq(sr_, MGQ[oo[s]->model]);
                    } else { os[s] = mulq(osc_tick(v, s, oo[s], inc, &sr_, sup_l, sup_r_, sup_inc, 0), MGQ[oo[s]->model]); ors[s] = os[s]; }
                }
                {
                    int32_t src[NSRC], srcR[NSRC];
                    src[0] = os[0]; srcR[0] = oo[0]->model == M_SUPER ? ors[0] : os[0];
                    src[1] = os[1]; srcR[1] = oo[1]->model == M_SUPER ? ors[1] : os[1];
                    src[2] = mulq(nzv, NGQ); srcR[2] = src[2];
                    src[3] = mulq((int32_t)(((int64_t)os[P->ring_src ? 1 : 0] * os[1]) >> 23), RGQ); srcR[3] = src[3];   /* ring: osc1 x osc2 (or osc2 x osc2 when ring_src) */
                    for (s = 0; s < NSRC; s++) {
                        for (k = 0; k < 2; k++) {
                            int32_t gl = v->gprev[s][k][0] + (int32_t)((tg[s][k][0] - v->gprev[s][k][0]) * ramp), gr = v->gprev[s][k][1] + (int32_t)((tg[s][k][1] - v->gprev[s][k][1]) * ramp);
                            if (oo[s < 2 ? s : 0]->model == M_SUPER && s < 2) {   /* a stereo source: its own L and R, gains scale by level only (pan is inside the saws) */
                                double lvv = lev[s] * (k ? bal_b[s] : bal_a[s]);
                                bus[k][0] += (int32_t)((int64_t)src[s] * (int64_t)lrint(lvv * 268435456.0) >> COEF_Q);
                                bus[k][1] += (int32_t)((int64_t)srcR[s] * (int64_t)lrint(lvv * 268435456.0) >> COEF_Q);
                            } else {
                                bus[k][0] += (int32_t)((int64_t)src[s] * gl >> COEF_Q);
                                bus[k][1] += (int32_t)((int64_t)src[s] * gr >> COEF_Q);
                            }
                        }
                    }
                }
                /* SUB LAYER: mono, centre, equal into L and R of bus A (and bus B in parallel gets nothing: it goes through the first unit that runs) */
                if (sub_g) { int32_t sb = mulq((int32_t)(((int64_t)sine_q(v->sub_ph) * sub_g) >> COEF_Q), SGQ); v->sub_ph += incs; bus[0][0] += sb; bus[0][1] += sb; }
                /* ---- filter section ---- */
                {
                    int32_t aL = bus[0][0], aR = bus[0][1], bL = bus[1][0], bR = bus[1][1], oL, oR;
                    int32_t xL1, xR1, xL2, xR2;
                    if (P->fa.on) { svf_pair(&ca, &v->fa1, &aL, &aR, P->fa.mode); if (P->fa.p24) svf_pair(&ca, &v->fa2, &aL, &aR, P->fa.mode); aL = mulq(aL, lvA); aR = mulq(aR, lvA); pan_pair(&pa, &aL, &aR); }
                    if (P->f_serial) { bL += aL; bR += aR; aL = aR = 0; }
                    if (P->fb.kind != FB_OFF) {
                        if (P->fb.kind == FB_SVF) { svf_s *sl = &v->fb.l, *sr2 = &v->fb.r; bL = svf_tick(&cb, sl, bL, P->fb.mode); bR = svf_tick(&cb, sr2, bR, P->fb.mode); }
                        else if (P->fb.kind == FB_LADDER) { bL = lad_tick(&lc, &v->lad[0], bL); bR = lad_tick(&lc, &v->lad[1], bR); }
                        else { bL = comb_tick(&cc, &v->comb[0], bL); bR = comb_tick(&cc, &v->comb[1], bR); }
                        bL = mulq(bL, lvB); bR = mulq(bR, lvB); pan_pair(&pb, &bL, &bR);
                    }
                    /* parallel: two buses to the dirt units; serial: one signal (in A's slot) */
                    xL1 = aL; xR1 = aR; xL2 = bL; xR2 = bR;
                    if (P->f_serial) { xL1 = bL; xR1 = bR; xL2 = bL; xR2 = bR; }
                    /* ---- dirt section ---- */
                    if (P->d_serial) {
                        int32_t l = P->f_serial ? xL1 : xL1 + xL2, r = P->f_serial ? xR1 : xR1 + xR2;
                        if (P->da.type) { dirt_pair(P->da.type, drA, P->da.bits, &l, &r); l = mulq(l, ldA); r = mulq(r, ldA); pan_pair(&pda, &l, &r); }
                        if (P->db.type) { dirt_pair(P->db.type, drB, P->db.bits, &l, &r); l = mulq(l, ldB); r = mulq(r, ldB); pan_pair(&pdb, &l, &r); }
                        oL = l; oR = r;
                    } else {
                        int32_t l1 = xL1, r1 = xR1, l2 = P->f_serial ? 0 : xL2, r2 = P->f_serial ? 0 : xR2;
                        if (P->f_serial) { l2 = xL1; r2 = xR1; l1 = xL1; r1 = xR1; }
                        if (P->da.type) { dirt_pair(P->da.type, drA, P->da.bits, &l1, &r1); l1 = mulq(l1, ldA); r1 = mulq(r1, ldA); pan_pair(&pda, &l1, &r1); }
                        if (P->db.type) { dirt_pair(P->db.type, drB, P->db.bits, &l2, &r2); l2 = mulq(l2, ldB); r2 = mulq(r2, ldB); pan_pair(&pdb, &l2, &r2); }
                        oL = l1 + l2; oR = r1 + r2;
                        if (P->f_serial && P->da.type && P->db.type) { oL >>= 1; oR >>= 1; }
                    }
                    /* ---- amp envelope (per-sample, exponential segments) and mid/side ---- */
                    if (v->stage == 1) { v->env += (int32_t)((double)(FS_Q23 - v->env) * atk * 1.0) + 1; if (v->env >= FS_Q23 * 0.99) { v->env = FS_Q23; v->stage = 2; } }
                    else if (v->stage == 2) { v->env += (int32_t)((double)(sus - v->env) * dec); }
                    else { v->env -= (int32_t)((double)v->env * rel) + 1; if (v->env < 64) { v->env = 0; v->active = 0; } }
                    { int64_t e = v->env; oL = (int32_t)((int64_t)oL * e >> 23); oR = (int32_t)((int64_t)oR * e >> 23); }
                    ms_pair(&ms, &oL, &oR);
                    oL = mulq(oL, VGQ); oR = mulq(oR, VGQ);
                    bufL[i] = oL; bufR[i] = oR;
                }
            }
        }
        for (s = 0; s < NSRC; s++) for (k = 0; k < 2; k++) { v->gprev[s][k][0] = tg[s][k][0]; v->gprev[s][k][1] = tg[s][k][1]; }
    }
    for (i = 0; i < BLK; i++) { outL[i] += bufL[i]; outR[i] += bufR[i]; }
    return v->active;
}

static void vparams_init(vparams *P)
{
    memset(P, 0, sizeof *P);
    P->o[0].model = M_SAW; P->o[0].level = 0.5; P->o[0].p1 = 0.5; P->o[1].model = M_SAW; P->o[1].level = 0.0; P->o[1].p1 = 0.5;
    P->o[0].p2 = 3; P->o[0].p3 = 0.5; P->o[1].p2 = 3; P->o[1].p3 = 0.5;
    P->fa.on = 0; P->fa.cutoff = 20000.0; P->fa.q = 0.7071; P->fa.level = 1.0; P->fb.kind = FB_OFF; P->fb.cutoff = 20000.0; P->fb.q = 0.7071; P->fb.level = 1.0;
    P->da.level = 1.0; P->da.drive = 1.0; P->da.bits = 24; P->db.level = 1.0; P->db.drive = 1.0; P->db.bits = 24;
    P->env.a_ms = 1.0; P->env.d_ms = 100.0; P->env.s = 1.0; P->env.r_ms = 50.0; P->mid = 1.0; P->side = 1.0;
}
#endif
