# Task 0355 stage i: what a placed light reference's XRDS, XLIG and XEMI mean (five masters; prints aggregates only).
import struct, zlib, sys, collections, statistics
D = "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/"
files = ["Skyrim.esm","Update.esm","Dawnguard.esm","HearthFires.esm","Dragonborn.esm"]
def subs(b):
    o=0
    while o+6<=len(b):
        t=b[o:o+4]; n=struct.unpack("<H",b[o+4:o+6])[0]
        if t==b"XXXX": n=struct.unpack("<I",b[o+6:o+10])[0]; o+=6+4; t=b[o:o+4]; yield t,b[o+6:o+6+n]; o+=6+n; continue
        yield t,b[o+6:o+6+n]; o+=6+n
rtype={}
lights={}  # (file,local)->(radius,flags,fov,color)
refs=[]
for fn in files:
    data=open(D+fn,"rb").read()
    masters=[]
    def rid(raw):
        i=raw>>24
        return ((masters[i] if i<len(masters) else fn), raw&0xFFFFFF)
    def walk(o,e):
        while o<e:
            k=data[o:o+4]
            if k==b"GRUP":
                s=struct.unpack("<I",data[o+4:o+8])[0]; walk(o+24,o+s); o+=s
            else:
                s,fl,fid=struct.unpack("<III",data[o+4:o+16]); b=data[o+24:o+24+s]
                if fl&0x40000: b=zlib.decompress(b[4:])
                if k!=b"TES4": rtype[rid(fid)]=k
                if k==b"TES4":
                    for t,v in subs(b):
                        if t==b"MAST": masters.append(v.split(b"\0")[0].decode())
                elif k==b"LIGH":
                    for t,v in subs(b):
                        if t==b"DATA" and len(v)>=48:
                            tm,rad,col,flg,fe,fov,nc=struct.unpack("<iI4sIfff",v[:28])
                            lights[rid(fid)]=[rad,flg,fov,col,fe,None]
                        pass
                    for t,v in subs(b):
                        if t==b"FNAM" and rid(fid) in lights: lights[rid(fid)][5]=struct.unpack("<f",v)[0]
                elif k==b"REFR" and not fl&0x20:
                    d={}
                    for t,v in subs(b):
                        if t in (b"NAME",b"XRDS",b"XLIG",b"XEMI",b"XSCL"): d[t]=v
                    if b"NAME" in d and (b"XRDS" in d or b"XLIG" in d or b"XEMI" in d):
                        refs.append((fn,rid(struct.unpack("<I",d[b"NAME"])[0]),d,rid(struct.unpack("<I",d[b"XEMI"])[0]) if b"XEMI" in d else None))
                o+=24+s
    walk(0,len(data))
    # keep per-file resolver

print("lights",len(lights),"refs with extras",len(refs))
sz=collections.Counter(); 
for fn,base,d,emi in refs:
    for t in (b"XRDS",b"XLIG",b"XEMI"):
        if t in d: sz[(t.decode(),len(d[t]))]+=1
print("sizes",dict(sz))
lr=[(fn,base,d,emi) for fn,base,d,emi in refs if base in lights]
print("refs on LIGH bases",len(lr), "extras overall by type", collections.Counter(k for r in refs for k in r[2] if k!=b"NAME"))
# XRDS
rat=[];vals=[];pairs=[]
for fn,base,d,emi in lr:
    if b"XRDS" in d:
        x=struct.unpack("<f",d[b"XRDS"])[0]; r=lights[base][0]
        vals.append(x); pairs.append((r,x))
print("XRDS on light refs",len(vals))
neg=[x for x in vals if x<0]; print("negative",len(neg),"zero",sum(1 for x in vals if x==0))
def pct(a,p): a=sorted(a); return a[int(len(a)*p)]
print("xrds abs p10 p50 p90 p99",[round(pct(vals,p),3) for p in (.1,.5,.9,.99)])
print("fraction in [0,1.0]",sum(1 for x in vals if 0<=x<=1)/len(vals),"in 0..5",sum(1 for x in vals if 0<=x<=5)/len(vals))
# compare to base radius: ratio
rt=[x/r for r,x in pairs if r>0 and x>0]
print("ratio x/base p10 p50 p90",[round(pct(rt,p),3) for p in (.1,.5,.9)], "exact eq base",sum(1 for r,x in pairs if abs(r-x)<1e-3))
print("round values", collections.Counter(round(x,2) for x in vals).most_common(15))
# correlation with base radius
import math
xs=[r for r,x in pairs]; ys=[x for r,x in pairs]
mx=sum(xs)/len(xs); my=sum(ys)/len(ys)
cor=sum((a-mx)*(b-my) for a,b in zip(xs,ys))/math.sqrt(sum((a-mx)**2 for a in xs)*sum((b-my)**2 for b in ys))
print("corr base radius vs xrds",cor)
# XRDS on non-light bases
nl=[struct.unpack("<f",d[b"XRDS"])[0] for fn,base,d,emi in refs if b"XRDS" in d and base not in lights]
print("XRDS on non-light bases",len(nl),[round(pct(nl,p),3) for p in (.1,.5,.9)] if nl else "")
# XLIG
xl=[(base,struct.unpack("<%df"%(len(d[b"XLIG"])//4),d[b"XLIG"])) for fn,base,d,emi in refs if b"XLIG" in d]
print("XLIG count",len(xl),"on lights",sum(1 for b,v in xl if b in lights))
for i in range(4):
    c=[v[i] for b,v in xl if len(v)>i]
    print(" xlig field",i,"p0 p10 p50 p90 p100",[round(pct(c,p),3) for p in (0,.1,.5,.9,.999)],"top",collections.Counter(round(x,3) for x in c).most_common(5))
# fov vs base fov
fv=[(lights[b][2],v[0]) for b,v in xl if b in lights]
print("xlig[0] vs base fov equal",sum(1 for a,b in fv if abs(a-b)<1e-3),"of",len(fv), " base fov distribution",collections.Counter(round(a) for a,b in fv).most_common(4))
# XEMI
em=[(base,emi) for fn,base,d,emi in refs if emi]
print("XEMI",len(em),"on light bases",sum(1 for b,e in em if b in lights),"emi is LIGH",sum(1 for b,e in em if e in lights))
print(" base is light and emi is light same?",sum(1 for b,e in em if b in lights and e==b))
print("---- hypotheses")
P=[(lights[b][0],struct.unpack("<f",d[b"XRDS"])[0]) for fn,b,d,e in refs if b in lights and b"XRDS" in d]
br=[r for r,x in P]; print("base radius p10 p50 p90",[pct(br,p) for p in (.1,.5,.9)])
pos=[x for r,x in P if x>0]; ng=[-x for r,x in P if x<0]
print("pos x p10 p50 p90",[round(pct(pos,p)) for p in (.1,.5,.9)],"neg |x| p10 p50 p90",[round(pct(ng,p)) for p in (.1,.5,.9)])
print("delta hypothesis: x < -base:",sum(1 for r,x in P if x<-r),"of",len(P),"(neg:",len(ng),")")
print("delta: base+x p10 p50 p90",[round(pct([r+x for r,x in P],p)) for p in (.1,.5,.9)])
print("max |x| neg",max(ng),"max pos",max(pos))
# refs per base: does x vary within a base? (sample)
by=collections.defaultdict(list)
for r,x in [(b,struct.unpack("<f",d[b"XRDS"])[0]) for fn,b,d,e in refs if b in lights and b"XRDS" in d]: by[r].append(x)
print("bases with xrds",len(by)); 
for b,v in list(by.items())[:6]: print(b,lights[b][0],[round(t) for t in v[:12]])
# XRDS for refs with XLIG vs without
print("xrds & xlig both",sum(1 for fn,b,d,e in refs if b in lights and b"XRDS" in d and b"XLIG" in d))
# 20 byte XLIG
x20=[struct.unpack("<5f",d[b"XLIG"]) for fn,b,d,e in refs if b"XLIG" in d and len(d[b"XLIG"])==20]
print("xlig20 extra field p0,p50,p100",[round(pct([v[4] for v in x20],p),3) for p in (0,.5,.999)],collections.Counter(round(v[4],2) for v in x20).most_common(4))
# xlig[1] ('fade offset') relation: fade vs base fade? print sample XLIG[3] meaning

print("---- fade / emi")
fb=[(lights[b][5],struct.unpack("<4f",d[b"XLIG"][:16])[1]) for fn,b,d,e in refs if b in lights and b"XLIG" in d and lights[b][5] is not None]
print("fade pairs",len(fb),"base fade p10 p50 p90",[round(pct([a for a,b in fb],p),3) for p in (.1,.5,.9)])
print("offset < -base",sum(1 for a,b in fb if b<-a),"of",len(fb),"; base+off p0 p10 p50 p90",[round(pct([a+b for a,b in fb],p),3) for p in (0,.1,.5,.9)])
print("offset/base p10 p50 p90",[round(pct([b/a for a,b in fb if a>0],p),3) for p in (.1,.5,.9)])
print("xlig[1] nonzero and xrds",sum(1 for fn,b,d,e in refs if b in lights and b"XLIG" in d and abs(struct.unpack("<f",d[b"XLIG"][4:8])[0])>1e-6))
print("XEMI base types",collections.Counter(rtype.get(b) for b,e in em).most_common(8))
print("XEMI target types",collections.Counter(rtype.get(e) for b,e in em).most_common(8))
print("XEMI count unique targets",len(set(e for b,e in em)))
# LIGH emi: of the LIGH targets, flags
print("xrds large >5000:",sum(1 for r,x in P if x>5000),"; >16384",sum(1 for r,x in P if x>16384))
print("neg ratio max |x|/base",max(-x/r for r,x in P if x<0))
