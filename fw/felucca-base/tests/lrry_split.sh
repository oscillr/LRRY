#!/bin/bash
# L/RRY phase 4.3b: run the split-by-voice checks (host only). From felucca-base, with the generated headers in build/gen:
#   tests/lrry_split.sh [OLD_TREE]
# 1 builds tests/lrry_split_test.c (one core, no second-core code) and tests/twocore_sim.c (two cores on threads) with the flags of fw/onepart.sh (NPART=1 NSHOWN=1), runs both,
# 2 compares the scene hashes the two programs print (the one-core program's against the simulation's reference run),
# 3 with OLD_TREE (a checkout of the 4.3a commit, felucca-base, with its build/gen) builds its tests/lrry_split_test.c (the new file copied in, the allocator part left out) and compares those hashes too,
# 4 runs tests/twocore_mut.sh (the mutants) when MUT=1.
FV=$(sed -n 's/^#define DUELL_VERSION_STR "\(.*\)"/\1/p' build/gen/duell_version.h); T=${TMPDIR:-/tmp}; fail=0
CF=(-O1 -w -DHOST_MIX_FOLLOWS_MASTER=1 -DDUELL_TEST_MODE=1 -Ibuild/gen -Ifirmware/src -Itests -DNPART=1 -DNSHOWN=1 "-DFELUCCA_VERSION=\"$FV\"")
cc "${CF[@]}" -o $T/lrry_split tests/lrry_split_test.c -lm || exit 1
cc "${CF[@]}" -o $T/twocore_sim tests/twocore_sim.c -lm -lpthread || exit 1
$T/lrry_split > $T/lrry_split.log; rc1=$?; tail -1 $T/lrry_split.log
$T/twocore_sim > $T/twocore_sim.log; rc2=$?; tail -2 $T/twocore_sim.log
[ $rc1 = 0 ] && [ $rc2 = 0 ] || fail=1
grep -o 'scene [0-9] ([a-z]*) hash [0-9a-f]*' $T/lrry_split.log | sed 's/ hash / /' > $T/h_one.txt
grep -o 'scene [0-9] ([a-z]*): reference hash [0-9a-f]*' $T/twocore_sim.log | sed 's/: reference hash / /' > $T/h_sim.txt
if cmp -s $T/h_one.txt $T/h_sim.txt && [ -s $T/h_one.txt ]; then echo "one-core program and the simulation's reference: $(wc -l < $T/h_one.txt) scene hashes equal"; else echo "FAIL: scene hashes differ"; diff $T/h_one.txt $T/h_sim.txt; fail=1; fi
if [ -n "$1" ]; then
  O=$1; cp tests/lrry_scn.h $O/tests/
  sed -e '/the allocator: n notes/,/^    printf("lrry_split: %d checks/{/printf/!d}' tests/lrry_split_test.c > $O/tests/lrry_split_test.c
  (cd $O && cc "${CF[@]}" -o $T/lrry_split_old tests/lrry_split_test.c -lm && $T/lrry_split_old | grep -o 'scene [0-9] ([a-z]*) hash [0-9a-f]*' | sed 's/ hash / /' > $T/h_old.txt)
  if cmp -s $T/h_one.txt $T/h_old.txt; then echo "4.3a tree and this tree, one core: $(wc -l < $T/h_old.txt) scene hashes equal"; else echo "FAIL: 4.3a hashes differ"; diff $T/h_one.txt $T/h_old.txt; fail=1; fi
fi
[ "$MUT" = 1 ] && { tests/twocore_mut.sh || fail=1; }
exit $fail
