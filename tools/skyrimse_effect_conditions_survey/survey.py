#!/usr/bin/env python3
"""Task 0236: which condition functions the effects of the Skyrim SE masters carry.

Reads SPEL, ALCH, ENCH, SCRL (a CTDA after an EFID/EFIT pair belongs to that effect) and MGEF (its own CTDAs) of the
five official masters and prints aggregates only: counts by function, by run-on subject (0 subject = the target of the
effect, 1 target, 2 reference, ...: CTDA +20), by record kind, and for the effects of the archetypes 6, 7, 8 and 21.

    python survey.py "<Skyrim SE>/Data"
"""
import collections
import os
import struct
import sys
import zlib

MASTERS = ["Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"]
KINDS = (b"SPEL", b"ALCH", b"ENCH", b"SCRL", b"MGEF")


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
            if kind in KINDS:
                if flags & 0x40000:
                    body = zlib.decompress(body[4:])
                visit(kind, form, body)
            offset += 24 + size


def subrecords(body):
    pos = 0
    while pos + 6 <= len(body):
        sub = body[pos:pos + 4]
        length = struct.unpack("<H", body[pos + 4:pos + 6])[0]
        yield sub, body[pos + 6:pos + 6 + length]
        pos += 6 + length


def main(folder):
    by_fn = collections.Counter()
    by_fn_kind = collections.defaultdict(collections.Counter)
    by_runon = collections.Counter()
    by_fn_runon = collections.defaultdict(collections.Counter)
    by_arch = collections.defaultdict(collections.Counter)
    effects_with = collections.Counter()
    effects_total = collections.Counter()
    odd = 0
    arch_of = {}

    for name in MASTERS:
        path = os.path.join(folder, name)
        if not os.path.exists(path):
            continue
        data = open(path, "rb").read()
        mgef = []

        def visit(kind, form, body):
            if kind == b"MGEF":
                arch = None
                ctdas = []
                for sub, value in subrecords(body):
                    if sub == b"DATA" and len(value) >= 72:
                        arch = struct.unpack("<I", value[64:68])[0]
                    elif sub == b"CTDA":
                        ctdas.append(value)
                mgef.append((form, arch, ctdas))
                return

        effects_by_rec = []

        def visit_effects(kind, form, body):
            if kind == b"MGEF":
                return
            current = None
            for sub, value in subrecords(body):
                if sub == b"EFID":
                    current = [struct.unpack("<I", value[:4])[0], []]
                    effects_total[kind] += 1
                    effects_by_rec.append((kind, current))
                elif sub == b"CTDA" and current is not None:
                    current[1].append(value)

        walk(data, 0, len(data), visit_effects)
        walk(data, 0, len(data), visit)
        for kind, e in effects_by_rec:
            if e[1]:
                effects_with[kind] += 1
            for c in e[1]:
                if len(c) != 32:
                    odd += 1
                    continue
                fn = struct.unpack("<H", c[8:10])[0]
                runon = struct.unpack("<I", c[20:24])[0]
                by_fn[fn] += 1
                by_fn_kind[fn][kind] += 1
                by_runon[runon] += 1
                by_fn_runon[fn][runon] += 1
        for form, arch, ctdas in mgef:
            effects_total[b"MGEF"] += 1
            if ctdas:
                effects_with[b"MGEF"] += 1
            for c in ctdas:
                if len(c) != 32:
                    odd += 1
                    continue
                fn = struct.unpack("<H", c[8:10])[0]
                runon = struct.unpack("<I", c[20:24])[0]
                by_fn[fn] += 1
                by_fn_kind[fn][b"MGEF"] += 1
                by_runon[runon] += 1
                by_fn_runon[fn][runon] += 1
                by_arch[arch][fn] += 1

    print("effects", {k.decode(): v for k, v in effects_total.items()})
    print("effects with conditions", {k.decode(): v for k, v in effects_with.items()})
    print("odd-size CTDA", odd)
    print("run-on", sorted(by_runon.items()), "(0 subject, 1 target, 2 reference, 3 combat target, 4 linked ref, 5 quest alias, 6 package data, 7 event data)")
    print("distinct functions", len(by_fn), "conditions", sum(by_fn.values()))
    for fn, n in by_fn.most_common():
        print("  fn %4d  %5d  by kind %s  run-on %s" % (
            fn, n, {k.decode(): v for k, v in by_fn_kind[fn].items()}, dict(sorted(by_fn_runon[fn].items()))))
    for arch in (6, 7, 8, 21):
        print("MGEF archetype", arch, "functions", dict(by_arch[arch].most_common()))


if __name__ == "__main__":
    main(sys.argv[1])
