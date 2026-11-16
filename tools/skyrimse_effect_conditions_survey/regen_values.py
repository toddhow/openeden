# Task 0238: the MGEFs on the regeneration rate actor values (27 to 29, 155 to 157) and a few neighbours.
import struct,zlib,collections,sys,glob
D="I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/"
res=collections.defaultdict(list)
spel_effects=collections.defaultdict(list)
for m in ["Skyrim","Update","Dawnguard","HearthFires","Dragonborn"]:
    data=open(D+m+".esm","rb").read()
    def walk(o,e,cb):
        while o<e:
            k=data[o:o+4]
            if k==b"GRUP":
                s=struct.unpack("<I",data[o+4:o+8])[0]; walk(o+24,o+s,cb); o+=s
            else:
                s,fl,fid=struct.unpack("<III",data[o+4:o+16]); b=data[o+24:o+24+s]
                if fl&0x40000:b=zlib.decompress(b[4:])
                cb(k,fid,b); o+=24+s
    def cb(k,fid,b):
        if k!=b"MGEF":return
        n=b[b.find(b"EDID")+6:].split(b"\0")[0].decode()
        i=b.find(b"DATA"); v=b[i+6:i+6+152]
        arch=struct.unpack("<I",v[64:68])[0]; prim=struct.unpack("<i",v[68:72])[0]; fl=struct.unpack("<I",v[:4])[0]
        nc=b.count(b"CTDA")
        res[(prim,arch)].append((n,hex(fl),nc))
    walk(0,len(data),cb)
for key in sorted(res):
    if key[0] in (27,28,29,83,60,61,62,63,64,65,66,67,68,69,70) or any("Rate" in n for n,_,_ in res[key]):
        print(key,len(res[key]),[ (n,f,c) for n,f,c in res[key][:12]])
