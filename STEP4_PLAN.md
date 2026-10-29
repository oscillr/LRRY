# L/RRY step 4: the shell on DUELL's base, plan (2026-10-24)

Plan only; nothing copied, changed or built. Facts below are counts from DUELL 1.0 (`release_1.0/v1r/felucca-base`).

## 1. What we are dealing with
- DUELL 1.0 firmware: **52,000 lines of C** in `firmware/src`, 190 test files, one parameter table (`core.h`, an int16 array `track_t.p[]` with ids `P_...`), and everything else (pages, MORPH, matrix, save, MIDI CCs, the editor) is organised around that table plus a per-engine set of 8 "E" knobs.
- Two-part footprint: `NLIVE` 104 references in 21 files, `NPART` 31 in 10, `trk[1]` 25 in 7, `PART_OF` 9 in 5. The mode words DUEL / STEREO / LAYER appear in 80+ files (the DUEL count includes the name DUELL itself, so it is overstated). Effects: the chorus, delay and reverb buses are `fx_bus.c` (4 files touch it, about 300 references).
- So the removal is wide but shallow: many small edits, few deep ones. The deep parts are the voice and the per-part mix (`voice.c`, `fx.c part_*`, `twocore*.c`), which we replace anyway.

## 2. The decision I need from you: how L/RRY's parameters plug in
**A (recommended): one built-in "L/RRY" sound source inside DUELL's existing parameter machinery.** The L/RRY voice becomes the only thing the part plays, its parameters get ids in the same `p[]` table (a new, larger set; patches are new from scratch, as you decided), its pages are DUELL-style pages, and MORPH, the 8-slot matrix, LFOs, ENVs, save, MIDI CCs, arp, sequencer and the editor protocol keep working because they only see parameter ids. To you nothing is an "engine": there is one instrument with an oscillator list. Inside the code it is one table entry that replaces the engine list. Cheapest, safest, keeps what you asked to keep.
**B: a new parameter system from scratch.** Cleaner in theory, but it means rewriting params, pages, morph, matrix, save, MIDI map and the editor protocol: months, and loses DUELL's tested behaviour. I do not recommend it.
(You said you did not want DUELL's engine architecture replicated. A keeps only its table mechanism, not its engines, filters, or per-part voice; if that still feels wrong, tell me.)

## 3. Phases of step 4 (each ends with host tests; no unit until you say)
| Phase | What | Output |
|---|---|---|
| 4.0 | Copy v1r into `LRRY/fw/` (the original stays untouched), run DUELL's host tests on the Mac for a baseline, and write the **removal list**: every file and function to delete or change, with reference counts | `LRRY/fw/` + `REMOVAL_LIST.md`, baseline pass/fail table |
| 4.1 | One part: remove part B, the DUEL / STEREO / LAYER modes, split buses, part colours, SPREAD; tests of removed features are deleted, the rest rewritten ("tests follow firmware") | one-part firmware that still builds and passes its tests on the host |
| 4.2 | Remove the effects (chorus, delay, reverb, shimmer, SENDS, PERFORM's effect stages); keep the master stage (width, bass mono, comp, limiter, soft clip) | firmware with no effects |
| 4.3 | Put the voice in: port the host voice (`lrry_*.h`) into firmware style (tables in RAM, a reciprocal instead of the edge divide, integer coefficient tables), the L/RRY parameter ids, the voice allocator and the two-core runner by voice replacing `twocore_run`'s part job | host test: identical to the Mac voice tests, one core = two cores |
| 4.4 | Re-attach: matrix destinations for the new units (pans, balances, drives), MORPH, MIDI CCs, arp, sequencer, velocity/aftertouch (velocity OFF), save | host tests for each |
| 4.5 | Pages and menus for the new parameters: **a layout proposal for you to approve first**, then the code | your OK, then pages |
| 4.6 | First device build (Docker + JieLi toolchain, DUELL's route), only on your explicit go, recovery path ready; you test sound and UI | a package for the unit |

## 4. What this costs and risks (honest)
- It is a large job in many sessions. 4.0 is small (copy, baseline, list). 4.1 and 4.2 are the long, boring part; 4.3 is where our work from steps 1 to 3 lands.
- Biggest risk: DUELL's tests and hot-RAM rules (the "hot set", pool-tail rule, hot RAM at least 228 bytes) were tuned for its voice and effects; removing a lot changes memory layout, and some rules and tests will need re-basing, not just deleting. I would re-base them as we go and say so.
- Second: DUELL's build runs only through Docker and the JieLi toolchain; on this Mac I have run only the host tests so far in this project. Whether the full device build works from the copy is checked in 4.0 (the build itself only if you say so).
- Usage: 4.1 and 4.2 are many small edits; I would do them in a few focused rounds, not one long run.

## 5. Yes / no for you
1. Option A (L/RRY as the one built-in source in DUELL's parameter table)? Yes / No
2. May I do phase 4.0 now: copy DUELL 1.0 into `LRRY/fw/`, run its host tests on the Mac for a baseline, and write the removal list (no device build)? Yes / No
