# Provenance log for Skyrim SE collision from `bhk` blocks (task 0144, plan section 4.9)

2026-11-12. Every format fact that `components/nifscene/havok.*` and `apps/openmw/mwphysics/skyrimhavok.*` rely on, with how it was
established. Rules (root `CLAUDE.md` guardrails, the task): clean room; no Havok SDK header, source or library; no decompiled
code; nothing copied from another project's source; `nif.xml` may be read as an oracle for field layout only and never copied
into the repository.

**What was read.** Nothing outside the repository and the user's own files. In particular `nif.xml` was NOT opened (no
network was used and no copy was on the machine), and no other project's source or documentation was read. The block LAYOUTS
(which fields follow which) are those the repository's own record reader already parses (`components/nif/physics.*`, inherited
from OpenMW, GPL-3.0, already in the tree); this task adds no new layout parsing and no new binary reading, it only turns those
parsed records into plain values. The meanings and conventions below (units, quaternion order, matrix layout, strip winding,
layers) were first guesses from memory of public community knowledge: **RECALLED** means a guess the data has not tested;
**DATA** means confirmed on the user's files, with the evidence. Nothing marked RECALLED is relied on for a decision that
could go wrong silently, except where stated.

**How the data was read.** `apps/components_tests/nifscene/testbhkreal.cpp` (gated by `OPENEDEN_SKYRIMSE_DATA`; prints aggregates
only; no path, name or vertex value except where a model path already named in the repository is probed) reads every `.nif` in
the five official masters' archives (22,091 files; 1 fails to parse, the same one the earlier surveys reported) through
`Nif::Reader`, builds `NifScene::Scene` with `mReadHavok`, and for the correspondence check also flattens the drawn geometry.
The check decodes the collision vertices under each candidate reading and counts how many land within 0.5 game units of a drawn
vertex; the reading that scores far above the others is the fact. It ran on every 8th file with collision (1,339 files; later runs
on every 6th and 40th for the extra counters). Raw outputs: `I:\Projects\OpenEden-work\skybhk\survey1.txt`, `survey3.txt`,
`survey5.txt` (not committed).

## Survey (stage a), aggregates

| Fact | Number |
| --- | --- |
| Files with a collision object | 11,112 of 22,091 |
| Bodies (`bhkRigidBody` 11,029 plus `bhkRigidBodyT` 3,423) | 14,452; every one has collision response 1; none without a shape |
| Collision objects that are not a `bhkRigidBody` (phantoms and so on) | 184, not read |
| Files with more than one body | 741 |
| Shape blocks | compressed mesh 8,211 (each under a `bhkMoppBvTreeShape`, 8,314 MOPP in all), convex vertices 3,494, capsule 1,906, box 1,805, convex transform 619, list 389, sphere 155, transform 11, `bhkNiTriStripsShape` 22, cylinder 2, plane 1 |
| Body signatures by count | `Mopp(CompressedMesh)` 5,811, `T:Mopp(CompressedMesh)` 2,400, convex vertices 2,912, capsule 1,601, `T:box` 987, `T:sphere` 35, lists of boxes/capsules/hulls and old `Mopp(NiTriStrips)` lists a few dozen each |
| Unsupported by this reader (model falls back to the visual triangles) | `bhkNiTriStripsShape` (22 blocks, about 15 bodies, old-style lists), `bhkPlaneShape` (1) |
| Layers (`HavokFilter` layer byte) | 1: 8,670; 4: 1,746; 8: 1,116; 2: 1,053; 0: 484; 5: 394; 19: 235; 10: 168; 15: 156; 3: 129; 13: 72; 14: 68; 9: 65; 6: 52; 20: 17; 28: 7; 51: 5; others 4 or fewer; 12: 1 |
| Rigid body filter equals the world object filter | all 14,452 (0 mismatches) |
| Motion type byte | 5: 9,344; 3: 2,460; 4: 2,357; 2: 291 (no 7: the fixed/static statics are NOT motion 7 here; the field is not used) |
| Bodies on a node whose world pose is not the identity | 4,708 (so node poses matter and are applied) |
| Compressed mesh: shape scale not 1 | 0 of 8,211; shape radius non-zero 8,211 of 8,211 (not used) |
| Models whose drawn triangles give no collision mesh (the task 0140 set) | 410 in the survey: 328 have a solid bhk body, 82 have only non-solid ones |

## Facts

| Item | Established by | Evidence |
| --- | --- | --- |
| U1 Game units per Havok unit is about 70 (`kHavokToGame` = 69.99125, i.e. 1 / 0.0142875) | **DATA for "about 70"; the exact value RECALLED** | Of 362,973 collision points (compressed meshes and hulls, every reading of the other questions at its best) the share within 0.5 units of a drawn vertex: scale 69.99125: 163,146; 70.0: 162,684; 7.0: 430; 1.0: 816. The data separates 70 from 7 and 1 by a factor of 200 and cannot separate 69.99125 from 70 (the tolerance is wider than the 0.01 percent difference); the slightly higher count at 69.99125 is consistent with it, not proof |
| U2 Block layouts of the repository's reader are right for Skyrim SE (stream version 100) | DATA | 22,091 files parsed with 1 failure; every body has response 1 and rigid body filter equal to the world object filter; the chunk bounds check in U3 passes for all chunks; the in-bounds numbers in U7 are 97 to 99 percent. A layout offset error would show up as garbage translation, rotation or shapes |
| U3 A compressed mesh chunk vertex is `chunk translation + 0.001 * quantised u16` | DATA | All 24,217 chunks decoded this way lie inside the data's own bounding box (`mAabbMin`/`mAabbMax`, plus 0.05); 0 index errors. A different step would also fail the correspondence in U1 (50 percent of compressed mesh points land on drawn vertices at this step: 123,351 of 278,903 for plain bodies and 38,995 of 73,071 for T bodies in the best reading). The containment alone would not exclude a smaller step; the correspondence does |
| U3b Strips: the first `sum(strip lengths)` indices are triangle strips with alternating winding, the rest a triangle list; indices address the chunk's quantised vertices | DATA | 0 bad indices in 24,217 chunks; and U9 (winding) |
| U4 Chunk transform quaternion is stored x, y, z, w | DATA | Correspondence points, chunk order xyzw / body order xyzw: 163,146; chunk wxyz / body xyzw: 29,899 (5.5 times lower) |
| U5 Rigid body (T) quaternion is stored x, y, z, w | DATA | chunk xyzw: body xyzw 163,146 against body wxyz 125,833 |
| U6 A `bhkConvexTransformShape` / `bhkTransformShape` matrix is row-vector (`p * M`), translation in elements 12 to 14 | **DATA for the translation row and for the layout as a whole; the rotation part is only weakly tested** | 630 transform shapes: translation non-zero in elements 12 to 14 in all 630, never in 3, 7, 11; element 15 is 0 in all 630 (not 1); 372 of 630 have an identity 3x3, 258 do not. Points of boxes and capsules under transform shapes inside the drawn bounds (plus 10 units), plain bodies: row-vector/last-row 654 of 682, column-vector/last-column 550 of 682. The test has few points; the rotation of a quarter-turn transform has no independent oracle other than this |
| U7 A plain `bhkRigidBody` (not `T`) does NOT place its shape with its translation/rotation; a `bhkRigidBodyT` does | DATA | Points inside drawn bounds plus 10 units: plain bodies ignoring translation/rotation 362,040 of 364,477 (99.3 percent), applying them 334,245 (91.7 percent); T bodies 104,095 of 104,859 (99.3 percent). 4,193 plain bodies have a non-zero translation and 2,101 a non-zero rotation |
| U8 Layer 15 (and 12) are non-solid; every other layer collides | **partly DATA, partly RECALLED** | In the Bannered Mare and Whiterun window run (`doorsdrift`), the 24 distinct models whose bodies are all layer 15 (437 reads in the log) are 20 hanging herbs and plants under `meshes/plants`, one landscape plant, one wreath and two effect models, things the game lets you walk through (all 24 have only layer 15 bodies; I looked at the names, not at each in game); layer 12 has 1 body in the masters. Layer 15 = "non-collidable" and 12 = "trigger" are the usual names (RECALLED); layers 16 and 18 are in the policy from memory and occur in no master body. Layer 8 (biped, 1,116 bodies) belongs to skeleton NIFs and does not reach this path. The policy also leaves out bodies with collision response 3 ("none"); none occurs in the masters |
| U9 Collision triangles keep the visual winding | DATA | 73,471 collision triangles whose three corners match one drawn triangle: 73,468 have the same cyclic order, 3 are reversed (99.996 percent); no flip is needed |
| U10 Body scale and the node pose | REASONING + DATA | A body's shape is placed by the node's world pose (translation, rotation, uniform scale as the NIF stores them) times its own placement; the node scale also scales the shape. 4,708 bodies sit on nodes with a non-identity pose and the in-bounds numbers hold, so the composition order is right |
| U11 MOPP code | REASONING | Not read: the wrapped shape is a complete shape; the MOPP bytecode is a query accelerator and a mesh can be built from the chunks without it. 8,314 MOPP wrappers all wrapped a readable shape |

## What is not established

- The exact unit scale to better than 0.1 percent (U1).
- The rotation convention of transform shapes beyond the layout (U6): only a few thousand points, no model with a rotated transform shape has been compared by eye.
- The convex radius of boxes and hulls: ignored. Jolt's own default is used (a box never gets a radius above its smallest half extent).
- Havok materials, friction, restitution, mass and motion types: read and carried in `Body`, not used (all bodies are one static world shape).
- Collision filters beyond the layer (the group and flag bits).
- `bhkNiTriStripsShape` and `bhkPlaneShape` shapes (about 15 bodies) and constraint data: not read.
- Dynamic bodies: clutter (layer 4, 1,746 bodies) is a static solid in this task.
- Whether layer meanings other than 15 and 12 should make a body non-solid in some situation (for example layer 14 "trap", 13 "terrain").
