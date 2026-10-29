# Dual core and a "never crackle" budget: study (2026-10-17)

Research only. No DUELL code was changed, nothing was built, pushed or posted. External repositories were cloned into a scratch folder and read; no code of theirs is copied here (only short identifiers). Figures marked **model** come from DUELL's host bench (`felucca-base/tests/bench_baseline.txt`, 33 host instructions a sample = 1 %). Figures marked **unit est.** are my estimate of the unit's cost from the rules in `docs/TESTING.md` "CPU on the unit" (code running from RAM x0.85, from the flash x1.1, EQ about x3, SHIM pitch and RANDOM delay x1.5, PHYS x1.3). They are not measurements; the first step below replaces them with unit readings.

## In plain words

- **Dual core works on most FM-1s, but not on all of them.** On some chips the second core (and then the first one too) reads memory wrong when both run. The other firmwares now handle that by raising the chip's supply to the levels M-VAVE's own firmware uses, by checking every job, and by falling back to one core for good after the first sign of trouble. That fix is two days old and nobody has yet posted a long clean run from one of the bad units.
- **Because the fallback is always one core, dual core cannot be the guarantee.** If DUELL must never crackle, the one-core budget has to be safe by itself. Dual core can then add headroom (more voices, heavier effects) on units where it works.
- **What takes the power today (unit est., per use):** a PHYS voice about 20 %; doing nothing at all about 15 % (the mixer plus the delay and reverb, which run even when nothing is sent); DUEL mode adds about 9 % more because every effect runs twice; the part EQ about 7 % a part (its 64-bit math is slow on this chip); SHIM PITCH BOTH and the RANDOM / OCT UP delay about 5 to 6 % each (about 12 % for SHIM BOTH in DUEL); the master SATURATOR about 5.6 %; any other voice 6 to 9 %; an INSERT 1 to 3 %; SAT, DUCK, COMP, WIDTH, BASS MONO about 1 to 2 % each.
- **With everything on (M16 style), there is room for about one note.** With a plain delay and reverb and nothing else, about 5 to 8 voices fit under 70 %. The table in section 5 gives the numbers per engine and mode.
- **Why WAVE crackles with the bar at half:** the bar is an average over one second. A fast run fills all 8 voices with release tails (and LAYER doubles every note), so single blocks go over 100 % while the average stays low. The TEST screen's PEAK HOLD shows it; the bar cannot.
- **Recommendation:** first make one core safe (measure every stage on the unit, then a budget that counts effects too and refuses or fades voices before an overload, with a warning in the editor). Then move the whole audio path into on-chip RAM (as M-VAVE's own firmware does). Only then add dual core, off by default, with the effects and master on the second core.

## 1. Findings per firmware

All five firmwares below use the same start sequence for core 1 (from JieLi's SDK `EnableOtherCpu`, found for the FM-1 by Melodee and X0X): the entry address in the word at 0x01C7FFF8, core 1's interrupt bank (0x1EEF300..) all off, C1_CON (0x1EEE004) bit 3 set and bit 1 cleared, a 20 ms wait for an "alive" flag. It must start at power-on before `audio_init` and the timers, with the PC limits opened while it boots through the ROM. Core 1 runs with interrupts off; its waiting loop is in RAM; it only works inside core 0's audio interrupt (fork and join per block), so the flash is never switched off under it. Hand-over is one job at a time: core 0 writes the function, the argument, a check word and then the job number; core 1 writes "done". `csync` around each flag; no locks (the two cores share the caches).

### X0X (charlesvestal/fm1-x0x, GPL-3.0-only)
- **Split:** by instrument. Core 1 renders the 808 and the break, core 0 the 909 and both 303s, then core 0 mixes, runs the sends and master. Output bit for bit as one core. First split (909 + 808 on core 1) left core 1 as the critical path; rebalanced after measuring the worst block.
- **Gain (one unit, its PERF TEST):** worst pattern 115 % -> 68 %, peak 84 %, no dropouts; all five instruments 83 % -> 54 %.
- **Reliability:** issue #10 (zednaked's unit: core 1 reads RAM wrong, 1572 bad reads in 68 s, crashes, USB update stalls; with core 1 held: stable) and #21 (fc79: core 0 crashed every few minutes with both cores, a single wrong address bit; 30 min clean with core 1 held). Fixes: check word on each job (1.0.5), one core after a bad read or a crash beside it, GLO > 2ND CORE (ON by default, saved Off after a two-core crash, 1.0.7), and since 1.0.9 (2026-10-15) the supplies at M-VAVE's own levels from the start: VDC14 7 (1.60 V), SYSVDD 15 (1.38 V). Both issues are closed; neither has a posted result from the affected unit on 1.0.9.
- **Files:** `firmware/hal/fm1_cpu1.{h,S}`, `firmware/hal/fm1_sys.h` (`fm1_vdc14_raise`, `fm1_sysvdd_raise`), `firmware/src/app/engine.c` (`render_sub`), `firmware/src/app/main_fm1.c` (start order, `one_core`), `docs/plans/2026-10-04-dual-core-and-clock.md` (the clock: measured 360 MHz, above JieLi's 320 MHz table; "raising the clock is not an option").

### Melodee (keremimo/melodee, GPL-3.0-only, Copyright Kerem Kilic)
- **Split:** the finest of all. Core 0 does events, envelopes, modulation and control preparation; voice sample work goes into a pipeline of 8 bounded job descriptors (`audio_batch.h`, 256 B of data and a PCM buffer each), split across tracks and within a track by measured per-engine costs and an allowance for core 0's interrupt work. Core 0 merges in the original voice order before the effects, so the output is the same as serial (host tests compare serial and truly concurrent rendering for all 1296 engine assignments).
- **RAM:** with `MELODEE_RAM_AUDIO` every synthesis callback, worker, the audio interrupt and the nested timer / input / UART paths are resident in SRAM (323 functions, `.audio_text` in the pool, `.dsp_text` in the low RAMTEXT); the build checks addresses and far calls. RAM was reclaimed for it.
- **Budget:** an admission table (reserved voice slots per engine combination) and later a shared weighted BUDGET (GLO, default 16 points; Prophet 3 or 4 points a voice, FM6 / SID / DRUM 2, CZ 3). Measured on the author's unit: all 1296 assignments with zero late blocks and zero emergency cuts, worst block 2581 us of 2901 us (89 %); the 80 % target is not met everywhere and they say so. A settings flash write during play caused 7 late blocks once (kept as failed evidence).
- **Reliability:** 1.0.0 crashed on some units (core 1 read the idle mailbox wrong and ran a null job); 1.0.1 added the check word and one core after a misread, fault or a 10 ms stall; a 320 MHz clock retune failed to boot on some units and was removed (1.3.2 .. 1.3.4); 1.3.6 applies M-VAVE's whole stock power-up sequence (SYSVDD 15, VDC14 7) to both packages; 1.4.0 (2026-10-15) ships dual core only ("single-core packages are retired"). Their own note: "matching stock's supply setup does not yet establish that all instability is resolved".
- **Files:** `firmware/hal/fm1_multicore.h`, `firmware/src/audio_worker.h`, `firmware/src/audio_batch.h`, `firmware/hal/fm1_power.h` (`fm1_power_init`), `firmware/app.ld` (`.audio_text`, `.dsp_text`, CACHE region), `docs/AUDIO_PERFORMANCE.md`, `docs/RELEASE_1.3.*.md`, `tests/dual_core_test.c`, `tests/audio_worker_test.c`.

### ChoralRoot (Quixotic7/ChoralRootFM1, GPL-3.0-only)
- **Split:** job 1, the bass part's render (and with the P5 engine a share of the chord's voices, at most 4 a core); job 2 (since 0.22), **the reverb on core 1** while core 0 runs the chorus and delay, then core 0 adds core 1's wet and runs the master. Every effect adds into the mix with integers, so the order does not change the result (bit for bit, checked under ThreadSanitizer). Their reasoning: the effects need both parts' sends, so they cannot run under the voices without one block of latency, which they did not take.
- **Gain:** small and stated honestly: the bass job is 50 to 70 us of a 2902 us half on average (5 to 8 %); the win is the voice budget (8 + 8 instead of a shared 8).
- **Reliability:** the owner's unit runs Dual Core On since 0.16. Rails 14 / 5 (1.35 V / 1.50 V) when On, as boot loader levels when Off; capped, never raised further. Fail-safes: the 20 ms start check, an 8 ms job timeout (core 0 then renders the job itself), core 1 faults handled without a reset, a second misread holds core 1 for good, and since 0.22 a persistent fallback (a crash or hang beside core 1 saves Dual Core Off for the next power-on), plus a single-core package in the installer. Issue #9 ("audio pops/crackles with dual core enabled ... crash/reboot loop", 2026-10-13) is still open; the reporter has not answered whether 0.22+ fixes it.
- **Files:** `docs/DUALCORE.md` (the best written account of all), `firmware/hal/fm1_cpu1.{h,S}`, `firmware/src/cr_cpu1.h` (`cr_c1_power_on`, `cr_c1_assess`, `cr_c1_bar_decide`), `firmware/src/fx.c` (`mix_parts_split`, `xf_reverb`), `firmware/hal/fm1_power.h`, `tests/cr_cpu1_test.c`, `tests/cr_c1_thread_test.c`.

### Felucca 1.6.1 (hugelton/Felucca, GPL-3.0-only): DUELL's own upstream
- **Split:** MENU > SYSTEM > 2ND CORE, OFF by default (beta), effective from the next power-on. `fx.c` `mix_split` picks, from each part's cost in the last block, the set of parts that leaves the busier core the least (none when the gain is under the hand-over cost `C1_HAND`); core 1 renders those parts into its own sums (`mx[1]`), core 0 the rest, then core 0 adds and runs the LFO ends, buses and master. Bit for bit one core (`tests/dualcore_test.c`, also under ThreadSanitizer).
- **Safety:** no supply raise ("a unit that misreads runs on one core"); check word; 4 ms timeout then one core; a misread clears 2ND CORE ON; every reset path holds core 1 first. Core 1's job code runs from the flash ("the work itself runs from the flash"); only its waiting loop is in RAM.
- **Status:** released 2026-10-15; on 2026-10-16 the author wrote that he is stepping back from active development (occasional maintenance only).
- **Files:** `firmware/hal/fm1_cpu1.{h,S}`, `firmware/src/fx.c` (lines about 975..1060: `c1_parts`, `mix_split`, `mix_block`), `firmware/src/main.c` (start before `audio_init`), `firmware/app.ld` (`.c1_text`), `tests/dualcore_test.c`, `tests/c1_host.h`, `CREDITS.md`.

### Others
- **Jangada** (zednaked/jangada, GPL-3.0-only): the per-part split Felucca 1.6.1 took, and the measurements on the misreading unit (supply table in X0X #10: 11 / 3 faults in 6 s, 14 / 4 fails sometimes, 15 / 5 clean for 15 min and 1.23 M blocks). Runs dual core by default at 15 / 5.
- **GammaWave-FM1** (fvalente) and **NESynth-FM1-SMK** (ChanceTheMaker) say "dual-core" in their descriptions; not read.
- Charles Vestal's newer repos (fm1-base, fm1-amb, fm1-ctl, fm1-radio) are MPL-2.0 on "felucca-bsp"; fm1-base has no second-core code.

### Reliability verdict
Dual core is real and gives real headroom (X0X: 115 % -> 68 %). On a minority of chips it fails at the boot loader's supply (1.26 V / 1.40 V), which is lower than what M-VAVE's own firmware sets (1.38 V / 1.60 V, read from stock V15 by fc79). The current consensus is to set the stock levels before starting core 1; that is two days old and not yet confirmed on an affected unit. Failures seen: core 1 misreads (caught by the check word), core 0 crashes beside it (not catchable, only the persistent fallback helps), USB update stalls on a misreading unit, a crash loop without the fallback (ChoralRoot #9). No report found of flash-write conflicts once the waiting loop is in RAM. Every author tests on one or two units; nobody has numbers on how many units are affected.

### Licence verdict
All five (X0X, Melodee, ChoralRoot, Jangada, Felucca) are **GPL-3.0-only**, the same as DUELL: their code may be used in DUELL. Conditions: keep their copyright and SPDX headers, mark the files as changed, credit them in NOTICE.md and ABOUT > CREDITS, and give the full source (DUELL does already). The cleanest route is Felucca 1.6.1's `fm1_cpu1.{h,S}`: DUELL is already a Felucca fork, and that file already carries the credit chain (X0X, Melodee, Jangada). Melodee's power-up sequence is Copyright Kerem Kilic. Register addresses and the start sequence are hardware facts; DUELL's own probe (below) was written from M-VAVE's runtime library, not from their code.

## 2. DUELL today

- **No release has any second-core code.** "Dual core off" means core 1 is never started: it stays held as the boot loader left it. There is no option in MENU. README line 87, RELEASE_NOTES line 80, docs/KNOWN_ISSUES.md line 11 and docs/TESTING.md lines 65..67 talk about a planned option that is off; TESTING's "do not turn on dual core if you see the option" refers to an option no build has.
- **A probe exists on branches only** (`wip/core1probe` 662191e, `wip/bench2` a9eb62b, also in `wip/hotram*`): `DUELL_CORE1_PROBE=1` bench build D, `hal/fm1_core1.h` (start read from M-VAVE's `fm1_open` runtime with objdump), a heartbeat, 1024 timed adds and a 4 KB pattern re-read on core 1 during a bench run, parked around the bench's flash save, a `.noinit` crash guard, results on the RESULT screen and the console. `main` does not have it (`release_test` checks no probe is in a release). I found no record that it was run on the unit.
- **Felucca base:** `UPSTREAM_COMMIT.txt` is 129a4cf (Felucca 1.5). Felucca's own dual core (1.6.1) came later and is not in DUELL.
- **Supply rails:** DUELL never touches them (no VDC14 / SYSVDD code in `felucca-base/firmware`), so it runs at the boot loader's 1.26 V / 1.40 V, below the stock firmware's levels. One core at these levels was stable in fc79's 30-minute control run.
- **RAM for code:** the hot block (`app_hot.ld` RAMCODE 0x01C7C100..0x01C7FD4C, 15,436 B incl. the flash driver) has 12 to 20 B left. The rest of the audio code (mix_block itself, PHYS, ANALOG, TRIO, HALL, PLATE, RANDOM / OCT UP delay, SHIM pitch, the master stages, the DUEL bus copies) runs from the SPI flash through the 32 KB instruction cache. The unit bench of 2026-10-15 showed layout alone moving the load by up to 15 points.
- **Unused SRAM:** in the hot build, app.ld's old RAMTEXT 0x01C00000..0x01C08000 (32 KB) is unused (`app_hot.ld` comment). Code there cannot call the flash directly (out of the 4 MiB call reach) but can call any RAM code, and Melodee runs its `.dsp_text` from the same place.
- **Overload handling today (`audio.c`):** a reactive shed (two halves over 85 %: one voice fades; a half over 100 %: two release tails fade at once) and a predictive VOICE CAP (`voice.c`, per-engine costs in model % plus filter and fade, without the effects). Neither counts the effects, and neither knows the unit's real cost.
- **Coupling between parts in `mix_part` (matters for a per-part split):** DUCK SRC PART reads the other part's block; `split_stash` takes part 1's sends in DUEL; shared scratch (`part_buf`, `pfx_dry`, `spread_on`, `mxf` of the morph, the matrix's `mod`, `eq_live`); the shared random generator; the 8-voice pool. Felucca 1.6.1 solved this for Felucca's code; DUELL's morph, matrix, DUCK and MODULES are its own and would need the same audit.

## 3. What takes the power (ranked, one use each)

| Item | model % | unit est. % |
|---|---|---|
| PHYS voice (1 note) | 15.8 | 20.5 |
| buses in DUEL, nothing sent (DIGITAL + HALL, twice) | 15.1 | 16.6 |
| idle mix, STEREO, nothing playing (includes the buses below) | 14.0 | 15.4 |
| SHIM PITCH BOTH over HALL, DUEL | 7.9 | 11.8 |
| VOICE / WHEEL / TRIO / ANALOG / NOISE voice | 7.3..8.5 | 8.0..9.4 |
| buses in STEREO / LAYER, nothing sent | 6.7 | 7.4 |
| PHASE voice | 6.6 | 7.3 |
| part EQ (64-bit math) | 2.5 | about 7 |
| FM6 voice / WAVE voice (hot code) | 8.2 / 7.3 | 7.0 / 6.2 |
| RANDOM / OCT UP delay over DIGITAL (STEREO; DUEL alike) | 3.8 | 5.7 |
| master SATURATOR | 5.1 | 5.6 |
| SHIM PITCH BOTH over HALL, STEREO | 3.7 | 5.5 |
| LOFI voice | 5.0 | 5.5 |
| INSERT (TAPE / VINYL / PHASR the dearest, CRUSH the cheapest) | 1.5..2.9 | 1.2..3.2 |
| chorus fed (STEREO) | 2.5 | 2.1 |
| master COMP, WIDTH, BASS MONO (not in DUEL) | 1.2..1.6 | 1.3..1.8 |
| part SAT, part DUCK | 1.5 / 1.0 | 1.2 / 0.8 |
| PING-PONG over DIGITAL; PLATE, ROOM, RESO over HALL | 0.9..-1.4 | about 0 or less |
| the 24 dB slope (VOICE CAP's figure, per voice) | +2 over 12 dB | +2.6 |

Note: in LAYER one key is two voices (one per part).

## 4. Options

| | What | Gain (% of one core) | Main risks | Proven where |
|---|---|---|---|---|
| **A** one core, strict budget | measure every stage on the unit; admission that counts effects and mode; cheap rewrites (EQ in 32-bit, buses skipped when unfed and silent, SAT cheaper); cut what costs too much | no extra core; EQ rewrite about 4 to 5 a part, other rewrites unknown until measured | sound changes where stages are cut; needs unit readings per build (layout) | Melodee's admission table and BUDGET; DUELL's VOICE CAP |
| **B** effects and master on core 1 | the whole chain after the part sums (chorus, delay, reverb incl. SHIM, PERFORM SPACE, COMP, WIDTH, BASS MONO, SATURATOR, master out) of block k-1 on core 1 while core 0 renders block k; the one-core fallback runs the same schedule | STEREO / LAYER full set about 30, DUEL about 35, plain set 7 (STEREO) / 17 (DUEL); less 1 to 2 for the hand-over | +32 samples (0.73 ms) on the whole output (dry and wet alike, so no smear); every global effect parameter must be copied into the job each block (matrix bus offsets, SPACE, MODULES fades, the DUEL dip); core 1 runs flash code (RANDOM, SHIM pitch, master) against core 0's cache | ChoralRoot runs its reverb there (no latency, overlap only); the one-block pipeline is my proposal, not done by anyone |
| **C** parts split across cores | core 1 renders one part (render, DIST, INSERT, EQ), core 0 the other, core 0 then does DUCK, level, pan, sends and everything after | up to half of the voices and part stages when both parts sound (4 + 4 ANALOG with INSERT + SAT: about 35); 1 key of M16: about 14 (parts unequal); nothing with one part playing | the coupling list in section 2 (DUCK SRC PART, morph, matrix, scratch, random generator); engine code in flash on both cores | Felucca 1.6.1, Jangada, X0X, ChoralRoot |
| **C2** voices split (Melodee) | a job pipeline of single voices, balanced by cost | best balance in every mode | the largest rework of the engines' voice code | Melodee |
| **D** whole audio path in RAM | the closed graph of the audio interrupt (mix_block, every engine, every effect, the helpers it calls) in on-chip SRAM: the unused 32 KB at 0x01C00000, the top block, room taken from the pool and the stacks | unknown: removes the cache-layout swing (up to 15 points) and the flash misses; M-VAVE's own firmware and Melodee do it | RAM: the pool has about 29 KB spare; no call from RAM code into the flash (the linker reports it); the PC limit's RAM window must cover the new block | Melodee (RAM audio), stock V15 |
| E clock | 320 MHz or higher | n/a | the chip already runs at 360 MHz, above JieLi's table; Melodee's retune failed to boot some units | rejected by all |

**The close call is B against C.** C is what everybody ships and needs no latency. B is the better fit for DUELL: most of DUELL's worst case is the fixed part (about 39 % unit est. with the full effect set before any voice), B helps whether one part or both play and in all three modes (DUEL most, where the buses run twice), and it does not touch the engines, the 8-voice pool, the morph or the DUCK coupling. Its price is 0.73 ms of extra latency (today a key waits 0 to 2.9 ms and then plays 2.9 ms later) and a parameter snapshot per block. If the owner does not accept any added latency, C is the choice, after the coupling audit.

## 5. What fits under 70 % on one core (unit est.)

Both parts sounding, the same engine on both, voices in total (both parts), 12 dB filters:

| Effect set | Mode | Fixed + part stages | LOFI | FM6 | WAVE | ANALOG | PHASE | VOICE | PHYS |
|---|---|---|---|---|---|---|---|---|---|
| plain delay + reverb, nothing else | STEREO / LAYER | 15.4 | 9 | 7 | 8 | 6 | 7 | 5 | 2 |
| | DUEL | 24.7 | 8 | 6 | 7 | 5 | 6 | 4 | 2 |
| + chorus, COMP, WIDTH, BASS MONO, INSERT + SAT a part | STEREO / LAYER | 29.3 | 7 | 5 | 6 | 5 | 5 | 4 | 1 |
| | DUEL | 33.8 | 6 | 5 | 5 | 4 | 4 | 3 | 1 |
| + RANDOM delay, SHIM BOTH, SATURATOR, EQ + DUCK a part | STEREO / LAYER | 61.9 | 1 | 1 | 1 | 1 | 1 | 0 | 0 |
| | DUEL | 66.7 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

So the cheapest set that keeps 8 voices of any normal engine under 70 % is "plain delay and reverb" with LOFI, WAVE or FM6; ANALOG-class engines fit 5 to 6; PHYS fits 2. Every stage of the third row costs about a voice. These are estimates; section 7 step 1 measures them.

## 6. A "never crackle" design for one core

**What is realistic:** no late block in steady play at any setting the unit accepts, because the unit refuses or fades load before it is spent. Not covered by such a promise: a build whose layout differs from the measured one (re-measure every release), flash writes (already ducked), USB audio streaming on top (to be measured), and a second core that fails mid-play (one glitch, then one core and a smaller budget).

1. **One cost table, measured on the unit.** A bench build times each stage of `mix_block` with the 24 MHz timer (average and worst, per half): each engine per voice, filter slopes, each INSERT, SAT, EQ, DUCK, each bus mode, each master stage, the mixer. The TEST screen and the console print it; it is written into the firmware as microseconds a half. ChoralRoot's GEEK OUT stage line and Melodee's editor scopes are the model.
2. **Budget, not average.** Target: the predicted worst block at most 80 % of a half (2320 us), leaving room for the nested timer, USB and layout drift. Predicted = fixed (mode, the effects that are on) + each sounding part's stages + every sounding voice incl. release tails + one voice-steal fade and one engine cross-fade per part.
3. **Admission (extends VOICE CAP).** Each block, each part's voice cap is recomputed from the table with the effects that are on now. A new note above the cap takes the oldest releasing voice of its part, else the oldest held voice that is not the part's lowest note, with the existing short fade; turning on a heavy effect lowers the caps at once and fades the extra voices over about 20 ms. The header says it (`CPU: 3 VOICES A PART`): UI honesty rule, nothing silent.
4. **Guard ladder when the measured time is over the prediction** (each step with a fade of one block and the header message; restored after about 2 s below 70 %, with a fade-in):
   1. release tails fade (exists today at 85 %);
   2. the voice caps drop by one per part;
   3. SHIM's pitch layer fades out (plain SHIM stays);
   4. RANDOM / OCT UP events stop (the plain repeats stay);
   5. chorus fades to dry; SATURATOR and WIDTH to bypass;
   6. EQ and INSERT cross-fade to dry;
   7. held voices down to each part's lowest note (exists today).
5. **A deadline check inside the half.** After each of the four 32-sample blocks, the elapsed time is compared with that block's share of the budget; if it is behind, the remaining blocks of the half render with the ladder's step 3 to 6 already applied. The half is always handed to the DMA in time: a would-be click becomes a short change in the effects.
6. **The editor warns before.** The same table and formula in the web editor (`layout.js`): the multi page shows "about N keys before the limit" per mode and a red mark on the stage that costs most; a patch shows its cost a voice. The unit's TEST screen shows the same estimate next to the measurement, which also checks the table.
7. **Limits per heavy engine.** From section 5: PHYS at most 2 voices with the plain set and 1 with the medium one, unless the other part is silent. The admission does this by itself; a fixed limit is only needed if the owner prefers a predictable number over a moving cap.

**Cheap wins on one core, by size (estimates):** EQ in 32-bit arithmetic (about 4 to 5 a part on the unit), skipping the delay and reverb while nothing is sent and their tails are silent (7 STEREO, 17 DUEL, only when idle), a cheaper SATURATOR (5.6), finding out what the 15 % idle mix is (mix_block is 8 KB in the flash), the whole audio path in RAM (option D, unknown, possibly the largest).

## 7. Recommendation and staged plan

**Recommendation:** A, then D, then B (off by default). C only if B is not enough and the owner wants more voices with both parts heavy.

| Stage | What | Proven on the host | Needs the unit |
|---|---|---|---|
| 0 | Owner decisions (open questions below) | | |
| **1 (first safe step)** | Bench build with per-stage timers (no change to the sound, no change to a release) and, in the same session, the existing core 1 probe build D | the timers do not change a render (goldens); the probe's host test exists (`host_tests.sh core1`) | the stage table for M16 and the bench scenes; whether the owner's core 1 runs clean at the boot loader's rails |
| 2 | One-core budget: measured table in the firmware, admission with effects, guard ladder, deadline check, editor estimate; EQ in 32-bit | deterministic model: for every factory multi and the bench scenes, predicted worst block under budget; the ladder's fades without clicks (render tests); EQ new against old within a tolerance | M16 with 1..8 keys, fast WAVE runs: MISSED stays 0, PEAK HOLD under 85 % |
| 3 | Option D, the whole audio path in RAM | build check: no call from RAM code into the flash, PC window, elfcheck; bit-identical renders | BASE and PEAK before and after |
| 4 | Dual core, option B, MENU > SYSTEM > 2ND CORE OFF by default: Felucca 1.6.1's HAL (credits), ChoralRoot 0.22's persistent fallback and fault handling, the rails decision | the job as an inline call and as a thread (ThreadSanitizer), bit-identical to the one-core schedule for all goldens; injected stall, misread, fault, null job | 30-minute soak with misread counter 0; power cycles; the fallback paths with a test build; then other owners' units |
| 5 | Option C, if needed | coupling audit, same thread tests | as stage 4 |

## 8. Open questions

1. Supply rails: may DUELL set M-VAVE's stock levels (SYSVDD 1.38 V, VDC14 1.60 V) at start, for dual core only or always? The earlier study said "not without the owner's decision". It is what the stock firmware does, and above the datasheet's table maxima.
2. Is 0.73 ms of added latency acceptable for option B? If not, option C.
3. Which effects may go, or be limited, if the table confirms the estimates (SHIM PITCH BOTH, RANDOM / OCT UP, SATURATOR, the DUEL bus doubling)?
4. Was bench build D (core 1 probe) ever run on the owner's unit? I found no record.
5. Does the boot loader or the update loader use 0x01C00000..0x01C08000 at run time (the 32 KB DUELL's hot build leaves unused)? Melodee puts code there, which suggests not.
6. Core 1's own PC limit registers: X0X says the PC limits apply to core 1; whether core 1 has its own pair or shares core 0's window was not established in the sources.
7. How large is the closed audio graph on the device (bytes of every function the audio interrupt reaches)? Needed to size option D.
8. Is the WAVE crackle the voice count (tails, LAYER) or a cost spike of its own? The PEAK HOLD with a fast run in STEREO against LAYER, with a short and a long release, separates them.

## Sources

- X0X: https://github.com/charlesvestal/fm1-x0x , issues https://github.com/charlesvestal/fm1-x0x/issues/10 and https://github.com/charlesvestal/fm1-x0x/issues/21
- Melodee: https://github.com/keremimo/melodee (docs/AUDIO_PERFORMANCE.md, docs/RELEASE_1.3.6.md, docs/RELEASE_1.4.0.md)
- ChoralRoot: https://github.com/Quixotic7/ChoralRootFM1 (docs/DUALCORE.md), issue https://github.com/Quixotic7/ChoralRootFM1/issues/9
- Felucca: https://github.com/hugelton/Felucca (commit a53ecf4 "Felucca 1.6.1", 3a91058 README)
- Jangada: https://github.com/zednaked/jangada
- DUELL: docs/SECOND_CORE_RESEARCH.md (2026-10-14), docs/TESTING.md, felucca-base/tests/bench_baseline.txt, felucca-base/tools/hot_fns.py, felucca-base/firmware/app_hot.ld, felucca-base/firmware/src/audio.c, fx.c, branches wip/core1probe and wip/bench2
