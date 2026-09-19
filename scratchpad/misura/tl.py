import sys, re, statistics as st
# tl.py <log> <riga_da> <riga_a>: per ogni fotogramma (put -> put) nella finestra di righe:
#  attesa del flip (waitflip-in -> out, e il valore i = ms attesi), fase PPU (waitflip-out -> put dopo), fine walk
log, a, b = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
ev = []
for i, line in enumerate(open(log, errors='replace'), 1):
    if a <= i <= b and line.startswith('[tl] '):
        p = line.split(); ev.append((int(p[1]), p[2], int(p[3], 16)))
puts = [k for k, e in enumerate(ev) if e[1] == 'put']
frame, wait, waited_i, ppu, walk, timeouts = [], [], [], [], [], 0
for k0, k1 in zip(puts, puts[1:]):
    seg = ev[k0:k1]; t0 = seg[0][0]
    wi = [e for e in seg if e[1] == 'waitflip-in']; wo = [e for e in seg if e[1] == 'waitflip-out']; wk = [e for e in seg if e[1] == 'walk']
    if not (wi and wo): continue
    d = (ev[k1][0] - t0) / 1000.0
    if d > 400 or d < 0: continue
    frame.append(d); wait.append((wo[0][0] - wi[0][0]) / 1000.0); waited_i.append(wo[0][2]); ppu.append((ev[k1][0] - wo[0][0]) / 1000.0)
    if wk: walk.append((wk[-1][0] - t0) / 1000.0)
    if wo[0][2] >= 64: timeouts += 1
f = lambda v: f"med {st.median(v):.1f} media {st.mean(v):.1f} min {min(v):.1f} max {max(v):.1f}" if v else "-"
print(f"fotogrammi {len(frame)}: durata ms {f(frame)}")
print(f"  attesa flip ms {f(wait)}  (timeout 64: {timeouts}/{len(frame)})")
print(f"  fase PPU (waitflip-out -> put) ms {f(ppu)}")
print(f"  fine walk dal put ms {f(walk)}")
