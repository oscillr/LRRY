#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 OSCILL/R (L/RRY). Part of L/RRY, a modified version of Felucca.
"""L/RRY PATCH LINT: does every factory sound follow the owner's sound design rule?

    python3 tools/patch_lint.py                 print the table of all 128 sounds, the summaries, the violators; exit 1 if a sound breaks a rule
    python3 tools/patch_lint.py --md FILE       also write the report (PATCH_COST_LINT.md) to FILE
    python3 tools/patch_lint.py --quiet         only the summary and the verdict (for a build script)

The owner's rule: heavy features are for LEAD and BASS sounds only; KEYS, PAD, PLUCK must keep enough voices.
THE TABLES YOU MAY CHANGE are the first two blocks below (targets, heavy features). Host only: it builds the real firmware/src/budget.c with the host
simulator in a temporary folder (tools/patch_lint_host.c), loads the 128 sounds of tools/lrry_sounds_128.py the way the unit does, and prints the model's
numbers. The model's costs are ESTIMATES (PHASE62_VOICE_COST_REPORT.md): nothing here is measured on the unit. Needs a C compiler (cc) only.
The tool never edits a sound.
"""
import argparse
import itertools
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# ------------------------------------------------------------------------------------------------------------------------------------------ TABLE 1: targets
# voices a sound must keep on TWO cores in the model. min: below it the sound FAILS (exit 1); ideal: below it the sound is marked NOT IDEAL (no exit code).
# A category that is not listed has no target (LEAD, BASS, FX, PERC, OTHER).
TARGETS = {"KEYS": (6, 10), "PAD": (6, 10), "PLUCK": (6, 10)}
# the categories that may use heavy features. PERC is outside all checks (owner: irrelevant)
HEAVY_ALLOWED = {"LEAD", "BASS", "PERC"}

# ------------------------------------------------------------------------------------------------------------------------------------------ TABLE 2: heavy features
# key: what the sound uses. cost_us: the host cost tool tests/lrry_voice_cost_test.c (voice kernel after round one, us a block, over a plain SAW voice; host figures, they rank).
# heavy: True = counts as heavy (change it to move the line). cheap: the cheap equivalent the lint proposes and its cost; save_us = cost_us - cheap cost (cost tool).
HEAVY = {
    "TWIN LADDER": dict(heavy=True, cost_us=50.7, cheap="TWIN SVF LP", cheap_us=21.2),
    "COMB": dict(heavy=True, cost_us=20.9, cheap="TWIN OFF (plus no note-on burst, about 21 us a voice at the peak)", cheap_us=0.0),
    "SUPER 4+ SAWS": dict(heavy=True, cost_us=7.0, cheap="SUPER 3 SAWS", cheap_us=0.0),   # cost_us here is per saw; the lint charges the saws above 3
    "FILTER A 24 dB": dict(heavy=True, cost_us=47.1, cheap="FILTER A 12 dB", cheap_us=25.7),
    "OSC MODEL FM": dict(heavy=True, cost_us=7.1, cheap="OSC MODEL SAW", cheap_us=0.0),   # any oscillator model above about 6 us over a plain saw (SINE 1.6, PULSE 1.4, SYNC 2.2, FOLD 1.4: no)
    "DIRT CRUSH": dict(heavy=True, cost_us=15.6, cheap="DIRT SOFT", cheap_us=13.7),      # any dirt type above 15 us over a plain saw (SOFT 13.7, HARD 8.8, FOLD 11.7, FUZZ 12.3: no)
    "TWIN SVF HP": dict(heavy=False, cost_us=27.4, cheap="TWIN SVF LP", cheap_us=21.2),   # costly, but not heavy by default (as filter A 12 dB with the blend: 25.7)
}
MODEL_NAMES = ["SAW", "TRI", "SINE", "PULSE", "SYNC", "SUPER", "FM", "FOLD"]
TW_NAMES = ["OFF", "LP", "BP", "HP", "NOTCH", "LADDER", "COMB"]
DIRT_NAMES = ["OFF", "SOFT", "HARD", "FOLD", "FUZZ", "CRUSH"]

# ------------------------------------------------------------------------------------------------------------------------------------------ the model (budget.c mirrored)
# rows in tenths of a microsecond, budget.c BD_LR_*; the sound's voice is the sum of the rows of what it uses (bd_lrry_voice)
ROWS = dict(BASE=320, OSC2=80, NOISE=20, RING=140, FA12=370, FA24=570, SVF=370, LAD=530, COMB=70, DIRT=110, SUB=270, FBMOD=20, MODEL=[0, 0, 10, 30, 50, 0, 70, 10])
# "with the savings of round one": each row times (cost tool, kernel after round one / before round one, host us over a plain SAW; measured 2026-11-02 with the old lrry_voice.c of
# 9acbe5d~1 against the one of merged-all). Rows that did not get cheaper stay as they are. Host instructions rank, they are not the unit's microseconds.
SAVE_RATIO = dict(FA12=25.75 / 29.87, FA24=47.07 / 55.40, SVF=0.875, LAD=50.68 / 70.99, DIRT=0.81, SUB=14.81 / 16.17)
# ------------------------------------------------------------------------------------------------------------------------------------------ TABLE 3: modulation load of a voice
# budget.c charges a matrix slot BD_MX_SLOT = 1.77 us once a PART (not a voice) and the moving pans nothing at all. The lint adds a charge per VOICE (the real work is per voice and block):
MOD_ROUTE_US = 0.8     # a route (matrix slot or fixed route) per voice (the owner's figure; report 8.2: matrix 1.1 us a key on a plain patch)
MOD_PAN_US = 1.8       # a MOVING pan per voice: cost tool, all 9 pan stages moving against steady (ALL moving 172.0 - ALL steady 155.7 = 16.3 host us over 9 pans)
UNIT_US = 0.0725        # one unit (1/100 % of a 725 us block) in us
LIMIT_PCT_NOTE = "83.1 % peak limit less 2 points margin = 81.1 % (budget.c LRRY_CP_LIM, two cores)"


def bd_u(i, m=100):
    return (i * 64 * m + 2099) // 2100


def bd_t(t10):
    return (t10 * 4526 + 5000) // 10000


def saws(p2):
    return 1 + (min(max(p2, 0), 127) * 6 + 63) // 127


def osc_row(rows, model, nsaw):
    if model != 5:
        return rows["MODEL"][model]
    return 90 * nsaw - 70


def voice_t10(f, rows):
    """f: features dict. The sum of bd_lrry_voice, in tenths of a us."""
    c = rows["BASE"] + osc_row(rows, f["o1m"], f["o1saw"])
    if f["o2on"]:
        c += rows["OSC2"] + osc_row(rows, f["o2m"], f["o2saw"])
    c += rows["NOISE"] if f["nz"] else 0
    c += rows["RING"] if f["rg"] else 0
    c += rows["FA24"] if f["sl"] == 2 else rows["FA12"] if f["sl"] else 0
    if f["tw"]:
        c += rows["LAD"] if f["tw"] == 5 else rows["COMB"] if f["tw"] == 6 else rows["SVF"]
        c += rows["FBMOD"] * f["fbmod"]
    c += (rows["DIRT"] if f["da"] else 0) + (rows["DIRT"] if f["db"] else 0)
    c += rows["SUB"] if f["sub"] else 0
    return c


def scaled_rows():
    r = dict(ROWS)
    for k, v in SAVE_RATIO.items():
        r[k] = int(round(ROWS[k] * v))
    return r


class Model:
    """budget.c bd_lr_peak and budget_solve2 for one part (two cores), and the pipelined variant."""

    def __init__(self, const):
        self.k = const

    def split(self, c, fx, g, k, bst=0, bst0=0, pipe=False):
        K = self.k
        cu = c
        ccu = min(K["ctl"], cu)
        au = cu - ccu
        n1, n0 = k // 2, k - k // 2
        if pipe and k >= 2:
            # PIPELINED (derivation in the report): the serial rest (globals + part items, g + fx) of block b runs on core 1 while core 0 renders the voices of block b + 1.
            # Per block: core 0 = hand-over + every voice's control + its share of the audio; core 1 = its head + its share + the serial rest. The split n0 + n1 = k is the best one.
            best = None
            for a in range(0, k + 1):
                c0 = K["h0"] + k * ccu + a * au
                c1 = K["h1"] + (k - a) * au + g + fx
                m = max(c0, c1)
                if best is None or m < best[0]:
                    best = (m, k - a)
            cp, nb = best[0] + K["j2"], best[1]
            nb = k - nb if False else n0   # (the burst voices stay with core 0's count, as budget.c)
        else:
            cs = K["h0"] + k * ccu + max(n0 * au, K["h1"] + n1 * au)
            if k >= 2 and cs < k * cu:
                cp, nb = g + fx + cs + K["j2"], n0
            else:
                cp, nb = g + fx + k * cu + K["j2"], k
        return cp + (bst0 + nb * bst if bst else 0)

    def voices(self, c, fx, g, cap, bst=0, bst0=0, pipe=False, lim=None):
        lim = self.k["lim2"] if lim is None else lim
        for k in range(cap, 1, -1):
            if self.split(c, fx, g, k, bst, bst0, pipe) <= lim:
                return k
        return 1

    def need_c(self, k, fx, g, pipe):
        """the largest voice cost c (units) that still gives k voices at the limit (no burst)."""
        lo, hi = 0, 20000
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if self.split(mid, fx, g, k, pipe=pipe) <= self.k["lim2"]:
                lo = mid
            else:
                hi = mid - 1
        return lo


# ------------------------------------------------------------------------------------------------------------------------------------------ build and run the host harness
def build_and_run(base):
    tmp = Path(tempfile.mkdtemp(prefix="patch_lint_"))
    try:
        gen = tmp / "gen"
        gen.mkdir()
        for s, o in [("gen_version.py", "lrry_version.h"), ("gen_tables.py", "lrry_tables.h"), ("gen_fm6_patches.py", "lrry_fm6.h"), ("gen_lrry_sounds.py", "lrry_sounds.h"),
                     ("gen_lrry_multis.py", "lrry_multis.h"), ("gen_midi_map.py", "midi_map_defaults.h")]:
            r = subprocess.run([sys.executable, "tools/" + s, str(gen / o)], cwd=base, capture_output=True, text=True)
            if r.returncode:
                sys.exit(f"patch_lint: generator {s} failed:\n{r.stdout}{r.stderr}")
        ver = "x"
        for ln in (gen / "lrry_version.h").read_text().splitlines():
            if ln.startswith("#define LRRY_VERSION_STR"):
                ver = ln.split('"')[1]
        exe = tmp / "lint"
        cmd = ["cc", "-O2", "-w", "-DHOST_MIX_FOLLOWS_MASTER=1", "-DLRRY_TEST_MODE=1", f"-I{gen}", "-Ifirmware/src", "-Itests", "-DNPART=1", "-DNSHOWN=1", f"-DLRRY_VERSION_SHOWN=\"{ver}\"",
               "-o", str(exe), "tools/patch_lint_host.c", "-lm"]
        r = subprocess.run(cmd, cwd=base, capture_output=True, text=True)
        if r.returncode:
            sys.exit("patch_lint: the host harness does not compile:\n" + r.stderr[-1500:])
        r = subprocess.run([str(exe)], cwd=base, capture_output=True, text=True)
        if r.returncode:
            sys.exit("patch_lint: the host harness failed:\n" + r.stderr[-800:])
        return r.stdout
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def parse(out):
    const, rows = None, []
    for ln in out.splitlines():
        a = ln.split("|")
        if a[0] == "CONST":
            const = {kv.split("=")[0]: float(kv.split("=")[1]) for kv in a[1:]}
            const = {k: (int(v) if k != "us_per_unit" else v) for k, v in const.items()}
        elif a[0] in ("SND", "REFSTEADY", "REFMOVING"):
            d = dict(tag=a[0], browse=int(a[1]), idx=int(a[2]), cat=a[3], name=a[4])
            for kv in a[5:]:
                k, v = kv.split("=")
                d[k] = int(v)
            rows.append(d)
    const["lim1"], const["lim2"] = const.pop("lim1"), const.pop("lim2")
    return const, rows


def features(d):
    return dict(o1m=d["o1m"], o1saw=saws(d["o1p2"]), o2on=d["o2on"], o2m=d["o2m"], o2saw=saws(d["o2p2"]), nz=d["nz"], rg=d["rg"] > 0, sl=d["sl"], tw=d["tw"], fbmod=d["fbmod"],
                da=d["da"], db=d["db"], sub=d["sub"] > 0)


def heavy_of(f):
    h = []
    if f["tw"] == 5:
        h.append("TWIN LADDER")
    if f["tw"] == 6:
        h.append("COMB")
    if f["o1m"] == 5 and f["o1saw"] >= 4 or (f["o2on"] and f["o2m"] == 5 and f["o2saw"] >= 4):
        h.append("SUPER 4+ SAWS")
    if f["sl"] == 2:
        h.append("FILTER A 24 dB")
    if f["o1m"] == 6 or (f["o2on"] and f["o2m"] == 6):
        h.append("OSC MODEL FM")
    if f["da"] == 5 or f["db"] == 5:
        h.append("DIRT CRUSH")
    if f["tw"] == 3:
        h.append("TWIN SVF HP")
    return [x for x in h if HEAVY[x]["heavy"]]


def uses_label(f):
    s = []
    if f["o1m"] == 5:
        s.append(f"O1 SUPER{f['o1saw']}")
    if f["o2on"] and f["o2m"] == 5:
        s.append(f"O2 SUPER{f['o2saw']}")
    return s


def fix_heavy(f, which):
    """the feature set with the cheap equivalents of the heavy features in `which` applied."""
    g = dict(f)
    for w in which:
        if w == "TWIN LADDER":
            g["tw"] = 1
        elif w == "COMB":
            g["tw"] = 0
            g["fbmod"] = 0
        elif w == "SUPER 4+ SAWS":
            if g["o1m"] == 5 and g["o1saw"] >= 4:
                g["o1saw"] = 3
            if g["o2on"] and g["o2m"] == 5 and g["o2saw"] >= 4:
                g["o2saw"] = 3
        elif w == "FILTER A 24 dB":
            g["sl"] = 1
        elif w == "OSC MODEL FM":
            if g["o1m"] == 6:
                g["o1m"] = 0
            if g["o2on"] and g["o2m"] == 6:
                g["o2m"] = 0
        elif w == "DIRT CRUSH":
            g["da"] = 1 if g["da"] == 5 else g["da"]
            g["db"] = 1 if g["db"] == 5 else g["db"]
        elif w == "TWIN SVF HP":
            g["tw"] = 1
    return g


# optional cuts (not heavy features: parts of "everything active") the lint may propose, only when the heavy fixes are not enough for the minimum. Each: label, function
OPTIONAL = [
    ("SUB off", lambda g: g.update(sub=False)),
    ("OSC 2 off", lambda g: g.update(o2on=0, rg=False)),
    ("RING off", lambda g: g.update(rg=False)),
    ("NOISE off", lambda g: g.update(nz=0)),
    ("DIRT B off", lambda g: g.update(db=0)),
    ("DIRT A off", lambda g: g.update(da=0)),
    ("TWIN modulation off", lambda g: g.update(fbmod=0)),
    ("FILTER A off", lambda g: g.update(sl=0)),
    ("TWIN off", lambda g: g.update(tw=0, fbmod=0)),
    ("OSC 1 SUPER 3 saws", lambda g: g.update(o1saw=min(g["o1saw"], 3)) if g["o1m"] == 5 else None),
    ("OSC 2 SUPER 3 saws", lambda g: g.update(o2saw=min(g["o2saw"], 3)) if g["o2m"] == 5 else None),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--md", help="write the report to this file")
    ap.add_argument("--quiet", action="store_true", help="summary and verdict only")
    args = ap.parse_args()
    base = Path(__file__).resolve().parent.parent
    const, rows = parse(build_and_run(base))
    M = Model(const)
    sav = scaled_rows()
    snd = [r for r in rows if r["tag"] == "SND"]
    refS = [r for r in rows if r["tag"] == "REFSTEADY"][0]
    refM = [r for r in rows if r["tag"] == "REFMOVING"][0]
    snd.sort(key=lambda r: r["browse"])

    def evaluate(d, f=None, check=False):
        """c in us, the model's counters and the variants for the sound d (features f replaced if given)."""
        f = f or features(d)
        eng_now = bd_t(voice_t10(f, ROWS))
        eng_sav = bd_t(voice_t10(f, sav))
        if check and eng_now != d["eng"]:
            sys.exit(f"patch_lint: my copy of bd_lrry_voice differs from budget.c for {d['name']} ({eng_now} against {d['eng']}): the tool is out of date with budget.c")
        c_now = bd_u(eng_now + d["filt"] + d["ext"])
        c_sav = bd_u(eng_sav + d["filt"] + d["ext"])
        mod_u = int(round((MOD_ROUTE_US * (d["nmx"] + d["nfx"]) + MOD_PAN_US * d["npm"]) / UNIT_US))
        fx, g, cap = bd_u(d["fx"]), bd_u(d["idle"] + d["gfx"]), d["cap"]
        bst = bd_u(d["bst"]) if f["tw"] == 6 else 0
        bst0 = bd_u(d["bst0"]) if f["tw"] == 6 else 0
        out = dict(c=c_now * UNIT_US, c_sav=c_sav * UNIT_US, mod=mod_u * UNIT_US, cm=(c_now + mod_u) * UNIT_US, cm_sav=(c_sav + mod_u) * UNIT_US)
        out["v1"] = None
        out["v2"] = M.voices(c_now, fx, g, cap, bst, bst0)                      # the firmware's counter (no modulation charge)
        out["v2m"] = M.voices(c_now + mod_u, fx, g, cap, bst, bst0)             # the lint's estimate: modulation charged per voice
        out["pipe"] = M.voices(c_now + mod_u, fx, g, cap, bst, bst0, True)
        out["sav"] = M.voices(c_sav + mod_u, fx, g, cap, bst, bst0)
        out["both"] = M.voices(c_sav + mod_u, fx, g, cap, bst, bst0, True)
        return out

    res = []
    for d in snd:
        f = features(d)
        e = evaluate(d, f, check=True)
        e["v1"] = d["n1"]
        if e["v2"] != d["n2"]:
            sys.exit(f"patch_lint: my copy of the counter differs from budget.c for {d['name']} (two cores {e['v2']} against {d['n2']}): the tool is out of date with budget.c")
        h = heavy_of(f)
        tg = TARGETS.get(d["cat"])
        viol = bool(h) and d["cat"] not in HEAVY_ALLOWED
        miss_min = bool(tg) and e["v2m"] < tg[0]
        not_ideal = bool(tg) and tg[0] <= e["v2m"] < tg[1]
        res.append(dict(d=d, f=f, e=e, heavy=h, viol=viol, miss_min=miss_min, not_ideal=not_ideal, tg=tg))

    # ---- proposals: the smallest set of replacements
    for r in res:
        r["prop"] = None
        if not (r["viol"] or r["miss_min"]):
            continue
        d, f = r["d"], r["f"]
        forced = r["heavy"] if r["viol"] else []
        base_f = fix_heavy(f, forced)
        need = r["tg"][0] if r["tg"] else 0
        e0 = evaluate(d, base_f)
        if not need or e0["v2m"] >= need:
            r["prop"] = (forced, [], e0["v2m"])
            continue
        best = None
        for n in range(1, 4):
            for combo in itertools.combinations(range(len(OPTIONAL)), n):
                g = dict(base_f)
                for i in combo:
                    OPTIONAL[i][1](g)
                if g == base_f:
                    continue
                ee = evaluate(d, g)
                cost = voice_t10(base_f, ROWS) - voice_t10(g, ROWS)
                if ee["v2m"] >= need and (best is None or cost < best[0]):
                    best = (cost, combo, ee["v2m"])
            if best:
                break
        r["prop"] = (forced, [OPTIONAL[i][0] for i in best[1]] if best else ["NOT REACHABLE with the optional cuts"], best[2] if best else e0["v2m"])

    # ---- the two reference all-active voices (steady; all pans moving and 8 routes)
    refs = []
    for ref, label in ((refS, "all active, steady (4 routes, no pan moves)"), (refM, "all active + 9 pans moving + 8 matrix slots and 3 fixed routes")):
        er = evaluate(ref, features(ref), check=True)
        fxr, gr = bd_u(ref["fx"]), bd_u(ref["idle"] + ref["gfx"])
        cases = [("budget.c today (no modulation charge)", er["c"], er["v2"], False), ("today + modulation charge", er["cm"], er["v2m"], False), ("pipelining", er["cm"], er["pipe"], True),
                 ("savings", er["cm_sav"], er["sav"], False), ("pipelining + savings", er["cm_sav"], er["both"], True)]
        gap = []
        for name, c, v, pipe in cases:
            n6, n10 = M.need_c(6, fxr, gr, pipe) * UNIT_US, M.need_c(10, fxr, gr, pipe) * UNIT_US
            gap.append((name, c, v, n6, n10, c - n6, c - n10))
        refs.append((label, er, gap))

    # ---- print
    L = []
    P = L.append
    cats = ["KEYS", "PAD", "PLUCK", "LEAD", "BASS", "FX", "PERC", "OTHER"]
    P("L/RRY PATCH LINT (model: budget.c as built here; two-core limit " + LIMIT_PCT_NOTE + ")")
    P("")
    P("REFERENCE ALL-ACTIVE VOICE: OSC 1 SAW + OSC 2 SAW, NOISE, RING, SUB, filter A 12 dB blend, TWIN SVF LP, both DIRT SOFT, envelopes, 2 LFOs, pans on every source")
    for label, er, gap in refs:
        P(f"  {label}: c {er['c']:.1f} us, modulation charge {er['mod']:.1f} us")
        P(f"  {'case':<38}{'c us':>7}{'voices':>8}{'c for 6':>9}{'c for 10':>10}{'gap to 6':>10}{'gap to 10':>11}   (gap = c less c that fits; positive = too dear)")
        for g_ in gap:
            P(f"  {g_[0]:<38}{g_[1]:>7.1f}{g_[2]:>8}{g_[3]:>9.1f}{g_[4]:>10.1f}{g_[5]:>+10.1f}{g_[6]:>+11.1f}")
        P("")
    P("SUMMARY PER CATEGORY (voices on two cores; min/ideal from TARGETS; 'violate' = heavy feature in a category that may not have one)")
    P(f"{'cat':<6}{'n':>4}{'violate':>8}{'avg v viol':>11}{'avg v ok':>9} | {'meet min: now':>14}{'pipe':>6}{'sav':>6}{'both':>6} | {'ideal: now':>11}{'pipe':>6}{'sav':>6}{'both':>6}")
    summ = {}
    for c in cats:
        rs = [r for r in res if r["d"]["cat"] == c]
        if not rs:
            continue
        v = [r for r in rs if r["viol"]]
        ok = [r for r in rs if not r["viol"]]
        av = lambda xs: (sum(r["e"]["v2m"] for r in xs) / len(xs)) if xs else float("nan")
        tg = TARGETS.get(c)
        if tg:
            mm = [sum(r["e"][k] >= tg[0] for r in rs) for k in ("v2m", "pipe", "sav", "both")]
            ii = [sum(r["e"][k] >= tg[1] for r in rs) for k in ("v2m", "pipe", "sav", "both")]
            ms = "".join(f"{x:>{w}}" for x, w in zip(mm, (14, 6, 6, 6)))
            iss = "".join(f"{x:>{w}}" for x, w in zip(ii, (11, 6, 6, 6)))
        else:
            ms, iss = f"{'no target':>14}{'':>18}", f"{'':>29}"
        summ[c] = (len(rs), len(v), av(v), av(ok), tg and mm, tg and ii)
        P(f"{c:<6}{len(rs):>4}{len(v):>8}{av(v):>11.1f}{av(ok):>9.1f} | {ms} | {iss}")
    nviol = sum(r["viol"] for r in res if r["d"]["cat"] != "PERC")
    nmiss = sum(r["miss_min"] for r in res)
    nni = sum(r["not_ideal"] for r in res)
    P("")
    P(f"RULE VIOLATIONS (heavy feature outside {'/'.join(sorted(HEAVY_ALLOWED))}): {nviol}.  Below the minimum voices (KEYS/PAD/PLUCK): {nmiss}.  Marked NOT IDEAL (min met, ideal not): {nni}.")
    P("")
    if not args.quiet:
        P(f"{'#':>3} {'name':<13}{'cat':<6}{'c us':>6}{'mod':>5}{'1c':>3}{'2c':>3}{'2c+m':>4}{'pipe':>5}{'sav':>4}{'both':>5}  {'rule':<10}{'target':<9} heavy features")
        for r in res:
            d, e = r["d"], r["e"]
            tgs = "-" if not r["tg"] else "FAIL" if r["miss_min"] else "NOT IDEAL" if r["not_ideal"] else "ok"
            rule = "-" if d["cat"] == "PERC" else "VIOLATES" if r["viol"] else "RULE OK"
            P(f"{d['browse']:>3} {d['name']:<13}{d['cat']:<6}{e['c']:>6.1f}{e['mod']:>5.1f}{e['v1']:>3}{e['v2']:>3}{e['v2m']:>4}{e['pipe']:>5}{e['sav']:>4}{e['both']:>5}  {rule:<10}{tgs:<9} {', '.join(r['heavy']) or '-'}")
        P("")
    bad = [r for r in res if (r["viol"] and r["d"]["cat"] != "PERC") or r["miss_min"]]
    P(f"SOUNDS TO FIX ({len(bad)}): proposals only, no sound is edited. 'replace' = the cheap equivalents of the heavy features, 'and also' = extra cuts, only where the minimum is still missed")
    for r in sorted(bad, key=lambda r: (r["e"]["v2m"], r["d"]["browse"])):
        d, e = r["d"], r["e"]
        fo, ex, vn = r["prop"]
        sv = []
        for w in r["heavy"]:
            hh = HEAVY[w]
            if w == "SUPER 4+ SAWS":
                nx = max(r["f"]["o1saw"] if r["f"]["o1m"] == 5 else 0, r["f"]["o2saw"] if r["f"]["o2on"] and r["f"]["o2m"] == 5 else 0)
                sv.append(f"{w} {'/'.join(uses_label(r['f']))} -> {hh['cheap']} (saves about {hh['cost_us'] * (nx - 3):.0f} us)")
            else:
                sv.append(f"{w} -> {hh['cheap']} (saves about {hh['cost_us'] - hh['cheap_us']:.0f} us)")
        why = "heavy: " + "; ".join(sv) if sv else "no heavy feature: " + cost_parts(r["f"], d)
        P(f"#{d['browse']} {d['name']} [{d['cat']}] c {e['c']:.0f}+{e['mod']:.0f} us, {e['v2m']} voices (pipe {e['pipe']}, sav {e['sav']}, both {e['both']}): {why}")
        P(f"      replace: {'; '.join(fo) or '-'}" + (f"; and also: {', '.join(ex)}" if ex else "") + f"  => {vn} voices on two cores")
    P("")
    worst = sorted(bad, key=lambda r: r["e"]["v2m"])[:10]
    P("WORST TEN: " + ", ".join(f"#{r['d']['browse']} {r['d']['name']} ({r['d']['cat']}) {r['e']['v2m']}" for r in worst))
    exitcode = 1 if (nviol or nmiss) else 0
    P(f"VERDICT: {'FAIL' if exitcode else 'PASS'} ({nviol} rule violations, {nmiss} below the minimum voices)")
    text = "\n".join(L)
    print(text)
    if args.md:
        Path(args.md).write_text(make_md(res, refs, summ, nviol, nmiss, nni, worst, bad, text, M))
    return exitcode


def cost_parts(f, d):
    parts = []
    t = lambda v: v / 10.0
    if f["sl"]:
        parts.append(f"filter A {'24' if f['sl'] == 2 else '12'} dB {t(ROWS['FA24'] if f['sl'] == 2 else ROWS['FA12']):.0f} us")
    if f["tw"]:
        parts.append(f"TWIN {TW_NAMES[f['tw']]} {t(ROWS['LAD'] if f['tw'] == 5 else ROWS['COMB'] if f['tw'] == 6 else ROWS['SVF']):.0f} us")
    if f["o1m"] == 5:
        parts.append(f"O1 SUPER{f['o1saw']} {t(osc_row(ROWS, 5, f['o1saw'])):.0f} us")
    if f["o2on"]:
        parts.append(f"OSC 2 {t(ROWS['OSC2'] + osc_row(ROWS, f['o2m'], f['o2saw'])):.0f} us")
    if f["sub"]:
        parts.append(f"SUB {t(ROWS['SUB']):.0f} us")
    if f["da"] or f["db"]:
        parts.append(f"dirt {t(ROWS['DIRT'] * ((1 if f['da'] else 0) + (1 if f['db'] else 0))):.0f} us")
    return ", ".join(parts) + f"; fixed work of the part {bd_u(d['fx']) * UNIT_US:.0f} us"


def make_md(res, refs, summ, nviol, nmiss, nni, worst, bad, text, M):
    K = M.k
    o = []
    P = o.append
    P("# L/RRY patch cost lint (2026-11-02, branch patch-lint)\n")
    P("Run: `python3 fw/lrry/tools/patch_lint.py` (add `--md FILE` to rewrite this report). Exit code 1 when a sound breaks a rule. Host only; nothing measured on the unit.\n")
    P("## 1. The rule and the targets (tables at the top of the tool)\n")
    P("- Heavy features are for LEAD and BASS only (PERC is left out of every check). Heavy = TWIN LADDER, COMB, SUPER with 4 or more saws, filter A 24 dB, oscillator model FM, dirt CRUSH. TWIN SVF HP is costly but not heavy by default.")
    P("- Voices (two cores, the budget model as built in merged-all): KEYS, PAD, PLUCK at least 6 (below: FAIL), ideally 10 (below: NOT IDEAL). LEAD, BASS, FX, PERC: no target.\n")
    P("## 2. The gap for a sound that uses everything (reference all-active voices)\n")
    P("Reference: OSC 1 SAW + OSC 2 SAW, NOISE, RING, SUB on, filter A 12 dB with the MODE blend, TWIN SVF LP, both DIRT units SOFT, envelopes, 2 LFOs, a pan on every source. Two versions: **steady** (4 matrix routes, no pan moves) and **moving** (all 8 matrix slots and 3 fixed routes in use, all 9 pans moving: OSC 1, OSC 2, SUB, NOISE, RING, filter A, filter B, DIRT A, DIRT B, driven by LFO 1, LFO 2, ENV 2, CYCLE, RAND, plus cutoff and level routes). Limit: " + LIMIT_PCT_NOTE + ". Two cores.\n")
    for label, er, gap in refs:
        P(f"**{label}**: voice c {er['c']:.1f} us + modulation charge {er['mod']:.1f} us.\n")
        P("| case | c incl. modulation (us) | voices | c that fits 6 voices (us) | c that fits 10 voices (us) | gap to 6 (us) | gap to 10 (us) |")
        P("|---|---|---|---|---|---|---|")
        for g in gap:
            P(f"| {g[0]} | {g[1]:.1f} | {g[2]} | {g[3]:.1f} | {g[4]:.1f} | {g[5]:+.1f} | {g[6]:+.1f} |")
        P("")
    P("Gap = the voice cost minus the cost that still fits; positive means too dear by that many us a voice per block. `savings` lowers c (round-one kernel savings, rows scaled by the cost tool's ratios), not the limit; `pipelining` changes the formula (section 8), so the c that fits is higher.\n")
    P("## 3. Summary per category (two cores, voices of the model)\n")
    P("| category | sounds | rule violations | avg voices of violators | avg voices of the others | meet min 6: now / pipe / sav / both | meet ideal 10: now / pipe / sav / both |")
    P("|---|---|---|---|---|---|---|")
    for c, (n, v, av, ao, mm, ii) in summ.items():
        f = lambda x: "-" if x is None or x != x else f"{x:.1f}"
        P(f"| {c} | {n} | {v} | {f(av)} | {f(ao)} | {' / '.join(map(str, mm)) if mm else 'no target'} | {' / '.join(map(str, ii)) if ii else 'no target'} |")
    P(f"\nRule violations (outside LEAD, BASS, PERC): **{nviol}**. Below the minimum 6 (KEYS, PAD, PLUCK): **{nmiss}**. NOT IDEAL: {nni}.\n")
    P("## 4. Worst ten (fewest voices among the sounds that need a fix)\n")
    P("| # | name | cat | c + modulation (us) | voices | pipe | sav | both | heavy features |")
    P("|---|---|---|---|---|---|---|---|---|")
    for r in worst:
        d, e = r["d"], r["e"]
        P(f"| {d['browse']} | {d['name']} | {d['cat']} | {e['c']:.0f} + {e['mod']:.0f} | {e['v2m']} | {e['pipe']} | {e['sav']} | {e['both']} | {', '.join(r['heavy']) or '-'} |")
    P("\n## 5. Sounds to fix and the smallest replacements (proposals only, no sound is edited)\n")
    P("For a rule violator the smallest set is every heavy feature replaced by its cheap equivalent (the rule needs all of them gone). Where KEYS, PAD or PLUCK still miss 6 voices after that, the fewest extra cuts (by count, then by least cost removed) are added.\n")
    P("| # | name | cat | c + modulation (us) | voices (pipe / sav / both) | cause | replace | and also | voices after |")
    P("|---|---|---|---|---|---|---|---|---|")
    for r in sorted(bad, key=lambda r: (r["e"]["v2m"], r["d"]["browse"])):
        d, e = r["d"], r["e"]
        fo, ex, vn = r["prop"]
        cause = ", ".join(r["heavy"]) if r["heavy"] else cost_parts(r["f"], d)
        rep = "; ".join(f"{w} to {HEAVY[w]['cheap']}" for w in fo) or "-"
        P(f"| {d['browse']} | {d['name']} | {d['cat']} | {e['c']:.0f} + {e['mod']:.0f} | {e['v2m']} ({e['pipe']} / {e['sav']} / {e['both']}) | {cause} | {rep} | {', '.join(ex) or '-'} | {vn} |")
    P("\nSaving per heavy feature (cost tool, voice kernel after round one, host us a block; the counters above use the model's own rows):\n")
    P("| heavy feature | cost (us) | cheap equivalent | cheap cost (us) | saving (us) |")
    P("|---|---|---|---|---|")
    for k, h in HEAVY.items():
        unit = " per saw (7 saws to 3: about 28 us per oscillator)" if k.startswith("SUPER") else ""
        sv = (h["cost_us"] * 4) if k.startswith("SUPER") else h["cost_us"] - h["cheap_us"]
        P(f"| {k}{'' if h['heavy'] else ' (not heavy)'} | {h['cost_us']:.1f}{unit} | {h['cheap']} | {h['cheap_us']:.1f} | {sv:.1f} |")
    P("\nCOMB: replacing it by TWIN OFF saves its voice cost and the note-on burst; replacing it by an SVF would not lower the model's voice (the model charges COMB 7 us, SVF 37 us) but removes the burst. DIRT CRUSH to SOFT changes the counter by nothing (the model charges every dirt unit the same 11 us).\n")
    P("## 6. All sounds by category\n")
    for c in ["KEYS", "PAD", "PLUCK", "LEAD", "BASS", "FX", "PERC", "OTHER"]:
        rs = [r for r in res if r["d"]["cat"] == c]
        if not rs:
            continue
        P(f"### {c}\n")
        P("| # | name | c (us) | modulation (us) | 1 core (budget.c) | 2 cores (budget.c) | 2 cores with modulation | pipe | sav | both | heavy features | rule | target |")
        P("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
        for r in rs:
            d, e = r["d"], r["e"]
            tgs = "-" if not r["tg"] else "FAIL" if r["miss_min"] else "NOT IDEAL" if r["not_ideal"] else "ok"
            rule = "-" if c == "PERC" else "VIOLATES" if r["viol"] else "RULE OK"
            P(f"| {d['browse']} | {d['name']} | {e['c']:.1f} | {e['mod']:.1f} | {e['v1']} | {e['v2']} | {e['v2m']} | {e['pipe']} | {e['sav']} | {e['both']} | {', '.join(r['heavy']) or '-'} | {rule} | {tgs} |")
        P("")
    sv = [r["e"]["c"] - r["e"]["c_sav"] for r in res]
    P("## 7. Does the budget model charge for modulation? (answer: too little)\n")
    P("- **Routes:** budget.c charges every matrix slot with SRC, DST and AMOUNT set BD_MX_SLOT = 8 units = 1.8 us, once per PART, not per voice, and 1.1 us per LFO a slot reads (BD_LFO). The fixed routes (CYCLE, LFO 2 to filter A PAN, and so on) are not charged. The measured per-voice work (report 8.2: envelope 2.3, matrix 1.1, control 0.8 us a key) sits inside BD_LR_BASE = 32 us, measured on TEST SAW with a few routes. With 8 slots the model charges 14 us per part whatever the voice count; per voice at 0.8 us a slot the work is 6.4 us times the voices (4 voices 26 us, 8 voices 51 us). So: about right at 2 voices, too little from 4 up.")
    P("- **Moving pans: not charged at all.** budget.c says so itself (\"Pan ramps (+27 in the report, only with ramping pans) are not charged\"). Cost tool, all nine pan stages moving against the same voice steady (OSC 1, OSC 2, SUB, NOISE, RING, filter A, filter B, DIRT A, DIRT B): 172.0 against 155.7 host us per voice and block = +16.3 us, about 1.8 us a pan (host instructions; the SAW ratio unit/host is about 1.1, so about 18 us on the unit). That is 9 % of a 183 us all-active voice that the model leaves out. The lint adds it per voice (TABLE 3: 0.8 us a route, 1.8 us a moving pan).")
    P("- **Does it match the report's +27 us?** No, and the report's reading looks wrong. The +27 is the unit's figure for the kernel-bench line `PAN RAMP (filter A pan)` (49 us) over a plain SAW (22 us): that line is a SAW with filter A 12 dB and one ramping pan, and filter A alone is +26 us in the cost tool (25.8 host us). The ramping pan itself is 48.2 - 45.4 = +2.8 us in the cost tool (one pan stage). So pan ramps cost about 2 to 3 us each, nine of them 16 us, not 27 us a voice for one. Please check on the unit (`bench vk` with a filter A line without and with the pan ramp) before anyone relies on either figure.")
    P(f"- **Round-one savings, typical voice:** the lint's savings model (rows scaled by the cost tool's before/after ratios) lowers the 128 sounds' voice by {sum(sv) / len(sv):.1f} us on average (range {min(sv):.1f} to {max(sv):.1f}); the task note said about 12 to 15.\n")
    P("## 8. How the numbers are made, and what is not verified\n")
    P("- **c** is the model's voice cost (budget.c bd_lrry_voice: the sum of the measured feature rows of PHASE62_VOICE_COST_REPORT.md) plus the morph item, in us of a 725 us block. **1 core / 2 cores** are the counters budget.c shows (budget_solve2, the real file). The tool checks its own copy of the voice sum and of the counter against budget.c for all 128 sounds and stops if they differ.")
    P(f"- **Limit:** two cores {K['lim2'] / 100:.1f} % predicted peak, one core {K['lim1'] / 100:.1f} %.")
    P("- **Pipelining** (my formula; the cores-balance lever-3 note is not in this tree): the serial rest of a block (globals and the part's own items, g + fx) of block b runs on core 1 while core 0 renders the voices of block b + 1. Per block core 0 = hand-over + every voice's control + its audio share; core 1 = its head + its audio share + g + fx; the audio split is the best one; peak = the longer core + the steady-peak allowance J2 (+ the COMB burst as budget.c). Without pipelining the model adds g + fx after the longer core. This is a derivation, not a measured schedule; if lever 3 in cores-balance differs, replace `Model.split(pipe=True)`.")
    P("- **Savings:** each feature row of the model times (kernel cost after round one / before), from the cost tool run against the old and the new lrry_voice.c on the host (LADDER 0.71, filter A 24 dB 0.85, 12 dB 0.86, SVF 0.875, DIRT 0.81, SUB 0.92; OSC 2, noise, ring, COMB, SUPER and the plain SAW did not get cheaper). Host instruction counts, not unit microseconds.")
    P("- Everything is an ESTIMATE of the model; nothing was measured on the unit. Oscillator, noise, ring and filter B SVF rows are themselves estimates in budget.c.")
    P("- The sounds are the ones in the generated bank (tools/lrry_sounds_128.py through gen_lrry_sounds.py, owner edits included if tools/owner_sounds.py has any). `#` is the unit's browse number.\n")
    P("## 9. Full tool output\n")
    P("```")
    P(text)
    P("```")
    return "\n".join(o) + "\n"


if __name__ == "__main__":
    sys.exit(main())
