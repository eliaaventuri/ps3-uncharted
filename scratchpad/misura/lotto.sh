#!/bin/bash
# lotto.sh <n> <prefisso> [VAR=..]: n corse framecatch da 160 s con le variabili date.
# Riga per corsa in <prefisso>.tsv: nome, esito, ultimo fotogramma, assert PM, carico medio 1 min a fine corsa.
n=$1; p=$2; shift 2; D="$HOME/Documents/Coding/uncharted-data"; M=$(dirname "$0")
for ((i=1; i<=n; i++)); do
  name="${p}_$i"
  r=$("$M/framecatch.sh" 160 "$name.log" "$D/$name.sample" "$@")
  fr=$(grep -a "fotogramma #" "$D/$name.log" | tail -1 | grep -o '#[0-9]*')
  as=$(grep -a -c "spu-assert" "$D/$name.log")
  ld=$(sysctl -n vm.loadavg | awk '{print $2}')
  echo -e "$name\t${r%% *}\t$fr\t$as\tload=$ld" >> "$D/$p.tsv"
done
