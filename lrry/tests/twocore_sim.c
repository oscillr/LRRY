/* SPDX-License-Identifier: GPL-3.0-only */
/* L/RRY phase 4.3b: HOST-THREAD SIMULATION of the two-core split BY VOICE, written to the unit's protocol (firmware/src/twocore.{h,c}: separate 32-bit words, a sequence number, core 1
 * decides who plays a job) with the real code on both sides. A model of the PROTOCOL, the DATA SEPARATION and the runner's decisions, not of the device's timing.
 *   core 1 (a pthread)        the real tc_c1_step loop (what hal/fm1_core1.h runs in RAM), with the computer hal (tests/twocore_hal_host.h: acquire / release atomics, load and store
 *                             hooks for the fault injection, a simulated clock, a start hook); its job is the real tc_job_run (twocore_run.c: the job's voices into core 1's own accumulator
 *                             from the block copy of the patch), wrapped by sim_job (delays, the ownership audit)
 *   core 0 (the main thread)  the firmware's own mix_block (fx.c) with LRRY_TWOCORE on: the real runner (tcr_begin, then in track_render lrry_render_stage: tcr_voices_post with the deadline and
 *                             tc_post, voices 0..3, tcr_voices_collect: the wait, the take, the three cases, the pause), the part's mix, gather, sends, master. A half is 4 blocks of 32
 *                             (as audio.c calls it: tc_half_begin before the first); the main-loop passes (tc_watch, tc_trim_poll) are called at the start of each half
 * The clock is simulated: it stands still unless a delay is running (then every 4th read of core 0 moves it a tick), so a healthy core 1 is never late because the machine was busy; the harness
 * sets it to the half's start at every half. A delay of core 1 is "until the clock passes T" (T from the runner's deadline of that job), so every late case happens in the block it is aimed at.
 * Each run is one scene of tests/lrry_scn.h (7 of them, 2000 blocks) in a child process with ONE injected fault. The reference is the same firmware with core 1 never started (tc_boot not armed:
 * "unarmed", core 0 plays all eight voices); its output must equal every other run's SAMPLE FOR SAMPLE (the samples are kept and compared with memcmp, not hashed) except the runs that make the job's voices silent
 * on purpose. tests/lrry_split_test.c is the one-core program with no second-core code at all; tests/lrry_split.sh checks that the hashes printed by both agree.
 *   none          the hand-over at full speed with the ownership audit: core 1 waits until core 0 is in its wait, then hashes everything core 0 owns (track, block scratch, accumulators, the patch,
 *                 voices 0..3's state and COMB lines, the control block lrj) around the real job, and everything core 1 owns (the job's voices's state and COMB lines) against the hash taken at the post
 *   conc          the same without the audit gate: both cores really run at once                                         output = the reference
 *   torn1         core 1 reads a new job with the OLD arg / chk once: re-read, run, counted                              same
 *   tornp         ... persistently: refused (BADJOB), a strike, core 0 plays the job's voices from then on                    same
 *   late_pre      core 1 sleeps before it looks: take, yld, core 0 plays them ("late")                                  same
 *   late_post     core 1 sleeps after its claim, done within its wait limit ("very late", single blocks: nothing pauses) same
 *   voff          many very late blocks, 4 in a row and 8 within 2 s among them, BENCHT4 defaults: nothing pauses         same
 *   noans         core 1 sleeps before it looks until past the 100 us answer time of the take, then yields: "no answer", a fault pause, core 0 plays them   same
 *   vlong         core 1 sleeps after its claim until 60 ticks past the WAIT LIMIT: core 0 stops waiting AT the limit, the job's voices silent, a fault pause   counters only
 *   late_run      a run of late blocks pauses the jobs, they resume                                                      same
 *   badchk        the store hook corrupts the check value: refused, a strike                                              same
 *   stale         the load hook returns the older number for done: not done, the deadline path                           same
 *   ahead         a done word ahead (BADWORD)                                                                              same
 *   badsnap       the block copy core 1 reads does not fit its hash: refused like a wrong check                           same
 *   hang_pre      core 1 hangs before it looks at a job: the job's voices silent until tc_watch stops it (HANG, 100 ms), then core 0 plays them   = a one-core run that skips exactly those blocks
 *   hang_post     core 1 hangs after its claim: the same, its voices fade out when core 0 takes over                    = the same with the fade
 *   park          a flash park in the middle of play: no click, output equal, counted
 *   park_stuck    a flash park with core 1 stuck: FLASH after 20 ms simulated                                            = as hang_post
 *   silent_late   core 1 finishes a job after its half: the job's voices silent, then core 0 plays them                       counters only (the late job renders later state)
 *   unarmed / noanswer   the firmware stays on one core: output = the reference; the record says why
 *   shared        self-check: core 1 writing one patch value of core 0's (the OSC 1 level) changes the output and the audit sees the write
 * cores-pipeline (LRRY_PIPELINE, the default with two cores; the same file also runs the plain runner with -DLRRY_PIPELINE=0): the sound is one block later in every mode, so
 *   - the reference (core 1 never started) must equal the PLAIN one-core program's output (tests/lrry_split_test.c writes it, LRRY_PLAIN_DIR) delayed by exactly one block, the first block silent;
 *   - every fault run must equal that reference as before (the voices of job N come out in block N + 1; the runs that make voices silent are compared with the reference silenced in the blocks the run lost);
 *   - the SWITCH SWEEP (fault `sweep`, 64 variants a scene): a flash park, a pause with its resume, core 1 stopped for good, and a core 1 hung in a job and stopped by the watch, each at every position of a
 *     block in a half (4) and every moment of a block (4: the render stage's start, after the job of the block before is closed, after this block's job is posted, between two blocks): the output equals the
 *     reference, sample for sample, no block dropped or played twice (the hung one: equals the reference silenced in the blocks it lost, with the fade);
 *   - the chooser's objective with the pipeline (core 0's fixed work on its side) against a brute force.
 * Mutants (the four of host/lrry_split.h and the ones L/RRY's twocore_mut had): tests/twocore_mut.sh builds copies of the firmware with one rule broken, this program must FAIL on each.
 * Built and run by tests/lrry_split.sh (flags of fw/onepart.sh: -DNPART=1 -DNSHOWN=1; add -lpthread) */
#define LRRY_TWOCORE 1
#define TC_AUTO_ARM 0                                   /* (1.0: every TWOCORE build arms itself; the 'unarmed' scene tests the explicit arm (tc_arm)) */
#define TC_JOB_RUN sim_job
#include <stdint.h>
static void sim_job(uint32_t arg);
#define LRRY_MASK_HOOK(ti, mp) ref_mask_hook(ti, mp)    /* (the reference run silences the job's voices in the blocks the fault run lost) */
static void ref_mask_hook(uint32_t ti, uint32_t *mask);
#define LRRY_STAGE_HOOK(pt) sim_stage_hook(pt)          /* (PIPELINE: the sweep runs act at the render stage's three points as the main loop could between two blocks; voice.c) */
static void sim_stage_hook(int pt);
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "lrry_scn.h"
#include <sys/mman.h>
#include <pthread.h>
#include <sched.h>

#define MAXB SCN_BLOCKS
#define BASE_TICKS 24000u                             /* the simulated clock's start */
#define MS_TICKS (TC_TICKS_PER_US * 1000u)

static int bad, ncheck, shift_warned;
static uint32_t n_shift_runs;                            /* scenes whose reference was compared with the plain one-core program's output (delayed by one block with LRRY_PIPELINE) */
static uint32_t n_eq_runs, n_eq_blocks;                  /* runs whose output was compared with the reference sample for sample, and the blocks in them */
static int expect(const char *what, int ok)
{
    ncheck++;
    if (!ok) {
        printf("twocore: FAIL %s\n", what);
        bad++;
    }
    return ok;
}

enum { F_NONE, F_CONC, F_TORN1, F_TORNP, F_LATE_PRE, F_LATE_POST, F_LATE_RUN, F_BADCHK, F_STALE, F_AHEAD, F_BADSNAP, F_HANG_PRE, F_HANG_POST, F_PARK, F_PARK_STUCK, F_SILENT_LATE,
       F_UNARMED, F_NOANSWER, F_SHARED, F_VOFF, F_NOANS, F_VLONG, F_SWEEP, F_N };
#define F_TRIMSTOP 100                                  /* (the fade run that stops core 1: not one of the scene runs) */
static const char *const FNAME[F_N] = {"none", "conc", "torn1", "tornp", "late_pre", "late_post", "late_run", "badchk", "stale", "ahead", "badsnap", "hang_pre", "hang_post", "park",
                                       "park_stuck", "silent_late", "unarmed", "noanswer", "shared", "voff", "noans", "vlong", "sweep"};

/* what a run reports (shared memory: the runs are child processes) */
typedef struct {
    tc_stat_t s;
    uint32_t parks, up, failed, strike, nv_last, reread, bad_w, misread, c1_jobs, nblk, park_us;
    uint32_t lim_over, end_over, lim_blocks, lim_max_over;   /* blocks with a posted job where core 0 left its block later than the wait limit (+8 ticks) / later than the half's end; blocks looked at; the largest overrun (ticks) */
    uint32_t own_checks, own_writes, own_timeouts, c1own_checks, c1own_writes, c1open_writes;   /* the ownership audit (sim_job): jobs audited, jobs that changed core 0's state, gate timeouts; core 1's state changed between the post and the job's start */
    uint32_t pp_copies;                                 /* PIPELINE: block copies whose patch part was made (the posted jobs reused the others') */
    uint32_t nott;                                      /* posted jobs of blocks where core 0 could no longer take the voices back in time (item 90, tcr.nott: no take-over) */
    uint32_t posted;                                    /* jobs posted */
    uint32_t c1hist[9], lohist[9], moved, stayed;       /* voices on core 1 and on core 0 in the blocks with a job (histograms); voices that sounded in two blocks in a row and changed core / kept it */
    uint64_t hash;
    vmask_t skip[MAXB];                                 /* the voices that were silent in this block (tcr.sil, a mask) */
    uint32_t hang_blk;                                  /* the first block that saw core 1 hung (0xFFFFFFFF: none) */
    uint32_t fade_mask;                                 /* the voices tcr_begin faded out after a core 1 that stopped in a claimed job */
    uint64_t dl_sum[TCR_BLOCKS];                        /* the deadlines of the posted jobs, ticks after the half's start, summed per block of the half */
    uint32_t dl_n[TCR_BLOCKS];
    uint64_t lim_sum[TCR_BLOCKS];
    int32_t out[MAXB * 2 * CTL];                        /* every output sample of the run */
} res_t;
static res_t *R;                                        /* the child writes here */

static int fault;                                       /* the run's injected fault (the child's) */
enum { SW_PARK, SW_PAUSE, SW_STOP, SW_HANGSTOP, SW_KINDS };   /* the switch sweep's kinds (below) */
static int sw_kind, sw_pt, sw_ph, sw_stopped;
static uint32_t nblk;                                   /* blocks so far */
static uint32_t clk_jump;                               /* ticks added to the half starts (the calm periods skipped) */
static uint32_t jumps;                                  /* pauses already followed by a jump */
static int parked;
static volatile uint32_t quit, c1_ready;
static pthread_t thr;
static int thr_on;

static inline uint32_t raw_ticks(void) { return __atomic_load_n(&tc_host_ticks, __ATOMIC_RELAXED); }
static inline int c1_live(void) { return __atomic_load_n(&tc_host_live, __ATOMIC_ACQUIRE) && !__atomic_load_n(&quit, __ATOMIC_ACQUIRE); }
static void cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#else
    sched_yield();
#endif
}
/* The clock stands still unless someone wants time to pass: a delay core 1 was told to take, a hung core 1, a stale word being held back. Then core 0's wait loops move
 * it (every divsel-th read). So a healthy core 1 is never "late" because the machine was busy, and every late case is the one aimed at. */
static pthread_mutex_t tmx = PTHREAD_MUTEX_INITIALIZER;
static int tusers;
static uint32_t divsel = 4u;
static void time_adj(int d)
{
    pthread_mutex_lock(&tmx);
    tusers += d;
    __atomic_store_n(&tc_host_tick_div, tusers > 0 ? divsel : 0u, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&tmx);
}
/* core 1's delay: until the simulated clock reaches t (core 0's wait loops move it; it does not pass t until core 1 is awake, so the case aimed at is the one that
 * happens) or core 1 is held / the run is over */
static void wait_clock(uint32_t t)
{
    __atomic_store_n(&tc_host_stop, t, __ATOMIC_RELEASE);
    time_adj(1);
    while (c1_live() && (int32_t)(raw_ticks() - t) < 0)
        cpu_relax();
    time_adj(-1);                                       /* (time stands still first, then the barrier goes: no tick slips by) */
    __atomic_store_n(&tc_host_stop, 0u, __ATOMIC_RELEASE);
}
/* a delay aimed at job j's deadline (lim = 0) or wait limit (lim = 1) plus off ticks. Plain runner: both are known at the post. PIPELINE: core 0 works them out when it collects the job (the next block), so core 1 waits
 * until they are in force for job j, then until the clock passes them: the same late case, in the block it is aimed at */
static void wait_dl(uint32_t j, uint32_t off, int lim)
{
#if LRRY_PIPELINE
    while (c1_live() && __atomic_load_n(&tcr.dl_job, __ATOMIC_ACQUIRE) != j)
        cpu_relax();
#else
    (void)j;
#endif
    wait_clock((lim ? tcr.lim : tcr.dl) + off);
}
static volatile uint32_t release_silent;                /* the harness lets the silent_late core 1 go on at the next half */
static void wait_release(void)
{
    time_adj(1);
    while (c1_live() && !__atomic_load_n(&release_silent, __ATOMIC_ACQUIRE))
        cpu_relax();
    time_adj(-1);
}
static volatile uint32_t hang_on;                       /* core 1 is hung (the heartbeat guard of sim_block stops helping then) */
static uint64_t c1own_hash(uint32_t c1);
static volatile uint64_t c1own_at_hang;                 /* the hung job's voices as they were when core 1 stopped (it writes nothing from then on) */
static void hang(void)                                  /* hung: until core 0 stops it (or the run ends) */
{
    c1own_at_hang = c1own_hash(TC_CTXJ(tc.job).mask);
    __atomic_store_n(&hang_on, 1u, __ATOMIC_RELEASE);
    time_adj(1);
    while (c1_live())
        cpu_relax();
    time_adj(-1);
}

/* ------------------------------------------------------------ the fault plan: which job gets what --- */
static __thread uint32_t c1_job, c1_seen, torn_left, prev_arg, prev_chk;   /* core 1's: the job it is looking at, the torn reads left */
static uint32_t trim_job, ahead_left;
static volatile uint32_t c0_wait;                       /* the ownership audit's gate: the job core 0 is waiting for (set when it loads the done word) */
static int stale_on;                  /* (the fade runs name their own job) */
static int pl_torn1(uint32_t j) { return fault == F_TORN1 && j % 7u == 3u; }
static int pl_tornp(uint32_t j) { return fault == F_TORNP && j == 150u; }
static int pl_late_pre(uint32_t j)
{
    return (fault == F_LATE_PRE && j % 31u == 2u) || (fault == F_LATE_RUN && ((j >= 150u && j < 154u) || (j >= 500u && j < 505u))) || (fault == F_AHEAD && j == 100u);
}
static int pl_late_post(uint32_t j)
{
    return (fault == F_LATE_POST && j % 401u == 5u) ||
           (fault == F_VOFF && (((j >= 150u && j < 154u) || (j >= 300u && j < 304u) || (j >= 450u && j < 453u)) || (j >= 700u && j < 716u && !(j & 1u)) || j % 401u == 5u));
}
static int pl_noans(uint32_t j) { return (fault == F_NOANS && j % 53u == 9u) || (fault == F_NONE && trim_job && j == trim_job); }
static int pl_vlong(uint32_t j) { return fault == F_VLONG && j % 61u == 7u; }
static int pl_stale(uint32_t j) { return fault == F_STALE && j % 29u == 4u; }
static int pl_hang_pre(uint32_t j) { return fault == F_HANG_PRE && j == 150u; }
static int pl_hang_post(uint32_t j) { return (fault == F_HANG_POST || fault == F_PARK_STUCK || (fault == F_SWEEP && sw_kind == SW_HANGSTOP)) && j == 150u; }
static int pl_silent(uint32_t j) { return fault == F_SILENT_LATE && j == 150u; }

static uint64_t c1own_hash(uint32_t c1);
static volatile uint64_t c1own_at_post;
/* every load of a shared word, core 1's and core 0's (the hal's hook) */
static uint32_t ld_hook(const uint32_t *p, uint32_t v)
{
    if (tc_host_is_c1) {
        if (p == &tc_mb.job && v != c1_seen) {          /* core 1 looks at a new job: what happens first (delays before it looks, a hang, torn reads, a misread snapshot) */
            c1_seen = c1_job = v;
            if (pl_noans(v))
                wait_dl(v, 2600u, 0);             /* later than the 100 us (2,400 ticks) core 0 waits for the answer to its take, earlier than the wait limit: "no answer", then yld */
            else if (pl_late_pre(v))
                wait_dl(v, 1000u, 0);             /* late to wake: core 0 takes the job back before core 1 looks */
            if (pl_hang_pre(v))
                hang();                                 /* hung before it looks: it never answers (held at the end: its stores are dropped) */
            torn_left = pl_torn1(v) ? 1u : pl_tornp(v) ? 100u : 0u;
            if (fault == F_BADSNAP && v == 100u)
                *(volatile uint32_t *)&TC_CTXJ(v) ^= 0x10u;   /* the snapshot as core 1 reads it: one bit wrong */
            return v;
        }
        if (torn_left && p == &tc_mb.arg)
            return prev_arg;                            /* the old job's words */
        if (torn_left && p == &tc_mb.chk) {
            torn_left--;
            return prev_chk;
        }
        return v;
    }
    if (p == &tc_mb.done) {
#if LRRY_PIPELINE
        if (tcr.cj != tc.job)                           /* PIPELINE: only core 0's collect of the job (its wait) counts: the main loop's watch reads the done word too, at a half's start, with core 0 about to work */
            return v;
#endif
        if (fault == F_NONE)
            __atomic_store_n(&c0_wait, tc.job, __ATOMIC_RELEASE);   /* core 0 is in its wait for this job: part 1's stages are over, it touches no part state until done */
        int hide = pl_stale(tc.job) && v == tc.job && (int32_t)(raw_ticks() - (tcr.dl + 500u)) < 0;
        if (hide != stale_on) {                         /* (time passes while the word is held back) */
            stale_on = hide;
            time_adj(hide ? 1 : -1);
        }
        if (hide)
            return v - 1u;                              /* the older number: not done yet */
        if (fault == F_AHEAD && tc.job == 100u && ahead_left) {
            ahead_left--;
            return v + 5u;                              /* a word ahead of the last job posted */
        }
    }
    return v;
}
/* every store of a shared word */
static uint32_t st_hook(uint32_t *p, uint32_t v)
{
    if (!tc_host_is_c1 && p == &tc_mb.job && fault == F_NONE)
        c1own_at_post = c1own_hash(TC_CTXJ(tc.job + 1u).mask);               /* core 0 posts: core 1's state as it is at that moment */
    if (!tc_host_is_c1 && p == &tc_mb.chk && ((fault == F_BADCHK && tc.job + 1u == 200u) || (fault == F_TRIMSTOP && tc.job + 1u == trim_job)))
        return v ^ 0x40u;                               /* core 0's check value written wrong */
    return v;
}


/* ------------------------------------------------------------ the ownership audit --- */
/* A shared write put back by mistake is a race in the threaded run: it shows only if both cores run that spot at the same moment, so it is caught one run in a few. This makes it show every time:
 * in the run `none` core 1 waits until core 0 is in its wait for done (voices 0..3 are rendered and core 0 touches no state until it reads done), hashes everything core 0 owns, runs the real job,
 * and hashes again. The job may write only core 1's own (tc_acc, the job's voices's audio state and COMB lines): any change in core 0's is a write into it. And the other way: core 1's own state
 * (the job's voices's audio state and COMB lines) is hashed by core 0 at the post and by core 1 at its start (core 0 is in its wait by then): core 0 wrote none of it while the job was open. */
static void hash_bytes(uint64_t *h, const void *p, size_t n)
{
    const uint8_t *q = (const uint8_t *)p;
    size_t i;
    for (i = 0; i < n; i++)
        *h = (*h ^ q[i]) * 0x100000001B3ull;
}
static void hash_voices(uint64_t *h, uint32_t set)  /* the audio state and COMB lines of the voices in set */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if ((set >> i) & 1u) {
            hash_bytes(h, &lrry_vs[0][i], sizeof lrry_vs[0][i]);
            hash_bytes(h, LRRY_COMB_LINE(0, i, 0), 2u * LRRY_COMB_LEN * sizeof(int16_t));
        }
}
static uint64_t own_hash(uint32_t c1)                   /* core 0's: everything but the job's voices */
{
    uint64_t h = 0xCBF29CE484222325ull;
    hash_bytes(&h, &trk[0], sizeof trk[0]);
    hash_bytes(&h, &pscr[0], sizeof pscr[0]);
    hash_bytes(&h, &pacc[0], sizeof pacc[0]);
    hash_bytes(&h, &lrry_pp[0], sizeof lrry_pp[0]);
    hash_voices(&h, ~c1 & ((1u << NVOICE) - 1u));
    hash_bytes(&h, &lrj, sizeof lrj);
    hash_bytes(&h, mix_l, sizeof mix_l);
    hash_bytes(&h, mix_r, sizeof mix_r);
#if LRRY_PIPELINE
    hash_bytes(&h, lrry_pl, sizeof lrry_pl);            /* (core 0's voice buffers, and the block copy of the job that is not running) */
    hash_bytes(&h, &tc_ctxs[(c1_job + 1u) & 1u], sizeof tc_ctxs[0]);
#endif
    return h;
}
static uint64_t c1own_hash(uint32_t c1)                 /* core 1's: the job's voices */
{
    uint64_t h = 0xCBF29CE484222325ull;
    hash_voices(&h, c1 & ((1u << NVOICE) - 1u));
    return h;
}
/* c1own_at_post: core 0's hash of core 1's state, taken at the post (the store of the job word) */
static uint32_t own_checks, own_writes, own_timeouts, c1own_checks, c1own_writes, c1open_writes;
static int own_gate(uint32_t j)                         /* wait for core 0 to be in its wait for job j */
{
    uint64_t n = 0;
    while (c1_live() && __atomic_load_n(&c0_wait, __ATOMIC_ACQUIRE) != j) {
        cpu_relax();
        if (++n > 400000000ull)
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------ core 1 --- */
/* the job as core 1 runs it: delays, the audit around the real job */
static uint32_t c1_jobs;
static void sim_job(uint32_t arg)
{
    uint32_t j = c1_job;
    uint64_t ho = c1own_hash(TC_CTXJ(j).mask);
#if LRRY_PIPELINE
    prev_arg = __atomic_load_n(&tc_mb.arg, __ATOMIC_RELAXED);   /* the words of this job: the "old" ones of the next job's torn read */
    prev_chk = __atomic_load_n(&tc_mb.chk, __ATOMIC_RELAXED);
#endif                         /* core 1's state when it claimed the job; core 0 must not touch it while the job is open, however long core 1 sleeps (the delays below) */
    if (pl_vlong(j))
        wait_dl(j, 60u, 1);                      /* slower than core 0 may wait: it stops at the wait limit, the job's voices silent for the block */
    else if (pl_late_post(j))
        wait_dl(j, 1000u, 0);                     /* slow in the middle: core 0 takes it back, then waits for done */
    if (pl_silent(j))
        wait_release();                                 /* not done in the half: core 0 gives the voices up for the block; it goes on at the next half */
    if (pl_hang_post(j)) {
        hang();                                         /* hung after its claim: nothing is touched */
        return;
    }
    if (!c1_live())
        return;
    if (c1own_hash(TC_CTXJ(j).mask) != ho)
        c1open_writes++;
    if (fault == F_NONE) {
        uint64_t a0 = 0;
        int gated = own_gate(j);
        if (gated) {
            a0 = own_hash(TC_CTXJ(j).mask);
            c1own_checks++;
            if (c1own_hash(TC_CTXJ(j).mask) != c1own_at_post)
                c1own_writes++;
        } else
            own_timeouts++;
        tc_job_run(arg);
        if (gated) {
            own_checks++;
            if (own_hash(TC_CTXJ(j).mask) != a0)
                own_writes++;
        }
    } else
        tc_job_run(arg);
    if (fault == F_BADSNAP && j == 100u)
        *(volatile uint32_t *)&TC_CTXJ(j) ^= 0x10u;     /* (the misread was core 1's: the copy in memory is whole, core 0 may play the job from it) */
    if (fault == F_SHARED && c1_jobs == 300u)
        XV(&trk[0], X_O1LVL) = (int16_t)(XV(&trk[0], X_O1LVL) / 2);   /* the deliberate shared write: core 1 changes a patch value of core 0's */
    c1_jobs++;
}
static void *core1(void *arg)
{
    tc_c1_t c;
    (void)arg;
    tc_host_is_c1 = 1;
    tc_c1_init(&c);
    __atomic_store_n(&c1_ready, 1u, __ATOMIC_RELEASE);
    while (c1_live()) {
        uint32_t before = c.last;
        tc_c1_step(&c);
#if !LRRY_PIPELINE
        if (c.last != before) {                         /* (the words of the job it just answered: the "old" ones of the next job's torn read) */
            prev_arg = __atomic_load_n(&tc_mb.arg, __ATOMIC_RELAXED);
            prev_chk = __atomic_load_n(&tc_mb.chk, __ATOMIC_RELAXED);
        }
#else
        (void)before;                                   /* (PIPELINE: core 0 posts the next job the moment this one is closed, so the old words are taken inside the job: sim_job) */
#endif
        cpu_relax();
    }
    return 0;
}
static void start_core1(void)
{
    thr_on = 1;
    pthread_create(&thr, 0, core1, 0);
}

/* ------------------------------------------------------------ core 0: the blocks --- */
static void record(const int32_t *out, uint32_t skip)
{
    if (nblk < MAXB) {
        memcpy(R->out + (size_t)nblk * 2u * CTL, out, 2u * CTL * sizeof out[0]);
        R->skip[nblk] = (vmask_t)skip;
    }
    nblk++;
}

/* ------------------------------------------------------------ the switch sweep (PIPELINE; also run on the plain runner) --- */
/* A switch between one core and two (core 1 parked for a flash access, the jobs paused and resumed, core 1 stopped for good) at EVERY moment of a block: 4 block positions in a half x 4 moments in the block
 * (0 the render stage's start, after the control; 1 after the job of the block before is closed; 2 after this block's job is posted; 3 between two blocks) x 3 kinds. The output of every such run must equal the
 * one-core reference sample for sample: no block dropped, none played twice, no step in the latency. A cycle repeats every 64 blocks (the position is the block number mod 4). */
#define SW_N (SW_KINDS * 16)
static void sw_set(int v) { sw_kind = v / 16, sw_pt = (v / 4) % 4, sw_ph = v % 4; }
static void sw_act(int pt)
{
    uint32_t b = nblk, m;
    if (fault != F_SWEEP || pt != sw_pt || (b & 3u) != (uint32_t)sw_ph)
        return;
    m = b % 64u;
    if (sw_kind == SW_PARK) {
        if (m == 16u + (uint32_t)sw_ph)
            tc_flash_park();                            /* the flash driver: no job from now on, the last one over within 20 ms */
        else if (m == 21u + (uint32_t)sw_ph)
            tc.park = 0;                                /* (the main loop's next pass) */
    } else if (sw_kind == SW_PAUSE) {
        if (m == 16u + (uint32_t)sw_ph)
            tcr_pause(raw_ticks(), TCR_PW_FAULT);       /* the jobs pause */
        else if (m == 40u + (uint32_t)sw_ph) {
            clk_jump += TCR_CALM_TICKS;                 /* the calm period passes: the jobs resume at the next block's start */
            __atomic_store_n(&tc_host_ticks, raw_ticks() + TCR_CALM_TICKS, __ATOMIC_RELAXED);
        }
    } else if (sw_kind == SW_HANGSTOP) {
        if (!sw_stopped && __atomic_load_n(&hang_on, __ATOMIC_ACQUIRE) && tc.up) {   /* core 1 hung inside job 150: the main loop's watch stops it at this moment (the job is pending, or already given up) */
            tc_fail(TCR_HANG);
            sw_stopped = 1;
        }
    } else if (!sw_stopped && m == 16u + (uint32_t)sw_ph && tc.up) {
        while (c1_live() && !tc_closed())
            cpu_relax();
        tc_fail(TCR_BADJOB);                            /* core 1 held for the session, as the watch does */
        sw_stopped = 1;
    }
}
static void sim_stage_hook(int pt) { sw_act(pt); }

/* the reference paths: core 1 never started. ref_skip != 0: the blocks it flags have the job's voices silent (the hung core 1's), and at block ref_fade core 1's voices fade out (tcr.fade2, as tcr_begin
 * finds it after a core 1 that stopped in a claimed job) */
static const vmask_t *ref_skip;                         /* per block: the voices (a mask) that were silent in the fault run */
static uint32_t ref_fade = 0xFFFFFFFFu, ref_fade_mask;
static void ref_mask_hook(uint32_t ti, uint32_t *mask)
{
    (void)ti;
    if (ref_skip && nblk < MAXB)
        *mask &= ~(uint32_t)ref_skip[nblk];
}
/* While a job is hung, the harness lets every note go and, 60 blocks later, plays 8 new ones: voices of the hung job that have ended are struck again while the job is still open (their audio-state
 * clear must wait: voice.c lrry_pend). hang_blk is the first block that sees the hang; the reference run replays the same notes at the same blocks (ref_hang_blk) */
static uint32_t hang_blk = 0xFFFFFFFFu, ref_hang_blk = 0xFFFFFFFFu;
static void inject_notes(uint32_t b, uint32_t hb)
{
    uint32_t k;
    if (hb == 0xFFFFFFFFu)
        return;
    if (b == hb + 1u) {
        trk[0].p[P_REL] = 0;                            /* (a short release, so the voices have ended when the new notes come) */
        for (k = 0; k < 128u; k++)
            trk_note_off(&trk[0], k);
    }
    if (b == hb + 60u)
        for (k = 0; k < 8u; k++)
            trk_note_on(&trk[0], 45u + 4u * k, 100);
}
static void ref_block(int32_t *out, uint32_t n)
{
    inject_notes(nblk, ref_hang_blk);
    if (nblk == ref_fade) {
        tcr.fade2 = 1;
        tcr.fade_mask = (vmask_t)ref_fade_mask;
    }
    mix_block(out, n);
    record(out, ref_skip ? ref_skip[nblk] : 0);
}

/* the main loop's passes and the audio interrupt's half start, then the firmware's own mix_block */
static uint32_t park_t0, park_t1;
static void sim_block(int32_t *out, uint32_t n)
{
    uint32_t b = nblk;
    if (hang_blk == 0xFFFFFFFFu && __atomic_load_n(&hang_on, __ATOMIC_ACQUIRE))
        hang_blk = b;
    inject_notes(b, hang_blk);
    if (b % TCR_BLOCKS == 0) {
        uint32_t h = b / TCR_BLOCKS, t = BASE_TICKS + h * TCR_HALF_TICKS + clk_jump;
        if (fault == F_VLONG && tcs.pauses > jumps && tcr.open) {   /* the late job is let go before the calm period passes (its clock figures stay those of one job) */
            uint32_t want = tcr.lim + 100u;
            if ((int32_t)(want - raw_ticks()) > 0)
                __atomic_store_n(&tc_host_ticks, want, __ATOMIC_RELAXED);
            while (c1_live() && !tc_closed())
                cpu_relax();
        }
        if ((fault == F_LATE_POST || fault == F_LATE_RUN || fault == F_LATE_PRE || fault == F_STALE || fault == F_NOANS || fault == F_VLONG) && tcs.pauses > jumps) {
            jumps = tcs.pauses;
            clk_jump += TCR_CALM_TICKS;                 /* the calm period passes */
            t += TCR_CALM_TICKS;
        }
        if ((int32_t)(t - raw_ticks()) > 0)
            __atomic_store_n(&tc_host_ticks, t, __ATOMIC_RELAXED);
        if (fault == F_SILENT_LATE && thr_on && tcs.silent) {   /* the late core 1 goes on now, and the next half starts with it idle */
            __atomic_store_n(&release_silent, 1u, __ATOMIC_RELEASE);
            while (c1_live() && !tc_closed())
                cpu_relax();
        }
        if (thr_on && tc.up && !__atomic_load_n(&hang_on, __ATOMIC_ACQUIRE)) {   /* the heartbeat watch reads this clock as real time: a healthy core 1 has beaten since the watch's last pass (compared with tc.beat, the watch's own record) */
            uint32_t ms = raw_ticks() / MS_TICKS;
            if (ms - tc.beat_t > 50u)
                while (c1_live() && __atomic_load_n(&tc_mb.beat, __ATOMIC_RELAXED) == tc.beat)
                    cpu_relax();
        }
        tc_watch(raw_ticks() / MS_TICKS);               /* a main-loop pass: the safety net; a flash park of the pass before ends */
        tc_trim_poll();
        tc_half_begin(raw_ticks());
        if (fault == F_PARK && h == 60u)
            tc_flash_park();                            /* the flash driver: this half plays the job's voices on core 0 */
        if (fault == F_PARK_STUCK && !parked && tcs.silent) {
            parked = 1;
            divsel = 1u;                                /* (20 ms of ticks in single steps) */
            time_adj(0);
            park_t0 = raw_ticks();
            tc_flash_park();
            park_t1 = raw_ticks();
        }
    }
    sw_act(3);
    mix_block(out, n);
    if (__atomic_load_n(&hang_on, __ATOMIC_ACQUIRE) && tcr.open && c1own_hash(tcr.open_set) != c1own_at_hang)
        c1open_writes++;                                /* core 0 wrote the audio state of a voice of the hung job while the job was open (a note on a voice that had ended, say) */
#if LRRY_PIPELINE
    if (tcr.cj) {                                       /* the job closed in this block (posted in the block before): where core 0 is when it leaves its wait */
        int32_t lo = (int32_t)(raw_ticks() - tcr.lim), eo = (int32_t)(raw_ticks() - (tcr.t0 + TCR_HALF_TICKS));   /* never later than the limit, so never later than the half's end */
        R->lim_blocks++;
        R->lim_over += lo > 8;
        R->end_over += eo > 0;
        if (lo > (int32_t)R->lim_max_over)
            R->lim_max_over = (uint32_t)lo;
        R->nott += tcr.nott != 0u;
        R->dl_sum[b % TCR_BLOCKS] += tcr.dl - tcr.t0;
        R->dl_n[b % TCR_BLOCKS]++;
        R->lim_sum[b % TCR_BLOCKS] += tcr.lim - tcr.t0;
    }
    if (tcr.job) {
        R->posted++;
        R->c1hist[__builtin_popcount(tcr.c1set)]++;
        R->lohist[__builtin_popcount(lrj.mask & ~(uint32_t)tcr.c1set)]++;
        {
            static uint32_t pm, pc;                         /* last job's voices and core-1 set */
            uint32_t both = pm & lrj.mask;
            R->moved += __builtin_popcount(both & (pc ^ tcr.c1set));
            R->stayed += __builtin_popcount(both & ~(pc ^ tcr.c1set));
            pm = lrj.mask;
            pc = tcr.c1set;
        }
    }
    if (tcr.psil && b > 0 && b - 1u < MAXB)
        R->skip[b - 1u] |= tcr.psil;                    /* (the voices of the job closed here that were silent in the block before) */
#else
    if (tcr.job) {
        int32_t lo = (int32_t)(raw_ticks() - tcr.lim), eo = (int32_t)(raw_ticks() - (tcr.t0 + TCR_HALF_TICKS));   /* where core 0 is when it leaves the block's wait: never later than the limit, so never later than the half's end */
        R->lim_blocks++;
        R->lim_over += lo > 8;
        R->end_over += eo > 0;
        if (lo > (int32_t)R->lim_max_over)
            R->lim_max_over = (uint32_t)lo;
        R->nott += tcr.nott != 0u;
        R->posted++;
        R->c1hist[__builtin_popcount(tcr.c1set)]++;
        R->lohist[__builtin_popcount(lrj.mask & ~(uint32_t)tcr.c1set)]++;
        {
            static uint32_t pm, pc;                         /* last job's voices and core-1 set */
            uint32_t both = pm & lrj.mask;
            R->moved += __builtin_popcount(both & (pc ^ tcr.c1set));
            R->stayed += __builtin_popcount(both & ~(pc ^ tcr.c1set));
            pm = lrj.mask;
            pc = tcr.c1set;
        }
        R->dl_sum[b % TCR_BLOCKS] += tcr.dl - tcr.t0;
        R->dl_n[b % TCR_BLOCKS]++;
        R->lim_sum[b % TCR_BLOCKS] += tcr.lim - tcr.t0;
    }
#endif
    record(out, tcr.sil);
}

/* ------------------------------------------------------------ a run --- */
static void fill_res(uint64_t h)
{
    R->hash = h;
    R->s = tcs;
    R->parks = tc.parks;
    R->up = tc.up;
    R->failed = tc.failed;
    R->strike = tc.strike;
    R->nv_last = tc_nv.last;
    R->reread = TC_LD(&tc_mb.reread);
    R->bad_w = TC_LD(&tc_mb.bad);
    R->misread = TC_LD(&tc_mb.misread);
    R->c1_jobs = c1_jobs;
    R->own_checks = own_checks;
    R->own_writes = own_writes;
    R->own_timeouts = own_timeouts;
    R->c1own_checks = c1own_checks;
    R->c1own_writes = c1own_writes;
    R->c1open_writes = c1open_writes;
    R->nblk = nblk;
#if LRRY_PIPELINE
    R->pp_copies = tcr.pp_copies;
#endif
    R->fade_mask = tcr.fade_mask;
    R->hang_blk = hang_blk;
    R->park_us = (park_t1 - park_t0) / TC_TICKS_PER_US;
}
/* one run in a child process: kind -1 the reference (core 1 never started), -2 the reference with the blocks of ref_skip silent, else the injected fault */
static void child_run(uint32_t id, int kind)
{
    uint64_t h;
    if (kind == -1 || kind == -2) {
        h = scn_run(id, ref_block, 0);
        fill_res(h);
        return;
    }
    fault = kind;
    tc_host_ld_hook = ld_hook;
    tc_host_st_hook = st_hook;
    time_adj(0);
    __atomic_store_n(&tc_host_ticks, BASE_TICKS, __ATOMIC_RELAXED);
    ahead_left = 1;
    tc_host_c1_started = start_core1;
    if (fault == F_NOANSWER)
        tc_host_start_ok = 0;
    if (fault != F_UNARMED)
        tc_arm();
    tc_boot(1u, 0, 0);
    while (thr_on && !__atomic_load_n(&c1_ready, __ATOMIC_ACQUIRE))
        cpu_relax();                                    /* (core 1 is in its loop before the first block) */
    h = scn_run(id, sim_block, 0);
    __atomic_store_n(&quit, 1u, __ATOMIC_RELEASE);
    if (thr_on)
        pthread_join(thr, 0);
    fill_res(h);
}
static void run(uint32_t id, int kind, res_t *out)
{
    pid_t pid;
    int st = 0;
    memset(R, 0, sizeof *R);
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        child_run(id, kind);
        _exit(0);
    }
    waitpid(pid, &st, 0);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0))
        R->hash = 0;
    *out = *R;
}

/* ------------------------------------------------------------ the fallback fade (item 11) --- */
/* eight notes sound, the number drops to 3 (the budget), then the jobs pause (a very late block) or core 1 stops (a wrong check value): the notes above the number fade out at once, in the next
 * block, the released / oldest first, with no step in the output over 1.5 x the calm before it (budget_test.c (d)'s rule). Reports through R */
static int trim_child(int how)
{
    const uint32_t frames = 2u * FS;
    int32_t *L = calloc(frames + CTL, sizeof *L), o[2 * CTL], calm = 1, a = 0;
    uint32_t f, i, at = 0, after = 99, young = 0;
    scn_setup(SC_PLAIN);
    trk[0].p[P_REL] = 120;
    bnotes[0] = bnotes1[0] = 0;
    fault = how ? F_TRIMSTOP : F_NONE;
    tc_host_ld_hook = ld_hook;
    tc_host_st_hook = st_hook;
    time_adj(0);
    __atomic_store_n(&tc_host_ticks, BASE_TICKS, __ATOMIC_RELAXED);
    tc_host_c1_started = start_core1;
    tc_arm();
    tc_boot(1u, 0, 0);
    while (thr_on && !__atomic_load_n(&c1_ready, __ATOMIC_ACQUIRE))
        cpu_relax();
    for (f = 0; f < frames; f += CTL) {
        if (f == 0)
            for (i = 0; i < 8u; i++)
                trk_note_on(&trk[0], 48u + 4u * i, 100);
        if (f == (uint32_t)(0.8 * FS) / CTL * CTL)
            bnotes[0] = bnotes1[0] = 3;                 /* the budget drops (both rules' numbers): nothing is cut yet */
        if (f == (uint32_t)(1.0 * FS) / CTL * CTL)
            trim_job = tc.job + 3u;                     /* the third job from now is the late one (or the one written wrong) */
        sim_block(o, CTL);
        for (i = 0; i < CTL; i++)
            L[f + i] = o[2 * i];
        if (!at && tcs.trims)
            at = f;
        if (at && f == at + 8u * CTL) {
            after = young = 0;
            for (i = 0; i < NVOICE; i++) {
                after += trk[0].v[i].active && trk[0].v[i].stage != 4u;
                young += trk[0].v[i].active && trk[0].v[i].stage != 4u && trk[0].v[i].note >= 48u + 4u * 5u;
            }
        }
    }
    __atomic_store_n(&quit, 1u, __ATOMIC_RELEASE);
    if (thr_on)
        pthread_join(thr, 0);
    if (at) {
        for (i = at - FS / 4u; i < at - 2u * CTL; i++)
            calm = abs(L[i] - L[i - 1]) > calm ? abs(L[i] - L[i - 1]) : calm;
        for (i = at; i < at + 3u * CTL; i++)
            a = abs(L[i] - L[i - 1]) > a ? abs(L[i] - L[i - 1]) : a;
    }
    free(L);
    R->hash = at ? 1u : 0u;
    R->s = tcs;
    R->failed = tc.failed;
    R->c1_jobs = (uint32_t)a;
    R->park_us = (uint32_t)calm;
    R->nblk = after;
    R->own_checks = young;
    return 1;
}

int main(int argc, char **argv)
{
    uint32_t s, s0 = 0, s1 = SC_N, f, fi;
    int nomut = 0, tsan = 0, audit = 0, k, only = -1, nosweep = 0, v, nvar;
    static res_t ref, r, rs;
    tc_stat_t tot[F_N];
    uint32_t ntot[F_N];
    for (k = 1; k < argc; k++) {
        if (argv[k][0] != '-')
            s0 = (uint32_t)atoi(argv[k]), s1 = s0 + 1u;
        else if (!strcmp(argv[k], "-nomut"))
            nomut = 1;
        else if (!strcmp(argv[k], "-audit"))
            audit = nomut = 1;                          /* (all scenes, the runs `none` and `conc` only: the ownership audit and the fast equality of tests/twocore_mut.sh) */
        else if (!strcmp(argv[k], "-nosweep"))
            nosweep = 1;                                /* (the mutant check: the sweep is run only for the mutants that need it) */
        else if (!strcmp(argv[k], "-tsan"))
            tsan = nomut = 1;
        else if (!strcmp(argv[k], "-only") && k + 1 < argc)
            only = atoi(argv[++k]);                     /* (one fault only, by its number in FNAME: a debugging aid) */
    }
    R = mmap(0, sizeof *R, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    memset(tot, 0, sizeof tot);
    memset(ntot, 0, sizeof ntot);
#if LRRY_PIPELINE
    {   /* cores-pipeline: THE CHOOSER'S OBJECTIVE. Core 0's side holds its fixed work (the idle mix, the part's fixed work, every voice's control, the hand-over) and its own voices, core 1's side its voices: the cycle is the
         * longer side (voice.c lrry_load). Random masks and weights against a brute force over every set of at most max1 voices; the equal-weight case; each voice on exactly one core. */
        uint32_t t, bad_sub = 0, bad_max = 0, nondet = 0, unstable = 0, nopt = 0, ntot2 = 0, rs = 99, k8 = 0, n1_8 = 0;
        double worst = 1.0;
        for (t = 0; t < 3000u; t++) {
            uint16_t w[NVOICE];
            uint32_t mask, prev, c1, c1b, set, best, i, v, n;
            rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; mask = rs & ((1u << NVOICE) - 1u);
            rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; prev = rs & ((1u << NVOICE) - 1u);
            for (i = 0; i < NVOICE; i++) {
                rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
                w[i] = (uint16_t)(t % 3u == 0 ? 400u : 150u + rs % 900u);
            }
            c1 = lrry_split_choose(mask, w, prev);
            c1b = lrry_split_choose(mask, w, prev);
            nondet += c1 != c1b;
            bad_sub += (c1 & ~mask) != 0;
            bad_max += (uint32_t)__builtin_popcount(c1) > LRRY_SP.max1;
            n = (uint32_t)__builtin_popcount(mask);
            if (n < LRRY_SP.min_total)
                continue;
            unstable += lrry_split_choose(mask, w, c1) != c1;
            v = lrry_cost(mask, c1, w);
            best = lrry_cost(mask, 0, w);
            for (set = 0; set < (1u << NVOICE); set++)
                if ((set & ~mask) == 0 && (uint32_t)__builtin_popcount(set) <= LRRY_SP.max1) {
                    uint32_t x = lrry_cost(mask, set, w);
                    if (x < best)
                        best = x;
                }
            ntot2++;
            nopt += v == best;
            if (best && (double)v / best > worst)
                worst = (double)v / best;
        }
        {   /* eight typical 128 voices (715 units): core 1 gets more than half (core 0 also does the mix, master and the control) */
            uint16_t w[NVOICE];
            uint32_t i;
            for (i = 0; i < NVOICE; i++)
                w[i] = 715;
            k8 = lrry_split_choose(0xFFu, w, 0);
            n1_8 = (uint32_t)__builtin_popcount(k8);
        }
        {
            char wtxt[300];
            expect("pipeline chooser: the set for core 1 is inside the sounding voices and never larger than max1", bad_sub == 0 && bad_max == 0);
            expect("pipeline chooser: the same input gives the same set, and its own answer fed back as the previous set is left alone", nondet == 0 && unstable == 0);
            snprintf(wtxt, sizeof wtxt, "pipeline chooser: the longer core's load is the brute-force optimum in %u of %u random cases, never more than %.3f x it", nopt, ntot2, worst);
            expect(wtxt, worst < 1.13 && nopt * 10u >= ntot2 * 7u);
            snprintf(wtxt, sizeof wtxt, "pipeline chooser: eight equal typical voices: core 1 gets %u (more than half: core 0 also has the mix, the master and every voice's control), core 0 %u", n1_8, 8u - n1_8);
            expect(wtxt, n1_8 == 5u);
            printf("twocore: %s\n", wtxt);
            printf("twocore: pipeline chooser: brute-force optimum in %u of %u random cases, worst %.3f x\n", nopt, ntot2, worst);
        }
    }
#endif
    for (s = s0; s < s1; s++) {
        char what[300];
        uint64_t hr;
        run(s, -1, &ref);
        hr = ref.hash;
        snprintf(what, sizeof what, "scene %u (%s) is not silent", s, SCN[s]);
        expect(what, hr != 0 && ref.s.own == ref.nblk);
        {   /* cores-pipeline: the reference (core 1 never started) is the PLAIN one-core program's output, delayed by exactly one block when LRRY_PIPELINE is on (tests/lrry_split_test.c writes the plain samples) */
            static int32_t plain[(size_t)MAXB * 2u * CTL];
            const char *dir = getenv("LRRY_PLAIN_DIR");
            char fn[400];
            FILE *fp = 0;
            if (dir) {
                snprintf(fn, sizeof fn, "%s/s%u.bin", dir, s);
                fp = fopen(fn, "rb");
            }
            if (fp && fread(plain, sizeof plain[0], (size_t)MAXB * 2u * CTL, fp) == (size_t)MAXB * 2u * CTL) {
                size_t d = LRRY_PIPELINE ? 2u * CTL : 0, i, first = 0;
                for (i = 0; i < d; i++)
                    first |= (size_t)(ref.out[i] != 0);
                snprintf(what, sizeof what, "scene %u (%s): the one-core reference equals the plain one-core program's output %s, every sample (%u blocks compared)", s, SCN[s],
                         LRRY_PIPELINE ? "delayed by exactly one block (the first block silent)" : "unchanged", MAXB - (LRRY_PIPELINE ? 1u : 0u));
                if (expect(what, !first && !memcmp(ref.out + d, plain, (sizeof plain) - d * sizeof plain[0])))
                    n_shift_runs++;
            } else if (!shift_warned) {
                shift_warned = 1;
                printf("twocore: NOTE the plain one-core program's samples were not given (LRRY_PIPELINE only: env LRRY_PLAIN_DIR, written by tests/lrry_split_test.c): the shifted check is skipped\n");
            }
            if (fp)
                fclose(fp);
        }
        printf("twocore: scene %u (%s): reference hash %016llx, %u blocks\n", s, SCN[s], (unsigned long long)hr, ref.nblk);
        for (fi = 0; fi < F_SWEEP + (nosweep ? 0u : SW_N); fi++) {      /* (the faults in order, then the switch sweep's 48 variants, all numbered F_SWEEP) */
            int equal_ref = 0;                              /* (set after the run: the output must equal the reference whenever no block of the job's voices was dropped) */
            const tc_stat_t *c;
            uint32_t sum;
            f = fi < F_SWEEP ? fi : F_SWEEP;
            if (f == F_SWEEP)
                sw_set((int)(fi - F_SWEEP));
            if (f == F_SWEEP && !LRRY_PIPELINE && sw_pt != 3)
                continue;                               /* (the plain runner has no stage points: a switch can only come between two blocks) */
            if (audit && f != F_NONE && f != F_CONC)
                continue;
            if (only >= 0 && (int)f != only)
                continue;
            if (f == F_SHARED && (nomut || s != SC_PLAIN))
                continue;
            if (tsan && (f == F_HANG_PRE || f == F_HANG_POST || f == F_PARK_STUCK || f == F_SILENT_LATE || f == F_AHEAD))
                continue;                               /* (a window in which core 0 renders while core 1 still holds the voices is not race free by design: the silent block; AHEAD: core 0 takes over while the simulated halt leaves core 1's thread running the job: the unit's halt stops it) */
            run(s, (int)f, &r);
            c = &r.s;
            equal_ref = f != F_HANG_PRE && f != F_HANG_POST && f != F_PARK_STUCK && f != F_SILENT_LATE && f != F_SHARED && f != F_VLONG && c->silent == 0 && !(f == F_SWEEP && sw_kind == SW_HANGSTOP);
            snprintf(what, sizeof what, "scene %u %s: core 0 changed nothing of core 1's state (the job's voices, COMB lines) while a job was open, however late it was (%u jobs)", s, FNAME[f], r.c1open_writes);
            expect(what, r.c1open_writes == 0);
            if (f != F_SWEEP || fi == F_SWEEP || (fi - F_SWEEP) % 16u == 0)
            printf("twocore:   %-11s blocks %4u in time %4u late %3u very late %3u silent %3u (waiting %3u) own %4u refused %u no answer %u | pauses %u (fault %u run %u window %u late %u) very late without pause %u resumes %u | rereads %u parks %u | "
                   "core 1 jobs %4u | no take-over %u | up %u strike %u record %u failed %u bad %u | %016llx\n",
                   FNAME[f], c->blocks, c->in_time, c->late, c->vlate, c->silent, c->wait, c->own, c->refused, c->noanswer, c->pauses, c->pause_fault, c->pause_run, c->pause_win, c->pause_late,
                   c->vlate_ok, c->resumes, r.reread, r.parks, r.c1_jobs,
                   r.nott, r.up, r.strike, r.nv_last, r.failed, r.bad_w, (unsigned long long)r.hash);
            if (f == F_NONE) {
                uint32_t k2;
                printf("twocore:   split (jobs %u): voices on core 1:", r.posted);
                for (k2 = 0; k2 <= 6u; k2++)
                    printf(" %u:%u", k2, r.c1hist[k2]);
                printf(" | on core 0:");
                for (k2 = 0; k2 <= 8u; k2++)
                    printf(" %u:%u", k2, r.lohist[k2]);
                printf(" | voices that changed core %u, kept their core %u\n", r.moved, r.stayed);
#if LRRY_PIPELINE
                printf("twocore:   scene %u: the patch part of the hand-over copy was made %u times for %u jobs (the rest reused it)\n", s, r.pp_copies, r.posted);
#endif
                printf("twocore:   deadlines (mean, microseconds after the half's start, of the posted jobs; the half is %u):", TCR_HALF_TICKS / TC_TICKS_PER_US);
                for (k2 = 0; k2 < TCR_BLOCKS; k2++)
                    printf(" block %u: %u (%u jobs)", k2, r.dl_n[k2] ? (uint32_t)(r.dl_sum[k2] / r.dl_n[k2]) / TC_TICKS_PER_US : 0u, r.dl_n[k2]);
                printf("\n");
            }
            sum = c->in_time + c->late + c->vlate + c->refused + c->own + c->wait;
            snprintf(what, sizeof what, "scene %u %s: every block is counted once (blocks %u = %u)", s, FNAME[f], c->blocks, sum);
            expect(what, c->blocks == r.nblk && sum == c->blocks);
            snprintf(what, sizeof what, "scene %u %s: the pauses (%u) add up by reason (fault %u + run %u + window %u + late %u), and every very late block (%u) either paused, was a fault (silent %u at most) or is counted without a pause (%u)", s, FNAME[f], c->pauses,
                     c->pause_fault, c->pause_run, c->pause_win, c->pause_late, c->vlate, c->silent, c->vlate_ok);
            expect(what, c->pauses == c->pause_fault + c->pause_run + c->pause_win + c->pause_late && c->vlate_ok <= c->vlate && c->vlate_ok + c->pause_run + c->pause_win + c->silent + c->refused + c->noanswer >= c->vlate);
            snprintf(what, sizeof what, "scene %u %s: core 0 never waited past its wait limit nor past the half's end (%u of %u blocks with a job left the wait later than the limit, %u later than the half's end, the largest overrun %u ticks)", s, FNAME[f],
                     r.lim_over, r.lim_blocks, r.end_over, r.lim_max_over);
            expect(what, r.lim_over == 0 && r.end_over == 0);
            if (equal_ref) {
                snprintf(what, sizeof what, "scene %u %s: all %u output samples equal the one-core reference, every one (memcmp)", s, FNAME[f], r.nblk * 2u * CTL);
                if (expect(what, r.hash == hr && !memcmp(r.out, ref.out, sizeof r.out)))
                    n_eq_runs++, n_eq_blocks += r.nblk;
            }
            tot[f].blocks += c->blocks, tot[f].in_time += c->in_time, tot[f].late += c->late, tot[f].vlate += c->vlate, tot[f].silent += c->silent, tot[f].own += c->own;
            tot[f].refused += c->refused, tot[f].noanswer += c->noanswer, tot[f].pauses += c->pauses, tot[f].resumes += c->resumes, tot[f].wait += c->wait;
            ntot[f] += r.reread;
            switch (f) {
            case F_NONE:
                snprintf(what, sizeof what, "scene %u none: no fault, no late block, no refusal, no strike", s);
                expect(what, c->late == 0 && c->vlate == 0 && c->refused == 0 && !r.strike && r.up && !r.failed && c->pauses == 0);
                printf("twocore:   scene %u none: jobs posted with no take-over (the cost model says core 0 could not play the voices in time): %u of %u\n", s, r.nott, r.lim_blocks);
                snprintf(what, sizeof what, "scene %u none: core 1 played its voices in time in %u of %u blocks (jobs it ran: %u)", s, c->in_time, c->blocks, r.c1_jobs);
                expect(what, c->in_time >= 100u && r.c1_jobs >= c->in_time && c->in_time + c->own + c->wait == c->blocks);
                snprintf(what, sizeof what, "scene %u none: core 1's job changed nothing of core 0's state (track, block scratch, accumulators, patch, voices 0..3, their COMB lines, the control block): %u of %u jobs wrote, %u gate timeouts", s, r.own_writes, r.own_checks, r.own_timeouts);
                expect(what, r.own_writes == 0 && r.own_timeouts == 0 && r.own_checks >= c->in_time);
                snprintf(what, sizeof what, "scene %u none: core 0 changed nothing of core 1's state (the job's voices, their COMB lines) between the post and the job's start: %u of %u jobs", s, r.c1own_writes, r.c1own_checks);
                expect(what, r.c1own_writes == 0 && r.c1own_checks >= c->in_time);
                break;
            case F_CONC:
                snprintf(what, sizeof what, "scene %u conc: both cores really at once: in time %u, no fault", s, c->in_time);
                expect(what, c->in_time >= 100u && c->late + c->vlate + c->refused == 0 && !r.strike && r.up);
                break;
            case F_TORN1:
                snprintf(what, sizeof what, "scene %u torn1: re-read and run, counted (%u), no refusal", s, r.reread);
                expect(what, r.reread > 0 && c->refused == 0 && !r.strike && r.bad_w == 0);
                break;
            case F_TORNP:
                snprintf(what, sizeof what, "scene %u tornp: refused, a strike (BADJOB), core 1 held, core 0 plays the job's voices from then on", s);
                expect(what, c->refused == 1 && r.failed == TCR_BADJOB && r.strike && !r.up && c->own > 0 && r.nv_last == TCR_BADJOB);
                break;
            case F_LATE_PRE:
                if (!r.nott) {
                    snprintf(what, sizeof what, "scene %u late_pre: the late path (take, yld) was taken and counted (%u), core 0 played the voices itself, no strike", s, c->late);
                    expect(what, c->late > 0 && !r.strike && r.up && c->refused == 0 && (LRRY_PIPELINE ? c->vlate * 20u <= c->late : c->vlate == 0));   /* (PIPELINE: the clock a late core 1 moved can make the next job a very late one, a rare single block) */
                }
                break;
            case F_LATE_POST:
                if (!r.nott) {
                    snprintf(what, sizeof what, "scene %u late_post: single very late blocks that finished in their half are counted (%u) and pause nothing (pauses %u, without a pause %u), no silent block, no strike", s, c->vlate, c->pauses, c->vlate_ok);
                    expect(what, c->vlate > 0 && c->silent == 0 && c->pauses == 0 && c->vlate_ok == c->vlate && !r.strike && r.up);
                }
                break;
            case F_VOFF:
                if (!r.nott) {
                    snprintf(what, sizeof what, "scene %u voff: with the BENCHT4 defaults %u very late blocks (4 in a row, 8 within the window among them) paused nothing (pauses %u, without a pause %u), no silent block, no strike", s, c->vlate, c->pauses, c->vlate_ok);
                    expect(what, c->vlate >= 10u && c->pauses == 0 && c->vlate_ok == c->vlate && c->silent == 0 && !r.strike && r.up);
                }
                break;
            case F_NOANS:
                snprintf(what, sizeof what, "scene %u noans: a core 1 that answers the take later than 100 us is a real fault: no answer %u, then yld, a fault pause (%u of %u pauses), core 0 played the voices (output equal where nothing was silent: silent %u), no strike, nothing counted as without a pause (%u)", s, c->noanswer, c->pause_fault, c->pauses, c->silent, c->vlate_ok);
                expect(what, c->noanswer > 0 && c->pauses > 0 && c->pause_fault == c->pauses && c->vlate_ok == 0 && (r.nott || c->silent == 0) && !r.strike && r.up);
                break;
            case F_VLONG:
                snprintf(what, sizeof what, "scene %u vlong: a job that is not done by the wait limit makes the job's voices silent for the block (silent %u) and pauses the jobs as a fault (%u of %u pauses); core 0 left every wait at the limit (largest overrun %u ticks, %u of %u blocks over), none past the half's end (%u); no strike", s, c->silent, c->pause_fault, c->pauses, r.lim_max_over, r.lim_over, r.lim_blocks, r.end_over);
                expect(what, c->silent > 0 && c->pauses > 0 && c->pause_fault == c->pauses && c->vlate_ok == 0 && r.lim_over == 0 && r.end_over == 0 && !r.strike);
                break;
            case F_LATE_RUN:
                snprintf(what, sizeof what, "scene %u late_run: a run of late blocks paused the jobs (%u) and they resumed (%u), no strike (blocks with no take-over %u)", s, c->pauses, c->resumes, r.nott);
                expect(what, (c->late >= 4u || r.nott) && c->pauses > 0 && (r.nott || c->resumes + 1u >= c->pauses) && !r.strike && r.up);
                break;
            case F_BADCHK:
                snprintf(what, sizeof what, "scene %u badchk: refused, a strike (BADJOB), core 0 plays the job's voices", s);
                expect(what, c->refused == 1 && r.failed == TCR_BADJOB && r.strike && !r.up);
                break;
            case F_STALE:
                snprintf(what, sizeof what, "scene %u stale: a stale done word took the deadline path (very late %u), done after, no strike", s, c->vlate);
                expect(what, c->vlate > 0 && c->refused == 0 && !r.strike && r.up);
                break;
            case F_AHEAD:
                snprintf(what, sizeof what, "scene %u ahead: a done word ahead is BADWORD, a strike, core 1 held", s);
                expect(what, r.failed == TCR_BADWORD && r.strike && !r.up && c->refused == 1);
                break;
            case F_BADSNAP:
                snprintf(what, sizeof what, "scene %u badsnap: the block copy that fits no hash is refused, a strike (BADJOB), core 0 plays the job's voices", s);
                expect(what, c->refused == 1 && r.failed == TCR_BADJOB && r.strike && !r.up);   /* (core 0 may see bad_job before core 1 wrote bad: it is held at once) */
                break;
            case F_HANG_PRE:
            case F_HANG_POST:
            case F_PARK_STUCK:
            hang_stop: {
                uint32_t first = 0, last = 0, nsk = 0, i, pre_ok = 1;
                static vmask_t sk[MAXB];
                res_t *ps = &rs;
                for (i = 0; i < r.nblk && i < MAXB; i++)
                    if (r.skip[i]) {
                        if (!nsk)
                            first = i;
                        last = i;
                        nsk++;
                    }
                snprintf(what, sizeof what, "scene %u %s: core 1 stopped (%s), a strike, the job's voices silent in %u blocks (%u counted), then core 0 played them", s, FNAME[f],
                         f == F_PARK_STUCK ? "FLASH" : "HANG", nsk, c->silent);
                expect(what, nsk > 0 && nsk == c->silent && r.failed == (f == F_PARK_STUCK ? TCR_FLASH : TCR_HANG) && r.strike && !r.up && last + 1u < r.nblk && c->own > 0);   /* (not contiguous: a block where none of the open job's voices sounds is not silent) */
                for (i = 0; i < first * 2u * CTL; i++)
                    pre_ok &= r.out[i] == ref.out[i];
                snprintf(what, sizeof what, "scene %u %s: the output equals the reference up to the hang (block %u)", s, FNAME[f], first);
                expect(what, pre_ok);
                memcpy(sk, r.skip, sizeof sk);
                ref_skip = sk;
                ref_fade = f == F_HANG_PRE ? 0xFFFFFFFFu : last + 1u;
                ref_fade_mask = r.fade_mask;
                ref_hang_blk = r.hang_blk;
                run(s, -2, ps);
                ref_skip = 0;
                ref_hang_blk = 0xFFFFFFFFu;
                ref_fade = 0xFFFFFFFFu;
                snprintf(what, sizeof what, "scene %u %s: the whole output equals the one-core run with exactly those blocks silent%s, every sample", s, FNAME[f], f == F_HANG_PRE ? "" : " (and the fade)");
                if (expect(what, ps->hash == r.hash && !memcmp(ps->out, r.out, sizeof r.out)))
                    n_eq_runs++, n_eq_blocks += r.nblk;
                if (f == F_PARK_STUCK) {
                    snprintf(what, sizeof what, "scene %u park_stuck: FLASH after 20 ms of simulated time (%u us)", s, r.park_us);
                    expect(what, r.park_us >= 20000u && r.park_us < 20100u);
                } else {
                    snprintf(what, sizeof what, "scene %u %s: the stop came from the 100 ms heartbeat watch (noanswer %u)", s, FNAME[f], c->noanswer);
                    expect(what, f == F_HANG_PRE ? c->noanswer > 0 : 1);
                }
                break;
            }
            case F_PARK:
                snprintf(what, sizeof what, "scene %u park: a flash park in the middle of play, counted (%u), the job's voices played by core 0 meanwhile (own %u), core 1 still up", s, r.parks, c->own);
                expect(what, r.parks == 1u && c->own >= 3u && r.up && !r.strike);
                break;
            case F_SILENT_LATE:
                snprintf(what, sizeof what, "scene %u silent_late: the job's voices silent in %u blocks, the jobs paused (%u) and resumed (%u), no strike", s, c->silent, c->pauses, c->resumes);
                expect(what, c->vlate > 0 && c->silent > 0 && c->pauses > 0 && c->pause_fault == c->pauses && !r.strike);
                break;
            case F_SWEEP:
                snprintf(what, sizeof what, "scene %u sweep %s at block position %d, moment %d: %s, output equal to the reference (no block dropped or repeated)", s,
                         sw_kind == SW_PARK ? "park" : sw_kind == SW_PAUSE ? "pause / resume" : "stop", sw_ph, sw_pt,
                         sw_kind == SW_PARK ? "parked and released" : sw_kind == SW_PAUSE ? "paused and resumed" : "core 1 held");
                if (sw_kind == SW_PARK)
                    expect(what, r.parks >= 1u && r.up && !r.strike && c->silent == 0);
                else if (sw_kind == SW_PAUSE)
                    expect(what, c->pauses >= 1u && c->resumes >= 1u && r.up && !r.strike && c->silent == 0);
                else if (sw_kind == SW_STOP)
                    expect(what, !r.up && r.failed == TCR_BADJOB && c->silent == 0);
                else
                    goto hang_stop;                     /* a core 1 that hung in a job and is stopped by the watch at this moment: the same checks as the hang runs */
                break;
            case F_UNARMED:
                snprintf(what, sizeof what, "scene %u unarmed: one core, every block on core 0 (own %u), the record says none (%u)", s, c->own, r.nv_last);
                expect(what, !r.up && c->own == c->blocks && r.nv_last == TCR_NONE && !r.failed && r.c1_jobs == 0);
                break;
            case F_NOANSWER:
                snprintf(what, sizeof what, "scene %u noanswer: the start failed, one core, the record says NOANSWER (%u), a strike", s, r.nv_last);
                expect(what, !r.up && c->own == c->blocks && r.nv_last == TCR_NOANSWER && r.failed == TCR_NOANSWER && r.strike);
                break;
            case F_SHARED:
                snprintf(what, sizeof what, "self-check: core 1 writing a patch value of core 0's changes the output (the test sees a shared write)");
                expect(what, r.hash != hr);
                break;
            default:
                break;
            }
        }
    }
    printf("twocore: totals over %u scenes (blocks, in time, late, very late, silent, own, refused, pauses, resumes):\n", s1 - s0);
    for (f = 0; f < F_N; f++)
        if (tot[f].blocks)
            printf("twocore:   %-11s %6u %6u %5u %5u %5u %6u %3u %4u %4u  rereads %u\n", FNAME[f], tot[f].blocks, tot[f].in_time, tot[f].late, tot[f].vlate, tot[f].silent, tot[f].own,
                   tot[f].refused, tot[f].pauses, tot[f].resumes, ntot[f]);
    if (!audit)
        expect("the late path was taken and counted in some blocks (late_pre)", tot[F_LATE_PRE].late > 0);
    expect("several hundred blocks per scene went through the hand-over (none)", tot[F_NONE].in_time >= 100u * (s1 - s0));
    if (!tsan && !audit) {
        /* the fallback fade (item 11) */
        int how;
        for (how = 0; how < 2; how++) {
            pid_t pid;
            int st = 0;
            res_t t;
            memset(R, 0, sizeof *R);
            fflush(stdout);
            pid = fork();
            if (pid == 0)
                _exit(trim_child(how) ? 0 : 1);
            waitpid(pid, &st, 0);
            t = *R;
            {
                char what[300];
                int32_t a = (int32_t)t.c1_jobs, calm = (int32_t)t.park_us;
                snprintf(what, sizeof what, "fade at once (%s): 8 notes held, the number drops to 3, then %s: %u fade out in the next block, %u sound 8 blocks later (the %u youngest kept), largest step %d vs %d before (%.2f x, limit 1.50 x)",
                         how ? "core 1 stops" : "the jobs pause", how ? "a wrong check value stops core 1" : "a very late block pauses the jobs", t.s.trims, t.nblk, t.own_checks, a, calm,
                         calm ? (double)a / calm : 0.0);
                expect(what, WIFEXITED(st) && WEXITSTATUS(st) == 0 && t.hash == 1u && t.s.trims == 5u && t.nblk <= 3u && t.own_checks == 3u && a * 2 <= calm * 3 &&
                                 (how ? t.failed == TCR_BADJOB : t.s.pauses > 0));
            }
        }
    }
    printf("twocore: %u runs equal to the one-core output sample for sample (%u blocks, %u samples compared)\n", n_eq_runs, n_eq_blocks, n_eq_blocks * 2u * CTL);
    printf("twocore: %u scenes' one-core reference equal to the plain one-core program's output %s\n", n_shift_runs, LRRY_PIPELINE ? "delayed by exactly one block" : "(no delay)");
    printf("twocore: %d checks, %d failed\n", ncheck, bad);
    return bad ? 1 : 0;
}
