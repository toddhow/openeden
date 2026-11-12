from ids import *
build_types()
# REGN
rd_follow=collections.Counter(); wtypes=collections.Counter(); gl=collections.Counter(); chance_sum=collections.Counter(); nent=collections.Counter()
rdat_flags=collections.Counter(); rdat_pri=collections.Counter(); rdwt_empty=0; rdwt_n=0
for m in MASTERS:
    r=resolver(m)
    for t,fid,fl,data in records(D+m,{b'REGN'}):
        last=None
        for st,sd in subs(data):
            if st=='RDAT':
                ty,fl_,pr,_u=struct.unpack('<IBBH',sd); last=ty
                if ty==3: rdat_flags[fl_]+=1; rdat_pri[pr]+=1
            elif st=='RDWT':
                rdwt_n+=1
                rd_follow[last]+=1
                if len(sd)%12: rd_follow['badsize']+=1
                ent=[struct.unpack_from('<IiI',sd,i) for i in range(0,len(sd)//12*12,12)]
                nent[len(ent)]+=1
                if not ent: rdwt_empty+=1
                s=0
                for a,b,c in ent:
                    wtypes[ALLTYPES.get(r(a),'UNRES')]+=1
                    gl['null' if c==0 else ALLTYPES.get(r(c),'UNRES')]+=1
                    s+=b
                chance_sum[s]+=1
print('REGN RDWT n',rdwt_n,'preceded by RDAT type',dict(rd_follow),'entries/record',dict(nent),'entry weather types',dict(wtypes),'globals',dict(gl))
print(' chance sums',dict(chance_sum)); print(' RDAT3 flags',dict(rdat_flags),'priority',dict(rdat_pri))
# CELL XCCM interior/exterior
def cells(path):
    b=open(path,'rb').read()
    out=collections.Counter()
    def walk(pos,end,ext):
        while pos<end:
            t=b[pos:pos+4]; size=struct.unpack_from('<I',b,pos+4)[0]
            if t==b'GRUP':
                gt=struct.unpack_from('<i',b,pos+12)[0]
                walk(pos+24,pos+size, ext or gt==1)
                pos+=size
            else:
                flags=struct.unpack_from('<I',b,pos+8)[0]
                if t==b'CELL':
                    data=b[pos+24:pos+24+size]
                    if flags&0x40000: data=zlib.decompress(data[4:])
                    for st,sd in subs(data):
                        if st in('XCCM','XCWT','XCIM','XCLR'): out[(st,'ext' if ext else 'int')]+=1
                pos+=24+size
    walk(0,len(b),False)
    return out
tot=collections.Counter()
for m in MASTERS: tot.update(cells(D+m))
print('CELL links',dict(tot))
# WRLD climate inheritance
worlds={}
for m in MASTERS:
    r=resolver(m)
    for t,fid,fl,data in records(D+m,{b'WRLD'}):
        g=r(fid); w=worlds.setdefault(g,{'edid':None,'cnam':None,'wnam':None,'pnam':None,'nam2':None})
        for st,sd in subs(data):
            if st=='EDID': w['edid']=sd[:-1].decode()
            if st=='CNAM': w['cnam']=r(struct.unpack('<I',sd)[0])
            if st=='WNAM': w['wnam']=r(struct.unpack('<I',sd)[0])
            if st=='PNAM': w['pnam']=struct.unpack('<H',sd[:2])[0]
            if st=='NAM2': w['nam2']=r(struct.unpack('<I',sd)[0])
def climate(g,depth=0):
    w=worlds.get(g)
    if w is None or depth>16: return None
    if w['cnam']: return w['cnam']
    if w['wnam'] and w['pnam'] is not None and w['pnam']&0x10: return climate(w['wnam'],depth+1)
    return None
nclim=sum(1 for g in worlds if climate(g)); print('worlds',len(worlds),'with climate after inheritance',nclim,'own CNAM',sum(1 for w in worlds.values() if w['cnam']))
for g,w in worlds.items():
    if w['edid'] in ('Tamriel','WhiterunWorld','Sovngarde','DLC2SolstheimWorld','SolitudeWorld','DLC01FalmerValley'):
        print(' ',w['edid'],hex(g),'climate',hex(climate(g) or 0),'own',bool(w['cnam']),'pnam',w['pnam'])
# WLST glob/types
gl=collections.Counter()
for m in MASTERS:
    r=resolver(m)
    for t,fid,fl,data in records(D+m,{b'CLMT'}):
        for st,sd in subs(data):
            if st=='WLST':
                for i in range(0,len(sd)//12*12,12):
                    a,b,c=struct.unpack_from('<IiI',sd,i); gl[('wthr',ALLTYPES.get(r(a)),'glob',c)]+=1
print('CLMT WLST',dict(gl))
for m in MASTERS:
    for t,fid,fl,data in records(D+m,{b'WTHR'}):
        S=dict(subs(data))
        if len(S['NAM0']) != 272 or 'JNAM' not in S: print(' short:',S['EDID'][:-1].decode(),len(S['NAM0']),len(S.get('PNAM',b'')),len(S.get('QNAM',b'')), struct.unpack('<I',S['LNAM'])[0]);
