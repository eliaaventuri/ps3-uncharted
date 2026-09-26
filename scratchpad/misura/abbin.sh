#!/bin/bash
# abbin.sh <n> <prefisso> [secondi]: A/B di DUE ESEGUIBILI (s.47), per le modifiche
# senza interruttore a runtime. Servono $D/uncharted_on e $D/uncharted_off; a ogni
# corsa uno dei due viene copiato su $D/uncharted, alternando l'ordine fra le coppie.
# Ogni corsa e' un cpufoto.sh (una riga per corsa in <prefisso>.txt).
# Alla fine rimette uncharted_on al suo posto.
n=$1; p=$2; secs=${3:-160}; D="$HOME/Documents/Coding/uncharted-data"; M=$(dirname "$0")
for ((i=1; i<=n; i++)); do
  for k in 0 1; do
    v=$([ $(( (k+i) % 2 )) = 1 ] && echo on || echo off)
    cp "$D/uncharted_$v" "$D/uncharted"
    "$M/cpufoto.sh" "$secs" "${p}_${i}_$v.log" | head -1 >> "$D/$p.txt"
  done
done
cp "$D/uncharted_on" "$D/uncharted"
