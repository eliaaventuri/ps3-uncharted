#!/bin/bash
# prof2.sh <log> <sample_out> [VAR=val...] -- corsa, sample di 10 s a 100 s, kill
log=$1; out=$2; shift 2
D="$HOME/Documents/Coding/uncharted-data"
cd "$D" && env RSX_FRAMESTAT=50 "$@" ./run.sh > "$log" 2>&1 &
sleep 100
pid=$(pgrep -f "uncharted-data/uncharted" | head -1)
sample "$pid" 10 -mayDie -f "$out" >/dev/null 2>&1
sleep 3; pkill -9 -f "uncharted-data/uncharted"; sleep 1
echo "profilo fatto: $out"
