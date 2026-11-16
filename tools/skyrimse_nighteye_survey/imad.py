# Task 0257: which MGEF records name an IMAD, and the IMAD record layout. Aggregates of game data only.
import struct, zlib, collections, sys
D = "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/"
mgefs = {}; imads = {}; edids = {}
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
        if k not in (b"MGEF", b"IMAD"): return
        s = subs(b); ed = next((x[1].split(b"\0")[0].decode() for x in s if x[0] == b"EDID"), "")
        if k == b"MGEF": mgefs[fid] = (ed, s)
        else: imads[fid] = (ed, s)
    walk(0, len(data), cb)
print("MGEF", len(mgefs), "IMAD", len(imads))
cnt = collections.Counter(len(dict(s)[b"DATA"]) for ed, s in mgefs.values())
print("MGEF DATA sizes", dict(cnt))
# which dword offset holds an IMAD id
hit = collections.Counter()
for fid, (ed, s) in mgefs.items():
    d = dict(s)[b"DATA"]
    for o in range(0, len(d) - 3, 4):
        if struct.unpack("<I", d[o:o+4])[0] in imads: hit[o] += 1
print("offsets naming an IMAD", dict(hit))
off = max(hit, key=hit.get) if hit else None
users = collections.Counter()
for fid, (ed, s) in mgefs.items():
    d = dict(s)[b"DATA"]; v = struct.unpack("<I", d[off:off+4])[0]
    if v in imads:
        users[imads[v][0]] += 1
        if "night" in ed.lower() or "nighteye" in imads[v][0].lower(): print("  ", hex(fid), ed, "->", imads[v][0], "arch", struct.unpack("<I", d[64:68])[0])
print("IMADs named by MGEFs:", len(users)); [print("  ", n, c) for n, c in sorted(users.items())]
sub = collections.Counter(); sizes = collections.defaultdict(set)
for fid, (ed, s) in imads.items():
    for t, v in s: sub[t.decode()] += 1; sizes[t.decode()].add(len(v))
print("IMAD subrecords", dict(sub)); print("sizes", {k: sorted(v) for k, v in sizes.items()})
for fid, (ed, s) in imads.items():
    if "night" in ed.lower():
        print("IMAD", hex(fid), ed)
        for t, v in s:
            if t == b"DNAM": print("  DNAM", len(v), struct.unpack("<%dI" % (len(v)//4), v) if len(v) % 4 == 0 else v.hex())
