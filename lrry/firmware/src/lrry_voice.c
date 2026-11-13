/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3a: the stereo voice as firmware code. A port of LRRY/host/lrry_voice.h, lrry_filt2.h and lrry_dsp.h (design: LRRY/release_drafts/LRRY_STEREO_VOICE_DESIGN.md; results:
 * LRRY/RESULTS_STEP1..3.md) into the firmware's style: integers only (no libm, no run-time double: coefficient maths through lrry_tab.h), every product through lrry_mac.h, no divide in the
 * sample loop, one control block of CTL samples (the host rendered 128 at a time: the per-block ramps are now CTL long).
 *
 * What this file is: the KERNEL. It knows lrry_pp_t (the part's values for the block, decoded from track_t.p[] by lrry_param.c), lrry_vc_t (what one voice adds: its pitch and cutoffs) and
 * lrry_vs_t (one voice's state). It reads nothing else of the firmware but dsp.c's amp ramp (vmod_t amp0 / amp1, L/RRY's envelope and velocity and LFO -> AMP, exactly where the old engine took it).
 *
 * NO SHARED PER-VOICE GLOBALS (4.3b, the split by voice 0-3 / 4-7 on two cores): a voice's state is its own lrry_vs_t (an array indexed by part and voice, written only while that voice renders),
 * its scratch is on the stack, the part's lrry_pp_t is READ-ONLY while the voices render, and the tables are const. The COMB's lines are per voice slices of the part's engine memory (below).
 *
 * Signal path of one voice (the Teensy architecture of the design): sources OSC 1, OSC 2, NOISE, RING (each with its own pan and A/B balance; SUPER is a stereo source) -> bus A / bus B
 * (+ the SUB LAYER, mono, with SUB PAN, into bus A) -> filter A (SVF 12 / 24 dB, L/RRY's MODE blend LP .. BP .. HP) and filter B (SVF, LADDER, COMB), each with its own pan, series or parallel -> dirt A and dirt B
 * (SOFT HARD FOLD FUZZ CRUSH), each with drive, pan and level, series or parallel -> the amp ramp (L/RRY's) -> the voice gain (24000 / 32768, the host's level plan item 2).
 * PANS (phase 50, owner): a MONO source (SAW TRI SINE PULSE SYNC FM FOLD, NOISE, RING, SUB, SUPER with SPREAD 0 or one saw) has the constant power law (lt_pan_lr: unity at the centre, 1.414 on the
 * panned side). A STEREO PAIR (SUPER with SPREAD and more than one saw; filter A, filter B, dirt A, dirt B: their L and R have their own states) is BALANCED (lrry_pan_set): the centre is exact identity,
 * pan right attenuates the LEFT only and pan left the RIGHT only, the near channel is never touched, the far gain is the mono law's far side, 0 at +-63. The output MID / SIDE stage is removed.
 * Differences from the host voice, all deliberate: (0) 32-bit (4.3c): every product is a 32-bit multiply-high behind lrry_mac.h's one macro, the filters are the host's variant 4 (Q31 coefficients, Q26 states), the 64-bit filter path is gone; (1) the envelope is L/RRY's (the host had its own); (2) the filters' RES and the levels are the table steps of the parameters (lrry_param.c);
 * (3) the SYNC correction of the slave's reset (host SYNC_CORR, off by default there) is not ported; (4) every SUPER oscillator has its own phases (the host shared one set of seven between the
 * two oscillators: a bug when both are SUPER, found while porting; the host file is untouched); (5) the filters' and dirt units' LEVEL is fixed at unity for filter A and B (no slot, ext.h);
 * (6) the COMB's lines are 16-bit (Q14) in the part's engine memory and 402 samples long (lowest note 110 Hz, lower notes sound an octave up), the host's were 32-bit and 2048 long. */
#ifndef LRRY_VOICE_C
#define LRRY_VOICE_C
#include "lrry_tab.h"

enum { LM_SAW, LM_TRI, LM_SINE, LM_PULSE, LM_SYNC, LM_SUPER, LM_FM, LM_FOLD, LM_N };   /* the host's M_*: the numbers are stored (X_O1MODEL) */
enum { LS_O1, LS_O2, LS_NOISE, LS_RING, LS_N };
enum { LF_LP, LF_BP, LF_HP, LF_NOTCH };   /* the SVF modes of filter B (TYPE LP BP HP NOTCH). Filter A's MODE is not one of them: it is L/RRY's blend 0..127 (lrry_fa_weights) */
enum { LB_OFF, LB_SVF, LB_LADDER, LB_COMB };
enum { LD_OFF, LD_SOFT, LD_HARD, LD_FOLD, LD_FUZZ, LD_CRUSH };
#define LRRY_SUPMAX 7

/* the COMB's line memory: per voice and channel a slice of the part's engine memory (engines.c ENG_MEM, the pool: free now that no other engine runs), Q14 samples */
#ifndef LRRY_COMB_LEN
#define LRRY_COMB_LEN 268u      /* samples a line (6.1 ms, the COMB's lowest pitch ~164 Hz): the length 12 voices had; 2026-11-12, 16 voices: fixed, engines.c ENG_MEM_PART grew to hold it */
#endif
#define LRRY_COMB_SHIFT 9               /* a line holds the Q23 sample >> 9: +-2 FS fits 16 bits (-84 dB steps; the owner may want the 32-bit lines: RAM) */
#define LRRY_COMB_LINE(pp, vi, ch) (((int16_t *)ENG_MEM(pp)) + (((uint32_t)(vi) * 2u + (uint32_t)(ch)) * LRRY_COMB_LEN))

/* ------------------------------------------------------------------------------ gains (the host's level plan, as constants) */
static const int32_t LRRY_MGQ[8] = {Q28C(1.00), Q28C(0.97), Q28C(0.79), Q28C(0.57), Q28C(1.01), Q28C(1.0), Q28C(1.10), Q28C(1.27)};   /* SAW TRI SINE PULSE SYNC SUPER FM FOLD */
#define LRRY_NGQ Q28C(0.97)             /* noise */
#define LRRY_SGQ Q28C(0.79)             /* SUB (a sine; the other waves are the same peak) */
#define LRRY_RGQ Q28C(1.33)             /* ring modulation */
#define LRRY_VGQ Q28C(0.7324)           /* 24000 / 32768: L/RRY's voice scale */
#define LRRY_ONE Q28C(1.0)

/* ------------------------------------------------------------------------------ types */
typedef struct { int32_t g; int32_t side; } lrry_pan_t;                    /* a balance: side 0 identity, +1 L' = g L (pan right), -1 R' = g R (pan left); g Q28 */
typedef struct { int32_t a1, a2, a3, k; } lrry_svfc_t;      /* the SVF: a1 a2 a3 Q31, k (damping) Q30 */
typedef struct { int32_t G, k; } lrry_ladc_t;                /* the LADDER: G = g / (1 + g) Q31, feedback k Q29 (4 = self oscillation) */
typedef struct { int32_t ic1, ic2; } lrry_svfs_t;            /* SVF state, Q26 (the LADDER's four stages and y4, the COMB's one-pole: Q26 too; the signal is Q23) */

/* the part's values for one block (lrry_param.c fills it; read only while the voices render). Q formats in the comments */
#define LRRY_NPAN 9                                    /* the pans that slew: OSC 1, OSC 2, NOISE, RING (LS_*), SUB, MULTI FILTER, TWIN FILTER, DIRT A, DIRT B */
typedef struct {
    uint8_t model[2];
    int8_t coarse[2], fine[2];                          /* semitones, cents */
    int32_t tg[LS_N][2][2];                             /* target gain of the mono sources [source][bus A/B][L/R]: pan gain x level x balance, Q28 */
    int32_t lvq[2][2];                                  /* the SUPER oscillator's: level x balance [osc][bus], Q28 (voice.c's cost reads it) */
    int32_t tg_off[LS_N][2][2];                         /* the same with no filter running (the host's rule: bus A only, balance 1): used while neither filter runs for the voice */
    int32_t lvq_off[2][2];
    int32_t lvp[2][2][2], lvp_off[2][2][2];             /* phase 50: lvq x the SUPER's pan [osc][bus][L/R] (the saws' SPREAD is inside the saws; the pan: the mono law at SPREAD 0 or one saw, else the balance) */
    uint32_t pw[2];                                     /* PULSE width, Q32 */
    uint32_t ratio[2];                                  /* SYNC / FM ratio, Q16 */
    int64_t fmidx[2];                                   /* FM index in phase units a radian (2^32 / 2 pi) */
    int32_t fold[2];                                    /* FOLD amount, Q24 */
    uint8_t sup_n[2];
    uint32_t sup_mul[2][LRRY_SUPMAX];                   /* the detune factor of saw k, Q28 */
    int32_t sup_pl[2][LRRY_SUPMAX], sup_pr[2][LRRY_SUPMAX];   /* its pan gains, Q28 */
    int32_t sub_g;                                      /* SUB LAYER level, Q28 (0: not run) */
    int32_t sub_tg[2];                                  /* SUB PAN: the mono law's gains L / R, Q28; LRRY_ONE both at the centre (then no gain is applied at all) */
    uint8_t nz_type;                                    /* NOISE TYPE 0..2: WHITE PINK BROWN (X_NZTYPE; L/RRY's COLR 0 / 64 / 127 as three choices) */
    uint8_t sub_wave;                                   /* SUB WAVE 0..3: SINE TRIANGLE SQUARE PULSE (L/RRY's four) */
    uint8_t sub_shift;                                  /* the octaves below the note: 0, 1, 2 */
    uint8_t fa_mode, fa_p24, fa_always;                 /* filter A: MODE 0..127 (L/RRY's blend: 0 LP, 64 BP, 127 HP), 24 dB, 1 = runs whatever the cutoff (lrry_param.c: MODE not LP at the open end) */
    int32_t fa_res;                                     /* RES 0..127 */
    uint8_t fb_kind, fb_mode;                           /* LB_*, and F_* of the SVF */
    int32_t fb_res;                                     /* RES 0..127 (SVF, LADDER) */
    int32_t fb_comb_fb, comb_damp;                      /* COMB feedback (Q31, +-0.95) and damping (Q31) */
    uint8_t f_serial, d_serial;
    lrry_pan_t pa, pb, pd[2];
    lrry_pan_t pa0, pb0, pd0[2];                        /* phase 56: the stage pans of the last decoded block: each one ramps from it over the block, sample by sample (lrry_pr_t) */
    uint8_t d_type[2], d_bits[2];
    int32_t d_drive[2];                                 /* Q24 gain */
    int32_t d_lvl[2];                                   /* Q28 */
    int32_t d_drive0[2], d_lvl0[2];                     /* (2026-11-12, "nothing crackles") the block before's drive and level: d_ramp set, the unit ramps from them sample by sample (a matrix / ENV 2 route on DIRT LEVEL or DRIVE stepped once a block: a zipper) */
    uint8_t d_ramp;                                     /* 0: drive and level held still (the same bits as before the ramp); voice.c clears it when the decode is cached */
    uint8_t any_dirt;
    struct { int32_t identity; } ms;                    /* RETIRED (phase 50: the output MID / SIDE stage is removed): always 1; only voice.c's cost (the allocation job's file) still reads it */
    int32_t pan_now[LRRY_NPAN];                         /* pan slew (owner 2026-11-12: pan jumps never click): every pan's Q15 value as played, kept across decodes (lrry_param.c lrry_pan_slew) */
    uint8_t pan_live, pan_moving;                       /* pan_now holds values / one of them has not reached its target yet (voice.c: the decode is not skipped while one moves) */
} lrry_pp_t;

/* what one voice brings to the block */
typedef struct {
    uint32_t inc[2], incs;                              /* the oscillators' and the sub's phase increments (Q32 of a cycle a sample) */
    int32_t pos_a, pos_b;                               /* the cutoffs as table positions (lt_g), -1: the filter does not run */
    int32_t uc;                                         /* filter A's effective cutoff, Q8 dial steps 0 .. UF_TOP (the kill zones of the MODE blend) */
    int32_t comb_d;                                     /* the COMB's delay in samples, 0 when B is not a COMB */
    uint32_t pp, vi;                                    /* the part and the voice (the COMB's lines) */
} lrry_vc_t;

#define LK_NINV (2 + 2 + 2 * LRRY_SUPMAX + 1)           /* inv memo slots: master 0..1, slave 2..3, SUPER saws 4 + 7 s + k, SUB 18 */
/* one voice's state */
typedef struct {
    uint32_t ph[2], ph2[2], sub_ph, sup_ph[2][LRRY_SUPMAX], nst;
    int32_t nz_b[4];                                    /* PINK: the three poles, BROWN: the integrator (L/RRY's eng_noise.c, s[4..7]) */
    lrry_svfs_t fa1[2], fa2[2], fb[2];                  /* [channel] */
    int16_t faw[3];                                     /* filter A's output weights of the last block (Q10: x, band-pass, low-pass), L/RRY's ufw: the ramp starts from them */
    uint8_t fa_t;                                       /* filter A: 0 from rest (no ramp), 1 ran 12 dB, 2 ran 24 dB last block (L/RRY's uft; 0 again while filter A is bypassed) */
    int32_t lad[2][4], lad_y[2];
    int32_t comb_lp[2];
    uint32_t comb_w;
    uint8_t comb_on;
    int32_t gprev[LS_N][2][2];                          /* last block's source gains: the per-sample ramp (no zipper; phase 56: a SUPER oscillator's are its level x balance x pan, lvq, ramped the same way) */
    int32_t sub_gp[2];                                  /* the SUB's last pan gains (its ramp); sub_gp_ok 0: a new note, the first block starts at the target (the sub never faded in) */
    uint8_t sub_gp_ok;
    uint32_t ik[LK_NINV];                               /* PHASE62: a memo of lt_inv56 (the p / inc reciprocals: the master's, the slave's, each SUPER saw's, the SUB's): the argument it was made for and the result. A pure function of its argument, so */
    uint64_t iv[LK_NINV];                               /* a hit is the same bits; most blocks have the same steps as the block before. Last in the struct: tests/lrry_kern_equiv_test.c compares the state before it */
    struct { int32_t kp, kk; lrry_svfc_t c; } mfa, mfb; /* voice-cheaper 2: a memo of the filters' coefficients (a pure function of the cutoff position and the damping: a hit is the same bits). kk 0 = empty (the damping is never 0: lt_k >= 0.05, ladder: res + 1) */
    struct { int32_t kp, kk; lrry_ladc_t c; } mlad;
} lrry_vs_t;

/* ------------------------------------------------------------------------------ the kernels */
/* the balance's far gain: the mono law's far side (lt_pan_lr), exactly 0 at the stored +-63 (LRRY_PAN_Q15 gives +32767 / -32768: the table's last interval does not end at 0 for +32767) */
static inline int32_t lrry_pan_far(int32_t pan_q15)
{
    int32_t gl, gr;
    if (pan_q15 >= 32767 || pan_q15 <= -32767)
        return 0;
    lt_pan_lr(pan_q15, &gl, &gr);
    return pan_q15 > 0 ? gl : gr;
}
/* a MONO source's pan law (lt_pan_lr) with the far side exactly 0 at the stored +-63 too (the table gave about -89 dB at +63, its last interval; the host's law is 0 there) */
static inline void lrry_pan_mono(int32_t pan_q15, int32_t *gl, int32_t *gr)
{
    lt_pan_lr(pan_q15, gl, gr);
    if (pan_q15 >= 32767)
        *gl = 0;
    else if (pan_q15 <= -32767)
        *gr = 0;
}
static inline void lrry_pan_set(lrry_pan_t *p, int32_t pan_q15)
{
    p->side = pan_q15 > 0 ? 1 : pan_q15 < 0 ? -1 : 0;
    p->g = lrry_pan_far(pan_q15);
}
static inline void lrry_pan_pair(const lrry_pan_t *p, int32_t *l, int32_t *r)
{
    if (p->side > 0)
        *l = MUL_Q28(*l, p->g);
    else if (p->side < 0)
        *r = MUL_Q28(*r, p->g);
}
/* L/RRY phase 56 (owner, test2: crackle in the stereo field on the arp): a stage pan (MULTI FILTER, TWIN FILTER, DIRT A, DIRT B) the matrix, a DEST card or CYCLE moves was a gain step once a block
 * (32 samples), a zipper; an envelope route with a fast attack made a step on every note, on the release tails of the notes before it too. Now it ramps from the last block's to this block's over
 * the block, sample by sample, as the sources' gains do (g0 << CTL_LOG2 plus the step each sample). A pan that did not move is applied as before, bit for bit (lrry_pan_pair); while one moves,
 * a side whose gain is 1.0 at both ends is left untouched (exact) */
typedef struct { int64_t acc[2]; int32_t step[2]; uint8_t on[2], moving; } lrry_pr_t;
static inline void lrry_pr_init(lrry_pr_t *r, const lrry_pan_t *p0, const lrry_pan_t *p1)
{
    int32_t a[2], b[2];
    uint32_t k;
    r->moving = p0->side != p1->side || (p1->side && p0->g != p1->g);
    if (!r->moving)
        return;
    a[0] = p0->side > 0 ? p0->g : LRRY_ONE; a[1] = p0->side < 0 ? p0->g : LRRY_ONE;
    b[0] = p1->side > 0 ? p1->g : LRRY_ONE; b[1] = p1->side < 0 ? p1->g : LRRY_ONE;
    for (k = 0; k < 2u; k++) {
        r->on[k] = a[k] != LRRY_ONE || b[k] != LRRY_ONE;
        r->acc[k] = (int64_t)a[k] << CTL_LOG2;
        r->step[k] = b[k] - a[k];
    }
}
static inline void lrry_pr_tick(lrry_pr_t *r, const lrry_pan_t *p1, int32_t *l, int32_t *rr)
{
    if (!r->moving) {
        lrry_pan_pair(p1, l, rr);
        return;
    }
    if (r->on[0]) {
        r->acc[0] += r->step[0];
        *l = MUL_Q28(*l, (int32_t)(r->acc[0] >> CTL_LOG2));
    }
    if (r->on[1]) {
        r->acc[1] += r->step[1];
        *rr = MUL_Q28(*rr, (int32_t)(r->acc[1] >> CTL_LOG2));
    }
}

/* SVF coefficients (the host's variant 4, LRRY/PREC_32_vs_64.md) from the cutoff position and the damping k (Q28, as lt_k gives it): a1 = 1 / (1 + g (g + k)), a2 = g a1, a3 = g a2, all in Q31
 * (1.0 clamps to 2^31 - 1), k in Q30 (2.0 clamps). The coefficient maths runs once a block and is 64-bit; the table g is Q28. The tick below is 32-bit multiply-high with the states in Q26 (3 more
 * fractional bits than the signal: the product's cut-off lands on an 8 times finer step than Q23, the reason this form is as clean as the old 64-bit kernel or cleaner). */
#define LRRY_Q31MAX 2147483647u
static inline uint32_t lrry_q31(uint64_t v) { return v > LRRY_Q31MAX ? LRRY_Q31MAX : (uint32_t)v; }
static inline void lrry_svf_coef(lrry_svfc_t *c, int32_t pos_q8, int32_t k)
{
    uint32_t g = lt_g(pos_q8), den, a1, a2, a3;
    den = (1u << 28) + (uint32_t)(((uint64_t)g * (uint64_t)(g + (uint32_t)k)) >> 28);
    a1 = lrry_q31(lt_recip31(den));
    a2 = lrry_q31(((uint64_t)g * a1) >> 28);
    a3 = lrry_q31(((uint64_t)g * a2) >> 28);
    c->a1 = (int32_t)a1; c->a2 = (int32_t)a2; c->a3 = (int32_t)a3;
    c->k = k >= (1 << 29) ? (int32_t)LRRY_Q31MAX : k * 4;
}
static inline int32_t lrry_svf_tick(const lrry_svfc_t *c, lrry_svfs_t *s, int32_t in, int mode)   /* Q26 x Q31 -> high word -> Q25, x2 -> Q26 */
{
    int32_t x = in * 8, v3 = x - s->ic2, v1, v2, out;
    v1 = (LRRY_MULH(c->a1, s->ic1) + LRRY_MULH(c->a2, v3)) * 2;
    v2 = s->ic2 + (LRRY_MULH(c->a2, s->ic1) + LRRY_MULH(c->a3, v3)) * 2;
    s->ic1 = lt_clamp32(2 * (int64_t)v1 - s->ic1, -8 * 8 * LRRY_FS, 8 * 8 * LRRY_FS);   /* +-8 FS */
    s->ic2 = lt_clamp32(2 * (int64_t)v2 - s->ic2, -8 * 8 * LRRY_FS, 8 * 8 * LRRY_FS);
    switch (mode) {
    case LF_BP: out = v1; break;
    case LF_HP: out = x - LRRY_MULH(v1, c->k) * 4 - v2; break;
    case LF_NOTCH: out = x - LRRY_MULH(v1, c->k) * 4; break;
    default: out = v2;
    }
    return out >> 3;
}
/* FILTER A's MODE, L/RRY's blend (voice.c uf_run, 0.2.9, ported with the same numbers). md 0..127: 0 low-pass, 64 band-pass, 127 high-pass and every blend between. Output = cx x + cb band-pass + cl low-pass (Q10,
 * the SVF's own three outputs: x the input, band-pass = k v1 (unity at the cutoff), low-pass v2; the high-pass is x - band-pass - low-pass, which is cx 1, cb -1, cl -1). The KILL ZONES: within the last 4 dial steps
 * at an end the share that end closes fades to 0 (the low-pass and band-pass shares towards the bottom, the high-pass and band-pass ones towards the top), so a filter fully closed is silent. uc: the effective
 * cutoff, Q8 dial steps. Worked out once a block and ramped over it from the last block's (lrry_voice) */
#ifndef UF_KZ
#define UF_KZ (4 << 8)
#endif
static inline void lrry_fa_weights(int32_t md, int32_t uc, int32_t *cx, int32_t *cb, int32_t *cl)
{
    int32_t wb = md <= 64 ? md << 4 : ((127 - md) * 65) >> 2, wl = md < 64 ? 1024 - wb : 0, wh = md > 64 ? 1024 - wb : 0;
    int32_t gl = uc < UF_KZ ? uc : UF_KZ, gt = (UF_STEPS << 8) - uc < UF_KZ ? (UF_STEPS << 8) - uc : UF_KZ;
    *cx = (wh * gt) >> 10;
    *cb = ((((wb * gl) >> 10) * gt) >> 10) - *cx;
    *cl = ((wl * gl) >> 10) - *cx;
}
/* one stage of filter A: the SVF's state update as lrry_svf_tick's, the output the weighted sum (Q10 weights; lp: the steady low-pass, its output is the low-pass itself, bit for bit what the filter was before the blend) */
static inline int32_t lrry_fa_tick(const lrry_svfc_t *c, lrry_svfs_t *s, int32_t in, int lp, int32_t wx, int32_t wb, int32_t wl)
{
    int32_t x = in * 8, v3 = x - s->ic2, v1, v2;
    v1 = (LRRY_MULH(c->a1, s->ic1) + LRRY_MULH(c->a2, v3)) * 2;
    v2 = s->ic2 + (LRRY_MULH(c->a2, s->ic1) + LRRY_MULH(c->a3, v3)) * 2;
    s->ic1 = lt_clamp32(2 * (int64_t)v1 - s->ic1, -8 * 8 * LRRY_FS, 8 * 8 * LRRY_FS);
    s->ic2 = lt_clamp32(2 * (int64_t)v2 - s->ic2, -8 * 8 * LRRY_FS, 8 * 8 * LRRY_FS);
    if (lp)
        return v2 >> 3;
    return (int32_t)(((int64_t)wx * x + (int64_t)wb * (LRRY_MULH(v1, c->k) * 4) + (int64_t)wl * v2) >> 10) >> 3;   /* (a 64-bit sum: exact at the weights' pure points, 1024 = 1.0) */
}
static inline void lrry_lad_coef(lrry_ladc_t *c, int32_t pos_q8, int32_t res)
{
    uint32_t g = lt_g(pos_q8);
    c->G = (int32_t)lrry_q31(((uint64_t)g * lt_recip31((1u << 28) + g)) >> 28);   /* g / (1 + g), Q31 */
    c->k = res * 16401408;                              /* 4 x 0.97 x res / 127, Q29: 4 x 0.97 x 2^29 / 127 = 16401408.2 (127 x that is under 2^31) */
}
static inline int32_t lrry_lad_tick(const lrry_ladc_t *c, int32_t *st, int32_t *y4, int32_t in)   /* stages and y4 in Q26 */
{
    int32_t u = lt_softclip((in * 8 - LRRY_MULH(*y4, c->k) * 8) >> 3) * 8, x = u, y = 0;
    int i;
    for (i = 0; i < 4; i++) {
        int32_t v = LRRY_MULH(x - st[i], c->G) * 2;
        y = st[i] + v;
        st[i] = lt_clamp32((int64_t)y + v, -8 * 8 * LRRY_FS, 8 * 8 * LRRY_FS);
        x = y;
    }
    *y4 = y;
    return lt_softclip(y >> 3);
}
/* COMB: a feedback delay of D samples damped by a one-pole in the loop, 16-bit Q14 lines (LRRY_COMB_SHIFT) */
static inline int32_t lrry_comb_tick(const lrry_pp_t *P, int16_t *line, int32_t *lp, uint32_t w, uint32_t D, int32_t in)
{
    uint32_t rd = w >= D ? w - D : w + LRRY_COMB_LEN - D;
    int32_t d = (int32_t)line[rd] * (1 << (LRRY_COMB_SHIFT + 3)), out;      /* the line's sample as Q26 (the loop's one-pole state is Q26: the host's variant 4) */
    *lp += LRRY_MULH(d - *lp, P->comb_damp) * 2;
    out = lt_clamp32((int64_t)in + (LRRY_MULH(*lp, P->fb_comb_fb) >> 2), -2 * LRRY_FS, 2 * LRRY_FS);
    line[w] = (int16_t)lt_clamp32(out >> LRRY_COMB_SHIFT, -32768, 32767);
    return lt_softclip(out);
}

/* voice-cheaper 2: the COMB tick with the damping and the feedback in registers (P-> was read through a pointer every sample) and the read / write cells handed in */
#define LK_COMB_INL static inline __attribute__((always_inline))
LK_COMB_INL int32_t lk_comb_tick(int16_t rdv, int16_t *wr, int32_t *lp, int32_t damp, int32_t fbk, int32_t in)
{
    int32_t d = (int32_t)rdv * (1 << (LRRY_COMB_SHIFT + 3)), out;
    *lp += LRRY_MULH(d - *lp, damp) * 2;
    out = lt_clamp32((int64_t)in + (LRRY_MULH(*lp, fbk) >> 2), -2 * LRRY_FS, 2 * LRRY_FS);
    *wr = (int16_t)lt_clamp32(out >> LRRY_COMB_SHIFT, -32768, 32767);
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
        int32_t q, sh;
        if (bits >= 24)
            return v;
        if (bits < 1)
            bits = 1;
        sh = 24 - bits;                                 /* step = 2^sh: the divide is a shift, in 32 bits (v is under 4 FS here) */
        q = v >= 0 ? (v + ((1 << sh) >> 1)) >> sh : -((-v + ((1 << sh) >> 1)) >> sh);
        return lt_clamp32((int64_t)q * (1 << sh), -LRRY_FS, LRRY_FS);
    }
    default: return v;
    }
}
/* drive: the signal is clamped to +-4 FS first (exact: the gain is 1.0 or more, so a larger input would be clamped by the result clamp anyway) so that MUL_Q24's pre-shift by 6 bits fits 32 bits */
static inline int32_t lrry_drive_in(int32_t x)
{
    return x > 4 * LRRY_FS - 1 ? 4 * LRRY_FS - 1 : x < -4 * LRRY_FS + 1 ? -4 * LRRY_FS + 1 : x;
}
static inline int32_t lrry_drive_mul(int32_t x, int32_t gq24)
{
    return lt_clamp32(MUL_Q24(lrry_drive_in(x), gq24), -4 * LRRY_FS, 4 * LRRY_FS);
}
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
/* the SUB's four waves, L/RRY's chain_sub.c shapes (peak = the sine's FS): TRIANGLE a quarter cycle on (starts at its zero crossing as the sine does); SQUARE low then high; PULSE 1/4 wide, high +FS,
 * low -FS/3 (L/RRY's x 4/3: unsymmetric there too); the edges band-limited by the voice's BLEP (inv = lt_inv56(sub step), once a block) */
static inline int32_t lrry_subw(uint32_t w, uint32_t p, uint32_t inc, uint64_t inv)
{
    if (w == 1u) { int32_t x = (int32_t)(p + 0x40000000u); x = x < 0 ? -x - 1 : x; return (x >> 7) - LRRY_FS + 1; }
    if (w == 2u)
        return lrry_pulse(p + 0x80000000u, inc, inv, 0x80000000u);
    if (w == 3u)
        return (int32_t)(((int64_t)lrry_pulse(p + 0x40000000u, inc, inv, 0x40000000u) * 2) / 3) + LRRY_FS / 3;
    return lt_sin(p);
}
static inline int32_t lrry_tri(uint32_t p) { int32_t x = (int32_t)(p + 0x40000000u); x = x < 0 ? -x - 1 : x; return (x >> 7) - LRRY_FS + 1; }

/* a new note (voice.c voice_start; sounding = the voice was playing: a retrigger or a steal keeps the state, as L/RRY keeps phases and filters) */
static inline void lrry_state_clear(lrry_vs_t *s, uint32_t note)
{
    typedef uint32_t __attribute__((may_alias)) word_t;    /* PHASE62: a word at a time (the byte loop was 21 us of a note-on on the unit) */
    uint32_t i;
    word_t *b = (word_t *)s;
    _Static_assert(sizeof(lrry_vs_t) % 4 == 0, "lrry_state_clear clears words");
    for (i = 0; i < sizeof *s / 4u; i++)
        b[i] = 0;
    s->nst = 0x9E3779B9u * (note + 1u);
}

/* NOISE TYPE PINK / BROWN (L/RRY's eng_noise.c, 32-bit maths only: three leaky poles (Kellet) for pink, one leaky integrator for brown), on the 15-bit white sample, back to the 24-bit scale; PINK x 1.344 so that
 * all three types have the same RMS as WHITE (measured: pink is 0.744, brown 0.992 of white's RMS before it). type 1 PINK, 2 BROWN; WHITE (0) never comes here */
static inline int32_t lrry_nz_color(lrry_vs_t *V, int32_t nzv, uint32_t type)
{
    int32_t w = nzv >> 8, y;
    if (type == 1u) {
        V->nz_b[0] = ((V->nz_b[0] * 32690) >> 15) + ((w * 3245) >> 15);
        V->nz_b[1] = ((V->nz_b[1] * 31555) >> 15) + ((w * 9716) >> 15);
        V->nz_b[2] = ((V->nz_b[2] * 18678) >> 15) + ((w * 17247) >> 14);
        y = (V->nz_b[0] + V->nz_b[1] + V->nz_b[2] + ((w * 6055) >> 15)) >> 2;
        y += (y * 88) >> 8;
    } else {
        V->nz_b[3] = ((V->nz_b[3] * 32580) >> 15) + ((w * 3480) >> 15);
        y = V->nz_b[3];
    }
    return y << 8;
}
static inline uint32_t lrry_xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ------------------------------------------------------------------------------ PHASE62: the kernel as PASSES
 * Measured on the unit (bench vk, PHASE62_VOICE_COST_REPORT.md): the old kernel was ONE loop over the samples that carried every stage of the voice at once (sources, sub, noise, ring, both filters,
 * both dirt units, four pans, amp). With nothing playing that loop still cost 80 us of a 725 us block (2.5 us a sample): all its branches, its 330 stack loads and 190 stack stores (too many live
 * values for the registers) ran every sample whatever was switched on. Now the block is rendered in PASSES, each a small loop over the 32 samples with its own few values in registers; a stage
 * that is off has no pass at all. The arithmetic of every sample is the old one, expression for expression (tests/lrry_kern_equiv_test.c proves it against tests/lrry_voice_ref.h: the same samples
 * and the same state, bit for bit, on random voices): every stage's own state is touched in the same order, and the sums into a bus are integer adds (their order does not matter).
 * LK_INL: always inlined, so that the loop's constants (the oscillator MODEL, the filter type, the dirt type) fold away inside the pass. */
#define LK_INL static inline __attribute__((always_inline))

typedef struct { uint32_t inc, w, w1, wm; uint64_t inv; } lrry_bl_t;   /* an oscillator's BLEP constants for the block: the step, the window (BLEP_M / 2 steps), the p / inc reciprocal;
                                                                              * w1 = w - 1 and wm = 2 w - 1: the window "p < w or p > -w" is the one compare (uint32)(p + w1) < wm (empty: wm = 0) */
LK_INL void lrry_bl_set(lrry_bl_t *b, uint32_t inc, uint64_t inv)
{
    b->inc = inc;
    b->inv = inv;
    if (inc > (1u << 29)) {                             /* (lrry_blep: no edge correction at such a step: a window that is never entered) */
        b->w = 0;
        b->w1 = 0;
        b->wm = 0;
    } else {
        b->w = (uint32_t)(LRRY_BLEP_M / 2) * inc;
        b->w1 = b->w - 1u;
        b->wm = b->w ? 2u * b->w - 1u : 0xFFFFFFFFu;    /* (a step of 0: the old code's second test, p > 0, is the whole window, kept as it was) */
    }
}
LK_INL int32_t lrry_blep_b(uint32_t p, const lrry_bl_t *b)   /* lrry_blep with the block's constants worked out */
{
    if ((uint32_t)(p + b->w1) < b->wm) {
        if (p < b->w)
            return lrry_res((int64_t)(((uint64_t)p * b->inv) >> 32));
        return lrry_res(-(int64_t)(((uint64_t)(0u - p) * b->inv) >> 32));
    }
    return 0;
}
LK_INL int32_t lrry_saw_b(uint32_t p, const lrry_bl_t *b) { return (int32_t)(p + 0x80000000u) / 256 - lrry_blep_b(p, b); }

/* ---- the oscillators: one pass each, writing the oscillator's output (after the model's level) to o[0 .. n-1]. ph: the phase (kept in a register, written back) */
LK_INL void lrry_osc_saw(uint32_t *ph, const lrry_bl_t *b, uint32_t n, int32_t *o)
{
    uint32_t p = *ph, i;
    for (i = 0; i < n; i++) {
        o[i] = MUL_Q28(lrry_saw_b(p, b), LRRY_MGQ[LM_SAW]);
        p += b->inc;
    }
    *ph = p;
}
LK_INL void lrry_osc_tri(uint32_t *ph, uint32_t inc, uint32_t n, int32_t *o)
{
    uint32_t p = *ph, i;
    for (i = 0; i < n; i++) {
        o[i] = MUL_Q28(lrry_tri(p), LRRY_MGQ[LM_TRI]);
        p += inc;
    }
    *ph = p;
}
LK_INL void lrry_osc_sine(uint32_t *ph, uint32_t inc, uint32_t n, int32_t *o)
{
    uint32_t p = *ph, i;
    for (i = 0; i < n; i++) {
        o[i] = MUL_Q28(lt_sin(p), LRRY_MGQ[LM_SINE]);
        p += inc;
    }
    *ph = p;
}
LK_INL void lrry_osc_pulse(uint32_t *ph, const lrry_bl_t *b, uint32_t pw, uint32_t n, int32_t *o)
{
    uint32_t p = *ph, i;
    for (i = 0; i < n; i++) {
        o[i] = MUL_Q28((p < pw ? LRRY_FS - 1 : -LRRY_FS) + lrry_blep_b(p, b) - lrry_blep_b(p - pw, b), LRRY_MGQ[LM_PULSE]);
        p += b->inc;
    }
    *ph = p;
}
LK_INL void lrry_osc_sync(uint32_t *ph, uint32_t *ph2, uint32_t inc, const lrry_bl_t *bs, uint32_t ratio, uint32_t n, int32_t *o)
{
    uint32_t p = *ph, s2 = *ph2, i;
    for (i = 0; i < n; i++) {                           /* master phase p (before this sample's advance), slave s2; the reset placed in time */
        uint32_t sp = s2, pn = p + inc;
        int32_t y = lrry_saw_b(sp, bs);
        if (pn < p)
            s2 = (uint32_t)(((uint64_t)pn * ratio) >> 16);   /* the slave restarts, already as far on as the master's step past the wrap */
        else
            s2 = sp + bs->inc;
        o[i] = MUL_Q28(y, LRRY_MGQ[LM_SYNC]);
        p = pn;
    }
    *ph = p;
    *ph2 = s2;
}
LK_INL void lrry_osc_fm(uint32_t *ph, uint32_t *ph2, uint32_t inc, uint32_t sinc, int64_t fmidx, uint32_t n, int32_t *o)
{
    uint32_t p = *ph, s2 = *ph2, i;
    for (i = 0; i < n; i++) {
        int32_t mod = lt_sin(s2);
        uint32_t pp = p + (uint32_t)(((int64_t)mod * fmidx) >> 23);
        o[i] = MUL_Q28(lt_sin(pp), LRRY_MGQ[LM_FM]);
        s2 += sinc;
        p += inc;
    }
    *ph = p;
    *ph2 = s2;
}
LK_INL void lrry_osc_fold(uint32_t *ph, uint32_t inc, int32_t fold, uint32_t n, int32_t *o)
{
    uint32_t p = *ph, i;
    for (i = 0; i < n; i++) {
        int32_t y = lrry_dirt_shape(LD_FOLD, MUL_Q24(lrry_tri(p), fold), 24);
        y = lt_clamp32(y, -LRRY_FS, LRRY_FS);
        o[i] = MUL_Q28(y, LRRY_MGQ[LM_FOLD]);
        p += inc;
    }
    *ph = p;
}
/* SUPER: the saws one after the other over the whole block (a saw's phase and constants stay in registers); the pair's sums are the old per sample sums (adds: any order) */
LK_INL void lrry_osc_super(const lrry_pp_t *P, lrry_vs_t *V, uint32_t s, const uint32_t *sinc, const uint64_t *sinv, uint32_t n, int32_t *oL, int32_t *oR)
{
    /* voice-cheaper 2: the accumulators hold the SUM OF THE MULTIPLY-HIGH WORDS (no x 4 per product: MUL_Q28 is MULH(a, c x 4) x 4, and a sum of products x 4 is the sum x 4 in the same 32-bit arithmetic), the one
     * x 4 is applied once at the end; the pan gains are pre-multiplied by 4 once a block; the saw is inlined by hand: the window test is a single compare against the block's constants */
    int32_t accL[CTL], accR[CTL];
    uint32_t i, k, nk = P->sup_n[s];
    int32_t invsq = LT_INVSQ[nk];
    if (!nk)
        for (i = 0; i < n; i++)
            accL[i] = accR[i] = 0;
    for (k = 0; k + 1u < nk; k += 2u) {                 /* two saws a loop, one read and one write of the accumulators for both; the first pair writes them (no clearing pass) */
        lrry_bl_t b0, b1;
        uint32_t p0 = V->sup_ph[s][k], p1 = V->sup_ph[s][k + 1u];
        int32_t pl0 = P->sup_pl[s][k] * 4, pr0 = P->sup_pr[s][k] * 4, pl1 = P->sup_pl[s][k + 1u] * 4, pr1 = P->sup_pr[s][k + 1u] * 4;
        lrry_bl_set(&b0, sinc[k], sinv[k]);
        lrry_bl_set(&b1, sinc[k + 1u], sinv[k + 1u]);
        if (k)
            for (i = 0; i < n; i++) {
                int32_t w0 = lrry_saw_b(p0, &b0), w1 = lrry_saw_b(p1, &b1);
                p0 += b0.inc;
                p1 += b1.inc;
                accL[i] += LRRY_MULH(w0, pl0) + LRRY_MULH(w1, pl1);
                accR[i] += LRRY_MULH(w0, pr0) + LRRY_MULH(w1, pr1);
            }
        else
            for (i = 0; i < n; i++) {
                int32_t w0 = lrry_saw_b(p0, &b0), w1 = lrry_saw_b(p1, &b1);
                p0 += b0.inc;
                p1 += b1.inc;
                accL[i] = LRRY_MULH(w0, pl0) + LRRY_MULH(w1, pl1);
                accR[i] = LRRY_MULH(w0, pr0) + LRRY_MULH(w1, pr1);
            }
        V->sup_ph[s][k] = p0;
        V->sup_ph[s][k + 1u] = p1;
    }
    if (k < nk) {
        lrry_bl_t b;
        uint32_t p = V->sup_ph[s][k];
        int32_t pl = P->sup_pl[s][k] * 4, pr = P->sup_pr[s][k] * 4;
        lrry_bl_set(&b, sinc[k], sinv[k]);
        if (k)
            for (i = 0; i < n; i++) {
                int32_t w = lrry_saw_b(p, &b);
                p += b.inc;
                accL[i] += LRRY_MULH(w, pl);
                accR[i] += LRRY_MULH(w, pr);
            }
        else
            for (i = 0; i < n; i++) {
                int32_t w = lrry_saw_b(p, &b);
                p += b.inc;
                accL[i] = LRRY_MULH(w, pl);
                accR[i] = LRRY_MULH(w, pr);
            }
        V->sup_ph[s][k] = p;
    }
    for (i = 0; i < n; i++) {
        oR[i] = MUL_Q28(MUL_Q28(accR[i] * 4, invsq), LRRY_MGQ[LM_SUPER]);
        oL[i] = MUL_Q28(MUL_Q28(accL[i] * 4, invsq), LRRY_MGQ[LM_SUPER]);
    }
}

/* ---- sources into a bus: bus[i] += MUL_Q28(src[i], g), the gain g steady or ramping (the old ramp: g0 << CTL_LOG2 plus the step each sample, floored) */
LK_INL void lrry_bus_add(int32_t *d, const int32_t *src, int64_t acc, int32_t step, uint32_t n, int has)   /* has: the bus already holds a source (else the first source writes it) */
{
    uint32_t i;
    if (!step) {
        int32_t g = (int32_t)(acc >> CTL_LOG2);
        if (has)
            for (i = 0; i < n; i++)
                d[i] += MUL_Q28(src[i], g);
        else
            for (i = 0; i < n; i++)
                d[i] = MUL_Q28(src[i], g);
        return;
    }
    if (has)
        for (i = 0; i < n; i++) {
            acc += step;
            d[i] += MUL_Q28(src[i], (int32_t)(acc >> CTL_LOG2));
        }
    else
        for (i = 0; i < n; i++) {
            acc += step;
            d[i] = MUL_Q28(src[i], (int32_t)(acc >> CTL_LOG2));
        }
}

/* the same source into both sides of a bus with one gain (the pair's two products are the same product) */
LK_INL void lrry_bus_pair(int32_t *dL, int32_t *dR, const int32_t *src, int32_t g0, int32_t step, uint32_t n, int has)
{
    uint32_t i;
    int64_t acc = (int64_t)g0 << CTL_LOG2;
    if (!step) {
        if (has)
            for (i = 0; i < n; i++) {
                int32_t v = MUL_Q28(src[i], g0);
                dL[i] += v;
                dR[i] += v;
            }
        else
            for (i = 0; i < n; i++) {
                int32_t v = MUL_Q28(src[i], g0);
                dL[i] = v;
                dR[i] = v;
            }
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t v;
        acc += step;
        v = MUL_Q28(src[i], (int32_t)(acc >> CTL_LOG2));
        if (has) {
            dL[i] += v;
            dR[i] += v;
        } else {
            dL[i] = v;
            dR[i] = v;
        }
    }
}
/* ---- a stage pan over the block (lrry_pr_tick per sample): not moving: the balance's one far side; moving: the ramps */
LK_INL void lrry_pan_pass(const lrry_pr_t *r, const lrry_pan_t *p1, int32_t *L, int32_t *R, uint32_t n)
{
    uint32_t i, c;
    if (!r->moving) {
        int32_t g = p1->g, *X = p1->side > 0 ? L : R;
        if (p1->side)
            for (i = 0; i < n; i++)
                X[i] = MUL_Q28(X[i], g);
        return;
    }
    for (c = 0; c < 2u; c++)
        if (r->on[c]) {
            int64_t acc = r->acc[c];
            int32_t st = r->step[c], *X = c ? R : L;
            for (i = 0; i < n; i++) {
                acc += st;
                X[i] = MUL_Q28(X[i], (int32_t)(acc >> CTL_LOG2));
            }
        }
}

/* ---- the filters' state update in 32 bits where it cannot overflow. The old ticks clamp a 64-bit 2 v - ic to +-8 FS (Q26): every state is itself such a clamp (or 0), so |ic| <= 2^29, and
 * when |v| < 2^29 the sum stays under 2^31: the same result in plain 32-bit arithmetic (a 64-bit subtract and two 64-bit compares are a dozen instructions on this core). Otherwise the old line. */
#define LK_STB (8 * 8 * LRRY_FS)
LK_INL int32_t lk_stc(int32_t v, int32_t ic)            /* lt_clamp32(2 * (int64_t)v - ic, -LK_STB, LK_STB) */
{
    /* voice-cheaper (a): v is clamped to +-LK_STB first, then everything is 32 bits and one more clamp. Exact because |ic| <= LK_STB (every state is such a clamp, or 0): a v above LK_STB makes
     * 2 v - ic >= LK_STB (result LK_STB), and so does the clamped v; below -LK_STB likewise; inside the range 2 v - ic is under 2^31 in magnitude. (The old line tested the range and kept a 64-bit path.) */
    int32_t w = v < -LK_STB ? -LK_STB : v > LK_STB ? LK_STB : v, t = 2 * w - ic;
    return t < -LK_STB ? -LK_STB : t > LK_STB ? LK_STB : t;
}
LK_INL int32_t lk_fa_tick(const lrry_svfc_t *c, lrry_svfs_t *s, int32_t in, int lp, int32_t wx, int32_t wb, int32_t wl)   /* lrry_fa_tick with lk_stc */
{
    int32_t x = in * 8, v3 = x - s->ic2, v1, v2;
    v1 = (LRRY_MULH(c->a1, s->ic1) + LRRY_MULH(c->a2, v3)) * 2;
    v2 = s->ic2 + (LRRY_MULH(c->a2, s->ic1) + LRRY_MULH(c->a3, v3)) * 2;
    s->ic1 = lk_stc(v1, s->ic1);
    s->ic2 = lk_stc(v2, s->ic2);
    if (lp)
        return v2 >> 3;
    return (int32_t)(((int64_t)wx * x + (int64_t)wb * (LRRY_MULH(v1, c->k) * 4) + (int64_t)wl * v2) >> 10) >> 3;
}
LK_INL int32_t lk_fa_tick_nx(const lrry_svfc_t *c, lrry_svfs_t *s, int32_t in, int32_t wb, int32_t wl)   /* lk_fa_tick with wx = 0: (0 * x + wb bp + wl lp) >> 10 >> 3, the first product is 0 */
{
    int32_t x = in * 8, v3 = x - s->ic2, v1, v2;
    v1 = (LRRY_MULH(c->a1, s->ic1) + LRRY_MULH(c->a2, v3)) * 2;
    v2 = s->ic2 + (LRRY_MULH(c->a2, s->ic1) + LRRY_MULH(c->a3, v3)) * 2;
    s->ic1 = lk_stc(v1, s->ic1);
    s->ic2 = lk_stc(v2, s->ic2);
    return (int32_t)(((int64_t)wb * (LRRY_MULH(v1, c->k) * 4) + (int64_t)wl * v2) >> 10) >> 3;
}
LK_INL int32_t lk_svf_tick(const lrry_svfc_t *c, lrry_svfs_t *s, int32_t in, int mode)   /* lrry_svf_tick with lk_stc */
{
    int32_t x = in * 8, v3 = x - s->ic2, v1, v2, out;
    v1 = (LRRY_MULH(c->a1, s->ic1) + LRRY_MULH(c->a2, v3)) * 2;
    v2 = s->ic2 + (LRRY_MULH(c->a2, s->ic1) + LRRY_MULH(c->a3, v3)) * 2;
    s->ic1 = lk_stc(v1, s->ic1);
    s->ic2 = lk_stc(v2, s->ic2);
    switch (mode) {
    case LF_BP: out = v1; break;
    case LF_HP: out = x - LRRY_MULH(v1, c->k) * 4 - v2; break;
    case LF_NOTCH: out = x - LRRY_MULH(v1, c->k) * 4; break;
    default: out = v2;
    }
    return out >> 3;
}
/* voice-cheaper (a): one stage of the ladder in plain 32-bit maths with ONE clamp. Why that is exact (no 64-bit path, no range test): every state is a clamp to +-2^29 (LK_STB) or 0; the first
 * stage's input x is a softclip times 8 (under 2^26); G < 1, so y = st + v lies between x and st (|y| <= 2^29 + 2) and every later stage's x is the stage before's y. So |x - st| < 2^30 + 4,
 * |v| < 2^30 + 4 and y + v < 1.5 x 2^30: no int32 overflow anywhere, and the old code's two paths (32-bit clamp, 64-bit clamp) both computed lt_clamp32(y + v) of this very number. */
LK_INL int32_t lk_lad_stage(int32_t *st, int32_t x, int32_t G)   /* one stage: returns y, updates *st */
{
    int32_t v = LRRY_MULH(x - *st, G) * 2, y = *st + v, t = y + v;
    *st = t < -LK_STB ? -LK_STB : t > LK_STB ? LK_STB : t;
    return y;
}
LK_INL int32_t lk_lad_tick(const lrry_ladc_t *c, int32_t *st, int32_t *y4, int32_t in)   /* lrry_lad_tick with the 32-bit state update */
{
    int32_t u = lt_softclip((in * 8 - LRRY_MULH(*y4, c->k) * 8) >> 3) * 8, y;
    y = lk_lad_stage(&st[0], u, c->G);
    y = lk_lad_stage(&st[1], y, c->G);
    y = lk_lad_stage(&st[2], y, c->G);
    y = lk_lad_stage(&st[3], y, c->G);
    *y4 = y;
    return lt_softclip(y >> 3);
}
/* ---- filter A: one stage over the block (lrry_fa_tick per sample; lp: the steady low-pass, the weights are not used) */
LK_INL void lrry_fa_pass(const lrry_svfc_t *c, lrry_svfs_t *sL, lrry_svfs_t *sR, int lp, int32_t fax, int32_t fab, int32_t fal, int32_t fdx, int32_t fdb, int32_t fdl, uint32_t n, int32_t *L, int32_t *R)
{
    lrry_svfs_t l = *sL, r = *sR;
    uint32_t i;
    if (lp) {
        for (i = 0; i < n; i++) {
            L[i] = lk_fa_tick(c, &l, L[i], 1, 0, 0, 0);
            R[i] = lk_fa_tick(c, &r, R[i], 1, 0, 0, 0);
        }
    } else if (!(fdx | fdb | fdl)) {                   /* voice-cheaper (a): steady weights (the ramp adds nothing: fax >> 5 is the same every sample), the shifts done once; with no HIGH-PASS share (wx 0: MODE up to 64) */
        int32_t wx = fax >> 5, wb = fab >> 5, wl = fal >> 5;     /* the x term is then exactly 0, so it is left out */
        if (!wx)
            for (i = 0; i < n; i++) {
                L[i] = lk_fa_tick_nx(c, &l, L[i], wb, wl);
                R[i] = lk_fa_tick_nx(c, &r, R[i], wb, wl);
            }
        else
            for (i = 0; i < n; i++) {
                L[i] = lk_fa_tick(c, &l, L[i], 0, wx, wb, wl);
                R[i] = lk_fa_tick(c, &r, R[i], 0, wx, wb, wl);
            }
    } else {
        for (i = 0; i < n; i++) {
            int32_t wx, wb, wl;
            fax += fdx; fab += fdb; fal += fdl;
            wx = fax >> 5; wb = fab >> 5; wl = fal >> 5;
            L[i] = lk_fa_tick(c, &l, L[i], 0, wx, wb, wl);
            R[i] = lk_fa_tick(c, &r, R[i], 0, wx, wb, wl);
        }
    }
    *sL = l;
    *sR = r;
}
/* ---- filter B */
#define LK_FB_SVF_LOOP(MODE) do { \
        lrry_svfs_t l = *sL, r = *sR; \
        uint32_t i; \
        for (i = 0; i < n; i++) { \
            L[i] = lk_svf_tick(c, &l, L[i], MODE); \
            R[i] = lk_svf_tick(c, &r, R[i], MODE); \
        } \
        *sL = l; \
        *sR = r; \
    } while (0)
/* voice-cheaper 2: one loop a TYPE, the mode a literal in each (it was a run-time value: a compare chain in every tick of the HP, BP and NOTCH loops) */
LK_INL void lrry_fb_svf(const lrry_svfc_t *c, lrry_svfs_t *sL, lrry_svfs_t *sR, int mode, uint32_t n, int32_t *L, int32_t *R)
{
    switch (mode) {
    case LF_BP: LK_FB_SVF_LOOP(LF_BP); break;
    case LF_HP: LK_FB_SVF_LOOP(LF_HP); break;
    case LF_NOTCH: LK_FB_SVF_LOOP(LF_NOTCH); break;
    default: LK_FB_SVF_LOOP(LF_LP); break;
    }
}
/* ---- dirt: one unit over the block (drive, shape, level), the type folded into the loop */
LK_INL void lrry_dirt_pass(int ty, int32_t gq24, int bits, int32_t lvl, uint32_t n, int32_t *L, int32_t *R)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t l, r;
        if (ty == LD_SOFT || ty == LD_HARD || ty == LD_FUZZ) {   /* voice-cheaper (a): these three saturate at or below 4 FS themselves (softclip: from 2^25 on; HARD: 24000 * 256; FUZZ: 20000 * 256 / softclip), so the drive's own clamp to +-4 FS after the product is a no-op for them, whatever the product (also a wrapped one): left out */
            l = lrry_dirt_shape(ty, MUL_Q24(lrry_drive_in(L[i]), gq24), bits);
            r = lrry_dirt_shape(ty, MUL_Q24(lrry_drive_in(R[i]), gq24), bits);
        } else {
            l = lrry_dirt_shape(ty, lrry_drive_mul(L[i], gq24), bits);
            r = lrry_dirt_shape(ty, lrry_drive_mul(R[i], gq24), bits);
        }
        L[i] = MUL_Q28(l, lvl);
        R[i] = MUL_Q28(r, lvl);
    }
}
/* the same unit while its drive or level moves (d_ramp): both ramp from the block before's to this block's, sample by sample, as the pans do (CTL samples a block) */
static __attribute__((noinline)) void lrry_dirt_ramp(int ty, int32_t g0, int32_t g1, int bits, int32_t v0, int32_t v1, uint32_t n, int32_t *L, int32_t *R)
{
    int64_t ga = (int64_t)g0 << CTL_LOG2, va = (int64_t)v0 << CTL_LOG2;
    int32_t gs = g1 - g0, vs = v1 - v0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t g, v, l, r;
        ga += gs; va += vs;
        g = (int32_t)(ga >> CTL_LOG2); v = (int32_t)(va >> CTL_LOG2);
        l = lrry_dirt_shape(ty, lrry_drive_mul(L[i], g), bits);
        r = lrry_dirt_shape(ty, lrry_drive_mul(R[i], g), bits);
        L[i] = MUL_Q28(l, v);
        R[i] = MUL_Q28(r, v);
    }
}
static inline void lrry_dirt_unit(int ty, int32_t gq24, int bits, int32_t lvl, uint32_t n, int32_t *L, int32_t *R)
{
    switch (ty) {
    case LD_SOFT: lrry_dirt_pass(LD_SOFT, gq24, bits, lvl, n, L, R); break;
    case LD_HARD: lrry_dirt_pass(LD_HARD, gq24, bits, lvl, n, L, R); break;
    case LD_FOLD: lrry_dirt_pass(LD_FOLD, gq24, bits, lvl, n, L, R); break;
    case LD_FUZZ: lrry_dirt_pass(LD_FUZZ, gq24, bits, lvl, n, L, R); break;
    case LD_CRUSH: lrry_dirt_pass(LD_CRUSH, gq24, bits, lvl, n, L, R); break;
    default: lrry_dirt_pass(0, gq24, bits, lvl, n, L, R); break;
    }
}

LK_INL uint64_t lk_inv(lrry_vs_t *V, uint32_t slot, uint32_t x)   /* lt_inv56(x), x >= 1, from the voice's memo when the step is the block before's */
{
    if (V->ik[slot] != x) {
        V->iv[slot] = lt_inv56(x);
        V->ik[slot] = x;
    }
    return V->iv[slot];
}

/* voice-cheaper (a): the SUB LAYER pass as a function of its own (it sat in the voice's one big function, where its loop competed with every other stage for registers and spilled): mono into L and R of bus A,
 * with SUB PAN (the mono law, ramped as the sources); at the centre no gain at all. The wave is chosen once a block (one loop a wave), not by a switch every sample. Same arithmetic, same order. */
static __attribute__((noinline)) void lrry_sub_pass(const lrry_pp_t *P, lrry_vs_t *V, uint32_t incs, uint64_t sub_inv, int32_t sub_on, int64_t *sacc, const int32_t *sstep, uint32_t n, int32_t *aL, int32_t *aR)
{
    /* voice-cheaper 2: the SUB PAN branch is outside the loops (centre: no gain; moving: the two ramps in registers), the pulse waves use the block's BLEP constants (lrry_bl_set: the window worked out once a block, not twice
     * a sample) and the "x 2 / 3" runs in 32 bits (the pulse is under 2^26 in magnitude: LRRY_FS plus two residuals of at most 2^24 each, so x 2 fits) */
    uint32_t ph = V->sub_ph, i;
    int32_t v, g = P->sub_g;
    int64_t acc0 = sacc[0], acc1 = sacc[1];
    int32_t st0 = sstep[0], st1 = sstep[1];
    lrry_bl_t bb;
    if (P->sub_wave >= 2u)
        lrry_bl_set(&bb, incs, sub_inv);
    else
        bb.inc = bb.w = bb.w1 = bb.wm = 0, bb.inv = 0;
#define LK_SUB_LOOP(WAVE_EXPR) \
    if (sub_on) \
        for (i = 0; i < n; i++) { \
            v = (WAVE_EXPR); \
            v = MUL_Q28(MUL_Q28(v, g), LRRY_SGQ); \
            ph += incs; \
            acc0 += st0; \
            acc1 += st1; \
            aL[i] += MUL_Q28(v, (int32_t)(acc0 >> CTL_LOG2)); \
            aR[i] += MUL_Q28(v, (int32_t)(acc1 >> CTL_LOG2)); \
        } \
    else \
        for (i = 0; i < n; i++) { \
            v = (WAVE_EXPR); \
            v = MUL_Q28(MUL_Q28(v, g), LRRY_SGQ); \
            ph += incs; \
            aL[i] += v; \
            aR[i] += v; \
        }
#define LK_PULSEB(p, pw) (((p) < (pw) ? LRRY_FS - 1 : -LRRY_FS) + lrry_blep_b((p), &bb) - lrry_blep_b((p) - (pw), &bb))
    switch (P->sub_wave) {
    case 1: LK_SUB_LOOP(lrry_tri(ph)) break;
    case 2: LK_SUB_LOOP(LK_PULSEB(ph + 0x80000000u, 0x80000000u)) break;
    case 3: LK_SUB_LOOP((int32_t)(((int32_t)LK_PULSEB(ph + 0x40000000u, 0x40000000u) * 2) / 3) + LRRY_FS / 3) break;
    default: LK_SUB_LOOP(lt_sin(ph))
    }
    sacc[0] = acc0; sacc[1] = acc1;
    V->sub_ph = ph;
}
/* ------------------------------------------------------------------------------ the voice block */
/* Renders n (<= CTL) samples of the voice and ADDS them to outL / outR at the firmware's sample scale (the one conversion: LRRY_TO_FW). m carries L/RRY's amp ramp (amp0 -> amp1: envelope,
 * velocity, LFO -> AMP). Returns nothing: the voice's life (its release end) is L/RRY's env_tick. */
static __attribute__((noinline)) void lrry_voice(const lrry_pp_t *P, const lrry_vc_t *C, lrry_vs_t *V, const vmod_t *m, int32_t *outL, int32_t *outR, uint32_t n)
{
    uint32_t i, s, k;
    uint64_t inv[2], sl_inv[2], sup_inv[2][LRRY_SUPMAX], sub_inv = 0;
    uint32_t sup_inc[2][LRRY_SUPMAX], slave_inc[2];
    lrry_svfc_t ca, cb;
    lrry_ladc_t lc;
    int32_t fcx = 0, fcb = 0, fcl = 1024, fax = 0, fab = 0, fal = 0, fdx = 0, fdb = 0, fdl = 0, ffd = 0, fxg = 32;   /* filter A's MODE weights and their ramp, the slope fade */
    int flp = 1, frun2 = 0;
    int fa_on = C->pos_a >= 0, fb_on = P->fb_kind != LB_OFF && C->pos_b >= 0 && (P->fb_kind != LB_COMB || C->comb_d > 0);
    int16_t *line[2] = {0, 0};
    uint8_t act[LS_N][2], use_osc[2], nz_on, ring_on;
    int64_t sacc[2];                                    /* SUB PAN's ramp, as gacc */
    lrry_pr_t pra, prb, prd[2];                         /* phase 56: the four stage pans' ramps */
    int32_t sstep[2], sub_on;
    int any_f, a_init = 0, b_init = 0, b_live;
    if (n > CTL)
        n = CTL;
    any_f = fa_on || P->fb_kind != LB_OFF;              /* (the host's rule: with no filter running every source goes to bus A at its level) */
    const int32_t (*tg)[2][2] = any_f ? P->tg : P->tg_off;      /* the block's target gains: with a filter running, or the host's "no filter" rule (bus A only) */
    const int32_t (*lvq)[2][2] = any_f ? P->lvp : P->lvp_off;
    int fresh = !V->sub_gp_ok;                          /* (phase 56: a new note; a SUPER oscillator starts at its gains, as before: no fade in) */
    if (!V->sub_gp_ok) {                                /* a new note: the SUB starts at its target pan (no fade in, as before SUB PAN) */
        V->sub_gp[0] = P->sub_tg[0];
        V->sub_gp[1] = P->sub_tg[1];
        V->sub_gp_ok = 1;
    }
    sub_on = (P->sub_tg[0] ^ LRRY_ONE) | (P->sub_tg[1] ^ LRRY_ONE) | (V->sub_gp[0] ^ LRRY_ONE) | (V->sub_gp[1] ^ LRRY_ONE);
    sacc[0] = (int64_t)V->sub_gp[0] << CTL_LOG2;
    sacc[1] = (int64_t)V->sub_gp[1] << CTL_LOG2;
    sstep[0] = P->sub_tg[0] - V->sub_gp[0];
    sstep[1] = P->sub_tg[1] - V->sub_gp[1];
    lrry_pr_init(&pra, &P->pa0, &P->pa);
    lrry_pr_init(&prb, &P->pb0, &P->pb);
    lrry_pr_init(&prd[0], &P->pd0[0], &P->pd[0]);
    lrry_pr_init(&prd[1], &P->pd0[1], &P->pd[1]);
    {                                                   /* which sources sound (a gain at 0 now and last block: nothing to mix, and an oscillator nobody hears is not rendered); j = source x 2 + bus, the arrays flat [s][k][c] */
        const int32_t *tgf = &tg[0][0][0], *lvf = &lvq[0][0][0], *gpf = &V->gprev[0][0][0];
        for (k = 0; k < 2u * LS_N; k++) {
            int32_t t0 = tgf[2 * k], t1 = tgf[2 * k + 1], g0l = gpf[2 * k], g0r = gpf[2 * k + 1];
            if (k < 4u && P->model[k >> 1] == LM_SUPER) {   /* (phase 56: SUPER's gains ramp too, from its last block's; they stepped once a block) */
                t0 = lvf[2 * k]; t1 = lvf[2 * k + 1];
                if (fresh) { g0l = t0; g0r = t1; }
            }
            act[k >> 1][k & 1u] = (t0 | t1 | g0l | g0r) != 0;
        }
    }
    b_live = fb_on || P->f_serial;                      /* bus B can hold something: a source on it, filter B running, or SERIAL (bus A goes through B) */
    for (s = 0; s < LS_N; s++)
        b_live |= act[s][1];
    ring_on = act[LS_RING][0] | act[LS_RING][1];
    nz_on = act[LS_NOISE][0] | act[LS_NOISE][1];
    use_osc[0] = act[LS_O1][0] | act[LS_O1][1] | ring_on;
    use_osc[1] = act[LS_O2][0] | act[LS_O2][1] | ring_on;
    if (P->sub_g && P->sub_wave >= 2u)
        sub_inv = lk_inv(V, 18, C->incs ? C->incs : 1u);
    for (s = 0; s < 2; s++) {
        uint32_t mo = P->model[s];
        inv[s] = use_osc[s] && (mo == LM_SAW || mo == LM_PULSE) ? lk_inv(V, s, C->inc[s] ? C->inc[s] : 1u) : 0;   /* (only the models that band-limit an edge of their own read it) */
        slave_inc[s] = (uint32_t)(((uint64_t)C->inc[s] * P->ratio[s]) >> 16);
        sl_inv[s] = use_osc[s] && mo == LM_SYNC ? lk_inv(V, 2 + s, slave_inc[s] ? slave_inc[s] : 1u) : 0;
        if (mo == LM_SUPER)
            for (k = 0; k < P->sup_n[s]; k++) {
                sup_inc[s][k] = lt_scale_inc(C->inc[s], P->sup_mul[s][k]);
                sup_inv[s][k] = use_osc[s] ? lk_inv(V, 4 + LRRY_SUPMAX * s + k, sup_inc[s][k] ? sup_inc[s][k] : 1u) : 0;
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
    if (fa_on) {
        uint32_t s24 = P->fa_p24, t0 = V->fa_t;
        {
            int32_t kk = lt_k(P->fa_res);
            if (V->mfa.kp != C->pos_a || V->mfa.kk != kk) {
                lrry_svf_coef(&V->mfa.c, C->pos_a, kk);
                V->mfa.kp = C->pos_a; V->mfa.kk = kk;
            }
            ca = V->mfa.c;
        }
        lrry_fa_weights(P->fa_mode, C->uc, &fcx, &fcb, &fcl);
        if (!t0) {                                      /* from rest (a new note, or filter A was bypassed): no ramp */
            V->faw[0] = (int16_t)fcx; V->faw[1] = (int16_t)fcb; V->faw[2] = (int16_t)fcl;
        }
        fax = V->faw[0] << 5; fab = V->faw[1] << 5; fal = V->faw[2] << 5;   /* (CTL 32: the weights ramp, Q15, exactly onto this block's) */
        fdx = fcx - V->faw[0]; fdb = fcb - V->faw[1]; fdl = fcl - V->faw[2];
        flp = !(fdx | fdb | fdl | fcx | fcb) && fcl == 1024;   /* a steady low-pass */
        if (s24 && t0 != 2u)                            /* the second stage starts charged with the first's low-pass (L/RRY's: no step when 24 dB is chosen while it sounds) */
            for (k = 0; k < 2; k++) {
                V->fa2[k].ic1 = 0;
                V->fa2[k].ic2 = V->fa1[k].ic2;
            }
        if (t0 && t0 != 1u + s24)                       /* SLOPE turned while sounding: the second stage fades in (out) over the block */
            ffd = s24 ? 1 : -1;
        fxg = ffd > 0 ? 0 : 32;
        frun2 = s24 || ffd;
    } else {
        V->fa_t = 0;
    }
    if (fb_on) {
        if (P->fb_kind == LB_SVF) {
            int32_t kk = lt_k(P->fb_res);
            if (V->mfb.kp != C->pos_b || V->mfb.kk != kk) {
                lrry_svf_coef(&V->mfb.c, C->pos_b, kk);
                V->mfb.kp = C->pos_b; V->mfb.kk = kk;
            }
            cb = V->mfb.c;
        } else if (P->fb_kind == LB_LADDER) {
            if (V->mlad.kp != C->pos_b || V->mlad.kk != P->fb_res + 1) {
                lrry_lad_coef(&V->mlad.c, C->pos_b, P->fb_res);
                V->mlad.kp = C->pos_b; V->mlad.kk = P->fb_res + 1;
            }
            lc = V->mlad.c;
        }
        else {
            line[0] = LRRY_COMB_LINE(C->pp, C->vi, 0);
            line[1] = LRRY_COMB_LINE(C->pp, C->vi, 1);
            if (!V->comb_on) {                          /* the lines start empty (once, when the voice first needs them) */
                typedef uint32_t __attribute__((may_alias)) word_t;    /* PHASE62: two samples a store (a line is LRRY_COMB_LEN x 2 bytes, the pool is 8 byte aligned: with an even length every line is word aligned) */
                if (LRRY_COMB_LEN % 2u == 0u)
                    for (i = 0; i < LRRY_COMB_LEN / 2u; i++)
                        ((word_t *)line[0])[i] = ((word_t *)line[1])[i] = 0;
                else
                    for (i = 0; i < LRRY_COMB_LEN; i++)
                        line[0][i] = line[1][i] = 0;
                V->comb_lp[0] = V->comb_lp[1] = 0;
                V->comb_w = 0;
                V->comb_on = 1;
            }
        }
    }
    /* ---------------------------------------------------------------- the passes (PHASE62). The signal arrays of the block: bus A (aL, aR), bus B (bL, bR), the oscillators and the sources */
    {
    int32_t aL[CTL], aR[CTL], bL[CTL], bR[CTL];
    int32_t os[2][CTL], orr[2][CTL], nzs[CTL], rgs[CTL];
    const int32_t *srcL[LS_N], *srcR[LS_N];             /* the source arrays: a mono source's right is its left */

    /* pass 1: the oscillators and the noise */
    for (s = 0; s < 2; s++) {
        uint32_t inc = C->inc[s];
        lrry_bl_t b, bs;
        if (!use_osc[s])
            continue;
        lrry_bl_set(&b, inc, inv[s]);
        switch (P->model[s]) {
        case LM_SAW: lrry_osc_saw(&V->ph[s], &b, n, os[s]); break;
        case LM_TRI: lrry_osc_tri(&V->ph[s], inc, n, os[s]); break;
        case LM_SINE: lrry_osc_sine(&V->ph[s], inc, n, os[s]); break;
        case LM_PULSE: lrry_osc_pulse(&V->ph[s], &b, P->pw[s], n, os[s]); break;
        case LM_SYNC:
            lrry_bl_set(&bs, slave_inc[s], sl_inv[s]);
            lrry_osc_sync(&V->ph[s], &V->ph2[s], inc, &bs, P->ratio[s], n, os[s]);
            break;
        case LM_FM: lrry_osc_fm(&V->ph[s], &V->ph2[s], inc, slave_inc[s], P->fmidx[s], n, os[s]); break;
        case LM_FOLD: lrry_osc_fold(&V->ph[s], inc, P->fold[s], n, os[s]); break;
        case LM_SUPER: {
            lrry_osc_super(P, V, s, sup_inc[s], sup_inv[s], n, os[s], orr[s]);
            V->ph[s] += inc * n;                        /* (the old loop advanced the main phase here too: SUPER does not read it) */
            break; }
        }
        srcL[s] = os[s];
        srcR[s] = P->model[s] == LM_SUPER ? orr[s] : os[s];
    }
    if (nz_on) {                                        /* voice-cheaper 2: one loop a colour, the state in registers (it was read and written through V on every sample) */
        uint32_t x = V->nst;
        if (P->nz_type == 1u) {                         /* PINK (lrry_nz_color's expressions) */
            int32_t b0 = V->nz_b[0], b1 = V->nz_b[1], b2 = V->nz_b[2];
            for (i = 0; i < n; i++) {
                int32_t w, y;
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                w = ((int32_t)x >> 8) >> 8;
                b0 = ((b0 * 32690) >> 15) + ((w * 3245) >> 15);
                b1 = ((b1 * 31555) >> 15) + ((w * 9716) >> 15);
                b2 = ((b2 * 18678) >> 15) + ((w * 17247) >> 14);
                y = (b0 + b1 + b2 + ((w * 6055) >> 15)) >> 2;
                y += (y * 88) >> 8;
                nzs[i] = MUL_Q28(y << 8, LRRY_NGQ);
            }
            V->nz_b[0] = b0; V->nz_b[1] = b1; V->nz_b[2] = b2;
        } else if (P->nz_type) {                        /* BROWN */
            int32_t b3 = V->nz_b[3];
            for (i = 0; i < n; i++) {
                int32_t w;
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                w = ((int32_t)x >> 8) >> 8;
                b3 = ((b3 * 32580) >> 15) + ((w * 3480) >> 15);
                nzs[i] = MUL_Q28(b3 << 8, LRRY_NGQ);
            }
            V->nz_b[3] = b3;
        } else
            for (i = 0; i < n; i++) {
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                nzs[i] = MUL_Q28((int32_t)x >> 8, LRRY_NGQ);
            }
        V->nst = x;
        srcL[LS_NOISE] = srcR[LS_NOISE] = nzs;
    }
    if (ring_on) {                                      /* RING: OSC 1 x OSC 2 */
        for (i = 0; i < n; i++)
            rgs[i] = MUL_Q28(MUL_Q23(os[0][i], os[1][i]), LRRY_RGQ);
        srcL[LS_RING] = srcR[LS_RING] = rgs;
    }

    /* pass 2: the sources into the two buses (each its own gains, ramped from the last block's), then the SUB LAYER into bus A */
    for (s = 0; s < LS_N; s++)
        for (k = 0; k < 2; k++) {
            int sup = s < 2u && P->model[s] == LM_SUPER;
            uint32_t c;
            if (!act[s][k])
                continue;
            int32_t t[2], g[2];                         /* the targets and the starts of the gain ramps (the block before's, a new SUPER's: its own targets) */
            for (c = 0; c < 2u; c++) {
                t[c] = sup ? lvq[s][k][c] : tg[s][k][c];
                g[c] = sup && fresh ? t[c] : V->gprev[s][k][c];
            }
            if (!sup && t[0] == t[1] && g[0] == g[1])   /* a mono source with the same gain on both sides (a centred one): one product for the pair */
                lrry_bus_pair(k ? bL : aL, k ? bR : aR, srcL[s], g[0], t[0] - g[0], n, k ? b_init : a_init);
            else
                for (c = 0; c < 2u; c++)                /* (SUPER: its own L and R, level x balance x its pan; a mono source: gain L and gain R) */
                    lrry_bus_add(c ? (k ? bR : aR) : (k ? bL : aL), c ? srcR[s] : srcL[s], (int64_t)g[c] << CTL_LOG2, t[c] - g[c], n, k ? b_init : a_init);
            if (k)
                b_init = 1;
            else
                a_init = 1;
        }
    if (!a_init) {                                      /* bus A is read below whatever happens (the filters and the sums run on silence too) */
        for (i = 0; i < n; i++)
            aL[i] = aR[i] = 0;
        a_init = 1;
    }
    if (!b_init && b_live && !P->f_serial) {            /* bus B runs a filter on silence (a serial B gets bus A's output below) */
        for (i = 0; i < n; i++)
            bL[i] = bR[i] = 0;
        b_init = 1;
    }
    if (P->sub_g)
        lrry_sub_pass(P, V, C->incs, sub_inv, sub_on, sacc, sstep, n, aL, aR);

    /* pass 3: filter A (12 or 24 dB, MODE as a blend), its pan */
    if (fa_on) {
        int32_t uL[CTL], uR[CTL];
        for (k = 0; k < 1u + (uint32_t)frun2; k++) {    /* stage 1; stage 2 ("24 dB: the same MODE again") */
            int32_t *xL = aL, *xR = aR;
            if (k && ffd) {                             /* SLOPE turned while sounding: the second stage fades in (out) over the block: it works on a copy */
                for (i = 0; i < n; i++) {
                    uL[i] = aL[i];
                    uR[i] = aR[i];
                }
                xL = uL; xR = uR;
            }
            lrry_fa_pass(&ca, k ? &V->fa2[0] : &V->fa1[0], k ? &V->fa2[1] : &V->fa1[1], flp, fax, fab, fal, fdx, fdb, fdl, n, xL, xR);
        }
        if (frun2 && ffd)
            for (i = 0; i < n; i++) {
                fxg += ffd;
                aL[i] = aL[i] + (((uL[i] - aL[i]) * fxg) >> 5);
                aR[i] = aR[i] + (((uR[i] - aR[i]) * fxg) >> 5);
            }
        lrry_pan_pass(&pra, &P->pa, aL, aR, n);
    }
    if (P->f_serial) {                                  /* serial: A's output goes through B (what is left on A is 0: only a SERIAL filter B and the sums below read it) */
        if (b_init)
            for (i = 0; i < n; i++) {
                bL[i] += aL[i];
                bR[i] += aR[i];
            }
        else
            for (i = 0; i < n; i++) {
                bL[i] = aL[i];
                bR[i] = aR[i];
            }
        b_init = 1;
    }

    /* pass 4: filter B: SVF, LADDER or COMB, its pan */
    if (fb_on) {
        if (P->fb_kind == LB_SVF) {
            lrry_fb_svf(&cb, &V->fb[0], &V->fb[1], P->fb_mode, n, bL, bR);
        } else if (P->fb_kind == LB_LADDER) {
            int32_t l4[4], r4[4], ly0 = V->lad_y[0], ly1 = V->lad_y[1];
            for (k = 0; k < 4u; k++) {
                l4[k] = V->lad[0][k];
                r4[k] = V->lad[1][k];
            }
            for (i = 0; i < n; i++) {
                bL[i] = lk_lad_tick(&lc, l4, &ly0, bL[i]);
                bR[i] = lk_lad_tick(&lc, r4, &ly1, bR[i]);
            }
            for (k = 0; k < 4u; k++) {
                V->lad[0][k] = l4[k];
                V->lad[1][k] = r4[k];
            }
            V->lad_y[0] = ly0;
            V->lad_y[1] = ly1;
        } else {
            uint32_t w = V->comb_w, D = (uint32_t)C->comb_d;
            int32_t lp0 = V->comb_lp[0], lp1 = V->comb_lp[1], damp = P->comb_damp, fbk = P->fb_comb_fb;
            for (i = 0; i < n;) {                       /* voice-cheaper 2: runs without a wrap of the write or the read index (at most three a block): pointers that only step, no wrap test a sample */
                uint32_t rd = w >= D ? w - D : w + LRRY_COMB_LEN - D, run = n - i, t = LRRY_COMB_LEN - w, j;
                int16_t *w0 = line[0] + w, *w1 = line[1] + w, *r0 = line[0] + rd, *r1 = line[1] + rd;
                if (t < run) run = t;
                t = LRRY_COMB_LEN - rd;
                if (t < run) run = t;
                for (j = 0; j < run; j++) {
                    bL[i + j] = lk_comb_tick(r0[j], &w0[j], &lp0, damp, fbk, bL[i + j]);
                    bR[i + j] = lk_comb_tick(r1[j], &w1[j], &lp1, damp, fbk, bR[i + j]);
                }
                i += run;
                w += run;
                if (w >= LRRY_COMB_LEN)
                    w = 0u;
            }
            V->comb_lp[0] = lp0;
            V->comb_lp[1] = lp1;
            V->comb_w = w;
        }
        lrry_pan_pass(&prb, &P->pb, bL, bR, n);
    }

    /* pass 5: the dirt units. SERIAL (dirt): one signal through A then B; else one unit on each filter branch, side by side; the two branches are summed in pass 6. Where the filters are in
     * SERIAL the one signal is bus B's (in the side by side form it feeds both units and is summed twice: the old routing, kept). A bus B that holds nothing (no source, no filter B, not
     * SERIAL) is silence, and a dirt unit on silence gives silence: it is left out */
    {
    int32_t *pL = aL, *pR = aR, *qL = 0, *qR = 0;       /* what pass 6 reads: p, plus q when there is one, halved when both dirt units run on a SERIAL filter */
    int half = 0;
    if (P->d_serial) {
        if (P->f_serial) {
            pL = bL; pR = bR;
        } else if (b_live) {
            for (i = 0; i < n; i++) {
                aL[i] += bL[i];
                aR[i] += bR[i];
            }
        }
    } else if (P->f_serial) {                           /* both units read filter B's output */
        if (P->d_type[0] || P->d_type[1])               /* (one of them changes its copy, the other side must stay raw) */
            for (i = 0; i < n; i++) {
                aL[i] = bL[i];
                aR[i] = bR[i];
            }
        else {
            pL = bL; pR = bR;
        }
    }
    if (!P->d_serial && b_live) {
        qL = bL; qR = bR;
        half = P->f_serial && P->d_type[0] && P->d_type[1];
    }
    for (k = 0; k < 2u; k++)                            /* the dirt units (dirt, level, pan): in SERIAL both on the one signal; else unit 1 on bus B (nothing to do when B is silent) */
        if (P->d_type[k] && (k == 0u || P->d_serial || b_live)) {
            int32_t *uL = k && !P->d_serial ? bL : pL, *uR = k && !P->d_serial ? bR : pR;
            if (P->d_ramp && (P->d_drive0[k] != P->d_drive[k] || P->d_lvl0[k] != P->d_lvl[k]))
                lrry_dirt_ramp(P->d_type[k], P->d_drive0[k], P->d_drive[k], P->d_bits[k], P->d_lvl0[k], P->d_lvl[k], n, uL, uR);
            else
                lrry_dirt_unit(P->d_type[k], P->d_drive[k], P->d_bits[k], P->d_lvl[k], n, uL, uR);
            lrry_pan_pass(&prd[k], &P->pd[k], uL, uR, n);
        }

    /* pass 6: (the sum of the two branches,) the amp ramp (envelope x velocity x LFO -> AMP), the voice gain, and the one conversion to the firmware's scale, added to the part's block.
     * voice-cheaper (b): the amp gain and the voice gain are ONE gain a sample (ag = MULH(a << 16, VGQ x 4): a x 0.7324 in Q14, 1 multiply for both channels) and each channel is ONE multiply
     * (MULH(x, ag) >> 5), 1.5 multiplies a channel-sample against 2 before, 1 against 2 when the amp is steady. The old line cut its product twice (MUL_Q15, then the voice gain): the new one once,
     * so a sample can differ by 1 LSB of the output (tests/lrry_kern_equiv_test.c measures it: the error against the old kernel). ON BY DEFAULT since voice-cheaper 2 (owner's rule "perfect firmware first": 1 LSB at Q15, -107.7 dB, about 0.9 us a voice; tests/lrry_pan_test.c's "every pan at 0" hashes were re-based): -DLRRY_PASS6_LEGACY brings back the old two-product line, bit for bit the reference (the proof of the (a) changes). */
#if !defined(LRRY_PASS6_ONEGAIN) && !defined(LRRY_PASS6_LEGACY)
#define LRRY_PASS6_ONEGAIN 1
#endif
#ifndef LRRY_PASS6_ONEGAIN
#define LK_P6(x, a) (LRRY_MULH(MUL_Q15((x), (a)), LRRY_VGQ * 4) >> 6)
#else
#define LK_P6(x, a) (LRRY_MULH((x), (a)) >> 5)
#define LK_P6G(a) LRRY_MULH((a) * 65536, LRRY_VGQ * 4)
#endif
#ifndef LRRY_PASS6_ONEGAIN
#define LK_AG(a) (a)
#else
#define LK_AG(a) LK_P6G(a)
#endif
    {
        int32_t d = m->amp1 - m->amp0, xr = 0;           /* amp_at(m, i) as a running product: xr = d * i, the same value, no multiply a sample */
#define LK_AMPI (m->amp0 + ((xr + ((xr >> 31) & (CTL - 1))) >> CTL_LOG2))
    if (!qL) {
        if (!d) {                                       /* a steady amp: the same gain every sample (amp_at gives amp0 for each i) */
            int32_t a = LK_AG(m->amp0);
            for (i = 0; i < n; i++) {
                outL[i] += LK_P6(pL[i], a);
                outR[i] += LK_P6(pR[i], a);
            }
        } else
            for (i = 0; i < n; i++) {
                int32_t a = LK_AG(LK_AMPI);
                xr += d;
                outL[i] += LK_P6(pL[i], a);
                outR[i] += LK_P6(pR[i], a);
            }
    } else if (half) {
        for (i = 0; i < n; i++) {
            int32_t a = LK_AG(LK_AMPI);
            xr += d;
            outL[i] += LK_P6((pL[i] + qL[i]) >> 1, a);
            outR[i] += LK_P6((pR[i] + qR[i]) >> 1, a);
        }
    } else {
        for (i = 0; i < n; i++) {
            int32_t a = LK_AG(LK_AMPI);
            xr += d;
            outL[i] += LK_P6(pL[i] + qL[i], a);
            outR[i] += LK_P6(pR[i] + qR[i], a);
        }
    }
    }
    }
    }
    {                                                   /* the gains of this block, the next block's ramps start from them */
        const int32_t *tgf = &tg[0][0][0], *lvf = &lvq[0][0][0];
        int32_t *gpf = &V->gprev[0][0][0];
        for (k = 0; k < 4u * LS_N; k++)
            gpf[k] = tgf[k];
        for (s = 0; s < 2u; s++)
            if (P->model[s] == LM_SUPER)
                for (k = 4u * s; k < 4u * s + 4u; k++)
                    gpf[k] = lvf[k];
    }
    V->sub_gp[0] = P->sub_tg[0];
    V->sub_gp[1] = P->sub_tg[1];
    if (fa_on) {
        V->faw[0] = (int16_t)fcx; V->faw[1] = (int16_t)fcb; V->faw[2] = (int16_t)fcl;
        V->fa_t = (uint8_t)(1u + P->fa_p24);
    }
}
#endif
