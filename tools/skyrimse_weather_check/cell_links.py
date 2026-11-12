from ids import *
build_types()
t=collections.defaultdict(collections.Counter)
for m in MASTERS:
    r=resolver(m)
    for ty,fid,fl,data in records(D+m,{b'CELL'}):
        for st,sd in subs(data):
            if st in('XCCM','XCWT','XCIM'):
                v=struct.unpack('<I',sd)[0]
                t[st][ALLTYPES.get(r(v),'UNRES') if v else 'null']+=1
            if st=='XCLR':
                for i in range(0,len(sd),4):
                    v=struct.unpack_from('<I',sd,i)[0]
                    t[st][ALLTYPES.get(r(v),'UNRES') if v else 'null']+=1
print({k:dict(v) for k,v in t.items()})
