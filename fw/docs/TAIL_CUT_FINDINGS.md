# Tail cut ("like a noise gate") - findings, 0.2.29 (private)

Owner report (real unit, T0226): a released pad on WAVE or TRIO, and then M07 FINAL REEL (#58 BRASS PAD + #54 STRING MACH in a hall, no inserts): at the end of the tail "some of the sound is taken away at once", like a noise gate.

Branch `wip/gate0229` (from `wip/layout0228`, f8046c6 = T0226). All figures were measured on the host: the same fixed-point code as the unit.

## The cause (certain): the HALL reverb's quiet-line rounding

`fx.c hall_end / hall_end_lp` (the HALL's 8 lines, `fx_bus.c rev_hall`): each pass, a line's gain product (about 0.374, Q14) was rounded to the nearest only while the line was "loud". Once two samples of a line added up to less than 32 (`HL_LOUD`, about 16 LSB) it was rounded **towards zero**. The point was that the tail would end in exact silence. But at a few LSB that is a loss of up to 1/6 per pass (0.374 x 15 = 5.6 -> 5), so the tail suddenly falls at about 200 dB/s instead of its natural 15..22 dB/s. This code dates from Felucca 1.2 (upstream), not from a DUELL change. HALL is the power-on reverb and the reverb of M07, M09..M13.

M07 FINAL REEL (LAYER, HALL SIZE 117 DAMP 51), both parts holding a chord for 3 s, then released. MASTER is at the default. `tests/m07_tail_probe.c`, levels in dBFS:

| s after key-off | output before | wet bus before | output after the fix | wet bus after the fix |
|---|---|---|---|---|
| 2.3 (voices end, dry -89.5) | -49.0 | -41.8 | -48.6 | -41.5 |
| 2.8 | -55.0 | -48.0 | -52.9 | -45.8 |
| 3.0 | -60.3 | -52.9 | -55.3 | -47.9 |
| 3.1 | -63.0 | -56.1 | -54.8 | -47.7 |
| 3.2 | -70.7 | -63.4 | -55.7 | -48.4 |
| 3.3 | **-89.8** | -78.7 | -57.7 | -50.1 |
| 3.4 | **silence** | (DC only) | -57.0 | -50.0 |
| 4.0 | silence | | -62.4 | -55.2 |
| 4.5 | silence | | -67.5 | -59.9 |
| 5.0 | silence | | -77.3 | -69.1 |
| 5.3 | silence | | -93.3 | -81.4 |
| 5.4 | silence | | silence | |

Before the fix, the hall fell 30 dB in 0.3 s from about -60 dBFS at the output (-53 on the wet bus): the gate he hears. After it, the tail decays at its own rate for 2 s longer, down to about -80 dBFS at the output, and then ends.

The reverb alone (a 0.3 s noise burst into the send, `tests/hall_tail_test.c`): at the old SIZE 60..127, the worst fall in one 100 ms window above -72 dBFS was 16..29 dB (the gate). After the fix it is 2.5..5.5 dB, which is the natural slope.

The other reverbs were measured the same way and have no early collapse: ROOM (natural to about -94), SPRING (to about -72, then about 0.2 s to silence), SHIM (to about -75; its own soft gate `SH_GATE` sits at -62 dBFS of its halo, by design), PLATE (natural to -93), RESO (short by nature). ROOM and RESO hold a constant offset of a few LSB after the tail. That offset is old and inaudible, because the master DC blocker removes it.

## The fix

`hall_end` and `hall_end_lp` now always round to the nearest. The quiet path and `HL_LOUD` are removed.

- The tail still ends in exact silence. A line of 1 LSB rounds to 0 (0.374 < 0.5), and the diffusers and low-passes still go towards zero. I found no limit cycle in 288 + 126 renders: SIZE 0..150 (SPACE's top), DAMP 0/60/127, noise, 100 Hz, 5 kHz, an impulse, at 0 to -70 dBFS. All were silent within 12 s (SIZE 127/150 from +16 dBFS: within 24 s).
- What still remains is the 16-bit lines' last LSBs: the tail now runs out at about -75..-80 dBFS on the bus (about -81..-86 at the output with MASTER at the default). This is a natural fade, 20 dB below where it stopped before. Removing this too would mean wider lines (no RAM: `rev_comb` is 4937 int16, HALL uses 4888) or block gain-ranging of the lines. That is possible, but it is not a small change.
- Only HALL changes, and only by the rounding of quiet lines (its tails, and the first moments of its build-up from silence). The dry signal and ROOM, SPRING, SHIM, RESO and PLATE are bit for bit the same (the test checks their 0.2.28 hashes). SPLIT's bus B is the same code (`rev_hall_b`).

## The other candidates (checked, not the cause here)

1. **The voice end, `voice.c env_tick`**: the voice stops when the envelope is below 1<<12 of 1<<24 (-72 dB relative to the envelope's top), and its last block fades linearly to 0. Measured on PWM PAD and STRING MACH dry: the dry tail is at -90..-100 dBFS at that moment, well under the reverb. The only effect: a waveform with DC (PWM PAD's 36 % pulse) loses its residual DC there. The master DC blocker turns that into a decaying offset of about 8 LSB (-72 dBFS, sub-audio). Not audible, not changed.
2. **The part going silent, `fx.c mix_part`**: once no voice renders, the part runs its DIST/INSERT/EQ tail for 16 blocks (11.6 ms), or 64 with an INSERT (46 ms), and is then skipped. The sends are not cut: the buses run on by themselves. With no insert there is nothing left to cut. **With TAPE or VINYL**, though, the insert's own noise stops at once 46 ms after the last voice: TAPE hiss -75 dBFS, VINYL NOISE 64 -60.5 dBFS (PWM PAD, MASTER at the default). That is a real gate, but no factory sound uses them and M07 has no insert. Not changed. A fade of the insert noise (about 0.4 s, in the flash code `ins_lofi` + `mix_part`) is a small follow-up if he wants it.
3. **The engines (`eng_wave.c`, `eng_trio.c`)**: no per-voice gate, no threshold, no reset at the end. The "held at a silent sustain" skip (`voice.c`) only applies at SUS 0 while the key is held.
4. **Shedding / LIGHT FX**: the 85 % shed and the 100 % fast shed only act on overloaded halves. The overload guard only engages after two halves over 85 % and only swaps RANDOM/OCT UP for the plain delay. Neither is active at low load, and neither touches the reverb. The filter-bypass edge cross-fade and the SUB/DTN smoothing work on sounding voices only and do not gate.
5. **Delay, chorus, master**: the delay repeats decay evenly to about -100 (no gate). The chorus tail is smooth to -96, and its idle skip only acts when the line is already all zero. The master DC blocker, limiter and soft clip, plus BASS MONO, WIDTH and COMP (1:1 in M07), have no level threshold that could gate.

## Changed goldens / tests

- `tests/multi_render_golden.txt`: 51 of 102 renders, exactly the multis and scenes with HALL (M07, M09..M13, S03..S07, S10, S13, S14, S16, X07, X14).
- `MODE_STEREO_HASH` (the busy STEREO scenario runs HALL): mode, pan, bassmono, comp and master tests, 0xC970174114DFD9BA -> 0xF7CB1C4DE80B2968.
- `tests/locut_test.c`: the HALL's LOCUT-0 reference hashes (A, B).
- `tests/reverb_test.c`: the HALL's RT60 at SIZE 90 is now 3.53 s (the bound was 3.5; now 4.0). The truncation had shortened the measured decay; the design says 4 s. The full-scale SIZE 127 corner and the limit-cycle grid may now ring past 12 s, so they must end in silence within 48 s (they do, by 24 s).
- `tests/perform_test.c`: the delay echo onset threshold is now 1/4 of the echo's peak instead of 1/10. At SPACE +100 the HALL difference between the two renders is no longer rounded away (about 1800 against an echo of about 14000).
- New: `tests/hall_tail_test.c` (host_tests `halltail`). It fails on the old code (12 gate failures) and passes on the new.
- Sound audit: 128 sounds, 0 FAIL.

## Hot RAM, CPU

- `rev_hall` is in the flash (not in hot_fns' set). The device build's elfcheck: hot audio code 12268 B, **244 B free**, unchanged.
- CPU: the reverb bus runs every block whether or not it sounds (it has no idle skip), so a longer tail costs nothing extra. The fix also removes 8 compares per block, so it is slightly cheaper. No voice stays active longer.
