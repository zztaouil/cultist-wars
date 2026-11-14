#!/bin/bash
# usage: sweep.sh OUT BUDGET MAPS SHUFFLE tag=spec ...
cd "$(dirname "$0")/../.."
OUT=$1; B=$2; M=$3; SH=$4; shift 4
for kv in "$@"; do
  tag=${kv%%=*}; spec=${kv#*=}
  for p in 0 1 2 3; do
    python3 tests/arena_match.py --a bot.c --b "$spec" --tag $tag --out $OUT --budget-ms $B --maps-used $M --shuffle-seed $SH --part $p --parts 4 > $OUT/log_${tag}_$p.txt 2>&1 &
  done
  wait
  python3 tests/arena_match.py --summarize --tag $tag --out $OUT >> $OUT/summary.txt
done
echo DONE >> $OUT/summary.txt
