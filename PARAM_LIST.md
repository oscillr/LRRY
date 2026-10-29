# L/RRY voice parameters (phase 4.3a): the plain list for page layout

Status 2026-10-29. Nothing here is a page: it is the table the pages will be laid out from. Ids are in `fw/felucca-base/firmware/src` (`ext.h` slot names, `params.c` `XTP[]` descriptors, `lrry_param.c` the decode). The test `tests/lrry_fw_test.c` plays every one of them.

Reading the tables. "Stored" is what `track_t.p[]` holds (every value is inside -64..+127, the range every store and the editor protocol use). "Format" is DUELL's `param_desc_t.fmt` (how a card shows it). Labels are the short card labels in `XTP[]`; several repeat (PAN, LEVEL ...), so a page card needs its group word; the wording is yours.

## 1. New parameters (36 slots, ext.h X28 .. X63, wide ids 156 .. 191)

| Slot | Group | Name | Stored range | Default | Format | Meaning |
|---|---|---|---|---|---|---|
| X28 | OSC 1 | MODEL | 0..7 | SAW | enum | SAW TRI SINE PULSE SYNC SUPER FM FOLD |
| X29 | OSC 1 | COARSE | -24..+24 | 0 | semitones | pitch offset |
| X30 | OSC 1 | FINE | -50..+50 | 0 | cents | exact (2^x table) |
| X31 | OSC 1 | LEVEL | 0..127 | 64 | percent | source level (127 = 1.0) |
| X32 | OSC 1 | PAN | -64..+63 | 0 | bipolar | -63 and +63 are hard left / right (the width preserving law: unity at the centre, +3 dB on the panned side) |
| X33 | OSC 1 | BAL | -64..+63 | 0 | bipolar | A / B filter balance of this source (negative: toward A, positive: toward B; in SERIES positive sends part of it past filter A) |
| X34 | OSC 1 | P1 | 0..127 | 64 | percent | by MODEL, see section 5 |
| X35 | OSC 1 | P2 | 0..127 | 64 | percent | by MODEL |
| X36 | OSC 1 | P3 | 0..127 | 64 | percent | by MODEL |
| X37..X45 | OSC 2 | MODEL COARSE FINE LEVEL PAN BAL P1 P2 P3 | as OSC 1 | LEVEL 0, the rest as OSC 1 | | the same nine |
| X46 | NOISE | LEVEL | 0..127 | 0 | percent | white noise |
| X47 | NOISE | PAN | -64..+63 | 0 | bipolar | |
| X48 | NOISE | BAL | -64..+63 | 0 | bipolar | |
| X49 | RING | LEVEL | 0..127 | 0 | percent | OSC 1 x OSC 2 |
| X50 | RING | PAN | -64..+63 | 0 | bipolar | |
| X51 | RING | BAL | -64..+63 | 0 | bipolar | |
| X52 | FILTER A | PAN | -64..+63 | 0 | bipolar | pans the whole unit's output (L and R keep their own filter states) |
| X53 | FILTER B | PAN | -64..+63 | 0 | bipolar | |
| X54 | DIRT A | TYPE | 0..5 | OFF | enum | OFF SOFT HARD FOLD FUZZ CRUSH |
| X55 | DIRT A | PAN | -64..+63 | 0 | bipolar | |
| X56 | DIRT A | LEVEL | 0..127 | 127 | percent | |
| X57 | DIRT B | TYPE | 0..5 | OFF | enum | as DIRT A |
| X58 | DIRT B | DRIVE | 0..127 | 0 | percent | gain 1 .. 16 (on CRUSH: the bit depth, 24 down to 1) |
| X59 | DIRT B | PAN | -64..+63 | 0 | bipolar | |
| X60 | DIRT B | LEVEL | 0..127 | 127 | percent | |
| X61 | DIRT | ROUTE | 0..1 | PARAL | enum | PARAL: each filter branch to its own dirt unit and summed; SERIES: one signal through A then B |
| X62 | OUTPUT | MID | 0..127 | 64 | percent | 64 = unity (0 = no mid, 127 = x 2) |
| X63 | OUTPUT | SIDE | 0..127 | 64 | percent | 64 = unity |

## 2. Existing ids the voice keeps, with what they do now (22)

| Id | Group | Name | Stored range | Default | Meaning in the voice |
|---|---|---|---|---|---|
| P_FREEA | FILTER A | CUT | 0..82 | 82 (open) | dial of DUELL's multimode filter, 30 Hz .. 16 kHz; plus every cutoff route (LFO, FILTER ENV, matrix CUT, CYCLE) and KEY TRK |
| P_FREEB | FILTER A | RES | 0..127 | 0 | damping 2.0 (Q 0.5) down to 0.05 (Q 20), 128 table steps |
| P_FREEC | FILTER A | MODE | 0..3 | LP | **was DUELL's 0..127 blend; now a choice**: LP BP HP NOTCH. LP with CUT open = filter A is not run (DUELL's bypass rule) |
| P_FREED | FILTER A | SLOPE | 1..2 | 1 | 12 dB or 24 dB |
| P_KTRK | FILTER A | KEY TRK | -63..+63 | 32 | cutoff follows the note around C3 (default 32 is DUELL's; with the filter open it closes low notes, so filter A runs and costs CPU on every patch: see open choice 5) |
| P_TWTYPE | FILTER B | TYPE | 0..6 | OFF | OFF LP BP HP NOTCH LADDER COMB (the host's unit B) |
| P_CUT2 | FILTER B | CUTOFF | 0..82 / offset / tune | 0 | LINK off: own cutoff dial; LINK on: OFFSET in semitones from filter A's cutoff (+-36); on COMB: TUNE in semitones (+-24) |
| P_RES2 | FILTER B | RES | 0..127 / -64..+63 | 0 | RES for SVF and LADDER; FDBK on COMB (-0.95 .. +0.95) |
| X_LINK | FILTER B | LINK | OFF / ON | ON | as above |
| X_KTRK2 | FILTER B | KEY TRK 2 | -63..+63 | 0 | |
| X_ENV2, X_LFO1, X_LFO2 | FILTER B | ENV 2 / LFO 1 / LFO 2 to CUTOFF 2 | -64..+63 | 0 | three depth cards |
| X_ROUTE | FILTERS | ROUTE | SERIES / PARAL | SERIES | the host's f_serial |
| P_DRV2 | DIRT A | DRIVE | 0..127 | 0 | gain 1 .. 16 (on CRUSH: bit depth) |
| P_BLEND | SOURCES | BLEND | -64..+63 | 0 | **new meaning, same job**: added to every source's A / B balance (the matrix routes FILTER BLEND, FILTER ENV and LFO 2 to BLEND live again) |
| X_SUBLVL | SUB | LEVEL | 0..127 | 0 | the SUB LAYER, a sine, centre, mono, through the filters and dirt |
| X_SUBOCT | SUB | OCTAVE | -2 / -1 / 0 | -1 | |
| P_ATK P_DEC P_SUS P_REL | AMP ENV | ATK DEC SUS REL | 0..127 | 10 70 90 60 | DUELL's envelope, DUELL's velocity and LFO to AMP: the amp ramp of the voice |

DUELL's own, unchanged and still acting on the part: P_LEVEL (mixer), P_TRIM, P_PAN (the part's balance), P_MUTE, ENV 2 (4), LFO 1 and 2 (RATE WAVE PHS FADE SYNC TRIG POL, LEVEL, the PIT and FLT depth cards), CYCLE (X10..X14, X19..X21), the 8-slot matrix (P_M1..P_M8), glide, voice mode (POLY MONO LEGATO UNISON), DETUNE, TRANS, the arp, the steps, chords (STRUM).

## 3. No reader (the voice does not use them; they stay in the tables, nothing was renumbered)

- X_SUBWAVE, X_SUBHARM: the SUB is a sine (the host's design); wave and harmonics would be a second step.
- P_E0 .. P_E7 (the engine's slots) and the engine choice: no engine plays. P_ED_SHP, P_LD_SHP, P_E2SHP, matrix SHP (TIMBRE): nothing to drive (option: OSC 1 P1).
- P_DIST (SAT), P_ITYPE and its A B C MIX, the EQ, DUCK, P_CHOR P_DLY P_REV (sends), SPREAD (P_SPRD): the effects are OFF; they are mono stages and none runs on the stereo block.
- Not in the table at all (no slot left, ext.h): RING SOURCE (fixed OSC 1 x OSC 2), FILTER A LEVEL and FILTER B LEVEL (fixed 1.0), the BITS of the two dirt units (CRUSH's DRIVE is its bit depth). See the open choices in PHASE43A_REPORT.md.

## 4. Matrix destinations added (MD_N is now 64, the format's limit)

In `mod.c`, per block like PAN (the matrix writes the extension slot, the decode reads it): 

| No. | Name (N_MDST) | Moves | Matrix list position now |
|---|---|---|---|
| 57 | O1PAN | OSC 1 PAN | end of the DESTINATION knob, category AMP |
| 58 | O2PAN | OSC 2 PAN | same |
| 59 | APAN | FILTER A PAN | same |
| 60 | BPAN | FILTER B PAN | same |
| 61 | MID | MID | same |
| 62 | SIDE | SIDE | same |
| 63 | BDRV | DIRT B DRIVE | end, category FILTER |

Already existing and now live on the voice: PITCH, CUT (filter A), RES, FMODE (filter A MODE), CUT2, RES2, DRIVE (dirt A), BLEND, SUBLV, PAN (the part), AMP, TRIM, the AMP ENV times, ENV 2 times, LFO rates.

Wanted by the owner's goals but **not added** (no number left: the destination byte of the motion records and `lk_virt` is 64 wide, and the 57 older ones are stored): pan of NOISE, RING, DIRT A, DIRT B; LEVEL of OSC 1, OSC 2, NOISE, RING, DIRT A, DIRT B; BAL of the four sources; FILTER A / B level (no slot). The owner decides which of these should replace destinations that no longer mean anything (CHO, DLY, REV, INS A/B/C, IMIX, EQLO, EQMID, EQHI, DUCK, SHP, DEP1, DEP2, the bus values 39..45 and SPACE 56 have no sound without the effects) or whether to widen the format (core.h MOTION_VBASE, a different byte layout: patterns and stores change).

## 5. What P1, P2, P3 mean by MODEL (lrry_param.c decode)

| MODEL | P1 | P2 | P3 |
|---|---|---|---|
| SAW, TRI, SINE | unused | unused | unused |
| PULSE | width 5 % .. 95 % | unused | unused |
| SYNC | slave ratio 1 .. 8 | unused | unused |
| SUPER | detune 0 .. 100 cents | saws 1 .. 7 | spread 0 .. 100 % (pan of the outer saws) |
| FM | ratio 0.5 .. 8 | index 0 .. 8 radians | unused |
| FOLD | fold amount x1 .. x8 | unused | unused |

## 6. Count

Voice parameters: 58 rows (36 new ids, 22 existing). Slots NX 28 -> 64, EXT_P_LEN 156 -> 192, matrix destinations 57 -> 64. The patch record, the multi, the project and the editor protocol count the slots (they read min(n, NX) and default the rest), the patch store's worst record is kept at its old size by a dense tag in the extension block (up_xb.h XB_T_DENSE).
