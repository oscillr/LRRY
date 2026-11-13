/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 52 host check: every card label of the firmware obeys owner rule C2 (text that does not fit keeps the same face and weight and gets smaller or tighter, never another weight and never shortened;
 * at the smallest allowed size it wraps onto two lines). The real drawing code (ui_draw.c dot_label / card_paint) on ui_test.c's stub display; every page of the page table, with the TWIN FILTER on every TYPE, both
 * LINK states, both ROUTEs, every OSC MODEL on each oscillator and the dirt units on, and the MATRIX page's DEST card on every destination.
 * Build (from lrry, generated headers in build/gen):
 *   cc -O1 -w -DHOST_MIX_FOLLOWS_MASTER=1 -DLRRY_TEST_MODE=1 -Ibuild/gen_test -Ibuild/gen -Ifirmware/src -Itests -DNPART=1 -DNSHOWN=1 -o lrry_labels tests/lrry_labels_test.c -lm
 *   (MD_E1 is defined in this file: tests/ui_test.c still names a L/RRY matrix destination in a check this file does not run)
 * Output: for each label that does not fit its room at full size: page, knob, label, the width before (S 15, one line), the room, and what the drawing does (tighter spacing, or two lines) with the width after;
 * the count of labels that still do not fit. -DLBL_LIST=1 prints the whole list; the checks always run. */
#define UI_TEST_NO_MAIN 1
#ifndef MD_E1
#define MD_E1 0   /* tests/ui_test.c still names a L/RRY matrix destination in a check this file does not run; the define makes the file build without the command line flag */
#endif
#include <stdlib.h>
static void lh_need(uint32_t c, const char *l, int32_t need, int32_t room);
static void lh_fit(uint32_t c, const char *l, int32_t before, int32_t room, int32_t mode, int32_t trk, int32_t after, int32_t lines);
static void lh_trk(const char *l, int32_t t);
#define CARD_LABEL_HOOK(c, l, need, room) lh_need(c, l, need, room)
#define CARD_LABEL_FIT_HOOK(c, l, before, room, mode, trk, after, lines) lh_fit(c, l, before, room, mode, trk, after, lines)
#define CARD_TRACK_HOOK(l, trk) lh_trk(l, trk)
static void lh_val(uint32_t c, const char *v, int32_t need, int32_t room);
#define CARD_VALUE_HOOK(c, v, need, room) lh_val(c, v, need, room)   /* (values are not part of this check: listed for the report only) */
#include "ui_test.c"

static int nfail, ncheck;
static uint32_t lt_page(const char *t) { uint32_t i; for (i = 0; i < NPAGES; i++) if (!strcmp(PAGES[i].title, t)) return i; return 0; }
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_labels: %-130s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}
#define MAXL 6000
static struct { char scr[48], lab[32]; uint32_t k; int32_t before, room, mode, trk, after, lines; } L[MAXL];
static uint32_t nl;
static char cur[48] = "";
static int find(uint32_t c, const char *l)
{
    uint32_t i;
    for (i = 0; i < nl; i++)
        if (L[i].k == c + 1u && !strcmp(L[i].lab, l) && !strcmp(L[i].scr, cur))
            return (int)i;
    if (nl >= MAXL)
        return -1;
    snprintf(L[nl].scr, sizeof L[nl].scr, "%s", cur);
    snprintf(L[nl].lab, sizeof L[nl].lab, "%s", l);
    L[nl].k = c + 1u;
    L[nl].mode = -1;
    return (int)nl++;
}
static void lh_need(uint32_t c, const char *l, int32_t need, int32_t room)
{
    int i = find(c, l);
    if (i >= 0) { L[i].before = need; L[i].room = room; }
}
static void lh_fit(uint32_t c, const char *l, int32_t before, int32_t room, int32_t mode, int32_t trk, int32_t after, int32_t lines)
{
    int i = find(c, l);
    if (i >= 0) { L[i].before = before; L[i].room = room; L[i].mode = mode; L[i].trk = trk; L[i].after = after; L[i].lines = lines; }
}
static void lh_trk(const char *l, int32_t t) { (void)l; (void)t; }
static struct { char scr[48], v[24]; uint32_t k; int32_t need, room; } V[400];
static uint32_t nv;
static void lh_val(uint32_t c, const char *v, int32_t need, int32_t room)
{
    uint32_t i;
    for (i = 0; i < nv; i++)
        if (V[i].k == c + 1u && !strcmp(V[i].v, v) && !strcmp(V[i].scr, cur))
            return;
    if (nv < 400u) { snprintf(V[nv].scr, 48, "%s", cur); snprintf(V[nv].v, 24, "%s", v); V[nv].k = c + 1u; V[nv].need = need; V[nv].room = room; nv++; }
}

static uint32_t pixhash;                                /* PIXHASH=1: the screen after each page drawn, to compare two builds of ui_draw.c pixel for pixel */
static void one_page(uint32_t i, const char *tag)
{
    snprintf(cur, sizeof cur, "%s%s%s", PAGES[i].title, tag[0] ? " " : "", tag);
    ui.page = (uint8_t)i;
    page_entered();
    ui.force = 1;
    ui_draw();
    if (getenv("PIXHASH")) {
        uint32_t h = 2166136261u, k;
        for (k = 0; k < 240u * 240u; k++) { h ^= host_screen[k]; h *= 16777619u; }
        printf("PIX %s %08x\n", cur, h);
        if (getenv("PIXDUMP") && getenv("PIXOUT") && !strcmp(getenv("PIXDUMP"), cur)) {   /* PIXDUMP="TWIN MOD" PIXOUT=file: the raw RGB565 of that page (the last state drawn), to look at */
            FILE *f = fopen(getenv("PIXOUT"), "wb");
            if (f) { fwrite(host_screen, 2, 240u * 240u, f); fclose(f); }
        }
        pixhash ^= h;
    }
}
static void vary(uint32_t k)
{
    track_t *t = TSEL;
    uint32_t tw = k % 7u, m1 = (k / 7u) % 8u, m2 = (k / 56u) % 8u;
    t->p[P_TWTYPE] = (int16_t)tw;
    XV(t, X_LINK) = (int16_t)(k & 1u);
    XV(t, X_ROUTE) = (int16_t)((k >> 1) & 1u);
    XV(t, X_O1MODEL) = (int16_t)m1; XV(t, X_O2MODEL) = (int16_t)m2;
    XV(t, X_O2LVL) = 90; XV(t, X_NZLVL) = 40; XV(t, X_RGLVL) = 40; XV(t, X_SUBLVL) = 40;
    XV(t, X_DATYPE) = (int16_t)(1 + k % 5u); XV(t, X_DBTYPE) = (int16_t)(1 + (k / 5u) % 5u); XV(t, X_DSER) = (int16_t)((k >> 2) & 1u);
    t->p[P_LD_FLT] = 10; t->p[P_E2FLT] = 10; t->p[P_FREEA] = 50;
}
int main(void)
{
    uint32_t i, k, n, big = 0, still = 0, tight = 0, two = 0, bad_after = 0;
    ui_power_on();
    for (k = 0; k < 448u; k += (k < 56u ? 1u : 5u)) {
        vary(k);
        for (i = 0; i < NPAGES; i++)                /* (every page of the table, also the ones the page order hides in L/RRY: their code stays) */
            one_page(i, page_visible(i) ? "" : "(hidden)");
    }
    for (i = 0; i < NPAGES; i++)                    /* the MATRIX page: the DEST card on every destination, slot 1 set */
        if (strstr(PAGES[i].title, "MATRIX")) {
            for (k = 0; k < MD_N; k++) {
                char tag[24];
                vary(0);
                TSEL->p[P_M1SRC] = 3; TSEL->p[P_M1AMT] = 20; TSEL->p[P_M1DST] = (int16_t)k;
                snprintf(tag, sizeof tag, "DEST %u", k);
                one_page(i, tag);
            }
        }
    {   /* the descriptors: every parameter name as a card label (the page and state independent upper bound): the natural width of its one-line form against the room of the card templates (91 px: templates 1 and 8's 116 wide card) */
        uint32_t d, wide = 0, seen = 0;
        char b[32];
        for (d = 0; d < P_COUNT + NX; d++) {
            const param_desc_t *pd = d < P_COUNT ? &TP[d] : &XTP[d - P_COUNT];
            const char *lb = pd->label ? (d < P_COUNT ? desc_shown(pd) : label_shown(pd->label)) : 0;
            uint32_t j, q = 0;
            int32_t w;
            if (!lb || lb[0] == '-' || !lb[0])
                continue;
            for (j = 0; lb[j] && q + 1u < sizeof b; j++)
                b[q++] = lb[j] == '\n' ? ' ' : lb[j];
            b[q] = 0;
            w = text_w(&AF_S, b);
            seen++;
            if (w > 91) {
                wide++;
                printf("DESC %-3u %-24s one-line width %d (room 91)\n", d, b, w);
            }
        }
        printf("lrry_labels: descriptors: %u labels, %u wider than 91 px on one line\n", seen, wide);
    }
    for (n = 0; n < nl; n++) {
        int32_t room = L[n].room;
        if (L[n].before <= 0)
            continue;
        big++;
        if (L[n].mode < 0) {
            still++;
            printf("LABELCUT %-34s K%u %-24s width %d room %d (no fit record)\n", L[n].scr, L[n].k, L[n].lab, L[n].before, room);
            continue;
        }
        tight += L[n].mode == 1;
        two += L[n].mode == 2;
        if (L[n].mode == 3) {
            still++;
            printf("STILL %-34s K%u %-24s width %d room %d  lines %d widest after %d\n", L[n].scr, L[n].k, L[n].lab, L[n].before, room, L[n].lines, L[n].after);
        } else {
#ifdef LBL_LIST
            printf("FIT %-34s K%u %-24s before %d room %d  %s trk %d/16  after %d  lines %d\n", L[n].scr, L[n].k, L[n].lab, L[n].before, room, L[n].mode == 1 ? "TIGHT" : "TWO LINES", L[n].trk, L[n].after, L[n].lines);
#endif
            bad_after += L[n].after > room + UT_CARD_LBL_PAD_R - 1;   /* (item 158: the ink ends 1 px inside the card, the room's right margin is UT_CARD_LBL_PAD_R) */
        }
    }
    for (n = 0; n < nv; n++)
        printf("VALUECUT %-34s K%u %-24s width %d room %d (a value, not part of this check)\n", V[n].scr, V[n].k, V[n].v, V[n].need, V[n].room);
    {   /* phase 53: GLO's MIXER PAN card text, both formats behind the one switch (ui_layer.c LRRY_PAN_WORDS), and the page structure the owner asked for */
        static const int16_t PV[5] = {-30, 19, 0, -64, 63};
        static const char *const PA[5] = {"-46", "+30", "0", "-100", "+100"}, *const PB[5] = {"LEFT 46", "RIGHT 30", "CENTER", "LEFT 100", "RIGHT 100"};
        const param_desc_t *pd = &XTP[X_O1PAN];
        char b[PV_LEN], msg[160];
        int okA = 1, okB = 1, fitB = 1, okN = 1, fitN = 1, v;
        uint32_t j, firstglo = NPAGES, o1 = NPAGES, o2 = NPAGES, m1 = NPAGES, fb = NPAGES, ringed = 0, abbr = 0;
        for (j = 0; j < 5u; j++) {
            smix_pan_text(pd, PV[j], 0, b);
            okA &= !strcmp(b, PA[j]);
            if (strcmp(b, PA[j])) printf("PAN A %d: %s (want %s)\n", PV[j], b, PA[j]);
            smix_pan_text(pd, PV[j], 1, b);
            okB &= !strcmp(b, PB[j]);
            if (strcmp(b, PB[j])) printf("PAN B %d: %s (want %s)\n", PV[j], b, PB[j]);
        }
        for (v = -64; v <= 63; v++) {                   /* every option B text fits the MIXER card's value room in the value face (L), as L/RRY's number does */
            smix_pan_text(pd, v, 1, b);
            fitB &= text_w(&AF_L, b) <= UT_T4_W - 2 * UT_T4_PAD;
        }
        snprintf(msg, sizeof msg, "MIXER PAN, L/RRY's format (switch 0): -30 -46, +19 +30, 0 0, -64 -100, +63 +100, no %% sign (default now: %d)", LRRY_PAN_WORDS);
        ck(msg, okA);
        ck("MIXER PAN, the words (switch 1): LEFT 46, RIGHT 30, CENTER, LEFT 100, RIGHT 100", okB);
        ck("MIXER PAN, the words: every value -64..+63 fits the card's value room in L, nothing cut", fitB);
        for (j = 0; j < NPAGES; j++) {
            const page_t *pg = &PAGES[j];
            uint32_t s2;
            if (pg->fam == FAM_GLO && firstglo == NPAGES) firstglo = j;
            if (!strcmp(pg->title, "OSC 1")) o1 = j;
            if (!strcmp(pg->title, "TUNING")) o2 = j;
            if (!strcmp(pg->title, "MIXER 1/3")) m1 = j;
            if (!strcmp(pg->title, "FILTER BALANCE")) fb = j;
            if (strstr(pg->title, "PARAL") && !strstr(pg->title, "PARALLEL")) abbr++;
            for (s2 = 0; s2 < 4u; s2++)
                if (pg->fam == FAM_EDIT && pg->id[s2] == XW_BASE + X_O1BAL)   /* (no balance card on EDIT; SUB PAN is back, owner) */
                    ringed++;
            if (pg->graph == GR_SMIX)
                for (s2 = 0; s2 < 4u; s2++) {
                    const char *nm = pg->id[s2] != 0xFFu ? smix_name(pg->id[s2], s2) : 0;
                    if (pg->id[s2] == 0xFFu)
                        continue;
                    okN &= nm != 0 && (strncmp(nm, (s2 >> 1) ? "PAN " : "LEVEL ", (s2 >> 1) ? 4u : 6u) == 0);
                    fitN &= nm && text_w(&AF_S, nm) <= UT_T4_W - UT_T4_PAD - UT_T4_LBL_DX - 4;
                }
        }
        ck("OSC 1 is K1 MODEL + P1 P2 P3 with the model's picture (GR_OSC); TUNING is OSC 1 COARSE, OSC 1 FINE, OSC 2 COARSE, OSC 2 FINE (PHASE58)", o1 < NPAGES && o2 < NPAGES && PAGES[o1].graph == GR_OSC && PAGES[o1].id[0] == XW_BASE + X_O1MODEL &&
           PAGES[o1].id[1] == XW_BASE + X_O1P1 && PAGES[o2].id[0] == XW_BASE + X_O1COARSE && PAGES[o2].id[1] == XW_BASE + X_O1FINE && PAGES[o2].id[2] == XW_BASE + X_O2COARSE && PAGES[o2].id[3] == XW_BASE + X_O2FINE);
        ck("no filter balance card on EDIT", ringed == 0);
        {   /* PHASE58 (owner): the EDIT mixers, the SUB's level, the NOISE type, the pans next to their units, STAGE PAN on GLO */
            const page_t *om = &PAGES[lt_page("OSC MIXER")], *so = &PAGES[lt_page("SUB OSC")], *nz = &PAGES[lt_page("NOISE")], *sm = &PAGES[lt_page("SOURCE MIXER")];
            ck("EDIT > OSC MIXER K1..K4 OSC 1 LEVEL, OSC 2 LEVEL, OSC 1 PAN, OSC 2 PAN; SOURCE MIXER NOISE LEVEL, RING LEVEL, NOISE PAN, RING PAN (GR_SMIX, the MIXER template)",
               om->fam == FAM_EDIT && om->graph == GR_SMIX && om->id[0] == XW_BASE + X_O1LVL && om->id[1] == XW_BASE + X_O2LVL && om->id[2] == XW_BASE + X_O1PAN && om->id[3] == XW_BASE + X_O2PAN &&
               sm->fam == FAM_EDIT && sm->graph == GR_SMIX && sm->id[0] == XW_BASE + X_NZLVL && sm->id[1] == XW_BASE + X_RGLVL && sm->id[2] == XW_BASE + X_NZPAN && sm->id[3] == XW_BASE + X_RGPAN);
            ck("EDIT > SUB OSC K1 WAVE, K2 OCTAVE, K3 SUB LEVEL, K4 SUB PAN; EDIT > NOISE K1 TYPE alone",
               so->id[0] == XW_BASE + X_SUBWAVE && so->id[1] == XW_BASE + X_SUBOCT && so->id[2] == XW_BASE + X_SUBLVL && so->id[3] == XW_BASE + X_SUBPAN && nz->id[0] == XW_BASE + X_NZTYPE && nz->id[1] == 0xFFu && nz->id[2] == 0xFFu && nz->id[3] == 0xFFu);
            ck("pans next to their unit on the FX pages: MULTI FILTER 2/2 K4, TWIN CHAIN K3, DIRT A K4, DIRT B 2/2 K1; GLO > STAGE PAN keeps the four and follows FILTER BALANCE; GLO > MIXER 2/3 K3 is SUB PAN",
               PAGES[lt_page("MULTI FILTER 2/2")].id[3] == XW_BASE + X_FAPAN && PAGES[lt_page("TWIN CHAIN")].id[2] == XW_BASE + X_FBPAN && PAGES[lt_page("DIRT A")].id[3] == XW_BASE + X_DAPAN && PAGES[lt_page("DIRT B 2/2")].id[0] == XW_BASE + X_DBPAN &&
               PAGES[lt_page("STAGE PAN")].fam == FAM_GLO && lt_page("STAGE PAN") == fb + 1u && PAGES[lt_page("MIXER 2/3")].id[2] == XW_BASE + X_SUBPAN);
        }
        ck("GLO's first page is MIXER 1/3, FILTER BALANCE comes right after MIXER 3/3", m1 == firstglo && fb == m1 + 3u && PAGES[fb].fam == FAM_GLO);
        ck("MIXER cards are named as L/RRY's: LEVEL <source> on K1 K2, PAN <source> on K3 K4, in full words, and every name fits its card in S", okN && fitN);
        ck("no page title abbreviates PARALLEL", abbr == 0);
        {   /* phase 54 (owner: the Edit area focuses on the oscillators): DIRT A, DIRT B, STAGE PAN follow TWIN CHAIN in the FX family; EDIT keeps the oscillators and the voice */
            static const char *const FXO[] = {"MULTI FILTER 1/2", "MULTI FILTER 2/2", "TWIN FILTER", "TWIN MOD", "TWIN CHAIN", "DIRT A", "DIRT B 1/2", "DIRT B 2/2"};
            static const char *const EDO[] = {"OSC 1", "OSC 2", "TUNING", "OSC MIXER", "SUB OSC", "NOISE", "SOURCE MIXER", "VOICE 1/2", "VOICE 2/2"};
            uint32_t fx[16], ed[16], nfx = 0, ned = 0, i, okfx = 1, oked = 1, ct[2][3], w, saved = TSEL->p[P_TWTYPE];
            for (i = 0; i < NPAGES; i++) {
                if (PAGES[i].fam == FAM_FX && PAGES[i].scope == SC_TRACK && (PAGES[i].graph == GR_FILT || !strcmp(PAGES[i].title, "DIRT A") || !strcmp(PAGES[i].title, "DIRT B 1/2") || !strcmp(PAGES[i].title, "DIRT B 2/2")) && nfx < 16u)
                    fx[nfx++] = i;
                if (PAGES[i].fam == FAM_EDIT && ned < 16u) ed[ned++] = i;
            }
            okfx = nfx == 8u; oked = ned == 9u;
            for (i = 0; i < 8u && okfx; i++) okfx &= !strcmp(PAGES[fx[i]].title, FXO[i]) && (i == 0 || fx[i] == fx[i - 1u] + 1u);
            for (i = 0; i < 9u && oked; i++) oked &= !strcmp(PAGES[ed[i]].title, EDO[i]);
            ck("FX family, in table order: MULTI FILTER 1/2, 2/2, TWIN FILTER, TWIN MOD, TWIN CHAIN, DIRT A, DIRT B 1/2, DIRT B 2/2 (the last three directly after TWIN CHAIN; STAGE PAN is GLO's)", okfx);
            ck("EDIT family: OSC 1, OSC 2, TUNING, OSC MIXER, SUB OSC, NOISE, SOURCE MIXER, VOICE 1/2, VOICE 2/2 and nothing else", oked);
            for (w = 0; w < 2u; w++) {              /* the footer's counts, TWIN TYPE on (w 0) and OFF (w 1) */
                uint32_t n = 0, e = 0, g = 0;
                TSEL->p[P_TWTYPE] = w ? 0 : 1;
                XV(TSEL, X_DBTYPE) = 1;                    /* (DIRT B on: its 2/2 page has a card) */
                for (i = 0; i < NPAGES; i++) {
                    n += PAGES[i].fam == FAM_FX && page_visible(i);
                    e += PAGES[i].fam == FAM_EDIT && page_visible(i);
                    g += PAGES[i].fam == FAM_GLO && page_visible(i);
                }
                XV(TSEL, X_DBTYPE) = 0;
                ct[w][0] = n; ct[w][1] = e; ct[w][2] = g;
            }
            TSEL->p[P_TWTYPE] = saved;
            snprintf(msg, sizeof msg, "page counts (DIRT B on): FX 8 TWIN on / 6 OFF, +1 FX page; EDIT 9; GLO 10 / 9, +1 COMPRESSOR, +4 effects (got FX %u / %u, EDIT %u / %u, GLO %u / %u)", (unsigned)ct[0][0], (unsigned)ct[1][0], (unsigned)ct[0][1], (unsigned)ct[1][1], (unsigned)ct[0][2], (unsigned)ct[1][2]);
            ck(msg, ct[0][0] == 8u + LRRY_FX_SENDS && ct[1][0] == 6u + LRRY_FX_SENDS && ct[0][1] == 9u && ct[1][1] == 9u && ct[0][2] == 10u + LRRY_COMP + 4u * LRRY_FX_SENDS && ct[1][2] == 9u + LRRY_COMP + 4u * LRRY_FX_SENDS);   /* (LRRY_FX_SENDS: CHORUS, DELAY 1/2, DELAY 2/2, REVERB; SHIMMER only with the reverb TYPE SHIM) */
            ck("DIRT A, DIRT B 1/2, DIRT B 2/2 are no PART FX pages (page_fx_part false); INSERT 1/2 and EQ 1/2 still are", !page_fx_part(&PAGES[lt_page("DIRT A")]) && !page_fx_part(&PAGES[lt_page("DIRT B 1/2")]) && !page_fx_part(&PAGES[lt_page("DIRT B 2/2")]) && page_fx_part(&PAGES[lt_page("INSERT 1/2")]) && page_fx_part(&PAGES[lt_page("EQ 1/2")]));
        }
        {   /* PHASE58 (owner): the part capsule (with the A / B letter) is drawn on every page, GLO's and SCALE's too: no page is a "global screen" (ui_draw.c screen_global), and no page title or card carries the word PART */
            uint32_t pj, okg = 1, okp = 1, sv = ui.page;
            for (pj = 0; pj < NPAGES; pj++) {
                uint32_t s4;
                ui.page = (uint8_t)pj;
                okg &= !screen_global();
                for (s4 = 0; s4 < 4u; s4++) {
                    int16_t *vp2;
                    const param_desc_t *d2;
                    const char *cn = page_card_name(&PAGES[pj], s4);
                    okp &= !cn || !strstr(cn, "PART");
                    d2 = PAGES[pj].id[s4] != 0xFFu && PAGES[pj].scope != SC_GLOBAL ? page_desc(&PAGES[pj], s4, &vp2) : 0;
                    okp &= !d2 || !d2->label || !strstr(d2->label, "PART");
                }
                okp &= !strstr(PAGES[pj].title, "PART");
            }
            ui.page = (uint8_t)sv;
            ck("the capsule is drawn on every page (screen_global is false on all 54+ pages, GLO's included)", okg);
            ck("no page title, card label or card name has the word PART", okp);
        }
        {   /* PHASE58: the quick layers' cards: QUICK OSC (EDIT held), QUICK FX (FX held), QUICK GLO (GLO held): the id of each knob, the name in one line, in the card's room (91 px; GLO's: the MIXER card's) */
            static const uint32_t LL[3] = {LAYER_EDIT, LAYER_FX, LAYER_GLO};
            uint32_t li, s3, st, okq = 1, okn = 1, okfit = 1;
            static const uint16_t WANTID[3][4] = {{XW_BASE + X_O1LVL, XW_BASE + X_O2LVL, XW_BASE + X_SUBLVL, XW_BASE + X_NZLVL}, {P_FREEA, P_CUT2, P_DRV2, XW_BASE + X_DBDRV}, {XW_BASE + X_O1PAN, XW_BASE + X_O2PAN, XW_BASE + X_NZPAN, XW_BASE + X_RGPAN}};
            for (li = 0; li < 3u; li++)
                for (st = 0; st < 2u; st++) {               /* (the dirt units on CRUSH: BITS) */
                    const page_t *qp;
                    XV(TSEL, X_DATYPE) = XV(TSEL, X_DBTYPE) = st ? LRRY_DIRT_CRUSH : 0;
                    qp = ly_page(LL[li]);
                    for (s3 = 0; s3 < 4u; s3++) {
                        const char *nm = quick_label(qp, s3);
                        okq &= qp->id[s3] == WANTID[li][s3];
                        okn &= nm != 0 && !strchr(nm, '\n');
                        if (nm)
                            okfit &= text_w(&AF_S, nm) <= (li == 2u ? UT_T4_W - UT_T4_PAD - UT_T4_LBL_DX - 4 : 91);
                    }
                }
            XV(TSEL, X_DATYPE) = XV(TSEL, X_DBTYPE) = 0;
            ck("quick layers: QUICK OSC = OSC 1, OSC 2, SUB, NOISE LEVEL; QUICK FX = MULTI CUTOFF, TWIN CUTOFF, DIRT A DRIVE, DIRT B DRIVE; QUICK GLO = PAN OSC 1, OSC 2, NOISE, RING", okq);
            ck("quick layers: every card name is one line and fits its card in S (BITS on CRUSH too)", okn && okfit);
        }
        {
            uint32_t mo = NPAGES;
            for (j = 0; j < NPAGES; j++)
                if (PAGES[j].scope == SC_MODS) mo = j;
            ck("GLO > MODULES: PART FX hidden (inert in L/RRY), ARP, SEQ shown; SENDS shown only with the buses built in (core.h LRRY_FX_SENDS)", mo < NPAGES && slot_hidden(&PAGES[mo], 3) && !slot_hidden(&PAGES[mo], 0) && !slot_hidden(&PAGES[mo], 1) && (slot_hidden(&PAGES[mo], 2) == !LRRY_FX_SENDS));
        }
        {
            const param_desc_t *md = &XTP[X_O1MODEL];
            ck("MODEL names on the OSC page in full: TRIANGLE, SUPERSAW", !strcmp(val_shown(md, md->names[1]), "TRIANGLE") && !strcmp(val_shown(md, md->names[5]), "SUPERSAW"));
        }
    }
    printf("lrry_labels: %u pages x states drawn, %u distinct (page, knob, label) seen, %u wider than their room at full size: %u tightened, %u on two lines, %u still do not fit\n", (unsigned)NPAGES, nl, big, tight, two, still);
    ck("every card label that is wider than its room at full size is fitted by tighter spacing or two lines (same face, same weight, never shortened)", still == 0 && bad_after == 0);
    printf("lrry_labels: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
