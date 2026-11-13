#!/bin/bash
# usage: onepart.sh NPART NSHOWN tests...   (run from lrry)
N=$1; S=$2; shift 2; D=${TMPDIR:-/tmp}; mkdir -p build/mod_demo
FV=$(sed -n 's/^#define LRRY_VERSION_STR "\(.*\)"/\1/p' build/gen/lrry_version.h)
for t in "$@"; do f=tests/${t}_test.c; [ -f "$f" ] || { echo "$t: no file"; continue; }
  if cc -O2 -w -DHOST_MIX_FOLLOWS_MASTER=1 -DLRRY_TEST_MODE=1 -Ibuild/gen_test -Ibuild/gen -Ifirmware/src -Itests -DNPART=$N -DNSHOWN=$S "-DLRRY_VERSION_SHOWN=\"$FV\"" $LRRY_CFX -o $D/o_$t $f -lm > $D/c_$t.log 2>&1; then
    out=$($D/o_$t build/mod_demo 2>&1); rc=$?
    echo "$t: compiles, exit $rc; $(echo "$out" | grep -ciE 'fail') lines with FAIL; last: $(echo "$out" | tail -1 | cut -c1-110)"
  else echo "$t: does not compile: $(grep -m2 'error' $D/c_$t.log | cut -c1-150 | tr '\n' ' ')"; fi
done
