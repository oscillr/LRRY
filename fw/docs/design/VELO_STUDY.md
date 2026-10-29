# Velo study: velocity and polyphonic aftertouch from the FM-1 keys in DUELL (2026-10-19)

Private. Research only: no DUELL code changed, no branch made, nothing built, flashed, committed or pushed. Velo (github.com/charlesvestal/fm1-velocity, Charles Vestal, GPL-3.0-only, built on Felucca) was read on GitHub pages and raw files only: not cloned, not downloaded, nothing of it run. DUELL was read in /Users/balinth/wt/bench1/felucca-base/firmware (the bench1 worktree, base cc95ab3 + bench stage 0).

Marks: **READ** (seen in the code or text today), **ESTIMATE** (worked out, not seen on the unit), **UNKNOWN** (nobody knows yet; says what finds out). Where a fetch tool summarised a Velo file instead of quoting it, I say so; line counts of Velo files are approximate.

## Verdict (half a page)

- **Possible, but not small, and the hard part is the keys, not the code.** Velo shows that the FM-1's 20 lower keys (F3 to C5, notes 0 to 19) can give a usable velocity with no hardware change. That is one author's measurements on his unit, parked on 2026-10-14 with per-key calibration "not right yet" and aftertouch, CHORD and release velocity untested. Taking it means re-writing the scan's analog part for DUELL's own scan code (which has moved a long way from Felucca's: LED glow, breathing, key LEDs), not copying a file.
- **Size (ESTIMATE):** about 10 to 14 firmware files touched, 600 to 900 new or changed firmware lines and 500 to 800 lines of host tests. About 9 to 14 local helper rounds, 1 cloud job, and 5 to 7 sessions on your unit of 15 to 30 minutes each, where your hands press keys. Velocity alone (no aftertouch) is about two thirds of that.
- **Cost on the sound:** one extra converter reading per scan tick (every 100 µs). ESTIMATE 1 to 3 % of one core, on core 0, **and it does not show on the CPU bar**, which measures the audio's own time with the scan taken out. So the NOTES budget needs an explicit scan allowance (section 2). No hot RAM is needed: the scan runs from the flash (cold code), so the 228 B left are not touched. It does add code to the flash path the cache has to keep warm.
- **What stays hard (section 3):** the soft end of the range; per-key differences (hardest landings 389 to 510 counts between keys, Velo's own table); the top 7 keys have no converter; a chord played with several fingers lands at wildly different strengths; the note starts 3 to 5 ms later than today (about 2.2 ms more in the mode that works best).
- **Recommendation:** not now. Do it after the two-core stage 1 gate and the bench suite port, because both change the code and the numbers this feature depends on. Do first a small local step (port the converter reading and a raw logger, one session of your hands to record real curves from your unit), then one cloud job on that data. A cloud job before any recording would tune to Charles Vestal's keys, not yours. Plan B (section 5): velocity only, 20 keys, a fixed velocity for the top 7, no aftertouch.

## Words used here (also suggested for GLOSSARY.md; I did not touch it)

- **ADC (converter):** the chip part that turns a voltage on a pin into a number, here 0 to 1023 (10 bit). There is one, shared by everything that reads a voltage.
- **Key matrix:** the keys are wired as a grid of 11 columns and 6 rows with a diode in each key. The scan lights one column at a time and reads which rows show a closed key. Today a key is only on or off.
- **Rubber dome with a carbon pill:** the key's contact. A carbon pill is a conductive rubber disc; pressed harder, it touches more and its resistance falls.
- **Scan tick:** the interrupt that runs 10,000 times a second (TIMER5, every 100 µs); each tick advances one column. A frame is 11 ticks (about 1.1 ms).
- **Interrupt (ISR):** code the chip runs at once when a timer fires, pausing whatever else runs. The scan tick can also run inside the audio render.
- **Landing, floor, ceiling:** the landing is how low the converter reading is just after the key hits the bottom; the floor is the lowest reading a key ever gives (hardest press); the ceiling is the highest (a light touch). Velo learns both for each key.
- **DRIVE:** for a held key, Velo briefly pushes the row line high from the chip instead of leaving it pulled up, so the reading shows the pill's resistance. Without it the reading is crushed by the diode and a hard and a soft press differ by only about 5 counts.
- **Probe (top row):** the 7 top keys (G5 side, notes 20 to 26) have no converter on their row. Velo reads them through the LED line of that row (pin PA10). Result: weak and saturating.
- **Host test, cloud, bench:** as in GLOSSARY.md.

## 1. What Velo does at the hardware level, and what we would take or re-write

### 1.1 The hardware idea (READ: README, notes/01-lab.md, fm1_input.h, fm1_adc.h, sense.c)

1. The FM-1's key matrix is 11 columns by 6 rows. Rows PA7 and PA8 hold 20 of the 27 note keys (F3 to C5) and also the OCT - and OCT + buttons on PA8. Both rows have a connection to the chip's converter (channels 0 and 1). In DUELL's key map: PA7 holds note ids 23 to 33 (notes 9 to 19), PA8 holds ids 14 to 22 (notes 0 to 8) plus the two octave buttons. PA6 holds the top 7 notes (20 to 26) and some buttons.
2. In the scan tick Velo starts one converter reading per tick on the row of the column just latched: PA7 on one frame, PA8 on the next. So each analog key is read once every two frames, about **2.2 ms**. It takes the result at the next tick, with the column still latched (nothing spins and waits). One slot belongs to the MASTER pot (the OCT - position on the odd frame).
3. Velo's measured converter settings (lab notes): clock divider 7, start delay 2 (a conversion of about 60 µs, fits in the 100 µs tick). Every divider below 7 returned rail values (0 or 1023); delay 4 overran the tick. DUELL's own blocking read uses divider 6, delay 15 (CON word 0xF04E), far too slow for the tick.
4. A key reads about 1023 open. Seated, 170 to 200 counts, varying per key and with how hard it is held. **Without DRIVE** hard, regular and soft presses differ by only about 5 counts and the landing carries no timing information (the dome snaps in one or two samples), so velocity there is the depth just after landing (a hit reads about 185, a gentle press 270 to 360, sinking over 20 to 40 ms).
5. **With DRIVE** (default level 2, one sample in two while a key is held, about one tick in 22 per held key) the driven reading spreads over 100 to 150 counts (softest about 515 to 584, hardest 389 to 448 on one key). Velocity is mapped on a log scale of the pill's resistance between a per-key floor and ceiling: velocity = 1 + 126 x (1 - t)^0.7. Its drawbacks: the note starts about 2.2 ms later, release up to about 4 ms later; the drive current is 2.4 to 24 mA by level.
6. **Aftertouch** is the same driven reading while the key is held: 0 at the landing, 127 at 64 counts below it. Pull-only aftertouch has almost no range.
7. **Top 7 keys:** read through PA10 (the LED line of that row) with the row driven at the weakest strength; rest about 770, contact about 20, light hold 25 to 34, squeeze 40 to 46. It saturates at 44 to 45, so firm presses sound alike. Off by default in Velo, who plays them at a fixed velocity.

### 1.2 What Velo has, file by file (READ; sizes approximate)

| Velo file | Size | What | For DUELL |
|---|---|---|---|
| `firmware/hal/fm1_adc.h` | 45 lines | non-blocking converter: `start(ch, con)`, `done`, `take`, `abort`, CON word with divider and delay | re-write small (DUELL's has only a blocking read) |
| `firmware/hal/fm1_input.h` | about 500 lines in all (Felucca's scan, plus Velo's analog stage) | the scan tick with the converter stage, DRIVE, the PA10 probe, a capacitance probe (closed as useless), self-test | **re-write into DUELL's tick**; DUELL's `fm1_input.h` is 754 lines and has the LED glow, breathing, key LEDs, press-latency stats. Only the analog stage (about 120 to 180 lines in Velo) is wanted |
| `firmware/src/app/sense.c` | about 470 lines, one C file, no hardware calls, needs only `stdint.h` and a fast-math header | the per-key state machine (OPEN, TOUCH, DOWN, LIFT), thresholds, the floor and ceiling learning, velocity and aftertouch mapping, the top-row probe, calibration get and set, an event ring | **the part to port or re-write**; about 2 KB of state for 27 keys (about 72 B a key as written) |
| `firmware/src/app/ui.c` | not read | the lab screen, pages VELOCITY / CONVERTER / EXPERIMENT, MIDI out, SysEx dump of curves | not taken; DUELL gets its own small page |
| `firmware/src/dsp/velo.c` | not read | an 8-voice FM test voice | not needed |
| `tools/velo_log.py`, `velo_notes.py`, `velo_compare.py`, `velo_ctl.py` | not read | host logging of each press and its curve to CSV | idea to copy: our own logger on the bench's CSV tool |
| `tests/host/sense_test.c` | not read | host test with synthetic curves | idea to copy |
| `notes/01-lab.md` | | the measurements | the best part: facts, not code |

Velo's history that touches the scan (READ, 15 commits on GitHub, 2026-10-08 to 2026-10-14): e7b6a7a lab firmware; 97fd4c0 "works on the device", velocity from the landing depth above a learned floor; cd1120d release takes two samples (a landed key can read open for one sample: doubles); 7296688 aftertouch; 9cbcaaa parked, verdict in the README; 7344600 DRIVE (a held key sampled with its row driven reads the pill's resistance), velocity on a log scale; d474ca6 DRIVE 2 default, top row TOP LOW / TOP HIGH; 8d1e63b probe any row through PA10, each press reported both ways; b21a5a3 the probe against the direct reading is the same measurement, neither steadier; 3b6f48b per-key calibration as running quantiles, saved with the settings; 3164efc parked again: the first calibration session produced more extremes, not fewer.

### 1.3 Licence, headers, credit (GPL-3.0 on both sides)

Both are GPL-3.0-only, so code may flow either way with the licence kept. What to do if code (not only ideas) is taken:

- **Headers (READ):** Velo's `fm1_adc.h` and `fm1_input.h` carry "SPDX GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments" (Felucca's). `sense.c` carries only the SPDX line, **no copyright line and no "based on"** (the fetch tool showed no copyright text in its first lines). A taken or derived file keeps the SPDX line and every copyright line it has, and DUELL adds "Portions from fm1-velocity, Copyright (C) 2026 Charles Vestal" (his GitHub name; the repo's LICENSE page and the commit authorship are the evidence; ask him for the wording he wants).
- **NOTICE.md:** one new bullet: "Key velocity and aftertouch: based on Velo (fm1-velocity) by Charles Vestal, https://github.com/charlesvestal/fm1-velocity, GPL-3.0-only; built on Felucca." State what was taken (the converter-in-the-scan-tick method, the drive reading, the velocity mapping; and files, if any) and what was re-written.
- **felucca-base/LICENSING.md:** the third-party list (add one line; not a separate licence text, since it is GPL-3.0-only like DUELL, so `LICENSES/` needs no new file).
- **ABOUT > CREDITS** on the unit (the two-core plan already adds the X0X, Melodee, ChoralRoot credits): one more line.
- **Source files:** the SPDX line plus the "portions from" comment on each file with taken code; a re-written file that only follows the idea gets a comment "after Velo's method" and the same NOTICE bullet (cheap, polite, and the owner's tribute practice with other firmwares).
- **Courtesy (my recommendation, not a legal need):** a short message to Charles Vestal before release; DUELL already credits his X0X.
- **Release rule:** DUELL is one distributed firmware, GPL-3.0-only as a whole; its source stays public at github.com/oscillr/DUELL, as now. Nothing else is needed.

## 2. Where it hooks into DUELL

### 2.1 The hooks (READ: bench1 felucca-base)

| # | Where | Today | Change | Lines (ESTIMATE) |
|---|---|---|---|---|
| 1 | `hal/fm1_adc.h` (45 lines) | blocking `fm1_adc_read(ch)`, CON 0xF04E (divider 6, delay 15), used by `master.c` (the pot, once a UI frame) and `main.c` (battery, **every main loop pass**) | add non-blocking start/done/take and the CON arguments | +30 |
| 2 | `hal/fm1_input.h` `fm1_input_tick` (called from `fm1_timer5_irq` in `main.c`, 10 kHz) | digital scan, LED glow/breath pulses, key debounce (2 frames closed = press, 8 open = release) | the converter stage: take the previous reading, start the next, DRIVE, callback to the sense code; a state record `an` | +120 to 180 |
| 3 | `main.c` and `master.c` | the main loop reads the pot and the battery with the blocking read | **must stop**: Velo's own note says there is one converter and the tick owns it. Move both into tick slots (Velo does this for the pot; for the battery the page I read is silent: UNKNOWN) | 20 to 40 |
| 4 | new `src/sense.c` (or `key_sense.c`) | none | state machine, calibration, mapping | 350 to 470 |
| 5 | `seq.c` `key_on` (line 752 on) and `keyboard_block` (line 875) | every key plays **velocity 100**: `input_on(t, x, 100)` / `lay_input(t, x, 100)` and `midi_out_event(... 100 ...)`; the note-on happens when the debounced key bit rises | note-on waits for the velocity (3 to 5 ms), or fires with the first estimate; velocity passed through `chord_build` notes; MIDI out gets it | 30 to 60 |
| 6 | `voice.c` `trk_note_on`, `mod.c` | velocity already flows: `t->m_vel`, `v->mvel` per voice, matrix source **VELOCITY** (`MS_VEL`, per voice in `mod_voice`), engines (FM6 note init, PHYS accent), the LAYER velocity crossfade (`xf_gain`), step recording | nothing, if the velocity arrives in `trk_note_on` | 0 |
| 7 | `mod.c` `MS_AT` (line 233), `mod_midi` (line 463) | **AFTERTOUCH is per part**: `t->at`, set only from MIDI channel pressure (0xD0). I found **no polyphonic pressure (0xA0) handler** anywhere | true poly aftertouch needs a per-voice value `v->at` and `MS_AT` moved into the per-voice list in `mod_voice` (today only ENV, ENV 2, VEL, KEY, RAND), plus how `mod_begin` sorts sources into per-voice and per-block. The cheap form: channel aftertouch from the strongest held key | 40 to 60 (cheap form 15) |
| 8 | MIDI out (`usb.c`, `midi_uart.c`) | note on/off only | optional: 0xA0 or 0xD0 out | 15 |
| 9 | settings, MENU, one small page | none | on/off, curve, a calibration walk, stored per-key floor and ceiling (about 80 B) | 80 to 250 |
| 10 | arp and sequencer (`seq.c` lines 262, 509, 1530) | their notes play at a fixed 100 | stay as they are (the arp has its own velocity rules), or take the key's | 0 to 20 |
| 11 | tests (`tests/input_test.c`, new `sense_test.c`, goldens), tools | the key debounce model exists in `input_test.c` | extend with a simulated ADC; new fingerprints for velocity scenes | 500 to 800 |

Total (ESTIMATE): **10 to 14 firmware files, 600 to 900 firmware lines**, plus the tests.

### 2.2 RAM, hot RAM, flash and cache

- **Hot RAM (228 B free, 15,436 B block):** READ in the disassembly: `fm1_timer5_irq` and the scan tick live in the **flash** (0x02012A5E), not in the hot set (`tools/hot_fns.py` lists audio functions only). So the scan is **cold code** and Velo's new code needs no hot RAM. Nothing in the 228 B changes.
- **State RAM (.bss):** Velo's state is about 2 KB for 27 keys as written. DUELL needs 20 keys; trimmed to the essentials about 0.6 to 1 KB (ESTIMATE). Today RAM after the two-core stage 1 plans has about 82 KB unused, so it fits; the "pool" (voices) is not touched.
- **Flash and cache:** the tick runs 10,000 times a second from the flash through the shared 32 KB instruction cache. After an audio render the tick's code may have been pushed out, so its first run after the render re-reads the flash. Velo's extra code (about 1 to 2 KB for the sense update and the converter stage) makes that miss longer and the tick more variable. UNKNOWN until measured (the bench suite's tick cost row, below). **If it hurts**, the tick plus sense code (about 3 KB) can go into the unused RAM range Z (28.4 KB, of which 21 KB are planned for stage 2 of the two-core work, about 7 KB spare, direct calls reach the flash); not into the hot block (228 B).
- **Interrupts-off windows:** a flash erase stops interrupts for about 40 ms (CPU_ANALYSIS). Ticks stop meanwhile, an ADC reading in flight is lost, and the state machine sees a 40 ms gap in a 2.2 ms rhythm. The sense code must treat a gap as "unknown", not as a release or a touch (host-testable).
- **LEDs:** PA10 is the LED line of the top row. DUELL's key LEDs (scale display, played keys, glow, breath) use it. The top-row probe turns that LED off during its sample frames and conflicts with the glow pulses. Another reason for plan B's fixed velocity on the top 7.

### 2.3 The converter in the tick: timing risk (READ + ESTIMATE)

DUELL's tick already does shift-register writes, a row read, the LED dim pulse (it times itself against TIMER4), and a debounce frame every 11th tick; it records its own cost and gaps (`cost_sum/max`, `gap_max`, console `inp`). The converter reading is **asynchronous** in Velo (start in one tick, take in the next), so the added cost is writes to about six registers, the take, a call into the sense update, and (for a driven reading) setting a pin direction and strength and restoring it. The sense update is a few compares for an idle key and the log mapping only when a press lands. Details to settle on the unit: the take must come before DUELL's dim pulse changes the shift register; the digital read of a driven row must come after the pull-up recovered; a late tick (interrupt delay) must not leave a row driven.

### 2.4 Two-core plans (TWO_CORE_DESIGN.md)

- **Core 0 scans the keys.** In the design core 0 plays part 1, the shared effects, the master, **events** (MIDI, keys, arp, sequencer), screen and MIDI; core 1 only runs part 2's job in its loop from RAM. The TIMER5 interrupt and the one converter stay on core 0; core 1 never touches pins, so no new sharing appears. The velocity travels from the scan (core 0 interrupt) to `keyboard_block` (core 0 events step), unchanged in kind.
- **The budget moves on core 0, the busier core in STEREO.** Core 0's fixed load (20 to 35 % of a block, measured) gets the scan on top. ESTIMATE of the scan's extra: per half (2.9 ms) there are 29 ticks. If the analog stage costs 1 to 2 µs a tick (not measured; the chip's clock and the compiled size decide), that is 29 to 58 µs per half, **1 to 2 % of core 0**; with DRIVE and the sense updates up to **3 %**. Today's tick cost is itself UNKNOWN to me (the console `inp` shows it; I found no recorded number), so the first measurement is "tick cost with the converter off and on".
- **The CPU bar does not see it.** The bar is the audio interrupt's own time with the nested TIMER5 time subtracted (SPEC, CPU_ANALYSIS), so a heavier scan makes the half later without moving the bar. The plan's 20 % reserve (80 % target) is meant to cover "the key scan and USB nested". An added 1 to 3 % comes out of that reserve, so I would add it to the NOTES cost table's core 0 fixed load (a line "KEY SENSE", about 2 % until measured) rather than let the reserve shrink silently. With the NOTES numbers on screen this costs at most about one note in the worst, heaviest case on core 0; for core 1 nothing.
- **Core 1 stability:** no new code runs on core 1. The sense code is not in the ranges X, Y, Z unless the cache result forces it.

### 2.5 The bench suite (BENCH_SUITE_DESIGN.md)

- **The bench cannot press keys.** Its notes come from an internal generator at velocity 64 and the keys are silenced (`kb_boot_hold`) during a run. So the suite can show only: the scan's cost, the number of converter timeouts, idle readings (all 20 keys near 1023, noise of a few counts, which is itself a useful health check), and that **nothing changed when the feature is off**.
- **Rules for the port:** (1) with the feature off (a flag, then a MENU switch) the package must be byte-identical to the build before, the bench's existing rule; (2) in bench builds the sense stage is off, so null tests (exact comparisons) stay exact; (3) new CSV columns: scan tick cost average and max (µs), converter timeouts, drive counts; the audio average in the CSV does not include the scan, so these are separate columns.
- **New bench rows (small):** KEY SCAN OFF / ON / DRIVE with an idle keyboard, for the cost; M16 STRESS TEST with it on, for the late-block count.
- **Order:** the bench suite port into cloud/base (stage 0 exists on this line) should come first so this feature is measured by the same tool.

## 3. What is unfinished in Velo, and what you would face

### 3.1 The known open items (READ: README and notes)

| Item | State in Velo | What it means for you |
|---|---|---|
| Per-key calibration | built, "not accurate yet": the first real session (585 notes) gave quartiles 37 / 64 / 97, 18 % at 110 or more, 11 % at 127, 11 % below 10, against the uncalibrated round 4 (604 notes) of 40 / 59 / 74, median 59, which he called "remarkably usable" | calibration made it worse. Suspected causes (untested): the large first steps, fast creep, the quantile width, the minimum span 80. We would write our own, tested on simulated and recorded data first |
| Aftertouch | the pull mode is nearly flat (a few to a few tens of counts); the driven mode is built and **untested** (marked in the README); "lightly held notes read as full pressure" without a landing reference | UNKNOWN whether it feels like aftertouch. This needs your hands first, before any code |
| Release velocity | not sent, untested | not offered in DUELL (it has none today) |
| CHORD | a 5 to 80 ms window gives all notes the first one's velocity; untested | for a chord pressed with several fingers the spread was 59 / 76 / 127 / 104 on one chord in session 1; some window like Velo's is needed, and it is a rule, not a sensing result |
| Top 7 keys (notes 20 to 26, G5 side) | no converter; a probe through the LED line saturates at about 45; off by default | fixed velocity, or the probe at the cost of the LEDs on that row. Plan B |
| Soft end | the probe's softest press only about 50 on the scale; in the main keys, pressing near the end of a key reads softer than the centre | some keys will feel less dynamic, some positions differ |
| Latency | 3 to 5 ms touch to note; DRIVE adds about 2.2 ms; release up to 4 ms later | today's key to note is about 2.2 ms (two debounce frames, from the code's own comment; the `inp` press stats on the unit would confirm it). Playing feel will change a little |
| Floor learning | one-way: it only moves down, cannot recover from a too-low value | auto-calibration must be able to move both ways and be reset |
| Aging, temperature, unit-to-unit | not studied (Velo has one unit) | UNKNOWN: your unit's pills may differ from his |
| Drive current | 2.4 to 24 mA by level; the default level's current is not stated | a row driven high against a closed key sinks the current into the column output. UNKNOWN margin for the 595 and the pins at higher levels; I would not go above Velo's default and would ask him |

### 3.2 What a musician testing by ear on the real unit would face

- **What works to start with (Velo's own words and numbers):** playing the 20 lower keys, with DRIVE, gives a spread of velocities with a median around 59 and quartiles around 40 and 74. Nothing is as even as a real velocity keyboard, but it is playable.
- **What he would hear and feel:** (1) the top 7 keys play louder or softer than the others at the same strength, unless all keys use a fixed velocity; (2) a slightly later note than today; (3) some keys always too hot or too soft; (4) chords sounding uneven; (5) aftertouch that is either absent or twitchy.
- **Per-key calibration, split:**
  - **Automatic (no hands needed):** the open level of each key at power-on (the ceiling; about 1023, a noise check), the converter's offset, the diode constant, the timing of the scan (our own tick), gap and bounce handling, the landing statistics learned from normal play (running quantiles with slow creep, in both directions, with a reset), the clamp of impossible values.
  - **Needs your hands:** (1) a calibration walk: press each of the 20 keys a few times soft and a few times hard (about 3 minutes; the unit can light each key's LED in turn and show a counter, as DUELL's UI already lights key LEDs); (2) the feel: curve, sensitivity, DRIVE level, WAIT (the sample count before the velocity is taken); (3) whether aftertouch is worth keeping; (4) whether the top 7 should be fixed, and (5) whether the key latency is acceptable.
  - **Nobody can know without the unit:** the actual curves of your 20 keys.
- **UI rules to apply (your memory notes):** a menu line that does exactly what it says (the label law); no silent controls (the honesty rule: if velocity is off, VELOCITY sources in the matrix should say so or stay visible with a reason); nothing sticky in knobs; any new page proposed to you before it is built; the calibration saved so that it is stable from the alpha on (a separate small store, so a later settings change cannot wipe it). The setting is OFF by default until you say otherwise, so today's behaviour (velocity 100) stays.

## 4. Effort

All numbers ESTIMATE. A "round" is one local helper working session with tests; a "session" is you at the unit.

### 4.1 (a) Host-testable logic

| Piece | What | Rounds |
|---|---|---|
| Sense core | the key state machine (open, touch, down, lift), thresholds, debounce of a one-sample open, gap guard after a 40 ms interrupts-off window, event ring; re-written clean from Velo's method | 1 to 2 |
| Mapping | landing and driven reading to velocity (log curve with the curve exponent), aftertouch from the held reading, smoothing and limits, the chord window | 1 |
| Auto-calibration | running quantiles both ways, reset, floor/ceiling spans, storage format; tested with a simulated key population (spread of floors per key, drift) so the quartile spread comes out stable | 1 to 2 |
| Simulated ADC | a model of the pill, diode and timing: pull-up reading, driven reading, dome snap, bounce, sampling every 2.2 ms with jitter; the existing `tests/input_test.c` already models contacts and the scan, so it extends | 1 |
| Hooks in the notes path | key_on with velocity, the arp/chord/LAYER cases, matrix VELOCITY and AFTERTOUCH sources, per-voice AT (or the cheap form), MIDI out; goldens for a velocity scene | 2 to 3 |
| Menu, settings, calibration walk page | proposal to you first, then build; editor field if wanted | 1 to 2 |
| Total (a) | | **7 to 11 rounds** |

### 4.2 (b) Hardware-dependent parts

| Piece | What only the unit can show | Rounds + sessions |
|---|---|---|
| Converter in DUELL's tick | the CON settings (divider 7, delay 2 on Velo's unit), the timing against the LED dim pulses, the take-before-pulse order, the battery and pot slots, timeouts | 2 rounds, 1 session |
| Raw recording | each key's curve, logged over the cable as Velo's `velo_log.py` does (CSV), 20 keys x soft/regular/hard | 1 round, 1 to 2 sessions |
| Scan cost and cache | the tick's average and maximum with and without; whether the extra code makes the tick or the audio late; the M16 late-block check | 1 round, 1 session |
| Calibration and feel | the walk, the curve, DRIVE level, WAIT, latency | 1 to 2 rounds, 2 sessions |
| Aftertouch | is it usable on the real keys | 1 round, 1 session |
| Top 7 keys | probe or fixed | 0 to 1 round, 1 session |
| Total (b) | | **5 to 8 rounds, 5 to 7 sessions** |

**Total:** 12 to 19 rounds if everything is done, about 9 to 14 for velocity only. The two-core plan, for comparison, is 10 to 14 local rounds and 5 to 7 cloud jobs.

### 4.3 What the cloud could do usefully

The cloud sees only github.com/oscillr/DUELL-core (firmware source, host tests, tools, CPU study documents), has no JieLi SDK, cannot build the device package, cannot touch hardware. Useful there:

1. **The sense core and the mapping as plain C with host tests** (section 4.1 first three rows) on a simulated ADC. This is clean, isolated, deterministic and checkable by the Mac's tests.
2. **A clean port of Velo's scan code behind a flag** (the converter stage in the tick, written against the repo's `input_test.c` model, so that with the flag off the code is byte-identical): code that compiles and passes the host tests; the real converter timing is not testable there.
3. **A test data format and a replay test**: a CSV of ADC samples with timestamps (what the raw logger on the unit will write) played through the sense core, so your recordings become regression tests. The CSV would have to be added to the private repo (a small file; it is not a bench result of the full kind).
4. **The tool for the calibration quartiles** (a script that reports velocity quartiles from a CSV).

Must stay local: the build of the device package; the real converter settings; the LED/tick timing; cost and cache measurements; the calibration on your keys; the feel; the NOTES table entries (measured); the NOTICE/credit text decisions.

### 4.4 Recommendation: cloud job or not, and when

- **Yes, one cloud job, but later:** after (1) the bench suite port into cloud/base, (2) the two-core stage 1 gate (RAM placement and hot RAM numbers settle; a stage 1 build is the test line for the scan cost), and (3) a **first recording from your unit** (a local round that ports the non-blocking converter and a raw logger, and one session where you press 20 keys soft/hard; Velo's tool shows this is practical). Without (3) the job would tune to Charles Vestal's unit.
- **Why not earlier:** the cloud/base line changes `fx.c`, `main.c`, `felucca.c`; the scan layer touches `main.c` and the NOTES cost table. The logic is independent, but its constants are not.
- **Credit:** ESTIMATE 4 to 12 of the credit (the typical figure in REFERENCE_NOTES.md; the credit is counted in US dollars). Do not duplicate the job.
- **The local step before it:** about 2 rounds + 1 session.

Draft brief outline (not the full brief):

1. Goal and scope: key sense core, mapping, auto-calibration, host tests, flag-guarded port of the converter stage. No change to sound code; with the flag off, the build must be identical.
2. Inputs: the repo state (cloud/base + bench); the CSV recordings (path); the numbers and the failure list from this study (sections 1 and 3).
3. Build list: (a) `sense` module (state machine, thresholds as parameters), (b) mapping with curve exponent and limits, (c) auto-calibration with reset, both ways, (d) the gap guard, (e) hooks in `key_on` and the matrix sources behind a build flag, (f) the converter stage in the tick behind a flag, using the repo's `input_test.c` model.
4. Tests: simulated key population; replay of the CSV; quartile gates (median within 50 to 70, no more than 3 % at 110 or more, no more than 5 % below 10, for the simulated hand); gap/bounce cases; flag-off identity; goldens untouched.
5. Out of scope: any UI/page design, the NOTES table, hot RAM, linker, core 1, the bench suite, aftertouch per voice if it needs the sound code to change (the cheap form only).
6. Deliverables: a branch on DUELL-core, a short result note with the counts and open questions; honest reporting of anything the tests cannot show.
7. Hard rules: GPL headers on each file; no code copied from Velo (the brief names the method, the notes quote the measurements); no emoji or invented numbers; a tidy NOTICE bullet draft for the owner to review.

## 5. Risks, unknowns, plan B

### 5.1 Risks

| Risk | Why | How we find out |
|---|---|---|
| Timing of the scan | one more converter reading each tick; a tick that grows past its 100 µs would delay the LED pulse, the debounce frame and nested USB; an interrupts-off window (a flash erase, 40 ms) loses a reading | tick cost and gap stats on the unit (the `inp` stats, a bench row); host tests of the gap guard |
| Flash and cache | the tick code is in the flash; more code means a longer miss after a render; on two cores the cache is shared | tick cost with the feature off vs on, with a render running; if bad, move the tick and sense to RAM range Z |
| Sound quality | the scan runs inside the audio render (nested); a heavy tick makes a half late: a click; the CPU bar does not see it | M16 late-block count; add the scan allowance to the NOTES cost table |
| Key feel | later note-on (3 to 5 ms, plus 2.2 ms with DRIVE); the velocity curve; uneven chords; position on the key | your hands; the calibration walk; a chord window |
| One converter | the pot and the battery are read in the main loop today; two readers at once return nonsense or lock | move both into the tick; test with the knob turned during play |
| Drive current | up to 24 mA at higher levels; the 595 and the pins' margin is not stated | stay at Velo's default; ask him; look at temperature after a long hold |
| Calibration in the alpha | saved values must be stable; a bad calibration stored in flash would persist | a separate small store, a reset gesture (never sticky), default OFF |
| Matrix/AT design | AFTERTOUCH is per part today; poly needs sound-code changes in `mod_voice`; goldens change | start with the cheap form (strongest held key as channel pressure) |
| Licence/credit | `sense.c` has no copyright line | NOTICE bullet, ask Charles Vestal for his wording |
| Velo is parked | no support; his unit only | do not depend on updates |

### 5.2 Unknowns (and what finds them out)

1. Does your unit's keys behave like Velo's (floor, ceiling, spread)? The first recording.
2. The converter settings that work in DUELL's tick with its LED pulses (divider 7 and delay 2 are Velo's).
3. The tick's cost today and with the stage. The `inp` stats.
4. Does driven aftertouch feel like aftertouch? One session.
5. Does the battery read exist in Velo's tick (the page I read said nothing)? Read his `main_fm1.c` and `plat_fm1.c` when a code port starts (not read here).
6. Is a MENU velocity option wanted ON by default? Your decision.
7. What `ui.c`, `velo_log.py` and `sense_test.c` contain exactly (not fetched; only summarised from the README).

### 5.3 Plan B (in order of how much is dropped)

1. **Velocity only, no aftertouch:** drop the driven hold readings after the landing; keep DRIVE only if the velocity needs it. Saves per-voice AT work (about a third of the effort) and drive current exposure.
2. **20 keys, fixed velocity for the top 7:** no probe, no LED conflict. This is also Velo's own default.
3. **Fixed per-key velocity classes** (soft/medium/hard from three or four bands rather than a continuous curve), with the calibration walk giving only the band edges: fewer ways to go wrong, coarser.
4. **A global velocity feel setting** (a curve between flat 100 and the measured range) so a user can reduce it to nearly constant without losing the rest.
5. **Park it**: keep the notes in this study; take it again when Velo's calibration is finished or another firmware solves it (watch fm1-velocity; the repo is parked since 2026-10-14).

## Sources

- Velo: https://github.com/charlesvestal/fm1-velocity (README, commit list, `firmware/hal/fm1_adc.h`, `firmware/hal/fm1_input.h`, `firmware/src/app/sense.c`, `notes/01-lab.md`; read through GitHub pages and raw files; the two big files were summarised by the reading tool, not quoted in full).
- DUELL (bench1 worktree): `firmware/hal/fm1_input.h`, `fm1_adc.h`, `fm1_timer.h`, `firmware/src/main.c`, `master.c`, `seq.c`, `mod.c`, `voice.c`, `midi_control.c`, `build/felucca.dis`, `app_hot.ld`, `tools/hot_fns.py`, `tests/input_test.c`.
- DUELL documents: release_drafts/REFERENCE_NOTES.md (Velo section), GLOSSARY.md, TWO_CORE_DESIGN.md, BENCH_SUITE_DESIGN.md, docs/CPU_PLAN.md, docs/CPU_ANALYSIS.md, NOTICE.md, felucca-base/LICENSING.md.
