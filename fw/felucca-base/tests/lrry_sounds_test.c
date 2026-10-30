/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY factory test sounds (tools/duell_sounds.py, 2026-10-30): each sound loaded as the UI loads it (hostsim.c host_ds_load, MORPH re-based with the sound's own End B),
 * rendered through the real mix path. Build (from felucca-base):  ../onepart.sh 1 1 lrry_sounds
 * Per sound: a held note and an 8-note chord at velocity 100 and 127, fader at A and at B; printed: rms of L and R (dB below the master's full scale), the master peak, the factor
 * of fx.c part_mix_st's int32 product (it wraps at 2^19, LRRY_PHASE44A_REPORT.md open choice 1), the notes the budget gives (budget.c part_notes). STEREO TEST: left and right alone */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int nfail, ncheck;
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_sounds: %-100s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}
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
}
static uint32_t cur;                                    /* the sound being played */
#define NB 2400                                         /* blocks: 1.45 s held, then 0.29 s of release */
#define NOFF 2000
#define NW 140                                          /* the loudness window: the loudest 0.1 s (a held note's body, a pluck's attack) */
static double el[NB], er[NB], ex[NB];
static double sl, sr, sx;                               /* the held part's sums: L, R, L x R (master output) */
static int64_t opk, xpk;                                /* the master output's peak, part_mix_st's factor */
static void run(const uint8_t *notes, int nn, int vel, int fader)
{
    int32_t o[2 * CTL];
    int b, k, i;
    int64_t lv;
    fresh();
    host_ds_load(&trk[0], 0, cur);
    morph_rebase(&trk[0]);                              /* (the sound's End B: its seed and its category's AMOUNT, as a load makes it) */
    mph[0].fader = (uint8_t)fader;
    sl = sr = sx = 0;
    opk = xpk = 0;
    memset(el, 0, sizeof el); memset(er, 0, sizeof er); memset(ex, 0, sizeof ex);
    for (k = 0; k < nn; k++)
        trk_note_on(&trk[0], notes[k], (uint32_t)vel);
    for (b = 0; b < NB; b++) {
        if (b == NOFF)
            for (k = 0; k < nn; k++)
                trk_note_off(&trk[0], notes[k]);
        mix_block(o, CTL);
        lv = LV4(part_lvl(&trk[0]));
        for (i = 0; i < CTL; i++) {
            int64_t xl = ((int64_t)pscr[0].buf[i] * lv) >> 10, xr = ((int64_t)pscr[0].bufR[i] * lv) >> 10, a = o[2 * i] < 0 ? -(int64_t)o[2 * i] : o[2 * i], c = o[2 * i + 1] < 0 ? -(int64_t)o[2 * i + 1] : o[2 * i + 1];
            xl = xl < 0 ? -xl : xl;
            xr = xr < 0 ? -xr : xr;
            if (xl > xpk) xpk = xl;
            if (xr > xpk) xpk = xr;
            if (a > opk) opk = a;
            if (c > opk) opk = c;
            el[b] += (double)o[2 * i] * o[2 * i];
            er[b] += (double)o[2 * i + 1] * o[2 * i + 1];
            ex[b] += (double)o[2 * i] * o[2 * i + 1];
        }
    }
    {
        double wl = 0, wr = 0, wx = 0, best = -1;
        for (b = 0; b < NB; b++) {
            wl += el[b]; wr += er[b]; wx += ex[b];
            if (b >= NW) { wl -= el[b - NW]; wr -= er[b - NW]; wx -= ex[b - NW]; }
            if (b >= NW - 1 && wl + wr > best) { best = wl + wr; sl = wl; sr = wr; sx = wx; }
        }
    }
}
static const char *const LCAT[9] = {"-", "BASS", "LEAD", "PAD", "PLUCK", "KEYS", "PERC", "FX", "OTHER"};   /* (category.c CAT_NAME: not in this build) */
static double db(double s) { return 10.0 * log10(s / ((double)NW * CTL) / (32767.0 * 32767.0) + 1e-20); }

int main(int argc, char **argv)
{
    static const uint8_t one[1] = {48}, chord[8] = {36, 43, 48, 52, 55, 60, 64, 67};
    uint32_t n = ds_count(0), k, ovf = 0, clip = 0, silent = 0;
    double lv1[16];
    (void)argc; (void)argv;
    printf("lrry_sounds: %u factory sounds on engine 0; power-on sound %u (%s)\n", n, TRK_DEF[0][1], ds_name(0, TRK_DEF[0][1]));
    printf("lrry_sounds: %-12s %-5s | note C3 vel 100, loudest 0.1 s: L dB  R dB | 8 notes vel 127, fader A: L dB  R dB  peak  mix factor | fader B: L dB  R dB  mix factor | notes (budget)\n", "", "cat");
    for (k = 0; k < n; k++) {
        double a1, b1, a8, b8, aB, bB;
        int64_t x8, xB, o8, oB;
        uint32_t notes;
        cur = k;
        run(one, 1, 100, 0);
        a1 = db(sl); b1 = db(sr);
        lv1[k] = 10.0 * log10((pow(10, a1 / 10) + pow(10, b1 / 10)) / 2);
        silent += a1 < -60 && b1 < -60;
        run(chord, 8, 127, 0);
        a8 = db(sl); b8 = db(sr); x8 = xpk; o8 = opk;
        notes = part_notes(0);
        run(chord, 8, 127, 127);
        aB = db(sl); bB = db(sr); xB = xpk; oB = opk;
        ovf += x8 >= 524288 || xB >= 524288;
        clip += o8 > 32767 || oB > 32767;
        printf("lrry_sounds: %-12s %-5s |              %6.1f %6.1f |                     %6.1f %6.1f %6lld %7lld %s |  %6.1f %6.1f %7lld %s | %u\n",
               ds_name(0, k), LCAT[ds_cat_of(0, k) % 9u], a1, b1, a8, b8, (long long)o8, (long long)x8, x8 >= 524288 ? "WRAP" : "ok  ", aB, bB, (long long)xB, xB >= 524288 ? "WRAP" : "ok  ", notes);
    }
    ck("8 factory sounds, the power-on sound is one of them", n == 8u && TRK_DEF[0][1] < n);
    ck("every sound is heard (a held C3 above -60 dB on a channel)", silent == 0);
    ck("8 notes at velocity 127, fader at A and at B: no wrap in the part mix (factor under 2^19)", ovf == 0);
    ck("8 notes at velocity 127: the master output stays inside +-32767", clip == 0);
    {   /* loudness: a held C3 within 6 dB of TEST SAW's (TRIM levels them; STEREO TEST and CPU STRESS are tools, not counted) */
        uint32_t far = 0;
        for (k = 0; k < n; k++) {
            printf("lrry_sounds: %-12s held C3 %+5.1f dB against TEST SAW\n", ds_name(0, k), lv1[k] - lv1[0]);
            if (k != 6u && k != 7u && fabs(lv1[k] - lv1[0]) > 6.0)
                far++;
        }
        ck("the playable sounds' held C3 within 6 dB of TEST SAW's", far == 0);
    }
    {   /* STEREO TEST: the saw only left, the pulse only right */
        cur = 6;
        run(one, 1, 100, 0);
        printf("lrry_sounds: STEREO TEST: L %.1f dB, R %.1f dB, L/R correlation %.3f\n", db(sl), db(sr), sx / sqrt(sl * sr + 1e-9));
        ck("STEREO TEST: both channels sound and they are different signals (correlation under 0.3)", db(sl) > -40 && db(sr) > -40 && fabs(sx / sqrt(sl * sr + 1e-9)) < 0.3);
    }
    printf("lrry_sounds: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
