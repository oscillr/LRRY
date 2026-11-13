# SPDX-License-Identifier: GPL-3.0-only
"""LRRY_RAMPLACE: the code of range X, the old program RAM 0x01C00000..0x01C07F00.

X is 31.75 KB (32,512 B; crt0 keeps a debug area at 0x01C07F00). From there a direct call (+-4 MiB) does not reach the flash
(0x02000120 is 0x400120 away) and the flash cannot call into it. So X holds closed code only: functions that no flash code calls and
that call nothing but RAM code (range X, range Z, the hot block).

L/RRY 4.8 (LRRY/RAM_PLACEMENT.md): the list is what core 1 runs, from the real call graph of the compiled object (build/lrry_hot.o,
every relocation from core 1's entry .c1_text = fm1_c1_main with tc_c1_step and tc_job_run inlined). Core 1 reaches 6 code sections:
.c1_text, lrry_voice, lrry_svf_coef, lrry_dirt_shape, tc_ctx_hash, tc_c1_init. Of those, only lrry_svf_coef has no caller in the
flash (its one caller is lrry_voice, in range Z) and calls nothing: it is X's. L/RRY 5.5 (test2): lrry_dirt_shape moved to range Z, the
DIRT page's curve (ui_graph.c draw_graph, flash) calls it now. lrry_voice (core 0's lrry_render_set
calls it from the flash) and tc_ctx_hash (core 0's part_voice) must be reachable from the flash: range Z (tools/ram_fns_z.py).
L/RRY's engine clusters (PHYS, ANALOG, TRIO, PHASE, LOFI, VOICE, NOISE) and its two-core additions are gone with the engines or are no
longer reached by core 1. tools/ram_place.py checks the object at every RAMPLACE build, tools/elfcheck.py the linked result.
"""
import os

NAMES = ["lrry_svf_coef"]                       # the voice's filter coefficients: called only by lrry_voice (range Z)
# LRRY_TWOCORE=1: core 1's own code (hal/fm1_core1.h: the waiting loop fm1_c1_main with tc_c1_step and the job tc_job_run inlined, and the
# entry stub fm1_c1_entry, both in the section .c1_text) is linked into this range by firmware/app_ram.ld (KEEP(*(.c1_text*)) in .ramx):
# inside the PC window, and it calls only RAM (lrry_voice, lrry_dirt_shape, tc_ctx_hash, tc_c1_init in range Z). It is not a .text.f section, so ram_place
# does not rename it; elfcheck counts it in the range (ELF_NAMES).
C1_NAMES = ["fm1_c1_main"]
ELF_NAMES = NAMES + (C1_NAMES if os.environ.get("LRRY_TWOCORE") == "1" else [])
LO, HI = 0x01C00000, 0x01C07F00                 # the range (app_ram.ld RAMX)
