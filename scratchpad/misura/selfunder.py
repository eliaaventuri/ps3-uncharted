# selfunder.py <sample.txt> <antenato regex>: tempo self dei simboli sotto quell'antenato
import sys,re,collections
f,anc=sys.argv[1],re.compile(sys.argv[2])
nodes=[]  # (depth,n,name)
for l in open(f,errors='replace'):
    if l.startswith('Total number in stack') or l.startswith('Sort by top'): break
    m=re.match(r'^([\s+!:|]*)(\d+)\s+(\S+)',l)
    if m: nodes.append((len(m.group(1)),int(m.group(2)),m.group(3)))
selfc=collections.Counter(); stack=[]
for i,(d,n,name) in enumerate(nodes):
    while stack and stack[-1][0]>=d: stack.pop()
    ch=0; j=i+1
    while j<len(nodes) and nodes[j][0]>d:
        # figli diretti: primo livello piu' profondo
        pass; break
    stack.append((d,name,n,i))
# ricalcolo self: n - somma dei figli diretti
kids=collections.defaultdict(int); st=[]
for i,(d,n,name) in enumerate(nodes):
    while st and nodes[st[-1]][0]>=d: st.pop()
    if st: kids[st[-1]]+=n
    st.append(i)
st=[]
for i,(d,n,name) in enumerate(nodes):
    while st and nodes[st[-1]][0]>=d: st.pop()
    if any(anc.search(nodes[k][2]) for k in st) or anc.search(name):
        s=n-kids[i]
        if s>0:
            key=name
            if re.search('wait|kernreturn|mach_msg|semaphore|nanosleep|swtch|yield',name): key='[ATTESA] '+name
            selfc[key]+=s
    st.append(i)
tot=sum(selfc.values()); print('totale',tot)
for k,v in selfc.most_common(int(sys.argv[3]) if len(sys.argv)>3 else 30): print(v,k)
