#!/bin/bash
# fpsmed.sh <log> [t0 t1]: mediana e quartili del tempo per fotogramma (blocchi RSX_FRAMESTAT da 50) fra t0 e t1 s
log=$1; t0=${2:-100}; t1=${3:-190}
python3 - "$log" "$t0" "$t1" <<'PY'
import sys, re, statistics as st
log, t0, t1 = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
pts = []
for line in open(log, errors='replace'):
    m = re.search(r'fotogramma #(\d+): .* t=(\d+)ms', line)
    if m: pts.append((int(m.group(1)), int(m.group(2))))
base = pts[0][1]
ft = [(t - tp) / (f - fp) for (fp, tp), (f, t) in zip(pts, pts[1:]) if t0 <= (t - base) / 1000 <= t1 and f > fp]
ft.sort(); n = len(ft)
print(f"{log}: blocchi {n}, ms/fotogramma mediana {st.median(ft):.1f} (q1 {ft[n//4]:.1f} q3 {ft[3*n//4]:.1f}) => {1000/st.median(ft):.2f} fps mediani")
PY
