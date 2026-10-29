/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY step 3 (host first): 8 voices split BY VOICE over two simulated cores. Design: LRRY_STEREO_VOICE_DESIGN.md section 4.
 * Core 0: voices 0..3 and the mix; core 1 (a thread here, the second core on the unit): voices 4..7. Each core has its own stereo accumulator; core 0 adds them.
 * The hand-over copies the block's control data (the patch) before posting the job; core 1 reads only that copy. Integer addition is exact in any order, so one core
 * and two cores must give identical output, bit for bit: that is what the test proves. Mutants (-DMUT_*) break one rule each and must make the test fail. */
#ifndef LRRY_SPLIT_H
#define LRRY_SPLIT_H
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
#include "lrry_voice.h"

#define NV 8
#define PER_CORE 4

typedef struct {
    voice_t v[NV]; uint32_t age[NV]; uint8_t held[NV], note[NV]; uint32_t clock;
    vparams P;                                         /* the patch as the control side edits it */
    vparams blockP;                                    /* the copy core 1 reads (published before the job is posted) */
    int32_t acc1L[BLK], acc1R[BLK];                    /* core 1's accumulator */
    int two_core;                                      /* 0: core 0 renders everything (the fallback), 1: split */
    _Atomic uint32_t job, done; _Atomic int quit; pthread_t th; int started;
    int c1_blocks; int32_t *shL, *shR;                                     /* blocks core 1 rendered (statistics) */
} engine_t;

static inline int core_of(int i) { return i / PER_CORE; }
static inline int stage_released(const voice_t *v) { return v->stage == 3; }

static void eng_init(engine_t *e) { memset(e, 0, sizeof *e); vparams_init(&e->P); e->blockP = e->P; }

/* voice allocation: the core with fewer sounding voices first, a free slot there; else any free slot; else steal a released voice, else the oldest */
static int eng_note_on(engine_t *e, int note, int vel)
{
    int cnt[2] = {0, 0}, i, best = -1, c, pick;
    for (i = 0; i < NV; i++) if (e->v[i].active) cnt[core_of(i)]++;
    for (c = 0; c < 2 && best < 0; c++) {
        pick = cnt[0] <= cnt[1] ? c : 1 - c;
        for (i = pick * PER_CORE; i < (pick + 1) * PER_CORE; i++) if (!e->v[i].active) { best = i; break; }
    }
    if (best < 0) {
        uint32_t oldest = 0xFFFFFFFFu; int rel = 0;
        for (i = 0; i < NV; i++) { int r = stage_released(&e->v[i]); if ((r && !rel) || (r == rel && e->age[i] < oldest)) { rel = r; oldest = e->age[i]; best = i; } }
    }
    voice_on(&e->v[best], note, vel); e->age[best] = ++e->clock; e->held[best] = 1; e->note[best] = (uint8_t)note;
    return best;
}
static void eng_note_off(engine_t *e, int note)
{
    int i; for (i = 0; i < NV; i++) if (e->v[i].active && e->held[i] && e->note[i] == note) { e->held[i] = 0; voice_off(&e->v[i]); }
}

static void render_voices(engine_t *e, int a, int b, const vparams *P, int32_t *L, int32_t *R)
{
    int i; for (i = a; i < b; i++) voice_render(&e->v[i], P, L, R);
}

static void *core1_main(void *arg)
{
    engine_t *e = (engine_t *)arg; uint32_t seen = 0;
    for (;;) {
        while (atomic_load_explicit(&e->job, memory_order_acquire) == seen) { if (atomic_load(&e->quit)) return 0; sched_yield(); }
        seen = atomic_load_explicit(&e->job, memory_order_acquire);
        memset(e->acc1L, 0, sizeof e->acc1L); memset(e->acc1R, 0, sizeof e->acc1R);
#ifdef MUT_STALE
        { static vparams old; static int have; if (have) render_voices(e, PER_CORE, NV, &old, e->acc1L, e->acc1R); else render_voices(e, PER_CORE, NV, &e->blockP, e->acc1L, e->acc1R); old = e->blockP; have = 1; }
#else
#ifdef MUT_SHARED
        render_voices(e, PER_CORE, NV, &e->blockP, e->shL, e->shR);          /* core 1 adds into core 0's accumulator while core 0 does too: a race */
#else
        render_voices(e, PER_CORE, NV, &e->blockP, e->acc1L, e->acc1R);
#endif
#endif
#ifdef MUT_DOUBLE
        render_voices(e, PER_CORE - 1, PER_CORE, &e->blockP, e->acc1L, e->acc1R);     /* voice 3 belongs to core 0 and is rendered here too */
#endif
        e->c1_blocks++;
        atomic_store_explicit(&e->done, seen, memory_order_release);
    }
}
static void eng_start(engine_t *e) { pthread_create(&e->th, 0, core1_main, e); e->started = 1; }
static void eng_stop(engine_t *e) { if (e->started) { atomic_store(&e->quit, 1); pthread_join(e->th, 0); e->started = 0; } }

/* one block: ADDS to outL / outR */
static void eng_block(engine_t *e, int32_t *outL, int32_t *outR)
{
    int i;
    if (!e->two_core || !e->started) { render_voices(e, 0, NV, &e->P, outL, outR); return; }
    e->shL = outL; e->shR = outR; e->blockP = e->P;      /* publish the control data, then post the job (release) */
    { uint32_t j = atomic_load(&e->job) + 1; atomic_store_explicit(&e->job, j, memory_order_release);
      render_voices(e, 0, PER_CORE, &e->P, outL, outR);
#ifndef MUT_NOWAIT
      while (atomic_load_explicit(&e->done, memory_order_acquire) != j) sched_yield();
#endif
    }
    for (i = 0; i < BLK; i++) { outL[i] += e->acc1L[i]; outR[i] += e->acc1R[i]; }
}
#endif
