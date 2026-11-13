/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY PATCH LINT, host harness (run by tools/patch_lint.py, which builds it in a temporary folder; nothing is installed or written into the repo).
 * Loads every factory sound as the unit does (hostsim.c host_ds_load, the bank = tools/lrry_sounds_128.py through gen_lrry_sounds.py), builds the real budget.c items of the part
 * (bd_build) and prints one line per sound: the model's items and the stored feature values the model reads, plus the model's own counter (budget_solve2) on one and two cores.
 * Then a line "REF" for the reference all-active voice, and "CONST" with the model's constants. Python does the rest (variants, rules, report).
 * Build (done by the tool):  cc -O2 -w -DHOST_MIX_FOLLOWS_MASTER=1 -DLRRY_TEST_MODE=1 -I<gen> -Ifirmware/src -Itests -DNPART=1 -DNSHOWN=1 -DLRRY_VERSION_SHOWN='"x"' tools/patch_lint_host.c -lm */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "lrry_scn.h"

static void fresh(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    memset(lrry_vs, 0, sizeof lrry_vs);
    memset(&pscr, 0, sizeof pscr);
    memset(mph, 0, sizeof mph);
    for (i = 0; i < NLIVE; i++)
        pan_gv[i] = mx_ok[i] = 0;
    dc_l = dc_r = dce_l = dce_r = 0;
    lim_env = lim_env_r = LIM_T;
    song.master_q12 = MASTER_USB_REF;
}

/* the line of the part on trk[0]: items (raw budget units), the counter, the features exactly as bd_lrry_voice reads them */
static void line(const char *tag, uint32_t browse, uint32_t k, const char *cat, const char *name)
{
    bd_t b;
    uint32_t n1[2], n2[2], i, fbmod = 0, nzx, o2, nmx = 0, nfx = 0, pm = 0, npm = 0;
    const int16_t *p = trk[0].p, *x = &p[XW_BASE];
    uint32_t sl;
    bd_build(&b);
    budget_solve2(&b, n1, 0);
    budget_solve2(&b, n2, 1);
    sl = bd_slope(&ENG_ANALOG, p);
    if (x[X_KTRK2] || x[X_ENV2] || x[X_LFO1] || x[X_LFO2])
        fbmod += 1;
    for (i = 0; i < NMSLOT; i++)
        if (p[MSLOT_ID(i)] && p[MSLOT_ID(i) + 1u] == MD_CUT2 && p[MSLOT_ID(i) + 2u]) {
            fbmod += 1;   /* (budget.c adds FBMOD once for the fixed depths and once more for a CUT2 route) */
            break;
        }
    for (i = 0; i < NMSLOT; i++)
        if (p[MSLOT_ID(i)] && p[MSLOT_ID(i) + 1u] && p[MSLOT_ID(i) + 2u]) {
            int d = p[MSLOT_ID(i) + 1u];
            nmx++;
            if (d == MD_PAN || d == MD_NZPAN || d == MD_RGPAN || d == MD_DAPAN || d == MD_DBPAN || (d >= MD_O1PAN && d <= MD_SUBPAN))
                pm |= 1u << (d & 31);
        }
    {   /* the fixed routes (mod.c table): each nonzero depth is a route; LFO 2 to filter A PAN and CYCLE to OSC 1 PAN move a pan */
        static const int FX_ID[] = {X_E2DADRV, X_EDBDRV, X_CYCUT2, X_CYPAN, X_CYPIT, X_L2RES2, X_L2FAPAN, X_L2BLD};
        for (i = 0; i < sizeof FX_ID / sizeof FX_ID[0]; i++)
            nfx += x[FX_ID[i]] != 0;
        if (x[X_L2FAPAN]) pm |= 1u << (MD_FAPAN & 31);
        if (x[X_CYPAN]) pm |= 1u << (MD_O1PAN & 31);
    }
    for (i = 0; i < 32u; i++)
        npm += pm >> i & 1u;
    o2 = x[X_O2LVL] > 0 || x[X_RGLVL] > 0;
    nzx = x[X_NZLVL] > 0;
    printf("%s|%u|%u|%s|%s|eng=%u|filt=%u|ext=%u|fx=%u|bst=%u|bst0=%u|idle=%u|gfx=%u|n1=%u|n2=%u|cap=%u|"
           "nmx=%u|nfx=%u|npm=%u|o1m=%d|o1p2=%d|o2on=%u|o2m=%d|o2p2=%d|o2lvl=%d|nz=%u|rg=%d|sl=%u|tw=%d|fbmod=%u|da=%d|db=%d|sub=%d|ms=%u\n",
           tag, browse, k, cat, name, b.p[0].eng, b.p[0].filt, b.p[0].ext, b.p[0].ins + b.p[0].sat + b.p[0].eq + b.p[0].misc + b.p[0].base, b.p[0].bst, b.p[0].bst0, b.idle, b.fx,
           n1[0], n2[0], b.p[0].cap, nmx, nfx, npm, x[X_O1MODEL], x[X_O1P2], o2, x[X_O2MODEL], x[X_O2P2], x[X_O2LVL], nzx, x[X_RGLVL], sl, p[P_TWTYPE], fbmod, x[X_DATYPE], x[X_DBTYPE], x[X_SUBLVL],
           b.p[0].misc);
}

static void ref_voice(int moving)
{
    track_t *t;
    fresh();
    t = &trk[0];
    t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 30; t->p[P_LEVEL] = 112; t->p[P_TRIM] = 112; t->p[P_KTRK] = 0;
    scn_x(t, X_O1MODEL, 0); scn_x(t, X_O1LVL, 100); scn_x(t, X_O1PAN, -25);
    scn_x(t, X_O2MODEL, 0); scn_x(t, X_O2LVL, 100); scn_x(t, X_O2COARSE, 7); scn_x(t, X_O2FINE, 9); scn_x(t, X_O2PAN, 25);
    scn_x(t, X_SUBLVL, 100); scn_x(t, X_SUBPAN, 10);
    scn_x(t, X_NZLVL, 40); scn_x(t, X_NZPAN, -15); scn_x(t, X_RGLVL, 40); scn_x(t, X_RGPAN, 15);
    t->p[P_FREEA] = 40; t->p[P_FREEB] = 50; t->p[P_FREEC] = 64; t->p[P_FREED] = 1; scn_x(t, X_FAPAN, -30);
    t->p[P_TWTYPE] = TT_LP; t->p[P_CUT2] = 50; t->p[P_RES2] = 30; scn_x(t, X_FBPAN, 30);
    scn_x(t, X_ENV2, 40); scn_x(t, X_LFO1, 30);
    t->p[P_E2FLT] = 30;
    scn_x(t, X_DATYPE, 1); t->p[P_DRV2] = 40; scn_x(t, X_DAPAN, -35); scn_x(t, X_DALVL, 100);
    scn_x(t, X_DBTYPE, 1); scn_x(t, X_DBDRV, 40); scn_x(t, X_DBPAN, 35); scn_x(t, X_DBLVL, 100);
    /* steady: 4 matrix routes (cutoffs, dirt levels). moving: all 8 slots on pan and cutoff targets, plus the fixed routes LFO 2 to filter A PAN and CYCLE to OSC 1 PAN: 9 pans move */
    t->p[MSLOT_ID(0)] = MS_LFO;  t->p[MSLOT_ID(0) + 1] = MD_CUT2;  t->p[MSLOT_ID(0) + 2] = 40;
    t->p[MSLOT_ID(1)] = MS_ENV2; t->p[MSLOT_ID(1) + 1] = MD_CUT;   t->p[MSLOT_ID(1) + 2] = 30;
    t->p[MSLOT_ID(2)] = MS_LFO2; t->p[MSLOT_ID(2) + 1] = MD_DALVL; t->p[MSLOT_ID(2) + 2] = 30;
    t->p[MSLOT_ID(3)] = MS_LFO2; t->p[MSLOT_ID(3) + 1] = MD_DBLVL; t->p[MSLOT_ID(3) + 2] = 30;
    if (moving) {
        t->p[MSLOT_ID(1) + 1] = MD_DAPAN; t->p[MSLOT_ID(2) + 1] = MD_DBPAN; t->p[MSLOT_ID(3) + 1] = MD_NZPAN;
        t->p[MSLOT_ID(4)] = MS_ENV2;  t->p[MSLOT_ID(4) + 1] = MD_RGPAN;  t->p[MSLOT_ID(4) + 2] = 30;
        t->p[MSLOT_ID(5)] = MS_CYCLE; t->p[MSLOT_ID(5) + 1] = MD_FBPAN;  t->p[MSLOT_ID(5) + 2] = 30;
        t->p[MSLOT_ID(6)] = MS_LFO;   t->p[MSLOT_ID(6) + 1] = MD_SUBPAN; t->p[MSLOT_ID(6) + 2] = 30;
        t->p[MSLOT_ID(7)] = MS_RAND;  t->p[MSLOT_ID(7) + 1] = MD_O2PAN;  t->p[MSLOT_ID(7) + 2] = 30;
        scn_x(t, X_L2FAPAN, 30); scn_x(t, X_CYPAN, 30); scn_x(t, X_CYCUT2, 20);
    }
    line(moving ? "REFMOVING" : "REFSTEADY", 0, 0, "REF", "ALL-ACTIVE");
}

int main(void)
{
    uint32_t k, n = ds_count(0), pos, i;
    static const char *const CATS[] = {"NONE", "BASS", "LEAD", "PAD", "PLUCK", "KEYS", "PERC", "FX", "OTHER"};
    printf("CONST|ctl=%u|h0=%u|h1=%u|j1=%u|j2=%u|lim1=%u|lim2=%u|us_per_unit=%.6f\n", BD_U(BD_LR_T(BD_LR_CTL)), BD_U(BD_LR_HO0), BD_U(BD_LR_HO1), BD_LR_J1, BD_LR_J2, LRRY_CP_LIM1, LRRY_CP_LIM,
           7.25 / 100.0);
    for (k = 0; k < n; k++) {
        const ds_sound_t *s = ds_get(0, k);
        pos = 0;
        for (i = 0; i < DS_N; i++)
            if (DS_LIST[i] == DS_ENG_AT[0] + k)
                pos = i + 1u;
        fresh();
        host_ds_load(&trk[0], 0, k);
        (void)morph_ready(0);
        line("SND", pos, k, CATS[s->cat < 9u ? s->cat : 8u], s->name);
    }
    ref_voice(0);
    ref_voice(1);
    return 0;
}
