# CPU plan: eight notes a part, never a crackle (study, 2026-10-17, revised after the owner's decisions)

Research only. No DUELL code was changed, nothing was committed, built for the unit or pushed. The other firmwares were cloned into a scratch folder and read for their mechanism; no code of theirs is copied here. Base: worktree `wip/rel0224` (0.2.24 test build, 16d97b9), `wip/dsp0225` (74a015d, the 32-bit EQ), and the earlier study `docs/DUAL_CORE_STUDY.md`. This file replaces `docs/CPU_PLAN_4NOTES.md`.

## The owner's decisions this plan follows

1. The master SATURATOR is removed from DUELL. None of the numbers below include it.
2. No skipping of idle effects. A synth must be ready for the worst case, so only savings that lower the worst case are planned.
3. The goal is 8 notes a part, up to 10 where possible, with any patch, in any mode. Where an engine cannot, that engine's polyphony is limited, the limit is documented and shown, and nothing crackles.
4. Second core: Felucca's per-part split, **on by default**, with automatic fallback to one core and a notice on the home screen. No added latency.
5. A per-part voice budget computed from what is switched on, shown as `NOTES n` and enforced.
6. Fallback for DUEL: one send effect a part.

## How to read the numbers

Every figure is a share of one core's time for a half buffer (128 frames, 2.90 ms). Three kinds, marked everywhere:

- **measured (host)**: instruction counts on the Mac for the 0.2.24 source, in the bench's scale (33 host instructions a sample = 1 %). Made with two scratch harnesses built from `tests/cpu_bench.c` (not in the repo): an idle breakdown per stage and a 4 + 4 scene table per engine. The official bench reproduced its baseline within 2 %. Notes beyond 4 a part could not be measured: today's pool is 8 voices shared by both parts. They are extrapolated, which is safe because a voice's cost adds up linearly (the 1-voice and the 4-voice measurements agree within about 10 %).
- **device est.**: the host figure times the factors of `docs/TESTING.md` "CPU on the unit": code in the hot RAM block x0.85, code in the flash x1.1, RANDOM / OCT UP delay and the SHIM pitch shifter x1.5, PHYS x1.3, dsp0225's 32-bit EQ about x1.2. Which code is in RAM was read from the 0.2.24 ELF. **These are estimates, not readings.**
- **measured (unit)**: only three exist. 0.2.10's TEST 3 HEAVY read about 60 % where the model said 65 to 75 %; the 2026-10-15 bench builds moved BASE from 75 % to 89.5 % by code placement alone; 0.2.23's M16 overloaded at one key where the model said 78 %. So the device estimates carry an error band of about 15 points until stage 2 measures them.

The target is **80 % on the busier core**, not 100 %. The tables are steady states (held notes). A real worst block adds note starts, steal fades, engine cross-fades, and the key scan and USB nested in the audio interrupt. The 20 % kept back is for those.

"Typical" effects: both parts PHASR INSERT + SAT + one EQ band, chorus, DIGITAL delay, HALL, COMP. "Heavy": the same with RANDOM delay, SHIM PITCH DOWN with MOD 50, WIDTH and BASS MONO. STEREO costs the same as LAYER (measured host, within 0.1 %).

## In short

- **One core cannot do 8 notes a part.** With typical effects on both parts, one core holds 3 to 5 notes a part (device est.), 2 to 4 with heavy effects.
- **Two cores with the per-part split reach 8 a part for most engines with typical effects:**
  - LOFI, WAVE and FM6 reach 8 (LOFI 10);
  - PHASE, NOISE, TRIO and ANALOG reach 7 to 8;
  - VOICE and WHEEL reach 6 to 7;
  - with heavy effects, 5 to 7.

  In DUEL the gain is larger when part 2's own effects run on core 1 too (D+).
- **A voice share on top (part of one part's voices rendered on the other core) adds 1 to 2 notes where the parts are unequal.** It lets a part playing alone reach 10 for every normal engine. It is the stage after.
- **So "8 everywhere" needs per-engine limits.** These are the guaranteed limits with the heaviest effects in the worst mode, on two cores (dearest factory sound, device est.):

  | Limit | Engines |
  |---|---|
  | 8 or more | LOFI, WAVE |
  | 7 | FM6, PHASE, NOISE, TRIO |
  | 6 | ANALOG, VOICE, WHEEL |
  | 2 to 3 | PHYS (one part only) |

  With typical effects they are 1 to 2 higher, and the `NOTES n` budget gives each part what its current settings allow.
- **PHYS:** one part only (the owner's rule). 3 notes beside a part of up to 4 normal notes on two cores, 2 beside 8. On one core PHYS cannot share the unit with a busy part.
- **DUEL, one send effect a part:** saves about 5.5 % (typical) to 12.7 % (heavy) device est., which is 1 to 2 notes a part. DUEL without any sends is the cheapest mode of all: every normal engine 8 a part on two cores, LOFI, WAVE and FM6 10.
- **Second core on by default** is workable with the other firmwares' safety net:
  - a check word on every job;
  - a wait limit inside the half;
  - a crash record that survives the reset and saves 2ND CORE OFF;
  - the watchdog read as a hang;
  - the boot guard that skips core 1 after a failed start;
  - a home-screen notice.

  ChoralRoot (since 0.22) and X0X do exactly this, on by default.
- **What to build first:** the `NOTES n` budget with its enforcement, on one core, with the estimated table and a safety margin. It makes "never crackle" true on every build, it is what the second core needs anyway, and it shows honest numbers today. In the same step: the SATURATOR removal and dsp0225's EQ.

## 1. What runs, and the savings that lower the worst case

What runs with nothing playing (STEREO, power-on: HALL, DIGITAL, REVERB LOW CUT 75, chorus send 0). It is kept here because it is also the fixed part of every playing block.

| Stage | measured (host) % | device est. % |
|---|---|---|
| events (clock, arp, MIDI ring) | 0.73 | 0.8 |
| clearing the five block sums | 0.27 | 0.3 |
| two silent parts (render shell 1.11, morph 0.73, matrix 0.67) | 2.72 | 3.0 |
| chorus (left out since 0.2.21 while its line and input are exactly 0) | 0 | 0 |
| delay DIGITAL | 1.43 | 1.6 |
| reverb low cut | 0.31 | 0.3 |
| reverb HALL | 5.80 | 6.4 |
| HALL's side into the mix | 0.36 | 0.4 |
| master out: DC blocker x2, limiter, soft clip x2 | 3.07 | 3.4 |
| **mix_block total** | **15.1** | **16.6** |

DUEL: 21.7 % measured (host), 23.9 % device est. Of that, both buses are 14.6 and the two low cuts 0.6.

One bus, input silent (measured host / device est.):

| Type | host / device est. % |
|---|---|
| reverb ROOM | 4.7 / 4.0 |
| reverb SPRING | 5.1 / 5.6 |
| reverb HALL | 5.8 / 6.4 |
| reverb SHIM (UP) | 7.0 / 5.9 |
| reverb SHIM PITCH DOWN | 8.4 / 8.0 |
| reverb RESO | 4.4 / 4.9 |
| reverb PLATE | 5.5 / 6.1 |
| delay PING-PONG | 1.8 / 2.0 |
| delay RANDOM and OCT UP | 4.8 / 7.2 |

Master stages when on (measured host): COMP 1.7, WIDTH 1.1, BASS MONO 1.2. The SATURATOR was 5.1 and is removed.

**Savings that lower the worst case (planned):**

| Saving | worst case, device est. % | Sound change |
|---|---|---|
| master SATURATOR removed (owner) | 5.6 off the worst possible setting; its code (about 1.5 KB of flash) leaves the cache | the stage is gone |
| SHIM PITCH BOTH removed (owner) | about 1.3 STEREO, 2.4 DUEL against PITCH DOWN | BOTH is gone |
| part EQ in 32-bit (dsp0225, done on its branch) | about 1.3 a part with one band, up to 4 with three | within 1 step |
| DUEL: HALL's side output, which DUEL never mixes, not computed | about 0.5 to 1 (estimate, not measured) | none |
| hot RAM placement (section 9) | unknown: up to 15 points either way on the 2026-10-15 bench | none |

**Not planned (owner, decision 2):** skipping effects while they are idle. The exact-zero shortcuts (master out on an exactly silent block, a silent part's shell) give the same samples, but they only help when idle, so they are left out too. The chorus's own exact-zero skip of 0.2.21 stays as it is.

## 2. Voice costs per engine

| Engine (dearest factory sound) | 1 voice, measured (host) %: dearest / mean | a voice of 4, measured (host) % | code in | device est. % a voice: dearest / mean | engine cap today |
|---|---|---|---|---|---|
| LOFI (WOW FLUTTER) | 5.0 / 3.9 | 4.5 | flash | 5.0 / 4.3 | 8 |
| WAVE (DIGITAL PAD) | 7.3 / 5.6 | 7.3 | RAM | 6.2 / 4.7 | 8 |
| FM6 (HAN SOLO) | 8.3 / 7.3 | 8.2 | RAM | 6.9 / 6.2 | 6 |
| PHASE (WIRE HARP) | 6.6 / 4.5 | 6.5 | flash | 7.2 / 5.0 | 8 |
| NOISE (SEA ORGAN) | 7.3 / 5.5 | 6.7 | flash | 7.4 / 6.0 | 8 |
| TRIO (CHIP CHAPEL) | 7.4 / 6.1 | 6.9 | flash | 7.6 / 6.7 | 8 |
| ANALOG (GLASS SHARDS) | 7.3 / 5.5 | 7.1 | flash | 7.8 / 6.1 | 8 |
| VOICE (VESPERS) | 8.4 / 7.7 | 7.9 | flash | 8.6 / 8.5 | 4 |
| WHEEL (LESLIE STORM) | 7.9 / 6.5 | 7.8 | flash | 8.6 / 7.2 | 8 |
| PHYS (PAN WHISTLE) | 15.7 / 12.1 | 16.0 | flash | 20.8 / 15.7 | 3 |

Extra costs:

- The multimode filter, where a sound did not have it: 12 dB about +1.8 a voice (model); 24 dB about +2 (model) and +2.6 (device est.).
- Part stages while the part sounds (measured host): INSERT 1.4 to 3.0 (PHASR the dearest), SAT 1.5, one EQ band about 0.7, DUCK 1.0.
- The typical set's stages come to about 4.7 a part (device est.).

FM6's cap of 6 and VOICE's of 4 are not memory limits: an FM6 note is 304 B (host build) and the part's engine memory is 12,888 B. So the caps can follow the budget.

**Memory for 10 notes a part:** a voice slot is 124 B (host build), so 10 slots instead of 8 cost about 1 KB more for the four tracks. The shared pool of 8 (`voice.c` `voices_busy() < NVOICE`) becomes a per-part limit.

## 3. Eight notes a part: which engine fits where

Both parts sounding the same engine, the same number of notes each, the busier core at or under 80 % (device est., dsp0225 EQ, hand-over 1.5 % a core). The columns are LAYER/STEREO typical, LAYER/STEREO heavy, DUEL typical, DUEL heavy.

The options:

- **(A)** one core.
- **(D)** per part: part 2 on core 1 (Felucca 1.6.1).
- **(D+)** per part, and in DUEL also part 2's own bus (chorus, delay, reverb of side B) on core 1, in the same block, with no latency.
- **(E)** D+ plus a voice share: core 1 also renders a few voices of part 1 when that balances the cores (ChoralRoot 0.22's idea).
- The earlier study's option (effects on core 1 one block later) is dropped: it adds 0.73 ms of latency, and it leaves every voice on core 0.

**Dearest factory sound (what "any patch" must survive):**

| Engine | (A) one core | (D) per part | (D+) | (E) |
|---|---|---|---|---|
| LOFI | 5 4 4 3 | 10 8 9 7 | 10 8 10 10 | 10 10 10 10 |
| WAVE | 4 3 3 2 | 9 7 8 6 | 9 7 9 8 | 10 9 9 8 |
| FM6 | 3 3 3 2 | 8 6 7 5 | 8 6 8 7 | 9 8 8 7 |
| PHASE | 3 2 3 2 | 7 6 6 5 | 7 6 8 7 | 8 8 8 7 |
| NOISE | 3 2 3 2 | 7 6 6 5 | 7 6 8 7 | 8 7 8 7 |
| TRIO | 3 2 3 2 | 7 5 6 4 | 7 5 7 7 | 8 7 7 7 |
| ANALOG | 3 2 2 2 | 7 5 6 4 | 7 5 7 6 | 8 7 7 6 |
| VOICE, WHEEL | 3 2 2 2 | 6 5 5 4 | 6 5 7 6 | 7 6 7 6 |

**Mean factory sound (an everyday patch):**

| Engine | (A) | (D+) | (E) |
|---|---|---|---|
| LOFI | 6 4 5 4 | 10 10 10 10 | 10 10 10 10 |
| WAVE | 5 4 4 3 | 10 9 10 10 | 10 10 10 10 |
| PHASE | 5 4 4 3 | 10 8 10 10 | 10 10 10 10 |
| FM6, ANALOG | 4 3 3 2 | 9 7 9 8 | 10 9 9 8 |
| NOISE | 4 3 3 2 | 9 7 10 8 | 10 9 10 8 |
| TRIO | 3 3 3 2 | 8 6 9 7 | 9 8 9 7 |
| WHEEL | 3 2 3 2 | 7 6 8 7 | 8 8 8 7 |
| VOICE | 3 2 2 2 | 6 5 7 6 | 7 6 7 6 |

**One part playing alone** (the other silent or muted), dearest sound:

| | One core | (D+) | (E) |
|---|---|---|---|
| LOFI | 7 to 10 | 7 to 10 | 10 for every normal engine in every column (the voice share splits the one part over both cores) |
| WAVE, FM6 | 5 to 9 | 5 to 9 | 10 |
| ANALOG | 5 to 7 | 5 to 7 | 10 |
| VOICE, WHEEL | 4 to 6 | 4 to 7 | 10 |
| PHYS | 1 to 2 | 2 | 4 to 5 |

The per-part split alone cannot help a part that plays alone, except in DUEL, where its bus can move.

**PHYS on one part, the other part a normal engine** (largest PHYS count under 80 %):

| Other part | (A) one core | (D+) | (E) |
|---|---|---|---|
| 4 notes, typical ANALOG | 0 to 1 | 3 | 3 |
| 8 notes, typical ANALOG | 0 | 2 to 3 | 2 to 3 |
| 4 notes, VOICE/WHEEL dearest | 0 | 3 | 3 |
| 8 notes, VOICE/WHEEL dearest | 0 | 1 to 2 | 1 to 2 |

**Per-engine limits to document and show** (dearest sound, so they hold for any patch; the budget raises them when less is switched on):

| Engine | one core: typical / heavy, worst mode | two cores (D+): typical / heavy, worst mode | two cores + voice share (E): typical / heavy |
|---|---|---|---|
| LOFI | 4 / 3 | 9 / 8 | 10 / 10 |
| WAVE | 3 / 2 | 8 / 7 | 9 / 8 |
| FM6 | 3 / 2 | 8 / 6 | 8 / 7 |
| PHASE | 2 / 2 | 7 / 6 | 8 / 7 |
| NOISE | 3 / 2 | 7 / 6 | 8 / 7 |
| TRIO | 3 / 2 | 7 / 5 | 7 / 7 |
| ANALOG | 2 / 2 | 7 / 5 | 7 / 6 |
| VOICE | 2 / 2 | 6 / 5 | 7 / 6 |
| WHEEL | 2 / 2 | 6 / 5 | 7 / 6 |
| PHYS (one part) | 1 / 0 beside a busy part, 2 alone | 3 beside 4 notes, 2 beside 8 | 3 / 3 |

Where the voices of a sound use the 24 dB slope although the factory sound did not, take 1 more note off (about +2.6 a voice, device est.).

**Ten notes:** reachable on two cores for LOFI everywhere, for WAVE and the lighter patches of PHASE, NOISE, FM6 and ANALOG with typical effects, and for every normal engine on a part playing alone with the voice share (E). The dearer engines stay at 6 to 8 when both parts play.

**A no-latency extra for STEREO and LAYER (ChoralRoot's "job 2"):** once both parts' sends are complete, core 1 runs the reverb while core 0 runs the chorus, the delay and the master. It gives about +1 note with heavy effects (FM6: 6 to 7), at the cost of a second hand-over each block and the reverb's state living on core 1. Optional, after (D).

## 4. What each choice gains and loses

| Choice | Gain | Loss |
|---|---|---|
| one core only | runs on every unit; simplest; no chip risk | 2 to 5 notes a part with the dearer engines; 8 never |
| (D) per part, Felucca 1.6.1's method | about double the notes when both parts play; no latency; the same samples as one core; proven in Felucca, Jangada, X0X; DUELL's mix loop is Felucca's, so it fits | a part playing alone gains nothing; in STEREO/LAYER the shared effects stay on core 0 and cap the heavy case; DUELL's own coupling to untie (DUCK SRC PART, morph, matrix, shared scratch); some chips misread on core 1 |
| (D+) DUEL bus B on core 1 | DUEL +1 to 3 notes a part (DUEL heavy: VOICE 4 to 6, LOFI 7 to 10) | bus B's scratch to split from bus A's (`lo_buf`, `part_buf` in the reverb switch) |
| reverb on core 1 in STEREO/LAYER (job 2) | about +1 note with heavy effects | a second hand-over a block; more state on core 1 |
| (E) voice share | +1 to 2 notes where the parts are unequal; a part alone gets 10; best balance short of Melodee | each engine's voice render must touch only its voice (FM6's shared operator buses, the noise generators, PHYS); more tests |
| Melodee-style voice jobs in full | the best balance in every case | the largest rework (section 8b); Melodee also runs all audio code from RAM, which does not fit DUELL (about 120 KB, section 9) |
| per-engine limits shown to the user | never a crackle; the user sees why | the dearer engines play 6 to 7, not 8, with heavy effects |
| DUEL one send effect a part | 5.5 to 12.7 % (device est.), 1 to 2 notes a part in DUEL | DUEL loses the full effect set a part |
| heavy effects allowed (RANDOM, SHIM DOWN, WIDTH, BASS MONO) | the sound options stay | about 1 to 2 notes a part less wherever they are on (the budget shows it) |

## 5. The polyphony budget: `NOTES n`

**What exists elsewhere:**

- **Melodee** (BUDGET, GLO > GLOBAL K2): one device-wide allowance of 8 to 32 weighted points, default 16. A voice costs a fixed weight by engine (Prophet 3 points a voice with one Prophet track, 4 with several; FM6, SID and DRUM 2; CZ 3). Idle tracks reserve nothing. A new note over the allowance steals with the usual priorities (the lowest held POLY note protected). Lowering BUDGET only affects new notes. It counts engines only, not what is switched on in a track; it is one shared number, not shown per track; and its weights have "limited device validation" (their own words).
- **Nord Modular** (Clavia): polyphony "depends upon patch complexity"; the editor shows the patch's DSP load, and a patch gets the voices that fit (a user reported 22 of 24 voices requested).
- **DUELL's own VOICE CAP** (0.2.11, `voice.c`): a per-part cap from per-engine costs and the filter slope, without the effects, shown only on the TEST screen, never below 4. That floor is why 4 + 4 of a heavy pair overloads today.

**As built (job 4, branch `cloud/notes`, one core):** `firmware/src/budget.c`, described with its tables and tests in `docs/MIXSPLIT_NOTES.md` section 8. It differs from the design below in two places: the number is the most a part can play while the other plays the same, not the leftover form of item 4; and the second-core numbers (item 3) are not built.

**DUELL's design:**

1. **A cost table** in microseconds of a half, one row per item:
   - each engine a voice (its dearest factory sound);
   - the filter slope (12 / 24 dB) a voice;
   - the morph extras a voice;
   - each INSERT type, SAT, an EQ band, DUCK, the arp's own step work;
   - each delay mode, each reverb type, the chorus;
   - COMP, WIDTH, BASS MONO;
   - the mixer and master out per mode;
   - the hand-over.
2. **Each part's cost** = its stages (what is on: the INSERT type, SAT if its drive is above 0, the EQ bands whose gain is not 0, DUCK if its depth is above 0) + n x (voice + slope + morph extras). Arp notes are voices like any other, release tails included (a tail holds a slot).
3. **Each core's fixed load** comes from what runs on that core:
   - core 0: events, the mixer, and in STEREO/LAYER the shared effects and the master stages;
   - core 1: part 2, and in DUEL bus B and side B;
   - on one core, everything.

   An effect counts when any part sends to it, whether a note sounds or not (no idle skip, decision 2).
4. **`NOTES n` for each part** = the largest n for which the busier core stays at or under 80 %. It is never more than the engine's documented limit (section 3) or the pool (8, later 10).
   - When the two parts share a core (one core, or in STEREO/LAYER before the voice share), the room on that core is split so both parts get the same n. The exception: a part with fewer stages may get one more, and a muted part costs 0.
   - Recomputed every UI frame (as `vcap_poll` is now), so a change shows at once.
5. **Enforcement:**
   - At note-on, a part already at n takes its own oldest releasing voice, else its oldest held voice that is not its lowest note, with the existing short steal fade. The new note always sounds; nothing is refused silently.
   - When n drops because something was switched on (the user turns on an EQ band or a 24 dB slope), the voices above n fade out over about 20 ms, releasing ones first.
   - The overload shed of `audio.c` (85 % and 100 %) stays as the last guard and should never trigger. A counter on the TEST screen shows if it ever does.
6. **Where to show it:**
   - in each part page's header (`NOTES 8`, the part's own number), because that is where a part's settings change it;
   - on the GLO MIXER page next to each part's level, as a read-only figure for both parts at a glance (a display, not a control, so the page rule on mixing global and part controls is kept);
   - for a moment when it drops: `NOTES 6: EQ` or `NOTES 5: 24 DB`, naming the largest contributor (UI honesty rule);
   - in the editor, the same number per part and per patch;
   - on the TEST screen in developer builds, the full prediction against the measured time.
7. **Keeping it honest before the unit is measured:**
   - The table starts as host bench figures x device factors x a margin of 1.15, so it errs low. The `NOTES` numbers on one core will look small until stage 2 replaces them.
   - The TEST screen shows the predicted and the measured busier-core time side by side, so any row that is too low shows at once.
   - After stage 2 the table holds the measured worst per item plus 10 %.
   - A release only ships with a table measured on that release's build, because the code layout moves the costs.

## 6. Second core on by default: what the others do, and DUELL's design

| | Default | Start check | Misread / stall | Crash or hang beside core 1 | After a failed start | Shown |
|---|---|---|---|---|---|---|
| X0X (fm1-x0x) | ON (GLO > 2ND CORE) | alive flag | check word; one core for the session | 1.0.7: saved one core after a two-core crash | boot guard: one core after a failed start (as Melodee) | crash screen "THE SECOND CORE CRASHED" |
| Melodee | dual core only since 1.4.0 (no single-core package) | alive flag | check word; one core after a misread, fault or 10 ms stall | (not read) | boot safety guards | |
| ChoralRoot | ON since 0.22 (OFF 0.16 to 0.21) | 20 ms alive | check word; 8 ms wait limit, core 0 then renders the job itself; second misread: held for good; saved OFF | a `.noinit` record (core 1 ran, why barred) survives the reset; a watchdog reset while core 1 ran counts as a hang; the next boot saves Dual Core OFF | the boot after a counted failed boot does not start core 1 (nothing saved); it cannot boot-loop | red message after the splash, the reason under Options > Dual Core; single-core package as well |
| Felucca 1.6.1 | OFF (beta) | alive flag | check word; 4 ms wait limit; a misread clears 2ND CORE ON | none persistent | | |

DUELL already has the pieces under it: a watchdog armed at boot and fed in the main loop, a boot guard that counts a crash or hang in the first 30 s (`main.c`, from Felucca), a `.noinit` debug record with the reset cause, and a SAFE start after repeated failures.

**DUELL's design:**

- **Default and switch:**
  - MENU > SYSTEM > 2ND CORE: ON by default (a new unit, ERASE DATA, a reset of the settings). OFF by the user's choice.
  - A change takes effect at the next power-on, and the line says so.
- **Start (power-on, before `audio_init`):** started only if 2ND CORE is ON, no bar is recorded and the boot guard is clean. Then:
  1. set the supply levels (see the open decision below);
  2. start core 1;
  3. check its alive flag within 20 ms;
  4. mark the `.noinit` record "core 1 ran".
- **During play:**
  - Every job carries a check word.
  - Core 0 waits only up to a deadline inside the half (8a below).
  - A wrong check word, a missed deadline or a fault on core 1: core 1 is held for good, core 0 renders part 2 itself in that block, and the record is marked with the reason.
  - From the next block the one-core `NOTES` apply: extra voices fade over about 20 ms.
  - The header shows `2ND CORE OFF`.
- **Crash or hang:** a crash beside core 1 marks the record before the crash screen and the reset. A watchdog reset while "core 1 ran" is set counts as a hang.
- **Next boot:** a marked record (crash, hang, misread or stall) keeps core 1 off and saves 2ND CORE OFF (ChoralRoot's and X0X's rule). A start that failed before core 1 answered only skips core 1 for that one boot (ChoralRoot's rule: no false OFF from an unrelated crash at power-on). SAFE start never starts core 1.
- **Notice:**
  - after the splash, on the home screen until a key is pressed: `2ND CORE WAS SWITCHED OFF: CRASH` (or `HANG`, `READ ERROR`, `TOO SLOW`);
  - the same reason under MENU > SYSTEM > 2ND CORE (`OFF AFTER A CRASH`);
  - the parts' `NOTES` show the one-core numbers.

  The user can switch it ON again; it then starts at the next power-on.
- **Update and flash writes:** core 1 is held before any firmware update and only runs inside the audio interrupt, so it never runs while the flash is off (Felucca's argument). X0X saw update stalls on a misreading unit, which is why the hold comes first.
- **Open decision, the supply levels:** M-VAVE's stock levels (VDC14 7 = 1.60 V, then SYSVDD 15 = 1.38 V, X0X 1.0.9 and Melodee 1.3.6) only while 2ND CORE is ON, never higher (recommended: every FM-1 runs at them under its own firmware). The alternatives are none (Felucca) or ChoralRoot's 1.35 V / 1.50 V. With the second core on by default, this matters more than before: every unit would run at the raised levels. Neither X0X #10 nor ChoralRoot #9 has a confirmed clean result from a unit that misreads.

## 7. DUEL: one send effect a part

**What DUEL is:** part 1 and its effects only on the left, part 2 and its effects only on the right, so the two outputs can feed two external processors. In DUEL each part has one channel, so its reverb is mono on its side. A stereo reverb for a part would put part 1's reverb on the right too, which is no longer DUEL. A stereo reverb belongs to STEREO and LAYER, where it stays as it is.

**One effect a part** (NONE, CHORUS, DELAY or REVERB for each part; the bus then runs only that one):

| DUEL setting | fixed load, device est. % | saving against all three a part | notes a part on two cores (D+), dearest VOICE/WHEEL / FM6 / LOFI |
|---|---|---|---|
| all three a part, typical | 23.9 | | 7 / 8 / 10 |
| all three a part, heavy | 36.2 | | 6 / 7 / 10 |
| one each, worst choice typical (HALL both) | 18.4 | 5.5 | 7 / 9 / 10 |
| one each, worst choice heavy (SHIM DOWN both) | 23.5 | 12.7 | 6 / 8 / 10 |
| one each, the owner's example (part 1 RANDOM delay, part 2 SHIM DOWN) | about 22.4 | about 13.8 | 7 / 8 / 10 |
| no sends in DUEL | about 5 | about 19 to 31 | 8 / 10 / 10 |

**As a default for DUEL:** NONE for both parts (the owner does not use sends in DUEL) gives DUEL the most notes of any mode. One effect a part, chosen on the part's FX page, gives most of the benefit when an effect is wanted. That is a feature decision for the owner. With two cores it is not needed for 8 notes of most engines; on one core it is worth about one note a part.

## 8. In plain words

### 8a. Felucca's "timeout 4 ms"

The owner's reading is right, with two corrections from the code (`hal/fm1_cpu1.h` `fm1_cpu1_wait`, `src/fx.c` `mix_block`).

- **It happens every block, not every half.** Core 0 hands core 1 its parts for each 32-sample block (0.73 ms; there are 4 blocks in a half of 2.9 ms), renders its own parts, then waits for core 1.
- **The 4 ms is counted from the start of that wait.** So a core 1 that stops makes the whole half late long before core 0 gives up.
- **When it gives up**, it stops core 1 for good and **renders those parts itself in that block** (`c1_parts`). It does not leave them silent. The result is one late half (a click or a short dropout), then one core for the rest of the session.

**The fix for DUELL:**

- The wait gets a deadline taken from the half's own clock: the latest moment at which core 0 can still render part 2 itself and finish the half in time. With both cores planned at 80 % at most, that is about 0.2 to 0.3 ms after core 1 should have finished.
- At the deadline: core 1 is held, core 0 renders part 2 for that block, the record is marked, and the one-core `NOTES` take over from the next block.
- **Rendering on core 0 (recommended):** the right sound, and on time whenever the load allowed it.
- **Silence for part 2 in that block:** always on time, but a sudden gap in part 2 of 0.73 ms is a click too.
- So a failure in the middle of play can cost one click at worst, once, never a run of them.
- Core 1 may have advanced part 2's voices half way before it stopped, so that one block of part 2 can be slightly off. It is not repeated.

### 8b. "Melodee-style voice jobs"

Instead of giving core 1 a whole part, core 0 cuts the work into single notes:

1. Core 0 does all the planning for every note (which notes play, their envelopes and modulation values).
2. It puts each note's sound-making on a short list of jobs.
3. Each core takes jobs from the list until it is empty.
4. Core 0 adds the results back together in the original order, then runs the effects.

**Why it balances best:** the pieces are small (one note), so the two cores always end up with nearly the same amount of work. That holds whatever is playing: one part with 10 notes, two unequal parts, a heavy and a light engine.

**Why it is the largest rework:**

- Every engine's note rendering must be able to run on either core at any time. It must not touch anything shared with other notes or the part, such as FM6's shared operator buses, the noise generators, PHYS's strings, or the filter's and the spread's scratch.
- Each engine's per-note work must be split cleanly from its per-part work.
- The list, the merge and its tests must be written.
- Melodee also moved all its audio code into on-chip RAM so the two cores do not fight over the flash cache. DUELL's audio code is about 120 KB, too big for that (section 9).

The voice share (E) is the small version: core 1 takes a whole part, plus a few notes of the other part when that helps.

## 9. Hot RAM

- **The hot block:**
  - In 0.2.24 it is 0x01C7C100..0x01C7FD4C (15,436 B: the flash driver 2,924 B and the hot audio functions), with 20 B left; 200 B after dsp0225.
  - Core 1's waiting loop (about 60 B in Felucca) fits after dsp0225.
  - Core 1's job code runs from the flash at first, as in Felucca 1.6.1.
- **The closed audio graph** (every function the audio interrupt can reach, from the 0.2.24 disassembly) is **337 functions, about 120 KB**, of which 12.4 KB are in RAM now. Running it all from RAM (Melodee, stock firmware) does not fit.
- **Unused RAM in the 0.2.24 link map:**

  | Range | Size | Can call |
  |---|---|---|
  | 0x01C00000..0x01C08000 (the old RAMTEXT) | 32 KB | the flash only through a pointer |
  | 0x01C148F8..0x01C1A000 (after .bss) | about 22 KB | only the first about 80 KB of the image |
  | 0x01C6E6C6..0x01C74000 (the pool's spare end) | about 23 KB | the whole image |

  All three ranges must be checked unused by the boot and update loaders.
- **Partial move:**
  1. the flash driver (reached through a pointer) into the 32 KB block, giving 2.9 KB back in the hot block;
  2. then, chosen by stage 2's readings, the largest audio functions in the flash (`mix_block` 9.2 KB, `rev_hall` 1.6, `fx_buses_m` 0.9, `analog_render` and `_f` 3.9, `split_buses` 1.1) into the pool's end.

  The PC limit has one RAM window, so it must be widened (it would also allow jumps into data and stacks). The gain is unknown until measured.

## 10. Staged plan

### Stage 1: always-safe work on one core (a normal release; host-provable)

1. **The `NOTES n` budget** (section 5) with its enforcement, the per-engine limits, the header and mixer display, the drop messages and the TEST-screen comparison. Estimated table x 1.15 margin. The shared 8-voice pool becomes a per-part limit, with the numbers allowed by the budget.
2. Remove the master SATURATOR (code, page item, saved field, editor, docs).
3. Merge dsp0225's 32-bit EQ.
4. SHIM PITCH UP or DOWN only; PHYS on one part only (the other part's engine list leaves it out).
5. DUEL: HALL's unused side output not computed. If the owner wants it: one send effect a part (section 7), default his choice.
6. The ground work for stage 3, without a sample changing on one core:
   - the matrix and morph values prepared before the parts render;
   - per-part scratch;
   - `eq_live` one byte a part;
   - FM6's operator buses per core.

   The one sound change is DUCK SRC PART keyed from the previous block (0.73 ms); the owner hears a render first.

Host tests:

- goldens bit-identical where nothing should change;
- for every factory multi and bench scene, the prediction under 80 % with the table;
- the steal and drop fades without clicks (render tests);
- the `NOTES` numbers for a list of settings checked against the formula.

### Stage 2: developer timing build (needs the owner's go)

- A bench build with scenes set up in code (no factory multis touched), with per-stage timers on the 24 MHz timer: each engine a voice, each part stage, each bus type, each master stage, the mixer, the hand-over, in us a half (average and worst).
- It also runs the existing core 1 probe (`wip/core1probe`) at the boot loader's levels and, if he agrees, at the stock levels.
- The owner reads the TEST screen (a photo per scene).
- The readings become the cost table: measured worst plus 10 %. They also replace the device estimates in this file.

### Stage 3: second core, on by default, with fallback

- The per-part split (D), then D+ for DUEL. Felucca 1.6.1's `hal/fm1_cpu1.{h,S}` as the base, with these changes:
  - the deadline inside the half (8a);
  - the persistent record and the boot rules (section 6);
  - the supply levels decision;
  - the home-screen notice;
  - DUELL's own split code.
- The `NOTES` numbers switch between the one-core and two-core tables.
- Host tests:
  - the job as an inline call and as a real thread (pthreads, ThreadSanitizer), bit-identical for every golden in all three modes;
  - injected misread, stall, fault, null job and crash: the fallback, the saved OFF, the notice, the `NOTES` drop;
  - `release_test`: the probe absent.
- Unit tests (owner's unit, his go):
  - 30 minutes of play on the stage 2 scenes (MISSED 0, PEAK HOLD under 85 %);
  - 20 power cycles;
  - saves while playing;
  - a firmware update with 2ND CORE ON;
  - on battery and on USB;
  - an hour warm;
  - the fault test build (an injected crash) once, to see the notice and the saved OFF.

### Stage 3b: voice share (E), only if needed

Only if the stage 3 readings show the dearer engines under the owner's target, or if a part playing alone should reach 10. Its extra test: each voice rendered alone into its own buffer, summed, equals the part's render, for every engine.

### What would be built first now

Stage 1 item 1, the `NOTES n` budget, together with items 2 and 3.

## 11. Risks

- **The device factors:** the device figures are estimates (layout alone moves them by up to 15 points). The one-core numbers in stage 1 will therefore be set low on purpose, and the owner may find them smaller than what he plays today; the truth comes with stage 2.
- **Cache contention:** core 1 running engine code from the flash shares the 32 KB instruction cache with core 0. Unknown cost.
- **Bad chips:** with the second core on by default, every unit meets the chip risk. The safety net handles misreads and stalls in play, and crashes after one reboot. A crash loop is prevented by the boot rules. No other firmware has yet confirmed a clean run on a unit that misreads.
- **Stealing becomes audible:** a strict per-part number means notes are stolen sooner on heavy settings. That is the price of no crackle, and the screen says why.
- **The DUCK SRC PART key one block later** is a small sound change.

## 12. Decisions left for the owner

1. Supply levels with 2ND CORE ON (now the default): stock levels (recommended), none, or ChoralRoot's?
2. DUEL: one send effect a part, and the DUEL default (NONE, one each, or all three as now)?
3. DUCK SRC PART keyed from the previous block: acceptable after hearing a render?
4. Where `NOTES n` shows: the part header and the GLO MIXER page, as proposed?
5. Stage 2 developer build on his unit, including a try at the stock levels: yes or no?
6. Stage 3b (voice share): only if the readings ask for it (recommended)?

## Sources

- DUELL:
  - docs: `docs/DUAL_CORE_STUDY.md`, `docs/TESTING.md` "CPU on the unit" (rel0224), `docs/CPU_ANALYSIS.md`;
  - tests: `felucca-base/tests/cpu_bench.c`, `tests/bench_baseline.txt`;
  - firmware: `firmware/src/fx.c`, `fx_bus.c`, `audio.c`, `voice.c` (`track_render`, the VOICE CAP, `voices_busy`), `engines.c` (`ENG_MEM_PART`), `eng_fm6.c` (`FM6_POLY`), `main.c` (watchdog, boot guard), `firmware/app_hot.ld`, `hal/fm1_guard.h`, `tools/hot_fns.py`;
  - the 0.2.24 `build/felucca.elf` and `felucca.dis`; branch `wip/dsp0225` 74a015d.
- X0X: https://github.com/charlesvestal/fm1-x0x (`firmware/src/app/main_fm1.c`), issue https://github.com/charlesvestal/fm1-x0x/issues/10 (closed, no 1.0.9 result from the affected unit)
- Melodee: https://github.com/keremimo/melodee (`docs/AUDIO_PERFORMANCE.md` "shared workload budget", `docs/RELEASE_1.4.0.md`, `docs/RELEASE_1.4.1.md`)
- ChoralRoot: https://github.com/Quixotic7/ChoralRootFM1 (`docs/DUALCORE.md`: the fail-safes, the persistent fallback), issue https://github.com/Quixotic7/ChoralRootFM1/issues/9 (open)
- Felucca 1.6.1: https://github.com/hugelton/Felucca (`firmware/hal/fm1_cpu1.h` `fm1_cpu1_wait`, `firmware/src/fx.c` `mix_split`, `C1_HAND`)
- Nord Modular: https://en.wikipedia.org/wiki/Nord_Modular , https://www.nordkeyboards.com/node/484 , https://www.soundonsound.com/node/4925124 , https://www.norduserforum.com/viewtopic.php?p=118040
