"""Task 0236 helper: for each condition function, up to six editor ids of SPEL/ALCH/ENCH/SCRL/MGEF records that use it
on an effect (aggregates by name only; no text of the game)."""
import collections, struct, sys, zlib
data = open(sys.argv[1], "rb").read()
want = set(int(x) for x in sys.argv[2:])
edid = {}
uses = collections.defaultdict(set)
def walk(o, e):
    while o < e:
        k = data[o:o+4]
        if k == b"GRUP":
            s = struct.unpack("<I", data[o+4:o+8])[0]; walk(o+24, o+s); o += s
        else:
            s, fl, fid = struct.unpack("<III", data[o+4:o+16])
            if k in (b"SPEL", b"ALCH", b"ENCH", b"SCRL", b"MGEF"):
                b = data[o+24:o+24+s]
                if fl & 0x40000: b = zlib.decompress(b[4:])
                p = 0; name = ""
                while p + 6 <= len(b):
                    t = b[p:p+4]; l = struct.unpack("<H", b[p+4:p+6])[0]; v = b[p+6:p+6+l]; p += 6 + l
                    if t == b"EDID": name = v[:-1].decode()
                    elif t == b"CTDA":
                        fn = struct.unpack("<H", v[8:10])[0]
                        if fn in want: uses[fn].add(k.decode() + ":" + name)
            o += 24 + s
walk(0, len(data))
for fn in sorted(want):
    print(fn, len(uses[fn]), sorted(uses[fn])[:8])
