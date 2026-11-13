#!/usr/bin/env python3
"""Emulator-only: run Felucca's generators (no JieLi toolchain / Docker). Run with LRRY/.venv/bin/python."""
import os, subprocess, sys
from pathlib import Path
root = Path(__file__).resolve().parent
base = root / "lrry"
env = dict(os.environ, DYLD_FALLBACK_LIBRARY_PATH="/opt/homebrew/lib")  # libraqm for Pillow
(base / "build/gen").mkdir(parents=True, exist_ok=True)
jobs = [("gen_version.py", "lrry_version.h"), ("gen_aa_font.py", "ui_fonts.h", "--preset", "inter-tight"), ("gen_aa_icons.py", "ui_icons.h"),
        ("gen_aa_keycaps.py", "ui_keycaps.h"), ("gen_ui_palettes.py", "ui_palettes.h"), ("gen_splash_logo.py", "splash_logo.h"),
        ("gen_tables.py", "lrry_tables.h"), ("gen_fm6_patches.py", "lrry_fm6.h"),
        ("gen_lrry_sounds.py", "lrry_sounds.h"),
        ("gen_lrry_multis.py", "lrry_multis.h"), ("gen_midi_map.py", "midi_map_defaults.h")]
for script, out, *extra in jobs:
    r = subprocess.run([sys.executable, f"tools/{script}", f"build/gen/{out}", *extra], cwd=base, env=env)
    if r.returncode:
        sys.exit(f"FAILED: {script}")
print("generated:", *sorted(p.name for p in (base / "build/gen").iterdir()))
