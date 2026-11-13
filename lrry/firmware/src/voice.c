/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Voice allocation, envelopes, LFO and per-track rendering.
 * Runs in the audio ISR.
 *
 * Each synth part has its own NVOICE voices (so MONO / LEGATO / UNISON keep using
 * v[0..]); VPOOL (L/RRY 1.1: 2 x NVOICE, each part its own) of all the parts' voices sound at once: a part that starts
 * a voice while the budget is full takes one from any part (voice_victim): the oldest
 * released voice, else the oldest extra UNISON voice, else the oldest held voice of a
 * POLY part that is not its lowest note. A voice taken from another part fades out
 * over one block (stage 4: the envelope goes to 0, the block's amplitude ramp
 * declicks it); one of the part's own is restarted in place, as before. Extra UNISON
 * voices only start when there is room. */
/* L/RRY phase 4.3a: the stereo voice replaces the engine and the chain. LRRY_ENGINE 1: track_render's voice loop calls it (lrry_voice.c kernel, lrry_param.c decode) and the part's block is stereo
 * (pscr[].buf = left, bufR = right); 0 keeps L/RRY's mono engines and chain on the left channel (the old path: bench comparisons only, the right channel stays silent) */
#ifndef LRRY_ENGINE
#define LRRY_ENGINE 1
#endif
#include "lrry_param.c"
#if LRRY_BENCH                                           /* bench: where core 0's time of a block goes (ticks / 8 summed per stage; console `status`: stg_*). Stages in order: pre (events, blk_snap, tcr_begin), ctl (the voices' control
                                                          * work up to the render stage), post (weights, split choice, the post), own (core 0's voices), wait (core 1's job + its sums), pv (the rest of part_voice), fx (part_fx),
                                                          * mix (part_mix + part_end), gat (gather), snd (sends), mst (master + the output stage) */
static uint32_t stg_acc[24], stg_mx[24], stg_last;   /* stg_mx: the longest single block of each stage (console `bench stgmax`, PHASE62: where a note-on's spike is) */
static uint32_t nt_last, nt_mx[8];                       /* PHASE62: the stages of one note-on (console `bench stgmax`, second line) */
#define NT_RESET() (nt_last = TC_TICKS())
#define NT(i) do { uint32_t t_ = TC_TICKS(), d_ = (t_ - nt_last + 4u) >> 3; if (d_ > nt_mx[i]) nt_mx[i] = d_; nt_last = t_; } while (0)
#define STG_RESET() (stg_last = TC_TICKS())
#define STG(i) do { uint32_t t_ = TC_TICKS(), d_ = (t_ - stg_last + 4u) >> 3; stg_acc[i] += d_; if (d_ > stg_mx[i]) stg_mx[i] = d_; stg_last = t_; } while (0)
#else
#define NT_RESET() ((void)0)
#define NT(i) ((void)0)
#define STG_RESET() ((void)0)
#define STG(i) ((void)0)
#endif
#ifndef LRRY_NO_DECODE_CACHE
#define LRRY_NO_DECODE_CACHE 0                          /* 1 (host test only): the decode runs every block, as before the cache (the hashes of tests/lrry_split.sh must be the same) */
#endif
static int16_t lrry_key[NLIVE][EXT_P_LEN] __attribute__((aligned(4)));   /* the t->p the last decode was made from (the decode's cache key) */
static uint8_t lrry_key_ok[NLIVE];
/* PHASE62: t->p and the key compared a word (two values) at a time, four words a round: the compare ran every block (8 us on the unit, one value at a time) */
_Static_assert(EXT_P_LEN % 8 == 0 && __builtin_offsetof(track_t, p) % 4 == 0, "lrry_same reads t->p in words, eight values a round");
static inline uint32_t lrry_same(const int16_t *a, const int16_t *b, uint32_t n)
{
    typedef uint32_t __attribute__((may_alias)) word_t;
    const word_t *x = (const word_t *)a, *y = (const word_t *)b;
    uint32_t i, d = 0;
    for (i = 0; i < n / 2u; i += 4)
        d |= (x[i] ^ y[i]) | (x[i + 1] ^ y[i + 1]) | (x[i + 2] ^ y[i + 2]) | (x[i + 3] ^ y[i + 3]);
    return d == 0u;
}
static inline void lrry_copy(int16_t *dst, const int16_t *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}
static lrry_pp_t lrry_pp[NLIVE];                        /* the part's values of this block (lrry_decode; read only while its voices render) */
#if LRRY_PIPELINE
static uint32_t lrry_pp_ver[NLIVE];                     /* PIPELINE: +1 whenever lrry_pp[] changes (the decode ran, or the stage pans' ramp starts moved): the hand-over copies the patch only when this moved (twocore_run.c tcr_pipe_fill) */
#endif
static lrry_vs_t lrry_vs[NLIVE][NVOICE] __attribute__((section(".pool")));   /* (2026-11-12, 16 voices: in the zeroed pool beside the COMB lines, engines.c eng_mem; .bss had no room) one voice's state: written only while that voice renders (4.3b: voices 0-3 and 4-7 on two cores touch disjoint elements) */

/* L/RRY phase 4.3b: the split BY VOICE, DYNAMIC. track_render does the control work of every voice on core 0, in voice order, as before (envelope, glide, pitch, matrix, lrry_voice_ctl: it writes
 * track_t, so it is never split) and leaves each voice's pair of numbers in lrj (what lrry_voice reads: its lrry_vc_t and its vmod_t). The audio work, lrry_voice, is then done by
 * lrry_render_stage: all on core 0 (one core, or core 1 down / late / paused), or split in two sets for this block: the voices that sound are weighed (lrry_weights, an estimate of what each costs) and
 * lrry_split_choose picks the set for core 1 that makes the two cores' loads most equal, keeping a voice where it was unless moving it pays (LRRY_SP.stick). Any voice may go to either core in any
 * block: a voice's audio state (lrry_vs, its COMB lines) is touched by one core at a time, handed over with the job (acquire / release). The sum of the voices is integer addition, so it does not depend on
 * the order or on which core added what: every split gives the same samples. */
#if NVOICE > 8
#define LRRY_C1_MAX 8u                                  /* (bench build, 12 voices: core 1 may get up to 8; the chooser's own max1 stays 6 until a test raises it) */
#else
#define LRRY_C1_MAX 6u
#endif
#define LRRY_C1_MAX_DOC                                  /* the most voices one job carries (the job context holds this many) */
typedef struct {
    lrry_vc_t vc[NVOICE];
    vmod_t m[NVOICE];
    uint32_t mask;                                      /* bit i: voice i renders this block */
} lrry_job_t;
static lrry_job_t lrj;                                  /* core 0's: written by track_render's loop, read by the render stage */
#if LRRY_ENGINE
#if LRRY_TWOCORE
/* twocore_run.c (included after this file): the hand-over of a set of voices to core 1 */
#if LRRY_PIPELINE
static uint32_t tcr_pipe_fill(uint32_t ti, uint32_t c1set);   /* the next job's block copy made (1) or refused (0) */
static void tcr_pipe_post(uint32_t n, uint32_t c1set);        /* the job posted */
static void tcr_pipe_collect(uint32_t ti, int32_t *out, int32_t *outR, uint32_t n);   /* the job of the block before closed: its sums into the block before's */
static uint32_t tcr_pending(void);                            /* the block before's job is not closed yet */
static uint32_t tcr_go_post(void);                            /* a job may be posted now (after the block before's job is closed: core 1 still up, not parked, not paused) */
#else
static uint32_t tcr_voices_post(uint32_t ti, uint32_t n, uint32_t c1set);   /* 1: the job was posted */
static void tcr_voices_collect(uint32_t ti, int32_t *out, int32_t *outR, uint32_t n);
#endif
static uint32_t tcr_open_set(void);                     /* the voices of a job that is open (core 1 may still write them), else 0 */
static uint32_t tcr_go_now(void);                       /* a job may be posted this block */
#endif
/* a new voice's audio state is cleared at its note-on (voice_start). While core 1 may still be writing that voice (it is in a job that is open: the job was late), the clear waits: lrry_pend */
static vmask_t lrry_pend[NLIVE]; static uint8_t lrry_pend_note[NLIVE][NVOICE];
static void lrry_state_new(uint32_t pp, uint32_t vi, uint32_t note)
{
#if LRRY_TWOCORE
    if ((tcr_open_set() >> vi) & 1u) {
        lrry_pend[pp] |= (vmask_t)(1u << vi);
        lrry_pend_note[pp][vi] = (uint8_t)note;
        return;
    }
#endif
    lrry_pend[pp] &= (vmask_t)~(1u << vi);
    lrry_state_clear(&lrry_vs[pp][vi], note);
}

/* ---- the split's estimate and its choice. Tunable: the bench fine-tunes these (a runtime struct, so one build can try values). Units: host instructions a sample, the unit of budget.c's L/RRY rows.
 * w_*: what a voice costs (the same rows as budget.c BD_LR_*, without its margin); ho0 / ho1: what a job costs core 0 (copy + hash) and core 1 (start latency + hash + clear), charged only when core 1 gets
 * voices; stick: a voice is moved to the other core only if that lowers the longer core's load by more than this; max1: the most voices core 1 may get (<= LRRY_C1_MAX); min_total: with fewer sounding
 * voices than this everything stays on core 0. */
typedef struct {
    uint16_t base, osc2, noise, ring, sub, ms, dirt, super_saw, model_x[8];
    uint16_t fa12, fa24, fb_svf, fb_lad, fb_comb;
    uint16_t ho0, ho1, stick;
#if LRRY_PIPELINE
    uint16_t fix, fixk;                                 /* PIPELINE: what core 0 does in a block besides its own voices' audio (the idle mix and the part's fixed work, the hand-over ho0), and the control work of each voice */
#endif
    uint8_t max1, min_total;
} lrry_sp_t;
/* COST-BALANCED SPLIT (branch cores-balance). The numbers are budget.c's measured rows (PHASE62_VOICE_COST_REPORT.md section 10, BD_LR_*) through BD_LR_T (1 us a block = 4.526 units), so the split and the
 * counter use one cost. w_*: a voice's AUDIO work (budget's row less its control share BD_LR_CTL: the control runs on core 0 before the post, for every voice, whoever renders it). ho0: what a job costs core 0
 * BEFORE the post (copy, hash, the split's own work: 42.4 us, BD_LR_HO0); it is not time the two cores work side by side, so it is NOT charged to core 0's side of the balance, only to the question whether a split
 * pays at all (lrry_cost). ho1: core 1's head of its job (1.6 us, BD_LR_HO1), charged to core 1's side. Before: the stale Mac-era rows (a plain voice 280 = 62 us, ho0 40 = 8.8 us, ho1 45) with ho0 on core 0's side. */
/* 2026-11-12 (bench 1112M, FIT-PEAK): core 0's work that fix does not hold: the effect buses and the master stages (budget.c bd_global's fx, the same units), written by the budget's
 * poll (budget.c budget_poll, every frame). fix was measured with them off: with CHORUS / DELAY / REVERB on, the split kept core 0's voices as if they were not there (#03 SAW BASS poly, 4 keys: core 0 61 %,
 * core 1 28 %, a voice shed), while the counter assumed the cores balanced */
static volatile uint16_t lrry_sp_gx;
static lrry_sp_t LRRY_SP = {
    106, 38, 33, 16, 48, 9, 63, 36, {0, 0, 2, 6, 14, 0, 28, 0},   /* merged with budget-real2: budget.c's measured rows through BD_LR_T (dirt: the mean of the five types, 14.0 us; fb_svf: the mean of LP BP HP NOTCH, 25.7 us) */
    98, 194, 116, 229, 90,
    235, 5, 0,
#if LRRY_PIPELINE
    794, 38,                                            /* fix: 123.5 + 52.0 us (measured fixed work of a block with #03 TEST SAW on the pipelined unit, BD_LR_HO0 as refitted) = 794 units; fixk: BD_LR_CTL 8.5 us = 38 units.
                                                         * (2026-11-12, benches 1112O / 1112P: a refit to the measured core loads (each core's real cost of a voice, core 0's work outside core 1's
                                                         * window) was tried and taken back: SBX 3 keys, effects ON, shed at the chord start in 3 of 4 runs where these values played it clean 3 of 3.
                                                         * The two sounds that still shed here, M1 9 keys and SB 4 keys effects ON, are the counter's: budget.c LRRY_FXCAP) */
#endif
    LRRY_C1_MAX, 2,
};
/* the cost of each voice of the mask, from the patch of the block and the voice's own cutoffs (filters that do not run cost nothing). w[i] for the voices in the mask */
static void lrry_weights(const lrry_pp_t *P, const lrry_job_t *J, uint16_t *w)
{
    const lrry_sp_t *S = &LRRY_SP;
    uint32_t com = S->base, i, o;
    for (o = 0; o < 2u; o++) {
        uint32_t k, c, on = o == 0u;
        for (k = 0; k < 2u; k++)
            for (c = 0; c < 2u; c++)
                on |= o == 1u && P->tg[LS_O2][k][c] != 0;
        if (o == 1u)
            on |= P->lvq[1][0] != 0 || P->lvq[1][1] != 0;
        if (!on)
            continue;
        com += o ? S->osc2 : 0u;
        com += P->model[o] == LM_SUPER ? (uint32_t)P->sup_n[o] * S->super_saw : S->model_x[P->model[o] & 7u];
    }
    if (P->tg[LS_NOISE][0][0] | P->tg[LS_NOISE][0][1] | P->tg[LS_NOISE][1][0] | P->tg[LS_NOISE][1][1])
        com += S->noise;
    if (P->tg[LS_RING][0][0] | P->tg[LS_RING][0][1] | P->tg[LS_RING][1][0] | P->tg[LS_RING][1][1])
        com += S->ring;
    com += P->sub_g ? S->sub : 0u;
    com += (P->d_type[0] ? S->dirt : 0u) + (P->d_type[1] ? S->dirt : 0u);
    com += P->ms.identity ? 0u : S->ms;
    for (i = 0; i < NVOICE; i++) {
        uint32_t c = com;
        if (!((J->mask >> i) & 1u))
            continue;
        if (J->vc[i].pos_a >= 0)
            c += P->fa_p24 ? S->fa24 : S->fa12;
        if (P->fb_kind != LB_OFF && J->vc[i].pos_b >= 0 && (P->fb_kind != LB_COMB || J->vc[i].comb_d > 0))
            c += P->fb_kind == LB_LADDER ? S->fb_lad : P->fb_kind == LB_COMB ? S->fb_comb : S->fb_svf;
        w[i] = (uint16_t)(c > 65535u ? 65535u : c);
    }
}
static uint32_t lrry_load(uint32_t mask, uint32_t c1, const uint16_t *w, uint32_t *l0, uint32_t *l1)
{
    const lrry_sp_t *S = &LRRY_SP;
    uint32_t i, a = 0, b = 0;
    for (i = 0; i < NVOICE; i++)
        if ((mask >> i) & 1u) {
            if ((c1 >> i) & 1u)
                b += w[i];
            else
                a += w[i];
        }
#if LRRY_PIPELINE
    /* PIPELINE: the two cores work side by side the whole block (core 1's voices of block N while core 0 does N-1's mix and master and N+1's control), so core 0's side holds everything core 0 does: its own voices'
     * audio, every voice's control, the idle mix and the part's fixed work, and the hand-over ho0 when there is a job. Core 1's side is its voices and its head. The cycle is the longer side. */
    {
        uint32_t k = (uint32_t)__builtin_popcount(mask);
        a += S->fix + lrry_sp_gx + S->fixk * k - (c1 ? 0u : (uint32_t)S->ho0);
    }
    if (c1)
        b += S->ho1;
#else
    if (c1)
        b += S->ho1;                                    /* (core 0 posts first: its hand-over work ho0 is before the time the cores share, in neither side) */
#endif
    *l0 = a;
    *l1 = b;
    return a > b ? a : b;
}
/* what core 0 spends on the block's voices with the set c1 for core 1: the longer side plus the hand-over ho0 when there is a job; no job: all voices on core 0. A split pays when this is below the no-job value */
static uint32_t lrry_cost(uint32_t mask, uint32_t c1, const uint16_t *w)
{
    uint32_t l0, l1, t = lrry_load(mask, c1, w, &l0, &l1);
#if LRRY_PIPELINE
    return t;                                           /* (ho0 is inside core 0's side) */
#else
    return c1 ? t + LRRY_SP.ho0 : t;
#endif
}
/* The set of voices for core 1. Starts from last block's set (prev) cut to the voices that sound, then moves one voice, or swaps two, while that lowers the longer core's load by more than stick
 * (a move that ties is not taken; the lowest voice number wins among equals). Nothing for core 1 if the split with its hand-over (lrry_cost: the longer side + ho0) is no better than everything on core 0, if fewer than min_total voices sound, or
 * if core 1 would get more than max1. Pure: the same mask, weights and prev give the same set. */
static uint32_t lrry_split_improve(uint32_t mask, const uint16_t *w, uint32_t c1)   /* moves and swaps while they pay */
{
    const lrry_sp_t *S = &LRRY_SP;
    uint32_t it, i, j, l0, l1;
    for (it = 0; it < 16u; it++) {
        uint32_t best = lrry_load(mask, c1, w, &l0, &l1), bset = c1, cand, v, cur = best;
        for (i = 0; i < NVOICE; i++) {
            if (!((mask >> i) & 1u))
                continue;
            cand = c1 ^ (1u << i);                      /* move voice i to the other core */
            if ((uint32_t)__builtin_popcount(cand) <= S->max1) {
                v = lrry_load(mask, cand, w, &l0, &l1);
                if (v < best) { best = v; bset = cand; }
            }
            for (j = i + 1u; j < NVOICE; j++) {
                if (!((mask >> j) & 1u) || (((c1 >> i) ^ (c1 >> j)) & 1u) == 0u)
                    continue;
                cand = c1 ^ (1u << i) ^ (1u << j);      /* swap i and j (one on each core) */
                v = lrry_load(mask, cand, w, &l0, &l1);
                if (v < best) { best = v; bset = cand; }
            }
        }
        if (bset == c1 || cur - best <= S->stick)
            break;
        c1 = bset;
    }
    return c1;
}
static uint32_t lrry_split_choose(uint32_t mask, const uint16_t *w, uint32_t prev)
{
    const lrry_sp_t *S = &LRRY_SP;
    uint32_t c1, lpt = 0, l0, l1, all0, a, b, k, o[NVOICE], n = 0, i;
    if ((uint32_t)__builtin_popcount(mask) < S->min_total)
        return 0;
    c1 = lrry_split_improve(mask, w, prev & mask);      /* from last block's set */
    for (i = 0; i < NVOICE; i++)                        /* and from scratch: the dearest voice first, each to the core that is less loaded (longest processing time first) */
        if ((mask >> i) & 1u) {
            for (k = n++; k > 0 && w[o[k - 1u]] < w[i]; k--)
                o[k] = o[k - 1u];
            o[k] = i;
        }
#if LRRY_PIPELINE
    a = S->fix + lrry_sp_gx + S->fixk * n;              /* core 0's load so far: its fixed work, the effects and master stages too (PIPELINE: it works while core 1 does) */
#else
    a = 0;                                              /* core 0's load so far; core 1 starts with its head (ho0 is before the post: not in the balance) */
#endif
    b = S->ho1;
    for (k = 0; k < n; k++) {
        if (a <= b || (uint32_t)__builtin_popcount(lpt) >= S->max1)
            a += w[o[k]];
        else {
            lpt |= 1u << o[k];
            b += w[o[k]];
        }
    }
    lpt = lrry_split_improve(mask, w, lpt);
    if (lrry_load(mask, lpt, w, &l0, &l1) + S->stick < lrry_load(mask, c1, w, &l0, &l1))
        c1 = lpt;                                       /* the fresh set wins only if it is better by more than stick: a voice keeps its core unless moving it pays */
    all0 = lrry_cost(mask, 0, w);
    if (c1 && lrry_cost(mask, c1, w) >= all0)
        c1 = 0;                                         /* the job costs core 0 more than it saves (few cheap voices): one core plays them all */
    return c1;
}
static uint32_t lrry_prev_c1;                           /* last block's set for core 1 (the chooser keeps voices where they are) */
/* the choice for this block. The chooser is a pure function and its own answer is a fixed point (lrry_split_choose(mask, w, answer) == answer: tests/lrry_split_test.c), so when the sounding voices and
 * their weights are the same as at the last call the answer is the last answer and nothing is computed: a held chord costs a compare of 9 words a block */
static uint32_t lrry_pick(uint32_t mask, const uint16_t *w)
{
    static uint32_t cmask = 0xFFFFFFFFu;
    static uint16_t cw[NVOICE], cgx;
    uint32_t i, same = mask == cmask && cgx == lrry_sp_gx;   /* (the effects switched: choose again) */
    for (i = 0; i < NVOICE && same; i++)
        same = !((mask >> i) & 1u) || cw[i] == w[i];
    if (!same) {
        lrry_prev_c1 = lrry_split_choose(mask, w, lrry_prev_c1);
        cmask = mask;
        cgx = lrry_sp_gx;
        for (i = 0; i < NVOICE; i++)
            cw[i] = ((mask >> i) & 1u) ? w[i] : 0u;
    }
    return lrry_prev_c1;
}
#endif

static uint32_t vage;                                   /* voice ages: one clock for every part */
/* an LFO's WAVE (N_LWAVE) at phase ph; rnd: its S&H value of the cycle */
static int32_t lfo_wave_of(int32_t wave, uint32_t rnd, uint32_t ph)
{
    switch (wave) {
    case 1:
        return osc_tri(ph);
    case 2:
        return (int32_t)(ph >> 16) - 32768;
    case 3:
        return ph < 0x80000000u ? 32767 : -32767;
    case 4:
        return (int32_t)(rnd >> 16) - 32768;
    default:
        return osc_sine(ph);
    }
}

/* the S&H value of a new LFO cycle (the previous value is a part's own state) */
static uint32_t lfo_rand(track_t *t)
{
    return part_rand(t, &t->lfo_rnd);
}

/* LFO 2 (1.2, #154; params.c N_LSYNC): with SYNC on, one cycle lasts the note value of the tempo (fx.c div_samples:
 * BPM, or the external clock's measured tempo while it runs), whatever RATE says (the matrix's RATE too): the phase
 * advances 2^32 x CTL / cycle a block (two 32-bit divides; the 1-bit truncation of 0xFFFFFFFF drifts by less than a
 * millionth of a cycle a cycle). OFF: RATE's table, as before */
static uint32_t div_samples(uint32_t div);         /* (fx.c) */
static __attribute__((noinline)) uint32_t lfo_sync_inc(uint32_t sync)   /* (not inlined: its divides stay out of
                                                                       * the audio ISR's loop over the tracks) */
{
    uint32_t cyc, q, r;
    cyc = div_samples(LSYNC_DIV[sync % NELEM(LSYNC_DIV)]);
    cyc = cyc ? cyc : 1u;
    q = 0xFFFFFFFFu / cyc;
    r = 0xFFFFFFFFu % cyc;
    return q * CTL + (r * CTL + CTL) / cyc;
}

/* LFO 2's S&H draws: its own xorshift on every part (the shared rng, and so the golden renders, never see LFO 2) */
static uint32_t lfo2_rand(uint32_t s)
{
    s = s ? s : 0x7F4A7C15u;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* one control tick of an LFO l of track t (L/RRY: LFO 1, t->lfo1, and LFO 2, t->lfo2, share it): r its RATE (WAVE PHS FADE
 * follow), s its SYNC (TRIG POL follow); lfo1: LFO 1's S&H generator (lfo_rand: the shared rng on parts 1..3), else LFO 2's.
 * LFO 1's numbers are the ones track_lfo_tick made before (the same operations in the same order) */
static void lfo_tick(track_t *t, lfo_t *l, uint32_t r, uint32_t s, int lfo1)
{
    const int16_t *p = t->p;
    uint32_t old = l->ph;
    l->ph += p[s] ? lfo_sync_inc((uint32_t)p[s]) : LFO_INC[p[r] & 127];
    if (l->ph < old) {
        l->prev = (int16_t)(l->rnd ? (int32_t)(l->rnd >> 16) - 32768 : 0);   /* (mod.c mod_sh: SLEW glides on from there) */
        l->rnd = lfo1 ? lfo_rand(t) : lfo2_rand(l->rnd);
    }
    l->val = lfo_wave_of(p[r + 1u], l->rnd, l->ph);
    if (MXF(t).on && MXF(t).w[lfo1 ? MX_LWAVE : MX_L2WAVE]) {   /* L/RRY MORPH: the other end's WAVE runs too, cross-faded */
        uint32_t x = lfo1 ? MX_LWAVE : MX_L2WAVE;
        l->val += ((lfo_wave_of(MXF(t).b[x], l->rnd, l->ph) - l->val) * (int32_t)MXF(t).w[x]) >> 15;
    }
    if (p[s + 2u])                                      /* UNI: 0..+1 (the depths reach as far, one way only) */
        l->val = (l->val + 32768) >> 1;
    {                                                   /* L/RRY 1.1 LFO LEVEL (X_LFO1LVL / X_LFO2LVL, 0..127, 127 = full): the LFO's master depth, applied where the value is made so that every reader (the DEST depths, the matrix source, the TWIN depth, the meter) sees it; 127 skips this, the value as before */
        int32_t lv = XV(t, lfo1 ? X_LFO1LVL : X_LFO2LVL);
        if (lv < 127)
            l->val = mulq15(l->val, lv < 0 ? 0 : lv * 258);
    }
    if (BYP(t, lfo1 ? BY_LFO1 : BY_LFO2))               /* L/RRY 1.1 BYPASS: the LFO keeps running (in step when it comes back) but sends 0 */
        l->val = 0;
    if (l->fade < 32767) {
        int32_t step = (int32_t)(ENV_LIN[p[r + 3u] & 127] >> 9);
        l->fade = p[r + 3u] ? clamp(l->fade + (step ? step : 1), 0, 32767) : 32767;
    }
}
static void track_lfo_tick(track_t *t) { lfo_tick(t, &t->lfo1, P_LRATE, P_LSYNC, 1); }

/* L/RRY 0.2.11 VOICE CAP (owner, 2026-10-14; SPEC 6.3): a heavy combination gets fewer voices per part, so the predicted
 * worst case of both parts stays under VC_BUDGET of the half (the loadcost scale of docs/CPU_ANALYSIS.md: model %, the device
 * reads ~10-25 % lower). The cost of a voice (0.1 % of the half): the engine's (its heaviest factory sound, 8 voices held, minus the
 * idle mix over the voices it holds, CPU_ANALYSIS 2a, measured again: tests/cpu_scenarios_test.c P), + the multimode filter
 * 12 dB / 24 dB (2b: +14 / +28 % at 8 voices), and with the MORPH on that part:
 * + VC_MORPH (the hook), the filter as the heaviest of the sound and both ends (a bypassed filter that an end switches on counts,
 * SLOPE 12 / 24 dB that differ: 24, both stages run), + VC_FADE when an engine choice is cross-faded (both shapes made). The worst case: the
 * dearer part takes all the voices its cap allows, the other the rest of the 8-voice pool, + the idle mix (LAYER: every note on
 * both parts, 4 + 4: no cap of 4 or more changes it, so none is set). Over VC_BUDGET: the
 * dearer part's cap down by one (never below VC_MIN), again. Only the dearer part's cap can lower the worst case: the pool stays
 * 8 voices, so a cap moves voices to the cheaper part (a muted part costs 0, so a part played alone is capped to fit by itself).
 * When even VC_MIN cannot bring the pair under VC_BUDGET (two dear parts: 4 + 4 is over), the caps cannot help the pair, and
 * each part gets the cap that fits it alone (VC_BUDGET - VC_IDLE over its voice, VC_MIN at least) instead of 4. Computed
 * every UI frame (vcap_poll: main loop; a load, an engine, filter or SLOPE change, a roll are seen at once), deterministic.
 * It bites only for NEW notes: voice_alloc / UNISON take at most vcap voices (a note over the cap steals within the part, as
 * at an engine's own cap); notes that sound beyond the cap keep sounding until they end. Shown only on the TEST screen */
#define VC_IDLE 150u                                    /* the idle mix with power-on FX: 15 % */
#define VC_BUDGET 850u                                  /* 85 % */
#define VC_F12 18u                                      /* the multimode filter a voice: 12 dB */
#define VC_F24 36u                                      /* .. 24 dB (24 dB HP, the dearest) */
#define VC_MORPH 5u                                     /* the morph a voice (the hook, its blends: +0.3..0.5 %, measured) */
#define VC_FADE 20u                                     /* .. an engine choice cross-faded (M04 WIRED SNAIL's ANALOG WAVE, M14 PHASE WAVE: +2..2.6 %) */
#define VC_MIN 4u
static uint8_t vcap[NTRK];                              /* the cap of each part, 0: none yet (all of the engine's) */
static uint8_t bnotes[NTRK];                            /* L/RRY NOTES budget (budget.c budget_poll): the notes of each part, 0: none yet (the budget is not asked) */
static uint8_t bnotes1[NTRK];                           /* L/RRY 1.1 item 90: the one-core numbers, always (bnotes holds the two-core ones while that rule is in force) */
static volatile uint8_t bd_one_now;                     /* L/RRY 1.1 item 90: 1 = the audio plays both parts on core 0 right now (core 1 down, paused, or a flash park: twocore_run.c tcr_begin sets it
                                                         * every block): the note-on path and the counter use bnotes1. 0 in every one-core build */
#define VPOOL (NVOICE * 2u)                             /* L/RRY 1.1 item 90: the voices that may sound at once over both parts: each part its own NVOICE (was NVOICE shared); the NOTES budget holds the CPU */
static const struct { const engine_t *e; uint8_t c; } VCOST[] = {   /* (tests/cpu_scenarios_test.c CPU_SCEN=P: the heaviest sound, its voices) */
    {&ENG_ANALOG, 51}, {&ENG_FM6, 61}, {&ENG_PHASE, 40}, {&ENG_LOFI, 24}, {&ENG_FORMANT, 60}, {&ENG_TRIO, 44}, {&ENG_WHEEL, 59},
    {&ENG_PHYS, 144}, {&ENG_NOISE, 47}, {&ENG_WAVE, 52},   /* (FM6: 6 voices, PHYS 3, VOICE 4; WAVE: #60 DIGITAL PAD less its 12 dB filter) */
};
/* voices the engine may use (POLY and UNISON): its cap, else all of them; L/RRY 0.2.11: and the part's VOICE CAP; and the part's NOTES
 * (budget.c: the stricter of the two caps and the budget; a new note over it steals within the part, as at an engine's own cap) */
static uint32_t trk_nvoice_eng(const track_t *t)
{
    uint32_t c = ENGINES[t->engine]->poly;
    return c && c < NVOICE ? c : NVOICE;
}
static uint32_t trk_nvoice(const track_t *t)
{
    uint32_t p = (uint32_t)(t - trk) % NTRK, c = trk_nvoice_eng(t), v = vcap[p], b = bd_one_now ? bnotes1[p] : bnotes[p];
#if LRRY_NOLIMIT                                        /* bench1: no budget gate, no VOICE CAP: the engine's own limit (PHYS 3, VOICE 4, FM6 6) and the pool only */
    (void)v, (void)b;
    return c;
#endif
    if (b)                                              /* (item 90: the NOTES budget supersedes the 0.2.11 VOICE CAP, whose table and 8-voice pool are out of date) */
        return b < c ? b : c;
    return v && v < c ? v : c;
}
/* the voices the screen promises part t (ui_draw.c head_notes): trk_nvoice's own rule without the bench's open gate. In every build but a LRRY_NOLIMIT bench build it IS
 * trk_nvoice; in such a build the audio engine plays past it (the bench measures beyond the limit) but the number shown stays the real one (owner 2026-11-11: the number is
 * always real, on every build he plays) */
static uint32_t trk_nvoice_shown(const track_t *t)
{
    uint32_t p = (uint32_t)(t - trk) % NTRK, c = trk_nvoice_eng(t), v = vcap[p], b = bd_one_now ? bnotes1[p] : bnotes[p];
    if (b)
        return b < c ? b : c;
    return v && v < c ? v : c;
}
static uint32_t vc_eng_cap(const engine_t *e) { return e->poly && e->poly < NVOICE ? e->poly : NVOICE; }
static uint32_t vcap_shown(uint32_t p)                  /* the TEST screen: the voices part p may use (its sound's engine, the cap) */
{
    uint32_t c;
    if (p >= NTRK)                                      /* (L/RRY: a part that does not exist) */
        return 0;
    c = vc_eng_cap(ENGINES[eng_idx(trk[p].eng_req)]);
    return vcap[p] && vcap[p] < c ? vcap[p] : c;
}
static uint32_t vc_filter(const engine_t *e, const int8_t *p)   /* the multimode filter's cost a voice in these values */
{
    if (!e->ufilt || (p[P_FREEA] >= UF_STEPS && p[P_FREEC] <= 0 && !p[P_KTRK]))
        return 0;                                       /* (the bypass: MODE LP, CUT open, no KEY TRK) */
    return p[P_FREED] == 2 ? VC_F24 : VC_F12;
}
static uint32_t vc_voice(const track_t *t)              /* a voice of part t, 0.1 % of the half (0: the part is muted) */
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];   /* (the sound's engine: a switch fades first, eng_req leads) */
    uint32_t i, c = 60u, f, pi = (uint32_t)(t - trk);   /* (an engine not in the table: as the dearest) */
    int8_t v[P_COUNT];
    if (t->p[P_MUTE])
        return 0;
    for (i = 0; i < NELEM(VCOST); i++)
        if (VCOST[i].e == e)
            c = VCOST[i].c;
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int8_t)t->p[i];
    f = vc_filter(e, v);
    if (pi < NSHOWN && (mph[pi].nid || mph[pi].fnid) && !morph_stale(t, &mph[pi])) {
        const morph_part_t *mp = &mph[pi];
        uint32_t fa = vc_filter(e, morph_cur(mp, 0)), fb = vc_filter(e, morph_cur(mp, 1));
        f = fa > f ? fa : f;
        f = fb > f ? fb : f;
        if (fa && fb && morph_cur(mp, 0)[P_FREED] != morph_cur(mp, 1)[P_FREED])
            f = VC_F24;                                 /* (the slopes cross-fade: both stages run) */
        for (i = 0; i < mp->nid; i++)
            if ((mp->ent[i].kind & 0x7Fu) == MK_FADE && mp->ent[i].id >= P_E0 && mp->ent[i].id < P_E0 + 8u &&
                morph_cur(mp, 0)[mp->ent[i].id] != morph_cur(mp, 1)[mp->ent[i].id]) {
                c += VC_FADE;                           /* an engine choice cross-faded: both shapes */
                break;
            }
        c += VC_MORPH;
    }
    return c + f;
}
/* the predicted worst case (0.1 %) of the per-voice costs k[0..1] with the caps c[0..1] */
static uint32_t vc_worst(const uint32_t *k, const uint32_t *c)
{
    uint32_t h = k[1] > k[0], nh = c[h] < NVOICE ? c[h] : NVOICE, nl = c[!h] < NVOICE - nh ? c[!h] : NVOICE - nh;
    return VC_IDLE + nh * k[h] + nl * k[!h];
}
/* the caps for the per-voice costs k[0..1] and the engines' own caps e[0..1] into cap[0..1]; returns the predicted worst case with them */
static uint32_t vc_caps(const uint32_t *k, const uint32_t *e, uint32_t *cap, uint32_t layer)
{
    uint32_t w, h;
    cap[0] = e[0];
    cap[1] = e[1];
    if (layer)                                          /* LAYER: every note on both parts, the pool split evenly (4 + 4): a cap of 4 or */
        return VC_IDLE + (e[0] < NVOICE / 2u ? e[0] : NVOICE / 2u) * k[0] + (e[1] < NVOICE / 2u ? e[1] : NVOICE / 2u) * k[1];   /* more changes nothing */
    h = k[1] > k[0];
    while ((w = vc_worst(k, cap)) > VC_BUDGET && k[h] > k[!h] && cap[h] > VC_MIN)
        cap[h]--;                                       /* (one voice of the dearer part goes to the cheaper one: the pool stays 8) */
    if (w > VC_BUDGET) {                                /* the pair cannot get under it with caps of 4 or more: then each part as if it */
        for (h = 0; h < 2u; h++) {                      /* played alone (a part on its own fits), never below VC_MIN */
            uint32_t c = k[h] ? (VC_BUDGET - VC_IDLE) / k[h] : NVOICE;
            c = c < VC_MIN ? VC_MIN : c;
            cap[h] = c < e[h] ? c : e[h];
        }
        w = vc_worst(k, cap);
    }
    return w;
}
static uint32_t vcap_worst;                             /* the predicted worst case with the caps (0.1 %; the TEST screen) */
static void vcap_poll(void)                             /* main loop, every frame */
{
    uint32_t k[2] = {0, 0}, e[2] = {NVOICE, NVOICE}, cap[2], p;     /* (L/RRY: the loops follow NSHOWN, a missing part 2 costs nothing) */
    for (p = 0; p < NSHOWN; p++) {
        k[p] = vc_voice(&trk[p]);
        e[p] = vc_eng_cap(ENGINES[eng_idx(trk[p].eng_req)]);
    }
    vcap_worst = vc_caps(k, e, cap, 0u);
    for (p = 0; p < NSHOWN; p++)
        vcap[p] = (uint8_t)cap[p];
}

/* the part's voice mode: P_VOICE */
static uint32_t trk_vmode(const track_t *t)
{
    return (uint32_t)t->p[P_VOICE];
}

/* ------------------------------------------------- the shared voice budget --- */
static uint32_t voices_busy(void)                       /* sounding voices of all parts (not the fading ones) */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
    return n;
}

static uint32_t lowest_held(const track_t *t)           /* index of the lowest held note (the bass), NVOICE = none */
{
    uint32_t i, low = NVOICE;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && (low == NVOICE || t->v[i].note < t->v[low].note))
            low = i;
    return low;
}

/* the voice to give up for a new one (see the top); soft: only released voices and
 * extra voices of other non-POLY parts (also ones left after a mode change).
 * Returns its index, *pp its part; NVOICE = none */
static uint32_t voice_victim(const track_t *self, int soft, track_t **pp)
{
    uint32_t p, i, best = NVOICE, cat = 4;
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        uint32_t mode = trk_vmode(t);
        uint32_t low = mode == V_POLY ? lowest_held(t) : NVOICE;
        if (soft && t == self)
            continue;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            uint32_t c;
            if (!v->active || v->stage == 4u)
                continue;
            if (!v->gate)
                c = 1;                                  /* released */
            else if (mode != V_POLY && i > 0)
                c = 2;                                  /* extra UNISON, or a voice left by a mode / cap change */
            else if (!soft && mode == V_POLY && i != low)
                c = 3;                                  /* held, not the bass */
            else
                continue;
            if (c < cat || (c == cat && v->age < (*pp)->v[best].age)) {
                cat = c;
                best = i;
                *pp = t;
            }
        }
    }
    return best;
}

static uint32_t voice_kills;                            /* voices given up (budget, overload): console, hostsim */
static void voice_kill(voice_t *v)                      /* fade out over the next block (env_tick stage 4) */
{
    v->gate = 0;
    v->stage = 4;
    voice_kills++;
}

/* L/RRY NOTES budget (budget.c): a part whose number dropped while its notes sounded (an item was switched on) keeps them until its next note-on; then the
 * voices above its number fade out (voice_kill: the one-block fade, never a cut), so a part never sounds more than part_notes() voices past that note.
 * Only while the budget is on (bnotes set by budget_poll); a part within its number loses nothing */
static void voice_trim(track_t *t)
{
    uint32_t i, np;
    if (!bnotes[(t - trk) % NTRK])
        return;
    np = trk_nvoice(t);
    for (i = np; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].stage != 4u)
            voice_kill(&t->v[i]);
}

#if LRRY_TWOCORE
/* L/RRY TWO-CORE stage 1 (twocore_run.c tc_trim_poll): the jobs paused or core 1 stopped, so the part plays alone. Voices above the part's number fade out AT ONCE
 * (voice_kill: the one-block fade), the released ones first (the oldest), then the oldest held: not at the next note-on as voice_trim does. Only while the budget is on
 * (bnotes set); called by the audio interrupt at a block's start for a part core 1 does not hold. Returns how many it faded. In stage 1 the numbers are the
 * one-core ones, so a part within its number loses nothing */
static uint32_t voice_trim_now(track_t *t)
{
    uint32_t i, np, n = 0, k = 0;
    if (!bnotes[(t - trk) % NTRK])
        return 0;
    np = trk_nvoice(t);
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].stage != 4u;
    for (; n > np; n--, k++) {
        voice_t *best = 0;
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &t->v[i];
            if (v->active && v->stage != 4u && (!best || (!v->gate && best->gate) || (!v->gate == !best->gate && v->age < best->age)))
                best = v;
        }
        if (!best)
            break;
        voice_kill(best);
    }
    return k;
}
#endif

/* the budget is full: free a voice for part t. 0 = nothing to take (soft) */
static int voice_room(track_t *t, int soft)
{
    track_t *vp = 0;
    uint32_t k;
    if (voices_busy() < VPOOL)
        return 1;
    k = voice_victim(t, soft, &vp);
    if (k == NVOICE)
        return !soft;                                   /* hard: over the budget (cannot happen: each of the
                                                         * NPART < NVOICE parts protects one voice at most) */
    voice_kill(&vp->v[k]);
    return 1;
}

static voice_t *voice_reuse(track_t *t, voice_t *v)
{
    if (v->stage == 4u)
        voice_room(t, 0);                               /* its budget was taken before this block */
    return v;
}

/* POLY allocation (within the engine's voice cap): the same note again reuses its voice.
 * Else a free voice: ROTATE takes
 * the next one round-robin (release tails ring out), REUSE (or any GLIDE) takes
 * the free voice whose last pitch is closest, so poly portamento moves each
 * voice the shortest way. Steal: the oldest released voice, else the oldest
 * held one, never the lowest held note (the bass). A free voice needs room in the
 * shared budget: when the voice to give up is one of this part's, it is restarted
 * in place instead. */
static voice_t *voice_alloc(track_t *t, uint32_t note)
{
    uint32_t i, np = trk_nvoice(t), best = np, low = np, nfree = 0;
    int32_t bd = 0x7FFFFFFF;
    NT(2);
    voice_trim(t);
    NT(2);
    for (i = 0; i < np; i++) {
        if (t->v[i].active && t->v[i].note == note)
            return voice_reuse(t, &t->v[i]);
        nfree += !t->v[i].active;
    }
    NT(2);
    if (nfree && voices_busy() >= VPOOL) {
        track_t *vp = 0;
        uint32_t k = voice_victim(t, 0, &vp);
        if (k < np && vp == t)
            return &t->v[k];                            /* our own: restart it in place (no click) */
        if (k < NVOICE)
            voice_kill(&vp->v[k]);
    }
    if (t->p[P_GLIDE]) {                                /* (T0227, owner: the stored VOICE ALLOCATION is ignored: ROTATE always; with GLIDE the free voice nearest in pitch, as ever) */
        for (i = 0; i < np; i++) {
            int32_t d = t->v[i].pitch_cur - (int32_t)note * 16;
            if (t->v[i].active)
                continue;
            d = d < 0 ? -d : d;
            if (!t->v[i].pitch_cur)
                d = 0x7FFFFFF0;                          /* never played: last choice */
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
    } else {
        for (i = 0; i < np && best == np; i++) {
            uint32_t k = (t->rr + i) % np;
            if (!t->v[k].active)
                best = k;
        }
        t->rr = (uint8_t)((best == np ? t->rr : best) + 1u) % np;
    }
    if (best < np)
        return &t->v[best];
    for (i = 0; i < np; i++)                             /* the lowest held note is protected */
        if (t->v[i].gate && (low == np || t->v[i].note < t->v[low].note))
            low = i;
    for (i = 0; i < np; i++)                             /* oldest released (not one fading out for another part) */
        if (!t->v[i].gate && t->v[i].stage != 4u && (best == np || t->v[i].age < t->v[best].age))
            best = i;
    if (best == np)
        for (i = 0; i < np; i++)                         /* oldest held, not the bass */
            if (i != low && (best == np || t->v[i].age < t->v[best].age))
                best = i;
    return voice_reuse(t, &t->v[best == np ? 0 : best]);
}

/* glide: RATE = a fixed speed, TIME = the same time for any interval */
static void glide_set(track_t *t, voice_t *v, int glide)
{
    if (!glide || !v->pitch_cur) {
        v->pitch_cur = v->pitch16;
        v->gstep = 0;
        return;
    }
    if (t->p[P_GLMODE]) {
        int32_t d = v->pitch16 - v->pitch_cur, ticks = 1 + t->p[P_GLIDE] * t->p[P_GLIDE] / 8;
        d = d < 0 ? -d : d;
        v->gstep = d / ticks > 0 ? d / ticks : 1;
    } else {
        v->gstep = 0;
    }
}

/* hunt3: STEP CANCELLATION at a re-strike. A key struck again over a voice that still sounds (or a voice restarted in place) can make the voice's output jump:
 * an engine that resets a state at its note-on (a wrong-state reset) puts its new first sample somewhere else than where the old note was going. That step is an
 * artifact; voice_step() measures it (the new first sample against where the voice's last two output samples (v->lastc, v->lastd) were heading) and takes it off
 * again, decaying linearly over VOICE_STEP_LEN samples. Nothing else is touched: a voice that continues without a step is bit for bit as before, and a note that
 * starts from silence keeps the waveform's own first value (a saw's edge, a kick's first sample, a DX7's one-block attack): the engine's normal behaviour, not an artifact.
 * VOICE_OWN_START is the per-engine switch (bit = engine index, engines.c ENGINES[]): a 1 cancels the waveform's own first value of a fresh note too (the same decay).
 * All 0 by decision, engine by engine:
 *   ANALOG 0   the oscillators' start phase and the saw's edge are the model
 *   PHASE 2    phase distortion's cosine starts at its peak by design (the CZ's), the strike is the sound
 *   LOFI 3     the chip waves start at their edge, as the chips did
 *   VOICE 5    formant buzz: starts at the pulse
 *   TRIO 6     the three oscillators start at phase 0, the model
 *   WHEEL 7    tonewheel / bar starts at its excitation
 *   PHYS 9     a struck string / membrane starts with its excitation: the hit is the sound
 *   NOISE 11   noise has no start phase
 *   FM6 12     msfa / Dexed (the DX7 reference): carrier phase 0, the operators' gains ramp across the first block (rate 99 = one block), so a loud low note starts with an edge
 *   WAVE 14    the table read starts at phase 0 of its frame
 * VOICE_STEP_LEN: 64 samples = 1.45 ms. A step counts when it is bigger than VOICE_STEP_K x the old note's last change per sample + VOICE_STEP_MIN (32768 = full scale). */
#ifndef VOICE_STEP_LEN
#define VOICE_STEP_LEN 64u
#define VOICE_STEP_K 2
#define VOICE_STEP_MIN 256
#endif
static const uint16_t VOICE_OWN_START = 0x0000u;
static __attribute__((noinline)) void voice_step(voice_t *v, int32_t *dst, int32_t b0, uint32_t n)
{
    uint32_t i;
    if (v->offp) {                                      /* the first block of a re-strike (or of a fresh note, VOICE_OWN_START) */
        int32_t err = dst[0] - b0 - (v->lastc + v->lastd), ad = v->lastd < 0 ? -v->lastd : v->lastd, ae = err < 0 ? -err : err;
        v->offp = 0;
        if (ae > VOICE_STEP_K * ad + VOICE_STEP_MIN) {  /* (not the wave's own slope: only a jump the old note's last two samples did not promise) */
            v->offc = err;
            v->offn = (uint8_t)VOICE_STEP_LEN;
        }
    }
    for (i = 0; i < n && v->offn; i++, v->offn--)
        dst[i] -= (v->offc * (int32_t)v->offn) / (int32_t)VOICE_STEP_LEN;
}

static void cycle_start(track_t *t, voice_t *v, int sounding);   /* (cycle.c) */
static void voice_start(track_t *t, voice_t *v, uint32_t note, uint32_t vel, int glide)
{
    const engine_t *e = ENGINES[t->engine];
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s0 = v->s[0], s1 = v->s[1], s4 = v->s[4], s5 = v->s[5], s6 = v->s[6], s7 = v->s[7];
    uint32_t keep = e->keep;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->xg = t->lay_xg;                                  /* LAYER XFADE: this note's share (0: none) */
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = (int32_t)note * 16;
    v->mvel = t->m_vel;                                 /* mod.c: the note's VEL and RAND */
    v->mrnd = t->m_rnd;
    t->m_vi = (uint8_t)(v - t->v);
    glide_set(t, v, glide);
    v->apitch = t->atg_note ? (int32_t)t->atg_note * 16 : v->pitch16;   /* ARP TARGET: at the arp's note now */
    v->again = t->atg_vel * 258;
    v->stage2 = 1;                                      /* ENV 2 (L/RRY): an attack, from where it is (as ENV) */
    v->offn = 0;                                        /* (hunt3: a step still being cancelled ends with the old note) */
    v->offp = (uint8_t)(sounding || ((VOICE_OWN_START >> t->engine) & 1u));
    if (!sounding)
        v->lastc = v->lastd = 0;
    NT(3);
    cycle_start(t, v, sounding);                        /* CYCLE: a rise, from where the generator stands (cycle.c) */
    NT(4);
    if (!sounding) {
        v->env = 0;
        v->env_out = 0;
        v->env2 = 0;
        v->uft = 0;                                     /* the multimode filter from rest */
        v->chn = VCH_FRESH;                              /* the voice chain: a note that has not rendered yet (voice_chain.h) */
    }                                                   /* sounding: the attack starts from the current level */
    if (TRK_HIDDEN(t)) {                                /* L/RRY: tracks 3..4 never sound */
        v->active = v->gate = 0;
        v->stage = 0;
        return;
    }
    e->note_on(t, v);
    NT(5);
#if LRRY_ENGINE
    if (!sounding)                                      /* L/RRY: a new voice starts from rest; a retrigger or steal keeps phases and filter states, as above */
        lrry_state_new(PART_OF(t), (uint32_t)(v - t->v), note);
#endif
    if (sounding && !e->sampled) {                     /* retrigger / steal: keep phases and filter */
        v->ph[0] = ph0;                                 /* states (resetting them clicks) */
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        if (keep & 0x01u)                               /* engine_t.keep: the slots the engines use */
            v->s[0] = s0;
        if (keep & 0x02u)
            v->s[1] = s1;
        if (keep & 0x10u)
            v->s[4] = s4;
        if (keep & 0x20u)
            v->s[5] = s5;
        if (keep & 0x40u)
            v->s[6] = s6;
        if (keep & 0x80u)
            v->s[7] = s7;
    }
}

/* MONO / LEGATO / UNISON: one note on one voice (eight for UNISON, or the
 * engine's voice cap, spread by DETUNE over the same width) */
static void mono_play(track_t *t, uint32_t note, uint32_t vel, int retrig, int glide)
{
    uint32_t mode = (uint32_t)t->p[P_VOICE], nv = mode == V_UNISON ? trk_nvoice(t) : 1u, i;
    voice_trim(t);
    for (i = 0; i < nv; i++) {
        voice_t *v = &t->v[i];
        int32_t k = 2 * (int32_t)i - (int32_t)(nv - 1u);   /* -7 .. 7 */
        if (nv > 1u && nv < NVOICE)
            k = k * (NVOICE - 1) / (int32_t)(nv - 1u);      /* fewer voices: the outer ones as wide */
        v->fine = nv > 1u ? k * t->p[P_DETUNE] * 56 / 889 : 0;   /* up to ~±40 cents */
        if (retrig || !v->active || !v->gate) {
            if ((!v->active || v->stage == 4u) && !voice_room(t, i > 0))
                continue;                                   /* no room for this extra UNISON voice */
            /* level: about the same sum for 8 or 4 voices at random phases */
            voice_start(t, v, note, nv >= NVOICE ? vel * 36u / 100u : nv > 1u ? vel / 2u : vel, glide);
            if (nv > 1u && i && !ENGINES[t->engine]->sampled) {   /* random start phases: */
#if LRRY_BENCH
#define MONO_SEED bench_mono_seed                           /* (bench builds: at file level, so the per-test reset can put it back: bench.c) */
#else
                static uint32_t seed = 0x1234567u;          /* in phase they stack, evenly spread they cancel */
#define MONO_SEED seed
#endif
                MONO_SEED = MONO_SEED * 1664525u + 1013904223u;
                v->ph[0] += MONO_SEED;
            }
        } else {                                            /* legato: new pitch, same envelope */
            v->note = (uint8_t)note;
            v->pitch16 = (int32_t)note * 16;
            v->mvel = t->m_vel;
            v->mrnd = t->m_rnd;
            v->xg = t->lay_xg;
            if (vel > v->vel && nv == 1u)
                v->vel = (uint8_t)vel;
            glide_set(t, v, glide);
        }
    }
    t->mono_note = (uint8_t)note;
}

/* the note the mono stack asks for (PRIO: LAST / LOW / HIGH), 0 = none */
static uint32_t mono_pick(const track_t *t)
{
    uint32_t i, n;
    if (!t->nmono)
        return 0;
    n = t->mono_stack[t->nmono - 1u];
    for (i = 0; i < t->nmono; i++) {
        if (t->p[P_PRIO] == 1 && t->mono_stack[i] < n)
            n = t->mono_stack[i];
        if (t->p[P_PRIO] == 2 && t->mono_stack[i] > n)
            n = t->mono_stack[i];
    }
    return n;
}

static void mono_remove(track_t *t, uint32_t note)
{
    uint32_t j, k = 0;
    for (j = 0; j < t->nmono; j++)
        if (t->mono_stack[j] != note)
            t->mono_stack[k++] = t->mono_stack[j];
    t->nmono = (uint8_t)k;
}

static void trk_note_on_x(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t any = 0, i, mode = trk_vmode(t);
    if (t->p[P_MUTE])
        return;
    if (t->xf_on || t->eng_req != t->engine) {          /* engine switch under way: after the fade */
        for (i = 0; i < t->xp_n && t->xp_note[i] != note; i++)
            ;
        if (i == t->xp_n && t->xp_n < 4u)
            t->xp_n++;
        i = i < t->xp_n ? i : t->xp_n - 1u;             /* (full: the last one is replaced) */
        t->xp_note[i] = (uint8_t)note;
        t->xp_vel[i] = (uint8_t)vel;
        return;
    }
    mod_note(t, note, vel);                             /* the matrix's VEL / KEY / RAND */
    NT(1);
    for (i = 0; i < NVOICE; i++)
        any |= t->v[i].gate;
    if (!any && !t->arp_ph) {                       /* fresh phrase: the LFO restarts at PHS only with TRIG NOTE (FREE, the default since 0.2.2, never restarts the phase), and its FADE starts over with either (an arp is one phrase: its steps after the first are not fresh) */
        if (!t->p[P_LTRIG])
            t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
        t->lfo_fade = 0;
        if (!t->p[P_L2TRIG])                            /* L/RRY: LFO 2 the same */
            t->lfo2.ph = (uint32_t)t->p[P_L2PHASE] << 25;
        t->lfo2.fade = 0;
    }
    if (mode == V_POLY) {
        voice_t *v = voice_alloc(t, note);
        NT(2);
        t->nmono = 0;                                   /* no stale mono stack after a mode change */
        t->mono_note = 0;
        v->fine = 0;
        voice_start(t, v, note, vel, t->p[P_GLIDE] != 0);
        return;
    }
    {
        uint32_t sounding = t->mono_note && t->v[0].active && t->v[0].gate, want;
        mono_remove(t, note);
        if (t->nmono >= 8u)
            mono_remove(t, t->mono_stack[0]);
        t->mono_stack[t->nmono++] = (uint8_t)note;
        want = mono_pick(t);
        if (sounding && want == t->mono_note)
            return;                                     /* LOW / HIGH priority: this key does not win */
        if (mode == V_MONO)                             /* MONO: always retrigger, glide when GLIDE is set */
            mono_play(t, want, vel, 1, t->p[P_GLIDE] != 0 || t->slide_glide);
        else                                            /* LEGATO / UNISON: glide only between held notes */
            mono_play(t, want, vel, !sounding, sounding && (t->p[P_GLIDE] != 0 || t->slide_glide));
    }
}

/* a note-on of part t: what the voice and the engine read as it starts (DTUNE, LFO PHASE, GLIDE, an engine's note-on
 * values) with the MORPH fader's values (morph.c), as its block will have them */
static void trk_note_on_now(track_t *t, uint32_t note, uint32_t vel)
{
    int mo;
    NT_RESET();
    mo = morph_begin(t, 0);
    NT(0);
    trk_note_on_x(t, note, vel);
    NT(6);
    if (mo)
        morph_end(t);
    NT(7);
}

/* NOTE-START STAGGER (L/RRY 1.1 item 144; owner: internal, not a setting). A burst of new voices in ONE block (a chord, several keys or MIDI notes at once)
 * is what made core 1's job late on the unit (HANDOFF item 129 / 139: every POLY-MAX failure at 7..8 voices a part came 7..15 ms into the note-on burst; the
 * steady load at the same count was fine). So a POLY part starts at most VS_MAX voices in one block; the next note-ons of that block wait in the part's queue
 * (vs_q, in order) and start in the following blocks, VS_MAX a block (vs_block, at the head of events_block): 8 voices at once start 2 + 2 + 2 + 2, the last
 * 3 blocks (2.18 ms) late; a pair starts as before, a third tone (a triad's top) 1 block (0.73 ms) late; a single key is never held back unless a burst is still
 * waiting. VS_MAX = 2 (was 3 in test8 / BENCHV6, item 144; 3 was not enough for WHEEL / FM6 on the unit, item 145 E): a measured voice start on the host (tests/cost_cal.txt's scale; scratch probe of item 144) costs ~940..2000 host instructions for the
 * note-on itself (0.15..0.35 of one voice's block) and its first blocks render within +-2 % of the steady ones, so the unit's burst is not the start code
 * but the fresh voices' first blocks (cache, inferred from v4: +60..+360 us over steady at 8 voices = ~7..45 us a fresh voice); 2 fresh voices a block keeps a
 * part's burst well below what POLY-MAX showed clean at 6 voices starting together (a triad's third tone is one block late). A test build may set another.
 * Only POLY parts are held back (MONO / LEGATO / UNISON: one note-on is one gesture; UNISON's voices start together as before, they are counted), and only a
 * note that starts a voice from rest: a re-strike of a note the part sounds already (an arp or sequencer step on a held chord) restarts its voice in place
 * with its states kept, as cheap as any note-on, so it plays at once and is not counted.
 * The queue lives in events_block's context only: every note-on and note-off of a part comes from there (keys, MIDI, arp, sequencer, STRUM, an engine
 * switch's waiting notes), on core 0, before core 1's job for the block is posted, so it is the same whichever core renders part 2 (two cores, the take-over,
 * one core). A note-off of a waiting note cancels it (trk_note_off), a panic / STOP ALL / MODE change empties it (trk_all_off). Starting later through
 * trk_note_on_x, a waiting note meets the voice cap, stealing and the NOTES budget exactly as one played then would. */
#ifndef VS_MAX
#define VS_MAX 1u                                       /* new voices a part starts in one block (2026-11-12: 1, as DUELL since 2026-11-03: bench 1112F/G, a 1 .. 1.5 ms gap at the start of
                                                         * a 4 to 6 voice chord of a heavy sound at 2 a block; L/RRY's voices are dearer than DUELL's) */
#endif
#define VS_QN 16u                                       /* note-ons a part may hold back (full: the next one starts at once; nothing is ever dropped) */
typedef struct { uint8_t note, vel; uint16_t xg; } vs_ent_t;
static vs_ent_t vs_q[NTRK][VS_QN];
static uint8_t vs_qn[NTRK], vs_n[NTRK];                 /* waiting; started this block */
static uint32_t vs_held;                                /* note-ons held back since power-on (tests, console) */
/* note sounds on part t already (a re-strike: voice_alloc restarts that voice in place, its states kept: no start from rest, nothing to stagger) */
static int vs_sounding(const track_t *t, uint32_t note)
{
    uint32_t i, np = trk_nvoice(t);
    for (i = 0; i < np; i++)
        if (t->v[i].active && t->v[i].stage != 4u && t->v[i].note == note)
            return 1;
    return 0;
}
static void trk_note_on_1(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t p = (uint32_t)(t - trk);
    if (p < NTRK && !t->p[P_MUTE] && !(trk_vmode(t) == V_POLY && vs_sounding(t, note))) {
        if (trk_vmode(t) == V_POLY && (vs_qn[p] || vs_n[p] >= VS_MAX) && vs_qn[p] < VS_QN) {
            vs_ent_t *e = &vs_q[p][vs_qn[p]++];         /* (in order behind the ones already waiting) */
            e->note = (uint8_t)note;
            e->vel = (uint8_t)vel;
            e->xg = t->lay_xg;                          /* (LAYER XFADE: the share it was played with) */
            vs_held++;
            return;
        }
        vs_n[p] = (uint8_t)(vs_n[p] + (trk_vmode(t) == V_UNISON ? trk_nvoice(t) : 1u));
    }
    trk_note_on_now(t, note, vel);
}
/* a block starts (seq.c events_block, before its keys, MIDI, arp and steps): the counts start again, the waiting note-ons start, VS_MAX a part, oldest first */
static __attribute__((noinline)) void vs_block(void)
{
    uint32_t p, i, k;
    for (p = 0; p < NTRK; p++) {
        track_t *t = &trk[p];
        vs_n[p] = 0;
        for (k = 0; k < vs_qn[p] && vs_n[p] < VS_MAX; k++) {
            uint16_t xg = t->lay_xg;
            t->lay_xg = vs_q[p][k].xg;
            vs_n[p]++;
            trk_note_on_now(t, vs_q[p][k].note, vs_q[p][k].vel);
            t->lay_xg = xg;
        }
        for (i = 0; k + i < vs_qn[p]; i++)
            vs_q[p][i] = vs_q[p][k + i];
        vs_qn[p] = (uint8_t)i;
    }
}
/* a note-off: the same note waiting in part t's queue never starts */
static void vs_cancel(track_t *t, uint32_t note)
{
    uint32_t p = (uint32_t)(t - trk), i, k = 0;
    if (p >= NTRK)
        return;
    for (i = 0; i < vs_qn[p]; i++)
        if (vs_q[p][i].note != note)
            vs_q[p][k++] = vs_q[p][i];
    vs_qn[p] = (uint8_t)k;
}

/* L/RRY step 5, SPEC 5b.1 LAYER: nothing here. The notes reach both parts as INPUT (seq.c key_on / key_off, midi_control.c
 * midi_play / midi_release: each part's own input_on / input_off, chord and arp), so every part sounds what its own arp
 * (or its direct play) asks, one voice per note from the shared budget (NVOICE) */
#define trk_note_on trk_note_on_1
static void trk_note_off_1(track_t *t, uint32_t note);
#define trk_note_off trk_note_off_1

static void trk_note_off_1(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, mode = trk_vmode(t);
    vs_cancel(t, note);                                 /* (item 144: held back by the start limit: it never starts) */
    for (i = 0; i < t->xp_n; i++)                       /* not sounding yet (engine switch): forget it */
        if (t->xp_note[i] != note) {
            t->xp_note[k] = t->xp_note[i];
            t->xp_vel[k++] = t->xp_vel[i];
        }
    t->xp_n = (uint8_t)k;
    mono_remove(t, note);                              /* a mode change must not retain a released key */
    if (mode != V_POLY) {
        uint32_t nv = mode == V_UNISON ? trk_nvoice(t) : 1u;
        if (t->mono_note == note) {
            uint32_t next = mono_pick(t);
            if (next) {                                 /* fall back to a held note, legato */
                mono_play(t, next, t->v[0].vel, 0, t->p[P_GLIDE] != 0);
            } else {
                for (i = 0; i < nv; i++)
                    if (t->v[i].gate) {
                        t->v[i].gate = 0;
                        t->v[i].stage = 3;
                    }
                t->mono_note = 0;
            }
        }
    }
    for (i = 0; i < NVOICE; i++)                        /* a fallback above already has its new pitch */
        if (t->v[i].note == note && t->v[i].gate) {
            t->v[i].gate = 0;
            t->v[i].stage = 3;
        }
}

static void trk_all_off_1(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        t->v[i].gate = 0;
        t->v[i].stage = !t->v[i].active ? 0 : t->v[i].stage == 4u ? 4 : 3;   /* (item 135: a voice already fading out (voice_kill: STOP ALL, CC120, a steal, the NOTES trim) keeps
                                                                             * its one-block fade; made a release it sounded on for its whole RELEASE, e.g. STOP ALL and a load in one block) */
    }
    t->nmono = 0;
    t->mono_note = 0;
    t->xp_n = 0;
    if ((uint32_t)(t - trk) < NTRK)
        vs_qn[t - trk] = 0;                             /* (item 144: nothing held back by the start limit starts after it) */
}
#define trk_all_off trk_all_off_1

/* engine switch, at each block start (events_block), before any note of the block. The UI writes
 * eng_req and the new engine's P_E0..P_E7 together (IRQ off). The part's sounding voices (released by
 * the preset change) then fade out over XF_BLOCKS blocks on the old engine, rendered with its own
 * parameters (pe_old: an engine never reads another engine's values, which index its tables); only
 * then the engine switches and the notes that came during the fade start on it. A part with nothing
 * sounding switches at once. */
#define XF_BLOCKS 4u                                    /* 4 x 32 samples: 2.9 ms */
static void engine_block(track_t *t)
{
    uint32_t i, any = 0;
    if (t->xf_on || t->eng_req != t->engine) {
        if (!t->xf_on) {
            for (i = 0; i < NVOICE; i++)
                any |= t->v[i].active;
            if (any) {
                t->xf_on = 1;
                t->xf = XF_BLOCKS;
                return;
            }
        } else if (t->xf) {
            return;                                     /* still fading (track_render counts down) */
        }
        for (i = 0; i < NVOICE; i++) {                  /* faded to 0: gone */
            voice_t *v = &t->v[i];
            v->active = v->gate = 0;
            v->stage = 0;
            v->env = v->env_out = 0;
        }
        t->engine = eng_idx(t->eng_req);
        eng_mem_claim(t);                               /* its state region, before the waiting notes */
        t->xf_on = 0;
        t->nmono = 0;
        t->mono_note = 0;
        {
            uint32_t n = t->xp_n;
            t->xp_n = 0;
            for (i = 0; i < n; i++)
                trk_note_on_1(t, t->xp_note[i], t->xp_vel[i]);   /*  */
        }
    }
    eng_mem_claim(t);                                   /* (an engine set directly: power-on, a test) */
    for (i = 0; i < 8u; i++)                            /* the engine's own values, for a later fade */
        t->pe_old[i] = t->p[P_E0 + i];
}

/* ENV SYNC (1.5, #175; P_ESYNC, params.c ESYNC_NAMES): ATK DEC REL as note values of the tempo (BPM, or the external
 * clock's measured tempo: fx.c beat_samples, as LFO 2 SYNC). The part's three steps, made once a block before its
 * voices (track_render; the parts render one at a time), as gen_tables.py makes ENV_LIN / ENV_EXP for a time in ms:
 * the attack's linear step 2^24 CTL / T (rounded), the decay's and release's 1 - e^-x (x = 4.6 CTL / T: ~99 % of the way
 * after T), here as x - x^2/2 + x^3/6 - x^4/24 (x <= 0.33 at the shortest, 1/64T at 240 BPM: under 0.04 % off). The
 * coefficient is Q16 as ENV_EXP's: past ~2 s it is a few units, the time within half of one (4BAR at 90 BPM: 2.5 %).
 * Value 0 (and a time below a block) takes the tables' 0, as with SYNC OFF. OFF: the tables' values, as before (looked
 * up once a block here instead of once a voice in env_tick: the same numbers) */
static uint32_t beat_samples(void);                /* (fx.c) */
_Static_assert(CTL == 32, "esync_one: 4.6 CTL in Q16 is 9646899");
/* t->esk[3]: ATK's step, DEC's and REL's coefficient (Q16): the part's, this block (env_tick); t->esk2[3] .. ENV 2's (L/RRY, env2_tick; made only while the matrix reads it).
 * Fields of track_t (job 3: they were two globals the parts' stages both wrote) */
static uint32_t esync_one(uint32_t quarter, int32_t v, uint32_t i)
{
    uint32_t u = ESYNC_UNITS[esync_idx(v)], T = quarter * u / 96u, x, x2, x3, x4;   /* T: samples */
    if (T < CTL)
        return i ? ENV_EXP[0] : ENV_LIN[0];
    if (!i)
        return ((1u << 24) * CTL + T / 2u) / T;
    x = (9646899u + T / 2u) / T;                        /* 4.6 CTL (CTL 32), Q16, rounded */
    if (x > 49152u)                                     /* (below ~200 samples: 0.75, the series 0.3 % low) */
        x = 49152u;
    x2 = x * x >> 16;
    x3 = x2 * x >> 16;
    x4 = x3 * x >> 16;
    x = x - x2 / 2u + x3 / 6u - x4 / 24u;
    return x < 1u ? 1u : x > 65535u ? 65535u : x;
}
/* the steps of the envelope whose ATK DEC REL are ids a, d, r of t into k (ENV 1: P_ATK..; L/RRY's ENV 2: P_E2ATK..) */
static void esync_coefs(const track_t *t, uint32_t a, uint32_t d, uint32_t r, uint32_t *k)
{
    const int16_t *p = t->p;
    if (p[P_ESYNC]) {
        uint32_t q = beat_samples();
        k[0] = esync_one(q, p[a], 0);
        k[1] = esync_one(q, p[d], 1);
        k[2] = esync_one(q, p[r], 2);
    } else {                                            /* OFF: the tables, as always */
        k[0] = ENV_LIN[p[a] & 127];
        k[1] = ENV_EXP[p[d] & 127];
        k[2] = ENV_EXP[p[r] & 127];
    }
}
static __attribute__((noinline)) void esync_coef(const track_t *t)   /* (not inlined: its divides stay out of the
                                                                    * audio ISR's loop over the voices) */
{
    esync_coefs(t, P_ATK, P_DEC, P_REL, ((track_t *)t)->esk);
}
static __attribute__((noinline)) void esync2_coef(const track_t *t)
{
    esync_coefs(t, P_E2ATK, P_E2DEC, P_E2REL, ((track_t *)t)->esk2);
}

#include "cycle.c"                                      /* the CYCLE envelope (cloud job F): cycle_start, cycle_tick, cycle_value */
/* ENV 2 (L/RRY, SPEC 2.4): a second ADSR per voice, ENV 1's stages and times (ENV SYNC too: esync2_coef): the filter
 * envelope of the classic arrangement (ENV 2 DEST: FLT, SHP, track_render) and a matrix source (mod.c MS_ENV2). It follows the gate: an attack as the voice starts (voice_start; a legato note keeps it), the
 * release from its level when the key is let go; at 0 it stays there. It never ends the voice (ENV 1 does). Ticked once
 * per control tick of a voice while an active matrix slot reads it (mod.e2) or an ENV 2 DEST depth is set: otherwise no
 * cost, no change to the sound */
static void env2_tick(const track_t *t, voice_t *v)
{
    int32_t sus = (int32_t)t->p[P_E2SUS] << 17;         /* Q24 */
    if (!v->gate && v->stage2 && v->stage2 < 3u)
        v->stage2 = 3;
    switch (v->stage2) {
    case 1:
        v->env2 += (int32_t)t->esk2[0];
        if (v->env2 >= (1 << 24)) {
            v->env2 = 1 << 24;
            v->stage2 = 2;
        }
        break;
    case 2:
        v->env2 += mulq16(sus - v->env2, t->esk2[1]);
        break;
    case 3:
        v->env2 -= mulq16(v->env2, t->esk2[2]);
        if (v->env2 < (1 << 12)) {
            v->env2 = 0;
            v->stage2 = 0;
        }
        break;
    default:
        v->env2 = 0;
        break;
    }
}

/* one control tick (CTL samples) of the amplitude envelope; returns Q15 */
static int32_t env_tick(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    int32_t sus = (int32_t)p[P_SUS] << 17;              /* Q24 */
    switch (v->stage) {
    case 1:
        v->env += (int32_t)t->esk[0];                  /* ATK's step (esync_coef: the table's, or ENV SYNC's) */
        if (v->env >= (1 << 24)) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += mulq16(sus - v->env, t->esk[1]);
        break;
    case 3:
        v->env -= mulq16(v->env, t->esk[2]);
        if (v->env < (1 << 12)) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    case 4:                                             /* given up for another part: this block fades it */
        v->env = 0;
        v->stage = 0;
        v->active = 0;
        break;
    default:
        v->env = 0;
        break;
    }
    return v->env >> 9;
}

/* ARP TARGET (SPEC 2.3, seq.c atg_fire): the voice's arp pitch towards the arp's note now (the root's while there is none),
 * with the part's GLIDE as a note's (RATE: GLD's speed; 0: a jump), as the offset adelta over the root's pitch16 (so
 * TUNE, bend, LFO, ENV and the matrix move both alike); its level towards the step's gate (velocity, 0 off) in ~3 ms */
static __attribute__((noinline)) void atg_voice(const track_t *t, voice_t *v, vmod_t *m)
{
    int32_t want = t->atg_note ? (int32_t)t->atg_note * 16 : v->pitch_cur, d = want - v->apitch, g;
    if (d && !t->p[P_GLIDE]) {
        v->apitch = want;
    } else if (d) {
        int32_t st = (int32_t)(ENV_LIN[t->p[P_GLIDE] & 127] >> 14);
        if (st < 1)
            st = 1;
        v->apitch += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
    }
    m->adelta = v->apitch - v->pitch_cur;
    g = t->atg_vel * 258 - v->again;
    v->again += g > -4 && g < 4 ? g : g / 4;
    m->again = v->again;
}

/* L/RRY 0.2.9 MULTIMODE FILTER (SPEC 2.5; 0.2.2's universal filter): a per-voice state-variable filter on the engines with no filter of
 * their own (engine_t.ufilt 1: all of them since cloud job J; ANALOG's VCF and NOISE's filter ARE this filter, TRIO's lives on as filter 2). FILTER page: K1 CUT (P_FREEA, 0..UF_STEPS: 30 Hz .. 16 kHz, core.h),
 * K2 RES (P_FREEB: the damping from 2.0, Q 0.5, to 0.145, Q 6.9; the 24 dB path is unchanged), K3 MODE (P_FREEC: 0 LP, 64 BP, 127 HP and every blend between:
 * the SVF's low-pass, band-pass and high-pass outputs summed with weights), K4 SLOPE (P_FREED: 12 dB, or 24 dB: a second stage of
 * the same MODE, the two dampings a 4th-order Butterworth's, 1.848 and 0.765, RES narrowing the second down to 0.10).
 * The EFFECTIVE cut = CUT + every cutoff route (ENV 1 / ENV 2 / LFO 1 / LFO 2 FLT, the matrix's CUT; 1/256 dial steps, 63 steps = the
 * whole of an old dial: their depths as in 0.2.2), per voice and block, clamped to 0..UF_STEPS; the accent's opening does not apply.
 * KEY TRK (0.2.21, P_KTRK): the effective cut also follows the played note (core.h UF_KT: -100..+100 %, around C3, ANALOG's KTR centre), added per voice
 * and block before the clamp, so the coefficient of uf_run just takes another cut (it is worked out every block anyway: nothing new in the hot loop, no click
 * on a turn); 0 adds nothing. With CUT at the open end a positive KEY TRK closes the notes below C3, so the bypass test below is on the effective cut.
 * BYPASS: MODE LP with the effective cut at the open end (RES and SLOPE do not matter there): the voice renders straight into the
 * part's buffer (no work, bit for bit as without a filter): the defaults, so every sound of before plays as it did. (A store of before 0.2.9, with 0.2.2's
 * bipolar CUT, was converted at its load into this one until L/RRY 0.2.14; it is no longer read.)
 * KILL ZONES: within the last 4 dial steps at an end the output's share that the end closes fades to 0 (the low-pass and band-pass
 * shares to 30 Hz, the high-pass and band-pass ones to 16 kHz), so a filter fully closed is silent. The weights are worked out per
 * block (Q10) and ramp over it from the last block's (a MODE turn, a route through a kill zone: no click).
 * The filter is the trapezoidal SVF of dsp.c (ANALOG's, TRIO's: unconditionally stable at any cutoff and resonance; the integrators
 * clamp at +-150000, the second stage's input at +-65536, the output goes through a soft knee at the voice's full level, 48000), run on
 * the voice AFTER its amplitude, in a scratch block (the engine renders into a cleared buffer, the filtered block is added to the
 * part's). It starts from rest (the integrators charged with the block's first sample: no step) when a voice starts or after a
 * bypass; the second stage starts charged with the first's low-pass state, and a SLOPE turned while it sounds fades the second stage
 * in (out) over one block */
#define UF_TOP (UF_STEPS << 8)                          /* the open end, Q8 dial steps */
/* the three engines that lost their own filter (cloud job J, TWIN_FILTER_DESIGN_2 1.2): the accent (velocity over 110 opens the filter with the envelope) lifts BOTH filters on them, filter 1
 * (in its effective cutoff) and filter 2 even with LINK off (chain_f2.c f2_cut); on the other engines the lift stays the engine's own tone control and filter 2's linked cutoff */
#define ACCENT_BOTH(e) ((e) == &ENG_ANALOG || (e) == &ENG_TRIO || (e) == &ENG_NOISE)
#define UF_KZ (4 << 8)                                  /* the kill zones: 4 dial steps (= 1.0 in Q10, the weights' unit) */
#define UF_K(res) (8192 - (res) * 7600 / 127)           /* damping, Q12: 12 dB. 2.0 down to 0.145 (the law of dsp.c tsvf_coef, ANALOG's VCF's: TWIN_FILTER_DESIGN_2 question 2a; until the engine filters left it was 1.414 down to 0.145: 5793 - res * 5200 / 127) */
#define UF_K24A 7568                                    /* 24 dB: the first stage's (1.848), the second's from 0.765 down with RES */
#define UF_K24B(res) (3135 - (((res) * 2731) >> 7))   /* (0.765 down to 0.104) */
#define UF_KNEE 48000
static __attribute__((noinline)) void uf_coef(tsvf_t *f, int32_t idx, int32_t k) { tsvf_coef_k(f, idx, k); }   /* (one copy: a divide) */
/* one voice's block through the filter, in place: uc the effective cut (Q8 dial steps), p the part's values (RES MODE SLOPE) */
static __attribute__((noinline)) void uf_run(voice_t *v, int32_t *buf, uint32_t n, int32_t uc, const int16_t *p)
{
    tsvf_t f, g;
    uint32_t i, s24 = p[P_FREED] == 2, run2, lp;
    int32_t md = clamp(p[P_FREEC], 0, UF_HP), res = clamp(p[P_FREEB], 0, 127), k1 = s24 ? UF_K24A : UF_K(res), k2 = UF_K24B(res);
    int32_t bl = 0, blend = 0;                           /* MORPH (0.2.9): the two ends' SLOPEs cross-faded, 0 (12 dB) .. 32 (24 dB) */
    int32_t wb = md <= 64 ? md << 4 : ((UF_HP - md) * 65) >> 2, wl = md < 64 ? 1024 - wb : 0, wh = md > 64 ? 1024 - wb : 0;   /* MODE: LP .. BP
                                                         * .. HP (Q10; 1024 / 63 a step above BP, no divide) */
    int32_t gl = uc < UF_KZ ? uc : UF_KZ, gt = UF_TOP - uc < UF_KZ ? UF_TOP - uc : UF_KZ, idx = 40 + ((uc * 99) >> 6);
    int32_t cx = (wh * gt) >> 10, cb = ((((wb * gl) >> 10) * gt) >> 10) - cx, cl = ((wl * gl) >> 10) - cx;   /* the output: cx x + cb band-pass
                                                         * + cl low-pass (Q10; the kill zones in them), this block's */
    int32_t a1 = v->uf[0], a2 = v->uf[1], b1 = v->uf[2], b2 = v->uf[3], ax, ab, al, dx, db, dl, fd = 0, xg;
    if (v->msl) {                                        /* (v->msl: the part's MXF slope weight, bit 15 set when the other end is 24 dB; track_render sets it) */
        int32_t b24 = v->msl & 0x8000u ? 32 : 0;
        bl = s24 ? 32 : 0;
        bl += ((b24 - bl) * (int32_t)(v->msl & 0x7FFFu)) >> 15;
        s24 = bl >= 32;
        blend = bl > 0 && bl < 32;
        if (blend)
            k1 = UF_K(res) + (((UF_K24A - UF_K(res)) * bl) >> 5);   /* (the first stage's damping between the two) */
    }
    if (!v->uft) {                                      /* from rest: no ramp */
        a1 = a2 = 0;
        v->ufw[0] = (int16_t)cx; v->ufw[1] = (int16_t)cb; v->ufw[2] = (int16_t)cl;
    } else if (!blend && v->uft != 1u + s24) {
        fd = s24 ? 1 : -1;                              /* SLOPE turned while sounding: the second stage fades in (out) over the block */
    }
    if (!cx && !a1 && !a2)
        a2 = buf[0] >> 1;                               /* (no high-pass share: the low-pass charged with the signal, no step at the start; 0.2.2's rule) */
    if ((s24 || blend) && v->uft != 2u) {               /* the second stage starts: charged with the first's low-pass */
        b1 = 0;
        b2 = a2;
    }
    v->uft = (uint8_t)(1u + (s24 || blend));
    run2 = s24 || blend || fd;
    xg = fd > 0 ? 0 : 32;
    uf_coef(&f, idx, k1);
    if (run2)
        uf_coef(&g, idx, k2);
    ax = v->ufw[0] << 5; ab = v->ufw[1] << 5; al = v->ufw[2] << 5;   /* (CTL 32: the weights ramp, Q15, exactly onto this block's) */
    dx = cx - v->ufw[0]; db = cb - v->ufw[1]; dl = cl - v->ufw[2];
    lp = !(dx | db | dl | cx | cb) && cl == 1024;      /* a steady low-pass: its output is the low-pass itself (0.2.2's loop's cost) */
    for (i = 0; i < n; i++) {
        int32_t x = buf[i] >> 1, v3 = x - a2, v1 = (f.a1 * a1 + f.a2 * v3) >> 13, v2 = a2 + ((f.a2 * a1 + f.a3 * v3) >> 13), y = v2;
        a1 = clamp(2 * v1 - a1, -150000, 150000);
        a2 = clamp(2 * v2 - a2, -150000, 150000);
        if (!lp) {
            ax += dx; ab += db; al += dl;
            y = ((ax >> 5) * x + (ab >> 5) * ((k1 * v1) >> 12) + (al >> 5) * v2) >> 10;
        }
        if (run2) {                                     /* 24 dB: the same MODE again */
            int32_t z = clamp(y, -65536, 65536), u3 = z - b2, u1 = (g.a1 * b1 + g.a2 * u3) >> 13, u2 = b2 + ((g.a2 * b1 + g.a3 * u3) >> 13);
            b1 = clamp(2 * u1 - b1, -150000, 150000);
            b2 = clamp(2 * u2 - b2, -150000, 150000);
            u2 = lp ? u2 : ((ax >> 5) * z + (ab >> 5) * ((k2 * u1) >> 12) + (al >> 5) * u2) >> 10;
            if (fd) {
                xg += fd;
                u2 = y + (((u2 - y) * xg) >> 5);
            } else if (blend) {
                u2 = y + (((u2 - y) * bl) >> 5);
            }
            y = u2;
        }
        buf[i] = soft_knee(y << 1, UF_KNEE);
    }
    v->ufw[0] = (int16_t)cx; v->ufw[1] = (int16_t)cb; v->ufw[2] = (int16_t)cl;
    v->uf[0] = a1; v->uf[1] = a2; v->uf[2] = b1; v->uf[3] = b2;
}
/* one voice of a ufilt engine with the filter on: the engine into a cleared scratch block, filtered, added to dst */
static __attribute__((noinline)) void uf_voice(const engine_t *e, track_t *t, voice_t *v, int32_t *dst, uint32_t n,
                                               const vmod_t *m, int32_t uc)
{
    int32_t tmp[CTL];
    uint32_t i;
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++)
        tmp[i] = 0;
    e->render(t, v, tmp, n, m);
    uf_run(v, tmp, n, uc, t->p);
    for (i = 0; i < n; i++)
        dst[i] += tmp[i];
}
/* L/RRY 0.2.25: the bypass edge without a click (owner: CUTOFF turned between 81 and the open end 82 crackled, #83 TINE ROOM sits at 82). The
 * bypass (MODE LP, the effective cut at the open end) is dry; the filter one step below is a low-pass near 16 kHz with its own phase and
 * soft knee, and it started from rest: the step between the two was ~4 x the music's sharpest. A voice that crosses the edge while it
 * sounds now has one block cross-faded: in = the filter starts (from rest, as before) and the block goes from the dry signal to the filter's;
 * out = the filter runs one more block at the open end and the block goes from it to the dry signal. Out of line, in the flash (uf_run and
 * uf_voice, hot, are untouched); a voice that never crosses, and a note's start, play bit for bit as before. v->uft 3: bypassed while sounding
 * (the next filtered block fades in); 0 as ever: from rest (a new note) */
#define UF_BYP 3u
static __attribute__((noinline)) void uf_edge(const engine_t *e, track_t *t, voice_t *v, int32_t *dst, uint32_t n, const vmod_t *m, int32_t uc, int in)
{
    int32_t tmp[CTL], dry[CTL];
    uint32_t i;
    if (n > CTL)
        n = CTL;
    for (i = 0; i < n; i++)
        tmp[i] = 0;
    e->render(t, v, tmp, n, m);
    for (i = 0; i < n; i++)
        dry[i] = tmp[i];
    uf_run(v, tmp, n, uc, t->p);
    for (i = 0; i < n; i++) {
        int32_t w = (int32_t)(in ? i + 1u : n - 1u - i);  /* (the filter's share, of n) */
        dst[i] += dry[i] + (tmp[i] - dry[i]) * w / (int32_t)n;   /* (32-bit: |a difference| < 2^18, x 32) */
    }
}
#include "voice_chain.c"                                /* cloud job B: the voice chain (voice_chain.h), its stage hooks chain_f2.c / chain_sub.c (jobs E, F) and its sequence; uf_run, uf_voice, uf_edge as they are */
/* MASTER TUNE (L/RRY 0.2.22): G_TUNE is the frequency of A4 in 0.1 Hz (TUNE_MIN 4153 .. TUNE_MAX 4662, TUNE_DEF 4400 = 440.0 Hz); a value outside the range (an older
 * project's cents, a stray write) reads as 440.0. The offset 1200 log2(f / 440) is worked out ONCE, when the value changes (tune_get: the last value is kept), in the form
 * the voice already adds: whole 1/16 semitones (*u) and a rest in 1/4096 of the increment (*f; 1 ct = 2.367), both towards zero as the cents were. log2 by repeated squaring
 * (Q30, 24 bits), integers only. The rest is on the 1/4096 grid, so the pitch is within 0.21 ct of the exact one (440.0 is exactly 0 / 0: the sound of TUNE 0). */
static int16_t tune_key = TUNE_DEF;
static int16_t tune_u, tune_f;
static void tune_get(int32_t *u, int32_t *f)
{
    int32_t g = song.g[G_TUNE];
    if (g != tune_key) {
        int32_t su = 0, sf = 0;
        if (g >= TUNE_MIN && g <= TUNE_MAX && g != TUNE_DEF) {
            uint64_t x = ((uint64_t)(g > TUNE_DEF ? g : TUNE_DEF) << 30) / (uint64_t)(g > TUNE_DEF ? TUNE_DEF : g);   /* the ratio >= 1, Q30 */
            uint32_t l = 0, i, w, r;
            for (i = 0; i < 24u; i++) {
                x = (x * x) >> 30;
                l <<= 1;
                if (x >= (2ull << 30)) {
                    x >>= 1;
                    l |= 1u;
                }
            }
            w = l * 192u;                              /* 1/16 semitones, Q24 (a 1/16 semitone = 6.25 ct) */
            r = (uint32_t)((((uint64_t)(w & 0xFFFFFFu)) * 14794u / 1000u + (1u << 23)) >> 24);   /* the rest: 6.25 ct x 2.367 = 14.794 per 1/16 semitone */
            su = (int32_t)(w >> 24);
            sf = (int32_t)r;
            if (g < TUNE_DEF)
                su = -su, sf = -sf;
        }
        tune_key = (int16_t)g;
        tune_u = (int16_t)su;
        tune_f = (int16_t)sf;
    }
    *u = tune_u;
    *f = tune_f;
}

#if LRRY_ENGINE
/* voices first..last-1 of the mask, from lrj: the kernel adds each pair to out / outR */
static void lrry_render_set(uint32_t ti, uint32_t set, int32_t *out, int32_t *outR, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (((lrj.mask & set) >> i) & 1u)
            lrry_voice(&lrry_pp[ti], &lrj.vc[i], &lrry_vs[ti][i], &lrj.m[i], out, outR, n);
}
#if LRRY_PIPELINE
/* L/RRY cores-pipeline: THE VOICE SUM IS ONE BLOCK LATE, in every mode (one core, two cores, core 1 paused, parked, failed), so a switch between modes never changes the latency: nothing is inserted or dropped.
 * Block N's voices are rendered (core 0's share into lrry_pl[cur], core 1's by the job posted here) and handed to the rest of the part (insert, mix, gather, master) in block N+1: out of this call comes
 * block N-1's sum. The job of block N-1 is closed here first (twocore_run.c tcr_pipe_collect: its sums added to block N-1's), before this block's job is posted, so core 1 runs block N's voices while core 0 does
 * the rest of block N (mix, master, the DAC write), block N+1's events and control. Returns the voice count to report (the part sounds when block N-1 had voices) */
static int32_t lrry_pl[2][2][CTL];                      /* [buffer][left, right][frame]: core 0's own sums of a block (+ core 1's, added at the collect) */
/* the stage pans' ramp starts = the pans of this block (the decode was cached): the same assignments as the plain build, and the version moves only if one of them changed a value */
static void lrry_pp_ramps(lrry_pp_t *pp, uint32_t ti)
{
    if (pp->pa0.g != pp->pa.g || pp->pa0.side != pp->pa.side || pp->pb0.g != pp->pb.g || pp->pb0.side != pp->pb.side ||
        pp->pd0[0].g != pp->pd[0].g || pp->pd0[0].side != pp->pd[0].side || pp->pd0[1].g != pp->pd[1].g || pp->pd0[1].side != pp->pd[1].side || pp->d_ramp)
        lrry_pp_ver[ti]++;
    pp->pa0 = pp->pa; pp->pb0 = pp->pb; pp->pd0[0] = pp->pd[0]; pp->pd0[1] = pp->pd[1]; pp->d_ramp = 0;
}
static uint32_t lrry_pl_nr[2];                          /* the voices each buffer holds */
static uint32_t lrry_pl_cur;
static uint32_t lrry_render_stage(uint32_t ti, int32_t *out, int32_t *outR, uint32_t n, uint32_t nr)
{
    int32_t *cl = lrry_pl[lrry_pl_cur][0], *cr = lrry_pl[lrry_pl_cur][1], *pl = lrry_pl[lrry_pl_cur ^ 1u][0], *pr = lrry_pl[lrry_pl_cur ^ 1u][1];
    uint32_t open, c1 = 0, c1pre = 0, prep = 0, i, ret;
#ifdef LRRY_MASK_HOOK
    LRRY_MASK_HOOK(ti, &lrj.mask);                      /* (a test may silence voices in chosen blocks; not defined in the firmware) */
#endif
#ifdef LRRY_STAGE_HOOK
    LRRY_STAGE_HOOK(0);                                 /* (a test acts here as the main loop could between two blocks: not defined in the firmware; the same at 1 and 2) */
#endif
    if (!nr && !lrj.mask && !lrry_pl_nr[lrry_pl_cur ^ 1u] && !tcr_open_set() && !tcr_pending()) {   /* idle: nothing now, nothing a block ago, no job */
        lrry_pl_nr[lrry_pl_cur] = 0;
        lrry_pl_cur ^= 1u;
        return 0;
    }
    STG(1);
    if (lrj.mask && tcr_go_now()) {                     /* the choice and the block copy first: core 1 must find the next job ready the moment it is free */
        uint16_t w[NVOICE];
        lrry_weights(&lrry_pp[ti], &lrj, w);
        c1pre = lrry_pick(lrj.mask, w);
#ifdef LRRY_SPLIT_PROBE
        LRRY_SPLIT_PROBE(ti, lrj.mask, w, c1pre);
#endif
        if (c1pre)
            prep = tcr_pipe_fill(ti, c1pre);
    }
    STG(2);
    tcr_pipe_collect(ti, pl, pr, n);                    /* block N-1's job: wait for core 1 up to the deadline, take it back, or play it here (from its own block copy) */
    STG(3);
#ifdef LRRY_STAGE_HOOK
    LRRY_STAGE_HOOK(1);
#endif
    open = tcr_open_set();
    if (lrry_pend[ti] & ~open)
        for (i = 0; i < NVOICE; i++)
            if (((lrry_pend[ti] & ~open) >> i) & 1u) {
                lrry_pend[ti] &= (vmask_t)~(1u << i);
                lrry_state_clear(&lrry_vs[ti][i], lrry_pend_note[ti][i]);
            }
    if (prep && tcr_go_post() && !open) {
        c1 = c1pre;
        tcr_pipe_post(n, c1);
    }
#ifdef LRRY_STAGE_HOOK
    LRRY_STAGE_HOOK(2);
#endif
    for (i = 0; i < CTL; i++)
        cl[i] = cr[i] = 0;
    lrry_render_set(ti, ~(c1 | open), cl, cr, n);       /* core 0's share of block N: every voice that sounds, but core 1's and the ones of a job still open */
    STG(4);
    for (i = 0; i < n; i++) {                           /* block N-1's sum is the part's voice sum now */
        out[i] = pl[i];
        outR[i] = pr[i];
    }
    ret = lrry_pl_nr[lrry_pl_cur ^ 1u];
    lrry_pl_nr[lrry_pl_cur] = nr;
    lrry_pl_cur ^= 1u;
    return ret;
}
#else
static void lrry_render_stage(uint32_t ti, int32_t *out, int32_t *outR, uint32_t n)
{
#ifdef LRRY_MASK_HOOK
    LRRY_MASK_HOOK(ti, &lrj.mask);                      /* (a test may silence voices in chosen blocks; not defined in the firmware) */
#endif
    STG(1);
#if LRRY_TWOCORE
    uint32_t open = tcr_open_set(), c1 = 0, i;
    if (lrry_pend[ti] & ~open)
        for (i = 0; i < NVOICE; i++)
            if (((lrry_pend[ti] & ~open) >> i) & 1u) {
                lrry_pend[ti] &= (vmask_t)~(1u << i);
                lrry_state_clear(&lrry_vs[ti][i], lrry_pend_note[ti][i]);
            }
    if (tcr_go_now()) {
        uint16_t w[NVOICE];
        lrry_weights(&lrry_pp[ti], &lrj, w);
        c1 = lrry_pick(lrj.mask, w);
#ifdef LRRY_SPLIT_PROBE
        LRRY_SPLIT_PROBE(ti, lrj.mask, w, c1);
#endif
        if (c1 && !tcr_voices_post(ti, n, c1))
            c1 = 0;                                     /* (the half is too far gone: core 0 plays everything) */
    }
    STG(2);
    lrry_render_set(ti, ~(c1 | open), out, outR, n);    /* core 0's: every voice that sounds, but core 1's and the ones of a job still open */
    STG(3);
    tcr_voices_collect(ti, out, outR, n);               /* core 1's sums added, or core 0 plays them */
    STG(4);
#else
#ifdef LRRY_SPLIT_PROBE
    {
        uint16_t w[NVOICE];
        uint32_t c1;
        lrry_weights(&lrry_pp[ti], &lrj, w);
        c1 = lrry_pick(lrj.mask, w);
        LRRY_SPLIT_PROBE(ti, lrj.mask, w, c1);
    }
#endif
    lrry_render_set(ti, ~0u, out, outR, n);
#endif
}
#endif
#endif
/* render one block of a part into out (cleared here); returns the voices rendered */
#if LRRY_ENGINE
static uint8_t lrry_ran[NLIVE];                         /* phase 56: the part's voices rendered in the block before (its stage pans ramp from that block's; when nothing sounded they start where they are) */
#endif
/* Channel bend is live performance state, outside projects/presets. Q8 semitones. */
static int32_t midi_bend_q8[NTRK], midi_bend_target[NTRK];
static uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
#if LRRY_ENGINE
    const engine_t *e = &ENG_ANALOG;                    /* L/RRY: no engine to choose; the neutral one for the code below that still reads engine flags (no own envelope, no block hook) */
#else
    const engine_t *e = ENGINES[t->engine];
#endif
    const int16_t *p = t->p;
    uint32_t i;
    const mod_win_t *mod = &MODW(t);
    uint32_t msl = MXF(t).on && MXF(t).w[MX_SLOPE] ? MXF(t).w[MX_SLOPE] | (MXF(t).b[MX_SLOPE] == 2u ? 0x8000u : 0u) : 0u;   /* the filter's SLOPE cross-fade (uf_run) */
    const part_scr_t *scr = &pscr[PART_OF(t)];          /* (this part's scratch) */
    int32_t *outR = (int32_t *)scr->bufR;
    uint32_t lrry_dec = 0;
    int32_t lfo = mulq15(t->lfo_val, t->lfo_fade);
    /* L/RRY (SPEC 2.4, the classic arrangement): ENV 2 DEST / LFO 2 DEST, their own depths: either set, that one runs
     * (as when the matrix reads it); all 0, neither costs anything (every sound of before) */
    uint32_t e2on = (mod->on && mod->e2) || p[P_E2FLT] || (p[P_TWTYPE] && XV(t, X_ENV2));   /* (TWIN FILTER: filter 2's ENV 2 amount) */
    uint32_t l2on = (mod->on && mod->l2) || (p[P_TWTYPE] && XV(t, X_LFO2));   /* (L/RRY 1.1: LFO 2 DEST's old PITCH / MULTI CUTOFF / TIMBRE cards no longer act: they reach the engine through the matrix) */   /* (.. and its LFO 2 amount) */
    int32_t tune, tune_fine;                           /* MASTER TUNE (tune_get above): whole 1/16 semitones in the pitch, the rest as a fine factor (no dead zone) */
    int32_t bend, bend16, bend_fine;
    uint32_t ti = (uint32_t)(t - trk);
    tune_get(&tune, &tune_fine);
    if (ti >= NLIVE)                                   /* L/RRY: a hidden part never renders (no engine memory) */
        return 0;
#if LRRY_ENGINE
    uint32_t lrry_ran0 = lrry_ran[ti];
    lrry_ran[ti] = 0;
#endif
    {
        int32_t d = midi_bend_target[ti] - midi_bend_q8[ti];
        /* ~6 ms smoothing, with an exact landing (no permanent small offset). */
        midi_bend_q8[ti] += d > 0 ? (d < 4 ? d : (d + 3) / 4) : (d > -4 ? d : (d - 3) / 4);
    }
    bend = midi_bend_q8[ti];
    bend16 = bend >= 0 ? bend / 16 : -((-bend + 15) / 16);
    bend_fine = (bend - bend16 * 16) * 2367 / 2560;
    STG(12);                                            /* (bench: before the part's control: morph_begin, part_voice's start) */
    uint32_t nr = 0, fade = t->xf_on && t->xf, abits = atgt_bits(t), uf = e->ufilt;
    int32_t ucut0 = uf ? clamp(p[P_FREEA], 0, UF_STEPS) << 8 : 0;   /* the multimode filter's dial (Q8) */
    int16_t pe_new[8];
    for (i = 0; i < n; i++)
        out[i] = outR[i] = 0;
    lrj.mask = 0;
    if (mod->on && mod->ldep < 32767)                     /* the matrix's DEPTH (mod.c) */
        lfo = mulq15(lfo, mod->ldep);
    lfo = mx_slew(t, NMSLOT, lfo);                      /* (hunt1: LFO 1's value for its own depths (VIB, CUT, SHP, AMP) moves at most MX_SLEW a block like a matrix source's: a saw's or a square's edge, an S&H step or
                                                         * a restart of the LFO glides over 5 to 10 blocks (3.6 to 7.3 ms) instead of stepping the pitch / cutoff / shape at one block boundary; an LFO that moves less passes unchanged) */
    if (fade)                                           /* engine switch: the old engine, its own values */
        for (i = 0; i < 8u; i++) {
            pe_new[i] = t->p[P_E0 + i];
            t->p[P_E0 + i] = t->pe_old[i];
        }
    track_lfo_tick(t);
    if (l2on)                                           /* L/RRY: LFO 2, read by the matrix or LFO 2 DEST */
        lfo_tick(t, &t->lfo2, P_L2RATE, P_L2SYNC, 0);
    esync_coef(t);                                      /* this block's envelope steps for env_tick (ENV SYNC) */
    if (e2on)                                           /* L/RRY: ENV 2, read by the matrix or ENV 2 DEST */
        esync2_coef(t);
    if (e->block)                                       /* the engine's per-part work (WHEEL: bars, rotor) */
        e->block(t);
    STG(13);                                            /* (bench: the part's LFOs, envelope steps, the engine's block work) */
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        int32_t env, pitch;
        if (!v->active)
            continue;
        {
            env = env_tick(t, v);
            if (e->ownenv && v->active) {               /* the engine's envelopes (FM6): they end the voice */
                if (e->done(t, v)) {
                    v->active = v->gate = 0;
                    v->stage = 0;
                    v->env = v->env_out = 0;
                    continue;
                }
                v->env = 1 << 24;                       /* (the ADSR's release never ends it) */
                env = 32767;
            }
            if (e2on)                                   /* L/RRY: ENV 2 (read by the matrix or ENV 2 DEST) */
                env2_tick(t, v);
            if (t->cy)                                  /* CYCLE (cycle.c), read by an active matrix slot: ticked here, not in mod_voice (hot RAM) */
                cycle_tick(t, v);
            if (e->amp)                                 /* the engine's own amplitude curve */
                env = e->amp(t, v, env);
            m.envq15 = e->ownenv ? 0 : env;
            m.amp1 = e->ownenv ? env : mulq15(env, v->vel * 258);
            if (v->xg)                                  /* LAYER XFADE (core.h mix_xf): the note's share between the parts */
                m.amp1 = mulq15(m.amp1, v->xg);
            if (p[P_LD_AMP] && !BYP(t, BY_LFO1))
                m.amp1 = mulq15(m.amp1, 32767 - mulq15(p[P_LPOL] ? lfo : (lfo + 32768) >> 1, p[P_LD_AMP] * 258));
            if (fade)                                   /* linear to 0 over the fade */
                m.amp1 = m.amp1 * (int32_t)(t->xf - 1u) / (int32_t)XF_BLOCKS;
            m.amp0 = v->env_out;
            v->env_out = m.amp1;
        }
        if (v->pitch_cur != v->pitch16) {               /* glide */
            int32_t d = v->pitch16 - v->pitch_cur;
            int32_t st = v->gstep ? v->gstep : (int32_t)(ENV_LIN[(p[P_GLIDE] ? p[P_GLIDE] : 40) & 127] >> 14);   /* slide: ~45 ms */
            if (st < 1)
                st = 1;
            v->pitch_cur += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
        }
        if (!env && !m.amp0 && v->stage == 2 && !e->sampled) {
            v->lastc = v->lastd = 0;
            continue;
        }                                   /* held at a silent sustain (SUS 0): nothing to render */
        pitch = v->pitch_cur + tune + bend16 + ((lfo * DEP_AMT(p[P_LD_PIT]) * 3) >> 15) + ((m.envq15 * DEP_AMT(p[P_ED_PIT]) * 3) >> 15);
        m.pitch16 = clamp(pitch, 0, 2047);
        m.inc = pitch_inc(m.pitch16);
        m.fine = v->fine + tune_fine + bend_fine;
        if (v->fine + tune_fine + bend_fine)             /* residual below 1/16 semitone */
            m.inc += (uint32_t)((int32_t)(m.inc >> 12) * (v->fine + tune_fine + bend_fine));
        m.cutoff = ((lfo * DEP_AMT(p[P_LD_FLT])) >> 7) +
                   (((E2Q(t, v)) * DEP_AMT(p[P_E2FLT])) >> 7); /* (L/RRY 1.1: AMP ENV's and LFO 2's cutoff depths no longer act; FILTER ENV's MULTI CUTOFF and LFO 1's stay) */
        if (e == &ENG_NOISE)                            /* NOISE's DRFT wander of the cutoff, worked out by the block before (eng_noise.c): filter 1's, one block late */
            m.cutoff += v->s[0];
        m.acc1 = 0;
        if (v->vel > 110 && (uf != 1u || ACCENT_BOTH(e)))   /* accent opens the filter with the env: filter 1 on the three engines whose own filter went (ACCENT_BOTH), the engine's own tone control on the others (below) */
        {
            int32_t al = accent_lift(v->vel, m.envq15);
            if (ACCENT_BOTH(e))
                al = m.acc1 = accent_dir(al, accent_dir_mode(p[P_FREEC]));   /* filter 1 opens the way its MODE opens (LP up, HP down, BP none) */
            m.cutoff += al;
        }
        m.shape = (64 << 8) + ((lfo * DEP_AMT(p[P_LD_SHP])) >> 7) + ((m.envq15 * DEP_AMT(p[P_ED_SHP])) >> 7);   /* (L/RRY 1.1: FILTER ENV's and LFO 2's TIMBRE depths no longer act) */
        m.amask = abits;                                /* ARP TARGET (SPEC 2.3): 0, every unit on the root */
        if (abits)
            atg_voice(t, v, &m);
        STG(17);                                        /* (bench: a voice's envelope, glide, pitch, cutoff sums) */
        if (mod->on) {                                   /* the modulation matrix (mod.c) */
            mod_voice(t, v, &m, v->fine + tune_fine + bend_fine);
            if (t->cy)                                   /* its second list (cloud job B, mod.c): CYCLE's per-note routes, after mod_voice .. */
                mod_cycle_voice(t, v, &m, v->fine + tune_fine + bend_fine);
            t->venv = m.envq15;                          /* .. and this voice's ENV for filter 2's sum (mod_f2_voice, called by the chain) */
        }
        STG(15);                                        /* (bench: the matrix for a voice) */
        if (e == &ENG_WAVE && p[P_E6])                  /* WAVE STEPS (eng_wave.c): POS lands on one of STEPS places; the block ramp is the cross-fade between two steps */
            wave_steps(t, v, &m);
        m.ucut = m.ucn = UF_TOP;                                /* filter 1's effective cutoff (core.h vmod_t): set below / by chain_uc where the engine has the multimode filter */
#if LRRY_ENGINE
        {   /* the L/RRY stereo voice: its block decoded once, then each voice adds its pair to out (left) and outR (right) */
            lrry_vc_t lc;
            if (!lrry_dec) {
                lrry_pp_t *pp = &lrry_pp[ti];
                /* the patch decode is a pure function of t->p (every value it reads, the extension slots included) and of the stage pans of the block before, which it keeps as the ramps' start: when t->p is
                 * the same as at the last decode the result is the same, and the ramps' start is the pan it already holds (pa0 = pa ..): the decode is skipped (same bits, ~29 us a block on the unit) */
                if (!LRRY_NO_DECODE_CACHE && lrry_key_ok[ti] && !pp->pan_moving && lrry_same(t->p, lrry_key[ti], EXT_P_LEN)) {   /* (a pan still sliding to its target: decode again) */
#if LRRY_PIPELINE
                    lrry_pp_ramps(pp, ti);
#else
                    pp->pa0 = pp->pa; pp->pb0 = pp->pb; pp->pd0[0] = pp->pd[0]; pp->pd0[1] = pp->pd[1]; pp->d_ramp = 0;
#endif
                } else {
#if LRRY_PIPELINE
                    lrry_pp_ver[ti]++;
#endif
                    lrry_decode(t, pp);
                    lrry_copy(lrry_key[ti], t->p, EXT_P_LEN);
                    lrry_key_ok[ti] = 1;
                }
                if (!lrry_ran0) {                       /* (phase 56: nothing sounded in the block before: no stage pan to ramp from) */
#if LRRY_PIPELINE
                    lrry_pp_ramps(pp, ti);
#else
                    pp->pa0 = pp->pa; pp->pb0 = pp->pb; pp->pd0[0] = pp->pd[0]; pp->pd0[1] = pp->pd[1]; pp->d_ramp = 0;
#endif
                }
                lrry_ran[ti] = 1;
                lrry_dec = 1;
                STG(14);                                /* (bench: the patch decode, once a block) */
            }
            lrry_voice_ctl(t, v, &m, &lrry_pp[ti], clamp(p[P_FREEA], 0, UF_STEPS) << 8, ti, i, &lc);
            STG(16);                                    /* (bench: a voice's control for the kernel) */
            lrj.vc[i] = lc;                             /* 4.3b: the audio work comes after the loop (lrry_render_stage), on one core or two */
            lrj.m[i] = m;
            lrj.mask |= 1u << i;
            nr++;
            continue;
        }
#endif
        int32_t *dst = out, b0 = dst[0], bn = dst[n - 1], bm = dst[n - 2];   /* (hunt3: the voice's own contribution is what it adds to dst) */
#define VSTEP() do { if (v->offp | v->offn) voice_step(v, dst, b0, n); v->lastc = dst[n - 1] - bn; v->lastd = v->lastc - (dst[n - 2] - bm); } while (0)
        {
            uint32_t on = chain_on(t, v) | chain_f1_on(e, t, v, &m, ucut0);   /* the voice chain: TWIN / SUB on, or (CHAIN_ALL, voice_chain.h) filter 1 runs */
            if (on | (v->chn == VCH_RUN)) {      /* the voice chain (cloud job B, voice_chain.c): engine, SUB, filter 1, filter 2, then the amp; a voice entering or leaving it is cross-faded over a block */
                chain_block(e, t, v, dst, n, &m, ucut0, msl, on);
                VSTEP();                                /* (hunt3 + cloud: the re-strike step cancellation sees the chain's output too) */
                nr++;
                continue;
            }
        }
        v->chn = VCH_OFF;
        if (uf) {                                       /* the multimode filter: the cutoff routes are its own */
            v->msl = (uint16_t)msl;
            int32_t kt = p[P_KTRK] ? UF_KT(p[P_KTRK], v->pitch_cur) : 0;   /* KEY TRK (0.2.21): the note's pitch around C3, an octave of cutoff per octave at +100 %; 0: nothing added, nothing computed */
            int32_t uc = clamp(ucut0 + m.cutoff + kt, 0, UF_TOP);   /* the effective cut: the dial plus every route (and the key) */
            int32_t ucn = clamp(ucut0 + m.cutoff - m.acc1 + kt, 0, UF_TOP);   /* the same without the accent (filter 2 LINK, NOISE's make-up) */
            m.cutoff = accent_lift(v->vel, m.envq15);   /* the routes are the filter's, whatever CUT is; the accent's lift stays the engine's own (its tone control, as ever) */
            m.ucut = uc;
            m.ucn = ucn;
            if ((uc < UF_TOP || p[P_FREEC] > 0) && !BYP(t, BY_MULTI)) {   /* (the bypass: MODE LP, the effective cut at the open end; L/RRY 1.1: or the BYPASS switch) */
                if (v->uft == UF_BYP) {                 /* (0.2.25: out of the bypass while sounding: from rest, faded in over the block) */
                    v->uft = 0;
                    uf_edge(e, t, v, dst, n, &m, uc, 1);
                } else {
                    uf_voice(e, t, v, dst, n, &m, uc);
                }
                VSTEP();
                nr++;
                continue;
            }
            if (v->uft && v->uft != UF_BYP) {           /* (0.2.25: into the bypass while sounding: the filter's last block fades to dry) */
                uf_edge(e, t, v, dst, n, &m, UF_TOP, 0);
                v->uft = UF_BYP;
                VSTEP();
                nr++;
                continue;
            }
            v->uft = UF_BYP;                            /* bypassed while it sounds (the filter from rest, faded in, if it comes back) */
        }
        e->render(t, v, dst, n, &m);                    /* (SPREAD: the left ones) */
        VSTEP();
        nr++;
    }
#if LRRY_ENGINE
#if LRRY_PIPELINE
    nr = lrry_render_stage(ti, out, outR, n, nr);       /* (every block: the sum of the block before is handed on, and the job of the block before closed, whether or not a voice sounds now) */
#else
    if (lrj.mask)
        lrry_render_stage(ti, out, outR, n);
#endif
#endif
    if (fade) {
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = pe_new[i];
        t->xf--;
    }
    return nr;
}
