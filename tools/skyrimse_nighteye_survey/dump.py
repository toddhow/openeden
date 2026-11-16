# Task 0257: dump one IMAD (by editor id, argv[1]) track by track and the MGEF + script data of NightEyeEffect.
import struct, zlib, sys
D = "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/"
want = sys.argv[1:]
for m in ["Skyrim", "Update", "Dawnguard", "HearthFires", "Dragonborn"]:
    data = open(D + m + ".esm", "rb").read()
    def walk(o, e, cb):
        while o < e:
            k = data[o:o+4]
            if k == b"GRUP":
                s = struct.unpack("<I", data[o+4:o+8])[0]; walk(o+24, o+s, cb); o += s
            else:
                s, fl, fid = struct.unpack("<III", data[o+4:o+16]); b = data[o+24:o+24+s]
                if fl & 0x40000: b = zlib.decompress(b[4:])
                cb(k, fid, b); o += 24+s
    def subs(b):
        o = 0; out = []
        while o + 6 <= len(b):
            t = b[o:o+4]; n = struct.unpack("<H", b[o+4:o+6])[0]; out.append((t, b[o+6:o+6+n])); o += 6+n
        return out
    def cb(k, fid, b):
        if k not in (b"IMAD", b"MGEF"): return
        s = subs(b); ed = next((x[1].split(b"\0")[0].decode() for x in s if x[0] == b"EDID"), "")
        if ed not in want: return
        print(k.decode(), hex(fid), ed)
        if k == b"MGEF":
            for t, v in s:
                if t == b"DATA": print("  DATA dwords", [hex(x) if x > 1000 and x < 0x3f000000 else x for x in struct.unpack("<38I", v)])
                else: print("  ", t, len(v), v[:120])
            return
        dn = dict(s)[b"DNAM"]; c = struct.unpack("<61I", dn)
        print("  DNAM flags", c[0], "duration", struct.unpack("<f", dn[4:8])[0], "counts", c[2:])
        for t, v in s:
            if t in (b"EDID", b"DNAM"): continue
            nm = t.decode("latin1"); n = len(v)//8
            vals = struct.unpack("<%df" % (len(v)//4), v)
            print("  ", repr(nm), n, [round(x, 3) for x in vals])
    walk(0, len(data), cb)
