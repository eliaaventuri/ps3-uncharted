#!/bin/bash
# framecatch.sh <secondi> <log> <campione> [VAR=..]: corsa con RSX_FRAMESTAT=1; se i
# fotogrammi smettono di avanzare per 20 s (dopo il primo), campiona il processo
# vivo con `sample` (pile host di tutti i thread, func_XXXX del codice liftato) e
# chiude. Esito su stdout: FERMO@<fotogramma> oppure OK.
secs=$1; log=$2; out=$3; shift 3
D="$HOME/Documents/Coding/uncharted-data"
[ -d /tmp/uc_iso/PS3_GAME/USRDIR ] || { echo "ERRORE: ISO non montata (/tmp/uc_iso)"; exit 1; }
if pgrep -f "uncharted-data/uncharted" >/dev/null; then echo "ERRORE: uncharted gia' in esecuzione"; exit 1; fi
cd "$D" && env RSX_FRAMESTAT=1 "$@" ./run.sh > "$log" 2>&1 &
last=""; same=0; res="OK"
for ((t=0; t<secs; t+=2)); do
    sleep 2
    cur=$(grep -a "fotogramma #" "$D/$log" 2>/dev/null | tail -1 | cut -c1-40)
    if [ -n "$cur" ] && [ "$cur" = "$last" ]; then same=$((same+2)); else same=0; fi
    last=$cur
    if [ $same -ge 20 ]; then
        pid=$(pgrep -x uncharted | head -1)
        sample "$pid" 3 -file "$out" >/dev/null 2>&1
        # FC_PEEK="ea1 ea2 ...": 32 byte di memoria guest per indirizzo (big-endian
        # grezzo), letti dal processo vivo con lldb, in "$out.peek".
        if [ -n "$FC_PEEK" ]; then
            cmds=(); for a in $FC_PEEK; do cmds+=(-o "memory read -s1 -c32 -fx '(char*)vm_base+$a'"); done
            lldb -p "$pid" --batch "${cmds[@]}" > "$out.peek" 2>&1
        fi
        # FC_DUMP=1: senza sonda SPU_ROWWATCH, fa stampare al processo fermo lo stato
        # di tutte le job list (spurs_pm_dump_lists, righe [rowwatch] nel log).
        if [ -n "$FC_DUMP" ]; then
            lldb -p "$pid" --batch -o "expr (void)spurs_pm_dump_lists(0x2F80)" > "$out.dump" 2>&1
            sleep 1
        fi
        res="FERMO@$(echo "$cur" | grep -o '#[0-9]*')"; break
    fi
done
pkill -9 -f "uncharted-data/uncharted"; sleep 1
echo "$res $log"
