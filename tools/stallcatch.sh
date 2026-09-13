#!/bin/bash
# stallcatch.sh <log> [VAR=..]: avvia, aspetta la richiesta di sky-dive-boat, se dopo 45 s non e' caricato campiona il processo
log=$1; shift
D="$HOME/Documents/Coding/uncharted-data"; S=${STALL_OUT:-/tmp}
cd "$D" && env "$@" ./run.sh > "$log" 2>&1 &
for i in $(seq 1 150); do sleep 1; grep -q "RequestLevel \[sky-dive-boat\]" "$D/$log" 2>/dev/null && break; done
echo "richiesta a t=$i s"
sleep 45
if grep -q "'sky-dive-boat': loaded" "$D/$log"; then echo "CARICATO (nessuno stallo)"; else
  pid=$(pgrep -x uncharted | head -1); echo "STALLO: campiono pid $pid"
  sample $pid 3 -file "$S/sample_stall.txt" >/dev/null 2>&1; echo "campione in $S/sample_stall.txt"
fi
pkill -9 -f "uncharted-data/uncharted"; sleep 1; echo "fatto: $log ($(wc -l < "$D/$log") righe)"
