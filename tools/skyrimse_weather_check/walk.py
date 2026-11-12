# Task 0127: a small independent TES4 walker for the weather survey (aggregates only). Data folder from the environment.
import os, struct, zlib, sys, collections
D=os.environ["OPENEDEN_SKYRIMSE_DATA"].rstrip("/\\")+"/"
MASTERS=["Skyrim.esm","Update.esm","Dawnguard.esm","HearthFires.esm","Dragonborn.esm"]
def records(path, want):
    b=open(path,'rb').read()
    def walk(pos,end):
        while pos<end:
            t=b[pos:pos+4]
            size=struct.unpack_from('<I',b,pos+4)[0]
            if t==b'GRUP':
                yield from walk(pos+24,pos+size)
                pos+=size
            else:
                flags,fid=struct.unpack_from('<II',b,pos+8)
                data=b[pos+24:pos+24+size]
                if want is None:
                    yield t.decode(),fid,flags,None
                elif t in want:
                    if flags&0x40000:
                        n=struct.unpack_from('<I',data,0)[0]
                        data=zlib.decompress(data[4:]); assert len(data)==n
                    yield t.decode(),fid,flags,data
                pos+=24+size
    yield from walk(0,len(b))
def subs(data):
    pos=0; over=None
    while pos<len(data):
        t=data[pos:pos+4].decode('latin1'); s=struct.unpack_from('<H',data,pos+4)[0]; pos+=6
        if over is not None: s=over; over=None
        if t=='XXXX': over=struct.unpack_from('<I',data,pos)[0]; pos+=4; continue
        yield t,data[pos:pos+s]; pos+=s
