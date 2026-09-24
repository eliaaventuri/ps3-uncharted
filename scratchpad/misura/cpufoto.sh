#!/bin/bash
# cpufoto.sh <secondi> <log> [t0] [VAR=..]: quanto processore costa un fotogramma.
# Corsa con RSX_FRAMESTAT=1; da t0 secondi (default 60: livello gia' caricato)
# alla fine legge il tempo CPU del processo e l'ultimo fotogramma, e stampa
#   ms di CPU per fotogramma, core medi occupati, fps.
# Prima misura (24/09/2026, corsa s44ad_18 con SPU_ASSERT_DMA=1, 30 s nel
# livello): 271 ms CPU/fotogramma, 6,52 core, 24,1 fps.
# E' il numero da far scendere: gli fps sono al tetto (vsync a 30), il calore no.
# Da confrontare solo fra corse con lo stesso t0/secondi (il contenuto conta, s.39).
secs=$1; log=$2; shift 2
t0=60; case "${1:-}" in ''|*=*) ;; *) t0=$1; shift;; esac
R="$(cd "$(dirname "$0")/../.." && pwd)"
D="${UNCHARTED_DATA:-$(dirname "$R")/uncharted-data}"
[ -d /tmp/uc_iso/PS3_GAME/USRDIR ] || { echo "ERRORE: ISO non montata (/tmp/uc_iso)"; exit 1; }
if pgrep -f "uncharted-data/uncharted" >/dev/null; then echo "ERRORE: uncharted gia' in esecuzione"; exit 1; fi
cd "$D" && env RSX_FRAMESTAT=1 "$@" "$R/uncharted/run.sh" > "$log" 2>&1 &
sleep 3; pid=$(pgrep -x uncharted | head -1)
[ -n "$pid" ] || { echo "ERRORE: il port non e' partito (vedi $log)"; exit 1; }

cpu() { ps -o cputime= -p "$pid" | python3 -c "
import sys; p=sys.stdin.read().strip().split(':'); s=0.0
for x in p: s=s*60+float(x)
print(s)"; }
# tempo CPU (s) di ogni thread, in ordine di lista (stabile a livello caricato)
thr() { ps -M -p "$pid" | awk 'NR>1 {t=0; for(i=NF-1;i<=NF;i++){n=split($i,a,":"); v=0; for(j=1;j<=n;j++) v=v*60+a[j]; t+=v}; printf "%s ", t}'; }
frame() { grep -a -oE "fotogramma #[0-9]+:" "$D/$log" | tail -1 | grep -oE "[0-9]+"; }

sleep $((t0 - 3))
c0=$(cpu); t0s=$(thr); f0=$(frame); w0=$(python3 -c 'import time; print(time.time())')
sleep $((secs - t0))
c1=$(cpu); t1s=$(thr); f1=$(frame); w1=$(python3 -c 'import time; print(time.time())')
pkill -9 -f "uncharted-data/uncharted"; sleep 1

D="$D" python3 - "$c0" "$c1" "${f0:-0}" "${f1:-0}" "$w0" "$w1" "$log" "$t0s" "$t1s" <<'EOF'
import sys
c0,c1,f0,f1,w0,w1=map(float,sys.argv[1:7]); log=sys.argv[7]
ta=[float(x) for x in sys.argv[8].split()]; tb=[float(x) for x in sys.argv[9].split()]
fr=f1-f0; wall=w1-w0; cpu=c1-c0
if fr <= 0: print(f"{log}: nessun fotogramma nella finestra (#{int(f0)} -> #{int(f1)})"); sys.exit()
# s.45: draw medi per fotogramma nella finestra -- la CPU segue il contenuto
# (~6 ms ogni 100 draw), confrontare le corse a parita' di draw.
import re; dr=[int(m.group(2)) for m in re.finditer(rb"fotogramma #(\d+): negli ultimi 1 presentati (\d+) draw", open(__import__('os').environ['D']+'/'+log,'rb').read()) if f0 <= int(m.group(1)) <= f1]
print(f"{log}: {cpu*1000/fr:.0f} ms CPU/fotogramma, {cpu/wall:.2f} core medi, "
      f"{fr/wall:.1f} fps, fotogrammi #{int(f0)}-#{int(f1)}, {sum(dr)//max(len(dr),1)} draw medi")
# per thread (ms CPU/fotogramma, i 9 piu' pesanti): lane SPU, PPU, ticker...
if ta and tb:  # un thread nato o finito nella finestra: zip tronca, gli indici bassi restano allineati
    d = sorted(((i, (b - a) * 1000 / fr) for i, (a, b) in enumerate(zip(ta, tb))), key=lambda x: -x[1])[:9]
    print("   thread (indice:ms/fotogramma): " + " ".join(f"{i}:{x:.0f}" for i, x in d))
EOF
