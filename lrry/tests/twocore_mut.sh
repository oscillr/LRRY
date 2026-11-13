#!/bin/bash
# L/RRY phase 4.3b: the mutant self-check of tests/twocore_sim.c. Each mutant is a copy of the firmware with ONE rule of the by-voice split broken (the four of host/lrry_split.h: NOWAIT, DOUBLE,
# STALE, SHARED, and the ones L/RRY's twocore_mut had that still apply); the simulation must FAIL on it. The unbroken copy must PASS (first line), else the check proves nothing.
# cores-pipeline: LRRY_PIPELINE is on by default, so the rules of the pipeline are mutated too (PIPE_*); the plain runner's own mutants work on its code when LRRY_CFX=-DLRRY_PIPELINE=0. The check runs the sim
# without its switch sweep (-nosweep: faster) except for the mutants that need it. LRRY_PLAIN_DIR (the plain one-core program's samples; tests/lrry_split.sh writes it) makes the shifted-reference check part of it.
# usage (from lrry, after the generated headers exist in build/gen):  tests/twocore_mut.sh [name ...]      (default: all)
FV=$(sed -n 's/^#define LRRY_VERSION_STR "\(.*\)"/\1/p' build/gen/lrry_version.h)
OUT=${TMPDIR:-/tmp}; D=build/twocore_mut; fail=0; want="$*"
FLAGS=(-O1 -w -DHOST_MIX_FOLLOWS_MASTER=1 -DLRRY_TEST_MODE=1 -I$D/build/gen -I$D/firmware/src -I$D/tests -DNPART=1 -DNSHOWN=1 "-DLRRY_VERSION_SHOWN=\"$FV\"" $LRRY_CFX)
run_limited() {   # seconds, command ...: a mutant may hang the simulation (a wait that never ends is a detection too: it counts as "caught: timeout" and the log says so); the whole process group is stopped
  local lim=$1 pid wd rc; shift
  set -m
  "$@" > $OUT/twocore_mut.log 2>&1 &
  pid=$!
  ( sleep $lim; kill -- -$pid 2>/dev/null ) &
  wd=$!
  wait $pid; rc=$?
  kill $wd 2>/dev/null; wait $wd 2>/dev/null
  set +m
  [ $rc -ge 128 ] && echo "twocore: FAIL timeout after $lim s (the simulation hung)" >> $OUT/twocore_mut.log
  return $rc
}
mut() {   # name, file, sed expression, [sim arguments: default -nomut -nosweep]
  local name=$1 file=$2 expr=$3 args=${4:--nomut -nosweep}
  if [ -n "$want" ] && [[ " $want " != *" $name "* ]]; then return; fi
  rm -rf $D; mkdir -p $D/build
  cp -R firmware tests $D/ && cp -R build/gen $D/build/ || { echo "$name: copy failed"; fail=1; return; }
  if [ "$expr" != "-" ]; then
    sed -E "$expr" "$D/$file" > "$D/$file.new" && mv "$D/$file.new" "$D/$file"
    if cmp -s "$D/$file" "$file"; then echo "$name: FAIL (the sed expression changed nothing)"; fail=1; return; fi
  fi
  if ! cc "${FLAGS[@]}" -o $OUT/twocore_mut $D/tests/twocore_sim.c -lm -lpthread > $OUT/twocore_mut.log 2>&1; then echo "$name: FAIL (does not compile: $(grep -m1 error $OUT/twocore_mut.log))"; fail=1; return; fi
  if run_limited ${MUT_LIMIT:-240} $OUT/twocore_mut $args; then
    if [ "$expr" = "-" ]; then echo "$name: PASS (the unbroken copy passes: $(tail -1 $OUT/twocore_mut.log))"; else echo "$name: FAIL (the mutation was NOT detected)"; fail=1; fi
  else
    if [ "$expr" = "-" ]; then echo "$name: FAIL (the unbroken copy fails)"; fail=1; else echo "$name: PASS (caught: $(grep -c 'twocore: FAIL' $OUT/twocore_mut.log) checks failed; first: $(grep -m1 'twocore: FAIL' $OUT/twocore_mut.log | cut -c1-150))"; fi
  fi
}
mut ORIGINAL firmware/src/twocore_run.c -
mut NOWAIT firmware/src/twocore_run.c 's/^    tcr\.got = 1;$/    tcr.got = 1;\n    if (j) { tcr_add_acc(out, outR, n); tcr.open = 0; return; }   \/\/ MUTANT: core 0 adds core 1 sums without waiting for done/'
mut DOUBLE firmware/src/voice.c 's/lrry_render_set\(ti, ~\(c1 \| open\), out, outR, n\);/lrry_render_set(ti, ~(((c1 \& (c1 - 1u))) | open), out, outR, n);   \/\/ MUTANT: the lowest voice of core 1 is played by core 0 too/; s/lrry_render_set\(ti, ~\(c1 \| open\), cl, cr, n\);/lrry_render_set(ti, ~(((c1 \& (c1 - 1u))) | open), cl, cr, n);   \/\/ MUTANT: the lowest voice of core 1 is played by core 0 too/'
mut MISSING firmware/src/voice.c 's/lrry_render_set\(ti, ~\(c1 \| open\), out, outR, n\);/lrry_render_set(ti, ~(c1 | open | 1u), out, outR, n);   \/\/ MUTANT: voice 0 is played by neither core when it is core 0 s/; s/lrry_render_set\(ti, ~\(c1 \| open\), cl, cr, n\);/lrry_render_set(ti, ~(c1 | open | 1u), cl, cr, n);   \/\/ MUTANT: voice 0 is played by neither core when it is core 0 s/'
mut STALE firmware/src/twocore_run.c 's/^    tc_ctx\.pp = lrry_pp\[ti\];.*$/    { static int once; if (!once) { tc_ctx.pp = lrry_pp[ti]; once = 1; } }   \/\/ MUTANT: the patch copy is not refreshed/; s/^ *c->pp = lrry_pp\[ti\];.*$/    { static int once; if (!once) { c->pp = lrry_pp[ti]; once = 1; } }   \/\/ MUTANT: the patch copy is not refreshed/'
mut SHARED firmware/src/twocore_run.c 's/&tc_ctx\.m\[i\], tc_acc\[0\], tc_acc\[1\], CTL\);/\&tc_ctx.m[i], pscr[0].buf, pscr[0].bufR, CTL);   \/\/ MUTANT: core 1 adds into core 0 block/; s/&c->m\[i\], tc_acc\[0\], tc_acc\[1\], CTL\);/\&c->m[i], pscr[0].buf, pscr[0].bufR, CTL);   \/\/ MUTANT: core 1 adds into core 0 block/'
mut SHARED_STATE firmware/src/twocore_run.c 's/&lrry_vs\[0\]\[tc_ctx\.id\[i\]\]/\&lrry_vs[0][i]/; s/&lrry_vs\[0\]\[c->id\[i\]\]/\&lrry_vs[0][i]/'
mut PEND_OFF firmware/src/voice.c 's/if \(\(tcr_open_set\(\) >> vi\) & 1u\) \{/if (0) {/'
mut SPLIT_NOT_SOUNDING firmware/src/voice.c 's/^    return c1;$/    return c1 | (c1 ? 0x80u : 0u);   \/\/ MUTANT: a voice that does not sound is handed over/'
mut FADE_OFF firmware/src/twocore_run.c 's/tcr\.fade2 = 1;/tcr.fade2 = 0;/'
mut VLATE_RUN firmware/src/twocore_run.c 's/#define TCR_VLATE_RUN 0u/#define TCR_VLATE_RUN 1u/'
mut VWIN_ON firmware/src/twocore_run.c 's/#define TCR_VWIN_K 0u/#define TCR_VWIN_K 8u/'
mut NOANS_NOPAUSE firmware/src/twocore_run.c 's|tcr_pause\(TC_TICKS\(\), TCR_PW_FAULT\); /\* \[no answer\] \*/||'
mut FAULT_NOPAUSE firmware/src/twocore_run.c 's/    if \(!tcr\.paused\) \{/    if (!tcr.paused \&\& why != TCR_PW_FAULT) {/'
mut SILENT_NOPAUSE firmware/src/twocore_run.c 's|tcr_pause\(TC_TICKS\(\), TCR_PW_FAULT\); /\* \[silent\] \*/||'
mut WAIT_TO_HALF_END firmware/src/twocore_run.c 's/a, lim = tcr\.lim, t1;/a, lim = tcr.t0 + TCR_HALF_TICKS, t1;/; s/^    lim = tcr\.lim;$/    lim = tcr.t0 + TCR_HALF_TICKS;/'
mut NO_HASH firmware/src/twocore_run.c 's/if \(tc_ctx_hash\(&tc_ctx\) != arg >> 16 \|\| n != CTL\)/if (n != CTL)/; s/if \(tc_ctx_hash\(c\) != arg >> 16 \|\| n != CTL\)/if (n != CTL)/'
# cores-pipeline: the rules of the pipeline. Each must be caught. (Not with LRRY_CFX=-DLRRY_PIPELINE=0: the plain runner has none of this code.)
case "$LRRY_CFX" in *LRRY_PIPELINE=0*) exit $fail ;; esac
mut PIPE_NO_ADD firmware/src/twocore_run.c 's/^( *)tcr_add_acc\(out, outR, n\);$/\1;   \/\/ MUTANT: core 1 sums are never added to the block/'
mut PIPE_PLAY_FROM_LRJ firmware/src/twocore_run.c 's/^    tc_ctx_t \*c = &tc_ctxs\[buf & 1u\];$/    tc_ctx_t *c = \&tc_ctxs[buf \& 1u]; lrry_render_set(0, c->mask, out, outR, n); return;   \/\/ MUTANT: core 0 plays the job from this block voices, not from the job copy/'
mut PIPE_NODELAY_ONECORE firmware/src/voice.c 's/out\[i\] = pl\[i\];/out[i] = tcr_go_post() ? pl[i] : cl[i];   \/\/ MUTANT: no delay while one core plays: a switch changes the latency/; s/outR\[i\] = pr\[i\];/outR[i] = tcr_go_post() ? pr[i] : cr[i];/'
mut PIPE_COPY_SAME firmware/src/twocore_run.c 's/#define TC_CTXJ\(j\) \(tc_ctxs\[\(j\) & 1u\]\)/#define TC_CTXJ(j) (tc_ctxs[0])   \/\/ MUTANT: one block copy for every job/; s/&tc_ctxs\[\(arg >> 8\) & 1u\]/\&tc_ctxs[0]/; s/&tc_ctxs\[buf & 1u\]/\&tc_ctxs[0]/'
mut PIPE_COPY_PARITY firmware/src/twocore_run.c 's/\(j & 1u\) << 8/((j + 1u) \& 1u) << 8/'
mut PIPE_POST_OPEN firmware/src/voice.c 's/if \(prep && tcr_go_post\(\) && !open\) \{/if (prep \&\& tc.up) {   \/\/ MUTANT: a job is posted although the one before is still open/'
mut PIPE_NR_NOW firmware/src/voice.c 's/ret = lrry_pl_nr\[lrry_pl_cur \^ 1u\];/ret = nr;   \/\/ MUTANT: the part sounds when this block has voices, not the block before/'
mut PIPE_DL_AT_HALF_START firmware/src/twocore_run.c 's/^    tcr\.dl = tcr\.pdl;$/    tcr.dl = tcr.t0;   \/\/ MUTANT: the deadline of the job is the half start/'
mut PIPE_NO_ADMISSION firmware/src/twocore_run.c 's/^        if \(\(int32_t\)\(dl - now\) <= 0 \|\| nofit\) \{$/        if (0) {   \/\/ MUTANT: a job goes whatever the half has left/'
mut PIPE_STOP_DROP firmware/src/twocore_run.c 's/^            tcr_add_acc\(out, outR, n\);$/            ;   \/\/ MUTANT: a core 1 held with its job done loses the sums/' "-nomut"
mut PIPE_PP_STALE firmware/src/voice.c 's/^( *)lrry_pp_ver\[ti\]\+\+;/\1;   \/\/ MUTANT: the patch version never moves, the hand-over keeps its first copy of the patch/'
mut PIPE_PP_HASH firmware/src/twocore_run.c 's/tcr_hash_rest\(c, tcr\.pp_hash\[j & 1u\]\) << 16/tcr_hash_rest(c, 0x811C9DC5u) << 16/'
# (not mutated: tcr_settle's guard for the pending job. In the simulation a hung core 1 is frozen BEFORE it writes anything, so settling the pending job one block early is invisible in the samples; the unit's stop leaves half-written state)
exit $fail
