/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3b: the ONE-CORE reference. The scenes of lrry_scn.h through the firmware's mix_block with no second core (LRRY_TWOCORE 0: lrry_render_stage plays all eight voices on this core).
 * Prints one hash a scene over every output sample (the master output, both channels). tests/twocore_sim.c prints the same hashes for its core-0 reference and for the two-core run, and
 * checks equality sample for sample; tests/lrry_split.sh compares the two programs' lists (and, with a checkout of the 4.3a tree, that tree's list: the control / render split changed nothing).
 * Also: the dynamic split (voice.c lrry_split_choose: which voices go to core 1 each block) on its own: random masks and weights against a brute force, stability, and on the scenes the modelled load of the
 * longer core with the split chosen each block against the fixed split (voices 0..3 / 4..7) and against everything on one core.
 * Build (from lrry): ../onepart.sh 1 1 lrry_split */
#include <stdint.h>
#ifndef LRRY_OLD_TREE                                    /* (-DLRRY_OLD_TREE=1: built against the 4.3a tree, which has no split: only the scene hashes) */
static void split_probe(uint32_t mask, const uint16_t *w, uint32_t c1);
#define LRRY_SPLIT_PROBE(ti, mask, w, c1) split_probe(mask, w, c1)
#endif   /* (the firmware calls the chooser here in the one-core build too; the hook only watches) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "lrry_scn.h"

static int nfail, ncheck;
static void ck(const char *what, int ok)
{
    ncheck++;
    printf("lrry_split: %-110s %s\n", what, ok ? "ok" : "FAIL");
    nfail += !ok;
}
static void blk_plain(int32_t *out, uint32_t n) { mix_block(out, n); }

#ifndef LRRY_OLD_TREE
/* the modelled load of the longer core (host instructions a sample, the estimate of lrry_weights) summed over the blocks of a scene: everything on core 0, the fixed split 0..3 / 4..7, the chosen split */
static struct { double all0, fixed, dyn; uint32_t blocks, moved, kept; uint32_t pm, pc; } PR;
static void split_probe(uint32_t mask, const uint16_t *w, uint32_t c1)
{
    uint32_t both = PR.pm & mask;
    PR.all0 += lrry_cost(mask, 0, w);                   /* (cores-balance: the longer side plus the hand-over ho0 when there is a job) */
    PR.fixed += lrry_cost(mask, mask & 0xF0u, w);
    PR.dyn += lrry_cost(mask, c1, w);
    PR.blocks++;
    PR.moved += __builtin_popcount(both & (PR.pc ^ c1));
    PR.kept += __builtin_popcount(both & ~(PR.pc ^ c1));
    PR.pm = mask;
    PR.pc = c1;
}
#else
static struct { double all0, fixed, dyn; uint32_t blocks, moved, kept; } PR;
#endif
static int32_t keep_buf[(size_t)SCN_BLOCKS * 2u * CTL];
/* one scene in a child process (the firmware's globals start at power-on state, as in tests/twocore_sim.c's runs): the hash and the check results come back through a pipe */
int main(void)
{
    uint32_t id;
    for (id = 0; id < SC_N; id++) {
        int fd[2], st = 0;
        uint64_t res[8] = {0};
        pid_t pid;
        char w[160];
        pipe(fd);
        fflush(stdout);
        pid = fork();
        if (pid == 0) {
            uint32_t i, nz = 0, same = 1;
            uint64_t h = scn_run(id, blk_plain, keep_buf);
            if (getenv("LRRY_PLAIN_DIR")) {             /* cores-pipeline: every output sample of the scene, for tests/twocore_sim.c (its core-0 reference must equal this, delayed by one block) */
                char fn[400];
                FILE *fp;
                snprintf(fn, sizeof fn, "%s/s%u.bin", getenv("LRRY_PLAIN_DIR"), id);
                fp = fopen(fn, "wb");
                if (fp) {
                    fwrite(keep_buf, sizeof keep_buf[0], (size_t)SCN_BLOCKS * 2u * CTL, fp);
                    fclose(fp);
                }
            }
            for (i = 0; i < SCN_BLOCKS * 2u * CTL; i++)
                nz += keep_buf[i] != 0;
            for (i = 0; i < SCN_BLOCKS * CTL; i++)
                same &= keep_buf[2u * i] == keep_buf[2u * i + 1u];
            res[0] = h, res[1] = nz, res[2] = same;
            memcpy(&res[3], &PR.all0, 8); memcpy(&res[4], &PR.fixed, 8); memcpy(&res[5], &PR.dyn, 8); res[6] = PR.blocks; res[7] = ((uint64_t)PR.moved << 32) | PR.kept;
            write(fd[1], res, sizeof res);
            _exit(0);
        }
        read(fd[0], res, sizeof res);
        waitpid(pid, &st, 0);
        close(fd[0]); close(fd[1]);
        printf("lrry_split: scene %u (%s) hash %016llx\n", id, SCN[id], (unsigned long long)res[0]);
        {
            double a0, fx, dy;
            memcpy(&a0, &res[3], 8); memcpy(&fx, &res[4], 8); memcpy(&dy, &res[5], 8);
            if (res[6])
                printf("lrry_split:   modelled load of the longer core over %u blocks with voices (summed instructions a sample): everything on core 0 %.0f, fixed split 0..3 / 4..7 %.0f (%.2f x), chosen each block %.0f (%.2f x); voices that changed core %u, kept it %u\n",
                       (uint32_t)res[6], a0, fx, a0 / fx, dy, a0 / dy, (uint32_t)(res[7] >> 32), (uint32_t)res[7]);
            ck("the chosen split is never worse than the fixed split in the model (summed over the scene)", dy <= fx + 1e-6);
        }
        snprintf(w, sizeof w, "scene %s sounds (%u non-zero samples of %u)", SCN[id], (uint32_t)res[1], SCN_BLOCKS * 2u * CTL);
        ck(w, WIFEXITED(st) && res[1] > 1000);
        if (id == SC_MONO)                              /* every pan 0, no stereo source: left equals right, sample for sample */
            ck("scene mono: left equals right in every sample (voices 4..7 add to both channels alike)", res[2] == 1);
    }
#ifndef LRRY_OLD_TREE
    {   /* the chooser alone: random masks and weights against a brute force over all 2^8 sets */
        uint32_t t, bad_sub = 0, bad_max = 0, nopt = 0, ntot = 0, unstable = 0, nondet = 0;
        double worst = 1.0;
        uint32_t rs = 99;
        for (t = 0; t < 20000u; t++) {
            uint16_t w[NVOICE];
            uint32_t mask, prev, c1, c1b, set, best, i, l0, l1, v, n;
            rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; mask = rs & ((1u << NVOICE) - 1u);
            rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; prev = rs & ((1u << NVOICE) - 1u);
            for (i = 0; i < NVOICE; i++) {
                rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
                w[i] = (uint16_t)(t % 3u == 0 ? 400u : 150u + rs % 900u);     /* a third of the trials have equal weights */
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
            v = lrry_cost(mask, c1, w);                 /* (cores-balance: the cost is the longer side + ho0, or all on core 0) */
            best = lrry_cost(mask, 0, w);
            (void)l0; (void)l1;
            for (set = 0; set < (1u << NVOICE); set++)
                if ((set & ~mask) == 0 && (uint32_t)__builtin_popcount(set) <= LRRY_SP.max1) {
                    uint32_t x = lrry_cost(mask, set, w);
                    if (x < best) best = x;
                }
            ntot++;
            nopt += v == best;
            if (best && (double)v / best > worst) worst = (double)v / best;
        }
        {
            char wtxt[200];
            ck("chooser: the set for core 1 is always inside the sounding voices and never larger than max1", bad_sub == 0 && bad_max == 0);
            ck("chooser: the same input gives the same set, and its own answer fed back as the previous set is left alone", nondet == 0 && unstable == 0);
            snprintf(wtxt, sizeof wtxt, "chooser: the longer core's load is the brute-force optimum in %u of %u random cases, never more than %.3f x it", nopt, ntot, worst);
            ck(wtxt, worst < 1.10 && nopt * 10u >= ntot * 7u);   /* (cores-balance: was 8 of 10. The objective is now the cost with ho0 = 42 us, so more random cases sit at the pay line, where the greedy search ends at a set a few percent off the optimum: the 1.10 bound is the same, the exact cases are checked in the rule test below) */
        }
    }
    {   /* cores-balance: THE COST-BALANCED RULE, every subset of 1..NVOICE sounding voices, three kinds of weights (a typical 128 voice, a plain SAW voice, random), each with a random previous set:
         *   once       every sounding voice is on exactly one core: core 1's set is inside the mask, core 0's is the rest, nothing twice, nothing lost
         *   balanced   equal weights: core 1 gets floor(n / 2), core 0 the rest (budget.c's n1 = k / 2)
         *   optimal    the cost (the longer side + ho0, or all on core 0) is the brute-force minimum over all sets, equal weights exactly, random weights within 10 %
         *   pays       a split is used exactly when it costs less than one core: plain SAW voices from 4 up, a typical voice from 2 up (the two-core rows of runs.csv: break-even at 4 keys of #16)
         *   constants  the split's costs are budget.c's rows (one cost for the split and the counter) */
        static const uint16_t UNI[2] = {715u, 106u};    /* audio work of a typical 128 voice (158 us) and of a plain SAW voice (23.5 us, refit 2026-11-02), units a sample */
        uint32_t kind, mask, rs = 4242, bad_once = 0, bad_bal = 0, bad_opt = 0, bad_pay = 0, ncase = 0, nsplit = 0, first_pay[2] = {99, 99}, nn;
        double worst = 1.0;
        for (kind = 0; kind < 3u; kind++)
            for (mask = 1; mask < (1u << NVOICE); mask++) {
                uint16_t w[NVOICE];
                uint32_t i, prev, c1, c0, set, best, cost, n = (uint32_t)__builtin_popcount(mask), paying = 0;
                for (i = 0; i < NVOICE; i++) {
                    rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
                    w[i] = kind < 2u ? UNI[kind] : (uint16_t)(100u + rs % 900u);
                }
                rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; prev = rs & ((1u << NVOICE) - 1u);
                c1 = lrry_split_choose(mask, w, prev);
                c0 = mask & ~c1;
                bad_once += (c1 & ~mask) != 0 || (c0 & c1) != 0 || (c0 | c1) != mask;
                best = lrry_cost(mask, 0, w);
                for (set = 1; set < (1u << NVOICE); set++)
                    if ((set & ~mask) == 0 && (uint32_t)__builtin_popcount(set) <= LRRY_SP.max1 && n >= LRRY_SP.min_total) {
                        cost = lrry_cost(mask, set, w);
                        if (cost < best) { best = cost; paying = 1; }
                    }
                cost = lrry_cost(mask, c1, w);
                ncase++;
                nsplit += c1 != 0;
                if (kind < 2u) {
                    bad_opt += cost != best;
                    bad_pay += (c1 != 0) != paying;
                    if (c1)
                        bad_bal += (uint32_t)__builtin_popcount(c1) != n / 2u;
                    if (c1 && n < first_pay[kind])
                        first_pay[kind] = n;
                } else {
                    bad_pay += c1 != 0 && cost >= lrry_cost(mask, 0, w);   /* (random weights: a split that is used must pay; how close it is to the best is the 10 % check) */
                    if (best && (double)cost / best > worst)
                        worst = (double)cost / best;
                }
            }
        nn = ncase;
        ck("rule: for every subset of the voices and every kind of weights, each sounding voice is on exactly one core", bad_once == 0);
        ck("rule: equal weights are balanced by cost, core 1 gets floor(n / 2) of the n voices (the counter's n1 = k / 2)", bad_bal == 0);
        ck("rule: equal weights give the brute-force minimum of the cost (the longer side + ho0, or all on core 0)", bad_opt == 0);
        {
            char wtxt[240];
            snprintf(wtxt, sizeof wtxt, "rule: random weights, the cost is at most %.3f x the brute-force minimum over %u cases", worst, nn);
            ck(wtxt, worst < 1.10);
        }
        ck("rule: a split is used exactly when it costs less than one core (equal weights; random: never one that does not pay)", bad_pay == 0);
        {
            char wtxt[200];
            snprintf(wtxt, sizeof wtxt, "rule: the split starts at %u typical voices and at %u plain SAW voices (this test is the plain runner: ho0 on core 0's side; refit 2026-11-02 HO0 52 us: break-even 6 SAW keys; the pipelined runner pays from 3 SAW keys: unit data #03 two cores)", first_pay[0], first_pay[1]);
            ck(wtxt, first_pay[0] == 2u && first_pay[1] == 6u);
        }
        ck("rule: the split's costs are budget.c's rows (BD_LR_HO0, BD_LR_HO1, the plain voice less its control share)",
           LRRY_SP.ho0 == BD_LR_HO0 && LRRY_SP.ho1 == BD_LR_HO1 && LRRY_SP.base == BD_LR_T(BD_LR_BASE - BD_LR_CTL) &&
           LRRY_SP.fa24 == BD_LR_T(BD_LR_FA24) && LRRY_SP.fb_lad == BD_LR_T(BD_LR_FB_LAD) && LRRY_SP.dirt == BD_LR_T(140u));   /* (BD_LR_DIRT is a per-type table: the chooser has one dirt weight, the mean 14.0 us) */
        (void)nsplit;
    }
#endif
    {   /* the allocator is L/RRY's again: 40 more notes steal, none lost */
        uint32_t n, c0, i;
        scn_setup(SC_PLAIN);
        for (n = 0; n < 40u; n++) {
            int32_t o[2 * CTL];
            trk_note_on(&trk[0], 30u + n, 100);
            mix_block(o, CTL);
        }
        c0 = 0;
        for (i = 0; i < NVOICE; i++)
            c0 += trk[0].v[i].active;
        ck("allocator: after 40 notes (steals) all 8 voices sound, none doubled or lost", c0 == NVOICE);
    }
    printf("lrry_split: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
