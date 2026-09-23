# callers.py <sample.txt> <simbolo> [livelli]: chi chiama <simbolo> (conteggi dal call graph di sample)
import sys,re,collections
f,sym=sys.argv[1],sys.argv[2]; lv=int(sys.argv[3]) if len(sys.argv)>3 else 1
stack=[]; cnt=collections.Counter()
for l in open(f,errors='replace'):
    if l.startswith('Total number in stack') or l.startswith('Sort by top'): break
    m=re.match(r'^([\s+!:|]*)(\d+)\s+(\S+)',l)
    if not m: continue
    d=len(m.group(1)); n=int(m.group(2)); name=m.group(3)
    while stack and stack[-1][0]>=d: stack.pop()
    if name==sym:
        key=' <- '.join(s[1] for s in stack[-lv:][::-1])
        cnt[key]+=n
    stack.append((d,name))
for k,v in cnt.most_common(12): print(v,k)
