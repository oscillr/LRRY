#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""DUELL factory multis (0.2.23; 0.2.24: the owner's multis back; 0.2.25: COLD CHAPEL in M13): the ONE source of the sixteen factory multis M01..M16 and the tool that builds them.

    tools/factory_multis.py build      the records tools/factory_multis/M01.bin..M16.bin (needs build/gen from DUELL/gen_only.py)
    tools/factory_multis.py check      the records against this list (version, name, sounds, MODE, split) and the similarity check
    tools/factory_multis.py list       the list as text (slot, name, mode, parts '#NN NAME', owner / mine)
    tools/factory_multis.py selftest   the similarity check catches each kind of pair (and lets an owner pair through, reported)

How a record is made: MULTIS below says, per slot, the two parts' factory sounds (by name), what the multi sets on top of each sound
(raw parameter values, core.h P_* ids; an engine EDIT value as "E:LABEL", e.g. "E:CUT" on an ANALOG part), the SEQUENCER arp patterns,
End B, the globals (core.h G_* ids), the output MODE, XFADE and SPLIT (item 157: BALANCE is no multi value; its record byte is a spare, 0). `build` writes that as a C table (build/gen_mm/factory_multis_def.h)
and runs tests/gen_factory_multis.c, which loads each sound with the firmware's own code (set_engine_of + apply_preset_to, as PATCHES loads it),
applies the table and saves with multi.c's packer (project_save_as): the records are byte for byte what a unit saves (version MU_VER 7; a kept record of an older version is converted by tools/multi_upgrade.py). A slot
marked `kept` is a record made earlier that is kept byte for byte (owner, 0.2.20: the factory multis do not follow reworked sounds): it is not
rebuilt, only checked and, when its slot moved, moved (git mv). A slot marked `owner` is a multi the owner made or edited himself (0.2.24, owner:
never drop or change them): kept, and exempt from the similarity check (a pair with one is reported, never a failure). The reverb LOW CUT of
every built multi is the unit's default (G_RLOCUT 75 = 299 Hz) unless the multi sets it; DUCK SRC is PUMP / PART / MIDI (0 / 1 / 2, MU_VER 6); MU_VER 7 adds, per part, the NX extension slots and the hand-set B edit list after the values (tools/multi_upgrade.py).

The owner's own edits of a multi come back into this list through tools/import_owner_edits.py (docs/OWNER_REFINE.md): it writes
tools/owner_multis.py, read below (apply_owner), whose entries replace a slot's record (kept, owner) and its line here.

Old one-off converters (factory_multis_levelsplit.py, _locut.py, _ducksrc.py, _delaymode.py, _lfo_free.py) are gone: every built
record is made in today's format, and the kept ones were converted by them before (0.2.14 .. 0.2.22).

The test scenes of the host tests (the CPU TEST multis of before 0.2.23 and the factory multis of 0.2.22) are in tests/scenes/,
not in a factory slot (tests/scenes.h). SPARE below lists multis made for 0.2.23 that left the sixteen in 0.2.24 (their records are in
tests/scenes/spare/, so they can come back: move the entry into MULTIS, give it a slot and run `build`).
"""
import importlib.util
import math
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIR = ROOT / "tools" / "factory_multis"
GEN = ROOT / "build" / "gen_mm"
REC_MAX, MU_G_AT, MB_N, PS_STEP, NAME_MAX = 1916, 20, 7, 9, 12
ENGINES = ["ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS", "DRUM", "NOISE", "FM6", "SLICE", "WAVE"]
MODES = {"DUEL": 0, "STEREO": 1, "LAYER": 2}           # core.h DM_SPLIT (shown DUEL), DM_STEREO, DM_LAYER: the record's byte 4
NOTE = {n: i for i, n in enumerate(["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"])}


def note(s):
    """'F3' -> 53 (C4 = 60)"""
    m = re.fullmatch(r"([A-G]#?)(-?\d)", s)
    return 12 * (int(m.group(2)) + 1) + NOTE[m.group(1)]


# ---- the multis ---------------------------------------------------------------------------------------------------------------
# A part: sound (factory sound name), set {P_id: value or 'NAME' of the parameter's list; "E:LABEL": the engine's EDIT value}, steps (the
# SEQUENCER arp's pattern: offsets in semitones from the held key, '.' a rest, suffix A an accent; LEN = its length), bseed / amt (End B rolled
# again with this seed / at this AMOUNT %; 0 the sound's own). A multi: g {G_id: value}, mode, xf (0..100), split (a note:
# below it part 1, at or above part 2; compared with the key as played, before any TRN: F3 is the lowest key at octave 0, so F4 is the middle
# of the keyboard), what (one line, the character).
# Units are the stored ones (params.c): sends / SAT / RES 0..127, LEVEL / TRIM 0..127 (112 = 0 dB), PAN -64..63, EQ dB -12..12,
# DUCK DEPTH 0..100, arp GATE 1..127, the lists by name. Owner rules (0.2.23): at most 2 or 3 tributes (none in the sixteen since 0.2.25: BASILDON is spare),
# no Moroder multi (it is a pattern), classic honest names, no two multis alike (check_similar). Owner (0.2.24): his own multis
# (COLD CHAPEL, ACID ETHER, GARGLE ACID of 0.2.22; DEAD BUFFER holds his edits) are never dropped: `owner`.
# In LAYER each part maps a played key with its own TRN (seq.c key_on, 0.2.23: until 0.2.22 part 2 got part 1's note and the split compared it),
# so an octave between the parts can be set with either part's TRN.
# Order (0.2.24): 1-4 bass grooves and splits, 5-8 pads, layers and the dry pair, 9-12 the four multis of 0.2.1 as made (the owner's),
# 13-15 for performing (0.2.25: 13 is the owner's original COLD CHAPEL, in the slot of BASILDON, the multi least different from the rest), 16 the stress test.
SEND = ("P_CHOR", "P_DLY", "P_REV")
# L/RRY (owner, 2026-10-30): DUELL's sixteen factory multis are dropped with DUELL's factory sounds (each named two of them; L/RRY plays one part). The M*.bin
# records are gone too. No factory multi until L/RRY has its own: duell_multis.h holds DF_N 0 (tools/gen_duell_multis.py)
MULTIS = [
]

# ---- spare multis (0.2.24): made for 0.2.23, out of the sixteen so the owner's COLD CHAPEL, ACID ETHER and GARGLE ACID could come back.
# Chosen by the similarity check's own measure against the restored and kept ones: VELVET TINES and SEVENOAKS are both a PAD + KEYS layer
# like the owner's EERIE SIGNAL (VESPERS + JAZZ CLUB), and the 0.2.23 drone shared SINGING BOWL with his COLD CHAPEL and took its name
# (renamed here). The 0.2.23 TINE STRINGS is VELVET TINES here since 0.2.25: the owner's M06 has that name (its record still says TINE STRINGS). Their records of 0.2.23 are tests/scenes/spare/ (rendered and hashed by tests/multi_render_test.c). Not built.
SPARE = []   # (L/RRY: DUELL's spare multis named DUELL sounds: dropped with them)


def apply_owner(multis):
    """tools/owner_multis.py (written by tools/import_owner_edits.py): OWNER_MULTIS = [dict(slot, name, mode, parts=[dict(sound), dict(sound)], what, ...)],
    each a record the owner saved on his unit, kept byte for byte in tools/factory_multis/Mnn.bin: it replaces the slot's entry here (kept, owner)"""
    f = ROOT / "tools" / "owner_multis.py"
    if not f.exists():
        return multis
    spec = importlib.util.spec_from_file_location("owner_multis", f)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    out = list(multis)
    for o in mod.OWNER_MULTIS:
        if o.get("slot") not in range(1, 17):
            sys.exit("factory_multis: owner_multis.py: slot %r" % o.get("slot"))
        e = dict(o, kept=True, owner=True)
        out = [m for m in out if m["slot"] != e["slot"]] + [e]
    return sorted(out, key=lambda m: m["slot"])


MULTIS = []   # (L/RRY: the owner's DUELL multis of tools/owner_multis.py name DUELL sounds: not applied)


# ---- the firmware's names --------------------------------------------------------------------------------------------------------
def _lists():
    src = (ROOT / "firmware" / "src" / "params.c").read_text()
    out = {}
    for m in re.finditer(r"static const char \*const (N_\w+)\[\]\s*=\s*\{(.*?)\};", src, re.S):
        body = re.sub(r"/\*.*?\*/", "", m.group(2), flags=re.S)
        out[m.group(1)] = [s.replace("\\n", " ") for s in re.findall(r'"((?:[^"\\]|\\.)*)"|\b0\b', body)]   # (DUELL 1.1: a shown name may break at a line (\n): the key is the words with a space, SOFT CLIP)
    return out


LISTS = _lists()
P_LIST = {"P_AMODE": "N_AMODE", "P_ARATE": "N_DIV", "P_AHOLD": "N_HOLD", "P_CHRD": "N_CHRD", "P_VOIC": "N_VOIC", "P_VOICE": "N_VOICE",
          "P_LSYNC": "N_LSYNC", "P_ITYPE": "N_ITYPE", "P_DKSRC": "N_DKSRC", "P_DKRATE": "N_DIV", "P_M8SRC": "N_MSRC", "P_M8DST": "N_MDST",
          "P_SDIV": "N_DIV"}
G_LIST = {"G_DTIME": "N_DIV", "G_RTYPE": "N_RTYPE", "G_DMODE": "N_DMODE", "G_SHPITCH": "N_SHP"}
RETIRED = {("G_SHPITCH", "BOTH"): 2}               # 0.2.25 (owner): SHIM's PITCH BOTH removed; a stored 2 plays UP (project.c, fx_bus.c rev_shimx). M16 STRESS TEST holds it


def value(lists, key, v):
    if isinstance(v, int):
        return v
    if v.startswith("u_uf("):                      # the multimode filter's CUT for a frequency in Hz (tools/duell_sounds.py, core.h UF_STEPS 82)
        hz = float(v[5:-1])
        return max(0, min(82, round(82 * math.log(hz / 30) / math.log(16000 / 30))))
    names = LISTS[lists[key]]
    if (key, v) in RETIRED:                        # (0.2.25: a value the unit no longer offers; the record keeps its byte, the unit reads it as the stand-in)
        return RETIRED[(key, v)]
    if v not in names:
        sys.exit("factory_multis: %s has no value %r (%s)" % (key, v, ", ".join(names)))
    return names.index(v)


def _gen_sounds():
    spec = importlib.util.spec_from_file_location("gen_duell_sounds", ROOT / "tools" / "gen_duell_sounds.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def sounds():
    spec = importlib.util.spec_from_file_location("duell_sounds", ROOT / "tools" / "duell_sounds.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return _gen_sounds().with_owner(mod.SOUNDS)        # (0.2.24: the owner's edited sounds, tools/owner_sounds.py, as the build sees them)


def resolve(snd):
    by = {s["name"]: s for s in snd}
    for m in MULTIS + SPARE:
        for p in m["parts"]:
            if p["sound"] not in by:
                sys.exit("factory_multis: M%02d %s: no factory sound %r" % (m.get("slot", 0), m["name"], p["sound"]))
            s = by[p["sound"]]
            p["eng"], p["idx"] = ENGINES.index(s["engine"]), s["idx"]


_EDIT = {}


def edit_key(eng, label):
    """'E:CUT' on a part of engine eng -> 'P_E0 + k' (the engine's EDIT table, eng_*.c, read by tools/gen_duell_sounds.py)"""
    if not _EDIT:
        engines, _ = _gen_sounds().parse_engines()
        for i, e in engines.items():
            _EDIT[e["name"]] = {r[0]: k for k, r in enumerate(e["edit"])}
    labels = _EDIT.get(ENGINES[eng], {})
    if label not in labels:
        sys.exit("factory_multis: %s has no EDIT value %r (%s)" % (ENGINES[eng], label, ", ".join(labels)))
    return "P_E0 + %d" % labels[label]


# ---- the C table ------------------------------------------------------------------------------------------------------------------
def emit(path):
    lines = ["/* generated by tools/factory_multis.py: the factory multis built by tests/gen_factory_multis.c (do not edit) */",
             "static const multi_t MM[] = {"]
    for m in MULTIS:
        if m.get("kept"):
            continue
        g = []
        for k, v in m.get("g", {}).items():
            g += [k, str(value(G_LIST, k, v))]
        split = note(m["split"]) + 1 if m.get("split") else 0
        lines.append('    {"%s", %d, %d, %d, %d, {%s}, {' % (m["name"], m["slot"] - 1, MODES[m["mode"]], m.get("xf", 0), split,
                                                         ", ".join(g + ["END"])))
        for p in m["parts"]:
            st = []
            for k, v in p.get("set", {}).items():
                if k.startswith("E:"):
                    st += [edit_key(p["eng"], k[2:]), str(v)]
                else:
                    st += [k, str(value(P_LIST, k, v))]
            steps = p.get("steps")
            n = len(steps.split()) if steps else 16
            lines.append('        {%d, %d, "%s", {%s}, %s, %d, %d, %d},' % (p["eng"], p["idx"], p["sound"], ", ".join(st + ["END"]),
                                                                       '"%s"' % steps if steps else "0", n, p.get("bseed", 0), p.get("amt", 0)))
        lines.append("    }},")
    lines.append("};")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")


BOOT_H = """/* tools/factory_multis.py: no factory multis (the build of the generator itself) */
#define DF_TEST_MULTIS DUELL_TEST_MODE
#define DF_N 0u
static const uint8_t DF_SLOT[1] = {0};
static const uint16_t DM_LEN[1] = {0};
static const uint16_t DM_AT[1] = {0};
static const uint8_t DM_REC[1] = {0};
"""


def build():
    snd = sounds()
    resolve(snd)
    emit(GEN / "factory_multis_def.h")
    (GEN / "duell_multis.h").write_text(BOOT_H)
    exe = ROOT / "build" / "host" / "gen_factory_multis"
    exe.parent.mkdir(parents=True, exist_ok=True)
    cc = ["cc", "-O1", "-w", "-DDUELL_TEST_MODE=1", "-I" + str(GEN), "-Ibuild/gen", "-Ifirmware/src", "-Itests", '-DFELUCCA_VERSION="x"',
          "-o", str(exe), "tests/gen_factory_multis.c", "-lm"]
    if subprocess.run(cc, cwd=ROOT).returncode:
        sys.exit("factory_multis: tests/gen_factory_multis.c does not compile (run DUELL/gen_only.py first: build/gen)")
    if subprocess.run([str(exe), "build", str(DIR)], cwd=ROOT).returncode:
        sys.exit("factory_multis: the build failed")
    want = {"M%02d.bin" % m["slot"] for m in MULTIS}
    for f in DIR.glob("M*.bin"):
        if f.name not in want:
            f.unlink()
            print("removed", f.name)
    check(snd)


# ---- checks -------------------------------------------------------------------------------------------------------------------------
def parts_of(b):
    """the (engine, factory index) of both parts of record b, the end position (after the automation)"""
    np_, pos, out = b[1], MU_G_AT + 2 * b[2], []
    for _ in range(2):
        out.append((b[pos], b[pos + 1], pos))
        flags = b[pos + 3]
        pos += 4 + MB_N + np_
        pos += 1 + b[pos]                               # MU_VER 7: nx, the extension slots
        pos += 1 + 2 * b[pos]                           # .. ne, the hand-set B edits (wide id, value + 64)
        pos += 8 + PS_STEP * sum(bin(x).count("1") for x in b[pos:pos + 8]) + (128 if flags & 1 else 0)
    return out, pos


def check_record(m, b, by_idx, P):
    slot, err = m["slot"], []
    if len(b) > REC_MAX or len(b) < MU_G_AT or b[0] != 7:
        return ["M%02d: not a version 7 record of at most %d bytes" % (slot, REC_MAX)]
    raw = b[8:20].split(b"\0")[0]
    if raw.decode("ascii", "replace") != m["name"] or any(not 32 <= c <= 126 for c in raw) or len(m["name"]) > NAME_MAX:
        err.append("M%02d: name is %r, expected %r" % (slot, raw, m["name"]))
    if b[4] != MODES[m["mode"]]:
        err.append("M%02d: MODE byte %d, expected %d (%s)" % (slot, b[4], MODES[m["mode"]], m["mode"]))
    parts, pos = parts_of(b)
    for t, (eng, idx, at) in enumerate(parts):
        have = by_idx.get((ENGINES[eng] if eng < len(ENGINES) else "?", idx))
        if have != m["parts"][t]["sound"]:
            err.append("M%02d part %d: engine %d sound %d is %r, expected %r" % (slot, t + 1, eng, idx, have, m["parts"][t]["sound"]))
        v = at + 4 + MB_N                               # (the P_COUNT values follow the End B block)
        if not any(b[at + 4:v]):
            err.append("M%02d part %d: no End B block" % (slot, t + 1))
        if b[v + P["P_FREED"]] - 64 not in (1, 2):
            err.append("M%02d part %d: the multimode filter's SLOPE is not 1 or 2" % (slot, t + 1))
        if b[v + P["P_DKSRC"]] - 64 not in (0, 1, 2, 3):          # (3 = OFF, 0.2.24: the default of a new sound)
            err.append("M%02d part %d: DUCK SRC is not PUMP / PART / MIDI / OFF" % (slot, t + 1))
    n = b[pos]
    if pos + 1 + 3 * n + 3 != len(b):
        err.append("M%02d: record length does not match its parts" % slot)
    else:
        split = b[-1]
        want = note(m["split"]) + 1 if m.get("split") else 0
        if not m.get("kept") and split != want:
            err.append("M%02d: SPLIT byte %d, expected %d" % (slot, split, want))
    locut = b[MU_G_AT + 2 * 21] | b[MU_G_AT + 2 * 21 + 1] << 8
    if locut != 75 and not m.get("owner"):          # (an owner's record keeps whatever he set)
        err.append("M%02d: reverb LOW CUT %d, not the default 75" % (slot, locut))
    return err


def p_ids():
    core = re.sub(r"//[^\n]*", " ", re.sub(r"/\*.*?\*/", " ", (ROOT / "firmware" / "src" / "core.h").read_text(), flags=re.S))
    m = re.search(r"enum\s*\{\s*P_LEVEL\b(.*?)P_COUNT", core, re.S)
    names = ["P_LEVEL"] + re.findall(r"\b(P_\w+)\b", m.group(1))
    return {n: i for i, n in enumerate(names)}


def check_similar(entries):
    """No two multis alike (owner, 0.2.23: "no two multis near-duplicates"). entries: (slot, name, mode, (sound 1, sound 2), split,
    ((engine, category) of part 1, of part 2), owner). Alike: the same two sounds (either order); one sound in the same place with the same
    MODE and split; or the same kind: both parts of the same engines and categories (either order) in the same MODE. Returns (bad, allowed):
    the clashes as text; a pair with an owner multi (0.2.24: his are never dropped or changed) is `allowed`, reported and not a failure."""
    bad, allowed = [], []
    for i, a in enumerate(entries):
        for b in entries[i + 1:]:
            why = None
            if set(a[3]) == set(b[3]):
                why = "the same two sounds"
            elif any(a[3][t] == b[3][t] for t in (0, 1)) and a[2] == b[2] and a[4] == b[4]:
                why = "the same part %d sound (%s) in the same MODE and split" % (1 if a[3][0] == b[3][0] else 2, a[3][0] if a[3][0] == b[3][0] else a[3][1])
            elif sorted(a[5]) == sorted(b[5]) and a[2] == b[2]:
                why = "the same kind: %s + %s in the same MODE" % ("/".join(a[5][0]), "/".join(a[5][1]))
            if why:
                line = "M%02d %s and M%02d %s: %s" % (a[0], a[1], b[0], b[1], why)
                owner = (len(a) > 6 and a[6]) or (len(b) > 6 and b[6])
                (allowed if owner else bad).append(line + (" (allowed: the owner's)" if owner else ""))
    return bad, allowed


def selftest():
    """check_similar must catch each kind of pair, pass distinct ones, and let an owner pair through (reported)"""
    k = lambda e1, c1, e2, c2: ((e1, c1), (e2, c2))
    base = (1, "A", 2, ("REESE BASS", "SQUELCH"), 0, k("WAVE", "BASS", "ANALOG", "BASS"), False)
    cases = [((2, "B", 2, ("SQUELCH", "REESE BASS"), 0, k("ANALOG", "BASS", "WAVE", "BASS"), False), 1, 0),      # the same two sounds, swapped
             ((2, "B", 2, ("REESE BASS", "DRAWBARS"), 0, k("WAVE", "BASS", "WAVE", "KEYS"), False), 1, 0),        # part 1's sound, same MODE and split
             ((2, "B", 2, ("GROWL BASS", "LADDER BASS"), 0, k("WAVE", "BASS", "ANALOG", "BASS"), False), 1, 0),   # the same kind
             ((2, "B", 0, ("REESE BASS", "DRAWBARS"), 0, k("WAVE", "BASS", "WAVE", "KEYS"), False), 0, 0),        # the sound shared, another MODE
             ((2, "B", 2, ("TINE ROOM", "VELVET BOW"), 0, k("FM6", "KEYS", "ANALOG", "PAD"), False), 0, 0),       # nothing shared
             ((2, "B", 2, ("GROWL BASS", "SQUELCH"), 0, k("WAVE", "BASS", "ANALOG", "BASS"), True), 0, 1)]        # the owner's: allowed, reported
    ok = True
    for e, want_bad, want_allowed in cases:
        bad, allowed = check_similar([base, e])
        ok &= len(bad) == want_bad and len(allowed) == want_allowed
    print("factory_multis: selftest of the similarity check %s" % ("ok" if ok else "FAILED"))
    return ok


def check(snd=None):
    snd = snd or sounds()
    resolve(snd)
    by_idx = {(s["engine"], s["idx"]): s["name"] for s in snd}
    cat = {s["name"]: (s["engine"], s["cat"]) for s in snd}
    P = p_ids()
    err, entries = [], []
    if len({m["slot"] for m in MULTIS}) != len(MULTIS) or len({m["name"] for m in MULTIS + SPARE}) != len(MULTIS) + len(SPARE):
        err.append("two multis share a slot or a name (the spare ones counted)")
    if sum(1 for m in MULTIS if m.get("tribute")) > 3:
        err.append("more than three tribute multis (owner: at most 2 or 3)")
    for m in MULTIS:
        f = DIR / ("M%02d.bin" % m["slot"])
        if not f.exists():
            err.append("M%02d: %s missing" % (m["slot"], f.name))
            continue
        b = f.read_bytes()
        err += check_record(m, b, by_idx, P)
        if not m.get("stress"):
            snds = (m["parts"][0]["sound"], m["parts"][1]["sound"])
            entries.append((m["slot"], m["name"], b[4], snds, b[-1], (cat[snds[0]], cat[snds[1]]), bool(m.get("owner"))))
    for m in SPARE:                                   # (the spare records exist and name their sounds)
        f = ROOT / "tests" / "scenes" / (m["scene"] + ".bin")
        if not f.exists():
            err.append("spare %s: %s missing" % (m["name"], f.name))
        else:
            parts, _ = parts_of(f.read_bytes())
            for t, (eng, idx, _at) in enumerate(parts):
                if by_idx.get((ENGINES[eng], idx)) != m["parts"][t]["sound"]:
                    err.append("spare %s part %d: the record holds %r, the list says %r" % (m["name"], t + 1, by_idx.get((ENGINES[eng], idx)), m["parts"][t]["sound"]))
    sim, allowed = check_similar(entries)
    for e in err + sim + allowed:
        print("factory_multis:", e)
    print("factory_multis: %d multis checked, %d record problems, multi similarity: %d pairs of %d alike, %d allowed (the owner's); %d spare" % (
        len(MULTIS), len(err), len(sim), len(entries) * (len(entries) - 1) // 2, len(allowed), len(SPARE)))
    return not err and not sim


def listing():
    snd = sounds()
    resolve(snd)
    num = patch_numbers(snd)
    for m in MULTIS:
        print("M%02d %-12s %-6s %-5s %s + %s%s | %s" % (m["slot"], m["name"], m["mode"], "owner" if m.get("owner") else "kept" if m.get("kept") else "mine",
                                                       *("#%d %s" % (num[p["sound"]], p["sound"]) for p in m["parts"]),
                                                       "  split " + m["split"] if m.get("split") else "", m.get("what", "")))
    for m in SPARE:
        print("--  %-12s %-6s spare %s + %s | %s" % (m["name"], m["mode"], *("#%d %s" % (num[p["sound"]], p["sound"]) for p in m["parts"]), m.get("what", "")))


def patch_numbers(snd):
    """sound name -> #NN, from docs/PATCH_NUMBERS.md (the PATCHES list)"""
    out = {}
    for line in (ROOT.parent / "docs" / "PATCH_NUMBERS.md").read_text().splitlines():
        m = re.match(r"\|\s*(\d+)\s*\|\s*(.+?)\s*\|", line)
        if m:
            out[m.group(2).replace(" (NEW)", "")] = int(m.group(1))
    return out


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    os.chdir(ROOT)
    if cmd == "build":
        build()
        sys.exit(0 if check() else 1)
    if cmd == "check":
        sys.exit(0 if check() else 1)
    if cmd == "selftest":
        sys.exit(0 if selftest() else 1)
    if cmd == "list":
        listing()
        sys.exit(0)
    sys.exit(__doc__)
