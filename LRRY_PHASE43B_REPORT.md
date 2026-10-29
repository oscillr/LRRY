# L/RRY phase 4.3b: the two-core machinery splits by voice (host only, 2026-10-29)

Branch `claude/vibrant-cannon-hcmhai`. Host only: nothing was built for the device, nothing was flashed, nothing ran on the unit. `fw_base_test11` and `fw_1.0_removal` were not touched.

## What it does now
- There is one part with 8 voices. Core 0 plays voices 0..3 and everything else. Core 1 plays voices 4..7, one job a block.
- Core 0 does the control work of all 8 voices first, in voice order, as before (envelope, glide, pitch, matrix, `lrry_voice_ctl`). It writes `track_t`, so it is never split. For each voice it leaves the voice's `lrry_vc_t` and `vmod_t` in a core-0 block (`lrj`).
- The audio work (`lrry_voice`) comes after the loop. One core, or core 1 down, late or paused: core 0 plays all 8. Two cores: core 0 posts voices 4..7 to core 1, plays 0..3, then adds core 1's accumulator.
- Core 1 reads only a block copy (`tc_ctx`): the decoded patch `lrry_pp_t` (528 bytes), 4 x `lrry_vc_t`, 4 x `vmod_t`, a mask. The whole copy is 888 bytes with a 16-bit hash in `arg`. Core 1 writes only its own int32 L/R accumulator `tc_acc`, the audio state of voices 4..7 and their COMB lines (slices of the part's engine memory). No `track_t`, no matrix or LFO state.
- DUELL's protocol is kept: the words, sequence numbers, check value, torn-read re-read and refusal, the deadline per half-block, the wait limit, the three late cases (yld, claimed, nothing), pausing after 4 late blocks, flash park, heartbeat hang detection, the strike rules, fallback to one core with all 8 voices.
- No job is posted in a block where voices 4..7 have nothing to render.

## Per file (under `fw/felucca-base/` unless named)
- `firmware/src/voice.c`: `track_render` keeps the control loop and leaves each voice's pair in `lrj`; new `lrry_render_stage` / `lrry_render_range` (the render stage). `VSLOT`: the allocator's slot j is voice 0,4,1,5,2,6,3,7, so n notes sit ceil(n/2) on core 0 and floor(n/2) on core 1. Used in `voice_alloc`, `voice_trim`, `vs_sounding`, `mono_play`. `lrry_state_new`: a new voice's audio-state clear waits (`lrry_pend`) while a job is open and core 1 may still write voices 4..7. `LRRY_MASK_HOOK`: an empty test hook (not defined in the firmware).
- `firmware/src/twocore_run.c`: rewritten for the by-voice job. `tc_job_run` (voices 4..7 into `tc_acc`), `tcr_begin` (block start: settle, pause/resume, trim, fade, `go` flag), `tcr_voices_post`, `tcr_voices_collect`, `tcr_end`. The deadline and wait limit use the same formulas with "part 2" = voices 4..7 and "part 1" = fixed work + voices 0..3. `tcr_active` is gone (voices counted from the mask). TC_MEAS code is adapted (renamed fields, one new reason `TCM_OWN_NOV`).
- `firmware/src/fx.c`: `mix_block` calls `tcr_begin(n)`, `mix_part`, `tcr_end()`. The two `tcr.skip2` tests in `mix_gather` are dead (one part) and left.
- `firmware/src/twocore.h`: comments only. `twocore.c`, `twocore_reasons.h`: unchanged.
- `firmware/src/budget.c`: two-core rule by voice in `budget_solve2` (below). One-core numbers and every other number unchanged.
- `tests/lrry_scn.h` (new): 7 scenes. `tests/lrry_split_test.c` (new): the one-core program, prints one hash a scene, checks the allocator. `tests/twocore_sim.c`: rewritten (below). `tests/twocore_mut.sh` (new): the mutants. `tests/lrry_split.sh` (new): runs both, compares hashes, optional 4.3a tree comparison. `tests/lrry_budget_test.c` (new).
- `fw/host_tests.sh`: the `twocore` and `twocore_tsan` targets now call the new files. The old by-part mutants are gone with the by-part code. `.gitignore`: `build/gen`, `build/twocore_mut`.

## What was measured
Run from `fw/felucca-base`: `../onepart.sh 1 1 lrry_fw lrry_split lrry_budget twocore_stage1 twocore_bench`, and `tests/lrry_split.sh [4.3a tree]` for the simulation.

**Equal bit for bit: yes.**
- 7 scenes of 2000 blocks (see `lrry_scn.h`): plain, full (SUPER + PULSE, both filters, dirt, moving cutoff, LFO and matrix on CUT / OSC 1 PAN / SIDE), steal (20 notes over 8 voices), silence, burst (8 notes at once), ladcomb (LADDER then COMB, CRUSH), mono.
- The simulation ran each scene with 22 fault cases in a child process. 118 runs were compared with the one-core run sample for sample (memcmp of every output sample, not a hash): 236,000 blocks, 15,104,000 samples, 0 differences. Includes note on/off at every block position, steals across voices 3/4, silence, matrix and LFOs running, and every late path (late_pre: core 0 took the job back and played voices 4..7 itself in the same half; late_post, stale, noans, voff: core 0 waited for a very late job; torn reads, park, unarmed, refused jobs).
- The rule used: wherever no block of voices 4..7 was dropped, the output must equal one core. Runs that drop blocks on purpose (hang_pre, hang_post, park_stuck, silent_late, vlong, shared) are not compared that way. The hang cases are compared with a one-core run that silences exactly those blocks (and the fade): equal.
- Independent one-core check: `lrry_split_test` (built with no second-core code) prints the same 7 scene hashes as the simulation's reference run. The 4.3a commit (e937e4d), built the same way, prints the same 7 hashes too: the control/render split and the new allocator order changed nothing in the one-core output.
- `lrry_fw`: 53 checks, 0 fail. `lrry_split`: 10 checks, 0 fail (allocator: with 1..8 notes the cores carry ceil/floor of n/2; after 40 more notes all 8 voices sound). `twocore_stage1` 41 checks and `twocore_bench` 18 checks still pass. Simulation: 923 checks, 0 failed in 4 runs of the final code. Earlier runs showed two flakes in the simulation's own harness (the heartbeat guard raced the firmware's beat record; the guard was off for the hang faults from the first block); both fixed in `twocore_sim.c`, not in the firmware.
- Ownership audit (run `none`, every job): core 1's job changed nothing of core 0's state (track, block scratch, accumulators, patch, voices 0..3 and their COMB lines, `lrj`, `mix_l/r`): 0 writes. Core 0 changed nothing of core 1's state between the post and the job's start, and nothing while a job was open however late it was: 0 writes. Checked in every fault run.
- ThreadSanitizer: all 7 scenes, 0 warnings, all faults except hang_pre, hang_post, park_stuck, silent_late (core 0 renders while core 1 is stuck, by design) and ahead (the simulated halt does not stop core 1's thread).

**Mutants: each caught** (`tests/twocore_mut.sh`; a copy of the firmware with one rule broken; the unbroken copy passes first, 918 checks):
- NOWAIT (core 0 adds core 1's sums without waiting for done): caught (490 checks fail; first the open-job audit, also the output).
- DOUBLE (core 0 also plays voice 4): caught (183).
- STALE (the patch copy is not refreshed): caught (21; scene full: the output differs).
- SHARED (core 1 adds into core 0's block instead of its own accumulator): caught (8; the ownership audit).
- Also: SHARED_STATE (core 1 plays voices 0..3's state) caught; PEND_OFF (the deferred clear removed) caught, but by one check only (scene vlong, open-job audit); FADE_OFF, VLATE_RUN, VWIN_ON, NOANS_NOPAUSE, FAULT_NOPAUSE, SILENT_NOPAUSE, WAIT_TO_HALF_END, NO_HASH: all caught.
- Self-check `shared` (core 1 halves one patch value of core 0's): the output changes, the test sees it.
- The proven model `host/lrry_split_test.c` with its own mutants is untouched.

## Voices available with two cores (budget.c, an ESTIMATE)
Rule (`budget_solve2`, two cores): k notes sit ceil(k/2) on core 0 and floor(k/2) on core 1. Core 0 load = globals + part fixed + ceil(k/2) whole voices + floor(k/2) x 6 % of a voice (control work that stays on core 0) + 40 instructions a sample (hand-over). Core 1 load = floor(k/2) x 94 % of a voice + 45. Both must fit `BUDGET_LIMIT` (84 %). Result is capped at 8.

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
- Cache lines: `lrry_vs[0][3]` and `[4]` (248 bytes each) and the COMB slices of voice 3/4 can share a line between the cores. Not padded.
- `tests/deadline_test.c` and `tests/budget_test.c` do not compile in this tree (known); the deadline function changed shape, so the old `deadline_test` expectations would need a rewrite. Not done. The deadline now reads the voice counts from the mask; its numbers for the new split are not checked against anything but the simulation.
- TC_MEAS (bench) code in `twocore_run.c` is adapted; it compiles and scene 0 passes with `-DTC_MEAS=1`. The old TC_MEAS scenes (tcmclr, vrun, vwin, floor, floorx, nofloor) are not ported.
- Cost model finding (not changed): `bd_lrry_voice` already applies a 1.15 margin and the deadline then applies `BUDGET_DL_MARGIN` 1.15 again. Result: a voice costs 11 % to 50 % of a half in the deadline, and 8 voices of a heavier patch have "no take-over" (the post goes out with the wait limit as its deadline). In the simulation that is 499 of 1001 jobs in scene full, 497 of 1990 in burst, 58 of 2000 in ladcomb, 0 in the others. In those blocks a late job is a silent block and a fault pause, not a take-over, so the late-path equality was only checked where nothing was dropped (the four light scenes cover all of it).
- The UI voice counter (`ui_draw.c`), pages, editor, parameters and master stage were not touched.
- A fault found by the runner mid-block (refused job, word ahead) sets the fade for the next block's start, not the same block (DUELL faded before playing part 2 in the same block). One block of voices 4..7 may play from a half-written state in that rare case.
- Build tooling here was stale and is not fixed: `tools/gen_duell_sounds.py` expects 28 extension slots (now 64) and rejects FREEC values above 3; `gen_aa_font` needs the `barlow` preset and `fontTools`. To get `build/gen` I used a scratch wrapper outside the repo. `build/gen` is not committed.

## Open choices for the owner
1. **Control stays on core 0.** Core 1 only renders. Option: also move the control work of voices 4..7 to core 1 (needs a per-core copy of the matrix window, LFO values and `track_t` reads; saves 2 to 6 % of a voice on core 0, costs a lot of copying and risk). Left as is.
2. **Allocator order (`VSLOT`).** Needed so n notes load the cores evenly (and so the estimate holds). It changes which voice index a note gets (0,4,1,5 ...), not the sound (the one-core output is bit for bit the 4.3a output on the 7 scenes). Option: keep DUELL's 0,1,2,3 order; then up to 4 notes all land on core 0 and the two-core estimate does not hold.
3. **Fixed 4 / 4 split.** Core 1 has 30 to 45 points spare. Option: 3 voices on core 0 and 5 on core 1 (or a dynamic split by sounding voices). Not designed.
4. **Hand-over cost and the whole-copy hash.** 222 words hashed twice per block. Options: hash only part of it; or drop the copy hash (mutant NO_HASH shows the torn-copy case then reaches the output).
5. **Budget constants** (`BD_LR_CTL_PCT` 6, `BD_LR_HO0` 40, `BD_LR_HO1` 45) and whether the hand-over is charged at all (DUELL item 132 stopped charging it). Also the double 1.15 margin above: keep, or take `BD_LR_MARGIN` out of the voice row used by the deadline.
6. **Where the job is posted.** After the control work, inside the voice stage (the earliest point at which the data exists); DUELL posted at the block start. Core 1 starts about one block's control work later than DUELL's part 2 did.
7. **Voices 4..7 while a job is open.** Silent, as DUELL's part 2 was; new notes on them wait (`lrry_pend`) to clear their audio state. Option: refuse notes on those voices instead.
8. **Fade timing on a mid-block fault** (see above): fade the same block by silencing voices 4..7 for it, or as now.
9. **UI.** `ui_draw.c` reads `trk_nvoice` and the voice list by index; I did not check what it shows with the new order, and the UI was not touched.
