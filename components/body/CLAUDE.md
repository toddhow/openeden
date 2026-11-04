# components/body (AdvancedBodies, stage B1, task 0063)

The body contract: the package model, the `openeden.body` v1 JSON reader and canonical writer, the one validator, the
packed form (`.edenpak`) and the command-line validator as a function. Target `eden_body`. Engine-independent: no ESM,
VSG, Jolt, Qt or MyGUI here (root `CLAUDE.md`, "Architecture rules"). The Morrowind adapter is
`apps/openmw/mwrender/bodylegacy.*`, and the program is `apps/edenbodyvalidate`.

## Rules

- **One rule set.** Every semantic rule is in `bodyvalidate.cpp`. The reader checks types, finiteness, enum names,
  integer ranges, id and path syntax of the package id, mesh ids and mesh paths, list sizes and nesting; nothing else.
  Programmatic and loaded packages therefore follow the same rules.
- **Untrusted input.** A package comes from a modder. `readBodyPackage` first scans the text for nesting depth (32) and
  the number of values (commas, a bound on memory) before nlohmann/json builds anything, caps strings (4096 bytes),
  lists (`Limits`) and the sum of numeric array lengths, and never throws. It also counts `[` and `{` apart from
  commas: at most 2,097,152 arrays (a skin of 100,000 vertices with 4 influences each, plus morph deltas, needs under
  1,000,000) and 65,536 objects (all lists together hold about 45,000 entries at their caps; a real package has a few
  thousand), and at most 262,144 region ranges per file (loader and validator). Messages from the validator escape control characters and are cut at 200 bytes. The validator caps its findings at 64 per
  rule and scope (first four path segments), then adds one "further findings are not listed" finding.
- **Reuse, do not copy.** The reader is derived from CreativeMotion's `opas::format::detail::FileReader`
  (internal header, linked PRIVATE through `opas_format_json`; no JSON type in a public header). An inline skeleton
  profile is re-dumped as indented text and read by `opas::anim::loadSkeletonProfile`; its error line is added to the
  line of the sub-object and its pointer is put below `/binding/skeleton`. Rig and profile files are checked with
  `opas::rig::loadRig` and `loadProfile`.
- **Canonical text** (the writer is in `bodyjson.cpp`, a small tree plus a layout pass): two-space indent, `\n`, one
  trailing newline; an array of scalars is one line (`[1, 2, 3]`); an object whose values are all scalars or such
  arrays is one line (`{"a": 1}`); everything else is expanded. Numbers are the shortest text that reads back, except
  that a whole number below 1e15 is written in full (`std::to_chars` would give `1e+06`); negative zero is `0`.
  Absent optional parts are omitted; arrays, `ageClass`, `unmappedBones` and `restrictedTier` are always written.
- **Units** are metres, kilograms, radians. Morrowind units exist only in `bodylegacy.cpp`, converted in `toBodyUnits`.
- **Packing** uses store-only generic path entries on one thread, so the pack and its manifest digest depend only on
  the files. Entry names must satisfy the Eden Archive path rules (lower case, no `\`): a file such as `Mesh/Body.glb`
  cannot be packed, and `writePackageFile` says so.
- **No clock, no randomness, no threads.** Reports are sorted (errors first, then path, code, message).

## Thresholds and their sources (`Body::Thresholds`, decision D7)

| Constant | Value | Source |
| --- | --- | --- |
| `sWeightSumTolerance` | 1e-3 | Weights are authored as text and normalised by exporters in float; 1e-3 is far above float round-off (about 1e-7 for 8 terms) and far below a visible skinning error (0.1 percent of a bone's pull). The fixtures use 0.25/0.75 (exact). Tests pin both sides (0.5e-3 passes, 2e-3 fails). A starting value, to be tightened with real exporter output |
| `sMaxInfluencesPerVertex` | 8 | glTF requires at least 4 and allows more through extra JOINTS/WEIGHTS sets; Morrowind NIF skins have at most 4 in practice; 8 is two glTF sets, the common upper bound of engine skinning. Starting value |
| `sMinBodyExtentM`, `sMaxBodyExtentM` | 0.05 m, 20 m | The extent is the largest side of the union of the LOD 0 skin meshes' bounds. The conformance bodies are 1.8 m (humanoid) and 1.4 m (quadruped); a wasp is a few centimetres and a dragon ten metres. The range catches unit mistakes (Morrowind units, 70 times too large, or millimetres) without rejecting small or huge creatures. Starting values, set from the fixtures and creature sizes, not from a measurement of real packs |
| rotation tolerance (private) | 1e-6 | CreativeMotion's `kUnitTolerance` in `core/rig/src/Skeleton.cpp` and `kUnitQuatTolerance` in the format reader; used for BIND-012 (unit length) and BIND-013 (identity above the root) |

## Pitfalls

- **Test name `Setup`.** A struct called `Setup` cannot be used inside `TEST()` bodies: googletest's `Test` has a member
  of that name (see the report of task 0063 stage b).
- **`humanoidGait`** needs the profile's canonical sources `Pelvis`, `LeftFoot` and `RightFoot`; with a profile that
  cannot be read it is false and all bone-existence rules are skipped (BIND-005 or BIND-006 says why).
- **A bone is "known" in two senses.** BIND-004 (skin bones) accepts profile targets and unmapped bones only; proxy,
  physics, landmark and soft tissue rules also accept rest skeleton bones.
- **`regionMap` coverage** is a sweep over sorted ranges, not a per-triangle array, so a hostile triangle count costs
  nothing.
- **Mesh summaries are not built in B1.** The in-engine `bodycheck` command passes no `MeshSource`, so every legacy
  mesh warns (BODY-MESH-001) and the skin and geometry rules are not exercised there (task 0063, risk 1).

## The merger (task 0069, stage b: `bodymerge.*`)

A pure, game-agnostic library: `mergeParts` (parts plus declared seams to one welded, skinned mesh) and `computeWeldMap`
(the weld sets alone, for a game whose parts already meet exactly). The header comment is the specification.

- **Nothing here names a game, an ESM record or a NIF.** The adapters fill `PartMesh`: Morrowind's from
  `components/bodynif` and `apps/openmw/mwrender/bodylegacy.*`; a seamless game (body, hands, feet that meet in exactly
  coincident vertices) through the same code with a tolerance of 0.
- **Determinism is a total order.** Parts are sorted by (slot, material, name, sizes, content); the grid lookup is a
  sorted vector; weld sets are rooted at their lowest vertex; ties in the four-influence cut go by canonical bone
  index. No unordered container is iterated. A test runs all 24 orders of four parts and compares the results.
- **Welds never join two vertices of one part,** and never join parts without a declared seam: the overlapping closed
  shells of Morrowind's joints are not welded (decision of 2026-11-03: only the two exact seams weld in the first
  version).
- **Named starting values:** `uvMergeTolerance` 1e-5, `normalMergeAngle` 5 degrees (stage a measured normals up to 27
  degrees apart at exact seams: those are creases and stay separate), `maxCoordinate` 1e7, `maxSeamTolerance` 100,
  `MergeLimits`. Tune with the data of stage c, not before.
- **Weights.** A set gets the equal-weight mean of its members (member-weighted: a part with two vertices in the set
  counts twice); `MergeReport::mMaxWeightDisagreement` is the largest difference between two members before the mean,
  the bound of how far a weld moves a skinned vertex.
- **Not verified:** the Lua tests, the Linux gate, real data (stage c).
