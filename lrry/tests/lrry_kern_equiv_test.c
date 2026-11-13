/* SPDX-License-Identifier: GPL-3.0-only
 * PHASE62 host check: the voice kernel (lrry_voice.c, rendered in passes) against the reference kernel (tests/lrry_voice_ref.h: the one-loop kernel of commit 7427354, verbatim).
 * Random voices: every model of both oscillators (SUPER with 1..7 saws, SYNC, FM, FOLD, PULSE ...), noise of the three colours, RING, SUB (four waves, SUB PAN ramps), filter A (12 / 24 dB,
 * MODE blends, the SLOPE fade, a steady low-pass), filter B (SVF four types, LADDER, COMB), dirt A and B (five shapes, serial or side by side), serial or parallel filters, the four stage pans
 * (steady and moving), the amp ramp, steps from near zero to above the BLEP limit, blocks of 1 .. 32 samples, and four blocks in a row with the parameters changing between them
 * (the gain ramps, the filter weights' ramps, the pan ramps start from the block before). After every block: the output (added to a random base) and the whole voice state (memcmp)
 * and the COMB's lines must be bit for bit equal.
 * Build (from lrry):  ../onepart.sh 1 1 lrry_kern_equiv      (LRRY_CFX="-fwrapv" is added by this file's own check of overflow: the inputs are random, extremes too)
 * voice-cheaper 2: pass 6 (one gain, LRRY_PASS6_ONEGAIN) is now the DEFAULT: its OUTPUT is compared with a tolerance (max difference 2 LSB; the error energy against the signal energy printed in dB) while the voice STATE and the COMB's
 * lines stay bit for bit; built with -DLRRY_PASS6_LEGACY (LRRY_CFX) the kernel has the old two-product pass 6 and EVERYTHING is bit for bit the reference's (the proof of the (a) changes: run both). The filters' coefficient memo
 * is exercised by blocks that keep the same cutoffs.
 * Argument: the number of voices (default 40000). */
#include <stddef.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "lrry_voice_ref.h"

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void)
{
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
    return (uint32_t)(rs >> 16);
}
static uint32_t rr(uint32_t n) { return rnd() % n; }                 /* 0 .. n-1 */
static int32_t gain(void)                                           /* a Q28 gain: zero, one, or anything up to 1.4 */
{
    uint32_t k = rr(8);
    if (k < 2) return 0;
    if (k < 4) return LRRY_ONE;
    return (int32_t)(rnd() % (uint32_t)(1.4 * LRRY_ONE));
}
static void rnd_pan(lrry_pan_t *p)
{
    uint32_t k = rr(3);
    p->side = k == 0 ? 0 : k == 1 ? 1 : -1;
    p->g = rr(4) == 0 ? 0 : (int32_t)(rnd() % (uint32_t)LRRY_ONE);
}
static void rnd_pp(lrry_pp_t *P)
{
    uint32_t s, k, c, i;
    memset(P, 0, sizeof *P);
    for (s = 0; s < LS_N; s++)
        for (k = 0; k < 2; k++)
            for (c = 0; c < 2; c++) {
                P->tg[s][k][c] = gain();
                P->tg_off[s][k][c] = gain();
            }
    for (s = 0; s < 2; s++)
        for (k = 0; k < 2; k++) {
            P->lvq[s][k] = gain(); P->lvq_off[s][k] = gain();
            for (c = 0; c < 2; c++) { P->lvp[s][k][c] = gain(); P->lvp_off[s][k][c] = gain(); }
        }
    for (s = 0; s < 2; s++) {
        P->model[s] = (uint8_t)rr(LM_N);
        P->pw[s] = rnd();
        P->ratio[s] = 65536u + rnd() % (7u * 65536u);
        P->fmidx[s] = (int64_t)(rnd() % 1500000000u) * (rr(2) ? 1 : 2);
        P->fold[s] = (1 << 24) + (int32_t)(rnd() % (7u << 24));
        P->sup_n[s] = (uint8_t)(1 + rr(LRRY_SUPMAX));
        for (k = 0; k < LRRY_SUPMAX; k++) {
            P->sup_mul[s][k] = (uint32_t)(0.7 * LRRY_ONE) + rnd() % (uint32_t)(0.7 * LRRY_ONE);
            P->sup_pl[s][k] = gain(); P->sup_pr[s][k] = gain();
        }
    }
    P->sub_g = rr(3) ? gain() : 0;
    P->sub_tg[0] = P->sub_tg[1] = LRRY_ONE;
    if (rr(2)) { P->sub_tg[0] = gain(); P->sub_tg[1] = gain(); }
    P->nz_type = (uint8_t)rr(3);
    P->sub_wave = (uint8_t)rr(4);
    P->fa_mode = (uint8_t)(rr(3) == 0 ? 0 : rr(128));
    P->fa_p24 = (uint8_t)rr(2);
    P->fa_always = (uint8_t)rr(2);
    P->fa_res = (int32_t)rr(128);
    P->fb_kind = (uint8_t)rr(4);
    P->fb_mode = (uint8_t)rr(4);
    P->fb_res = (int32_t)rr(128);
    P->fb_comb_fb = (int32_t)((int64_t)rr(127) - 63) * LRRY_COMBFB;
    P->comb_damp = (int32_t)(rnd() % 2000000000u);
    P->f_serial = (uint8_t)rr(2);
    P->d_serial = (uint8_t)rr(2);
    rnd_pan(&P->pa); rnd_pan(&P->pb); rnd_pan(&P->pd[0]); rnd_pan(&P->pd[1]);
    rnd_pan(&P->pa0); rnd_pan(&P->pb0); rnd_pan(&P->pd0[0]); rnd_pan(&P->pd0[1]);
    if (rr(2)) { P->pa0 = P->pa; P->pb0 = P->pb; P->pd0[0] = P->pd[0]; P->pd0[1] = P->pd[1]; }
    for (i = 0; i < 2; i++) {
        P->d_type[i] = (uint8_t)(rr(3) == 0 ? 0 : rr(6));
        P->d_bits[i] = (uint8_t)(1 + rr(24));
        P->d_drive[i] = (1 << 24) + (int32_t)(rnd() % (15u << 24));
        P->d_lvl[i] = gain();
    }
    P->any_dirt = P->d_type[0] | P->d_type[1];
}
static uint32_t rnd_inc(void)
{
    uint32_t k = rr(10);
    if (k == 0) return 0;
    if (k == 1) return (1u << 29) + rr(3) - 1u;               /* at the BLEP limit */
    if (k == 2) return rnd();
    return (rnd() >> rr(18)) | 1u;                              /* 2^14 .. 2^32 */
}
static void rnd_vc(lrry_vc_t *C, const lrry_pp_t *P)
{
    memset(C, 0, sizeof *C);
    C->inc[0] = rnd_inc(); C->inc[1] = rnd_inc(); C->incs = rnd_inc();
    C->uc = (int32_t)rr(UF_TOP + 1);
    C->pos_a = rr(3) == 0 ? -1 : (int32_t)rr(127 << 8);
    C->pos_b = rr(3) == 0 ? -1 : (int32_t)rr(127 << 8);
    C->comb_d = P->fb_kind == LB_COMB ? (int32_t)(2 + rr(LRRY_COMB_LEN - 2)) : 0;
    C->pp = 0;
    C->vi = rr(NVOICE);
}
#if defined(LRRY_PASS6_LEGACY)
#define LRRY_EXACT_FLAG 1
#else
#define LRRY_EXACT_FLAG 0
#endif
static int nfail, ncheck;
static double e_err, e_sig, e_max;                                    /* error and signal energy of the outputs (the signal = the output less the random base), the largest single difference */
#if defined(LRRY_PASS6_LEGACY)
#define OUT_DIFF(a, b, base) (memcmp((a), (b), sizeof(a)) != 0)
#else
static int out_diff(const int32_t *a, const int32_t *b, const int32_t *base)
{
    uint32_t i;
    for (i = 0; i < CTL; i++) {
        double d = (double)a[i] - (double)b[i], sg = (double)a[i] - (double)base[i];
        e_err += d * d; e_sig += sg * sg;
        if (fabs(d) > e_max) e_max = fabs(d);
    }
    return 0;
}
#define OUT_DIFF(a, b, base) out_diff((a), (b), (base))
#endif

int main(int argc, char **argv)
{
    uint32_t trials = argc > 1 ? (uint32_t)atoi(argv[1]) : 40000u, tr, b, i, bad = 0, nblk = 0, modelhit[LM_N] = {0};
    static lrry_vs_t vr, vn;
    static uint8_t mem0[ENG_MEM_PART], memr[ENG_MEM_PART];
    for (tr = 0; tr < trials && bad < 5; tr++) {
        lrry_pp_t P;
        uint32_t note = rnd();
        rnd_pp(&P);
        lrry_state_clear(&vr, note);
        if (rr(2)) {                                            /* a voice that has been sounding: random phases, filter states, COMB position and last gains (the clamps, the COMB's wrap, the ramps' starts) */
            uint32_t j, q;
            int32_t big = rr(2) ? (1 << 27) : (1 << 23);
#define RS() ((int32_t)(rnd() % (2u * (uint32_t)big)) - big)
            for (j = 0; j < 2; j++) { vr.ph[j] = rnd(); vr.ph2[j] = rnd(); vr.fa1[j].ic1 = RS(); vr.fa1[j].ic2 = RS(); vr.fa2[j].ic1 = RS(); vr.fa2[j].ic2 = RS(); vr.fb[j].ic1 = RS(); vr.fb[j].ic2 = RS();
                                  vr.lad_y[j] = RS(); vr.comb_lp[j] = RS(); vr.sub_gp[j] = gain();
                                  for (q = 0; q < 4; q++) vr.lad[j][q] = RS(); }
            vr.sub_ph = rnd();
            for (j = 0; j < 2; j++) for (q = 0; q < LRRY_SUPMAX; q++) vr.sup_ph[j][q] = rnd();
            for (j = 0; j < 4; j++) vr.nz_b[j] = (int32_t)(rnd() % 20000u) - 10000;
            for (j = 0; j < LS_N; j++) for (q = 0; q < 4; q++) vr.gprev[j][q >> 1][q & 1] = gain();
            vr.sub_gp_ok = 1;
            vr.comb_on = 1;
            vr.comb_w = LRRY_COMB_LEN - 1u - rr(40);
            for (j = 0; j < 3; j++) vr.faw[j] = (int16_t)rr(1025);
            vr.fa_t = (uint8_t)rr(3);
        }
        vn = vr;
        for (i = 0; i < ENG_MEM_PART; i++)
            ((uint8_t *)ENG_MEM(0))[i] = (uint8_t)rnd();         /* the COMB's lines start as whatever is in the pool: the kernel clears them itself */
        modelhit[P.model[0]]++;
        lrry_vc_t C;
        for (b = 0; b < 4u; b++) {
            vmod_t m;
            int32_t baseL[CTL], baseR[CTL], oRL[CTL], oRR[CTL], oNL[CTL], oNR[CTL];
            uint32_t n = rr(4) == 0 ? 1 + rr(CTL) : CTL;
            if (b > 0 && rr(2)) {                               /* the parameters change between blocks: ramps run from the block before's values */
                lrry_pp_t Q;
                rnd_pp(&Q);
                if (rr(2)) { Q.model[0] = P.model[0]; Q.model[1] = P.model[1]; Q.fb_kind = P.fb_kind; Q.d_serial = P.d_serial; Q.f_serial = P.f_serial; }
                Q.pa0 = P.pa; Q.pb0 = P.pb; Q.pd0[0] = P.pd[0]; Q.pd0[1] = P.pd[1];
                P = Q;
            } else if (b > 0) {
                P.pa0 = P.pa; P.pb0 = P.pb; P.pd0[0] = P.pd[0]; P.pd0[1] = P.pd[1];
            }
            if (b > 0 && rr(2)) {                               /* the same steps as the block before (the inv memo hits), or new ones */
                lrry_vc_t C2 = C;
                rnd_vc(&C, &P);
                C.inc[0] = C2.inc[0]; C.inc[1] = C2.inc[1]; C.incs = C2.incs;
                if (rr(2)) { C.pos_a = C2.pos_a; C.pos_b = C2.pos_b; C.uc = C2.uc; }   /* voice-cheaper 2: the same cutoffs too (the filters' coefficient memo hits; with the same RES it must give the same bits) */
            } else
                rnd_vc(&C, &P);
            memset(&m, 0, sizeof m);
            m.amp0 = (int32_t)rr(32768); m.amp1 = (int32_t)rr(32768);
            for (i = 0; i < CTL; i++) { baseL[i] = (int32_t)rnd() >> 8; baseR[i] = (int32_t)rnd() >> 8; }
            memcpy(mem0, ENG_MEM(0), ENG_MEM_PART);
            memcpy(oRL, baseL, sizeof baseL); memcpy(oRR, baseR, sizeof baseR);
            lrry_voice_ref(&P, &C, &vr, &m, oRL, oRR, n);
            memcpy(memr, ENG_MEM(0), ENG_MEM_PART);
            memcpy(ENG_MEM(0), mem0, ENG_MEM_PART);
            memcpy(oNL, baseL, sizeof baseL); memcpy(oNR, baseR, sizeof baseR);
            lrry_voice(&P, &C, &vn, &m, oNL, oNR, n);
            ncheck++;
            nblk++;
            if (OUT_DIFF(oRL, oNL, baseL) | OUT_DIFF(oRR, oNR, baseR) || memcmp(&vr, &vn, offsetof(lrry_vs_t, ik)) || memcmp(memr, ENG_MEM(0), ENG_MEM_PART) || e_max > 2.0) {
                nfail++;
                bad++;
                printf("lrry_kern_equiv: MISMATCH trial %u block %u n %u: models %u %u fa_on %d fb_kind %u d %u %u fser %u dser %u sub %d nz %u : out %s state %s comb %s\n", tr, b, n,
                       P.model[0], P.model[1], C.pos_a >= 0, P.fb_kind, P.d_type[0], P.d_type[1], P.f_serial, P.d_serial, P.sub_g != 0, P.nz_type,
                       (e_max > 2.0 || memcmp(oRL, oNL, sizeof oRL) * LRRY_EXACT_FLAG || memcmp(oRR, oNR, sizeof oRR) * LRRY_EXACT_FLAG) ? "DIFF" : "same", memcmp(&vr, &vn, offsetof(lrry_vs_t, ik)) ? "DIFF" : "same", memcmp(memr, ENG_MEM(0), ENG_MEM_PART) ? "DIFF" : "same");
                break;
            }
        }
    }
    if (!LRRY_EXACT_FLAG)
        printf("lrry_kern_equiv: output error against the reference kernel (pass 6 changed): largest difference %.0f LSB (Q15 output), error %.1f dB below the signal (energy ratio over %u blocks)\n", e_max, 10.0 * log10((e_err > 0 ? e_err : 1e-30) / (e_sig > 0 ? e_sig : 1.0)), nblk);
    printf("lrry_kern_equiv: %u voices, %u blocks compared (osc 1 model counts SAW %u TRI %u SINE %u PULSE %u SYNC %u SUPER %u FM %u FOLD %u): %s\n", tr, nblk, modelhit[0], modelhit[1], modelhit[2], modelhit[3], modelhit[4],
           modelhit[5], modelhit[6], modelhit[7], nfail ? "FAIL" : LRRY_EXACT_FLAG ? "all bit for bit equal" : "state bit for bit equal, output within 2 LSB");
    return nfail != 0;
}
