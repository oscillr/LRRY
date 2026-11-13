/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 51 host check: the matrix's destination and source lists and the ENV / LFO / CYCLE destination pages, through the real firmware path (mix_block: mod_begin, lrry_decode, the voice).
 * Build (from lrry):  ../onepart.sh 1 1 lrry_matrix
 * Sections:  1 the lists: DESTINATION knob order (every number once, the groups together, L/RRY's group names), the not offered set, the reasons on a plain patch, the names in full words, the sources
 *            2 every card of AMP DEST, ENV 2 DEST, LFO 1 DEST, LFO 2 DEST, CYCLE ENV 1/2, CYCLE ENV 2/2, LFO 1 TRIG, LFO 2 TRIG moves the render (largest block difference of level L, level R or brightness)
 *            3 the locks: every offered destination has a lock target, TIMBRE's lock moves both oscillators' P1
 *            4 the pans at fast modulation: how far a pan's gain steps from one 32-sample block to the next (the stage pans and SUPER's pan step once a block; the source pans ramp each sample)
 * (Section 1 of lrry_morph_test.c routes every destination 1..63 and every source and checks the render moves.) */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int nfail, ncheck;
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_matrix: %-110s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}
static void fresh(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(lrry_vs, 0, sizeof lrry_vs);
    memset(lrry_pp, 0, sizeof lrry_pp);                 /* (2026-11-12: the pan slew's memory: a fresh part starts at its own pans) */
    memset(lrry_key_ok, 0, sizeof lrry_key_ok);
    memset(&pscr, 0, sizeof pscr);
    memset(mph, 0, sizeof mph);
    memset(mod_cc, 0, sizeof mod_cc);
    memset(mx_sl, 0, sizeof mx_sl);
    memset(lk_virt, 0, sizeof lk_virt);
    memset(lk_virt_n, 0, sizeof lk_virt_n);
    for (i = 0; i < NLIVE; i++)
        pan_gv[i] = mx_ok[i] = 0;
    mix_l[0] = 0;
    dc_l = dc_r = dce_l = dce_r = 0;
}
static void xset(track_t *t, uint32_t k, int v) { XV(t, k) = (int16_t)v; }
static void patch_plain(track_t *t)        /* as tests/lrry_fw_test.c: an instant attack, full sustain, mixer and TRIM at 112, KEY TRK 0, OSC 1 SAW, filter A open, PARALLEL */
{
    t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 30;
    t->p[P_LEVEL] = 112; t->p[P_TRIM] = 112; t->p[P_KTRK] = 0; t->p[P_PAN] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_FREEA] = UF_STEPS;
    XV(t, X_ROUTE) = 1;
    XV(t, X_LINK) = 0;
    xset(t, X_O1LVL, 100);
}

/* ---------------------------------------------------------------------------------------------------- 1 the lists */
static int word_ok(const char *w, size_t n)
{
    static const char *const OK[] = {"NONE", "PITCH", "MULTI", "CUTOFF", "TIMBRE", "AMP", "LEVEL", "PAN", "NOISE", "RING", "DIRT", "A", "B", "LFO", "1", "2", "RATE", "DEPTH",
                                     "OSC", "1/2", "KNOB", "3", "4", "PATCH", "COARSE", "FINE", "FILTER", "ENV", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "BALANCE", "RES", "MODE",
                                     "TWIN", "DRIVE", "BLEND", "SUB", "WIDTH", "RATIO", "DETUNE", "SAWS", "SPREAD", "INDEX", "AMOUNT", "TUNE", "FEEDBACK", "SPARE", 0};
    int i;
    for (i = 0; OK[i]; i++)
        if (strlen(OK[i]) == n && !strncmp(OK[i], w, n))
            return 1;
    return 0;
}
static int name_ok(const char *s, char *bad, size_t nb)   /* every word of s is a full word or an approved abbreviation (MOD ENV OSC LFO FX RES MIDI EQ), never PARAL */
{
    const char *p = s;
    if (strstr(s, "PARAL") && !strstr(s, "PARALLEL"))
        return snprintf(bad, nb, "PARAL"), 0;
    while (*p) {
        size_t n = strcspn(p, " \n");
        if (n && !word_ok(p, n))
            return snprintf(bad, nb, "%.*s", (int)n, p), 0;
        p += n;
        p += *p ? 1 : 0;
    }
    return 1;
}
static void sec_lists(void)
{
    uint8_t seen[64] = {0}, gdone[8] = {0};
    uint32_t i, d, dup = 0, split = 0, lastg = 99, nbad = 0, nrm = 0, nrmok = 0;
    char b[200], bad[32] = "";
    track_t *t = &trk[0];
    for (i = 0; i < 64; i++)
        dup += seen[MDST_ORDER[i]]++ != 0;
    ck("DESTINATION knob order (params.c MDST_ORDER): every number 0..63 exactly once", dup == 0);
    for (i = 1; i < 64; i++) {                          /* the groups along the knob, the not offered ones left out: each group in one run */
        d = MDST_ORDER[i];
        if (mod_dst_removed(0, (int32_t)d))
            continue;
        if (MDST_GRP[d] != lastg) {
            split += gdone[MDST_GRP[d] & 7u];
            gdone[MDST_GRP[d] & 7u] = 1;
            lastg = MDST_GRP[d];
        }
    }
    ck("the groups PITCH FILTER AMP ENGINE AMP ENV FILTER ENV LFO each come once along the knob, in L/RRY's order", split == 0 && !strcmp(MDST_GRPNAME[1], "PITCH") &&
       !strcmp(MDST_GRPNAME[2], "FILTER") && !strcmp(MDST_GRPNAME[3], "AMP") && !strcmp(MDST_GRPNAME[4], "ENGINE") && !strcmp(MDST_GRPNAME[5], "AMP ENV") &&
       !strcmp(MDST_GRPNAME[6], "FILTER ENV") && !strcmp(MDST_GRPNAME[7], "LFO"));
    for (d = 1; d < MD_N; d++) {
        int rm = mod_dst_removed(0, (int32_t)d), want = d == MD_DEPTH || d == MD_DEPTH2 || (d >= 42 && d <= 45) || d == 56 || d == 62;
        nrm += rm;
        nrmok += rm == want;
    }
    snprintf(b, sizeof b, "not offered: exactly L/RRY's retired LFO depths 20, 22 and the spares 42..45, 56, 62 (%u of 63 right, %u not offered)", nrmok, nrm);
    ck(b, nrmok == 63u && nrm == 8u);
    {   /* every destination that is offered somewhere has a group, a full-word name and a stored target */
        uint32_t ngrp = 0, nid = 0, noff = 0;
        for (d = 1; d < MD_N; d++) {
            if (mod_dst_removed(0, (int32_t)d))
                continue;
            noff++;
            fresh(); patch_plain(t);
            xset(t, X_O1MODEL, 5); xset(t, X_O2MODEL, 5);          /* SUPERSAW: all three model controls named */
            ngrp += MDST_GRP[d] != 0;
            nid += mod_lock_id(d) != 0;
            if (!name_ok(mod_dst_name(t, (int32_t)d), bad, sizeof bad) || !name_ok(mod_dst_name(&trk[1], (int32_t)d), bad, sizeof bad)) {
                printf("lrry_matrix:   destination %u: '%s' has the word '%s'\n", d, mod_dst_name(t, (int32_t)d), bad);
                nbad++;
            }
        }
        snprintf(b, sizeof b, "the %u destinations on the knob: each has a group (%u), a lock target (%u) and its name in full words (%u not)", noff, ngrp, nid, nbad);
        ck(b, ngrp == noff && nid == noff && nbad == 0 && noff == 55u);
    }
    {   /* the reasons on a plain patch (SAW, SAW, TWIN OFF, both dirt units OFF): exactly the values that cannot act, each with its word */
        uint32_t wrong = 0;
        fresh(); patch_plain(t);
        for (d = 1; d < MD_N; d++) {
            const char *w = mod_dst_why(t, (int32_t)d);
            const char *want = (d == MD_TIMBRE || MD_OSCP(d)) ? "MODEL" :
                               (d == MD_CUT2 || d == MD_RES2 || d == MD_BLEND || d == MD_FBPAN || d == MD_O1BAL || d == MD_O2BAL || d == MD_NZBAL || d == MD_RGBAL) ? "TWIN OFF" :
                               (d == MD_DRIVE || d == MD_DALVL || d == MD_DAPAN || d == MD_BDRV || d == MD_DBLVL || d == MD_DBPAN) ? "DIRT OFF" : 0;
            if ((w == 0) != (want == 0) || (w && strcmp(w, want))) {
                printf("lrry_matrix:   destination %u (%s): reason '%s', expected '%s'\n", d, N_MDST[d], w ? w : "-", want ? want : "-");
                wrong++;
            }
        }
        ck("plain patch: MODEL on TIMBRE and the model controls, TWIN OFF on the TWIN values, its pan and the balances, DIRT OFF on the dirt values; no other reason", wrong == 0);
        xset(t, X_O1MODEL, 5);                           /* SUPERSAW: SAWS steps (a count): not offered, as L/RRY's list values; DETUNE and SPREAD are */
        ck("SUPERSAW on OSC 1: DETUNE and SPREAD offered, SAWS (a stepped count) not, TIMBRE offered", mod_dst_offered(t, MD_O1P1) && !mod_dst_offered(t, MD_O1P2) &&
           mod_dst_offered(t, MD_O1P3) && mod_dst_offered(t, MD_TIMBRE) && !mod_dst_offered(t, MD_O2P1));
        ck("model control names follow the MODEL: OSC 1 DETUNE / SAWS / SPREAD on SUPERSAW, OSC 2 KNOB 2 on SAW (phase 53: the model controls are K2 .. K4 of the first page)",
           !strcmp(mod_dst_name(t, MD_O1P1), "OSC 1\nDETUNE") && !strcmp(mod_dst_name(t, MD_O1P3), "OSC 1\nSPREAD") && !strcmp(mod_dst_name(t, MD_O2P1), "OSC 2\nKNOB 2"));
    }
    {
        uint32_t s, nduck = 0;
        for (s = 0; s < MS_N; s++)
            nduck += !strcmp(N_MSRC[s], "DUCK");
        ck("sources: 17 (OFF .. FOOT), L/RRY's DUCK removed (no ducker on L/RRY), BREATH and FOOT kept", MS_N == 17 && nduck == 0 && !strcmp(N_MSRC[MS_BREATH], "BREATH") && !strcmp(N_MSRC[MS_FOOT], "FOOT"));
    }
    {   /* all nine voice pans and the part's PAN are destinations */
        static const uint8_t PN[10] = {MD_PAN, MD_O1PAN, MD_O2PAN, MD_SUBPAN, MD_NZPAN, MD_RGPAN, MD_FAPAN, MD_FBPAN, MD_DAPAN, MD_DBPAN};
        static const uint8_t XS[10] = {0, X_O1PAN, X_O2PAN, X_SUBPAN, X_NZPAN, X_RGPAN, X_FAPAN, X_FBPAN, X_DAPAN, X_DBPAN};
        uint32_t ok = 0;
        for (i = 0; i < 10; i++)
            ok += mod_param(PN[i]) == (i ? XW_BASE + XS[i] : (uint32_t)P_PAN) && !mod_dst_removed(0, PN[i]) && MDST_GRP[PN[i]] == 3;
        ck("PAN and the nine voice pans (OSC 1, OSC 2, SUB, NOISE, RING, MULTI FILTER, TWIN FILTER, DIRT A, DIRT B) are destinations, group AMP", ok == 10);
    }
}

/* ---------------------------------------------------------------------------------------------------- 2 the destination pages */
#define MB 140
static int32_t fw_l[MB * CTL], fw_r[MB * CTL];
static double ml[MB], mr[MB], mbr[MB];
static void play(int nblk, int off_blk)
{
    int32_t o[2 * CTL];
    int b;
    for (b = 0; b < 37; b++)                            /* (37 blocks before the note: a free-running LFO is somewhere in its cycle when the note starts, so LFO TRIGGER FREE and FIRST NOTE differ) */
        mix_block(o, CTL);
    trk_note_on(&trk[0], 60, 100);
    for (b = 0; b < nblk; b++) {
        if (off_blk && b == off_blk)
            trk_note_off(&trk[0], 60);
        mix_block(o, CTL);
        memcpy(fw_l + b * CTL, mix_l, CTL * sizeof(int32_t));
        memcpy(fw_r + b * CTL, mix_r, CTL * sizeof(int32_t));
    }
}
static void measure(void)
{
    int b, i;
    for (b = 0; b < MB; b++) {
        double l = 0, r = 0, d = 0;
        for (i = b * CTL; i < (b + 1) * CTL; i++) {
            double x = fw_l[i], y = fw_r[i], px = i ? fw_l[i - 1] : 0;
            l += x * x; r += y * y; d += (x - px) * (x - px);
        }
        ml[b] = 10 * log10(l + 1); mr[b] = 10 * log10(r + 1); mbr[b] = 10 * log10(d + 1);
    }
}
typedef void (*setup_f)(track_t *);
static void base(track_t *t)                            /* a held saw through filter A at 50, both LFOs fast sines, the envelopes slow enough to be heard moving, the CYCLE looping */
{
    fresh(); patch_plain(t);
    t->p[P_FREEA] = 50; t->p[P_FREEB] = 40;
    t->p[P_LRATE] = 85; t->p[P_LWAVE] = 0; t->p[P_L2RATE] = 80; t->p[P_L2WAVE] = 0;
    t->p[P_ATK] = 30; t->p[P_DEC] = 50; t->p[P_SUS] = 90; t->p[P_REL] = 40;
    t->p[P_E2ATK] = 30; t->p[P_E2DEC] = 50; t->p[P_E2SUS] = 40; t->p[P_E2REL] = 40;
    XV(t, X_CYRISE) = 30; XV(t, X_CYFALL) = 30;
}
/* the run of setup set with card id at value v (id 0: none); per block three measures */
static void run_card(setup_f set, uint32_t id, int v, double *L, double *R, double *B)
{
    track_t *t = &trk[0];
    base(t);
    set(t);
    if (id)
        t->p[id] = (int16_t)v;
    play(MB, 100);
    measure();
    memcpy(L, ml, sizeof ml); memcpy(R, mr, sizeof mr); memcpy(B, mbr, sizeof mbr);
}
static double card_diff(setup_f set, uint32_t id, int v, int v0)   /* the largest block difference (dB) of the card at v against the card at v0 (its rest value) */
{
    static double L0[MB], R0[MB], B0[MB], L1[MB], R1[MB], B1[MB];
    int b;
    double w = 0;
    run_card(set, id, v0, L0, R0, B0);                  /* (a first run not measured: state fresh() does not clear, lrry_morph_test.c's note) */
    run_card(set, id, v0, L0, R0, B0);
    run_card(set, id, v, L1, R1, B1);
    for (b = 4; b < MB; b++) {
        double x = fabs(L0[b] - L1[b]), y = fabs(R0[b] - R1[b]), z = fabs(B0[b] - B1[b]);
        if (x > w) w = x;
        if (y > w) w = y;
        if (z > w) w = z;
    }
    return w;
}
static void s_none(track_t *t) { (void)t; }
static void s_pulse(track_t *t) { xset(t, X_O1MODEL, 3); xset(t, X_O1P1, 40); }
static void s_dirtb(track_t *t) { xset(t, X_DBTYPE, 3); xset(t, X_DBDRV, 30); t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 70; }
static void s_dirta(track_t *t) { xset(t, X_DATYPE, 1); t->p[P_DRV2] = 30; }
static void s_twin(track_t *t) { t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 60; t->p[P_RES2] = 50; t->p[P_FREEA] = 25; }
static void s_esync(track_t *t) { t->p[P_ATK] = 60; }
static void s_cypit(track_t *t) { xset(t, X_CYPIT, 30); }
static void s_l1pit(track_t *t) { t->p[P_LD_PIT] = 20; }
static void s_l2pan(track_t *t) { xset(t, X_L2FAPAN, 50); }
static void s_l1note(track_t *t) { s_l1pit(t); t->p[P_LTRIG] = 0; }   /* PHASE: the start phase of TRIGGER FIRST NOTE (with FREE the phase runs on) */
static void s_l2note(track_t *t) { s_l2pan(t); t->p[P_L2TRIG] = 0; }
static void sec_pages(void)
{
    static const char *const PG[] = {"AMP DEST", "ENV 2 DEST", "LFO 1 DEST", "LFO 2 DEST", "CYCLE ENV 1/2", "CYCLE ENV 2/2", "LFO 1 TRIG", "LFO 2 TRIG"};
    uint32_t i, k, ncard = 0, nlive = 0, p;
    char b[220];
    printf("lrry_matrix: section 2: each card turned from its rest value, the render against the run at rest (largest block difference of level L, level R or brightness, dB)\n");
    for (i = 0; i < sizeof PG / sizeof PG[0]; i++) {
        for (p = 0; p < NELEM(PAGES) && strcmp(PAGES[p].title, PG[i]); p++)
            ;
        if (p == NELEM(PAGES)) {
            ck(PG[i], 0);
            continue;
        }
        for (k = 0; k < 4u; k++) {
            uint32_t id = PAGES[p].id[k];
            const param_desc_t *pd;
            setup_f set = s_none;
            int v, v0;
            double w;
            if (id == 0xFFu) {
                printf("lrry_matrix:   %-14s K%u: no card\n", PG[i], k + 1);
                ncard++;
                continue;
            }
            pd = track_desc(&trk[0], id);
            v0 = pd->def;
            v = pd->names ? (v0 == pd->min ? pd->min + 1 : pd->min) : v0 + (pd->max - v0 > v0 - pd->min ? (pd->max - v0) / 2 : -(v0 - pd->min) / 2);
            if (id == P_ED_SHP || id == P_LD_SHP) set = s_pulse;                   /* TIMBRE: the PULSE WIDTH */
            else if (id == XW_BASE + X_EDBDRV) set = s_dirtb;                      /* DIRT B DRIVE */
            else if (id == XW_BASE + X_E2DADRV) set = s_dirta;                     /* DIRT A DRIVE */
            else if (id == XW_BASE + X_E2BLD || id == XW_BASE + X_LFO2 || id == XW_BASE + X_L2RES2 || id == XW_BASE + X_L2BLD || id == XW_BASE + X_CYCUT2) set = s_twin;
            else if (id == P_ESYNC) set = s_esync;                                 /* SYNC: the AMP ENV's times as note values */
            else if (id >= XW_BASE + X_CYMODE && id <= XW_BASE + X_CYSYNC) set = s_cypit;   /* CYCLE ENV 1/2 and CURVE: heard through CYCLE to PITCH */
            else if (id == P_LTRIG || id == P_LPHASE || id == P_LPOL || id == XW_BASE + X_LFO1LVL) set = s_l1pit;   /* LFO 1 TRIG: heard through LFO 1 to PITCH */
            else if (id == P_L2TRIG || id == P_L2PHASE || id == P_L2POL || id == XW_BASE + X_LFO2LVL) set = s_l2pan;   /* LFO 2 TRIG: heard through LFO 2 to MULTI FILTER PAN */
            if (id == P_LPHASE) set = s_l1note;
            if (id == P_L2PHASE) set = s_l2note;
            if (id == P_LTRIG || id == P_L2TRIG) v = 0;  /* (TRIGGER: FIRST NOTE against the default FREE) */
            if (id == XW_BASE + X_CYMODE) v = 1;
            w = card_diff(set, id, v, v0);
            ncard++;
            nlive += w > 1.0;
            printf("lrry_matrix:   %-14s K%u %-14s %-6s %4d -> %4d: %6.2f dB %s\n", PG[i], k + 1, label_shown(pd->label), pd->label, v0, v, w, w > 1.0 ? "moves" : "STILL");
        }
    }
    snprintf(b, sizeof b, "the ENV, LFO and CYCLE destination pages and the LFO TRIG pages: all %u cards present and each moves the render (%u)", ncard, nlive);
    ck(b, ncard == 32u && nlive == 32u);
    {   /* the cards' shown names (params.c LBL_SHOWN) in full words */
        uint32_t bad = 0;
        char w[32];
        for (i = 0; i < sizeof PG / sizeof PG[0]; i++) {
            for (p = 0; p < NELEM(PAGES) && strcmp(PAGES[p].title, PG[i]); p++)
                ;
            for (k = 0; p < NELEM(PAGES) && k < 4u; k++) {
                const param_desc_t *pd = track_desc(&trk[0], PAGES[p].id[k]);
                const char *nm = label_shown(pd->label);
                static const char *const MORE[] = {"SYNC", "TRIGGER", "PHASE", "POLARITY", "CURVE", "RISE", "FALL", 0};
                int j, extra = 0;
                for (j = 0; MORE[j]; j++)
                    extra |= !strcmp(nm, MORE[j]);
                if (!extra && !name_ok(nm, w, sizeof w)) {
                    printf("lrry_matrix:   %s K%u '%s': the word '%s'\n", PG[i], k + 1, nm, w);
                    bad++;
                }
            }
        }
        ck("AMP DEST: PITCH, TIMBRE, DIRT B DRIVE, SYNC; ENV 2 DEST K3 DIRT A DRIVE; LFO 2 DEST K3 MULTI FILTER PAN; every DEST card's name in full words", bad == 0 &&
           !strcmp(label_shown(TP[P_ED_SHP].label), "TIMBRE") && !strcmp(label_shown(XTP[X_EDBDRV].label), "DIRT B\nDRIVE") &&
           !strcmp(label_shown(XTP[X_E2DADRV].label), "DIRT A\nDRIVE") && !strcmp(label_shown(XTP[X_L2FAPAN].label), "MULTI FILTER\nPAN"));
    }
}

/* ---------------------------------------------------------------------------------------------------- 3 the locks */
static void sec_locks(void)
{
    track_t *t = &trk[0];
    int32_t p1a, p2a, want = 40 + (((32767 * MX_AMT(32)) >> 6) * 127 >> 15);   /* (+32 is +50 %: AMT 64 = the whole range of P1, 0..127) */
    uint32_t d, ok = 0, n = 0;
    for (d = 1; d < MD_N; d++)
        if (!mod_dst_removed(0, (int32_t)d)) {
            n++;
            ok += mod_lock_dst(mod_lock_id(d)) == d;
        }
    ck("every destination on the knob is a lock target, and the target leads back to it (mod_lock_id, mod_lock_dst)", ok == n);
    base(t);
    xset(t, X_O1MODEL, 3); xset(t, X_O2MODEL, 7); xset(t, X_O1P1, 40); xset(t, X_O2P1, 40);
    lk_virt[0][MD_TIMBRE] = 32; lk_virt_n[0] = 1;
    (void)mod_begin(t);
    p1a = XV(t, X_O1P1); p2a = XV(t, X_O2P1);
    mod_end(t);
    printf("lrry_matrix:   TIMBRE lock +32: OSC 1 P1 40 -> %d, OSC 2 P1 40 -> %d (expected %d)\n", p1a, p2a, want);
    ck("TIMBRE's lock (+50 %) moves OSC 1 P1 and OSC 2 P1 by half their range for the block, and mod_end puts them back", p1a == want && p2a == want && XV(t, X_O1P1) == 40 && XV(t, X_O2P1) == 40);
    lk_virt[0][MD_TIMBRE] = 0; lk_virt_n[0] = 0;
}

/* ---------------------------------------------------------------------------------------------------- 4 the pans at fast modulation */
/* LFO 1 (a sine, RATE r) at AMT 63 to destination d: the largest change of a pan's far gain from one block to the next, over the held note, in dB of the gain at the centre (1.0). The stage pans and
 * SUPER's pan are set once a block (lrry_pan_t g); the mono source pans ramp their gains over each block, so from block to block they move by the same step but without a jump inside the block */
static double pan_step(uint32_t d, int rate, double *per_ms)
{
    track_t *t = &trk[0];
    int32_t o[2 * CTL], prev = -1;
    int b;
    double w = 0;
    base(t);
    t->p[P_FREEA] = UF_STEPS;
    t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 82;                       /* (TWIN on, open: its pan acts) */
    xset(t, X_DATYPE, 1); xset(t, X_DBTYPE, 1);
    t->p[P_LRATE] = (int16_t)rate;
    t->p[P_M1SRC] = MS_LFO; t->p[P_M1DST] = (int16_t)d; t->p[P_M1AMT] = 63;
    trk_note_on(t, 60, 100);
    for (b = 0; b < 400; b++) {
        int32_t g;
        const lrry_pan_t *pp = d == MD_FAPAN ? &lrry_pp[0].pa : d == MD_FBPAN ? &lrry_pp[0].pb : &lrry_pp[0].pd[d == MD_DBPAN];
        mix_block(o, CTL);
        g = pp->side ? pp->g : LRRY_ONE;
        if (b > 20 && prev >= 0) {
            double x = fabs((double)g - prev) / LRRY_ONE;
            if (x > w) w = x;
        }
        prev = g;
    }
    *per_ms = w / (CTL * 1000.0 / 44100.0);
    return w;
}
static void sec_zipper(void)
{
    static const struct { uint8_t d; const char *n; } S[] = {{MD_FAPAN, "MULTI FILTER PAN"}, {MD_FBPAN, "TWIN FILTER PAN"}, {MD_DAPAN, "DIRT A PAN"}, {MD_DBPAN, "DIRT B PAN"}};
    static const int R[3] = {127, 100, 60};
    uint32_t i, k;
    printf("lrry_matrix: section 4: the stage pans, LFO 1 sine at AMOUNT 63 (full left to full right): the largest step of the far channel's gain from one 32-sample block (0.73 ms) to the next\n");
    for (i = 0; i < sizeof S / sizeof S[0]; i++)
        for (k = 0; k < 3u; k++) {
            double ms, st = pan_step(S[i].d, R[k], &ms);
            printf("lrry_matrix:   %-16s LFO RATE %3d: step %.4f of full gain (%.1f %% a block)\n", S[i].n, R[k], st, st * 100.0);
        }
    ck("section 4 measured (the numbers above; a per-sample ramp of the stage pans is a separate job: report)", 1);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    sec_lists();
    sec_pages();
    sec_locks();
    sec_zipper();
    printf("lrry_matrix: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
