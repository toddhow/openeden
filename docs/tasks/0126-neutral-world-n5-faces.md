# Task: Neutral world, Skyrim SE NPC faces (facegen heads, face tint, hair tint)

Status: **BUILT on Windows, checked in game** (local commit on branch `n5faces`, not pushed). Authorised by the request to give the Skyrim SE NPCs of the Bannered Mare their faces (follow-up to task 0121, stages N5-1 and N5-2). The number 0126 was picked because 0123 and 0124 were taken (another agent may have taken the next one).
Design doc section: `docs/decisions/0017-neutral-actors-and-items.md` section 5; task 0121 (the floating-head fix and what was open).
Goal: an NPC of a Skyrim SE neutral cell is drawn with its own face (skin tone, eyes, brows, hair in its own colour) instead of only eyes, brows and hair.
Inherited code touched: none (all new or Skyrim-only). Skyrim-only edits: `apps/openmw/mwrender/skyrimmodel.cpp` (`texturePath`, `addSurface`, `buildSkinnedShape`), `components/nifscene/skinned.*`.
Constraints: only reachable with `[Game] neutral world` and `[Game] neutral actors` on; Morrowind code is not entered; main thread; every list capped; nothing throws across the SDK.
Acceptance criteria:
- [x] Tests first, red against stubs, then green (see the stage log).
- [x] In game, the NPCs of the Bannered Mare have faces (frames in the stage log).
- [x] `CI/check_layer_rules.sh` stays at 104; the other `CI/check_*.sh` and `tools/testprofile/coverage.sh` pass.
- [x] With the settings off, Morrowind is unchanged: the `tools/worldobjects_check` dump equals the parent's (111 lines).
Out of scope: face morphs beyond what the facegen file already holds, skin tint of body parts (shader type 5), normal maps and specular, animation, facial animation, eye shine, hair under a helmet, a facegen file in an override plugin's own folder.

## What a Skyrim NPC's face is (facts, with how they were established)

1. **Per-NPC facegen mesh.** `meshes\actors\character\facegendata\facegeom\<plugin>\<8 hex digits>.nif`, `<plugin>` the lower-case name of the plugin that owns the NPC's form id and the 8 digits the form id local to that plugin (load order byte zero). **Verified**: the five official masters' `Skyrim - Meshes0.bsa` holds 3,158 such files; the pack's rule, applied to all 6,361 NPC_ records of the five masters, names an existing file for 4,669 NPCs and 3,157 distinct files (of 3,158). A light plugin's naming (the low 12 bits, `00000abc.nif`) is RECALLED-VERIFY: no light plugin was checked. A facegen file in the folder of an overriding plugin (not the owner) is not looked for.
2. **What the file holds.** Read on a sample (`00013255.nif`, Python reader of the NIF header and block table) and by the pack's own reader on 316 files: a `BSFadeNode` with the head bone chain (`NPC Head [Head]`, `NPC Spine2 [Spn2]`), and per file about 7 `BSDynamicTriShape` shapes with `BSLightingShaderProperty`: the head, mouth, hair, brows, eyes and beard, the head parts already morphed. So the facegen file **replaces** the NPC's head parts. 316 of 316 files read, all with skinned shapes, bones in the race skeleton.
3. **Colour.** The face shape has shader type 4 (face tint). Its `BSShaderTextureSet` has 9 slots: 0 the shared detail/diffuse map (`Actors\Character\Male\MaleHead.dds`, **no `textures\` prefix**), 1 model-space normal map, 2 skin map, 3 detail, 4 and 5 empty, **6 the NPC's baked tint** (`data\Textures\Actors\Character\FaceGenData\FaceTint\<plugin>\<id>.dds`, 512 x 512 skin tone, blush and paint), 7 specular. All 316 tint textures exist in the texture archives (316 of 316). The diffuse map alone is dark grey-brown (the shared head map averages (77, 69, 65) of 255).
4. **Hair, brows, beards** have shader type 6 (hair tint) and store the NPC's colour in the shader (`mHairTintColor`; 1,077 shapes of 316 files, 3 black, mean (0.41, 0.41, 0.39)).
5. Sources: the facts above are from the installed data (read only, aggregates). The Creation Kit wiki, xEdit and CommonLibSSE (allowed by the decision 0014 amendment) were NOT opened in this task; the formulas that are not in the data are marked RECALLED-VERIFY.

## Why the heads were dark and rough (cause)

- The heads of 0121 were only the NPC's **head parts** (eyes, brows, hair, beard): the face itself is not a head part, it is the facegen mesh, so there was no skin at all and the head looked like a few ragged alpha-tested hair cards.
- Hair and brow maps are grey masks that the game multiplies by the NPC's hair colour (shader type 6); the renderer drew them with a white material, so every hair was the same dull grey-brown. Fixed: the shader's colour is the material colour.
- The face's colour comes from the baked tint (slot 6), not from the diffuse map; drawn alone the map is dark. Fixed: diffuse * tint * 2 on the CPU (`NifScene::FaceTint::composite`; the 2 is RECALLED-VERIFY, judged by frames: the result is a plausible skin tone in the frames below; the game's own shader may use an overlay blend).
- The face's texture paths have no `textures\` root (slot 0) or a `data\` prefix (slot 6), so the plain lookup would have missed them: `texturePath` now normalises (lower case, forward slashes, no `data/`, with `textures/`). After normalising, 0 of 2,113 sampled facegen shapes name a diffuse texture missing from the archives.
- "Rough": not separately measured. The normal map (slot 1, model-space), skin map and specular are not used (out of scope); they are the next candidate if the faces still look flat.

## Design decisions

1. **SDK** (`actor_appearance.hpp`, DRAFT 0.1 to 0.2): `ActorPartKind::Face` appended. The facegen head is a part like the others (a path relative to the mesh root); the head parts it contains are not listed when it is present.
2. **Pack**: `faceGeometryPath(formId, LoadOrder)` (pure), `LoadOrder::nameOfId`, `FaceFileProbe` (the load order and an existence test) passed to `makeActorAppearance`; the file of the NPC whose **traits** the actor uses (a template's, when the traits are templated) is looked for; no file (or no probe) keeps the head parts. `SkyrimWorldView::describeActor` passes a probe built on `FileSource::exists` (new, with a default that reads; `VfsFileSource` overrides it with `hasFile`).
3. **Core, pure** (`components/nifscene/facetint.*`): `composite(base, tint, out, error)` and `normaliseTexturePath`. `Skinned::Shape` gains `mShaderType`, `mTintTexture` (face tint shader, slot 6) and `mHasTintColor`/`mTintColor` (hair tint shader).
4. **Host** (`skyrimmodel.cpp`): the diffuse and tint maps are read through the VFS, decoded (`vsgUtil::detail::decodeToRgba8`: BC1 to BC3, BC7, uncompressed), multiplied, given a mip chain and used as the shape's diffuse map; if either map cannot be read the plain diffuse map is used. The cost is on the main thread when an actor is built (a 1024 x 1024 decode and product per face; not measured separately).

## Stage log

### Tests, red first

- `apps/components_tests/skyrimse/testskyrimseappearance.cpp`: 11 new tests (path, owner lookup, face replaces head parts, no file keeps them, no probe, empty probe, template, path limit). `testfacetint.cpp` (9) and `testskinned.cpp` (6 new). Run in a private link (the test objects, the pack objects and `componentsrd.lib` of this build): against stubs (`faceGeometryPath` empty, `nameOfId` nullopt, `composite` refusing, `normaliseTexturePath` identity, the new fields never filled) **17 of 47 failed** (`red_run.txt`; the other 30 pass by absence or are the old tests); after the code 47 of 47. Hair tint tests: written after the code, then the code stripped: 1 of 16 failed (the other tests absence) and passed after it was restored (`red_hair.txt`). 49 of 49 pass at the end.
- Test-writing hazard met: this environment's shell collapses `\\` in heredocs, so path literals in the first version of the tests were wrong and were fixed with the editor tool (one test of the tint slot failed in the real-data run first: the tint is slot 6, not 4).
- Not red-first: the host glue in `skyrimmodel.cpp` (no unit test: it needs a device and the VFS; seen in game).

### Real data (five Skyrim SE masters; the gated `SkyrimAppearanceRealData`, aggregates)

The rest-pose metric is changed to the renderer's rule: skinned position = sum of weight * bone world * inverse bind * vertex, **no skin-data transform**; the expected position is the stored one with the skin transform inverted when the shape has one (Skyrim SE stores such shapes in the node's frame, a lift of about 120 units). It also prints the skinned height range per kind.

| Fact | Result |
| --- | --- |
| NPCs described / refused | 6,361 / 0 |
| Skeletons read | 52 of 52 |
| Rest pose, skin parts (6 parts, 4,514 vertices) | mean distance 7.8e-5 units, 0 over 5 units, skinned z -0.1 to 115.1 |
| Rest pose, armour (172 parts, 232,908 vertices) | mean 0.13, 9 over 5 units, z -1.7 to 145.8 |
| Rest pose, head parts (140 parts) | mean 109 (authored in head space: not a meaningful distance), z 97.9 to 137.0 (the head's height) |
| Skin bones found in the skeleton | 4,415 of 4,417 |
| NPCs with a facegen file / without | 4,669 / 1,692; distinct files 3,157; none with both a face and head parts |
| Facegen files read (every 10th, 316) | 316 readable, 0 without skinned shapes, 2,113 shapes, 316 face tint shaders, 316 tint textures present, 0 diffuse maps missing |
| Shader types of those materials | 0: 302, 1: 12, 4: 316, 5: 81, 6: 1,077, 16: 325 |

The previous (0121) metric applied the skin transform and printed 0.29 (skin) and 0.13 (armour); the new numbers are the same quantity under the renderer's rule (7.8e-5 and 0.13), plus the heights.

### In game (`tools/neutral_session_check/check.sh ... WhiterunBanneredMare 14000 n5actors`, `[Game] neutral actors = true`, `OPENEDEN_QUIET_WINDOW=1`)

- Six of six actors built: 6 of 6 with 4 or 5 parts drawn (skin, outfit, face), 0 unreadable, exit code 0, 0 dumps, 0 unexpected error lines. No "face tint not applied" line.
- Frames (`I:\Projects\OpenEden-work\n5faces`): `before\user\screenshots\screenshot000.png` against `after2\user\screenshots\screenshot000.png` (the barmaid: a floating hair-and-eyes head, then a face); crops `crop_before_0.png`, `crop_after_0.png`; sheets `sheet_after.png` (faces, hair untinted: grey hair), `sheet_after2.png` (hair in each NPC's colour) and `faces_closeup2.png`. The faces have skin tone, eyes, brows, hair; the armoured NPC shows a helmet (no head visible).
- Morrowind unchanged: `tools/worldobjects_check` (new build against `integ2\build`, which is near main): 111 result lines, `OK` (identical).

### Not run / open

- The Linux gate and GCC `-Werror` (Docker is down), clang-format 14, clang-tidy, the Lua tests, `openmw-tests` and `components-tests` as wholes (this build tree was made for `openmw` and a private test link only; the test objects of this task and the pack objects were built and run, the other tests were not), a fresh review.
- The factor 2 of the face tint and the hair tint rule are judged by eye, not against the game; hair colour was not compared with a real game frame.
- A light plugin's facegen name; a facegen file stored under an overriding plugin; skin tint (type 5, 81 shapes of the sample), eye shine (type 16), normal and specular maps; the neck is the body skin and may not match the face's tone exactly; the NPCs that have no facegen file (1,692 of 6,361; why was not examined) still show head parts only.
- Time of an actor build with a face (decode and product of the maps) was not measured.

### Why 1,692 NPCs have no facegen file (follow-up, 2026-11-12; investigation only, no code change)

An independent Python walk (not the pack's code) of the five masters' NPC_, LVLN, ACHR and RACE records and the facegeom listings of every Skyrim, Update, Dawnguard, HearthFires and Dragonborn BSA, applying the pack's rule (owner folder from the form id's master byte; the traits template followed while the ACBS template flag 0x1 is set, through NPC_ and nested LVLN lists). It reproduces the pack's numbers exactly: 4,669 with a file, 1,692 without (6,361 NPCs; the walk sees one more NPC_, the Player, which the pack does not describe). Aggregates only; the script is not committed.

| Class of the 1,692 | Count |
| --- | --- |
| Creature races (no human skeleton): Draugr 409, Falmer 90, Dragon 83, Skeleton 44, Horse 43, Wolf 39, Riekling 39, spiders, spheres, werewolves and others; 714 are placed by an ACHR, 743 never | 1,457 |
| Humanoid, never placed: the character-creation presets (`<Race><Sex>Preset NN`, 233 of them, e.g. 100 female presets) | 233 |
| Humanoid, placed, no template, no file (`DLC01SoulCairnSoulMorven`, `dlc2DBAncientDragonborn`; InvisibleRace) | 2 |

Checked and ruled out as lookup mistakes: (1) templated NPCs: the walk follows templates and levelled lists exactly as `ActorIndex::resolve` (a first version that ignored nested LVLN lists gave 2,026, which showed nothing but its own gap); (2) a facegen file in an overriding plugin's folder: 0 of 3,158 files belong to an (NPC, overriding plugin) pair that is not the owner pair, so the "not looked for" limit costs nothing on the five masters (a coincident local id in another folder, for example `0003b540.nif` under dragonborn.esm, belongs to Dragonborn's own `0403b540`); (3) file under another name or case: none; (4) only one facegeom file (`update.esm\00002ecf.nif`) is not claimed by any NPC. Light plugins: none among the masters, still RECALLED-VERIFY.

Conclusion: the lookup is correct. The remaining NPCs have no facegen in vanilla because creatures are not facegen-built and the presets are never placed; no NPC gains a face from a lookup change, so no code changed and no before/after frame exists. What the engine shows for them is what 0121 drew: their head parts (Draugr and Falmer heads come from their race and head parts; creatures have none). Not run: a game run of this follow-up (nothing to compare), the pack itself on the walk's list (the equality of counts is the check).
