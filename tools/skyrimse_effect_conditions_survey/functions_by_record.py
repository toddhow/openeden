# Task 0238: which records use the given condition functions, with their parameters (usage: functions_by_record.py <esm> <fn> ...).
import struct, sys, zlib, collections
path = sys.argv[1]; fns = set(int(x) for x in sys.argv[2:])
data = open(path, "rb").read()
edid = {}; recs = []
def walk(o, e):
    while o < e:
        k = data[o:o+4]
        if k == b"GRUP":
            s = struct.unpack("<I", data[o+4:o+8])[0]; walk(o+24, o+s); o += s
        else:
            s, fl, fid = struct.unpack("<III", data[o+4:o+16])
            b = data[o+24:o+24+s]
            if fl & 0x40000: b = zlib.decompress(b[4:])
            p = 0; subs = []
            while p + 6 <= len(b):
                t = b[p:p+4]; l = struct.unpack("<H", b[p+4:p+6])[0]; subs.append((t, b[p+6:p+6+l])); p += 6 + l
            for t, v in subs:
                if t == b"EDID": edid[fid] = v[:-1].decode(); break
            if k in (b"MGEF", b"SPEL", b"ALCH", b"ENCH", b"SCRL"): recs.append((k, fid, subs))
            o += 24 + s
walk(0, len(data))
agg = collections.defaultdict(lambda: collections.defaultdict(set))
for k, fid, subs in recs:
    for t, v in subs:
        if t == b"CTDA":
            fn = struct.unpack("<H", v[8:10])[0]
            if fn in fns:
                p1, p2 = struct.unpack("<II", v[12:20]); op = v[0]; cmpv = struct.unpack("<f", v[4:8])[0]; ro = struct.unpack("<I", v[20:24])[0]
                agg[fn][(edid.get(p1, hex(p1)) if fn in (74,310,365,629,719) else p1 if fn==722 else 0, op>>5, cmpv, ro)].add(k.decode()+":"+edid.get(fid,"?"))
for fn in sorted(agg):
    print("fn", fn)
    for key, v in sorted(agg[fn].items(), key=lambda x:-len(x[1])):
        print("   ", key, len(v), sorted(v)[:3])
