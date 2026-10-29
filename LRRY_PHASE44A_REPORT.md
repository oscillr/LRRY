# L/RRY phase 4.4a: MORPH, randomizer and matrix on the L/RRY parameters, old engines out (2026-10-29)

Work is in `fw/felucca-base`. Host only. No device build, no flashing, nothing ran on the unit. DUELL's old test suite was not run. Not touched: `twocore*.c`, the twocore tests, `budget.c`, `fx.c`, `lrry_voice.c`, `lrry_mac.h`, `lrry_tab.h`, `host/`.

## Summary

- MORPH works on all 58 L/RRY parameters. The 36 new slots have tags now. The per-engine tag table and the FM6 patch morph are gone.
- Randomizer (the ROLL): 1000 rolled patches, 3000 renders. All values stay in range, nothing that must not move moves, no stuck note, the master output never clips.
- **One real failure, not in my files:** a signed overflow in `fx.c` `part_mix_st` (the 4.3a stereo part mix). It happens in 6 of 1000 random Originals with no MORPH at all, and in 6 of 3000 rolled renders. The randomizer does not cause it. The patch space inside `PARAM_LIST.md`'s ranges reaches it. Details and fix options in "Test results" and open choice 1.
- Matrix: all 34 destinations the voice reads move the sound. The 29 others have no reader (no engine, effects OFF). All 16 live sources move filter A's cutoff. DUCK stays 0 (no ducker on L/RRY).
- Old engines: the DSP of all 10 engines is removed, plus `phys_dsp.c`, `phys_symp.c`, the msfa synthesis and its tables. Flash about 45 KB less (45339 bytes) and RAM about 2.2 KB less (2211 bytes), host estimate, method below.

## What changed, per file (all under `fw/felucca-base` unless named)

`firmware/src/morph.c`
- Tags for X28..X63 in `MTX`, and `morph_osc_tag` for P1..P3 by MODEL (as DUELL tagged an engine's E slots by its WAVE).
- Filter A MODE (`P_FREEC`) and SLOPE: a choice now (see the rules below). DUELL's multimode-filter safety rules (`morph_uf_range`, `morph_uf_safe`, `morph_uf_open`) rewritten for the 4 modes. The windows are DUELL's numbers at the blend's pure points (LP = 0, BP = 64, HP = 127).
- No engine anywhere: `MET[]` is gone, and so is every `const engine_t *e` argument. P_E0..P_E7 never roll. ANALOG's rules (cutoff steering, "no dead band-pass") now apply to filter A, because the 4.3a voice path stands on ANALOG's flags. PHASE and LOFI rules gone.
- Routes: a WILD roll never moves or adds a route onto a destination nothing reads (TIMBRE, SAT, the sends, E1..E8). This is DUELL's own rule ("only what the engine answers to"), applied to L/RRY.
- FM6 patch morph removed (NFM bytes, `fm6_morph` hook, history arrays). `morph_view_fm` returns the stored byte, so the FM6 pages still compile and show the patch. The field `fnid` stays (always 0) because `budget.c` and `voice.c` read it.
- New gates (SMART): see the rules below. Two resonance-against-drive rules re-paired (filter B RES with DIRT B DRIVE, filter A RES with DIRT A DRIVE).

`firmware/src/lrry_param.c`: one `_Static_assert` that morph.c's copies of the MODEL and MODE numbers match the voice's.

`firmware/src/eng_*.c` (10 files): descriptor only. Kept: names, EDIT cards, presets, voice caps and flags, the mode-dependent labels, and the helpers the page graphs draw with. Removed: note-on, render, block, amp and done, and every DSP helper. Each file has a header line saying so. `.mem` sizes kept as numbers (PHYS 12888, FM6 1824), so a sound load clears the part memory exactly as before.

`firmware/src/engines.c`: one stub note-on and one stub render for all engines. The table, the indices and the order are unchanged (stores and the editor protocol hold them).

`firmware/src/fm6_core.c` renamed `fm6_layout.c`. It keeps only the patch layout (FP_*) and msfa's 32 algorithm tables with their carriers (FM6 page, editor, stores). Apache-2.0 header kept.

`firmware/src/phys_dsp.c`, `firmware/src/phys_symp.c`: deleted.

`firmware/src/dsp.c`: 5 helpers only engines used are gone (`tsvf_coef`, `tsvf_lp`, `voice_amp`, `atg_pitch`, `atg_inc`).

`firmware/src/bench.c`: no longer resets engine state that does not exist (VOICE noise, WHEEL rotor, FM6 LFO, the FM6 morph bytes).

`tools/gen_tables.py`: the FM6 tables are no longer generated (FM6_EXP2, FM6_LFO_DELTA, FM6_DETUNE, FM6_FINE, the FM6_* units).

`tools/gen_duell_sounds.py`: group names for X28..X63. Without them the generator stops ("64 slots read, 28 expected"). Needed for any build.

`LICENSING.md`: DaisySP and Rings rows marked removed. msfa row points to `fm6_layout.c`.

`tests/lrry_morph_test.c`: new, my test (below).

## MORPH rule per parameter group

Columns: SMART / WILD. "Fader" is what the fader does when the two ends differ: blend, or flip at the middle. Every row reuses an existing DUELL tag. The test prints the full table (section 1).

| Group | Parameters | Tag (DUELL sibling) | SMART | WILD | Fader |
|---|---|---|---|---|---|
| Filter A | CUT, RES | multimode filter CUT / RES | smooth, safety windows | smooth | blend |
| Filter A | KEY TRK | its own KEY TRK | smooth, at most 32 | same | blend |
| Filter A | MODE, SLOPE | TWIN TYPE (a choice the voice does not cross-fade) | never | flips | middle |
| Filter B | TYPE, LINK, ROUTE | TWIN | never | flips (TYPE never to OFF, OFF never on) | middle |
| Filter B | CUTOFF, ENV 2 | TWIN | smooth (held while TYPE OFF) | smooth | blend |
| Filter B | RES | TWIN RES 2 | smooth, ceiling max(orig, 108) | smooth | blend |
| Filter B | KEY TRK 2, LFO 1, LFO 2 | TWIN | smooth, at most 32 | smooth | blend |
| Sources | OSC 1 / 2, NOISE, RING LEVEL, SUB LEVEL | SUB / noise level (MF_RISE) | smooth, at most 48 above orig | smooth | blend |
| Sources | PAN (all 4), FILTER A / B PAN, DIRT PANs | PAN (L/RRY is always stereo) | smooth, full range | smooth | blend |
| Sources | BAL (all 4), BLEND | TWIN BLEND | smooth, held while filter B OFF | smooth | blend |
| OSC | MODEL | engine WAVE (not faded here) | never | flips | middle |
| OSC | COARSE | TRANS, TRIO intervals (T_PIT) | only with PITCH on, drawn not set | steps (PITCH on) | middle |
| OSC | FINE | the fine detunes | smooth, at most 10 cents | smooth | blend |
| OSC P1..P3 | PULSE width, SYNC ratio | ANALOG / LOFI width, ANALOG SYNC | smooth | smooth | blend |
| OSC P1..P3 | SUPER detune / saws / spread | fine detune (13 steps = 10 ct) / stepped count / SPREAD | smooth cap / never / smooth | smooth / flips / smooth | blend / middle / blend |
| OSC P1..P3 | FM ratio / index | FM6 MRAT (T_PIT) / FM6 MLVL | PITCH only / smooth, at most +12 | | |
| OSC P1..P3 | FOLD amount | none | never (open choice 3) | never | |
| OSC P1..P3 | unused ones (SAW TRI SINE, P2 P3 of PULSE SYNC) | an engine's unused slot | never | flips | middle |
| DIRT | TYPE A, TYPE B | INSERT TYPE | never | flips | middle |
| DIRT | DRIVE A, DRIVE B | DRIVE (MF_RISE 32) | smooth, at most 32 above orig, held while the unit is OFF | smooth | blend |
| DIRT | LEVEL A, LEVEL B, PAN A, PAN B | level / pan, INSERT gate | held while the unit is OFF | smooth | blend |
| DIRT | ROUTE | ROUTE | never | flips | middle |
| Output | MID | PATCH TRIM (never) | never | never | |
| Output | SIDE | SPREAD | smooth | smooth | blend |
| Amp env | ATK DEC SUS REL | unchanged DUELL | as DUELL | as DUELL | blend |
| Sub | OCTAVE | unchanged DUELL | never | flips | middle |
| No engine | P_E0..P_E7 | "no card, no sound" | never | never | |

DUELL rules kept as they were: WILD has no caps or loudness rules, VOICE MODE flips in WILD (571 of 1000 WILD ends moved it), mixer LEVEL, TRIM, MUTE, LFO LEVELs and STRUM never move.

## Randomizer ranges (SMART unless named)

- Every value stays inside its `PARAM_LIST.md` range (the descriptor's min and max), in SMART and WILD.
- A smooth value moves by 1 to AMOUNT % of its range, then the tag's limit applies.
- Levels (OSC 1, OSC 2, NOISE, RING, SUB, DIRT A / B LEVEL): never more than 48 above the Original. They may go down.
- Drives: never more than 32 above the Original. RES above 100 is never rolled together with more DRIVE (filter B with DIRT B, filter A with DIRT A).
- Pans: the full range, as DUELL's PAN. The pan law adds at most +3 dB on one side.
- Filter A: LP CUT at least min(Original, 45), BP 36 to 50 with RES at most 40, HP at most max(Original, 25), RES at most max(Original, 60). Filter B: DUELL's TWIN windows.
- Envelope: ATK at most max(Original, ~1 s), SUS within about -2 / +2 dB of the Original, DEC limited on decaying sounds.
- Never touched by MORPH at all: the master and global settings (they are not in the part's values), MID, mixer LEVEL, TRIM, MUTE.
- Matrix: SMART rolls only the amounts of active routes. WILD may move a route or add up to 2 (at most 4 active), only onto CUT, AMP, PAN, RATE, DEPTH, RATE 2, DEPTH 2.

## Test results

Run from `fw/felucca-base`, the flags of `fw/onepart.sh`:

```bash
../onepart.sh 1 1 lrry_morph lrry_fw
```

The build headers need the generators first (`python3 ../gen_only.py`, then the font with `--preset barlow` as `tools/build.py` does, and the `build/gen_test` headers as `fw/host_tests.sh` makes them). One more thing: `tools/gen_duell_sounds.py` still stops on factory sound 37 (HORNET: FREEC 11 out of range 0..3, because 4.3a made MODE a choice). I generated `duell_sounds.h` with a scratch copy that clamps. Open choice 6.

`tests/lrry_morph_test.c`: **16 checks, 2 FAIL**. Run time about 2.5 minutes.
1. Tags: all 36 new slots tagged, the gates work, P_E0..P_E7 never roll. 3 ok.
2. Fader: fader 0 is End A exactly and 127 is End B exactly (111 values differed, 36 slots included). At 63 / 64 smooth values lie between the ends and switches flip at the middle. A slot moved by the fader reaches the sound (OSC 2 LEVEL silent at A, rms 8494 at B). The same seed gives the same end. 4 ok.
3. Randomizer, 1000 rolled patches (500 SMART, 500 WILD, every 4th with PITCH, AMOUNT 5 to 100, 200 with 6 chained ROLLs), each rendered as an 8-note chord at fader 0, 64 and 127 (3000 renders), plus every Original unmorphed (1000 renders):
   - values out of range 0, never-values moved 0, SMART levels above +48: 0. ok
   - part sum never reaches 2^30, master output peak 24029 (limit 32767). ok
   - every note ends after its release (longest 20.0 s, after a roll lengthened REL). ok
   - **FAIL: signed overflow in `fx.c` `part_mix_st`, 6 of 3000 rolled renders.**
   - **FAIL: the same overflow in 6 of 1000 Originals with no MORPH.**
   - 88 of 3000 renders are silent (random settings that close or mute everything). Not counted as a failure.
4. Matrix: 34 of 34 readable destinations move the sound (0.5 dB threshold, largest block difference in level L, level R or brightness). The 29 without a reader do not move it. 16 of 16 sources move filter A's cutoff, DUCK stays 0. 2 ok.

The overflow, in detail: `fx.c` lines 2738 and 2739, `ac->l[i] = (xl * gli) >> 12` with `xl = (part sample x LV4(level)) >> 10` and `gli` up to 4096. Both are int32, so the product wraps when `xl` reaches 2^19 (524288). Measured `xl` up to 1868838 (3.6 x the limit) on a random Original, 1823641 on a rolled end. The 4.3a headroom check measured the product before this one and missed it. My part-sum check missed it too, because the sum is read after the wrap. UBSan found it.

`tests/lrry_fw_test.c` (4.3a): **53 checks, 0 FAIL**, before and after the engine removal.

UBSan (`-fsanitize=signed-integer-overflow,integer-divide-by-zero,shift-exponent`) on both tests, final tree: the only runtime errors are `fx.c:2738` and `fx.c:2739` above. Nothing in morph.c, the voice or the decode. There is no float in the firmware path, so no NaN is possible.

Compile checks (host, syntax and object): a build of `tests/editor_test.c` (hostsim, UI core, editor, stores) and of the UI include block of `tests/ui_test.c` (all `ui_*.c`, `ui_graph.c`, upreset, project, storage) both compile. `bench.c` cannot build on the host, but it gives exactly the same device-only errors as before my change, and no new ones. DUELL's own tests of removed code no longer compile: `morph_test`, `phys_test`, `wave_test` (open choice 7). They were not run.

## Flash and RAM saved

Method: no device toolchain here. I compiled the same translation unit (`tests/editor_test.c`, most of the firmware) at `-Os -ffunction-sections -fdata-sections` for x86-64, at the start of this phase (`e937e4d`) and now, and compared `size -A` per section and `nm -S` per symbol.

| | before | after | saved |
|---|---|---|---|
| text (code) | 261191 | 223154 | 38037 B |
| rodata (tables) | 86650 | 79908 | 6742 B |
| data | 35515 | 34955 | 560 B |
| bss (RAM) | 648436 | 646785 | 1651 B |
| flash (text + rodata + data) | | | **45339 B (about 45 KB)** |
| RAM (data + bss) | | | **2211 B (about 2.2 KB)** |

A second unit (the UI include block) gives text -32.6 KB, rodata -6.9 KB, bss -1.6 KB. Biggest single items: PHYS render 3.6 KB, FM6_EXP2 table 4.1 KB, VOICE / ANALOG / TRIO / NOISE renders 1.5 to 1.9 KB each, WHEEL voice state 736 B RAM, the FM6 morph bytes 522 B RAM per part. These are x86-64 numbers. The device (32-bit, JieLi toolchain) will differ, pointer data about halves. The device build will give the real figure.

The part's engine memory (`eng_mem`, 12888 B a part) stays: the L/RRY COMB lines live in it.

## Kept because something else uses it (shared)

- FM6 patch layer in `eng_fm6.c` and `fm6_layout.c` (patch storage, pack and unpack, voice bank, SLOT, the envelope numbers the pages show, the algorithm diagram): the FM6 pages, editor, presets and projects.
- WAVE's table recipes and builder and the user wavetables in `eng_wave.c` (and `wtab.c`, untouched): the WAVE page picture, the editor's wavetable objects, backups. Nothing asks for a table build any more (that was the removed block hook), so the builder never writes into the COMB memory.
- PHASE, VOICE, WHEEL wave helpers (`pd_setup`, `pd_wave`, `vowel_at`, `fres_coef`, `drw_level`): the page graphs.
- `dsp.c` (mulq, clamp, sine, softclip, noise, the TWIN filter coefficients): the voice, the mix, the master and the chain use it.
- All engine descriptors, presets and the ENGINES table: pages, PATCHES browser, editor protocol, stores, `budget.c` (which names `ENG_ANALOG` and the other engines and reads their flags).
- Not removed although dead for sound: the old mono voice chain (`voice_chain.c`, `chain_f2.c`, `chain_sub.c`, the multimode filter in `voice.c`). Parts of them serve the UI (TWIN graph, `twin_link_set`, `twin_type_set`). The compiler already drops what is unreferenced. A cleanup step could take them out (open choice 8).

## Not done, not verified

- Nothing on the unit. No device build, so real flash, RAM, hot RAM and ISR time are unchecked.
- The `fx.c` overflow is not fixed (the file belongs to the dual-core session).
- COPY A TO B does not exist in DUELL's MORPH (the actions are FADER, AMOUNT, TARGET, ROLL, UNDO, SMART, ARP, PITCH). I added nothing. Open choice 2.
- A project's stored MORPH block (`morph_pack`) holds the ids' ends only, not the extension slots (DUELL's format, "the slots at the Original"). After a project load, the L/RRY slots of a rolled End B come back at the Original. A patch's saved End B is fine (it is regenerated from its seed, slots included). Open choice 4.
- `LRRY_ENGINE 0` (the old mono engine path, kept by 4.3a for bench comparisons) still compiles, but it is silent now, and so is the `-DLRRY_ENGINE=0` timing in `budget.c`'s comment.
- Stored seeds: DUELL's WILD rolls of the engine slots and its FM6 bytes no longer draw, so an old DUELL patch's saved End B would roll differently. Irrelevant for L/RRY patches (new format), noted for completeness.
- No UI, page, layout or menu change. The engine pages still show (their cards act on nothing since 4.3a).

## Open choices for the owner

1. **The part-mix overflow (`fx.c` `part_mix_st`).** Options: (a) widen the product: `(int32_t)(((int64_t)xl * gli) >> 12)`, exact, costs an int64 multiply a sample and channel. (b) clamp `xl` to plus or minus 524287 before the product: a hard clip of the part, no wrap, cheap. (c) move gain: `((xl >> 4) * gli) >> 8`, 16 times the headroom, loses 4 low bits. (d) lower the voice's output (bigger pre-shift) and raise it after. (e) limit the patch space (lower maximum levels, MID, SIDE). My advice: (a) or (b), in the dual-core session that owns `fx.c`.
2. **COPY A TO B.** (a) none, as DUELL. (b) add it as a new MORPH action (End B := End A, one undo step). Page design is yours.
3. **FOLD amount (FOLD's P1).** DUELL has no wavefolder, so no sibling rule. Now: never rolled. Options: (a) keep. (b) as DRIVE: smooth, at most 32 above the Original. (c) smooth like a timbre value.
4. **Project MORPH block and the slots.** (a) keep DUELL's format (slots at the Original after a project load). (b) store the 64 slots of both ends (the project format grows by 128 bytes a part).
5. **WILD and the new slots.** DUELL's WILD flips every switch, including DIRT TYPE (OFF to FUZZ), OSC MODEL and VOICE MODE. Options: keep, or hold some of them in WILD too (MF_NEVER).
6. **Factory sounds and FILTER A MODE.** `tools/gen_duell_sounds.py` stops on HORNET (FREEC 11, a blend value, now out of range 0..3). Options: (a) map the old blend to the nearest mode in the factory list, (b) drop the DUELL factory sounds (L/RRY sounds come later), (c) clamp in the generator.
7. **DUELL tests of removed code** (`morph_test`, `phys_test`, `wave_test`, and others of the old suite). Options: delete them, or keep them out of the run list.
8. **The old voice chain** (`voice_chain.c`, `chain_f2.c` DSP, `chain_sub.c`, `uf_run`). Options: remove its DSP in a next step (keep the UI helpers), or leave.
9. **Gates.** I hold the BALs and FILTER B PAN while filter B is OFF, and a DIRT unit's DRIVE, PAN, LEVEL while it is OFF (SMART), as DUELL holds TWIN and INSERT values. Options: keep, or roll them always.
10. **Cross-fade of filter A MODE and SLOPE.** DUELL cross-faded the multimode filter's SLOPE (`MX_SLOPE`). The L/RRY voice has no cross-fade, so MODE and SLOPE flip at the middle of the fader. Option: add a cross-fade in the voice (a voice change, not mine).
