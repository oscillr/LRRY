/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 OSCILL/R (L/RRY). Part of L/RRY, a modified version of Felucca. */
/* L/RRY TWO-CORE, split BY VOICE (L/RRY phase 4.3b; LRRY_TWOCORE=1 only). Was: core 1 plays part 2 (L/RRY's split by part). Now there is ONE part and the voices are split:
 * core 0 plays voices 0..3 and everything else (control of all eight voices, the part's mix, gather, sends, master); core 1 plays core 1's voices, one job a block. The proven model is
 * host/lrry_split.h; this file keeps L/RRY's protocol (twocore.h: the words, the sequence numbers, the check value, core 1 answers every job exactly once) and its runner (deadline, the three
 * cases when core 1 is not done, the wait limit, pausing, the fallback to one core) and changes WHAT a job is.
 *
 * THE JOB (tc_job_run, core 1, called by tc_c1_step after its claim): lrry_voice for each of core 1's voices whose bit is set in tc_ctx.mask, into core 1's OWN stereo accumulator (tc_acc, int32 L / R).
 * Everything core 1 reads is the block copy in tc_ctx (the decoded patch lrry_pp_t, the four voices' lrry_vc_t and vmod_t), written by core 0 before tc_post; a 16-bit hash of the whole
 * copy is in arg with n; a copy that does not fit its hash (read wrong) runs nothing and is refused like a wrong check value. Everything core 1 writes is tc_acc, the audio state of its own
 * voices (lrry_vs[0][4..7]) and their COMB lines (engine memory slices of core 1's voices). It reads and writes no track_t, no matrix or LFO state and no per-voice control state: the control work of
 * all eight voices is done by core 0 in track_render before the job is posted (it writes track_t, so it cannot be split).
 *
 * THE RUNNER (core 0): tcr_begin (block start: settle the old job, pause / resume, trim, decide whether a job may be posted), then inside track_render's lrry_render_stage: tcr_voices_post
 * (the deadline, copy, tc_post), core 0 renders voices 0..3, tcr_voices_collect (wait for done until the DEADLINE; not done: take it back, and by core 1's answer
 *   yld      core 0 plays core 1's voices itself, no click                           "late"       (a run of TCR_LATE_RUN late blocks pauses the jobs)
 *   claim    wait for done until the WAIT LIMIT tcr.lim: the latest moment at which core 1's done word still leaves core 0 the rest of this block and every later block of the half at the whole one-core cost
 *     done   fine, tc_acc is added                                                "very late"  (never a pause by itself, counted as very late without a pause, tcs.vlate_ok)
 *     not    core 1's voices silent this block (core 0 touches nothing of them: tcr.skip2); the jobs pause (a fault); they stay silent until the open job is done
 *   refused  a strike (BADJOB), core 0 plays core 1's voices
 *   nothing  (no answer within TCR_ANSWER_US) core 1's voices silent, the jobs pause; tc_watch stops core 1 after TC_HANG_MS without a beat)
 * then tcr_end (the block counter). The sum is integer addition: the output with the job done in time, taken back, waited for, or never posted is the same, bit for bit. Only the silent
 * blocks (a fault) differ from the one-core output, and they are counted.
 * A real fault (refused job, a word ahead, a refused copy): tc_fail, a strike, core 0 plays core 1's voices from then on; if core 1 had claimed the open job the voices fade out first (tcr.fade2: voice_kill at
 * the next block's start) because their audio state may be half written. Lateness is never a strike.
 * A voice's audio state is cleared at its note-on (lrry_state_new). While a job is open that clear waits (voice.c lrry_pend), so core 0 never writes core 1's voices's state while core 1 may.
 * No job is posted in a block where core 1's voices have nothing to render (silence, up to four notes).
 * Pausing: core 1 stays alive in its loop (a stopped core 1 cannot be started again before the next power-on); the jobs resume after TCR_CALM_MS. */

/* ---------------------------------------------------------------- the constants --- */
#define TCR_FRAME_TICKS 544u            /* TIMER4 ticks per I2S frame (audio.c DAC_TICKS: 24 MHz / 44,117.6 Hz) */
#define TCR_BLOCK_TICKS (CTL * TCR_FRAME_TICKS)                  /* one block: 17,408 ticks (0.725 ms) */
#define TCR_HALF_TICKS (HALF_FRAMES * TCR_FRAME_TICKS)           /* one half buffer: 69,632 ticks (2.9 ms), the time the ISR has to render it */
#define TCR_BLOCKS (HALF_FRAMES / CTL)                           /* blocks in a half: 4 */
#define TCR_SLACK_TICKS (TCR_HALF_TICKS / 20u)                   /* kept back from the deadline for what the cost table does not hold (events, note starts, the ISR's entry): 5 % of a half, 3,481 ticks */
#define TCR_NEED_MAX 200000u            /* the cost sums (1/100 %) are clamped to this: a tick count of it fits 32 bits */
#define TCR_ANSWER_US 100u              /* after the take, core 1 answers a job within this (an idle core 1 does so in microseconds); no answer: it is stuck */
#define TCR_ANSWER_TICKS (TCR_ANSWER_US * TC_TICKS_PER_US)
#define TCR_LATE_RUN 4u                 /* this many late blocks in a row (one half's worth) pause the jobs */
#define TCR_VLATE_RUN 0u                /* BENCHT4: 0 = OFF. A very late block whose job finished inside its wait limit never pauses, however many in a row (BENCHT3: 4; BENCHT2: 1). `bench vrun N` switches the run rule on for a diagnosis */
#define TCR_VWIN_K 0u                   /* BENCHT4: 0 = OFF. The window rule: this many very late blocks (not in a row) within TCR_VWIN_MS pause the jobs (BENCHT3: 8). `bench vwin K [MS]` switches it on */
#define TCR_VWIN_MS 2000u
#define TCR_VWIN_TICKS (TCR_VWIN_MS * TC_TICKS_PER_US * 1000u)
#define TCR_VWIN_MAX 16u                /* the ring of times for the window: K can be set up to this */
#define TCR_FLOOR_BLOCKS 128u           /* the deadline's floor: the run times of core 1 are kept in two buckets of this many blocks, so the maximum covers the last 128 to 256 blocks */
#define TCR_CALM_MS 10000u              /* a pause lasts this long, then the jobs resume */
#define TCR_CALM_TICKS (TCR_CALM_MS * 1000u * TC_TICKS_PER_US)   /* 240 million ticks: fits 32 bits (the timer wraps every 179 s) */

/* BENCHT2 (TC_MEAS: developer bench builds only): the margin settings can be changed at run time by the bench console (bench slack / calm / pause / lrun /
 * vrun / vwin / floor, bench.c), so one build compares settings. Without TC_MEAS they are the constants above and the compiler sees no variable.
 * BENCHT3: vwin = K very late blocks within vwin_ticks pause the jobs (K 0: off); floor 1 = the deadline has the floor of core 1's measured run time (needs the
 * measuring, so a build without TC_MEAS has no floor).
 * BENCHT4: the defaults are vrun 0 (off), vwin 0 (off), floor 0 (off): only a real fault, a job not finished inside its wait limit (tcr.lim) or a run of late blocks pauses. */
#if TC_MEAS
typedef struct { uint32_t slack, calm_ticks, lrun, vrun, vwin_k, vwin_ticks, floor, nocore1; } tcm_cfg_t;
static tcm_cfg_t tcm_cfg = { TCR_SLACK_TICKS, TCR_CALM_TICKS, TCR_LATE_RUN, TCR_VLATE_RUN, TCR_VWIN_K, TCR_VWIN_TICKS, 0u, 0u };
#define TCR_SLACK_NOW tcm_cfg.slack
#define TCR_CALM_NOW tcm_cfg.calm_ticks
#define TCR_LATE_NOW tcm_cfg.lrun
#define TCR_VLATE_NOW tcm_cfg.vrun
#define TCR_VWINK_NOW tcm_cfg.vwin_k
#define TCR_VWINT_NOW tcm_cfg.vwin_ticks
#define TCR_FLOOR_NOW tcm_cfg.floor
#define TCR_NOCORE1_NOW tcm_cfg.nocore1                 /* HANDOFF_V2 item 112 (bench word `core1 off`): 1 = core 1 is treated as down, with no power-cycle: core 0 plays part 2, the budget has the one-core numbers */
#define TCR_OR_NOCORE1 || tcm_cfg.nocore1               /* (the extra term of the conditions, written as tokens so that outside the bench the source text is exactly the old one) */
#define TCR_AND_NOT_NOCORE1 && !tcm_cfg.nocore1
#else
#define TCR_SLACK_NOW TCR_SLACK_TICKS
#define TCR_CALM_NOW TCR_CALM_TICKS
#define TCR_LATE_NOW TCR_LATE_RUN
#define TCR_VLATE_NOW TCR_VLATE_RUN
#define TCR_VWINK_NOW TCR_VWIN_K
#define TCR_VWINT_NOW TCR_VWIN_TICKS
#define TCR_FLOOR_NOW 0u
#define TCR_OR_NOCORE1                                  /* (no such switch outside the developer bench: these expand to nothing, the normal build is byte for byte the old one) */
#define TCR_AND_NOT_NOCORE1
#endif
_Static_assert(TCR_VWIN_K <= TCR_VWIN_MAX && (TCR_VWIN_MAX & (TCR_VWIN_MAX - 1u)) == 0, "twocore_run.c: the window's ring");

_Static_assert(HALF_FRAMES % CTL == 0 && TCR_BLOCKS >= 1, "twocore_run.c: a half is whole blocks");
_Static_assert(NLIVE == 1 && (NVOICE == 8 || NVOICE == 12 || NVOICE == 16) && LRRY_C1_MAX <= 8u && LRRY_C1_MAX <= NVOICE, "twocore_run.c: one part, 8 voices (12 in the bench build), a job carries at most LRRY_C1_MAX of them");

/* the budget's cost table (budget.c, included after fx.c; budget.h declares the functions): the deadline reads part 2's cost at its current settings */
#include "budget.h"

/* ---------------------------------------------------------------- the counters (the developer page shows these) --- */
typedef struct {
    uint32_t blocks;            /* blocks through the runner */
    uint32_t in_time;           /* core 1 was done by the deadline */
    uint32_t late;              /* core 1 yielded (had not started): core 0 played part 2, no click */
    uint32_t vlate;             /* core 1 had claimed and was not done by the deadline (done within the half, or not: silent) */
    uint32_t silent;            /* blocks whose part 2 was silent: the very late ones that did not finish, no answer, and the blocks waiting for an open job */
    uint32_t wait;              /* of those: no new job, core 1 still held the old one */
    uint32_t own;               /* core 0 played part 2 with no job (core 1 down, paused, parked, the half too far gone) */
    uint32_t refused;           /* a refused job or snapshot, or a word ahead: a strike, core 0 played part 2 */
    uint32_t noanswer;          /* no answer to the take within TCR_ANSWER_US */
    uint32_t pause_g21;         /* (G21, 2026-11-11) late blocks that rested core 1 for 10 s before and now do not (counted only) */
    uint32_t pauses, resumes;   /* the jobs paused (any reason below), and resumed after the calm period */
    uint32_t trims;             /* notes faded at once because the jobs paused or core 1 stopped (voice_trim_now) */
    uint32_t vlate_ok;          /* BENCHT3: very late blocks whose job finished inside its half and that did NOT cause a pause (they are counted in vlate too) */
    uint32_t pause_fault, pause_run, pause_win, pause_late;   /* BENCHT3: pauses by reason: a real fault (wrong answer, refused, no answer, silent block, a job not finished in its half) /
                                 * a run of very late blocks / K very late blocks within the window / a run of late blocks; they add up to pauses */
} tc_stat_t;                    /* (the flash parks are tc.parks) */
static tc_stat_t tcs TC_BSS;

typedef struct {
    uint32_t t0;                /* the half's start (audio.c: the tick at the ISR's entry) */
    uint32_t dl;                /* this block's deadline */
    uint32_t lim;               /* BENCHT4: the wait limit of this block's job: the latest moment core 1's done word still leaves core 0 its own half (see tcr_deadline); core 0 never waits past it */
    uint32_t job;               /* the job posted this block (0: none) */
    uint32_t open;              /* the last job posted that is not known to be closed (core 1 may still write part 2) */
    uint32_t pause_t;           /* when the jobs paused */
    uint32_t c1f, c1v, g;       /* the cost table's items (1/100 %): the part's fixed and a voice, the globals; read at a half's first block */
    uint32_t t1f, t1v, tg;      /* L/RRY 1.1 item 142: the same items at their true cost (ceil(x * 100 / BUDGET_DL_MARGIN), as tcr_deadline_true), for the wait limit */
    uint8_t blk;                /* the block in the half */
    uint8_t t0_ok, cost_ok;
    uint8_t skip2;              /* core 1's voices are silent this block and core 1 may still write tc_acc: nothing of it is added */
    uint8_t go;                 /* a job may be posted this block (tcr_begin: core 1 up, not parked, not paused, no job open) */
    uint8_t a_lo, a_hi;         /* voices rendering this block on core 0 and on core 1 */
    vmask_t c1set;              /* the voices of the job posted this block (0: none) */
    vmask_t open_set;           /* the voices of the job that is open (core 1 may still write their audio state) */
    vmask_t fade_mask;          /* the voices to fade out at the next block's start (core 1 stopped in their job) */
    vmask_t sil;                /* the voices silent this block (tcr.skip2) */
    uint8_t got;                /* tcr_voices_collect ran this block (the block is counted) */
    uint8_t paused;
    uint16_t late_run, vlate_run;   /* late / very late blocks in a row (the runtime settings go up to 1000) */
    uint8_t vw_i, vw_n;         /* BENCHT3: the ring of the times of the last very late blocks (the window rule): the next slot, and how many are valid */
    uint32_t vw_t[TCR_VWIN_MAX];
    uint8_t fade2;              /* part 2 may be half written (core 1 stopped in a claimed job): fade its voices out before core 0 plays it */
    uint8_t trim;               /* bit 0: notes above the part's number to fade at once (tc_trim_poll asks, the next block does it) */
    uint8_t trim_on;            /* the poll saw the condition (so it asks once) */
    uint8_t two;                /* L/RRY 1.1 item 90: the two-core NOTES rule is in force (read at a half's first block with the costs): the later blocks count core 0's own share only */
    uint8_t nott;               /* item 90: this block's job may not be taken back (the take-over no longer fits the half): core 1 finishes it, or part 2 is silent */
#if LRRY_PIPELINE
    uint8_t pbuf;               /* PIPELINE: the block copy (tc_ctxs index) of the job posted last block, the one not closed yet */
    uint32_t pj;                /* PIPELINE: the job posted last block (0: none), closed by this block's tcr_pipe_collect */
    vmask_t pset;               /* .. and its voices */
    uint32_t cj;                /* PIPELINE: the job that tcr_pipe_collect closed in this block (0: none; the tests read where core 0 left its wait) */
    vmask_t psil;               /* PIPELINE: the voices of that job that were silent in the block BEFORE (not done by the wait limit); sil is this block's */
    volatile uint32_t dl_job;   /* PIPELINE: the job whose deadline dl and wait limit lim are in force (set with them, at the collect; the host tests aim core 1's delays at them) */
    uint32_t fill_arg;          /* PIPELINE: the next job's arg (block length, copy index, hash of the copy), made by tcr_pipe_fill */
    uint32_t fill_dl, fill_lim; /* .. and its deadline and wait limit, worked out for the block that will close it (kept with the job: pdl, plim) */
    uint32_t pdl, plim;
    uint8_t fill_nott, pnott;   /* .. and whether it may not be taken back (no take-over) */
    uint8_t pp_ok[2];           /* PIPELINE: the patch part of block copy [i] holds the patch of version pp_ver[i] (lrry_pp_ver), and the FNV state after its words is pp_hash[i] */
    uint32_t pp_ver[2], pp_hash[2];
    uint32_t pp_copies;         /* PIPELINE: block copies whose patch part was made (the others reused it) */
#endif
#if TC_MEAS
    uint8_t own_why;            /* why this block has no job: TCM_OWN_* (the own counter's split) */
    uint16_t fl_n;              /* BENCHT3, the deadline's floor: blocks in the current bucket */
    uint32_t cap;               /* the latest moment core 0 can still play part 2 itself (this block; the floor's ceiling) */
    uint32_t fl_cur, fl_prev;   /* the largest run time (ticks) core 1 showed in the current and in the last bucket of TCR_FLOOR_BLOCKS blocks */
#endif
} tcr_t;
static tcr_t tcr TC_BSS;

#if TC_MEAS
/* ---------------------------------------------------------------- BENCHT2: what the jobs measure (core 0 collects; developer bench builds only) ---
 * Per job core 1 ran: START DELAY = core 1's clock when it saw the job word less core 0's clock just before it posted; RUN TIME = core 1's clock at the return of the
 * job less its clock at the sight (the claim and the job; the done word is written after); FINISH MARGIN = the block's deadline less the return (negative: after it).
 * All in TIMER4 ticks (24 a us), shown in us. Collected when the job is seen done (in time, or very late but finished) or, for a very late one that finished after
 * its half, when the runner settles it at a later block. A job core 0 took back (late) or that was refused has no figures.
 * Eight bins each, edges in us:  delay <2 <5 <10 <25 <50 <100 <250 >=250;  run <100 <150 <200 <300 <400 <600 <900 >=900;  margin <-500 <-100 <0 <100 <250 <500 <1000 >=1000.
 * Cleared by the audio interrupt itself at the next block after the main loop sets tcm.clr (bench run does), so the figures are those of one test. */
enum { TCM_OWN_DOWN, TCM_OWN_PARK, TCM_OWN_PAUSE, TCM_OWN_DL, TCM_OWN_FL, TCM_OWN_NOV, TCM_OWN_N };   /* no job because core 1 is down / a flash park / paused / the deadline was already past / (BENCHT3) the floor said core 1's measured run time cannot fit before the latest deadline / core 1's voices had nothing to render */
#define TCM_NB 8u
#define TCM_NEV 16u
typedef struct { uint32_t blk, at, a1, a2; int32_t sd, rt, mg; } tcm_ev_t;     /* a very late job: the block in the half, the block count since the test's start, voices rendering (core 0's side, core 1's), ticks */
typedef struct {
    uint32_t clr;                       /* the main loop asks for a clear (written there, taken here) */
    uint32_t b0;                        /* tcs.blocks at the clear */
    uint32_t posted, own[TCM_OWN_N];
    uint32_t n, badclk;                 /* jobs measured; jobs whose clock figures were impossible (not counted in the bins) */
    uint32_t sd_max, rt_max;
    int32_t mg_min;
    uint64_t sd_sum, rt_sum;
    int64_t mg_sum;
    uint32_t hsd[TCM_NB], hrt[TCM_NB], hmg[TCM_NB];
    uint32_t nv, vsd_max, vrt_max, vblk[TCR_BLOCKS];   /* the very late jobs that were measured: their delay, run and the block they were in */
    uint64_t vrt_sum;
    uint64_t st_sum[TCR_BLOCKS], dl_sum[TCR_BLOCKS];   /* per block of the half: where core 1 started the job and where its deadline was, ticks after the half's start, summed */
    uint32_t nb[TCR_BLOCKS], st_max;    /* jobs measured per block of the half; the latest start after the half's start */
    uint32_t hacc, hpk;                 /* core 1's run ticks in this half, and the largest half so far */
    uint32_t ev_n;                      /* very late jobs seen (the ring holds the newest TCM_NEV) */
    tcm_ev_t ev[TCM_NEV];
} tcm_t;
static tcm_t tcm TC_BSS;
static struct { uint32_t t_post, t0, blk, at, a1, a2; uint8_t vl; } tcj TC_BSS;   /* the job in flight (not cleared with the figures) */
static const int32_t TCM_E_SD[TCM_NB - 1u] = { 2 * 24, 5 * 24, 10 * 24, 25 * 24, 50 * 24, 100 * 24, 250 * 24 };
static const int32_t TCM_E_RT[TCM_NB - 1u] = { 100 * 24, 150 * 24, 200 * 24, 300 * 24, 400 * 24, 600 * 24, 900 * 24 };
static const int32_t TCM_E_MG[TCM_NB - 1u] = { -500 * 24, -100 * 24, 0, 100 * 24, 250 * 24, 500 * 24, 1000 * 24 };
static uint32_t tcm_bin(int32_t v, const int32_t *e)
{
    uint32_t i = 0;
    while (i < TCM_NB - 1u && v >= e[i])
        i++;
    return i;
}
static void tcm_half(void)              /* a half begins: its run ticks closed */
{
    if (tcm.hacc > tcm.hpk)
        tcm.hpk = tcm.hacc;
    tcm.hacc = 0;
}
/* job j is closed and core 1 ran it: take its figures (the words core 1 wrote before done) */
static void tcm_collect(uint32_t j)
{
    int32_t sd, rt, mg;
    uint32_t ts, te;
    if (TC_LD(&tc_mb.mj) != j || TC_LD(&tc_mb.bad_job) == j)
        return;
    ts = TC_LD(&tc_mb.t_seen);
    te = TC_LD(&tc_mb.t_end);
    sd = (int32_t)(ts - tcj.t_post);
    rt = (int32_t)(te - ts);
    mg = (int32_t)(tcr.dl - te);
    if (sd < 0 || rt < 0 || sd > (int32_t)(4u * TCR_HALF_TICKS) || rt > (int32_t)(4u * TCR_HALF_TICKS)) {   /* (a clock that does not fit: counted, not binned) */
        tcm.badclk++;
        return;
    }
    tcm.n++;
    {   /* where in the half: the start of this job (core 1's clock) and its deadline, after the half's start */
        uint32_t b = tcj.blk % TCR_BLOCKS, st = ts - tcj.t0;
        tcm.nb[b]++;
        tcm.st_sum[b] += st;
        tcm.dl_sum[b] += tcr.dl - tcj.t0;
        if (st > tcm.st_max && st < 4u * TCR_HALF_TICKS)
            tcm.st_max = st;
    }
    tcm.hsd[tcm_bin(sd, TCM_E_SD)]++;
    tcm.hrt[tcm_bin(rt, TCM_E_RT)]++;
    tcm.hmg[tcm_bin(mg, TCM_E_MG)]++;
    if ((uint32_t)sd > tcm.sd_max)
        tcm.sd_max = (uint32_t)sd;
    if ((uint32_t)rt > tcm.rt_max)
        tcm.rt_max = (uint32_t)rt;
    if (tcm.n == 1u || mg < tcm.mg_min)
        tcm.mg_min = mg;
    tcm.sd_sum += (uint32_t)sd;
    tcm.rt_sum += (uint32_t)rt;
    tcm.mg_sum += mg;
    tcm.hacc += (uint32_t)rt;
    if ((uint32_t)rt > tcr.fl_cur)
        tcr.fl_cur = (uint32_t)rt;                      /* (the deadline's floor: the recent maximum) */
    if (tcj.vl) {
        tcm_ev_t *e = &tcm.ev[tcm.ev_n++ % TCM_NEV];
        tcm.nv++;
        tcm.vblk[tcj.blk % TCR_BLOCKS]++;
        tcm.vrt_sum += (uint32_t)rt;
        if ((uint32_t)rt > tcm.vrt_max)
            tcm.vrt_max = (uint32_t)rt;
        if ((uint32_t)sd > tcm.vsd_max)
            tcm.vsd_max = (uint32_t)sd;
        e->blk = tcj.blk, e->at = tcj.at - tcm.b0, e->a1 = tcj.a1, e->a2 = tcj.a2, e->sd = sd, e->rt = rt, e->mg = mg;
    }
}
static void tcm_clear(void)
{
    uint32_t b0 = tcs.blocks - 1u;                      /* (this block is counted already and is the test's first) */
    memset(&tcm, 0, sizeof tcm);
    tcm.b0 = b0;
    tcr.fl_n = 0, tcr.fl_cur = tcr.fl_prev = 0;        /* (the floor starts again with the test) */
}
#endif

/* ---------------------------------------------------------------- the job (core 1) --- */
#ifndef TC_CTX_SECT
#define TC_CTX_SECT                     /* (the unit: the pool's tail beside tc_mb, hal/fm1_core1.h) */
#endif
typedef struct {
    lrry_pp_t pp;                       /* the patch for this block, copied whole (the model's blockP) */
    uint32_t mask;                      /* the job's voices: bit i = voice i */
    uint32_t n;                         /* how many (<= LRRY_C1_MAX) */
    uint32_t id[LRRY_C1_MAX];           /* their numbers, in order */
    lrry_vc_t vc[LRRY_C1_MAX];          /* the k-th voice: its pitch and cutoffs */
    vmod_t m[LRRY_C1_MAX];              /* .. and L/RRY's amp ramp */
} __attribute__((aligned(64))) tc_ctx_t;   /* (rounded up to whole 64-byte lines: nothing else shares them) */
#if LRRY_PIPELINE
/* PIPELINE: two block copies, used by turns (job number j uses tc_ctxs[j & 1]): core 0 makes the copy for job j + 1 while core 1 may still run job j on the other, and posts it the moment job j is closed */
static tc_ctx_t tc_ctxs[2] TC_CTX_SECT;
#define TC_CTXJ(j) (tc_ctxs[(j) & 1u])
#else
static tc_ctx_t tc_ctx TC_CTX_SECT;     /* the block's copy: core 0 writes it before tc_post, core 1 reads it after its claim, nobody in between */
#define TC_CTXJ(j) (tc_ctx)
#endif
static int32_t tc_acc[2][CTL] __attribute__((aligned(64)));   /* core 1's stereo accumulator (int32 L, R): written by core 1 only, read by core 0 only after done (PIPELINE: added to the block's sum before the next job is posted) */

/* 16 bits of the copy: a misread word changes it (FNV over the words). Only the entries the job uses are read: the fixed head, then n voices' vc and m */
static uint32_t tc_ctx_hash(const tc_ctx_t *c)
{
    const uint32_t *w = (const uint32_t *)c;
    uint32_t i, h = 0x811C9DC5u, n = c->n < LRRY_C1_MAX ? c->n : LRRY_C1_MAX;
    for (i = 0; i < __builtin_offsetof(tc_ctx_t, vc) / 4u; i++)
        h = (h ^ w[i]) * 0x01000193u;
    w = (const uint32_t *)c->vc;
    for (i = 0; i < n * sizeof c->vc[0] / 4u; i++)
        h = (h ^ w[i]) * 0x01000193u;
    w = (const uint32_t *)c->m;
    for (i = 0; i < n * sizeof c->m[0] / 4u; i++)
        h = (h ^ w[i]) * 0x01000193u;
    return (h ^ h >> 16) & 0xFFFFu;
}

/* refused like a wrong check value: core 1's own words (bad_job = this job, bad + 1), nothing run. tc_c1_step still answers done; core 0 reads bad */
static void tc_job_refuse(void)
{
    TC_ST(&tc_mb.bad_job, TC_LD(&tc_mb.job));
    TC_SYNC();
    TC_ST(&tc_mb.bad, TC_LD(&tc_mb.bad) + 1u);
}

/* core 1: core 1's voices for one block into tc_acc. arg: n | hash << 16 */
#if LRRY_PIPELINE
static void tc_job_run(uint32_t arg)                    /* arg: n | copy index << 8 | hash << 16 */
{
    uint32_t n = arg & 0xFFu, i;
    tc_ctx_t *c = &tc_ctxs[(arg >> 8) & 1u];
    TC_PROBE(2);
    if (tc_ctx_hash(c) != arg >> 16 || n != CTL) {
        tc_job_refuse();
        return;
    }
    TC_PROBE(3);
    for (i = 0; i < CTL; i++)
        tc_acc[0][i] = tc_acc[1][i] = 0;
    for (i = 0; i < c->n && i < LRRY_C1_MAX; i++)
        if (c->id[i] < NVOICE) {
#if TC_MEAS
            tc_probe[2] = i;
#endif
            TC_PROBE(4);
            lrry_voice(&c->pp, &c->vc[i], &lrry_vs[0][c->id[i]], &c->m[i], tc_acc[0], tc_acc[1], CTL);
            TC_PROBE(5);
        }
    TC_PROBE(6);
}
#else
static void tc_job_run(uint32_t arg)
{
    uint32_t n = arg & 0xFFFFu, i;
    if (tc_ctx_hash(&tc_ctx) != arg >> 16 || n != CTL) {   /* (a block is always CTL frames; the kernel gets the constant CTL, not n, so the compiler folds the block length) */
        tc_job_refuse();
        return;
    }
    for (i = 0; i < CTL; i++)
        tc_acc[0][i] = tc_acc[1][i] = 0;
    for (i = 0; i < tc_ctx.n && i < LRRY_C1_MAX; i++)
        if (tc_ctx.id[i] < NVOICE)
            lrry_voice(&tc_ctx.pp, &tc_ctx.vc[i], &lrry_vs[0][tc_ctx.id[i]], &tc_ctx.m[i], tc_acc[0], tc_acc[1], CTL);
}
#endif

/* ---------------------------------------------------------------- the deadline (core 0) --- */
/* The latest tick at which core 0 can still play core 1's voices itself and finish the half in time: the half's end, less
 *   their cost at the current settings (the voices rendering there, a voice each),
 *   the rest of this block (gather, sends, master: the globals),
 *   the blocks still to come, each at the whole one-core cost (both parts and the globals: the worst case, core 1 late again),
 *   and TCR_SLACK_TICKS.
 * The costs are the NOTES table of budget.c (1/100 % of a half's time for a block's worth), with a 15 % margin in them (BUDGET_DL_MARGIN: part_dl, global_cost_dl). The table's
 * items are read at a half's first block; the sounding voices at every block (a count over 8 voices each). Core 1 is idle (the last job is closed)
 * when this runs, so reading part 2's settings is safe. It reads neither BUDGET_LIMIT, BUDGET_MARGIN nor BUDGET_HO (L/RRY 1.1 item 132 changed the limit and the hand-over,
 * item 137 the caps' margin 1.15 -> 1.00 and the limit 88 -> 81: the NOTES caps rose, this deadline stayed bit for bit: tests/deadline_test.c). Item 142: the wait limit is
 * the later of the 1.15 one and the true-cost one less TCR_SLACK (below); the deadline and the items are unchanged */
#define TCR_TRUE(x) (((x) * 100u + BUDGET_DL_MARGIN - 1u) / BUDGET_DL_MARGIN)   /* a 1.15 item back at its true cost, rounded up (>= the exact 1.00 item: item 137) */
static uint32_t tcr_deadline(uint32_t now, int *nofit)
{
    uint32_t k = tcr.blk, c1, c2, need, dl, cap, a1 = tcr.a_lo, a2 = tcr.a_hi;
    if (k == 0 || !tcr.cost_ok) {
        part_dl(&trk[0], &tcr.c1f, &tcr.c1v);           /* (item 142: one bd_part a part) */
        tcr.g = global_cost_dl();
        tcr.two = (uint8_t)bd_two_now();
        tcr.t1f = TCR_TRUE(tcr.c1f), tcr.t1v = TCR_TRUE(tcr.c1v), tcr.tg = TCR_TRUE(tcr.g);
        tcr.cost_ok = 1;
    }
#if TC_MEAS
    tcj.a1 = a1, tcj.a2 = a2;
#endif
    c1 = tcr.c1f + a1 * tcr.c1v;                        /* core 0's side: the part's fixed work and its voices 0..3 */
    c2 = a2 * tcr.c1v;                                  /* core 1's voices */
    /* L/RRY 1.1 item 90: under the two-core rule the parts may hold more notes than one core can play, so the later blocks are counted at core 0's own share (part 1 and the
     * globals: core 1 plays part 2 there again); under the one-core rule at the whole one-core cost as before */
    need = c2 + tcr.g + (TCR_BLOCKS - 1u - k) * (tcr.two ? c1 + tcr.g : c1 + c2 + tcr.g);
    if (need > TCR_NEED_MAX)
        need = TCR_NEED_MAX;
    cap = tcr.t0 + TCR_HALF_TICKS - need * TCR_BLOCK_TICKS / 10000u;   /* the latest moment core 0 can still play part 2 itself and finish the half (no slack) */
    dl = cap - TCR_SLACK_NOW;                           /* the table's deadline */
    {   /* the wait limit: with part 2 done by core 1 core 0 still has to do the globals of this block and every later block at the whole one-core cost, so core 1's done word
         * is useful until the half's end less exactly that (= cap + part 2's own cost). Never later than the half's end (the globals are >= 0).
         * L/RRY 1.1 item 142 (HANDOFF_V2 item 139): the later of that (the 1.15 table) and the same rest at TRUE cost (the items brought back as tcr_deadline_true does, rounded up) less
         * TCR_SLACK: the cushion is a flat 0.2 block instead of 15 % of the rest. Measured on the unit (item 139, 312 clean setups) the true rest covered core 0's real post-wait need
         * at the half's peak in every case; core 1's block-0 job then fits the caps of BUDGET_LIMIT 84 (tests/deadline_test.c CORE 1). Never after the half's end */
        uint32_t rest = need > c2 ? need - c2 : 0u, rt, c1t = tcr.t1f + a1 * tcr.t1v, c2t = a2 * tcr.t1v;
        int32_t lo, lt;
        rt = tcr.tg + (TCR_BLOCKS - 1u - k) * (tcr.two ? c1t + tcr.tg : c1t + c2t + tcr.tg);
        rt = rt > TCR_NEED_MAX ? TCR_NEED_MAX : rt;
        lo = (int32_t)TCR_HALF_TICKS - (int32_t)(rest * TCR_BLOCK_TICKS / 10000u);          /* (ticks after the half's start; negative: before it) */
        lt = (int32_t)TCR_HALF_TICKS - (int32_t)(rt * TCR_BLOCK_TICKS / 10000u) - (int32_t)TCR_SLACK_NOW;
        lo = lt > lo ? lt : lo;
        lo = lo < (int32_t)TCR_HALF_TICKS ? lo : (int32_t)TCR_HALF_TICKS;
        tcr.lim = tcr.t0 + (uint32_t)lo;
    }
    *nofit = 0;
#if TC_MEAS
    tcr.cap = cap;
    /* BENCHT3, the floor: the deadline is not earlier than core 1's own measurement says it needs (now + its recent maximum run time + the slack), but never later than
     * cap, so core 0 can still play part 2 itself without a miss. If now + the recent maximum is already past cap, the job would be very late whatever the deadline:
     * no job (nofit), core 0 plays part 2 now; only when the table's deadline itself was still ahead (else it is the plain "deadline past") */
    if (TCR_FLOOR_NOW) {
        uint32_t rm = tcr.fl_cur > tcr.fl_prev ? tcr.fl_cur : tcr.fl_prev;
        if (rm && (int32_t)(dl - now) > 0) {
            uint32_t tgt = now + rm + TCR_SLACK_NOW;
            if ((int32_t)(now + rm - cap) > 0)
                *nofit = 1;
            else if ((int32_t)(tgt - dl) > 0)
                dl = (int32_t)(tgt - cap) > 0 ? cap : tgt;
        }
    }
#else
    (void)now;
#endif
    return dl;
}

/* L/RRY 1.1 item 137: the block-0 take-over at the caps of margin 1.00. Called only under the two-core rule when tcr_deadline's deadline (the 1.15 table) is already past at
 * the post, i.e. when the old code had no take-over at all (tcr_begin's "nott" path: a core 1 that has not started by the wait limit = part 2 silent + a pause). The same
 * sum as tcr_deadline with each item at the true cost (1.00), taken from the 1.15 items and rounded UP (ceil(x * 100 / 115) >= the exact 1.00 item, never under it), less the same
 * slack. If that moment is still ahead of now and before the wait limit, it is the take-over deadline: a job core 1 has not started by then is played by core 0 (no click);
 * a job core 1 is running is waited for up to the wait limit, exactly as before. Otherwise dl comes back unchanged (the old "nott" path). The wait limit is not touched. */
static uint32_t tcr_deadline_true(uint32_t now, uint32_t dl)
{
    uint32_t k = tcr.blk, c1, c2, need, cap, dt;
    c1 = tcr.t1f + tcr.a_lo * tcr.t1v;      /* (item 142: the true items tcr_deadline keeps; the same values as TCR_TRUE of the 1.15 ones) */
    c2 = tcr.a_hi * tcr.t1v;
    need = c2 + tcr.tg + (TCR_BLOCKS - 1u - k) * (c1 + tcr.tg);   /* (two-core rule only: the later blocks at core 0's own share, as tcr_deadline) */
    if (need > TCR_NEED_MAX)
        need = TCR_NEED_MAX;
    cap = tcr.t0 + TCR_HALF_TICKS - need * TCR_BLOCK_TICKS / 10000u;
    dt = cap - TCR_SLACK_NOW;
#if TC_MEAS
    if (TCR_FLOOR_NOW) {   /* item 142: BENCHT3's floor (a diagnosis switch, off by default) on this deadline too, as tcr_deadline has it: not earlier than now + core 1's recent maximum
                            * run time + the slack, never later than this cap; a job that cannot fit before the cap gets no take-over here (the old path: wait to the limit). Without it the later
                            * wait limit of item 142 let a slow job overshoot this deadline (very late) where the floor run before waited for it in time */
        uint32_t rm = tcr.fl_cur > tcr.fl_prev ? tcr.fl_cur : tcr.fl_prev;
        if (rm) {
            if ((int32_t)(now + rm - cap) > 0)
                return dl;
            if ((int32_t)(now + rm + TCR_SLACK_NOW - dt) > 0)
                dt = (int32_t)(now + rm + TCR_SLACK_NOW - cap) > 0 ? cap : now + rm + TCR_SLACK_NOW;
        }
    }
#endif
    if ((int32_t)(dt - now) > 0 && (int32_t)(dt - tcr.lim) < 0)
        return dt;
    return dl;
}
/* the deadline a job is posted with (tcr_begin; tests/deadline_test.c checks it for every setup): tcr_deadline, and under the two-core rule, when that is already past, the true-cost one */
static uint32_t tcr_post_deadline(uint32_t now, int *nofit)
{
    uint32_t dl = tcr_deadline(now, nofit);
    if (tcr.two && (int32_t)(dl - now) <= 0 && !*nofit)
        dl = tcr_deadline_true(now, dl);
    return dl;
}

/* ---------------------------------------------------------------- the runner (core 0) --- */
/* the start of a half (audio.c, before its first block): the clock the deadlines are measured from */
static void tc_half_begin(uint32_t t0)
{
    tcr.t0 = t0;
    tcr.t0_ok = 1;
    tcr.blk = 0;
#if TC_MEAS
    tcm_half();
#endif
}

/* main loop (main.c, beside tc_watch): the jobs paused or core 1 stopped: notes above a part's number fade out at once (the next block does it, in the
 * audio interrupt: the voices are not touched from here). Asked once for each pause or stop */
static uint8_t tc_say;          /* L/RRY 1.1 (owner 2026-10-25): 1 = the unit plays on one core and the screen has not said it yet (ui_input.c ui_notices says it once, then clears this) */
static uint8_t tc_boot_looked;  /* the power-on state of core 1 was looked at (tc_trim_poll) */
static void tc_trim_poll(void)
{
    uint8_t on = tcr.paused || tc.failed;
    if (on && !tcr.trim_on) {
        tcr.trim = 1u;
        tc_say = 1;                                     /* (one core now, for a pause or for the rest of the session) */
    }
    tcr.trim_on = on;
    if (!tc_boot_looked) {                              /* the first pass: core 1 did not start at this power-on (the last session ended in a fault, or the boot guard skipped it) */
        tc_boot_looked = 1;
        if (TC_AUTO_ARM && !tc.up && !tc.failed)
            tc_say = 1;
    }
}

enum { TCR_PW_FAULT, TCR_PW_RUN, TCR_PW_WIN, TCR_PW_LATE };   /* why the jobs pause (BENCHT3): a real fault / a run of very late blocks / K very late blocks in the window / a run of late blocks */
static void tcr_pause(uint32_t now, uint32_t why)
{
    if (!tcr.paused) {
        tcr.paused = 1;
        tcr.pause_t = now;
        tcs.pauses++;
        if (why == TCR_PW_FAULT)
            tcs.pause_fault++;
        else if (why == TCR_PW_RUN)
            tcs.pause_run++;
        else if (why == TCR_PW_WIN)
            tcs.pause_win++;
        else
            tcs.pause_late++;
    }
}

/* a very late block whose job finished inside its half (nothing silent, refused or wrong): it pauses the jobs only when it is the vrun-th in a row (TCR_VLATE_NOW), or
 * the vwin_k-th within the window (the ring of the last times; a gap longer than the window starts the chain again, so a stale slot never counts). Else it is counted
 * (vlate_ok) and core 1 goes on */
static void tcr_vlate_done(uint32_t now)
{
    uint32_t k = TCR_VWINK_NOW, win = 0;
    if (k > TCR_VWIN_MAX)
        k = TCR_VWIN_MAX;
    if (k) {
        if (tcr.vw_n && (uint32_t)(now - tcr.vw_t[(tcr.vw_i - 1u) % TCR_VWIN_MAX]) > TCR_VWINT_NOW)
            tcr.vw_n = 0;
        tcr.vw_t[tcr.vw_i % TCR_VWIN_MAX] = now;
        tcr.vw_i = (uint8_t)((tcr.vw_i + 1u) % TCR_VWIN_MAX);
        if (tcr.vw_n < TCR_VWIN_MAX)
            tcr.vw_n++;
        win = tcr.vw_n >= k && (uint32_t)(now - tcr.vw_t[(tcr.vw_i - k) % TCR_VWIN_MAX]) <= TCR_VWINT_NOW;
    }
    if (TCR_VLATE_NOW && ++tcr.vlate_run >= TCR_VLATE_NOW)   /* (BENCHT4: the run rule is off at 0; then the run is not even counted) */
        tcr_pause(now, TCR_PW_RUN);
    else if (win)
        tcr_pause(now, TCR_PW_WIN);
    else
        tcs.vlate_ok++;
}

/* an old job: core 1 stopped (its words are final: a claimed job may have left part 2 half written, else part 2 is untouched), or it is closed now */
static void tcr_settle(void)
{
    if (!tcr.open)
        return;
#if LRRY_PIPELINE
    if (tcr.open == tcr.pj)
        return;                                         /* (the job of the block before is closed by tcr_pipe_collect: its sums are still to be added, even if core 1 is held by now) */
#endif
    if (!tc.up) {
        uint32_t a = tc_answer(tcr.open);
        if (a == TCA_CLAIMED || a == TCA_BAD) {
            tcr.fade2 = 1;
            tcr.fade_mask = tcr.open_set;
        }
        tcr.open = 0;
        tcr.open_set = 0;
    } else if (tc_closed()) {
#if TC_MEAS
        tcm_collect(tcr.open);                          /* (a very late job that finished after its half) */
#endif
        tcr.open = 0;
        tcr.open_set = 0;
    }
}

/* core 0 plays core 1's voices itself: the one-core order (the same sum) */
static void tcr_play_hi(uint32_t ti, int32_t *out, int32_t *outR, uint32_t n)
{
    lrry_render_set(ti, tcr.c1set, out, outR, n);
}
static uint32_t tcr_open_set(void) { return tcr.open ? tcr.open_set : 0u; }
static uint32_t tcr_go_now(void) { return tcr.go; }
static uint32_t tcr_open_now(void) { return tcr.open != 0u; }

/* the block's start (fx.c mix_block, before the part): settle the old job, resume after the calm period, trim, and say whether a job may go this block (the post itself is tcr_voices_post) */
static void tcr_begin(uint32_t n)
{
    uint32_t now = TC_TICKS(), i;
    (void)n;
    tcs.blocks++;
    tcr.job = 0;
    tcr.skip2 = 0;
    tcr.got = 0;
    tcr.a_lo = tcr.a_hi = 0;
    tcr.c1set = 0;
    tcr.sil = 0;
#if TC_MEAS
    if (tcm.clr)
        tcm_clear();
#endif
    if (!tcr.t0_ok) {
        tcr.t0 = now;
        tcr.t0_ok = 1;
    }
#if TC_MEAS
    if (++tcr.fl_n >= TCR_FLOOR_BLOCKS)                 /* (the floor's buckets turn over by blocks, not by jobs, so a maximum that stopped the jobs fades too) */
        tcr.fl_n = 0, tcr.fl_prev = tcr.fl_cur, tcr.fl_cur = 0;
#endif
    tcr_settle();
    if (tcr.fade2) {                                    /* core 1 stopped in a claimed job: core 1's voices's audio state may be half written, they fade out (voice_kill: the one-block fade) before they are played */
        tcr.fade2 = 0;
        for (i = 0; i < NVOICE; i++)
            if (((tcr.fade_mask >> i) & 1u) && trk[0].v[i].active && trk[0].v[i].stage != 4u)
                voice_kill(&trk[0].v[i]);
    }
    {   /* L/RRY 1.1 item 90: everything on core 0 from this block (core 1 down, paused, or a flash park): the one-core numbers hold at once (trk_nvoice reads bnotes1), and the
         * notes above them fade now (voice_trim_now: the one-block fade), not at the main loop's next pass */
        uint8_t one = (uint8_t)(!tc.up || tc.park || tcr.paused TCR_OR_NOCORE1);
        if (one && !bd_one_now)
            tcr.trim = 1u;
        bd_one_now = one;
    }
    if (tcr.trim & 1u) {
        tcr.trim &= (uint8_t)~1u;
        tcs.trims += voice_trim_now(&trk[0]);
    }
    if (tcr.paused && (int32_t)(now - tcr.pause_t - TCR_CALM_NOW) >= 0) {
        tcr.paused = 0;
        tcr.late_run = tcr.vlate_run = 0;
        tcr.vw_n = 0;
        tcs.resumes++;
    }
#if LRRY_PIPELINE
    tcr.cj = 0;
    tcr.psil = 0;
    tcr.go = (uint8_t)(tc.up && !tc.park && !tcr.paused TCR_AND_NOT_NOCORE1);   /* (the job of the block before is open here; the render stage asks again once it is closed) */
#else
    tcr.go = (uint8_t)(tc.up && !tc.park && !tcr.paused && !tcr.open TCR_AND_NOT_NOCORE1);
#endif
#if TC_MEAS
    tcr.own_why = !tc.up || TCR_NOCORE1_NOW ? TCM_OWN_DOWN : tc.park ? TCM_OWN_PARK : tcr.paused ? TCM_OWN_PAUSE : TCM_OWN_DL;
#endif
}

#if !LRRY_PIPELINE
/* in the render stage, after the control work of the voices (voice.c): hand core 1's voices to core 1 if the hand-over is on and they have something to render */
static uint32_t tcr_voices_post(uint32_t ti, uint32_t n, uint32_t c1set)
{
    uint32_t now, dl, k, i;
    int nofit;
    tcr.a_hi = (uint8_t)__builtin_popcount(c1set);
    tcr.a_lo = (uint8_t)__builtin_popcount(lrj.mask & ~c1set);
    if (!c1set || !tcr.go || tcr.a_hi > LRRY_C1_MAX) {
        tcr.a_hi = 0;
        tcr.a_lo = (uint8_t)__builtin_popcount(lrj.mask);
        return 0;
    }
    now = TC_TICKS();
    dl = tcr_post_deadline(now, &nofit);                /* (item 137: the 1.15 deadline, or when that is past the true-cost one if it is still ahead) */
    tcr.nott = 0;
#if LRRY_NOLIMIT                                        /* bench: core 1's job always goes, core 0 waits for it up to the half's end and never plays core 1's voices itself, whatever the cost table says (measures the real capacity of both cores) */
    dl = tcr.lim = tcr.t0 + TCR_HALF_TICKS;
    tcr.nott = 1;
    nofit = 0;
#else
    if (tcr.two && ((int32_t)(dl - now) <= 0 || nofit)) {   /* item 90: two-core numbers sound and core 0 can no longer take the voices back in time: post it all the same, no take-over */
        tcr.nott = 1;
        dl = tcr.lim;
        nofit = 0;
    }
#endif
    if ((int32_t)(dl - now) <= 0 || nofit) {
#if TC_MEAS
        tcr.own_why = TCM_OWN_DL;
        if (nofit)
            tcr.own_why = TCM_OWN_FL;
#endif
        tcr.a_hi = 0;
        tcr.a_lo = (uint8_t)__builtin_popcount(lrj.mask);
        return 0;                                       /* the half is too far gone to wait for core 1 (or core 1's measured run time does not fit before the latest deadline: not a fault) */
    }
    tc_ctx.pp = lrry_pp[ti];                            /* the block copy of the patch (the model's blockP): core 1 reads only this */
    tc_ctx.mask = c1set;
    tc_ctx.n = tcr.a_hi;
    for (k = 0, i = 0; i < NVOICE; i++)
        if ((c1set >> i) & 1u) {
            tc_ctx.id[k] = i;
            tc_ctx.vc[k] = lrj.vc[i];
            tc_ctx.m[k] = lrj.m[i];
            k++;
        }
    tcr.dl = dl;
#if TC_MEAS
    tcj.t_post = TC_TICKS();                            /* (before the post, so core 1 cannot see the job before this tick) */
    tcj.blk = tcr.blk;
    tcj.t0 = tcr.t0;
    tcj.at = tcs.blocks;
    tcj.vl = 0;
    tcm.posted++;
#endif
    tcr.job = tc_post(n | (uint32_t)tc_ctx_hash(&tc_ctx) << 16);
    tcr.open = tcr.job;
    tcr.open_set = (vmask_t)c1set;
    tcr.c1set = (vmask_t)c1set;
    return tcr.job;
}
#endif

/* a fault of core 1 found by the runner: stopped for the session, a strike, core 0 plays core 1's voices (they fade out at the next block if core 1 had claimed the job) */
static void tcr_fault(uint32_t why, uint32_t ti, int32_t *out, int32_t *outR, uint32_t n)
{
    tc_fail(why);
    tcs.refused++;
    tcr_settle();
    tcr_play_hi(ti, out, outR, n);
}
/* core 1's sums added to the block */
static void tcr_add_acc(int32_t *out, int32_t *outR, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        out[i] += tc_acc[0][i];
        outR[i] += tc_acc[1][i];
    }
}
/* the block's end (fx.c mix_block): a block with no voices of core 1's side to render is counted here */
static void tcr_end(void)
{
    if (!tcr.got) {
        if (tcr.open)
            tcs.wait++;
        else {
            tcs.own++;
#if TC_MEAS
            tcm.own[tcr.own_why]++;
#endif
        }
    }
    if (tcr.blk < TCR_BLOCKS - 1u)
        tcr.blk++;
}

/* after core 0 rendered voices 0..3: core 1's voices are on core 1 (wait for it, the late path), or core 0 plays them, or they stay silent */
static void tcr_voices_collect(uint32_t ti, int32_t *out, int32_t *outR, uint32_t n)
{
    uint32_t j = tcr.job, a, lim = tcr.lim, t1;
    int vl = 0;                                         /* very late: counted already */
    int noans = 0;                                      /* no answer to the take: a fault even if core 1 finishes the job later */
    tcr.got = 1;
    if (!j) {
        if (tcr.open) {                                 /* core 1 still holds an older job: its voices are its until it is done */
            tcs.wait++;
            if (lrj.mask & tcr.open_set) {
                tcs.silent++;
                tcr.skip2 = 1;
                tcr.sil = (vmask_t)(lrj.mask & tcr.open_set);
            }
        } else {
            tcs.own++;
#if TC_MEAS
            tcm.own[tcr.own_why]++;
#endif
            tcr_play_hi(ti, out, outR, n);
        }
        return;
    }
    while ((a = tc_answer(j)) == TCA_NONE || a == TCA_CLAIMED)
        if ((int32_t)(TC_TICKS() - tcr.dl) >= 0)
            break;
    if (a == TCA_NONE || a == TCA_CLAIMED) {            /* not done by the deadline: take it back and see what core 1 says */
        tc_take(j);
        t1 = TC_TICKS() + TCR_ANSWER_TICKS;
        if ((int32_t)(t1 - lim) > 0)
            t1 = lim;                                   /* (the wait for the answer is inside the limit too) */
        while ((a = tc_answer(j)) == TCA_NONE && (int32_t)(TC_TICKS() - t1) < 0)
            ;
        if (a == TCA_YIELD && tcr.nott) {               /* item 90: it had not started and core 0 has no time left for them: silent this block (core 1 never touched it), a pause */
            tcs.late++;
            tcs.silent++;
            tcr.open = 0;
            tcr.open_set = 0;
            tcr.skip2 = 1;
            tcr.sil = tcr.c1set;
            tcs.pause_g21++;                            /* (owner rule G21, ported from DUELL 1.2 2026-11-11: NO REST AFTER A LATE BLOCK; a late block is a defect to find at its cause) */
            return;
        }
        if (a == TCA_YIELD) {                           /* it had not started: core 0 plays part 2, no click */
            tcs.late++;
            tcr.open = 0;
            tcr.open_set = 0;
            tcr.vlate_run = 0;
            if (++tcr.late_run >= TCR_LATE_NOW)         /* (owner rule G21, ported from DUELL 1.2 2026-11-11: NO REST AFTER A LATE BLOCK; a late block is a defect to find at its cause) */
                tcs.pause_g21++, tcr.late_run = 0;
            tcr_play_hi(ti, out, outR, n);
            return;
        }
        if (a == TCA_CLAIMED || a == TCA_NONE) {        /* it is on it (or stuck): wait for done, but not past the wait limit (BENCHT4; up to BENCHT3: the end of the half) */
            if (a == TCA_NONE)
                tcs.noanswer++, noans = 1;
            while ((a = tc_answer(j)) == TCA_CLAIMED || a == TCA_NONE)
                if ((int32_t)(TC_TICKS() - lim) >= 0)
                    break;
            vl = 1;
            tcs.vlate++;
#if TC_MEAS
            tcj.vl = 1;
#endif
            tcr.late_run = 0;
            if (a == TCA_YIELD) {                       /* (no answer in time, then yld: it never touched them) */
                tcr_pause(TC_TICKS(), TCR_PW_FAULT); /* [no answer] */
                tcr.open = 0;
                tcr.open_set = 0;
                tcr_play_hi(ti, out, outR, n);
                return;
            }
            if (a == TCA_CLAIMED || a == TCA_NONE) {    /* not done by the wait limit: core 1's voices silent for this block, core 0 touches nothing of them */
                if (a == TCA_NONE)                      /* (owner rule G21, ported from DUELL 1.2 2026-11-11: NO REST AFTER A LATE BLOCK; a late block is a defect to find at its cause): a job core 1 is WORKING on and only late rests nothing; no answer at all is stuck, a fault */
                    tcr_pause(TC_TICKS(), TCR_PW_FAULT); /* [silent] */
                else
                    tcs.pause_g21++;
                tcs.silent++;
                tcr.skip2 = 1;
                tcr.sil = tcr.c1set;
                return;
            }
            /* done inside the half: a real fault only if there was no answer to the take or the answer is wrong (below); else a very late block that did no harm */
            if (noans || a != TCA_DONE || TC_LD(&tc_mb.bad))
                tcr_pause(TC_TICKS(), TCR_PW_FAULT);
            else
                tcr_vlate_done(TC_TICKS());
        }
    }
    if (a == TCA_DONE && !TC_LD(&tc_mb.bad)) {
        if (!vl) {                                      /* (done by the deadline, or between it and the take) */
            tcs.in_time++;
            tcr.late_run = tcr.vlate_run = 0;
        }
#if TC_MEAS
        tcm_collect(j);
#endif
        tcr.open = 0;                                   /* (a very late one that is done: its sums are good) */
        tcr.open_set = 0;
        tcr_add_acc(out, outR, n);
        return;
    }
    if (vl)
        tcs.vlate--;                                    /* (counted as refused, once) */
    tcr_fault(a == TCA_BAD ? TCR_BADWORD : TCR_BADJOB, ti, out, outR, n);   /* refused (a check value, a snapshot), or a word ahead */
}

#if LRRY_PIPELINE
/* ================================================================ PIPELINE (L/RRY cores-pipeline; LRRY_PIPELINE=1, the default with two cores) ===================================================================
 * The same protocol, words, hash, deadline, take-back, wait limit, pause, strike, park and fall-back as above; what changes is WHEN core 0 asks for core 1's sums.
 *   one block, N          events, control of all voices, then the render stage (voice.c lrry_render_stage):
 *                           1  choose core 1's voices for block N and make their block copy (tcr_pipe_fill: the copy of the patch, the voices' numbers, vc, m; its hash) into the copy that is free, with the job's
 *                              deadline and wait limit (worked out for the block that will close it)
 *                           2  tcr_pipe_collect: close the job of block N-1 (it has run since the end of N-1's stage: all the rest of block N-1 and the events and control of N): wait for done up to the deadline, or take it
 *                              back, or wait up to the wait limit, exactly as before; its sums are added to block N-1's sum
 *                           3  post the job of block N (the copy is ready: the words and the post are all that is left; core 1 is idle only for the work between its done and this post)
 *                           4  core 0 renders its own voices of block N into its buffer, and block N-1's finished sum is what the rest of the part (insert, mix, gather, master, DAC) works on
 *   So the sound is block N's voices in block N+1's output: ONE BLOCK (0.725 ms) later than the plain runner, in every mode: with one core (core 1 down, paused, parked, failed) the stage does the same, with
 *   core 0 playing all voices into the buffer. A switch between the modes therefore changes nothing in the audio path: the job of the block before is closed by whoever can (core 1: its sums; core 0: from the
 *   job's own copy), the next block's voices go where the mode says. No block is dropped, none is played twice, the latency never changes (tests/twocore_sim.c proves it: every output sample equals the
 *   one-core run's, in all modes and with every fault, and equals the plain one-core program's shifted by one block).
 *   The voices' audio state is still touched by one core at a time: the job of block N-1 is closed before the voices of block N are rendered or posted; a note that starts in block N's control waits for the
 *   close (voice.c lrry_pend). The block copies alternate (job number & 1), so core 0 fills one while core 1 may read the other. A job's deadline and wait limit are the old formulas, worked out when the job is
 *   made ready for the block that will close it (the next block: a job posted in a half's last block is closed in the next half's first, no earlier than that half's start), with this block's split as the
 *   shares; the collect uses them as they are.
 *   A core 1 stopped by the main loop's watch while a job is pending: the collect finds it held (not up): done = its sums are good; never claimed = core 0 plays it from the copy; claimed = silent, the job's voices
 *   fade out at the next block (tcr_settle), as in the plain runner. */
static uint32_t tcr_pending(void) { return tcr.pj; }
static uint32_t tcr_go_post(void) { return tc.up && !tc.park && !tcr.paused && !tcr.open TCR_AND_NOT_NOCORE1; }

/* the check value with the patch part cached (core 0 only; core 1 hashes every word, tc_ctx_hash: the same value). The patch is 600 of the copy's 900 bytes and changes only when the decode ran or the stage
 * pans' ramp starts moved (lrry_pp_ver), so the copy and the FNV state after its words are kept for each of the two copies and redone only then: core 0 copies and hashes the voices' part, 300 bytes, a block */
#define TCR_FNV(h, w) (((h) ^ (w)) * 0x01000193u)
static uint32_t tcr_hash_pp(const tc_ctx_t *c)
{
    const uint32_t *w = (const uint32_t *)c;
    uint32_t i, h = 0x811C9DC5u;
    for (i = 0; i < sizeof c->pp / 4u; i++)
        h = TCR_FNV(h, w[i]);
    return h;
}
static uint32_t tcr_hash_rest(const tc_ctx_t *c, uint32_t h)
{
    const uint32_t *w = (const uint32_t *)c;
    uint32_t i, n = c->n < LRRY_C1_MAX ? c->n : LRRY_C1_MAX;
    for (i = sizeof c->pp / 4u; i < __builtin_offsetof(tc_ctx_t, vc) / 4u; i++)
        h = TCR_FNV(h, w[i]);
    w = (const uint32_t *)c->vc;
    for (i = 0; i < n * sizeof c->vc[0] / 4u; i++)
        h = TCR_FNV(h, w[i]);
    w = (const uint32_t *)c->m;
    for (i = 0; i < n * sizeof c->m[0] / 4u; i++)
        h = TCR_FNV(h, w[i]);
    return (h ^ h >> 16) & 0xFFFFu;
}
/* the block copy for the next job (number tc.job + 1) of core 1's voices c1set: made while core 1 may still run the job before, on the other copy. 0: no job (too many voices, or the half is too far gone) */
static uint32_t tcr_pipe_fill(uint32_t ti, uint32_t c1set)
{
    uint32_t j = tc.job + 1u, k, i;
    tc_ctx_t *c = &TC_CTXJ(j);
    if ((uint32_t)__builtin_popcount(c1set) > LRRY_C1_MAX)
        return 0;
    {   /* the admission and the job's deadline and wait limit, worked out here as the plain runner does at its post, for the block that will close the job (the next block; from a half's last block the next half's
         * first, no earlier than that half's start): under the one-core rule a job goes only if the half still has the time to wait for core 1; under the two-core rule it goes anyway when core 0 could no longer
         * take the voices back in time (no take-over: core 1 finishes it), if the wait limit is still ahead. They stay with the job (fill_dl, fill_lim, fill_nott): the collect uses them as they are, so a job is
         * never judged by a later block's different voices */
        int nofit;
        uint32_t now = TC_TICKS(), dl, sb = tcr.blk, st0 = tcr.t0;
        tcr.a_hi = (uint8_t)__builtin_popcount(c1set);
        tcr.a_lo = (uint8_t)__builtin_popcount(lrj.mask & ~c1set);
        if (sb + 1u >= TCR_BLOCKS) {
            tcr.blk = 0, tcr.t0 = st0 + TCR_HALF_TICKS;
            if ((int32_t)(now - tcr.t0) < 0)
                now = tcr.t0;
        } else
            tcr.blk = (uint8_t)(sb + 1u);
        dl = tcr_post_deadline(now, &nofit);
        tcr.fill_nott = 0;
#if LRRY_NOLIMIT                                        /* bench: core 0 waits for the job up to the half's end and never plays core 1's voices itself */
        dl = tcr.lim = tcr.t0 + TCR_HALF_TICKS;
        tcr.fill_nott = 1;
        nofit = 0;
#else
        if (tcr.two && ((int32_t)(dl - now) <= 0 || nofit)) {
            dl = tcr.lim;
            tcr.fill_nott = 1;
            nofit = 0;
        }
#endif
        tcr.fill_dl = dl;
        tcr.fill_lim = tcr.lim;
        tcr.blk = (uint8_t)sb;
        tcr.t0 = st0;
        if ((int32_t)(dl - now) <= 0 || nofit) {
#if TC_MEAS
            tcr.own_why = nofit ? TCM_OWN_FL : TCM_OWN_DL;
#endif
            return 0;
        }
    }
    if (!tcr.pp_ok[j & 1u] || tcr.pp_ver[j & 1u] != lrry_pp_ver[ti]) {   /* the block copy of the patch (the model's blockP): core 1 reads only this; redone only when the patch changed */
        c->pp = lrry_pp[ti];
        tcr.pp_ver[j & 1u] = lrry_pp_ver[ti];
        tcr.pp_hash[j & 1u] = tcr_hash_pp(c);
        tcr.pp_ok[j & 1u] = 1;
        tcr.pp_copies++;
    }
    c->mask = c1set;
    c->n = (uint32_t)__builtin_popcount(c1set);
    for (k = 0, i = 0; i < NVOICE; i++)
        if ((c1set >> i) & 1u) {
            c->id[k] = i;
            c->vc[k] = lrj.vc[i];
            c->m[k] = lrj.m[i];
            k++;
        }
    tcr.fill_arg = CTL | (j & 1u) << 8 | tcr_hash_rest(c, tcr.pp_hash[j & 1u]) << 16;
    return 1;
}
static void tcr_pipe_post(uint32_t n, uint32_t c1set)
{
    (void)n;
#if TC_MEAS
    tcj.t_post = TC_TICKS();                            /* (before the post, so core 1 cannot see the job before this tick) */
    tcj.blk = tcr.blk;
    tcj.t0 = tcr.t0;
    tcj.at = tcs.blocks;
    tcj.vl = 0;
    tcm.posted++;
#endif
    tcr.job = tc_post(tcr.fill_arg);
    tcr.open = tcr.job;
    tcr.open_set = (vmask_t)c1set;
    tcr.c1set = (vmask_t)c1set;
    tcr.pj = tcr.job;
    tcr.pset = (vmask_t)c1set;
    tcr.pbuf = (uint8_t)(tcr.job & 1u);
    tcr.pdl = tcr.fill_dl;
    tcr.plim = tcr.fill_lim;
    tcr.pnott = tcr.fill_nott;
}
/* core 0 plays a job's voices itself, from the job's own block copy (block N's control has overwritten lrj by now): the same sum */
static void tcr_play_ctx(uint32_t buf, int32_t *out, int32_t *outR, uint32_t n)
{
    tc_ctx_t *c = &tc_ctxs[buf & 1u];
    uint32_t i;
    for (i = 0; i < c->n && i < LRRY_C1_MAX; i++)
        if (c->id[i] < NVOICE)
            lrry_voice(&c->pp, &c->vc[i], &lrry_vs[0][c->id[i]], &c->m[i], out, outR, n);
}
static void tcr_fault_p(uint32_t why, uint32_t buf, int32_t *out, int32_t *outR, uint32_t n)
{
    tc_fail(why);
    tcs.refused++;
    tcr_settle();
    tcr_play_ctx(buf, out, outR, n);
}

/* close the job of the block before. out / outR: that block's sum so far (core 0's own voices); core 1's sums are added to it, or core 0 plays core 1's voices into it */
static void tcr_pipe_collect(uint32_t ti, int32_t *out, int32_t *outR, uint32_t n)
{
    uint32_t j = tcr.pj, buf = tcr.pbuf, a, lim, t1;
    int vl = 0, noans = 0;
    (void)ti;
    if (!j && !tcr.open)
        return;                                         /* (no job, none open: tcr_end counts the block) */
    tcr.got = 1;
    if (!j) {                                           /* core 1 still holds an older job (core 0 gave up on it): its voices are its until it is done; core 0 plays the others */
        tcs.wait++;
        if (lrj.mask & tcr.open_set) {
            tcs.silent++;
            tcr.sil = (vmask_t)(lrj.mask & tcr.open_set);
        }
        return;
    }
    tcr.cj = j;
    tcr.pj = 0;
    tcr.nott = tcr.pnott;                               /* the job's own deadline and wait limit, as they were worked out when it was posted */
    tcr.dl = tcr.pdl;
    tcr.lim = tcr.plim;
    if (!tc.up) {                                       /* core 1 is held (the watch stopped it, a fault found in this block's events): nothing more will happen to the job */
        a = tc_answer(j);
        tcr.dl_job = j;
        if (a == TCA_DONE && !TC_LD(&tc_mb.bad)) {      /* it was finished: its sums are good */
            tcs.in_time++;
            tcr.open = 0;
            tcr.open_set = 0;
            tcr_add_acc(out, outR, n);
        } else if (a == TCA_CLAIMED || a == TCA_BAD) {  /* it stopped inside the job: the voices' state may be half written: silent, they fade out at the next block (tcr_settle) */
            tcs.vlate++;
            tcs.silent++;
            tcr.psil = tcr.pset;
        } else {                                        /* never claimed, or refused: core 1 never wrote their state */
            if (a == TCA_DONE || a == TCA_REFUSED)
                tcs.refused++;
            else
                tcs.late++;
            tcr.open = 0;
            tcr.open_set = 0;
            tcr_play_ctx(buf, out, outR, n);
        }
        goto leave;
    }
    tcr.dl_job = j;
    lim = tcr.lim;
    while ((a = tc_answer(j)) == TCA_NONE || a == TCA_CLAIMED)
        if ((int32_t)(TC_TICKS() - tcr.dl) >= 0)
            break;
    if (a == TCA_NONE || a == TCA_CLAIMED) {            /* not done by the deadline: take it back and see what core 1 says */
        tc_take(j);
        t1 = TC_TICKS() + TCR_ANSWER_TICKS;
        if ((int32_t)(t1 - lim) > 0)
            t1 = lim;                                   /* (the wait for the answer is inside the limit too) */
        while ((a = tc_answer(j)) == TCA_NONE && (int32_t)(TC_TICKS() - t1) < 0)
            ;
        if (a == TCA_YIELD && tcr.nott) {               /* it had not started and core 0 has no time left for them: silent (core 1 never touched it), a pause */
            tcs.late++;
            tcs.silent++;
            tcr.open = 0;
            tcr.open_set = 0;
            tcr.psil = tcr.pset;
            tcs.pause_g21++;                            /* (owner rule G21, ported from DUELL 1.2 2026-11-11: NO REST AFTER A LATE BLOCK; a late block is a defect to find at its cause) */
            goto leave;
        }
        if (a == TCA_YIELD) {                           /* it had not started: core 0 plays them, no click */
            tcs.late++;
            tcr.open = 0;
            tcr.open_set = 0;
            tcr.vlate_run = 0;
            if (++tcr.late_run >= TCR_LATE_NOW)         /* (owner rule G21, ported from DUELL 1.2 2026-11-11: NO REST AFTER A LATE BLOCK; a late block is a defect to find at its cause) */
                tcs.pause_g21++, tcr.late_run = 0;
            tcr_play_ctx(buf, out, outR, n);
            goto leave;
        }
        if (a == TCA_CLAIMED || a == TCA_NONE) {        /* it is on it (or stuck): wait for done, but not past the wait limit */
            if (a == TCA_NONE)
                tcs.noanswer++, noans = 1;
            while ((a = tc_answer(j)) == TCA_CLAIMED || a == TCA_NONE)
                if ((int32_t)(TC_TICKS() - lim) >= 0)
                    break;
            vl = 1;
            tcs.vlate++;
#ifdef TCR_DBG
            fprintf(stderr, "DBG VLATE job %u a %u now-t0 %d dl-t0 %d lim-t0 %d a_hi %u mask %x\n", j, a, (int)(TC_TICKS() - tcr.t0), (int)(tcr.dl - tcr.t0), (int)(lim - tcr.t0), (unsigned)__builtin_popcount(tcr.pset), (unsigned)tcr.pset);
#endif
#if TC_MEAS
            tcj.vl = 1;
#endif
            tcr.late_run = 0;
            if (a == TCA_YIELD) {                       /* (no answer in time, then yld: it never touched them) */
                tcr_pause(TC_TICKS(), TCR_PW_FAULT); /* [no answer] */
                tcr.open = 0;
                tcr.open_set = 0;
                tcr_play_ctx(buf, out, outR, n);
                goto leave;
            }
            if (a == TCA_CLAIMED || a == TCA_NONE) {    /* not done by the wait limit: core 1's voices silent for the block, core 0 touches nothing of them */
                if (a == TCA_NONE)                      /* (owner rule G21, ported from DUELL 1.2 2026-11-11: NO REST AFTER A LATE BLOCK; a late block is a defect to find at its cause): a job core 1 is WORKING on and only late rests nothing; no answer at all is stuck, a fault */
                    tcr_pause(TC_TICKS(), TCR_PW_FAULT); /* [silent] */
                else
                    tcs.pause_g21++;
                tcs.silent++;
                tcr.psil = tcr.pset;
                goto leave;
            }
            if (noans || a != TCA_DONE || TC_LD(&tc_mb.bad))
                tcr_pause(TC_TICKS(), TCR_PW_FAULT);
            else
                tcr_vlate_done(TC_TICKS());
        }
    }
    if (a == TCA_DONE && !TC_LD(&tc_mb.bad)) {
        if (!vl) {                                      /* (done by the deadline, or between it and the take) */
            tcs.in_time++;
            tcr.late_run = tcr.vlate_run = 0;
        }
#if TC_MEAS
        tcm_collect(j);
#endif
        tcr.open = 0;
        tcr.open_set = 0;
        tcr_add_acc(out, outR, n);
        goto leave;
    }
    if (vl)
        tcs.vlate--;                                    /* (counted as refused, once) */
    tcr_fault_p(a == TCA_BAD ? TCR_BADWORD : TCR_BADJOB, buf, out, outR, n);
leave:
    if (tcr.open && (lrj.mask & tcr.open_set)) {        /* the job is still open (given up): this block's voices of it are silent too, core 0 renders the others */
        tcs.silent++;
        tcr.sil = (vmask_t)(lrj.mask & tcr.open_set);
    }
}
#endif
