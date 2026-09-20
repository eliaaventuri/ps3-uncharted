#!/bin/bash
# shots2.sh <secondi> <log> <prefisso_png> <da> <passo> [VAR=val ...] -- screenshot ogni <passo> s da <da> s, kill -9 alla fine
secs=$1; log=$2; pre=$3; from=$4; step=$5; shift 5
D="$HOME/Documents/Coding/uncharted-data"
if pgrep -f "uncharted-data/uncharted" >/dev/null; then echo "ERRORE: c'e' gia' un uncharted in esecuzione"; exit 1; fi
cd "$D" && env "$@" ./run.sh > "$log" 2>&1 &
t=0
at=$from
while [ $at -le $secs ]; do sleep $((at-t)); t=$at; id=$(/tmp/winid uncharted 2>/dev/null | head -1); [ -n "$id" ] && screencapture -x -l "$id" "${pre}_${at}.png"; echo "[shot] $at s $(date +%H:%M:%S.%N | cut -c1-12)" >> "$log"; at=$((at+step)); done
sleep $((secs-t))
pkill -9 -f "uncharted-data/uncharted" 2>/dev/null; sleep 1
echo "fatto: $log ($(wc -l < "$log") righe)"
