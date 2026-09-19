#!/bin/bash
# runshots.sh <secondi> <log> <prefisso_png> [VAR=val ...] -- screenshot a 95, 110, 125 s, kill -9 alla fine
secs=$1; log=$2; pre=$3; shift 3
D="$HOME/Documents/Coding/uncharted-data"
if pgrep -f "uncharted-data/uncharted" >/dev/null; then echo "ERRORE: c'e' gia' un uncharted in esecuzione"; exit 1; fi
cd "$D" && env "$@" ./run.sh > "$log" 2>&1 &
t=0
for at in 95 110 125; do sleep $((at-t)); t=$at; id=$(/tmp/winid uncharted 2>/dev/null | head -1); [ -n "$id" ] && screencapture -x -l "$id" "${pre}_${at}.png"; done
sleep $((secs-t))
pkill -9 -f "uncharted-data/uncharted" 2>/dev/null; sleep 1
echo "fatto: $log ($(wc -l < "$log") righe)"
