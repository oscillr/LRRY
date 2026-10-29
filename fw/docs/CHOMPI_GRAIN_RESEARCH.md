# CHOMPI granular delay against DUELL's delay: research notes (private)

> **Status 2026-10-15 (branch `wip/delaymode`):** section 5's proposal was built as the DELAY MODE (RAND, OCT+; SPEC 5c.2 "Delay mode", `fx_bus.c` `dly_ev`) after the owner's decision.
> Left out: the GRAINS variant (upstream does not ship it) and the lock (needs a gesture). The clock is the delay's TIME (one edge per delay time), not a fixed 1/8 note; the
> pan, the feedback source and the Haas offsets differ from section 1's description (centred voices, the feedback is the centre read, a 10 ms right offset). Cost and size as built: SPEC.


Branch `wip/graindelay` (from `wip/revtest` c383e70). Host experiment only: nothing in the firmware changed, nothing pushed, no package.
The file name ends in `_RESEARCH`, so it stays out of the public export. The name CHOMPI is a trademark: it appears here
(attribution, private) and in `LICENSING.md` (the notice), never in the renders' index or anything shown to users.

Upstream: <https://github.com/CHOMPI-Club/CHOMPI> at the "CHOMPI Open Source Bundle" commit a73d732.
Harness: `felucca-base/tests/graindelay_test.c` (build line in its header). Renders and `report.txt` come out of one run (4 s).

## 1. What upstream has

Only the TEMPO firmware has a granular delay (`firmware/chompi-tempo/code/src/`). TAPE and WAVE have none (grep finds nothing).
Two files, both read-only here:

- `granularDelay.h` (776 lines): the one that is compiled and shipped (`chompi_main.cpp` includes it).
- `granularDelay2.h` (880 lines): an alternative that its own comment says "is not compiled, it is just here if you want to try it
  out instead of the default delay". It adds a real grain engine (12 grains) on the right half of the knob.

### Licence and lineage

- Neither file has a licence header or any third-party notice. They sit under the repository's `LICENSE`: MIT, Copyright (c) 2026
  CHOMPI Club. `THIRD_PARTY.md` says the TAPE 2.0, TEMPO and WAVE firmwares were written at Chase Bliss on top of Electrosmith's
  platform, and releases them under that MIT licence.
- No Mutable Instruments Clouds code is in them (no grain player, no diffusion, no Clouds buffer layout; the grains are a plain
  half-sine window over a read-back). The only outside code they touch is DaisySP's utility functions `fonepole` and `fclamp`
  (Electrosmith, MIT) and their own small `SimpleCrossfade.h`. They also carry the well-known inverse-square-root bit trick
  (`0x5f3759df`) for the pan law; we do not need it.
- The reverb behind the delay is the other lineage (Emilie Gillet's Rings/Clouds reverb, MIT, 2014), already handled in
  `LICENSES/MIT-CHOMPI.txt` since the reverb experiment.
- What I did for the notice: the harness re-implements the behaviour in integer C and copies no line (upstream is float and
  structured differently), but it carries the MIT text for CHOMPI Club in its header anyway. `LICENSES/MIT-CHOMPI.txt` and the
  CHOMPI row of `LICENSING.md` now name the granular delay and the new test file. Copyright lines to keep if code is ever really
  reused: "Copyright (c) 2026 CHOMPI Club" (the granular delay), "Copyright 2014 Emilie Gillet" (the reverb only).

### Algorithm of `granularDelay.h` (the compiled one), in my words

The signal path in `FxEngine.h`: the sampler's wet signal is written into the delay and the delay's return goes into the reverb
(delay then reverb, in series), then a compressor and the dry signal. Everything is float at 48 kHz.

- Buffer: stereo, interleaved float, `kBufferSize` = 480000 frames = 10 s, in SDRAM (about 3.8 MB), and a second 10 s buffer, the
  "frozen" one, for the lock.
- Delay time: tempo-synced. The clock manager gives the beat length (free tempo or MIDI sync); time = beat x 4 x division, with the
  division an index into nine bar fractions `{1/8, 1/6, 1/4, 1/3, 3/8, 1/2, 3/4, 1, 2}` (so 1/8 of a bar = an 8th note, 2 = two bars).
  The time is smoothed with a one-pole (0.001 per sample), so a tempo change slides (a pitch glide). Changing the division jumps the read
  head and crossfades old and new over 256 samples.
- Feedback: the delay's own output times the feedback setting (scaled by 0.975, so at most 0.975) is added to the next input sample
  written. There is no filter in the loop. The compressor after it is raised when the feedback knob is above 0.6.
- Two "voices" (`delayVoice`), alternating. A voice reads the buffer at a read head and has an event:
  NONE (plain delay: the head follows the write head at the delay time), RETRIG (the head sits at 1 1/8 of the delay time, a longer
  echo), REVERSE (the head starts one delay time back and moves backwards, one sample per sample), PITCH_UP (starts one delay time
  back, or two beats back when the delay is shorter than 3/8 of a bar, and moves two samples per sample: an octave up, and it catches
  up with the write head), PITCH_DOWN (half a sample per sample: an octave down). Pitch is plain varispeed, linear interpolation,
  no correction and no window; the only envelope is a sine fade of 1024 samples (21 ms) when a voice starts or ends.
- Each voice is panned equal-power (sqrt law) with its own pan. Both channels are read from the one position but with fixed stereo
  offsets: the left head 960 samples (20 ms) and the right 480 samples (10 ms) behind it, a Haas-like widening.
- Trigger: on each tempo-clock edge of the engine's clock division (every edge, or every second one when that division is
  above 2) a random draw decides whether to start an event: probability = 0.5 x the RANDOMNESS setting (so at most 50 %).
  If yes: the knob side decides what. Left half: one of the four events uniformly. Right half: always PITCH_UP with a random pan,
  range 0.5 .. 1.0 of full width (depending on randomness). If no: when the previous edge had an event, a voice with the plain NONE
  event is started, so the effect is one clock long and then returns to the normal delay; otherwise nothing changes. The newest voice fades
  in while the other fades out.
- Lock (freeze): a short press of the 3rd encoder (under 750 ms) toggles it. Fade out through zero (256 samples), then input is no longer written; the
  frozen buffer (which had been recording the delay's output all along) is played as a loop whose length is the current delay
  division, kept on the grid by counting the 12-ppqn clock pulses (6, 8, 12, 16, 18, 24, 36, 48, 96 pulses for the nine divisions)
  with a 256-sample through-zero crossfade at the wrap. Changing the division while locked crossfades to the new loop phase.
  Option "delay mute": after unlocking the delay is muted for one delay time (a ramp down and up) so the old content does not come back.
- Parameters and ranges. One main knob (0..1) does everything on the left/right axis: 0 .. 0.4 left side = random events, the
  division index = round(knob x 20), so 0 = 1/8 bar and 0.4 = 2 bars; 0.4 .. 0.45 fades the delay in; 0.45 .. 0.55 is off (dead zone);
  0.55 .. 0.6 fades in; 0.6 .. 1.0 right side = octave-up with random pan and the same division mirrored (index round((1 - knob) x 20)); the right
  side also raises the reverb: amount 0.25 + 0.65 x sqrt(position), plus a dry boost of 0.3 into the reverb above 0.6.
  Second knob: dry / wet mix (per engine; 0.5 is full dry + full wet, then the dry falls). Third knob, page 0: RANDOMNESS 0..1.
  Fourth: FEEDBACK 0..1. Freeze: encoder press.

### Algorithm of `granularDelay2.h` (the grain variant, not compiled)

Same file with the lock/mute simplified and one addition: with the knob above 0.55 the output is a cloud instead of the voices.

- 12 grain slots. Every 6 clock pulses (12 per quarter: an 8th note), or every 3 (a 16th) above knob 0.83, one trial.
- Probability per trial: 0.1 at knob 0.55 up to 0.9 at 1.0 (above 0.83: 0.3 up to 0.75). Free slot or no grain.
- Each grain: speed 1, 1/2 or 2 (shares set by RANDOMNESS: at 0 always 1; at 1 half the grains are 1, a quarter 1/2, a quarter 2);
  size 1 s at the left of the range to 0.25 s at the right, plus or minus 20 % (halved for the half-speed grains); start point
  random between size x speed and 1 s (2 s for the octave-up) behind the write head; a half-sine amplitude window
  (256-point table) over the grain, times 0.75; random pan; the window advances with the read position, so the ends are in source samples.
- Grains read the same buffer that is written (feedback = output x 0.6 x the feedback setting, back into the input).
- No stereo offsets, no voices, no lock-through-zero (it uses a plain lock). The left half still runs the voices.

## 2. How it differs from our delay

| | DUELL delay (fx_bus.c) | the random-event delay | the grain variant |
|---|---|---|---|
| Time | tempo division, 1/4 .. 4BAR and tuplets (G_DTIME) | tempo division, 1/8 .. 2 bars of a bar | fixed 1 s read-back window, tempo only sets the grain clock |
| Repeats | exact copies, low-passed (G_DCOLOR), feedback G_DFDBK | exact copies until a random event bends the next one; no filter in the loop | no echo of its own: a cloud of slices from the last 1 s |
| Stereo | PING-PONG alternates left / right, SPREAD | Haas offsets 20 / 10 ms plus random pan on the octave-up events | random pan per grain |
| Movement | optional time wobble +-10 ms (G_DMDEP, G_DMRATE) | reverse, +-1 octave, retrigger on a random share of clock edges | random speed 1 / 1/2 / 2 per grain |
| Freeze | none | lock (loop of one delay time) | lock |
| Feedback with events | n/a | octave-up fed back climbs an octave per pass: bright and wild at high FDBK | 0.6 of the setting |

Chorus: ours is a separate 5 to 15 ms modulated delay (cho_buf, 2048 samples); the nearest thing upstream is the 10 / 20 ms offset pair,
which is fixed, not modulated. Our wobble (G_DMDEP) is the closest relative of a modulated delay and stays a different thing from random events.
Reverbs: ours run in parallel from their own send. Upstream puts the delay in series before its reverb (delay output + a dry boost into the
reverb). The "+plate" renders feed the delay's mid into the integer plate to show that routing; in DUELL that would be one extra add into `rev_in`.

## 3. The renders

Index: `scratchpad/graindelay/index.html`, served on port 8851. Four sources through the real engines and the real delay send:
pluck = ANALOG / PLUCK (chords C, Am, F, two bars); pad = ANALOG / SOFT PAD; chord = WAVE / HARM KEYS (Cmaj7 then Am7);
beat = PHYS / HAND DRUM + NOISE / METAL hats + ANALOG / PLUCK stabs (the hidden DRUM engine is not used, as the owner asked).
Three settings (light, medium, heavy: FDBK 40 / 60 / 100 and randomness 0.3 / 0.6 / 1.0) x five models:
PINGPONG (the real fx_buses code), PINGPONG_MOD (the same with DEPTH 60), EVENTS_RND (left half), EVENTS_UP (right half),
GRAINS; plus EVENTS_LOCK on medium; and all of it again with the integer plate reverb after it. Wet level matched to the ping-pong
of the same column (the raw difference is in `report.txt`). The random draws use a fixed seed: a file always sounds the same.

I could not listen. What I checked: levels (no clip in any file, peaks 13 to 19 k of 32767), that at the light setting the event delay's wet energy is within 1.5 dB
of the ping-pong's (its plain NONE voice is a plain delay), that grains and events appear in the right numbers (per-second RMS), and that the plate does not clip.
The sound judgement is the owner's.

Things that behave differently from what you might expect when listening:
- GRAINS is sparse by design: with a short pluck or hit it only makes an echo when a grain happens to start over the sound; the
  tail is short because grains feed back only 0.6 of the setting. On pads and the beat it sounds like what it is.
- The first events of each file arrive after the first clock edge (a 1/8 note).
- Upstream records stereo; ours takes the mono delay send, so the pan of one event does not carry into its repeats (the feedback is
  the mid).
- The lock here loops the line as it is (feedback included); upstream loops the separate frozen buffer of the already bent output.

## 4. Cost

Host instructions per sample (repo model: 1.7 % CPU per 100 instr/sample, +-30 %), 44.1 kHz, noise in, from `report.txt`.

| | instr/sample | CPU (device est.) | note |
|---|---|---|---|
| our PING-PONG (fx_buses minus ROOM alone) | 83 | 1.4 % | includes the chorus bus running idle (not separated) |
| our delay, plain | 68 | 1.2 % | same |
| our PING-PONG + modulation | 108 | 1.8 % | |
| random-event delay (EVENTS) | 177 light, 195 medium, 198 heavy | 3.0 to 3.4 % | whole delay incl. feedback LP, no chorus |
| grain variant | 162 light, 161 medium, 180 heavy | 2.7 to 3.1 % | 12 slots scanned each sample |
| integer plate reverb after it | 397 | 6.8 % | (full rate; the half-rate and small versions of the earlier experiment: 179) |

Net change when it replaces PING-PONG: about +95 to +115 instr/sample, about +1.6 to 2 % CPU (the chorus bus stays and is inside our 83, so the real
increase is between +80 and +125). The cost is state dependent: little when the voices idle on NONE, the most with two voices fading.

Memory (the pi32v2 -Os compile of the engine only, measured with the JieLi clang in Docker):
- Code: gd_run 1702 B (it inlines the voices' start/stop, the clock edge and the grain trigger), voice read 264 B, pan 72 B, isqrt 42 B,
  reset 106 B: about 2.2 KB. Constant tables 1028 B (two 257-entry int16 sine tables; one table would do: 514 B). One call pulls the 64-bit
  division (`__udivdi3`) for a grain's window step; it can be made a 32-bit one. Flash: about 2.3 to 3.3 KB.
- RAM: state 468 B (`gd_t`; 2 x 12 grain slots would not change it, they are in it). Buffer: **0 new bytes if it borrows `dly_buf`**
  (65536 int16 = 128 KiB = 1.49 s at 44.1 kHz, already in the pool). Needed: the same clear-on-mode-change the delay already does.
- If a separate buffer had to be bought out of the 9 KB left after the 8 KiB spare: 4 500 samples = 0.1 s at 44.1 kHz or 0.2 s at 22.05 kHz
  (8.8 KB). Too short for a tempo delay (1/8 at 100 BPM is 0.3 s), so borrowing is the way, and nothing on the 22 kHz option was rendered.

## 5. How it could live in the firmware (a proposal, not implemented)

- A new DELAY MODE on the existing delay pages, as a new stored global (append-only id) "MODE": DELAY (as now, with PING and the modulation),
  RANDOM, OCT UP, GRAINS. It lives next to PING-PONG, STEREO and LAYER only (as PING-PONG: SPLIT shares the line in halves; either fade it in SPLIT with
  the reason, or run it on a half line, 0.74 s, at the cost of a second state, 468 B).
- The old path stays bit for bit: the new mode is its own `noinline` function called from `fx_buses` the way `dly_mod` is today; its return
  goes out as the mid in `wet` and the side in `dly_sd`, which is exactly the PING-PONG interface, so the mix, SPREAD and the reverbs need no change.
- Knobs on DELAY 1/2 keep their printed meanings: TIME (the division, as now; the event clock is a fixed 1/8 note), FDBK, COLR (the low-pass in the loop,
  which also tames the octave-up climb), MIX. On DELAY 2/2, by the label law and the honesty rule: PING fades with "not in this mode"; SPREAD keeps its meaning
  (the width of the random pan); RATE fades in these modes (no wobble); DEPTH is replaced by a label that says what it does, CHANCE (0 to 50 % of the clock edges,
  upstream's randomness; mode GRAINS: the grain density). Global and part controls never share a page, so the new MODE sits on the delay page with the
  globals, not with a part.
- The lock needs a gesture (a hold key); a decision for the owner, left out of the first step.
- Delay-into-reverb (series), as upstream does: one add into `rev_in`; an option on the reverb page, not needed for the mode.
- Fit: about 3.3 KB flash and 0.5 KB RAM, +1.6 to 2 % CPU while the mode is on; the plate would add 6.8 %, so not that one.

## 6. Shimmer variants

The owner wants a later octave-down switch and modulation on SHIM. Our SHIM already has the right machine (fx_bus.c rev_shimmer: two
crossfaded grains reading `BUS_SHIM.buf` at twice the speed it is written, i.e. an octave up). Octave down is the same grains at half the
read step (0.5 instead of 2.0, with the window length and spacing set for it): a parameter, not new code. Upstream's voice engine
shows the same two speeds (2.0 and 0.5) but it is not a continuous shifter (voices live for a whole clock period, seconds, and the
octave-up read runs into the write head), so it is not reusable for SHIM. Its grain variant (half-sine window, speed 1 / 1/2 / 2) is
the same idea as ours. Nothing to take for SHIM; modulation (a slow wander of the read step or of the grain spacing) would be built on ours.

## 7. Recommendation

Worth a trial as one new DELAY MODE; the random-event part (RANDOM and OCT UP) is the one worth hearing first, the grain variant second.
Reasons: the cost is small (3.3 KB flash, 0.5 KB RAM, no new buffer, +1.6 to 2 % CPU), the old delay is untouched, and the result is something our delay does not
do (reverse, octave up and down, retrigger on the clock, random pan, a lock). The risks: the octave-up fed back climbs fast at high FDBK (COLR helps),
a mono send means less width than the original, and the lock needs a UI gesture. The upstream author left the grain variant out of the shipped build;
it is sparse on short sounds. I have not heard any of it: listen to pluck medium (PINGPONG, EVENTS_RND, GRAINS), pad heavy (GRAINS), beat medium (EVENTS_UP, EVENTS_RND) and the +plate section first,
then decide.
