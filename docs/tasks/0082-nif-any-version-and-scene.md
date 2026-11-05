# Task: NIF of any version, and an engine-neutral scene
Design doc section: `docs/design/game-packs.md` section 3 (the common compatibility pack) and stage b of section 10; `docs/research/0043-separating-formats-from-game-rules.md` section 5.
Goal: the NIF reader loads every version the game packs need by explicit option, with tests on generated and on user-supplied files, and offers its content as plain arrays (`Nif::Scene`) that no renderer type appears in.
Inherited code touched, at commit `671c7676e9`: `components/nif/niffile.cpp` (`Reader::parse`: `supportedVers`, `sLoadUnsupportedFiles`, `setLoadUnsupportedFiles`, the hashed-record-type and big-endian refusals) and `niffile.hpp` (`NIFFile::NIFVersion`, `BethVersion`); `components/nif/node.cpp` line 188 (`NiNode::read` replaces record 0's transform by the identity unless the node is named `bip01`: a Morrowind rule); `components/nif/node.hpp` (`BSTriShape`, `BSDynamicTriShape`, `BSSubIndexTriShape`), `data.cpp`, `physics.cpp`; the callers of `Reader::setLoadUnsupportedFiles` (`apps/niftest`, `apps/edenskelstat`, the body tools). Read-only for reference: `components/vsgadapters/nif/nif.cpp` (the NIF to VSG converter; `NiGeometry` only), `components/bodynif/*`, `components/nifjolt`, `components/lod/nifflatten.*`, `extensions/compat_morrowind/src`.
Inputs / outputs: new `Nif::ReadOptions` (`allowedVersions`, `strict`) replacing the process-wide flag; new `components/nifscene/` (`scene.hpp`, `build.cpp`: `Nif::Scene buildScene(const NIFFile&, const SceneOptions&)`); a generated-file builder for tests in `apps/components_tests/nif/`; a tool `apps/niftest` extension (`--report <folder>`, aggregate output only); no change to a file format.
Status: draft for toddhow's approval. **Full loop** (root guide: it handles untrusted input).

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

## Open questions (recommendation first)
1. Hand-written reader plus `nif.xml` as an oracle, or a generated reader? Recommend the first (research note 0043 section 5.2): cheaper, clean licence, same coverage when checked.
2. Is `components/nifscene` a new directory or part of `components/nif`? Recommend new, so `nif` stays the record reader and the scene stays a consumer; both are in the formats layer.
3. Where do Morrowind's `bip01` and animation-name conventions live afterwards? Recommend `compat_morrowind` supplies the `SceneOptions` (it is already the Morrowind data adapter).
4. How far down the influences does `Scene` go (4 as the renderer uses, 8 as read)? Recommend keep what the file has and let the consumer cut; the merged-body work (task 0069) already cuts to four.
