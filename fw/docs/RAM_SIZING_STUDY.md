# RAM sizing for a two-core design: what core 1 would run, and how much of it fits in RAM (2026-10-17)

Study only. No firmware code, branch, commit or push was changed. The one build made for it (plain `tools/build.py`, branch `wip/fix0227`, DUELL 0.2.25 beta, hot RAM level 3) is only the source of the ELF; its `.fwsc` was deleted afterwards.

**KB here is 1024 bytes.** "RAM" means on-chip SRAM, which core 0 and core 1 both reach without the instruction cache.

## 1. What is measured and what is estimated

| Kind | What |
|---|---|
| **Measured** from `build/felucca.elf` (toolchain `nm -S`, `objdump -t`, the `felucca.dis` listing) | every function's address and size; the call graph (calls, tail jumps, addresses of functions taken, engine tables with function pointers); which functions the audio interrupt `fm1_alnk0_irq` reaches; where each function sits (flash or hot RAM); the sizes of the tables the reached code refers to; the unused RAM ranges and their call reach; the hot block |
| **Estimated** | how `mix_block` (9,410 B, one function) splits into areas; how much of each function runs in a block; the cache footprint (sums of function sizes, no cache simulation); the sizes of the tables really touched in a block |
| **Not known** | the cache's line size and ways; whether data from flash and code share the same 32 KB (taken from the owner's brief); whether core 1 has its own PC limit window; whether the boot and update loaders use the three RAM ranges at run time |

Method: functions are the labelled code symbols below 0x02048BC0 (the end of code; read-only data follows) plus the hot block. Edges come from the disassembly. The engines are reached through the pointer tables `ENGINES` and `ENG_*`, which I read from the ELF's bytes. Source areas come from the C file that defines each function (the build is one compilation unit, so a function's file is found by name). Only `mix_block`, which has `events_block`, `mix_part` and the master tail inlined, needed an estimate: I split it by the order of its call sites (see 3.6).

## 2. The audio interrupt's closed graph and RAM today

- **326 functions, 123,502 B (120.6 KB)** are reachable from `fm1_alnk0_irq` in this build (the earlier study counted 337 and about 120 KB on 0.2.24). 111,382 B of them are in flash, **12,120 B (11.8 KB) already in RAM**.
- **Hot block (core 0 today):** 0x01C7C100..0x01C7FC58 inside a block that ends at 0x01C7FD4C (15,436 B). It holds the flash driver (2,924 B, must not call flash) and 31 hot functions (12,268 B; 12,120 B of them are reached from the audio interrupt, the other 148 B are UI drawing helpers). **244 B are left.** Biggest in it: `dly_mod` 1,342, `fm6_note_compute` 1,160, `uf_run` (the multimode filter) 1,074, `wave_render` 1,060, `mix_spread` 844, `ins_swept` 768, `fm6_render` 710.
- **Only bus A's copies are hot.** DUEL's bus B copies (`*_b`, 10.6 KB) and everything in `mix_block` run from flash.

### Unused RAM and the call reach

A direct call reaches +-4 MiB (`R_PI32V2_LONG_JUMP_23M2`, as in `app_hot.ld`). The rule is the same in both directions: a call from address X to code at T works if |T - X| < 0x400000. Data (tables, RAM variables) is addressed with full 32-bit constants and has no such limit. The flash code spans 0x02000120..0x02048BC0.

| Range | Where | Size | What it can call, and what can call it |
|---|---|---|---|
| X: the old RAMTEXT | 0x01C00000..0x01C07F00 (crt0 keeps a debug area at 0x01C07F00) | 32,512 B = 31.75 KB | Nothing in flash (the top end reaches only the first 31 KB of code). Flash cannot call into it. Only RAM-to-RAM calls and calls through a pointer. Needs closed clusters |
| Y: after .bss | 0x01C14820..0x01C1A000 | 22,496 B = 22.0 KB | Flash code below X+4 MiB: the first 83.7 KB of the code from its start, 105 KB from its top end. 74.7 KB of the reachable flash code lies below that |
| Z: the pool's spare end | 0x01C6CE6C..0x01C74000 (the stack guard) | 29,076 B = 28.4 KB | Everything (the hot block rule: needs an address above 0x01C6A85C to reach the end of the image) |
| hot block | 0x01C7FC58..0x01C7FD4C | 244 B | Everything |

- **New usable RAM: 84,084 B = 82.1 KB** (X 31.75 + Y 22.0 + Z 28.4). With the flash driver moved to X (it is already called through a pointer) the hot block gets 2,924 B back: 87,252 B = 85.2 KB with the 244 B.
- **The brief said 32 + 22 + 23 KB. Measured in this build: 31.75 + 22.0 + 28.4 KB.** Z is larger than in 0.2.24 because the pool is smaller now (339,564 B used of 368,640 B). X loses 256 B to the debug area.
- The PC limit has one RAM window today (`_rt_start.._rt_end`, `hal/fm1_guard.h`). Using X, Y and Z needs it widened. Whether core 1 has its own limit is not known.

## 3. Sizes by area (measured from the ELF unless marked est.)

"In RAM" is what the hot block already holds today. Flash = total - RAM.

### 3.1 The 10 engines (render code, with the helpers only that engine calls)

| Engine | File(s) | Total B | KB | In RAM B | Flash B |
|---|---|---|---|---|---|
| ANALOG | eng_analog.c (`analog_render` 1,998, `_f` 1,920, `_m` 2,884, `_x` 1,502) | 8,732 | 8.5 | 0 | 8,732 |
| PHYS | eng_phys.c 1,252 + phys_dsp.c 6,268 + phys_symp.c 1,010 | 8,530 | 8.3 | 0 | 8,530 |
| FM6 | eng_fm6.c 2,782 + fm6_core.c 1,872 | 4,654 | 4.5 | 2,406 | 2,248 |
| PHASE | eng_phase.c | 3,754 | 3.7 | 0 | 3,754 |
| TRIO | eng_trio.c (`trio_render` 2,612) | 3,530 | 3.4 | 0 | 3,530 |
| WAVE | eng_wave.c | 2,622 | 2.6 | 1,954 | 668 |
| LOFI | eng_lofi.c | 2,624 | 2.6 | 0 | 2,624 |
| VOICE | eng_formant.c | 2,394 | 2.3 | 0 | 2,394 |
| WHEEL | eng_wheel.c | 2,000 | 2.0 | 0 | 2,000 |
| NOISE | eng_noise.c | 1,764 | 1.7 | 0 | 1,764 |
| **All 10** | | **40,604** | **39.7** | **4,360** | **36,244** |

Engine glue (`engines.c`, `atgt_bits`): 208 B. **Each engine's call graph is closed.** Its own functions are all it needs, plus `memset` (PHYS) and `memcpy` (FM6, with 5 small morph helpers). That matters for the 32 KB block X. Engines cost 2.0 to 8.7 KB each and average 3.6 KB in flash.

### 3.2 voice.c, modulation, morph

| Part | B | KB | In RAM B |
|---|---|---|---|
| voice.c render side: `uf_run` 1,074 (filter), `lfo_tick` 336, `uf_edge` 160, `esync*` 304, `lfo_*` 168, `atg_voice` 130, others | 2,398 | 2.3 | 1,250 |
| voice.c note side (note on/off, mono, steal, glide: control) | 2,654 | 2.6 | 0 |
| The voice loop inlined in `mix_block` (est., 3.6) | about 2,462 | 2.4 | 0 |
| mod.c, the mod matrix | 2,442 | 2.4 | 466 |
| morph.c | 2,426 | 2.4 | 96 |

### 3.3 Part effects

| Part | B | KB | In RAM B |
|---|---|---|---|
| INSERT: `track_insert` 228, `ins_drive` 726, `ins_swept` 768, `ins_lofi` 1,028, rest 100 | 2,850 | 2.8 | 996 |
| SAT / DIST: `track_dist` 316, `dist_follow` 114 | 430 | 0.4 | 316 |
| EQ: `eq_design` 1,372, `track_eq` 868, `eq_run` 208, `eq_rest` 64, `eq_hz_q8` 78 | 2,590 | 2.5 | 208 |
| Ducker: `duck_run` 418, `duck_part` 296, `duck_pump` 176 (and `duck_half` 158 in audio.c) | 890 | 0.9 | 418 |
| Spread, MODULES fades, pan, level: `mix_spread` 844, `pfx_*`, `pan_follow`, `bal_lvl`, `part_lvl` | 1,350 | 1.3 | 844 |
| Other (`dlyev_clear`) | 30 | 0.0 | 0 |
| **Part effects** | **8,140** | **7.9** | **2,782** |
| The part chain and mix loop inlined in `mix_block` (est., 3.6) | about 1,110 | 1.1 | 0 |

### 3.4 Send effects (bus A copy / bus B copy; B only runs in DUEL)

| Effect | A copy B | B copy B | A in RAM B |
|---|---|---|---|
| Chorus (only `cho_zrun`; the chorus loop is inlined in each delay path) | 70 | 80 | 0 |
| Delay PLAIN (DIGITAL, PING-PONG), `fx_buses_m` (B: inlined in `split_buses`) | 940 | 0 | 0 |
| Delay MOD, `dly_mod` + `fx_buses_mod` | 1,478 | 1,038 | 1,478 |
| Delay EVENT (RAND, OCT UP): `dly_ev` 2,344, `dlyev_read` 330, `fx_buses_ev` 144, sqrt 42 | 2,860 | 2,396 | 0 |
| Reverb ROOM | 310 | 304 | 310 |
| Reverb SPRING (with `spring_glide`) | 1,082 | 1,146 | 0 |
| Reverb HALL | 1,622 | 1,622 | 0 |
| Reverb SHIM: `rev_shim` 362 + `rev_shimx` 792 (the pitch layer) | 1,154 | 1,252 | 362 |
| Reverb RESO | 548 | 544 | 0 |
| Reverb PLATE (with `plate_gain`, `plate_damp`) | 1,712 | 1,692 | 0 |
| Reverb common (low cut, switch, clear) | 644 | 760 | 0 |
| HALL / PING-PONG side mix | 132 | 0 | 0 |
| **Send effects** | **12,552 (12.3 KB)** | **10,834 (10.6 KB)** | **2,150** |
| SPACE (PERFORM K2: sends, tap, width) | 1,630 | | 0 |
| DUEL and MODULES SENDS glue: `split_buses` 1,070, `split_stash` 154, `snd_*` 476 | 1,700 | | 0 |
| The send dispatch inlined in `mix_block` (est., 3.6) | about 900 | | 0 |

### 3.5 Master stages and output

| Part | B |
|---|---|
| BASS MONO `bass_mono` | 738 |
| WIDTH `width_stage` | 548 |
| COMP `comp_stage` 1,332 + `cmp_coef` 204 + `cmp_big` 42 | 1,578 |
| DUEL master `split_master` | 458 |
| MODE dip `dm_step` 116 + `dm_ramp` 154, `ovl_step` 82 | 352 |
| USB output: `uac_tap` 160, `uac_tap48` 430, `uac_render_start` 254, `usb_fixed_dac` 50 | 894 |
| Scope, meters, `perf_master`, click | 598 |
| **master** (all in flash) | **5,166 (5.0 KB)** |
| The master tail and the output loop inlined in `mix_block` (est., 3.6) | about 714 |
| PERFORM stages (perform.c: `perf_block` 1,764, `pf_chan` 1,246, `perf_filter` 838, `perf_harm`, drive, flanger, phaser ...) | 7,516 (1,016 in RAM) |

### 3.6 Mixer and `mix_block` (9,410 B, one function in flash)

Estimated split from the order of its calls (addresses in the function):

| Piece (offset in the function) | B est. | KB |
|---|---|---|
| Events: notes, sequencer, arp, MIDI, clock (0x000..0xE3A) | 3,642 | 3.6 |
| Block set-up: MODE, SENDS, PERFORM start (0xE3A..0x1080) | 582 | 0.6 |
| Voice loop of `mix_part` (0x1080..0x1A1E) | 2,462 | 2.4 |
| Part chain, level, pan, send accumulation (0x1A1E..0x1E74) | 1,110 | 1.1 |
| Send dispatch (chorus, delay, reverb) and SPACE | 900 | 0.9 |
| Master dispatch, WIDTH / BASS MONO / COMP calls, output loop | 714 | 0.7 |

The boundaries are the call sites and are good to a few hundred bytes. The pieces are used in 3.2 to 3.5 and in section 4.

### 3.7 Morph, arp, sequencer, MIDI, clock (control that runs per block)

| Part | B | KB |
|---|---|---|
| morph.c (listed in 3.2) | 2,426 | 2.4 |
| seq.c (sequencer, arp, input) | 7,436 | 7.3 |
| midi_control.c | 3,696 | 3.6 |
| midi_clock.c | 880 | 0.9 |
| motion.c | 814 | 0.8 |
| chord.c | 804 | 0.8 |
| Events total with `midi_out_event` | 13,684 | 13.4 |
| `fm1_alnk0_irq`, `duck_half`, `shed_check` (audio.c) | 1,078 | 1.1 |

Most of this runs only when something happens (a key, a clock edge). It is large in size but small in the cache.

### 3.8 Shared helpers and tables

- Helpers (flash): `memcpy` 172, `memset` 126, `__udivdi3` 262, `__divdi3` 76, `div_divlu` 144, `div_nlz32` 58, `div_samples` 72, `beat_samples` 58, `exp2_neg` 54, `step_set_nudge` 38: **1,060 B**.
- **Tables:** the reached code refers to 89 tables of 26,268 B in flash. That is an upper bound; a block touches a few lines of each. Main ones: `UAC_FIR` 7,680 (USB output only), `FM6_EXP2` 4,100 (FM6), `TP` 2,540 (events, mod matrix), `SINE` 2,050 (used by almost every area: engines, chorus, delay, SHIM, PERFORM), `PITCH_OCT` 768 (ANALOG, LOFI, NOISE, PHASE, WAVE, RESO), `GP` 680, `TANH_Q15` 514, `ENV_LIN` 512, `LFO_INC` 512, `INS_BITS_STEP` 512, `PF_G` 324. No sample ROM is touched.
- **Touched in a block (est.):** 4 to 8 KB on the voice side (SINE, PITCH_OCT, TANH, ENV, SVF, LFO_INC, FM6_EXP2 for FM6), 2.5 to 5 KB on the send and master side (SINE again, LFO_INC, LOCUT_K, COMP tables). With SINE shared: about 5.5 KB typical, 11 KB worst.

### 3.9 Check

Sum of all areas above = 123,502 B, the same as the graph total.

## 4. Core 1's code in each split

"Total" = all code core 1 may run (any mode, any engine, any effect type), as it stands in the ELF. "Must be in RAM" = total minus what the hot block already holds. DUEL's bus B copies are in the sets where core 1 can reach them, because the mode can change at run time. The A figure includes DUEL.

| Option | Core 1 runs | Total B | Total KB | Already in RAM KB | Must be in RAM KB | Share of the 82.1 KB |
|---|---|---|---|---|---|---|
| **A** per part (part 2) | all 10 engines + glue, voice render and loop, mod matrix, morph, part effects and chain, helpers, and in DUEL bus B (all `*_b`, `split_buses`, `split_stash`) | 72,908 | **71.2** | 8.7 | **62.5** | 76 % |
| A in STEREO / LAYER only | the same without bus B | 60,850 | 59.4 | 8.7 | 50.7 | 62 % |
| **B** by function | all send effects (A and B copies), SPACE, DUEL glue, master, PERFORM, helpers | 42,072 | **41.1** | 3.1 | **38.0** | 46 % |
| **B2** B plus part effects | B + part effects and part chain | 51,322 | **50.1** | 5.8 | **44.3** | 54 % |
| **C** voice jobs (both cores run the same code) | all 10 engines + glue, voice render and loop, helpers (mod matrix and morph stay on core 0, which prepares the values) | 46,732 | **45.6** | 5.5 | **40.2** | 49 % |

Notes:
- In A and C the two cores run **the same** voice code, so one RAM copy serves both. The cost of the shared code is counted once.
- In B and B2 the code is split. Core 0 keeps what core 1 does not run.
- Core 0 in A still runs events, the interrupt, bus A, the master and PERFORM from flash. In B it runs all voices, engines and part effects from flash.

### What the whole graph would need

Everything reachable (120.6 KB) in RAM needs 24,130 B (23.6 KB) more than the hot block plus the new ranges plus the flash driver's 2.9 KB can hold (97.0 KB). Without the control code (events 13.4 KB, `mb:events` 3.6 KB, voice note side 2.6 KB) the audio-only graph is 103,522 B (101.1 KB): still 4,150 B (4.1 KB) over. So the earlier answer stands: all of the audio path in RAM does not fit; a half or less does.

## 5. Does each core-1 set fit in the usable RAM?

By size, **all four fit**. A with DUEL is the largest at 62.5 KB of 82.1 KB (76 %). Placement rules still shape it:

1. **X (31.75 KB) takes closed clusters only.** The engines are closed, so seven engines fit in X: PHYS 8,530 + ANALOG 8,732 + TRIO 3,530 + PHASE 3,754 + LOFI 2,624 + VOICE 2,394 + NOISE 1,764 = **31,328 B** (+ `memset` 126 B copied in). The engines are entered through pointers already. Nothing else in X can call flash.
2. **Z (28.4 KB) takes the rest of the voice, part and send code** (it reaches all of flash and flash reaches it).
3. **Y (22.0 KB) takes what is left that calls only the first 84 KB of flash code.**
4. A DUEL: 62.5 KB = X 31.3 + Z 29.1 + Y 2.1 at the least; about 20 KB of slack remains. B: 38.0 KB fits in Z + Y. B2 and C: 44.3 and 40.2 KB fit in Z + Y (C: engines in X).
5. The PC limit window must cover X, Y and Z. The mix loops that are inlined in `mix_block` (voice loop 2.4 KB, part chain 1.1 KB, send dispatch, master tail) have no function of their own today. A, B2 and C would need them cut out of `mix_block` first; the size counted here is that estimate.
6. Moving the flash driver into X gives the hot block 2.9 KB back for other audio code.

## 6. What stays in flash, and the cache footprint

The cache footprint is the code and tables a core runs in one block, from flash. Both cores share the one 32 KB cache, so the footprints add up (code both run is counted once). **These are sums of measured function sizes for stated scenes; the scene choice and the executed share are estimates.**

Scenes:
- **Typical (T):** STEREO, two parts with an average engine (3.6 KB of flash code each), mod matrix on, one INSERT, EQ, DUCK, chorus + plain delay + HALL, COMP + WIDTH + BASS MONO, a few events.
- **Worst (W):** DUEL, ANALOG on one part and PHYS on the other, MORPH and matrix on, every part effect type, EVENT delay + SHIM on both buses, PERFORM stages, a running sequencer and MIDI clock.

Flash code each area adds, KB (hot RAM code left out):

| Area | T | W |
|---|---|---|
| Interrupt and block set-up | 1.6 | 1.6 |
| Events | 1.0 | 5.9 |
| Engines (two parts) | 7.1 | 16.9 |
| Voice render and loop | 3.5 | 3.5 |
| Mod matrix (+ morph in W) | 1.9 | 4.2 |
| Part effects | 2.0 | 5.2 |
| Part chain and mix | 1.1 | 1.1 |
| Send effects (+ SPACE, glue in W) | 3.8 | 11.3 |
| Master (+ PERFORM in W) | 3.6 | 8.0 |
| Helpers | 0.5 | 0.5 |
| **Code total** | **26.1** | **58.2** |
| Tables touched (est.) | 5.5 | 11 |
| **Today, one core, hot block as is** | **31.6** | **69.2** |

So even on one core the typical scene's footprint is about the whole cache and the worst scene is over twice the cache. That fits the unit's layout swings of up to 15 points.

### With each core-1 set placed in RAM

Core 1's flash use is then its tables only (and a waiting loop of about 60 B in RAM). The union is what the cache must hold.

| Option | Core 1 flash (tables) T / W | Core 0 flash: code + tables T / W | Union T / W | vs today |
|---|---|---|---|---|
| A (per part, voice and part code in RAM) | 4 / 8 | 16.0 / 33.6 | **16.0 / 33.6** | -15.6 / -35.6 |
| B2 (sends, master, part effects in RAM) | 3.5 / 6.5 | 19.6 / 40.6 | 21.6 / 43.6 | -10.0 / -25.6 |
| C (voice code in RAM) | 4 / 8 | 21.0 / 48.8 | 21.0 / 48.8 | -10.6 / -20.4 |
| B (sends and master in RAM) | 2.5 / 5 | 22.7 / 46.9 | 24.2 / 49.9 | -7.4 / -19.3 |

What core 0 keeps in flash:
- **A:** interrupt, events, the send effects of bus A, the master, PERFORM (T 10.5 KB code; W 22.6 KB, bus B is on core 1 in RAM).
- **B2:** interrupt, events, engines, voice loop (the voice side stays on core 0).
- **C:** everything except the engines and the voice loop: mod matrix, part effects, sends, master, events.
- **B:** all voices, engines, part effects and the mod matrix (T 18.7 KB code, W 38.9 KB).

### With only the hot block (nothing new in RAM)

The union stays at 31.6 / 69.2 KB in every split: the same code runs, only on two cores. Core 1's flash code would then be its set (T / W): B 7.4 / 19.3 KB, B2 10.5 / 25.6, C 10.6 / 20.4, A 15.6 / 30.9 (of which the part-2 share beyond what core 0 already runs is mostly one other engine). This is the situation of the bench with 40 KB of core 1 code: core 0 misses deadlines.

### Inner loops only

- The current hot set (R3, 12.1 KB reached) covers 88 % of host samples of the bench scene but only 38 to 51 % in the factory multis (M01..M04, M14, M15): their ANALOG and HALL reverb stay in flash (from `tools/hot_fns.py`; host shares, not the unit's).
- **Largest functions not in RAM** (flash): `mix_block` 9,410 (mostly events, cold), `analog_render_m` 2,884, `trio_render` 2,612, `dly_ev` 2,344, `analog_render` 1,998, `dly_ev_b` 1,948, `analog_render_f` 1,920, `formant_render` 1,900, `perf_block` 1,764, `rev_plate_b` 1,692, `rev_hall` and `rev_hall_b` 1,622, `rev_plate` 1,598, `noise_render` 1,552, `analog_render_x` 1,502.
- **B inner loops only** (`fx_buses_m` 940, `rev_hall` 1,622, `comp_stage` 1,332, `bass_mono` 738, `width_stage` 548 and the send and master dispatch 1.6 KB: about 6.8 KB): the rest of core 1's code is one delay mode and one reverb type per bus at a time (up to 2.9 KB + 1.7 KB), so about 5 to 7 KB of flash code and 2.5 KB of tables remain on core 1 in the typical scene.
- **A or C inner loops only** (the 8 engines not yet in RAM are 36 KB; the four common ones ANALOG, TRIO, PHASE, VOICE are 18.4 KB, with the voice loop 2.5 KB): 21 KB in RAM leaves the other six engines (the one in use averages 2.6 KB) and the part effects in flash. I did not total this; it lies between the all-RAM A row and the hot-block-only row above.
- **Overlay idea (not tested):** only the engines, the delay mode and the reverb type that are selected need to be in RAM. Slots of 8.7 KB (engine, x2), 2.9 KB (delay) and 1.7 KB (reverb) per bus would cover every choice, if code could be loaded into a slot when a patch changes. The cost is a copy from flash at each change and code that is position-independent; I did not examine either.

## 7. Conclusion

| Option | Fits in RAM? | Must be in RAM | Cache union T / W (KB) | Remark |
|---|---|---|---|---|
| A per part | Yes, tightest: 76 % of the new space with DUEL | 62.5 KB | 16.0 / 33.6 | Lowest cache load. Needs bus B and all engines in RAM and the voice loop and part chain cut out of `mix_block`. Both cores run the same RAM engine code |
| B2 sends + master + part effects | Yes, 54 % | 44.3 KB | 21.6 / 43.6 | Voice side still in flash on core 0 |
| C voice jobs | Yes, 49 % | 40.2 KB | 21.0 / 48.8 | The largest rework of the voice code (see `CPU_PLAN.md`) |
| B sends + master | Yes, 46 % | 38.0 KB | 24.2 / 49.9 | Easiest to place, leaves the most in the cache |
| Whole audio path (option D) | **No** | 120.6 KB against 97.0 KB | | Short by 24 KB; 4 KB short even without the control code |

- **Every core-1 split fits by size; no split puts the combined cache load safely under 32 KB in the worst scene.** In the typical scene only A (16 KB) leaves the cache half empty; B2 and C reach about 21 KB; B about 24 KB.
- **The worst scene (DUEL, heavy engines, everything on) stays at 34 to 50 KB in every split**, so the cache still thrashes there. To cut it, the engines used by core 0 and the section's largest areas would have to be in RAM as well; they do not all fit (section 4).
- **The three ranges are not equal:** X (31.75 KB) can hold only closed clusters, which the 10 engines are. Z (28.4 KB) can hold anything. Y (22.0 KB) reaches only the first 84 KB of flash code.
- **Largest items to watch:** `mix_block` (9.4 KB, mostly events), the ANALOG engine (8.7 KB in four variants), PHYS (8.3 KB), the 10.6 KB of bus B copies, PERFORM (7.3 KB), `dly_ev` (2.3 KB).

## 8. Open questions and where the brief and the repository differ

1. **Unused ranges:** the brief said 32 + 22 + 23 KB. This build gives 31.75 + 22.0 + 28.4 KB (section 2).
2. **Function count:** 326 functions in this build, not 337 (0.2.24).
3. **Files:** the worktree `wip/fix0227` has no `docs/DUAL_CORE_STUDY.md` or `docs/CPU_PLAN.md`; they are in the main repository. This file is therefore written in the main repository's `docs/`. The worktree had no built ELF. The first `tools/build.py` run stopped for lack of Pillow and fontTools; I installed both into the session's scratch folder (not into the system or the repository) to build.
4. Does the boot or update loader use X, Y or Z at run time? (Also asked in `DUAL_CORE_STUDY.md`.)
5. Does core 1 have its own PC limit window, and how wide?
6. Cache line size and ways, and whether flash data and code share the 32 KB: with those, the sums in section 6 could be replaced by a simulation, or by the unit's own counters.
7. How much of `mix_block`'s events code runs in a block? I used 1 KB typical and 5.9 KB worst; it is an estimate.
