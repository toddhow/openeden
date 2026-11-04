# AdvancedBodies in OpenEden: design

Status: draft for toddhow's review, 2026-11-02; B1 (task 0063) is built. **Direction change 2026-11-03: section 7A** (hand-modelling a full body is sidelined; the engine merges the legacy parts into one cached body instead). Text it supersedes is marked "Superseded by 7A" below and kept. Companion to `docs/design/Body-System-Design-Guide.md` (toddhow's guide: goals, principles, milestones B1 to B10; it stays the source for *what*), `docs/research/0027-advancedbodies-in-openeden.md` (facts from the code, the **verify** list) and CreativeMotion's `docs/design/advancedbodies-handoff.md` (decisions D1 to D7, written before OpenEden's architecture settled).

Authority: this document is *how AdvancedBodies fits OpenEden*. Where it disagrees with the guide, the guide's goals win and the disagreement is a row in the decisions table (section 17), not a silent change. Where it disagrees with the root `CLAUDE.md`, stop and ask toddhow.

Words (decision 0006): an **extension** is OpenEden's native code system; AdvancedBodies is **not** one, it is a **core system** like CreativeMotion (decisions 0001, 0003). A **plugin** is an ESP/ESM file. "Package" below means a **body package**, the guide's term. Every engine fact cites a file and function (see the research note) or is marked **verify**.

## 1. In plain language

Today a Morrowind person is a skeleton with a pile of separate mesh pieces hung on it, chosen by race and sex from records. You can change the face and hair; you cannot make someone taller, broader, thinner or different in shape, and clothes cannot fit different shapes.

AdvancedBodies gives every character **one body definition** that all systems read. The definition says which skeleton the body hangs on, what it looks like (meshes), how it can change (sliders), and optionally how big its parts are (capsules, measurements). Anything missing is *worked out* from what is there and labelled so. Then:

- **NPC variety:** each resident NPC gets slightly different proportions, chosen from a seed so nothing needs storing.
- **Customisation:** the player can change proportions with sliders (later, in game and in Eden Studio).
- **Clothing that fits** (later): garments are separate objects fitted to the body.
- **Cosmetic motion** (later): soft tissue and cloth that move but never push back on the game.
- **Animation that knows the body:** CreativeMotion asks the body for limb lengths and surface points instead of guessing.

Everything is **off by default**; off means vanilla Morrowind, compared, not assumed (root `CLAUDE.md`).

## 2. Where it lives

AdvancedBodies is engine code compiled into the game, in engine-independent libraries with thin engine adapters, exactly as CreativeMotion is (decision 0001).

| Part | Path | Depends on | Notes |
| --- | --- | --- | --- |
| Body contract: package model, validator, derivation, morph evaluation, selection, queries | `components/body/` | CreativeMotion `core` (math, format JSON, rig) only; no engine, no VSG, no Jolt, no ESM types | Lowercase file names (`bodypackage.cpp`): `CI/check_file_names.sh` needs no exception. New directory, so CMake target `eden_body` |
| Garments (B6 and later) | `components/garment/` | `eden_body` | Same rules |
| Cosmetic physics (B7 and later) | `components/softbody/` | `eden_body` | Own solver; no Jolt types in its headers (section 12) |
| Restricted tiers (B9) | `components/bodyscene/` | `eden_body`, `eden_softbody` | Separate target, optional at configure time (section 14) |
| Morrowind legacy adapter | `apps/openmw/mwrender/bodylegacy.{hpp,cpp}` | `eden_body`, `ESM::BodyPart`, `ESM::NPC`, `ESM::Race` | The only place that names Morrowind records; moves to the compatibility pack when root `CLAUDE.md`'s "move Morrowind rules out of the core" step happens |
| Engine driver | `apps/openmw/mwrender/bodydriver.{hpp,cpp}` | `eden_body`, `MWRender::Npc` | Builds baked bodies for an actor, hands them to `Npc`; the only code that touches both |
| Editor module | `editor_modules/advancedbodies_editor/` | Editor Module API, `eden_body` | Snake_case names allowed (the tree is excluded from the check); on the fixed core list (decision 0007) |
| Tools | `tools/bodyvalidate/` (command line) | `eden_body` | The validator as a program (B1) |
| Per-area guides | `components/body/CLAUDE.md` etc. | | Created by the first task that touches each area (root `CLAUDE.md`) |

The names `body/`, `garment/`, `softbody/` are the guide's module names. The guide says the modules are "engine-independent, with a host interface for rendering, assets and time". Concretely: `eden_body` reads bytes and plain arrays through a small `BodyHost` interface (`readFile`, `now` is **not** provided: no clocks, root determinism rule) and never includes a VSG, Jolt or `ESM::` header (root rule: contracts carry no third-party types).

**CreativeMotion is edited here** (decision 0010, item 20), so the parts of `kinematic/markers` the guide moves under the body contract can be moved by OpenEden task specs; they do not wait for the standalone repository.

## 3. Relation to CreativeMotion (handoff D1, D3, D7)

Rule of thumb: **the body says what the body is; CreativeMotion says how it moves.**

- **D1, source of truth of the physical body.** The package is the source of the *shape and size data* (measurements, capsule proxy, region map, mass distribution). CreativeMotion's rig (`opas::rig::Rig`: bodies, joints, limits) is **derived** from the package when the package does not supply one (guide principle 2, "authored, derived, missing"), and may be **authored** inside the package as the physics profile. CreativeMotion's profile (motor gains, balance, stepping) stays CreativeMotion's file; the package's `physicsProfile` entry only *references* a rig file and a profile file by path. Reason: motor tuning is animation, not anatomy. (Recommended; decisions table row H1.)
- **D3, where `kinematic/markers` goes.** Move **down** into `components/body`: body mapping (`BodyMapping`, `opas::markers`), `measureBody`/`BodyMeasurements`, `CapsuleFit`. Stay with CreativeMotion: interaction markers, alignment, pose clips, contact detection, automatic markers, presets (they are animation authoring). The dependency direction is then `kinematic/markers` → `eden_body` → CreativeMotion `core`, with no cycle because `eden_body` only needs `core/rig`, `core/math`, `core/format`. This move is its own task (B4's prerequisite, section 16) and is a CreativeMotion edit through an OpenEden spec.
- **D7, one bone-mapping format.** The package's skeleton binding **is** `opas.skeletonprofile` (`opas::anim::SkeletonProfile`, `BoneMapping`; the shipped `humanoid-to-eden.skeletonprofile.json` already maps 17 canonical bones to Morrowind's `Bip01 ...` names). It grows, by an additive version bump (v2), the fields the guide asks for: extra/unmapped bones, proportion data for retargeting, and tail/extra-limb chains. One reader, one writer (the existing canonical ones). `anim/clip` is a dependency of `eden_body` for this one type; if that is too heavy, the mapping type moves to `core/rig` in the same task (decision row H2).
- **Units and axes.** Packages are in metres, kilograms, radians, Z up, right-handed (CreativeMotion's convention). The Morrowind legacy adapter converts from Morrowind units at **one named function** (`bodylegacy.cpp`, `toBodyUnits`), with `Constants::UnitsPerMeter`, as the pose driver does (`mwrender/CLAUDE.md`, "Units").
- **CreativeMotion's assumptions** (unrotated rest frames, nothing rotating above the animation root, two box feet, handoff section 4) become *validator checks* on a package's skeleton, with the failing assumption named. Non-humanoids are legal packages; they simply lack the balance and stepping behaviours (the validator marks `balance`/`stepping` "not applicable", and the conformance suite includes a non-humanoid, section 15).

## 4. The body package

### 4.1 Form: folder source, `.edenpak` packed (guide open decision 2)

Recommendation: **both, folder as source, Eden Archive as packed form**, tied to the packer (task 0052).

```text
orc_male.body/                 source folder (diffable, editable)
  body.json                    format "openeden.body" v1, canonical JSON
  skeleton.skeletonprofile.json  bone binding (D7)
  mesh/                        render meshes, glTF .glb (or, for legacy, references)
  morphs/                      optional morph sets
  rig.rig.json, profile.profile.json   optional physics profile (CreativeMotion files)
  proxy/ regions/ landmarks/ soft/     optional parts
orc_male.edenpak               the same files packed (generic and mesh entries, content-hashed)
```

- The package hash is the **manifest digest** of the packed form (`docs/spec/eden-archive-format-v0.1.md` section 13.5), or, for a folder, the same digest computed over its files in sorted path order. One rule, tested to give the same value for a folder and its pack.
- `body.json` is explicit versioned JSON with a canonical writer that round-trips byte for byte and located errors (file, line, field): the project rule for new formats (root `CLAUDE.md`, "Authored data").
- Package contents follow the guide's table (section 4 of the guide) unchanged; required: skeleton binding, render mesh with LODs, metadata (version, author, licence, contract version, age class). Everything else optional and labelled `authored`, `derived` or `missing`.
- Meshes: glTF 2.0 `.glb` is the source of truth for new content (Eden Archive design section 9). Whether the engine can *render* from a glTF mesh today is **verify** (research note section 8); until it can, B1 to B5 packages reference meshes by VFS path with a `meshFormat` tag (`nif`, `gltf`), and the validator reads meshes through a small `MeshSource` interface, not a file reader of its own. Tests feed `MeshSource` synthetic arrays.

### 4.2 The package model in memory

A `BodyPackage` is plain data (vectors of arrays, strings, enums): `identity` (id, version, author, licence, contractVersion, ageClass), `binding`, `meshes[]` (LODs), `morphSet?`, `proxy?`, `physicsProfile?`, `measurements?`, `regionMap?`, `landmarks?`, `softTissue?`, `provenance` for each optional part (`authored`/`derived`/`missing`). Immutable after load; shared as `std::shared_ptr<const BodyPackage>` (the same lifetime pattern as other baked, cached resources; the comment in code states why).

### 4.3 Derived data and the cache (guide section 4, "Derive what is missing")

Derivation (B2) is deterministic: same package and parameters give identical bytes (no clock, no randomness, no unordered iteration, root rules). Derived data is stored in **derived packs** in the user data folder, exactly as the LOD baker does (`docs/design/eden-archive-format.md` sections 11 and 14.7; `docs/design/world-lod-and-streaming.md` 4.5):

- File: `<user data>/openeden/body/derived.edenpak` (flag `derived`). Entries are **generic, path-keyed** (`derived/<packageDigest>/<contractVersion>/<part>.json`), so **no new entry type or key tag is needed** in the container registry (`eden-archive-format-v0.1.md` section 11.2 and 11.3 stay untouched). Each entry carries the provenance record (source digest, tool id and version, parameters).
- Cache key = digest of (package digest, contract version, derivation tool version, parameters). A derived entry whose key does not match is **ignored and recomputed**; stale data is never used (world-LOD doc 4.6).
- Written like the baker does: complete files with new names, switched by a tiny pointer file replaced by atomic rename, read at start (world-LOD doc 4.7). The game never edits a file in use.
- The cache can be deleted at any time; derivation costs are measured in B2 and recorded, not guessed.

## 5. Query API (guide section 5)

The consumers' questions, in order of when they are built (B4):

- `measurements(body)`, `landmark(body, name)`, `regionOf(body, triangle)`.
- `closestSurfacePoint(body, boneLocalPoint, tier)` returning point, normal, bone weights; `raycast(body, ray, tier)`.
- Tiers mirror LOD: `Capsules` (crowds), `ProxyMesh` (near/interaction). Proxies are rebuilt when the body changes, not every frame.
- Queries take a **handle** (`BodyId` plus generation), never a pointer (root rule: handles, not raw pointers, for anything the engine can destroy) and are const and thread-safe on immutable baked data.

**Open decision 1 (guide section 11), scope of the surface API.** Recommendation: **animation and alignment only first**; physics collision keeps shapes derived *once per body at creation* and never changes mid-run. Reasons: physics determinism must not depend on morphs (guide principle 4; handoff D2); and decision 0003 says physics bodies are created by the core. A body slider change on a live actor therefore takes effect on the *render and animation* side at once and on the *physics capsule* at the next spawn (or next cell reload) unless toddhow later decides otherwise (row G1).

## 6. How body changes are evaluated at run time

### 6.1 Two tiers of change

1. **Proportions (skeletal).** A small set of numbers (height, shoulder width, hip width, limb length scales, torso depth) become **per-bone scale and offset** applied to the skeleton *before animation*, so *everything* on the skeleton follows: body parts, armour, hair, weapons in hand. This is a continuous function of the skeleton, so it **cannot open seams**. It needs the engine to accept non-uniform bone scale in the animation system: `Anim::Transform` scale support and CreativeMotion's "no scale in clips" assumption (handoff 3.8) are both **verify**; the fallback is applying the proportions in a post-animation pass over `BonePose`, like IK (`mwrender/CLAUDE.md`).
2. **Surface morphs (mesh).** Named morph targets (muscle, fat, breast/chest shape, face) that move vertices of the skin meshes. Applied only to skin parts the body owns; garments conform separately (section 9).

Tier 1 is the first target and works on Morrowind's assembled parts. Tier 2 comes with the weld scheme (section 7) or with whole-mesh bodies. **Update 2026-11-03 (7A):** tier 2 comes with the **merged legacy body** (the engine builds the whole-mesh body from the stock parts), and with modders' own full-body packages; the per-part weld scheme of 7.2 is the fallback if a merge is refused.

### 6.2 CPU reference first (B3)

A CPU evaluator turns (package, slider values) into a **baked body**: final rest vertex positions, normals and tangents for each LOD, plus recomputed measurements and capsules. It is the *reference*: the GPU path (6.3) must reproduce it within a stated tolerance, tested.

- **Bake once per unique body**, cache by key = hash(package digest, slider values quantised to a fixed step, contract version). Two NPCs with equal quantised sliders share one baked body (population variety therefore picks from a lattice, e.g. 8 steps per slider; step size is a named constant tuned by measurement, not guessed here). LODs are generated *after* morphing; normals and tangents recomputed (guide section 10).
- Bakes run on worker threads from sorted inputs; the result is immutable and handed to the main thread, which builds scene nodes (compile and detach are main-thread only, `mwrender/CLAUDE.md`). Thread count must not change bytes (tested, as the baker does).
- **No heap allocation in the per-frame update:** baking is not per frame. The per-frame body code (applying proportions, reading queries) uses preallocated buffers and has an allocation-counter test (guide section 12).

### 6.3 GPU path (describe, do not build)

Older GPUs and the first stages use the CPU bake. A later stage may evaluate morphs on the GPU for the *flexible* cases (live sliders in Studio, cosmetic blend-shape drive in B7):

- A **Slang compute pass** (decision 0009: new shaders are Slang; `openeden_add_slang_shader()`), run per visible actor body: inputs are the rest vertex buffer, a sparse morph-delta buffer (vertex index + delta per target) and a small weights buffer; output is a vertex buffer the existing skin vertex shader reads. Sparse deltas fix the memory problem of the existing dense `MORPH` storage buffer (`vec4` per vertex per target, `nif.cpp` `handleGeomMorpherController`).
- The existing `MORPH` path (`files/shaders/default/main.vert`) stays for animation-driven NIF morphs. It could carry a *handful* of live sliders with no new shader; that is a cheap Studio preview, not the game's design.
- Whether to run the pass on the graphics or a compute queue, and whether it is worth it against the CPU bake, is decided by a measurement stage (like the S0 stages of the LOD track), not here.

### 6.4 Velocity and the G-buffer (task 0026)

A baked body is static, so it adds **no** vertex motion beyond skinning, which already writes previous-frame matrices (`Anim::Skin::keepPreviousMatrices`). Live morph weights (B7 blend-shape drive, GPU path) *do* move vertices, so they must keep **previous-frame weights** the same way and the motion output must read both; a body whose weights changed this frame by more than a threshold (a slider drag) resets its history for one frame to avoid a smear. Rule: any new vertex-moving stage states where it stores last frame's values (mirrors the task 0026 "F4 must know" rule).

### 6.5 Material marks: PBR and subsurface skin

Skin is marked today by *model path* in `markSkinModel` (`npc.cpp`), which depends on load order (task 0027 stage log). A baked body part has no stable path, so: the **region map or the part role says skin vs garment**, and the body driver sets the subsurface flag on the part's material when it creates it (`Pipeline::Material::subsurface`, task 0027), eyes excluded by role. Legacy parts keep `markSkinModel` until the legacy adapter moves to the role-based mark; both give the same result, compared by the `subsurface` debug view (`gbuffer debug view = subsurface`, task 0027).

### 6.6 LOD tiles and clusters

Actors are never baked into world-LOD clusters or tiles (`world-lod-and-streaming.md` section 3.5, "dynamic: lights, actors"; task 0059 D1). AdvancedBodies keeps it that way: baked bodies are *actor-level* assets under the same memory budgets as other streamed assets (guide section 10), with their own cache; distant actors use the lowest body LOD and, when the world-LOD system adds impostors, a billboard per baked body key (not designed here).

## 7. The Morrowind assembled-parts problem

### 7.1 The problem

`Npc::updateParts` builds a person from ~20 separate part slots (research note section 1). A morph that moves vertices per part opens gaps wherever two parts meet (neck/head, wrist/hand, ankle/foot, upper arm/forearm, groin/legs), and armour or clothing that replaces a part does not follow the new surface at all.

### 7.2 Proposal: a virtual body, a weld map, and two strictness levels

> **Partly superseded by 7A (2026-11-03).** The *virtual* body (a package that only *describes* the parts) is replaced at run time by a **real merged mesh** that the engine builds from the parts and caches. Items 1 and 2 below describe what stage a of task 0069 measures and the merger computes (the weld map becomes `weldGroups` of the merged package); item 3 (the displacement field) is unchanged and still the rule; items 4 to 7 hold as written (strictness, head and hair, beasts, werewolf). The per-part weld without a merge remains the fallback if stage a says a joint cannot be merged.

1. **Virtual assembled body.** The legacy adapter reads the *chosen parts* for (race, sex, view) with the same selection logic as `getBodyParts`/`getHeadModel`/`getHairModel` (`bodyparts.cpp`; the adapter *calls* them, it does not copy them) and presents them to `eden_body` as one `BodyPackage` whose `meshes[]` are the parts, each tagged with its `PartReferenceType`, its attach bone (`MWAnim::sPartBones`) and its `region` and role (skin, hair, head).
2. **Weld map (derived data).** At derive time (B2), the adapter finds boundary vertices of different parts that coincide at the bind pose (within a named tolerance, a constant in `bodylegacy.cpp` tuned against the reference body, not guessed here) and groups them. The derived data is a list of weld groups. Whether stock parts have exactly coincident boundary vertices or only overlap is **verify**; the derivation reports a *seam quality* number per seam so the answer is data, and if parts only overlap, groups are formed by nearest-neighbour on the overlap ring with a maximum distance.
3. **Morph evaluation respects the weld.** A surface morph is evaluated on the *virtual body* as a displacement **field of rest position** (a smooth function sampled where each vertex lies), not per-part deltas. Coincident vertices have the same rest position, so they get the same displacement and the seam stays closed. Authored morph targets that are per-part (modder content) are validated: delta at a weld group must agree across parts within a tolerance, or the validator reports the seam (guide's validator: "extreme slider values do not invert triangles" plus a new "no seam opening" check).
4. **Strictness (legacy adapter strictness, decisions table row H4).**
   - *Strict (recommended first)*: legacy equipment (armour, clothing parts) is **never morphed**; it follows only tier 1 skeletal proportions. The surface morphs of a region are **limited** (or disabled) when a legacy item covers that region, because the armour surface will not move with the body. The covered region is known from `Npc`'s `mPartPriorities` (a part with priority above 1 is equipment). This matches the guide's "strict exclusion" for legacy items and keeps old content working.
   - *Fitted*: later, B6 wraps legacy items as garments with derived coverage and conform them by cage deformation (guide section 7).
5. **Head and hair.** The head is a part like the rest, plus hair attached to the head bone; a head-size slider is tier 1 (bone scale on "Head"), so hair follows. Face morphs are a later set on the head part only, welded to the neck.
6. **Beast races, tails.** Tail is a part type (`MP_Tail`); the weld map treats it as one more part. Beast races use different skeletons; they are separate packages with their own skeleton binding. Not special in the contract (guide: non-humanoids are ordinary packages).
7. **Werewolf and vampire.** Werewolves use a different model set (`getBodyParts(... werewolf)` returns empty; `getHeadModel` special case). The adapter marks them `bodyClass = creature` and the body system leaves them alone in the first stages (no sliders). Vampire heads follow the head selection.

### 7.3 What the reference body is

For B1 the reference is "the default human male and female body parts" of Morrowind: the set `getBodyParts` returns for a given human race and sex. Tests use *synthetic* `ESM::BodyPart` records and synthetic part meshes (no Bethesda files in the repository, root guardrail); a written, repeatable in-game check uses the user's own data.

## 7A. The merged legacy body (direction of 2026-11-03, task 0069)

### 7A.1 In plain language

toddhow is not a modeller and will not hand-model a full body. So nothing has to be modelled or shipped: **the engine builds the one-piece body itself from the parts the player already owns.** It finds the parts a race and sex use, reads them, joins the vertices where parts touch, keeps the skin weights, and sorts the triangles by body region. The result is saved in the player's own data folder as derived data (the same rule as the LOD bakes: it is made from the game's meshes, so it is never committed and never shipped). A modder who has made a real full-body mesh supplies it as an ordinary body package and skips the merge. The merged body is also what makes surface sliders (tier 2, section 6.1) possible: one mesh has no gaps between parts.

Facts, with file and function, are in `docs/research/0032-merged-body-from-legacy-parts.md`; the staged plan is `docs/tasks/0069-advancedbodies-merged-legacy-body.md`. Every statement here that needs the user's own data is marked **verify** there and is settled by stage a (a read-only measurement), not by guessing.

### 7A.2 What changes and what does not

| Topic | Before (7.1, 7.2) | Now |
| --- | --- | --- |
| The body at run time | ~20 stock parts, a virtual package describes them | One merged skinned mesh per (race, sex, third-person view), neck down; head and hair stay parts |
| Seams | A weld map, per-part meshes untouched | The merge welds coincident vertices; UV and normal splits stay as `weldGroups`; the displacement field (7.2 item 3) still keeps duplicates together |
| Where it comes from | The legacy adapter reads the parts at each use | The adapter reads them **once** at bake time; the cache holds the result |
| Equipment | A part with higher priority replaces a part | A region of the merged body is hidden (a contiguous triangle range, switched off); armour parts stay separate skinned parts as today |
| Strictness | Strict: legacy equipment never merged or morphed | Unchanged |
| Beasts, werewolf, creatures, first person | Beasts one more package, others left alone | Humans first; beasts if stage a shows the same structure; werewolf, creatures and first person stay stock |
| Modder's own full body | A package with `meshes[]` | The same package, same loader, same validator, same renderer path, **no merge step** |
| Shipping | Nothing legacy is shipped; B1 had no mesh | Nothing is shipped; the merged mesh is a local derivative |

### 7A.3 The pipeline

1. **Select** the stock parts for (race, sex, third person) with the engine's own functions (`getBodyParts`; the adapter calls them, as in B1).
2. **Read** each part with a skinned NIF reader over `components/nif` (`components/bodynif`, no engine types): vertices, normals, UVs, triangles, per-bone weights, bone names, bind matrices, material facts, shape names for the skin filter.
3. **Merge** (a pure library in `components/body`): one bind space; weld within a data-derived tolerance; renormalise weights; canonical bone list; one submesh per material key; triangles sorted by region; fixed order everywhere (research note 0032 section 2).
4. **Validate** with B1's validator, extended with the new `bodymesh` mesh format and the `weldGroups` rules. A merge that fails validation is never cached.
5. **Cache** in `<user data>/openeden/bodies/` as a derived Eden Archive pack, keyed by the part byte hashes, the records, the skeleton and the merger version; written whole, published by atomic rename, stale entries ignored (section 4.3 and the LOD design 4.5 to 4.7).
6. **Draw** behind `[Bodies] merged body` (off by default): one node with the same skin controller bound by bone name, region draws under switches, the subsurface mark from region roles (6.5); any failure draws the stock parts.

### 7A.4 What it costs and what it must not break

Costs are **measured, not promised** (stage d records them): per-person part reads and attaches (the load-trace scopes `npc.part.read` and `npc.part.attach`), draw counts, memory. Off means unchanged, compared byte for byte, not "within noise". The merged and stock bodies are compared numerically with the software skin at five poses and by viewed difference images; the merged body may differ from stock only at weld groups whose stock weights disagree, and only by the measured amount.

### 7A.5 Open questions for toddhow (each with a recommendation)

1. **Head** (H14): merge the body neck down and keep head and hair as parts? *Recommend yes.* Merging the head multiplies bodies by the number of head records per race and sex (stage a counts them).
2. **When to build** (H15): ahead of time with a bake command plus a background worker, and an actor whose entry is missing at creation draws stock for its whole life (no live swap)? *Recommend yes.* Alternatives: block the loading screen, or swap a live actor (a visible pop).
3. **Cache payload** (H16): a small canonical `bodymesh` layout in the derived pack? *Recommend yes* (the engine can build a node from arrays today; glTF mesh rendering is unverified). The layout is a draft that freezes only after your approval, like the tile and cluster layouts. Alternative: write glTF `.glb`.
4. **Seam weights** (H17): the equal-weight mean of a weld group's members? *Recommend yes.* Alternative: one owning part per seam. Stage a measures how much the stock weights disagree; if it is zero this choice does not matter.
5. **Scope** (H18): humans first, beasts only if the structure matches, no werewolf, creature or first person? *Recommend yes.*
6. **Textures** (H19): one submesh per material, no atlas? *Recommend yes* (no block-compression encoder in the project; the draw count is measured, not assumed).
7. **Where the NIF reader lives** (H20): a new `components/bodynif`? *Recommend yes* (the bake mode and the tests need it without the engine).
8. **Runtime check of the cache:** the game trusts the bake's `current` pointer and compares a key made from the ESM selection and file sizes, without re-hashing model bytes at start (H15 note). *Recommend yes;* re-hashing on every start costs reading every part file. A changed mod is caught by the key's record and size parts; a same-size same-path byte change is caught only by running the bake again.
9. **A bake after a load-order change** (decided as in the LOD design 4.6): the status line says "bodies out of date, run --bake-bodies"; the game draws stock meanwhile. *Recommend yes.*
10. **Stage order:** stage a (measure) alone first, then your go/no-go on its table before stage b starts. *Recommend yes.*

### 7A.6 Answers of toddhow, 2026-11-03

toddhow answered "all yes" to the ten questions of 7A.5: every recommendation is accepted. In particular: H13 (merge from the player's own parts, cache, never ship) stands; H14 neck down, head and hair stay parts; H15 ahead of time with a bake command plus a background worker, stock parts for the whole life of an actor whose entry is missing; H16 a small canonical `bodymesh` layout (draft until he approves it); H17 the equal-weight mean of a weld group's members; H18 humans first, beasts only if the structure matches, no werewolf, creature or first person; H19 one submesh per material, no atlas; H20 `components/bodynif`; the runtime cache check compares ESM records and file sizes without re-hashing; a load-order change makes the game draw stock parts and say so; stage a runs alone and he reviews its go/no-go table before stage b starts.

## 8. Selection, replacement, priority, conflict log, validator, fallback (guide section 6)

- A package declares which race, sex or creature type it replaces and a numeric priority. Resolution picks the highest priority for a target; ties are broken by package id (stable sort, deterministic). Every conflict is written to a log file (one line per losing package with the reason) in the user data folder, and shown in the launcher's content page later.
- **Fallback.** A package that fails validation is rejected with a located error and the *default body* (the legacy adapter) is used; a bad package must never crash a load or break a save (guide section 6). The validator's report is available as a text file and in the in-game log.
- **Validator** (B1): command line `tools/bodyvalidate` plus an in-engine call returning the same report. Checks are the guide's list, grouped into: *structure* (versions, required parts), *skinning* (weights sum to 1 within a named tolerance, bones exist and are mapped, at most N influences per vertex), *geometry* (bounds and proportions in the retarget range, region coverage without gaps, morphs reference valid vertices, no inverted triangles at slider extremes, no seam opening), *physics* (shapes cover the skeleton, masses positive, limits agree with the rest pose), *CreativeMotion assumptions* (section 3), *age class* (section 14). Each finding has a code, severity and path (`/meshes/0/weights/1234`).
- **Versioning.** The contract has a version; additive changes keep it, breaking changes bump the major. A **conformance suite** of reference packages and queries (a humanoid and a non-humanoid, both synthetic) must keep passing on every contract change (guide sections 6 and 12). Contract headers carry no implementation (root rule).

## 9. Layered clothing (garments), later (B6)

Kept as the guide describes (section 7), with OpenEden specifics:

- A garment is its own object (`openeden.garment` v1, `.garment.json`, the format already named in the Studio plan section 7), with slot and layer, coverage regions, attachment type, states, fit data, material and condition.
- **The legacy system keeps working unchanged.** The legacy wrapper presents a legacy item as a single-layer garment in its traditional slots (reading `ESM::Armor`/`ESM::Clothing` part lists as `Npc::updateParts` does). Overlap between legacy and layered items is *strict exclusion by region* by default (guide open decision 5, row G5): partial overlap needs coverage data the legacy mesh lacks.
- Equipped items are not stored by the body system: inventory and equipment already live in `InventoryState` (`components/esm3/npcstate.hpp`). Garment *states* (buttoned, sleeves up) are a small per-actor record in the same body save record (section 11), only for actors that use layered garments.
- Rendering (guide section 7, "Saves and rendering"): near actors merge static layers into one skinned mesh and atlas; crowds bake the outfit once and instance it; dirt/wetness/damage are shader parameters. The merge is a derived-data bake keyed like the body bake.
- Conformance to bodies uses authored fit data first, cage or radial-basis deformation as fallback, then a penetration clean-up, as the guide says. Hair, hats, hoods get explicit rules.

## 10. Cosmetic physics, later (B7 to B9; guide section 8 and open decision 6)

- **One-way, outside gameplay.** Cosmetic motion reads the animated skeleton (and the simulated skeleton in ragdoll) and writes only to *render-side* data: extra spring-bone transforms and blend-shape weights. It never writes the skeleton, collision, AI or saves (guide principle 3). This is consistent with decision 0003: it does not create, move or step Jolt bodies.
- **Cosmetic world (decision row G6): a dedicated solver outside Jolt**, recommended, for these reasons: (1) decision 0003 says exactly one Jolt world and the core owns it; a second `PhysicsSystem` inside AdvancedBodies would contradict the spirit of "never a second Jolt world" (root, M10 item); (2) the cosmetic layer must be droppable when frame time is tight and must not share locks with the async actor simulation (`mtphysics.cpp`); (3) spring bones and position-based cloth are small. Jolt's soft-body support exists (since v4.0.0; present in the pinned v5.5.0 tree, research note section 6), and whether it could serve *region-level* soft bodies with skinned vertices is **verify** by a prototype in B9; if it does, it would be a separate library target using Jolt directly, not the gameplay world. The recommendation can change on that evidence.
- **Spring bones and blend-shape drive (B7)** are the cheap baseline: fixed substep rate independent of frame rate, clamped displacement/velocity/force, reset on teleport, cell change and animation snaps, running through ragdoll (guide "Stability"). Allocation-free per frame, counted in tests.
- **Cloth (B8)** collides against rest-shape capsules/proxy from the body query API (section 5), never the deformed surface.
- **Tier handoff (B9)** follows the guide: initialise the expensive tier from the cheap tier's current displacement and velocity, blend over a few frames, calibrate the cheap tier against the solver offline.
- **Determinism.** Cosmetic output is allowed to be non-bit-exact across platforms because it never feeds gameplay; it is deterministic on one build and thread count in tests, and the tests assert "no feedback": gameplay state hashes are equal with the layer on and off.
- **Threading.** The layer runs on a worker after animation and before skinning upload; it touches only its own actor's data; which thread each callback runs on is stated in each PR (root rule).

## 11. Saves (guide section 10, decision 0005)

Facts: residents' look is not saved (`NpcState` has no appearance); the player's look is a dynamic `ESM::NPC` named "Player" written by `ESMStore::write`; unknown records in a save are skipped with a warning (research note section 4). Extensions never touch saves (decision 0005); AdvancedBodies is core, so it may.

Recommendation (row H5):

- **Residents and every NPC from content files: store nothing.** The body is *seeded* from the actor's ref id and a world salt (a setting, default fixed), so the same save always gives the same body (determinism), and a new game gives the same bodies as an old one. Cost: if a mod changes the NPC's race/records the body changes; that is the same behaviour as appearance today.
- **The player and customised actors: one small core save record**, `BODY` (a four-letter id to be checked against `components/esm/defs.hpp`, **verify**), a table of `{refId or generated id, bodyId, sliders (quantised), garment states}`. Few bytes each. A save without the record loads exactly as before; a build without AdvancedBodies skips the record with a warning (the 0005 precedent). Changing `ESM::NPC`'s own subrecords is rejected: it is inherited format and other tools read it.
- Dynamic NPCs created at run time (Lua spawns, leveled lists) get a seeded body from their generated id, which is stable within a save.
- A package id that no longer exists on load: fall back to the legacy body and log (guide section 6).

Equipment is not duplicated (it is in `InventoryState`).

## 12. Population generator and `.character.json`

- **Seeded variety (no storage).** A `PopulationRules` document (`openeden.population` v1, JSON, e.g. the Studio plan's `NordBlacksmith` archetype with `muscularity`, `bodyFat`, `height` ranges and a `styleProfile`) maps *factions, races, classes, regions* to slider ranges. The generator is a pure function: (rules, actorKey, salt) → slider values, using a counter-based hash, never a global RNG, never iteration over unordered maps. Output is quantised to the bake lattice (section 6.2), so towns of 200 NPCs share a few dozen baked bodies.
- **Matching within the lattice** also bounds memory: the maximum number of live baked bodies is a setting with an LRU eviction (cost measured in B3).
- **`.character.json`** (`openeden.character` v1, the format the Studio plan assigns to Reflectron, section 7 of the plan): appearance, morphology (body id + sliders), face sculpt, style references. Decision row H6: the **core** owns the *body part* of this format (`body` object: `bodyId`, `sliders`), because the body system must read it for the player and customised actors; Reflectron (an extension) may write and extend the rest and can only hand over a character file the core reads; extensions have no save access (decision 0005) and no physics writes (0003). Until Reflectron exists the Studio module and the in-game customiser read/write the same file.
- The population panel runs as a cancellable background task (Studio plan section 3.11).

## 13. Eden Studio module (`advancedbodies_editor`)

A separate editor library on the fixed core list (decisions 0007, 0031 Editor Module API), modelled on `creativemotion_editor`: no Qt, no Studio type, no engine type, property ids as strings (`slider.<name>`), edits that would make an invalid package refused by running the validator, one conversion header. Panels (Studio plan section 5.2):

1. **Morphology Sliders**: live preview in the viewport. Uses the CPU bake for committed values and the existing dense `MORPH` path (or the future compute pass) for dragging; frames checked against the CPU reference.
2. **Population Generator**: rules editor + "generate N" with a histogram; background task.
3. **Character Profiles** (`.character.json` asset type).
4. **Body Contract Views** (overlays): capsules, proxy, regions, landmarks, weld seams and seam quality, measurements (guide's "debug view"). Needs the mesh viewer (task 0043) to load a body mesh and a debug-draw path for overlays.
5. **Profile / package editor**: the package folder as an asset; authored/derived/missing badges per part; validator report panel.
Nothing of this exists until Studio has loaded a module of this kind in practice (task 0031/0032 are the precedent); this is B10 work.

## 14. Adult gating (guide section 9; architecture only)

This section describes the *mechanism*. No document, sample, test or fixture in the project contains adult content; fixtures are neutral humanoid and non-humanoid bodies; tests exercise the gate with a stub tier.

- **Age class** is an enum on the package metadata (`unspecified`, `minor`, `adult`) **and** on the actor's answer from the game data (handoff D5: the actor's answer wins; a package cannot mark itself adult onto a child race). For Morrowind the actor's age answer comes from a compatibility-pack function; whether Morrowind data distinguishes children at all is **verify**. Rule: anything other than an explicit `adult` from *both* sides is **not adult**, and unknown means refuse.
- **The gate is in code, not a setting.** `eden_body` exports a single function that decides whether a restricted tier may run for an actor; the tier library cannot start without a value it returns. The validator rejects packages that enable restricted tiers without `adult`. The setting only switches the feature set on; it cannot widen what the gate allows.
- **Modular and removable.** The restricted tiers live in their own target (`components/bodyscene/`). Recommendation (row G7): a CMake option `OPENEDEN_WITH_RESTRICTED_BODY_TIERS`, **default OFF**; with it off the library is not compiled or shipped and the setting does not exist. The base body, garment and cosmetic systems have no dependency on it, so removing it removes nothing else.
- **Off by default**: a separate setting `[Bodies] restricted tiers = false`.

## 15. Testing

Following the guide's section 12 and root gates:

- **Unit/property tests:** package reader/writer round trip (byte-identical), every validator rule has a passing and failing fixture, morph then measure within tolerance, limb lengths/volumes/symmetry, no inversion at slider extremes, seam closure (distance between welded vertices stays within tolerance after any slider combination), energy bounded, displacement within limits.
- **Conformance suite:** `data` fixtures generated in code or small synthetic files: a humanoid (about 20 bones), a quadruped or tailed non-humanoid, a legacy-assembled body made from synthetic `ESM::BodyPart` records. Run for every contract version.
- **Determinism:** same inputs give equal bytes at 1 and 4 threads; folder digest equals pack digest; derived cache keys change when inputs change.
- **Allocation:** per-frame calls counted with the existing counting-allocator pattern.
- **Off means unchanged:** with every `[Bodies]` setting at its default, the NPC build is the same call sequence: compared by a recorded part list per NPC (unit) and by frames of a fixed scene with `diffmap` (as other renderer tasks), plus the 32 Lua tests.
- **Visual review:** debug views and a repeatable test scene (a Lua spawn rig in front of a static camera, as task 0033 used).
- **Fresh reviewer** for every stage (full loop, root `CLAUDE.md`: this is a new data model, derivation, morph application, loader of untrusted package files, saves).

## 16. Threading, determinism, allocation, settings, tiers

- **Threading.** Package loading, validation, derivation and baking: worker threads; results immutable (`std::shared_ptr<const ...>`). `MWRender::Npc` changes, scene-graph creation, compile and detach: main thread only (`mwrender/CLAUDE.md`). Package files are untrusted input: every size/offset/index checked, decoded sizes capped, a bad package never crashes (same stance as the container reader, world-LOD doc 9).
- **Determinism.** No wall-clock, no unseeded randomness, no unordered iteration where order matters; seeded variety by a counter hash; bake bytes equal across thread counts on one build (cross-machine bytes not promised, as the baker).
- **Allocation.** No allocation in per-frame body code.
- **Settings** (a new `[Bodies]` section, all off): `enabled`, `legacy adapter` (builds legacy packages and runs the validator without changing visuals; useful as a safe first step), `npc variety`, `variety strength`, `variety salt`, `body sliders` (player), `cosmetic physics` (B7), `restricted tiers` (B9, only if compiled), `baked body cache size`. `enabled = false` = no code runs, no file opened.
- **Graphics tiers (older GPUs).** Following the LOD design's tiering (`world-lod-and-streaming.md` section 8): tier 0 = vanilla; tier 1 = CPU bake once per unique body, skeletal proportions, no cosmetic physics; tier 2 = + spring bones; tier 3 = + GPU blend-shape drive (needs compute); tier 4 = + cloth/soft-body region solver. A missing feature turns off that feature only. Weak GPUs are tested by emulation (decision 0010 item 27).
- **Budgets.** Each tier gets a recorded per-character cost in the benchmark harness; the layer drops to the cheaper tier when frame time is tight (guide section 10).

## 17. Staged spec plan (guide B1 to B10 to task numbers)

Task numbers are reserved from 0063. Every stage is **full loop** (root `CLAUDE.md`): the data model, derivation, morph application, solvers, a loader of untrusted files and saves all carry the risks the full loop exists for. The lighter loop may apply to editor UI only, when toddhow marks the spec lighter.

> **Numbering note (2026-11-03): the table below is stale from B2 on.** Tasks 0064 (VFS pack placement), 0065 (CreativeMotion push sandbox), 0066 (whole-map view), 0067 (CreativeMotion runtime), 0068 (far water) and **0069 (the merged legacy body, section 7A)** were taken by other work, so the numbers 0064 to 0075 in this table are only the *order*. Later stages take the next free number when they are written, and this table is re-numbered then. The merged legacy body (task 0069) comes before B3: B3 (sliders, tier 2 surface morphs) depends on it instead of on the virtual assembled body.

| Task | Guide | Stage | Depends on |
| --- | --- | --- | --- |
| **0063** | B1 | Body contract: package format, validator, legacy adapter for the reference body, conformance suite (draft written) | none (0050 to 0052 if packed-form tests are included in B1b) |
| 0064 | B2 | Derivation (capsules, measurements, regions, physics profile) + derived cache + determinism tests; moves `BodyMapping`/`measureBody`/`CapsuleFit` under the body (D3) | 0063; 0052 (derived pack writer) |
| 0065 | B3 | Sliders, proportion adaptation (tier 1 skeletal), CPU morph application, weld map and seam tests; first visible result: NPC variety behind `[Bodies] npc variety` | 0064; 0043 (mesh viewer to inspect bodies) |
| 0066 | B4 | Surface query API at each proxy tier; IK/alignment integration; records decision G1 | 0064; CreativeMotion task for the markers move |
| 0067 | B5 | Selection, priority, conflict log, fallback; the `BODY` save record; player customisation hooks; population generator + `.character.json` body part | 0063, 0065; 0053 style layer unaffected |
| 0068 | B6 | Garments: format, layers, legacy wrapper, fit and states | 0064, 0065; 0046 (NIF editing) for authoring legacy conversions |
| 0069 | B7 | Cosmetic baseline: profile in contract, spring bones, blend-shape drive, reset/clamp/ragdoll handoff | 0065, 0066; CreativeMotion M10 physics adapter (task 0035) for ragdoll |
| 0070 | B8 | Clothed physics: tight-fit deformation, cloth against rest-shape colliders | 0068, 0069 |
| 0071 | B9 | Scene tier solver, tier handoff and calibration, contact hints, adult gating enforced and tested (target `bodyscene`, optional) | 0069, 0066 |
| 0072 | B10a | Render paths: baked + merged bodies, GPU morph compute (Slang), velocity/previous weights, mesh cache streaming | 0065; 0049 (texture downscaler for body textures); 0060 (streaming core) |
| 0073 | B10b | `advancedbodies_editor` module: sliders, population panel, debug overlays, validator panel | 0031, 0043, 0065, 0067 |
| 0074 | B10c | Importers (own readers, documented formats only; presets as data; no bundled third-party content) and licence check | 0063; verify list of the research note |
| 0075 | B10d | In-game customisation UI (replaces the discrete head/hair choice in `MWGui::RaceDialog` with sliders when `body sliders` is on; the vanilla dialog stays when off) | 0067 |

The first *visible* milestone is B3 (0065). B1, B2 produce no visible change by design and are the safe foundation.

## 18. Decisions for toddhow

Options with a recommendation first. Guide decisions G1 to G6 are the guide's section 11; D1 to D7 are the handoff's; H1 onward are new.

| # | Decision | Recommended | Alternatives |
| --- | --- | --- | --- |
| G1 | Surface query API scope (guide 1; handoff D2) | Animation and alignment only first; physics shapes derived once per body at creation, never changed mid-run | Share one source with physics and combat hit detection (cleaner, ties determinism to body changes) |
| G2 | Package form (guide 2) | Folder source + `.edenpak` packed form, one digest rule, packer from task 0052 | Archive only; folder only |
| G3 | Soft-tissue skeleton (guide 3) | Keep the skeleton unchanged for packages made from Morrowind parts; allow *optional* extra spring bones in packages that supply them (the cheap tier needs them) | Always expose extra bones; deform only through morphs |
| G4 | Soft-body morph reuse (guide 4) | Reuse the blend-shape tier's morphs first (consistent look, simple calibration); a separate simulation mesh only if B9's prototype demands it | Separate simulation mesh with a mapping layer |
| G5 | Legacy and layered overlap (guide 5) | Strict exclusion by region; partial overlap later with derived coverage | Partial overlap from the start (needs coverage for legacy meshes) |
| G6 | Cosmetic world (guide 6) | Dedicated solver outside Jolt; revisit after a Jolt soft-body prototype (**verify**) | A separate Jolt `PhysicsSystem` (contradicts "one Jolt world" in spirit) |
| D1 | Source of truth for the physical body | Package supplies shape/size data; CreativeMotion rig derived or authored inside it; motor profile stays CreativeMotion's | Rig is the source; profile merged into the package |
| D3 | Where `kinematic/markers` goes | Measurements, body map, capsule fit move under `components/body`; markers/alignment stay with CreativeMotion | Everything stays in CreativeMotion |
| D4 | Order | B1 to B5 first (contract, derivation, sliders, query, selection), then garments and physics after the renderer parity work | Garments earlier |
| D5 | Adult gating checks the actor too | Both package and actor must say adult; anything else is refused; gate function in `eden_body` | Package only |
| D6 | = G6 | | |
| D7 | One bone-mapping format | `opas.skeletonprofile` grows (v2) into the package's binding; one reader | Two formats with converters |
| H1 | What CreativeMotion's profile file owns | Motor/balance/stepping stay in the profile file; package references it | Fold motor defaults into the package |
| H2 | Where the bone-mapping type lives | Keep in `anim/clip` and depend on it; move to `core/rig` only if the dependency is too heavy | Move now |
| H3 | **First target** (new) | **NPC variety** through seeded proportions (tier 1): visible, no UI, no storage, works on Morrowind's assembled parts without seam risk. Player customisation follows as B5/B10d; clothing fit (B6) after | Player customisation first (needs a UI and the save record); clothing fit first (needs bodies to fit) |
| H4 | Legacy adapter strictness (new) | Strict: legacy equipment is never morphed; surface morph amplitude limited where equipment covers a region; fitted legacy items later in B6 | Fitted from the start; or no surface morphs at all on legacy bodies (proportions only, forever) |
| H5 | Save format (new) | Nothing for seeded residents; one small `BODY` record in `.omwsave` for the player and customised actors; do not touch `ESM::NPC`'s own subrecords | Add subrecords to the dynamic NPC record; or store every actor |
| H6 | Who owns `.character.json` (new) | Core owns the body part of the file; Reflectron (extension) may extend the rest | Reflectron owns it all; core reads nothing |
| H7 | Morrowind-first scope (new) | B1 to B5 target the Morrowind legacy adapter and a glTF-based package; New Vegas waits for its pack (**verify** list in the research note) | Design the loader abstraction for New Vegas now |
| H8 | Where restricted tiers live (new) | Separate target, CMake option default OFF, not built or shipped by default; gate in the always-built body library | Always built, setting off by default; or a separate repository |
| H9 | Importer scope and licences (new) | Own readers from documented formats; presets are data and are never bundled; BodySlide's GPL-3.0 code is not copied (check each header: GPL-3.0-only vs "or later") | Port parts of BodySlide with attribution after a licence review |
| H10 | Skin mark by role instead of path (new) | Yes, in B3/B10a; legacy keeps the path mark meanwhile | Keep path marks and accept the load-order risk |
| H11 | Vulkan morph compute | Describe now, measure later (B10a); CPU bake is the default | Build the compute pass in B3 |
| H12 | Bone scale vs post-animation pass for proportions (**verify** first) | Skeleton bone scale if `Anim::Transform` supports it; otherwise a `BonePose` pass | Always the pass |
| H13 | Direction of 2026-11-03: no hand-modelled body (new) | Merge the legacy parts programmatically from the player's own data, cache the result as derived data, never commit or ship it; modders supply their own full body as a package, no merge step (7A, task 0069) | Hand-model one body (toddhow is not a modeller); ship a converted body (a derivative of the game's meshes) |
| H14 | Head in the merged body (new) | Neck down; head and hair stay parts (the head is chosen per NPC record) | Merge per head record (bodies multiply by the head count; stage a counts) |
| H15 | When the cache is built (new) | Ahead of time with `openmw --bake-bodies` plus a background worker; an actor whose entry is missing at creation draws stock parts for its whole life; the runtime trusts the bake's `current` pointer and compares a key from the ESM selection and file sizes | Build in the loading screen (blocks); swap a live actor when the entry arrives (a visible pop); re-hash every part file at every start (slow) |
| H16 | Cache payload (new) | A small canonical `bodymesh` layout in the derived pack, a draft until toddhow approves it (like the tile and cluster layouts) | glTF `.glb` through a new writer (rendering glTF meshes is unverified) |
| H17 | Skin weights at a seam (new) | Equal-weight mean of a weld group's members, renormalised, top 4, ties by canonical bone order | One owning part per seam |
| H18 | Scope of the merge (new) | Humans first; beasts only if stage a shows the same structure; no werewolf, creature or first person | Merge every race now; include first person |
| H19 | Textures in the merged body (new) | One submesh per material key, no atlas | A texture atlas (needs a block-compression encoder) |
| H20 | Where the skinned NIF reader lives (new) | `components/bodynif` over `components/nif` only | Inside `bodylegacy.*` (unusable by tools and tests without the engine) |

## 19. Risks

1. **Seams** (section 7): the weld map depends on how stock parts meet, which is unknown until looked at with the user's own data. B2 reports it before B3 relies on it. **Update 2026-11-03:** the merge (7A) makes this the gate of task 0069: stage a measures every seam and ends in a go/no-go table before the merger is written.
2. **Bone scale** may not be supported in animation (**verify**); the fallback exists but is less elegant.
3. **Cache blow-up**: one baked body per unique slider set; the lattice and the LRU bound it; measure in B3.
4. **Format sprawl**: `openeden.body`, `.garment.json`, `.character.json`, population rules, skeleton profile v2. Keep one canonical writer and one validator per format; the existing JSON support in `core/format` is the base.
5. **CreativeMotion docs/rig sample** not yet in this repository (research note section 5).
6. **Scope**: the guide is ten milestones; only B1 to B3 are specified in depth. The rest are placeholders until B3 teaches us the real costs.

## Changelog

| Date | Change |
| --- | --- |
| 2026-11-02 | First draft, with research note 0027 and task 0063 (draft) |
| 2026-11-03 | Direction change (toddhow): no hand-modelled body; merge the legacy parts programmatically and cache the result (section 7A, decisions H13 to H20, research note 0032, task 0069 draft). Superseded text marked in 6.1 and 7.2; numbering note added to section 17 |
