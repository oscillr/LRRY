/* SPDX-License-Identifier: GPL-3.0-only */
/* L/RRY phase 4.9: HELD NOTES AT THE SHOWN NUMBER (owner, lrry-0.1.0-test1: "the unit showed two voices; holding one note and playing a second, the second was cut after one or two seconds").
 * The real audio interrupt (audio.c fm1_alnk0_irq: the overload rule shed_check / shed_voice) around the real mix_block with LRRY_TWOCORE on (the runner: deadline, post, wait, take, late, very late,
 * silent, pause, trim), core 1 a pthread running the real tc_c1_step and tc_job_run, and the main loop's passes between halves (tc_watch, tc_trim_poll, budget_poll).
 * THE CLOCK IS A COST CLOCK (an ESTIMATE, nothing measured on the unit): core 0's clock moves by what the work costs in budget.c's own rows, and core 1's job ends at its post + its cost:
 *   a voice                 lrry_weights (the voice as it sounds this block: a filter whose cutoff is at the open end does not run) x BD_LR_MARGIN 1.15 (what budget.c charges) x DEV (the unit's cost
 *                           over the estimate; 1.00 = the unit costs exactly what the budget charges); its control share (BD_LR_CTL_PCT) is core 0's before the post, the rest is the audio of whichever core plays it
 *   the hand-over           BD_LR_HO0 on core 0 at the post, BD_LR_HO1 at the head of core 1's job (x DEV)
 *   the part and the globals part_fixed + global_cost after the block's wait (1.00: L/RRY's rows measured on the unit)
 *   the peak                + PEAK points of the half in two halves in a row every PEAK_EVERY halves (budget.h: the 6 points of measured steady peak over the average the limit was set around)
 * A half starts at its boundary (or late, when the last one overran). Per stress patch: the budget's shown number N is read (part_notes_shown, and the allocator's trk_nvoice must equal it), N notes are started
 * 0.5 s apart (the second one higher than the first: the owner's case) and held 3 s after the last; every one must still sound, nothing shed (shed_count), faded (voice_kills), silent, paused or trimmed.
 * Runs at DEV 1.00 and at DEV 1.25. Build (from lrry, generated headers in build/gen):
 *   cc -O1 -w -DHOST_MIX_FOLLOWS_MASTER=1 -DLRRY_TEST_MODE=1 -Ibuild/gen_test -Ibuild/gen -Ifirmware/src -Itests -DNPART=1 -DNSHOWN=1 -DLRRY_VERSION_SHOWN='"x"' -o /tmp/vh tests/lrry_voice_hold_test.c -lm -lpthread
 * Argument -sweep: also prints, for each patch with two or more shown, the DEV at which a held note is first cut and when (no checks). */
#define LRRY_TWOCORE 1
#define TC_AUTO_ARM 0
#define TC_JOB_RUN sim_job
#include <stdint.h>
static void sim_job(uint32_t arg);
#define LRRY_MASK_HOOK(ti, mp) hold_hook(ti, mp)
static void hold_hook(uint32_t ti, uint32_t *mask);
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "lrry_scn.h"
#define FM1_AUDIO_HALF 0x80u
#define FM1_TICKS_PER_US 24u                            /* (the same simulated clock as the runner's TC_TICKS) */
static volatile uint32_t t5_nested_ticks;
static uint32_t fm1_ticks(void);
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0u; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t p) { (void)b; (void)n; (void)isr; (void)p; }
void isr_alnk0(void) {}
#define LRRY_UAC 1
static void uac_tap(const int32_t *out, uint32_t n) { (void)out; (void)n; }
static void uac_render_start(void) {}
#include "../firmware/src/audio.c"
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? 1023 : 620; }
#include "../firmware/src/master.c"
#include <sys/mman.h>
#include <pthread.h>
#include <sched.h>

#define BASE_TICKS 24000u
#define MS_TICKS (TC_TICKS_PER_US * 1000u)
#define PEAK 600u                                       /* 6 points of the half (1/100 %) */
#define PEAK_EVERY 128u                                 /* two peak halves in a row every 128 halves (0.37 s) */
#define HALVES_PER_S ((uint32_t)(FS / HALF_FRAMES))

static int nfail, ncheck;
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("voice_hold: %-150s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}

/* ------------------------------------------------------------------ the cost clock --- */
static uint32_t dev_pct = 100u;                         /* DEV in percent */
static inline uint32_t raw_ticks(void) { return __atomic_load_n(&tc_host_ticks, __ATOMIC_RELAXED); }
static inline void add_ticks(uint32_t d) { __atomic_store_n(&tc_host_ticks, raw_ticks() + d, __ATOMIC_RELAXED); }
static uint32_t u_ticks(uint64_t u) { return (uint32_t)(u * TCR_BLOCK_TICKS / 10000u); }   /* 1/100 % of the time -> ticks of one block's worth */
#define BD_LR_MARGIN 115u                               /* (budget.c no longer has it: this test's cost clock is still the OLD Mac estimate, lrry_weights x 1.15, NOT the bench12 rows; stale until reworked, not run for this change) */
static uint32_t voice_u(uint32_t w) { return (uint32_t)(((uint64_t)w * BD_LR_MARGIN * dev_pct * 6400u / 2100u + 9999u) / 10000u); }   /* a voice's weight (host instructions a sample) -> 1/100 % */
static uint32_t a_t[NVOICE], own_all, own, blk_on, blk_job, own_added, peak_now, post_t;   /* the block's: each voice's audio (ticks), all of it, core 0's share, a block open, a job posted, its share added */
static volatile uint32_t fin, cur_job, c1_claimed, quit, c1_ready;
static uint32_t in_half;
static void blk_close(void)                             /* the previous block's rest: core 0's own audio when no job went, the part and the globals */
{
    if (!blk_on)
        return;
    add_ticks((blk_job ? 0u : own_all) + post_t);
    blk_on = 0;
}
static void hold_hook(uint32_t ti, uint32_t *mask)      /* the render stage's start (voice.c): the control work of the block is done */
{
    uint16_t w[NVOICE];
    uint32_t i, pre = 0;
    blk_close();
    lrry_weights(&lrry_pp[ti], &lrj, w);
    own_all = 0;
    for (i = 0; i < NVOICE; i++)
        if ((*mask >> i) & 1u) {
            uint32_t c = voice_u(w[i]), cc = BD_LR_T(BD_LR_CTL) < c ? BD_LR_T(BD_LR_CTL) : c;
            pre += cc;
            a_t[i] = u_ticks(c - cc);
            own_all += a_t[i];
        }
    pre += peak_now;
    peak_now = 0;
    post_t = u_ticks(part_fixed(&trk[0]) + global_cost());
    add_ticks(u_ticks(pre));
    blk_on = 1;
    blk_job = own_added = 0;
}
static uint32_t fm1_ticks(void)                         /* audio.c: the half's start, then its end */
{
    if (!in_half) {
        in_half = 1;
        return raw_ticks();
    }
    blk_close();
    in_half = 0;
    return raw_ticks();
}
/* the runner's words: the post (core 0 stores the job word) and core 0's look at done */
static uint32_t st_hook(uint32_t *p, uint32_t v)
{
    if (!tc_host_is_c1 && p == &tc_mb.job) {
        uint32_t i, a1 = 0;
        add_ticks(u_ticks(BD_U(BD_LR_HO0) * dev_pct / 100u));
        for (i = 0; i < NVOICE; i++)
            if ((tc_ctxs[v & 1u].mask >> i) & 1u)   /* (PIPELINE: job v uses the block copy tc_ctxs[v & 1]) */
                a1 += a_t[i];
        own = own_all - a1;
        __atomic_store_n(&fin, raw_ticks() + u_ticks(BD_U(BD_LR_HO1) * dev_pct / 100u) + a1, __ATOMIC_RELEASE);
        __atomic_store_n(&cur_job, v, __ATOMIC_RELEASE);
        blk_job = 1;
    }
    return v;
}
static inline int c1_live(void) { return __atomic_load_n(&tc_host_live, __ATOMIC_ACQUIRE) && !__atomic_load_n(&quit, __ATOMIC_ACQUIRE); }
static void cpu_relax(void) { sched_yield(); }
static uint32_t ld_hook(const uint32_t *p, uint32_t v)
{
    if (!tc_host_is_c1 && p == &tc_mb.done && blk_job && !own_added) {
        while (c1_live() && __atomic_load_n(&c1_claimed, __ATOMIC_ACQUIRE) != __atomic_load_n(&cur_job, __ATOMIC_ACQUIRE))
            cpu_relax();                                /* (core 1 claims a job within microseconds on the unit: it is never "late" here because a thread was slow) */
        own_added = 1;
        add_ticks(own);                                 /* core 0 has rendered its own voices */
    }
    return v;
}
/* core 1: its job ends at fin (core 0's waits move the clock meanwhile: the hal's divider, as tests/twocore_sim.c) */
static pthread_mutex_t tmx = PTHREAD_MUTEX_INITIALIZER;
static int tusers;
static void time_adj(int d)
{
    pthread_mutex_lock(&tmx);
    tusers += d;
    __atomic_store_n(&tc_host_tick_div, tusers > 0 ? 1u : 0u, __ATOMIC_RELAXED);
    pthread_mutex_unlock(&tmx);
}
static void sim_job(uint32_t arg)
{
    uint32_t t = __atomic_load_n(&fin, __ATOMIC_ACQUIRE);
    __atomic_store_n(&c1_claimed, __atomic_load_n(&tc_mb.job, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
    __atomic_store_n(&tc_host_stop, t, __ATOMIC_RELEASE);
    time_adj(1);
    while (c1_live() && (int32_t)(raw_ticks() - t) < 0)
        cpu_relax();
    time_adj(-1);
    __atomic_store_n(&tc_host_stop, 0u, __ATOMIC_RELEASE);
    tc_job_run(arg);
}
static void *core1(void *arg)
{
    tc_c1_t c;
    (void)arg;
    tc_host_is_c1 = 1;
    tc_c1_init(&c);
    __atomic_store_n(&c1_ready, 1u, __ATOMIC_RELEASE);
    while (c1_live()) {
        tc_c1_step(&c);
        cpu_relax();
    }
    return 0;
}
static pthread_t thr;
static int thr_on;
static void start_core1(void) { thr_on = 1; pthread_create(&thr, 0, core1, 0); }

/* ------------------------------------------------------------------ the stress set --- */
static void sp_base(track_t *t)
{
    t->p[P_ATK] = 0; t->p[P_DEC] = 0; t->p[P_SUS] = 127; t->p[P_REL] = 30; t->p[P_LEVEL] = 112; t->p[P_TRIM] = 112; t->p[P_KTRK] = 0;
    t->p[P_FREEA] = UF_STEPS;
}
static void super7(track_t *t, uint32_t o) { scn_x(t, o ? X_O2MODEL : X_O1MODEL, 5); scn_x(t, o ? X_O2P1 : X_O1P1, 50); scn_x(t, o ? X_O2P2 : X_O1P2, 127); scn_x(t, o ? X_O2P3 : X_O1P3, 100); scn_x(t, o ? X_O2LVL : X_O1LVL, 90); }
static void s1(track_t *t) { super7(t, 0); scn_x(t, X_O2LVL, 80); scn_x(t, X_O2COARSE, 12); scn_x(t, X_O2FINE, 7); t->p[P_FREEA] = 70; t->p[P_FREEB] = 30; t->p[P_FREED] = 2; }   /* SUPER PAD */
static void s2(track_t *t)                                                                                          /* SUPER + LADDER, both dirt units */
{
    super7(t, 0); scn_x(t, X_O2LVL, 80); scn_x(t, X_O2COARSE, -12); t->p[P_FREEA] = 60; t->p[P_FREEB] = 40; t->p[P_FREED] = 2;
    t->p[P_TWTYPE] = 5; t->p[P_CUT2] = 55; t->p[P_RES2] = 70; scn_x(t, X_DATYPE, 1); t->p[P_DRV2] = 70; scn_x(t, X_DBTYPE, 4); scn_x(t, X_DBDRV, 50); scn_x(t, X_DBLVL, 90);
}
static void s3(track_t *t)                                                                                          /* SUPER LEAD: SUPER + PULSE, A 12 dB, B SVF, two dirt units */
{
    super7(t, 0); scn_x(t, X_O2MODEL, 3); scn_x(t, X_O2LVL, 80); t->p[P_FREEA] = 75; t->p[P_FREEB] = 50; t->p[P_FREED] = 1;
    t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 80; scn_x(t, X_DATYPE, 1); t->p[P_DRV2] = 60; scn_x(t, X_DBTYPE, 4); scn_x(t, X_DBDRV, 60); scn_x(t, X_DBLVL, 80);
}
static void s4(track_t *t)                                                                                          /* SUPER + COMB, dirt */
{
    super7(t, 0); t->p[P_FREEA] = 80; t->p[P_FREED] = 2; t->p[P_TWTYPE] = 6; t->p[P_CUT2] = 12; t->p[P_RES2] = 60; scn_x(t, X_DATYPE, 2); t->p[P_DRV2] = 50;
}
static void s5(track_t *t)                                                                                          /* SWEEP: the FILTER ENV opens filter A at the top, it closes over ~1.5 s */
{
    super7(t, 0); scn_x(t, X_O2LVL, 80); scn_x(t, X_O2FINE, -9); t->p[P_FREEA] = 30; t->p[P_FREEB] = 40; t->p[P_FREED] = 2;
    t->p[P_E2ATK] = 0; t->p[P_E2DEC] = 100; t->p[P_E2SUS] = 0; t->p[P_E2REL] = 40; t->p[P_E2FLT] = 127;
    t->p[P_TWTYPE] = 5; t->p[P_CUT2] = 60; t->p[P_RES2] = 50; scn_x(t, X_DATYPE, 1); t->p[P_DRV2] = 60;
}
static void s6(track_t *t)                                                                                          /* FM + RING + NOISE, A 24 dB, B SVF, two dirt units, MID / SIDE, SUB */
{
    scn_x(t, X_O1MODEL, 6); scn_x(t, X_O1P1, 40); scn_x(t, X_O1P2, 60); scn_x(t, X_O1LVL, 90); scn_x(t, X_O2MODEL, 4); scn_x(t, X_O2LVL, 70); scn_x(t, X_RGLVL, 40); scn_x(t, X_NZLVL, 20);
    t->p[P_FREEA] = 65; t->p[P_FREED] = 2; t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 70; scn_x(t, X_DATYPE, 1); t->p[P_DRV2] = 50; scn_x(t, X_DBTYPE, 3); scn_x(t, X_DBDRV, 40); scn_x(t, X_DBLVL, 90);
    scn_x(t, X_SUBLVL, 40);
}
static void s7(track_t *t)                                                                                          /* two SUPER 7, A 24 dB, B LADDER, two dirt units: the dearest voice */
{
    super7(t, 0); super7(t, 1); scn_x(t, X_O2FINE, 11); t->p[P_FREEA] = 60; t->p[P_FREED] = 2; t->p[P_TWTYPE] = 5; t->p[P_CUT2] = 50; t->p[P_RES2] = 60;
    scn_x(t, X_DATYPE, 1); t->p[P_DRV2] = 60; scn_x(t, X_DBTYPE, 4); scn_x(t, X_DBDRV, 50); scn_x(t, X_DBLVL, 90); scn_x(t, X_NZLVL, 10); scn_x(t, X_SUBLVL, 30);
}
static void s8(track_t *t)                                                                                          /* two dirt units, A 12 dB, B SVF (lrry_budget_test's), the middle weight */
{
    scn_x(t, X_O1LVL, 90); scn_x(t, X_O2LVL, 90); scn_x(t, X_O2COARSE, 12); t->p[P_FREEA] = 55; t->p[P_FREEB] = 30; t->p[P_TWTYPE] = 1; t->p[P_CUT2] = 60;
    scn_x(t, X_DATYPE, 1); t->p[P_DRV2] = 80; scn_x(t, X_DAPAN, -35); scn_x(t, X_DALVL, 100); scn_x(t, X_DBTYPE, 3); scn_x(t, X_DBDRV, 40); scn_x(t, X_DBPAN, 35); scn_x(t, X_DBLVL, 90);
}
static void f14(track_t *t) { host_ds_load(t, 0, 14); }                                                           /* phase 56: the factory sound #08 CLAVINET (owner, test2), as the unit loads it */
static const struct { const char *n; void (*f)(track_t *); } SP[] = {
    {"S1 SUPER PAD (SUPER 7 + SAW, A 24 dB)", s1}, {"S2 SUPER + LADDER + two dirt", s2}, {"S3 SUPER LEAD (A 12 dB, B SVF, two dirt)", s3}, {"S4 SUPER + COMB + dirt", s4},
    {"S5 SWEEP (FILTER ENV closes A, LADDER, dirt)", s5}, {"S6 FM RING NOISE, A 24, B SVF, two dirt, SUB", s6}, {"S7 two SUPER 7, A 24, LADDER, two dirt", s7}, {"S8 two dirt, A 12 dB, B SVF", s8},
    {"F14 CLAVINET (factory sound #08)", f14},
};
#define NSP (sizeof SP / sizeof SP[0])

/* ------------------------------------------------------------------ a run (a child process) --- */
typedef struct {
    uint32_t shown, alloc, one, voice_u, held_end, nheld, shed, kills, silent, pauses, trims, vlate, late, nott_blocks, overrun, first_cut_ms, peak_half_pct, mean_half_pct;
    uint32_t shown2, held2, meas;                       /* phase 56: the number just before the second note, the voices held then, the measured dev (budget.c bd_meas_pct) */
    tc_stat_t s;
} res_t;
static res_t *R;
static uint32_t park_at;                                /* a half before which the main loop parks core 1 for a flash access (0: none) */
static void child(uint32_t sp, uint32_t dev, uint32_t force_n)
{
    static const uint8_t NOTE[8] = {48, 55, 60, 64, 67, 72, 76, 79};
    uint32_t h, nh, k, i, t_end, last = 0, sum = 0, pk = 0, cut = 0;
    int32_t o;
    dev_pct = dev;
    scn_fresh();
    sp_base(&trk[0]);
    SP[sp].f(&trk[0]);
    tc_host_ld_hook = ld_hook;
    tc_host_st_hook = st_hook;
    tc_host_tick_step = 1u;
    time_adj(0);
    __atomic_store_n(&tc_host_ticks, BASE_TICKS, __ATOMIC_RELAXED);
    tc_host_c1_started = start_core1;
    tc_arm();
    tc_boot(1u, 0, 0);
    while (thr_on && !__atomic_load_n(&c1_ready, __ATOMIC_ACQUIRE))
        cpu_relax();
    shed_req = 0, shed_count = 0, shed_over = 0, voice_kills = 0;
    budget_poll();
    R->shown = part_notes_shown(0);
    R->alloc = trk_nvoice(&trk[0]);
    R->one = bnotes1[0];
    R->voice_u = part_cost(&trk[0]);
    nh = force_n ? force_n : R->shown;
    nh = nh < 8u ? nh : 8u;
    t_end = (nh - 1u) * HALVES_PER_S / 2u + 3u * HALVES_PER_S + HALVES_PER_S / 10u;
    for (h = 0; h < t_end; h++) {
        uint32_t hs = BASE_TICKS + h * TCR_HALF_TICKS, t0, used;
        if (thr_on && tc.up) {                          /* (the heartbeat watch reads this clock as real time: a live core 1 has beaten) */
            uint32_t ms = raw_ticks() / MS_TICKS;
            if (ms - tc.beat_t > 50u)
                while (c1_live() && __atomic_load_n(&tc_mb.beat, __ATOMIC_RELAXED) == tc.beat)
                    cpu_relax();
        }
        fm1_ms = raw_ticks() / MS_TICKS;                /* (phase 56: the main loop's clock: budget.c's measurement waits LRRY_MEAS_MS on it) */
        tc_watch(raw_ticks() / MS_TICKS);
        if (park_at && h == park_at)
            tc_flash_park();                            /* (a flash read: a patch load or browse; the next pass's tc_watch ends it) */
        tc_trim_poll();
        budget_poll();
        for (k = 0; k < nh; k++)
            if (h == HALVES_PER_S / 10u + k * HALVES_PER_S / 2u) {
                if (k == 1u) {                          /* (what the screen says just before the second note: the number less the keys held) */
                    R->shown2 = trk_nvoice(&trk[0]);
                    for (i = 0; i < NVOICE; i++)
                        R->held2 += trk[0].v[i].active && trk[0].v[i].gate && trk[0].v[i].stage != 4u;
                }
                trk_note_on(&trk[0], NOTE[k], 100), last = h;
            }
        if ((int32_t)(hs - raw_ticks()) > 0)
            __atomic_store_n(&tc_host_ticks, hs, __ATOMIC_RELAXED);
        else if (raw_ticks() != hs)
            R->overrun++;
        peak_now = (h % PEAK_EVERY == PEAK_EVERY - 2u || h % PEAK_EVERY == PEAK_EVERY - 1u) ? PEAK * (TCR_BLOCKS) : 0u;   /* (the half's 6 points, put on its first block) */
        t0 = raw_ticks();
        fm1_alnk0_irq();
        used = raw_ticks() - t0;
        o = (int32_t)(used * 1000u / TCR_HALF_TICKS);
        sum += (uint32_t)o;
        pk = (uint32_t)o > pk ? (uint32_t)o : pk;
        if (!cut && (shed_count || voice_kills) && nh >= 2u) {
            cut = 1;
            R->first_cut_ms = (uint32_t)((uint64_t)(h - (HALVES_PER_S / 10u + HALVES_PER_S / 2u)) * HALF_FRAMES * 1000u / FS);   /* after the second note's start */
        }
        R->nott_blocks += tcr.nott;
    }
    (void)last;
    for (k = 0; k < nh; k++)
        for (i = 0; i < NVOICE; i++)
            R->held_end += trk[0].v[i].active && trk[0].v[i].gate && trk[0].v[i].stage != 4u && trk[0].v[i].note == NOTE[k];
    R->nheld = nh;
    R->meas = bd_meas_pct;
    R->shed = shed_count;
    R->kills = voice_kills;
    R->s = tcs;
    R->peak_half_pct = pk;
    R->mean_half_pct = sum / t_end;
    __atomic_store_n(&quit, 1u, __ATOMIC_RELEASE);
    if (thr_on)
        pthread_join(thr, 0);
}
static void run(uint32_t sp, uint32_t dev, uint32_t force_n, res_t *out)
{
    pid_t pid;
    int st = 0;
    memset(R, 0, sizeof *R);
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        child(sp, dev, force_n);
        _exit(0);
    }
    waitpid(pid, &st, 0);
    *out = *R;
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0))
        out->held_end = 0xFFFFu;
}

int main(int argc, char **argv)
{
    static const uint32_t DEV[2] = {100u, 125u};
    uint32_t s, d, sweep = argc > 1 && !strcmp(argv[1], "-sweep");
    res_t r;
    setvbuf(stdout, NULL, _IONBF, 0);
    R = mmap(0, sizeof *R, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    printf("voice_hold: ESTIMATE (host cost clock from budget.c's rows, nothing measured on the unit); the overload rule sheds at %u %% of a half two halves in a row; peak +%u points twice every %u halves\n", 85u, PEAK / 100u, PEAK_EVERY);
    for (s = 0; s < NSP; s++)
        for (d = 0; d < 2u; d++) {
            char w[300];
            run(s, DEV[d], 0, &r);
            printf("voice_hold: %-46s DEV %.2f | voice %5.1f %% | shown %u (one core %u, allocator %u) | held %u of %u at the end | shed %u faded %u | half mean %4.1f %% peak %4.1f %% | late %u very late %u silent %u pauses %u trims %u no take-over %u overruns %u\n",
                   SP[s].n, DEV[d] / 100.0, r.voice_u / 100.0, r.shown, r.one, r.alloc, r.held_end, r.nheld, r.shed, r.kills, r.mean_half_pct / 10.0, r.peak_half_pct / 10.0,
                   r.s.late, r.s.vlate, r.s.silent, r.s.pauses, r.s.trims, r.nott_blocks, r.overrun);
            if (!d) {
                snprintf(w, sizeof w, "%s: the allocator's number equals the shown number (%u, %u)", SP[s].n, r.alloc, r.shown);
                ck(w, r.alloc == r.shown && r.shown >= 1u);
            }
            snprintf(w, sizeof w, "%s DEV %.2f: all %u notes at the shown number still sound 3 s after the last (%u), none shed (%u) or faded (%u)", SP[s].n, DEV[d] / 100.0, r.nheld, r.held_end, r.shed, r.kills);
            ck(w, r.held_end == r.nheld && r.shed == 0 && r.kills == 0);
            snprintf(w, sizeof w, "%s DEV %.2f: no silent block, no pause, no trim, no overrun (%u %u %u %u)%s", SP[s].n, DEV[d] / 100.0, r.s.silent, r.s.pauses, r.s.trims, r.overrun,
                     r.shown == 1u && r.overrun ? " NOTE: one note overruns; one note always plays, the number cannot go lower" : "");
            ck(w, r.s.silent == 0 && r.s.pauses == 0 && r.s.trims == 0 && (r.overrun == 0 || r.shown == 1u));
        }
    if (sweep)
        for (s = 0; s < NSP; s++) {
            run(s, 100u, 0, &r);
            if (r.shown < 2u) {
                printf("voice_hold: sweep %-46s shown 1: a second note takes the first one's voice (the allocator), nothing to hold\n", SP[s].n);
                continue;
            }
            for (d = 80u; d <= 200u; d += 5u) {
                run(s, d, 2u, &r);
                if (r.held_end != r.nheld || r.shed || r.kills)
                    break;
            }
            printf("voice_hold: sweep %-46s two notes held: first cut at DEV %.2f, %u ms after the second note (shed %u faded %u, pauses %u, trims %u, half mean %.1f %% peak %.1f %%)\n", SP[s].n, d / 100.0,
                   r.first_cut_ms, r.shed, r.kills, r.s.pauses, r.s.trims, r.mean_half_pct / 10.0, r.peak_half_pct / 10.0);
        }
    {   /* phase 56 (owner, test2: #08 CLAVINET, one note held, the second cut the first while the screen said a voice was free): the unit dearer than the model holds (DEV 1.60).
         * Before phase 56 the screen said 2 and the overload rule cut a held note. Now the measured voice lowers the number while the first note is held: the screen says 0 free, the
         * second note takes the first one's voice (the allocator), nothing is shed or faded. And a unit as the model (DEV 1.00) keeps 2 */
        char w[300];
        uint32_t s14 = NSP - 1u;
        run(s14, 160u, 2u, &r);
        printf("voice_hold: measured %-46s DEV 1.60, two notes: shown %u at the start, %u before the second note with %u held (measured dev %u %%), held %u at the end, shed %u faded %u\n",
               SP[s14].n, r.shown, r.shown2, r.held2, r.meas, r.held_end, r.shed, r.kills);
        snprintf(w, sizeof w, "%s DEV 1.60: before the second note the number is the unit's own (1: the screen says 0 free), nothing shed or faded after it", SP[s14].n);
        ck(w, r.shown2 == 1u && r.held2 == 1u && r.shed == 0u && r.kills == 0u);
        run(s14, 100u, 2u, &r);
        snprintf(w, sizeof w, "%s DEV 1.00: the measured number stays 2 (measured dev %u %%), both notes held (%u)", SP[s14].n, r.meas, r.held_end);
        ck(w, r.shown2 == 2u && r.held_end == 2u && r.shed == 0u);
    }
    park_at = HALVES_PER_S / 10u + HALVES_PER_S / 2u + 2u * HALVES_PER_S;   /* (no check: a finding for the report) */
    run(0, 100u, 2u, &r);
    park_at = 0;
    printf("voice_hold: park %-46s two notes held, a flash park 2 s after the second: held %u of 2 at the end, trims %u, faded %u (shown %u, one core %u: the park trims to the one-core number)\n",
           SP[0].n, r.held_end, r.s.trims, r.kills, r.shown, r.one);
    printf("voice_hold: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
