# Writes a SYNTHETIC modder body package for the body package panel check (task 0069 stage f, check.sh here): a body
# of tubes (no game data), about 1.75 m tall, as a folder holding body.json and mesh/body.bodymesh (the bodymesh
# payload, layout version 0, metres).
# Two deliberate defects so that the panel has findings to list: the head's triangles are in no region (BODY-GEOM-005)
# and one weld group names a vertex past the mesh (BODY-WELD-005).
#
# Usage: python make_synthetic_package.py <output folder>
import json
import math
import os
import struct
import sys

SEGMENTS = 12
RINGS = 4

# (region id, slot, bone, centre x, centre y, z0, z1, radius)
PARTS = [
    ("pelvis", 1, "Bip Pelvis", 0.0, 0.0, 0.85, 1.05, 0.16),
    ("torso", 2, "Bip Spine", 0.0, 0.0, 1.05, 1.45, 0.18),
    ("head", 3, "Bip Head", 0.0, 0.0, 1.50, 1.75, 0.10),
    ("left-leg", 4, "Bip L Thigh", 0.09, 0.0, 0.0, 0.85, 0.07),
    ("right-leg", 5, "Bip R Thigh", -0.09, 0.0, 0.0, 0.85, 0.07),
    ("left-arm", 6, "Bip L UpperArm", 0.26, 0.0, 0.85, 1.40, 0.05),
    ("right-arm", 7, "Bip R UpperArm", -0.26, 0.0, 0.85, 1.40, 0.05),
]
BONES = [part[2] for part in PARTS]
MATERIAL = "synthetic_skin"


def tube(cx, cy, z0, z1, r, base):
    positions = []
    for ring in range(RINGS + 1):
        z = z0 + (z1 - z0) * ring / RINGS
        for s in range(SEGMENTS):
            a = 2 * math.pi * s / SEGMENTS
            positions.append((cx + r * math.cos(a), cy + r * math.sin(a), z))
    triangles = []
    for ring in range(RINGS):
        for s in range(SEGMENTS):
            a = base + ring * SEGMENTS + s
            b = base + ring * SEGMENTS + (s + 1) % SEGMENTS
            c = a + SEGMENTS
            d = b + SEGMENTS
            triangles += [a, b, d, a, d, c]
    return positions, triangles


def main():
    out = sys.argv[1]
    positions, triangles, weights, regions, submeshes = [], [], [], [], []
    starts = {}
    for index, (rid, slot, bone, cx, cy, z0, z1, r) in enumerate(PARTS):
        base = len(positions)
        starts[rid] = base
        p, t = tube(cx, cy, z0, z1, r, base)
        positions += p
        first = len(triangles) // 3
        triangles += t
        count = len(t) // 3
        regions.append((slot, first, count))
        submeshes.append((0, slot, first, count))
        weights += [index] * len(p)

    # The torso's bottom ring and the pelvis's top ring are the same points: weld groups of two.
    welds = []
    for s in range(SEGMENTS):
        pelvis_top = starts["pelvis"] + RINGS * SEGMENTS + s
        torso_bottom = starts["torso"] + s
        welds.append([pelvis_top, torso_bottom])
    for group in welds:
        positions[group[1]] = positions[group[0]]

    payload = bytearray(b"EDENBMSH")
    payload += struct.pack("<II", 0, 0)  # layout version 0, no normals or UVs
    payload += struct.pack("<8I", len(positions), len(triangles) // 3, len(BONES), 1, len(regions), len(submeshes),
                           len(welds), sum(len(g) for g in welds))
    for name in BONES:
        payload += struct.pack("<H", len(name)) + name.encode()
    payload += struct.pack("<H", len(MATERIAL)) + MATERIAL.encode()
    for p in positions:
        payload += struct.pack("<3f", *p)
    for bone in weights:
        payload += struct.pack("<4H", bone, 0, 0, 0)
    for _ in weights:
        payload += struct.pack("<4f", 1.0, 0.0, 0.0, 0.0)
    payload += struct.pack("<%dI" % len(triangles), *triangles)
    for slot, first, count in regions:
        payload += struct.pack("<iII", slot, first, count)
    for material, slot, first, count in submeshes:
        payload += struct.pack("<IiII", material, slot, first, count)
    for group in welds:
        payload += struct.pack("<I", len(group))
    for group in welds:
        payload += struct.pack("<%dI" % len(group), *group)

    region_map = []
    for (rid, slot, bone, *_), (s, first, count) in zip(PARTS, regions):
        if rid == "head":
            continue  # defect 1: the head is in no region (BODY-GEOM-005)
        region_map.append({"id": rid, "ranges": [[first, first + count]]})
    weld_groups = [list(g) for g in welds] + [[1, 100000]]  # defect 2: a vertex past the mesh (BODY-WELD-005)

    rest = [{"name": "Bip Root", "parent": -1, "translation": [0, 0, 0], "rotation": [0, 0, 0, 1]}]
    for bone in BONES:
        rest.append({"name": bone, "parent": 0, "translation": [0, 0, 0.9], "rotation": [0, 0, 0, 1]})
    body = {
        "format": "openeden.body",
        "version": 1,
        "identity": {
            "id": "synthetic_tubes",
            "name": "Synthetic tube body (task 0069 stage f check)",
            "version": "1.0.0",
            "author": "OpenEden check script",
            "license": "CC0-1.0",
            "contractVersion": {"major": 1, "minor": 0},
            "ageClass": "unspecified",
        },
        "targets": [{"race": "synthetic", "sex": "male", "priority": 0}],
        "binding": {
            "skeleton": {
                "format": "opas.skeletonprofile",
                "version": 1,
                "name": "synthetic",
                "bones": [{"source": "Pelvis", "target": "Bip Pelvis"}, {"source": "Spine", "target": "Bip Spine"}],
                "legChain": [],
            },
            "rootBone": "Bip Root",
            "unmappedBones": ["Bip Root"] + BONES[2:],
            "restSkeleton": rest,
        },
        "meshes": [{"id": "body", "path": "mesh/body.bodymesh", "meshFormat": "bodymesh", "lod": 0, "role": "skin"}],
        "regionMap": {"origin": "authored", "mesh": "body", "regions": region_map},
        "weldGroups": {"origin": "authored", "mesh": "body", "tolerance": 0, "groups": weld_groups},
    }
    os.makedirs(os.path.join(out, "mesh"), exist_ok=True)
    with open(os.path.join(out, "mesh", "body.bodymesh"), "wb") as f:
        f.write(payload)
    with open(os.path.join(out, "body.json"), "w", newline="\n") as f:
        json.dump(body, f, indent=2)
        f.write("\n")
    print("vertices", len(positions), "triangles", len(triangles) // 3, "weld groups", len(welds))


main()
