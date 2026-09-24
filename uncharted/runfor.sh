#!/bin/bash
# runfor.sh <secondi> <log> [VAR=val ...] -- lancia run.sh, aspetta, uccide con -9, verifica.
secs=$1; log=$2; shift 2
R="$(cd "$(dirname "$0")/.." && pwd)"
D="${UNCHARTED_DATA:-$(dirname "$R")/uncharted-data}"
if pgrep -f "uncharted-data/uncharted" >/dev/null; then echo "ERRORE: c'e' gia' un uncharted in esecuzione"; exit 1; fi
cd "$D" && env "$@" "$R/uncharted/run.sh" > "$log" 2>&1 &
pid=$!
sleep "$secs"
kill -9 "$pid" 2>/dev/null; pkill -9 -f "uncharted-data/uncharted" 2>/dev/null
sleep 1
left=$(pgrep -f "uncharted-data/uncharted" | wc -l | tr -d ' ')
echo "fatto: $log ($(wc -l < "$D/$log") righe), processi rimasti: $left"
