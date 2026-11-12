from walk import *
import sys
want={a.encode() for a in sys.argv[1].split(',')}
cnt=collections.Counter(); sizes=collections.defaultdict(collections.Counter); nrec=0
for m in MASTERS:
    for t,fid,fl,data in records(D+m,want):
        nrec+=1
        seen=collections.Counter()
        for st,sd in subs(data):
            seen[st]+=1; sizes[st][len(sd)]+=1
        for st in seen: cnt[st]+=1
print(sys.argv[1],nrec)
for st,c in sorted(cnt.items()):
    sz=sizes[st]
    print(" ",st,"in",c,"records; sizes:",dict(sz.most_common(6)) if len(sz)<=6 else (len(sz),"distinct, min",min(sz),"max",max(sz)))
