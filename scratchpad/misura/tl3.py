import sys, statistics as st
# tl3.py <log> <f0> <f1>: durata put->put (ms) fra i fotogrammi RSX f0 e f1, e frazione del tempo fra walk(N) e put(N+1)
log, f0, f1 = sys.argv[1], sys.argv[2], sys.argv[3]
a = b = None; ev = []
for i, line in enumerate(open(log, errors='replace'), 1):
    if a is None and f"fotogramma #{f0}:" in line: a = i
    if a is not None and f"fotogramma #{f1}:" in line: b = i; break
    if a is not None and line.startswith('[tl] '):
        p = line.split(); ev.append((int(p[1]), p[2]))
puts = [k for k, e in enumerate(ev) if e[1] == 'put']
d = [(ev[k1][0] - ev[k0][0]) / 1000 for k0, k1 in zip(puts, puts[1:]) if 0 < ev[k1][0] - ev[k0][0] < 400000]
print(f"{log} f#{f0}-{f1}: {len(d)} put->put, med {st.median(d):.1f} ms, media {st.mean(d):.1f} ms => {1000/st.mean(d):.2f} fps")
