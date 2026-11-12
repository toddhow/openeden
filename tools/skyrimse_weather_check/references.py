from ids import *
build_types()
print("types known", len(ALLTYPES))
rt=collections.defaultdict(collections.Counter)  # field -> type counts
dat=[collections.Counter() for _ in range(19)]
lnam=collections.Counter(); nam1=collections.Counter(); nam1bits=0
fn=[[1e9,-1e9] for _ in range(8)]
unres=collections.Counter()
n=0
named={}
for m in MASTERS:
    r=resolver(m)
    for t,fid,fl,data in records(D+m,{b'WTHR'}):
        n+=1
        S={}
        for st,sd in subs(data):
            S.setdefault(st,[]).append(sd)
        edid=S['EDID'][0].rstrip(b'\0').decode()
        named[edid]=(m,fid)
        d=S['DATA'][0]
        for i,b in enumerate(d): dat[i][b]+=1
        lnam[struct.unpack('<I',S['LNAM'][0])[0]]+=1
        if 'NAM1' in S: nam1[struct.unpack('<I',S['NAM1'][0])[0]]+=1
        f=struct.unpack('<8f',S['FNAM'][0])
        for i,x in enumerate(f):
            fn[i][0]=min(fn[i][0],x); fn[i][1]=max(fn[i][1],x)
        def refs(key, inner=None):
            for sd in S.get(key,[]):
                for i in range(0,len(sd)-3,4):
                    if inner is not None and (i//4)%inner[0]!=inner[1]: continue
                    v=struct.unpack_from('<I',sd,i)[0]
                    if v==0: rt[key]['null']+=1; continue
                    g=r(v)
                    rt[key][ALLTYPES.get(g,'UNRESOLVED')]+=1
        for k in ('MNAM','NNAM','IMSP','HNAM','NAM2','NAM3','TNAM','ONAM'): refs(k)
        refs('SNAM',(2,0))
print("WTHR",n)
names=["wind","cloudLow","cloudUp","transDelta","sunGlare","sunDamage","precipBegin","precipEnd","thunderBegin","thunderEnd","thunderFreq","flags","lightR","lightG","lightB","visBegin","visEnd","windDir","windDirRange"]
for i,c in enumerate(dat):
    print(" DATA[%d] %s: %d distinct, min %d max %d, top %s"%(i,names[i],len(c),min(c),max(c),c.most_common(4)))
print(" LNAM",dict(lnam)); print(" NAM1",dict(nam1))
print(" FNAM ranges",fn)
for k,c in rt.items(): print(" refs",k,dict(c))
