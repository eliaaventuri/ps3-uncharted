#!/bin/bash
# ablotto.sh <n> <prefisso>: n coppie di corse da 160 s, alternando SPURS_WSTAT=0/1.
# Riga per corsa in <prefisso>.tsv: nome, wstat, esito, ultimo fotogramma, assert PM.
n=$1; p=$2; D="$HOME/Documents/Coding/uncharted-data"; M=$(dirname "$0")
for ((i=1; i<=n; i++)); do
  for w in 0 1; do
    ww=$(( (w+i) % 2 ))   # alterna l'ordine fra le coppie
    name="${p}_${i}_w${ww}"
    if [ $ww = 1 ]; then r=$("$M/framecatch.sh" 160 "$name.log" "$D/$name.sample" SPURS_WSTAT=1)
    else r=$("$M/framecatch.sh" 160 "$name.log" "$D/$name.sample"); fi
    fr=$(grep -a "fotogramma #" "$D/$name.log" | tail -1 | grep -o '#[0-9]*')
    as=$(grep -a -c "spu-assert" "$D/$name.log")
    echo -e "$name\t$ww\t${r%% *}\t$fr\t$as" >> "$D/$p.tsv"
  done
done
