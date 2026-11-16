#!/usr/bin/env python3
"""Task 0233: how the effects that change behaviour (paralysis, frenzy, fear) and the weakness effects are stored in the
Skyrim SE masters. Reads every MGEF of the Data folder's Skyrim.esm (DATA +0 flags, +64 archetype, +68 primary actor
value) and every poison ALCH (ENIT flags 0x20000) and prints aggregates: the archetype histogram, the effects of the
archetypes 6, 7, 8 and 21 and of every Peak (34) on an actor value 40 to 45, and the poisons that carry them.

    python survey.py "<Skyrim SE>/Data/Skyrim.esm"
"""
import collections
import struct
import sys
import zlib


def walk(data, offset, end, visit):
    while offset < end:
        kind = data[offset:offset + 4]
        if kind == b"GRUP":
            size = struct.unpack("<I", data[offset + 4:offset + 8])[0]
            walk(data, offset + 24, offset + size, visit)
            offset += size
        else:
            size, flags, form = struct.unpack("<III", data[offset + 4:offset + 16])
            body = data[offset + 24:offset + 24 + size]
            if kind in (b"MGEF", b"ALCH"):
                if flags & 0x40000:
                    body = zlib.decompress(body[4:])
                visit(kind, form, body)
            offset += 24 + size


def main(path):
    data = open(path, "rb").read()
    effects, potions = {}, []

    def visit(kind, form, body):
        pos, edid, dat, enit, ids, fits = 0, None, None, None, [], []
        while pos < len(body):
            sub = body[pos:pos + 4]
            length = struct.unpack("<H", body[pos + 4:pos + 6])[0]
            value = body[pos + 6:pos + 6 + length]
            pos += 6 + length
            if sub == b"EDID":
                edid = value[:-1].decode()
            elif sub == b"DATA" and kind == b"MGEF":
                dat = value
            elif sub == b"ENIT":
                enit = value
            elif sub == b"EFID":
                ids.append(struct.unpack("<I", value[:4])[0])
            elif sub == b"EFIT":
                fits.append(struct.unpack("<fII", value[:12]))
        if kind == b"MGEF" and dat is not None and len(dat) >= 72:
            effects[form] = (edid, struct.unpack("<I", dat[0:4])[0], struct.unpack("<I", dat[64:68])[0],
                             struct.unpack("<i", dat[68:72])[0])
        if kind == b"ALCH" and enit is not None:
            potions.append((edid, struct.unpack("<I", enit[4:8])[0], ids, fits))

    walk(data, 0, len(data), visit)
    print("archetypes", sorted(collections.Counter(e[2] for e in effects.values()).items()))
    for name, arch in (("calm", 6), ("fear/demoralize", 7), ("frenzy", 8), ("paralysis", 21)):
        rows = [e for e in effects.values() if e[2] == arch]
        print(name, "archetype", arch, "effects", len(rows), "actor values", sorted({e[3] for e in rows}))
    weak = [e for e in effects.values() if e[2] == 34 and 40 <= e[3] <= 45]
    print("peak on 40..45", len(weak), "values", sorted({e[3] for e in weak}))
    for e in weak:
        if (e[0] or "").startswith("AlchWeakness"):
            print("  ", e[0], "value", e[3], "flags 0x%x" % e[1], "hostile+recover+detrimental" if e[1] & 7 == 7 else "")
    poisons = [p for p in potions if p[1] & 0x20000]
    print("poisons", len(poisons))
    for edid, flags, ids, fits in poisons:
        kinds = [effects[i][2] for i in ids if i in effects]
        if any(k in (7, 8, 21) for k in kinds) or any(effects[i][2] == 34 and 40 <= effects[i][3] <= 45 for i in ids if i in effects):
            print("  ", edid, [(effects[i][0], effects[i][2], effects[i][3]) for i in ids if i in effects], fits)


if __name__ == "__main__":
    main(sys.argv[1])
