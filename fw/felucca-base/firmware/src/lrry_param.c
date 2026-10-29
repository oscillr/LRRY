/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3a: from the part's stored values (track_t.p[], after the matrix and MORPH have written the block's values: voice.c track_render calls this once a block) to what the
 * voice kernel (lrry_voice.c) reads. Integers only. The list of parameters, their ranges and defaults, and which old ids the voice keeps: LRRY/PARAM_LIST.md. The table of the new ones is
 * params.c XTP (ext.h X28 .. X63); the old ids keep their descriptors, with these meanings in the voice:
 *   P_FREEA CUT, P_FREEB RES, P_FREEC MODE (LP BP HP NOTCH), P_FREED SLOPE, P_KTRK      FILTER A (the DUELL "multimode filter"; LP with CUT open does not run, as DUELL's bypass)
 *   P_TWTYPE TYPE (OFF LP BP HP NOTCH LADDER COMB), P_CUT2, P_RES2, X_LINK, X_KTRK2,     FILTER B (the DUELL "twin filter", the same machinery: LINK, the routes to CUTOFF 2)
 *   X_ENV2 / X_LFO1 / X_LFO2 (to CUTOFF 2), X_ROUTE (SERIES / PARAL)
 *   P_DRV2 DRIVE                                                                       DIRT A's drive (on CRUSH: the bit depth)
 *   P_BLEND                                                                            added to every source's A / B balance (DUELL's FILTER BLEND, kept: its matrix routes live)
 *   X_SUBLVL, X_SUBOCT                                                                 the SUB LAYER (X_SUBWAVE, X_SUBHARM: no reader, the sub is a sine)
 *   P_ATK .. P_REL, ENV 2, LFO 1 / 2, CYCLE, the matrix, P_LEVEL, P_TRIM, P_PAN, P_MUTE, glide, voice mode, arp, steps: DUELL's, unchanged
 *   P_ED_SHP, P_LD_SHP, P_E2SHP, MD_SHP, P_E0 .. P_E7 (the engine's slots), P_DIST .. P_REV (SAT and the sends): nothing in the voice reads them (effects OFF; no engine) */
#ifndef LRRY_PARAM_C
#define LRRY_PARAM_C
#include "lrry_voice.c"
#ifndef UF_TOP
#define UF_TOP (UF_STEPS << 8)                          /* the open end of filter A's dial, Q8 (voice.c has the same line) */
#endif

#define LRRY_K127 2113664               /* 2^28 / 127: a 0..127 level as Q28 (127 -> 0.9999995) */
#define LRRY_PAN_Q15(v) ((((v) < -63 ? -63 : (v) > 63 ? 63 : (v)) * 8322) >> 4)   /* -64..+63 -> Q15 -1..+1 (+-63 = +-32767 / -32768) */
#define LRRY_PW_BASE ((uint32_t)(0.05 * 4294967296.0 + 0.5))                      /* PULSE: P1 0..127 = width 0.05 .. 0.95 */
#define LRRY_PW_STEP ((uint32_t)(0.9 * 4294967296.0 / 127.0 + 0.5))
#define LRRY_SYR_STEP 3612              /* SYNC ratio Q16 = 1 + 7 P1 / 127 (65536 + P1 x 3612) */
#define LRRY_FMR_BASE 32768             /* FM ratio Q16 = 0.5 + 7.5 P1 / 127 */
#define LRRY_FMR_STEP 3870
#define LRRY_FMI_STEP ((int64_t)(8.0 / 127.0 * 4294967296.0 / 6.283185307179586 + 0.5))   /* FM index: 0 .. 8 radians over P2, in phase units of 2^32 a cycle */
#define LRRY_FOLD_STEP ((int32_t)(7.0 * 16777216.0 / 127.0 + 0.5))                      /* FOLD amount Q24 = 1 + 7 P1 / 127 */
#define LRRY_DRV_STEP ((int32_t)(15.0 * 16777216.0 / 127.0 + 0.5))                      /* drive gain Q24 = 1 + 15 drive / 127 */
#define LRRY_SUPDET_Q8 51603            /* SUPER: detune cents Q8 = P1 x 100 / 127 (x 51603 >> 8) */
#define LRRY_COMBFB ((int32_t)(0.95 * 268435456.0 / 63.0 + 0.5))                        /* COMB feedback Q28 = FDBK / 63 x 0.95 */

/* the part's block: every value the voice reads from the stored ones, once a block */
static void lrry_decode(const track_t *t, lrry_pp_t *P)
{
    const int16_t *p = t->p;
    uint32_t s, k, i;
    int32_t lev[LS_N], pan[LS_N], bal[LS_N], blend = clamp(p[P_BLEND], -64, 63), ty;
    uint8_t *z = (uint8_t *)P;
    for (i = 0; i < sizeof *P; i++)
        z[i] = 0;
    P->f_serial = XV(t, X_ROUTE) == 0;
    P->d_serial = XV(t, X_DSER) == 1;
    lev[LS_O1] = clamp(XV(t, X_O1LVL), 0, 127) * LRRY_K127;
    lev[LS_O2] = clamp(XV(t, X_O2LVL), 0, 127) * LRRY_K127;
    lev[LS_NOISE] = clamp(XV(t, X_NZLVL), 0, 127) * LRRY_K127;
    lev[LS_RING] = clamp(XV(t, X_RGLVL), 0, 127) * LRRY_K127;
    pan[LS_O1] = XV(t, X_O1PAN); pan[LS_O2] = XV(t, X_O2PAN); pan[LS_NOISE] = XV(t, X_NZPAN); pan[LS_RING] = XV(t, X_RGPAN);
    bal[LS_O1] = XV(t, X_O1BAL); bal[LS_O2] = XV(t, X_O2BAL); bal[LS_NOISE] = XV(t, X_NZBAL); bal[LS_RING] = XV(t, X_RGBAL);
    for (s = 0; s < LS_N; s++) {                        /* the sources: pan law x level x the A / B balance (the host's rules, serial or parallel) */
        int32_t b = LRRY_PAN_Q15(clamp(bal[s] + blend, -63, 63)) * 8192, gl, gr, ba, bb;   /* Q28 */
        lt_pan_lr(LRRY_PAN_Q15(pan[s]), &gl, &gr);
        ba = b <= 0 ? LRRY_ONE : LRRY_ONE - b;
        if (P->f_serial)
            bb = b > 0 ? b : 0;
        else
            bb = b >= 0 ? LRRY_ONE : LRRY_ONE + b;
        for (k = 0; k < 2; k++) {
            int32_t bk = k ? bb : ba;
            P->tg[s][k][0] = MUL_Q28(MUL_Q28(gl, lev[s]), bk);
            P->tg[s][k][1] = MUL_Q28(MUL_Q28(gr, lev[s]), bk);
        }
        P->tg_off[s][0][0] = MUL_Q28(gl, lev[s]);
        P->tg_off[s][0][1] = MUL_Q28(gr, lev[s]);
        if (s < 2u) {
            P->lvq[s][0] = MUL_Q28(lev[s], ba);
            P->lvq[s][1] = MUL_Q28(lev[s], bb);
            P->lvq_off[s][0] = lev[s];
        }
    }
    for (s = 0; s < 2u; s++) {                          /* the two oscillators */
        uint32_t o = s ? X_O2MODEL : X_O1MODEL;
        int32_t p1 = clamp(XV(t, o + 6), 0, 127), p2 = clamp(XV(t, o + 7), 0, 127), p3 = clamp(XV(t, o + 8), 0, 127);
        P->model[s] = (uint8_t)clamp(XV(t, o), 0, LM_N - 1);
        P->coarse[s] = (int8_t)clamp(XV(t, o + 1), -24, 24);
        P->fine[s] = (int8_t)clamp(XV(t, o + 2), -50, 50);
        P->pw[s] = LRRY_PW_BASE + (uint32_t)p1 * LRRY_PW_STEP;
        P->ratio[s] = P->model[s] == LM_FM ? (uint32_t)LRRY_FMR_BASE + (uint32_t)p1 * LRRY_FMR_STEP : 65536u + (uint32_t)p1 * LRRY_SYR_STEP;
        P->fmidx[s] = (int64_t)p2 * LRRY_FMI_STEP;
        P->fold[s] = 16777216 + p1 * LRRY_FOLD_STEP;
        if (P->model[s] == LM_SUPER) {
            int32_t n = 1 + (p2 * 6 + 63) / 127, det_q8 = (p1 * LRRY_SUPDET_Q8) >> 8, spread = p3 * 258, inv_n1 = n > 1 ? (65536 + (n - 1) / 2) / (n - 1) : 0;
            P->sup_n[s] = (uint8_t)n;
            for (k = 0; k < (uint32_t)n; k++) {
                int32_t o_q16 = ((int32_t)(2 * k) - (n - 1)) * inv_n1, x_q24 = (int32_t)(((int64_t)det_q8 * o_q16 * 13981) >> 24);   /* detune cents of saw k in octaves, Q24 */
                int32_t pk = (int32_t)(((int64_t)(o_q16 >> 1) * spread) >> 15);   /* the saw's pan: (offset in -1..1) x SPREAD, Q15 */
                P->sup_mul[s][k] = lt_pow2(x_q24);
                lt_pan_lr(pk, &P->sup_pl[s][k], &P->sup_pr[s][k]);
            }
        }
    }
    P->sub_g = clamp(XV(t, X_SUBLVL), 0, 127) * LRRY_K127;
    P->sub_shift = (uint8_t)(2 - clamp(XV(t, X_SUBOCT), 0, 2));
    P->fa_mode = (uint8_t)clamp(p[P_FREEC], 0, 3);
    P->fa_p24 = p[P_FREED] == 2;
    P->fa_res = clamp(p[P_FREEB], 0, 127);
    P->fa_always = P->fa_mode != LF_LP;
    lrry_pan_set(&P->pa, LRRY_PAN_Q15(XV(t, X_FAPAN)));
    ty = clamp(p[P_TWTYPE], 0, 6);
    P->fb_kind = ty == 0 ? LB_OFF : ty <= 4 ? LB_SVF : ty == 5 ? LB_LADDER : LB_COMB;
    P->fb_mode = (uint8_t)(ty >= 1 && ty <= 4 ? ty - 1 : 0);
    P->fb_res = clamp(p[P_RES2], 0, 127);
    P->fb_comb_fb = clamp(p[P_RES2], -63, 63) * LRRY_COMBFB;
    P->comb_damp = Q28C(0.3);
    lrry_pan_set(&P->pb, LRRY_PAN_Q15(XV(t, X_FBPAN)));
    for (s = 0; s < 2u; s++) {                          /* the dirt units */
        int32_t ds = s ? clamp(XV(t, X_DBDRV), 0, 127) : clamp(p[P_DRV2], 0, 127);
        int32_t dt = clamp(XV(t, s ? X_DBTYPE : X_DATYPE), 0, LD_CRUSH);
        P->d_type[s] = (uint8_t)dt;
        P->d_bits[s] = 24;
        P->d_drive[s] = 16777216;
        if (dt == LD_CRUSH)
            P->d_bits[s] = (uint8_t)(24 - (ds * 23 + 63) / 127);   /* on CRUSH the DRIVE is the bit depth (no slot for BITS: ext.h) */
        else if (dt)
            P->d_drive[s] = 16777216 + ds * LRRY_DRV_STEP;
        P->d_lvl[s] = clamp(XV(t, s ? X_DBLVL : X_DALVL), 0, 127) * LRRY_K127;
        lrry_pan_set(&P->pd[s], LRRY_PAN_Q15(XV(t, s ? X_DBPAN : X_DAPAN)));
        P->any_dirt |= dt != 0;
    }
    {
        int32_t mid = clamp(XV(t, X_MID), 0, 127), side = clamp(XV(t, X_SIDE), 0, 127);
        P->ms.km = (mid - 64) * (1 << 22);
        P->ms.ks = (side - 64) * (1 << 22);
        P->ms.identity = mid == 64 && side == 64;
    }
}

/* one voice's block: its pitch and cutoffs (the voice's own: pitch with glide, bend, LFO and ENV to pitch, the per-note cutoff routes m->cutoff, KEY TRK, filter B's routes).
 * ucut0: filter A's dial (Q8). m: the voice's modulation as track_render built it. */
static void lrry_voice_ctl(const track_t *t, const voice_t *v, const vmod_t *m, const lrry_pp_t *P, int32_t ucut0, uint32_t pp, uint32_t vi, lrry_vc_t *C)
{
    const int16_t *p = t->p;
    uint32_t k;
    int32_t kt = p[P_KTRK] ? UF_KT(p[P_KTRK], v->pitch_cur) : 0, uc = clamp(ucut0 + m->cutoff + kt, 0, UF_TOP), ucn = clamp(ucut0 + m->cutoff - m->acc1 + kt, 0, UF_TOP);
    C->pp = pp;
    C->vi = vi;
    for (k = 0; k < 2u; k++) {                          /* pitch: the 1/16 semitone table (the voice's glide, bend, LFO, ENV to PITCH are in m->pitch16), then COARSE and the cents exactly (2^x table) and the voice's own residual m->fine (UNISON, TUNE, bend) */
        uint32_t inc = pitch_inc((uint32_t)clamp(m->pitch16 + P->coarse[k] * 16, 0, 2047));
        if (P->fine[k])
            inc = lt_scale_inc(inc, lt_pow2(P->fine[k] * 13981));   /* cents / 1200 octaves in Q24 (13981.0 a cent) */
        if (m->fine)
            inc += (uint32_t)((int32_t)(inc >> 12) * m->fine);
        C->inc[k] = inc;
    }
    C->incs = m->inc >> P->sub_shift;
    C->pos_a = -1;
    if ((P->fa_always || uc < UF_TOP) && !BYP(t, BY_MULTI))
        C->pos_a = 40 + ((uc * 99) >> 6);
    C->pos_b = -1;
    C->comb_d = 0;
    if (P->fb_kind != LB_OFF && !BYP(t, BY_TWIN)) {     /* filter B: the cutoff as chain_f2.c f2_cut makes it (LINK, the three routes, the matrix's, KEY TRK 2), without the accent and the glide of a LINK switch */
        const mod_win_t *mod = &MODW(t);
        int32_t ms = (E2Q(t, v) * DEP_AMT(XV(t, X_ENV2))) >> 7, lf = mulq15(t->lfo_val, t->lfo_fade), c, link = XV(t, X_LINK) != 0;
        if (mod->on && mod->ldep < 32767)
            lf = mulq15(lf, mod->ldep);
        ms += (lf * DEP_AMT(XV(t, X_LFO1))) >> 7;
        if (XV(t, X_LFO2)) {
            lf = mulq15(t->lfo2.val, t->lfo2.fade);
            if (mod->on && mod->ldep2 < 32767)
                lf = mulq15(lf, mod->ldep2);
            ms += (lf * DEP_AMT(XV(t, X_LFO2))) >> 7;
        }
        if (mod->on)
            ms += mod_f2_voice((track_t *)t, (voice_t *)v);
        if (P->fb_kind == LB_COMB) {                    /* tuned to the note: CUTOFF 2 is the TUNE (semitones) */
            int32_t tune = clamp((clamp(p[P_CUT2], -24, 24) << 4) + ((ms * 2711) >> 15), -768, 768);
            uint32_t incc = pitch_inc((uint32_t)clamp(m->pitch16 + tune, 0, 2047)), d;
            if (m->fine)
                incc += (uint32_t)((int32_t)(incc >> 12) * m->fine);
            d = (uint32_t)((lt_inv56(incc) + (1u << 23)) >> 24);     /* the period in samples: 2^32 / inc */
            while (d >= LRRY_COMB_LEN)
                d >>= 1;                                /* a note the line cannot hold sounds an octave up (DUELL's rule too) */
            C->comb_d = d < 2u ? 2 : (int32_t)d;
            C->pos_b = 0;
        } else {
            c = link ? ucn + TW_OFF_Q8(clamp(p[P_CUT2], -TW_OFF_MAX, TW_OFF_MAX)) : clamp(p[P_CUT2], 0, UF_STEPS) * 256;
            if (XV(t, X_KTRK2))
                c += UF_KT(XV(t, X_KTRK2), v->pitch_cur);
            c = clamp(c + ms, 0, UF_TOP);
            C->pos_b = 40 + ((c * 99) >> 6);
        }
    }
}
#endif
