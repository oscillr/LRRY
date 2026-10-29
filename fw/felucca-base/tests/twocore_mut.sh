#!/bin/bash
# L/RRY phase 4.3b: the mutant self-check of tests/twocore_sim.c. Each mutant is a copy of the firmware with ONE rule of the by-voice split broken (the four of host/lrry_split.h: NOWAIT, DOUBLE,
# STALE, SHARED, and the ones DUELL's twocore_mut had that still apply); the simulation must FAIL on it. The unbroken copy must PASS (first line), else the check proves nothing.
# usage (from felucca-base, after the generated headers exist in build/gen):  tests/twocore_mut.sh [name ...]      (default: all)
FV=$(sed -n 's/^#define DUELL_VERSION_STR "\(.*\)"/\1/p' build/gen/duell_version.h)
OUT=${TMPDIR:-/tmp}; D=build/twocore_mut; fail=0; want="$*"
FLAGS=(-O1 -w -DHOST_MIX_FOLLOWS_MASTER=1 -DDUELL_TEST_MODE=1 -I$D/build/gen -I$D/firmware/src -I$D/tests -DNPART=1 -DNSHOWN=1 "-DFELUCCA_VERSION=\"$FV\"")
mut() {   # name, file, sed expression
  local name=$1 file=$2 expr=$3
  if [ -n "$want" ] && [[ " $want " != *" $name "* ]]; then return; fi
  rm -rf $D; mkdir -p $D/build
  cp -R firmware tests $D/ && cp -R build/gen $D/build/ || { echo "$name: copy failed"; fail=1; return; }
  if [ "$expr" != "-" ]; then
    sed -E "$expr" "$D/$file" > "$D/$file.new" && mv "$D/$file.new" "$D/$file"
    if cmp -s "$D/$file" "$file"; then echo "$name: FAIL (the sed expression changed nothing)"; fail=1; return; fi
  fi
  if ! cc "${FLAGS[@]}" -o $OUT/twocore_mut $D/tests/twocore_sim.c -lm -lpthread > $OUT/twocore_mut.log 2>&1; then echo "$name: FAIL (does not compile: $(grep -m1 error $OUT/twocore_mut.log))"; fail=1; return; fi
  if $OUT/twocore_mut -nomut > $OUT/twocore_mut.log 2>&1; then
    if [ "$expr" = "-" ]; then echo "$name: PASS (the unbroken copy passes: $(tail -1 $OUT/twocore_mut.log))"; else echo "$name: FAIL (the mutation was NOT detected)"; fail=1; fi
  else
    if [ "$expr" = "-" ]; then echo "$name: FAIL (the unbroken copy fails)"; fail=1; else echo "$name: PASS (caught: $(grep -c 'twocore: FAIL' $OUT/twocore_mut.log) checks failed; first: $(grep -m1 'twocore: FAIL' $OUT/twocore_mut.log | cut -c1-150))"; fi
  fi
}
mut ORIGINAL firmware/src/twocore_run.c -
mut NOWAIT firmware/src/twocore_run.c 's/^    tcr\.got = 1;$/    tcr.got = 1;\n    if (j) { tcr_add_acc(out, outR, n); tcr.open = 0; return; }   \/\/ MUTANT: core 0 adds core 1 sums without waiting for done/'
mut DOUBLE firmware/src/voice.c 's/lrry_render_set\(ti, ~\(c1 \| open\), out, outR, n\);/lrry_render_set(ti, ~(((c1 \& (c1 - 1u))) | open), out, outR, n);   \/\/ MUTANT: the lowest voice of core 1 is played by core 0 too/'
mut MISSING firmware/src/voice.c 's/lrry_render_set\(ti, ~\(c1 \| open\), out, outR, n\);/lrry_render_set(ti, ~(c1 | open | 1u), out, outR, n);   \/\/ MUTANT: voice 0 is played by neither core when it is core 0 s/'
mut STALE firmware/src/twocore_run.c 's/^    tc_ctx\.pp = lrry_pp\[ti\];.*$/    { static int once; if (!once) { tc_ctx.pp = lrry_pp[ti]; once = 1; } }   \/\/ MUTANT: the patch copy is not refreshed/'
mut SHARED firmware/src/twocore_run.c 's/&tc_ctx\.m\[i\], tc_acc\[0\], tc_acc\[1\], CTL\);/\&tc_ctx.m[i], pscr[0].buf, pscr[0].bufR, CTL);   \/\/ MUTANT: core 1 adds into core 0 block/'
mut SHARED_STATE firmware/src/twocore_run.c 's/&lrry_vs\[0\]\[tc_ctx\.id\[i\]\]/\&lrry_vs[0][i]/'
mut PEND_OFF firmware/src/voice.c 's/if \(\(tcr_open_set\(\) >> vi\) & 1u\) \{/if (0) {/'
mut SPLIT_NOT_SOUNDING firmware/src/voice.c 's/^    return c1;$/    return c1 | (c1 ? 0x80u : 0u);   \/\/ MUTANT: a voice that does not sound is handed over/'
mut FADE_OFF firmware/src/twocore_run.c 's/tcr\.fade2 = 1;/tcr.fade2 = 0;/'
mut VLATE_RUN firmware/src/twocore_run.c 's/#define TCR_VLATE_RUN 0u/#define TCR_VLATE_RUN 1u/'
mut VWIN_ON firmware/src/twocore_run.c 's/#define TCR_VWIN_K 0u/#define TCR_VWIN_K 8u/'
mut NOANS_NOPAUSE firmware/src/twocore_run.c 's|tcr_pause\(TC_TICKS\(\), TCR_PW_FAULT\); /\* \[no answer\] \*/||'
mut FAULT_NOPAUSE firmware/src/twocore_run.c 's/    if \(!tcr\.paused\) \{/    if (!tcr.paused \&\& why != TCR_PW_FAULT) {/'
mut SILENT_NOPAUSE firmware/src/twocore_run.c 's|tcr_pause\(TC_TICKS\(\), TCR_PW_FAULT\); /\* \[silent\] \*/||'
mut WAIT_TO_HALF_END firmware/src/twocore_run.c 's/a, lim = tcr\.lim, t1;/a, lim = tcr.t0 + TCR_HALF_TICKS, t1;/'
mut NO_HASH firmware/src/twocore_run.c 's/if \(tc_ctx_hash\(&tc_ctx\) != arg >> 16 \|\| n != CTL\)/if (n != CTL)/'
exit $fail
