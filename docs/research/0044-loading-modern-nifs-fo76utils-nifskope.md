# Research note 0044: loading modern NIFs, what the fo76utils NifSkope fork shows

2026-11-04. Docs only: nothing in the engine changed, nothing was built or run. For `docs/tasks/0082-nif-any-version-and-scene.md`
(and through it `docs/design/game-packs.md`, task 0086, the Skyrim SE pack).

## 0. Method, honesty rules, and what this note is not

- **Source read:** the fork `https://github.com/fo76utils/nifskope`, branch `develop`, a shallow clone at commit
  `380631a408c54f82bfbd70786c75e8bfa4945ecb` (2026-10-14), in `I:\Projects\OpenEden-work\nifskope-fo76\` (outside the
  repository, never committed). Paths below are relative to that clone. The upstream schema repository
  `https://github.com/niftools/nifxml`, at the commit the fork pins as a submodule (`970a6238218a106daaeb89a61bcda0eeaf9d08c4`,
  2023-06-18), was fetched shallow into `I:\Projects\OpenEden-work\niftools-nifxml\` to read its licence.
- Every fact cites a file and a function or schema block. "Not read" marks what I did not open. The clone has one commit, so
  **upstream NifSkope's source was not read**; section 5 is sourced only from the fork's `README.md` and `CHANGELOG.md`.
- **No code was copied and none is quoted.** No Havok-related code was opened (`src/spells/havok.cpp`, `moppcode.cpp` and the
  MOPP tool are named only as existing). Only the schema's block names, which are data descriptions, were read for collision.
- The fork is a viewer/editor built on Qt. It loads a NIF into a generic tree of named items, then builds render objects from the
  tree. That is the opposite of a game loader, which matters for section 6.
- Facts about what is **in a file** come from the schema (`build/nif.xml`) or from the fork's own viewer assumptions. Neither is
  proof. The oracle in section 4 and in task 0082 exists to turn them into checked facts on toddhow's files.

## 1. How the fork parses a NIF

### 1.1 Schema-driven, read at run time

- `build/nif.xml` (10,000 lines) is **read when the program starts**, not compiled in and not used to generate C++.
  `NifModel::loadXML` (`src/xml/nifxml.cpp`) looks for `nif.xml` beside the executable (and `/usr/share/nifskope/nif.xml` on
  Linux); `NifModel::parseXmlDescription` parses it with `QXmlStreamReader`; the `NifXmlHandler` class fills static tables of
  compounds (`<struct>`) and blocks (`<niobject>`). The build copies the file next to the executable (`NifSkope.pro`, `XML +=`).
  If the file is missing the program shows an error and refuses to load.
- Loading a file creates a tree of `NifItem` nodes per block from those tables (`NifModel::insertNiBlock`) and then fills it:
  `NifModel::loadItem` (`src/model/nifmodel.cpp`) walks the children, asks `evalCondition(child)`, recurses into arrays and
  compounds and calls `NifIStream::read(NifValue&)` (`src/io/nifstream.cpp`) for leaves. Every value is a `NifValue` variant
  with a name, so a vertex is a dozen named child items, not a struct. This is slow and memory-hungry by game standards but
  gives the editor field-level names for everything.
- **The mechanics of version selection**, from `NifXmlHandler::startElement` (`tagAdd` case) and `NifModel::evalVersionImpl`:
  a `<field>` has `since` / `until` (file version, compared with `evalVersion(version)` as an early reject), `vercond`
  (an expression over the **stream** version token `#BSVER#`, and `$`-prefixed variables; evaluated by `NifExpr`,
  `src/xml/nifexpr.{h,cpp}`, with the operators `== != >= <= > < & | + - / * && || ! << >>`), `cond` (an expression over earlier
  sibling fields, evaluated by `BaseModel::evalConditionImpl`), `arg` (the value passed into a templated struct, e.g.
  `Vertex Desc >> 44` for the vertex attributes), `length` / `width` (array sizes by field name), `onlyT` / `excludeT`
  (sugar for conditions on the template type), and `calc` (a value the editor recomputes on save; ignored on read).
  Named version expressions are macros in the schema's `<token name="verexpr">` block (`#BS_SSE#`, `#BS_GTE_130#`,
  `#NI_BS_LT_FO4#`, ...), expanded at parse time.
- **The header is read first, separately** (`NifModel::loadHeader`): it reads the version string, resets `User Version` and
  `BS Header\BS Version` to 0, loads the header (the conditions that depend on them are invalidated and re-evaluated as the
  header streams), then caches the stream version in `bsVersion` (`cacheBSVersion`). From then on `vercond` uses that number.
  So "stream version" in OpenEden's vocabulary is the fork's `bsVersion`, `BS Header\BS Version`.
- **Block dispatch** (`NifModel::load`): from file version 10.0.0.0 on, block type names come from the header's
  `Block Types` / `Block Type Index` arrays; below that, each block starts with a length-prefixed type name; below 3.3.0.13 the
  old "Top Level Object / End Of File" form is used. A block type not in the schema **aborts the load** with "Encountered
  unknown block" unless the file records block sizes **and** the setting `Ignore Block Size` is off; that setting defaults to
  **on** (`settings.value("Ignore Block Size", true)`), so by default the fork does not use the header's `Block Size` array to
  skip or resynchronise. When block sizes are used (20.2.0.x and later), a mismatch between the position after a block and the
  recorded size is logged as a warning and the stream is repositioned to the recorded end. For Oblivion-era files
  (version below 10.2.0.0) the loop reads and checks a 4-byte separator before non-`bhk` blocks (a comment cites missing
  bytes in some Oblivion havok blocks). A version 20.3.1.2 branch resolves blocks by hash (not relevant to us).
- **Footer** is read without raising on failure (a comment says failures there must not prevent a load).
- **File-version support** is data: the schema's `<version id num user bsver ext>` rows (`build/nif.xml` lines 179 to 238).
  The rows that matter, verbatim facts from the schema:

| Game | File version | User version | Stream (`bsver`) | Schema row |
| --- | --- | --- | --- | --- |
| Morrowind | 4.0.0.2 (also 4.0.0.0 listed with Freedom Force) | none | none | `V4_0_0_2` |
| Oblivion | 20.0.0.5 | 10 or 11 | 11 | `V20_0_0_5_OBL`; also 10.0.1.2 (bsver 1 or 3), 10.1.0.101, 10.2.0.0 user 10 |
| Fallout 3 / New Vegas | 20.2.0.7 | 11 | 34 (earlier development values 14 to 33 listed too) | `V20_2_0_7_FO3` |
| Skyrim LE | 20.2.0.7 | 12 | 83 | `V20_2_0_7_SKY` |
| Skyrim SE | 20.2.0.7 | 12 | 100 | `V20_2_0_7_SSE` |
| Fallout 4 | 20.2.0.7 | 12 | 130 (132 and 139: dev files, `supported="false"`) | `V20_2_0_7_FO4` |
| Fallout 76 | 20.2.0.7 | 12 | 155 | `V20_2_0_7_F76` |
| Starfield | 20.2.0.7 | 12 | 170, 172, 173, 175 | `V20_2_0_7_STF` |

  This settles task 0082's "recalled, verify" note for the user versions **as the schema states them**; it is still the
  schema's word, so the tests should still read them from real files.

### 1.2 Vertex formats: `BSVertexDesc` and the packed vertex data

- `BSVertexDesc` is a 64-bit bitfield (`build/nif.xml`, `<bitfield name="BSVertexDesc">`, line 2632; read by
  `NifIStream::read` as one `u64`, `tBSVertexDesc`; the C++ wrapper is `class BSVertexDesc` in `src/data/niftypes.h`):

| Bits | Member |
| --- | --- |
| 0 to 3 | Vertex Data Size (in 4-byte units; the stride is this times 4, per `Vertex Size`'s `calc`) |
| 4 to 7 | Dynamic Vertex Size |
| 8 to 39 | eight 4-bit offsets: UV1, UV2, Normal, Tangent, Color, Skinning Data, Landscape Data, Eye Data (offsets in bytes or 4-byte units: not checked) |
| 40 to 43 | unused |
| 44 to 55 | `VertexAttribute` flags |

  Attribute flag bits (`<bitflags name="VertexAttribute">`, line 2618): 0 Vertex, 1 UVs, 2 UVs_2, 3 Normals, 4 Tangents,
  5 Vertex_Colors, 6 Skinned, 7 Land_Data, 8 Eye_Data, 9 Instance, 10 Full_Precision.
  (The offsets-per-member view above is the schema's; the fork's reader does not use the offsets, it reads the attribute
  fields in a fixed order, see next point.)
- **The layout is selected by the flags, not by the offsets.** The compound `BSVertexData` (stream 130 and later) and
  `BSVertexDataSSE` (stream 100, also listed for 130) are read with `arg = Vertex Desc >> 44`, so the `cond`s test the flags
  (`line 2647` and `2668`). The field order is fixed: position (+ the W slot), UV, normal (+ bitangent Y), tangent (+ bitangent Z),
  vertex colour, bone weights and indices, eye data. Types: `HalfTexCoord` (2 half floats), `ByteVector3` / `normbyte` (byte
  to [-1, 1]), `ByteColor4`, `hfloat` x4 weights, `byte` x4 bone indices.
- **Position precision differs between games.** `BSVertexDataSSE` always stores position as a 3-float vector (the SSE
  data). `BSVertexData` (Fallout 4 and later) stores a 3-float vector only when flags `Vertex` and `Full_Precision` are both
  set (`(ARG & 0x401) == 0x401`) and otherwise a half-float `HalfVector3`. The 4th slot of the position (4 bytes full,
  2 bytes half) carries **bitangent X** when the Tangents flag is set (`Bitangent X`) and is padding otherwise (`Unused W`).
  This is the "bitangent X in the position's W" layout that a reader must reproduce.
- **The `Vertex Desc` argument is special-cased** in `NifXmlHandler::startElement`
  (`if arg == "Vertex Desc\\Vertex Attributes"`: the type is "internalised" and the argument becomes the whole desc).
- **Half floats** are read by `qfloat16` in `NifIStream::read`; half vectors by the `tHalfVector3` and `tHalfVector2` cases.
- **Shapes with no vertex data in the shape:** `BSTriShape` has `Data Size` (a `calc` field: stride x vertices + triangles x 6)
  and every vertex and triangle array has `cond="Data Size > 0"`. So a `BSTriShape` whose `Data Size` is 0 reads as a shape with
  vertex and triangle counts but no arrays. That is the zero-vertex skinned shape of Skyrim SE (below).

### 1.3 Shapes, skin partitions, skin instances, weights

Block layouts, all in `build/nif.xml`:

- **`BSTriShape`** (line 9523; versions SSE, FO4, F76): bounding sphere (and a bounding box from stream 155), `Skin` ref,
  `Shader Property` ref, `Alpha Property` ref, `Vertex Desc`, triangle count (a `uint` from stream 130, a `ushort` below),
  vertex count (`ushort`), `Data Size`, `Vertex Data` (`BSVertexData` for stream >= 130, `BSVertexDataSSE` for stream 100),
  `Triangles` (3 x ushort), and, for SSE only, "particle" arrays of half vectors.
- **`BSSubIndexTriShape`** (line 9566): FO4 and later add primitive/segment counts and optional shared segment data (conditional
  on `Data Size > 0`); SSE has only a segment array (`BSGeometrySegmentData`). **`BSMeshLODTriShape`** (line 9544, FO4 only):
  adds three LOD sizes (`LOD0 Size`, `LOD1 Size`, `LOD2 Size`). Neither is drawn specially by the fork except for LOD selection
  in the viewer (`BSShape::updateLodLevel`, `isLOD`); how the LOD sizes divide the triangle list was not read.
- **`BSDynamicTriShape`** (line 9745, SSE): vertex positions come from a separate `Vertices` array of 4-float vectors, whose W
  holds **bitangent X**; `BSShape::updateData` (`src/gl/bsshape.cpp`) uses that array instead of the packed position when
  `isDynamic`. Face and head geometry in Skyrim uses this block.
- **`NiSkinPartition`** (line 5647) holds, for stream 100 only, a copy of the shape's vertex stream: `Data Size`,
  `Vertex Size`, `Vertex Desc`, `Vertex Data` (`BSVertexDataSSE`), followed by the `SkinPartition` structs (line 2683). A
  `SkinPartition` has `Num Vertices`, `Num Triangles`, `Num Bones`, `Num Weights Per Vertex`, a bone list, optional vertex map,
  weights, strips, triangles, per-vertex bone indices and, for stream > 34, `LOD Level`, `Global VB`; for stream 100 also a
  `Vertex Desc` and a `Triangles Copy`.
- **How the fork gathers an SSE skinned body** (`BSShape::updateData`, `src/gl/bsshape.cpp`):
  1. `isSkinned` comes from the shape's own `VertexAttribute` flags (Skinned bit), not from the skin ref.
  2. For stream >= 130 the skin blocks are `BSSkin::Instance` and `BSSkin::BoneData`; below that `NiSkinInstance` and
     `NiSkinData`. For stream 100 it also resolves the instance's `Skin Partition` link to a `NiSkinPartition`.
  3. If a partition block exists, the **vertices come from `NiSkinPartition.Vertex Data`** (count = `Data Size / Vertex Size`),
     not from the shape (the shape's `Data Size` is 0 there), and the **triangles are the partitions' own triangle arrays
     appended one after another** (`SkinPartition` objects in `src/gl/gltools.cpp`; the fork's BSShape path adds
     `partitions.constLast().triangles` as read, it does **not** remap them through the vertex map there; the remap helper
     `getRemappedTriangles` exists and is used elsewhere, not in this path). Out-of-range triangle indices are clamped by
     `Shape::removeInvalidIndices` rather than rejected.
  4. **Weights and bone indices are taken from the per-vertex data** (`Bone Weights`, `Bone Indices`, 4 each) and the bone index
     is compared against the skin instance's `Bones` count (`bns[j] < nTotalBones`) and used as an index into the skin instance's
     bone list. Weights below 0.00001 are dropped. The viewer packs each as `index + weight * 65535/65536` into a float4
     (`boneWeights0`). **The partition's own `Bones` map and the `Global VB` / `LOD Level` flags are not read by the viewer**
     (a search for them in `src/` finds only the schema names). This is therefore an assumption of the viewer (that the
     vertex-stream bone indices are already indices into the skin instance's list in the files it tested). Note 0036 measured
     that SSE weights agree with `NiSkinData`; the oracle should settle whether partition-local indices ever occur.
  5. Bind pose: `BoneData::setTransform` (`src/gl/gltools.cpp`) reads each `Bone List` entry's transform and bounding sphere.
- **When the shape is not skinned**, or for stream >= 130, vertices and triangles come from the shape's own arrays.
- **`BSSkin::Instance` / `BSSkin::BoneData`** (line 9621): skeleton root, `Data` ref, bone `Ptr`s, scales; bone data entries are
  a bounding sphere + 3x3 rotation + translation + scale (`BSSkinBoneTrans`, 68 bytes).
- **Skyrim LE** (stream 83) does not use `BSTriShape` at all: the schema's `BSTriShape` is versioned `#SSE# #FO4# #F76#`;
  LE uses `NiTriShape` + `NiTriShapeData` + `NiSkinInstance` / `BSDismemberSkinInstance` + `NiSkinPartition`
  (`BSDismemberSkinInstance`, line 8018). The glTF exporter and `CHANGELOG.md` confirm LE export through `NiTriShape`.
  Not read: the code path that draws `NiTriShape` skinned shapes.

### 1.4 Shader properties, texture sets, material files

- **`BSLightingShaderProperty`** (line 7806, Skyrim and later): `Shader Type`, two flag words, UV offset/scale, `Texture Set`
  ref, emissive colour/multiple, texture clamp mode, alpha, refraction strength, glossiness (Skyrim) or smoothness (FO4), specular
  colour/strength, lighting-effect floats, and a **type-specific tail guarded by `cond="Shader Type == N"`**: env-map scale
  (type 1), skin tint (5), hair tint (6), parallax-occlusion (7), multilayer parallax (11), sparkle (14), eye cubemap (16).
  Shader type names: `<enum name="BSLightingShaderType">` (line 1888). For streams 130 and 155 the property carries a
  **material path** in `Name` (a `.bgsm`) and the schema marks `Material` as an `abstract` field resolved from that file;
  for Starfield (>= 170) the `Material` is a `.mat` (layered material). The schema also has a `PBR Material Data` field for
  shader type 21 at stream 100 (a block the fork added; the community Skyrim PBR convention is not described further here).
- **`BSEffectShaderProperty`** (line 7940): source, greyscale, env-map, normal, env-mask textures inline; no texture set.
- **`BSShaderTextureSet`** (line 6861): `Num Textures` then strings; the schema documents the slots: 0 diffuse, 1 normal/gloss,
  2 glow or skin or hair or rim, 3 height/parallax, 4 environment, 5 environment mask, 6 subsurface for multilayer parallax,
  7 back-lighting; 9 and 10 for Fallout 76. The viewer reads slot `id` from the set (`BSShaderLightingProperty::fileName`,
  `src/gl/glproperty.cpp`), with a shift by one for slots 8 and 9 from stream 151.
- **Material files.** `BSShaderLightingProperty::setMaterial` (`glproperty.cpp`) decides by the property's `Name` ending in
  `.bgsm` (lighting) or `.bgem` (effect) and builds a `ShaderMaterial` / `EffectMaterial` (`src/io/material.h`,
  `materialfile.cpp`, 498 lines; **not read beyond the class names and `fileName`'s use of its texture list and flags**). Where a
  material file is present it overrides the inline fields (UV scale/offset, specular, emissive, alpha, double-sided, tiling,
  fresnel, env-map flags): see `BSLightingShaderProperty::updateParams`. Starfield materials use `CE2MaterialDB`
  (`lib/libfo76utils/src/bsmatcdb.*`, `material.*`, `mat_*.cpp`; file names only, **not read**).
- **Flags the viewer uses** for Skyrim: `SLSF1_*` / `SLSF2_*` bits for specular, parallax, glow map, double-sided, vertex
  colours, vertex alpha, back/rim/soft lighting, multilayer parallax, refraction, tree animation (`updateParams`,
  `glproperty.cpp`). Wire-level bit assignments are in the schema's `SkyrimShaderPropertyFlags1/2` enums (not listed here).
- **Texture file names** are resolved by the viewer to full paths with a `textures/` prefix and `.dds` suffix
  (`Game::GameManager::get_full_path(name, "textures/", ".dds")`, used in `src/lib/importex/gltf.cpp`); archives (BSA of
  Morrowind, Oblivion-and-later BSA with LZ4, BA2) are read by `BA2File` in `lib/libfo76utils/src/ba2file.cpp`
  (`loadBSAFile`; the header test for the `BSA\0` magic). Not read: `src/gamemanager.cpp`.

### 1.5 Tangent space and normal maps

- The packed vertex data has fields **named** `Normal`, `Bitangent X/Y/Z`, `Tangent` in `BSVertexData` (`build/nif.xml`
  line 2647). In the glTF exporter (`GltfStore::meshFileFromShape`, `src/lib/importex/gltf.cpp`) the exported TANGENT
  array is built from the shape's **bitangent** values (`mesh.tangents = shape->bitangents`), and the sign (the glTF `w`) is
  derived from the sign of cross(normal, exported tangent) dotted with the shape's **tangent** array
  (`bitangentsBasis`), then negated in `createPrimitive` (`* -1.0f`). In other words, Bethesda's "bitangent" slot is what glTF
  calls the tangent, with the handedness recovered by computation. **Our `Nif::Scene` must say plainly which convention it
  stores, and tests must pin it.**
- `src/spells/tangentspace.cpp` recomputes tangents from UVs (and checks `VF_VERTEX | VF_UV | VF_NORMAL`); it is a generator,
  not a reader (not read in detail).
- Green-channel convention of the normal maps: **not read** (would be in `res/shaders/sk_default.frag`).

### 1.6 Coordinate system and units

- The viewer keeps NIF coordinates (Z up, right-handed). The glTF export converts to **metres with
  `0.9144 / 64` per unit** for stream versions below 170 (`GltfStore::toMeters`, `gltf.cpp`), applied to bone translations, mesh
  positions and inverse bind matrices; Starfield (170 and up) is exported without that factor. So the fork treats Skyrim,
  Fallout 4 and Fallout 76 units as the same as Morrowind's (64 units per yard). This is a second source for note 0036's
  assumption, but it is the fork author's assumption too, not a statement from a game or a measurement.
- For Y-up glTF only the **root node** is rotated (`trans.rotation.toYUp()` and `(x, z, -y)` for the root translation in
  `createNodes`); children keep their local NIF transforms.
- Skyrim's actors and Morrowind's therefore share a unit in the fork's model; Skyrim armour/body skeleton heights of 1.83 m
  measured in note 0036 are consistent with it.

### 1.7 Collision and Havok

- The schema has about 90 `bhk*` types (struct and niobject names containing `name="bhk` in `build/nif.xml`, counted by
  `grep`), the generic `HavokFilter` / `HavokMaterial` compounds are read as inline "mixins" (`mixinTypes` in
  `NifXmlHandler`), and Fallout 4's `bhkPhysicsSystem` / `bhkRagdollSystem` / `bhkNPCollisionObject` are an opaque
  `ByteArray` blob (`build/nif.xml`, "Fallout 4 Physics"). The Windows build links Havok for MOPP generation; the fork's
  changelog says the spell was reworked to use an external tool (`NifMopp`). **Out of scope for OpenEden: we read and ignore,
  or map to Jolt shapes from the geometry; we never generate or consume Havok-specific data.** Not read: `col.cpp` beyond
  the file name, `havok.cpp`, `moppcode.cpp`.

## 2. The glTF export path

`src/lib/importex/gltf.cpp` (2,464 lines; `README_GLTF.md` for the user-facing limits). The entry is `exportGltf`; the
class `GltfStore` builds a `tinygltf::Model`.

1. **Scene tree.** `createNodes` walks `scene->nodes.list()` (the viewer's already-built `Node` objects), keeps `NiNode`,
   `NiTriShape`, `BSTriShape`, `BSGeometry` blocks, and emits one glTF node per block with the local transform (translation in
   metres, rotation quaternion, uniform scale), custom `extras` (`ID`, `Parent ID`, `Flags`, `Material Path`). Children are
   wired in a second pass from the NIF child links. Selection export restricts to the subtree of the selected block.
2. **Intermediate structure.** `MeshFile` (`src/io/MeshFile.h`): flat arrays: `positions`, `normals`, `colors` (Color4),
   `tangents`, `bitangentsBasis` (+-1), `coords1`, `coords2`, `weights` (packed `(bone << 16) | weight` in 16.16 fixed point,
   `weightsPerVertex`), `triangles` (3 x `ushort`), `lods`. `GltfStore::meshFileFromShape` fills it from the viewer's `Shape`
   (the same structure the loader of `.mesh` files fills for Starfield: `MeshFile::update`, which reads quantised 16-bit
   positions with a scale factor and up to 8 weights per vertex). This is the closest model of an engine-neutral scene
   structure for task 0082, and it is small.
3. **Vertices are not deduplicated or welded** in the exporter: it writes the shape's arrays as they are (one glTF primitive
   per shape, one accessor per attribute, 16-bit indices). Dedup exists only as editor operations (`--RemoveDuplicateVertices`).
4. **Skinning.** `createNodes` finds each shape's bone names from the skin block (`getBoneNames`) and matches them **by name** to
   the exported node names to fill `joints`; if any name is missing it falls back to a "flat" skeleton rebuilt from the bind
   transforms (`flatSkeleton`, with a warning). Inverse bind matrices are the skin data's bone transforms
   (`createInverseBoneMatrices`, translation in metres). The skeleton root is the node named `COM` or `COM_Twin` when present
   (the README explains the user may paste the `COM` branch from `skeleton.nif`). Weights are exported as `WEIGHTS_0/_1`
   (float, divided by 65535) and `JOINTS_0/_1` (u16); a "fix Bethesda's non-zero weights" step zeroes entries whose packed
   value is 3 or less.
5. **Materials.** `exportMaterial`: for Skyrim, FO4, FO76 it maps texture slots to glTF (albedo slot 0, normal slot 1 for
   lighting or 3 for effect shaders, occlusion/PBR from slots 5 and 7 on FO4 and 8 to 10 on FO76, emissive from slot 2 when
   the glow map flag is set), alpha mode from the alpha property (`BLEND`, `MASK` with the property's threshold, else
   `OPAQUE`), `doubleSided` from the shader flags, and `baseColorFactor` of 1. For **Skyrim only the diffuse and normal maps
   are exported** (README). Textures are decoded from DDS and re-encoded as PNG into the output (`addGltfTexture`,
   `loadTexture`, a mip level setting).
6. **What it cannot express** (README and code): collision, controllers (animations), particle systems; the full Skyrim shader
   model (envmap, skin tint, parallax, multilayer, subsurface, vertex-colour use) is reduced to a metallic-roughness material
   with a base colour and normal map; indices are 16-bit, so shapes with more than 65,535 vertices cannot be exported (the
   NIF format itself stores `ushort` vertex counts for `BSTriShape`, so this is rarely met in practice); at most 4 weights
   (8 only through `MeshFile` for Starfield `.mesh` data); LOD export is Starfield-only; glTF import does not support skin
   partitions and imports Skyrim shapes as `BSTriShape`.

## 3. Reading it as a model for `Nif::Scene`

What is worth taking as **ideas** (not code):

- The set of attributes a mesh has to carry: positions, normals, a tangent and a bitangent sign, two UV sets, colours with alpha,
  up to 4 (8 for Starfield) bone influences, triangles, bone name list with inverse bind matrices, a shader/material block of
  value fields plus texture paths, an alpha mode, double-sided, and a pointer from the shape to its material path.
- A **separate step from tree to arrays** (the viewer's `Shape::updateData` resolves "where is the vertex data" per version; the
  exporter then only sees arrays). That is what task 0082 already specifies.
- Handle **three places for vertex data** per game: the shape's own array (Oblivion/Fallout 3/Skyrim LE data blocks, Fallout 4
  `BSTriShape`), `NiSkinPartition` (Skyrim SE skinned shapes with `Data Size` 0), and `.mesh` files (Starfield, not our
  concern).

## 4. Validation tooling: oracle ideas

- **Headless mode** exists (`NifSkope -no-gui --Option <path>`, `src/main.cpp`, `README.md`): `ConvertToInternalGeometry`,
  `ConvertToExternalGeometry`, `RemoveUnusedStrings`, `RemoveDuplicateVertices`, `RemoveUnusedVertices`, `GenerateMeshLODs`,
  `OptimizeIndices`, `AddTangentSpacesAndUpdate`, `UpdateBounds`, `CombineProperties`, `RemoveBogusNodes`, `ReorderBlocks`,
  `SanitizeBeforeSave`. **Every one of them modifies `.nif` files in place** (the option text says "Files are overwritten
  in-place" for the folder-recursive ones). **It has no dump, report or export option, and it cannot export glTF.** Never
  point it at game or mod folders; only at copies. It does set a non-zero exit code and writes "failed to parse NIF data" when
  `NifModel::getMessages()` is non-empty after a load, which would make a parse-success sweep over a copy possible (not tried).
- **GUI glTF export** (File menu, `exportGltf`) is the ready-made per-file oracle for geometry: positions in metres (times the
  unit factor), the tangent swap above, 16-bit quantised weights, bone names, inverse bind matrices, material texture paths in
  the PNG-embedded form. Comparing needs the transform steps listed in the task (units, root Y-up rotation, tangent
  swap, weight quantisation of about 1.5e-5, the 0.00001 weight cut).
- **Schema oracle.** `build/nif.xml` can be read as data: per version row, the list of block types with their `since` /
  `until` / `vercond` guards. A dev tool in OpenEden could list which block types `Nif` has and which a given version needs,
  and compare field conditions (note 0043 section 5.2). This does not copy the file into the repository (section 7).
- **Tests in the fork:** `tests/` holds one test (`tst_autosanitize.cpp`) and a fixture folder; no corpus of NIFs, no golden
  outputs. Nothing to borrow.
- **Corpus sweeps** would be our own: the fork's behaviour of aborting on an unknown block is itself a signal (a file the fork
  loads is a file whose blocks the schema names).

## 5. What differs from upstream NifSkope, per the fork's own words

Sourced only from `README.md` and `CHANGELOG.md` of the fork; upstream's code was not read. The README calls the fork "an
experimental fork of 2.0.dev9": rendering for Starfield materials; updates for the current Fallout 4, Fallout 76 and
Starfield file formats (the schema marks the Fallout 4 development streams 132 and 139 `supported="false"`);
glTF export and import for Skyrim SE, FO4, FO76 and Starfield (LE export added 2025-08); resource extraction and chooser
dialogs; shading restored for Oblivion and Fallout 3/New Vegas; spells working on `BSTriShape` geometry; a mesh
simplifier and batch (headless) processing; Qt 6 and OpenGL 4 core. The schema file is also the fork's own edit of the
niftools one (about 2,300 differing lines against the pinned upstream commit, by `diff`, including added blocks such as
the Skyrim PBR block and many Starfield blocks).

## 6. What the fork does not do that a game engine needs

- **Streaming, caching, background loading:** none; a file loads synchronously into the tree.
- **LOD handling:** `BSMeshLODTriShape` LOD sizes and Starfield `.mesh` LODs are selected for display by a slider
  (`BSShape::updateLodLevel`); there is no runtime LOD policy and no distance switching.
- **Memory layout and speed:** a variant per value, a pointer per item, `QModelIndex` lookups per vertex field. The
  viewer's `updateData` loops with `getIndex` and `get<...>` for each vertex attribute. Reading 100,000 vertices that way is
  fine for an editor and far too slow for a game; the fork is not a performance reference.
- **Shader permutation tables / material systems:** the viewer reproduces the shaders of each game in GLSL
  (`res/shaders/sk_*.frag` and others) from the property fields. There is no material table, no batching.
- **Untrusted input handling:** not designed for it. Bounds on array sizes are the editor's own checks (not audited here).
- **Animation:** the viewer plays controllers; not read.
- **A stable ABI, a unit-tested library:** it is an application with Qt types throughout.

## 7. Risks and licence consequences

Read from files; where a licence text was not opened it says so. This is a record of facts for toddhow, not legal advice.

| Item | Licence, as read | File I read | May OpenEden reuse it? |
| --- | --- | --- | --- |
| NifSkope source written by the project (`src/`) | BSD 3-clause, copyright 2005-2014 NIF File Format Library and Tools; the file says it covers only "original NifSkope sources" | `LICENSE.md`; the same BSD text in the header of `src/xml/nifexpr.h` | Code could be reused **with the notice and the no-endorsement clause**; recommended: **no**, ideas only (it is Qt/variant based and our reader is hand-written). Files added by the fork's author (for example `src/lib/importex/gltf.cpp`) carry no licence line I found; **not checked per file**, so treat their licence as unknown |
| `build/nif.xml` in the fork | **No licence header in the file.** `LICENSE.md` says nothing about it | `build/nif.xml` (first lines, a search for "licen", "copyright") | Unknown for the fork's edits |
| `nif.xml` in niftools/nifxml (the fork's pinned upstream commit `970a6238…`) | **`LICENSE` at the repository root is the GNU GPL version 3 text**; `nif.xml` itself has no header | `I:\Projects\OpenEden-work\niftools-nifxml\LICENSE` and the first lines of its `nif.xml` | Not copied. Reading it as a dev oracle is fine. **Committing it, or code generated from it, would bring GPL-3.0 content (the file does not say "or later") into a repository whose guide says GPL-3.0-or-later**: ask toddhow. This corrects note 0043 section 5.2, which recalled a BSD licence for nifxml: **the recollection was wrong for this commit** |
| `build/docsys/*.py` (nifxml.py, gen_niflib.py) | BSD 3-clause, copyright 2005 NIF File Format Library and Tools | file headers | Not needed |
| `lib/libfo76utils/` (archives, material DB, DDS, ...) | MIT, copyright 2021-2024 fo76utils | `lib/libfo76utils/LICENSE` | Permitted with notice; not recommended (see below) |
| gli, tinygltf, nlohmann json, meshoptimizer, DirectXMesh | MIT (per `LICENSE.md`; DirectXMesh also in the header of `lib/meshlet.cpp`) | `LICENSE.md`, `lib/meshlet.cpp` header | meshoptimizer is already adopted by OpenEden (`docs/dependencies.md`); the others are not needed |
| Miniball | Apache-2.0 | `LICENSE.md`; `lib/miniball/LICENSE-2.0.txt` exists | not needed |
| xxHash | BSD 2-clause | `LICENSE.md` | not needed |
| Qhull | its own licence file | `LICENSE.md` names it; `lib/qhull` is a submodule not fetched: **not read** | not needed |
| **`lib/dxgiformat.h`** | LGPL 2.1 or later, copyright 2007 Andras Kovacs | file header | **Not credited in `LICENSE.md`.** Not needed |
| **`lib/stb_image.h`** | public domain (as the header says) | first line of the file | **Not credited in `LICENSE.md`.** `stb_image_write.h` **not read** |
| **`lib/libfo76utils/README.detex`** and its detex code | a short permission notice (copyright 2015 Harm Hanemaaijer) | first lines | Not credited in `LICENSE.md`; only the first lines read |
| **`lib/coacd.cpp` / `coacd.h`** | the files load a `lib_coacd` library at run time; **the licence of CoACD was not read** | headers only | do not touch |
| **`lib/dds.h`** | Microsoft DDS definitions; **licence not read** | first lines | do not touch |
| **Havok** (MOPP generation in the Windows build; `src/spells/moppcode.cpp`, `havok.cpp`) | `LICENSE.md` gives Havok's copyright line and no licence terms | `LICENSE.md` | **Must not be touched.** CLAUDE.md guardrail: no proprietary SDKs, never add Havok. Not opened. The external `NifMopp` tool the changelog mentions was not read |

**Practical consequences**

1. **No fork file is copied into the repository.** The fork is a reference and an oracle.
2. **The schema is the licence question.** If the oracle reads `nif.xml` at run time from a local copy outside the repository
   there is no licence consequence for OpenEden's code. If we want a generated reader or a committed schema, we need toddhow's
   decision and a recorded GPL-3.0-only versus "or later" analysis; the fork's own schema edits have no stated licence at all.
3. **Do not use `libfo76utils` for archives or materials**: OpenEden has `components/bsa` (TES3, TES4 with LZ4, BA2) already,
   and the material formats (BGSM/BGEM) belong to the Fallout 4 pack, which is not now needed.
4. **Facts from the fork are facts about file formats**, which are not copyrightable, but we cite them as "the fork's viewer
   assumes", not as truth, until our oracle agrees on toddhow's files.

## 8. What I did not read

`src/io/materialfile.cpp` and `material.h` (beyond how `fileName` uses them), `src/gamemanager.cpp`, `src/gl/BSMesh.cpp`
(Starfield), `src/io/MeshFile.cpp` beyond the first header and position loop, `lib/libfo76utils/src/*` beyond `ba2file.cpp`'s
BSA function names and the licence, all Havok-related code, `src/spells/*` (except looking at `tangentspace.cpp`'s checks and
`sanitize.cpp`'s first lines), `src/lib/importex/col.cpp` and the glTF import half, the shaders, `NiTriShape` skinned drawing,
animation/controller code, upstream NifSkope's source, the licences of the CoACD and DDS code, and `qhull`, `gli` and
`meshoptimizer` submodule contents (not fetched).
