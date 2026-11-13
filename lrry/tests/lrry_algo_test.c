/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 56 host check: the ALGORITHM knob (ui_input.c algo_input) and MENU > CONTROL > ALGORITHM KNOB (menu_items.c algo_code): the default is MORPH AMOUNT (the MORPH page's KNOB 4), on every
 * page; the list is MORPH (the default), MORPH AMOUNT, then every matrix destination that is a stored parameter, in the DESTINATION knob's order under the matrix's names; a choice moves that parameter as its
 * card's knob does and says the value in the header; a destination the selection makes do nothing is not moved and the header says why; the row and every value fit the menu's row
 * (a long name on its two lines); the stored byte (settings [15][29]): an older record's 0 or 1 reads as the default.
 * Build (from lrry):  ../onepart.sh 1 1 lrry_algo */
#define UI_TEST_NO_MAIN 1
#ifndef MD_E1
#define MD_E1 0   /* (tests/ui_test.c names a L/RRY matrix destination in a check this file does not run) */
#endif
#ifndef MSG_PC2
#define MSG_PC2 ""
#endif
#include <stdlib.h>
#include "ui_test.c"

static int nfail, ncheck;
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_algo: %-140s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}
static uint32_t page_of(const char *t) { uint32_t i; for (i = 0; i < NPAGES; i++) if (!strcmp(PAGES[i].title, t)) return i; return 0; }
static void choose(uint32_t code) { menu_put(MI_ALGO, algo_index(code)); }

int main(void)
{
    uint8_t l[ALGO_N_MAX];
    uint32_t n, i, d, okl, fit = 1, seen = 0;
    int32_t a0, room = MR_VR - MR_LX, lw;
    ui_power_on();
    frame();

    ui_algo_set = 0;
    ck("default (nothing stored): MORPH, the menu shows it", algo_code() == ALGO_FADER && !strcmp(menu_vname(MI_ALGO, menu_get(MI_ALGO)), "MORPH"));
    ui_algo_set = 1;                                    /* (an older record's STYLE LINE) */
    ck("an older record's [15][29] = 1 (the retired STYLE) reads as the default", algo_code() == ALGO_FADER);
    ui_algo_set = 0x80u | 0x7Fu;
    ck("an unknown stored code reads as the default", algo_code() == ALGO_FADER);
    ui_algo_set = 0;

    n = algo_list(l);
    okl = n >= 3u && l[0] == ALGO_FADER && l[1] == ALGO_AMOUNT;
    for (i = 2; i < n; i++) {
        d = l[i] - ALGO_DST0;
        okl &= d > 0u && d < MD_N && mod_lock_id(d) && mod_lock_id(d) < MOTION_VBASE && !MD_SPARE(d) && d != MD_PITCH && d != MD_AMP && d != MD_TIMBRE;
        seen |= (d == MD_CUT) | (d == MD_O1PAN) << 1 | (d == MD_BDRV) << 2;
    }
    printf("lrry_algo: the list has %u entries (MORPH, MORPH AMOUNT, %u destinations)\n", n, n - 2u);
    ck("the list: MORPH, MORPH AMOUNT, then destinations with a stored parameter only (no OFF, spare, PITCH, AMP LEVEL, TIMBRE); MULTI CUTOFF and OSC 1 PAN in it", okl && (seen & 3u) == 3u);
    ck("menu_n is the list's length, the row is in CONTROL, two units, label ALGORITHM KNOB", menu_n(MI_ALGO) == n && MI_TAB[MI_ALGO] == MTAB_CONTROL && menu_units(MI_ALGO) == 2u &&
       !strcmp(menu_label(MI_ALGO), "ALGORITHM KNOB") && str_len(MI_NAME[MI_ALGO]) <= 12u);
    lw = text_w(&AF_M, menu_label(MI_ALGO));
    for (i = 0; i < n; i++) {                           /* every value fits: on one line beside the label, or each of its two lines */
        const char *v1 = menu_vname(MI_ALGO, i), *t;
        if (lw + MENU_VAL_GAP + text_w(&AF_M, v1) <= room)
            continue;
        t = algo_name(l[i], 0);
        {
            char a[32], *nl;
            snprintf(a, sizeof a, "%s", t);
            nl = strchr(a, '\n');
            if (!nl) { fit = 0; printf("lrry_algo:   no line break and too long: %s\n", v1); continue; }
            *nl = 0;
            if (lw + MENU_VAL_GAP + text_w(&AF_M, a) > room || lw + MENU_VAL_GAP + text_w(&AF_M, nl + 1) > room) {
                fit = 0;
                printf("lrry_algo:   does not fit on two lines: %s\n", v1);
            }
        }
    }
    ck("every value fits the row beside the label (one line, or the matrix name's two lines), at least 8 px apart", fit);

    /* the default: MORPH (the fader) on any page */
    ui.page = (uint8_t)page_of("FILTER");
    frame();
    a0 = (int32_t)morph_fader_of(0);
    turn(EN_ALGO, 3);
    ck("default: ALGORITHM turned right 3 on a part page moves MORPH by 3, the header says it", (int32_t)morph_fader_of(0) == a0 + 3 && !strncmp(ui.msg, "MORPH ", 6) && strncmp(ui.msg, "MORPH AMOUNT", 12));
    printf("lrry_algo: header \"%s\"\n", ui.msg);
    ui.page = (uint8_t)page_of("TEMPO");
    frame();
    turn(EN_ALGO, -3);
    ck("default: on a GLOBAL page too (TEMPO): left 3 moves it back", (int32_t)morph_fader_of(0) == a0);

    /* MORPH AMOUNT */
    choose(ALGO_AMOUNT);
    a0 = (int32_t)morph_amount_of(0);
    turn(EN_ALGO, 3);
    ck("MORPH AMOUNT: right 3 raises it by 3, the header says MORPH AMOUNT", (int32_t)morph_amount_of(0) == a0 + 3 && !strncmp(ui.msg, "MORPH AMOUNT ", 13));
    turn(EN_ALGO, -3);

    /* a parameter: MULTI CUTOFF */
    choose(ALGO_DST0 + MD_CUT);
    {
        int16_t c0 = TSEL->p[P_FREEA] = 40;
        turn(EN_ALGO, 2);
        ck("MULTI CUTOFF: right 2 moves filter A's CUTOFF by 2; the header names it", TSEL->p[P_FREEA] == c0 + 2 && !strncmp(ui.msg, "MULTI CUTOFF ", 13));
        printf("lrry_algo: header \"%s\"\n", ui.msg);
    }
    /* a destination the selection makes do nothing: TWIN CUTOFF with the TWIN FILTER OFF */
    choose(ALGO_DST0 + MD_CUT2);
    TSEL->p[P_TWTYPE] = 0;
    {
        int16_t c0 = TSEL->p[P_CUT2];
        turn(EN_ALGO, 3);
        ck("TWIN CUTOFF with the TWIN FILTER OFF: not moved, the header says THE TWIN FILTER IS OFF", TSEL->p[P_CUT2] == c0 && strstr(ui.msg, "THE TWIN FILTER IS OFF") != 0);
        printf("lrry_algo: header \"%s\"\n", ui.msg);
    }
    /* a pan */
    choose(ALGO_DST0 + MD_O1PAN);
    XV(TSEL, X_O1PAN) = 0;
    turn(EN_ALGO, -4);
    ck("OSC 1 PAN: left 4 moves it to -4", XV(TSEL, X_O1PAN) == -4);
    printf("lrry_algo: header \"%s\"\n", ui.msg);
    ck("the stored byte holds the choice (bit 7 and OSC 1 PAN's code)", ui_algo_set == (0x80u | (ALGO_DST0 + MD_O1PAN)) && menu_get(MI_ALGO) == algo_index(ALGO_DST0 + MD_O1PAN));
    ui_algo_set = 0;
    printf("lrry_algo: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
