# abfit.py <file.txt di abbin/cpufoto>: retta comune CPU~draw con uno scarto per gruppo
# (on/off dal nome della corsa). Stampa la differenza on-off a parita' di draw +- 1 sigma.
import sys,re
import numpy as np
rows=[]
for l in open(sys.argv[1]):
    m=re.match(r'\S+_(on|off|v0|v1)\.log: (\d+) ms CPU/fotogramma.*?, (\d+) draw medi',l)
    if m: rows.append((m.group(1) in ('on','v1'), float(m.group(2)), float(m.group(3))))
g=np.array([r[0] for r in rows],float); y=np.array([r[1] for r in rows]); d=np.array([r[2] for r in rows])
X=np.column_stack([np.ones_like(y), g, d])
b,res,_,_=np.linalg.lstsq(X,y,rcond=None)
s2=((y-X@b)**2).sum()/max(len(y)-3,1); cov=s2*np.linalg.inv(X.T@X)
print(f"n={len(y)} (on {int(g.sum())}), on-off = {b[1]:+.1f} +- {cov[1,1]**.5:.1f} ms/fotogramma, "
      f"pendenza {b[2]*100:.1f} ms/100 draw, a 2200 draw: off {b[0]+b[2]*2200:.0f} on {b[0]+b[1]+b[2]*2200:.0f}")
