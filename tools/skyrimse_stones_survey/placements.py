# Task 0241: where the standing stones stand (the REFRs of the Doomstone* activators in Skyrim.esm) and the shrine altars.
import struct, zlib
D = "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/"
data = open(D + "Skyrim.esm", "rb").read()
acti = {}; refs = []
def subs(b):
    o = 0
    while o + 6 <= len(b):
        t = b[o:o+4]; n = struct.unpack("<H", b[o+4:o+6])[0]; yield t, b[o+6:o+6+n]; o += 6 + n
def walk(o, e):
    while o < e:
        k = data[o:o+4]
        if k == b"GRUP":
            s = struct.unpack("<I", data[o+4:o+8])[0]; walk(o+24, o+s); o += s
        else:
            s, fl, fid = struct.unpack("<III", data[o+4:o+16]); b = data[o+24:o+24+s]
            if fl & 0x40000: b = zlib.decompress(b[4:])
            if k == b"ACTI":
                ed = next((v.split(b"\0")[0].decode() for t, v in subs(b) if t == b"EDID"), "")
                if ed.lower().startswith("doomstone") or ed.startswith("Shrine"): acti[fid] = ed
            elif k == b"REFR":
                nm = next((struct.unpack("<I", v)[0] for t, v in subs(b) if t == b"NAME"), 0)
                dt = next((struct.unpack("<6f", v) for t, v in subs(b) if t == b"DATA"), None)
                refs.append((fid, nm, dt))
            o += 24 + s
walk(0, len(data))
for fid, nm, dt in refs:
    if nm in acti: print(hex(fid), "base", hex(nm), acti[nm], "pos", None if dt is None else tuple(round(x) for x in dt[:3]))
