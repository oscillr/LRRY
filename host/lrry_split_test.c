/* L/RRY step 3 tests: split by voice over two cores equals one core, bit for bit. cc -O2 -pthread lrry_split_test.c -o t -lm [-DMUT_NOWAIT|-DMUT_DOUBLE|-DMUT_STALE|-DMUT_SHARED] */
#include <stdio.h>
#include <stdlib.h>
#include "lrry_split.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; if (fails < 8) { printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)
static uint32_t rs = 777;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

#define NB 1500
static int32_t OL[2][NB * BLK], OR_[2][NB * BLK];
enum { SC_PLAIN, SC_FULL };
static void patch(vparams *P, int kind, int b)
{
    vparams_init(P);
    P->o[0].model = M_SAW; P->o[0].level = 0.3; P->env.r_ms = 40.0;
    if (kind == SC_FULL) {
        P->o[0].model = M_SUPER; P->o[0].p1 = 18; P->o[0].p2 = 5; P->o[0].p3 = 0.8; P->o[1].model = M_PULSE; P->o[1].p1 = 0.4; P->o[1].level = 0.2; P->o[1].pan = 0.6; P->o[1].fine_ct = 7;
        P->fa.on = 1; P->fa.p24 = 1; P->fa.cutoff = 600.0 + 3000.0 * (0.5 + 0.5 * sin(b * 0.05)); P->fa.q = 2.5; P->fa.pan = -0.3;      /* the cutoff moves every block: the patch copy must be fresh */
        P->fb.kind = FB_SVF; P->fb.mode = F_HP; P->fb.cutoff = 150; P->fb.q = 1; P->fb.pan = 0.4; P->da.type = D_SOFT; P->da.drive = 2; P->da.pan = -0.5; P->db.type = D_FOLD; P->db.drive = 1.3;
        P->d_serial = 1; P->sub_level = 0.25; P->sub_oct = -1; P->side = 1.2; P->nz.level = 0.03; P->nz.pan = 0.2;
    }
}

/* the same note script for both runs; mode_sched: 0 one core, 1 two cores, 2 switching every 37 blocks */
static void run(int which, int mode_sched, int kind, uint32_t seed)
{
    engine_t *e = (engine_t *)calloc(1, sizeof *e); int b, k;
    rs = seed; eng_init(e); if (mode_sched) eng_start(e);
    for (b = 0; b < NB; b++) {
        int n = (int)(rnd() % 4);
        for (k = 0; k < n; k++) { int note = 36 + (int)(rnd() % 48); if (rnd() % 3 == 0) eng_note_off(e, note); else eng_note_on(e, note, 100); }
        if (b % 200 == 199) { int q; for (q = 0; q < 128; q++) eng_note_off(e, q); }
        patch(&e->P, kind, b);
        e->two_core = mode_sched == 1 ? 1 : mode_sched == 2 ? (b / 37) & 1 : 0;
        eng_block(e, OL[which] + b * BLK, OR_[which] + b * BLK);
    }
    if (mode_sched) { eng_stop(e); }
    CHECK(mode_sched == 0 || e->c1_blocks > 0, "core 1 never rendered");
    free(e);
}
static double pk(const int32_t *x, int n) { double m = 0; int i; for (i = 0; i < n; i++) if (fabs((double)x[i]) > m) m = fabs((double)x[i]); return m; }

static void t_equal(void)
{
    int kind, sched;
    for (kind = 0; kind < 2; kind++) for (sched = 1; sched <= 2; sched++) {
        run(0, 0, kind, 4242 + kind); run(1, sched, kind, 4242 + kind);
        int same = memcmp(OL[0], OL[1], sizeof OL[0]) == 0 && memcmp(OR_[0], OR_[1], sizeof OR_[0]) == 0;
        CHECK(same, "kind %d schedule %d: two cores differ from one core", kind, sched);
        CHECK(pk(OL[0], NB * BLK) > 1e6, "kind %d: nothing sounded (peak %.0f)", kind, pk(OL[0], NB * BLK));
        if (!kind) { printf("  one core = two cores (schedule %d, plain patch): %s\n", sched, same ? "identical" : "DIFFERENT"); }
        else printf("  one core = two cores (schedule %d, full stereo voice, cutoff moving every block): %s\n", sched, same ? "identical" : "DIFFERENT");
    }
    /* two runs of the two-core engine agree with each other (no timing dependence) */
    run(0, 1, 1, 99); run(1, 1, 1, 99);
    CHECK(memcmp(OL[0], OL[1], sizeof OL[0]) == 0 && memcmp(OR_[0], OR_[1], sizeof OR_[0]) == 0, "two two-core runs differ");
    printf("  two runs of the two-core engine: %s\n", memcmp(OL[0], OL[1], sizeof OL[0]) == 0 ? "identical" : "DIFFERENT");
}

static void t_alloc(void)
{
    engine_t *e = (engine_t *)calloc(1, sizeof *e); int n, i, c[2]; eng_init(e);
    for (n = 1; n <= NV; n++) { eng_note_on(e, 40 + n, 100); c[0] = c[1] = 0; for (i = 0; i < NV; i++) if (e->v[i].active) c[core_of(i)]++; CHECK(abs(c[0] - c[1]) <= 1, "%d notes: cores %d / %d", n, c[0], c[1]); }
    printf("  8 notes: cores carry %d and %d voices\n", c[0], c[1]);
    for (n = 0; n < 40; n++) eng_note_on(e, 30 + n, 100);                 /* steals: still 8 voices, none doubled */
    { int cnt = 0; for (i = 0; i < NV; i++) cnt += e->v[i].active; CHECK(cnt == NV, "after steals %d active", cnt); }
    { int32_t l[BLK], r[BLK]; int b2; for (b2 = 0; b2 < 20; b2++) { memset(l, 0, sizeof l); memset(r, 0, sizeof r); render_voices(e, 0, NV, &e->P, l, r); } }   /* let them sound first */
    for (n = 0; n < 128; n++) eng_note_off(e, n);
    { int32_t l[BLK], r[BLK]; int b, any = 1; for (b = 0; b < 400 && any; b++) { memset(l, 0, sizeof l); memset(r, 0, sizeof r); render_voices(e, 0, NV, &e->P, l, r); any = 0; for (i = 0; i < NV; i++) any |= e->v[i].active; }
      CHECK(!any, "hanging voices after all notes off"); printf("  all notes off: every voice ended after %d blocks\n", b); }
    free(e);
}

int main(void)
{
    voice_tables_init(); t_equal(); t_alloc();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
