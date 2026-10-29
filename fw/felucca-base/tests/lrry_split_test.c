/* SPDX-License-Identifier: GPL-3.0-only
 * L/RRY phase 4.3b: the ONE-CORE reference. The scenes of lrry_scn.h through the firmware's mix_block with no second core (DUELL_TWOCORE 0: lrry_render_stage plays all eight voices on this core).
 * Prints one hash a scene over every output sample (the master output, both channels). tests/twocore_sim.c prints the same hashes for its core-0 reference and for the two-core run, and
 * checks equality sample for sample; tests/lrry_split.sh compares the two programs' lists (and, with a checkout of the 4.3a tree, that tree's list: the control / render split changed nothing).
 * Also: the allocator puts the notes on both cores (VSLOT), the stereo mix is the same left / right for a mono patch.
 * Build (from felucca-base): ../onepart.sh 1 1 lrry_split */
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

static int32_t keep_buf[(size_t)SCN_BLOCKS * 2u * CTL];
/* one scene in a child process (the firmware's globals start at power-on state, as in tests/twocore_sim.c's runs): the hash and the check results come back through a pipe */
int main(void)
{
    uint32_t id;
    for (id = 0; id < SC_N; id++) {
        int fd[2], st = 0;
        uint64_t res[3] = {0, 0, 0};
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
            write(fd[1], res, sizeof res);
            _exit(0);
        }
        read(fd[0], res, sizeof res);
        waitpid(pid, &st, 0);
        close(fd[0]); close(fd[1]);
        printf("lrry_split: scene %u (%s) hash %016llx\n", id, SCN[id], (unsigned long long)res[0]);
        snprintf(w, sizeof w, "scene %s sounds (%u non-zero samples of %u)", SCN[id], (uint32_t)res[1], SCN_BLOCKS * 2u * CTL);
        ck(w, WIFEXITED(st) && res[1] > 1000);
        if (id == SC_MONO)                              /* every pan 0, no stereo source: left equals right, sample for sample */
            ck("scene mono: left equals right in every sample (voices 4..7 add to both channels alike)", res[2] == 1);
    }
    {   /* the allocator: n notes put ceil(n / 2) voices on core 0's side (0..3) and floor(n / 2) on core 1's (4..7), and 40 more notes steal without leaving a voice out */
        uint32_t n, c0, c1, i, ok = 1;
        scn_setup(SC_PLAIN);
        for (n = 1; n <= NVOICE; n++) {
            trk_note_on(&trk[0], 40u + n, 100);
            { int32_t o0[2 * CTL]; mix_block(o0, CTL); }
            c0 = c1 = 0;
            for (i = 0; i < NVOICE; i++)
                if (trk[0].v[i].active)
                {
                    if (i < LRRY_C1_FIRST) c0++; else c1++;
                }
            ok &= c0 == (n + 1u) / 2u && c1 == n / 2u;
        }
        ck("allocator: with 1..8 notes the cores carry ceil(n/2) and floor(n/2) voices", ok);
        for (n = 0; n < 40u; n++) {
            int32_t o[2 * CTL];
            trk_note_on(&trk[0], 30u + n, 100);
            mix_block(o, CTL);
        }
        c0 = 0;
        for (i = 0; i < NVOICE; i++)
            c0 += trk[0].v[i].active;
        ck("allocator: after 40 more notes (steals) all 8 voices sound, none doubled or lost", c0 == NVOICE);
    }
    printf("lrry_split: %d checks, %d FAIL\n", ncheck, nfail);
    return nfail != 0;
}
