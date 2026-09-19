#!/bin/bash
# fps.sh <log> [f0] [f1] -- fps medi fra i fotogrammi f0 e f1 (default 2300-2500) dalle righe RSX_FRAMESTAT
log=$1; f0=${2:-2300}; f1=${3:-2500}
t0=$(grep -m1 "fotogramma #$f0:" "$log" | grep -oE "t=[0-9]+" | cut -c3-)
t1=$(grep -m1 "fotogramma #$f1:" "$log" | grep -oE "t=[0-9]+" | cut -c3-)
last=$(grep -oE "fotogramma #[0-9]+:" "$log" | tail -1)
if [ -n "$t0" ] && [ -n "$t1" ]; then echo "$log: f#$f0-$f1 = $(python3 -c "print(round(($f1-$f0)*1000/($t1-$t0),2))") fps (ultimo $last)"; else echo "$log: finestra non raggiunta (ultimo $last)"; fi
for f in 3000 3400; do :; done
t2=$(grep -m1 "fotogramma #3000:" "$log" | grep -oE "t=[0-9]+" | cut -c3-); t3=$(grep -m1 "fotogramma #3400:" "$log" | grep -oE "t=[0-9]+" | cut -c3-)
[ -n "$t2" ] && [ -n "$t3" ] && echo "  f#3000-3400 = $(python3 -c "print(round(400*1000/($t3-$t2),2))") fps"
echo "  stall/assert: $(grep -ciE 'stopd @|assert|ancora in volo|stuck on reference|deadlock|SIGSEGV|SIGBUS|crash' "$log")  morti: $(grep -c 'kPlayerDied' "$log")"
