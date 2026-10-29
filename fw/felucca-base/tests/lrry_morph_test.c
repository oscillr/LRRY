/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.4a host check: MORPH, its randomizer (the ROLL) and the 8-slot matrix on the L/RRY parameters, through the real firmware path (mix_block: morph_begin, mod_begin, the voice).
 * Build (from felucca-base, the flags of LRRY/fw/onepart.sh):  ../onepart.sh 1 1 lrry_morph      (UBSan: the same line with -fsanitize=signed-integer-overflow,shift-exponent, see the report)
 * Sections:  1 the MORPH tag of every L/RRY parameter (printed: the table of the report)   2 the fader and the ends (A at 0, B at 127, the blend, switches at the middle, determinism)
 *            3 the randomizer: 1000 rolled patches rendered (8 voices, fader at A, the middle and B): ranges, the values that never move, no wrap, no stuck note
 *            4 the matrix: every destination 1..63 and every source 1..17 routed, the render with the route against the run without it */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int nfail, ncheck;
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_morph: %-104s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}

/* the 58 L/RRY voice parameters (PARAM_LIST.md): 22 old ids, then the 36 slots */
static const struct { uint16_t id; const char *name; } LP[] = {
    {P_FREEA, "FILTER A CUT"}, {P_FREEB, "FILTER A RES"}, {P_FREEC, "FILTER A MODE"}, {P_FREED, "FILTER A SLOPE"}, {P_KTRK, "FILTER A KEY TRK"},
    {P_TWTYPE, "FILTER B TYPE"}, {P_CUT2, "FILTER B CUTOFF"}, {P_RES2, "FILTER B RES"}, {XW_BASE + X_LINK, "FILTER B LINK"}, {XW_BASE + X_KTRK2, "FILTER B KEY TRK 2"},
    {XW_BASE + X_ENV2, "FILTER B ENV 2"}, {XW_BASE + X_LFO1, "FILTER B LFO 1"}, {XW_BASE + X_LFO2, "FILTER B LFO 2"}, {XW_BASE + X_ROUTE, "FILTERS ROUTE"},
    {P_DRV2, "DIRT A DRIVE"}, {P_BLEND, "SOURCES BLEND"}, {XW_BASE + X_SUBLVL, "SUB LEVEL"}, {XW_BASE + X_SUBOCT, "SUB OCTAVE"},
    {P_ATK, "AMP ENV ATK"}, {P_DEC, "AMP ENV DEC"}, {P_SUS, "AMP ENV SUS"}, {P_REL, "AMP ENV REL"},
    {XW_BASE + X_O1MODEL, "OSC 1 MODEL"}, {XW_BASE + X_O1COARSE, "OSC 1 COARSE"}, {XW_BASE + X_O1FINE, "OSC 1 FINE"}, {XW_BASE + X_O1LVL, "OSC 1 LEVEL"},
    {XW_BASE + X_O1PAN, "OSC 1 PAN"}, {XW_BASE + X_O1BAL, "OSC 1 BAL"}, {XW_BASE + X_O1P1, "OSC 1 P1"}, {XW_BASE + X_O1P2, "OSC 1 P2"}, {XW_BASE + X_O1P3, "OSC 1 P3"},
    {XW_BASE + X_O2MODEL, "OSC 2 MODEL"}, {XW_BASE + X_O2COARSE, "OSC 2 COARSE"}, {XW_BASE + X_O2FINE, "OSC 2 FINE"}, {XW_BASE + X_O2LVL, "OSC 2 LEVEL"},
    {XW_BASE + X_O2PAN, "OSC 2 PAN"}, {XW_BASE + X_O2BAL, "OSC 2 BAL"}, {XW_BASE + X_O2P1, "OSC 2 P1"}, {XW_BASE + X_O2P2, "OSC 2 P2"}, {XW_BASE + X_O2P3, "OSC 2 P3"},
    {XW_BASE + X_NZLVL, "NOISE LEVEL"}, {XW_BASE + X_NZPAN, "NOISE PAN"}, {XW_BASE + X_NZBAL, "NOISE BAL"},
    {XW_BASE + X_RGLVL, "RING LEVEL"}, {XW_BASE + X_RGPAN, "RING PAN"}, {XW_BASE + X_RGBAL, "RING BAL"},
    {XW_BASE + X_FAPAN, "FILTER A PAN"}, {XW_BASE + X_FBPAN, "FILTER B PAN"},
    {XW_BASE + X_DATYPE, "DIRT A TYPE"}, {XW_BASE + X_DAPAN, "DIRT A PAN"}, {XW_BASE + X_DALVL, "DIRT A LEVEL"},
    {XW_BASE + X_DBTYPE, "DIRT B TYPE"}, {XW_BASE + X_DBDRV, "DIRT B DRIVE"}, {XW_BASE + X_DBPAN, "DIRT B PAN"}, {XW_BASE + X_DBLVL, "DIRT B LEVEL"},
    {XW_BASE + X_DSER, "DIRT ROUTE"}, {XW_BASE + X_MID, "OUTPUT MID"}, {XW_BASE + X_SIDE, "OUTPUT SIDE"},
};
#define NLP (sizeof LP / sizeof LP[0])
_Static_assert(NLP == 58, "58 voice parameters");

static void fresh(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(lrry_vs, 0, sizeof lrry_vs);
    memset(&pscr, 0, sizeof pscr);
    memset(mph, 0, sizeof mph);
    memset(mod_cc, 0, sizeof mod_cc);
    for (i = 0; i < NLIVE; i++)
        pan_gv[i] = mx_ok[i] = 0;
    mix_l[0] = 0;
    dc_l = dc_r = dce_l = dce_r = 0;
}
static void patch_plain(track_t *t)        /* as tests/lrry_fw_test.c: an instant attack, full sustain, mixer and TRIM at 112, KEY TRK 0, OSC 1 SAW at 64, filter A open */
{
    t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 30;
    t->p[P_LEVEL] = 112; t->p[P_TRIM] = 112; t->p[P_KTRK] = 0; t->p[P_PAN] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_FREEA] = UF_STEPS;
    XV(t, X_ROUTE) = 1;
    XV(t, X_LINK) = 0;
}
static void xset(track_t *t, uint32_t k, int v) { XV(t, k) = (int16_t)v; }

/* ---------------------------------------------------------------------------------------------------- 1 the tags */
static const char *kname(uint32_t k) { return k == MK_EX ? "never" : k == MK_HARD ? "switch" : k == MK_CONT ? "smooth" : k == MK_FADE ? "fade" : k == MK_SLOT ? "slot" : "step"; }
static void tag_str(mtag_t g, char *b, size_t n)
{
    snprintf(b, n, "%s%s%s%s%s%s", (g.flags & MF_SW) ? "drawn, not set" : g.kind == MK_EX && (g.flags & MF_DRAW) ? "held" : kname(g.kind),
             (g.flags & MF_CAP) ? " cap" : "", (g.flags & MF_RISE) ? " rise" : "", (g.flags & MF_CEIL) ? " ceil" : "", (g.flags & MF_FLOOR) ? " floor" : "", (g.flags & MF_LOG) ? " log" : "");
    if (g.lim && (g.flags & (MF_CAP | MF_RISE | MF_CEIL | MF_FLOOR)))
        snprintf(b + strlen(b), n - strlen(b), " %d", g.lim);
}
static void sec_tags(void)
{
    int8_t o[EXT_P_LEN];
    uint32_t i, untagged = 0, m;
    track_t *t = &trk[0];
    fresh(); patch_plain(t);
    t->p[P_TWTYPE] = 1; XV(t, X_DATYPE) = 1; XV(t, X_DBTYPE) = 1;    /* (the gates open: filter B and both DIRT units on) */
    for (i = 0; i < EXT_P_LEN; i++)
        o[i] = (int8_t)clamp(t->p[i], -128, 127);
    printf("lrry_morph: section 1: MORPH tags (gates open: filter B LP, DIRT A and B SOFT). Columns: SMART, SMART + PITCH, WILD + PITCH; the fader (morph_build): smooth = blend, switch / step = at the middle\n");
    for (i = 0; i < NLP; i++) {
        char a[48], b[48], c[48];
        mtag_t g;
        mrc = MC_STEREO; g = morph_tag(o, LP[i].id, 1); tag_str(g, a, sizeof a);
        mrc = MC_STEREO | MC_PIT; g = morph_tag(o, LP[i].id, 1); tag_str(g, b, sizeof b);
        mrc = MC_STEREO | MC_PIT | MC_WILD; g = morph_tag(o, LP[i].id, 1); tag_str(g, c, sizeof c);
        printf("lrry_morph: tag %-20s id %3u  | %-22s | %-22s | %-18s\n", LP[i].name, LP[i].id, a, b, c);
    }
    mrc = 0;
    for (m = 0; m < 8u; m++) {                           /* P1 .. P3 by MODEL */
        char a[3][48];
        uint32_t k;
        o[XW_BASE + X_O1MODEL] = (int8_t)m;
        mrc = MC_STEREO | MC_PIT;
        for (k = 0; k < 3u; k++)
            tag_str(morph_tag(o, XW_BASE + X_O1P1 + k, 1), a[k], sizeof a[k]);
        printf("lrry_morph: tag OSC P1 P2 P3 with MODEL %u: %-22s %-22s %-22s (SMART + PITCH)\n", m, a[0], a[1], a[2]);
    }
    mrc = 0;
    for (i = 28; i < NX; i++)                             /* every new slot has an entry of its own in MTX (or is a P1 .. P3) */
        if (!MORPH_OSCP(XW_BASE + i) && !memcmp(&MTX[i], &(mtag_t)T_EX, sizeof(mtag_t)) && i != X_DATYPE && i != X_DBTYPE)
            untagged++;
    ck("every one of the 36 new slots has a MORPH tag (TYPEs as DUELL's INSERT TYPE: never; P1..P3 by MODEL)", untagged == 0);
    {   /* the gates: closed, the gated values are held in SMART */
        fresh(); patch_plain(t);
        for (i = 0; i < EXT_P_LEN; i++)
            o[i] = (int8_t)clamp(t->p[i], -128, 127);
        mrc = MC_STEREO;
        ck("SMART gates: BALs and FILTER B PAN held while filter B is OFF; DIRT DRIVE / PAN / LEVEL held while the unit is OFF",
           morph_tag(o, XW_BASE + X_O1BAL, 1).kind == MK_EX && morph_tag(o, XW_BASE + X_FBPAN, 1).kind == MK_EX && morph_tag(o, P_DRV2, 1).kind == MK_EX &&
           morph_tag(o, XW_BASE + X_DALVL, 1).kind == MK_EX && morph_tag(o, XW_BASE + X_DBDRV, 1).kind == MK_EX && morph_tag(o, XW_BASE + X_O1PAN, 1).kind == MK_CONT);
        ck("P_E0..P_E7 (no engine) never roll, not even in WILD", (mrc = MC_STEREO | MC_WILD, morph_tag(o, P_E0, 1).kind == MK_EX && morph_tag(o, P_E7, 1).kind == MK_EX));
        mrc = 0;
    }
}

/* ---------------------------------------------------------------------------------------------------- render helpers */
#define MAXB 160
static int32_t fw_l[MAXB * CTL], fw_r[MAXB * CTL];
static int32_t out_pk;                                  /* the master output's peak of the last run */
static int32_t big_pk;                                  /* the part sum's peak */
/* notes on, nblk blocks of mix_block (the part sum kept: mix_l / mix_r), note off at off_blk (0: never) */
static int play(const uint8_t *notes, int nn, int nblk, int off_blk)
{
    int32_t o[2 * CTL];
    int b, k, i;
    out_pk = big_pk = 0;
    for (k = 0; k < nn; k++)
        trk_note_on(&trk[0], notes[k], 100);
    for (b = 0; b < nblk && b < MAXB; b++) {
        if (off_blk && b == off_blk)
            for (k = 0; k < nn; k++)
                trk_note_off(&trk[0], notes[k]);
        mix_block(o, CTL);
        memcpy(fw_l + b * CTL, mix_l, CTL * sizeof(int32_t));
        memcpy(fw_r + b * CTL, mix_r, CTL * sizeof(int32_t));
        for (i = 0; i < CTL; i++) {
            int32_t a = mix_l[i] < 0 ? -mix_l[i] : mix_l[i], c = mix_r[i] < 0 ? -mix_r[i] : mix_r[i];
            if (a > big_pk) big_pk = a;
            if (c > big_pk) big_pk = c;
        }
        for (i = 0; i < 2 * CTL; i++) {
            int32_t a = o[i] < 0 ? -o[i] : o[i];
            if (a > out_pk) out_pk = a;
        }
    }
    return b;
}

/* ---------------------------------------------------------------------------------------------------- 2 the fader and the ends */
static void sec_fader(void)
{
    track_t *t = &trk[0];
    morph_part_t *mp = &mph[0];
    int16_t va[EXT_P_LEN], vb[EXT_P_LEN], vm[EXT_P_LEN];
    int8_t e1[EXT_P_LEN], e2[EXT_P_LEN];
    uint32_t i, j, moved = 0, okA = 1, okB = 1, okM = 1, nsw = 0, nsm = 0, x;
    fresh(); patch_plain(t);
    t->p[P_FREEA] = 70; t->p[P_FREEB] = 30; t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 60;
    xset(t, X_O1LVL, 90); xset(t, X_O2LVL, 70); xset(t, X_O2COARSE, 7); xset(t, X_NZLVL, 10); xset(t, X_DATYPE, 1); t->p[P_DRV2] = 30;
    morph_rebase(t);
    morph_roll_cfg(0, 0u, 1234u, 40u, MC_STEREO | MC_WILD | MC_PIT);   /* WILD so the switches differ too */
    morph_roll_cfg(0, 1u, 4321u, 40u, MC_STEREO | MC_WILD | MC_PIT);
    for (x = 0; x < 2u; x++) {
        int16_t *v = x ? vb : va;
        mp->fader = x ? 127u : 0u;
        morph_view_all(0, v);
        for (j = 0; j < mp->nid; j++) {
            const ment_t *en = &mp->ent[j];
            uint32_t id = en->id, n = (en->kind & 0x3Fu) == MK_SLOT ? 3u : 1u, q;
            for (q = 0; q < n; q++) {
                uint32_t w = id + q;
                int32_t want = w >= XW_BASE ? morph_curx(mp, x)[w - XW_BASE] : morph_cur(mp, x)[w];
                if (v[w] != want) {
                    if (x) okB = 0; else okA = 0;
                }
            }
        }
    }
    mp->fader = 64u;
    morph_view_all(0, vm);
    for (j = 0; j < mp->nid; j++) {
        const ment_t *en = &mp->ent[j];
        uint32_t id = en->id, k = en->kind & 0x3Fu;
        int32_t a = va[id], b = vb[id], m = vm[id];
        if (a == b)
            continue;
        moved++;
        if (k == MK_CONT) {
            nsm++;
            if (m < (a < b ? a : b) || m > (a > b ? a : b))
                okM = 0;
        } else if (k == MK_STEP) {
            nsw++;
            if (m != b)                                  /* fader 64 is past the middle (w >= 16384): B's value */
                okM = 0;
        }
    }
    mp->fader = 63u;
    morph_view_all(0, vm);
    for (j = 0; j < mp->nid; j++) {
        const ment_t *en = &mp->ent[j];
        if ((en->kind & 0x3Fu) == MK_STEP && va[en->id] != vb[en->id] && vm[en->id] != va[en->id])
            okM = 0;
    }
    printf("lrry_morph: section 2: two WILD rolls at AMOUNT 40: %u values differ between A and B (%u smooth, %u switches), the fader moves %u entries\n", moved, nsm, nsw, mp->nid);
    ck("fader 0 sounds End A exactly, fader 127 End B exactly (every value the fader moves, the 36 slots included)", okA && okB && moved > 20);
    ck("fader 63 / 64: smooth values between the ends, switches flip at the middle (A at 63, B at 64)", okM && nsw > 0 && nsm > 0);
    {   /* an L/RRY slot really reaches the sound through morph_begin: OSC 2 LEVEL at A 0, at B 100: the render at 127 differs from the one at 0 */
        uint8_t n = 60;
        double ea = 0, eb = 0;
        int k;
        fresh(); patch_plain(t);
        xset(t, X_O1LVL, 0); xset(t, X_O2LVL, 0);
        morph_rebase(t);
        mp->fader = 127u;                                /* (EDITABLE B: End B's value by hand, at the B end) */
        morph_b_set(0, XW_BASE + X_O2LVL, 100);
        play(&n, 1, 40, 0);
        for (k = 20 * CTL; k < 40 * CTL; k++) eb += (double)fw_l[k] * fw_l[k];
        trk_note_off(t, n);
        for (k = 0; k < 60; k++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
        mp->fader = 0u;
        play(&n, 1, 40, 0);
        for (k = 20 * CTL; k < 40 * CTL; k++) ea += (double)fw_l[k] * fw_l[k];
        printf("lrry_morph: OSC 1 off, OSC 2 LEVEL 0 at A and 100 at B (set by hand): rms at fader 0 %.1f, at 127 %.1f\n", sqrt(ea / (20 * CTL)), sqrt(eb / (20 * CTL)));
        ck("an L/RRY slot moved by the fader reaches the sound (OSC 2 LEVEL: silent at A, heard at B)", ea == 0 && eb > 1000);
    }
    {   /* the same Original, seed, AMOUNT and settings give the same end */
        fresh(); patch_plain(t);
        xset(t, X_O1LVL, 90); xset(t, X_O2LVL, 70); t->p[P_TWTYPE] = 2; xset(t, X_DATYPE, 3);
        morph_rebase(t);
        morph_roll_cfg(0, 1u, 777u, 55u, MC_STEREO);
        memcpy(e1, morph_cur(mp, 1), P_COUNT); memcpy(e1 + XW_BASE, morph_curx(mp, 1), NX);
        morph_rebase(t);
        morph_roll_cfg(0, 1u, 777u, 55u, MC_STEREO);
        memcpy(e2, morph_cur(mp, 1), P_COUNT); memcpy(e2 + XW_BASE, morph_curx(mp, 1), NX);
        for (i = 0, j = 0; i < EXT_P_LEN; i++)
            if (i != P_COUNT && (i < P_COUNT || i >= XW_BASE))
                j += e1[i] != e2[i];
        ck("the same Original, seed, AMOUNT and settings give the same End B (36 slots included)", j == 0);
    }
    printf("lrry_morph: COPY A TO B: there is no such action in DUELL's MORPH (morph.c morph_do: FADER AMOUNT TARGET ROLL UNDO SMART ARP PITCH): nothing added, an open choice (report)\n");
}

/* ---------------------------------------------------------------------------------------------------- 3 the randomizer: 1000 rolled patches */
static uint32_t fz = 0x1234567u;
static uint32_t fzr(void) { fz ^= fz << 13; fz ^= fz >> 17; fz ^= fz << 5; return fz; }
static int32_t rin(int32_t lo, int32_t hi) { return lo + (int32_t)(fzr() % (uint32_t)(hi - lo + 1)); }
/* an Original: the plain patch, then (kind > 0) every L/RRY value at random inside its range, a few matrix routes */
static void original(track_t *t, uint32_t kind)
{
    uint32_t i;
    patch_plain(t);
    if (!kind) {
        xset(t, X_O1LVL, 100);
        return;
    }
    for (i = 0; i < NLP; i++) {
        const param_desc_t *d = morph_desc(LP[i].id);
        t->p[LP[i].id] = (int16_t)rin(d->min, d->max);
    }
    t->p[P_ATK] = (int16_t)rin(0, 60); t->p[P_REL] = (int16_t)rin(0, 50);   /* (the render is 60 blocks: short envelopes so a note is heard and ends) */
    if (kind & 1u) {                                    /* routes: CUT, PAN, AMP, the L/RRY pans */
        static const uint8_t D[] = {MD_CUT, MD_PAN, MD_AMP, MD_O1PAN, MD_FAPAN, MD_SIDE, MD_CUT2, MD_BDRV};
        for (i = 0; i < 3u; i++) {
            t->p[MSLOT_ID(i)] = (int16_t)(MS_LFO + fzr() % 4u);
            t->p[MSLOT_ID(i) + 1u] = D[fzr() % sizeof D];
            t->p[MSLOT_ID(i) + 2u] = (int16_t)rin(-64, 63);
        }
        t->p[P_LRATE] = (int16_t)rin(40, 100);
    }
}
static const uint16_t NEVER_IDS[] = {P_LEVEL, P_TRIM, P_MUTE, P_VOICE, XW_BASE + X_MID, XW_BASE + X_LFO1LVL, XW_BASE + X_LFO2LVL, XW_BASE + X_STRUM, P_E0, P_E7};
static void sec_random(void)
{
    static const uint8_t chord[8] = {36, 43, 48, 55, 60, 64, 67, 72};
    track_t *t = &trk[0];
    morph_part_t *mp = &mph[0];
    int8_t org[EXT_P_LEN];
    uint32_t r, nren = 0, range_bad = 0, never_bad = 0, rise_bad = 0, wrap = 0, clip = 0, stuck = 0, silent = 0, chains = 0;
    int32_t worst_big = 0, worst_out = 0, worst_tail = 0;
    uint32_t never_n[sizeof NEVER_IDS / sizeof NEVER_IDS[0]] = {0};
    uint32_t nsmart = 0, nwild = 0;
    for (r = 0; r < 1000u; r++) {
        uint32_t wild = r & 1u, pit = (r & 3u) == 3u, cfg = MC_STEREO | (wild ? MC_WILD : 0u) | (pit ? MC_PIT : 0u), amount = 5u + (r * 37u) % 96u, i, e, f, steps;
        fresh();
        original(t, r % 10u);
        for (i = 0; i < EXT_P_LEN; i++)
            org[i] = (int8_t)clamp(t->p[i], -128, 127);
        morph_rebase(t);
        morph_roll_cfg(0, 0u, 1u + r, amount, cfg);
        morph_roll_cfg(0, 1u, 30001u + r, amount, cfg);
        steps = (r % 5u == 4u) ? 6u : 0u;                /* ROLL CHAINED: every fifth patch 6 more steps on End B (the player's ROLL wanders) */
        for (i = 0; i < steps; i++) {
            mp->amount = (uint8_t)amount;
            morph.cfg = (uint8_t)(cfg & MC_SET);
            morph_roll_seed(0, 1u, 5000u + r * 7u + i);
        }
        chains += steps != 0u;
        nsmart += !wild;
        nwild += wild;
        for (e = 0; e < 2u; e++) {                       /* every value of both ends in its range; the never-values the Original's; SMART: a level at most 48 above the Original's */
            const int8_t *h = morph_cur(mp, e), *hx = morph_curx(mp, e);
            for (i = 0; i < EXT_P_LEN; i++) {
                const param_desc_t *d;
                int32_t v;
                if (i == P_COUNT || (i > P_COUNT && i < XW_BASE))
                    continue;
                v = i >= XW_BASE ? hx[i - XW_BASE] : h[i];
                d = morph_desc(i);
                if (v < d->min || v > d->max)
                    range_bad++;
            }
            for (i = 0; i < sizeof NEVER_IDS / sizeof NEVER_IDS[0]; i++) {
                uint32_t id = NEVER_IDS[i];
                int32_t v = id >= XW_BASE ? hx[id - XW_BASE] : h[id];
                if (v != org[id] && !(wild && id == P_VOICE)) {   /* (VOICE MODE: DUELL's T_EX, which WILD flips like every switch; SMART never) */
                    never_bad++;
                    never_n[i]++;
                }
                if (v != org[id] && wild && id == P_VOICE)
                    never_n[i]++;
            }
            if (!wild && !steps) {
                static const uint8_t LV[] = {X_O1LVL, X_O2LVL, X_NZLVL, X_RGLVL, X_SUBLVL, X_DALVL, X_DBLVL};
                for (i = 0; i < sizeof LV; i++)
                    if (hx[LV[i]] > org[XW_BASE + LV[i]] + 48)
                        rise_bad++;
            }
        }
        for (f = 0; f < 3u; f++) {                       /* the render: 8 voices at fader 0, 64, 127; released at block 40; 100 blocks */
            int b, k, active = 0;
            mp->fader = (uint8_t)(f == 0 ? 0u : f == 1 ? 64u : 127u);
            play(chord, 8, 100, 40);
            nren++;
            if (big_pk >= (1 << 30)) wrap++;
            if (out_pk > 32767) clip++;
            if (big_pk > worst_big) worst_big = big_pk;
            if (out_pk > worst_out) worst_out = out_pk;
            if (!big_pk) silent++;
            for (b = 0; b < 28000; b++) {                /* the release ends: every voice idle within 20 s (RELEASE 127 is 10 s; a roll may lengthen it) */
                int32_t o[2 * CTL];
                for (k = 0, active = 0; k < NVOICE; k++)
                    active += t->v[k].active != 0;
                if (!active)
                    break;
                mix_block(o, CTL);
            }
            if (b > worst_tail) worst_tail = b;
            stuck += active != 0;
            for (b = 0; b < 2; b++) {                    /* fresh voices for the next fader position */
                int32_t o[2 * CTL];
                mix_block(o, CTL);
            }
        }
    }
    printf("lrry_morph: section 3: 1000 rolled patches (%u SMART, %u WILD; every 4th with PITCH; AMOUNT 5..100; %u with 6 chained ROLLs), %u renders of an 8-note chord (fader 0, 64, 127)\n", nsmart, nwild, chains, nren);
    printf("lrry_morph:   values out of range %u, never-values moved %u, SMART levels more than 48 above the Original %u\n", range_bad, never_bad, rise_bad);
    for (r = 0; r < sizeof NEVER_IDS / sizeof NEVER_IDS[0]; r++)
        if (never_n[r])
            printf("lrry_morph:   id %u moved in %u ends%s\n", NEVER_IDS[r], never_n[r], NEVER_IDS[r] == P_VOICE ? " (VOICE MODE, WILD only: DUELL's rule, every switch flips in WILD)" : "");
    printf("lrry_morph:   the longest release after the note-off: %d blocks (%.2f s)\n", worst_tail, worst_tail * (double)CTL / FS);
    printf("lrry_morph:   part sum peak worst %d (%.2f x 2^20, limit 2^30), master output peak worst %d (limit 32767); renders with a wrap %u, over 32767 %u, silent %u, a voice still active 20 s after the release %u\n",
           worst_big, worst_big / 1048576.0, worst_out, wrap, clip, silent, stuck);
    ck("1000 rolls: every value of both ends inside its range", range_bad == 0);
    ck("1000 rolls: mixer LEVEL, TRIM, MUTE, MID, the LFO LEVELs, STRUM and the engine slots never move; VOICE MODE only in WILD", never_bad == 0);
    ck("1000 rolls, SMART: no source / sub / dirt level more than 48 above the Original's (MF_RISE)", rise_bad == 0);
    ck("3000 renders: no part-sum sample reaches 2^30 (no wrap), the master output stays inside +-32767", wrap == 0 && clip == 0 && worst_big < (1 << 30) && worst_out <= 32767);
    ck("3000 renders: every note ends after its release (no stuck voice)", stuck == 0);
    printf("lrry_morph:   (no float in the firmware path, so no NaN can exist; signed overflow: see the UBSan build in the report)\n");
}

/* ---------------------------------------------------------------------------------------------------- 4 the matrix */
/* a run: setup, the route (or none), one note 60 held 90 blocks then released, 140 blocks. Per block three measures: level L, level R (dB), brightness (rms of the first difference, dB) */
#define MB 140
static double ml[MB], mr[MB], mbr[MB];
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
static uint8_t run_note = 60;
static void run_route(setup_f set, uint32_t s, uint32_t d, int32_t amt, double *L, double *R, double *B)
{
    uint8_t n = run_note;
    track_t *t = &trk[0];
    fresh(); patch_plain(t);
    xset(t, X_O1LVL, 100);
    t->p[P_LRATE] = 85; t->p[P_LWAVE] = 0;              /* LFO 1 and 2: fast sines */
    t->p[P_L2RATE] = 80; t->p[P_L2WAVE] = 0;
    t->p[P_ATK] = 20; t->p[P_DEC] = 50; t->p[P_SUS] = 90; t->p[P_REL] = 40;
    t->p[P_E2ATK] = 30; t->p[P_E2DEC] = 50; t->p[P_E2SUS] = 40; t->p[P_E2REL] = 40;
    XV(t, X_CYRISE) = 30; XV(t, X_CYFALL) = 30;
    set(t);
    if (s && d) {
        t->p[P_M1SRC] = (int16_t)s; t->p[P_M1DST] = (int16_t)d; t->p[P_M1AMT] = (int16_t)amt;
    }
    morph_rebase(t);                                    /* (the MORPH source: fader at 127 below, both ends the Original) */
    morph_rebase(t);
    mph[0].fader = 127u;
    mod_midi(t, 0xB0u, 1u, 127u); mod_midi(t, 0xD0u, 127u, 0u); mod_midi(t, 0xB0u, 11u, 30u);   /* MODW, AT at the top, EXPR low (default is full) */
    mod_midi(t, 0xB0u, 2u, 127u); mod_midi(t, 0xB0u, 4u, 127u);                                /* BREATH, FOOT */
    play(&n, 1, MB, 90);
    measure();
    memcpy(L, ml, sizeof ml); memcpy(R, mr, sizeof mr); memcpy(B, mbr, sizeof mbr);
}
static double route_diff(setup_f set, uint32_t s, uint32_t d, int32_t amt)
{
    static double L0[MB], R0[MB], B0[MB], L1[MB], R1[MB], B1[MB];
    int b;
    double w = 0;
    run_route(set, 0, 0, 0, L0, R0, B0);
    run_route(set, s, d, amt, L1, R1, B1);
    for (b = 4; b < MB; b++) {
        double x = fabs(L0[b] - L1[b]), y = fabs(R0[b] - R1[b]), z = fabs(B0[b] - B1[b]);
        if (x > w) w = x;
        if (y > w) w = y;
        if (z > w) w = z;
    }
    return w;
}
/* the per-block value of destination d with the route, as mod_begin writes it (the window it opens around a block): spread over 40 blocks of the held note */
static int32_t value_spread(setup_f set, uint32_t s, uint32_t d, int32_t amt, int *perblock)
{
    uint8_t n = 60;
    track_t *t = &trk[0];
    uint32_t id;
    int b;
    int32_t lo = 1 << 30, hi = -(1 << 30);
    *perblock = !(d == MD_PITCH || d == MD_CUT || d == MD_SHP || d == MD_AMP || d == MD_CUT2 || MD_IS_BUS(d) || d == MD_DEPTH || d == MD_DEPTH2);
    if (!*perblock)
        return 0;
    id = mod_param(d);
    fresh(); patch_plain(t);
    xset(t, X_O1LVL, 100);
    t->p[P_LRATE] = 85; t->p[P_L2RATE] = 80;
    set(t);
    t->p[P_M1SRC] = (int16_t)s; t->p[P_M1DST] = (int16_t)d; t->p[P_M1AMT] = (int16_t)amt;
    trk_note_on(t, n, 100);
    for (b = 0; b < 60; b++) {
        int32_t o[2 * CTL], v;
        mix_block(o, CTL);
        if (b < 20)
            continue;
        if (mod_begin(t)) {
            v = t->p[id];
            mod_end(t);
        } else {
            v = t->p[id];
        }
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    return hi - lo;
}
static void s_none(track_t *t) { (void)t; }
static void s_fa(track_t *t) { t->p[P_FREEA] = 50; t->p[P_FREEB] = 40; }
static void s_fash(track_t *t) { s_fa(t); t->p[P_LRATE] = 127; }   /* (S&H, SLEW: a new value as each LFO cycle starts: a fast LFO so cycles end inside the run) */
static void s_fb(track_t *t) { t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 50; t->p[P_RES2] = 60; XV(t, X_LINK) = 0; }
static void s_blend(track_t *t) { s_fa(t); s_fb(t); t->p[P_CUT2] = 75; t->p[P_FREEA] = 30; }
static void s_drv(track_t *t) { xset(t, X_DATYPE, 1); t->p[P_DRV2] = 40; }
static void s_bdrv(track_t *t) { xset(t, X_DBTYPE, 3); xset(t, X_DBDRV, 40); t->p[P_FREEA] = 60; t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 70; }
static void s_sub(track_t *t) { xset(t, X_SUBLVL, 80); xset(t, X_O1LVL, 40); }
static void s_o2(track_t *t) { xset(t, X_O1LVL, 60); xset(t, X_O2LVL, 100); xset(t, X_O2COARSE, 7); }
static void s_ms(track_t *t) { xset(t, X_O1MODEL, 5); xset(t, X_O1LVL, 110); xset(t, X_O1P1, 60); xset(t, X_O1P2, 127); xset(t, X_O1P3, 127); }
static void s_lfoamp(track_t *t) { t->p[P_LD_AMP] = 40; t->p[P_LD_PIT] = 0; }
static void s_lfo2(track_t *t) { t->p[P_M2SRC] = MS_LFO2; t->p[P_M2DST] = MD_PAN; t->p[P_M2AMT] = 50; }
static void s_env2(track_t *t) { s_fa(t); t->p[P_E2FLT] = 40; }
static void s_fmode(track_t *t) { t->p[P_FREEA] = 55; t->p[P_FREEB] = 50; t->p[P_FREEC] = 0; }
static setup_f dst_setup(uint32_t d)
{
    switch (d) {
    case MD_CUT: case MD_RES: return s_fa;
    case MD_FMODE: return s_fmode;
    case MD_CUT2: case MD_RES2: case MD_FBPAN: return s_fb;
    case MD_BLEND: return s_blend;
    case MD_DRIVE: return s_drv;
    case MD_BDRV: return s_bdrv;
    case MD_SUBLVL: return s_sub;
    case MD_O2PAN: return s_o2;
    case MD_FAPAN: return s_fa;
    case MD_MID: case MD_SIDE: return s_ms;
    case MD_RATE: case MD_DEPTH: case MD_VIB: return s_lfoamp;
    case MD_RATE2: case MD_DEPTH2: return s_lfo2;
    case MD_E2ATK: case MD_E2DEC: case MD_E2SUS: case MD_E2REL: case MD_E2FLT: case MD_E2SHP: return s_env2;
    default: return s_none;
    }
}
static int no_reader(uint32_t d)   /* L/RRY: nothing reads these (PARAM_LIST.md section 3: no engine, effects OFF) */
{
    return d == MD_SHP || (d >= MD_DIST && d <= MD_REV) || MD_ENGINE(d) || (d >= MD_INSA && d <= MD_DUCK) || d == MD_E2SHP || (MD_IS_BUS(d) && d != MD_SPACE);
    /* (SPACE has a reader: PERFORM's SPACE in the master path, fx.c space_goal, which the matrix offsets) */
}
static void sec_matrix(void)
{
    uint32_t d, s, nmove = 0, ndead = 0, bad = 0, vmove = 0, nper = 0;
    char b[200];
    printf("lrry_morph: section 4: the matrix, one slot (SRC -> DST, AMT 60) against the same run without it; largest block difference of level L, level R or brightness (dB)\n");
    for (d = 1; d < MD_N; d++) {
        uint32_t src = (d == MD_RATE || d == MD_DEPTH) ? MS_LFO2 : MS_LFO;
        int per;
        double w = route_diff(dst_setup(d), src, d, 60);
        int32_t sp = value_spread(dst_setup(d), src, d, 60, &per);
        int dead = no_reader(d), moves = w > 0.5;
        printf("lrry_morph: dst %2u %-6s src %-5s sound %6.2f dB %s, per-block value spread %s%d  %s\n", d, N_MDST[d], N_MSRC[src], w, moves ? "moves" : "still",
               per ? "" : "(per voice / bus) ", sp, dead ? "(no reader in L/RRY)" : "");
        if (per && dead) {                             /* (a destination nobody reads: the matrix still writes its value; an engine slot of range 0..0 cannot move) */
            nper++;
            vmove += sp > 0;
        }
        if (dead) {
            ndead++;
            if (moves) bad++;                          /* (a dead one that moves the sound: the list below is wrong) */
        } else {
            nmove += moves;
            if (!moves) bad++;
        }
    }
    snprintf(b, sizeof b, "matrix: the %u destinations the voice reads all move the sound (%u of %u); the %u without a reader do not", 63u - ndead, nmove, 63u - ndead, ndead);
    ck(b, bad == 0);
    printf("lrry_morph: matrix: of the %u per-block destinations without a reader, the matrix still moves the stored value of %u (the rest: engine slots of range 0..0, IMIX at its end)\n", nper, vmove);
    {
        uint32_t ok = 0, nsrc = 0, deadsrc = 0;
        for (s = 1; s < MS_N; s++) {
            double w;
            run_note = s == MS_KEY ? 84 : 60;            /* (KEY is 0 at C4) */
            w = route_diff(s == MS_SH || s == MS_SLEW ? s_fash : s_fa, s, MD_CUT, 60);
            run_note = 60;
            int moves = w > 0.5;
            printf("lrry_morph: src %2u %-5s -> CUT (filter A at 50): %6.2f dB %s%s\n", s, N_MSRC[s], w, moves ? "moves" : "still", s == MS_DUCK ? "  (the ducker does not run on L/RRY: effects OFF)" : "");
            nsrc++;
            if (s == MS_DUCK) {
                deadsrc++;
                ok += !moves;
            } else {
                ok += moves;
            }
        }
        snprintf(b, sizeof b, "matrix: every source moves filter A's cutoff in the render (%u of %u; DUCK stays 0: no ducker on L/RRY)", ok - deadsrc, nsrc - deadsrc);
        ck(b, ok == nsrc);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    sec_tags();
    sec_fader();
    sec_random();
    sec_matrix();
    printf("lrry_morph: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
