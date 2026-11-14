#!/bin/bash
# chunk.sh OUT BUDGET MAPS SHUFFLE SECS tag=spec  -- resumable slice, runs <SECS
cd "$(dirname "$0")/../.."
OUT=$1; B=$2; M=$3; SH=$4; T=$5; kv=$6; tag=${kv%%=*}; spec=${kv#*=}
for p in 0 1 2 3; do timeout $T python3 tests/arena_match.py --a bot.c --b "$spec" --tag $tag --out $OUT --budget-ms $B --maps-used $M --shuffle-seed $SH --part $p --parts 4 > /dev/null 2>&1 & done
wait; cat $OUT/match_${tag}_*.jsonl | wc -l; python3 tests/arena_match.py --summarize --tag $tag --out $OUT
