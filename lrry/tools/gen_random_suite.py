#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""L/RRY bench: LRRY-RANDOM, random generated patches for the voice counter check (owner, 2026-11-10, step 2).

Owner rules: the counter's numbers come from GENERATED voices, never from factory patches, and the screen never shows more voices than
the unit really plays. Every random patch has EVERYTHING ON (rule G25 / G25a): two oscillators, SUB, noise and ring by chance, the
MULTI filter, the TWIN filter with a plain type (LP BP HP NOTCH; LADDER and COMB left out as G25a says), both dirt units, the LFOs,
ENV 2, CYCLE and 2 to 6 matrix routes with internal sources. Left alone: the amp envelope (the note must sustain the hold), level,
voice mode, glide, arp, chord, sequencer, sends (there are none), MORPH (the bench cannot roll).

Each patch is sent tool side like FULL STRESS: `load patch 1 #03 "SAW BASS"`, then `set 1 ID VALUE` for every value that differs.
Seeds are fixed, so a run is repeatable; the values of every seed are written next to the suite (random_<seed>_values.txt).

    python3 tools/gen_random_suite.py            writes tools/bench_suites/LRRY-RANDOM.txt and the values files
"""
import importlib.util
import os
import random
from pathlib import Path

HERE = Path(__file__).resolve().parent
SEEDS = [1701, 1707, 1711, 1717, 1771, 1777]
KEYS = 8
NOTES = ["C3", "E3", "G3", "B3", "D4", "F4", "A4", "C5", "E5", "G5", "B5", "D6"]
MATRIX_SRC = [1, 2, 3, 4, 5, 9, 10, 11, 12, 14]             # LFO1 AENV VEL KEY RAND S&H SLEW ENV2 LFO2 CYCLE (no external sources)
MATRIX_DST_BAD = {0, 42, 43, 44, 45, 56, 61}                 # OFF and the '-' entries of the destination list
MATRIX_SLOTS = [(49, 50, 51), (52, 53, 54), (55, 56, 57), (58, 59, 60), (61, 62, 63), (64, 65, 66), (67, 68, 69), (70, 71, 72)]


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def random_patch(seed):
    """-> {id: value}: the full state of one random patch (defaults where the dice did not fall)"""
    r = random.Random(seed)
    p = {}
    maybe = lambda chance=0.5: r.random() < chance
    # OSC 1 and OSC 2: both on
    p[156] = r.randint(0, 7); p[159] = r.randint(60, 127)
    p[165] = r.randint(0, 7); p[168] = r.randint(40, 127)
    for base in (156, 165):
        if maybe(): p[base + 1] = r.randint(-12, 12)        # COARSE
        if maybe(): p[base + 2] = r.randint(-50, 50)        # FINE
        if maybe(): p[base + 4] = r.randint(-63, 63)        # PAN
        if maybe(): p[base + 5] = r.randint(-63, 63)        # BAL
        for q in (6, 7, 8):                                 # P1 P2 P3
            p[base + q] = r.randint(0, 127)
    # noise, ring: by chance
    if maybe(): p[174] = r.randint(20, 80); p[175] = r.randint(-63, 63) if maybe() else 0; p[176] = r.randint(-63, 63) if maybe() else 0
    if maybe(): p[177] = r.randint(20, 100); p[178] = r.randint(-63, 63) if maybe() else 0; p[179] = r.randint(-63, 63) if maybe() else 0
    # SUB: on
    p[134] = r.randint(60, 127); p[135] = r.randint(0, 2); p[136] = r.randint(0, 3); p[137] = r.randint(0, 127)
    if maybe(): p[190] = r.randint(-63, 63)
    if maybe(): p[44] = r.randint(0, 100)                   # DETUNE
    # MULTI filter: on
    p[45] = r.randint(25, 82); p[46] = r.randint(0, 100); p[47] = r.randint(0, 127); p[48] = r.randint(1, 2)
    if maybe(): p[27] = r.randint(-63, 63)
    if maybe(): p[180] = r.randint(-63, 63)
    # TWIN filter: on, a plain type (G25a)
    p[43] = r.randint(1, 4); p[106] = r.randint(-40, 82); p[25] = r.randint(0, 100); p[26] = r.randint(0, 100)
    p[129] = r.randint(0, 1); p[128] = r.randint(0, 1); p[101] = r.randint(-63, 63)
    if maybe(): p[130] = r.randint(-63, 63)
    if maybe(): p[181] = r.randint(-63, 63)
    # DIRT A and B: on
    p[182] = r.randint(1, 5); p[184] = r.randint(80, 127); p[185] = r.randint(1, 5); p[186] = r.randint(0, 100); p[188] = r.randint(80, 127)
    p[189] = r.randint(0, 1)
    if maybe(): p[183] = r.randint(-63, 63)
    if maybe(): p[187] = r.randint(-63, 63)
    # LFO 1 and LFO 2: on, depths by chance
    p[9] = r.randint(10, 110); p[10] = r.randint(0, 4)
    for i in (13, 14, 15):
        if maybe(): p[i] = r.randint(-40, 40)
    if maybe(): p[16] = r.randint(0, 60)
    if maybe(): p[132] = r.randint(-63, 63)
    p[77] = r.randint(10, 110); p[78] = r.randint(0, 4)
    for i in (116, 117, 118, 133, 150, 151, 152):
        if maybe(): p[i] = r.randint(-40, 40)
    # ENV 2 and CYCLE: on, routes by chance
    p[73] = r.randint(0, 60); p[74] = r.randint(20, 110); p[75] = r.randint(20, 127); p[76] = r.randint(20, 100)
    for i in (114, 115, 131, 143, 144, 145, 146):
        if maybe(): p[i] = r.randint(-63, 63)
    p[139] = r.randint(0, 100); p[140] = r.randint(10, 110); p[141] = r.randint(-63, 63); p[138] = r.randint(0, 1); p[142] = r.randint(0, 1)
    for i in (147, 148, 149):
        if maybe(): p[i] = r.randint(-63, 63)
    # matrix: 2 to 6 routes
    for slot in MATRIX_SLOTS[:r.randint(2, 6)]:
        dst = r.randint(1, 63)
        while dst in MATRIX_DST_BAD:
            dst = r.randint(1, 63)
        amt = r.randint(-50, 50) or 17
        p[slot[0]], p[slot[1]], p[slot[2]] = r.choice(MATRIX_SRC), dst, amt
    return p


def main():
    bt = _load("bench_testpatch", HERE / "bench_testpatch.py")
    default, snd = bt._states()
    base = snd["128"]["SAW BASS"]
    out = ["# LRRY-RANDOM (generated by tools/gen_random_suite.py): random generated patches with everything on (rule G25 / G25a),",
           "# 1 .. %d keys, two cores then one core, no limit gate. Read: the largest N with 0 misses and no clicks (clean) against the NOTES column (the screen number)." % KEYS,
           "suite LRRY-RANDOM", ""]
    for seed in SEEDS:
        want = random_patch(seed)
        vals = ["# random seed %d: id value (the full state; values at their default left out)" % seed]
        for i in sorted(want):
            vals.append("%d %d" % (i, want[i]))
        (HERE / "bench_suites" / ("random_%d_values.txt" % seed)).write_text("\n".join(vals) + "\n")
        sets = []
        for i in sorted(set(base) | set(want)):
            b, w = base.get(i, default[i]), want.get(i, default.get(i, 0))
            if b != w:
                sets.append("set 1 %d %d" % (i, w))
        for cores in ("2C", "1C"):
            for k in range(1, KEYS + 1):
                out += ["test R%d_%s_N%02d  random seed %d, %s, %d keys" % (seed, cores, k, seed, "two cores" if cores == "2C" else "one core", k),
                        'load patch 1 #03 "SAW BASS"', *sets,
                        "tune core1 off" if cores == "1C" else "tune core1 on", "mods SENDS OFF",
                        "at 0.0  chord " + " ".join(NOTES[:k]) + " vel 100",
                        "at 5.0  release", "tail 1", "measure 1.0 5.0", "expect report", ""]
    (HERE / "bench_suites" / "LRRY-RANDOM.txt").write_text("\n".join(out) + "\n")
    print("LRRY-RANDOM:", len(SEEDS) * 2 * KEYS, "tests;", len(SEEDS), "seeds")


if __name__ == "__main__":
    main()
