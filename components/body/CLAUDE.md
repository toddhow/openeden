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
- **Welds never pair two vertices of one part directly,** and never join parts without a declared seam: the overlapping
  closed shells of Morrowind's joints are not welded (decision of 2026-11-03: only the two exact seams weld in the
  first version). A vertex of another part can still connect two vertices of one part (a chain); a set is then cut at
  the tolerance: a member further than the seam tolerance from the set's first member (the position every member takes)
  stays out, so no vertex ever moves further than the tolerance (review fix A5; the union-find alone allowed 2x).
- **Order independence is bit for bit.** The canonical part order ends in the skin weights and then in the bit patterns
  of every float, so parts that differ only in skin, or only in the sign of a zero, still sort the same whatever order
  the caller passes them in; the output never holds a negative zero (`+ 0.f`). Placements are bounded
  (`maxPlacementEntry` for rotation and scale, `maxCoordinate` for translation) and every placed position is checked
  against `maxCoordinate` BEFORE it is cast to float or turned into a grid cell (a double beyond `FLT_MAX` is undefined
  behaviour to cast).
- **The lookup has a work budget.** `MergeLimits::mMaxCandidateChecks` counts every candidate the grid compares, not
  only the pairs inside the tolerance: many vertices closer together than the finest cell (`minGridCell`, 1e-6) all land
  in one cell and the scan of it is quadratic.
- **Named starting values:** `uvMergeTolerance` 1e-5, `normalMergeAngle` 5 degrees (stage a measured normals up to 27
  degrees apart at exact seams: those are creases and stay separate), `maxCoordinate` 1e7, `maxSeamTolerance` 100,
  `MergeLimits`. Tune with the data of stage c, not before.
- **Weights.** A set gets the equal-weight mean of its members (member-weighted: a part with two vertices in the set
  counts twice); `MergeReport::mMaxWeightDisagreement` is the largest difference between two members before the mean,
  the bound of how far a weld moves a skinned vertex.
- **Not verified:** the Lua tests, the Linux gate, real data (stage c).

## The `bodymesh` payload and the cache (task 0069, stage c: `bodymesh.*`, `bodycache.*`)

- **`bodymesh.*`** is one merged body as bytes: magic `EDENBMSH`, layout version, flags, eight counts, then bones,
  materials, positions, normals, UVs, four bone indices and weights per vertex, triangles, regions, submeshes and weld
  groups. **Layout version 0 is a draft** until toddhow approves it (like the LOD tile layout S1); a reader refuses every
  other version. The reader and the writer share one `check()`: what the writer would refuse the reader refuses, so a
  payload that reads writes back to the same bytes (the bit-flip test asserts this for every flipped bit). Counts are
  checked against `BodyMeshLimits` and against the bytes that remain before anything is allocated.
- **`bodycache.*`** is the key, the manifest, the entry names and the publishing of the derived pack. The key
  (`computeBodyKey`) is a BLAKE3 digest of a length-prefixed text of everything a body's bytes depend on; it changes
  when a part's file bytes, record, flags, path, the skeleton file, the merger version, the layout version or a
  parameter changes, per body. Bump `bodyMergerVersion` whenever the merger's output for the same input can change.
  A pack is `merged-<16 hex of the manifest digest>.edenpak`; `current` names it and is renamed over atomically; a
  temporary file (`*.tmp`, unique per writer) and half-written packs are ignored. The pack reader is the Eden Archive's
  (hashes verified, caps).
- **The key is complete by construction** (review fixes B1 to B4). It is computed through structured bindings of
  `BodyKeyInput`, `BodyPartKey`, `MergeOptions`, `MergeLimits` and `SeamDeclaration`: a field added to one of them stops
  `bodycache.cpp` from compiling until the key learns about it. It carries the seams, the bone order, the UV and normal
  parameters, every limit, the merger's compile-time constants by name, `bodyMergerVersion` and
  `bodyReaderVersion`. The reader version is bumped by hand when the NIF reader, the skin filter, the placement or
  mirror rules or the adapter change; a test (`BodyNifReaderVersion`) pins a fingerprint of a synthetic reading, so a
  change cannot go unnoticed: when it fails, bump the version AND set the pin in one change.
- **Entry names** are `bodies/<race>-<sex>-<view>-<32 hex>.bodymesh` (a 128-bit digest of the exact tuple), and the
  baker refuses a name collision loudly instead of overwriting. Race, sex and view are percent-escaped in the manifest
  (`%XX` for control bytes and `%`, canonical only), so any id from a mod round-trips.
- **Writing.** A pack of the derived name that already reads back whole is not rewritten (a running game may have it
  open); otherwise it is written under a temporary name and renamed. `writeFileAtomic` serves the pointer, the status
  file and `check.txt`. `pruneOldPacks` deletes all packs but the named ones after a successful publish (the baker
  keeps the new pack and the previous one). A bake in which every body failed does not touch the pointer.
  `writeStatusJson` emits valid UTF-8 (a byte outside well-formed UTF-8 becomes `\ufffd`).
- **The payload reader is canonical, not only deterministic:** weights largest first (a tie by the lower bone index),
  unused slots last with weight 0 and bone 0, no bone twice for one vertex, weights adding up to 1 within 1e-5
  (`sCanonicalWeightSumTolerance`; the package validator keeps 1e-3), every listed bone used.
- **Package format:** `meshFormat "bodymesh"` and the `weldGroups` part (rules `BODY-WELD-001` to `007`). The weld
  groups of a merged body list the output vertices that sit at one place and must move together under a morph.
- **No game data** in any test: the synthetic quads of `testbodymesh.cpp` only.

## The skeleton mapping (task 0070, stages b to d: `skeletonmap.*`, `skeletonsuggest.*`, `skeletonmapping.*`, `bodyinfluences.*`)

Body replacers come with extended skeletons; OpenEden never adopts them. A `*.skeletonmap.json` (`openeden.skeletonmap` v1)
says for every source bone either "this is role X of OUR canonical skeleton" or what to do with it. Compatibility packs ship
their game's map; for a skeleton without one the suggestion engine writes a map a person must accept.

- **Refusal is the rule.** `buildMapping` returns nothing for a map that is invalid, built for another skeleton (digest or
  bone count), or has an extra entry that is not accepted or says `ask`. `BuildOptions::mAcceptAllSuggestions` is the explicit
  "accept all": it accepts unaccepted entries that have an action and never an `ask`. A refusal means the stock body is drawn
  (D7); partial maps are never applied.
- **Suggestions are suggestions.** `suggestSkeletonMap` writes `accepted: false` everywhere and never decides an ambiguity:
  equal top votes that disagree, and a bone nothing votes on, become `ask`. Weight, hierarchy and rest votes are fallbacks that
  only vote when no name vote exists. The vocabulary (stems, attach points, face and finger rules) is in the header comment;
  a stem of four or more characters matches by prefix, a shorter one only exactly.
- **Roles.** `canonicalRoles()` is the one table (17 core roles in the profile's order, then the optional ones). A pack's role
  table (`RoleBinding`: role, native bone, aliases) resolves roles first; name tokens resolve the rest (the pack's table
  decides which roles exist; with no table only the core roles, clavicles, `Spine2` and toes are tried).
- **Limits** (`SkeletonMapLimits`): 1 MiB, 1024 bones, 128-byte names, depth 64, 16 aliases per role, JSON nesting 6; the
  reader's pre-scan also caps objects, arrays and commas before any value is built. The validator caps its findings at 64 per
  rule.
- **The remap** is `remapVertex`: direct, split (the closer target gets the larger share by source rest distance), dropped
  (discarded and renormalised; a vertex left with nothing takes weight 1 on the fallback target that received the most
  dropped weight, summed per target); influences that are NaN, infinite, negative or zero are ignored; then `normalisedWeights` and `cutToFourWeights`
  (`bodyinfluences.*`, shared with `bodymerge.cpp`: change them and BOTH change, which is the point).
- **Matrices are row-vector** (`p * M`, world = local * parentWorld, skin = inverseBind * boneWorld). The inverse-bind
  correction `inverseBind * sourceWorldRest * inverse(canonicalWorldRest)` keeps rest vertex positions when the actor's rest
  pose differs from the one a mesh was skinned for; a test shows the uncorrected error (more than 1e-2) and the corrected one
  (under 1e-4).
- **Pitfall: `quoted(...)` is ambiguous.** A local helper named `quoted` taking a `std::string_view` or `std::string` collides
  with `std::quoted` through argument-dependent lookup on MSVC (error C2677); the helpers here are called `shown`.
- **Test fixtures** (`apps/components_tests/body/skeletonfixtures.hpp`) build every skeleton synthetically; no game or mod data
  is read. The vocabulary tests in `testskeletonsuggest.cpp` DO use a few functional bone names in the style of real
  extended skeletons (`NPC L Breast`, `NPC L Breast [LBrst]`, `NPC L Butt`, `NPC GenitalsScrotum [Gens]`,
  `NPC WeaponSword`): single names as identifiers (toddhow's answer 8 allows sharing bone names), never a table copied
  from a mod; whether that sits well with the "no tables from XPMSE" guardrail is a question for toddhow (review of
  2026-11-04).

### Review fixes (2026-11-04)

- **Names are compared as the engine compares them.** `engineBoneKey` is `Misc::StringUtils::lowerCase` (A to Z only;
  a test pins the two); every lookup by name (source bones in the validator's 005/006, skin bones in `remapTable`, actor
  bones in `compareRest` and `buildMapping`) goes through it. Files keep names as written; the skeleton digest hashes the
  exact bytes. `normalisedBoneName` (also merges separators) only refuses names a person could not tell apart: bones (004),
  aliases against bones and against each other (017), source bones (022).
- **Every core role is required** (BODY-MAP-024). The suggestion engine writes the core roles it found no bone for into
  `missingRoles` (after `roles`, omitted when empty), and while one is missing a weighted bone with no name vote is `ask`,
  never a fold that would freeze the joint.
- **The actor's whole skeleton** (`BuildOptions::mActorBones`): an extra bone that the actor also has (not a role bone)
  maps onto the actor's bone, whatever the map says, with a note; an extra bone named like the actor's bone of another
  role is a refusal. An optional role the actor lacks folds into the nearest canonical ancestor role it has (a note), and
  its rest frame is that ancestor's.
- **Two digests.** `sourceDigest()` ties a map to a skeleton; `skeletonMapDigest()` / `SkeletonMapping::mapDigest()`
  hashes the canonical text of the applied map, the part of a body cache key that changes with any decision.
- **Untrusted values.** Source rest values are refused before any cast when not finite, beyond `sMaxRestTranslation`
  (1e4 m) or not a unit quaternion within 1e-3 (BODY-MAP-025); the digest hashes a marker instead of casting such a
  value; a split fraction is never NaN; names must be well-formed UTF-8 (020, 016; the writer passes bytes through and
  the JSON reader refuses ill-formed UTF-8, so `read(write(m)) == m` needs it); the reader's pre-scan refuses a key that
  appears twice in one object (escapes decoded), because nlohmann keeps the last one.
- **Fail closed.** `compareRest` returns `RestComparison` with errors for an invalid skeleton, a role whose bone either
  side lacks, a singular parent, a non-finite value or nothing compared; `planRest` is `mValid = false` then, and a NaN
  angle counts as needing the correction.
- **Matrix conventions.** `skeletonmapping.hpp` is row-vector (`p * M`, `Mat4` row-major); the merger's `Mat34` is
  column-vector (`v' = M3 * v + t`, row-major); `vsg::dmat4` is column-vector, stored column-major. `toPlacement`,
  `fromPlacement` and `toColumnMajor` convert, and a test pins `toColumnMajor` against `vsg::translate * vsg::rotate`.
  Never convert element by element or through VSG's 16-number constructor.
- **Known limit (BODY-MAP-026, a warning):** a bone named exactly like a canonical role it is not cannot be named as a
  parent or fold target: role names resolve first.

## The engine's side of the merged body (task 0069, stage d: `mergedruntime.*`)

- **One decision, one order.** `decideMergedBody` takes plain facts and returns the FIRST that fails, in the order of
  `MergedBodyFacts` (setting, third person, werewolf, default skeleton, cache, entry, key computed, key current, payload,
  built, equipment). The engine (`apps/openmw/mwrender/mergedbody.*`, `Npc::attachMergedBody`) only gathers facts; add a
  new condition here, with a test, never as an extra `if` in the engine.
- **`planMergedDraws` returns nothing for any out-of-range index** (a submesh past the triangles, a material past the
  list, a vertex past the positions): the payload reader already refuses these, so an empty plan is a bug signal.
- **`invertAffine` is row-major `[M3 | t]`** like `BodyNif::Matrix34` (v' = M3 v + t) and refuses a determinant below
  1e-12 or any non-finite value, leaving its output untouched.
