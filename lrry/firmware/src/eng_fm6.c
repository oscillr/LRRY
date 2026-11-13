/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* L/RRY phase 4.4a: DESCRIPTOR ONLY. The engine's DSP (note-on, render and their helpers) is removed: the L/RRY voice (lrry_voice.c) is the only sound source. What stays is what the pages,
 * the editor, the stores and budget.c read (names, the EDIT cards, presets, the voice cap and flags, the page graphs' helpers). The description below is of the engine as L/RRY had it */
/* FM6: classic 6-operator FM. The synthesis is msfa (Dexed's core), ported to integer C in fm6_core.c
 * (Apache-2.0); this file is the Felucca engine around it.
 *
 * The patch is the sound: every track has one (fm6_patch, the generic 155-byte single-voice layout: six
 * operators with their 4-rate / 4-level envelopes, keyboard level and rate scaling, velocity, ratio or
 * fixed frequency, detune; 32 algorithms, feedback, LFO, pitch envelope, transpose). It is edited in the
 * web editor (EDITOR_PROTOCOL.md FM6_*: a .syx voice sent to a track), loaded from the factory slots, and saved inside
 * projects (FUN8) and user presets (up_fm6.c). Whatever is loaded becomes the track's patch.
 * On the device the eight EDIT values are macros on top of it:
 *   ALG   PAT = the patch's algorithm, 1..32 another one
 *   FB    added to the patch's feedback (0..7)
 *   MLVL  the output level of every operator that is not a carrier (-36 .. +36 dB): the brightness
 *   MRAT  added to the coarse ratio of the modulators (ratio mode only)
 *   MEG   the modulators' envelope times: + slower (rates down to 40 steps), - faster
 *   VMOD  added to the modulators' velocity sensitivity (0..7)
 *   DTUN  spreads the carriers apart in pitch (up to about +-36 cents between the outer ones)
 *   SLOT  F1..F8 load a factory patch; OWN (8) is the track's own patch: what a project, a user preset or the
 *         editor put there. A load shows F n when the patch is that factory one unchanged, else OWN (fm6_adopt).
 *         B1..B32 (9..40, 1.4.1) load a voice of the 32-voice bank in flash (fm6_vbank.c) as F n do; a bank slot
 *         without a voice is skipped by the knob (params.c enum_step) and refused anywhere else (fm6_poll).
 *         Turning SLOT from OWN to F n / B n keeps the own patch aside; back to OWN brings it back (fm6_poll).
 *         Before 1.0.3 the values 8..34 were F1..F8 then B1..B27, a flash bank (retired: its patches moved into
 *         the user presets, up_fm6.c). A stored value is never a bank slot: projects, user presets and motion
 *         load 9..40 as 8, OWN (params.c param_fit), and the patch comes with the project / the record. So a
 *         project or a preset made with a bank voice sounds the same after the bank is replaced
 * The operator envelopes are the voice's amplitude and end it (engine_t.ownenv / done): the track's ADSR curve,
 * ENV DEST and the matrix's ENV do nothing here. L/RRY 1.1 (owner 2026-10-25): the four AMP ENV knobs ATTACK DECAY
 * SUSTAIN RELEASE move the envelopes of the carrier operators by their distance from the numbers the patch gives
 * (fm6_env_derive, fm6_sync); a knob on that number leaves the DX7 voice exactly as it is. The track's FLT moves MLVL (ENV / LFO -> FLT, the
 * matrix's CUT), SHP the feedback, PIT the pitch (bend, glide, tune: the voice's pitch and fine factor).
 * Six voices per part at most (engine_t.poly).
 *
 * State: per voice an fm6_note_t (in the part's engine memory as PHYS's slots, engines.c eng_mem; fm6_note_of:
 * voice_t.s[] is too small), per part the patch, the patch through the macros (fm6_eff, rebuilt in the audio ISR when either
 * changes) and the LFO (once a block). */
#include "fm6_layout.c"         /* (L/RRY 4.4a: what is left of msfa's port: the patch layout and the 32 algorithms, which the FM6 pages and the stores read; the synthesis core is gone) */
#include "lrry_fm6.h"         /* tools/gen_fm6_patches.py: FM6_INIT, FM6_FACTORY[] */

#define ENGI_FM6 12u             /* engines.c ENGINES[] (append-only) */
#define FM6_POLY 6               /* engine_t.poly */
#define FM6_OWN FM6_NFACTORY     /* SLOT: the track's own patch (F1..F8 before it) */
#define FM6_NBANK 32u            /* SLOT: B1..B32 after OWN, the voice bank (fm6_vbank.c) */
/* L/RRY 0.2.17: FM6_NXF more factory patches (lrry_fm6.h FM6_XFACTORY: F9, F10) at SLOT 41 and up, AFTER B32, so the SLOT values of
 * F1..F8, OWN (8) and B1..B32 (9..40) mean what they meant. Like a bank slot a stored value is never one of them (params.c param_fit:
 * above OWN is OWN, the patch comes with the project / the record), the factory sounds that use them (FM BASS 2, TUBE BELLS) load them */
#define FM6_XBASE (FM6_OWN + FM6_NBANK + 1u)
#define FM6_PACKED 128u

static uint8_t fm6_patch[NTRK][FP_SIZE + 1u];   /* the tracks' patches (main loop writes, then fm6_pgen) */
static volatile uint8_t fm6_pgen[NTRK];          /* +1 after each write of fm6_patch[t] */
static uint8_t fm6_slot[NTRK];                   /* the SLOT value last loaded (main loop); 0xFF = none */
static uint8_t fm6_own[NTRK][FM6_PACKED] __attribute__((section(".pool")));   /* the own patch while SLOT shows a factory one (fm6_poll) */
static uint8_t fm6_own_ok;                       /* bit tr: fm6_own[tr] holds it */
static struct {                                  /* the patch through the macros: the audio ISR's copy */
    uint8_t p[FP_SIZE + 1u];
    uint8_t gen, alg, fb, ok;
    int16_t e[7];                                /* P_E0..P_E6 it was made with */
    int16_t env[4];                              /* the AMP ENV knobs (P_ATK, P_DEC, P_SUS, P_REL) it was made with (fm6_env_derive) */
    uint32_t mk;                                 /* the morph's key it was made with (fm6_morph: 0 = none) */
    int32_t dt[6];                               /* DTUN: per operator, Q24 log2 */
} fm6_eff[NLIVE]; /* this block's LFO value and delay (Q24) */

/* the voice bank (fm6_vbank.c; in the pool, zeroed at boot: no bank without the store) */
static uint32_t fm6_bank_used __attribute__((section(".pool")));   /* bit k: B k+1 holds a voice */
static int (*fm6_bank_read)(uint32_t k, uint8_t *pk) __attribute__((section(".pool")));   /* voice k -> pk, 0 = read */

/* SLOT value s is a bank slot (B1..B32): its voice number, else -1 */
static int32_t fm6_bank_slot(int32_t s)
{
    return s > (int32_t)FM6_OWN && s <= (int32_t)(FM6_OWN + FM6_NBANK) ? s - (int32_t)FM6_OWN - 1 : -1;
}
/* SLOT value s is one of the factory patches behind the bank (F9..): its number in FM6_XFACTORY, else -1 */
static int32_t fm6_xslot(int32_t s)
{
    return s >= (int32_t)FM6_XBASE && s < (int32_t)(FM6_XBASE + FM6_NXF) ? s - (int32_t)FM6_XBASE : -1;
}
/* SLOT value s can load: a factory slot (F1..F8, F9..), OWN, or a bank slot with a voice */
static int fm6_slot_ok(int32_t s)
{
    int32_t k = fm6_bank_slot(s);
    return k < 0 ? (s >= 0 && s <= (int32_t)FM6_OWN) || fm6_xslot(s) >= 0 : (int)((fm6_bank_used >> k) & 1u);
}

/* ------------------------------------------------------- patch formats --- */
/* the highest value of each byte of the 155-byte voice */
static uint32_t fm6_max(uint32_t i)
{
    static const uint8_t OPMAX[21] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
    static const uint8_t VMAX[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
    if (i < 126u)
        return OPMAX[i % 21u];
    if (i < FP_NAME)
        return VMAX[i - 126u];
    return 126u;
}

/* every value inside its range (a name byte outside 32..126 becomes a space) */
static void fm6_sanitize(uint8_t *v)
{
    uint32_t i;
    for (i = 0; i < FP_SIZE; i++) {
        if (i >= FP_NAME)
            v[i] = v[i] < 32u || v[i] > 126u ? ' ' : v[i];
        else if (v[i] > fm6_max(i))
            v[i] = (uint8_t)fm6_max(i);
    }
    v[FP_SIZE] = 0;
}

/* 128-byte packed record (the 32-voice bank's) -> 155-byte voice; bits a record does not use are ignored */
static void fm6_unpack(const uint8_t *b, uint8_t *v)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = b + k * 17u;
        uint8_t *d = v + k * FP_OP;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[FP_LC] = o[11] & 3u;
        d[FP_RC] = (o[11] >> 2) & 3u;
        d[FP_RS] = o[12] & 7u;
        d[FP_DET] = (o[12] >> 3) & 15u;
        d[FP_AMS] = o[13] & 3u;
        d[FP_KVS] = (o[13] >> 2) & 7u;
        d[FP_OL] = o[14] & 0x7Fu;
        d[FP_MODE] = o[15] & 1u;
        d[FP_FC] = (o[15] >> 1) & 31u;
        d[FP_FF] = o[16] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        v[FP_PR1 + i] = b[102 + i] & 0x7Fu;              /* pitch EG, algorithm */
    v[FP_ALG] &= 31u;
    v[FP_FB] = b[111] & 7u;
    v[FP_OKS] = (b[111] >> 3) & 1u;
    for (i = 0; i < 4u; i++)
        v[FP_LFS + i] = b[112 + i] & 0x7Fu;
    v[FP_LKS] = b[116] & 1u;
    v[FP_LFW] = (b[116] >> 1) & 7u;
    v[FP_LPMS] = (b[116] >> 4) & 7u;
    v[FP_TRNSP] = b[117] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        v[FP_NAME + i] = b[118 + i] & 0x7Fu;
    fm6_sanitize(v);
}

/* 155-byte voice -> 128-byte packed record (7-bit bytes: it travels in SysEx as it is) */
static void fm6_pack(const uint8_t *v, uint8_t *b)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = v + k * FP_OP;
        uint8_t *d = b + k * 17u;
        for (i = 0; i < 11u; i++)
            d[i] = o[i] & 0x7Fu;
        d[11] = (uint8_t)((o[FP_LC] & 3u) | (o[FP_RC] & 3u) << 2);
        d[12] = (uint8_t)((o[FP_RS] & 7u) | (o[FP_DET] & 15u) << 3);
        d[13] = (uint8_t)((o[FP_AMS] & 3u) | (o[FP_KVS] & 7u) << 2);
        d[14] = o[FP_OL] & 0x7Fu;
        d[15] = (uint8_t)((o[FP_MODE] & 1u) | (o[FP_FC] & 31u) << 1);
        d[16] = o[FP_FF] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++)
        b[102 + i] = v[FP_PR1 + i] & 0x7Fu;
    b[110] &= 31u;
    b[111] = (uint8_t)((v[FP_FB] & 7u) | (v[FP_OKS] & 1u) << 3);
    for (i = 0; i < 4u; i++)
        b[112 + i] = v[FP_LFS + i] & 0x7Fu;
    b[116] = (uint8_t)((v[FP_LKS] & 1u) | (v[FP_LFW] & 7u) << 1 | (v[FP_LPMS] & 7u) << 4);
    b[117] = v[FP_TRNSP] & 0x7Fu;
    for (i = 0; i < 10u; i++)
        b[118 + i] = v[FP_NAME + i] & 0x7Fu;
}

/* ------------------------------------------- the AMP ENV knobs (L/RRY 1.1) --- */
/* Owner 2026-10-25 (FM6_ENV_MAP_V2.md): the part's AMP ENV knobs ATTACK, DECAY, SUSTAIN, RELEASE carry and move the envelopes of the CARRIER operators. The knob's stored value T (0..127, as on
 * every engine: a higher value is longer or higher) is compared with D, the value the loaded patch itself gives (fm6_env_derive: the numbers of the loudest carrier); the sound changes only by
 * T - D, so a knob on D is bit for bit the DX7 voice. The knob shows the DX7 number 0..99 (params.c F_N99): ATTACK, DECAY and RELEASE as 99 minus the DX7 rate (the knob inverts the rate), SUSTAIN
 * as the DX7 level L3. Nothing per sample: D is made in the main loop (fm6_env_look) and in fm6_sync only when something changed */
static int32_t fm6_rate_d(int32_t r) { return ((99 - r) * 127 + 49) / 99; }    /* a DX7 rate -> the knob's stored value (a higher value is slower) */
static int32_t fm6_level_d(int32_t l) { return (l * 127 + 49) / 99; }          /* a DX7 level -> the knob's stored value */
/* the four stored values patch p (155 bytes) gives with algorithm alg: those of its loudest carrier (the highest output level FP_OL; a tie: the one fm6_sync visits first, OP1's side).
 * ATTACK from R1; DECAY from the falling stage with the bigger drop (L1 to L2: R2, L2 to L3: R3), R2 on a tie or no drop; SUSTAIN from L3; RELEASE from R4 */
static void fm6_env_derive(const uint8_t *p, uint32_t alg, int32_t *d)
{
    uint32_t car = fm6_carriers(alg), k, best = 0;
    int32_t top = -1, drop2, drop3;
    const uint8_t *op;
    for (k = 0; k < 6u; k++) {
        uint32_t ki = 5u - k;
        if (((car >> ki) & 1u) && (int32_t)p[ki * FP_OP + FP_OL] > top) {
            top = p[ki * FP_OP + FP_OL];
            best = ki;
        }
    }
    op = p + best * FP_OP;
    drop2 = (int32_t)op[FP_L1] - (int32_t)op[FP_L1 + 1];
    drop3 = (int32_t)op[FP_L1 + 1] - (int32_t)op[FP_L1 + 2];
    d[0] = fm6_rate_d(op[FP_R1]);
    d[1] = fm6_rate_d(drop3 > drop2 ? op[FP_R1 + 2] : op[FP_R1 + 1]);
    d[2] = fm6_level_d(op[FP_L1 + 2]);
    d[3] = fm6_rate_d(op[FP_R1 + 3]);
}
static uint8_t fm6_dlast[NTRK][4];               /* the numbers the patch gave when the main loop last looked (not saved) */
static uint8_t fm6_dok;                          /* bit tr: fm6_dlast[tr] is valid */
enum { FM6_LOOK_FOLLOW, FM6_LOOK_NEW, FM6_LOOK_KEEP };
/* main loop: the knobs of part tr against its patch. NEW (a sound was loaded): the knobs go on the numbers of the new patch. KEEP (a project or a user preset brought its own knobs with its patch):
 * the knobs stay, the numbers are noted. FOLLOW (every main loop pass): when an edit of the patch or the ALG macro moved the numbers, every knob moves by the same amount (clamped to 0..127), so a knob
 * that was on the number still is, and a knob that had been moved keeps its distance. A part that was not looked at before is KEEP */
static void fm6_env_look(uint32_t tr, int mode)
{
    static const uint8_t ID[4] = {P_ATK, P_DEC, P_SUS, P_REL};
    track_t *t;
    int32_t d[4];
    uint32_t k, e0;
    tr %= NTRK;
    t = &trk[tr];
    if (t->eng_req != ENGI_FM6)
        return;
    e0 = (uint32_t)t->p[P_E0];
    fm6_env_derive(fm6_patch[tr], e0 >= 1u && e0 <= 32u ? e0 - 1u : fm6_patch[tr][FP_ALG] & 31u, d);
    if (mode == FM6_LOOK_FOLLOW && !((fm6_dok >> tr) & 1u))
        mode = FM6_LOOK_KEEP;
    for (k = 0; k < 4u; k++) {
        if (mode == FM6_LOOK_NEW)
            t->p[ID[k]] = (int16_t)d[k];
        else if (mode == FM6_LOOK_FOLLOW && d[k] != (int32_t)fm6_dlast[tr][k])
            t->p[ID[k]] = (int16_t)clamp(t->p[ID[k]] + d[k] - (int32_t)fm6_dlast[tr][k], 0, 127);
        fm6_dlast[tr][k] = (uint8_t)d[k];
    }
    fm6_dok |= (uint8_t)(1u << tr);
}
/* a card is one of part t's four AMP ENV knobs on FM6: 0..3 (ATTACK .. RELEASE), else -1 (the reset puts such a knob on the number the patch gives, ui_input.c reset_desc) */
static int32_t fm6_env_knob(const track_t *t, const int16_t *vp)
{
    if (t->eng_req != ENGI_FM6 || vp < &t->p[P_ATK] || vp > &t->p[P_REL])
        return -1;
    return (int32_t)(vp - &t->p[P_ATK]);
}
/* the number the patch gives for part t's knob k */
static int32_t fm6_env_value(const track_t *t, uint32_t k)
{
    int32_t d[4];
    uint32_t tr = (uint32_t)(t - trk) % NTRK, e0 = (uint32_t)t->p[P_E0];
    fm6_env_derive(fm6_patch[tr], e0 >= 1u && e0 <= 32u ? e0 - 1u : fm6_patch[tr][FP_ALG] & 31u, d);
    return d[k & 3u];
}

/* ---------------------------------------------------- the track's patch --- */
/* track tr's patch = v (155 bytes, sanitized). Main loop: the ISR takes it at its next block */
static void fm6_set_patch(uint32_t tr, const uint8_t *v)
{
    uint8_t s[FP_SIZE + 1u];
    tr %= NTRK;
    memcpy(s, v, FP_SIZE);
    fm6_sanitize(s);                                /* not in place: fm6_sync must never copy an unsanitized byte */
    memcpy(fm6_patch[tr], s, FP_SIZE + 1u);
    RING_PUBLISH();
    fm6_pgen[tr]++;
}

/* SLOT value s < FM6_NFACTORY: that factory patch into track tr; a bank slot: its voice (0 = loaded; 1 = the bank
 * has none there, nothing changed) */
static int fm6_load_slot(uint32_t tr, uint32_t s)
{
    uint8_t v[FP_SIZE + 1u], pk[FM6_PACKED];
    int32_t k = fm6_bank_slot((int32_t)s), x = fm6_xslot((int32_t)s);
    if (x >= 0) {
        memcpy(pk, FM6_XFACTORY[x], FM6_PACKED);
    } else if (k < 0) {
        s %= FM6_NFACTORY;
        memcpy(pk, FM6_FACTORY[s], FM6_PACKED);
    } else if (!fm6_slot_ok((int32_t)s) || !fm6_bank_read || fm6_bank_read((uint32_t)k, pk)) {
        return 1;
    }
    fm6_unpack(pk, v);
    fm6_set_patch(tr, v);
    fm6_slot[tr % NTRK] = (uint8_t)s;
    fm6_env_look(tr, FM6_LOOK_NEW);                  /* a sound was loaded: the AMP ENV knobs show what its carriers do */
    return 0;
}

/* the SLOT value a patch shows: the factory patch it equals unchanged (F n; the one at the SLOT s it had first, then on from there;
 * then F9.. behind the bank), else OWN. The search unpacks up to FM6_NFACTORY + FM6_NXF patches: a project load does it before it
 * turns the interrupts off (project.c) */
static int32_t fm6_adopt_slot(const uint8_t *patch, int32_t s)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t k, f = s >= 0 && s < (int32_t)FM6_NFACTORY ? (uint32_t)s : 0u;
    for (k = 0; k < FM6_NFACTORY; k++) {
        fm6_unpack(FM6_FACTORY[(f + k) % FM6_NFACTORY], v);
        if (!memcmp(v, patch, FP_SIZE))
            return (int32_t)((f + k) % FM6_NFACTORY);
    }
    for (k = 0; k < FM6_NXF; k++) {
        fm6_unpack(FM6_XFACTORY[k], v);
        if (!memcmp(v, patch, FP_SIZE))
            return (int32_t)(FM6_XBASE + k);
    }
    return (int32_t)FM6_OWN;
}

/* track tr's patch was just put in as its own (a project, a user preset, the editor, a conversion): an FM6 track's
 * SLOT shows the factory patch it equals unchanged (F n; the SLOT it had first), else OWN; nothing reloads over it
 * (fm6_poll) */
static void fm6_adopt(uint32_t tr)
{
    track_t *t = &trk[tr % NTRK];
    int32_t s = t->p[P_E7];
    tr %= NTRK;
    fm6_own_ok &= (uint8_t)~(1u << tr);
    if (t->eng_req == ENGI_FM6) {
        s = fm6_adopt_slot(fm6_patch[tr], s);
        t->p[P_E7] = (int16_t)s;
    }
    fm6_slot[tr] = (uint8_t)s;
    fm6_env_look(tr, FM6_LOOK_KEEP);                 /* (a project, a user preset: its own AMP ENV knobs stay) */
}

/* a factory sound load put a SLOT value in (a preset, an engine change): its patch (OWN: the patch stays) */
static void fm6_track_loaded(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    if (tr < NTRK && t->eng_req == ENGI_FM6) {
        fm6_own_ok &= (uint8_t)~(1u << tr);
        if ((t->p[P_E7] >= 0 && t->p[P_E7] < (int32_t)FM6_NFACTORY) || fm6_xslot(t->p[P_E7]) >= 0)
            fm6_load_slot(tr, (uint32_t)t->p[P_E7]);
        else
            fm6_adopt(tr);
        fm6_env_look(tr, FM6_LOOK_NEW);              /* (a factory sound: the AMP ENV knobs on the numbers of its patch) */
    }
}

/* power-on: every track the init voice (what a project stores for the tracks that never played FM6) */
static void fm6_init(void)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t tr;
    fm6_unpack(FM6_INIT, v);
    for (tr = 0; tr < NTRK; tr++) {
        fm6_set_patch(tr, v);
        fm6_slot[tr] = 0xFFu;
    }
}

/* main loop: SLOT turned (a knob, the editor, MIDI, motion) -> that factory patch or bank voice; from OWN the own
 * patch is kept aside (fm6_own), and turning back to OWN brings it back. A bank slot without a voice (the editor, a
 * MIDI CC) is refused: SLOT goes back to what it was, nothing loads */
static void fm6_poll(void)
{
    uint32_t tr, bit;
    for (tr = 0; tr < NTRK; tr++) {
        int32_t s = trk[tr].p[P_E7];
        bit = 1u << tr;
        if (trk[tr].eng_req != ENGI_FM6) {
            fm6_dok &= (uint8_t)~bit;
            continue;
        }
        fm6_env_look(tr, FM6_LOOK_FOLLOW);           /* (the AMP ENV knobs follow an edit of the patch or the ALG macro) */
        if (s == fm6_slot[tr])
            continue;
        if (s < 0 || (s >= (int32_t)FM6_NFACTORY && fm6_bank_slot(s) < 0 && fm6_xslot(s) < 0)) {   /* OWN */
            if (fm6_own_ok & bit) {
                uint8_t v[FP_SIZE + 1u];
                fm6_unpack(fm6_own[tr], v);
                fm6_set_patch(tr, v);
            }
            fm6_own_ok &= (uint8_t)~bit;
            fm6_slot[tr] = FM6_OWN;
            if (s != FM6_OWN)
                trk[tr].p[P_E7] = FM6_OWN;
            continue;
        }
        if (fm6_slot[tr] == FM6_OWN && !(fm6_own_ok & bit) && fm6_slot_ok(s)) {
            fm6_pack(fm6_patch[tr], fm6_own[tr]);
            fm6_own_ok |= (uint8_t)bit;
        }
        if (fm6_load_slot(tr, (uint32_t)s))                  /* a bank slot without a voice: SLOT back */
            trk[tr].p[P_E7] = (int16_t)(fm6_slot[tr] <= FM6_OWN + FM6_NBANK + FM6_NXF ? fm6_slot[tr] : FM6_OWN);
    }
}

/* --------------------------------------------------------------- voice --- */
static fm6_note_t *fm6_note_of(track_t *t, voice_t *v)
{
    uint32_t i;
    if (t < &trk[0] || t >= &trk[NLIVE])
        return 0;
    i = (uint32_t)(v - t->v);
    return i < FM6_POLY ? (fm6_note_t *)ENG_MEM(t - trk) + i : 0;
}

/* the SUB LAYER's follow (chain_sub.c; FM6 owns its amplitude, the operators' envelopes are inside its signal): voice v's level as the block just rendered left it, 0..1 as Q15. It is the loudest
 * carrier's gain_out (fm6_note_t, Q24: the operator's linear amplitude, which fm6_op_run multiplies the sine by) against 2^25, the gain of a carrier at full level (one carrier at full: VOICE_FS / 2
 * at the output, as fm6_render's comment has it). Read only; once a block, after fm6_render */
static __attribute__((noinline)) int32_t fm6_sub_follow(track_t *t, voice_t *v)
{
    const fm6_note_t *n = fm6_note_of(t, v);
    uint32_t car, k;
    int32_t g = 0;
    if (!n || !n->live)
        return 0;
    car = fm6_carriers(fm6_eff[t - trk].alg);
    for (k = 0; k < 6u; k++)
        if ((car >> k & 1u) && n->op[k].gain_out > g)
            g = n->op[k].gain_out;
    return g >= (1 << 25) ? 32767 : g >> 10;
}

/* --------------------------------------------------------- the engine --- */
static const char *const N_FM6_ALG[] = {"PAT", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13",
                                        "14", "15", "16", "17", "18", "19", "20", "21", "22", "23", "24", "25",
                                        "26", "27", "28", "29", "30", "31", "32", 0};
static const char *const N_FM6_PATCH[] = {"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "OWN",
                                          "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "B9", "B10", "B11", "B12",
                                          "B13", "B14", "B15", "B16", "B17", "B18", "B19", "B20", "B21", "B22", "B23",
                                          "B24", "B25", "B26", "B27", "B28", "B29", "B30", "B31", "B32", "F9", "F10", "F11", 0};
_Static_assert(NELEM(N_FM6_PATCH) == FM6_OWN + FM6_NBANK + FM6_NXF + 2u, "a SLOT name per value");

/* {ALG, FB, MLVL, MRAT, MEG, VMOD, DTUN, SLOT}: the factory patch F1..F8 as it is, DTUN on the pad */
static const preset_t FM6_PRESETS[] = {
    {"TINE EP", {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 127, 0}, 0, 0, FX(0, 45, 25, 35), PAT(6)},
    {"BELL", {0, 0, 0, 0, 0, 0, 0, 1}, {0, 0, 127, 0}, 0, 0, FX(0, 10, 30, 70), PAT(7)},
    {"FM BASS", {0, 0, 0, 0, 0, 0, 0, 2}, {0, 0, 127, 0}, 0, 1, FX(0, 0, 10, 10), PAT(2)},
    {"BRASS", {0, 0, 0, 0, 0, 0, 0, 3}, {0, 0, 127, 0}, 0, 0, FX(0, 25, 20, 40), PAT(4)},
    {"PAD", {0, 0, 0, 0, 0, 0, 30, 4}, {0, 0, 127, 0}, 0, 0, FX(0, 60, 30, 70), PAT(5)},
    {"MARIMBA", {0, 0, 0, 0, 0, 0, 0, 5}, {0, 0, 127, 0}, 0, 0, FX(0, 0, 25, 40), PAT(3)},
    {"ORGAN", {0, 0, 0, 0, 0, 0, 0, 6}, {0, 0, 127, 0}, 0, 0, FX(10, 40, 0, 30), PAT(6)},
    {"PLUCK", {0, 0, 0, 0, 0, 0, 0, 7}, {0, 0, 127, 0}, 0, 0, FX(0, 20, 35, 30), PAT(13)},
};

/* 0.2.18 (page-order audit, finding 1): the cards show SLOT first (it loads a whole patch and so replaces what the cards after it hold), then ALG FB MLVL / MRAT MEG VMOD DTUN.
 * Card position -> E index; the stored ids E1..E8 (P_E0..P_E0+7) do not move, nor do patches, multis, projects, the matrix's E1..E8 or MIDI LEARN */
static const uint8_t FM6_UIORD[8] = {7, 0, 1, 2, 3, 4, 5, 6};
static const engine_t ENG_FM6 = {
    .name = "FM6",
    .page_title = {"PATCH", "MACRO"},   /* (EDIT 1: SLOT ALG FB MLVL; EDIT 2: MRAT MEG VMOD DTUN) */
    .edit = {
        {"ALG", F_INT, 0, 32, 0, N_FM6_ALG, 0},
        {"FB", F_INT, -7, 7, 0, 0, 0},
        {"MLVL", F_BIPCT, -64, 63, 0, 0, 0},
        {"MRAT", F_INT, -16, 16, 0, 0, 0},
        {"MEG", F_BIPCT, -64, 63, 0, 0, 0},
        {"VMOD", F_INT, -7, 7, 0, 0, 0},
        {"DTUN", F_PCT, 0, 127, 0, 0, 0},
        {"SLOT", F_INT, 0, FM6_OWN + FM6_NBANK + FM6_NXF, 0, N_FM6_PATCH, 0},
    },
    .presets = FM6_PRESETS,
    .npresets = NELEM(FM6_PRESETS),
    .note_on = eng_none_note_on,
    .render = eng_none_render,
    .knob = {P_E2, P_E3, P_E4, P_E7},
    .poly = FM6_POLY,
    .ownenv = 1,
    .uiord = FM6_UIORD,
    .mem = 1824u,                /* (4.4a: sizeof(fm6_note_t) x FM6_POLY as it was: what a claim clears stays as it was; the operators are gone) */
    .atgt = ATG_FM6,             /* ARP TARGET: OP1..OP6 (SPEC 2.3) */
    .ufilt = 1,                 /* no filter of its own: the multimode filter (L/RRY 0.2.9; voice.c uf_run: CUT RES MODE SLOPE, P_FREEA..D) */
};
