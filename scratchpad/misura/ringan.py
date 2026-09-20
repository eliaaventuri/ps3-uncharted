import sys, re
# ringan.py <log>: dal GCM_RING_TRACE, per lane: posizione virtuale del produttore (blocchi (r12,r5]) e del
# consumatore (RELEASE); segnala i blocchi scritti oltre un giro rispetto al consumatore (gap > SIZE).
SIZE = 0x199980
log = sys.argv[1]
st = {}
ev = 0; over = []; shots = []
for ln, line in enumerate(open(log, errors='replace'), 1):
    if line.startswith('[shot]'): shots.append((ln, line.strip())); continue
    if not line.startswith('[ring] '): continue
    p = line.split()
    lane = int(p[3]); s = st.setdefault(lane, dict(lap_p=0, lap_c=0, last_r12=None, last_rel=None, prod=0, cons=None, maxgap=0, blocks=0, over=0, labelback=0, last_label=None))
    if p[4] == 'check':
        r12 = int(p[5][4:], 16); r5 = int(p[6][3:], 16); label = int(p[7][6:], 16); r23 = int(p[8][4:], 16); r26 = int(p[9][4:], 16)
        wrap = r5 > r26; end = r23 if wrap else r5
        if s['last_r12'] is None or r12 != s['last_r12']:
            if s['last_r12'] is not None and r12 + SIZE // 2 < s['last_r12']: s['lap_p'] += 1
            s['last_r12'] = r12; s['blocks'] += 1
            s['prod'] = (s['lap_p'] + (1 if wrap else 0)) * SIZE + end
            if s['cons'] is not None:
                gap = s['prod'] - s['cons']
                if gap > s['maxgap']: s['maxgap'] = gap
                if gap > SIZE: s['over'] += 1; over.append((ln, lane, r12, r5, gap - SIZE))
    elif p[4] == 'RELEASE':
        v = int(p[5], 16)
        if s['last_rel'] is not None and v + SIZE // 2 < s['last_rel']: s['lap_c'] += 1
        s['last_rel'] = v; s['cons'] = s['lap_c'] * SIZE + v
for lane in sorted(st):
    s = st[lane]; print(f"lane {lane}: blocchi {s['blocks']}, gap max {s['maxgap']/SIZE:.2f} giri, oltre un giro: {s['over']}")
print("shot:", [(ln, t.split()[1]) for ln, t in shots][:12])
for o in over[:20]: print("  riga %d lane %d blocco (%08X,%08X] oltre di %d B" % o)
