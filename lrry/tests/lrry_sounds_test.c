/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY factory test and stress sounds (tools/lrry_sounds.py, 2026-10-31; LRRY/PATCHES_TEST_SET.md): each sound loaded as the UI loads it (hostsim.c host_ds_load), its End B made as
 * the unit makes it on the fader's first use (morph.c morph_ready: the sound's seed and AMOUNT), rendered through the real mix path AT THE UNIT'S MIX LEVEL (MASTER_USB_REF; hostsim's
 * default renders 6 dB hotter). Build (from lrry):  ../onepart.sh 1 1 lrry_sounds
 * Per sound:
 *   - the budget (budget.c budget_solve2, an ESTIMATE): the voices one core and two cores give it, the cost of one voice;
 *   - a held C3 at velocity 100, fader at A: the loudness (the loudest 0.1 s, L and R); and each pan route heard: 6 s held with every route and without that one (its AMOUNT 0), the
 *     largest difference of the L / R balance in a 70 ms window ("pan dB": the weakest route's);
 *   - "all notes held": an 8-note chord at velocity 127, the voices capped at the two-core number (what the unit plays), fader at A and at B: the limiter's input peak (master_out: after
 *     the DC blocker; the limiter acts above LIM_T), the soft clip's loss at that peak, the part mix's int32 factor (fx.c part_mix_st: wraps at 2^19), the output peak;
 *   - silence: nothing at all before a note; 2 s after the last voice has ended (master_out's DC blocker settles), nothing.
 * Then the coverage matrix of LRRY/PATCHES_TEST_SET.md, worked out from the STORED values of every loaded sound and compared with the document's claims (CLAIM below), row by row */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int nfail, ncheck;
#define KNEE4_MAX 12.0                                  /* the 4-note chord's limiter input stays at most this many dB over LIM_T (measured worst: COMB PLUCK +11.0 dB; the limiter is meant to take the peaks of a chord) */
#define LEVEL_TARGET -17.0                              /* the average level of a held C3: the cloud's first set (about -17 dB); every sound within LEVEL_SPREAD of the mean */
#define LEVEL_SPREAD 1.5
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_sounds: %-120s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}
static void fresh(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    memset(lrry_vs, 0, sizeof lrry_vs);
    memset(lrry_pp, 0, sizeof lrry_pp);                 /* (2026-11-12: the pan slew's memory: a fresh part starts at its own pans) */
    memset(lrry_key_ok, 0, sizeof lrry_key_ok);
    memset(&pscr, 0, sizeof pscr);
    memset(mph, 0, sizeof mph);
    for (i = 0; i < NLIVE; i++)
        pan_gv[i] = mx_ok[i] = 0;
    dc_l = dc_r = dce_l = dce_r = 0;
    lim_env = lim_env_r = LIM_T;                        /* (the limiter from rest: a run must not inherit the run before's envelope) */
    song.master_q12 = MASTER_USB_REF;                   /* the unit's mix level */
}
static uint32_t cur;                                    /* the sound being played */
#define NOFF 2000                                       /* blocks held (1.45 s), then 0.3 s of release; then the render goes on until every voice is idle */
#define NB (NOFF + 400)
#define NTAIL 140                                       /* the silence window once every voice is idle: 0.1 s */
#define NW 140                                          /* the loudness window: the loudest 0.1 s (a held note's body, a pluck's attack) */
#define NSETTLE 2760                                    /* 2 s after the last voice ended: the DC blocker settles */
#define NIDLE 64                                        /* blocks rendered before the first note */
#define NBMAX 8700                                      /* the longest render: the 6 s pan run */
static double el[NBMAX], er[NBMAX];
static double sl, sr;                                   /* the loudest window's sums of L and R */
static int64_t opk, xpk, ppk, tpk, ipk;                 /* output peak, part_mix_st factor, the master's input peak, the tail's peak, the idle peak */
static double tidle;                                   /* seconds from the note off to every voice idle */
static int voices_on(void) { return lrj.mask != 0; }   /* (voice.c: the voices the last block rendered) */
static uint32_t no_panmod;                              /* not 0: load() takes these pan routes out (bit k: matrix slot k + 1's AMOUNT 0 if it goes to a pan; bit 8 the LFO 2 DEST MULTI FILTER PAN card, bit 9
                                                         * the CYCLE PAN card): the render without that pan movement */
static void zero_panmod(void);
static void load(uint32_t k)
{
    fresh();
    cur = k;
    host_ds_load(&trk[0], 0, k);
    if (no_panmod)
        zero_panmod();
    (void)morph_ready(0);                               /* (End B as the fader's first use makes it: the sound's seed and AMOUNT. morph_rebase alone does nothing on a part whose morph was never used) */
}
static void run(const uint8_t *notes, int nn, int vel, int fader, uint32_t cap, int noff, int nb)
{
    int32_t o[2 * CTL];
    int b, k, i;
    int64_t lv;
    int32_t mg, tdl = 0, tdr = 0, tel = 0, ter = 0;
    load(cur);
    mg = MIX_MG();                                      /* (after the load: fresh sets the unit's level) */
    mph[0].fader = (uint8_t)fader;
    bnotes[0] = bnotes1[0] = (uint8_t)cap;              /* (the allocator's cap, as budget_poll leaves it on the unit) */
    sl = sr = 0;
    opk = xpk = ppk = tpk = ipk = 0;
    tidle = -1;
    memset(el, 0, sizeof el); memset(er, 0, sizeof er);
    for (b = 0; b < NIDLE; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2 * CTL; i++)
            if (llabs((int64_t)o[i]) > ipk) ipk = llabs((int64_t)o[i]);
    }
    for (k = 0; k < nn; k++)
        trk_note_on(&trk[0], notes[k], (uint32_t)vel);
    for (b = 0; b < nb; b++) {
        if (b == noff)
            for (k = 0; k < nn; k++)
                trk_note_off(&trk[0], notes[k]);
        mix_block(o, CTL);
        if (b >= noff && tidle < 0 && !voices_on())
            tidle = (double)(b - noff) * CTL / 44100.0;
        lv = LV4(part_lvl(&trk[0]));
        for (i = 0; i < CTL; i++) {
            int64_t xl = llabs(((int64_t)pscr[0].buf[i] * lv) >> 10), xr = llabs(((int64_t)pscr[0].bufR[i] * lv) >> 10);
            int64_t ml = ((int64_t)(mix_l[i] + wet[i]) * LV4(mg)) >> 10, mr = ((int64_t)(mix_r[i] + wet[i]) * LV4(mg)) >> 10;
            int64_t pl = llabs((int64_t)dc_block((int32_t)ml, &tdl, &tel)), pr = llabs((int64_t)dc_block((int32_t)mr, &tdr, &ter));   /* the limiter's input: master_out's DC blocker, run on a copy of its state */
            int64_t a = llabs((int64_t)o[2 * i]), c = llabs((int64_t)o[2 * i + 1]);
            if (xl > xpk) xpk = xl;
            if (xr > xpk) xpk = xr;
            if (pl > ppk) ppk = pl;
            if (pr > ppk) ppk = pr;
            if (a > opk) opk = a;
            if (c > opk) opk = c;
            el[b] += (double)o[2 * i] * o[2 * i];
            er[b] += (double)o[2 * i + 1] * o[2 * i + 1];
        }
    }
    for (b = 0; b < 30000 && voices_on(); b++)          /* (the release to its end: every voice idle, at most 22 s) */
        mix_block(o, CTL);
    if (tidle < 0)
        tidle = (double)(nb - noff + b) * CTL / 44100.0;
    for (b = 0; b < NSETTLE; b++)                       /* (master_out's DC blocker, about 2 Hz, lets go of the held sound's offset) */
        mix_block(o, CTL);
    for (b = 0; b < NTAIL; b++) {                       /* then 0.1 s of what the idle part gives */
        mix_block(o, CTL);
        for (i = 0; i < 2 * CTL; i++)
            if (llabs((int64_t)o[i]) > tpk) tpk = llabs((int64_t)o[i]);
    }
    {
        double wl = 0, wr = 0, best = -1;
        for (b = 0; b < noff; b++) {
            wl += el[b]; wr += er[b];
            if (b >= NW) { wl -= el[b - NW]; wr -= er[b - NW]; }
            if (b >= NW - 1 && wl + wr > best) { best = wl + wr; sl = wl; sr = wr; }
        }
    }
}
/* the L / R balance of a held note: 10 log10(L / R) in 70 ms windows from the note on (an envelope's route moves the pan in the attack) into x[], windows quieter than 50 dB
 * under the loudest marked absent (1e9); returns the number of windows */
#define NBW 96
static int balance(int noff, double *x)
{
    double top = 0;
    int b, j, n = 0;
    for (b = 0; b + NBW <= noff; b += NBW) {
        double l = 0, r = 0;
        for (j = 0; j < NBW; j++) { l += el[b + j]; r += er[b + j]; }
        if (l + r > top) top = l + r;
    }
    for (b = 0; b + NBW <= noff; b += NBW) {
        double l = 0, r = 0;
        for (j = 0; j < NBW; j++) { l += el[b + j]; r += er[b + j]; }
        x[n++] = l + r < top * 1e-5 || l <= 0 || r <= 0 ? 1e9 : 10.0 * log10(l / r);
    }
    return n;
}
static const char *const LCAT[9] = {"-", "BASS", "LEAD", "PAD", "PLUCK", "KEYS", "PERC", "FX", "OTHER"};   /* (category.c CAT_NAME: not in this build) */
static double db(double s) { return 10.0 * log10(s / ((double)NW * CTL) / (32767.0 * 32767.0) + 1e-20); }
static double clip_loss(int64_t pk) { double x = (double)pk / 32768.0; return x < 1e-6 ? 0 : 20.0 * log10(x / tanh(x)); }   /* the soft clip (tanh) at the input peak, dB */

/* ---- the coverage matrix: the predicates read the STORED values of a loaded sound (trk[0].p, its extension slots, its matrix slots, its End B seed) ---- */
#define XS(k) XV(&trk[0], k)
#define PS(i) (trk[0].p[i])
static int lvl_o1(void) { return XS(X_O1LVL) > 0; }
static int lvl_o2(void) { return XS(X_O2LVL) > 0; }
static int model_on(int m) { return (lvl_o1() && XS(X_O1MODEL) == m) || (lvl_o2() && XS(X_O2MODEL) == m); }
static int saws(int o) { return 1 + (XS(o ? X_O2P2 : X_O1P2) * 6 + 63) / 127; }   /* (lrry_param.c: SUPER's P2) */
static int super_on(int o) { return o ? lvl_o2() && XS(X_O2MODEL) == MO_SUPER : lvl_o1() && XS(X_O1MODEL) == MO_SUPER; }
static int super_pair(int o) { return super_on(o) && XS(o ? X_O2P3 : X_O1P3) > 0 && saws(o) > 1; }   /* a stereo pair: its PAN is a balance */
static int twin(void) { return PS(P_TWTYPE) != 0; }
static int multi_runs(void) { return PS(P_FREEA) < UF_STEPS || PS(P_FREEC) != 0; }   /* (MODE LP with CUT open is not run; KEY TRK aside) */
static int da(void) { return XS(X_DATYPE) != 0; }
static int dbu(void) { return XS(X_DBTYPE) != 0; }
static int is_pan_dst(int d) { return d == MD_PAN || d == MD_NZPAN || d == MD_RGPAN || d == MD_DAPAN || d == MD_DBPAN || (d >= MD_O1PAN && d <= MD_SUBPAN); }
static int route_to(int src, int dst)                   /* a matrix slot with this source (0: any) and this destination (-1: any pan), AMOUNT not 0 */
{
    int k;
    for (k = 0; k < 8; k++) {
        int s = PS(P_M1SRC + 3 * k), d = PS(P_M1SRC + 3 * k + 1), a = PS(P_M1SRC + 3 * k + 2);
        if (s && a && (!src || s == src) && (dst < 0 ? is_pan_dst(d) : d == dst))
            return 1;
    }
    return 0;
}
static void zero_panmod(void)
{
    int k;
    for (k = 0; k < 8; k++)
        if (no_panmod >> k & 1u && is_pan_dst(PS(P_M1SRC + 3 * k + 1)))
            PS(P_M1SRC + 3 * k + 2) = 0;
    if (no_panmod >> 8 & 1u)
        XS(X_L2FAPAN) = 0;
    if (no_panmod >> 9 & 1u)
        XS(X_CYPAN) = 0;
}
static uint32_t pan_routes(void)                        /* the sound's pan routes, as no_panmod's bits */
{
    uint32_t m = 0, k;
    for (k = 0; k < 8; k++)
        if (PS(P_M1SRC + 3 * k) && PS(P_M1SRC + 3 * k + 2) && is_pan_dst(PS(P_M1SRC + 3 * k + 1)))
            m |= 1u << k;
    return m | (XS(X_L2FAPAN) ? 1u << 8 : 0u) | (XS(X_CYPAN) ? 1u << 9 : 0u);
}
static int pan_by_lfo(void) { return route_to(MS_LFO, -1) || route_to(MS_LFO2, -1) || XS(X_L2FAPAN) != 0; }
static int pan_by_cycle(void) { return route_to(MS_CYCLE, -1) || XS(X_CYPAN) != 0; }
static int pan_by_env(void) { return route_to(MS_ENV, -1) || route_to(MS_ENV2, -1); }
static int pan_moves(void) { return pan_by_lfo() || pan_by_cycle() || pan_by_env(); }
static int npans(void)
{
    return (lvl_o1() && XS(X_O1PAN)) + (lvl_o2() && XS(X_O2PAN)) + (XS(X_SUBLVL) && XS(X_SUBPAN)) + (XS(X_NZLVL) && XS(X_NZPAN)) + (XS(X_RGLVL) && XS(X_RGPAN)) +
           (multi_runs() && XS(X_FAPAN) != 0) + (twin() && XS(X_FBPAN)) + (da() && XS(X_DAPAN)) + (dbu() && XS(X_DBPAN));
}
static int dense(void)                                  /* everything at once: a SUPERSAW, both filters, both dirt units, at least 6 pans set, pans moved two ways */
{
    return (super_on(0) || super_on(1)) && multi_runs() && twin() && da() && dbu() && npans() >= 6 && pan_by_lfo() + pan_by_cycle() + pan_by_env() >= 2;
}
static int super7_s(int o, int lv) { return super_on(o) && saws(o) == 7 && XS(o ? X_O2P1 : X_O1P1) == 50 && XS(o ? X_O2P3 : X_O1P3) == 100 && XS(o ? X_O2LVL : X_O1LVL) == lv; }
static int stress_env(void) { return PS(P_ATK) == 0 && PS(P_DEC) == 0 && PS(P_SUS) == 127 && PS(P_REL) == 30 && PS(P_KTRK) == 0; }
static int rec_s2(void)
{
    return stress_env() && super7_s(0, 90) && XS(X_O2MODEL) == MO_SAW && XS(X_O2LVL) == 80 && XS(X_O2COARSE) == -12 && PS(P_FREEA) == 60 && PS(P_FREEB) == 40 && PS(P_FREED) == 2 &&
           PS(P_TWTYPE) == 5 && PS(P_CUT2) == 55 && PS(P_RES2) == 70 && XS(X_DATYPE) == 1 && PS(P_DRV2) == 70 && XS(X_DBTYPE) == 4 && XS(X_DBDRV) == 50 && XS(X_DBLVL) == 90 && XS(X_DSER) == 0;
}
static int rec_s5(void)
{
    return stress_env() && super7_s(0, 90) && XS(X_O2MODEL) == MO_SAW && XS(X_O2LVL) == 80 && XS(X_O2FINE) == -9 && PS(P_FREEA) == 30 && PS(P_FREEB) == 40 && PS(P_FREED) == 2 &&
           PS(P_E2ATK) == 0 && PS(P_E2DEC) == 100 && PS(P_E2SUS) == 0 && PS(P_E2REL) == 40 && PS(P_E2FLT) == 63 && PS(P_TWTYPE) == 5 && PS(P_CUT2) == 60 && PS(P_RES2) == 50 &&
           XS(X_DATYPE) == 1 && PS(P_DRV2) == 60 && XS(X_DBTYPE) == 0;
}
static int rec_s6(void)
{
    return stress_env() && XS(X_O1MODEL) == MO_FM && XS(X_O1P1) == 40 && XS(X_O1P2) == 60 && XS(X_O1LVL) == 90 && XS(X_O2MODEL) == 4 && XS(X_O2LVL) == 70 && XS(X_RGLVL) == 40 &&
           XS(X_NZLVL) == 20 && XS(X_SUBLVL) == 40 && PS(P_FREEA) == 65 && PS(P_FREED) == 2 && PS(P_TWTYPE) == 1 && PS(P_CUT2) == 70 && XS(X_DATYPE) == 1 && PS(P_DRV2) == 50 &&
           XS(X_DBTYPE) == 3 && XS(X_DBDRV) == 40 && XS(X_DBLVL) == 90;
}
static int own_b(void) { const ds_sound_t *s = ds_get(0, cur); return s && s->bseed != 0; }
/* (the row predicates as small functions: C has no lambdas) */
#define ROW(f, expr) static int f(void) { return (expr); }
ROW(r_mlp, multi_runs() && PS(P_FREEC) == 0)  ROW(r_mbp, PS(P_FREEC) == 64)  ROW(r_mhp, PS(P_FREEC) == 127)
ROW(r_mmid, PS(P_FREEC) != 0 && PS(P_FREEC) != 64 && PS(P_FREEC) != 127)
ROW(r_m12, multi_runs() && PS(P_FREED) == 1)  ROW(r_m24, multi_runs() && PS(P_FREED) == 2)
ROW(r_tlp, PS(P_TWTYPE) == 1) ROW(r_tbp, PS(P_TWTYPE) == 2) ROW(r_thp, PS(P_TWTYPE) == 3) ROW(r_tno, PS(P_TWTYPE) == 4) ROW(r_tla, PS(P_TWTYPE) == 5) ROW(r_tco, PS(P_TWTYPE) == 6)
ROW(r_lon, twin() && XS(X_LINK) == 1)  ROW(r_loff, twin() && XS(X_LINK) == 0)  ROW(r_ser, twin() && XS(X_ROUTE) == 0)  ROW(r_par, twin() && XS(X_ROUTE) == 1)
ROW(r_po1, lvl_o1() && XS(X_O1PAN) != 0) ROW(r_po2, lvl_o2() && XS(X_O2PAN) != 0) ROW(r_psub, XS(X_SUBLVL) && XS(X_SUBPAN)) ROW(r_pnz, XS(X_NZLVL) && XS(X_NZPAN))
ROW(r_prg, XS(X_RGLVL) && XS(X_RGPAN)) ROW(r_pfa, multi_runs() && XS(X_FAPAN) != 0) ROW(r_pfb, twin() && XS(X_FBPAN)) ROW(r_pda, da() && XS(X_DAPAN)) ROW(r_pdb, dbu() && XS(X_DBPAN))
ROW(r_ppart, XS(X_CYPAN) != 0 || route_to(0, MD_PAN))
ROW(r_mlfo, pan_by_lfo()) ROW(r_mcyc, pan_by_cycle()) ROW(r_menv, pan_by_env()) ROW(r_mslot, route_to(0, -1))
ROW(r_s7, (super_on(0) && saws(0) == 7) || (super_on(1) && saws(1) == 7)) ROW(r_sfew, (super_on(0) && saws(0) < 7) || (super_on(1) && saws(1) < 7))
ROW(r_sdet_lo, (super_on(0) && XS(X_O1P1) < 32) || (super_on(1) && XS(X_O2P1) < 32)) ROW(r_sdet_hi, (super_on(0) && XS(X_O1P1) >= 47) || (super_on(1) && XS(X_O2P1) >= 47))
ROW(r_ssp0, (super_on(0) && XS(X_O1P3) == 0) || (super_on(1) && XS(X_O2P3) == 0))
ROW(r_sleft, (super_pair(0) && XS(X_O1PAN) < 0) || (super_pair(1) && XS(X_O2PAN) < 0)) ROW(r_sright, (super_pair(0) && XS(X_O1PAN) > 0) || (super_pair(1) && XS(X_O2PAN) > 0))
ROW(r_opp, lvl_o1() && lvl_o2() && XS(X_O1PAN) * XS(X_O2PAN) < 0)
ROW(r_das, XS(X_DATYPE) == 1) ROW(r_dah, XS(X_DATYPE) == 2) ROW(r_daf, XS(X_DATYPE) == 3) ROW(r_daz, XS(X_DATYPE) == 4) ROW(r_dac, XS(X_DATYPE) == 5)
ROW(r_dbs, XS(X_DBTYPE) == 1) ROW(r_dbh, XS(X_DBTYPE) == 2) ROW(r_dbf, XS(X_DBTYPE) == 3) ROW(r_dbz, XS(X_DBTYPE) == 4) ROW(r_dbc, XS(X_DBTYPE) == 5)
ROW(r_dpar, da() && dbu() && XS(X_DSER) == 0) ROW(r_dser, da() && dbu() && XS(X_DSER) == 1) ROW(r_dpan, da() && dbu() && XS(X_DAPAN) != XS(X_DBPAN))
ROW(r_dense, dense()) ROW(r_s2, rec_s2()) ROW(r_s5, rec_s5()) ROW(r_s6, rec_s6())
ROW(r_saw, model_on(MO_SAW)) ROW(r_tri, model_on(1)) ROW(r_sine, model_on(2)) ROW(r_pulse, model_on(MO_PULSE)) ROW(r_sync, model_on(4)) ROW(r_fm, model_on(MO_FM)) ROW(r_fold, model_on(MO_FOLD))
ROW(r_super, model_on(MO_SUPER))
ROW(r_sub2, XS(X_SUBLVL) && XS(X_SUBOCT) == 0) ROW(r_sub1, XS(X_SUBLVL) && XS(X_SUBOCT) == 1) ROW(r_sub0, XS(X_SUBLVL) && XS(X_SUBOCT) == 2)
ROW(r_noise, XS(X_NZLVL) > 0) ROW(r_ring, XS(X_RGLVL) > 0) ROW(r_morph, own_b())
/* CLAIM: the patches the document's coverage matrix marks for each row (factory index list, -1 ends it). The test fails on any difference, either way. (Phase 56: every patch's pans move,
 * by its own LFO rates, CYCLE and envelopes: rows (b) part PAN and (c) follow) */
#define L(...) {__VA_ARGS__, -1}
static const struct { const char *grp, *row; int (*f)(void); int claim[17]; } COV[] = {
    {"(a) MULTI FILTER", "MODE LP (0), the filter running", r_mlp, L(1, 2, 8, 9, 10)},
    {"(a) MULTI FILTER", "MODE BP (64)", r_mbp, L(7)},
    {"(a) MULTI FILTER", "MODE HP (127)", r_mhp, L(13)},
    {"(a) MULTI FILTER", "MODE between (LP>BP, BP>HP)", r_mmid, L(11, 12, 14)},
    {"(a) MULTI FILTER", "SLOPE 12 dB", r_m12, L(1, 12, 13, 14)},
    {"(a) MULTI FILTER", "SLOPE 24 dB", r_m24, L(2, 7, 8, 9, 10, 11)},
    {"(a) TWIN FILTER", "TYPE LP", r_tlp, L(10, 13)},
    {"(a) TWIN FILTER", "TYPE BP", r_tbp, L(4)},
    {"(a) TWIN FILTER", "TYPE HP", r_thp, L(14)},
    {"(a) TWIN FILTER", "TYPE NOTCH", r_tno, L(11)},
    {"(a) TWIN FILTER", "TYPE LADDER", r_tla, L(7, 8, 9, 12)},
    {"(a) TWIN FILTER", "TYPE COMB", r_tco, L(5)},
    {"(a) TWIN FILTER", "LINK ON", r_lon, L(7, 11)},
    {"(a) TWIN FILTER", "LINK OFF", r_loff, L(4, 5, 8, 9, 10, 12, 13, 14)},
    {"(a) TWIN FILTER", "ROUTE SERIES", r_ser, L(7, 8, 9, 10, 11, 14)},
    {"(a) TWIN FILTER", "ROUTE PARALLEL", r_par, L(4, 5, 12, 13)},
    {"(b) pans (set off centre)", "OSC 1 PAN", r_po1, L(4, 6, 11, 12, 13, 14, 15)},
    {"(b) pans (set off centre)", "OSC 2 PAN", r_po2, L(3, 4, 6, 11, 12, 13, 14, 15)},
    {"(b) pans (set off centre)", "SUB PAN", r_psub, L(11, 12, 14)},
    {"(b) pans (set off centre)", "NOISE PAN", r_pnz, L(11, 14)},
    {"(b) pans (set off centre)", "RING PAN", r_prg, L(12, 14)},
    {"(b) pans (set off centre)", "MULTI FILTER PAN", r_pfa, L(11, 12, 13, 14)},
    {"(b) pans (set off centre)", "TWIN FILTER PAN", r_pfb, L(11, 12, 13, 14)},
    {"(b) pans (set off centre)", "DIRT A PAN", r_pda, L(11, 12, 13, 14)},
    {"(b) pans (set off centre)", "DIRT B PAN", r_pdb, L(11, 12, 13, 14)},
    {"(b) pans", "part PAN (moved: CYCLE card or matrix PAN)", r_ppart, L(0, 3, 5, 6, 7, 9, 10, 12, 14)},
    {"(c) pan modulation", "by an LFO (matrix or the LFO 2 DEST card)", r_mlfo, L(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)},
    {"(c) pan modulation", "by CYCLE (card or matrix)", r_mcyc, L(0, 1, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14)},
    {"(c) pan modulation", "by an envelope (AMP ENV or FILTER ENV, matrix)", r_menv, L(1, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14)},
    {"(c) pan modulation", "by a matrix slot", r_mslot, L(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)},
    {"(d) SUPERSAW", "7 saws", r_s7, L(1, 7, 8, 9)},
    {"(d) SUPERSAW", "fewer saws (2 to 5)", r_sfew, L(11, 12, 13, 14, 15)},
    {"(d) SUPERSAW", "small DETUNE (under 32)", r_sdet_lo, L(11, 12, 13, 14)},
    {"(d) SUPERSAW", "large DETUNE (47 and up)", r_sdet_hi, L(1, 7, 8, 9, 15)},
    {"(d) SUPERSAW", "SPREAD 0 (one mono signal: the pan law)", r_ssp0, L(15)},
    {"(d) SUPERSAW", "stereo pair panned left (balance)", r_sleft, L(11, 13, 15)},
    {"(d) SUPERSAW", "stereo pair panned right (balance)", r_sright, L(12, 14)},
    {"(d) two oscillators", "at opposite pans", r_opp, L(4, 6, 11, 12, 13, 15)},
    {"(e) DIRT A", "SOFT", r_das, L(2, 8, 9, 10)},
    {"(e) DIRT A", "HARD", r_dah, L(7, 14)},
    {"(e) DIRT A", "FOLD", r_daf, L(11)},
    {"(e) DIRT A", "FUZZ", r_daz, L(12)},
    {"(e) DIRT A", "CRUSH", r_dac, L(13)},
    {"(e) DIRT B", "SOFT", r_dbs, L(11, 13)},
    {"(e) DIRT B", "HARD", r_dbh, L(12)},
    {"(e) DIRT B", "FOLD", r_dbf, L(7, 10)},
    {"(e) DIRT B", "FUZZ", r_dbz, L(4, 8)},
    {"(e) DIRT B", "CRUSH", r_dbc, L(14)},
    {"(e) DIRT", "both units, ROUTE PARALLEL", r_dpar, L(8, 10, 11, 13, 14)},
    {"(e) DIRT", "both units, ROUTE SERIES", r_dser, L(7, 12)},
    {"(e) DIRT", "both units at different pans", r_dpan, L(11, 12, 13, 14)},
    {"(f) dense", "everything at once (SUPERSAW, both filters, both dirt units, 6+ pans, pans moved two ways)", r_dense, L(11, 12, 13, 14)},
    {"(f) stress", "S2 recipe (SUPERSAW + LADDER + two dirt units)", r_s2, L(8)},
    {"(f) stress", "S5 recipe (SWEEP)", r_s5, L(9)},
    {"(f) stress", "S6 recipe (FM / RING / NOISE)", r_s6, L(10)},
    {"(g) OSC MODEL", "SAW", r_saw, L(0, 1, 2, 6, 8, 9)},
    {"(g) OSC MODEL", "TRIANGLE", r_tri, L(5, 11)},
    {"(g) OSC MODEL", "SINE", r_sine, L(3)},
    {"(g) OSC MODEL", "PULSE", r_pulse, L(4, 6, 13, 14)},
    {"(g) OSC MODEL", "SYNC", r_sync, L(4, 10)},
    {"(g) OSC MODEL", "FM", r_fm, L(3, 7, 10)},
    {"(g) OSC MODEL", "WAVEFOLD", r_fold, L(12)},
    {"(g) OSC MODEL", "SUPERSAW", r_super, L(1, 7, 8, 9, 11, 12, 13, 14, 15)},
    {"(h) SUB", "OCTAVE -2", r_sub2, L(12)},
    {"(h) SUB", "OCTAVE -1", r_sub1, L(7, 10, 11, 13)},
    {"(h) SUB", "OCTAVE 0", r_sub0, L(14)},
    {"(h) NOISE", "LEVEL above 0", r_noise, L(5, 7, 10, 11, 14)},
    {"(h) RING", "LEVEL above 0", r_ring, L(7, 10, 12, 14)},
    {"(i) MORPH", "its own End B (a chosen seed, the category's AMOUNT)", r_morph, L(1, 2, 5, 11, 12)},
};
#define NCOV (sizeof COV / sizeof COV[0])

#ifndef LRRY_SOUNDS_LIB                                 /* (a scratch tool may include this file for its renders, with its own main) */
int main(int argc, char **argv)
{
    static const uint8_t one[1] = {48}, chord[8] = {36, 43, 48, 52, 55, 60, 64, 67}, chord4[4] = {48, 52, 55, 60};   /* (C3 E3 G3 C4) */
    uint32_t n = ds_count(0), k, j;
    uint32_t silent = 0, ovf = 0, over4 = 0, clipd = 0, noisy = 0, notidle = 0, nomove = 0, far = 0, atcap = 0;
    double lv1[256], ref, worst4 = -99, worst8 = -99;
    (void)argc; (void)argv;
    printf("lrry_sounds: %u factory sounds on engine 0; power-on sound %u (%s). Rendered at the unit's mix level (MASTER_USB_REF). Budget: an ESTIMATE (budget.c), not measured on the unit\n",
           n, TRK_DEF[0][1], ds_name(0, TRK_DEF[0][1]));
    ck("the factory bank: 128 sounds, the power-on sound is one of them", n == 128u && TRK_DEF[0][1] < n);   /* (2026-11-13: the firmware ships the 128-sound bank, tools/lrry_sounds_128.py; was the cloud's 16-sound test set) */
    printf("lrry_sounds: %-2s %-12s %-5s | voice %%  1 core 2 cores | C3 v100: L dB  R dB  pan dB (the weakest route) | all notes v127, fader A: in-peak clip dB out-peak factor | fader B: in-peak clip dB | to idle s, tail, before\n", "#", "", "cat");
    for (k = 0; k < n && k < 256u; k++) {
        bd_t b;
        uint32_t n1[2], n2[2], c;
        double a1, b1, sw, ti;
        int64_t pA, pB, oA, xA, xB, t1, t2, i1, qA, qB, xq;
        load(k);
        bd_build(&b);
        budget_solve2(&b, n1, 0);
        budget_solve2(&b, n2, 1);
        c = BD_U(b.p[0].eng + b.p[0].filt + b.p[0].ext);
        {   /* the pan routes heard: the held note's L / R balance with all of them, and without each one alone (6 s), the largest difference of a 70 ms window; sw: the smallest of them */
            static double xw[128], xo[128];
            uint32_t pr, bit;
            int nw, w;
            load(k);
            pr = pan_routes();
            run(one, 1, 100, 0, n2[0], 8250, 8250 + 400);
            nw = balance(8250, xw);
            sw = pr ? 1e9 : 0;
            for (bit = 0; bit < 10u; bit++) {
                double d = 0;
                if (!(pr >> bit & 1u))
                    continue;
                no_panmod = 1u << bit;
                run(one, 1, 100, 0, n2[0], 8250, 8250 + 400);
                no_panmod = 0;
                (void)balance(8250, xo);
                for (w = 0; w < nw; w++)
                    if (xw[w] < 1e8 && xo[w] < 1e8 && fabs(xw[w] - xo[w]) > d)
                        d = fabs(xw[w] - xo[w]);
                printf("lrry_sounds: %2u %-12s pan route %s: moves the held note's L / R balance by up to %.2f dB\n", k, ds_name(0, k),
                       bit < 8u ? (const char *[]){"matrix slot 1", "matrix slot 2", "matrix slot 3", "matrix slot 4", "matrix slot 5", "matrix slot 6", "matrix slot 7", "matrix slot 8"}[bit] : bit == 8u ? "LFO 2 DEST MULTI FILTER PAN" : "CYCLE PAN", d);
                if (d < sw)
                    sw = d;
            }
        }
        run(one, 1, 100, 0, n2[0], NOFF, NB);
        a1 = db(sl); b1 = db(sr); ti = tidle;
        lv1[k] = 10.0 * log10((pow(10, a1 / 10) + pow(10, b1 / 10)) / 2);
        silent += a1 < -60 && b1 < -60;
        t1 = tpk; i1 = ipk;
        run(chord, 8, 127, 0, n2[0], NOFF, NB);
        pA = ppk; oA = opk; xA = xpk; t2 = tpk;
        run(chord, 8, 127, 127, n2[0], NOFF, NB);
        pB = ppk; xB = xpk;
        run(chord4, 4, 127, 0, n2[0], NOFF, NB);        /* (the 4-note chord, velocity 127, the voices capped at the two-core number) */
        qA = ppk; xq = xpk;
        run(chord4, 4, 127, 127, n2[0], NOFF, NB);
        qB = ppk; xq = xq > xpk ? xq : xpk;
        ovf += xq >= 524288 || qA >= 2147483647LL || qB >= 2147483647LL || opk >= 32768;
        {
            int64_t q = qA > qB ? qA : qB, e = pA > pB ? pA : pB;
            double d4 = 20.0 * log10((double)(q > 0 ? q : 1) / LIM_T), d8 = 20.0 * log10((double)(e > 0 ? e : 1) / LIM_T);   /* dB against the limiter's threshold: under 0 below it */
            if (d4 > worst4) worst4 = d4;
            if (d8 > worst8) worst8 = d8;
            over4 += d4 > KNEE4_MAX;
            printf("lrry_sounds: %2u %-12s limiter input, against LIM_T %d: 4-note chord A %6lld B %6lld (%+6.1f dB), 8-note chord (capped) %+6.1f dB%s\n", k, ds_name(0, k), LIM_T, (long long)qA, (long long)qB, d4, d8,
                   d8 > 0 ? " (reaches the limiter)" : "");
        }
        ovf += xA >= 524288 || xB >= 524288 || pA >= 2147483647LL || pB >= 2147483647LL;
        clipd += oA >= 32768 || opk >= 32768;
        noisy += t1 > 2 || t2 > 2 || tpk > 2;
        notidle += i1 != 0;
        printf("lrry_sounds: %2u %-12s %-5s | %5.1f %6u %7u |      %6.1f %5.1f %5.1f |              %6lld %6.2f %7lld %7lld |       %6lld %6.2f | %5.2f %4lld %4lld\n",
               k, ds_name(0, k), LCAT[ds_cat_of(0, k) % 9u], c / 100.0, n1[0], n2[0], a1, b1, sw, (long long)pA, clip_loss(pA), (long long)oA, (long long)xA,
               (long long)pB, clip_loss(pB), ti, (long long)(t1 > t2 ? (t1 > tpk ? t1 : tpk) : (t2 > tpk ? t2 : tpk)), (long long)i1);
        load(k);
        if (pan_moves() && sw < 1.0)
            nomove++, printf("lrry_sounds: %s: a stored pan route moves the held note's L / R balance by only %.2f dB\n", ds_name(0, k), sw);
    }
    printf("lrry_sounds: INFO (the owner's patches, not checked): %u sound(s) under -60 dB on a held C3 (short hits: SPARK SHOWER, NOISE HIT)\n", silent);
    ck("all notes held at velocity 127, fader at A and at B: no wrap in the part mix (factor under 2^19), the master's input inside int32", ovf == 0);
    printf("lrry_sounds: limiter input against LIM_T: worst 4-note chord %+.1f dB, worst 8-note chord (capped) %+.1f dB\n", worst4, worst8);
    ck("4-note chord at velocity 127, fader at A and at B: the master's input at most KNEE4_MAX dB over the limiter's threshold (LIM_T 18000), and never wraps (part mix under 2^19, output under 32768)", over4 == 0);
    ck("8-note chord (capped at the two-core number), fader at A and at B: the output peak (after the limiter and the soft clip) stays under 32768, the limiter holds what is above LIM_T", clipd == 0);
    ck("silence when idle: no output before the first note", notidle == 0);
    ck("silence when idle: 2 s after the last voice ended (the DC blocker settled), at most +-2 (of 32767) at the output, every render", noisy == 0);
    printf("lrry_sounds: INFO (the owner's patches, not checked): %u sound(s) store a pan route that moves the held note's L / R balance by under 1 dB (listed above)\n", nomove);
    ref = lv1[0];
    for (k = 0; k < n && k < 256u; k++) {               /* (2026-11-13: the levels are the owner's patches: printed, not checked; the old set's level target, its STEREO TEST patch and its coverage table are gone with it) */
        load(k);
        atcap += PS(P_TRIM) == 127;
        printf("lrry_sounds: %3u %-12s held C3 %6.1f dB, %+5.1f dB against sound 0\n", k, ds_name(0, k), lv1[k], lv1[k] - ref);
    }
    printf("lrry_sounds: %u sound(s) at the TRIM cap (127)\n", atcap);
    (void)far; (void)j; (void)worst4; (void)worst8;
    printf("lrry_sounds: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
#endif
