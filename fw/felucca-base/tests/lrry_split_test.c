/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3b: the ONE-CORE reference. The scenes of lrry_scn.h through the firmware's mix_block with no second core (DUELL_TWOCORE 0: lrry_render_stage plays all eight voices on this core).
 * Prints one hash a scene over every output sample (the master output, both channels). tests/twocore_sim.c prints the same hashes for its core-0 reference and for the two-core run, and
 * checks equality sample for sample; tests/lrry_split.sh compares the two programs' lists (and, with a checkout of the 4.3a tree, that tree's list: the control / render split changed nothing).
 * Also: the dynamic split (voice.c lrry_split_choose: which voices go to core 1 each block) on its own: random masks and weights against a brute force, stability, and on the scenes the modelled load of the
 * longer core with the split chosen each block against the fixed split (voices 0..3 / 4..7) and against everything on one core.
 * Build (from felucca-base): ../onepart.sh 1 1 lrry_split */
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
    uint32_t l0, l1, both = PR.pm & mask;
    PR.all0 += lrry_load(mask, 0, w, &l0, &l1);
    PR.fixed += lrry_load(mask, mask & 0xF0u, w, &l0, &l1);
    PR.dyn += lrry_load(mask, c1, w, &l0, &l1);
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
            rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; mask = rs & 0xFFu;
            rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; prev = rs & 0xFFu;
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
            v = lrry_load(mask, c1, w, &l0, &l1);
            best = lrry_load(mask, 0, w, &l0, &l1);
            for (set = 0; set < 256u; set++)
                if ((set & ~mask) == 0 && (uint32_t)__builtin_popcount(set) <= LRRY_SP.max1) {
                    uint32_t x = lrry_load(mask, set, w, &l0, &l1);
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
            ck(wtxt, worst < 1.10 && nopt * 10u >= ntot * 8u);
        }
    }
#endif
    {   /* the allocator is DUELL's again: 40 more notes steal, none lost */
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
