# L/RRY phase 4.3a: the stereo voice is the firmware's sound engine, one core (2026-10-29)

Work is in `LRRY/fw/felucca-base` (plus one line in `LRRY/host/lrry_voice.h`). `fw_base_test11` and `fw_1.0_removal` were not touched. No git, no flashing, no device build, no bench on the unit. Only my own new test was compiled and run on the Mac. The parameter list for page layout is `LRRY/PARAM_LIST.md`.

## What it does now
Notes go through DUELL's allocator, envelope, LFOs and matrix as before. Each voice makes a stereo pair (two int32 blocks). The part's mix is stereo from the voices on: left and right are scaled by level and balance-pan on their own, no mono sum, no mono-then-pan. Then the master stage as DUELL has it (limiter, soft clip, width, bass mono, comp). Effects are OFF: sends are 0 and none of the part's mono stages (SAT, INSERT, EQ, DUCK) runs.

## Per file (all under `fw/felucca-base/firmware/src` unless named)
New:
- `lrry_mac.h`: the number format and the multiplies. `MUL_Q28`, `MUL_Q23`, `MUL_Q24`, `MUL_Q15`, `MAC2_Q28`, all through `LRRY_MUL`, so one edit lowers the precision (`-DLRRY_MUL32` switches to 32-bit products with `LRRY_PRE_A/C` shifts: it compiles, it is NOT tuned, with the default shifts the filters drift). The one conversion Q23 to the firmware's Q15 scale is `LRRY_TO_FW` (shift 8), used in one place (end of `lrry_voice`).
- `lrry_tab.h` and `lrry_tab_data.h` (made by `tools/gen_lrry_tables.py`): coefficient maths without libm and without a divide: tan table (508 intervals on DUELL's cutoff axis), damping table (128 steps), pan law table, quarter-wave sine, tanh, BLEP (M 8 and 4), 2^x, a Newton reciprocal for the SVF's 1/(1+g(g+k)) and for the BLEP's p/inc. Same code on host and device. Flash 29.2 KB.
- `lrry_voice.c`: the kernel, a port of the host voice (sources OSC 1 and 2 with 8 models, NOISE, RING, SUB; filter A SVF 12/24 dB LP BP HP NOTCH; filter B SVF, LADDER, COMB; dirt A and B with 5 types; series or parallel; per-unit pans; mid/side). One control block of CTL (32). No shared per-voice globals: state is `lrry_vs_t` per part and voice, scratch on the stack, the part's `lrry_pp_t` is read only while the voices render, tables are const, the COMB lines are slices of the part's engine memory per voice and channel. Sources nobody hears are not rendered (phases move on).
- `lrry_param.c`: decode of `track_t.p[]` into the kernel's values, once a block (`lrry_decode`), and per voice (`lrry_voice_ctl`: pitch, cutoffs with LFO, FILTER ENV, matrix, KEY TRK, filter B's LINK and routes).
- `tools/gen_lrry_tables.py`, `tests/lrry_fw_test.c` (the check).

Changed:
- `ext.h`: NX 28 to 64 (slots X28..X63, 36 L/RRY parameters). 64 is the most: the motion records use one byte for slots (128..191) and virtual targets (192..255).
- `core.h`: EXT_P_LEN 192, `pscr[].bufR` (right channel), pad of track_t.
- `up_xb.h`: `XB_DEFS` for the new slots, and a dense tag (`XB_T_DENSE`) in the patch record's extension block, used when more than half the slots are off default. Without it the worst patch record grew by 72 bytes and the patch store (6 records a chunk, 128 slots) no longer fit; with it the worst record is 8 bytes bigger and the store geometry is unchanged.
- `params.c`: descriptors of the 36 slots (`XTP`), enum names, 7 new destination names, `P_FREEC` is now a 4-way choice (LP BP HP NOTCH), `MDST_ORDER` to 64.
- `mod.c`: matrix destinations 57..63 (OSC 1 PAN, OSC 2 PAN, FILTER A PAN, FILTER B PAN, MID, SIDE, DRIVE B), MD_N 64.
- `ui_graph.c` (`MDST_GRP`, the new ones at the end of the knob, under AMP and FILTER: a placeholder), `midi_control.c` (ranges), `midi_learn.c` (names).
- `voice.c`: includes the new files; `voice_start` clears a new voice's state (a retrigger or steal keeps phases and filters as before); `track_render` hands each voice to `lrry_voice` and clears both channels. `LRRY_ENGINE 0` keeps the old mono path (it compiles; the right channel is then silent).
- `fx.c`: `part_voice` (no SPREAD, no tail), `part_fx` (effects off, PERFORM MUTE on both channels), `part_mix_st` (the stereo mix), and a real out-of-range read fixed in `part_rest` (`trk[1]` with one part).
- `budget.c`: the voice's cost estimate and the part's items (below).
- `host/lrry_voice.h`: `BLK` may be set from outside (the test builds it with 32). Nothing else of the host voice changed.

## Parameter count
58 voice parameters: 36 new ids, 22 existing ids with the voice's meaning (filter A 5, filter B 9, drive A, BLEND, SUB 2, amp envelope 4). Matrix destinations 57 to 64. Not in the table (no slot left): ring source, filter A and B level, the bits of the two dirt units (CRUSH's DRIVE is its depth). Details: `PARAM_LIST.md`.

## What the host check measured (`tests/lrry_fw_test.c`, 53 checks, 0 fail)
Run: `cd fw/felucca-base; ../onepart.sh 1 1 lrry_fw` (the flags of `onepart.sh`).
1. Tables against libm: tan 3.3e-4 relative (largest near 16 kHz), damping 3e-8, pan law 7e-6 absolute, 2^x 9e-7, 2^56/x 5e-8 (24 bits at the large end), SVF a1 4.2e-4. Sine and BLEP table equal to the host's (0 LSB), softclip bit for bit. These are the stated tolerances.
2. Sample for sample, the real `mix_block` path (part sum before the master) against the host voice, 21 settings (every model, noise, ring, sub, filter A 12/24, filter B SVF/LADDER/COMB, series and parallel, balances, BLEND, both dirt routes, CRUSH, mid/side, a chord), sustain window of 40 blocks: largest difference 1.7e-3 of the peak (SUPER and the two-oscillator detune; most settings 3e-5 to 1e-4; dirt 1e-4 to 8e-4). The amp path matches: fitted gain 0.78118 against 0.78119 expected (DUELL's envelope times velocity). The remaining difference is table and reciprocal rounding and the host's own double rounding. The checks use velocity 100: above 110 DUELL's accent opens filter A, the host voice has none.
3. Left and right: all pans 0 and no stereo source gives left equal to right sample for sample. OSC 1 at pan -63 gives right exactly 0, +63 the mirror. OSC 1 hard left plus OSC 2 hard right: correlation 0.17. SUPER 7 saws at full spread: side/mid rms 1.3.
4. Matrix: LFO 1 on each of the 7 new destinations moves the sound against the run without the route (4.9 to 28 dB), two runs without agree to 0.00 dB.
5. Silence: no note gives exact 0 in the part sum and at the master output. A released note ends (last non-zero block 21 after the release) and the mix is 0 again.
6. Headroom: 8 voices, every source at 127, SUPER 7 saws, hard pans: part sum peak 259166 = 10.8 x VOICE_FS, 12 % of 2^31/1024 (the next product `b x LV4(level)`), master output after limiter and soft clip 19926 (limit 32767). Worst-case settings and 60 random settings of every parameter with 1 to 8 voices: no sample near 2^30, no stuck block. There is no float in the firmware path, so no NaN; a UBSan build shows only DUELL's own left shifts of negative numbers (master_stage.c, pan_follow).
7. Compile checks only (syntax): project, persistence, storage, morph, editor, audio, ota, subcyc, menu_product, bootguard, lcd_boot, velo_mono tests still build with NX 64. `release_test` still stops on the old `LAYER_SCL` (baseline, not mine).

## Voices available (budget.c, an ESTIMATE)
Method: the same test built with `-DLRRY_BENCH` times the real `mix_block` (8 voices less the idle mix); built with `-DLRRY_ENGINE=0` it times DUELL's ANALOG voice, which the table prices at 115 (filter bypassed), 204 (12 dB), 238 (24 dB). The three anchors read 21 to 26 host instructions a nanosecond; 22.5 is used, the rows get a 15 % margin because nothing is measured on the unit. Rows (instructions a sample and voice, before margin): base 280 (one SAW, stereo, no filter), second oscillator +100, SUPER 7 saws +290, PULSE +20, SYNC +25, FM +35, noise +25, ring +70, filter A 12 dB +120 and 24 dB +345, filter B SVF +100, LADDER +345, COMB +110, a dirt unit +85, mid/side +20, SUB +25. The rows add (all on: 73 ns measured, 74 summed).
Result in the existing mechanism (`part_notes`, one core): OSC 1 SAW alone 6 notes; two oscillators 4; SUPER + OSC 2 2; filter A BP + B HP 3; LADDER 2; two dirt units + filters 2. The global cost is 18.9 % of the half (idle mix and the three buses still run on silence). No existing budget number was changed (the old rows are all still there; the L/RRY branch replaces the engine, TWIN, SUB and effect items for the part).
This is below the goal. Honest reading: on the Mac the voice is about 2.7 times DUELL's ANALOG voice for the plainest patch. And int64 products are dearer on the device's 32-bit core than on the Mac, so the true numbers may be worse, not better. See choice 5.

## Not done, not verified
- Nothing ran on the unit. No arm build, so hot RAM, pool tail, flash and ISR time are unchecked. RAM growth by sizeof: about 3.1 KB (voice state 2.0, decode 0.5, right buffer 0.13, track_t +80, morph and motion +0.36); flash for tables 29 KB plus code.
- Dual core (4.3b) is not built. The voice code is ready for it (no shared per-voice state); `lrry_decode` runs once a block inside `track_render`, so a split must decode once before both cores start or give each core its copy.
- The effects have no stereo stages yet. They are skipped, not wrong.
- MORPH: the 36 new slots have no morph tags (morph.c returns "not rolled" for slots past X24, the LFO LEVELs and STRUM too). They are not rolled or faded. No tags were invented.
- Old DUELL tests were not run (as told). The velocity retrigger step cancel (hunt3) is not applied to the stereo block; a retrigger keeps phases and filter states instead.
- Host voice found with a bug: both oscillators SUPER share one set of seven phases (`sup_ph`). The firmware has separate phases; the host file is untouched. The host's SYNC correction (off by default there) is not ported.
- Web editor, `layout.json`, MIDI map defaults and the editor's library labels were not touched. The editor gets the 36 slots through the existing slot protocol; labels repeat (PAN, LEVEL ...), it needs group words from the pages.
- The unit pages still show the old engine pages and effect cards; nothing was designed.

## Open choices for the owner
1. **Slot limit 64.** Three things have no slot: ring source (fixed OSC 1 x OSC 2), filter A / B level (fixed 1.0), dirt BITS (CRUSH's DRIVE is the depth). Options: (a) leave as is; (b) widen the motion format (byte layout of patterns and stores changes) and add them; (c) give up other slots instead (SUB wave and harmonics have no reader now: X8 and X9 could be reused, 2 slots).
2. **Matrix destinations.** 7 added, 64 is the format's limit. Wanted but missing: pans of NOISE, RING, DIRT A, DIRT B; levels of the sources and dirt units; balances. Options: (a) replace destinations that mean nothing without effects (CHO DLY REV, INS A/B/C, IMIX, EQ three, DUCK, bus values, SPACE); stored numbers change, fine before the alpha; (b) widen the format; (c) keep the 7. Where the new ones sit on the DESTINATION knob (now at the end) is page design.
3. **FILTER A MODE.** I kept the id and made it LP BP HP NOTCH (DUELL's blend 0..127 is gone). Option: a new slot for the choice, leaving the blend id unused.
4. **KEY TRK default 32** (DUELL's) means filter A runs on every note below C3 even with CUT open, so it is charged and costs. Option: default 0 for L/RRY patches (0 is how the host voice behaves).
5. **Voice count.** First estimate 2 to 6 notes on one core. Options, none done: (a) a mono shortcut: while every pan is centre and no stereo source is on, left equals right, so filters and dirt run once (halves the filter and dirt rows); (b) lower precision through `LRRY_MUL32` (needs tuning and a listening test); (c) BLEP kernel 4 instead of 8 (`LRRY_BLEP_M 4`, less cost, more aliasing, measurable on the host); (d) turn the buses off (the idle mix and buses are 18.9 % of the half; the buses alone are about 7 points): SENDS OFF as the default, or the 4.2 removal; (e) dual core 4.3b, about twice. The bench on the unit replaces my estimate.
6. **SUB wave and harmonics** (X8, X9): no reader, the sub is a sine. Keep the slots, or remove, or add TRI / SQUARE to the sub.
7. **Engine leftovers** (P_E0..E7 slots, the engine page, SHP / TIMBRE depth cards and matrix SHP): nothing reads them. Options: leave until page design; or map SHP onto OSC 1 P1 (pulse width, fold amount).
8. **MORPH tags** for the new slots (continuous ones smooth, MODEL and TYPE as switches): yours to decide; I added none.
9. **COMB memory.** 16-bit lines, 402 samples, in the part's engine memory: lowest tuned note 110 Hz, lower notes sound an octave up, steps of about -84 dB. Options: 32-bit lines of 400 samples (+12.8 KB), or 16-bit lines of 1024 (about +20 KB), or leave.
10. **Accent.** Velocity above 110 still opens filter A (DUELL's accent through the neutral engine), though velocity default is OFF. Keep, or remove for L/RRY.
11. **Effects on stereo.** When they come back, SAT, INSERT, EQ, DUCK need stereo versions (or run per channel). Until then they are skipped, also if their stored values are not 0.
12. **Patch record format.** The dense tag changes what is stored (old records are not read as before; fine before the alpha). The multi and project records grow by 36 bytes a part for the new slots.
13. **Budget margin.** 1.15 on my estimate rows (DUELL's measured rows have 1.00). Say if you want 1.00 until the unit says otherwise.
