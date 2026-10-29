# L/RRY phase 4.3b: the two-core machinery splits by voice, split chosen every block (host only, 2026-10-29)

Branch `claude/vibrant-cannon-hcmhai`. Host only: nothing was built for the device, nothing was flashed, nothing ran on the unit. `fw_base_test11` and `fw_1.0_removal` were not touched.

## What it does now
- One part, 8 voices. **The split is dynamic** (second version, on the owner's decision): every block, core 0 picks which of the voices that sound go to core 1. Any voice can go to either core in any block.
- Core 0 does the control work of all 8 voices first, in voice order, as before (envelope, glide, pitch, matrix, `lrry_voice_ctl`). It writes `track_t`, so it is never split. For each voice it leaves the voice's `lrry_vc_t` and `vmod_t` in a core-0 block (`lrj`).
- The audio work (`lrry_voice`) comes after that loop. `lrry_weights` estimates what each sounding voice costs (the BD_LR rows of budget.c, only the filters that really run). `lrry_split_choose` picks the set for core 1 that makes the longer core's load smallest, counting a hand-over cost on each core. It starts from last block's set and moves or swaps voices only while that lowers the longer load by more than `stick`, and also tries a fresh longest-first set; the fresh one wins only if it is better by more than `stick`. Nothing goes to core 1 if that is no better than everything on core 0, or fewer than `min_total` voices sound.
- One core, or core 1 down, late or paused: core 0 plays all 8. Two cores: core 0 posts its set to core 1, plays the rest, then adds core 1's accumulator.
- Core 1 reads only a block copy (`tc_ctx`): the decoded patch `lrry_pp_t` (528 bytes), the voice numbers, and for each voice sent its `lrry_vc_t` and `vmod_t`. The hash covers only the entries in use (the head 560 bytes plus 88 bytes a voice). Core 1 writes only its own int32 L/R accumulator `tc_acc`, and the audio state and COMB lines of the voices it was given. No `track_t`, no matrix or LFO state.
- A voice's audio state is touched by one core at a time. While a job is open (late), its voices are core 1's: silent for core 0, and a new note on one of them waits to clear its state (`lrry_pend`).
- DUELL's protocol is kept as before (words, sequence numbers, check value, torn-read refusal, deadline, wait limit, late cases, pause, flash park, hang detection, fallback). No job is posted in a block where nothing is worth sending.
- The fixed 0-3 / 4-7 split and the allocator reorder (`VSLOT`) of the first version are gone. DUELL's allocator order is back, so `voice_alloc`, `voice_trim`, `mono_play` are as in 4.3a.

## Per file (under `fw/felucca-base/` unless named)
- `firmware/src/voice.c`: `track_render` keeps the control loop and leaves each voice's pair in `lrj`; new render stage `lrry_render_stage` / `lrry_render_set`. New: `LRRY_SP` (the tunables, a runtime struct for the bench), `lrry_weights`, `lrry_load`, `lrry_split_improve`, `lrry_split_choose`, `lrry_pick` (reuses the last answer while the sounding voices and their weights are unchanged: a held chord costs a compare of 9 words a block). `lrry_state_new`: a new voice's audio-state clear waits (`lrry_pend`) while the voice is in an open job. Test hooks, empty in the firmware: `LRRY_MASK_HOOK`, `LRRY_SPLIT_PROBE`.
- `firmware/src/twocore_run.c`: rewritten for the by-voice job. `tc_job_run` (the job's voices into `tc_acc`), `tcr_begin` (block start: settle, pause/resume, trim, fade of a stopped job's voices, `go` flag), `tcr_voices_post(c1set)`, `tcr_voices_collect`, `tcr_end`. New: `open_set`, `c1set`, `fade_mask`, `sil` (the voices of the open job, of the posted job, to fade, silent). The deadline and wait limit use the same formulas with "part 2" = the voices sent and "part 1" = fixed work + the voices core 0 keeps. `tcr_active` is gone (voices counted from the mask). TC_MEAS code is adapted (renamed fields, one new reason `TCM_OWN_NOV`).
- `firmware/src/fx.c`: `mix_block` calls `tcr_begin(n)`, `mix_part`, `tcr_end()`. The two `tcr.skip2` tests in `mix_gather` are dead (one part) and left.
- `firmware/src/twocore.h`: comments only. `twocore.c`, `twocore_reasons.h`: unchanged.
- `firmware/src/budget.c`: two-core rule by voice in `budget_solve2` (below). One-core numbers and every other number unchanged.
- `tests/lrry_scn.h` (new): 7 scenes. `tests/lrry_split_test.c` (new): the one-core program, prints one hash a scene; tests the chooser alone against a brute force; models the longer core's load per scene. `tests/twocore_sim.c`: rewritten (below). `tests/twocore_mut.sh` (new): the mutants. `tests/lrry_split.sh` (new): runs both, compares hashes, optional 4.3a tree comparison. `tests/lrry_budget_test.c` (new).
- `fw/host_tests.sh`: the `twocore` and `twocore_tsan` targets now call the new files. The old by-part mutants are gone with the by-part code. `.gitignore`: `build/gen`, `build/twocore_mut`.

## What was measured
Run from `fw/felucca-base`: `../onepart.sh 1 1 lrry_fw lrry_split lrry_budget twocore_stage1 twocore_bench`, and `tests/lrry_split.sh [4.3a tree]` for the simulation.

**Equal bit for bit: yes**, with the dynamic split.
- 7 scenes of 2000 blocks (see `lrry_scn.h`): plain, full (SUPER + PULSE, both filters, dirt, moving cutoff, LFO and matrix on CUT / OSC 1 PAN / SIDE), steal (20 notes over 8 voices), silence, burst (8 notes at once), ladcomb (LADDER then COMB, CRUSH), mono.
- The simulation ran each scene with 22 fault cases in a child process. 116 runs were compared with the one-core run sample for sample (memcmp of every output sample, not a hash): 232,000 blocks, 14,848,000 samples, 0 differences. Includes note on/off at every block position, steals, silence, matrix and LFOs running, the split moving between blocks, and every late path (late_pre: core 0 took the job back and played core 1's voices itself in the same half; late_post, stale, noans, voff: core 0 waited for a very late job; torn reads, park, unarmed, refused jobs).
- The rule used: wherever no block of a job's voices was dropped, the output must equal one core. Runs that drop blocks on purpose (hang_pre, hang_post, park_stuck, silent_late, vlong, shared) are not compared that way. The hang cases are compared with a one-core run that silences exactly the voices (a mask per block, recorded in the fault run) and applies the same fade: equal.
- Independent one-core check: `lrry_split_test` (built with no second-core code) prints the same 7 scene hashes as the simulation's reference run. The 4.3a commit (e937e4d), built the same way (`-DLRRY_OLD_TREE=1`), prints the same 7 hashes: the control/render split changed nothing in the one-core output.
- `lrry_fw`: 53 checks, 0 fail. `lrry_split`: 19 checks, 0 fail. `lrry_budget` 22, `twocore_stage1` 41 and `twocore_bench` 18 pass. Simulation: 921 checks, 0 failed (10 runs of the final code, no flake after the harness fixes below).
- Harness fixes (not firmware): the heartbeat guard raced the firmware's beat record; the guard was off for the hang faults from the first block. A hung job now also gets a test of the deferred clear (all notes off, short release, 8 new notes while the job is open).
- Ownership audit (run `none`, every job): core 1's job changed nothing of core 0's state (track, block scratch, accumulators, patch, the voices core 0 keeps and their COMB lines, `lrj`, `mix_l/r`): 0 writes. Core 0 changed nothing of core 1's voices between the post and the job's start, nor while a job was open however late, nor while it hung: 0 writes.
- ThreadSanitizer: all 7 scenes, 0 warnings, all faults except hang_pre, hang_post, park_stuck, silent_late (core 0 renders while core 1 is stuck, by design) and ahead (the simulated halt does not stop core 1's thread).

**The split itself**
- Chooser against a brute force over all 2^8 sets (20,000 random masks and weights): the longer core's load is the optimum in 85 % of the cases and never above 1.056 x it; same input gives the same set; its own answer fed back is left alone; the set is always inside the sounding voices.
- Modelled load of the longer core (the estimate of `lrry_weights`, summed over each scene's blocks with voices), speed-up over everything on core 0: fixed split 0..3 / 4..7 against the chosen split: plain 1.92 / 1.92, full 1.98 / 1.98, steal 1.92 / 1.95, silence 1.65 / 1.75, burst 1.96 / 1.96, ladcomb 1.34 / 1.80, mono 1.84 / 1.84. Where the fixed split was already balanced the choice changes nothing. It wins when the sounding voices are not spread over both halves of the voice list, and when voices differ in cost (ladcomb: filters that do not run for some voices). These are model numbers; the bench replaces them.
- In the simulation the voices stay where they are: 3 to 73 moves against 8,000 to 16,000 voice-blocks that kept their core, per scene.
- Cost of the choice on the Mac: 0.5 to 0.7 microseconds per call, about 10 % of a block of 8 voices, with the other tests running; `lrry_pick` skips it while the sounding voices and weights do not change. Not measured on the unit.

**Mutants: each caught** (`tests/twocore_mut.sh`; a copy of the firmware with one rule broken; the unbroken copy passes first):
- NOWAIT (core 0 adds core 1's sums without waiting for done): caught (462 checks fail).
- DOUBLE (core 0 also plays the lowest voice of core 1's set): caught (263).
- MISSING (voice 0 is played by neither core when it is core 0's): caught (123).
- STALE (the patch copy is not refreshed): caught (19; scene full).
- SHARED (core 1 adds into core 0's block instead of its own accumulator): caught (10; the ownership audit).
- SHARED_STATE (core 1 uses the wrong voice's state), SPLIT_NOT_SOUNDING (a voice that does not sound is handed over), PEND_OFF (the deferred clear removed; caught by the hung-job test, 14 checks): caught.
- FADE_OFF, VLATE_RUN, VWIN_ON, NOANS_NOPAUSE, FAULT_NOPAUSE, SILENT_NOPAUSE, WAIT_TO_HALF_END, NO_HASH: all caught (16 mutants in the full run; the first 8 above re-run after the last edit).
- Self-check `shared` (core 1 halves one patch value of core 0's): the output changes, the test sees it.
- The proven model `host/lrry_split_test.c` with its own mutants is untouched.

## Voices available with two cores (budget.c, an ESTIMATE)
Not changed for the dynamic split, as asked (the split is the best case of the same rule). Rule (`budget_solve2`, two cores): k sounding voices sit ceil(k/2) on core 0 and floor(k/2) on core 1 (equal voices; the chooser does as well or better with unequal ones). Core 0 load = globals + part fixed + ceil(k/2) whole voices + floor(k/2) x 6 % of a voice (control work that stays on core 0) + 40 instructions a sample (hand-over). Core 1 load = floor(k/2) x 94 % of a voice + 45. Both must fit `BUDGET_LIMIT` (84 %). Result is capped at 8.

How the constants were found (none measured on the unit):
- 6 %: Mac timing with the render stage switched off, 8 voices, 3000 blocks, best of 5: control is 5.5 % of a voice for OSC 1 SAW alone, 2.3 % for LADDER + dirt, 2.2 % for the SUPER patch.
- 40 / 45: counted from the code: 222 words of copy and hash on each side, about 4 instructions a word, 32 samples a block.

| patch (of PHASE43A_REPORT) | voice | one core (unchanged) | two cores | core 0 / core 1 load at that number |
|---|---|---|---|---|
| OSC 1 SAW alone | 9.8 % | 6 | 8 | 63.7 % / 38.3 % |
| two oscillators | 13.3 % | 4 | 8 | 78.6 % / 51.5 % |
| SUPER 7 + OSC 2 | 27.3 % | 2 | 4 | 80.1 % / 52.8 % |
| A BP + B HP | 21.0 % | 3 | 4 | 66.7 % / 40.9 % |
| LADDER | 21.9 % | 2 | 4 | 68.6 % / 42.6 % |
| two dirt units + filters | 27.0 % | 2 | 4 | 79.4 % / 52.1 % |

Reading: core 1 is half empty because core 0 carries the globals (18.9 %) and the fixed work. Two cores give about 1.3 to 2 times the notes, not 2 times. The unit's bench replaces this.

## NOT done, NOT verified
- The device was never run. Real core-1 start-up with this job, hot RAM (the job, `tc_ctx`, `tc_acc`, `lrry_voice` are not placed in RAM; 888 + 256 bytes plus `lrj` 708 bytes new), pool tail, flash park timing and ISR timing are unchecked. `lrry_voice` must run from core 1's RAM; not arranged.
- Cache lines: `lrry_vs` elements are 248 bytes and the COMB slices are per voice; two voices on different cores can share a line. Not padded. With a dynamic split a voice also changes core now and then, which costs a cold cache on a chip that has one (the `stick` setting limits it).
- The weights (`LRRY_SP`) are the estimates of budget.c without margin; the hand-over cost (40 / 45) is counted, not measured. The split is only as good as these. The bench should print, per block, each core's real time and tune `LRRY_SP` (it is a runtime struct for that).
- `tests/deadline_test.c` and `tests/budget_test.c` do not compile in this tree (known); the deadline function changed shape, so the old `deadline_test` expectations would need a rewrite. Not done. The deadline now reads the voice counts from the mask; its numbers for the new split are not checked against anything but the simulation.
- TC_MEAS (bench) code in `twocore_run.c` is adapted; it compiles and scene 0 passes with `-DTC_MEAS=1`. The old TC_MEAS scenes (tcmclr, vrun, vwin, floor, floorx, nofloor) are not ported.
- Cost model finding (not changed): `bd_lrry_voice` already applies a 1.15 margin and the deadline then applies `BUDGET_DL_MARGIN` 1.15 again. Result: a voice costs 11 % to 50 % of a half in the deadline, and 8 voices of a heavier patch have "no take-over" (the post goes out with the wait limit as its deadline). In the simulation that is 499 of 1001 jobs in scene full, 497 of 1990 in burst, 58 of 2000 in ladcomb, 0 in the others. In those blocks a late job is a silent block and a fault pause, not a take-over, so the late-path equality was only checked where nothing was dropped (the four light scenes cover all of it).
- The UI voice counter (`ui_draw.c`), pages, editor, parameters and master stage were not touched.
- A fault found by the runner mid-block (refused job, word ahead) sets the fade for the next block's start, not the same block (DUELL faded before playing part 2 in the same block). One block of the job's voices may play from a half-written state in that rare case.
- Build tooling here was stale and is not fixed: `tools/gen_duell_sounds.py` expects 28 extension slots (now 64) and rejects FREEC values above 3; `gen_aa_font` needs the `barlow` preset and `fontTools`. To get `build/gen` I used a scratch wrapper outside the repo. `build/gen` is not committed.

## Open choices for the owner
1. **Control stays on core 0.** Core 1 only renders. Option: also move the control work of some voices to core 1 (needs a per-core copy of the matrix window, LFO values and `track_t` reads; saves 2 to 6 % of a voice on core 0, costs a lot of copying and risk).
2. **Tunables** (`LRRY_SP` in voice.c): the voice rows, `ho0` 40 / `ho1` 45, `stick` 0 (a voice moves whenever that lowers the longer load at all; raise it to keep voices on their core), `max1` 6, `min_total` 2. All are guesses until the bench.
3. **Exhaustive search.** The chooser is a local search plus a longest-first start (85 % optimal, worst 5.6 % off). An exact search over the 256 sets costs about 1,000 instructions a block when the sounding voices change. Not done.
4. **Hand-over cost and the whole-copy hash.** The hash covers 560 bytes of head and 88 bytes a voice sent, twice per block. Options: hash less of the patch; or drop it (mutant NO_HASH shows a torn copy then reaches the output).
5. **Budget constants** (`BD_LR_CTL_PCT` 6, `BD_LR_HO0` 40, `BD_LR_HO1` 45) and whether the hand-over is charged at all. Also the double 1.15 margin: `bd_lrry_voice` applies one and the deadline another. Not changed, as asked.
6. **Where the job is posted.** After the control work, inside the voice stage (the earliest point at which the data exists); DUELL posted at the block start.
7. **Voices of an open job.** Silent, as DUELL's part 2 was; new notes on them wait to clear their state. Option: refuse notes on those voices.
8. **Fade timing on a mid-block fault**: a fault found while core 0 plays (refused job, word ahead) fades the job's voices at the next block's start, not the same block (one block may play from a half-written state). Option: silence them for that block and fade.
9. **UI.** `ui_draw.c` reads `trk_nvoice` and the voice list by index; with DUELL's allocator order back it shows what it showed in 4.3a. Not touched.
