/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY stereo voice kernels, step 1 (host first). Design: DUELL/release_drafts/LRRY_STEREO_VOICE_DESIGN.md.
 * Derived from DUELL 1.0 (dsp.c tsvf_*, fx.c ins_shape) and the Teensy synth (dsp.h stereoPanMix); both GPL-3.0, credits in NOTICE at release.
 *
 * FORMAT (the "32 bit" quality choice): a signal is an int32 in Q23, full scale +-8388608 (the DAC's 24 bits), with 8 bits of headroom in the
 * word; every product is made in int64, so nothing is rounded to 16 bits on the way (DUELL's voice scale is ~16 bit amplitude, VOICE_FS 24000).
 * Coefficients: Q28 in int32. Coefficient MATHS (tan, cos, sin, exp) runs once per control tick and here uses libm; the device build replaces
 * it with tables (DUELL SVF_G) once the host tests pass: that is a later step, the kernels below are what the tests prove.
 * Every unit is ONE CHANNEL; a "pair" calls it twice with separate state, so L and R can never leak into each other. */
#ifndef LRRY_DSP_H
#define LRRY_DSP_H
#include <stdint.h>
#include <math.h>

#define FS_Q23 8388608
#define COEF_Q 28
#define SR 44100.0

static inline int32_t clamp32(int64_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : (int32_t)v; }
static inline int32_t mulq(int32_t a, int32_t c) { return (int32_t)(((int64_t)a * c) >> COEF_Q); }   /* signal x Q28 coefficient */
static inline int32_t cq(double x) { return (int32_t)lrint(x * (double)(1 << COEF_Q)); }

/* ---------------------------------------------------------------- pan: width preserving (Teensy stereoPanMix), pan -1..+1 */
typedef struct { int32_t ll, lr, rl, rr; int identity; } pan_t;   /* L' = ll L + lr R ; R' = rl L + rr R */
static inline void pan_set(pan_t *p, double pan)
{
    double a, gl, gr;
    if (pan < -1) pan = -1;
    if (pan > 1) pan = 1;
    a = (pan + 1.0) * M_PI / 4.0;
    gl = M_SQRT2 * cos(a);
    gr = M_SQRT2 * sin(a);
    p->identity = (pan == 0.0);
    if (pan <= 0) { p->ll = cq(1.0); p->lr = cq(gl - 1.0); p->rl = 0; p->rr = cq(gr); }
    else          { p->ll = cq(gl); p->lr = 0; p->rl = cq(gr - 1.0); p->rr = cq(1.0); }
}
/* a mono source panned into a pair (the same law with R = 0 on the way in: L = gl s, R = gr s) */
static inline void pan_mono(double pan, int32_t *gl_q, int32_t *gr_q)
{
    double a = (pan < -1 ? -1 : pan > 1 ? 1 : pan);
    a = (a + 1.0) * M_PI / 4.0;
    *gl_q = cq(M_SQRT2 * cos(a));
    *gr_q = cq(M_SQRT2 * sin(a));
}
static inline void pan_pair(const pan_t *p, int32_t *l, int32_t *r)
{
    int32_t a, b;
    if (p->identity) return;                       /* exact identity at pan 0 */
    a = (int32_t)(((int64_t)*l * p->ll + (int64_t)*r * p->lr) >> COEF_Q);
    b = (int32_t)(((int64_t)*l * p->rl + (int64_t)*r * p->rr) >> COEF_Q);
    *l = a; *r = b;
}

/* ---------------------------------------------------------------- mid/side: L' = L + (mid-1) M + (side-1) S, defaults 1,1 = identity */
typedef struct { int32_t km, ks; int identity; } ms_t;     /* km = mid - 1, ks = side - 1, Q28 */
static inline void ms_set(ms_t *m, double mid, double side) { m->km = cq(mid - 1.0); m->ks = cq(side - 1.0); m->identity = (mid == 1.0 && side == 1.0); }
static inline void ms_pair(const ms_t *m, int32_t *l, int32_t *r)
{
    int32_t M, S;
    if (m->identity) return;
    M = (int32_t)(((int64_t)*l + *r) >> 1);
    S = (int32_t)(((int64_t)*l - *r) >> 1);
    { int32_t dm = mulq(M, m->km), ds = mulq(S, m->ks); *l += dm + ds; *r += dm - ds; }
}

/* ---------------------------------------------------------------- trapezoidal SVF (Simper), one channel; modes LP BP HP NOTCH */
typedef struct { int32_t a1, a2, a3, k; } svf_c;      /* coefficients (Q28); k = damping */
typedef struct { int32_t ic1, ic2; } svf_s;           /* state (one per channel) */
enum { F_LP, F_BP, F_HP, F_NOTCH };
static inline void svf_coef(svf_c *c, double fc, double q)   /* fc Hz, q = resonance 0.5 (flat) .. 20 */
{
    double g = tan(M_PI * fc / SR), k = 1.0 / q;
    double a1 = 1.0 / (1.0 + g * (g + k));
    c->a1 = cq(a1); c->a2 = cq(g * a1); c->a3 = cq(g * g * a1); c->k = cq(k);
}
static inline int32_t svf_tick(const svf_c *c, svf_s *s, int32_t in, int mode)
{
    int32_t v3 = in - s->ic2, v1, v2, out;
    v1 = (int32_t)(((int64_t)c->a1 * s->ic1 + (int64_t)c->a2 * v3) >> COEF_Q);
    v2 = s->ic2 + (int32_t)(((int64_t)c->a2 * s->ic1 + (int64_t)c->a3 * v3) >> COEF_Q);
    s->ic1 = clamp32(2 * (int64_t)v1 - s->ic1, -16 * FS_Q23, 16 * FS_Q23);
    s->ic2 = clamp32(2 * (int64_t)v2 - s->ic2, -16 * FS_Q23, 16 * FS_Q23);
    switch (mode) {
    case F_BP: out = v1; break;
    case F_HP: out = in - mulq(v1, c->k) - v2; break;
    case F_NOTCH: out = in - mulq(v1, c->k); break;
    default: out = v2;
    }
    return out;
}
/* the stereo pair of a unit: the same coefficients, two states; then the unit's own pan */
typedef struct { svf_s l, r; } svf_pair_s;
static inline void svf_pair(const svf_c *c, svf_pair_s *s, int32_t *l, int32_t *r, int mode)
{
    *l = svf_tick(c, &s->l, *l, mode);
    *r = svf_tick(c, &s->r, *r, mode);
}

/* ---------------------------------------------------------------- tanh soft clip (table 2048 + linear interpolation, built once) */
#define TANH_N 2048
static int32_t TANH_T[TANH_N + 2];
static inline void tanh_init(void) { int i; for (i = 0; i < TANH_N + 2; i++) TANH_T[i] = (int32_t)lrint(tanh((double)i / TANH_N * 4.0) * (FS_Q23 - 1)); }
/* tanh(x / FS) * FS, |x| clipped at 4 FS (tanh = 1 there) */
static inline int32_t softclip(int32_t x)
{
    int64_t a = x < 0 ? -(int64_t)x : x;
    int32_t y;
    int64_t pos = a * TANH_N / (4 * (int64_t)FS_Q23);       /* table index (whole) */
    if (pos >= TANH_N) y = TANH_T[TANH_N];
    else {
        int64_t fr = (a * TANH_N) - pos * 4 * (int64_t)FS_Q23;             /* fraction, scaled by 4 FS */
        y = TANH_T[pos] + (int32_t)(((int64_t)(TANH_T[pos + 1] - TANH_T[pos]) * fr) / (4 * (int64_t)FS_Q23));
    }
    return x < 0 ? -y : y;
}

/* ---------------------------------------------------------------- dirt shapers (DUELL ins_shape, rescaled from 32768 to Q23) */
enum { D_OFF, D_SOFT, D_HARD, D_FOLD, D_FUZZ, D_CRUSH };
typedef struct { int32_t held; } dirt_s;            /* (state: CRUSH is memoryless here; kept so the pair has the same shape as the others) */
/* v is the signal after DRIVE (already multiplied by the caller); bits 1..24 for CRUSH (24 = clean) */
static inline int32_t dirt_shape(int ty, int32_t v, int bits)
{
    switch (ty) {
    case D_SOFT: return softclip(v);
    case D_HARD: return clamp32(v, -24000 * 256, 24000 * 256);
    case D_FOLD: {
        int32_t u = (int32_t)(((uint32_t)(v + 16384 * 256)) & 0xFFFFFFu) - 32768 * 256;   /* triangle of period 2^24 */
        u = 16384 * 256 - (u < 0 ? -u : u);
        return u + (u >> 1);
    }
    case D_FUZZ: return v > 0 ? (v > 20000 * 256 ? 20000 * 256 : v) : softclip(v) >> 1;
    case D_CRUSH: {
        int32_t step;
        int64_t q;
        if (bits >= 24) return v;
        if (bits < 1) bits = 1;
        step = 1 << (24 - bits);
        q = v >= 0 ? ((int64_t)v + step / 2) / step : -(((int64_t)-v + step / 2) / step);
        return clamp32(q * step, -FS_Q23, FS_Q23);
    }
    default: return v;
    }
}
/* drive: gain Q28 up to 8 (so Q28 would overflow int32: drive is Q24 here, 1.0 = 1<<24, up to 127) */
static inline int32_t drive_mul(int32_t x, int32_t gq24) { return clamp32(((int64_t)x * gq24) >> 24, -4 * FS_Q23, 4 * FS_Q23); }
static inline void dirt_pair(int ty, int32_t gq24, int bits, int32_t *l, int32_t *r)
{
    *l = dirt_shape(ty, drive_mul(*l, gq24), bits);
    *r = dirt_shape(ty, drive_mul(*r, gq24), bits);
}
#endif
