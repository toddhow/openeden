# Task: NIF of any version, and an engine-neutral scene
Design doc section: `docs/design/game-packs.md` section 3 (the common compatibility pack) and stage b of section 10; `docs/research/0043-separating-formats-from-game-rules.md` section 5.
Goal: the NIF reader loads every version the game packs need by explicit option, with tests on generated and on user-supplied files, and offers its content as plain arrays (`Nif::Scene`) that no renderer type appears in.
Inherited code touched, at commit `671c7676e9`: `components/nif/niffile.cpp` (`Reader::parse`: `supportedVers`, `sLoadUnsupportedFiles`, `setLoadUnsupportedFiles`, the hashed-record-type and big-endian refusals) and `niffile.hpp` (`NIFFile::NIFVersion`, `BethVersion`); `components/nif/node.cpp` line 188 (`NiNode::read` replaces record 0's transform by the identity unless the node is named `bip01`: a Morrowind rule); `components/nif/node.hpp` (`BSTriShape`, `BSDynamicTriShape`, `BSSubIndexTriShape`), `data.cpp`, `physics.cpp`; the callers of `Reader::setLoadUnsupportedFiles` (`apps/niftest`, `apps/edenskelstat`, the body tools). Read-only for reference: `components/vsgadapters/nif/nif.cpp` (the NIF to VSG converter; `NiGeometry` only), `components/bodynif/*`, `components/nifjolt`, `components/lod/nifflatten.*`, `extensions/compat_morrowind/src`.
Inputs / outputs: new `Nif::ReadOptions` (`allowedVersions`, `strict`) replacing the process-wide flag; new `components/nifscene/` (`scene.hpp`, `build.cpp`: `Nif::Scene buildScene(const NIFFile&, const SceneOptions&)`); a generated-file builder for tests in `apps/components_tests/nif/`; a tool `apps/niftest` extension (`--report <folder>`, aggregate output only); no change to a file format.
Status: **approved by toddhow, 2026-11-06, with all eight recommendations of the open questions below** (answers recorded after them). **Full loop** (root guide: it handles untrusted input).

## Version priority (toddhow, 2026-11-04: Skyrim SE is the second game)
1. Morrowind 4.0.0.2 (and 4.0.0.0): must be byte-for-byte what loads today.
2. Skyrim SE 20.2.0.7, user version 12, stream version 100.
3. Skyrim LE (stream 83), Oblivion 20.0.0.5 and 10.0.1.2, Fallout 3/New Vegas (stream 34), Fallout 4 (stream 130). Fallout 76 and Starfield: constants exist, not required.
The user versions of Oblivion, Fallout 3/NV and Skyrim LE are recalled, not read: **verify** from the files before asserting them in tests.

## Design
- **Reader options.** `Reader::parse(…, const ReadOptions&)`. The game default is `{Morrowind}` (exactly today); tools and the editor pass `{Any}`. `sLoadUnsupportedFiles` and its setter are removed after their callers move (a listed compile-time change in three tools).
- **Version tables in one place.** A `VersionInfo` table (name, file version, user version, stream version, the first-class blocks) so "what do we load" is data, printed by `niftest --versions`.
- **The Morrowind rule leaves the reader's default path.** The `bip01` rule becomes a `SceneOptions` flag (`morrowindRootRule`), set by the Morrowind adapter; the node reader keeps the identity for record 0 only under that flag. Morrowind output is unchanged (test).
- **`Nif::Scene`.** Plain structs of floats and vectors (no VSG, no Jolt): nodes (name, local transform, parent, flags, extra data as key/value), geometry as flat arrays (positions, normals, tangents, UV sets, colours, 32-bit indices, skin influences with at most 8 per vertex kept as read plus the count), shader/material properties as value structs with texture paths, skin (bone names, inverse bind, partitions), collision data, controllers as description records. `BSTriShape` is decoded here: packed vertex formats (half-float positions and weights per the vertex flags), the zero vertex count of Skyrim SE skinned shapes (the data is in `NiSkinPartition`; research note 0036 section 2), and Fallout 4 weights that exist only in the packed vertex data.
- **No renderer work in this task.** `vsgadapters/nif` is unchanged and still draws `NiGeometry` only. A later task converts `Scene` to VSG and draws `BSTriShape` (it is needed for Skyrim bodies in the game, research note 0036 section 9 item 8, and is not part of this task).
- **Schema oracle (optional, separate approval).** A dev tool that compares the factory's block list with `nif.xml`, only after the licences of `nif.xml` and NifSkope are read and recorded (research note 0043 section 5.2). No `nif.xml` content enters the repository in this task.
- **Errors:** every refusal names file, version, record index and block type; no exception crosses a module boundary except the reader's existing `Nif::Exception`, caught at the loader (current rule).

## Constraints
- Determinism: the same file gives the same `Scene` bytes (a canonical dump used in tests); no unordered iteration.
- Untrusted input: record counts, array lengths and string sizes are bounded before allocation (the reader's current checks are listed in the stage log and extended; fuzz test with the repository's deterministic fuzz pattern from the Eden Archive tests, task 0052).
- Allocation: reading allocates as today; `buildScene` reserves from counts; none per frame.
- No game assets in the repository. Generated files only (below); real files are read in place from the user's installs and only aggregates are written, as in note 0036.
- No new dependency. `nifly` or any other library is not adopted by this task.

## Acceptance criteria
- [ ] All existing `components-tests`, `openmw-tests`, `eden-studio-tests` and the 32 Lua tests pass; `niftest` over Morrowind's meshes reports the same results as before the change (list in the stage log)
- [ ] **Generated files:** the test builder writes minimal valid files for each priority version (header, string table or inline names, root node, one shape, one skin) and the reader returns the expected `Scene` for each; one malformed variant per refusal (bad header, truncated, giant count, big-endian, hashed names) fails with a located error and no crash
- [ ] **User-supplied files:** `niftest --report` over toddhow's installed Skyrim SE set (CBBE 3BBB, XPMSSE, HIMBO, The New Gentleman) and Fallout 4 files reads all 43 files of note 0036 and writes aggregates only (counts of blocks, vertices, bones, shapes with zero vertex count); the numbers agree with note 0036 (60 skinned shapes with count 0 in 30 files; weights agree with `NiSkinData` within 5e-6, as the note measured)
- [ ] `Scene` of a Morrowind mesh contains the same vertices, indices and transforms as the current converter reads (a comparison test on generated Morrowind-version data, and a byte-identical `bodynif` skeleton result on the repository's synthetic cases)
- [ ] With Morrowind selected nothing changes: `vsgadapters/nif`'s output on Morrowind data is identical (the existing `vsgadapters` tests), and an in-game check of a few cells with the stage log's steps shows the same frame (a frame is viewed before any render-adjacent commit, project rule)
- [ ] Layer rules (task 0081): `nifscene` is in `formats` and includes nothing above `base`
- [ ] Linux `-Werror` build and the sanitizer tests pass (run once before the push)
Out of scope: drawing `BSTriShape`; Havok `.hkx`; NIF writing for any game; the `nif.xml` generator; Starfield/FO76 support beyond the constants.

## Reference: the fo76utils NifSkope fork
Research note 0044 (`docs/research/0044-loading-modern-nifs-fo76utils-nifskope.md`) records what was read in the fork (`fo76utils/nifskope`, commit `380631a408`) and in the upstream schema. The fork is a reference and an oracle; **no file from it enters the repository** and no Havok-related code was opened. Findings that change this plan:

1. **The fork's reader is a run-time interpreter of `nif.xml`**, producing a tree of named variant items (`NifModel::loadXML`, `loadItem`). It is not a model for a fast typed reader; it confirms open question 1 (hand-written reader, the schema as oracle). It also shows the whole selection mechanism is small: `since`/`until` on the file version, `vercond` on the stream version, `cond` on earlier fields, `arg` for the vertex flags.
2. **Version table, per the schema (`nif.xml` rows 191 to 221):** Oblivion 20.0.0.5 user 10 or 11 stream 11; Fallout 3/NV 20.2.0.7 user 11 stream 34; Skyrim LE user 12 stream 83; Skyrim SE stream 100; Fallout 4 stream 130; Fallout 76 stream 155; Starfield 170 and up. This replaces "recalled" above with "per the schema"; tests still read the numbers from real files.
3. **Skyrim SE skinned shapes have no vertex data in the shape.** `BSTriShape.Data Size` is 0 and the vertices, bone weights and indices are in `NiSkinPartition.Vertex Data` (stream 100 only); the triangles are the partitions' own lists. Fallout 4 (stream 130) keeps vertices in the shape and uses `BSSkin::Instance`/`BoneData`. The fork's viewer **assumes** the vertex stream's bone indices index the skin instance's `Bones` list and ignores the partition's own `Bones`, `Global VB` and `LOD Level`: our oracle must check that on toddhow's files.
4. **Vertex format is decided by the flag bits 44 to 55 of `BSVertexDesc`, not by the offsets.** Fixed field order; the position's 4th slot holds bitangent X; positions are always 3 floats in stream 100 and half floats in stream 130 and later unless `Full_Precision` (bit 10) is set.
5. **Tangent naming is swapped** relative to glTF: the fork exports the NIF's "bitangent" as the glTF tangent and recovers the sign by cross product. `Nif::Scene` must state its convention and pin it with a test. Normal-map green-channel convention: not read.
6. **Units:** the fork converts stream < 170 to metres with 0.9144/64 per unit, the same as Morrowind (the fork author's assumption, not a measurement). `BSDynamicTriShape` (heads) takes positions from a separate 4-float array.
7. **Licence:** the schema in niftools/nifxml at the fork's pinned commit is under the **GNU GPL v3** (its `LICENSE`), not BSD as note 0043 recalled; the fork's schema edits state no licence. Reading it as a dev oracle outside the repository is fine; committing it or generating code from it is a decision for toddhow (open question 5).

### The oracle (dev tool, separate approval)
- **What:** a dev tool `niftest --compare-gltf <glb> <nif>` (or a script in `tools/`) that loads one NIF with our reader, builds the `Scene`, reads the same file's glTF export made by toddhow in NifSkope's GUI (File, Export, glTF; the fork's headless mode cannot export), and prints **aggregates only**: counts of shapes, vertices, triangles, bones; maximum position difference (after undoing metres and the root Y-up rotation), maximum normal and tangent difference (after the tangent swap), maximum weight difference (tolerance about 2e-5: 16-bit quantisation and the 1e-5 cut), bone-name set equality, material texture paths equal. No file contents are written or committed; the report goes to the work folder.
- **Never run the fork's headless mode on game or mod folders:** every headless option rewrites `.nif` files in place. Copies only.
- **Second oracle (schema):** a dev tool that lists, per version row, the block types the schema has and our factory lacks; run on a local copy of `nif.xml` kept outside the repository.
- **Third check (cheap):** a parse-success sweep: our reader against the fork's "load without messages" over the same copied folder, listing files where they disagree.

### Reimplement first for Skyrim SE (order, replacing the implicit order above)
1. **Header and version table** for 20.2.0.7 stream 100 (the string table and block-type index path is already in `Reader::parse`; the new part is `ReadOptions` and the table).
2. **`BSVertexDesc` and `BSVertexDataSSE`**: flag decode, fixed field order, half UVs, byte normals and tangents, colours, 4 half weights and 4 byte indices; the bitangent-X-in-W rule; `Data Size == 0` handled as "no data here".
3. **`BSTriShape` (+ `BSDynamicTriShape`, `BSSubIndexTriShape` as thin subclasses)** into `Scene` geometry; then **`NiSkinPartition`'s global vertex stream and per-partition triangles**, and `NiSkinInstance`/`NiSkinData`/`BSDismemberSkinInstance` bind poses; this is what brings the 60 zero-vertex skinned shapes of note 0036 into `Scene`.
4. **`BSLightingShaderProperty` / `BSEffectShaderProperty` / `BSShaderTextureSet`** as value structs with texture paths (shader type, the two flag words, UV scale/offset, alpha, specular, emissive, clamp mode, the type-specific tail by `Shader Type`), plus `NiAlphaProperty`.
5. Then Skyrim LE data blocks (`NiTriShapeData`), Oblivion/Fallout 3 shaders, Fallout 4 (`BSVertexData` half positions, `BSSkin::*`, BGSM/BGEM material paths), collision shapes as data only (never Havok-specific generation), controllers.

## Open questions (recommendation first)
1. Hand-written reader plus `nif.xml` as an oracle, or a generated reader? Recommend the first (research note 0043 section 5.2): cheaper, clean licence, same coverage when checked.
2. Is `components/nifscene` a new directory or part of `components/nif`? Recommend new, so `nif` stays the record reader and the scene stays a consumer; both are in the formats layer.
3. Where do Morrowind's `bip01` and animation-name conventions live afterwards? Recommend `compat_morrowind` supplies the `SceneOptions` (it is already the Morrowind data adapter).
4. How far down the influences does `Scene` go (4 as the renderer uses, 8 as read)? Recommend keep what the file has and let the consumer cut; the merged-body work (task 0069) already cuts to four.
5. May a **local copy of `nif.xml`** (GPL-3.0 upstream, the fork's edits unlicensed) be used by the dev oracle tool, kept outside the repository and never committed? Recommend yes, local only; revisit before any generated reader.
6. Build the oracle on the fork's **GUI glTF export** (your hands, per file) or ask for a local, uncommitted patch to the fork that adds a headless dump? Recommend the GUI export first: no new code to maintain; the patch only if the number of files makes it tiresome.
7. For `Scene` tangents: store **glTF-style tangent + sign** (recommended: matches the oracle and the renderer's normal mapping) or the NIF's two vectors as read?
8. Treat SSE vertex-stream bone indices as **global** (the fork's assumption) until the oracle finds a counter-example? Recommend yes, with a loud error rather than silent skinning when a partition's bone map differs from the identity.

### Answers (toddhow, 2026-11-06: every recommendation accepted)
1. Hand-written reader; `nif.xml` only as an oracle.
2. New directory `components/nifscene` in the formats layer; `components/nif` stays the record reader.
3. `compat_morrowind` supplies the Morrowind `SceneOptions`; the `bip01` and animation-name rules move out of the nif reader there, step by step, with Morrowind loading byte for byte as today.
4. `Scene` keeps every influence the file has; the consumer cuts.
5. A local copy of `nif.xml` may be used by a dev oracle; it is never committed.
6. The oracle is the NifSkope fork's GUI glTF export, made by toddhow's hands; the reader is built against note 0036's measurements and the schema notes, and the comparison is a documented manual step that does not block the work.
7. `Scene` tangents are glTF-style tangent plus sign.
8. SSE vertex-stream bone indices are treated as global; a partition whose bone map differs from the identity is a loud error.

