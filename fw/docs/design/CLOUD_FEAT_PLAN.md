# CLOUD_FEAT_PLAN: the alpha feature set through cloud jobs (2026-10-20)

Private. Research and writing only: no code, no branch, nothing pushed, INDEX.md and GLOSSARY.md not touched. Marks: **CONFIRMED** (read today in the joined line `/Users/balinth/wt/join0234`, branch wip/join0234, commit 4da80f9, or in the named document), **ESTIMATE** (worked out, not measured), **HYPOTHESIS** (my reading). The ten pasteable briefs are `CLOUD_FEAT_A_BRIEF.md` to `CLOUD_FEAT_J_BRIEF.md` in this folder.

## 0. One-screen summary

**Scope (your words, 2026-10-20):** TWIN FILTER, the MIDI CC setup (the Virus-based map: another helper fills the CC list; this plan leaves a slot), and the FEATURE_LIST "for the next version" items: velocity/aftertouch from the keys, editable B, CYCLE, SUB layer, WAVE STEPS, MIDI MAP, plus the patch-format work they need. Nothing else. No new test tooling: the existing host tests are the gate.

**Ten cloud jobs in three waves, plus one small local step before and merges between.**

| Wave | Jobs (branch cloud/feat-...) | Runs |
|---|---|---|
| 0, local | P: push `cloud/base3`, add the design documents and a small "interface commit" (section 3) | before anything |
| 1 | **A** store (patch record, extension block, chunks, multi/project, backup); **B** values (value list, morph, matrix plumbing, id cleanups, the voice-chain stub); **C** MIDI MAP engine (+ the slot for the Virus CC list); **D** velocity logic | all four at once |
| 2 (after A and B are merged; base branch `cloud/base3-wave2` = base3 + A + B, pushed by the local helper; C and D may be in it too) | **E** TWIN DSP; **F** SUB + CYCLE + WAVE STEPS; **G** editable B; **H** unit pages; **I** editor | all five at once |
| 3 (after E and F are merged; base `cloud/base3-wave3` = the wave-2 base + E, F, G, H, I) | **J** engine filters out + the conversion tool + proof renders | alone |

Critical path: P, then A and B, then E and F, then J, then the device build. C, D, G, H, I are off the critical path.

**Local work that never goes to the cloud:** device builds, RAM placement of everything core 1 reaches, filling the NOTES cost rows from the Mac bench, regenerating test fingerprints (only after J), listening, the unit test, the key recording for velocity.

**What you must answer before launch (section 8):** O1 copy the design documents into the private repo; O2 approve the UI points not yet confirmed (SUB OSC page, CYCLE page and where SYNC goes, STEPS card, MIDI MAP page, editable-B details, velocity menu); O3 the RAM rule (below); O4 use the last free flash pair for the velocity calibration store.

**Cost and time (ESTIMATE):** about $70 to $110 for ten jobs, $30 reserve for repairs, so about $140 of roughly $200. About 12 local rounds if each wave is merged in one go, 2 listening/unit sessions of yours, plus 1 to 2 short sessions for the key recording (velocity only).

**Honest verdict on the three hard parts:**
1. **RAM is the thing most likely to break the plan.** The joined line has 676 B free in range X and only 48 B (bench rule) or about 368 B (normal build, HYPOTHESIS) of room in range Z / pool tail. TWIN + SUB + CYCLE + the matrix/morph growth need about 2.5 to 3.6 KB of core-1 code (ESTIMATE); removing the engines' own filters gives back only 1.0 to 1.4 KB. It does not fit as it stands (section 5). A decision is needed, not a coding trick.
2. **Velocity/aftertouch is the only feature that depends on your hands.** Cloud can do the maths and the test harness; the scan code and constants need a raw key recording from your unit. Run job D in wave 1 off the critical path; if the recording and tuning are not done when the rest is ready, the alpha ships with velocity OFF (fixed 100, as today) and loses nothing (section 7).
3. **J changes sounds on purpose.** "Bit-exact when a feature is off" holds for A to I. J (engine filters out, chain before the amp on every engine, filter 1 RES floor) changes 43 factory sounds, 10 multis and, a little, the other 85 sounds; test fingerprints change there and only there, and only the local helper regenerates them, after classifying every change.

## 1. Words

Already in GLOSSARY.md: test fingerprint (golden), host tests, block, voice, matrix, multimode filter (= filter 1), NOTES counter, core 0 / core 1, hot RAM, cloud session. New here:
- **Wave**: a set of cloud jobs started together, each on its own branch, merged by the local helper in one go.
- **Interface commit**: a small local commit on `cloud/base3` that adds names only (no behaviour), so parallel jobs agree on them. **Stub**: a function that exists and does nothing yet, so a later job fills it without touching its neighbours.
- **Extension slot / wide id / extension block**: as in EXTENSION_BLOCK_PLAN.md. Wide id 0 to 126 are today's ids, 128 to 255 the extension slots X0 to X127; the extension block is the new part of a patch record that stores them.
- **Chain**: the per-note path "engine at full level, SUB, filter 1, DRIVE, filter 2, then the amp".
- **Range X / range Z / pool tail / reach**: range X is fast RAM holding the engines' code for core 1 (closed: it calls only RAM code); range Z holds the rest of part 2's job; the pool tail is the unused RAM after the pool and range Z up to the stack guard (range Z's free room and the pool tail are **the same bytes**: every byte added to Z comes out of the tail). Reach is every function core 1 can call. Core 1 may run only code in RAM.
- **Hot block**: the audio code kept in RAM for speed (`tools/hot_fns.py`), 232 B free in the bench build, 228 B in the normal build, rule: at least 228 B. So it may grow by 4 B (bench) or 0 B (normal): treat it as closed.

## 2. What the joined line allows (CONFIRMED unless marked)

| Fact | Number | Source |
|---|---|---|
| Hot block free | 232 B bench, 228 B normal; rule at least 228 B | MERGE_SHRINK1 s7 |
| Hot functions (31) | uf_run, uf_voice, uf_coef, mod_voice, mod_voice_add, wave_render, wv_line, wv_lvl, wv_warp, fm6_*, eq_run, ins_swept, track_insert, track_dist, duck_run, mix_spread, rev_room, rev_shim, dly_mod, fx_buses_mod, morph_end, sine_i, osc_sine, spread_clear, perf_flanger, perf_drive | `tools/hot_fns.py` |
| `track_render` | NOT hot (flash; in the two-core build it is inlined into `part_voice`, range Z). Design 1's "branch must sit in hot track_render" is out of date | hot_fns.py header notes, ram_fns_z.py |
| Pool tail = range Z free room | 4,144 B; bench rule 4,096 B (48 B spare); normal rule 8,192 B; normal two-core build cannot be built today, estimate 8,560 B (about 368 B spare, HYPOTHESIS) | MERGE_SHRINK1 s5, s7 |
| Range X | 31,836 B used, 676 B free, closed | same |
| Ordinary RAM (.data + .bss) | 67,508 of 73,728 B; headroom 6,220 B; rule 4,096 B; so 2,124 B spare | same |
| Voices | 8 per part, 4 parts in `track_t`, 2 live (`NLIVE` = `NSHOWN` = 2); `voice_t` array is 4 x 8 = 32 slots, so per-voice state in `voice_t` costs twice what state sized NLIVE x NVOICE (16) costs | `core.h` |
| Cost of a note | 4 to 7 points (ANALOG 6.1 + filter 1 1.8; PHYS 16); 1 point = 33 host instructions; RAM code x0.85, flash code x1.1; NOTES limit 80 %, margin 115 % | budget.c, CPU_PLAN |
| Patch record | biggest today 434 B, limit 447 B (8 per chunk); with 6 per chunk limit 596 B; planned worst 534 B (ESTIMATE); 128 slots, 22 chunks x 6 = 132 | EXTENSION_BLOCK_PLAN s1 |
| Flash | chunks 16 to 21 would take 0xBA000 to 0xC5FFF; one 8 KiB pair 0xC6000 to 0xC7FFF stays free (read from source, no linker map exists) | storage.c, EXTENSION_BLOCK_PLAN s3.5 |
| Matrix | 8 slots, 51 destinations of 128, 15 sources of 128; per-voice destinations only PITCH, CUT, SHP, AMP | mod.c |
| Ids | P_COUNT 127, P_E0 119, asserted in core.h; ids 25, 26 still ROOT/SCALE, 42 ALLOC unread, 106 free, 113 ANALOG filter TYPE | EXTENSION_BLOCK_PLAN s1 |
| Settings record | read only at its exact size; a different size resets everyone's settings | settings_persist.c |

## 3. Wave 0: the local step P (before any cloud job; about 2 rounds)

1. Push `wip/join0234` as `cloud/base3` to oscillr/DUELL-core with the same file filter as the earlier snapshot (needs your yes, ALPHA_PLAN Q6).
2. Copy the design documents into the private repo under `docs/design/` in one commit (O1): EDITOR_NAMING_PROPOSAL (rules R1 to R6, approved; for job I), FEATURE_LIST, EXTENSION_BLOCK_PLAN, TWIN_FILTER_DESIGN_2, TWIN_FILTER_DESIGN, FILTER_PAGES_PROPOSAL, SUB_AND_CYCLE_DESIGN, EDITABLE_B_DESIGN, MIDI_MAP_DESIGN, VELO_STUDY, and this plan. The briefs point there. (Without this the jobs would have to work from the short decision lists inside each brief, which is not enough for the filter maths.)
3. **Interface commit** (names and sizes only; flag-off output unchanged): `ext.h` with NX = 16, the slot names X_ROUTE, X_LINK, X_KTRK2, X_ENV2, X_LFO1, X_LFO2, X_SUBLVL, X_SUBOCT, X_SUBWAVE, X_SUBHARM, X_CYMODE, X_CYRISE, X_CYFALL, X_CYCURVE, X_CYSYNC (X0 to X14; X15 spare) with wide ids 128 to 142; the new matrix destination numbers 51 CUTOFF 2, 52 RES 2, 53 DRIVE, 54 BLEND, 55 SUB LEVEL and source number 15 CYCLE; `track_t.p[]` grown from 127 to 144 entries (128 + NX) with the accessor `XV(t,k)`; the hook names `cycle_value(t,v)`, `chain_sub(...)`, `chain_f2(...)` declared; and name aliases for the five on-id values so jobs B and C agree: `P_TWTYPE` (42), `P_CUT2` (106), `P_RES2` (25), `P_DRV2` (26), `P_BLEND` (113). This lets A, B, C, D run in parallel (A stores through `XV`, B makes the rest of the code see the slots).
4. Probe: ask job A to print whether `node`, a headless browser and `numpy` exist in the cloud image (decides the editor tests of job I and the render maths of job J).

## 4. Jobs: order, files, conflicts

### 4.1 The jobs

| Job | Branch | Goal | Depends on | Core files it owns |
|---|---|---|---|---|
| **A** | feat-store | Patch store: 6 records/chunk, 22 chunks, `PT_VER` 3, extension block with sections 1 (extension values) and 2 (editable-B edits), multi `MU_VER` 7 and project `PROJ_REV` 5 sections, backup as per-slot records, erase list, PATCH_RAW limits, factory-multi record upgrade tool | P | upreset.c, storage.c/.h, fm1_flash.h, multi.c, project.c, editor_backup.c, editor_patchraw.c, erase_data.c, tools/multi_upgrade.py, tests for these, web/editor/patchrecord.js, duellbackup.js |
| **B** | feat-values | Make every part of the code see the 16 slots: morph arrays and tags, matrix plumbing (new destinations/source as cold side channels), descriptors, id cleanups (25, 26, 42, 106, 113), factory-sound loader for wide ids, chain stub, NOTES stub | P | core.h, params.c, morph.c (tables), mod.c (new cold functions), sounds.c, tools/gen_duell_sounds.py, voice.c (one hook), new voice_chain.c/.h, new budget_feat.c/.h |
| **C** | feat-midimap | MIDI MAP engine: one 128-line table, dispatch, settings record with the reserved 64 B tail, conflict rules, commands, generator reading the **defaults CSV** (the slot for the Virus CC list) | P | midi_control.c, midi_learn.c, settings_persist.c, editor_learn.c, new editor_map.c, tools/gen_midi_map.py, docs/MIDI_MAP_DEFAULTS.csv |
| **D** | feat-velo | Velocity/aftertouch: sense core (state machine, mapping, calibration), simulated converter, replay format and script, flag-guarded hooks (`key_on`, scan tick, non-blocking converter), cheap aftertouch form | P (+ A for OBJ_VELO) | new sense.c/.h, hal/fm1_adc.h, hal/fm1_input.h, seq.c (key_on, keyboard_block), main.c, master.c |
| **E** | feat-twin | TWIN DSP and logic: chain with filter 2 (LP/BP/HP/NOTCH TRIO-style, LADDER, COMB), DRIVE, ROUTE, BLEND, LINK, the four amounts, CUTOFF 2 destination, morph behaviour, NOTES rows | A, B | new chain_f2.c, voice_chain.c (the sequence), budget_feat.c (TWIN section) |
| **F** | feat-subcyc | SUB layer, CYCLE envelope, WAVE STEPS (engine side; WAVE's own SUB stays until J) | A, B | new chain_sub.c, new cycle.c, eng_wave.c (STEPS only), voice.c (cycle start, 3 lines), budget_feat.c (SUB/CYCLE section) |
| **G** | feat-editb | Editable B: edit routing at the B end, hand-edit list, ROLL/UNDO B, save/load/autosave, header letter A/B | A, B | morph.c (edit paths), ui_draw.c (one new header function), project.c call sites |
| **H** | feat-pages | Unit pages: TWIN FILTER/MOD/CHAIN, SUB OSC, CYCLE ENV, STEPS card, QUICK MULTI layouts, MIDI MAP page, velocity menu entry, hide rules, graphs | B (C, D for their pages) | params.c PAGES, ui.c, ui_draw.c, ui_graph.c |
| **I** | feat-editor | Editor: scope 2 in the protocol, layout.json generation from the new tables, FILTER tab, SUB/CYCLE/STEPS cards, MIDI MAP tab with export/import (the editor's A|B view of End B is a later round, as in EDITABLE_B_DESIGN) | B (C for MAP) | editor.c (scope 2), web/editor/*.js, tests/editordump.c |
| **J** | feat-convert | Engine filters out of ANALOG, TRIO, NOISE; chain before the amp on every engine; filter 1 RES floor; WAVE's SUB retired; engine pages shrink; the conversion tool for sounds and multis; proof renders | E, F | eng_analog.c, eng_trio.c, eng_noise.c, eng_wave.c (SUB removal), uf RES constants, tools/convert_twin.py |

Why this split: it follows your decisions (nothing re-decided) and puts each file under one owner. A and B are the foundation and are merged first; E and F run in parallel because B's stub gives them separate stage files.

### 4.2 Merge conflict map (CONFIRMED overlaps from the design documents and the earlier merges)

| Shared area | Who touches it | How it is avoided |
|---|---|---|
| Patch record, chunks, multi, project, backup | A only (G uses A's list API; C does not touch) | one owner |
| `core.h` (ids, `p[]`), `params.c` TP/descriptors, `morph.c` tables, `mod.c` tables, `sounds.c` | B only; later jobs only **fill** what B created | B pre-fills every table row for all 15 slots (labels, ranges, defaults, morph kinds from EXTENSION_BLOCK_PLAN 2A/2B, destination rows, source 15) so E and F add no table rows |
| The voice chain | B creates `voice_chain.c` with the sequence and empty stage hooks; E owns `chain_f2.c`; F owns `chain_sub.c` | one file each; the only shared edit is the sequence file (E) |
| `mod_voice`, `mod_voice_add` (HOT) | nobody | new per-voice work (CUTOFF 2 sum, CYCLE source) goes into new cold functions called from the voice path (B builds them; F supplies `cycle_value`) |
| `morph.c` | B (arrays/tags, early), G (edit paths, late) | different functions; G after B |
| `eng_wave.c` | F (STEPS hook), J (SUB removal) | sequential: F in wave 2, J in wave 3 |
| Engine files ANALOG/TRIO/NOISE, filter 1 constants | J only | one owner |
| `budget.c` | B adds one call to `budget_feat.c`; E, F, J fill their own marked sections | marked blocks, blank lines between |
| Settings record (`settings_persist.c`, `storage` settings object) | C only, **including** the 64 B reserved tail; ALPHA_PLAN's J8 (strike counter) must hand C its fields or wait | one owner (ALPHA_PLAN 3.4 rule) |
| `editor.c`, `proto.js`, `app.js`, `panels.js` | I (scope 2, tabs); C adds MAP commands in a new file plus 3 dispatch lines | tiny, resolved locally |
| `params.c` PAGES, `ui_draw.c`, `ui.c` | H; G adds one new header function | separate functions |
| `seq.c`, `hal/fm1_*.h`, `main.c`, `master.c` | D only | one owner |
| `host_tests.sh` | every job | append one block at the end; union conflicts |
| Test fingerprints, `layout.json`, `docs/PATCH_NUMBERS.md` (absent in the cloud repo: use a stub) | never regenerated in the cloud | local helper, once per wave; J's changes are by design |
| `ram_fns_x.py`, `ram_fns_z.py`, `hot_fns.py`, `build.py`, `elfcheck.py` | local only | the jobs list functions and sizes instead |
| ALPHA_PLAN jobs J7 (click hunt: voice.c, mod.c, mix path), J8 (second-core logic), J10 (bus B), J12 | J12 is **replaced** by A + B. J7 touches voice.c/mod.c like B, E, F. J10 would take X/Z room too (about 4 KB of bus B code, see CLOUD_RESULTS_JOB6) | if J7 runs, merge it before wave 2; J10 and this plan compete for the same RAM (section 5): decide together |

**Merge order inside a wave:** wave 1: A, B (together), C, D. Wave 2: E, F, G, then H, I. Wave 3: J. After each wave: full `host_tests.sh`, compare the pass/fail list with the previous head (known failures: perform SPACE 1.55 vs 1.5 in DUEL, bench INSERT rows, `twocore_tsan` vlong, flaky `uf`), one device build.

## 5. Per-job design pointers, proofs, and the device facts

Common to all: do not re-decide anything in FEATURE_LIST.md; with a feature at its off/default value the rendered samples are bit-identical to `cloud/base3` (every test fingerprint unchanged), except where this table says "by design"; the existing host tests are the gate, including `partindep` and the two-core simulation (new state must be per part, never written by both cores); no new test tooling; never regenerate a fingerprint.

| Job | Designs to implement (decided) | Host tests must prove | Report for the local fit check |
|---|---|---|---|
| A | EXTENSION_BLOCK_PLAN 3.1 to 3.7 as recommended: 6 per chunk, sparse pairs, dense in multi/project, `PT_VER` 3 / `MU_VER` 7 / `PROJ_REV` 5, per-slot backup (archive v5), reserve motion id 127 and the OBJ_VELO object on 0xC6000 (O4) | all sound fingerprints unchanged; old versions refused; save, power cycle, restore, PATCH_RAW and backup round trips; torn write at every byte; "128 saved, any slot still writes" with the real worst record; unknown tags/ids skipped; wrong length refused; fuzz | real worst record (limit 596 B), chunk/flash addresses, every new buffer (`pt_rbuf`, `pt_enc`, `up_rec_t` ring) with bytes and **where** (pool or .bss) |
| B | EXTENSION_BLOCK_PLAN 2A/2B and 3.4; TWIN_FILTER_DESIGN_2 3.1 to 3.5 (ids, destinations 51 to 54, morph kinds); SUB/CYCLE morph kinds | fingerprints unchanged; ext slots at default change nothing; old ids' rolls give the same B ends except the five re-defined ids (list them); partindep and two-core sim still bit-exact | RAM per slot and in total at morph history depth 9 and at 3; `mod_begin`, `morph_apply` size change; which hot functions the build touches (should be none) |
| C | MIDI_MAP_DESIGN s2 and s3 as recommended (5 own CCs: 80, 81, 82, 83, 85 for CUTOFF 2, RES 2, SUB LEVEL, CYCLE RISE, CYCLE FALL until the Virus list replaces the CSV) | a recorded CC script gives identical parameter writes before and after for all 24 shipped CCs and for learned ones; morph trigger by target; conflict rule; locked numbers refused; settings size exact; generator rejects duplicates and unknown targets | settings record size, table RAM, code size |
| D | VELO_STUDY 1, 2, 4.1 to 4.3 (method only, no copied code) | sense core on a simulated key population and the replay CSV; gap guard; chord window; flag off = same preprocessed tokens as base3 | per-tick cost estimate, state RAM, code size, what the simulation cannot show |
| E | TWIN_FILTER_DESIGN_2 3, FILTER_PAGES s5 (names, hide rules), DESIGN 1 2.1 to 2.6 (chain, types, DRIVE, stability) | TYPE OFF bit-exact; stability (RES 127 + DRIVE 127 + loud chord on every type; one minute of noise with all values jumping each block); no click on TYPE/ROUTE/LINK/DRIVE changes while sounding; LINK conversion keeps the sound; morph rules; DUEL/LAYER/two-core output unchanged | per function size (host -Os x 0.76), per-voice cost per TYPE (host instructions a sample, SERIES/PARALLEL, DRIVE 0/64/127), state bytes per voice |
| F | SUB_AND_CYCLE_DESIGN A, B, C and FEATURE_LIST decisions (SUB everywhere, OCTAVE -2/-1/0, WAVE SINE/TRI/SQUARE/PULSE, HARMONICS, own CYCLE SYNC, STEPS OFF/2..19 with table 24 renamed STAIR) | LEVEL 0 and HARMONICS 0 bit-exact; STEPS OFF bit-exact; CYCLE unread costs nothing; no click at note start/retrigger/steal/OCTAVE-WAVE switch; unison one centred sub; STEPS hysteresis; cross-fade | sizes, state bytes per voice, per-voice costs, FM6 hook outcome |
| G | EDITABLE_B_DESIGN 2.2 and 3.2 with the recommended answers | edit routing only at the B end; edit A after B edits leaves hand values; ROLL B replaces; UNDO B restores; save/load and power cycle keep the list; 32 cap; CC/automation/editor writes go to A | RAM for the "set by hand" marks, code size |
| H | FILTER_PAGES Option B (confirmed), QUICK MULTI three layouts (decided), SUB OSC / CYCLE / STEPS / MIDI MAP / velocity pages only where approved (O2) | page visibility rules; every new label fits its 50 px card by the unit's font table; hide-inert rules per type | code size (flash only) |
| I | FILTER_PAGES s3 (FILTER tab), EXTENSION_BLOCK_PLAN 3.7 (scope 2), MIDI_MAP_DESIGN 2.4 | editor tests against the mock unit if `node` exists | none |
| J | TWIN_FILTER_DESIGN_2 s1 (engine by engine, conversion rules, table 1.5), s5 step 2; SUB_AND_CYCLE A2 (WAVE SUB to SUB LAYER SINE -1) | converter output equals design 2's table rows; idempotent; render comparison old vs new for all 49 sounds and 12 multis (numbers, flagged outliers); renders of the other sounds for the chain-before-amp change; a list of every fingerprint that changes | engine size removed (expected 1.0 to 1.4 KB in range X), new RAM, hot-set effect |

### 5.1 Where each feature's per-sample code lives, what it costs, what fights the hot block (ESTIMATE from the designs; none is measured)

| Feature | Per-sample code lives in | Cost (points a note) | Hot-block risk |
|---|---|---|---|
| TWIN path (scratch, amp after the chain) | new `voice_chain.c` (noinline, range X or Z) | 0.2 to 0.3 (0.4 to 0.5 if filter 1 is bypassed) | none: `uf_run`/`uf_voice` are reused unchanged; the entry branch is in `track_render` (cold) |
| Filter 2 LP/BP/HP/NOTCH | `chain_f2.c` | 0.9 to 1.2 | none |
| LADDER / COMB | `chain_f2.c` | 1.4 to 1.8 / 0.5 to 0.7 (COMB uses the engines' empty memory; not on WAVE, PHYS) | none |
| DRIVE | `chain_f2.c` | 0.4 to 0.5 | none |
| Filter 2 modulation | per block, cold `mod_f2_voice` | 0.05 | none |
| SUB (SINE / SQUARE, HARMONICS) | `chain_sub.c` | 0.7 / 1.0, harmonics +0.3 to 1.1 | none |
| CYCLE tick | per block, new `cycle.c` | 0.02 to 0.05 | **mod_voice is hot.** The design said "tick inside mod_voice": not allowed. Build the CYCLE source as a cold side channel (B) |
| WAVE STEPS | per block, rounding of the WAVE position | 0.01 | **`wave_render` is hot and computes the position (line 848).** Do it through a cold hook that adjusts `m->shape`; if impossible, STOP. J later removes WAVE's SUB (hot bytes come back) |
| Engine filters out (J) | engines | **+0.9 to +1.6 on ANALOG, TRIO, NOISE even with TWIN off** (about one note fewer at 8) | `uf_run` damping constants change for the RES floor: size must not grow |
| Key scan (D) | core 0, flash, 10 kHz tick | 1 to 3 % of core 0, not on the CPU bar: add a "KEY SENSE" row to the NOTES table | none (cold) |
| MIDI MAP, editable B, pages | flash, per event/UI | none | none |

With everything on (LADDER + DRIVE + SUB SQUARE) an ANALOG note goes from about 7.9 to about 11 points (ESTIMATE): a part of 8 notes shows 5 to 6. The NOTES counter pays it; the cost rows come from the Mac bench (local), the cloud may only put ratio estimates (marked est.) in the table.

### 5.2 The RAM fit (ESTIMATE; this is the plan's weak spot)

| | Core-1 code (device bytes) | Ordinary RAM |
|---|---|---|
| TWIN | 1,500 to 1,800 | 0.58 to 1.15 KB (36 B a voice: 16 voices if sized NLIVE x NVOICE, 32 if in `voice_t`) |
| SUB | 400 to 700 | 128 to 256 B |
| CYCLE | 150 to 250 | 128 to 256 B |
| STEPS | 40 to 80 | 0 |
| Matrix, morph, voice-loop glue for 16 slots | 400 to 800 (my estimate, no source) | about 1.1 KB at morph history depth 9 (EXTENSION_BLOCK_PLAN: 70 B a slot) |
| Editable B marks, MIDI map, velocity state, record buffers | flash only | 0.3 to 0.5 + 0.26 + 0.6 to 1.0 + 0.5 KB |
| **Sum** | **2.5 to 3.6 KB** | **about 3.5 to 4.4 KB (sized NLIVE) against 2.1 KB spare** |
| Given back | engine filters 1.0 to 1.4 KB (range X); WAVE SUB 60 to 100 B; `ufilt 2` branch 20 to 40 B | none |

Result: range X (676 B + about 1.2 KB freed) can take the chain, SUB and STEPS code (about 2 to 2.6 KB) only if J lands in the same build; the Z-resident rest (CYCLE, matrix/morph growth, glue: 0.5 to 1.1 KB) exceeds the 48 B bench spare by about 0.5 to 1 KB. Ordinary RAM exceeds its spare by about 1.4 to 2.3 KB.
**Levers already named in the studies (not decided by me):** per-voice state sized NLIVE x NVOICE (instructed in the briefs); morph history for extension slots at depth 3 or in the pool (B reports both); MIDI map table in flash or the pool; the bench pool-tail rule back to the 2,816 B you accepted for BENCHM1 (gives 1,328 B of Z room, bench builds only); and a fallback where part 2 runs on core 0 whenever its settings need code that is not in RAM (the same idea as job 6's "bus B in RAM for these settings"; costs the two-core gain exactly when the new features are used on part 2). **O3 asks you which levers you accept**; the local fit check runs after wave 2 and tells which are needed.

## 6. Local work and what stays with you

**Local helper (cannot be done in the cloud):** the P step; merging each wave; device builds (normal, bench, RAM1) and their checks; **RAM placement of every new function core 1 reaches** (core 1 runs part 2 from RAM): the chain sequence and amp (`chain_voice`, `chain_amp`), filter 2 types and DRIVE (`chain_f2_*`, `f2_coef`), SUB (`sub_add`, its wave and harmonic helpers), CYCLE (`cycle_tick`, `cycle_apply`), the cold matrix evaluators (`mod_f2_voice`, `mod_cycle_voice`), the STEPS hook, and the grown `mod_begin`, `morph_apply`, `part_voice`; these go into `ram_fns_x.py` / `ram_fns_z.py` after `tools/ram_reach.py` lists the real reach; the hot-set check (`tools/hot_fns.py` unchanged, elfcheck); filling the NOTES cost rows from the Mac bench; regenerating fingerprints (after J only), layout.json, PATCH_NUMBERS; the flash-map check of 0xBA000 to 0xC7FFF on a real unit (read from source only so far); the unit test; the unit overflow test of the new cards; NOTICE and credits for Velo; the key recording tool (non-blocking converter + raw logger) for D.

**You, only where not yet confirmed (O2).** Confirmed already (FEATURE_LIST 2026-10-19, checked): TWIN pages = Option B (TWIN FILTER, TWIN MOD, TWIN CHAIN after MULTI FILTER 1/2 and 2/2), DRIVE hidden and not run while TYPE is OFF, new FILTER tab in the editor, the three QUICK MULTI layouts (filter 2 off: CUT, RES, MODE, AMP REL; linked: CUT, RES, DRIVE, AMP REL; unlinked: CUT 1, RES 1, CUT 2, RES 2). **Not confirmed, needs a one-line yes or change:**
1. SUB OSC page: K1 WAVE, K2 OCTAVE, K3 HARMONICS, K4 LEVEL, after the engine pages and before MULTI FILTER 1/2 (SUB_AND_CYCLE A4 option A).
2. CYCLE ENV page: the design gave K1 MODE, K2 RISE, K3 FALL, K4 CURVE, but CYCLE has **five** values (your SYNC decision made the fifth) and no place for SYNC. Recommendation: two pages, CYCLE ENV 1/2 = MODE, SYNC, RISE, FALL; 2/2 = CURVE with the curve drawn. Your call.
3. WAVE STEPS on EDIT 1 K4 (the empty card; WAVE's SUB card disappears in J, nothing else moves).
4. Editable B: the five answers of EDITABLE_B_DESIGN s6 (recommended: B stays where set; ROLL B replaces everything; letter always shown on sound pages while the morph is live; knobs for never-morphed values keep working with a message once; 32 edits).
5. MIDI MAP: the unit page in the GLO family after MIDI LEARN and its views; the five questions of MIDI_MAP_DESIGN s5 (recommended answers are the defaults in brief C). The shipped CC list itself comes from the other helper (section 9).
6. Velocity: a MENU entry (on/off, curve, calibration walk); default OFF.

## 7. Velocity and aftertouch from the keys: the honest split

- **Cloud can do (host-testable):** the sense state machine, velocity and aftertouch maths (log curve), automatic calibration both ways with reset, gap guard after a flash-erase pause, chord window, a simulated converter and key population, the replay format and script, the hooks behind a flag that leaves the build token-identical when off, the cheap aftertouch form (channel pressure from the strongest held key; no per-voice change in the hot `mod_voice`). Constants live in one data table.
- **Must stay local / with you:** the non-blocking converter settings (Velo's divider 7, delay 2 are one author's), the timing against the LED pulses, the pot and battery moving into tick slots, the scan cost on the unit and the cache effect, the raw recording of your 20 keys, the calibration walk, the feel, whether aftertouch is worth keeping, the top 7 keys (fixed velocity, as Velo's default). Plan B inside the job: velocity only, 20 keys, fixed velocity on the top 7.
- **Aftertouch is a matrix source only.** FEATURE_LIST row 1 says "aftertouch into the morph is only worth doing together with velocity", but the same file's rejected list says "a morph played from the keys, velocity or aftertouch". I treat the rejection as final.
- **Recommendation:** run D in wave 1, off the critical path, with constants as data. In parallel the local helper ports the converter reading and a raw logger (about 2 rounds) and you record 20 keys soft/regular/hard (15 to 30 min, 1 to 2 sessions). The replay script then tunes the constants locally. If that is not finished when the rest is ready, ship with velocity OFF. Do not start a second cloud job for tuning unless the algorithm itself must change (a $4 to $6 follow-up in the same session).

## 8. What you must decide or approve before launch

| # | Question | Recommendation |
|---|---|---|
| O1 | Copy the ten design documents into the private DUELL-core repo (`docs/design/`)? | yes |
| O2 | The six UI points in section 6 | accept the recommendations |
| O3 | RAM levers (section 5.2): allow the bench pool-tail rule back to 2,816 B for feature builds; allow the core-0 fallback for part 2; allow morph history depth 3 for extension slots | decide after wave 2's fit check, but say now whether any is forbidden |
| O4 | Use the last free flash pair 0xC6000 for the velocity calibration store (saves stable from the alpha; the erase test's planted pattern moves elsewhere) | yes |
| O5 | Velocity in the alpha only if tuned in time; otherwise OFF | yes |

## 9. The MIDI CC slot for the Virus-based map

Job C builds the map engine and reads its shipped defaults from **`docs/MIDI_MAP_DEFAULTS.csv`** through `tools/gen_midi_map.py` into a generated header. Format (one line per CC): `cc,target,part_rule,tag` with `target` a name from a fixed list (the C names of the part values `P_...`, the extension slots `X_...`, the morph actions, the MODE values, `CUTOFF_SMART`, `RES_SMART`), `part_rule` one of CHANNEL, PART1, PART2, GLOBAL, `tag` SHIPPED or LOCKED. The generator **fails** on a duplicate CC, an unknown target, a locked standard number given to a parameter, or a missing entry for one of today's 24 shipped CCs unless the line says `REMOVED`. Job C ships a PLACEHOLDER CSV (today's 24 CCs plus the five proposed new ones). When the other helper's Virus-based proposal is final, it only has to deliver that CSV (and the target names for any value it adds); the local helper drops it in, runs the generator, and nothing else changes. If the proposal wants more than the 128 numbers allow or a different table shape (for example banks), tell me before job C starts.

## 10. Cost and time (ESTIMATE; typical job $4 to $12, credit about $200 after job 5, job 6's cost not recorded)

| Job | $ | Local rounds to verify and merge | Note |
|---|---|---|---|
| A | 8 to 12 | 1 | a second session possible |
| B | 8 to 12 | 1 | |
| C | 6 to 10 | 1 | |
| D | 6 to 10 | 1 | tuning after the recording is local |
| E | 10 to 14 | 1 | the biggest DSP job; may need a follow-up message ($3) |
| F | 8 to 12 | 1 | |
| G | 5 to 9 | 0.5 | |
| H | 5 to 9 | 0.5 | |
| I | 4 to 8 | 0.5 | |
| J | 8 to 12 | 2 | plus your listening session |
| **Total** | **68 to 108** | **about 9.5, with waves merged together about 6** | repairs reserve $30 |

Local rounds in all, ESTIMATE: P 2, wave merges 6, RAM fit + device builds + NOTES rows 3, fingerprint regeneration and fixes 1 = **about 12**, then your two sessions (listening to the 43 + 6 sounds and 12 multis; the unit test). Check the credit before each wave; never start a job twice.

## 11. Conflicts between your scope and the joined line, with a recommendation

1. **RAM and the hot block (5.2).** Recommendation: build everything, decide O3 at the wave-2 fit check; do not touch the 228 B hot rule.
2. **Engine filters leaving the engines vs "bit-exact".** The conversion changes 43 sounds, 10 multis, and the other sounds slightly (chain before the amp on every engine, RES floor). Recommendation: J alone does it; the tool, not hands; it keeps your seeds, AMOUNTs and PATCH LEVELs (it reports, it does not retune; re-picking End B seeds or levels only on your yes) and also converts anything you saved on the old firmware (it reads old-format records); your multis are converted as you decided, you listen first.
3. **Per-voice state size.** `voice_t` is sized for 4 parts; 2 are live. Recommendation: new per-voice state in arrays sized NLIVE x NVOICE (halves it).
4. **BLEND needs id 113, which frees only when ANALOG's filter TYPE goes.** Recommendation: job B retires ANALOG's TYPE switch (every factory ANALOG sound is LP; the engine keeps LP only until J); B reports the tests that change.
5. **Editable B in FEATURE_LIST says "not yet designed"; EDITABLE_B_DESIGN.md exists** with five open details: recommended answers are used as defaults (O2).
6. **FILTER_PAGES recommended leaving QUICK MULTI alone; FEATURE_LIST decided three layouts later.** I used the later decision.
7. **ALPHA_PLAN planned only "reserve-only" format room in one job J12 and feature fill-in later.** Now you want the features in: A + B replace J12 and the fill-in happens in wave 2.
8. **The settings record has one owner** (ALPHA_PLAN 3.4): C. If J8 (strike counter) runs, it must give C its fields.
9. **Cloud has no drafts** (REFERENCE_NOTES): O1.
10. **Velocity vs the rejected "morph from velocity/aftertouch":** section 7.

## 12. Suggested GLOSSARY.md additions (not made)

wave (of cloud jobs); interface commit; stub; extension slot, wide id, extension block; chain; range X, range Z, pool tail, reach, hot block (the file has "hot RAM" only); TYPE, LINK, OFFSET, ROUTE, BLEND, DRIVE as used on the TWIN pages.

## 13. Decisions and corrections (2026-10-20, written when wave 0 was done)

**Owner answers (2026-10-20):**

| # | Answer |
|---|---|
| O1 | Yes: the design documents are copied into the private repo under `docs/design/` (done; the set is listed in `docs/design/` itself). |
| O2 | The recommendations on the six UI points of section 6 are accepted (SUB OSC page; CYCLE ENV in two pages with SYNC on page 1; STEPS on EDIT 1 K4; the five editable-B answers; the MIDI MAP page; the velocity MENU entry). |
| O3 | All four RAM levers are allowed (pool-tail rule back to 2,816 B for feature builds, core-0 fallback for part 2, morph history depth 3 for extension slots, MIDI map table in flash or the pool). The choice is made after wave 2's fit check. |
| O4 | Yes: the last free flash pair 0xC6000 to 0xC7FFF holds the velocity calibration store (object `OBJ_VELO`, job A task 7). |
| O5 | Velocity is OFF in the alpha unless it is tuned in time. |

**Corrections found while doing wave 0 (the text above this section is not edited; where it disagrees, this section wins):**

1. **Two id numbers in the designs are wrong; the names are right.** Section 3 and the designs write TWIN TYPE on id 42 ("ALLOC") and BLEND on id 113 ("ANALOG's filter TYPE"). In `core.h` (counted from the enum; P_COUNT 127 and P_E0 119 check) ALLOC is **43** (42 is PRIO, in use) and ANALOG's filter TYPE, `P_FTYPE`, is **101** (113 is `P_DKTHR`, the DUCK THRESHOLD, in use). Ids 25 (ROOT), 26 (SCALE) and 106 (`P_DKNOTE`, retired and free) are right. The aliases in `ext.h` are therefore bound by name (`P_TWTYPE` = `P_ALLOC` = 43, `P_BLEND` = `P_FTYPE` = 101) with static asserts, and the briefs B, C and E carry 43 and 101.
2. **The MIDI map is `MIDI_MAP_DESIGN_2.md` (the owner's final).** It replaces section 5's row C and section 9 above: the five own CCs are **21 CUTOFF 2, 22 RES 2, 14 SUB LEVEL, 115 CYCLE RISE, 116 CYCLE FALL** (not 80, 81, 82, 83, 85), and the map has 88 direct lines, not "today's 24 plus five". Today's 24 CCs therefore do **not** all keep their numbers (morph and MODE numbers 20 to 25 and 111 to 118 move, the delay send goes 94 to 92). The shipped list is no longer a separate helper's file: job C writes `docs/MIDI_MAP_DEFAULTS.csv` from section 4 of the design. The first version `MIDI_MAP_DESIGN.md` and `MIDI_MAP_PROPOSAL_2.md` are not in the repo (as instructed); briefs H and I point at the design's section 5, brief C carries the table specification itself.
3. **Still open for the owner:** the eight exceptions X1 to X8 of MIDI_MAP_DESIGN_2 section 6 are proposals, not decisions. Job C uses the recommended answers as defaults; a refused exception later only deletes its lines from the csv.
4. **Interface commit, as built:** `ext.h` (constants first, hook declarations after the types; core.h includes it twice), `track_t.p[]` is `EXT_P_LEN` = 144 entries, `rsv_sz` 4 to 2 so `sizeof(track_t)` stays a multiple of 16: +32 B a part, +128 B ordinary RAM for the four parts. The matrix numbers are `EXT_MD_CUT2` 51, `EXT_MD_RES2` 52, `EXT_MD_DRIVE` 53, `EXT_MD_BLEND` 54, `EXT_MD_SUBLVL` 55, `EXT_MS_CYCLE` 15 (prefixed so that job B can put its own `MD_*` / `MS_*` names into mod.c without a clash). The hook argument lists are provisional; job B owns them.
5. **Not copied:** `EDITOR_NAMING_PROPOSAL.md` (section 3 listed it, the ticket did not, and no brief points at it: the naming rules are written into brief I) and `HEADLINE_FEATURES_2.md` (no brief points at it).
