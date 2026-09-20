import sys, statistics as st
# present.py <log> [t0 t1]: durata del present (present-in -> present-out, ms) per fotogramma
# fra t0 e t1 s di orologio, per fasce di draw (la durata cresce con i draw:
# confrontare A/B nella stessa fascia). Serve GCM_TIMELINE.
log = sys.argv[1]; t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 60; t1 = float(sys.argv[3]) if len(sys.argv) > 3 else 130
rows = []; tin = None; base = None; last = None; wrap = 0
for line in open(log, errors='replace'):
    if not line.startswith('[tl] '): continue
    p = line.split()
    if len(p) < 5: continue
    us = int(p[1])
    if last is not None and us < last - 50000000: wrap += 100000000   # orologio mod 1e8
    last = us; us += wrap
    if base is None: base = us
    if p[2] == 'present-in': tin = (us, int(p[4]))
    elif p[2] == 'present-out' and tin:
        d = us - tin[0]
        if 0 < d < 200000: rows.append(((tin[0] - base) / 1e6, tin[1], d / 1000.0))
        tin = None
sel = [r for r in rows if t0 <= r[0] <= t1]
print(f"{log}: {len(sel)} present fra {t0}-{t1} s, mediana {st.median([r[2] for r in sel]):.2f} ms")
for lo, hi in ((0, 500), (500, 1500), (1500, 2000), (2000, 2500), (2500, 3000), (3000, 5000)):
    b = [r[2] for r in sel if lo <= r[1] < hi]
    if len(b) >= 5: print(f"  draw {lo:4d}-{hi:4d}: n={len(b):4d} mediana {st.median(b):.2f} ms  q1 {sorted(b)[len(b)//4]:.2f} q3 {sorted(b)[3*len(b)//4]:.2f}")
