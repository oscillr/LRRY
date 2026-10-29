/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3a: the stereo voice as firmware code. A port of LRRY/host/lrry_voice.h, lrry_filt2.h and lrry_dsp.h (design: DUELL/release_drafts/LRRY_STEREO_VOICE_DESIGN.md; results:
 * LRRY/RESULTS_STEP1..3.md) into the firmware's style: integers only (no libm, no run-time double: coefficient maths through lrry_tab.h), every product through lrry_mac.h, no divide in the
 * sample loop, one control block of CTL samples (the host rendered 128 at a time: the per-block ramps are now CTL long).
 *
 * What this file is: the KERNEL. It knows lrry_pp_t (the part's values for the block, decoded from track_t.p[] by lrry_param.c), lrry_vc_t (what one voice adds: its pitch and cutoffs) and
 * lrry_vs_t (one voice's state). It reads nothing else of the firmware but dsp.c's amp ramp (vmod_t amp0 / amp1, DUELL's envelope and velocity and LFO -> AMP, exactly where the old engine took it).
 *
 * NO SHARED PER-VOICE GLOBALS (4.3b, the split by voice 0-3 / 4-7 on two cores): a voice's state is its own lrry_vs_t (an array indexed by part and voice, written only while that voice renders),
 * its scratch is on the stack, the part's lrry_pp_t is READ-ONLY while the voices render, and the tables are const. The COMB's lines are per voice slices of the part's engine memory (below).
 *
 * Signal path of one voice (the Teensy architecture of the design): sources OSC 1, OSC 2, NOISE, RING (each with its own pan and A/B balance; SUPER is a stereo source) -> bus A / bus B
 * (+ the SUB LAYER, mono centre, into bus A) -> filter A (SVF 12 / 24 dB, LP BP HP NOTCH) and filter B (SVF, LADDER, COMB), each with its own pan, series or parallel -> dirt A and dirt B
 * (SOFT HARD FOLD FUZZ CRUSH), each with drive, pan and level, series or parallel -> the amp ramp (DUELL's) -> MID / SIDE -> the voice gain (24000 / 32768, the host's level plan item 2).
 * Differences from the host voice, all deliberate: (1) the envelope is DUELL's (the host had its own); (2) the filters' RES and the levels are the table steps of the parameters (lrry_param.c);
 * (3) the SYNC correction of the slave's reset (host SYNC_CORR, off by default there) is not ported; (4) every SUPER oscillator has its own phases (the host shared one set of seven between the
 * two oscillators: a bug when both are SUPER, found while porting; the host file is untouched); (5) the filters' and dirt units' LEVEL is fixed at unity for filter A and B (no slot, ext.h);
 * (6) the COMB's lines are 16-bit (Q14) in the part's engine memory and 402 samples long (lowest note 110 Hz, lower notes sound an octave up), the host's were 32-bit and 2048 long. */
#ifndef LRRY_VOICE_C
#define LRRY_VOICE_C
#include "lrry_tab.h"

enum { LM_SAW, LM_TRI, LM_SINE, LM_PULSE, LM_SYNC, LM_SUPER, LM_FM, LM_FOLD, LM_N };   /* the host's M_*: the numbers are stored (X_O1MODEL) */
enum { LS_O1, LS_O2, LS_NOISE, LS_RING, LS_N };
enum { LF_LP, LF_BP, LF_HP, LF_NOTCH };
enum { LB_OFF, LB_SVF, LB_LADDER, LB_COMB };
enum { LD_OFF, LD_SOFT, LD_HARD, LD_FOLD, LD_FUZZ, LD_CRUSH };
#define LRRY_SUPMAX 7

/* the COMB's line memory: per voice and channel a slice of the part's engine memory (engines.c ENG_MEM, the pool: free now that no other engine runs), Q14 samples */
#ifndef LRRY_COMB_LEN
#define LRRY_COMB_LEN ((uint32_t)(ENG_MEM_PART / (2u * 2u * NVOICE)))   /* 402 */
#endif
#define LRRY_COMB_SHIFT 9               /* a line holds the Q23 sample >> 9: +-2 FS fits 16 bits (-84 dB steps; the owner may want the 32-bit lines: RAM) */
#define LRRY_COMB_LINE(pp, vi, ch) (((int16_t *)ENG_MEM(pp)) + (((uint32_t)(vi) * 2u + (uint32_t)(ch)) * LRRY_COMB_LEN))

/* ------------------------------------------------------------------------------ gains (the host's level plan, as constants) */
static const int32_t LRRY_MGQ[8] = {Q28C(1.00), Q28C(0.97), Q28C(0.79), Q28C(0.57), Q28C(1.01), Q28C(1.0), Q28C(1.10), Q28C(1.27)};   /* SAW TRI SINE PULSE SYNC SUPER FM FOLD */
#define LRRY_NGQ Q28C(0.97)             /* noise */
#define LRRY_SGQ Q28C(0.79)             /* SUB (a sine) */
#define LRRY_RGQ Q28C(1.33)             /* ring modulation */
#define LRRY_VGQ Q28C(0.7324)           /* 24000 / 32768: DUELL's voice scale */
#define LRRY_ONE Q28C(1.0)

/* ------------------------------------------------------------------------------ types */
typedef struct { int32_t ll, lr, rl, rr; int32_t identity; } lrry_pan_t;     /* L' = ll L + lr R ; R' = rl L + rr R, Q28 */
typedef struct { int32_t km, ks; int32_t identity; } lrry_ms_t;              /* mid - 1, side - 1, Q28 */
typedef struct { int32_t a1, a2, a3, k; } lrry_svfc_t;
typedef struct { int32_t G, k; } lrry_ladc_t;
typedef struct { int32_t ic1, ic2; } lrry_svfs_t;

/* the part's values for one block (lrry_param.c fills it; read only while the voices render). Q formats in the comments */
typedef struct {
    uint8_t model[2];
    int8_t coarse[2], fine[2];                          /* semitones, cents */
    int32_t tg[LS_N][2][2];                             /* target gain of the mono sources [source][bus A/B][L/R]: pan gain x level x balance, Q28 */
    int32_t lvq[2][2];                                  /* the SUPER oscillator's: level x balance [osc][bus], Q28 (its pan is inside the saws) */
    int32_t tg_off[LS_N][2][2];                         /* the same with no filter running (the host's rule: bus A only, balance 1): used while neither filter runs for the voice */
    int32_t lvq_off[2][2];
    uint32_t pw[2];                                     /* PULSE width, Q32 */
    uint32_t ratio[2];                                  /* SYNC / FM ratio, Q16 */
    int64_t fmidx[2];                                   /* FM index in phase units a radian (2^32 / 2 pi) */
    int32_t fold[2];                                    /* FOLD amount, Q24 */
    uint8_t sup_n[2];
    uint32_t sup_mul[2][LRRY_SUPMAX];                   /* the detune factor of saw k, Q28 */
    int32_t sup_pl[2][LRRY_SUPMAX], sup_pr[2][LRRY_SUPMAX];   /* its pan gains, Q28 */
    int32_t sub_g;                                      /* SUB LAYER level, Q28 (0: not run) */
    uint8_t sub_shift;                                  /* the octaves below the note: 0, 1, 2 */
    uint8_t fa_mode, fa_p24, fa_always;                 /* filter A: LP BP HP NOTCH, 24 dB, 1 = runs whatever the cutoff (lrry_param.c: not LP at the open end) */
    int32_t fa_res;                                     /* RES 0..127 */
    uint8_t fb_kind, fb_mode;                           /* LB_*, and F_* of the SVF */
    int32_t fb_res;                                     /* RES 0..127 (SVF, LADDER) */
    int32_t fb_comb_fb, comb_damp;                      /* COMB feedback (Q28, +-0.95) and damping (Q28) */
    uint8_t f_serial, d_serial;
    lrry_pan_t pa, pb, pd[2];
    uint8_t d_type[2], d_bits[2];
    int32_t d_drive[2];                                 /* Q24 gain */
    int32_t d_lvl[2];                                   /* Q28 */
    lrry_ms_t ms;
    uint8_t any_dirt;
} lrry_pp_t;

/* what one voice brings to the block */
typedef struct {
    uint32_t inc[2], incs;                              /* the oscillators' and the sub's phase increments (Q32 of a cycle a sample) */
    int32_t pos_a, pos_b;                               /* the cutoffs as table positions (lt_g), -1: the filter does not run */
    int32_t comb_d;                                     /* the COMB's delay in samples, 0 when B is not a COMB */
    uint32_t pp, vi;                                    /* the part and the voice (the COMB's lines) */
} lrry_vc_t;

/* one voice's state */
typedef struct {
    uint32_t ph[2], ph2[2], sub_ph, sup_ph[2][LRRY_SUPMAX], nst;
    lrry_svfs_t fa1[2], fa2[2], fb[2];                  /* [channel] */
    int32_t lad[2][4], lad_y[2];
    int32_t comb_lp[2];
    uint32_t comb_w;
    uint8_t comb_on;
    int32_t gprev[LS_N][2][2];                          /* last block's source gains: the per-sample ramp (no zipper) */
} lrry_vs_t;

/* ------------------------------------------------------------------------------ the kernels */
static inline void lrry_pan_set(lrry_pan_t *p, int32_t pan_q15)
{
    int32_t gl, gr;
    lt_pan_lr(pan_q15, &gl, &gr);
    p->identity = pan_q15 == 0;
    if (pan_q15 <= 0) {
        p->ll = LRRY_ONE; p->lr = gl - LRRY_ONE; p->rl = 0; p->rr = gr;
    } else {
        p->ll = gl; p->lr = 0; p->rl = gr - LRRY_ONE; p->rr = LRRY_ONE;
    }
}
static inline void lrry_pan_pair(const lrry_pan_t *p, int32_t *l, int32_t *r)
{
    int32_t a, b;
    if (p->identity)
        return;
    a = MAC2_Q28(*l, p->ll, *r, p->lr);
    b = MAC2_Q28(*l, p->rl, *r, p->rr);
    *l = a;
    *r = b;
}
static inline void lrry_ms_pair(const lrry_ms_t *m, int32_t *l, int32_t *r)
{
    int32_t M, S, dm, ds;
    if (m->identity)
        return;
    M = (int32_t)(((int64_t)*l + *r) >> 1);
    S = (int32_t)(((int64_t)*l - *r) >> 1);
    dm = MUL_Q28(M, m->km);
    ds = MUL_Q28(S, m->ks);
    *l += dm + ds;
    *r += dm - ds;
}

/* SVF coefficients from the cutoff position and the damping (Q28): the host's svf_coef with the table g and the Newton reciprocal */
static inline void lrry_svf_coef(lrry_svfc_t *c, int32_t pos_q8, int32_t k)
{
    uint32_t g = lt_g(pos_q8), den;
    int32_t a1;
    den = (1u << 28) + (uint32_t)(((uint64_t)g * (uint64_t)(g + (uint32_t)k)) >> 28);
    a1 = (int32_t)lt_recip28(den);
    c->a1 = a1;
    c->a2 = MUL_Q28((int32_t)g, a1);
    c->a3 = MUL_Q28((int32_t)g, c->a2);
    c->k = k;
}
static inline int32_t lrry_svf_tick(const lrry_svfc_t *c, lrry_svfs_t *s, int32_t in, int mode)
{
    int32_t v3 = in - s->ic2, v1, v2, out;
    v1 = MAC2_Q28(c->a1, s->ic1, c->a2, v3);
    v2 = s->ic2 + MAC2_Q28(c->a2, s->ic1, c->a3, v3);
    s->ic1 = lt_clamp32(2 * (int64_t)v1 - s->ic1, -16 * LRRY_FS, 16 * LRRY_FS);
    s->ic2 = lt_clamp32(2 * (int64_t)v2 - s->ic2, -16 * LRRY_FS, 16 * LRRY_FS);
    switch (mode) {
    case LF_BP: out = v1; break;
    case LF_HP: out = in - MUL_Q28(v1, c->k) - v2; break;
    case LF_NOTCH: out = in - MUL_Q28(v1, c->k); break;
    default: out = v2;
    }
    return out;
}
static inline void lrry_lad_coef(lrry_ladc_t *c, int32_t pos_q8, int32_t res)
{
    uint32_t g = lt_g(pos_q8);
    c->G = (int32_t)(((uint64_t)g * lt_recip28((1u << 28) + g)) >> 28);
    c->k = res * 8200704;                               /* 4 x 0.97 x res / 127, Q28: 4 x 0.97 x 2^28 / 127 = 8200704.1 */
}
static inline int32_t lrry_lad_tick(const lrry_ladc_t *c, int32_t *st, int32_t *y4, int32_t in)
{
    int32_t u = lt_softclip(in - MUL_Q28(*y4, c->k)), x = u, y = 0;
    int i;
    for (i = 0; i < 4; i++) {
        int32_t v = MUL_Q28(x - st[i], c->G);
        y = st[i] + v;
        st[i] = lt_clamp32((int64_t)y + v, -16 * LRRY_FS, 16 * LRRY_FS);
        x = y;
    }
    *y4 = y;
    return lt_softclip(y);
}
/* COMB: a feedback delay of D samples damped by a one-pole in the loop, 16-bit Q14 lines (LRRY_COMB_SHIFT) */
static inline int32_t lrry_comb_tick(const lrry_pp_t *P, int16_t *line, int32_t *lp, uint32_t w, uint32_t D, int32_t in)
{
    uint32_t rd = w >= D ? w - D : w + LRRY_COMB_LEN - D;
    int32_t d = (int32_t)line[rd] * (1 << LRRY_COMB_SHIFT), out;
    *lp += MUL_Q28(d - *lp, P->comb_damp);
    out = lt_clamp32((int64_t)in + MUL_Q28(*lp, P->fb_comb_fb), -2 * LRRY_FS, 2 * LRRY_FS);
    line[w] = (int16_t)lt_clamp32(out >> LRRY_COMB_SHIFT, -32768, 32767);
    return lt_softclip(out);
}

/* dirt: the host's dirt_shape on lt_softclip (D_* as LD_*) */
static inline int32_t lrry_dirt_shape(int ty, int32_t v, int bits)
{
    switch (ty) {
    case LD_SOFT: return lt_softclip(v);
    case LD_HARD: return lt_clamp32(v, -24000 * 256, 24000 * 256);
    case LD_FOLD: {
        int32_t u = (int32_t)(((uint32_t)(v + 16384 * 256)) & 0xFFFFFFu) - 32768 * 256;   /* triangle of period 2^24 */
        u = 16384 * 256 - (u < 0 ? -u : u);
        return u + (u >> 1);
    }
    case LD_FUZZ: return v > 0 ? (v > 20000 * 256 ? 20000 * 256 : v) : lt_softclip(v) >> 1;
    case LD_CRUSH: {
        int32_t step;
        int64_t q;
        if (bits >= 24)
            return v;
        if (bits < 1)
            bits = 1;
        step = 1 << (24 - bits);
        q = v >= 0 ? ((int64_t)v + step / 2) / step : -(((int64_t)-v + step / 2) / step);   /* (CRUSH only: a divide by a power of two) */
        return lt_clamp32(q * step, -LRRY_FS, LRRY_FS);
    }
    default: return v;
    }
}
static inline int32_t lrry_drive_mul(int32_t x, int32_t gq24) { return lt_clamp32(((int64_t)x * gq24) >> 24, -4 * LRRY_FS, 4 * LRRY_FS); }
static inline void lrry_dirt_pair(int ty, int32_t gq24, int bits, int32_t *l, int32_t *r)
{
    *l = lrry_dirt_shape(ty, lrry_drive_mul(*l, gq24), bits);
    *r = lrry_dirt_shape(ty, lrry_drive_mul(*r, gq24), bits);
}

/* ------------------------------------------------------------------------------ oscillators */
static inline int32_t lrry_res(int64_t x)               /* the BLEP residual at x samples (Q24) from the jump, Q24 of a unit jump */
{
    const int64_t lim = (int64_t)(LRRY_BLEP_M / 2) << 24;
    int64_t u;
    uint32_t idx, fr;
    int32_t v;
    if (x >= lim || x <= -lim)
        return 0;
    u = x + lim;
    idx = (uint32_t)(u >> 16);
    fr = (uint32_t)(u & 0xFFFF);
    v = LT_BLEP[idx] + (int32_t)(((int64_t)(LT_BLEP[idx + 1u] - LT_BLEP[idx]) * fr) >> 16);
    return x >= 0 ? v - (1 << 24) : v;
}
/* inv = lt_inv56(inc), once a block: the host's (p << 24) / inc without the divide */
static inline int32_t lrry_blep(uint32_t p, uint32_t inc, uint64_t inv)
{
    uint32_t w = (uint32_t)(LRRY_BLEP_M / 2) * inc;
    if (inc > (1u << 29))
        return 0;
    if (p < w)
        return lrry_res((int64_t)(((uint64_t)p * inv) >> 32));
    if (p > (uint32_t)(0u - w))
        return lrry_res(-(int64_t)(((uint64_t)(0u - p) * inv) >> 32));
    return 0;
}
static inline int32_t lrry_saw(uint32_t p, uint32_t inc, uint64_t inv) { return (int32_t)(p + 0x80000000u) / 256 - lrry_blep(p, inc, inv); }
static inline int32_t lrry_pulse(uint32_t p, uint32_t inc, uint64_t inv, uint32_t pw) { return (p < pw ? LRRY_FS - 1 : -LRRY_FS) + lrry_blep(p, inc, inv) - lrry_blep(p - pw, inc, inv); }
static inline int32_t lrry_tri(uint32_t p) { int32_t x = (int32_t)(p + 0x40000000u); x = x < 0 ? -x - 1 : x; return (x >> 7) - LRRY_FS + 1; }

/* a new note (voice.c voice_start; sounding = the voice was playing: a retrigger or a steal keeps the state, as DUELL keeps phases and filters) */
static inline void lrry_state_clear(lrry_vs_t *s, uint32_t note)
{
    uint32_t i;
    uint8_t *b = (uint8_t *)s;
    for (i = 0; i < sizeof *s; i++)
        b[i] = 0;
    s->nst = 0x9E3779B9u * (note + 1u);
}

static inline uint32_t lrry_xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ------------------------------------------------------------------------------ the voice block */
/* Renders n (<= CTL) samples of the voice and ADDS them to outL / outR at the firmware's sample scale (the one conversion: LRRY_TO_FW). m carries DUELL's amp ramp (amp0 -> amp1: envelope,
 * velocity, LFO -> AMP). Returns nothing: the voice's life (its release end) is DUELL's env_tick. */
static __attribute__((noinline)) void lrry_voice(const lrry_pp_t *P, const lrry_vc_t *C, lrry_vs_t *V, const vmod_t *m, int32_t *outL, int32_t *outR, uint32_t n)
{
    int32_t bufL[CTL], bufR[CTL];
    uint32_t i, s, k;
    int32_t tg[LS_N][2][2];
    uint64_t inv[2], sl_inv[2], sup_inv[2][LRRY_SUPMAX];
    uint32_t sup_inc[2][LRRY_SUPMAX], slave_inc[2];
    lrry_svfc_t ca, cb;
    lrry_ladc_t lc;
    int fa_on = C->pos_a >= 0, fb_on = P->fb_kind != LB_OFF && C->pos_b >= 0 && (P->fb_kind != LB_COMB || C->comb_d > 0);
    int16_t *line[2] = {0, 0};
    int32_t lvq[2][2];
    int64_t gacc[LS_N][2][2];                           /* the source gains' ramp: g0 << CTL_LOG2 plus the step each sample (no multiply in the loop) */
    int32_t gstep[LS_N][2][2];
    uint8_t act[LS_N][2], use_osc[2], nz_on, ring_on;
    int any_f;
    if (n > CTL)
        n = CTL;
    any_f = fa_on || P->fb_kind != LB_OFF;              /* (the host's rule: with no filter running every source goes to bus A at its level) */
    for (s = 0; s < LS_N; s++)
        for (k = 0; k < 2; k++) {
            tg[s][k][0] = any_f ? P->tg[s][k][0] : P->tg_off[s][k][0];
            tg[s][k][1] = any_f ? P->tg[s][k][1] : P->tg_off[s][k][1];
        }
    for (s = 0; s < 2; s++)
        for (k = 0; k < 2; k++)
            lvq[s][k] = any_f ? P->lvq[s][k] : P->lvq_off[s][k];
    for (s = 0; s < LS_N; s++)                          /* which sources sound (a gain at 0 now and last block: nothing to mix, and an oscillator nobody hears is not rendered) */
        for (k = 0; k < 2; k++) {
            int32_t g0l = V->gprev[s][k][0], g0r = V->gprev[s][k][1];
            act[s][k] = (s < 2u && P->model[s] == LM_SUPER) ? lvq[s][k] != 0 : (tg[s][k][0] | tg[s][k][1] | g0l | g0r) != 0;
            gacc[s][k][0] = (int64_t)g0l << CTL_LOG2;
            gacc[s][k][1] = (int64_t)g0r << CTL_LOG2;
            gstep[s][k][0] = tg[s][k][0] - g0l;
            gstep[s][k][1] = tg[s][k][1] - g0r;
        }
    ring_on = act[LS_RING][0] | act[LS_RING][1];
    nz_on = act[LS_NOISE][0] | act[LS_NOISE][1];
    use_osc[0] = act[LS_O1][0] | act[LS_O1][1] | ring_on;
    use_osc[1] = act[LS_O2][0] | act[LS_O2][1] | ring_on;
    for (s = 0; s < 2; s++) {
        inv[s] = lt_inv56(C->inc[s] ? C->inc[s] : 1u);
        slave_inc[s] = (uint32_t)(((uint64_t)C->inc[s] * P->ratio[s]) >> 16);
        sl_inv[s] = P->model[s] == LM_SYNC ? lt_inv56(slave_inc[s] ? slave_inc[s] : 1u) : 0;
        if (P->model[s] == LM_SUPER)
            for (k = 0; k < P->sup_n[s]; k++) {
                sup_inc[s][k] = lt_scale_inc(C->inc[s], P->sup_mul[s][k]);
                sup_inv[s][k] = lt_inv56(sup_inc[s][k] ? sup_inc[s][k] : 1u);
            }
    }
    for (s = 0; s < 2; s++)
        if (!use_osc[s]) {                              /* not rendered: the phases move on as if it were */
            V->ph[s] += C->inc[s] * n;
            V->ph2[s] += slave_inc[s] * n;
            if (P->model[s] == LM_SUPER)
                for (k = 0; k < P->sup_n[s]; k++)
                    V->sup_ph[s][k] += sup_inc[s][k] * n;
        }
    if (fa_on)
        lrry_svf_coef(&ca, C->pos_a, lt_k(P->fa_res));
    if (fb_on) {
        if (P->fb_kind == LB_SVF)
            lrry_svf_coef(&cb, C->pos_b, lt_k(P->fb_res));
        else if (P->fb_kind == LB_LADDER)
            lrry_lad_coef(&lc, C->pos_b, P->fb_res);
        else {
            line[0] = LRRY_COMB_LINE(C->pp, C->vi, 0);
            line[1] = LRRY_COMB_LINE(C->pp, C->vi, 1);
            if (!V->comb_on) {                          /* the lines start empty (once, when the voice first needs them) */
                for (i = 0; i < LRRY_COMB_LEN; i++)
                    line[0][i] = line[1][i] = 0;
                V->comb_lp[0] = V->comb_lp[1] = 0;
                V->comb_w = 0;
                V->comb_on = 1;
            }
        }
    }
    for (i = 0; i < n; i++) {
        int32_t bus[2][2] = {{0, 0}, {0, 0}};
        int32_t os[2], ors[2] = {0, 0}, src[LS_N], srcR[LS_N];
        int32_t nzv = nz_on ? (int32_t)lrry_xs(&V->nst) >> 8 : 0;
        int32_t aL, aR, bL, bR, oL, oR, xL1, xR1, xL2, xR2, a;
        for (s = 0; s < 2; s++) {                       /* the oscillators: mono sources, SUPER gives its own pair */
            uint32_t *p = &V->ph[s], inc = C->inc[s];
            int32_t y = 0, yr = 0;
            if (!use_osc[s]) {
                os[s] = ors[s] = 0;
                continue;
            }
            switch (P->model[s]) {
            case LM_SAW: y = lrry_saw(*p, inc, inv[s]); break;
            case LM_TRI: y = lrry_tri(*p); break;
            case LM_SINE: y = lt_sin(*p); break;
            case LM_PULSE: y = lrry_pulse(*p, inc, inv[s], P->pw[s]); break;
            case LM_SYNC: {                             /* master phase *p (before this sample's advance), slave V->ph2[s]; the reset placed in time */
                uint32_t sp = V->ph2[s], sinc = slave_inc[s], pn = *p + inc;
                y = lrry_saw(sp, sinc, sl_inv[s]);
                if (pn < *p)
                    V->ph2[s] = (uint32_t)(((uint64_t)pn * P->ratio[s]) >> 16);   /* the slave restarts, already as far on as the master's step past the wrap */
                else
                    V->ph2[s] = sp + sinc;
                break; }
            case LM_FM: {
                int32_t mod = lt_sin(V->ph2[s]);
                uint32_t ph = *p + (uint32_t)(((int64_t)mod * P->fmidx[s]) >> 23);
                y = lt_sin(ph);
                V->ph2[s] += slave_inc[s];
                break; }
            case LM_FOLD: {
                int32_t t = lrry_tri(*p);
                y = lrry_dirt_shape(LD_FOLD, (int32_t)(((int64_t)t * P->fold[s]) >> 24), 24);
                y = lt_clamp32(y, -LRRY_FS, LRRY_FS);
                break; }
            case LM_SUPER: {
                int64_t l = 0, r = 0;
                for (k = 0; k < P->sup_n[s]; k++) {
                    int32_t w = lrry_saw(V->sup_ph[s][k], sup_inc[s][k], sup_inv[s][k]);
                    V->sup_ph[s][k] += sup_inc[s][k];
                    l += (int64_t)w * P->sup_pl[s][k] >> LRRY_COEF_Q;
                    r += (int64_t)w * P->sup_pr[s][k] >> LRRY_COEF_Q;
                }
                yr = (int32_t)((r * LT_INVSQ[P->sup_n[s]]) >> LRRY_COEF_Q);
                y = (int32_t)((l * LT_INVSQ[P->sup_n[s]]) >> LRRY_COEF_Q);
                break; }
            }
            *p += inc;
            os[s] = MUL_Q28(y, LRRY_MGQ[P->model[s]]);
            ors[s] = P->model[s] == LM_SUPER ? MUL_Q28(yr, LRRY_MGQ[P->model[s]]) : os[s];
        }
        src[0] = os[0]; srcR[0] = ors[0];
        src[1] = os[1]; srcR[1] = ors[1];
        src[2] = MUL_Q28(nzv, LRRY_NGQ); srcR[2] = src[2];
        src[3] = MUL_Q28(MUL_Q23(os[0], os[1]), LRRY_RGQ); srcR[3] = src[3];   /* ring: OSC 1 x OSC 2 */
        for (s = 0; s < LS_N; s++)
            for (k = 0; k < 2; k++) {
                if (!act[s][k])
                    continue;
                if (s < 2u && P->model[s] == LM_SUPER) {   /* a stereo source: its own L and R, level x balance only (the pan is inside the saws) */
                    bus[k][0] += MUL_Q28(src[s], lvq[s][k]);
                    bus[k][1] += MUL_Q28(srcR[s], lvq[s][k]);
                } else {
                    int32_t gl, gr;
                    gacc[s][k][0] += gstep[s][k][0];    /* g0 + the step x (i + 1) / CTL, floored: the host's ramp */
                    gacc[s][k][1] += gstep[s][k][1];
                    gl = (int32_t)(gacc[s][k][0] >> CTL_LOG2);
                    gr = (int32_t)(gacc[s][k][1] >> CTL_LOG2);
                    bus[k][0] += MUL_Q28(src[s], gl);
                    bus[k][1] += MUL_Q28(src[s], gr);
                }
            }
        if (P->sub_g) {                                 /* SUB LAYER: mono, centre, equal into L and R of bus A */
            int32_t sb = MUL_Q28(MUL_Q28(lt_sin(V->sub_ph), P->sub_g), LRRY_SGQ);
            V->sub_ph += C->incs;
            bus[0][0] += sb;
            bus[0][1] += sb;
        }
        aL = bus[0][0]; aR = bus[0][1]; bL = bus[1][0]; bR = bus[1][1];
        if (fa_on) {                                    /* filter A (12 or 24 dB), its pan */
            aL = lrry_svf_tick(&ca, &V->fa1[0], aL, P->fa_mode);
            aR = lrry_svf_tick(&ca, &V->fa1[1], aR, P->fa_mode);
            if (P->fa_p24) {
                aL = lrry_svf_tick(&ca, &V->fa2[0], aL, P->fa_mode);
                aR = lrry_svf_tick(&ca, &V->fa2[1], aR, P->fa_mode);
            }
            lrry_pan_pair(&P->pa, &aL, &aR);
        }
        if (P->f_serial) {
            bL += aL; bR += aR; aL = aR = 0;
        }
        if (fb_on) {                                    /* filter B: SVF, LADDER or COMB, its pan */
            if (P->fb_kind == LB_SVF) {
                bL = lrry_svf_tick(&cb, &V->fb[0], bL, P->fb_mode);
                bR = lrry_svf_tick(&cb, &V->fb[1], bR, P->fb_mode);
            } else if (P->fb_kind == LB_LADDER) {
                bL = lrry_lad_tick(&lc, V->lad[0], &V->lad_y[0], bL);
                bR = lrry_lad_tick(&lc, V->lad[1], &V->lad_y[1], bR);
            } else {
                bL = lrry_comb_tick(P, line[0], &V->comb_lp[0], V->comb_w, (uint32_t)C->comb_d, bL);
                bR = lrry_comb_tick(P, line[1], &V->comb_lp[1], V->comb_w, (uint32_t)C->comb_d, bR);
                V->comb_w = V->comb_w + 1u >= LRRY_COMB_LEN ? 0u : V->comb_w + 1u;
            }
            lrry_pan_pair(&P->pb, &bL, &bR);
        }
        xL1 = aL; xR1 = aR; xL2 = bL; xR2 = bR;
        if (P->f_serial) {
            xL1 = bL; xR1 = bR; xL2 = bL; xR2 = bR;
        }
        if (P->d_serial) {                              /* dirt: one signal through A then B */
            int32_t l = P->f_serial ? xL1 : xL1 + xL2, r = P->f_serial ? xR1 : xR1 + xR2;
            if (P->d_type[0]) {
                lrry_dirt_pair(P->d_type[0], P->d_drive[0], P->d_bits[0], &l, &r);
                l = MUL_Q28(l, P->d_lvl[0]); r = MUL_Q28(r, P->d_lvl[0]);
                lrry_pan_pair(&P->pd[0], &l, &r);
            }
            if (P->d_type[1]) {
                lrry_dirt_pair(P->d_type[1], P->d_drive[1], P->d_bits[1], &l, &r);
                l = MUL_Q28(l, P->d_lvl[1]); r = MUL_Q28(r, P->d_lvl[1]);
                lrry_pan_pair(&P->pd[1], &l, &r);
            }
            oL = l; oR = r;
        } else {                                        /* two dirt units side by side, one per filter branch */
            int32_t l1 = xL1, r1 = xR1, l2 = P->f_serial ? 0 : xL2, r2 = P->f_serial ? 0 : xR2;
            if (P->f_serial) {
                l2 = xL1; r2 = xR1; l1 = xL1; r1 = xR1;
            }
            if (P->d_type[0]) {
                lrry_dirt_pair(P->d_type[0], P->d_drive[0], P->d_bits[0], &l1, &r1);
                l1 = MUL_Q28(l1, P->d_lvl[0]); r1 = MUL_Q28(r1, P->d_lvl[0]);
                lrry_pan_pair(&P->pd[0], &l1, &r1);
            }
            if (P->d_type[1]) {
                lrry_dirt_pair(P->d_type[1], P->d_drive[1], P->d_bits[1], &l2, &r2);
                l2 = MUL_Q28(l2, P->d_lvl[1]); r2 = MUL_Q28(r2, P->d_lvl[1]);
                lrry_pan_pair(&P->pd[1], &l2, &r2);
            }
            oL = l1 + l2; oR = r1 + r2;
            if (P->f_serial && P->d_type[0] && P->d_type[1]) {
                oL >>= 1; oR >>= 1;
            }
        }
        a = amp_at(m, i);                               /* DUELL's amp ramp: envelope x velocity x LFO -> AMP, Q15 */
        oL = MUL_Q15(oL, a); oR = MUL_Q15(oR, a);
        lrry_ms_pair(&P->ms, &oL, &oR);
        bufL[i] = MUL_Q28(oL, LRRY_VGQ);
        bufR[i] = MUL_Q28(oR, LRRY_VGQ);
    }
    for (s = 0; s < LS_N; s++)
        for (k = 0; k < 2; k++) {
            V->gprev[s][k][0] = tg[s][k][0];
            V->gprev[s][k][1] = tg[s][k][1];
        }
    for (i = 0; i < n; i++) {                           /* the one conversion to the firmware's scale */
        outL[i] += LRRY_TO_FW(bufL[i]);
        outR[i] += LRRY_TO_FW(bufR[i]);
    }
}
#endif
