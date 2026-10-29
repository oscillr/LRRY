# L/RRY step 3 results (2026-10-24): the split by voice on the host

Files: `LRRY/host/lrry_split.h` (8 voices, allocator, one-core and two-core runner), `lrry_split_test.c` (run: `cc -O2 -pthread lrry_split_test.c -o t -lm && ./t`). Mac only, no unit. Steps 1 and 2 are unchanged. Result: **29 checks, 0 failed**, five runs in a row identical; **all four mutants are caught.**

## What was built
- 8 voices: 0 to 3 belong to core 0, 4 to 7 to core 1. Core 1 is a thread here (the second core on the unit). Each core has its own stereo accumulator; core 0 adds them. A voice is one job (own state, no globals): the same `voice_render` as step 2.
- Hand-over (as DUELL's two-core design): core 0 copies the block's control data (the patch) into a block copy, posts a job number, renders its own voices, waits until core 1 reports done, adds the two accumulators. Core 1 reads only the copy.
- Allocator: a new note goes to the core with fewer sounding voices; with 8 notes the cores carry 4 and 4; when all 8 sound, it steals a released voice first, otherwise the oldest.
- A fallback switch: core 0 can render all 8 voices itself (the one-core path), also in the middle of a run.

## Measured
- One core and two cores give **identical output, bit for bit**, over 1,500 blocks of random notes (note on and off, steals, all-off), for a plain patch and for the full stereo voice with the filter cutoff changing every block, and also when the engine switches between one and two cores every 37 blocks. Two runs of the two-core engine are identical too (no timing dependence).
- Voices per core: never more than 1 apart (8 notes: 4 and 4). After 40 extra notes (steals) still 8 voices, none doubled. After all notes off every voice ends (36 blocks, about 105 ms with the 50 ms release plus the envelope's tail).

## Mutants (deliberately broken versions, the test must fail)
| Mutant | What is broken | Result |
|---|---|---|
| NOWAIT | core 0 does not wait for core 1 | caught (5 checks fail) |
| DOUBLE | voice 3 is rendered by both cores | caught |
| STALE | core 1 uses the previous block's patch | caught |
| SHARED | both cores add into one accumulator | caught |

## What this does not prove
- The device: core 1 on the unit runs from RAM, cannot read flash, has a 2,816 B stack, and needs the supply raise and heartbeat watch that DUELL already has (SPEC 5d). A thread on the Mac proves the logic of the split, not those. Every table a voice reads (sine, tanh, later WAVE tables) must be reachable without flash on core 1: not checked yet (the voice's sine and tanh tables are plain arrays and could be placed in RAM).
- Speed: nothing says core 1 finishes inside the block; DUELL's deadline and fallback logic (strike counter) is not ported yet, only the fallback switch exists.
- Voices share one patch here (per-voice modulation will differ per voice but is owned by the voice's core).
- The mix after the cores (master width, bass mono, comp, limiter, soft clip) is not in this step.
