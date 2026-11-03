# Task: AdvancedBodies stage B1 (the body contract: package format, validator, legacy adapter, conformance suite)
Design doc section: `docs/design/advancedbodies-in-openeden.md`, sections 2 (where it lives), 3 (CreativeMotion relation), 4 (the package), 7 (assembled parts), 8 (validator, versioning) and 15 (testing); `docs/design/Body-System-Design-Guide.md` sections 4, 6, 12, 14 (B1) and 15; `docs/research/0027-advancedbodies-in-openeden.md`; decisions 0001, 0003, 0005, 0007 and 0010 (item 20).
Goal: define the body package as canonical JSON, check packages with one validator (command line and in engine), describe Morrowind's default human body parts as such a package through a legacy adapter, and prove all of it with a conformance suite that includes a humanoid and a non-humanoid; nothing the player sees changes.
Inherited code touched (read at the commit that starts each stage; stage c reads, stage e adds one console command): `apps/openmw/mwrender/bodyparts.{hpp,cpp}` (`getBodyParts`, `getHeadModel`, `getHairModel`: **called, not changed**), `apps/openmw/mwrender/npc.cpp` (`Npc::updateParts`, `Npc::defaultPartModels`, `markSkinModel`: **read only**), `components/mwanimation/partbones.hpp` (`MWAnim::sPartBones`, read), `components/esm3/loadbody.hpp`, `loadnpc.hpp`, `loadrace.hpp` (read), `apps/openmw/mwclass/npc.cpp` (`Npc::adjustScale`, read), `apps/openmw/mwscript/` console command registration (stage e: one new command `bodycheck`), `CI/check_file_names.sh` (read: new files are lowercase). CreativeMotion's `anim/clip` `opas::anim::SkeletonProfile` is **read and reused**; `components/creativemotion` is not edited in this task.
Inputs / outputs: new library `components/body/` (target `eden_body`; `bodypackage.*`, `bodyjson.*`, `bodyvalidate.*`, `bodycontract.hpp`), new tool `tools/bodyvalidate/` (program `bodyvalidate`), new `apps/openmw/mwrender/bodylegacy.{hpp,cpp}`, a new JSON format `openeden.body` v1 (`body.json`), a new report text format, fixtures generated in code (and a few tiny hand-written JSON files under `apps/components_tests/body/data/`), console command `bodycheck [refid]`. No new setting, no save change, no Lua API, no extension contract, no change to any existing file format, no shader.

Status: DRAFT written 2026-11-02, not approved, nothing built. Full loop when started (new data model, a loader of untrusted files, the first piece of a core system that later touches saves, physics shapes and rendering). Every fact below is cited from the code at the current commit or marked **verify**. toddhow decides the table at the end before stage a starts.

## What exists and what is missing (from research note 0027)

- Morrowind has body-part records and a selection function (`getBodyParts`), but no body: a person is a skeleton with up to ~20 mesh parts attached by name to bones (`Npc::insertBoundedPart`, `MWAnim::sPartBones`).
- CreativeMotion has the rig, the skeleton profile (bone-name mapping) and body-map measurements, none of which is a package.
- There is no format that says "this is a body", no way to check one, and no test fixture of a non-humanoid.

B1 builds the foundation every later stage reads and **changes no behaviour**: it is the one stage where "off means unchanged" is true by construction (no setting exists to switch on). It is still full loop, because the format and validator are contracts that later stages (derivation, morphs, saves, garments, physics) depend on and that load untrusted files.

## Design, in plain language

A **body package** is a folder (`something.body/`) with a main file `body.json` that names the body, its skeleton binding, its meshes, and optional extras (morphs, proxy, regions...). A package needs only a skeleton binding, at least one render mesh and some metadata; everything else is optional and each optional part is labelled **authored**, **derived** or **missing** (the guide's principle 2). In B1 nothing is *derived* yet (that is B2), so the label is either authored or missing, and the format already has room for derived.

The **validator** reads a package and reports problems with a code, a severity and a path into the file ("`/meshes/0/weights/12`"). A package that fails is *rejected*: later stages fall back to the default body. In B1 the validator is a library function, a command line tool and a console command.

The **legacy adapter** makes a package out of Morrowind's records: for a race and sex it calls the engine's own selection functions, takes the chosen head, hair and body-part models and presents them as one package whose meshes are the parts, each knowing its part type and its attach bone. It does not copy the selection logic (so it cannot disagree with the game) and it does not read mesh files itself: it asks the engine's mesh reader for a **mesh summary** (vertex count, bone names used, weights per vertex, triangle count, bounds), the only thing the validator needs. The adapter's package is written **only in memory** (and optionally to a text dump); no Bethesda mesh is copied into any file.

The **conformance suite** is a set of generated reference packages (a humanoid with about 20 bones, a tailed four-legged non-humanoid, a legacy-assembled body from synthetic records) and expected validator outcomes. Every later contract change must keep it passing.

## The package format (`openeden.body` v1)

Canonical JSON: keys in a fixed order, numbers written the way CreativeMotion's `core/format` writes them (negative zero as `0`; the pitfall in `kinematic/markers/CLAUDE.md`), two-space indent, trailing newline; `write(read(canonical))` is byte-identical; loader errors give file, 1-based line and JSON pointer. The reader reuses CreativeMotion's `opas::format_json` machinery (`core/format`: `LoadError`, `detail::JsonReader`, `parseAndRead`) and the build flag `JSON_Diagnostic_Positions`; no JSON type appears in a public header.

Top level (all keys in this order; unknown keys are an error in v1 so mistakes are visible, a later minor version may add keys the old reader must ignore: decided in the table):

| Key | Required | Content |
| --- | --- | --- |
| `format` | yes | the string `"openeden.body"` |
| `version` | yes | `1` |
| `identity` | yes | `id` (lowercase letters, digits, `_`, `.`, `-`; max 64), `name`, `version` (package's own, `major.minor.patch`), `author`, `license` (SPDX id or free text), `contractVersion` (`{major, minor}`), `ageClass` (`"unspecified"`, `"minor"`, `"adult"`; default `"unspecified"`) |
| `targets` | no | what the package replaces: list of `{race?, sex?, creatureType?, priority}` (used by B5; B1 validates the shape only) |
| `binding` | yes | `skeleton` (path of an `opas.skeletonprofile` file or an inline profile object), `rootBone`, `unmappedBones` (extra bones the profile does not name) |
| `meshes` | yes (at least one) | list; each: `id`, `path` (VFS path), `meshFormat` (`"nif"`, `"gltf"`, `"summary"`), `lod` (integer, 0 is highest), `role` (`"skin"`, `"head"`, `"hair"`, `"garmentBase"`, `"other"`), `partType` (optional, a Morrowind-neutral string like `"neck"`, `"hand"`), `attachBone` (optional) and `summary` (optional inline mesh summary, below, used by tests and by packages that carry precomputed data) |
| `morphSet` | no | `{origin: "authored"|"derived"|"missing", morphs: [{id, range:[min,max], default, targets: [...]}]}`; B1 validates shape only, evaluation is B3 |
| `proxy`, `physicsProfile`, `measurements`, `regionMap`, `landmarks`, `softTissue` | no | each `{origin, ...}` with the contents the guide's section 4 lists; B1 defines the **shape** of each (types, ranges, units), derivation and use come later |
| `provenance` | no | for derived parts: source digest, tool id and version, parameters (the archive design's provenance record, B2 fills it) |

Units: metres, kilograms, radians, Z up, right-handed (CreativeMotion's convention). A package never contains Morrowind units; the legacy adapter converts at one function, `toBodyUnits`, with `Constants::UnitsPerMeter`.

**Mesh summary** (`MeshSummary`, plain arrays, no engine types): `vertexCount`, `triangleCount`, `boundsMin`, `boundsMax`, `boneNames[]` (the bones the skin refers to), `influences[]` per vertex as `(boneIndex, weight)` runs with a fixed maximum, `regionPerTriangle[]` (optional), `uvSetCount`. This is what `MeshSource` returns; the validator never opens a file.

## The validator

One rule set in `bodyvalidate.cpp` (one place, like `detail::findRigProblem` in `core/rig`): the loader checks only types and finiteness; everything else is a validator rule, so programmatic and loaded data follow the same rules. Each finding: `code` (stable string, e.g. `BODY-SKIN-002`), `severity` (`error` rejects the package, `warning` does not), `path` (JSON pointer), `message` (plain text, one line).

Rule groups (the guide's list, section 6; thresholds are named constants with a comment on their source, none invented in this spec: the first numbers are **starting values to be settled in stage b with a written reason**):

| Group | Checks |
| --- | --- |
| Structure | format, version, required parts present, contract version supported (a package newer than the engine is refused), ids valid and unique, meshes have distinct LODs |
| Binding | the skeleton profile loads and passes `validateSkeletonProfile`; root bone exists; no bone mapped twice; every bone a mesh's skin uses is in the profile or in `unmappedBones`; CreativeMotion's assumptions checked on the bound skeleton (parents before children, finite rest transforms, unit rotations, **nothing rotating above the animation root**; failures name the assumption, handoff section 4) |
| Skinning | per vertex: weights finite, non-negative, sum to 1 within a named tolerance; at most N influences; bone index in range; no vertex with zero influence on a skinned mesh |
| Geometry | bounds finite and inside a proportion range the retargeting can handle (stage b sets the range from the conformance bodies, not from a guess); triangle indices in range; if a region map is present every triangle belongs to exactly one region (no gaps, no overlaps) |
| Morph (shape only in B1) | each target references valid vertex indices, deltas finite, slider range contains the default, ids unique; the *inversion at extremes* check is B3 |
| Physics profile (shape only in B1) | shapes finite and positive, masses positive, joint limits contain the rest pose; the references to `rig`/`profile` files load with CreativeMotion's own `validateRig`/`validateProfile` |
| Age | `ageClass` is a known value; a package that enables a restricted tier (a flag in `physicsProfile`/`softTissue` reserved for B9) without `adult` is an error; **unknown never counts as adult** |
| Non-humanoid | no rule may require two legs, a pelvis or two feet; `balance`/`stepping` applicability is a derived flag (`humanoidGait: true|false`) from the skeleton, not an error |

Report formats: plain text for people (one finding per line, errors first, sorted by path then code, so the report is deterministic) and a canonical JSON form for tools. The tool prints the text report and exits 0 (no errors), 1 (errors), 2 (cannot read).

## The legacy adapter (`apps/openmw/mwrender/bodylegacy.*`)

Input: a race id, a sex, a view (third person only in B1), a store reference (`MWWorld::Store<ESM::BodyPart>`), a `MeshSource`. Output: a `BodyPackage` plus a list of notes.

Steps (each mapped to code that exists; the adapter calls, never copies):

1. `getBodyParts(race, female, false, false, store)` gives the skin part for each `ESM::PartReferenceType` slot (nullptr for missing). `getHeadModel`/`getHairModel` give head and hair (needs an `ESM::NPC`'s head/hair ids; for the reference body the adapter takes the first available head and hair record of the race and sex, in store order, and says so in a note).
2. Each non-null part becomes a package mesh: `id` from the part type (`"neck"`, `"cuirass"`, `"rhand"`...), `path` the record's model, `meshFormat: "nif"`, `role` `"skin"` (head `"head"`, hair `"hair"`), `partType` the part type's lower-case name, `attachBone` from `MWAnim::sPartBones`.
3. The skeleton binding is the shipped `humanoid-to-eden.skeletonprofile.json` (canonical to Morrowind `Bip01 ...` names). **Beast races** have a different skeleton (`ESM::Race::Beast`, `Npc::updateNpcBase`): B1 supports the human reference only and reports `LEGACY-BEAST-001` (warning, not supported yet) for them.
4. Units: the mesh summary comes from the engine's reader in Morrowind units; `toBodyUnits` converts at this one function.
5. The package's `identity.ageClass` is `"unspecified"` (Morrowind data has no age field in the records read here; whether the compatibility pack can answer is **verify** and is B9's concern). The package is therefore *never* eligible for a restricted tier.
6. Missing parts, fallbacks `getBodyParts` made (a male part standing in for a female one), and the head/hair choice become notes (the "authored/derived/missing" labels, here `legacyFallback`).

`bodycheck [refid]` (console, stage e) runs the adapter for the target actor's race and sex, runs the validator and prints the report to the console and the log. It changes nothing. It lets toddhow see the reference body's report without a tool.

**Mesh reading for the summary.** The adapter obtains `MeshSummary` for each part by reading the model through the engine's resource system the way `Npc::insertBoundedPart` does and walking its `Anim::Skin` bone data and vertex arrays (`handleSkin`, `nif.cpp`). Whether that walk is tractable without changing `nif.cpp` is the first risk (below); if it is not, B1 ships the adapter with `summary` taken from a **precomputed sidecar** produced by a small tool, and the in-engine check covers the structural part only.

## Decisions (recommendation first; toddhow decides)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | Package form | Folder source; `.edenpak` packing and the folder/pack digest equality test in B1d (needs task 0052's packer; if 0052 is not done, B1d waits and the rest stands) | Folder only in B1 |
| D2 | Meshes in B1 | Reference by path with a `meshFormat` tag; validator reads `MeshSummary` through `MeshSource`; no mesh parser in `eden_body` | Parse glTF in `eden_body` now |
| D3 | Unknown JSON keys | Error in v1 (strict, catches typos); a future minor version declares new keys explicitly | Ignore (lenient) |
| D4 | Bone mapping | Reuse `opas::anim::SkeletonProfile` unchanged in B1; the v2 additions (extra chains, proportions) come in B2/B3 | Define the grown format now |
| D5 | Reference body | Morrowind's default human male and female (`getBodyParts` for a human race), synthetic records in tests, user's own data for the in-game check | Include a beast race in B1 |
| D6 | Where `eden_body` gets CreativeMotion types | Link `opas` `core/rig`, `core/format`, `core/math` and `anim/clip` (skeleton profile) | Copy the profile type into `eden_body` (a fork of the format; no) |
| D7 | Strictness thresholds | Settle each numeric threshold in stage b with a written source (the conformance bodies and CreativeMotion's own tolerances), never a bare number | Fix them in this spec |
| D8 | Console command | `bodycheck` in B1 (no setting needed) | A `[Bodies] legacy adapter` setting that runs it at actor build |
| D9 | Package id charset and the mesh `role` list | As in the table above | Free text |

## Stages (each committed working and tested; full loop each)

- **a Model and JSON.** `components/body/` CMake target, `BodyPackage` and its parts as plain data, the `openeden.body` v1 reader and canonical writer with located errors, the `MeshSummary`/`MeshSource` types, `components/body/CLAUDE.md`. Tests first from this spec by a test-writing subagent: round trip (byte identical for every fixture), every malformed-input case refused with file, line and pointer (wrong type, missing required key, unknown key, NaN/Infinity text, overflowing number, wrong format/version, duplicate id, path traversal in a mesh path), canonical writer keys/number format, negative zero.
- **b Validator and tool.** `bodyvalidate.*`, the rule table above (each rule with a passing and a failing fixture), the text and JSON reports, deterministic ordering, `tools/bodyvalidate`, exit codes, thresholds with their written sources.
- **c Legacy adapter.** `bodylegacy.*`, `toBodyUnits`, the part-to-mesh mapping, notes and the beast-race warning; tests with synthetic `ESM::BodyPart` records in a synthetic store (male and female, with a missing female part to prove the fallback note, a first-person part that must not be picked, a vampire head that must not be picked for a plain human) and a fake `MeshSource`. The adapter's selected parts equal `getBodyParts`'s own result in every case (the test calls both).
- **d Conformance suite.** Generated reference packages: `humanoid` (about 20 bones, two feet, 5 meshes, a region map and a morph shape), `tailed_quadruped` (non-humanoid: no pelvis, four legs, a tail chain, `humanoidGait: false`), `legacy_assembled` (from stage c). A table of expected findings per fixture (clean ones have none; a set of single-defect copies each produce exactly the expected code). The packed-form digest equality test (D1) if task 0052 allows. The suite is a gtest target `body-conformance` that every later contract change must keep passing.
- **e In-game check.** Register `bodycheck` (one new console command, nothing else in `mwscript` changes), run it on a human male and female with the user's data, record the report in the stage log. No other engine code changes.
- **f Close-out.** Per-area `CLAUDE.md` files, formatting (clang-format 14), the file-name check, a fresh reviewer reads every stage, changelog row for the root `CLAUDE.md` (proposed text in the stage log, not edited by the implementer without toddhow).

## Constraints

- **Determinism.** No clock, no randomness; reports sorted; canonical writer; the same package gives the same report bytes (tested).
- **Untrusted input.** A package file can come from a modder: all sizes, indices and counts checked before use, caps on string and array lengths (named constants), paths normalised in one place and rejected if they escape the package; a bad package never crashes or hangs (a fuzz-style mutation test like the one for the terrain tile payload).
- **No game assets in the repository.** Fixtures are generated in code or tiny hand-written JSON; synthetic meshes only; the adapter's output for real data is never committed (root guardrail).
- **Contracts carry no third-party types:** no VSG, Jolt, OSG, Qt, MyGUI or `ESM::` in `eden_body`'s public headers; `ESM::` appears only in `bodylegacy.*`.
- **Ownership:** value types; `std::shared_ptr<const BodyPackage>` only as the package handle, with the comment saying why (immutable, shared by all actors using it).
- **Allocation:** none in per-frame code, and B1 has none per frame; the validator is a load-time operation.
- **Threading:** pure functions; `bodycheck` runs on the main thread; nothing here spawns a thread. Stated per PR as the root guide requires.
- **Off means unchanged:** B1 adds a library, a tool, a command and an adapter that is only called by the command. The 32 Lua tests, `components-tests`, `openmw-tests` pass unchanged, and a recorded NPC part list for a fixed set of NPCs (existing `Npc::defaultPartModels` output) is identical before and after.
- **Units:** package data in metres, kilograms, radians, Z up; Morrowind units only inside `bodylegacy.*`, converted at one function.
- **Names:** new C and C++ files lowercase letters and digits only (`CI/check_file_names.sh`); new tool program name `bodyvalidate`.
- **No settings, no save changes, no Lua API, no shaders.**

## Risks and open questions

1. **Can the adapter get a `MeshSummary` from the engine's loader without changing `nif.cpp`?** `handleSkin` builds `Anim::Skin` bones, `boneIndices`, `boneWeights` (`components/animation/skin.hpp`), which carries what the summary needs, but reading them back from a loaded `vsg::Node` may need a small visitor. If it needs a change in vsgopenmw code, stop and ask (root rule: inherited code changes only for a reason a spec states).
2. **Do stock parts carry bone names that match the skeleton profile?** Unknown until the in-game check (**verify**); the validator reports it as `BODY-BIND-*` findings, which is the point.
3. **Does a human race's default selection give a complete set?** `getBodyParts` returns nullptr slots for missing parts; some are legitimately empty (skirts, hair for bald characters). The adapter notes them; the validator treats a missing *required-for-humanoid* part (neck? hands?) as a warning, not an error, because armour covers them. Which parts are required is settled with the real report in stage e.
4. **`anim/clip` as a dependency** pulls cgltf into `eden_body`'s link line. If that is too heavy, move the profile type to `core/rig` (a CreativeMotion edit through an OpenEden task; allowed by decision 0010 item 20, but its own spec).
5. **The interface of `MeshSource`** may change when B2 needs positions (derivation). Version it from the start as part of the contract (`contractVersion`), and keep `MeshSummary` additive.
6. **JSON strictness (D3)** makes a future minor version a breaking change for old readers; the version policy in the guide ("additive changes stay compatible") needs an explicit "ignored keys" list; settled with D3.

## Acceptance criteria

- [ ] The format table above is implemented; `write(read(x))` equals `x` byte for byte for every canonical fixture; every malformed-input case in stage a is refused with file, line and JSON pointer; a mutation test (flip each byte of a small package) never crashes or hangs.
- [ ] The validator has a passing and a failing fixture for every rule group; reports are deterministic (same bytes twice, and with fixture order shuffled); error versus warning behaves as the table says; thresholds each have a written source in the stage log.
- [ ] `tools/bodyvalidate` prints the text report and returns 0, 1 or 2 as specified; `--json` prints the canonical JSON report.
- [ ] The legacy adapter, on synthetic records, selects exactly what `getBodyParts`, `getHeadModel` and `getHairModel` select (test calls both), notes every fallback and every missing slot, rejects nothing silently, and reports `LEGACY-BEAST-001` for a beast race.
- [ ] The conformance suite has a humanoid, a non-humanoid (no rule requires two legs; `humanoidGait: false`) and a legacy-assembled body; each defect fixture yields exactly its expected code; the target is named `body-conformance` and is part of `components-tests` or its own test program.
- [ ] `bodycheck` run on a human male and a human female with the user's data prints a report; the stage log records the report and what it shows about bone names and missing parts (**verify** items closed or restated).
- [ ] With nothing enabled: `components-tests`, `openmw-tests` and the 32 Lua tests pass; the recorded part lists of a fixed NPC set match the commit before; no frame, setting or save changed.
- [ ] No Bethesda file, no adult content and no third-party body asset in the repository, fixtures included; `CI/check_file_names.sh`, `CI/check_clang_format.sh` (clang-format 14) and `CI/check_no_osg.sh` pass; Linux `-Werror` build passes.
- [ ] A fresh reviewing subagent read each stage's diff against this spec and found no unresolved issue; the per-area `CLAUDE.md` for `components/body` exists.

Out of scope: derivation of capsules, measurements and regions (B2); sliders, morph evaluation and proportion changes (B3); the surface query API (B4); selecting or replacing a body for an actor, save records, the population generator (B5); garments (B6); any cosmetic physics (B7 to B9); render paths, the Studio module, importers and the in-game customiser (B10); beast races and creatures; first person; Fallout: New Vegas; changing `bodyparts.cpp`, `npc.cpp` or any renderer file; any setting.

## Stage log (implementer's notes)

*(Empty. Nothing is built.)*

Proposed root `CLAUDE.md` changelog line (not applied; for toddhow): `| 2026-11-02 | AdvancedBodies planned as a core system (research note 0027, design doc docs/design/advancedbodies-in-openeden.md, task specs 0063 onward reserved for B1 to B10; B1 spec 0063 drafted). Toddhow's Body System Design Guide committed. Nothing built; decisions in the design doc's section 18 are open |`
