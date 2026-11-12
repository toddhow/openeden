from walk import *
# global id resolution over the five masters (load order = MASTERS order)
def header(path):
    b=open(path,'rb').read(4096*64)
    size=struct.unpack_from('<I',b,4)[0]
    flags=struct.unpack_from('<I',b,8)[0]
    data=b[24:24+size]
    masters=[]
    for t,sd in subs(data):
        if t=='MAST': masters.append(sd.rstrip(b'\0').decode())
    return masters,flags
ORDER={m:i for i,m in enumerate(MASTERS)}
def resolver(plugin):
    masters,_=header(D+plugin)
    def r(stored):
        hi=stored>>24
        if hi<len(masters): m=masters[hi]
        else: m=plugin
        return (ORDER[m]<<24)|(stored&0xFFFFFF)
    return r
ALLTYPES={}
def build_types():
    for m in MASTERS:
        r=resolver(m)
        for t,fid,fl,data in records(D+m,None):
            ALLTYPES[r(fid)]=t
