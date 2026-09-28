#!/bin/bash
# Fa avanzare il port da solo.
#
# Ogni giro: esegue il titolo per un po', raccoglie gli indirizzi che il runtime
# ha riportato come "unresolved indirect call", li aggiunge alla lista funzioni,
# rilifta e ricostruisce. Si ferma quando un giro non ne produce di nuovi (o al
# limite di iterazioni), perche' a quel punto il blocco e' di altra natura e
# serve un occhio.
#
#   ./iterate.sh [numero-di-giri] [secondi-per-run]
set -u
R="$(cd "$(dirname "$0")/.." && pwd)"
D="${UNCHARTED_DATA:-$(dirname "$R")/uncharted-data}"
PY="$R/.venv/bin/python"
ROUNDS=${1:-8}
RUNSECS=${2:-100}

ACC="$D/analysis/extra_targets.txt"     # gli indirizzi accumulati finora
[ -f "$ACC" ] || echo -n "" > "$ACC"
LOG="$D/iterate.log"

echo "=== iterate: $ROUNDS giri, ${RUNSECS}s per run ===" | tee -a "$LOG"

for i in $(seq 1 "$ROUNDS"); do
    echo "--- giro $i/$ROUNDS $(date '+%H:%M:%S') ---" | tee -a "$LOG"

    RUNLOG="$D/run_$i.log"
    "$R/uncharted/run.sh" > "$RUNLOG" 2>&1 &
    BP=$!
    ( sleep "$RUNSECS"; kill -9 $BP 2>/dev/null ) &
    wait $BP 2>/dev/null

    LINES=$(wc -l < "$RUNLOG")
    NEW=$(grep -aoE "unresolved indirect call -> 0x[0-9A-F]+" "$RUNLOG" \
          | grep -oE "0x[0-9A-F]+" | sort -u)
    NUNRES=$(echo "$NEW" | grep -c "0x" || true)
    echo "  run: $LINES righe, $NUNRES target irrisolti distinti" | tee -a "$LOG"

    # quali sono davvero nuovi rispetto a quelli gia' accumulati?
    # Solo indirizzi dentro il segmento di codice e allineati: un salto a un
    # puntatore corrotto riporta valori come 0x6F535879 (ASCII "oSXy"), e
    # aggiungerli alla lista funzioni fa liftare spazzatura.
    ADDED=""
    for t in $NEW; do
        V=$((t))
        [ "$V" -ge 65536 ] && [ "$V" -lt 7849368 ] && [ $((V % 4)) -eq 0 ] || {
            echo "  scartato (fuori dal codice): $t" | tee -a "$LOG"; continue; }
        grep -qi "$t" "$ACC" || ADDED="$ADDED$t,"
    done

    if [ -z "$ADDED" ]; then
        echo "  nessun target nuovo: il blocco non e' piu' un ingresso mancante." | tee -a "$LOG"
        echo "  ultime righe utili:" | tee -a "$LOG"
        grep -avE "^Global,|spurs-|WAIT\]|SIGNAL\]|^\s*$" "$RUNLOG" | tail -6 | tee -a "$LOG"
        break
    fi

    echo "  nuovi: $ADDED" | tee -a "$LOG"
    printf "%s" "$ADDED" >> "$ACC"

    EXTRA=$(cat "$ACC" | sed 's/,$//')
    "$PY" "$R/tools/find_indirect_entries.py" "$D/EBOOT.elf" \
        --functions "$D/analysis/EBOOT.functions.orig.json" \
        --extra "$EXTRA" -o "$D/analysis/EBOOT.functions.json" 2>&1 | tail -1 | tee -a "$LOG"

    rm -rf "$D/recomp"
    "$PY" "$R/tools/ppu_lifter.py" "$D/EBOOT.elf" \
        --functions "$D/analysis/EBOOT.functions.json" \
        --hle-stubs "$D/analysis/EBOOT.imports.json" \
        --names "$D/analysis/names.json" --toc 0x7d3190 --spin-backoff 0x742388 --atomic-inc16 0x492064,0x492388 -o "$D/recomp" 2>&1 | tail -1 | tee -a "$LOG"

    "$R/uncharted/build.sh" 2>&1 | tail -1 | tee -a "$LOG"
done
echo "=== iterate finito $(date '+%H:%M:%S') ===" | tee -a "$LOG"
