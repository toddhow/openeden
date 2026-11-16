"""Task 0236 helper: print the conditions of named MGEFs of Skyrim.esm with form ids resolved to editor ids."""
import struct, sys, zlib
data = open(sys.argv[1], "rb").read()
names = sys.argv[2:]
edid = {}
recs = []
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
            if k in (b"MGEF", b"SPEL", b"ALCH", b"ENCH"): recs.append((k, fid, subs))
            o += 24 + s
walk(0, len(data))
for k, fid, subs in recs:
    n = edid.get(fid, "")
    if n not in names: continue
    print(k.decode(), n)
    for t, v in subs:
        if t == b"CTDA":
            op = v[0]; cmpv = struct.unpack("<f", v[4:8])[0]; fn = struct.unpack("<H", v[8:10])[0]
            p1, p2 = struct.unpack("<II", v[12:20]); ro = struct.unpack("<I", v[20:24])[0]; ref = struct.unpack("<I", v[24:28])[0]
            print("   fn %d op 0x%02x cmp %g p1 %s p2 %x runon %d ref %x" % (fn, op, cmpv, edid.get(p1, hex(p1)), p2, ro, ref))
        elif t == b"EFID":
            print("   EFFECT", edid.get(struct.unpack("<I", v)[0], "?"))
