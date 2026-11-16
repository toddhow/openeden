# Task 0238: the share of effect conditions the evaluator answers, before and after (the five masters, aggregates).
import struct,zlib,collections
D="I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/"
before={14,46,69,70,71,72,77,80,214,244,277,289,448,560,597,640,682,699,700,715,724}
ext=before|{74,300,310,365,719,722}
tot=0;b=0;a=0;un=collections.Counter()
for m in ["Skyrim","Update","Dawnguard","HearthFires","Dragonborn"]:
    data=open(D+m+".esm","rb").read()
    def walk(o,e):
        global tot,b,a
        while o<e:
            k=data[o:o+4]
            if k==b"GRUP":
                s=struct.unpack("<I",data[o+4:o+8])[0]; walk(o+24,o+s); o+=s
            else:
                s,fl,fid=struct.unpack("<III",data[o+4:o+16])
                if k in (b"MGEF",b"SPEL",b"ALCH",b"ENCH",b"SCRL"):
                    bb=data[o+24:o+24+s]
                    if fl&0x40000: bb=zlib.decompress(bb[4:])
                    p=0
                    while p+6<=len(bb):
                        t=bb[p:p+4]; l=struct.unpack("<H",bb[p+4:p+6])[0]; v=bb[p+6:p+6+l]; p+=6+l
                        if t==b"CTDA" and len(v)>=28:
                            fn=struct.unpack("<H",v[8:10])[0]; ro=struct.unpack("<I",v[20:24])[0]; ref=struct.unpack("<I",v[24:28])[0]; p1=struct.unpack("<I",v[12:16])[0]
                            okro = ro in (0,1) or (ro==2 and ref==0x14)
                            tot+=1
                            if fn in before and okro: b+=1
                            ok = fn in ext and okro and (fn!=719 or p1 in (0,0x14))
                            if ok: a+=1
                            else: un[fn]+=1
                o+=24+s
    walk(0,len(data))
print("conditions",tot,"before",b,round(100*b/tot,1),"after",a,round(100*a/tot,1)); print(un.most_common(12))
