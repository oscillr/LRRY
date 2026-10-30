# SPDX-License-Identifier: GPL-3.0-only
"""L/RRY factory sounds: the data (tools/gen_duell_sounds.py validates it and writes build/gen/duell_sounds.h).

L/RRY (owner, 2026-10-30): DUELL's 128 factory sounds are dropped (they were made for DUELL's engines). This is a first TEST SET for the unit, not the
L/RRY bank: each sound checks one part of the stereo voice (LRRY/PARAM_LIST.md). They all play on engine 0 (ANALOG in the table): L/RRY has one sound source,
the engine number is only the sound's identity (favourites, shadows, the End B seed). Every sound: idx (its factory index: append-only), name (12 at most),
cat, p: the parameters that differ from INIT (params.c TP and XTP defaults; extension slots by their ext.h names, X_...), bseed / bamt: End B (absent: the
hash of (engine, idx) and the category's AMOUNT). TRIM levels each sound to about the same loudness (tests/lrry_sounds_test.c measures them).
"""

SOUNDS = [
    # 0 the power-on sound and the reference: one saw, filter A open, KEY TRK 0 (filter A not run), no dirt. The cheapest voice
    dict(engine='ANALOG', idx=0, name='TEST SAW', cat='OTHER',
         p={'X_O1LVL': 100, 'KTRK': 0, 'ATK': 0, 'DEC': 60, 'SUS': 110, 'REL': 40, 'TRIM': 104}),
    # 1 stereo pad: SUPER 7 saws spread wide, a second saw an octave up, filter A low-pass opened by ENV 2, LFO 1 moving FILTER A PAN, SIDE up
    dict(engine='ANALOG', idx=1, name='WIDE PAD', cat='PAD',
         p={'X_O1MODEL': 'SUPER', 'X_O1LVL': 80, 'X_O1P1': 70, 'X_O1P2': 127, 'X_O1P3': 110,
            'X_O2MODEL': 'SAW', 'X_O2LVL': 45, 'X_O2COARSE': 12, 'X_O2FINE': 7,
            'FREEA': 50, 'FREEB': 20, 'FREEC': 'LP', 'KTRK': 0, 'E2FLT': 18, 'E2ATK': 70, 'E2DEC': 80, 'E2SUS': 60, 'E2REL': 80,
            'ATK': 70, 'DEC': 80, 'SUS': 110, 'REL': 85, 'LRATE': 45, 'MOD1': ('LFO1', 'APAN', 25), 'X_SIDE': 80, 'TRIM': 108}),
    # 2 mono bass: legato with glide, filter A 24 dB with high resonance and an ENV 2 sweep, DIRT A SOFT, an octave down
    dict(engine='ANALOG', idx=2, name='ACID BASS', cat='BASS',
         p={'VOICE': 'LEG', 'GLIDE': 20, 'TRANS': -12, 'X_O1LVL': 110,
            'FREEA': 25, 'FREEB': 95, 'FREEC': 'LP', 'FREED': 2, 'KTRK': 20, 'E2FLT': 40, 'E2ATK': 0, 'E2DEC': 55, 'E2SUS': 0, 'E2REL': 30,
            'ATK': 0, 'DEC': 70, 'SUS': 100, 'REL': 25, 'X_DATYPE': 'SOFT', 'DRV2': 45, 'X_DALVL': 100, 'MOD1': ('VEL', 'CUT', 10), 'TRIM': 100}),
    # 3 FM keys: OSC 1 FM (ratio 3.5, index 3 rad), a sine an octave up a little to the right, a decaying envelope, no filter
    dict(engine='ANALOG', idx=3, name='FM BELL', cat='KEYS',
         p={'X_O1MODEL': 'FM', 'X_O1LVL': 100, 'X_O1P1': 51, 'X_O1P2': 48,
            'X_O2MODEL': 'SINE', 'X_O2LVL': 40, 'X_O2COARSE': 12, 'X_O2PAN': 30, 'KTRK': 0,
            'ATK': 0, 'DEC': 90, 'SUS': 0, 'REL': 80, 'TRIM': 106}),
    # 4 lead: OSC 1 SYNC to the right, OSC 2 PULSE an octave down to the left, both partly into filter B BP (PARAL), DIRT B FUZZ on that branch, a delayed vibrato
    dict(engine='ANALOG', idx=4, name='SYNC LEAD', cat='LEAD',
         p={'X_O1MODEL': 'SYNC', 'X_O1LVL': 100, 'X_O1P1': 50, 'X_O1PAN': 30, 'X_O1BAL': 30,
            'X_O2MODEL': 'PULSE', 'X_O2LVL': 60, 'X_O2P1': 40, 'X_O2COARSE': -12, 'X_O2PAN': -30, 'X_O2BAL': 30, 'KTRK': 0,
            'TWTYPE': 'BP', 'X_LINK': 'OFF', 'CUT2': 55, 'RES2': 40, 'X_ROUTE': 'PARAL', 'X_DBTYPE': 'FUZZ', 'X_DBDRV': 40, 'X_DBLVL': 90,
            'LRATE': 60, 'LFADE': 60, 'LD_PIT': 3, 'GLIDE': 10, 'ATK': 5, 'DEC': 70, 'SUS': 100, 'REL': 40, 'TRIM': 117}),
    # 5 COMB: a triangle and noise into filter B COMB (tuned to the note, feedback +0.79), a decaying envelope: the COMB lines in the part memory.
    #   The COMB's output sits about 9 dB under the dry source here: TRIM 121 makes it up
    dict(engine='ANALOG', idx=5, name='COMB PLUCK', cat='PLUCK',
         p={'X_O1MODEL': 'TRI', 'X_O1LVL': 100, 'X_O1BAL': 63, 'X_NZLVL': 100, 'X_NZBAL': 63, 'KTRK': 0,
            'TWTYPE': 'COMB', 'X_LINK': 'OFF', 'CUT2': 0, 'RES2': 58, 'X_ROUTE': 'PARAL',
            'ATK': 0, 'DEC': 70, 'SUS': 0, 'REL': 45, 'TRIM': 121}),
    # 6 L / R check: a saw hard left, a pulse a fifth up hard right (left alone, right alone: each speaker on its own)
    dict(engine='ANALOG', idx=6, name='STEREO TEST', cat='OTHER',
         p={'X_O1LVL': 100, 'X_O1PAN': -63, 'X_O2MODEL': 'PULSE', 'X_O2LVL': 100, 'X_O2P1': 64, 'X_O2COARSE': 7, 'X_O2PAN': 63, 'KTRK': 0,
            'ATK': 0, 'DEC': 60, 'SUS': 120, 'REL': 30, 'TRIM': 102}),
    # 7 CPU stress: every source (SUPER 7 saws, FM, noise, ring, sub), filter A BP 24 dB, filter B LADDER, both dirt units in series, SIDE up.
    #   The dearest voice: for the unit's bench (budget.c caps the notes)
    dict(engine='ANALOG', idx=7, name='CPU STRESS', cat='OTHER',
         p={'X_O1MODEL': 'SUPER', 'X_O1LVL': 60, 'X_O1P1': 60, 'X_O1P2': 127, 'X_O1P3': 90,
            'X_O2MODEL': 'FM', 'X_O2LVL': 50, 'X_O2P1': 30, 'X_O2P2': 40, 'X_O2COARSE': 7,
            'X_NZLVL': 25, 'X_RGLVL': 30, 'SUB LEVEL': 40,
            'FREEA': 50, 'FREEB': 30, 'FREEC': 'BP', 'FREED': 2, 'KTRK': 0,
            'TWTYPE': 'LADDER', 'CUT2': 0, 'RES2': 60, 'X_DATYPE': 'HARD', 'DRV2': 30, 'X_DBTYPE': 'FOLD', 'X_DBDRV': 30, 'X_DSER': 'SERIES',
            'X_SIDE': 90, 'ATK': 10, 'DEC': 70, 'SUS': 100, 'REL': 50, 'TRIM': 116}),
]
