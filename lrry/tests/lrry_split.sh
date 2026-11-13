#!/bin/bash
# L/RRY phase 4.3b: run the split-by-voice checks (host only). From lrry, with the generated headers in build/gen:
#   tests/lrry_split.sh [OLD_TREE]
# 1 builds tests/lrry_split_test.c (one core, no second-core code) and tests/twocore_sim.c (two cores on threads) with the flags of fw/onepart.sh (NPART=1 NSHOWN=1), runs both,
# 2 compares the scene hashes the two programs print (the one-core program's against the simulation's reference run),
# 3 with OLD_TREE (a checkout of the 4.3a commit, lrry, with its build/gen) builds its tests/lrry_split_test.c (the new file copied in, the allocator part left out) and compares those hashes too,
# 4 runs tests/twocore_mut.sh (the mutants) when MUT=1.
FV=$(sed -n 's/^#define LRRY_VERSION_STR "\(.*\)"/\1/p' build/gen/lrry_version.h); T=${TMPDIR:-/tmp}; fail=0
CF=(-O1 -w -DHOST_MIX_FOLLOWS_MASTER=1 -DLRRY_TEST_MODE=1 -Ibuild/gen -Ifirmware/src -Itests -DNPART=1 -DNSHOWN=1 "-DLRRY_VERSION_SHOWN=\"$FV\"" $LRRY_CFX)   # LRRY_CFX: extra flags, e.g. -DLRRY_NVOICE=12 (the bench build)
cc "${CF[@]}" -o $T/lrry_split tests/lrry_split_test.c -lm || exit 1
cc "${CF[@]}" -o $T/twocore_sim tests/twocore_sim.c -lm -lpthread || exit 1
export LRRY_PLAIN_DIR=$T/lrry_plain; rm -rf $LRRY_PLAIN_DIR; mkdir -p $LRRY_PLAIN_DIR   # cores-pipeline: the one-core program writes its samples, the simulation compares its own one-core reference with them (delayed by one block with LRRY_PIPELINE)
$T/lrry_split > $T/lrry_split.log; rc1=$?; tail -1 $T/lrry_split.log
$T/twocore_sim > $T/twocore_sim.log; rc2=$?; tail -3 $T/twocore_sim.log
[ $rc1 = 0 ] && [ $rc2 = 0 ] || fail=1
grep -o 'scene [0-9] ([a-z]*) hash [0-9a-f]*' $T/lrry_split.log | sed 's/ hash / /' > $T/h_one.txt
grep -o 'scene [0-9] ([a-z]*): reference hash [0-9a-f]*' $T/twocore_sim.log | sed 's/: reference hash / /' > $T/h_sim.txt
if grep -q "delayed by exactly one block" $T/twocore_sim.log; then   # PIPELINE: the reference is the one-core program's output delayed by a block: no hash to compare, the simulation compared every sample itself
  n=$(sed -n "s/^twocore: \([0-9]*\) scenes' one-core reference equal.*/\1/p" $T/twocore_sim.log); k=$(grep -c "reference hash" $T/twocore_sim.log)
  if [ "$n" = "$k" ] && [ "$n" -gt 0 ]; then echo "one-core program and the simulation's reference: $n scenes equal sample for sample, delayed by one block"; else echo "FAIL: the delayed one-core comparison did not run for every scene ($n of $k)"; fail=1; fi
elif cmp -s $T/h_one.txt $T/h_sim.txt && [ -s $T/h_one.txt ]; then echo "one-core program and the simulation's reference: $(wc -l < $T/h_one.txt) scene hashes equal"; else echo "FAIL: scene hashes differ"; diff $T/h_one.txt $T/h_sim.txt; fail=1; fi
if [ -n "$1" ]; then
  O=$1; cp tests/lrry_scn.h tests/lrry_split_test.c $O/tests/; rm -f $T/h_old.txt
  if (cd $O && cc "${CF[@]}" -DLRRY_OLD_TREE=1 -o $T/lrry_split_old tests/lrry_split_test.c -lm); then
    $T/lrry_split_old | grep -o 'scene [0-9] ([a-z]*) hash [0-9a-f]*' | sed 's/ hash / /' > $T/h_old.txt
    if cmp -s $T/h_one.txt $T/h_old.txt; then echo "4.3a tree and this tree, one core: $(wc -l < $T/h_old.txt) scene hashes equal"; else echo "FAIL: 4.3a hashes differ"; diff $T/h_one.txt $T/h_old.txt; fail=1; fi
  else echo "FAIL: the 4.3a tree's test does not compile"; fail=1; fi
fi
# cores-balance: the split RULE's mutants (voice.c): each copy of the firmware with ONE rule of lrry_split_choose broken must make tests/lrry_split_test.c FAIL
rule_mut() {
  local name=$1 expr=$2 D=build/split_mut
  rm -rf $D; mkdir -p $D/build; cp -R firmware tests $D/ && cp -R build/gen $D/build/ || { echo "$name: copy failed"; fail=1; return; }
  sed -E "$expr" $D/firmware/src/voice.c > $D/firmware/src/voice.c.new && mv $D/firmware/src/voice.c.new $D/firmware/src/voice.c
  if cmp -s $D/firmware/src/voice.c firmware/src/voice.c; then echo "$name: FAIL (the sed expression changed nothing)"; fail=1; return; fi
  if ! (cd $D && cc "${CF[@]}" -o $T/lrry_split_rmut tests/lrry_split_test.c -lm > $T/lrry_split_rmut.log 2>&1); then echo "$name: FAIL (does not compile)"; fail=1; return; fi
  if $T/lrry_split_rmut > $T/lrry_split_rmut.log 2>&1; then echo "$name: FAIL (the mutation was NOT detected)"; fail=1; else echo "$name: PASS (caught: $(grep -c ' FAIL$' $T/lrry_split_rmut.log) checks failed)"; fi
  rm -rf $D
}
if [ "$MUT" = 1 ]; then
  rule_mut RULE_PAY_OFF 's/if \(c1 && lrry_cost\(mask, c1, w\) >= all0\)/if (0)/'
  rule_mut RULE_HO0_FREE 's/return c1 \? t \+ LRRY_SP\.ho0 : t;/return t;/'
  rule_mut RULE_OUTSIDE_MASK 's/^    return c1;$/    return c1 | (c1 ? 1u : 0u);/'
fi
[ "$MUT" = 1 ] && { tests/twocore_mut.sh || fail=1; }
exit $fail
