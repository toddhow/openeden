# Task 0248: SPEL and MGEF numbers of race powers, diseases and blessings (aggregates of game data only).
import struct, zlib, collections, sys
D = "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/"
spells = {}; mgefs = {}
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
        if k not in (b"SPEL", b"MGEF"): return
        s = subs(b); ed = next((x[1].split(b"\0")[0].decode() for x in s if x[0] == b"EDID"), "")
        if k == b"MGEF":
            d = next(x[1] for x in s if x[0] == b"DATA")
            mgefs[fid] = (ed, struct.unpack("<I", d[64:68])[0], struct.unpack("<i", d[68:72])[0], struct.unpack("<I", d[:4])[0],
                          struct.unpack("<I", d[44:48])[0] if len(d) >= 48 else 0, sum(1 for x in s if x[0] == b"CTDA"), struct.unpack("<I", d[8:12])[0], struct.unpack("<I", d[136:140])[0] if len(d) >= 140 else 0, any(x[0] == b"VMAD" for x in s), len(d))
        else:
            spit = next((x[1] for x in s if x[0] == b"SPIT"), b"")
            effs = []; cur = None
            for t, v in s:
                if t == b"EFID": cur = [struct.unpack("<I", v)[0]]; effs.append(cur)
                elif t == b"EFIT" and cur is not None: cur += list(struct.unpack("<fII", v))
            spells[fid] = (ed, struct.unpack("<IIIIf", spit[:20]) if len(spit) >= 20 else (), effs)
    walk(0, len(data), cb)
for fid, (ed, spit, effs) in sorted(spells.items(), key=lambda kv: kv[1][0]):
    if not any(p in ed.lower() for p in sys.argv[1:]): continue
    print(hex(fid), ed, "SPIT", spit)
    for e in effs:
        g = mgefs.get(e[0], ("?", 0, 0, 0, 0, 0, 0, 0, False, 0))
        print("   ", g[0], "arch", g[1], "value", g[2], "mflags", hex(g[3]), "conds", g[5], "assoc", hex(g[6]), "perk", hex(g[7]), "vmad", g[8], "len", g[9], "mag", e[1] if len(e) > 1 else "?",
              "area", e[2] if len(e) > 2 else "?", "dur", e[3] if len(e) > 3 else "?")
