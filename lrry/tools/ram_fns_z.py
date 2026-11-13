# SPDX-License-Identifier: GPL-3.0-only
"""LRRY_RAMPLACE: the code of range Z, the spare end of the memory pool.

Z starts after .pool (app_ram.ld: .ramz follows .pool in the POOL region) and must end below the stack guard at 0x01C74000; the pool
tail rule (tools/build.py POOL_TAIL_MIN, 8,192 B) counts Z in. From there a direct call reaches the whole flash image and the flash
reaches it, and X (tools/ram_fns_x.py) and the hot block reach it too: so Z takes what core 1 runs and the flash also calls.

L/RRY 4.8 (LRRY/RAM_PLACEMENT.md): from the real call graph of the compiled object (every relocation from core 1's entry .c1_text):
  lrry_voice    the stereo voice render (lrry_voice.c with lrry_tab.h, lrry_mac.h and the oscillator, BLEP, filter, dirt and pan
                kernels inlined): core 1's job tc_job_run calls it, and core 0 in the flash (voice.c lrry_render_set)
  lrry_dirt_shape  the dirt shaper: lrry_voice calls it on both cores, and the DIRT page's curve (ui_graph.c draw_graph, flash) since
                L/RRY 5.5 (test2), so it left range X
  tc_ctx_hash   the check value of the block copy: core 1 (tc_c1_step) and core 0 (part_voice, in the flash)
  tc_c1_init    core 1's loop start (called from .c1_text only; L/RRY's entry, kept where L/RRY had it)
memset and memcpy are no longer reached by core 1 (no call in the object). main.c fm1_cstart copies X and Z right after the boot guard.
"""
import os

NAMES = ["lrry_voice"]   # PHASE62: lrry_dirt_shape is inlined everywhere now (the passes specialise it per type; the DIRT page's curve in the flash has its own copy)
TWOCORE_NAMES = ["tc_c1_init"]   # L/RRY's "CORE 1" group, still reached (retired_render is not: the L/RRY job renders no engine)
TWOCORE = os.environ.get("LRRY_TWOCORE") == "1"
if "-O2" in os.environ.get("LRRY_OPT", "").split():      # (bench experiments: at -O2 the compiler inlines tc_ctx_hash everywhere, it has no section of its own)
    TWOCORE_NAMES = [n for n in TWOCORE_NAMES if n != "tc_ctx_hash"]
if TWOCORE:
    NAMES = NAMES + TWOCORE_NAMES
# PHASE60: core 0's per-block path from RAM (it ran from the flash through the instruction cache): part_voice (track_render, the decode, the split and the post inlined, 9.3 KB),
# events_block, mod_begin, morph_begin, lfo_tick, mix_gather, mix_sends, mix_master (the limiter, DC blockers and soft clip inlined), blk_snap
CORE0_RAM = os.environ.get("LRRY_CORE0_RAM", "1") == "1"       # on by default in every RAMPLACE build (PHASE60: the per-block path of core 0 ran from the flash through the instruction cache; from RAM the fixed part of a block fell from ~250 to ~150 us on the unit)
if CORE0_RAM:
    NAMES = NAMES + ["part_voice", "events_block", "mod_begin", "morph_begin", "lfo_tick", "mix_gather", "mix_sends", "mix_master", "blk_snap", "master_dac", "click_render"]
    if os.environ.get("LRRY_BENCH") == "1":
        NAMES = NAMES + ["bench_cal_alu_r", "bench_cal_mem_r"]
    # THE NOTE START AND THE EVENT STEP (2026-11-10, ported from DUELL 1.2's NOTE PATH and EVENT STEP groups, owner yes "port 1 to 3"): on DUELL the first note of
    # a chord found this code out of the instruction cache (89..201 us a note start instead of 22..50, the MIDI step up to 406 us) and its crackles at a chord start went
    # to 0 once it ran from RAM. L/RRY's random-patch run of 2026-11-10 failed the same way: one to a few missed halves at the chord start, at 70..84 percent average. The list
    # is the whole flash call closure of events_block, morph_begin, the note start (trk_note_on_now / _1), the MIDI note and CC path and the note end (input_off,
    # midi_release), read from the disassembly of bench 1110 (108 functions, about 19.1 KB; range Z had about 190 KB free). Left in the flash: memcpy and memset
    # (the start-up's own), and morph_stale, eng_mem_claim, beat_samples: fm1_main calls them from beyond a direct call's reach (4 MB) of range Z
    NAMES = NAMES + [
        "sk_taken", "sense_gate", "sense_at_value", "midi_channel", "midi_track", "midi_targets", "midi_expression", "midi_expression_channel", "div_samples", "step_samples",
        "rec_release", "mono_remove", "mono_pick", "voice_trim", "voices_busy", "voice_victim", "voice_room", "glide_set", "wv_isqrt", "cycle_start", "voice_start",
        "mono_play", "trk_note_off_1", "midi_out_event", "arp_note_off", "arp_chord_off", "arp_off", "seq_release", "motion_setp", "motion_restore", "seq_stop",
        "clock_choice", "motion_begin", "seq_start", "cin_block", "midi_clock_transport", "cin_begin", "midi_forget_track", "atg_release", "trk_all_off_1", "mo_flush",
        "stop_all", "mchord_of", "midi_local_held", "midi_note_held", "hold_all_keys", "atgt_bits", "input_off", "midi_release", "morph_dip", "morph_apply", "trk_note_on_now",
        "trk_note_on_1", "arp_silence", "vs_block", "cin_keep", "rec_nudge", "step_set_nudge", "rec_note", "arp_add", "input_on", "key_tone", "strum_acts", "strum_blocks",
        "mchord_tone", "strum_block", "note_snap_to", "chord_make", "mod_midi", "velo_at_block", "chord_build", "kb_lat", "clock_rescale", "midi_clock_pulse", "map_line",
        "midi_glob_act", "midi_map_global", "midi_note_event", "morph_push", "midi_pedal_up", "param_desc_of", "midi_cc_set", "midi_morph", "midi_map_cc", "midi_pc",
        "midi_event", "midi_in_take", "clock_unlock", "midi_clock_advance", "arp_note_on", "seq_step", "seq_audition", "seq_mods_off", "motion_unlock", "motion_step",
        "seq_play", "seq_nd_due", "seq_nudge", "seq_nd_start", "cin_record", "arp_list", "arp_seq_notes", "arp_pick", "atg_fire", "arp_seq", "arp_chord", "arp_fire",
        "duck_pump",
    ]
    # THE BUTTON SCAN (2026-11-10, ported from DUELL 1.2's BUTTON SCAN group, owner: "you can still port the led thing from duell"): TIMER5's tick runs 10,000 times a second, nested in the
    # audio render; from the flash its time grows and wanders with the render's load (DUELL: ~7.3 us a tick from the flash), so the LED columns stay lit unevenly. From RAM every tick takes the
    # same time. fm1_input_tick (noinline since 2026-11-10, hal/fm1_input.h; fm1__keys_frame is folded into it here) and the key sensing it calls. fm1_timer5_irq stays in the flash (elfcheck)
    NAMES = NAMES + ["fm1_input_tick", "fm1__sr_bits", "sense_update", "sense_want_drive", "sk_driven", "sk_finish", "sk_range", "sk_land", "sk_publish",
                     "sk_final_depth", "sc_pow12", "sc_log2q8", "sc_ev_push"]
    if os.environ.get("LRRY_FX_SENDS", "1") != "1":     # core.h LRRY_FX_SENDS (default 1 since 2026-11-12; 0: mix_sends compiles to nothing, no buses, so it has no section of its own)
        NAMES = [n for n in NAMES if n != "mix_sends"]
# (2026-11-11, from DUELL 2026-11-07) the erase's LED steps (hal/fm1_flash.h fl_erase4k_led, fl_led_col): they run while the flash is off, so they must be in RAM
NAMES = NAMES + ["fl_erase4k_led", "fl_led_col"]
HI = 0x01C74000                                 # the stack guard (app_ram.ld _guard0): Z ends below it
