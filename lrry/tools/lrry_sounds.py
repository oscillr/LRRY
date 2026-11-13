# SPDX-License-Identifier: GPL-3.0-only
"""L/RRY factory sounds: the data (tools/gen_lrry_sounds.py validates it and writes build/gen/lrry_sounds.h).

L/RRY (owner, 2026-10-30): L/RRY's 128 factory sounds are dropped (they were made for L/RRY's engines). This is the TEST AND STRESS SET for the unit (owner, beta 1
feedback, 2026-10-31: every filter, every pan, pan modulation, the stereo oscillator models, distortion, dense combinations; for listening tests and CPU stress tests),
not the L/RRY bank. Documented in LRRY/PATCHES_TEST_SET.md; tests/lrry_sounds_test.c renders every sound and proves the coverage matrix from the stored values.
They all play on engine 0 (ANALOG in the table): L/RRY has one sound source, the engine number is only the sound's identity (favourites, shadows, the End B seed).
Every sound: idx (its factory index: append-only), name (12 at most), cat, p: the parameters that differ from INIT (params.c TP and XTP defaults; extension slots by
their ext.h names, X_...), bseed / bamt: End B (absent: the hash of (engine, idx) and the category's AMOUNT). TRIM levels each sound to about the same loudness
(tests/lrry_sounds_test.c measures them at the unit's mix level).
Matrix routes: (SOURCE, DESTINATION, AMOUNT) in mod.c's short names: O1PAN OSC 1 PAN, O2PAN OSC 2 PAN, APAN MULTI FILTER PAN, BPAN TWIN FILTER PAN, NZPAN NOISE PAN,
RGPAN RING PAN, DAPAN DIRT A PAN, DBPAN DIRT B PAN, SUB PAN, PAN the part's pan. A bipolar source (LFO) at AMOUNT a swings a pan by +-a steps; a one-way source
(ENV, CYCLE) moves it by up to 2a steps.
0..7 are the cloud's first eight (names kept, they still say what the sound is); 8..10 the allocation report's three heaviest recipes (LRRY/PHASE49_ALLOC_REPORT.md
S2, S5, S6); 11..14 the dense patches (everything at once); 15 the pan law test.
"""

SOUNDS = [
    # 0 the power-on sound and the reference: one saw, filter A open, KEY TRK 0 (filter A not run), no dirt. The cheapest voice
    #   Movement (phase 56): LFO 1 (0.15 Hz) sways OSC 1 PAN, CYCLE (slow LOOP) drifts the part's PAN
    dict(engine='ANALOG', idx=0, name='TEST SAW', cat='OTHER',
         p={'X_O1LVL': 100, 'KTRK': 0, 'ATK': 0, 'DEC': 60, 'SUS': 110, 'REL': 40, 'TRIM': 103,
            'LRATE': 21, 'L2RATE': 31, 'X_CYRISE': 111, 'X_CYFALL': 117, 'X_CYPAN': 7, 'MOD1': ('LFO1', 'O1PAN', 11)}),
    # 1 stereo pad (string machine recipe: detuned saws, a second saw an octave up, slow attack): SUPER 7 saws spread wide, filter A low-pass 12 dB opened by ENV 2,
    #   LFO 1 moving MULTI FILTER PAN. MORPH: its own End B (bseed, with the category's AMOUNT: chosen by the seed search of LRRY/PATCHES_TEST_SET.md)
    #   Movement (phase 56): LFO 2 (0.32 Hz) sways the SUPERSAW pair's balance (OSC 1 PAN), CYCLE drifts OSC 2 PAN and opens MULTI CUTOFF a little, ENV 2 leans OSC 1 left as the pad opens
    dict(engine='ANALOG', idx=1, name='WIDE PAD', cat='PAD', bseed=28610,
         p={'X_O1MODEL': 'SUPER', 'X_O1LVL': 80, 'X_O1P1': 70, 'X_O1P2': 127, 'X_O1P3': 110,
            'X_O2MODEL': 'SAW', 'X_O2LVL': 45, 'X_O2COARSE': 12, 'X_O2FINE': 7,
            'FREEA': 50, 'FREEB': 20, 'FREEC': 0, 'KTRK': 0, 'E2FLT': 18, 'E2ATK': 70, 'E2DEC': 80, 'E2SUS': 60, 'E2REL': 80,
            'ATK': 70, 'DEC': 80, 'SUS': 110, 'REL': 85, 'LRATE': 45, 'MOD1': ('LFO1', 'APAN', 17), 'TRIM': 97,
            'L2RATE': 35, 'X_CYRISE': 107, 'X_CYFALL': 111, 'MOD2': ('LFO2', 'O1PAN', 11), 'MOD3': ('CYCLE', 'O2PAN', 9), 'MOD4': ('ENV2', 'O1PAN', -12), 'MOD5': ('CYCLE', 'CUT', 4)}),
    # 2 mono bass (303 recipe: one saw, resonant low-pass, a short filter decay, glide): filter A 24 dB with high resonance and an ENV 2 sweep, DIRT A SOFT, an octave down.
    #   MORPH: its own End B
    #   Movement (phase 56): LFO 1 (0.41 Hz) sways DIRT A PAN, CYCLE drifts MULTI FILTER PAN and MULTI CUTOFF a little, the FILTER ENV adds a little DIRT A DRIVE on each note
    dict(engine='ANALOG', idx=2, name='ACID BASS', cat='BASS', bseed=51095,
         p={'VOICE': 'LEG', 'GLIDE': 20, 'TRANS': -12, 'X_O1LVL': 110,
            'FREEA': 25, 'FREEB': 95, 'FREEC': 0, 'FREED': 2, 'KTRK': 20, 'E2FLT': 40, 'E2ATK': 0, 'E2DEC': 55, 'E2SUS': 0, 'E2REL': 30,
            'ATK': 0, 'DEC': 70, 'SUS': 100, 'REL': 25, 'X_DATYPE': 'SOFT', 'DRV2': 45, 'X_DALVL': 100, 'MOD1': ('VEL', 'CUT', 10), 'TRIM': 80,
            'LRATE': 40, 'L2RATE': 49, 'X_CYRISE': 101, 'X_CYFALL': 107, 'X_E2DADRV': 7, 'MOD2': ('LFO1', 'DAPAN', 46), 'MOD3': ('CYCLE', 'APAN', 7), 'MOD4': ('CYCLE', 'CUT', 3)}),
    # 3 FM keys (two-operator bell: ratio 3.5, index about 3 radians, a long decay): OSC 1 FM, a sine an octave up a little to the right, no filter
    #   Movement (phase 56): LFO 1 (0.28 Hz) sways the FM tone (OSC 1 PAN), the AMP ENV moves it right on the strike and lets it back as the bell decays, CYCLE drifts the part's PAN
    dict(engine='ANALOG', idx=3, name='FM BELL', cat='KEYS',
         p={'X_O1MODEL': 'FM', 'X_O1LVL': 100, 'X_O1P1': 51, 'X_O1P2': 48,
            'X_O2MODEL': 'SINE', 'X_O2LVL': 40, 'X_O2COARSE': 12, 'X_O2PAN': 30, 'KTRK': 0,
            'ATK': 0, 'DEC': 90, 'SUS': 0, 'REL': 80, 'TRIM': 101,
            'LRATE': 33, 'L2RATE': 43, 'X_CYRISE': 71, 'X_CYFALL': 97, 'X_CYPAN': 9, 'MOD1': ('LFO1', 'O1PAN', 13), 'MOD2': ('AENV', 'O1PAN', 7)}),
    # 4 lead: OSC 1 SYNC to the right, OSC 2 PULSE an octave down to the left, both partly into the TWIN FILTER BP (PARALLEL, LINK OFF), DIRT B FUZZ on that branch,
    #   a delayed vibrato
    #   Movement (phase 56): LFO 2 (0.25 Hz) sways OSC 1 PAN, CYCLE drifts TWIN FILTER PAN and TWIN CUTOFF a little, the AMP ENV moves DIRT B PAN left and adds a little DIRT B DRIVE. LFO 1 stays the vibrato (1.24 Hz)
    dict(engine='ANALOG', idx=4, name='SYNC LEAD', cat='LEAD',
         p={'X_O1MODEL': 'SYNC', 'X_O1LVL': 100, 'X_O1P1': 50, 'X_O1PAN': 30, 'X_O1BAL': 30,
            'X_O2MODEL': 'PULSE', 'X_O2LVL': 60, 'X_O2P1': 40, 'X_O2COARSE': -12, 'X_O2PAN': -30, 'X_O2BAL': 30, 'KTRK': 0,
            'TWTYPE': 'BP', 'X_LINK': 'OFF', 'CUT2': 55, 'RES2': 40, 'X_ROUTE': 'PARALLEL', 'X_DBTYPE': 'FUZZ', 'X_DBDRV': 40, 'X_DBLVL': 90,
            'LRATE': 61, 'LFADE': 60, 'LD_PIT': 3, 'GLIDE': 10, 'ATK': 5, 'DEC': 70, 'SUS': 100, 'REL': 40, 'TRIM': 118,
            'L2RATE': 32, 'X_CYRISE': 107, 'X_CYFALL': 101, 'X_CYCUT2': 5, 'X_EDBDRV': 7, 'MOD1': ('LFO2', 'O1PAN', 9), 'MOD2': ('CYCLE', 'BPAN', 40), 'MOD3': ('AENV', 'DBPAN', -15)}),
    # 5 COMB: a triangle and noise into the TWIN FILTER COMB (tuned to the note, feedback +0.79), a decaying envelope (plucked string recipe: a noise burst into a tuned comb).
    #   MORPH: its own End B (the hash seed's End B was 3 dB quieter and far brighter)
    #   Movement (phase 56): LFO 1 (0.33 Hz) sways TWIN FILTER PAN, the AMP ENV throws the noise burst right, LFO 2 (0.56 Hz) sways the part's PAN, CYCLE moves the COMB's FEEDBACK a little
    dict(engine='ANALOG', idx=5, name='COMB PLUCK', cat='PLUCK', bseed=5655,
         p={'X_O1MODEL': 'TRI', 'X_O1LVL': 100, 'X_O1BAL': 63, 'X_NZLVL': 100, 'X_NZBAL': 63, 'KTRK': 0,
            'TWTYPE': 'COMB', 'X_LINK': 'OFF', 'CUT2': 0, 'RES2': 58, 'X_ROUTE': 'PARALLEL',
            'ATK': 0, 'DEC': 70, 'SUS': 0, 'REL': 45, 'TRIM': 122,
            'LRATE': 36, 'L2RATE': 46, 'X_CYRISE': 111, 'X_CYFALL': 117, 'MOD1': ('LFO1', 'BPAN', 39), 'MOD2': ('AENV', 'NZPAN', 31), 'MOD3': ('LFO2', 'PAN', 20), 'MOD4': ('CYCLE', 'FRES2', 3)}),
    # 6 L / R check: a saw hard left, a pulse a fifth up hard right (each speaker on its own: two oscillators at opposite pans, the mono pan law)
    #   Movement (phase 56): LFO 1 (0.18 Hz) sways the part's PAN a little (each oscillator stays on its own side)
    dict(engine='ANALOG', idx=6, name='STEREO TEST', cat='OTHER',
         p={'X_O1LVL': 100, 'X_O1PAN': -63, 'X_O2MODEL': 'PULSE', 'X_O2LVL': 100, 'X_O2P1': 64, 'X_O2COARSE': 7, 'X_O2PAN': 63, 'KTRK': 0,
            'ATK': 0, 'DEC': 60, 'SUS': 120, 'REL': 30, 'TRIM': 96,
            'LRATE': 24, 'L2RATE': 34, 'MOD1': ('LFO1', 'PAN', 9)}),
    # 7 the full stress (the cloud's CPU STRESS, renamed: no abbreviation in a name): every source (SUPER 7 saws, FM, noise, ring, sub), filter A BP 24 dB, TWIN FILTER LADDER (LINK ON, offset 0), both dirt units in SERIES.
    #   For the unit's bench (budget.c caps the notes)
    #   Movement (phase 56): LFO 1 (0.59 Hz) sways the part's PAN, LFO 2 (0.39 Hz) DIRT B PAN, CYCLE drifts DIRT B PAN and MULTI CUTOFF, ENV 2 moves DIRT A PAN and adds a little DIRT A DRIVE
    dict(engine='ANALOG', idx=7, name='FULL STRESS', cat='OTHER',
         p={'X_O1MODEL': 'SUPER', 'X_O1LVL': 60, 'X_O1P1': 60, 'X_O1P2': 127, 'X_O1P3': 90,
            'X_O2MODEL': 'FM', 'X_O2LVL': 50, 'X_O2P1': 30, 'X_O2P2': 40, 'X_O2COARSE': 7,
            'X_NZLVL': 25, 'X_RGLVL': 30, 'X_SUBLVL': 40,
            'FREEA': 50, 'FREEB': 30, 'FREEC': 64, 'FREED': 2, 'KTRK': 0,
            'TWTYPE': 'LADDER', 'CUT2': 0, 'RES2': 60, 'X_DATYPE': 'HARD', 'DRV2': 30, 'X_DBTYPE': 'FOLD', 'X_DBDRV': 30, 'X_DSER': 'SERIES',
            'ATK': 10, 'DEC': 70, 'SUS': 100, 'REL': 50, 'TRIM': 117,
            'LRATE': 47, 'L2RATE': 39, 'X_CYRISE': 101, 'X_CYFALL': 111, 'X_E2DADRV': 5, 'MOD1': ('LFO1', 'PAN', 11), 'MOD2': ('LFO2', 'DBPAN', 13), 'MOD3': ('CYCLE', 'DBPAN', 9), 'MOD4': ('ENV2', 'DAPAN', 7), 'MOD5': ('CYCLE', 'CUT', 3)}),
    # 8 PHASE49_ALLOC_REPORT.md S2, as written there (named for its two dirt units: SUPERSAW STRESS is over 12 letters) (the stress set's common envelope ATK 0 DEC 0 SUS 127 REL 30, KEY TRK 0): OSC 1 SUPER 7 (detune 50, spread 100,
    #   level 90), OSC 2 SAW level 80 an octave down, filter A 60 / RES 40 / 24 dB, TWIN FILTER LADDER 55 / RES 70 (LINK OFF: 55 is the dial), DIRT A SOFT 70,
    #   DIRT B FUZZ 50 level 90, dirt PARALLEL
    #   Movement (phase 56): LFO 1 (0.77 Hz) and LFO 2 (0.46 Hz) sway the two dirt units' pans against each other, CYCLE drifts TWIN FILTER PAN and TWIN CUTOFF, the AMP ENV moves OSC 2 right and adds a little DIRT B DRIVE
    dict(engine='ANALOG', idx=8, name='DIRT STRESS', cat='OTHER',
         p={'X_O1MODEL': 'SUPER', 'X_O1LVL': 90, 'X_O1P1': 50, 'X_O1P2': 127, 'X_O1P3': 100,
            'X_O2MODEL': 'SAW', 'X_O2LVL': 80, 'X_O2COARSE': -12,
            'FREEA': 60, 'FREEB': 40, 'FREEC': 0, 'FREED': 2, 'KTRK': 0,
            'TWTYPE': 'LADDER', 'X_LINK': 'OFF', 'CUT2': 55, 'RES2': 70,
            'X_DATYPE': 'SOFT', 'DRV2': 70, 'X_DBTYPE': 'FUZZ', 'X_DBDRV': 50, 'X_DBLVL': 90, 'X_DSER': 'PARALLEL',
            'ATK': 0, 'DEC': 0, 'SUS': 127, 'REL': 30, 'TRIM': 108,
            'LRATE': 52, 'L2RATE': 42, 'X_CYRISE': 117, 'X_CYFALL': 107, 'X_CYCUT2': 4, 'X_EDBDRV': 5, 'MOD1': ('LFO1', 'DAPAN', 18), 'MOD2': ('LFO2', 'DBPAN', -41), 'MOD3': ('CYCLE', 'BPAN', 11), 'MOD4': ('AENV', 'O2PAN', 21)}),
    # 9 S5 SWEEP: OSC 1 SUPER 7, OSC 2 SAW level 80 FINE -9, filter A 30 / RES 40 / 24 dB, FILTER ENV 0 / 100 / 0 / 40 to MULTI CUTOFF at the card's top (+63; the
    #   report's "127" is above the card's range), TWIN FILTER LADDER 60 / RES 50 (LINK OFF), DIRT A SOFT 60. Filter A starts open (not run) and starts to run as the
    #   envelope falls: the late cut of the report
    #   Movement (phase 56): LFO 1 (0.37 Hz) sways the part's PAN, LFO 2 (0.23 Hz) MULTI FILTER PAN, CYCLE drifts DIRT A PAN and TWIN CUTOFF, the FILTER ENV moves TWIN FILTER PAN left with the sweep
    dict(engine='ANALOG', idx=9, name='SWEEP STRESS', cat='OTHER',
         p={'X_O1MODEL': 'SUPER', 'X_O1LVL': 90, 'X_O1P1': 50, 'X_O1P2': 127, 'X_O1P3': 100,
            'X_O2MODEL': 'SAW', 'X_O2LVL': 80, 'X_O2FINE': -9,
            'FREEA': 30, 'FREEB': 40, 'FREEC': 0, 'FREED': 2, 'KTRK': 0,
            'E2ATK': 0, 'E2DEC': 100, 'E2SUS': 0, 'E2REL': 40, 'E2FLT': 63,
            'TWTYPE': 'LADDER', 'X_LINK': 'OFF', 'CUT2': 60, 'RES2': 50, 'X_DATYPE': 'SOFT', 'DRV2': 60,
            'ATK': 0, 'DEC': 0, 'SUS': 127, 'REL': 30, 'TRIM': 96,
            'LRATE': 38, 'L2RATE': 29, 'X_CYRISE': 107, 'X_CYFALL': 117, 'X_CYCUT2': 5, 'MOD1': ('LFO1', 'PAN', 11), 'MOD2': ('LFO2', 'APAN', 33), 'MOD3': ('CYCLE', 'DAPAN', 13), 'MOD4': ('ENV2', 'BPAN', -20)}),
    # 10 S6 FM / RING / NOISE: OSC 1 FM (P1 40, P2 60, level 90), OSC 2 SYNC level 70, RING 40, NOISE 20, SUB 40 (octave -1), filter A 65 / 24 dB, TWIN FILTER LP 70
    #   (LINK OFF), DIRT A SOFT 50, DIRT B FOLD 40 level 90. (The report's SIDE 90 is gone with the stage, phase 50)
    #   Movement (phase 56): LFO 1 (0.48 Hz) and CYCLE sway DIRT A PAN (after the drive: a pan before it is pressed flat), LFO 2 (0.81 Hz) the part's PAN, CYCLE also drifts MULTI CUTOFF, the AMP ENV moves DIRT A PAN left on each note, the FILTER ENV adds a little DIRT A DRIVE
    dict(engine='ANALOG', idx=10, name='FM STRESS', cat='OTHER',
         p={'X_O1MODEL': 'FM', 'X_O1LVL': 90, 'X_O1P1': 40, 'X_O1P2': 60,
            'X_O2MODEL': 'SYNC', 'X_O2LVL': 70, 'X_RGLVL': 40, 'X_NZLVL': 20, 'X_SUBLVL': 40,
            'FREEA': 65, 'FREEC': 0, 'FREED': 2, 'KTRK': 0,
            'TWTYPE': 'LP', 'X_LINK': 'OFF', 'CUT2': 70,
            'X_DATYPE': 'SOFT', 'DRV2': 50, 'X_DBTYPE': 'FOLD', 'X_DBDRV': 40, 'X_DBLVL': 90,
            'ATK': 0, 'DEC': 0, 'SUS': 127, 'REL': 30, 'TRIM': 113,
            'LRATE': 43, 'L2RATE': 53, 'X_CYRISE': 111, 'X_CYFALL': 101, 'X_E2DADRV': 5, 'MOD1': ('LFO1', 'DAPAN', 9), 'MOD2': ('LFO2', 'PAN', 9), 'MOD3': ('CYCLE', 'DAPAN', 11), 'MOD4': ('AENV', 'DAPAN', -7), 'MOD5': ('CYCLE', 'CUT', 3)}),
    # 11 dense pad (string machine recipe again, here with every stage): OSC 1 SUPER 5 saws (detune 27, spread 77) to the left, OSC 2 TRIANGLE an octave up to the
    #   right, SUB (octave -1) a little right, a breath of NOISE far left. Filter A LP>BP blend 24 dB opened slowly by ENV 2, then the TWIN FILTER NOTCH (LINK ON, an
    #   octave above filter A) in SERIES. DIRT A FOLD and DIRT B SOFT, both gentle, PARALLEL, at opposite pans; the two filter pans opposite too.
    #   The pans move: LFO 1 (0.2 Hz) sways OSC 1 PAN, LFO 2 (0.3 Hz, the LFO 2 DEST card) sways MULTI FILTER PAN, ENV 2 carries the octave TRIANGLE from the right
    #   toward the centre as the pad opens.
    #   MORPH: its own End B
    #   Movement (phase 56), added: CYCLE drifts DIRT B PAN, LFO 2 also sways DIRT A PAN, the FILTER ENV adds a little DIRT A DRIVE (64 stored values: the record is full)
    dict(engine='ANALOG', idx=11, name='AUTOPAN PAD', cat='PAD', bseed=58891,
         p={'X_O1MODEL': 'SUPER', 'X_O1LVL': 71, 'X_O1P1': 27, 'X_O1P2': 85, 'X_O1P3': 77, 'X_O1PAN': -21,
            'X_O2MODEL': 'TRI', 'X_O2LVL': 41, 'X_O2COARSE': 12, 'X_O2FINE': 7, 'X_O2PAN': 27,
            'X_SUBLVL': 21, 'X_SUBPAN': 11, 'X_NZLVL': 7, 'X_NZPAN': -47,
            'FREEA': 57, 'FREEB': 21, 'FREEC': 32, 'FREED': 2, 'KTRK': 17, 'E2FLT': 11, 'E2ATK': 71, 'E2DEC': 77, 'E2SUS': 71, 'E2REL': 77,
            'TWTYPE': 'NOTCH', 'X_LINK': 'ON', 'CUT2': 12, 'RES2': 27, 'X_ROUTE': 'SERIES', 'X_FAPAN': -11, 'X_FBPAN': 11,
            'X_DATYPE': 'FOLD', 'DRV2': 11, 'X_DALVL': 107, 'X_DAPAN': -27, 'X_DBTYPE': 'SOFT', 'X_DBDRV': 17, 'X_DBPAN': 27, 'X_DSER': 'PARALLEL',
            'LRATE': 27, 'L2RATE': 37, 'L2WAVE': 'TRI', 'X_L2FAPAN': 17,
            'MOD1': ('LFO1', 'O1PAN', 21), 'MOD2': ('ENV2', 'O2PAN', -11),
            'ATK': 71, 'DEC': 80, 'SUS': 110, 'REL': 85, 'TRIM': 122,
            'X_CYRISE': 117, 'X_CYFALL': 107, 'X_E2DADRV': 4, 'MOD3': ('CYCLE', 'DBPAN', 9), 'MOD4': ('LFO2', 'DAPAN', -16)}),
    # 12 dense mono lead (the fifth-stacked lead recipe: a second oscillator a fifth up, legato, glide, a delayed vibrato): OSC 1 SUPER 3 saws to the right, OSC 2 WAVEFOLD
    #   a fifth up to the left, a little RING further right, SUB two octaves down. PARALLEL filters: filter A BP>HP blend 12 dB on the bright branch, the TWIN FILTER
    #   LADDER (LINK OFF) on the body, opposite pans. DIRT A FUZZ then DIRT B HARD in SERIES, light, opposite pans.
    #   The pans move: CYCLE (slow LOOP, the CYCLE ENV 2/2 card) drifts the part's PAN, ENV 2 pulls DIRT B PAN toward the centre on each note, LFO 2 (0.4 Hz) sways TWIN FILTER PAN
    #   (the LADDER body).
    #   MORPH: its own End B
    #   Movement (phase 56), added: CYCLE also moves TWIN CUTOFF a little (64 stored values: the record is full; the pans already move by CYCLE, ENV 2 and LFO 2). LFO 1 stays the vibrato (1.12 Hz)
    dict(engine='ANALOG', idx=12, name='FIFTH LEAD', cat='LEAD', bseed=10295,
         p={'VOICE': 'LEG', 'GLIDE': 11,
            'X_O1MODEL': 'SUPER', 'X_O1LVL': 90, 'X_O1P1': 17, 'X_O1P2': 42, 'X_O1P3': 57, 'X_O1PAN': 17, 'X_O1BAL': -21,
            'X_O2MODEL': 'WAVEFOLD', 'X_O2LVL': 57, 'X_O2P1': 21, 'X_O2COARSE': 7, 'X_O2PAN': -27, 'X_O2BAL': 21,
            'X_RGLVL': 11, 'X_RGPAN': 37, 'X_SUBLVL': 17, 'X_SUBOCT': '-2', 'X_SUBPAN': -7,
            'FREEA': 61, 'FREEB': 27, 'FREEC': 96, 'KTRK': 0, 'E2FLT': 17, 'E2ATK': 0, 'E2DEC': 61, 'E2SUS': 47, 'E2REL': 40,
            'TWTYPE': 'LADDER', 'X_LINK': 'OFF', 'CUT2': 51, 'RES2': 47, 'X_ENV2': 17, 'X_ROUTE': 'PARALLEL', 'X_FAPAN': -17, 'X_FBPAN': 17,
            'X_DATYPE': 'FUZZ', 'DRV2': 21, 'X_DALVL': 97, 'X_DAPAN': -21, 'X_DBTYPE': 'HARD', 'X_DBDRV': 17, 'X_DBLVL': 107, 'X_DBPAN': 21, 'X_DSER': 'SERIES',
            'X_CYRISE': 107, 'X_CYFALL': 107, 'X_CYPAN': -11,
            'L2RATE': 41, 'MOD1': ('ENV2', 'DBPAN', -11), 'MOD2': ('LFO2', 'BPAN', 17),
            'LRATE': 59, 'LFADE': 61, 'LD_PIT': 3, 'ATK': 5, 'SUS': 100, 'REL': 40, 'TRIM': 118,
            'X_CYCUT2': 4}),
    # 13 dense bass (a two-band bass: the low body in the centre, a bright detuned top made wide): OSC 1 SUPER 3 saws (a little left) all into filter A, OSC 2 PULSE
    #   (a little right) mostly into the TWIN FILTER, SUB an octave down. PARALLEL filters: filter A HP 12 dB (about 200 Hz) is the top, the TWIN FILTER LP (LINK OFF, swept by ENV 2)
    #   the body. DIRT A CRUSH (about 10 bits) on the top, DIRT B SOFT on the body, PARALLEL, at their own pans.
    #   The pans move: ENV 2 throws the crushed top (DIRT A PAN) across on each note, CYCLE (LOOP, its default, through the matrix) sways MULTI FILTER PAN
    #   Movement (phase 56), added: LFO 2 (0.41 Hz) sways the PULSE (OSC 2 PAN), CYCLE moves TWIN CUTOFF a little, the AMP ENV adds a little DIRT B DRIVE
    dict(engine='ANALOG', idx=13, name='WIDE BASS', cat='BASS',
         p={'VOICE': 'MONO',
            'X_O1MODEL': 'SUPER', 'X_O1LVL': 117, 'X_O1P1': 21, 'X_O1P2': 42, 'X_O1P3': 71, 'X_O1PAN': -11, 'X_O1BAL': -63,
            'X_O2MODEL': 'PULSE', 'X_O2LVL': 27, 'X_O2P1': 51, 'X_O2FINE': -7, 'X_O2PAN': 11, 'X_O2BAL': 47,
            'X_SUBLVL': 27, 'X_SUBOCT': '-1',
            'FREEA': 25, 'FREEB': 21, 'FREEC': 127, 'FREED': 1, 'KTRK': 0, 'E2FLT': 17, 'E2ATK': 0, 'E2DEC': 77, 'E2SUS': 21, 'E2REL': 27,
            'TWTYPE': 'LP', 'X_LINK': 'OFF', 'CUT2': 41, 'RES2': 37, 'X_ENV2': 27, 'X_ROUTE': 'PARALLEL', 'X_FAPAN': -21, 'X_FBPAN': 7,
            'X_DATYPE': 'CRUSH', 'DRV2': 77, 'X_DALVL': 117, 'X_DAPAN': -37, 'X_DBTYPE': 'SOFT', 'X_DBDRV': 37, 'X_DBLVL': 117, 'X_DBPAN': 7, 'X_DSER': 'PARALLEL',
            'X_CYRISE': 101, 'X_CYFALL': 101,
            'MOD1': ('ENV2', 'DAPAN', 47), 'MOD2': ('CYCLE', 'APAN', 27),
            'ATK': 0, 'DEC': 70, 'SUS': 100, 'REL': 21, 'TRIM': 112,
            'LRATE': 31, 'L2RATE': 40, 'X_CYCUT2': 3, 'X_EDBDRV': 5, 'MOD3': ('LFO2', 'O2PAN', 11)}),
    # 14 dense keys (the clavinet recipe: a narrow pulse, a bright resonant filter with a fast decay, played through an auto-pan): OSC 1 PULSE (width about 20 %) a
    #   little right, OSC 2 SUPER 2 saws an octave up (a light chorus) to the right, SUB at the note's own octave, a click of NOISE left, a little RING right. Filter A LP>BP
    #   blend (MODE 21, mostly low-pass) 12 dB with a fast ENV 2, then the TWIN FILTER HP (LINK OFF, its dial at 21, about 150 Hz: a gentle low cut) in SERIES. DIRT A HARD and DIRT B
    #   CRUSH (about 13 bits), PARALLEL, opposite pans.
    #   The pans move: LFO 1 (about 0.7 Hz) auto-pans DIRT B, the AMP ENV puts each note's attack left of centre (OSC 1 PAN) and lets it drift back, CYCLE (the CYCLE ENV 2/2 card) drifts the part's PAN
    #   Movement (phase 56), added: LFO 2 (0.51 Hz) sways MULTI FILTER PAN, the FILTER ENV adds a little DIRT A DRIVE on each note
    dict(engine='ANALOG', idx=14, name='CLAVINET', cat='KEYS',
         p={'X_O1MODEL': 'PULSE', 'X_O1LVL': 97, 'X_O1P1': 21, 'X_O1PAN': 7,
            'X_O2MODEL': 'SUPER', 'X_O2LVL': 31, 'X_O2P1': 11, 'X_O2P2': 21, 'X_O2P3': 47, 'X_O2COARSE': 12, 'X_O2PAN': 27,
            'X_SUBLVL': 17, 'X_SUBOCT': '0', 'X_SUBPAN': 11, 'X_NZLVL': 7, 'X_NZPAN': -37, 'X_RGLVL': 17, 'X_RGPAN': 41,
            'FREEA': 61, 'FREEB': 37, 'FREEC': 21, 'FREED': 1, 'E2FLT': 21, 'E2ATK': 0, 'E2DEC': 41, 'E2SUS': 0, 'E2REL': 21,
            'TWTYPE': 'HP', 'X_LINK': 'OFF', 'CUT2': 21, 'RES2': 17, 'X_ROUTE': 'SERIES', 'X_FAPAN': 11, 'X_FBPAN': -11,
            'X_DATYPE': 'HARD', 'DRV2': 21, 'X_DALVL': 107, 'X_DAPAN': -27, 'X_DBTYPE': 'CRUSH', 'X_DBDRV': 61, 'X_DBLVL': 97, 'X_DBPAN': 31, 'X_DSER': 'PARALLEL',
            'X_CYRISE': 107, 'X_CYFALL': 107, 'X_CYPAN': 7,
            'LRATE': 51, 'MOD1': ('LFO1', 'DBPAN', 27), 'MOD2': ('AENV', 'O1PAN', -11),
            'ATK': 0, 'DEC': 61, 'SUS': 41, 'REL': 21, 'TRIM': 127,
            'L2RATE': 44, 'X_E2DADRV': 4, 'MOD3': ('LFO2', 'APAN', 20)}),
    # 15 pan law check (phase 50): two SUPER oscillators of 4 saws, the same detune, at opposite pans. OSC 1 has SPREAD 127, a stereo pair: its PAN -41 is a balance
    #   (the right side down, the left unchanged). OSC 2, an octave up, has SPREAD 0, one mono signal: its PAN +41 is the pan law (the right side up to +3 dB).
    #   No filter, no dirt
    #   Movement (phase 56): LFO 1 (0.19 Hz) and LFO 2 (0.28 Hz) move each SUPERSAW a little in from its side (the pan law and the balance are still heard at the pans' ends)
    dict(engine='ANALOG', idx=15, name='PAN LAW TEST', cat='OTHER',
         p={'X_O1MODEL': 'SUPER', 'X_O1LVL': 90, 'X_O1P1': 47, 'X_O1P2': 64, 'X_O1P3': 127, 'X_O1PAN': -41,
            'X_O2MODEL': 'SUPER', 'X_O2LVL': 90, 'X_O2P1': 47, 'X_O2P2': 64, 'X_O2P3': 0, 'X_O2COARSE': 12, 'X_O2PAN': 41, 'KTRK': 0,
            'ATK': 0, 'DEC': 60, 'SUS': 120, 'REL': 30, 'TRIM': 93,
            'LRATE': 25, 'L2RATE': 33, 'MOD1': ('LFO1', 'O1PAN', 20), 'MOD2': ('LFO2', 'O2PAN', -7)}),
]
