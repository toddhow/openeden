# Task: AdvancedBodies, the merged legacy body (merge Morrowind's body parts into one welded skinned mesh and cache it)
Design doc section: `docs/design/advancedbodies-in-openeden.md`, section 7A (this direction), with 4.3 (derived cache), 6 (tiers of change), 7 (the assembled-parts problem, which this supersedes as the *runtime* answer); research note `docs/research/0032-merged-body-from-legacy-parts.md` (the facts and the open "verify" list); guide `docs/design/Body-System-Design-Guide.md` sections 1, 4, 6 and 10.
Goal: from the player's own installed game data, build one welded, skinned, region-mapped body mesh per (race, sex, third-person view) by merging the stock body parts, cache it as derived data in the user data folder (never committed, never shipped), and draw it instead of the ~20 separate parts when a setting is on, so that no mesh has to be modelled or shipped by us and so that a modder's own full-body package later takes the same path with no merge step.
Status: DRAFT written 2026-11-03, not approved, nothing built. Full loop for every stage (hostile-input readers, a renderer path, a format that will freeze, determinism, save-adjacent). The numbering of the design doc's staged table is stale (see "Numbering" below); this spec is **0069**.
Inherited code touched (re-read at the commit that starts each stage; writing this spec changes nothing): `apps/openmw/mwrender/npc.cpp` (`Npc::updateParts` final default-parts loop, `addOrReplaceIndividualPart`, `removeIndividualPart`, `reserveIndividualPart`, `insertBoundedPart`, `defaultPartModels`, `markSkinModel`), `apps/openmw/mwrender/bodyparts.cpp` (`getBodyParts`, `getHeadModel`, `getHairModel`: called, never changed), `components/mwanimation/partbones.hpp` (`sPartBones`: read), `apps/openmw/mwrender/animcontext.cpp` (`partBoneOptions`: read), `components/vsgadapters/nif/nif.cpp` (`handleSkin`, `filterMatches`: read as the reference for what the engine does with a part; **not edited**), `components/animation/skin.hpp` and `softwareskin.hpp` (the skinning the merged mesh must reproduce), `apps/openmw/mwrender/bodylegacy.*` (B1's adapter: extended), `components/body` (B1's package model, reader, validator: extended in stage b), `apps/openmw/vsgengine.cpp` (a `--bake-bodies` and `--analyse-bodies` mode, like `--bake-lod-objects`).
Inputs / outputs: reads the ESM store and the VFS of the loaded content; writes `<user data>/openeden/bodies/merged.edenpak` (a derived Eden Archive pack: entry names are generic path keys, no change to the container's entry-type registry), a `current` pointer file and a `status.json`; new settings `[Bodies] merged body` (false) and companions; new libraries `components/bodynif` (skinned NIF reader over `components/nif` only) and merge code in `components/body`; new game modes `openmw --analyse-bodies` (read-only report) and `openmw --bake-bodies`.

## Plain language

Morrowind builds a person from about twenty small meshes hung on the skeleton. That makes bodies hard to change (every morph opens gaps where parts meet) and costs twenty reads per person. toddhow does not want to model a replacement body. So the engine will **make the one-piece body itself, from the parts the player already owns**: find the parts a race and sex use, read them, join the vertices where the parts touch, keep the skin weights, and sort the triangles by body region. The result is saved in the player's own data folder, like the LOD bakes, and is never shared or committed, because it is made from the game's meshes. Equipment still works as today: wearing a cuirass hides the chest region of the merged body instead of replacing the chest part. A modder who has made a real full-body mesh supplies it as an ordinary body package and skips the merge. The merged body is also what makes surface sliders (design tier 2) possible later: one mesh has no part-to-part gaps.

## Constraints that never change in this task

- **No Bethesda data in the repository, ever** (root `CLAUDE.md`, Guardrails). Tests use synthetic meshes and synthetic records. Stage a runs on the user's own data and commits **only aggregate numbers** (counts, histograms, distances), not part files and not their names beyond what earlier docs already cite. Raw reports stay in `I:\Projects\OpenEden-work\bodies\`.
- **Off means unchanged.** With `[Bodies] merged body` false the code path that exists today runs, nothing new executes except one setting read per actor build, and frames, part lists, settings and saves are as before (compared, not assumed; see stage d).
- **Fallback is stock.** Any failure (no cache entry, stale key, a reader refusing the entry, a validator error, an exception) means: draw the stock parts for that actor, log one line, never stop the game.
- **Untrusted input.** The cache file and any modder package are untrusted (a tampered or corrupted cache must not crash or run away). Readers have size caps, never throw across the boundary, and are fuzzed like the Eden Archive reader.
- **Determinism.** Same inputs and parameters give identical bytes on one machine (no clock, no randomness, no unordered iteration, one thread per body, thread count does not change bytes). Cross-machine equality is not promised (research note 0032 section 2, item 7).
- **Threading.** Merging runs in the bake mode or on a worker; node creation and compile happen on the main thread (`apps/openmw/mwrender/CLAUDE.md`). Say in the stage log which thread each new function runs on.
- **No allocation in per-frame code.** Merging is not per frame; the per-frame cost of a merged body is one skin controller update and a few switch toggles, with an allocation-counter test in stage d (as `components/body` stage B1 did for its own rules).
- **GCC `-Werror -Wshadow -Wunused -Wdangling-else` clean**; braces around every if/else body containing a macro; 120 columns; clang-format 14.
- **File names** lowercase letters and digits (`CI/check_file_names.sh`); `components/bodynif` and any new directory get a short `CLAUDE.md`.

## Design in short (the full text is design doc section 7A and research note 0032)

1. **Unit of merge:** one body per (race, sex, third-person view), **neck down** (neck, chest, groin, upper arms, forearms, wrists, hands, upper legs, knees, ankles, feet, and the tail slot for beasts). Head and hair stay parts (decision H14). First person, werewolves and creatures are not merged.
2. **The merge** (research note 0032 section 2): bring every part into one bind space; weld coincident vertices inside a data-derived tolerance; keep UV and normal splits as weld groups, not as one vertex; renormalise the 4-influence weights and give a weld group the equal-weight mean of its members' weights; one canonical bone list; one submesh per material key; triangles sorted by region (slot number) so a region is a contiguous range; fixed vertex, triangle, submesh and group order.
3. **The package:** the merged body is a B1 `BodyPackage` (one `skin` LOD 0 mesh with `meshFormat "bodymesh"`, a `regionMap`, `weldGroups` and `provenance`), the same package a modder's own full body is. Stage b adds the `"bodymesh"` value and the `weldGroups` part to the format and the validator.
4. **The cache:** a derived pack in `<user data>/openeden/bodies/`, key = merger version + parameters + (race, sex, view) + per part (record id, part type, flags, race, model path, **model byte hash**) + the skeleton file and its hash; stale entries are ignored and the body falls back to stock; written whole under a new name and published by an atomic rename; built ahead of time with `openmw --bake-bodies` and, per decision H15, also by a background worker for entries the player needs and does not have.
5. **The engine path:** `Npc::updateParts`, when the setting is on and an entry exists for the actor's race and sex, attaches the merged body node once (skin controller bound to the same bones by name) and hides the regions of every slot that equipment or a reservation owns; unequip shows them again; head, hair, weapons and equipment parts are unchanged.

## Stages (each committed working, tested, with its frames or numbers recorded; full loop each)

### Stage a. Measure the stock parts on the user's real data (no behaviour change)

Build `openmw --analyse-bodies`: a read-only game mode (needs the ESM store and the VFS, like the LOD bake modes; no window beyond the usual quiet one, `OPENEDEN_QUIET_WINDOW=1`) over a new `components/bodynif` reader. For every (race, sex) and every beast race it prints, as canonical JSON and as text: the selected part records (ids and slot only), per part vertices, triangles, bones, influence histogram, skinned or rigid, shape names per file, material keys; per **seam** (each adjacent slot pair listed in research note 0032 section 1): boundary vertex counts, the nearest cross-part boundary distance (count under 1e-4 units, minimum, median, 95th percentile, maximum), overlap depth, gap size, `max abs(wA - wB)` of the weights of nearest pairs, count of pairs whose UVs differ, angle between normals (median, maximum); file sizes; the number of head skin records per race and sex.
- [x] The report answers each row of research note 0032 section 8 with numbers, for the playable races of both sexes and for the beast races.
- [x] The report is deterministic (same bytes twice), written under `I:\Projects\OpenEden-work\bodies\analysis\`; the stage log records **aggregates only** and a **go/no-go table** with its reasons: weld tolerance rule applied to the real histograms and the number it gives; whether seams coincide, overlap or gap; weight disagreement; whether beast races follow the structure; whether rigid parts exist; head record counts; the size and time estimate from the formula in note 0032 section 4 with the measured V and R. **No-go** (any seam family whose parts neither coincide nor overlap in a way a nearest-neighbour ring weld can fix) revises stage b before it starts and is reported to toddhow.
- [x] `components/bodynif` has unit tests on synthetic NIF records built in memory (one skinned shape, two shapes with names for the skin filter, a rigid shape, a hostile file: zero bones, weights for a missing vertex, huge counts; refused with located errors).
- [ ] With the mode not used, nothing changes: unit tests and the 32 Lua tests pass.

### Stage b. The merger as a pure library (synthetic meshes only)

`components/body` gains `bodymerge.*`: input `PartMesh` plain arrays (positions in part space plus the part-to-bind transform, normals, UVs, triangle indices, per-vertex influences by **bone name**, material key, slot number, rigid flag, mirror flag), output `MergedBody` (arrays, bone list, submeshes, region triangle ranges, weld groups, a `MergeReport` with the numbers the validator and stage a compare) or a located error. Plus the format change: `meshFormat "bodymesh"`, the optional `weldGroups` part, and their validator rules (group members coincide within the tolerance; no vertex in two groups; seam quality recorded; the existing weight, index, region and bounds rules apply unchanged).
- [ ] **Tests first, written from this spec before any merge code, and run red** (stage b of 0066 skipped the red run; this task does not). Cases: two quads sharing an edge give one welded edge and no duplicate vertices; a UV-split seam keeps two vertices and one weld group; a rigid mirrored part flips winding and normals; weights are renormalised after the 4-influence cut and ties break by canonical bone order; a weld group's merged weights are the mean; vertex, triangle, submesh, region and group order are fixed; the result is **independent of the order the parts are passed in** and of thread count (byte-identical); the validator accepts every output and refuses each hostile input (NaN, infinite, indices out of range, zero or negative weights, a bone missing from the list, a count over the caps, a part with no triangles) with the part and index in the message.
- [ ] Property test on random meshes: CPU-skinning the merged body at random poses equals CPU-skinning each source part, to the float tolerance the test names, at every vertex that is not in a weld group with disagreeing weights; for those the difference is bounded by the largest weight disagreement times the largest bone displacement.
- [ ] The conformance suite of B1 gains a merged-body fixture (written from synthetic parts) and each defect fixture yields exactly its expected code.
- [ ] A fresh reviewer reads the diff against this spec; no unresolved finding.

### Stage c. The payload layout, the cache and `openmw --bake-bodies`

A strict canonical reader and writer for the `bodymesh` payload (one generic entry in the derived pack; layout version 0 is a **draft**; it freezes after toddhow's approval, like the tile layout S1), the key computation, the pack writer, `openmw --bake-bodies` (the game executable in bake mode, as `--bake-lod-objects`; low priority; one thread per body; a unit per (race, sex); the part byte hashes computed from the files), the `current` pointer, a minimal `status.json` (state, counts, per-body failures with a one-line reason; compatible with the shape of task 0061's contract so the unified baker can adopt it).
- [x] A bake of the user's Morrowind data writes the pack; a second bake changes no byte (**byte-identical re-bake**) and reports "up to date"; size and time are recorded next to the stage a estimate.
- [ ] **Staleness** (unit tests with a fake VFS and fake records): changing one part's bytes, one record's model path, one record's flags, the skeleton file, the merger version or a parameter changes the key of exactly the bodies it touches and no others; an entry with a non-matching key is ignored; a mod that replaces a part appears as a new key.
- [x] **Atomic publish:** a bake killed during the write leaves the previous `current` and a complete previous pack; a half-written pack is never read (a test truncates the file at every 4 KiB boundary and bit-flips every region: the reader refuses each, never crashes, never allocates over the cap; the fuzz test follows the Eden Archive's, `docs/tasks/0052-*.md`).
- [ ] The layout is written down (`docs/spec/` or the task) and approved by toddhow before it freezes; a frozen layout refuses any other version.

### Stage d. The engine path, behind `[Bodies] merged body` (off by default)

A builder in `apps/openmw/mwrender/` that turns a cache entry into a vsg node with an `Anim::Skin` controller bound by bone name (as `handleSkin` does), one draw per (submesh, region range) under a switch per region, the subsurface flag set from region roles, the same options the stock parts get (shared objects, front face). `Npc::updateParts`: when the setting is on, the merged body exists for (race, sex) and the actor is not a werewolf or creature, it attaches the merged body once, hides the regions of slots with priority above 0 and of reserved slots, and does not add the stock skin parts; head and hair are added as today. Any failure falls back to stock for that actor with one log line. Settings: `[Bodies] merged body` (false); the engine reads `current` once at start.
- [ ] **Off means unchanged.** With the setting false: a recorded list of the part slots and model paths of a fixed NPC set (a test or a trace line) matches the commit before; `enabled = false`-style byte-identical frames of the benchmark scenes; 32 Lua tests, `components-tests`, `openmw-tests` pass. The log says what was compared and that it is not "within noise".
- [ ] **Numeric equivalence:** with the software skin (`Anim::SoftwareSkin`) the merged body and the stock parts, at five poses (bind, walk mid-stride, arm raised, crouch, run) for one human male and one human female, agree at every non-seam vertex to the named float tolerance; at weld-group vertices the difference is within the disagreement stage a measured. The test runs in the game's test harness on the user's data (no data in the repository) and prints the numbers into the stage log.
- [ ] **Frames:** stock against merged at the same poses (a scripted screenshot set, `OPENEDEN_QUIET_WINDOW=1`): the number of pixels that differ by more than 4 levels, and where (the seams only, by a difference image that I view); the verdict is written down. Joint seams must show no gap and no new crack.
- [ ] **Cost:** `[Cells] load trace` with the setting on and off for the same NPC-heavy walk (Balmora): `npc.part.read` and `npc.part.attach` counts and total ms per person, resident and device memory with 50 NPCs. The saving is **recorded, not promised**. If merged is slower or larger, the log says so and the setting stays off.
- [ ] Fallbacks tested: no pack, stale key, corrupt entry (bit-flipped), validator-refused entry, an exception in the builder, a werewolf, a creature, first person: each draws the stock parts and logs one line.
- [ ] `eden-studio` is rebuilt and starts (the settings defaults file changes); the window of every test game is quiet.

### Stage e. Equipment and regions

Equip and unequip every slot type with the merged body on: the hidden regions equal the visible-region set the stock logic gives (a unit test of the pure function `slotsToHiddenRegions(priorities, reservations)` against `Npc::updateParts`'s outcome for every slot combination, written from the slot table in research note 0032 section 5), a scripted in-game check with screenshots (cuirass, greaves, pauldrons, boots, gauntlets, helmet with hair rule, shirt and pants, robe and skirt reservations, gloves, a full set), no gap at region boundaries, no z-fighting between the body and armour that the stock parts did not already show.
- [ ] The unit test passes for all combinations; the screenshots are viewed and the verdict recorded; swapping equipment repeatedly leaks no node (the allocation and node-count counters return to baseline after 200 equip/unequip cycles).
- [ ] Strict level kept: nothing legacy is merged or morphed; surface morph hooks (B3) get the region ranges and the weld groups but are not built here.

### Stage f. Eden Studio body package panel

A studio module (`editor_modules/advancedbodies_editor`, no Qt in the module, no engine types, like `creativemotion_editor`) that opens a body package (folder, `.edenpak`, or an entry of the merged cache), runs the validator and lists findings, colours regions and weld seams in the mesh viewer (task 0043; whether it can draw a skinned mesh in the rest pose is **verify**, the fallback is the bind-pose static mesh), and shows the stage a numbers for the body. Lighter loop only if toddhow marks this stage's spec lighter.
- [ ] Opens a merged cache entry and a synthetic modder package; findings and region colours match the validator and regionMap; `eden-studio-tests` cover the module's model.

## Numbering

`docs/design/advancedbodies-in-openeden.md` section 17 reserved 0063 to 0075 for B1 to B10 on 2026-11-02. Since then 0064 (VFS pack placement), 0065 (CreativeMotion push sandbox), 0066 (whole-map view), 0067 (CreativeMotion runtime) and 0068 (far water) were taken by other work, so **that table is stale from B2 on**. Later AdvancedBodies stages take the next free numbers when they are written, and the design doc's table should be re-numbered then. This task is outside the old table's numbering: it is the "merged legacy body" stage, which B3 (sliders) will depend on instead of the virtual assembled body.

## Decisions (recommendation first; toddhow decides; full table in the design doc, rows H13 to H20)

- H13: direction = merge programmatically from the player's own parts, cache, never ship (recommended, toddhow's instruction); alternative: hand-model one.
- H14: neck-down body, head and hair stay parts (recommended); alternative: merge per head record.
- H15: when to build: ahead of time with `--bake-bodies`, plus a background worker for missing entries, and the actor draws stock parts for its whole life if its entry is missing when it is created (recommended); alternatives: build during the loading screen (blocks), or swap a live actor when the entry arrives (a visible pop).
- H16: payload: a small canonical `bodymesh` layout in the derived pack (recommended; the engine can build a node from arrays today, glTF mesh rendering is not verified); alternative: glTF `.glb` through a new writer.
- H17: seam weights: equal-weight mean of weld-group members (recommended); alternative: one owner part per seam.
- H18: scope: humans first; beast races only if stage a shows the same structure; no werewolf, creature or first person (recommended).
- H19: one submesh per material key, no atlas (recommended); alternative: a texture atlas (needs a block-compression encoder; deferred as in the LOD clusters).
- H20: the skinned NIF reader lives in `components/bodynif` over `components/nif` only (recommended); alternative: inside `bodylegacy.*` (cannot be used by the tools and tests without the engine).

## Risks

1. **Seams are unknown until stage a.** The whole plan is gated by the go/no-go table. If stock parts neither coincide nor overlap cleanly at some joint, that joint needs a nearest-neighbour ring weld with a distance limit, or a small authored fix-up that is data, not a mesh.
2. **Weights may disagree at seams.** The animated seam could differ from stock by a visible amount. Stage d bounds it numerically and by frames; the fallback is the stock parts (setting off).
3. **Left and right in one file.** The skin filter picks a shape by name; if the naming is not uniform, the reader needs a table. Stage a counts.
4. **Material count.** If a race's parts use many textures the merged body has many submeshes; the draw count may not fall as much as hoped. Measured in stage d; not promised.
5. **Cache invalidation by mods.** A mod that changes a part model changes the key; a mod that changes the skeleton changes the skeleton hash. A load-order change that selects different parts changes the part set. Covered by the key tests; a bake after a load-order change is needed (the status line says so).
6. **Skin marking and PBR.** A merged body is created by code, not by a model path, so `markSkinModel` cannot mark it; the role-based mark (design 6.5) is built in stage d and compared with the path mark by the `subsurface` debug view.
7. **`components/body` stays engine-free.** The merger takes arrays; reading NIF is `components/bodynif` (depends on `components/nif` only); the ESM selection is the engine adapter. A file that includes `apps/` from `components/` breaks a root rule.

## Out of scope

Sliders and morph application (B3, design 6); garments and layered clothing (B6); cosmetic physics (B7 onward); NPC variety by seeded proportions (B3, the first visible AdvancedBodies milestone); the `BODY` save record; first person, werewolf and creature bodies; merging heads; texture atlases; the unified background baker (0061; this task writes a status file compatible with it); a hand-modelled body.

## Acceptance criteria (summary; each stage has its own list above)

- [ ] Stage a: the report, its go/no-go table, `components/bodynif` tests; aggregates only in the repository.
- [ ] Stage b: tests written first and run red; merger, `bodymesh` and `weldGroups` in the format and validator; property and conformance tests; a fresh reviewer.
- [ ] Stage c: byte-identical re-bake; staleness tests; atomic publish, truncation and bit-flip tests; layout approved by toddhow.
- [ ] Stage d: off means unchanged (byte-identical frames and recorded part lists); numeric equivalence; frames viewed; cost recorded; fallbacks tested.
- [ ] Stage e: the pure mapping test for all slot combinations; equipment screenshots viewed; no leak after 200 cycles.
- [ ] Stage f: the studio panel opens a merged entry and a modder package.
- [ ] With every new setting at its default, Morrowind behaves as before, and the pull request says how that was compared.
- [ ] No Bethesda file, no game asset, no cache file and no third-party body in the repository; `CI/check_file_names.sh`, `CI/check_clang_format.sh` (clang-format 14), `CI/check_no_osg.sh`, `CI/check_extension_sdk.sh` pass; the per-area `CLAUDE.md` for `components/bodynif` exists and `components/body/CLAUDE.md` is updated.
- [ ] A fresh reviewing agent read each stage's diff against this spec and found no unresolved issue; toddhow approved the merge.

## Open questions for toddhow

See the end of the design doc's section 7A (the same list, numbered, each with a recommendation).

## Stage log (implementer's notes)

### Stage a, 2026-11-03: the stock parts measured (`--analyse-bodies`, `components/bodynif`)

**Result: go, with a changed scope for stage b.** The go/no-go table and every aggregate number are in research note
0033; the raw report is under `I:ProjectsOpenEden-workodiesnalysis` and is never committed. In one line: forearm to
wrist and knee to ankle share their ring exactly (all 40 units, within 0.0011 units); every other joint is overlapping
closed shells or a gap; 16 of 19 human parts are rigid, only the chest and hands are skinned; no seam has skin on both
sides.

- **Built:** `components/bodynif` (`skinnedshape.*` reader, `seamanalysis.*` measurements, `analysisreport.*` report),
  `apps/openmw/bodyanalyser.*` and the `--analyse-bodies` option (read-only; main thread, one race and sex per frame;
  the report is byte-identical on a second run), module guide `components/bodynif/CLAUDE.md`.
- **Found while measuring, and built in response:** the first run found nothing meeting: rigid parts are in their attach
  bone's space and skinned parts in the skin's, so a rest pose from the skeleton file (`readRestPose`) places both; a left
  rigid part is mirrored in x in the bone's space as the engine does (`decorate`); a skinned part goes to its bind pose
  (formula in the note, **not yet proved** against the engine's skinning); closed parts have no boundary, so the pairing
  also runs over all vertices; the window grew from 2 to 6 units after overlaps were found larger than 2.
- **Tests:** `BodyNifFilter`, `BodyNifReader`, `BodyNifHostile`, `BodyNifRestPose`, `BodyNifPlacement`, `BodyNifBoundary`,
  `BodyNifDistribution`, `BodyNifPartStats`, `BodyNifMakePart`, `BodyNifSeam`, `BodyNifReportTotals/Aggregate/Json/Text`
  (71 tests with the `TwoHands` fixture), synthetic records and meshes only. Red run before implementing: yes for the
  reader, the seam maths, the report, the rest-pose placement, the mirror and the skinned bind pose (and the rigid
  placement test fails when the placement line is disabled). **Not red first:** the edge census and bounds and the
  all-vertex pairing tests were written just after their code (the first real-data run showed they were needed). One test
  fixture error was corrected: the synthetic grids number their UVs from 0, so the seam test needed matching UVs.
- **Results of the unit tests (Windows):** `components-tests` 4846, `openmw-tests` 859, `eden-studio-tests` 578, all
  passing. **Not run:** the 32 Lua integration tests and the Linux gate (so the box "with the mode not used, nothing
  changes: unit tests and the 32 Lua tests pass" stays open); the mode adds one option and one `if (bodyAnalyser)` per
  frame.
- **Threads:** every function in `components/bodynif` is pure; `BodyAnalyser::update` runs on the main thread inside the
  frame callback. No game window of its own: the test game ran with `OPENEDEN_QUIET_WINDOW=1`.
- **Not verified:** see research note 0033 section 7 (bind-pose formula, signed gap along the centre line, window, only
  vanilla data, no first person, no clothing).

Open for toddhow after reading note 0033: the recommendation in its section 6 (merge without welding the overlapping
joints first, weld only the two exact seams at 0.01 units, cap removal later) and whether stage b starts.

## Answers of toddhow, 2026-11-03

All ten open questions of the design doc's section 7A.5 were answered "all yes" (each recommendation accepted): 1 head stays a part (H14); 2 ahead-of-time bake plus background worker, stock for the actor's whole life when its entry is missing (H15); 3 a small canonical `bodymesh` layout, a draft until approved (H16); 4 equal-weight mean at weld groups (H17); 5 humans first, beasts only if the structure matches, no werewolf/creature/first person (H18); 6 one submesh per material, no atlas (H19); 7 a new `components/bodynif` (H20); 8 the runtime check trusts the bake's pointer and compares records and file sizes without re-hashing; 9 after a load-order change the game draws stock parts and says to re-run the bake; 10 stage a runs alone and toddhow reviews its go/no-go table before stage b starts.

toddhow, 2026-11-03 (after reading the stage a go/no-go table, note 0033): a first merged-body version that welds only the two exact seams (forearm-wrist, knee-ankle) is acceptable; the overlapping joints stay as overlapping shells; cap removal later. Stage b starts on that scope.

### Stage b, 2026-11-03: the merger as a pure library (`components/body/bodymerge.*`)

**Built:** `mergeParts` and `computeWeldMap` (game-agnostic; the header comment is the specification), 35 merger tests in
`apps/components_tests/body/testbodymerge.cpp` and two bind-pose tests appended to
`apps/components_tests/bodynif/testskinnedshape.cpp`. Thread: pure functions, one body per call, called from tests
only so far (stage c runs them in the bake mode or on a worker).
- **Input:** `PartMesh` plain arrays (positions in part space, normals, UVs, triangles, influences by bone name or one
  rigid bone, material key, slot, mirror flag, a 3 x 4 placement); `SeamDeclaration` (two slots, a tolerance).
- **Output:** `MergedBody`: arrays, one canonical bone list (the options' known order first, then the other used names
  by byte value; only bones still used), region ranges per slot, submeshes per (slot, material), weld groups, a
  vertex map per input part, the canonical part order, a `MergeReport`.
- **Rules as built:** parts in a canonical order (slot, material, name, sizes, content) so that the result does not
  depend on the order passed in; mirror flag and placement as one transform (positions by the matrix, normals by the
  cofactor matrix with the determinant's sign, winding flipped when the determinant is negative); weld sets by
  union-find over vertex pairs within a seam's tolerance (a sorted-vector grid; two vertices of one part are never
  welded to each other; a seam of a slot with itself welds two parts of that slot); a set takes the position of its
  lowest member; inside a set, vertices with equal UVs (1e-5) and normals within 5 degrees merge, the others stay and the
  set is a weld group; weights: rigid part = 1.0 on its bone, a set gets the equal-weight mean of its members' weights
  bone by bone, then the four largest (ties by canonical bone index) renormalised; degenerate triangles from a weld
  are dropped and counted.
- **Hostile input:** counts capped (`MergeLimits`), indices, finiteness, coordinates (1e7), placement (finite and not
  singular), seams (tolerance 0 to 100, slots exist, at most 2,000,000 pairs per seam), weights, bone indices and names;
  a located message per problem (at most 16), never an exception.
- **Tests first, red first:** the header and a stub that returns "not implemented" were committed to the build, and
  the 35 merger tests were run: **0 passed, 35 failed**; then the code was written and all 35 passed. A mutation check
  (a `<` for the inclusive tolerance, the tie-break reversed, the winding flip disabled) failed 6 tests (tolerance edge,
  tolerance-zero seamless case, tie-break, mirror winding, the seamless mean test and the disagreement bound), then the
  code was restored. The suite also covers the Skyrim-like seamless case at tolerance 0, the mean of weights at a seam
  (including a UV split), 24 permutations of the part order, a property test (CPU skinning of the merged body equals
  skinning of each part at random poses to 1e-4 when seam weights agree; bounded by the disagreement when they do not),
  and no vertex or triangle lost. `components-tests` 4915 passed on Windows.
- **The skinned bind-pose formula is now checked against the engine (stage a's open item).** `BodyNifBindPose` builds a
  skinned part from Nif records and a skeleton at rest, converts each transform with the engine's own
  `vsgAdapters::convertTrafo` and `Anim::Transform::t_transform`, composes the bone matrices as `Anim::Skin::apply`
  does, and skins with `Anim::SoftwareSkin`: the reader's bind pose equals the engine's skinned vertices to 1e-4.
  **Honest limits:** it composes the matrices by the formula read from `Skin::apply` (the real `Skin::apply` needs a
  linked scene graph and was not run); it holds when the skin's own transform is the identity, which is true for every
  skinned stock part (all 72 skinned part slots of the stage a report have identity transforms). Where the skin has a
  transform of its own, the engine multiplies it on the other side (`transform * skinMatrix`), so the reader's order
  differs; a second test pins that difference. A mod part with such a transform would be placed differently by the
  reader than the engine draws it: stage c's adapter must either use the engine's order or refuse such a part.
- **Not done in stage b (still open boxes above):** the `bodymesh` value of `meshFormat`, the optional `weldGroups` part
  and their validator rules, the conformance fixture of B1's suite, a fresh reviewer, the 32 Lua tests and the Linux
  gate. The merger takes bone names as given: the adapter lower-cases or maps them (stage c), and the engine's mirror
  of "Left" bone parts is the caller's `mMirror`. The normal merge rule is the equal-weight mean, not area weighted.

### Stage c, 2026-11-03: the payload, the cache, `--bake-bodies`, `--check-bodies` (commits `9c1042c603`, `fe7b70c474`)

**Built.** In `components/body`: `meshFormat "bodymesh"` and the optional `weldGroups` part of the package format with the
rules `BODY-WELD-001` to `007` (stage b's leftovers); `bodymesh.*` (the payload, layout version 0, a DRAFT, written down
in `docs/spec/bodymesh-payload-v0-draft.md`); `bodycache.*` (`computeBodyKey`, `bodyEntryName`, the manifest, `publishCache`,
`loadCurrentCache`, the status file). In `apps/openmw`: `bodybaker.*` and the options `--bake-bodies` and `--check-bodies`
(wiring in `main.cpp`, `options.cpp`, `arguments.hpp`, `vsgengine.cpp`: a few lines each, as `--analyse-bodies` has;
`MWRender::sLodBakeRun` is set so that the long view distance is not used). Threads: the pure functions run anywhere;
`BodyBaker::update` runs on the main thread, one race and sex per call; god mode is switched on at the start, as the
object bake learned.

**Tests first, red first.** `apps/components_tests/body/testbodymesh.cpp`, 25 tests written before the code against stubs
(the header and a `not implemented` body): **20 failed, 5 passed** (the five were refusal tests that a stub passes
vacuously); after the implementation all 25 pass; `components-tests` 4947, `openmw-tests` 859, `eden-studio-tests` 578
pass on Windows. Covered: the format round trip and canonical writer, every WELD rule with its code and path, hostile
weld groups (65,537 groups, a negative index, a fractional index, an unknown key); the payload round trip byte for byte,
**every truncation refused, a bit flip refused or canonical for every byte** (three bits per byte), hostile counts (all
eight set to 0xFFFFFFFF), a wrong magic, a wrong version, a trailing byte, thirteen bad bodies (NaN, infinity, indices,
weights, regions, submeshes, weld groups, names) refused by the writer; the key (every input changes it, the part order
does not, another body's key does not move), the entry names (safe lower-case paths, no collision between `Dark Elf`,
`dark_elf` and `Dark-Elf`), the manifest (round trip, strict reader: duplicates, order, over-long lines, control
characters, version), publishing (published, loaded, re-published identically, a second publish moves `current` and keeps
the old pack, a killed bake leaves the previous cache readable, a bad or unsafe pointer refused, **the pack truncated at
every 4 KiB and bit-flipped every 97 bytes refused or harmless**).

**Real data** (Morrowind, Tribunal, Bloodmoon; 20 races and sexes, third person; a copy of toddhow's config; user data
`I:\Projects\OpenEden-work\bodies\bake`, never committed). Beast races were baked too (the stage a structure allows it):
Argonian and Khajiit give 1,860 to 1,920 vertices and no failure.

| | |
| --- | --- |
| bodies / failures | 20 / 0 |
| input vertices into output vertices | 1,483 to 1,922 into 1,477 to 1,920 (2 to 6 vertices fewer: only the exact seams weld) |
| weld sets per body | 30 (the four exact seams); 24 to 28 weld groups (UV and normal creases stay split) |
| payload per body | 111,508 to 140,862 bytes; 2,367,858 bytes for all 20 |
| pack `merged-7aeaa1b7184bb7e2.edenpak` | 2,434,032 bytes (store-only, 4 KiB entry alignment) |
| bake time | the merges of all 20 bodies take 0.24 s of the 3 s the process runs; a second bake that keeps every entry takes 0.17 s |
| **byte-identical re-bake** | yes: a second bake into the same folder says "up to date (the same pack as before)", 20 kept, 0 merged; a bake into an EMPTY second folder (all 20 merged again) gives the same pack, SHA-256 `a3801420...ced06`, `cmp` equal |

The stage a estimate (note 0032 section 4: 44 to 52 bytes a vertex plus 6 a triangle, about 100 KB a body, 2 MB for 20)
was close: the measured payload is 56 bytes a vertex and 12 a triangle (u32 indices, float32 weights), 118 KB a body.

**Read-back check** (`--check-bodies`, `check.txt`): for all 20 bodies the key recomputed from the data now equals the
manifest's, the payload reads, merging the stock parts again gives **the same payload bytes**, every stock vertex has its
merged vertex within 0.001067 units (the weld tolerance is 0.01; 0 to 34 vertices of a body move at all, the females
only), and **at five poses (the bind pose and four seeded ones, every bone moved rigidly) the skinned position of every
vertex the weld did not touch equals the stock skin to 1.6e-5 units** (float rounding). The 68 weld-affected stock
vertices of each body differ from the stock skin by up to 23 units at the seeded poses: **this is the design, not a
bug, and stage d must say so in its numbers**: the stock forearm and wrist (and knee and ankle) are rigid parts that hold
the seam with weight 1.0 on two different bones, so the weight disagreement at the exact seams is 1.0 (the report's
`mMaxWeightDisagreement`), and the equal-weight mean puts a welded seam vertex halfway between the two bones' motion. The
difference is bounded by 1.0 times the displacement between the two bones' motions (23 units is what the seeded rotations
of up to 0.6 rad about the origin give at a vertex 40 units away). In the game a wrist turning against the forearm gives a
smooth bend at the seam instead of the stock's hard step (and gap or overlap). Whether that looks better or worse is a
frame question for stage d; the alternative rule (one owner part per seam, decision H17's alternative) is cheap to try.
The first check run flagged this as a problem because it counted only moved vertices; the report now separates
weld-affected vertices from the rest and the failing criterion is on the rest (that is a deliberate change of the check's
rule made after seeing the data, recorded here).

**Not done in stage c.** The conformance fixture of B1's suite for a merged body (the WELD rules are tested in
`testbodymesh.cpp`, not through the conformance harness); the staleness tests use the key's input struct, not a fake VFS
and fake records (the adapter that reads the VFS and the records is not unit tested: its behaviour is covered by the real
data run and the check); a fresh reviewer; the layout approval by toddhow; the 32 Lua tests and the Linux gate; the
background worker (stage d's neighbour: H15); `bodyanalyser.cpp` and `bodybaker.cpp` duplicate about 80 lines of NIF
reading helpers (`parseFile`, `shapesOf`, the slot list): a shared header is the cleanup, left out to keep the analysis
unchanged.

**Starting values to tune with stage d's frames:** the UV tolerance 1e-5 and the normal angle 5 degrees (the merged
bodies kept 24 to 28 weld groups, i.e. 24 to 28 creases among 30 weld sets: most seam vertices stay split, so the normals
at the seams are NOT averaged across; the welded vertex count is only 2 to 6 per body), the weld tolerance 0.01.

**What a fresh reviewer must check:** that `check()` is the only rule set and the reader cannot accept what the writer
would refuse (the bit-flip test asserts it for one small body); the key covers every input of the merge (the skeleton,
the part FILE bytes, the records' fields the adapter reads; not covered: a change to the engine's skin filter or to
`Placement`, which the merger version must be bumped for); the god-mode and `sLodBakeRun` handling; that a bake killed
between the pack and the pointer leaves a valid previous state (the code order: pack first, pointer last); that
`bodybaker.cpp` fails closed (a race with one unreadable slot is a failure, never a partial body).
