/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY filter unit B extras (DUELL chain_f2.c types LADDER and COMB), one channel each, pair = two states. Q23 signals, int64 products. */
#ifndef LRRY_FILT2_H
#define LRRY_FILT2_H
#include "lrry_dsp.h"

/* LADDER: four trapezoidal one-poles, feedback of the last stage through one tanh (bounded at any resonance) */
typedef struct { int32_t G, k; } lad_c;                 /* G = g/(1+g) Q28, k = feedback 0..4 (Q28; 4 = self oscillation) */
typedef struct { int32_t s[4]; int32_t y4; } lad_s;
static inline void lad_coef(lad_c *c, double fc, double res01) { double g = tan(M_PI * fc / SR); c->G = cq(g / (1.0 + g)); c->k = cq(4.0 * res01 * 0.97); }
static inline int32_t lad_tick(const lad_c *c, lad_s *st, int32_t in)
{
    int32_t u = softclip(in - mulq(st->y4, c->k) * 1), x = u, y = 0;
    int i;
    for (i = 0; i < 4; i++) {
        int32_t v = mulq(x - st->s[i], c->G);
        y = st->s[i] + v;
        st->s[i] = clamp32((int64_t)y + v, -16 * FS_Q23, 16 * FS_Q23);
        x = y;
    }
    st->y4 = y;
    return softclip(y);
}

/* COMB: feedback delay of D samples (a note's period), damped by a one-pole in the loop; feedback -0.95..+0.95 */
#define COMB_MAX 2048
typedef struct { int32_t fb, damp; int32_t D; } comb_c;
typedef struct { int32_t buf[COMB_MAX]; int32_t lp; uint32_t w; } comb_s;
static inline void comb_coef(comb_c *c, double hz, double fb, double damp01) { c->D = (int32_t)lrint(SR / hz); if (c->D < 2) c->D = 2; if (c->D >= COMB_MAX) c->D = COMB_MAX - 1; c->fb = cq(fb); c->damp = cq(damp01); }
static inline int32_t comb_tick(const comb_c *c, comb_s *s, int32_t in)
{
    int32_t d = s->buf[(s->w - (uint32_t)c->D) & (COMB_MAX - 1)];
    int32_t out;
    s->lp += mulq(d - s->lp, c->damp);
    out = clamp32((int64_t)in + mulq(s->lp, c->fb), -2 * FS_Q23, 2 * FS_Q23);
    s->buf[s->w & (COMB_MAX - 1)] = out;
    s->w++;
    return softclip(out);
}
#endif
