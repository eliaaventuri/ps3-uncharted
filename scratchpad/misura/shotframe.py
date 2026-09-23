import sys, re
# shotframe.py <log>: per ogni screenshot (<log>.shots, mono_ms) il numero del fotogramma RSX_FRAMESTAT piu' vicino
log = sys.argv[1]
pts = []
for line in open(log, errors='replace'):
    m = re.search(r'fotogramma #(\d+): .* t=(\d+)ms', line)
    if m: pts.append((int(m.group(2)), int(m.group(1))))
for line in open(log + '.shots'):
    m = re.search(r'\[shot\] (\d+) s mono_ms=(\d+)', line)
    if not m: continue
    at, ms = int(m.group(1)), int(m.group(2))
    best = min(pts, key=lambda p: abs(p[0] - ms)) if pts else (0, 0)
    print(f"shot {at}: f#{best[1]} ({(ms - best[0])/1000:+.1f} s)")
