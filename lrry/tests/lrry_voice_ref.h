/* SPDX-License-Identifier: GPL-3.0-only
 * PHASE62: the REFERENCE kernel for tests/lrry_kern_equiv_test.c: lrry_voice as it was in commit 7427354 (one monolithic per-sample loop), verbatim, renamed lrry_voice_ref. The faster
 * kernel (lrry_voice.c, passes) must give the same samples and leave the same state, bit for bit, on every input; this file is the proof's other side. It uses lrry_voice.c's helpers
 * (their meaning is unchanged). Never edit it. */
/* ------------------------------------------------------------------------------ the voice block */
/* Renders n (<= CTL) samples of the voice and ADDS them to outL / outR at the firmware's sample scale (the one conversion: LRRY_TO_FW). m carries L/RRY's amp ramp (amp0 -> amp1: envelope,
 * velocity, LFO -> AMP). Returns nothing: the voice's life (its release end) is L/RRY's env_tick. */
static __attribute__((noinline)) void lrry_voice_ref(const lrry_pp_t *P, const lrry_vc_t *C, lrry_vs_t *V, const vmod_t *m, int32_t *outL, int32_t *outR, uint32_t n)
{
    int32_t bufL[CTL], bufR[CTL];
    uint32_t i, s, k;
    int32_t tg[LS_N][2][2];
    uint64_t inv[2], sl_inv[2], sup_inv[2][LRRY_SUPMAX], sub_inv = 0;
    uint32_t sup_inc[2][LRRY_SUPMAX], slave_inc[2];
    lrry_svfc_t ca, cb;
    lrry_ladc_t lc;
    int32_t fcx = 0, fcb = 0, fcl = 1024, fax = 0, fab = 0, fal = 0, fdx = 0, fdb = 0, fdl = 0, ffd = 0, fxg = 32;   /* filter A's MODE weights and their ramp, the slope fade */
    int flp = 1, frun2 = 0;
    int fa_on = C->pos_a >= 0, fb_on = P->fb_kind != LB_OFF && C->pos_b >= 0 && (P->fb_kind != LB_COMB || C->comb_d > 0);
    int16_t *line[2] = {0, 0};
    int32_t lvq[2][2][2];
    int64_t gacc[LS_N][2][2];                           /* the source gains' ramp: g0 << CTL_LOG2 plus the step each sample (no multiply in the loop) */
    int32_t gstep[LS_N][2][2];
    uint8_t act[LS_N][2], use_osc[2], nz_on, ring_on;
    int64_t sacc[2];                                    /* SUB PAN's ramp, as gacc */
    lrry_pr_t pra, prb, prd[2];                         /* phase 56: the four stage pans' ramps */
    int32_t sstep[2], sub_on;
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
        for (k = 0; k < 2; k++) {
            lvq[s][k][0] = any_f ? P->lvp[s][k][0] : P->lvp_off[s][k][0];
            lvq[s][k][1] = any_f ? P->lvp[s][k][1] : P->lvp_off[s][k][1];
        }
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
    for (s = 0; s < LS_N; s++)                          /* which sources sound (a gain at 0 now and last block: nothing to mix, and an oscillator nobody hears is not rendered) */
        for (k = 0; k < 2; k++) {
            int sup = s < 2u && P->model[s] == LM_SUPER;   /* (phase 56: SUPER's gains ramp too, from its last block's; they stepped once a block) */
            int32_t t0 = sup ? lvq[s][k][0] : tg[s][k][0], t1 = sup ? lvq[s][k][1] : tg[s][k][1];
            int32_t g0l = sup && fresh ? t0 : V->gprev[s][k][0], g0r = sup && fresh ? t1 : V->gprev[s][k][1];
            act[s][k] = (t0 | t1 | g0l | g0r) != 0;
            gacc[s][k][0] = (int64_t)g0l << CTL_LOG2;
            gacc[s][k][1] = (int64_t)g0r << CTL_LOG2;
            gstep[s][k][0] = t0 - g0l;
            gstep[s][k][1] = t1 - g0r;
        }
    ring_on = act[LS_RING][0] | act[LS_RING][1];
    nz_on = act[LS_NOISE][0] | act[LS_NOISE][1];
    use_osc[0] = act[LS_O1][0] | act[LS_O1][1] | ring_on;
    use_osc[1] = act[LS_O2][0] | act[LS_O2][1] | ring_on;
    if (P->sub_g && P->sub_wave >= 2u)
        sub_inv = lt_inv56(C->incs ? C->incs : 1u);
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
    if (fa_on) {
        uint32_t s24 = P->fa_p24, t0 = V->fa_t;
        lrry_svf_coef(&ca, C->pos_a, lt_k(P->fa_res));
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
        if (P->nz_type && nz_on)
            nzv = lrry_nz_color(V, nzv, P->nz_type);
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
                y = lrry_dirt_shape(LD_FOLD, MUL_Q24(t, P->fold[s]), 24);
                y = lt_clamp32(y, -LRRY_FS, LRRY_FS);
                break; }
            case LM_SUPER: {
                int32_t l = 0, r = 0;
                for (k = 0; k < P->sup_n[s]; k++) {
                    int32_t w = lrry_saw(V->sup_ph[s][k], sup_inc[s][k], sup_inv[s][k]);
                    V->sup_ph[s][k] += sup_inc[s][k];
                    l += MUL_Q28(w, P->sup_pl[s][k]);
                    r += MUL_Q28(w, P->sup_pr[s][k]);
                }
                yr = MUL_Q28(r, LT_INVSQ[P->sup_n[s]]);
                y = MUL_Q28(l, LT_INVSQ[P->sup_n[s]]);
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
                if (s < 2u && P->model[s] == LM_SUPER) {   /* SUPER: its own L and R, level x balance x its pan (lvq per channel; phase 56: ramped as the mono sources) */
                    gacc[s][k][0] += gstep[s][k][0];
                    gacc[s][k][1] += gstep[s][k][1];
                    bus[k][0] += MUL_Q28(src[s], (int32_t)(gacc[s][k][0] >> CTL_LOG2));
                    bus[k][1] += MUL_Q28(srcR[s], (int32_t)(gacc[s][k][1] >> CTL_LOG2));
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
        if (P->sub_g) {                                 /* SUB LAYER: mono into L and R of bus A, with SUB PAN (the mono law, ramped as the sources); at the centre no gain at all */
            int32_t sb = MUL_Q28(MUL_Q28(lrry_subw(P->sub_wave, V->sub_ph, C->incs, sub_inv), P->sub_g), LRRY_SGQ);
            V->sub_ph += C->incs;
            if (sub_on) {
                sacc[0] += sstep[0];
                sacc[1] += sstep[1];
                bus[0][0] += MUL_Q28(sb, (int32_t)(sacc[0] >> CTL_LOG2));
                bus[0][1] += MUL_Q28(sb, (int32_t)(sacc[1] >> CTL_LOG2));
            } else {
                bus[0][0] += sb;
                bus[0][1] += sb;
            }
        }
        aL = bus[0][0]; aR = bus[0][1]; bL = bus[1][0]; bR = bus[1][1];
        if (fa_on) {                                    /* filter A (12 or 24 dB, MODE as a blend), its pan */
            int32_t wx, wb, wl;
            fax += fdx; fab += fdb; fal += fdl;
            wx = fax >> 5; wb = fab >> 5; wl = fal >> 5;
            aL = lrry_fa_tick(&ca, &V->fa1[0], aL, flp, wx, wb, wl);
            aR = lrry_fa_tick(&ca, &V->fa1[1], aR, flp, wx, wb, wl);
            if (frun2) {                                /* 24 dB: the same MODE again */
                int32_t uL = lrry_fa_tick(&ca, &V->fa2[0], aL, flp, wx, wb, wl), uR = lrry_fa_tick(&ca, &V->fa2[1], aR, flp, wx, wb, wl);
                if (ffd) {
                    fxg += ffd;
                    uL = aL + (((uL - aL) * fxg) >> 5);
                    uR = aR + (((uR - aR) * fxg) >> 5);
                }
                aL = uL; aR = uR;
            }
            lrry_pr_tick(&pra, &P->pa, &aL, &aR);
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
            lrry_pr_tick(&prb, &P->pb, &bL, &bR);
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
                lrry_pr_tick(&prd[0], &P->pd[0], &l, &r);
            }
            if (P->d_type[1]) {
                lrry_dirt_pair(P->d_type[1], P->d_drive[1], P->d_bits[1], &l, &r);
                l = MUL_Q28(l, P->d_lvl[1]); r = MUL_Q28(r, P->d_lvl[1]);
                lrry_pr_tick(&prd[1], &P->pd[1], &l, &r);
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
                lrry_pr_tick(&prd[0], &P->pd[0], &l1, &r1);
            }
            if (P->d_type[1]) {
                lrry_dirt_pair(P->d_type[1], P->d_drive[1], P->d_bits[1], &l2, &r2);
                l2 = MUL_Q28(l2, P->d_lvl[1]); r2 = MUL_Q28(r2, P->d_lvl[1]);
                lrry_pr_tick(&prd[1], &P->pd[1], &l2, &r2);
            }
            oL = l1 + l2; oR = r1 + r2;
            if (P->f_serial && P->d_type[0] && P->d_type[1]) {
                oL >>= 1; oR >>= 1;
            }
        }
        a = amp_at(m, i);                               /* L/RRY's amp ramp: envelope x velocity x LFO -> AMP, Q15 */
        oL = MUL_Q15(oL, a); oR = MUL_Q15(oR, a);
        bufL[i] = MUL_Q28(oL, LRRY_VGQ);
        bufR[i] = MUL_Q28(oR, LRRY_VGQ);
    }
    for (s = 0; s < LS_N; s++)
        for (k = 0; k < 2; k++) {
            int sup = s < 2u && P->model[s] == LM_SUPER;
            V->gprev[s][k][0] = sup ? lvq[s][k][0] : tg[s][k][0];
            V->gprev[s][k][1] = sup ? lvq[s][k][1] : tg[s][k][1];
        }
    V->sub_gp[0] = P->sub_tg[0];
    V->sub_gp[1] = P->sub_tg[1];
    if (fa_on) {
        V->faw[0] = (int16_t)fcx; V->faw[1] = (int16_t)fcb; V->faw[2] = (int16_t)fcl;
        V->fa_t = (uint8_t)(1u + P->fa_p24);
    }
    for (i = 0; i < n; i++) {                           /* the one conversion to the firmware's scale */
        outL[i] += LRRY_TO_FW(bufL[i]);
        outR[i] += LRRY_TO_FW(bufR[i]);
    }
}
