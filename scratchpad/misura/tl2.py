import sys, statistics as st
# tl2.py <log> <riga_da>: per fotogramma (put N -> put N+1): put(N+1) - flip-done(N), put(N+1) - walk(N), present-out(N) - put(N)
log, a = sys.argv[1], int(sys.argv[2])
ev = []
for i, line in enumerate(open(log, errors='replace'), 1):
    if i >= a and line.startswith('[tl] '):
        p = line.split(); ev.append((int(p[1]), p[2], int(p[3], 16)))
puts = [k for k, e in enumerate(ev) if e[1] == 'put']
dfd, dwk, dpo, neg = [], [], [], 0
for k0, k1 in zip(puts, puts[1:]):
    seg = ev[k0:k1]; t0 = seg[0][0]; t1 = ev[k1][0]
    if t1 - t0 > 400000 or t1 < t0: continue
    fd = [e[0] for e in seg if e[1] == 'flip-done']; wk = [e[0] for e in seg if e[1] == 'walk']; po = [e[0] for e in seg if e[1] == 'present-out']
    if fd: dfd.append((t1 - fd[-1]) / 1000.0); neg += (t1 < fd[-1])
    if wk: dwk.append((t1 - wk[-1]) / 1000.0)
    if po: dpo.append((po[-1] - t0) / 1000.0)
f = lambda v: f"med {st.median(v):.1f} min {min(v):.1f} max {max(v):.1f} (n={len(v)})" if v else "-"
print(f"put(N+1) - flip-done(N) ms: {f(dfd)}  negativi: {neg}")
print(f"put(N+1) - walk(N) ms:      {f(dwk)}")
print(f"present-out(N) - put(N) ms: {f(dpo)}")
