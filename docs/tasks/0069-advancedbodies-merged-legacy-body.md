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
- [x] Opens a merged cache entry and a synthetic modder package; findings and region colours match the validator and regionMap; `eden-studio-tests` cover the module's model. (Stage f log: 97 tests by a separate writer, the real editor on a copy of the stage e cache and a synthetic package, frames viewed; regions are coloured edges, not filled, and the bind pose is drawn, not a skinned rest pose: the stated deviations.)

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
- [x] Stage f: the studio panel opens a merged entry and a modder package (toddhow's approval of the merge still open).
- [ ] With every new setting at its default, Morrowind behaves as before, and the pull request says how that was compared.
- [ ] No Bethesda file, no game asset, no cache file and no third-party body in the repository; `CI/check_file_names.sh`, `CI/check_clang_format.sh` (clang-format 14), `CI/check_no_osg.sh`, `CI/check_extension_sdk.sh` pass; the per-area `CLAUDE.md` for `components/bodynif` exists and `components/body/CLAUDE.md` is updated.
- [ ] A fresh reviewing agent read each stage's diff against this spec and found no unresolved issue; toddhow approved the merge.

## Open questions for toddhow

See the end of the design doc's section 7A (the same list, numbered, each with a recommendation).

## Stage log (implementer's notes)

### Stage a, 2026-11-03: the stock parts measured (`--analyse-bodies`, `components/bodynif`)

**Result: go, with a changed scope for stage b.** The go/no-go table and every aggregate number are in research note
0033; the raw report is under `I:\Projects\OpenEden-work\bodies\analysis` and is never committed. In one line: forearm to
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

### Review fixes of stages b and c, 2026-11-04 (the report of the fresh review is in the session; findings A1 to A6, B1 to B4, C, D)

Done with the red-first discipline for each new test (they were run against the old code and failed, then the fix made
them pass). A2 (the 23-unit seam bend) is NOT changed: the weight policy is stage d's judgement by frames.

- **A1 (the check was circular).** `--check-bodies` now finds the weld-affected stock vertices WITHOUT the merger: a brute
  force over the stock parts for every vertex within a declared seam's tolerance of a vertex of the other slot, joined
  into connected groups. It asserts (1) the merger's set of welded vertices equals that set (real data: 68 and 68 in
  every body), (2) the weights of every welded vertex equal the mean of its group's stock weights, computed here from
  the stock weights (real data: largest departure 0.000000; vertices whose group has more than four bones are counted
  as not checked: none), (3) the skinned difference of a welded vertex is bounded by what its weight change and its
  position change explain (sum over bones of the weight change times how far the bone's motion carries the vertex, plus
  the move; an exact triangle-inequality bound for rigid bone motions). The unbounded "worst moved 23 units" is still
  reported but now only as information: the BOUND is what is asserted. `--check-bodies` ends the run with exit code 3
  when `problems` is not zero (new `Engine::exitCode`) and writes the verdict into check.txt. The earlier change of the
  rule (after seeing the data, stage c) stays recorded in the stage c log.
- **A3.** `compareParts` compares the skin (counts, bone indices, weights) and ends in a bit-pattern comparison of every
  float, so parts that differ only in skin or in the sign of a zero come out the same whatever order they are passed
  in; the output never holds a negative zero (`+ 0.f` on positions, normals and UVs). Real data: byte-identical to
  before.
- **A4.** Placement entries are bounded and every placed position is checked against `maxCoordinate` before the cast to
  float and before the grid cell; `cellOf` clamps and maps NaN to cell 0. Tests with 1e30 in the translation and in the
  scale, a translation beyond the limit, a scale that carries a vertex beyond the limit, and tolerances of 0, 1e-9 and
  1e-6 at coordinates of 9e6.
- **A5.** A weld set may not reach further than the largest tolerance of the seams that formed it from its first
  member; the member that does stays out. PINNED TEST CHANGED: `BodyMergeWeld.a_chain_of_close_vertices_is_one_set` had
  `p1` at 0.012 (a chain that the union-find joined and that moved a vertex further than the tolerance); it is now
  0.009 (a chain inside the tolerance, still one set) and `BodyMergeReview.a_weld_set_never_reaches_further_than_the_tolerance_from_its_first_member`
  pins the 0.012 case. The documents (header and module guide) no longer say "never joins two vertices of one part".
- **A6.** `MergeLimits::mMaxCandidateChecks` (50 million) bounds the work of the lookup, with a located error naming
  the seam; a test with 3,000 coincident vertices at tolerance 0 hits a budget of 100,000.
- **B1.** The key is computed through structured bindings of every struct it covers, includes the merger's constants and
  `bodyReaderVersion`, and is fed from the SAME objects the merger gets (`StockBody::mSeams`, `mOptions`, `mLimits`).
  `BodyNifReaderVersion` pins a fingerprint of a synthetic reading (recorded at its first run: 2143543024064129625, not
  red-first because it records existing behaviour). PINNED TEST CHANGED: `BodyCacheKey.everyInputChangesTheKey` had
  `mWeldTolerance`, `mUvTolerance` and `mNormalAngle`, which became `mSeams`, `mMerge.mUvTolerance` and
  `mMerge.mNormalAngle`.
- **B2.** Race, sex and view are percent-escaped in the manifest and the reader is canonical about it; entry names carry
  a 128-bit digest of the exact tuple; the baker refuses a name collision.
- **B3.** An existing pack that reads back whole is not rewritten; otherwise packs are written under a unique temporary
  name and renamed; `writeFileAtomic` for the pointer, status.json and check.txt, with the flush and close checked and
  failures reported (a failed status write counts as a problem). Not done: fsync (no portable call).
- **B4.** A bake in which every body failed leaves `current` alone and the status says failed; `pruneOldPacks` keeps the
  new pack and the previous one; the status JSON is valid UTF-8 whatever a race id holds. Not exercised in a running
  game: the all-failed case (it needs data without bodies) is a few lines, read, not run.
- **C.** The payload reader and writer are canonical (see docs/spec/bodymesh-payload-v0-draft.md). PINNED TESTS: none
  changed; 2 new tests (`BodyMeshPayloadCanonical`). Stage c's bit-flip test still passes: a flip that leaves a payload readable is canonical.
- **D.** The per-frame work is inside a try/catch that ends the run with a located message and a problem count (no
  exception reaches the game loop); NIF files are capped at 32 MiB before they are read (`maxNifBytes`, one pass for
  size and digest); the duplicated NIF helpers, the slot list and the unit moved to `apps/openmw/bodycommon.*`, used by
  `bodyanalyser.cpp` and `bodybaker.cpp`; `--bake-out` is the general name of the output folder (`--bake-gi-out` still
  works).
- **Real data, before and after the fixes (vanilla Morrowind, Tribunal, Bloodmoon; your config copied):** the 20
  payloads are byte-identical (SHA-256 of each, sorted lists equal), `analysis/bodies.json` and `bodies.txt` are
  byte-identical, `status` says complete, a second bake says up to date. What changed: the pack and manifest (new keys,
  new entry names, the digest of the manifest), and `check.txt` (the new fields). `--check-bodies`: 20 races and sexes,
  0 problems, exit code 0.
- **Tests (Windows):** see the report; new: 6 `BodyMergeReview`, 9 cache and payload ones, 1 `BodyNifReaderVersion`.
- **Not verified:** the Linux gate and the Lua tests; the all-failed publish path and the exception path in a running
  game; cross-compiler float equality of the cache bytes; the unit tests of the adapter against a fake file system (still
  not written).

### Stage d, 2026-11-04: the engine path behind `[Bodies] merged body` (commits `98b9809959`, `bdc6c1a229`)

**What was built.**
- `components/body/mergedruntime.*` (pure, engine-free): `decideMergedBody` (the facts in a fixed order, the first
  that fails is the reason: setting, third person, werewolf, the race's default skeleton, cache, entry, key computed, key
  current, payload, build, equipment), `describe`, `mergedSlotsCovered`, `mergedSlotsOf`, `planMergedDraws` (one draw per
  submesh with its own compact vertex list) and `invertAffine`. 18 tests, written first and run red against stubs (11
  failed, 7 passed vacuously); the `OtherSkeleton` fact was added later the same way (1 failed, then implemented).
- `[Bodies] merged body` (false) in `files/settings-default.cfg` and `Settings::bodies()`.
- `MWRender::MergedBodies` (`apps/openmw/mwrender/mergedbody.*`): reads `current`, the pack and the manifest once per
  session, when the first actor asks (nothing is read with the setting off). Each race and sex is checked once, when its
  first actor is built: `OMW::computeStockBodyFacts` (shared with the baker) recomputes the key from the data now and it
  must equal the manifest's, the payload must read, and the node must build. The node is one `StateGroup` per submesh:
  the pipeline options and textures captured from the stock part read through the engine's own reader with the part's
  own options (`Pipeline::Override::pipelineOptions`), adapted to the merged arrays (SKIN, no vertex colours, morphs or
  normal maps, counter-clockwise front face because the merger already reversed mirrored parts), and an `Anim::Skin`
  whose bones are only those the submesh's vertices use, linked by name like `handleSkin`. A template per race and sex
  is decorated per actor (`MWAnim::decorate`) and attached at the groin bone (the skin's matrices are relative to it).
- `Npc::attachMergedBody`, called by `updateParts` after the equipment: when the setting is on, the actor is in third
  person, not a werewolf, uses its race's default skeleton, its template exists and nothing it wears or reserves owns a
  merged slot, the stock parts of the merged slots are removed (and not added again) and the merged body is attached;
  otherwise nothing changes. It re-attaches on every `updateParts` call, because `clearControllers` drops what it linked.
  Head, hair, first person and werewolves keep their parts. **Equipment in stage d:** an actor that wears anything over a
  merged slot keeps the stock parts entirely (stage e hides regions instead).
- A failed race and sex logs one warning per reason and session (`Merged bodies: <race> <sex> draws the stock parts:
  <reason>`); a cache problem (no cache, stale key, refused payload) also shows one message box per session, which for a
  stale key says to re-run `openmw --bake-bodies` (answer 9). The details go to the Verbose log.
- `tools/mergedbody_check/check.sh` (stock or merged, one scripted scene in Balmora with `OPENEDEN_QUIET_WINDOW=1`): the
  player without clothes standing, walking, running, turning and attacking in third person, close-ups of the right
  wrist and knee from a static camera, then one NPC of every race and sex (stripped before it enters the world) in a
  row, with `OPENEDEN_FRAME_STATS_FILE`.

**Deviations from the spec, to be judged by toddhow.** (1) The runtime check recomputes the full key, so it reads and
hashes the part files and the skeleton once per race and sex (answer 8 asked to trust the pointer and compare records
and file sizes without re-hashing): the same function as the bake, at a cost that was not measured. (2) The cache is
read on the first actor, not at start. (3) No switch per region: stage d either draws the whole merged body or none.
(4) Beasts are merged: the bake builds them and their frames look right (H18 says beasts only if the structure
matches; stage a/c built all 20 bodies).

**Frames (Balmora hill, 800x600; work folder `I:\Projects\OpenEden-work\bodies\stage-d\runs`).** Sheets:
`sheets\stock.png` and `sheets\merged.png`; close-ups with stock on the left and merged on the right:
`closeups\pair006.png` (right wrist, hand-to-hand stance after the attack) and `closeups\pair007.png` (knee). Viewed:
the player (dark elf male) and the 20 NPCs of every race and sex, beasts included, draw a whole body in the merged
run, at rest, walking, running, turning and attacking; no gap and no new crack at the wrist or the knee; the
overlapping joints (thigh to knee, upper arm) look as in stock. The welded seam bend that stage c measured (up to 23
units at seeded poses) is not visible as a difference in these poses: they do not turn a wrist or an ankle hard against
its forearm or leg. **Pixel counts were not computed:** the scene runs on real time and the NPCs wander, so two runs are
never aligned to the pixel; a frozen-pose capture is needed for that number. After the bone change below the merged
frames were viewed again (row, run, wrist): unchanged.

**Cost (frame time, `OPENEDEN_FRAME_STATS_FILE`; medians and p99 in ms).** The first version gave every submesh's skin
all the body's bones, and 20 merged NPCs cost a frame median of 7.23 ms against 3.76 ms for stock (player alone: 3.67
against 3.52). With each skin limited to the bones its vertices use:

| Run | Player alone (6 to 22 s) | 20 NPCs in view (from 24 s) |
| --- | --- | --- |
| stock, run 1 | 3.52 / 5.28 | 3.76 / 5.38 |
| stock, run 2 | 3.51 / 5.34 | 3.74 / 5.33 |
| merged, all bones per skin | 3.67 / 5.39 | 7.23 / 9.03 |
| merged, run 1 | 3.52 / 5.33 | 3.77 / 5.50 |
| merged, run 2 | 3.52 / 5.38 | 3.74 / 5.35 |

So the merged body costs the same frame time as the stock parts here: no saving and no loss (recorded, not promised).
**Memory** (process private bytes and working set in MiB, one run each, sampled once a second): before the NPCs stock
983 / 726, merged 992 / 734; with the 20 NPCs stock 1023 / 765, merged 1039 / 783, so about +16 MiB with 20 races and
sexes built (the cache, the templates and the reads of the stock parts for their look). Device memory was not measured.

**Fallbacks.** The decision is unit tested for every reason. In the game, the merged run with an empty user folder (no
cache; `runs-nocache\merged`): one message box ("there is no merged body cache: run openmw --bake-bodies") in the first
frame and never again, one warning per race and sex, every body drawn with the stock parts, frame times as stock (3.51
/ 5.34 and 3.74 / 5.31). Not run in the game: a stale key, a bit-flipped or refused entry, an exception in the
builder, a werewolf, a creature, first person (the code paths are read, not run).

**Not done or not verified.** The byte-identical "off" frames and the recorded part lists against the commit before
(no executable of that commit exists, and the shared tree holds other agents' uncommitted work, so a build of it would
differ in other ways; off, the new code is one setting read and an empty `removePath` per `updateParts`, and one bool in
`updateNpcBase`); the numeric five-pose equivalence in the game's harness (stage c's `--check-bodies` compares the CPU
skin on the same data: non-weld vertices within 1.6e-5 at five poses; the GPU path is not compared numerically); the
`[Cells] load trace` counts and 50 NPCs (20 measured, by frame time and process memory instead); the allocation-counter
test; the role-based skin mark for subsurface scattering (risk 6: the captured look carries whatever the stock part's
options had; not checked with the `subsurface` debug view); the 32 Lua tests and the Linux gate. The "off" argument above
is reasoning from the code, not a comparison: the feasible comparison is a `git worktree` of `bdc6c1a229^` under
`I:\Projects\OpenEden-work` with its own full build, against this build with the setting off, on a frozen scene.

**Threads.** Everything new runs on the main thread: `Npc::attachMergedBody` from `updateParts`, and in it
`MergedBodies::getTemplate` (cache read, key, payload, `findLook`, the build) under `sMutex`; `report` uses the window
manager, main thread only. The skins are ordinary `Anim::Skin` controllers, updated where the stock parts' are.

**Tests and the editor.** `components-tests` (5,328 passed, the rest skipped or disabled as before) and `openmw-tests`
(859) pass on Windows. `eden-studio` was rebuilt; its unattended capture (`--open-cell Balmora --auto-screenshot 30
--auto-quit 40` with a copied config) loaded the defaults with `[Bodies]`, drew the cell and exited 0. A plain start
without a file stopped its extension host 6 to 16 ms after loading it and then stayed alive until killed (twice; the
path looks like `makeIPCServer` returning false, which logs nothing when `listen` fails; no other editor was running
the second time; not investigated, and not seen to depend on this change). Those plain starts read the real
`openmw.cfg` despite `--replace=config --config` and rewrote `eden-studio.log` in the real user folder.

### Review fixes of stage d, 2026-11-04 (commits `30dbca4893`, `d096b357ea`, `6572f560c2`, `9df44d50d4`, `6913d7049e`, `d435853899`)

A fresh review of stage d raised thirteen findings and an inherited bug. Work folder
`I:\Projects\OpenEden-work\bodies\stage-d2` (a copied config in `cfg`, a copied user folder with a fresh bake in `user`, runs in `runs*`, frame pairs in
`frames-d2`, stock on the left and merged on the right). Every game ran with `OPENEDEN_QUIET_WINDOW=1`.

**Tests written first.** `apps/components_tests/body/testmergedbodyreview.cpp`, 12 tests: 7 for
`planMergedDrawsByMaterial` (one draw per material in first-use order, the look from the first slot, every triangle
once, a shared vertex once per draw, deterministic, bad input gives nothing, fewer draws than the per-slot plan) and 5
for the quick keys (round trip, canonical text, refusals, size cap, empty list). Run red against stubs: 8 failed, 4
passed vacuously. All pass now; `components-tests` 5,379 passed.

**Per finding.**
1. *The stock look, MATERIAL included.* `findLook` marks the part as skin before reading it (`markSubsurfaceModel`, as
   `updateParts` does), adds `Mode::MATERIAL` when the captured group has a material descriptor (the reader adds it
   after the override ran, `nif.cpp` `createStateGroup`), and records the stock pipeline's defines; the merged pipeline
   must carry the same defines but for the adapted ones, or the body is refused with the define named. The check
   caught one at once: with `gbuffer` on, the merged draws had `GBUFFER_MOTION` and the rigid stock parts not
   (`Pipeline::Graphics` adds it to every skinned draw that writes the G-buffer); it is now an adapted define, so the
   merged body writes its bones' motion where the rigid stock parts wrote none. Frames with `hdr`, `gbuffer`, `pbr`,
   `sss`, `wetness` and `cover` on, the player and the 20 NPCs merged in each run (21 lines): rain (`rain-006` the
   player's wrist, `rain-008` the row), snow (`snow-007` the knee, `snow-008`), and the subsurface debug view
   (`gbuffer debug view = sss`: `sssdebug-006`, `sssdebug-008`); the three close-ups stacked in
   `closeups-rain-snow-sss.png`. Viewed: the merged skin shows the same subsurface signal as stock, stays dry in the
   rain (puddles on the ground) and bare in the snow (cover on the ground and the hair, as stock). **Correction:** the
   first set of these frames, before the finding 8 change below, had the player drawing the stock parts (it retired
   when first built dressed), so its close-ups were stock against stock; the frames named here are from the rerun, in
   which the log shows the player drawing the merged body. The define comparison is the test, in the game; it is not a
   unit test (it needs a pipeline).
2. *Compile failure.* `Npc::compile()` failing now also removes the merged body and clears its slots. Not run (no way
   to make a compile fail on demand); read only.
3. *Ownership and a normal exit.* The cache and templates live in `MergedBodies::Session`, which `OMW::Engine` holds
   after `mRenderManager` (so it goes before the resource system and the render engine's device). The scene now quits by
   itself (`core.quit()`) and `check.sh` fails unless the game exits with code 0 and leaves no crash dump: every run of
   this round (21 runs of `check.sh`, stock and merged) exited 0 with no dump.
4. *Draws.* One draw per material key (`planMergedDrawsByMaterial`). With `[Rendering] frame counters` and the player
   plus 20 NPCs in view: scene census 7,116 draws stock against 6,848 merged (about 13 fewer per actor; the player
   alone 6,562 against 6,550), triangles 524k against 523k, frame median 3.72 against 3.64 ms, p99 5.25 against 5.21
   ms (`runs-counters`, one run each). No allocation counter was added: the per-frame work is the ordinary `Anim::Skin`
   update; what the merged body allocates happens in `updateParts`.
5. *Skin transform.* The baker refuses a skinned shape whose transform is not the identity (one error per part)
   instead of baking it wrong. A rebake of the 20 bodies gave payloads byte-identical to the stage c bake (hashes in
   `bodies\bake3\payloads.sha`), so no stock body meets it.
6. *First-actor hitch.* The bake writes `quick.txt` (the key with file digests replaced by sizes); the game checks a
   race and sex with `OMW::computeStockBodyQuickFacts` (records, `VFS::Manager::getSize`, no part file read or hashed)
   and parses each skeleton once per session. Logged per template: key check 0.13 to 0.6 ms, template 2.6 to 4.1 ms;
   the first body of each skeleton (`base_anim.nif`, `base_animkna.nif`) 8.3 and 10.2 ms of key check, 13.8 ms in all
   (the skeleton parse). No "before" number: the old code did not time it. The look's reads still use a fresh
   `SharedObjects` each (they must, to capture), so there is not one `SharedObjects` per session.
7. *Textures.* The look's images are shared into the stock options' `SharedObjects` (`shareImages`). Device memory
   with 20 NPCs (`gpu_used_mib`, `VK_EXT_memory_budget`, MiB resolution): 177 stock, 177 merged; working set 765
   against 759 MiB.
8. *A stable decision.* At most one swap each way per actor: an actor first built dressed draws the stock parts and
   may take the merged body once undressed; an actor that has drawn the merged body and then puts equipment over a
   merged slot keeps the stock parts for the rest of its life (`Npc::mMergedRetired`, set only when `mMergedDrawn`).
   The first version retired every actor first built dressed, which kept the player (dressed at a game's start) on
   the stock parts for the whole run, silently; found before the report by checking the log, fixed in `d435853899`.
   Question for toddhow below.
9. *Template keys.* Keyed by race, sex and the context's read options; the player and the NPCs of the same race and
   sex got two templates in the log.
10. *Blended looks.* A look with alpha blending is refused (fail closed; no stock part is blended).
11. *Fallback logs.* Logged once each: a race the store lacks, every refusal reason per race and sex, an actor on
   another skeleton (Info, with both paths). One line per actor that draws the merged body, counted by `check.sh`. The
   default skeleton is compared as a normalised mesh path: my first version passed both sides through
   `correctMeshPath`, which always prefixes `meshes/`, and every beast was refused as "another skeleton"; the path is
   now prefixed only when it lacks it, and all 20 merge.
12. *Message box.* Localised (`OMWEngine:MergedBodiesRebake` in `files/data/l10n/OMWEngine/en.yaml`) and shown for
   no cache, no entry, a key that could not be computed, a stale key and a refused payload. Not seen in this round
   (no run with a broken cache).
13. *Check tool safety.* `check.sh` refuses a user data or work folder outside `I:/Projects/OpenEden-work` and a base
   or user folder under "My Games"; the bake, check and analysis runs draw stock bodies whatever the setting says
   (`MergedBodies::disableForThisRun`).

**The inherited bug (skin controllers lost after an equipment change): not reproduced.** Reading the code, `updateParts`
clears `mStaticControllers` and adds back only the parts whose priority is below 1, so a skinned stock part that
stays (the chest or the hands) should lose its `Anim::Skin` and freeze. A scripted check (`stage-d2\skinbug`, stock,
the player): fists raised; arms down; pants taken off through `types.Actor.setEquipment` (which goes through
`InventoryStore` to `Npc::updateEquipment`, the same path as the inventory); fists raised again. With the shirt on, the
hands (`stock-hands-before-after.png`), and with the shirt taken off first, the chest and shoulders
(`stock-chest-before-down-after.png`) follow the bones after the change exactly as before it. Nothing was changed. Why
the skins survive was not found. A hypothesis to check, not confirmed: the mwrender guide's frame order says
`Scene::update` "updates objects, then skins", so stock skins may be driven by the scene's own skin pass and not by
`mStaticControllers`. If so, the merged body's skins ride a different path from the stock skins (they are in
`mStaticControllers` and linked again on every `updateParts`); a reviewer should judge whether that matters.

**Not done or not verified.** `openmw-tests` (859 passed) and `eden-studio` built at the end, once another agent's
`lodclusters.cpp` compiled again; the editor was not started. clang-format 14 was run on the changed files in a
container and its changes taken (`6913d7049e` for the two files committed before the check). The Linux gate, the 32
Lua tests, a frozen-pose pixel comparison, the "off" comparison against the commit before, the message box in the
game and an allocation-counter test were not run.

**Questions for toddhow.** (1) The retire rule of finding 8 as built: at most one swap each way (stock until
undressed, merged, then stock for good once dressed again). The alternatives are no swap at all (an actor first built
dressed never merges, which in a real game is the player and almost every NPC) or swapping on every change. (2) Is an Info line per actor on another skeleton acceptable with the setting on, or should it be Verbose?
(3) The first body of each skeleton costs about 10 ms on the main thread for the skeleton parse; reading it on the
preload thread, or taking the engine's own skeleton, would remove it. (4) The merged body writes bone motion to the
G-buffer where the rigid stock parts wrote none (finding 1): fine, or should the merged draws be kept out of it?

### Stage e, 2026-11-04: equipment and regions (commits `87b191d445`, `63802e1b86`, `ffcaed3e7a`)

**What was built.**
- `components/body/mergedruntime.*` (pure): `hiddenMergedSlots(mergedSlots, partslots, priorities)`, the stock parts'
  own rule read from the same arrays `Npc::updateParts` fills (`mPartslots`, `mPartPriorities`): a merged slot is
  hidden when an inventory group (0 or more) holds it with a priority of 1 or more, which is exactly when the stock
  loop does not add the slot's body part (a body part kept from the last update has group -1 and is not hidden); a slot
  number outside the arrays counts as covered. Each `MergedDraw` now records its slot runs (`SlotRun`: a draw's indices
  are its submeshes' triangles in submesh order, and submeshes are ordered by slot, so one slot is one contiguous run
  per draw), and `visibleIndexRanges(runs, hidden)` gives the index ranges that remain, touching runs merged.
  15 tests (`testmergedregions.cpp`), written first: 13 of them failed against stubs. The
  slot rule is checked against a reference written from `npc.cpp` for every single-slot state (empty, the body part,
  inventory groups 0 to 18 with priorities 1 to 25), 20,000 seeded random states of all 27 slots, and the robe's
  reservations by name.
- `MWRender::MergedBodies::applyHiddenRegions` (`mergedbody.*`): the template keeps every draw whole; an actor's copy
  (made by `MWAnim::decorate`, whose state groups are its own because the bone matrices are cloned) gets, for each draw
  that has a hidden run, one `vsg::VertexIndexDraw` per remaining range (none when the whole draw is hidden) sharing the
  template's vertex and index buffer infos, so nothing is uploaded again. Everything is checked before anything
  changes; a copy that shares a state group with the template, or a structure that is not the template's, is refused.
  `TemplateResult::mDrawRuns` carries each draw's runs, in the order of the root's children.
- `Npc::attachMergedBody`: replaces stage d's "anything worn over a merged slot keeps the stock parts" with the hidden
  regions, applied on every `updateParts` (the copy is made again there anyway). Nothing runs per frame. The one-swap
  rule of stage d (finding 8) stays only as the fallback when the regions cannot be applied: then the stock parts are
  drawn, a warning is logged and an actor that has drawn the merged body retires.
- `tools/mergedbody_check`: `SCENE=equipment` (`mod/scripts/mbequip_*.lua`, `mbequip.omwscripts`) and `TIMEOUT`. The
  scene: the player gets one piece of every kind (Morrowind's common clothes and iron armour by record id, else the
  first record of the type; `iron_boots` does not exist, so the boots are `adamantium boots`); a row of eight NPCs
  behind the player in two lines, each wearing one combination (an NPC never puts on what Lua's `moveInto` gives it,
  `objectbindings.cpp` passes no auto-equip, so a small local script equips it and drops the AI packages); then the
  player wears twelve combinations in turn (naked, shirt, pants, cuirass and greaves, robe, skirt, cloth gloves and
  shoes, gauntlets and boots, pauldrons, helmet, the full set, naked again), each photographed from a static camera in
  front and close to the right wrist, knee and ankle; then 200 cycles of the full set on and off (one change every
  second frame), with the frame counters.

**Frames (Balmora hill, 1280x720, `OPENEDEN_QUIET_WINDOW=1`; work folder `I:\Projects\OpenEden-work\bodies\stage-e`,
a copy of `lodtest\config\openmw.cfg` with an empty `settings.cfg` in `cfg`, the stage d2 bake copied into `user`).**
Stock above merged, four views per combination: `pairs\pair-0.png` (naked, shirt) to `pair-5.png` (full, naked
again); the row `pairs\row4.png` and `row4-crop.png`; full-size crops `pairs\crop-*.png`; stock, merged and the
pixels that differ by more than 4 levels `diff\diff-*.png`; per-frame shares in `diff\fractions.txt`. Viewed:
- *Every combination* draws the same visible body as stock: shirt, pants, cuirass and greaves, robe, skirt (the legs
  below it), cloth gloves and shoes, gauntlets and boots, pauldrons, the helmet (hair hidden, as stock), the full set
  and naked. No skin pokes through any garment, no gap at a garment's edge (wrist under gloves and gauntlets, the knee
  at the top of the boots and below the greaves, the ankle above the shoes), no new z-fighting.
- *The row* (eight races, one combination each, merged in the log): the same as stock, garment for garment.
- *Pixels:* 43 of the 49 frames differ from stock in under 0.25 percent of their pixels (NPCs moving in the
  background, the idle animation's phase). The others: the wrist close-ups of pants (3.99 percent), pauldrons and
  helmet (2.62 each) and naked-again (6.98), and the front views of pants (1.05) and naked-again (1.20). In the
  pauldrons and helmet frames the difference is the hand, a few units apart (the idle phase of two runs). In the four
  pants and naked-again frames it is **the stock run that is wrong**: the stock chest part's lower edge breaks through
  the pants and the loincloth in jagged pieces, and from the front the chest is pulled out of shape
  (`diff\hip-010.png`, `hip-046.png`, `chest-045.png`), while the merged body is clean. It appears only after
  equipment changes and looks like the inherited "skin controllers lost after an equipment change" of the stage d
  review (a hypothesis, not traced: the chest kept from an earlier `updateParts` loses its `Anim::Skin` to
  `clearControllers` and stays in an old pose). It happens with the setting off, so it is not this stage's; the merged
  body does not show it because it is linked again on every `updateParts`. Reported, not fixed.

**200 equip and unequip cycles (frame counters, the census sampled every 30 frames).** Before (the 3 s before the first
change) and after (from 0.5 s after the last): merged 775 and 774 MiB resident, 185 and 185 MiB of device memory,
6,635 and 6,635 draws in the census; stock 749 and 745 MiB, 185 and 185 MiB, 6,793 and 6,793. During the cycles the
resident memory never went above the value before. So no node and no memory stays behind, within the counters'
resolution (1 MiB, about 5 KB per cycle). An earlier run of the same scene had one step of 7 MiB at the first change
and then stayed flat to the end; it did not repeat. There is no allocation counter in the game; the census is the
node count. With every actor in view dressed in some way, the merged scene still has 158 fewer draws in the census than
stock (the split draws do not outnumber the stock parts they replace), and the frame median before the cycles is 3.80
ms against 3.72 ms for stock (one run each). (Second review, finding 3: these runs had no load trace. With it on, its
own list of scopes, kept in memory until exit, adds 5 to 11 MiB over the cycles, stock and merged alike; without it,
on the final build, stock 747 to 746 MiB and merged 756 to 758 MiB, one step at the first changes.)

**An equipment change costs more with the merged body.** Toggling the full set every second frame raised the frame
median from 3.80 to 5.43 ms (p90 8.02 ms) with the merged body and from 3.72 to 4.57 ms (p90 5.26 ms) with stock: about
1.6 ms more main-thread time per change, because the merged body is decorated, linked and compiled again on every
`updateParts` while the stock loop keeps the body parts it has. Not a per-frame cost; keeping the actor's copy when
its hidden set did not change would remove most of it (not built).

**Fallback.** Never taken in these runs (no "could not be left out" warning in any log). Not exercised in the game:
there is no way to make the copy share a state group on demand; the refusal is read, not run. While an actor that never
drew the merged body falls back, the warning repeats on every `updateParts` (no retirement then).

**Strict level kept:** nothing legacy is merged or morphed beyond stage d: stage e only leaves index ranges out of the
draws. The B3 surface morph hooks are not built; the region ranges they would need are `MergedDraw::mRuns` and
`TemplateResult::mDrawRuns`, the weld groups are in the payload.

**Tests and gates.** `components-tests`: the 442 body, merged and skeleton tests pass; `openmw-tests` 868 pass.
clang-format 14 (in a container) leaves the changed C++ files as they are. Not run: the Linux gate, the 32 Lua tests,
a fresh reviewer, a frozen-pose
pixel comparison, the "off" comparison against the commit before (off, `attachMergedBody` still returns after its
first setting read, before any new code).

**Questions for toddhow.** (1) The fallback keeps stage d's one-swap rule; with regions it should never be taken. Keep
it, or drop the merged body for that actor with no swap at all? (2) The stock chest breaking through after an equipment
change (frames above) is an inherited bug the merged body does not have; a task of its own? (3) An equipment change
costs about 1.6 ms more with the merged body; worth keeping the copy when the hidden set is unchanged now, or later?

### Review fixes of stage e, 2026-11-04 (commits `7dea95782b`, `6151cceb4b`, `240a80935c`, `f87282e97e`; the fresh review's findings 1 to 6)

**Finding 1, the circular test.** The stage e reference (`referenceHidden` in `testmergedregions.cpp`) restated the
rule `hiddenMergedSlots` implements. The part slot bookkeeping of `Npc::updateParts` now lives in
`MWRender::PartSlots` (`apps/openmw/mwrender/partslots.*`), moved out of `Npc` unchanged: `remove`, `reserve`,
`removeGroup`, `take` (the decision of `addOrReplaceIndividualPart`), the equipment pass `wear` (the inherited slot
list, `((base + 1) << 1)` plus one for armour, so a robe is 24 and a skirt 8, the robe's eleven and the skirt's three
reservations, a part without a body part reserving its slot, the helmet taking the hair), `addBodyParts` (the race's
parts at group -1 and priority 1) and `releaseBodyParts` (the merged body taking slots from kept body parts). `Npc`
does the node work through `PartSlots::Nodes::detachPart` and the attach callbacks; the old `removePartGroup` and
`addPartGroup` are gone, `attachPart` is the second half of `addOrReplaceIndividualPart`. 13 tests
(`apps/openmw_tests/mwrender/testpartslots.cpp`, all 13 red against stubs first) run that code over whole update
sequences, a stock actor and a merged one side by side, and check after every update that the merged body hides
exactly the merged slots where the stock actor draws no skin, draws no stock skin there itself and draws the same
items: named sequences (a robe over a cuirass, skirt and robe in both orders, an armour skirt, part-less sleeves and
gloves, held items and the helmet, body parts kept from a stock update then released) and 3,000 seeded random
sequences of 5 outfits from a catalogue of 19 items. Research note 0032 section 5 states the rule in words and has no
slot table; the reservation lists are the ones in `npc.cpp` (pinned by two tests). The 15 tests of
`testmergedregions.cpp` are unchanged (deleting tests needs toddhow's approval); they remain as a restatement.
`applyHiddenRegions` has 10 tests on plain VSG nodes (`testmergedbodyregions.cpp`, suite `MergedBodyCopyRegions`, 6
red before the change below), among them a body of 70,000 vertices with 32-bit indices whose parts must keep
`VK_INDEX_TYPE_UINT32`: with `assignIndices` commented out exactly that test fails (run once, then restored).

**Finding 2, the hitch.** `attachMergedBody` keeps the actor's copy (`mMergedNode`) while its template is the same and
applies the regions again only when the hidden set changed; `applyHiddenRegions` now works on a copy it changed
before (a draw already as wanted is left alone; the draws it takes out come back in `RegionChange::mRemoved` and are
detached through `CompileContext::detach`). The copy's controllers are recorded at link time (`mMergedControllers`)
and added again by `relinkMergedBody` on every `updateParts`; `forgetMergedBody` ends the copy on a rebuild, a failed
compile and every stock-parts path. Measured with the new scene `tools/mergedbody_check SCENE=torches` (Pelagiad;
twenty dressed NPCs of every race and sex with torches; the hour set from noon to night and back four times, so 26
NPCs in range, the twenty and six of the town, take out or put away their torch in one frame; trace scopes
`npc.updateequipment`, `npc.compile` around `compile()`, `npc.mergedbody.detach`, `decorate`, `regions`, `link`,
`keep`), means of the 9 change frames (work folder `I:\Projects\OpenEden-work\bodies\stage-e-review`, `runs-before`
and `runs-after`, tables `equipment-frames.txt`):

| | frame | `npc.updateequipment` | `npc.compile` | decorate | regions | link | keep |
| --- | --- | --- | --- | --- | --- | --- | --- |
| merged, before | 18.27 ms | 12.39 ms | 6.47 ms | 0.98 ms | 0.08 ms | 1.31 ms | - |
| merged, after | 17.81 ms | 9.96 ms | 5.26 ms | 0 | 0 | 0 | 0.04 ms |
| stock, before / after | 11.40 / 11.88 ms | 6.20 / 6.34 ms | 3.10 / 2.92 ms | | | | |

The merged body's own work in a torch frame went from 2.4 ms to 0.04 ms; `npc.updateequipment` over stock from 6.2 to
3.6 ms. What remains is VSG's compile of the actor (5.26 against 2.92 ms, `compile.vsg`, with the copy unchanged),
not traced further; the whole frame is still about 6 ms above stock (the other 2.3 ms are outside the equipment
scopes, as before). The armour frame (26 NPCs putting on a full iron set at once; part reads cold and noisy): merged
201 ms before, 148 ms after (compile 145 to 53 ms, decorate and link 14.4 ms to 0), stock 52 and 68 ms. The stage e
equipment scene's 200 cycles of the full set (one change every second frame): 2.06 ms of `npc.updateequipment` per
change after, 3.23 ms before, 1.86 and 1.81 ms for stock; frame median 5.72 ms after, 6.37 before (stock 4.97 and
5.29). Resident memory over the 200 cycles rises 7 MiB for merged before and after and 4 to 6 MiB for stock, device
memory stays at 176 MiB and the census at 6,634 draws (merged) and 6,792 (stock). (Second review, findings 3 and 5: the
memory rise is the load trace's own list, not the merged body; the torch numbers are one run, whose excess of 3.62 ms
was 2.34 ms of compile and 1.28 ms outside every merged scope, not seen again in two later runs; the 0.04 ms is `keep`,
decorate and link are 0 in a torch frame.)

**Finding 3, fully hidden draws.** `RegionChange` names each draw's bone matrices (the copy's, from its
`BONE_BINDING` descriptor) and whether any of it is drawn; `relinkMergedBody` leaves out the skin of a draw nothing
of is drawn. Counted with two value scopes (`npc.mergedbody.matrices` and `.drawn`): an undressed actor computes 52
or 53 bone matrices per frame for its merged body; an NPC of the torch scene in a full iron set computes 1 to 7 after
(53 before). In the 4 s quiet window with every NPC armoured, the scene update (`render.scene.update`, where the skins
run) took 1.21 ms with merged bodies against 0.59 ms for stock before, 0.75 against 0.69 ms after; frame medians 5.04
against 4.42 ms before, 5.29 against 4.84 ms after (the after runs were slower across the board: the machine is
shared). (Second review, finding 7: "computes 1 to 7" is the summed size of the drawn skins' bone matrix arrays when
the controllers are linked again, not a count taken per frame, and twice the bones with the G-buffer.)

**Finding 4.** A refused copy is reported once per race and sex (`MergedBodies::reportRegionsRefused`), no longer on
every `updateParts`. Still not reachable in the game on demand; the refusal paths are unit-tested.

**Finding 5, motion.** With the copy kept, its skins keep their matrices across an equipment change, so the first
frame after it has the motion of the frame before. A draw that is drawn again after equipment hid it had its skin
left out (finding 3), so its previous matrices are from when it was hidden; `Anim::Skin::forgetPreviousMatrices`
(new, one line in `components/animation/skin.hpp`) makes its next apply write this frame's matrices to both halves,
as a new skin's first apply does, and `Npc::applyMergedRegions` calls it for such a draw (2 tests on a one-bone
skeleton, `apps/components_tests/mwanimation/testskinprevious.cpp`, the new one red against a stub first). A new copy
(the first build, a rebuild in `updateNpcBase`, which also replaces the skeleton) still starts with one frame without
motion, as every new part does. Not checked: a frame of the G-buffer's motion output. (Second review, finding 1:
checked with `SCENE=motion`, against an executable without the restart.)

**Finding 6, docs.** `apps/openmw/mwrender/CLAUDE.md` says why the parts may share the template's buffer infos while
LOD cluster pitfall 3 forbids sharing between draws (the parts are ranges of one draw's own index list over its own
vertices: same buffers, offsets and index type, compiled once; only `firstIndex` and `indexCount` differ). The
duplicated `tools/mergedbody_check` bullet of the stage e log and the control characters of the analysis path in
stage a's log are fixed. Merged actors' stock head and hair had the face controller bug of task 0074 too (no blinking
or talking after an equipment change); 0074's fix covers them.

**Frames (viewed; work folder `I:\Projects\OpenEden-work\bodies\stage-e-review`, `compare` and `compare-final`).**
Torch scene with the final build, stock over merged (`compare-final\torch-*-stock-over-merged.png`): the dressed row
by day (beasts in the front line: an Argonian man in shirt and pants, an Argonian woman in a robe, a Khajiit man in
cuirass and greaves, a Khajiit woman in shirt and skirt), the row at night with every torch out, the row in full iron
armour, and the paper doll (`doll-stock-merged.png`; the preview draws the merged body, one "player draws the merged
body" line per preview): garment for garment the same, no skin through armour or robes, no gap at the beasts'
greaves; the doll crop differs by at most 3 levels. Equipment scene: the front view of all twelve combinations,
before over after, for stock (`compare\eq-stock-front.png`: the default game looks the same by eye after the move to
`PartSlots`; the second review proved the bookkeeping identical by trace, finding 2) and for merged
(`compare\eq-merged-front.png`, the body whole again after the full set), the wrist, knee and ankle close-ups
of pants, gloves and shoes, gauntlets and boots and naked-again, stock over merged
(`compare\eq-after-closeups-stock-over-merged.png`), and the twelve front views of the final build, stock over merged
(`compare-final\eq-front-stock-over-merged.png`): the same, no gap. Pixel shares between runs are large in these
scenes (5 to 60 percent: the camera follows where the player settled, the weather change is still moving, torches
flicker), so the comparison is by eye. A third torch run with the final build (`runs-final`): 7.23 ms of
`npc.updateequipment` per change frame for merged against 5.65 ms for stock, decorate and link 0, keep 0.03 ms.
With `[Rendering] hdr` and `gbuffer` on (the doubled bone matrix arrays and the restart path), the equipment scene
runs to the end, 32 actors draw the merged body, and six combinations viewed against the run without the G-buffer
(`compare-final\eq-merged-plain-over-gbuffer.png`) show the same body; the motion output itself was not looked at
(the second review did, finding 1).

**Tests and gates.** `openmw-tests` 901 pass (23 new), `components-tests` 5,416 pass (2 new; the fuzz test left out).
clang-format 14 (throwaway container) applied to the changed C++ files. Not run: the Linux gate, the 32 Lua tests, a
fresh reviewer of these fixes. Open: the 2.3 ms of VSG compile per 26-actor torch frame that the merged body still
costs over stock; the 15 restating tests of `testmergedregions.cpp` (removing them needs toddhow's word).

### Second review of stage e, 2026-11-04 (commits `2fad796191`, `97413f8d79`, `9ed9d245a4`; findings 1 to 11 of `I:\Projects\OpenEden-work\bodies\stage-e-review\REVIEW2.md`)

The review found no code defect: test gaps, and claims in these notes stronger than the evidence. Work folder
`I:\Projects\OpenEden-work\bodies\stage-e-review2`. Every run below used a copy of the build (`exe-new`, made with the
build lock held by `variant_build.py`), so other sessions' rebuilds of `build-jolt` did not change what was measured;
the two variant executables were linked there from `build-jolt`'s objects plus the variant's own translation units,
without touching the source tree.

**Finding 1, the restart was unverified.** The decision is now pure and tested: `Body::drawsDrawnAgain` (hidden before,
drawn now; a draw the earlier list does not have is new, not drawn again) and `Body::linksOfDraws` (the copy's links
whose target is such a draw's bone matrices, matched by identity, never by position), 8 tests (`MergedBodyRestart` in
`apps/components_tests/body/testmergedregions.cpp`), 6 red against stubs that return nothing (the other 2 expect
nothing; `red-components.log`). `Npc::applyMergedRegions` uses them and records two value scopes per region change,
`npc.mergedbody.drawnagain` (draws drawn again) and `npc.mergedbody.restarted` (skins restarted), and logs a warning
once when they differ (every draw has its own bone matrices and one skin). The new scene `tools/mergedbody_check
SCENE=motion` (hdr, gbuffer, `gbuffer debug view = velocity`, a fixed frame time of 1/60 s and 3 frames a second, so
that each screenshot is written before the next is asked for: asked for in consecutive frames at full speed they all
took one file name): the player puts on the full iron set with the arms down, raises the fists (hand-to-hand) and with
the fists up takes everything off; screenshots of the frame before, the change frame and the four after. Run with
`exe-new` and with `exe-norestart` (today's code with the `forgetPreviousMatrices` call left out: a negative control,
never committed). Viewed (`compare\motion-final-over-norestart.png`, `motion-crop-final-over-norestart.png`): without
the restart the change frame shows the whole arms-down to fists-up swing in saturated colour on the arms, chest, legs
and feet; with it the body is the grey of no motion in that frame; the four frames after show only the head's faint idle
motion in both. In both runs 39 region changes were traced, the 5 with draws drawn again (one frame, the player's and
the paper doll's copies) counted 1, 3, 2, 2 and 3 draws (all in the change frame; the player has two copies, in the
scene and in the inventory preview) and restarted exactly as many skins; no warning. Not isolated: the motion of the
draws that stay drawn across a change (they are not told apart from the restarted ones in the frame).

**Finding 2, "off means unchanged" for the move to `PartSlots`.** `OPENEDEN_PARTSLOTS_TRACE=1` logs, for every NPC, each
part node attached (with its model) and detached (with whether a node was there), and after each `updateParts` the group
and priority of all 27 slots. `exe-old` is today's `npc.cpp` and `npc.hpp` with the bookkeeping of `6151cceb4b^` put
back verbatim (`mPartslots`, `mPartPriorities`, `removeIndividualPart`, `reserveIndividualPart`, `removePartGroup`,
`addOrReplaceIndividualPart`, `addPartGroup`, the loops of `updateParts`; script `variant-old\make_variant.py`, its diff
against today's code `variant-old\npc.cpp.diff` and `npc.hpp.diff`) and the same trace lines at the same places; the 8
translation units that include `npc.hpp` were compiled from a mirror of `apps/openmw`. That isolates the move and cost 8
translation units instead of a whole build of the commit before. With `[Bodies] merged body` off, the equipment scene
(31 actors, 181,704 lines) and the torch scene (30 actors, 19,170 lines) give identical lines for every actor: `exe-new`
against `exe-old`, against a second `exe-new` run, and `exe-old` against a second `exe-old` run (`compare_slots.py`,
`slots-compare.txt`). Actors are matched by record id and order of appearance: objects a script creates get other
reference numbers in two runs of the same executable. Not covered: first person and head-only views (none in the
scenes), werewolves, a run with `pbr` (where the skin mark acts). Frames of the final build, stock over merged, viewed
(`compare\eq-front-stock-over-merged.png`, `eq-closeups-crop.png`, `torch-stock-over-merged.png`): the twelve
combinations, the wrist, knee and ankle close-ups of pants, gloves and shoes, gauntlets and boots and naked again, the
torch row by day, at night, armoured and the paper doll: garment for garment the same, no gap, no skin through a
garment.

**Finding 3, the memory claims.** Both were right about their own runs. Stage e's runs had no load trace: resident
memory 775 to 774 MiB merged, 749 to 745 stock (recomputed from `stats.txt` by the scene's own clock, `memory.py`). The
review's runs had `[Cells] load trace` on, which keeps every scope in memory until exit (40 bytes each,
`Misc::LoadTrace::record`): 206,000 (stock) and 227,000 (merged) scopes from the start of "before" to the end of
"after" in this review's first pair of runs (7.9 and 8.7 MiB), and resident memory rose steadily through the cycles, 5
to 7 MiB for stock and 7 to 11 MiB for merged in five pairs of runs (merged records more scopes; the rise is not
exactly the list's size, which also grows in steps when the vector moves). Without the trace, on the final build: stock 747 to 746 MiB, merged 756 to 758 MiB,
one step of 2 MiB within the first 100 frames of the cycles and flat for the 700 after (an allocator high-water mark,
not growth per cycle); device memory and the census draws back to their values in every run.

**Finding 4, the worst case.** Added to the changelog row and the render guide's cost bullet: 26 NPCs putting on a full
iron set in one frame, merged 148 ms against stock 68 ms in the review's run. The second review's runs show it is noise
of a factor of two either way (cold part reads and VSG's compile): 107 against 47 ms (the review's final run), 48
against 77 and 79 against 45 ms (this review's two runs); the merged body's own scopes in that frame are 1.2 to 1.4 ms
(`decide` 0.42 to 0.50, `regions` 0.70 to 0.79, `keep` 0.03, `relink` 0.09 to 0.12).

**Finding 5, the torch frames.** `npc.updateequipment` per torch frame (mean of the 9 frames with 26 changes; armour
frame apart), merged against stock: 9.96 against 6.34 ms and 7.23 against 5.65 ms in the review's runs, 5.67 against
6.18 and 6.17 against 6.10 ms in this review's (`torchframes.py`). In the review's first run the excess of 3.62 ms was
2.34 ms of compile (5.26 against 2.92) and 1.28 ms outside every merged scope. The new scopes split `updateParts`:
`npc.partnodes` 2.83 ms merged against 2.77 to 2.97 stock, `npc.relink` 0.02 against 0.01, the merged body's own
(`decide`, `regions`, `keep`, `mergedbody.relink`) 0.10 to 0.11 ms; so the 1.28 ms of that run is not in the merged
body's work and did not come back in two runs. "decorate and link were 2.29 ms; now 0.04 ms" mixed scopes: decorate and
link are now 0 in a torch frame, 0.04 ms was `keep`.

**Finding 6, the template's own draw.** `applyHiddenRegions` now gives a draw that nothing hides the template's own draw
again, also after it was hidden (it made a new draw of the whole range); a part of the whole range made earlier is no
longer "as wanted". New test `the_template_draw_comes_back_when_nothing_is_hidden_again` (which draw object is there
after each call, and the two draws handed back), red against the code before (`red-openmw.log`); the 10 tests before are
unchanged and pass.

**Finding 7, the bone matrix counts.** "1 to 7 bone matrices per frame instead of 53" is the summed size of the drawn
skins' bone matrix arrays when the controllers are linked again (`relinkMergedBody`), not a count taken per frame, and
twice the bones with the G-buffer; the texts say so now. "The first frame after a change has the right motion" holds for
the restarted draws by the motion frames; for the draws that stay drawn it is reasoning (marked not isolated).

**Finding 8, the test suite's scope.** `components/body/CLAUDE.md` says what `testpartslots.cpp` proves (merged equals
stock given `PartSlots`, the oracle being what the stock actor draws) and what it does not (that `PartSlots` equals the
code before it, or that `updateParts` calls it in the order the test restates). New tests: anything worn in the helmet
slot takes the hair, and nothing else does; all 124,416 single outfits of the catalogue (per inventory slot nothing or
one of its 19 items), each on a new actor and on one drawn stock first, hide as stock (about 1 s). Both passed against
the code, so their red run was a deliberate mutation of `partslots.cpp` (the helmet's `remove(ESM::PRT_Hair)` left out,
and `releaseBodyParts` taking priority 2 for 1), run once and restored (`red-mutation.log`). This ticks stage e's "all
combinations" for single outfits of the catalogue; sequences stay sampled (3,000 random ones), and the catalogue is
synthetic, not every item of the game data.

**Finding 9, lifetimes.** The comments of `Session::~Session` and `mergedbody.hpp` and the render guide say who holds
what: the Session holds the cache and the templates; an actor holds its template and copy until its `Npc` goes with
RenderManager's `Scene`, after the Session and still before the render engine and its device (`vsgengine.hpp` declares
`mRenderEngine` before `mRenderManager` and `mMergedBodies`). Written into the guide: new parts need the actor's
`compile()`; every caller of `updateParts` compiles (`updateEquipment`, `rebuild`, `Scene`'s NPC insert,
`Preview::compile`).

**Finding 10, the skin mark.** `PartSlots::Nodes::beforeWornPart` is called by `wear` just before a worn part with a
body part asks for its slot, taken or not, after the slot's group went and the helmet took the hair; `Npc` marks a skin
part's model there, as `Npc::addPartGroup` did before `PartSlots` (`wornParts` no longer marks). Test
`a_worn_part_is_announced_just_before_it_asks_for_its_slot`, red against the stub. A no-op without `pbr` either way.

**Finding 11, allocations.** The equipment pass keeps one list of worn parts in the `Npc` (`mWornParts`, cleared per
item) and builds one `PartSlots::Attach` per update capturing two pointers (16 bytes, inside libstdc++'s small buffer),
so an equipment change no longer allocates a vector per worn item and a `std::function` per slot. Its trace is part of
finding 2's identical lines.

**Tests and gates.** `openmw-tests` 927 pass, `components-tests` 5,468 pass on Windows (the fuzz tests left out).
clang-format 14 (container) on the changed lines. Not run: the Linux gate, the 32 Lua tests, a fresh reviewer of these
fixes, the motion scene on another GPU.

### Stage f, design (2026-11-04, written before any code; full loop)

**The verify items, settled by reading the code at `504ff9c445`.**
1. *How editor modules register and draw.* A module is a shared library exporting `openeden_editor_module_descriptor`
   (`sdk/include/openeden/editor_module/module.hpp`, `OPENEDEN_EDITOR_MODULE`); Studio loads the core list
   (`CSMModule::coreModules()`, `apps/opencs/model/module/modulehost.cpp`) from `editor-modules/` beside the program. A
   module registers data only (`registrar.hpp`): workspaces, panels and inspector sections are `PropertySource`s that
   Studio turns into Qt forms (`CSVModule::ModuleUi::makePropertyForm`, `apps/opencs/view/module/moduleui.cpp`); gizmos
   submit **lines, points and labels only** (`OverlaySink`, `types.hpp`) in metres, which `ModuleUi::overlayLines`
   (its `LineSink::convert`) turns into Morrowind units, and only `CSVRender::WorldspaceWidget::updateModuleOverlay`
   draws them (cell views); labels are not drawn. A form's rows are made once (`PropertyForm::build`); after an edit
   only the values are read again (`refresh`), and a panel builds a new form only when its source's ADDRESS changes
   (`StudioShell`'s `ModulePanel::rebuild`); an inspector section only when the active asset changes
   (`ContextInspector::rebuildModuleSections`, `generation`). Module assets are opened from the Asset Browser's
   "Module assets" group (`ModuleUi::findAssets` over the data folders and `OPENEDEN_CS_ASSETDIR`, `CS::Editor::
   startModules`) by the longest registered file name suffix (`ModuleRegistry::findAssetType`). The API version is
   pinned at 1.0 by two existing tests (`testmodulehost.cpp` line 175, `testheader_types.cpp` lines 21 and 22), so
   this stage grows nothing in the API.
2. *What the mesh viewer draws.* `CSVRender::MeshScene::load` (`apps/opencs/view/render/meshscene.cpp`) loads only a
   VFS model through `MWAnim::createObject` (NIF and DAE, `createNodeOptions`); the view modes other than Textured
   draw a copy of the geometry taken at load, unskinned (`MeshCollector`, `rebuildSolid`, `rebuildOverlay`;
   `apps/opencs/CLAUDE.md`, "The mesh viewer"). It cannot load a `bodymesh` payload, it does not draw module gizmos,
   and nothing gives it colours from outside. `Render::DebugGeometry` can draw lines and triangles
   (`components/render/debuggeometry.hpp`), but the API delivers lines only. **So the skinned rest-pose question is
   answered "no": the fallback is taken, the bind-pose static mesh** (the payload's own positions, which are the
   bind pose), drawn as lines coloured by region.
3. *What `components/body` exposes.* `readPackageFolder`, `readPackageFile`, `PackageFiles` (`bodypack.hpp`);
   `readBodyPackage`, `LoadError` with file, line and pointer (`bodyjson.hpp`); `validateBodyPackage` with
   `ValidateOptions { mMeshSource, mReadFile }` and `Finding { code, severity, path, message }` (`bodyvalidate.hpp`);
   `RegionMap` (triangle ranges per region id) and `WeldGroups` (`bodypackage.hpp`); `readBodyMesh` (the canonical
   payload reader, `bodymesh.hpp`); `loadCurrentCache(folder)` (reads `current`, refuses an unsafe or non
   `merged-<digest>.edenpak` name), `readManifest`, `CacheEntry` (name, race, sex, view, key), `CacheFailure`
   (`bodycache.hpp`). **A merged cache holds payloads and `manifest.txt` only** (no `body.json`, no region map, no
   skeleton binding, no weld tolerance, no units): stage c built the payload, not the package the design 7A.3 named.
   The payload is in the game's units (the skeleton's bind space; stage a's bounds, `analysis/bodies.json`).
   `edenbodyvalidate` (`runBodyValidateTool`, `bodytool.cpp`) passes `mReadFile` and no mesh source.
4. *What the stage a report holds.* `openmw --analyse-bodies` writes `<user data or --bake-out>/analysis/bodies.json`
   (`BodyNif::toJson`, `components/bodynif/analysisreport.cpp`): `"format": "openeden.bodyanalysis"`, `"version": 1`,
   `units[]` with `race`, `skeleton`, `female`, `beast`, `playable`, `totals` (vertices, triangles, bones, materials,
   files, fileBytes, skinnedParts, rigidParts), `slots[]` and `seams[]` (label, slotA, slotB, touches, `distance`
   {count, min, median, p95, max}, `withinLadder` (6 counts for 0, 1e-4, 1e-3, 1e-2, 0.1, 0.5 units), `signedGap`,
   overlapPairs, gapPairs, meetingPairs, `weightDisagreement`, ...) and `heads[]`. There is no reader; the cache does
   not point at it.

**What the module is.** `editor_modules/advancedbodies_editor/`, built as `advancedbodies_editor` into
`editor-modules/` and added to the core list after `creativemotion_editor`. It links the Editor Module API headers,
`eden_body` and nlohmann/json; no Qt, no Studio type, no engine type (the one engine fact is the unit factor, below).
It only reads: nothing is written, `save` refuses, `isDirty` is false.

**The model** (`bodyview.hpp`, namespace `advancedbodies_editor`, pure, no clock, no threads; tested in
`eden-studio-tests`):
- `openBodySource(path)` dispatches: a file named `current` opens the merged cache of its folder through `current`
  (`Body::loadCurrentCache`); a `*.edenpak` opens the pack (`Body::readPackageFile`) as a packed package when it holds
  `body.json`, as a merged cache when it holds `manifest.txt`, else refuses; a file named `body.json` (exactly,
  ignoring ASCII case: `somebody.json` refuses) opens its folder as a package (`Body::readPackageFolder`). Anything else
  refuses. *(Review fixes:)* a pack is first classified from its index alone (`EdenArchive::Archive` entry names, no
  entry decoded), so a pack holding neither `body.json` nor `manifest.txt` is refused before any of it is read; a
  package's files are dropped once its body is worked out. `openPackageFolder`,
  `openPackageFile`, `openMergedCache` are the three ways by name. A failure gives one located message and no view.
- **A package** (folder or pack): `body.json` is read with `Body::readBodyPackage`; a load error is the open's error
  (its `toString()`: file, line, pointer). The report is `Body::validateBodyPackage(package, options)` with exactly the
  tool's `mReadFile` (files of the package by safe path) plus `PayloadMeshSource`: for a mesh of format `bodymesh` whose
  path is a file of the package, the summary of its payload (counts, bounds of the positions as stored, bone names,
  the influences with a weight above 0 in the payload's order, the triangle indices, UV sets 1 or 0); any other mesh
  has no summary (`MESH-001`, as in the tool). Findings are listed in the report's order.
- **A merged cache entry** has no package, so the model derives one (`mergedEntryPackage`) holding exactly what the
  cache knows: identity id `merged-body`, name `<race> <sex> <view>`, version `0.0.0`, licence text saying it is derived
  from the user's own game files; one target (race, sex); **no skeleton binding** (the cache records none: the engine
  binds by bone name to the race's skeleton), so the validator reports `BODY-BIND-001` and `BODY-BIND-014`, which the
  panel names as expected for a cache entry; one mesh `body` (path = the entry name, format `bodymesh`, LOD 0, role
  skin) with an inline summary of the payload **in metres** (positions divided by `kGameUnitsPerMetre` =
  `Constants::UnitsPerMeter`, `components/misc/constants.hpp`, the factor Studio multiplies by again in
  `LineSink::convert`); a region map (origin derived) with one region `slot-<n>` per payload region and its one range;
  weld groups (origin derived) = the payload's groups, tolerance = the largest distance of a member from its group's
  first member, in metres (the merger puts members at one position, so 0); provenance records for `regionMap` and
  `weldGroups` (source digest = the entry's key, tool `openeden.bodymerge`, version = the manifest's merger version,
  parameters `layout <n>`). A payload that does not read leaves no package: the entry shows the reader's message and
  nothing else.
- **The drawn mesh**: the first LOD 0 skin mesh of format `bodymesh` whose payload reads (a package's file, or the
  entry); positions in metres, bind pose, never skinned. Per triangle the region index: the first region (map order,
  then range order) whose range holds it, else -1; done with a "next free triangle" union-find, so a hostile map
  costs O(triangles + ranges), and nothing is coloured when the map names another mesh or has more than
  `Body::Limits::sMaxRegionRanges` ranges. `RegionInfo { id, triangles drawn in it, colour }`; `regionColour(i)` is
  the fixed palette below by `i % 12`. **Region lines**: every distinct edge (vertex index pair, smaller first, in the
  order the triangles first use it) once, in the colour of the region of the first triangle that uses it
  (`kUnassignedColour` for -1), at most `kMaxRegionLines` (the total is counted). **Weld points**: one per weld group
  of the drawn mesh at its first member's position (members past the mesh's vertices are skipped), at most
  `kMaxWeldPoints`.
- **The stage a numbers** (merged entries only): `analysisReportFor(cacheFolder)` = `<cacheFolder>/../../analysis/
  bodies.json` (the cache is `<user data>/openeden/bodies`, the report `<user data>/analysis`); `readAnalysisUnit(text,
  race, female)` reads `openeden.bodyanalysis` v1 and returns the unit whose race equals ignoring ASCII case and whose
  `female` matches: its totals and per seam the label, slots, touches, pair count, distance min/median/max, the six
  ladder counts, overlap/gap/meeting pairs and the largest weight disagreement (game units, as the report has them).
  Hostile text: over `kMaxAnalysisBytes` (64 MiB), nesting over 32 or more than `kMaxAnalysisValues` (1,000,000) values
  (commas, arrays and objects, counted before parsing), not JSON, another format or version, more than 1024 units or 256
  seams, a field of the wrong type, a text field read (race, skeleton, label, slots) longer than `kMaxAnalysisTextBytes`
  (4096 bytes): refused with a message naming the field (or "values"); no exception. A missing report is a note, not an
  error. A package has no stage a numbers.
- **Palette** (`regionColour`), RGB with alpha 1: (0.90, 0.30, 0.30), (0.30, 0.75, 0.35), (0.30, 0.50, 0.95),
  (0.95, 0.80, 0.25), (0.75, 0.40, 0.90), (0.25, 0.85, 0.85), (0.95, 0.55, 0.20), (0.60, 0.85, 0.30), (0.90, 0.45,
  0.70), (0.55, 0.60, 0.95), (0.80, 0.65, 0.45), (0.45, 0.70, 0.60). `kUnassignedColour` (0.45, 0.45, 0.45),
  `kWeldColour` (1.0, 0.2, 0.9).

**The asset types, panel and gizmos** (`documents.hpp`). Asset types: `openeden.body` for `body.json`,
`openeden.bodypack` for `.edenpak`, `openeden.bodycache` for `current` (a generic suffix: the open refuses a pointer
that does not name a merged body pack). Workspace `advancedbodies` ("AdvancedBodies"), panel `advancedbodies.panel`
("Body Package", right) showing the active asset only when it is this module's, inspector sections
`advancedbodies.section.<format>` (a fixed short summary), no gizmo provider (Studio draws the active asset's
gizmos). The panel's rows, all read only: `source.kind`, `source.path`, `source.entry` (merged cache only: the shown
body's `<race> <sex> <view>`), `report.verdict`, `report.errors`, `report.warnings`, `payload.status`,
`finding.<i>` (label `<severity> <code>`, value `<path>: <message>`, the same text as its tooltip, at most 200 rows then
`finding.more`), `mesh.*`, `region.<i>` (label the region id, value `<n> triangles, colour #rrggbb`), `weld.*`,
`analysis.*`; also (not listed at first) `source.position` (`<i> of <n>`) and `source.failures` for a merged cache,
`source.package` for a package, `report.note` (the expected binding findings of a cache entry); `makeSetCommand`
refuses every row. Because Studio builds a form's rows once, every body of a merged cache has its own property
source, kept for the document's life, and the panel shows the selected one's (a new address makes the panel rebuild).
Gizmos `advancedbodies.body.regions` (the region lines) and `advancedbodies.body.welds` (the weld points), in metres,
the body's own origin.

**Choosing the body of a merged cache (amended 2026-11-04 after the first editor run).** The first design made
`source.entry` an editable Choice whose `makeSetCommand` returned an undo step. In the real editor that step went on
the content document's undo stack (`ModuleUi::pushCommand`), whose clean flag is the document's modified state
(`CSMDoc::Document`, `QUndoStack::cleanChanged`), so looking at another body marked the user's ESM document
modified and the close waited on a "save changes?" box. Choosing what to view must not touch the undo stack. Now:
`selectBody(document, index)` (documents.hpp) selects a body of this module's document (false for another module's
document or an index out of range, nothing changes then), and two module commands do it from the Extensions menu:
`advancedbodies.nextBody` ("Bodies/Next Body", shortcut "Ctrl+Alt+PgDown") and `advancedbodies.previousBody`
("Bodies/Previous Body", "Ctrl+Alt+PgUp"); each moves the active asset's selection by one, wrapping around, and calls
`EditorContext::requestRedraw()` (Studio then redraws the panel, the inspector and the gizmos); neither ever calls
`pushCommand`. Both are enabled only when the active asset is this module's document with more than one body.

**The viewer change (Studio, off the game's path, no API change).** A Mesh view opened with no model shows the
active module asset's gizmos instead: `MeshViewWidget` gets the module overlay that `WorldspaceWidget` has
(`PhysicsDebugView` filled from `ModuleUi::overlayLines` with the origin at 0), frames it, and keeps it in step with
`ModuleUi::changed`; a Mesh view WITH a model is unchanged. `Extensions > Show Module Asset in Mesh Viewer` opens one
(`UniversalId::Type_Mesh` with an empty id). The region colours are therefore coloured edges, not filled triangles:
filling needs a triangle in `OverlaySink`, an API 1.1 change that edits the two pinned version tests, toddhow's
decision.

**Deviations from the spec's sentence, and why.** (1) Regions are coloured edges, not filled (above). (2) The
skinned rest pose is not drawn (the viewer cannot, verify item 2). (3) A merged entry is validated as the package the
model derives from the cache, which has no skeleton binding, so two binding findings are always present. (4) The
merged cache opens through the Asset Browser only when its folder is an asset root (`OPENEDEN_CS_ASSETDIR`, or a data
folder): the module cannot learn the user data folder through API 1.0; an API query or Studio scanning
`<user data>/openeden` are toddhow's choice. (5) `Constants::UnitsPerMeter` is used outside `bodylegacy.cpp`, because
the cache does not record its unit.

### Stage f, 2026-11-04: the body package panel (`editor_modules/advancedbodies_editor`)

**Built.** The module (`bodyview.*` the pure model, `body_document.cpp` the asset document, its rows, inspector summary
and gizmos, `advancedbodies_editor_module.cpp` the registration, the export file), built as `advancedbodies_editor` in
`editor-modules/` and second on `CSMModule::coreModules()`. Studio, outside the module and off the game's path: a Mesh
view opened with no model draws the active module asset's gizmos (`MeshViewWidget::showModuleAsset`,
`MeshSubView`, `Extensions > Show Module Asset in Mesh Viewer`); the unattended check `OPENEDEN_CS_BODYCHECK`
(`editor.cpp`, `runBodyCheck`) and `tools/bodypanel_check/` (a synthetic package generator and the run with its
pass/fail lines). `eden_body` gets `POSITION_INDEPENDENT_CODE` so that the shared module links on Linux (push.yml builds
`BUILD_OPENCS_TESTS=ON`, which adds `apps/opencs`); MSVC ignores it: `build.ninja` was regenerated at 08:41 and
`eden_body`'s objects still date from 02:43, so the game's Windows objects did not change. No file under `apps/openmw`
and nothing on the game's render path changed. Threads: main thread only (Studio calls modules there).

**Two Studio faults found by the first editor run, fixed.**
1. *A replaced asset's form read freed memory.* `ModuleUi::openAsset` destroys the old document before it emits
   `changed()`; the old `PropertyForm` is still connected in that emission (its panel only `deleteLater`s it) and its
   `refresh` read the destroyed source. Run 1: after the second open the Mesh view kept the old body (the emission
   stopped), the old form stayed in the panel beside the new one, and the third open crashed the editor (exit 139). Now
   a form reads and edits only while `ModuleUi::generation()` is the one it was built in, and `ModulePanel::rebuild`
   builds a new form on every generation change (a new source can sit at the old one's address). The same pattern was
   in CreativeMotion's rig-then-profile check (task 0032); it is not known to have crashed there.
2. *A view choice marked the content document modified.* The first design chose a merged cache's body with an
   editable Choice row and an undo step; Studio pushes module commands on the content document's undo stack, whose
   clean flag is that document's modified state, so the close of run 2 waited on a "save changes?" box until the
   300 s timeout. Amended in the design (above): the row is read only, `selectBody` and the module commands
   `Bodies > Next Body` and `Previous Body` choose, with `requestRedraw` and no undo step.

**Tests first, by a separate agent.** The test-writing agent read the design text above, the two module headers and the
public `components/body` headers, never the module's .cpp (stubs at the time), and wrote
`apps/opencs_tests/model/module/advancedbodies/` (`abtestsupport.hpp` with synthetic stacked-quad and strip bodies,
packages, packs, caches published with `Body::publishCache`, stage a report texts, and its own reference of every rule;
`testabview.cpp`, `testabhostile.cpp`, `testabmodule.cpp`). Red run against the stubs: 81 tests, **70 failed, 11
passed** (palette and id constants, refusals that a refuse-everything stub passes). After the implementation, at its
first build: 81 pass. Mutation check (region priority reversed, weld point at a group's last member): 4 tests failed,
code restored. The amendment: the same agent changed `testabmodule.cpp` from the amended text (3 tests removed because
the design no longer has an editable Choice, 1 renamed, 5 changed, 6 added: 87 tests); red against the old code: **14
failed**; after the change all pass. `eden-studio-tests` 676 pass (589 before). The model's own "more ranges than the
reader allows colours nothing" branch is not reachable through the open functions (the reader refuses such a file).

**In the real editor** (work folder `I:\Projects\OpenEden-work\0069f`; `--replace=config` with a copy of the lodtest
`openmw.cfg`, `--user-data` holding a copy of the stage e review's merged cache `merged-01c881d487d5110f.edenpak`
(20 bodies) and stage a's `analysis/bodies.json`, the asset root that cache folder; runs `runs/run1` to `run7` and
`bodypanel1`). Final run, the three sources in order:
- *The cache through `current`:* Argonian female third, 1 of 20: payload reads; BODY-BIND-001 and BODY-BIND-014 (the
  expected pair); 1,906 vertices, 2,610 triangles, 4,388 edges drawn; 20 regions `slot-2` to `slot-26`; 28 weld groups
  (64 members); stage a: 1,908 vertices and 2,610 triangles for the stock parts (2 vertices welded), forearm-wrist 20
  pairs within 1e-4 units, knee-ankle 14. Two Next Body commands (1.18 and 1.15 s each, three document windows open)
  showed Breton female third: 1,479 vertices, 2,238 triangles, 19 regions, 26 weld groups; stage a 1,483 vertices and
  the same triangles, forearm-wrist pairs at 0.00106 units, knee-ankle at 0.00093. The document's undo stack stayed at
  0 steps and not modified.
- *The synthetic package, folder and `.edenpak`* (`tools/bodypanel_check/make_synthetic_package.py`: tubes 1.75 m
  tall, 420 vertices, 672 triangles, a payload in metres, no game data): exactly its two planted errors,
  BODY-GEOM-005 at `/regionMap/regions` (triangle 192 of `body` in no region: the head) and BODY-WELD-005 at
  `/weldGroups/groups/12`; six regions of 96 triangles; 13 weld groups. `edenbodyvalidate` on the same folder gives
  only BODY-MESH-001, as designed (it has no mesh source, so its geometry rules do not run).
- **Frames viewed** (`runs/run7/shot_*_mesh.png`, the floating Mesh view, 960 x 720): the Argonian and Breton bodies
  with every slot in its own colour and magenta weld points exactly at the wrists and the knee-ankle seams (the only
  welded seams); the tube body with pelvis red, torso green, legs blue and yellow, arms purple and cyan, the head grey
  (unassigned) and the weld points on the pelvis-torso ring. Panel grabs `shot_*_panel.png` show the rows above.
- Exit code 0 in the final runs; the real user folder (`OneDrive/Documents/My Games/OpenMW`) unchanged by names,
  sizes and times in every run; `tools/edenstudio_check/check.sh` and `tools/bodypanel_check/check.sh` pass; the
  editor ignores `OPENEDEN_QUIET_WINDOW` (no reference in `apps/opencs`): its windows appeared for about a minute per
  run, without focus.

**Not done or not verified.** A skinned rest pose (the viewer cannot; the bind pose is drawn), filled region triangles
(the API has lines only: API 1.1, toddhow's decision), labels; opening the cache from the user data folder without
`OPENEDEN_CS_ASSETDIR` (API 1.0 has no user data folder); a person's click on the menu items and the Asset Browser
entries (the check calls `openAsset` and `runCommand`); the 32 Lua tests (no game file changed); on Linux only the
editor targets were built (see below). The time of a body switch (about 1.15 s with three document windows) was not
profiled: the first time a body is shown it is also worked out (payload, validator, edge sort, the stage a report read
and parsed), and Studio rebuilds the forms of every window, CreativeMotion's hidden panel included (it shows any active
asset); which part dominates is a guess. The 12-colour palette repeats for the 20 slots of a stock body (`slot-2` and
`slot-16` share red).

**The Linux link (found by the coordinator's gate at `4ec2afd409`).** `libadvancedbodies_editor.so` failed to link:
`edenarchive`'s `archive.cpp` (a function-local thread_local, the chunk scratch) was not position-independent
(`R_X86_64_TPOFF32 ... recompile with -fPIC`). The claim above that the Eden Archive libraries were already PIC was
wrong: only its zstd and BLAKE3 targets were. `creativemotion_editor` never met it because it links only
CreativeMotion's libraries, which `components/creativemotion/CMakeLists.txt` builds with
`CMAKE_POSITION_INDEPENDENT_CODE ON`. Fix: `POSITION_INDEPENDENT_CODE` on `edenarchive` (the alternative, reading packs
through the host, needs an API the host does not have). Checked in the Linux container (`cs_inside.sh`, HEAD with the
change overlaid): `eden-studio` and `eden-studio-tests` build with `-Werror`, the tests pass (676), the editor starts
under xvfb and loads both modules. Windows after the reconfigure: only the version stamps recompiled (no `edenarchive`
or `eden_body` object), `openmw`, `eden-studio`, `eden-studio-tests` (676), `components-tests` (5,482) and
`openmw-tests` (927) build and pass, and `tools/edenstudio_check/check.sh` passes.

**Fresh review of stage f** (a separate agent, read only, after `86e31bf674`; no blocker). What each finding became:
1. *A pack was read whole before it was known to be a body source* (every `.edenpak` in an asset root is listed, and
   a texture pack would be decoded, up to 512 MiB, on the main thread): fixed, the pack is classified from its index
   (`peekPack`) and refused without decoding; a package's files are dropped once its body is worked out.
2. *The stage a reader capped only bytes and nesting:* fixed, values (1,000,000) and text fields (4,096 bytes). The
   report is still parsed once per body shown (about 58,000 values); not cached.
3. *An exception in the lazy work of a body could leave a Studio callback* (and withdraw the module, or
   CreativeMotion's, whose panel shows any asset): fixed, caught in `BodyViewAccess::body`; the body shows the message.
4. *The core list and the Studio fixes had no test:* a core-list test added by the implementer (not red first: it pins
   existing behaviour); the generation guard, the panel rebuild and the model-less Mesh view are covered only by
   `tools/bodypanel_check` (it needs a window, a GPU and a baked cache).
5. *Long finding rows could not be read* (a read-only row is a disabled line edit): the full text is the tooltip.
6. *clang-format 14 not recorded:* it was run before `86e31bf674` (a throwaway Ubuntu 24.04 container with
   `clang-format-14`, every changed file; the only changes it made were in new lines) and again on the review fixes.
7. *The cause of the switch time was stated though not measured:* worded as a guess above.
8. *`body.json` and `current` are broad suffixes:* `openBodySource` now takes exactly `body.json`; `current` stays the
   registry's suffix (a file named `recurrent` is listed and refused), and the Asset Browser still lists
   `somebody.json` (the registry matches endings).
9. *Nothing ties the stage a report to the bake* (it is read from beside the cache): a question below.
10. *The Next and Previous Body shortcuts are enabled only when the module's menu is shown* (`View::setupExtensionsMenu`
    sets the enabled state on `aboutToShow`): existing Studio behaviour, not changed; nobody pressed them.
11. *The editor's windows appeared on the desktop* (it ignores `OPENEDEN_QUIET_WINDOW`): a question below.
12. Nits: `<cstddef>` in `documents.hpp`; the rows not listed at first are now in the design; the file names
    (`bodyview` beside `body_document`) are left as they are (the tests include the header); the union-find halves paths
    only, so it is near-linear, not linear.

Tests of the fixes: the same test writer added 9 from the amended design text (exactly `body.json`, a pack refused
from its index although its only entry's data is damaged while a damaged body package fails in the full reader instead,
the value and text caps at and over the cap, finding tooltips, the added rows), plus the implementer's core-list test.
Red against the committed code (the new header constants only): **6 of 97 failed**; after the fixes `eden-studio-tests`
686 pass. clang-format 14 leaves every changed file as it is. `tools/bodypanel_check/check.sh` passes with the fixes
(`bodypanel2`).

**Questions for toddhow.** (1) API 1.1 for a filled overlay (`OverlaySink::triangle`) and a user data folder query:
both change the two tests that pin the API at 1.0. (2) Should module edits have an undo stack of their own instead of
the content document's (today any module edit marks the ESM modified)? (3) Should CreativeMotion's panel show only its
own documents? (4) The merged entry always lists the two binding findings: keep, or let the cache record the skeleton
profile? (5) The stage a numbers come from `<user data>/analysis/bodies.json` beside the cache, with nothing tying
them to the bake: should the bake record the report's digest, or the panel say "from a report of <date>"? (6) Eden
Studio does not honour `OPENEDEN_QUIET_WINDOW`; each check run showed its windows for about a minute (without focus):
make the editor honour it, or accept it for editor checks?

### Stage f follow-up, 2026-11-05: the regions filled (Editor Module API 1.1; toddhow's approval of 2026-11-04)

Open question 1 of stage f (filled triangles) is answered by task 0031's API 1.1. `RegionsGizmo::draw` now draws one
blended triangle per triangle of the drawn mesh in its region's colour (`kFillAlpha` 0.35, `kMaxFillTriangles` 40,000,
depth tested), then the edges as before. `Bodies > Cycle Region Display` (`advancedbodies.cycleRegionDisplay`,
Ctrl+Alt+R) steps through fill and edges, fill alone, edges alone (`regionDisplay`, `setRegionDisplay` in
`documents.hpp`); not an undo step, never dirty. The default (both) is what the stage's gizmo tests pin for the edges, so
they are unchanged, and the gizmo list is still the two gizmos. Tests (`advancedbodies/testabfill.cpp`, 7: six written
first, 4 of them red with the fill switched off, `I:/Projects/OpenEden-work/studio2/red2-tests.log`; the cycle-command test was
written with the command and not run red). Existing tests changed (awaiting toddhow's approval): `testabmodule.cpp`, the
two `commands().size()` pins, 2 to 3 (the module registers the cycle command). Real editor,
`tools/bodypanel_check/check.sh` passes (5 of 5 lines) and the Mesh view frames show the regions filled with their edges
over them: `I:/Projects/OpenEden-work/studio2/bp1/frames/shot_0_mesh.png` (before, edges only:
`I:/Projects/OpenEden-work/0069f/bodypanel2/frames/shot_0_mesh.png`). Not looked at: the fill alone, the edges alone
(unit-tested), an always-on-top triangle (no module uses it yet; the second geometry is untested in a frame).

**The merged cache without `OPENEDEN_CS_ASSETDIR` (2026-11-05; open question 2 of stage f).** API 1.1's
`EditorContext::userDataPath()` lets the module find `<user data>/openeden/bodies/current` itself:
`Bodies > Open Merged Body Cache` (`advancedbodies.openMerged`, enabled when the host has a user data folder; with no
cache it logs "run openmw --bake-bodies first" and opens nothing). `OPENEDEN_CS_BODYCHECK=<prefix>` with no source runs
the same command. The module has 4 commands now (the two `commands().size()` pins went 3 to 4, awaiting toddhow's
approval). Tests: `advancedbodies/testabuserdata.cpp` (6, 5 red first). `tools/bodypanel_check/check.sh` passes 6 of 6
lines, the new one with no asset root named.
