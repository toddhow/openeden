# Task 0241: the SPEL records of the 13 standing stones (Doom*), the shrine blessings (Altar*) and the MGEFs they use.
# Prints per spell: editor id, SPIT (cost, flags, type, cast type, delivery) and per effect: MGEF editor id, archetype, primary actor
# value, flags, magnitude, area, duration. Aggregates of game data only (no text is kept).
import struct, zlib, collections
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
                          struct.unpack("<I", d[44:48])[0] if len(d) >= 48 else 0, sum(1 for x in s if x[0] == b"CTDA"))
        else:
            spit = next((x[1] for x in s if x[0] == b"SPIT"), b"")
            effs = []; cur = None
            for t, v in s:
                if t == b"EFID": cur = [struct.unpack("<I", v)[0]]; effs.append(cur)
                elif t == b"EFIT" and cur is not None: cur += list(struct.unpack("<fII", v))
            spells[fid] = (ed, struct.unpack("<IIIIf", spit[:20]) if len(spit) >= 20 else (), effs)
    walk(0, len(data), cb)
for fid, (ed, spit, effs) in sorted(spells.items(), key=lambda kv: kv[1][0]):
    if not (ed.lower().startswith("doom") or ed.startswith("Altar")): continue
    print(hex(fid), ed, "SPIT", spit)
    for e in effs:
        g = mgefs.get(e[0], ("?", 0, 0, 0, 0, 0))
        print("   ", g[0], "arch", g[1], "value", g[2], "mflags", hex(g[3]), "conds", g[5], "mag", e[1] if len(e) > 1 else "?",
              "area", e[2] if len(e) > 2 else "?", "dur", e[3] if len(e) > 3 else "?")
