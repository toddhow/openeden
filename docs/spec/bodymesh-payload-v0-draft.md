# The `bodymesh` payload, layout version 0 (DRAFT)

Status: draft written 2026-11-03 (task 0069, stage c). **Not approved**: it freezes only after toddhow approves it,
like the LOD tile layout (S1). Until then a reader refuses every layout version but 0 and the layout may change
without a migration (a changed layout means a re-bake: the cache key includes the layout version).

The normative text is the header comment of `components/body/bodymesh.hpp` and the single rule function `check()` in
`components/body/bodymesh.cpp`; this page repeats the layout for readers who do not read C++.

## What it is

One merged body (task 0069) as bytes: the arrays a renderer needs and nothing else. It is one entry (`bodies/<name>.bodymesh`)
of the derived cache pack (`merged-<digest>.edenpak`, an Eden Archive written with `Body::writePackageFile`, see
`components/body/bodycache.hpp`). The pack also holds `manifest.txt`. The payload is derived from the player's own game
files and is never committed or shipped.

## Layout (little-endian, binary32 floats, no padding)

| Part | Bytes |
| --- | --- |
| magic | `EDENBMSH` (8) |
| layout version | u32, 0 |
| flags | u32: bit 0 normals present, bit 1 UVs present, other bits 0 |
| counts | eight u32: vertices, triangles, bones, materials, regions, submeshes, weld groups, weld members (the sum of the group sizes) |
| bones | per bone: u16 length, then the UTF-8 bytes (1 to 128, no control characters, no repeats) |
| materials | per material: u16 length, then the bytes (same rules; strictly increasing by byte value) |
| positions | vertices x 3 f32 |
| normals | vertices x 3 f32, only with flag bit 0 |
| UVs | vertices x 2 f32, only with flag bit 1 |
| bone indices | vertices x 4 u16 (indices into the bone list; unused entries 0 with weight 0) |
| bone weights | vertices x 4 f32 (each finite and >= 0; the four add up to 1 within 1e-5; canonical, see below) |
| triangles | triangles x 3 u32 (vertex indices) |
| regions | per region: i32 slot, u32 first triangle, u32 triangle count (slots strictly increasing; the regions tile the triangle list in order) |
| submeshes | per submesh: u32 material, i32 slot, u32 first triangle, u32 triangle count (strictly increasing by (slot, material); tiling the triangle list in order; each inside the region of its slot) |
| weld group sizes | per group: u32 size (at least 2) |
| weld group members | members x u32 vertex indices: each group strictly increasing, groups ordered by first member, no vertex in two groups |

Nothing follows. The writer is canonical: reading a payload and writing it again gives the same bytes, and the reader
refuses anything the writer would not write.

## Limits (all checked against the bytes that remain before anything is allocated)

Vertices 1 to 500,000; triangles 1 to 1,000,000; bones 1 to 1,024; materials 1 to 256; regions 1 to 256; submeshes
1 to 4,096; weld groups at most 65,536; weld members at most 1,048,576; names at most 128 bytes.

## What it leaves out on purpose

The vertex map back to the input parts, the canonical part order and the merge report (they describe the merge, not the
body), inverse bind matrices (they come from the skeleton file at run time: the key records the skeleton's path and file
digest), and textures (a material is a key string; the engine finds the texture by it).

## Open for toddhow before it freezes

1. Is the one-entry-per-body layout right, or should regions and submeshes be a separate small entry?
2. float32 weights and u16 indices (56 bytes per vertex) against float16/u8 (about 44): size 2.4 MB for 20 bodies today.
3. Should the report numbers (weld sets, groups, maximum weight disagreement) be stored in the payload for the studio panel?

## Canonical skin (review fix of 2026-11-03, still layout version 0, a draft)

The reader accepts only what the writer writes: for every vertex the four influences are in order of falling weight,
a tie is listed lower bone index first, unused slots come last with weight 0 and bone index 0, no bone is listed twice
for one vertex, and the weights add up to 1 within 1e-5 (float round-off of four renormalised weights is a few 1e-7).
Every bone in the bone list must be used by some vertex (a positive weight). A payload that breaks any of these is
refused with the vertex and the rule, so a bit flip that leaves it readable still writes back to the same bytes.
