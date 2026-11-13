#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 OSCILL/R (L/RRY). Part of L/RRY, a modified version of Felucca.
"""Reads the results.csv of a LRRY-POLY run (tools/bench_run.py --suite LRRY-POLY [--record]) and prints, for each patch and for two cores / one core, the largest number of keys
that stays clean (every count from 1 up to it: no missed half, no click or gap in the recording when there is one, no pause / silent block of core 1), with the CPU average and peak
at that count. Standard library only.   python3 tools/lrry_poly_report.py RUN_FOLDER"""
import csv
import re
import sys

rows = list(csv.DictReader(open(sys.argv[1].rstrip("/") + "/results.csv")))
by = {}
for r in rows:
    m = re.match(r"P(\d+)_(2C|1C)_N(\d+)", r["test"])
    if m:
        by.setdefault((int(m[1]), m[2]), {})[int(m[3])] = r


def num(r, k):
    try:
        return int(float(r.get(k) or 0))
    except ValueError:
        return 0


def clean(r):
    if r["status"] and r["status"] not in ("ok", "OK", "") and "link" in (r.get("why") or ""):
        return False
    if num(r, "miss") or num(r, "run_late") or num(r, "tcsilent") or num(r, "tcpauses") or num(r, "tcrefused"):
        return False
    if "clicks" in r and (num(r, "clicks") or num(r, "gaps")):
        return False
    return r.get("verdict") != "FAIL" or (r.get("why") or "").startswith("USB audio rate")


print("patch  cores  clean keys  avg / peak at that count   first failing count (avg / peak, why)")
for (p, c), d in sorted(by.items()):
    n = 0
    while (n + 1) in d and clean(d[n + 1]):
        n += 1
    at = d.get(n)
    bad = d.get(n + 1)
    print(f"#{p:02d}    {c}     {n:2d}          " + (f"{at['avg']} / {at['peak']}" if at else "-") + "      " +
          (f"{n + 1} ({bad['avg']} / {bad['peak']}, {(bad.get('why') or '')[:60]})" if bad else "none failed"))
