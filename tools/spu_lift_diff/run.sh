#!/bin/bash
# Banco differenziale del lifter SPU (s.44): stessi moduli di job liftati in
# due modi, stessi stati di partenza, confronto dello stato finale e del tempo.
#   tools/spu_lift_diff/run.sh <cartella di lavoro> "<flag lift A>" "<flag lift B>" [casi per funzione]
#   es.: tools/spu_lift_diff/run.sh /tmp/d "" "--reg-cache" 20
# TIME=1: invece del confronto, secondi dentro il codice liftato (GEN_FLOAT=1
# EVERY=0, 10 casi per funzione). HELPERS_A/HELPERS_B: una copia alternativa
# di runtime/spu per quella variante (per provare modifiche agli helper senza
# toccare il tree). Compila con clang -O2 -DSPU_LS_NODIAG come build.sh, 4
# alla volta: ~3 minuti per variante, il confronto ~7 minuti.
set -e
R="$(cd "$(dirname "$0")/../.." && pwd)"
D="${UNCHARTED_DATA:-$(dirname "$R")/uncharted-data}"
PY="$R/.venv/bin/python"; [ -x "$PY" ] || PY=python3
H="$R/tools/spu_lift_diff"
O=$1; FA=$2; FB=$3; K=${4:-20}
mkdir -p "$O"
for v in A B; do
  [ -d "$O/$v" ] && continue
  f=FA; [ $v = B ] && f=FB
  "$PY" "$R/tools/lift_jobmods.py" --jobmods "$D/spu_jobs" --lifted "$O/$v" \
      --register-c "$O/$v.reg.c" --register-fn uncharted_jobmods_register_all \
      --image-base 200 --fallthrough-tailcall --merge-blocks ${!f} > "$O/$v.lift.log"
  hv=HELPERS_$v; inc=()
  if [ -n "${!hv}" ]; then     # "../../runtime/spu/x.h" dal liftato si risolve qui
    mkdir -p "$O/inc$v/a/b" "$O/inc$v/runtime"; ln -sfn "${!hv}" "$O/inc$v/runtime/spu"
    inc=(-I "$O/inc$v/a/b")
  fi
  for s in "$O/$v"/jobmod_*/spu_recomp.c; do
    m=$(basename "$(dirname "$s")")
    while [ $(jobs -r | wc -l) -ge 4 ]; do sleep 0.3; done
    ( clang -std=gnu17 -O2 -DSPU_LS_NODIAG -w -include "$H/hdefs.h" "${inc[@]}" -I "$R/include" \
          -I "$R/runtime/spu" -I "$R/runtime/ppu" -c "$s" -o "$O/$v/$m.o" &&
      clang -std=gnu17 -O2 -DSPU_LS_NODIAG -w -DREGFN=${m}_spu_recomp_register -I "$H" \
          -I "$R/include" -I "$R/runtime/spu" -c "$H/harness.c" -o "$O/$v/$m.h.o" &&
      clang "$O/$v/$m.o" "$O/$v/$m.h.o" -o "$O/$v/$m.exe" ) || echo "ERRORE $v $m" &
  done
  wait
done
tot=0; bad=0
for e in "$O"/A/jobmod_*.exe; do
  m=$(basename "$e" .exe); img="$D/spu_jobs/$m.bin"
  nf=$("$e" "$img" 1 bench 2>/dev/null | awk '/^#/{print $4}')
  if [ -n "$TIME" ]; then
    ta=$(GEN_FLOAT=1 EVERY=0 "$e" "$img" $((nf * 10)) bench 2>/dev/null | awk '/^#/{print $6}')
    tb=$(GEN_FLOAT=1 EVERY=0 "$O/B/$m.exe" "$img" $((nf * 10)) bench 2>/dev/null | awk '/^#/{print $6}')
    echo "${m#jobmod_} A=$ta B=$tb"; continue
  fi
  "$e" "$img" $((nf * K)) 2>/dev/null | grep -v '^#' > "$O/A_$m.txt" &
  "$O/B/$m.exe" "$img" $((nf * K)) 2>/dev/null | grep -v '^#' > "$O/B_$m.txt" &
  wait
  c=$(( $(wc -l < "$O/A_$m.txt") )); d=$(diff "$O/A_$m.txt" "$O/B_$m.txt" | grep -c '^<' || true)
  echo "${m#jobmod_} casi=$c diversi=$d"
  tot=$((tot + c)); bad=$((bad + d))
done
[ -z "$TIME" ] && echo "TOTALE casi=$tot diversi=$bad"
