# incl.py <sample.txt> <regex...>: tempo inclusivo (campioni) dei simboli che corrispondono, senza doppi conteggi
import sys,re,collections
f=sys.argv[1]; pats=[re.compile(p) for p in sys.argv[2:]]
stack=[]; cnt=collections.Counter()
for l in open(f,errors='replace'):
    if l.startswith('Total number in stack') or l.startswith('Sort by top'): break
    m=re.match(r'^([\s+!:|]*)(\d+)\s+(\S+)',l)
    if not m: continue
    d=len(m.group(1)); n=int(m.group(2)); name=m.group(3)
    while stack and stack[-1][0]>=d: stack.pop()
    for p in pats:
        if p.search(name) and not any(p.search(s[1]) for s in stack): cnt[p.pattern]+=n
    stack.append((d,name))
for p in pats: print(cnt[p.pattern],p.pattern)
