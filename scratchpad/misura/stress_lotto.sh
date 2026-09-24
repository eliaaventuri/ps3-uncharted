#!/bin/bash
# stress_lotto.sh <nbusy> <n> <prefisso> [VAR=..]: lotto.sh con <nbusy> cicli occupati sulla CPU.
nb=$1; shift; pids=()
for ((k=0; k<nb; k++)); do (while :; do :; done) & pids+=($!); done
"$(dirname "$0")/lotto.sh" "$@"
kill "${pids[@]}"
