# L/RRY 0.1.0 beta: pages and controls

Every page of the unit, by the button that opens it. Each page has four cards, one per knob (K1 to K4). Pages marked *(hidden)* appear only when they can do something.

## The sound path

```
OSC 1, OSC 2, NOISE, RING ──FILTER BALANCE──► MULTI FILTER ─┐
                                    └─────────► TWIN FILTER ─┴─ SERIES or PARALLEL ─► DIRT A, DIRT B ─► MID / SIDE ─► AMP ─► sends ─► STEREO MASTER
SUB OSC ───────────────────────────────────────► MULTI FILTER
```

Every source and every stage has its own pan. The voice is stereo from the first stage to the last.

## EDIT: the sources

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| **OSC 1** | MODEL | model control 1 | model control 2 | model control 3 |
| **OSC 2** | MODEL | model control 1 | model control 2 | model control 3 |
| **TUNING** | OSC 1 COARSE | OSC 1 FINE | OSC 2 COARSE | OSC 2 FINE |
| **OSC MIXER** | OSC 1 LEVEL | OSC 2 LEVEL | OSC 1 PAN | OSC 2 PAN |
| **SUB OSC** | WAVE (SINE, TRI, SQUARE, PULSE) | OCTAVE (-2, -1, 0) | LEVEL | PAN |
| **NOISE** | TYPE (WHITE, PINK, BROWN) | | | |
| **SOURCE MIXER** | NOISE LEVEL | RING LEVEL | NOISE PAN | RING PAN |
| **VOICE 1/2** | MODE (POLY, MONO, LEGATO) | PRIORITY | | TRANSPOSE |
| **VOICE 2/2** | GLIDE MODE | GLIDE | TRIM | |

### The oscillator models

| MODEL | Control 1 | Control 2 | Control 3 |
|---|---|---|---|
| SAW, TRI, SINE | | | |
| PULSE | width, 5 to 95 % | | |
| SYNC | slave ratio, 1 to 8 | | |
| SUPER | detune, 0 to 100 cents | saws, 1 to 7 | spread of the outer saws |
| FM | ratio, 0.5 to 8 | index | |
| WAVEFOLD | fold amount | | |

RING is OSC 1 x OSC 2.

## FX: filters, dirt and sends

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| **MULTI FILTER 1/2** | CUTOFF | RESONANCE | MODE (LP, BP, HP, NOTCH) | SLOPE (12 or 24 dB) |
| **MULTI FILTER 2/2** | KEY TRACKING | ENV 2 depth | LFO 1 depth | PAN |
| **TWIN FILTER** | TYPE (OFF, LP, BP, HP, NOTCH, LADDER, COMB) | LINK | CUTOFF (with LINK: offset in semitones from the MULTI FILTER; on COMB: tune) | RESONANCE (on COMB: feedback) |
| **TWIN MOD** *(hidden with TWIN OFF)* | KEY TRACKING | ENV 2 depth | LFO 1 depth | LFO 2 depth |
| **TWIN CHAIN** *(hidden with TWIN OFF)* | ROUTE (SERIES, PARALLEL) | BLEND | PAN | |
| **DIRT A** *(hidden with TYPE OFF)* | TYPE (OFF, SOFT, HARD, FOLD, FUZZ, CRUSH) | DRIVE (on CRUSH: bit depth) | LEVEL | PAN |
| **DIRT B 1/2** | TYPE | DRIVE | LEVEL | ROUTE (PARALLEL, SERIES) |
| **DIRT B 2/2** | PAN | | | |
| **FX** *(hidden with SENDS OFF)* | CHORUS send | DELAY send | REVERB send | |

## ENV: envelopes

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| **AMP ENV** | ATTACK | DECAY | SUSTAIN | RELEASE |
| **AMP DEST** | PITCH | TIMBRE | DIRT B DRIVE | SYNC |
| **ENV 2** | ATTACK | DECAY | SUSTAIN | RELEASE |
| **ENV 2 DEST** | MULTI CUTOFF | FILTER BLEND | DIRT A DRIVE | TWIN RESONANCE |
| **CYCLE ENV 1/2** | MODE (LOOP, ONCE) | SYNC | RISE | FALL |
| **CYCLE ENV 2/2** | CURVE | TWIN CUTOFF | PAN | PITCH |
| **MATRIX 1-4**, **MATRIX 5-8** | slot | SOURCE | DESTINATION | AMOUNT |

## LFO

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| **LFO 1** | WAVE (SINE, TRI, SAW, SQUARE, S&H) | RATE | SYNC (OFF, 4 bars to 1/32, triplets, dotted) | FADE |
| **LFO 1 TRIG** | TRIGGER (NOTE, FREE) | PHASE | POLARITY | LEVEL |
| **LFO 1 DEST** | PITCH | MULTI CUTOFF | TIMBRE | AMP |
| **LFO 2**, **LFO 2 TRIG** | as LFO 1 | | | |
| **LFO 2 DEST** | TWIN CUTOFF | TWIN RESONANCE | MULTI FILTER PAN | FILTER BLEND |
| **MATRIX 1-4**, **MATRIX 5-8** | the same matrix as under ENV | | | |

### The matrix
8 slots. **Sources:** LFO 1, LFO 2, AMP ENV, ENV 2, CYCLE, VELOCITY, KEY, RANDOM, S&H, SLEW, MORPH, MOD WHEEL, AFTERTOUCH, EXPRESSION, BREATH, FOOT. **Destinations** include pitch, both cutoffs and resonances, the level, pan and filter balance of every source, every stage pan, the three controls of both models, COARSE and FINE of both oscillators, both DIRT drives and levels, the envelope times, the LFO rates and depths, SUB LEVEL and TRIM.

## SCL: scales and chords

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| **SCALE** | the scale on the keys | | | |
| **CHORD** | CHORD (OFF, DIA3, DIA7, MAJ, MIN, DOM7, MAJ7, MIN7, SUS4, POW) | VOICING (CLOSE, OPEN, INV1, INV2, +OCT) | STRUM | |

## ARP

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| **ARP 1/3** | MODE (16 patterns) | HOLD | ORDER | OCTAVES |
| **ARP 2/3** | RATE | GATE | SWING | PROBABILITY |
| **ARP 3/3** | TARGET | | | |

## SEQ

**STEP** (the piano roll of the 64 steps), **SEQUENCER HELP** (the symbols in words), **PARAMETER LOCKS** (every lock, chance and ratchet as one list).

## HOME: MORPH

| Page | What it does |
|---|---|
| **MORPH** | The fader from End A to End B, ROLL, UNDO and AMOUNT. |
| **MORPH SET** | What the morph moves and how the roll works. |

The ALGORITHM knob moves the morph on every page. MENU > CONTROL > ALGORITHM KNOB can give it MORPH AMOUNT or any matrix destination instead.

## GLO: mixer, effects and settings

| Page | K1 | K2 | K3 | K4 |
|---|---|---|---|---|
| **MIXER 1/3** | OSC 1 LEVEL | OSC 2 LEVEL | OSC 1 PAN | OSC 2 PAN |
| **MIXER 2/3** | SUB LEVEL | NOISE LEVEL | SUB PAN | NOISE PAN |
| **MIXER 3/3** | RING LEVEL | | RING PAN | |
| **FILTER BALANCE** | OSC 1 | OSC 2 | NOISE | RING |
| **STAGE PAN** | MULTI FILTER | TWIN FILTER | DIRT A | DIRT B |
| **CHORUS** | RATE | DEPTH | | |
| **DELAY 1/2** | MODE (DIGITAL, PING-PONG, RANDOM, OCTAVE UP) | TIME | FEEDBACK | COLOUR |
| **DELAY 2/2** | SPREAD | MOD RATE | MOD DEPTH (CHANCE in RANDOM and OCTAVE UP) | MIX |
| **REVERB** | TYPE (ROOM, SPRING, HALL, SHIMMER, RESONATOR, PLATE) | SIZE | DAMPING | LOW CUT |
| **SHIMMER** *(only with SHIMMER)* | PITCH (UP, DOWN, BOTH) | MOD | | |
| **STEREO MASTER** | WIDTH | BASS MONO | | |
| **TEMPO** | CLOCK (INT, USB, TRS, AUTO) | TEMPO | TAP | SWING |
| **MODULES** | ARP | SEQ | SENDS | |
| **MIDI** | channel and MIDI input | | | |
| **MIDI LEARN** | LEARN | CLEAR | | |

The mixer, balance and stage pan pages hold the patch's own values (the same values as on the EDIT and FX pages), so the morph moves them.

## SAVE

**PATCHES**: one list of the 128 patches (ours and your own), SAVE opens the save window. **PATTERNS**: load and save sequencer patterns. **TOOLS**: clear the sequence, start from an init sound.

## Voices

Both cores play voices; every new note goes to the freer core. The number on the HOME screen is always what really plays. When more keys are held than it shows, a voice fades out instead of crackling. Effects cost voices; GLO > MODULES > SENDS OFF gives them back.

## Effects and their cost

The chorus, delay and reverb are shared by the whole instrument; a patch sets how much it sends to each on the FX page. The delay's RANDOM and OCTAVE UP modes cost the most.
