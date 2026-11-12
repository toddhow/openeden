import struct, zlib, sys, collections, json
D = "I:/SteamLibrary/steamapps/common/Skyrim Special Edition/Data/"
files = ["Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"]
allc = collections.Counter()
info = collections.Counter()
shape = collections.defaultdict(collections.Counter)

def subs(data):
    p = 0
    big = None
    while p + 6 <= len(data):
        t = data[p:p+4]; sz = struct.unpack_from('<H', data, p+4)[0]; p += 6
        if t == b'XXXX':
            big = struct.unpack_from('<I', data, p)[0]; p += 4; continue
        if big is not None:
            sz = big; big = None
        yield t, data[p:p+sz]
        p += sz

def walk(buf, p, end, rtypes):
    while p < end:
        t = buf[p:p+4]
        if t == b'GRUP':
            sz = struct.unpack_from('<I', buf, p+4)[0]
            walk(buf, p+24, p+sz, rtypes)
            p += sz
        else:
            sz, flags = struct.unpack_from('<II', buf, p+4)
            data = buf[p+24:p+24+sz]
            if flags & 0x40000:
                usz = struct.unpack_from('<I', data, 0)[0]
                data = zlib.decompress(data[4:])
            for st, sd in subs(data):
                if st == b'CTDA' and len(sd) == 32:
                    fn = struct.unpack_from('<H', sd, 8)[0]
                    allc[fn] += 1
                    if t == b'INFO':
                        info[fn] += 1
                    p1, p2 = struct.unpack_from('<II', sd, 12)
                    runon = struct.unpack_from('<I', sd, 20)[0]
                    cmpv = struct.unpack_from('<f', sd, 4)[0]
                    shape[fn][(p1 != 0, p2 != 0, runon, sd[0] >> 5, round(cmpv, 3) if abs(cmpv) < 1000 else 'big')] += 1
            p += 24 + sz

for f in files:
    b = open(D + f, 'rb').read()
    # skip TES4 header
    sz = struct.unpack_from('<I', b, 4)[0]
    walk(b, 24 + sz, len(b), None)
nz = {str(k): [sum(v for (a, b, r, c, x), v in shape[k].items() if a), sum(v for (a, b, r, c, x), v in shape[k].items() if b), sum(shape[k].values())] for k in shape}
json.dump({'all': dict(allc), 'info': dict(info), 'nz': nz}, open(sys.argv[1], 'w'))
print(len(allc), sum(allc.values()), sum(info.values()))
