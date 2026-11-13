#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 OSCILL/R (L/RRY). Part of L/RRY, a modified version of Felucca.
"""L/RRY quick bench (PHASE62): clean voices per patch on one and on two cores in a few minutes, from the unit's own counters. No recording, no loud audio needed (MASTER low).

  python3 tools/bench_quick.py                      one pass (about 6 minutes), results line by line into bench_runs/quick/<time>/results.csv
  python3 tools/bench_quick.py --summary DIR        the table of a (running or finished) pass; DIR as printed at the start
  python3 tools/bench_quick.py --vs DIR_OR_CSV      compare with an earlier pass (summary.csv); the default is the PHASE60 numbers (tools/bench_suites/quick_baseline.csv)
  python3 tools/bench_quick.py --only 16,06 --cores 1   some patches / one core mode;   --keys 1,2,4,8   other key counts;   --hold 2.0   seconds the keys are held
  python3 tools/bench_quick.py --save-baseline      also write the table as tools/bench_suites/quick_baseline.csv

Each test: `bench reset`, load the patch, `tune core1 on|off`, mods SENDS OFF, hold N keys (chord, velocity 100), the CPU window is the last part. A test is CLEAN when the unit
counted no missed half over the whole run (the note-on burst included: `status` audio_late before and after) and no shed voice. Key counts go up one after the other and stop
after the first test that is not clean ("clean voices" = the last count below it). The cost per voice and the fixed cost per block are a straight line through the averages of the
clean tests (per block of 32 samples = a quarter of a half; one half = 2902 us): avg percent x 7.25 us. Needs a bench build (bench9 or newer); the unit alone on the port.
"""
import argparse
import csv
import datetime
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bench_run as br  # noqa: E402
import bench_testpatch  # noqa: E402

# 2026-11-02: no test set in the unit. Replaced by the nearest of the 128 (number = SOUNDS_128.md): STEREO TEST -> #96 STEREO KEYS, WIDE PAD -> #60 STRING MACH,
# COMB PLUCK -> #67 HARP PLUCK. TEST SAW (base #03 SAW BASS) and FULL STRESS (base #28 SUPER LEAD) are no sound of the 128: their values are sent by the tool
# (tools/bench_testpatch.py), never stored. Rows of bench_suites/quick_baseline.csv for the old numbers 16, 14, 6, 12, 7 are stale: measure again.
PATCHES = [(3, "TEST SAW"), (96, "STEREO KEYS"), (60, "STRING MACH"), (28, "FULL STRESS"), (67, "HARP PLUCK")]
KEYS = [1, 2, 4, 6, 8, 12]
NOTES = "C3 E3 G3 B3 D4 F4 A4 C5 E5 G5 B5 D6".split()
BLOCK_US_PER_PCT = 725.0 / 100.0
BASELINE = os.path.join(HERE, "bench_suites", "quick_baseline.csv")
COLS = ["test", "patch", "name", "cores", "keys", "clean", "avg", "peak", "miss", "run_late", "run_shed", "cost_us", "why"]


def suite_text(num, name, cores, n, hold, win=None):
    return (f"suite QUICK\n\ntest Q{num:02d}_{cores}C_N{n:02d}  #{num} {name}, {cores} core(s), {n} keys\n"
            + "\n".join(bench_testpatch.suite_lines(name) if name in ("TEST SAW", "FULL STRESS") else [f'load patch 1 #{num:02d} "{name}"']) + f'\ntune core1 {"on" if cores == 2 else "off"}\nmods SENDS OFF\n'
            f"at 0.0  chord {' '.join(NOTES[:n])} vel 100\nat {hold:.1f}  release\ntail 0.4\nmeasure {win[0] if win else min(0.7, hold - 1.0):.2f} {win[1] if win else hold:.2f}\nexpect report\n")


def fit(points):
    """points [(keys, avg_percent)] -> (cost per voice, fixed) in us per block, least squares"""
    if len(points) < 2:
        return (None, None)
    n = len(points)
    sx = sum(k for k, _ in points)
    sy = sum(a * BLOCK_US_PER_PCT for _, a in points)
    sxx = sum(k * k for k, _ in points)
    sxy = sum(k * a * BLOCK_US_PER_PCT for k, a in points)
    d = n * sxx - sx * sx
    if not d:
        return (None, None)
    b = (n * sxy - sx * sy) / d
    return (b, (sy - b * sx) / n)


def read_rows(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def summarize(rows):
    """{(patch, cores): dict(clean, bad, cost, fixed)} from the result rows"""
    out = {}
    for r in rows:
        out.setdefault((int(r["patch"]), int(r["cores"])), {"rows": []})["rows"].append(r)
    for g in out.values():
        clean, pts, bad = 0, [], None
        for r in sorted(g["rows"], key=lambda r: int(r["keys"])):
            if str(r["clean"]) == "1" and bad is None:
                clean = int(r["keys"])
                pts.append((int(r["keys"]), float(r["avg"])))
            elif bad is None:
                bad = int(r["keys"])
        g["clean"], g["bad"] = clean, bad
        g["cost"], g["fixed"] = fit(pts)
        if g["cost"] is None and pts:
            g["cost"] = pts[0][1] * BLOCK_US_PER_PCT      # one clean point only: its whole cost
    return out


def fmt_table(rows, vs=None, title=""):
    s = summarize(rows)
    base = {}
    if vs:
        for r in read_rows(vs):
            base[(int(r["patch"]), int(r["cores"]))] = r
    names = dict(PATCHES)
    order = [p[0] for p in PATCHES]
    lines = [title, f"{'patch':<18}{'1 core':>8}{'2 cores':>9}{'us/voice':>10}{'fixed us':>10}    vs baseline: clean 1c / 2c, us/voice"]

    def cell(g):
        return "-" if not g else (f"{g['clean']}" + (f"(<{g['bad']})" if g["bad"] else "+"))

    def diff(now, was):
        return "?" if not now or not was or was.get("clean") in (None, "") else f"{now['clean'] - int(was['clean']):+d}"
    for num in sorted({k[0] for k in s}, key=lambda n: order.index(n) if n in order else 99):
        a, b = s.get((num, 1)), s.get((num, 2))
        cost = (a or b or {}).get("cost")
        fixed = (b or {}).get("fixed") or (a or {}).get("fixed")
        cmp_ = ""
        if base:
            b1, b2 = base.get((num, 1)), base.get((num, 2))
            cmp_ = f"{diff(a, b1)} / {diff(b, b2)}"
            if b1 and b1.get("cost") and cost:
                cmp_ += f", {cost - float(b1['cost']):+.0f} ({float(b1['cost']):.0f} -> {cost:.0f})"
        lines.append(f"#{num:02d} {names.get(num, ''):<13}{cell(a):>8}{cell(b):>9}{('%.0f' % cost) if cost else '-':>10}{('%.0f' % fixed) if fixed else '-':>10}    {cmp_}")
    lines.append("clean voices: highest key count with no missed half over the whole run, all lower counts clean too; (<N) = first count that failed, + = none failed up to the last count tried.")
    lines.append("us/voice and fixed: us per block of 32 samples (725 us = the whole block), straight line through the clean tests' average load.")
    return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description="L/RRY quick bench")
    ap.add_argument("--port")
    ap.add_argument("--out")
    ap.add_argument("--only", help="patch numbers, comma separated (default 16,14,06,12,07)")
    ap.add_argument("--cores", help="1, 2 or 1,2 (default 2,1)")
    ap.add_argument("--keys", help="key counts, comma separated")
    ap.add_argument("--hold", type=float, default=2.0, help="seconds the keys are held (default 2.0)")
    ap.add_argument("--win", help="CPU window A,B seconds (default 0.7,hold); 0.1,0.5 for decaying sounds like #67 HARP PLUCK, whose first 0.8 s are the load")
    ap.add_argument("--summary", metavar="DIR")
    ap.add_argument("--vs", metavar="DIR_OR_CSV")
    ap.add_argument("--save-baseline", action="store_true")
    a = ap.parse_args(argv)
    vs = BASELINE
    if a.vs:
        vs = os.path.join(a.vs, "summary.csv") if os.path.isdir(a.vs) else a.vs
    if a.summary:
        print(fmt_table(read_rows(os.path.join(a.summary, "results.csv")), vs if os.path.exists(vs) else None, a.summary))
        return 0
    want = [int(x) for x in a.only.split(",")] if a.only else None
    pats = [(n, nm) for n, nm in PATCHES if not want or n in want]
    cores = [int(x) for x in (a.cores or "2,1").split(",")]
    keys = [int(x) for x in a.keys.split(",")] if a.keys else KEYS
    port = a.port or br.pick_port(argparse.Namespace(port=None))
    out = a.out or os.path.join("bench_runs", "quick", datetime.datetime.now().strftime("%Y-%m-%d_%H%M%S"))
    os.makedirs(out, exist_ok=True)
    log = open(os.path.join(out, "console.log"), "a")
    link = br.SerialLink(port)
    for _ in range(20):
        try:
            link.open()
            break
        except OSError:
            time.sleep(1)
    unit = br.Unit(link, log, link.open)
    info = [x for x in unit.lines_of("bench info") if x.startswith("BENCH,INFO")]
    if not info:
        raise SystemExit("the unit does not answer `bench info`: a bench build is needed, nothing else may use the port")
    version = info[0].split(",")[2]
    st = unit.kv("status")
    base = {"cores": "2", "boots": st.get("boots", 0), "uac_under": 0, "uac_over": 0, "uac_missed": 0, "uac_stalls": 0}
    rc = br.RecCfg(onset=True)
    csvp = os.path.join(out, "results.csv")
    print(f"quick bench on {version}; {len(pats)} patches x cores {cores} x keys {keys}; results: {csvp}", flush=True)
    t_all = time.monotonic()
    rows = []
    with open(csvp, "w", newline="") as f:
        w = csv.DictWriter(f, COLS)
        w.writeheader()
        for num, name in pats:
            for c in cores:
                for n in keys:
                    _, tests = br.parse_suite(suite_text(num, name, c, n, a.hold, tuple(float(x) for x in a.win.split(',')) if a.win else None))
                    tests[0].settle = 0.4                      # (the scene needs no long wait: no recording)
                    t0 = time.monotonic()
                    try:
                        r = br.run_test(unit, tests[0], None, dict(base), None, None, out, rc)
                    except (br.LinkDown, OSError, RuntimeError) as e:
                        r = {"why": f"ERROR {e}"}
                    miss, late, shed = int(r.get("miss") or 0), int(r.get("run_late") or 0), int(r.get("run_shed") or 0)
                    ok = "avg" in r and not miss and not late and not shed and r.get("status") not in ("ERROR", "SKIPPED")
                    avg = float(r.get("avg") or 0)
                    row = {"test": tests[0].name, "patch": num, "name": name, "cores": c, "keys": n, "clean": int(ok), "avg": f"{avg:.1f}", "peak": r.get("peak", ""),
                           "miss": miss, "run_late": late, "run_shed": shed, "cost_us": f"{avg * BLOCK_US_PER_PCT:.0f}", "why": str(r.get("why", ""))[:80].replace(",", ";")}
                    w.writerow(row)
                    f.flush()
                    rows.append(row)
                    print(f"#{num:02d} {c}C {n:2d} keys  avg {avg:5.1f}  peak {str(r.get('peak', '')):>5}  late {late}  {'clean' if ok else 'NOT CLEAN'}  ({time.monotonic() - t0:.1f} s)", flush=True)
                    if not ok:
                        break
    try:
        unit.cmd("bench end")
    except (br.LinkDown, OSError):
        pass
    table = fmt_table(rows, vs if os.path.exists(vs) else None, f"quick bench {version}, {time.monotonic() - t_all:.0f} s")
    print("\n" + table)
    with open(os.path.join(out, "summary.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["patch", "cores", "clean", "cost", "fixed"])
        for (p, c), g in sorted(summarize(rows).items()):
            w.writerow([p, c, g["clean"], "" if g["cost"] is None else f"{g['cost']:.0f}", "" if g["fixed"] is None else f"{g['fixed']:.0f}"])
    with open(os.path.join(out, "summary.txt"), "w") as f:
        f.write(table + "\n")
    if a.save_baseline:
        import shutil
        shutil.copy(os.path.join(out, "summary.csv"), BASELINE)
    return 0


if __name__ == "__main__":
    sys.exit(main())
