/* SPDX-License-Identifier: GPL-3.0-only
 * voice-cheaper: the HOST COST TOOL of the voice kernel (report only, no checks): host instructions of one lrry_voice call (one block of 32 samples) for every feature of the voice, in the
 * configurations of the unit's own `bench vk` (firmware/src/bench.c bvk_cfg: a plain SAW at 220 Hz and one feature added per line), plus the dirt types one by one, filter B's SVF modes, SUPER
 * with 1 / 3 / 5 / 7 saws and a few stacks. Instructions come from proc_pid_rusage (macOS), the scale of tests/cpu_bench.c and budget.c: 1 us of a 725 us block = 144.8 host instructions
 * (BD_LR_T: 4.526 instructions a sample x 32), so  us = instructions a block / 144.8.   The host core (arm64) is not the unit's core (a 64-bit product is one instruction here): the figures rank
 * and compare, the unit's own numbers are in PHASE62_VOICE_COST_REPORT.md; the CAL column prints the unit's number where the report has one.
 * Build (from lrry, after ../gen_only.py):  cc -O1 -fno-builtin -w -DHOST_MIX_FOLLOWS_MASTER=1 -DLRRY_TEST_MODE=1 -Ibuild/gen_test -Ibuild/gen -Ifirmware/src -Itests -DNPART=1 -DNSHOWN=1
 *      -DLRRY_VERSION_SHOWN='"x"' -o OUT tests/lrry_voice_cost_test.c -lm        run: OUT [reps]  (prints one table; `OUT digest` prints a hash of the kernel's output over all configurations) */
#include <stddef.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#ifdef __APPLE__
#include <libproc.h>
#include <sys/resource.h>
#endif

static uint64_t instr_now(void)
{
#ifdef __APPLE__
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
#endif
    return 0;
}
static lrry_pp_t pp;
static lrry_vs_t vs;
static int32_t oL[CTL], oR[CTL];
enum { N_CFG = 64 };
typedef struct { const char *name; double cal; } cfg_t;   /* cal: the unit's us (report 8.3 / section 1, kernel bench), 0 = none */
static const cfg_t CFG[] = {
    {"EMPTY", 16.1}, {"SAW", 22}, {"TRI", 0}, {"SINE", 0}, {"PULSE", 0}, {"SYNC", 0}, {"FM", 0}, {"FOLD osc", 0}, {"SUPER7", 78}, {"SUPER1", 0}, {"SUPER3", 0}, {"SUPER5", 0},
    {"SAW+OSC2 SAW", 30}, {"SAW+NOISE", 0}, {"SAW+PINK", 0}, {"SAW+SUB sine", 0}, {"SAW+SUB pulse", 49}, {"SAW+RING", 0},
    {"FA12 LP steady", 0}, {"FA12 BLEND", 59}, {"FA24 BLEND", 66}, {"FA24 LP steady", 0},
    {"FB SVF LP", 0}, {"FB SVF HP", 0}, {"FB LADDER", 75}, {"FB COMB", 0},
    {"DIRT A SOFT", 0}, {"DIRT A HARD", 0}, {"DIRT A FOLD", 0}, {"DIRT A FUZZ", 0}, {"DIRT A CRUSH", 0}, {"DIRT A+B", 44},
    {"PAN RAMP (filter A pan)", 49}, {"FA12+FB SVF", 0}, {"FULL STRESS", 322},
    {"2nd stage fade (SLOPE moving)", 0}, {"FA12+FB SVF serial", 0},
    {"BUS B source only (no filter B)", 0}, {"FA12 LP + pan steady", 0}, {"FA12 HP blend steady", 0}
};
#define NCFG ((uint32_t)(sizeof CFG / sizeof CFG[0]))
static void cfg(uint32_t idx, lrry_vc_t *C, vmod_t *m)
{
    lrry_pp_t *P = &pp;
    uint32_t k;
    const char *nm = CFG[idx].name;
#define IS(s) (!strcmp(nm, s))
    memset(P, 0, sizeof *P);
    memset(C, 0, sizeof *C);
    C->inc[0] = 21418254u; C->inc[1] = 21440000u; C->incs = 10709127u; C->pos_a = -1; C->pos_b = -1; C->uc = 40 << 8; C->pp = 0; C->vi = 0;
    m->amp0 = 20000; m->amp1 = 20400;
    P->sub_tg[0] = P->sub_tg[1] = LRRY_ONE;
    for (k = 0; k < 2u; k++) { P->model[k] = LM_SAW; P->ratio[k] = 65536u * 3u / 2u; P->pw[k] = 0x80000000u; P->fmidx[k] = 5ll << 30; P->fold[k] = 3 << 24; }
    P->d_bits[0] = P->d_bits[1] = 24; P->d_drive[0] = P->d_drive[1] = 40000000; P->d_lvl[0] = P->d_lvl[1] = Q28C(0.9);
    P->fa_res = 40; P->fb_res = 40; P->fb_comb_fb = Q31C(0.7); P->comb_damp = Q31C(0.3); P->fa_mode = 0; P->fb_mode = LF_LP;
#define GAIN(s, g) do { P->tg[s][0][0] = P->tg[s][0][1] = P->tg_off[s][0][0] = P->tg_off[s][0][1] = (g); P->tg[s][1][0] = P->tg[s][1][1] = (g); } while (0)
    if (!IS("EMPTY"))
        GAIN(LS_O1, Q28C(0.7));
    if (IS("TRI")) P->model[0] = LM_TRI;
    if (IS("SINE")) P->model[0] = LM_SINE;
    if (IS("PULSE")) P->model[0] = LM_PULSE;
    if (IS("SYNC")) P->model[0] = LM_SYNC;
    if (IS("FM")) P->model[0] = LM_FM;
    if (IS("FOLD osc")) P->model[0] = LM_FOLD;
    if (!strncmp(nm, "SUPER", 5) || IS("FULL STRESS")) {
        uint32_t ns = IS("SUPER1") ? 1 : IS("SUPER3") ? 3 : IS("SUPER5") ? 5 : 7;
        P->model[0] = LM_SUPER; P->sup_n[0] = (uint8_t)ns;
        for (k = 0; k < ns; k++) { P->sup_mul[0][k] = (uint32_t)(Q28C(1.0) + ((int32_t)k - 3) * 300000); P->sup_pl[0][k] = P->sup_pr[0][k] = Q28C(0.5); }
        P->lvp[0][0][0] = P->lvp[0][0][1] = P->lvp_off[0][0][0] = P->lvp_off[0][0][1] = Q28C(0.7); P->lvp[0][1][0] = P->lvp[0][1][1] = Q28C(0.7);
    }
    if (IS("SAW+OSC2 SAW") || IS("SAW+RING")) GAIN(LS_O2, Q28C(0.5));
    if (IS("SAW+NOISE")) GAIN(LS_NOISE, Q28C(0.3));
    if (IS("SAW+PINK")) { GAIN(LS_NOISE, Q28C(0.3)); P->nz_type = 1; }
    if (IS("SAW+SUB sine")) { P->sub_g = Q28C(0.5); P->sub_wave = 0; }
    if (IS("SAW+SUB pulse")) { P->sub_g = Q28C(0.5); P->sub_wave = 3; }
    if (IS("SAW+RING")) GAIN(LS_RING, Q28C(0.4));
    if (IS("FA12 LP + pan steady")) { C->pos_a = 90; C->uc = 80 << 8; P->pa.side = 1; P->pa.g = Q28C(0.6); P->pa0 = P->pa; }
    if (IS("FA12 HP blend steady")) { C->pos_a = 90; C->uc = 80 << 8; P->fa_mode = 127; P->fa_always = 1; }
    if (IS("FA12 LP steady") || IS("FA24 LP steady")) { C->pos_a = 90; P->fa_p24 = IS("FA24 LP steady"); C->uc = 80 << 8; }
    if (IS("FA12 BLEND") || IS("FA24 BLEND") || IS("PAN RAMP (filter A pan)") || IS("FA12+FB SVF") || IS("FA12+FB SVF serial") || IS("FULL STRESS") || IS("2nd stage fade (SLOPE moving)")) {
        C->pos_a = 90; C->uc = 80 << 8;
        P->fa_p24 = IS("FA24 BLEND") || IS("FULL STRESS");
        if (!IS("FULL STRESS") || 1) { P->fa_mode = 64; P->fa_always = 1; }
        if (IS("PAN RAMP (filter A pan)")) { P->pa.side = 1; P->pa.g = Q28C(0.6); P->pa0.side = 1; P->pa0.g = Q28C(0.9); }
    }
    if (IS("FB SVF LP") || IS("FB SVF HP") || IS("FA12+FB SVF") || IS("FA12+FB SVF serial")) { P->fb_kind = LB_SVF; C->pos_b = 80; if (IS("FB SVF HP")) P->fb_mode = LF_HP; }
    if (IS("FA12+FB SVF serial")) P->f_serial = 1;
    if (IS("FB LADDER") || IS("FULL STRESS")) { P->fb_kind = LB_LADDER; C->pos_b = 80; }
    if (IS("FB COMB")) { P->fb_kind = LB_COMB; C->pos_b = 0; C->comb_d = 200; }
    if (IS("DIRT A SOFT") || IS("DIRT A+B")) { P->d_type[0] = LD_SOFT; P->any_dirt = 1; }
    if (IS("DIRT A HARD")) { P->d_type[0] = LD_HARD; P->any_dirt = 1; }
    if (IS("DIRT A FOLD")) { P->d_type[0] = LD_FOLD; P->any_dirt = 1; }
    if (IS("DIRT A FUZZ")) { P->d_type[0] = LD_FUZZ; P->any_dirt = 1; }
    if (IS("DIRT A CRUSH")) { P->d_type[0] = LD_CRUSH; P->d_bits[0] = 8; P->any_dirt = 1; }
    if (IS("DIRT A+B") || IS("FULL STRESS")) { P->d_type[1] = LD_HARD; P->any_dirt = 1; P->d_serial = 1; }
    if (IS("FULL STRESS")) {
        P->d_type[0] = LD_FUZZ; P->sub_g = Q28C(0.5); GAIN(LS_O2, Q28C(0.5)); GAIN(LS_NOISE, Q28C(0.3)); GAIN(LS_RING, Q28C(0.4)); P->model[1] = LM_SUPER; P->sup_n[1] = 7;
        for (k = 0; k < 7u; k++) { P->sup_mul[1][k] = (uint32_t)(Q28C(1.0) + ((int32_t)k - 3) * 200000); P->sup_pl[1][k] = P->sup_pr[1][k] = Q28C(0.5); }
        P->lvp[1][0][0] = P->lvp[1][0][1] = P->lvp_off[1][0][0] = P->lvp_off[1][0][1] = Q28C(0.5); P->lvp[1][1][0] = P->lvp[1][1][1] = Q28C(0.5);
    }
}
static uint64_t hsh;
static void hash_out(void)
{
    uint32_t i;
    for (i = 0; i < CTL; i++) { hsh = (hsh ^ (uint32_t)oL[i]) * 1099511628211ull; hsh = (hsh ^ (uint32_t)oR[i]) * 1099511628211ull; }
}
int main(int argc, char **argv)
{
    uint32_t reps = 400, idx, r, hashmode = 0;
    double empty = 0, saw = 0;
    if (argc > 1 && !strcmp(argv[1], "digest")) hashmode = 1; else if (argc > 1) reps = (uint32_t)atoi(argv[1]);
    if (!hashmode)
        printf("%-34s %12s %9s %9s %9s %9s %9s\n", "feature (bench vk config)", "instr/block", "us/block", "instr/smp", "over SAW", "unit us", "setup us");
    for (idx = 0; idx < NCFG; idx++) {
        lrry_vc_t C;
        vmod_t m;
        uint64_t a, b, n = 0;
        double ins, us, setup;
        memset(&m, 0, sizeof m);
        cfg(idx, &C, &m);
        lrry_state_clear(&vs, 60u);
        for (r = 0; r < 3; r++) { memset(oL, 0, sizeof oL); memset(oR, 0, sizeof oR); lrry_voice(&pp, &C, &vs, &m, oL, oR, CTL); }
        if (hashmode) {
            for (r = 0; r < 8; r++) { lrry_voice(&pp, &C, &vs, &m, oL, oR, CTL); hash_out(); }
            continue;
        }
        a = instr_now();
        for (r = 0; r < reps; r++) { lrry_voice(&pp, &C, &vs, &m, oL, oR, CTL); n += (uint32_t)oL[r % CTL]; }
        b = instr_now();
        ins = (double)(b - a) / reps - 4.0;
        a = instr_now();
        for (r = 0; r < reps; r++) lrry_voice(&pp, &C, &vs, &m, oL, oR, 0);
        b = instr_now();
        setup = ((double)(b - a) / reps - 4.0) / 144.8;
        us = ins / 144.8;
        if (idx == 0) empty = ins;
        if (idx == 1) saw = ins;
        {
            char cb[16] = "-";
            if (CFG[idx].cal > 0)
                snprintf(cb, sizeof cb, "%.1f", CFG[idx].cal);
            printf("%-34s %12.0f %9.2f %9.1f %+9.2f %9s %9.2f\n", CFG[idx].name, ins, us, ins / CTL, idx >= 2 ? (ins - saw) / 144.8 : 0.0, cb, setup);
        }
        (void)n;
    }
    if (hashmode)
        printf("hash %016llx\n", (unsigned long long)hsh);
    else
        printf("(EMPTY %.0f instr, SAW %.0f instr per block)\n", empty, saw);
    return 0;
}
