#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 OSCILL/R (L/RRY). Part of L/RRY, a modified version of Felucca.
"""Reads the results.csv of a LRRY-COST run and prints the cost of each feature per voice: the slope of the CPU average over 1, 4 and 8 keys on one core (percent of a half = of a block;
one block is 725 us), as microseconds a block, and against the plain #16 TEST SAW. Standard library only.   python3 tools/lrry_cost_report.py RUN_FOLDER"""
import csv
import re
import sys

avg = {}
for r in csv.DictReader(open(sys.argv[1].rstrip("/") + "/results.csv")):
    m = re.match(r"C_(.+)_N(\d+)$", r["test"])
    if m:
        avg.setdefault(m[1], {})[int(m[2])] = float(r["avg"])
BLK = 725.0
base = None
print(f"{'feature':<14}{'N1':>7}{'N4':>7}{'N8':>7}   per voice %  (N4 to N8)   us a block   vs BASE (us)")
for f, d in avg.items():
    if not all(k in d for k in (1, 4, 8)):
        continue
    pv = (d[8] - d[4]) / 4.0
    if base is None:
        base = pv
    print(f"{f:<14}{d[1]:7.1f}{d[4]:7.1f}{d[8]:7.1f}   {pv:8.1f}                {pv * BLK / 100:6.0f}       {(pv - base) * BLK / 100:+6.0f}")
