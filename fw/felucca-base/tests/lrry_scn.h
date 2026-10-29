/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3b: the scenes the one-core / two-core comparison renders. Included by tests/twocore_sim.c (two cores, the unit's protocol on threads) and tests/lrry_split_test.c (the
 * same scenes on one core, no DUELL_TWOCORE: the reference), after hostsim.c. A scene sets a patch on trk[0] (lrry_scn_setup) and then, before every block, notes and parameter moves
 * that depend on the block number and the scene's own random numbers only (lrry_scn_events), never on what the engine did, so the script is the same for every run of a scene.
 * lrry_scn_run renders it through the firmware's mix_block, one block at a time, through a caller's block function (the simulation's runner adds its hand-over to it).
 *   plain     one SAW, random notes (1 to 3 a block, a third of them note-offs), all notes off now and then: note on / off at every position, steals across voices 3 / 4
 *   full      SUPER + PULSE, filter A 24 dB with the cutoff moved every block, filter B SVF HP, dirt A and B, pans, SUB, noise, LFO 1 and the matrix (CUT, OSC 1 PAN, SIDE), random notes
 *   steal     a chord of 6 held, then 20 notes in a run over 8 voices with a long release: steals of released and held voices, across the core boundary
 *   silence   long silence, a single note (voice 0), two notes, a note burst of 8 at once, silence again: blocks where voices 4..7 have nothing to render, then everything
 *   burst     8 notes at once (the note-start stagger: 2 a block), held, released together, again, long release
 *   ladcomb   filter B LADDER, then COMB (from block 700), CRUSH dirt, notes in a rhythm: the COMB lines (engine memory slices of each voice) and the ladder state
 *   mono      a mono mix: every pan 0 and no stereo source (left equals right); a sustained chord with legato retriggers: voices 4..7 add to left and right alike */
#ifndef LRRY_SCN_H
#define LRRY_SCN_H

enum { SC_PLAIN, SC_FULL, SC_STEAL, SC_SILENCE, SC_BURST, SC_LADCOMB, SC_MONO, SC_N };
static const char *const SCN[SC_N] = {"plain", "full", "steal", "silence", "burst", "ladcomb", "mono"};
#define SCN_BLOCKS 2000u

static uint32_t scn_rs;
static uint32_t scn_rnd(void) { scn_rs ^= scn_rs << 13; scn_rs ^= scn_rs >> 17; scn_rs ^= scn_rs << 5; return scn_rs; }
static void scn_x(track_t *t, uint32_t k, int v) { XV(t, k) = (int16_t)v; }

static void scn_fresh(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    memset(eng_mem_owner, 0, sizeof eng_mem_owner);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(lrry_vs, 0, sizeof lrry_vs);
    memset(&pscr, 0, sizeof pscr);
    for (i = 0; i < NLIVE; i++)
        pan_gv[i] = mx_ok[i] = 0;
    mix_l[0] = 0;
    dc_l = dc_r = dce_l = dce_r = 0;
}

static void scn_setup(uint32_t id)
{
    track_t *t = &trk[0];
    scn_fresh();
    scn_rs = 0x1234567u + 7919u * id;
    t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 40;
    t->p[P_LEVEL] = 112; t->p[P_TRIM] = 112; t->p[P_KTRK] = 0; t->p[P_PAN] = 0;
    t->p[P_VOICE] = V_POLY;
    t->p[P_FREEA] = UF_STEPS;
    scn_x(t, X_O1LVL, 70);
    switch (id) {
    case SC_FULL:
        t->p[P_REL] = 60; t->p[P_ATK] = 6; t->p[P_DEC] = 40; t->p[P_SUS] = 90;
        scn_x(t, X_O1MODEL, 5); scn_x(t, X_O1P1, 40); scn_x(t, X_O1P2, 60); scn_x(t, X_O1P3, 90); scn_x(t, X_O1LVL, 80); scn_x(t, X_O1PAN, -20);
        scn_x(t, X_O2MODEL, 3); scn_x(t, X_O2P1, 50); scn_x(t, X_O2LVL, 60); scn_x(t, X_O2PAN, 35); scn_x(t, X_O2FINE, 7);
        scn_x(t, X_NZLVL, 12); scn_x(t, X_NZPAN, 20); scn_x(t, X_SUBLVL, 40);
        t->p[P_FREEA] = 70; t->p[P_FREEB] = 40; t->p[P_FREED] = 2; t->p[P_KTRK] = 20; scn_x(t, X_FAPAN, -30);
        t->p[P_TWTYPE] = 3; t->p[P_CUT2] = 50; t->p[P_RES2] = 30; scn_x(t, X_FBPAN, 40);
        scn_x(t, X_DATYPE, 1); t->p[P_DRV2] = 40; scn_x(t, X_DAPAN, -50); scn_x(t, X_DBTYPE, 3); scn_x(t, X_DBDRV, 30); scn_x(t, X_DSER, 1);
        scn_x(t, X_MID, 70); scn_x(t, X_SIDE, 90);
        t->p[P_LRATE] = 90; t->p[P_LWAVE] = 0;
        t->p[P_M1SRC] = MS_LFO; t->p[P_M1DST] = MD_CUT; t->p[P_M1AMT] = 50;
        t->p[P_M2SRC] = MS_LFO; t->p[P_M2DST] = MD_O1PAN; t->p[P_M2AMT] = 40;
        t->p[P_M3SRC] = MS_LFO; t->p[P_M3DST] = MD_SIDE; t->p[P_M3AMT] = 30;
        break;
    case SC_STEAL:
        t->p[P_REL] = 90; scn_x(t, X_O1MODEL, 1); scn_x(t, X_O2LVL, 50); scn_x(t, X_O2COARSE, 7); scn_x(t, X_O1PAN, -40); scn_x(t, X_O2PAN, 40);
        t->p[P_FREEA] = 80; t->p[P_FREEC] = 0;
        break;
    case SC_SILENCE:
        t->p[P_REL] = 70; scn_x(t, X_O1MODEL, 5); scn_x(t, X_O1P2, 90); scn_x(t, X_O1P3, 100); scn_x(t, X_O1PAN, 25);
        break;
    case SC_BURST:
        t->p[P_REL] = 100; t->p[P_ATK] = 10; scn_x(t, X_O1MODEL, 3); scn_x(t, X_O1P1, 30); scn_x(t, X_O2LVL, 40); scn_x(t, X_O2MODEL, 1); scn_x(t, X_O2PAN, -30);
        t->p[P_FREEA] = 60; t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 70;
        break;
    case SC_LADCOMB:
        t->p[P_REL] = 50; scn_x(t, X_O1MODEL, 0); scn_x(t, X_O1LVL, 90); scn_x(t, X_O2LVL, 40); scn_x(t, X_O2COARSE, 12);
        t->p[P_TWTYPE] = 5; t->p[P_CUT2] = 45; t->p[P_RES2] = 90; scn_x(t, X_FBPAN, 25);
        scn_x(t, X_DATYPE, 5); t->p[P_DRV2] = 70; scn_x(t, X_DAPAN, 30);
        break;
    case SC_MONO:
        t->p[P_REL] = 60; scn_x(t, X_O1MODEL, 0); scn_x(t, X_O1LVL, 100); scn_x(t, X_O2LVL, 60); scn_x(t, X_O2COARSE, 7); scn_x(t, X_O2FINE, 11);
        t->p[P_FREEA] = 75;
        break;
    default:
        break;
    }
}

static void scn_notes_random(uint32_t b, uint32_t nmax)
{
    uint32_t n = scn_rnd() % nmax, k;
    for (k = 0; k < n; k++) {
        uint32_t note = 36u + scn_rnd() % 48u;
        if (scn_rnd() % 3u == 0)
            trk_note_off(&trk[0], note);
        else
            trk_note_on(&trk[0], note, 100);
    }
    if (b % 200u == 199u) {
        uint32_t q;
        for (q = 0; q < 128u; q++)
            trk_note_off(&trk[0], q);
    }
}

/* the notes and parameter moves of block b, called before mix_block */
static void scn_events(uint32_t id, uint32_t b)
{
    track_t *t = &trk[0];
    uint32_t q, k;
    switch (id) {
    case SC_PLAIN:
        scn_notes_random(b, 4u);
        break;
    case SC_FULL:
        scn_notes_random(b, 3u);
        t->p[P_FREEA] = (int16_t)(50 + (int)(30.0 * sin(b * 0.05)));          /* the cutoff moves every block: the patch copy must be fresh */
        t->p[P_CUT2] = (int16_t)(40 + (int)(25.0 * sin(b * 0.031)));
        t->p[P_M1AMT] = (int16_t)(40 + (int)(30.0 * sin(b * 0.011)));
        break;
    case SC_STEAL:
        if (b == 5u)
            for (k = 0; k < 6u; k++)
                trk_note_on(t, 48u + 4u * k, 100);
        if (b >= 120u && b < 220u && b % 5u == 0u)
            trk_note_on(t, 40u + (b - 120u) / 5u * 3u % 50u, 90u + b % 30u);
        if (b == 150u || b == 190u)
            for (k = 0; k < 4u; k++)
                trk_note_off(t, 48u + 4u * k);
        if (b >= 400u && b < 520u && b % 3u == 0u) {
            trk_note_on(t, 50u + scn_rnd() % 30u, 100);
            trk_note_off(t, 50u + scn_rnd() % 30u);
        }
        if (b == 700u || b == 1500u)
            for (q = 0; q < 128u; q++)
                trk_note_off(t, q);
        if (b > 800u && b < 1400u)
            scn_notes_random(b, 3u);
        break;
    case SC_SILENCE:
        if (b == 300u) trk_note_on(t, 52, 100);
        if (b == 420u) trk_note_off(t, 52);
        if (b == 600u) { trk_note_on(t, 60, 100); trk_note_on(t, 67, 100); }
        if (b == 700u) { trk_note_off(t, 60); trk_note_off(t, 67); }
        if (b == 900u)
            for (k = 0; k < 8u; k++)
                trk_note_on(t, 40u + 5u * k, 100);
        if (b == 1100u)
            for (q = 0; q < 128u; q++)
                trk_note_off(t, q);
        if (b == 1500u) { trk_note_on(t, 55, 100); trk_note_on(t, 59, 100); trk_note_on(t, 62, 100); trk_note_on(t, 65, 100); trk_note_on(t, 69, 100); }
        if (b == 1560u) trk_note_off(t, 69);
        if (b == 1600u)
            for (q = 0; q < 128u; q++)
                trk_note_off(t, q);
        break;
    case SC_BURST:
        if (b % 300u == 10u)
            for (k = 0; k < 8u; k++)
                trk_note_on(t, 36u + 5u * k, 100);
        if (b % 300u == 200u)
            for (q = 0; q < 128u; q++)
                trk_note_off(t, q);
        if (b % 300u == 120u)
            trk_note_on(t, 80, 110);
        break;
    case SC_LADCOMB:
        if (b == 700u) { t->p[P_TWTYPE] = 6; t->p[P_CUT2] = 0; t->p[P_RES2] = 40; scn_x(t, X_O1MODEL, 3); }
        if (b % 40u == 0u) {
            uint32_t note = 38u + (b / 40u * 7u) % 36u;
            trk_note_on(t, note, 100);
            trk_note_on(t, note + 7u, 100);
        }
        if (b % 40u == 28u) {
            uint32_t note = 38u + ((b - 28u) / 40u * 7u) % 36u;
            trk_note_off(t, note);
            trk_note_off(t, note + 7u);
        }
        if (b % 400u == 399u)
            for (q = 0; q < 128u; q++)
                trk_note_off(t, q);
        t->p[P_FREEA] = (int16_t)(UF_STEPS - (b % 100u));
        break;
    case SC_MONO:
        if (b == 4u)
            for (k = 0; k < 7u; k++)
                trk_note_on(t, 45u + 3u * k, 100);
        if (b == 900u)
            for (q = 0; q < 128u; q++)
                trk_note_off(t, q);
        if (b > 1000u)
            scn_notes_random(b, 2u);
        break;
    default:
        break;
    }
}

/* the whole scene: setup, then SCN_BLOCKS blocks through blk(out, n); the 64-bit hash of every output sample is returned, out (if not 0) receives the samples */
static uint64_t scn_run(uint32_t id, void (*blk)(int32_t *out, uint32_t n), int32_t *keep)
{
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t b, i;
    int32_t o[2 * CTL];
    scn_setup(id);
    for (b = 0; b < SCN_BLOCKS; b++) {
        scn_events(id, b);
        blk(o, CTL);
        for (i = 0; i < 2u * CTL; i++)
            h = (h ^ (uint32_t)o[i]) * 0x100000001B3ull;
        if (keep)
            memcpy(keep + (size_t)b * 2u * CTL, o, sizeof o);
    }
    return h;
}
#endif
