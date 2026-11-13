#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""L/RRY bench: the two test patches that are no sound of the 128 (owner, 2026-11-02: no test set in the unit's bank).

    TEST SAW     one saw, filter A open, KEY TRK 0, no dirt: the cheapest voice, the base of LRRY-COST / LRRY-DIAG / LRRY-FX
    FULL STRESS  every source, both filters, both dirt units in series: the 'all options on' patch (LRRY-POLY, bench_quick)

They are never stored in the unit: a suite loads a sound of the 128 (BASES below) and then sends `set 1 ID VALUE` for every parameter in which the
test patch differs from that sound, so the part ends up with the test patch's values. The values come from tools/lrry_sounds.py (the old test set,
kept in the tree for this tool and the host tests) through tools/gen_lrry_sounds.py's own name resolution.

    tools/bench_testpatch.py NAME       print the suite lines (load patch + set lines)
"""
import importlib.util
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
BASES = {"TEST SAW": (3, "SAW BASS"), "FULL STRESS": (28, "SUPER LEAD")}   # the sound loaded first (browse number of the 128): the plain saw, and the heaviest 7-saw sound


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _states():
    gs = _load("gen_lrry_sounds", TOOLS / "gen_lrry_sounds.py")
    ids = gs.parse_ids()
    tp, names = gs.parse_tp(ids)
    xtp = gs.parse_xtp(names)
    engines, _order = gs.parse_engines()
    default = {ids[k]: v[3] for k, v in tp.items() if k in ids and v[0] != "-"}
    default.update({v[0]: v[4] for v in xtp.values()})
    for i, row in enumerate(engines[0]["edit"][:8]):
        default[ids["P_E0"] + i] = row[3]
    out = {}
    for label, f in (("128", TOOLS / "lrry_sounds_128.py"), ("test", TOOLS / "lrry_sounds.py")):
        sounds = gs.build(_load("snd_" + label, f))[0]
        out[label] = {s["name"]: dict(s["pairs"]) for s in sounds}
    return default, out


def suite_lines(name, part=1):
    default, snd = _states()
    bn, bname = BASES[name]
    base = snd["128"][bname]
    want = snd["test"][name]
    lines = [f'load patch {part} #{bn:02d} "{bname}"']
    for i in sorted(set(base) | set(want)):
        b, w = base.get(i, default[i]), want.get(i, default[i])
        if b != w:
            lines.append(f"set {part} {i} {w}")
    return lines


if __name__ == "__main__":
    print("\n".join(suite_lines(sys.argv[1])))
