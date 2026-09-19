#!/bin/bash
# fpswall.sh <log> [t0 t1]: fps medi fra t0 e t1 secondi dal primo fotogramma (righe RSX_FRAMESTAT ogni 50)
log=$1; t0=${2:-100}; t1=${3:-190}
python3 - "$log" "$t0" "$t1" <<'PY'
import sys, re
log, t0, t1 = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
pts = []
for line in open(log, errors='replace'):
    m = re.search(r'fotogramma #(\d+): .* t=(\d+)ms', line)
    if m: pts.append((int(m.group(1)), int(m.group(2))))
if not pts: print(log, "nessun framestat"); sys.exit()
base = pts[0][1]
sel = [(f, (t - base) / 1000) for f, t in pts if t0 <= (t - base) / 1000 <= t1]
if len(sel) < 2: print(log, "finestra vuota"); sys.exit()
(f0, s0), (f1, s1) = sel[0], sel[-1]
print(f"{log}: {f1 - f0} fotogrammi in {s1 - s0:.0f} s ({s0:.0f}-{s1:.0f} s) = {(f1 - f0) / (s1 - s0):.2f} fps; morti {sum(1 for l in open(log, errors='replace') if 'kPlayerDied' in l)}")
PY
