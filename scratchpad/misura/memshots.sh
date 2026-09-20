#!/bin/bash
# memshots.sh <secondi> <log> <prefisso_png> <da> <passo> <t_vmmap> [VAR=val ...]
# come shots2.sh, piu' un vmmap --summary del processo a <t_vmmap> s (righe IOAccelerator/TOTAL in <log>.vmmap)
secs=$1; log=$2; pre=$3; from=$4; step=$5; tvm=$6; shift 6
D="$HOME/Documents/Coding/uncharted-data"
if pgrep -f "uncharted-data/uncharted" >/dev/null; then echo "ERRORE: c'e' gia' un uncharted in esecuzione"; exit 1; fi
cd "$D" && env "$@" ./run.sh > "$log" 2>&1 &
[ -x /tmp/winid ] || swiftc -O "$HOME/Documents/Coding/ps3-uncharted/scratchpad/winid.swift" -o /tmp/winid
t=0; at=$from; vm_done=0
while [ $at -le $secs ]; do
  if [ $vm_done = 0 ] && [ $tvm -le $at ]; then sleep $((tvm-t)); t=$tvm; vm_done=1
     pid=$(pgrep -f "uncharted-data/uncharted" | head -1)
     { echo "[vmmap] t=$t s pid=$pid"; vmmap --summary "$pid" 2>/dev/null | grep -E "IOAccelerator|TOTAL|Physical footprint"; } >> "$log.vmmap"; fi
  sleep $((at-t)); t=$at; id=$(/tmp/winid uncharted 2>/dev/null | head -1); [ -n "$id" ] && screencapture -x -l "$id" "${pre}_${at}.png"; echo "[shot] $at s" >> "$log"; at=$((at+step)); done
sleep $((secs-t))
pkill -9 -f "uncharted-data/uncharted" 2>/dev/null; sleep 1
echo "fatto: $log ($(wc -l < "$log") righe)"
