# Task: CreativeMotion becomes the engine's full animation runtime; retire the COLLADA animation path
Status: APPROVED by toddhow 2026-11-11 ("Go ahead and start the C path"); stages C0 and C1 done 2026-11-12 (see the stage log); C2 onward not started. Written 2026-11-11 from a read-only pass over the tree.
Design doc section: CreativeMotion's Implementation Doc (animation runtime); `docs/decisions/0001` (CreativeMotion is compiled into the engine) and `0010` (it is edited here); `docs/research/0022-creativemotion-in-game-status.md`
Goal: every actor's pose is produced by CreativeMotion (`opas::anim`), `MWAnim::Play` stops being a second animation system, and the assimp COLLADA *animation* reader is gone, while Morrowind plays exactly as before until toddhow switches.
Inherited code touched: `components/mwanimation/play.{hpp,cpp}` (`MWAnim::Play`, `getActiveState`, `updateBones`, text keys), `apps/openmw/mwmechanics/character.cpp` (`createPoseControllers`, `runPoseDriver`, `runAnimation`, `postAnimation`), `apps/openmw/mwrender/creativemotiondriver.*`, `components/vsgadapters/dae/daeanim.*`, `components/resource/resourcesystem.cpp` (the reader list near line 200), `apps/openmw/mwlua/animationbindings.cpp`. Commit hash to be recorded when work starts.
Owner's decision (toddhow, 2026-11-11): "CreativeMotion is purpose built for this engine so we should do all the wiring in and get rid of the old collada stuff. We can load the collada animations if we need to." New assets use open formats (glTF); NIF/KF are for compatibility only.

Marking: anything not verified in code is marked RECALLED-VERIFY. Estimates are mine, plus or minus 50 percent.

## 0. Coordination with task 0123 (glTF clips)

A parallel task, 0123, is making glTF clips load through `opas::anim::readGltf` into `MWAnim::Play` groups. At the time of writing `docs/tasks/0123*` does not exist in this checkout, and the only users of `readGltf` outside CreativeMotion are two tests (`apps/components_tests/extension/test_compat_morrowind.cpp`, `test_vfs_service.cpp`; grep of `readGltf|GltfReader`), so what 0123 delivers is RECALLED-VERIFY. This task plans around it:
- 0123 owns: the glTF file lookup (VFS, `.glb`), the bone-name mapping from a glTF skin to the actor's `Anim::Bones`, and the first glTF group table (names, start/stop/loop) feeding the existing `Play`.
- This task (stage C1) generalises what 0123 builds into the single `AnimSet` type below and must not start until 0123's group-table shape is known. If 0123 lands first, C1 adopts its type; if not, 0123 is rebased onto `AnimSet`. Either way there is one group table, not two.

## 1. Today's state (read from the tree)

CreativeMotion lives in `components/creativemotion` (`anim/{clip,gltf,graph,runtime,style}`, `kinematic/{ik,markers}`, `physics/{interface,jolt}`, `core/*`). The game wires it through one class, `MWRender::CreativeMotionPoseDriver` (`apps/openmw/mwrender/creativemotiondriver.hpp`), owned per actor by `MWMechanics::CharacterController` (`createPoseControllers`, `runPoseDriver`).

| CM feature | Wired? | Where | Setting (`[Game]` unless noted) |
| --- | --- | --- | --- |
| Clip model `opas::anim::Clip` from a Morrowind `.kf` (per-source conversion, cached) | Yes | `CreativeMotionPoseDriver::registerSource` (converts an `Anim::ControllerMap` to a Clip; channels with no exact equivalent resampled at `sResampleRate` 60 Hz) | `creativemotion animation` (off) |
| Clip sampling at the group's time | Yes | `CreativeMotionPoseDriver::update`, fed by `MWAnim::Play::getActiveState` (source, group name, time) per bone group | same |
| glTF reader `opas::anim::readGltf` / `readGltfFile` | Not wired to the engine (library and tests only; task 0123 is wiring it) | `components/creativemotion/anim/gltf` | none yet |
| Inertializer (group-to-group transitions) | Yes | `CreativeMotionPoseDriver::update`, one per bone group; duration `creativemotion animation transition` | same |
| Transition rules by motion kind and intent (tasks 0053, 0067) | Yes, off by default | `classifyGroup`, `transitionSeconds`, `IntentTracker` (`creativemotionstyle.*`), `[CreativeMotion Transitions]` | `creativemotion transition rules`, `creativemotion preset = natural` |
| Style layer (`.style.json`: posture, cadence, idle life) | Yes, off by default | `CreativeMotionPoseDriver::setStyle`; `CharacterController::resolveStyleProfile` / `configureStyle`; cadence applied in `CharacterController::update` ("Layer 3.B") | `creativemotion style layer`, `creativemotion default style` |
| Animation graph (`AnimGraph`, `GraphInstance`) | **No** (no include outside `components/creativemotion`; grep) | `components/creativemotion/anim/graph` | none |
| Pose blend, `ClipPlayer`, `AnimationPlayer`, masks | **No** (the driver uses clips, the engine's `BlendMask` groups and the inertializer; it includes `Inertialization.hpp`, not `PoseBlend.hpp`; RECALLED-VERIFY for `AnimationPlayer`) | `components/creativemotion/anim/runtime` | none |
| Swing-twist / `HelperSolver` (driven helper bones) | **No** | `anim/runtime/SwingTwist.hpp`, `HelperSolver.hpp` | none |
| Clip events / sidecar (`ClipEvent`, `ClipSidecar`) | **No** (text keys still come from Morrowind's `Anim::Tags`) | `anim/clip` | none |
| Two-bone IK, foot planting, look-at, smoother turns | Yes, engine-side | `MWRender::ActorIkController` (uses CM's `opas::ik`), runs in `postAnimation` after the pose | `foot planting`, `look at`, `smoother turns` |
| Inertial locomotion, lean, foot lock, stride sync | Yes (engine library `components/inertialmotion`) | `MovementSolver`, `ActorIkController` | `inertial locomotion`, `procedural lean`, `foot lock`, `inertial stride sync` (task 0108) |
| Ragdolls | Yes, for dead NPCs | `ActorRagdollDriver::apply` in `CharacterController::updateRagdoll`, last in `postAnimation` | `[Physics] creativemotion ragdolls` |
| Authored locomotion clips (`walkstart`, `runstop`, ...) | **Exist in no data source** (task 0108 finding): `Play::hasAnimation` is false and the procedural fallback always runs | n/a | n/a |

What a player sees with `creativemotion animation` on is the vanilla pose plus an inertialized blend at each group change (research note 0022, section 3). Every "No" above is dormant code.

Frame order today (`apps/openmw/mwrender/CLAUDE.md`, "Frame order for an actor"): (1) `CharacterController::runAnimation`, possibly on an animation worker thread, one actor per call: `MWAnim::Play::runAnimation` advances states, text keys and movement and, unless `Play::updateBones` is false, writes bones; then `CreativeMotionPoseDriver::update` and `apply` (`createPoseControllers` sets `mAnimation->updateBones = !mPoseDriverApplies`). (2) `postAnimation` on the main thread: `Actor::manualAnimation`, IK/lean/hitboxes, the ragdoll override.

What `MWAnim::Play` still owns (`components/mwanimation/play.hpp`): the `AnimSource` list per actor (one `Anim::ControllerMap` per `.kf` or `.dae`), `AnimState` (group, time, loop, speed, priority per bone group), group lookup and text keys (`getStartTime`, `getTextKeyTime`, `TagEvent`, `EndedEvent`), priorities and blend masks (`sNumBlendMasks`), root-motion accumulation (default axes (1, 1, 0), mirrored in the driver by `setAccumulation`) and `listAnimationGroups`. The pose driver is a pure consumer of `Play::getActiveState`: `CharacterController` still chooses groups and `Play` still decides what is active.

## 2. Target: what "CreativeMotion is the runtime" means

### 2.1 Ownership

- **Clips.** Every animation an actor can play is an `opas::anim::Clip` inside one engine type, `Anim::AnimSet` (new, proposed in `components/animation`): a skeleton binding, the clips, and a **group table** (group name to clip, start, stop, loop start, loop stop in clip seconds) plus text-key events. One `AnimSet` per source file, cached per path in the resource system and shared by all actors with that skeleton (`ClipBinding` is already shareable: `components/creativemotion/anim/CLAUDE.md`).
- **Groups and text keys.** CM owns them. A Morrowind `.kf` text key `"walkforward: loop start"` at time t becomes a group-table entry and a `ClipEvent`; the `.txt` keys for COLLADA and `Anim::Tags` (`components/animation/tags.hpp`, which already splits `"group: key"`) go through one parser into the same table. `getStartTime` / `getTextKeyTime` answer from the table.
- **Root motion.** CM's `Clip::rootBone` and the player produce root motion; the accumulation rule is kept bit for bit as `Play` does it, so the movement solver sees the same `movementFromAnimation` (`CharacterController::postAnimation`, note 0022 section 1).
- **Bone writes.** The pose driver is the only writer of an actor's bones (as it already is with its setting on); `Play::updateBones` and the keyframe-controller write path go in stage C5.
- **Stays outside CM:** which group to play (gameplay) until C4; Morrowind bone-name and `.1st` conventions (they come from the game pack through `Misc::GameConventions`, task 0085 move 1); IK, lean and ragdoll adapters (after the pose).

### 2.2 Morrowind `.kf` compatibility

`.kf` stays a first-class *input*. At source load a converter (today in `CreativeMotionPoseDriver::registerSource`; the pack also has its own `compat_morrowind::KfTrack` reader) builds the `AnimSet`. NIF controller interpolation differs from CM clips (constant keys switch halfway between keys; quadratic/TCB translation: task 0007, "Interpolation rules"), so conversion resamples at 60 Hz where there is no exact equivalent, as today. Equivalence is **measured, not assumed**: stage C0 records vanilla traces and C2 compares against `Play`'s own output, using the existing comparison mode (`creativemotion animation compare`, `CharacterController::runPoseDriver`).

### 2.3 The graph drives state (staged)

`opas::anim::AnimGraph` / `GraphInstance` becomes the state machine for locomotion and stance. `CharacterController` stops choosing group names directly and feeds an input snapshot (the existing `ActorState`: moving, running, sneaking, drawn weapon, hit, turning, speed, plus `IntentTracker` intent) and reads the graph's output (clip, phase, blend). The graph runs in **shadow mode first** (computes what it would play, compares with `CharacterController`'s actual choice, changes nothing), then drives locomotion only, then combat, behind a new `[Game] creativemotion graph` (off by default, so vanilla group choice stays the default until toddhow switches). The shipped graph uses Morrowind's group names as its vocabulary so scripts, `playgroup` and combat text keys keep working.

### 2.4 What becomes of `MWAnim::Play`

A staged shrink, not one deletion: C1 makes it read groups and text keys from `AnimSet`; C2 makes the driver the intended bone writer; C5 reduces `Play` to a thin facade (`playgroup`, queue, priority arbitration and the Lua-facing getters) over CM's players, keeping its public method names so `CharacterController`, `Actor` and Lua need no change in the same stage. Non-skeletal animation (doors, scenery, `animatedchunk`, NIF controllers on non-bone nodes, particles, UV scroll, light flicker) is **not** CM's job and stays on `Anim::ControllerMap` controllers; only skeletal-actor animation moves.

## 3. Retiring the COLLADA animation path

### 3.1 What exists

- `vsgAdapters::daeAnimation` (`components/vsgadapters/dae/daeanim.cpp`, 249 lines) reads a `.dae` with assimp (`Assimp::Importer`), first shifts each `<animation_clip>`'s samplers by the clip start (`applyClipStartTimes`; assimp ignores `<library_animation_clips>`), turns each node's channels into `Anim::TransformController`s in an `Anim::ControllerMap`, and reads text keys from the `.txt` beside the model (`readTextKeys`: one `"group: key time"` per line). It is registered in `components/resource/resourcesystem.cpp` in the `vsgAdapters::vfs` reader list next to `vsgAdapters::kf`; the `findFileCallback` there returns a `.dae` path unchanged (a COLLADA model holds its own animations).
- Model and skin loading is a different reader, `vsgAdapters::dae` (`components/vsgadapters/dae/dae.cpp`) through `Resource::readAssimpScene`, which also removes the whitespace that makes assimp 6.0.5 misread Blender skin `<vcount>` (CLAUDE.md pitfall). `.dae` collision shapes also read through assimp (`apps/components_tests/resource/testphysicsshapemanager.cpp`, `meshes/box.dae`).
- Who needs COLLADA animations: the OpenMW example suite's animated player model, loaded in the 32 Lua integration tests (CLAUDE.md pitfall "Integration tests on Windows"; the suite revision is `EXAMPLE_SUITE_REVISION` in `.gitlab-ci.yml`, currently `a1ba6fc9d7315c2158ef4c60349fc4e71b7ca343`). The suite is not in this checkout, so which `.dae` files and groups it plays, and whether any test depends on a played group, is RECALLED-VERIFY (greps of `scripts/integration_tests.py` and `scripts/data/integration_tests/test_lua_api` for animation calls found none). Stage C0 lists them from a fetched suite. Unit tests: `apps/components_tests/vsgadapters/testdaeanimation.cpp`, `resource/colladadocuments.hpp`.
- Eden Studio opens `.dae` meshes and animation files (`apps/opencs/view/doc/assetbrowser.cpp`, `view.cpp`, `world/meshsubview.cpp`); that viewing must keep working through whichever path survives.

### 3.2 Options

- **(a) Read COLLADA animations into `opas` clips.** Reuse `daeAnimation`'s assimp channel extraction, `applyClipStartTimes` and `readTextKeys`, but emit an `AnimSet` instead of an `Anim::ControllerMap`. assimp stays as a compatibility reader. Pros: nothing to author or convert, the suite works unchanged, least risk to the 32 tests, honours "we can load the collada animations if we need to". Cons: assimp stays a dependency (it stays anyway for COLLADA models and shapes).
- **(b) Convert the suite's clips to glTF at build or test setup.** Needs a converter that does not exist (a `.dae` to `.glb` tool is option (a)'s reader plus a glTF writer) and rewrites the pinned external suite's data in CI. More parts, no run-time benefit.
- **(c) Keep `daeAnimation` and `ControllerMap` as a permanent second path.** Contradicts the decision; `Play` could never shrink.
- **(d) Drop COLLADA animation and require glTF.** Breaks the Lua integration tests if they need the animated model (RECALLED-VERIFY) and every COLLADA mod.

### 3.3 Recommendation: (a), with an optional converter tool later

Implement `.dae` animation as a reader into `AnimSet` (stage C3).
- **Removed:** `vsgAdapters::daeAnimation` as a producer of `Anim::ControllerMap` / `TransformController` for actors, its registration in `resourcesystem.cpp`'s animation reader list, and (in C5) `Play`'s dependence on `ControllerMap` for skeletal actors.
- **Kept and moved:** the assimp channel extraction, the clip-start offset and the `.txt` text-key parser, now a COLLADA-to-`AnimSet` reader beside the glTF one (suggested home: the common compatibility pack, since COLLADA is a compatibility format; RECALLED-VERIFY against `CI/layer_rules.txt`).
- **Stays exactly as is:** assimp for COLLADA model and skin loading (`vsgAdapters::dae`, `Resource::readAssimpScene` with its `<vcount>` fix), COLLADA collision shapes, the editor's `.dae` viewing, and the assimp dependency itself.
- **Optional later (not in this task's acceptance):** an `edenconvert` command that writes a `.dae`'s animations as glTF, for mod authors who want the open format.

## 4. Stages (each a shippable slice; estimates in working days of one agent plus review)

Common rules: full loop (this touches `mwanimation`, the CreativeMotion adapters and the movement/animation boundary, so the lighter loop is not available); every new behaviour sits behind a setting that is off by default; **"unchanged when off" is compared, not assumed**, with the method written per stage; Windows build and tests, plus the Linux gate once before the push; a pose or frame check before any commit that can affect rendering.

**C0 Characterization and inventory (3 to 4 days).** (1) Fetch the pinned example suite on the I: drive and list every `.dae`, its animations and groups, and whether any Lua test depends on a played group. (2) Golden pose traces from the *current* build for a fixed scenario (the existing `tools/creativemotion_check`: idle, walk, run, stop, turn, draw, attack, hit, sheathe) for the player (third and first person) and a set of NPCs and creatures with `.kf` sources, recorded as text. (3) Characterization tests of `Play`'s group lookup, priority and blend-mask arbitration, loop handling and text-key firing on generated `.kf` and `.dae` sources. Acceptance: the tests pass on unmodified code; the inventory is in the stage log; no source change.

**C1 `AnimSet`: one clip, group and text-key model (8 to 10 days; needs 0123's shape).** Add `Anim::AnimSet` and the shared text-key parser; build it from `.kf` (existing converter) and glTF (0123); `ControllerMap` stays alongside; `Play` answers `getStartTime`, `getTextKeyTime`, `listAnimationGroups` from it. Acceptance: for every C0 source the `AnimSet` group table equals the one `Play` derives from `ControllerMap` (a test over the real Morrowind `.kf` files on the dev machine and generated ones); setting off: C0 golden traces identical. No per-frame allocation (`anim/CLAUDE.md`).

**C2 The pose driver reads `AnimSet` and becomes the intended writer (6 to 8 days).** The driver takes clips from `AnimSet` instead of converting per `ControllerMap`; the compare mode runs over the whole C0 scenario and reports the maximum bone difference per bone group; first person and creatures are covered (the driver is only exercised where `CreativeMotionPoseDriver::isValid`). `creativemotion animation` stays false by default. Acceptance: with it on, the maximum pose difference to `Play`'s output stays within a tolerance fixed from C0's numbers, text-key times are preserved exactly (a hit-timing trace), no actor loses an animation (on conversion failure it falls back to the `Play` path and logs once, as task 0007 specifies); with it off, C0 traces identical.

**C3 COLLADA into `AnimSet`; retire `daeAnimation`'s controller output (5 to 7 days).** Implement the reader per 3.3. The gate is the example suite's player model and the 32 Lua tests, on Windows (shell runner) and on Linux (CI). Acceptance: all 32 Lua tests pass; the `.dae` cases of `testdaeanimation.cpp` are ported (each ported test names its predecessor; no existing test is edited without toddhow's approval); the group tables of the suite's `.dae` models equal the old reader's (C0 inventory); `daeAnimation` and its registration are deleted; assimp still serves models and shapes (checked by grep and a build); COLLADA still goes through `Resource::readAssimpScene`.

**C4 Graph in shadow mode, then driving locomotion (15 to 20 days; the largest and least certain stage).** (a) A shipped vanilla-vocabulary graph (format `docs/formats/animgraph.md` in CreativeMotion's own docs, RECALLED-VERIFY path), shadow mode logs disagreements with `CharacterController` over the C0 scenario and a play session. (b) `[Game] creativemotion graph` (off): the graph chooses locomotion and stance groups while `CharacterController` keeps combat, spells, scripted `playgroup` and Lua-queued animations (explicit overrides the graph yields to). (c) Combat, after toddhow has judged locomotion. Acceptance: shadow disagreements empty or explained; with the setting on the C0 locomotion trace matches vanilla group selection except where listed as intended; setting off, byte-identical. Authored locomotion clips (task 0108: none exist) are a content task; the stage must work with the procedural fallback.

**C5 `MWAnim::Play` becomes a facade (8 to 10 days; waits for toddhow's go).** Remove the keyframe-controller bone-write path and `updateBones`; keep `Play`'s public API over CM players; skeletal `ControllerMap` use ends. Acceptance: `openmw-tests` and `components-tests` pass; `openmw.animation` Lua behaviour pinned by new tests written first on the pre-change build (risks, below); this is the only stage that deletes the vanilla path.

**C6 Wire the unused CM runtime pieces (about 8 days, optional, may follow C2).** Pose blend and masks for layered actions, swing-twist / `HelperSolver` for twist helper bones, clip events from sidecars, and blending a ragdoll in and out of the animated pose (hit reactions) instead of `ActorRagdollDriver::apply`'s hard override. Each behind its own off setting. Acceptance per piece: bone-trace evidence in the style of note 0022; off means unchanged.

Total: roughly 45 to 59 days for C0 to C5, plus about 8 for C6. C0, C1 and C3 can overlap with 0123.

## 5. Risks

- **Performance per actor.** The driver samples every bone group and runs an inertializer per group per frame for every actor with the setting on, NPCs in loaded cells included. Crowd-scale cost is not measured (RECALLED-VERIFY; note 0022 measured behaviour, not cost). Require a per-actor cost measurement in crowd scenes against `Play`'s controllers, allocation tests extended from the driver's existing ones, and a distance-based reduced-rate update before any default flip.
- **Threading.** `runAnimation` runs on animation worker threads, one actor per call; the driver touches only its actor's bones and the shared clip cache. Rules: `AnimSet` is immutable after build and published through the resource cache; conversion on first use must not race (`registerSource` allocates and is per actor today; a shared cache needs build-on-load or a lock); graph instances are per actor. The pull request states which thread each new callback runs on.
- **Determinism.** No clock reads; time comes from `Play` or the frame; style noise is seeded per actor (`actorSeed`); the 60 Hz resample must not depend on frame rate; two runs of the C0 scenario must give identical traces.
- **Saves.** Extensions never write saves and this task adds nothing to the save format. Whether animation state beyond vanilla `CharacterController` state is saved is RECALLED-VERIFY (`ESM::AnimationState`). The graph's internal state must not enter saves; after a load it re-enters from the restored group choice. A needed save record is a separate decision.
- **Lua `openmw.animation`** (`apps/openmw/mwlua/animationbindings.cpp`: `getTextKeyTime`, `getCurrentTime`, `getCompletion`, `getLoopCount`, `getSpeed` / `setSpeed`, `playQueued`, `playBlended`, `clearAnimationQueue`, `hasGroup`, `addVfx`): mods rely on its semantics. C5 starts with tests of every function on the unchanged build; `playQueued` / `playBlended` queue into `CharacterController`, which stays authoritative for scripted animation even when the graph drives locomotion.
- **First person.** First-person bones are not updated in vanilla mode (note 0022, "How it was measured"), and the player's neck and arms depend on `Player::manualAnimation` and `assignBones` (CLAUDE.md changelog 2026-11-04). The `.1st` skeleton and `manualAnimation` ordering must be handled before the driver replaces `Play` there; C2 includes a first-person pose check.
- **Equivalence.** `.kf` interpolation differs from CM clips; resampling may shift sub-frame timing of footsteps and hit keys (damage fires on text keys). Text-key times are kept exactly in seconds.
- **Mods.** Mod `.kf` files with scale tracks would lose scale (CM clips carry none: `anim/CLAUDE.md`); the fallback must trigger on unsupported content, never silently drop it.
- **Scope.** C4 is large and judgment-heavy; it must not block C1 to C3.

## 6. Open questions for toddhow (each with a recommendation)

1. **When does the old `Play` bone-writer path die?** Recommendation: not until you have lived with `creativemotion animation = true` in the test profile; C5 waits for your explicit go, and the old path remains the off state until then ("original engine behaviour is a feature").
2. **COLLADA: (a) a reader into `AnimSet`, or (b) an offline converter?** Recommendation (a); an optional `edenconvert` tool later.
3. **Where does conversion code live?** Recommendation: the `AnimSet` type in core (`components/animation`), the `.kf` and `.dae` readers in the compatibility pack, checked against `CI/check_layer_rules.sh` when C1 starts.
4. **Does the shipped graph speak Morrowind's group names or its own?** Recommendation: Morrowind's names, so scripts and text keys keep working; new content may add states.
5. **Does C4 wait for authored locomotion clips (task 0108)?** Recommendation: no; it works with the procedural fallback and empty clip slots.
6. **Far-actor cost policy** (reduced-rate update, skip the inertializer beyond a distance). Recommendation: decide after the C2 crowd measurement.
7. **Clip events from sidecars instead of text keys for glTF content?** Recommendation: yes for new glTF assets; text keys remain the `.kf` / `.dae` input; one event model internally.
8. **May C0 and C5 add new Lua `openmw.animation` pin tests?** Recommendation: yes (new tests only, no existing test edited).

## Constraints
Determinism and allocation as in section 5; `AnimSet` public API free of VSG and Jolt types where avoidable (CreativeMotion stays free of engine types); every new setting goes in `tools/testprofile/profile.cfg` or `exclude.txt` and `tools/testprofile/coverage.sh` stays clean; the one new setting is `[Game] creativemotion graph` (C4); no change to Morrowind defaults.

## Acceptance criteria (whole task)
- [ ] One `AnimSet` model serves `.kf`, glTF and COLLADA; group tables and text keys equal the old ones for every source in the inventory
- [ ] With every new setting at its default, the C0 golden scenario (traces and Lua test results) is identical to the build before, compared per stage as written
- [ ] The 32 Lua integration tests pass on Windows and Linux with `daeAnimation`'s controller path removed
- [ ] `creativemotion animation = true` keeps every actor animated (fallback proven) within the stated pose tolerance
- [ ] The graph in shadow mode disagrees with `CharacterController` only where explained
- [ ] `openmw.animation` Lua functions behave as before (tests written first)
- [ ] A fresh reviewer found no unresolved issue; the area `CLAUDE.md` files (`apps/openmw/mwrender`, `components/creativemotion/anim`) are updated

## Out of scope
Authoring locomotion clips, `.omwfx` and UI work, non-skeletal object animation, facial animation (task 0071), a stable C ABI for animation extensions, Skyrim `.hkx` clips (task 0119 is the separate spike), changing saves, the fate of the `openeden.animation` contract.

## Stage log

### 2026-11-12: stages C0 and C1 (toddhow approved the task on 2026-11-11: "Go ahead and start the C path")

Same hand wrote tests and code (no separate test-writing agent, no fresh reviewer); Windows only; local commits on the branch `c0runtime`, not pushed. Built in `I:\Projects\OpenEden-work\c0\build` from `e611a47752` (main). Work files (traces, suite clone, scripts) are in `I:\Projects\OpenEden-work\c0\`, outside the repository.

#### C0 inventory: the example suite (revision `a1ba6fc9d7315c2158ef4c60349fc4e71b7ca343`, cloned to `c0\suite`)

61 `.dae` files in the suite, **two** with animations:

| Model | Size | `<animation>` | `<animation_clip>` | Text key file | Groups in the `.txt` | With start and stop key |
| --- | --- | --- | --- | --- | --- | --- |
| `example_animated_creature/data/meshes/land_racer.dae` | 3.2 MB | 380 | 10 | `land_racer.txt`, 22 lines | 10 | 10 (one with loop keys) |
| `game_template/data/meshes/BasicPlayer.dae` | 56.5 MB | 10,530 | 135 | `BasicPlayer.txt`, 583 lines | 112 | 98 (eight with loop start and stop) |

All other `.dae` (the hub's architecture, doors, weapons, markers, the barrel, `BasicPlayerBody.dae`) have no animation. No `.kf`, `.nif` or `.glb` in the suite.

How the engine uses them: the clip names in the `.dae` (`1h_chop`, `walk`, ...) are NOT used; `daeAnimation` only shifts each clip's samplers by the clip start (`applyClipStartTimes`). The group table comes entirely from the `.txt` (`weapononehand: chop start 65.6`, ...). Facts found in the files, all pinned by tests:
- `BasicPlayer.txt` has one line without a colon, `hit1 stop 101.07`: the key is stored but names no group, so `hit1` has a start key and **no stop key and can never be played** (`PlayDaeFixture`, `AnimSet.a_key_without_a_group_belongs_to_no_group`).
- `swimknocdown: start` (typo) and `swimknockdown: stop`: two broken groups, neither playable.
- The weapon groups (`weapononehand`, `bowandarrow`, ...) have no plain `start` key; they start with `equip start`, `chop start` and so on, which `CharacterController` asks for as markers, so "98 of 112 groups have start and stop" is not "98 playable groups".

Lua integration tests (`scripts/data/integration_tests/test_lua_api`, read, not run): they depend on the animations only indirectly. `record model property` expects `meshes/basicplayer.dae`; `player weapon attack` (player.lua, `basic_dagger1h`) needs the attack group's `hit` text key to fire, because damage is dealt on it (the test fails with "Did not damage any targets in 10 seconds" otherwise); `player forward running` needs the run group's root motion (distance > 0 along +Y); `load while teleporting` creates a `landracer` (the second animated model) and teleports it. No test asks for a group by name through `openmw.animation`. **The 32 Lua tests were not run in C0** (no Python/Windows runner set up; they are the C3 gate).

#### C0 golden pose traces (current build, `creativemotion animation debug`)

`tools/creativemotion_check` gained `MODDIR` (an alternative scenario folder) and `mod_firstperson/` (the same scenario with the camera in first person). Scenario: idle, walk, run, stop, turn, draw, attack, hit, sheathe, 1,400 frames at a fixed frame time (`OPENEDEN_FIXED_FRAME_TIME=0.0166667`), `--random-seed 7`, cell Seyda Neen, `OPENEDEN_QUIET_WINDOW=1`; the runner is `c0\run_trace.sh`. The log's `CMTRACE` lines (one per actor per frame: groups of the four bone groups, movement from animation, animation speed, yaw, six bone rotations) are stored as text per run in `c0\golden\<label>\trace.txt`; `pose.txt` is the same without `pos=` and `CMFEET_*` (world positions, which depend on the physics threads' timing: two runs of one executable differ in them from frame 304). Raw traces stay out of the repository (they name the user's game records); the hashes are here.

- Actors in each run: the player, Imperial guards (three at once), two `chargen` guards, Fargoth, Eldafire and five other NPCs, mudcrabs, slaughterfish, rats (14 distinct ids).
- **Player, determinism:** the first 1,111 lines of the player's pose (up to the hit phase) are byte-identical between two runs of the baseline executable, in third person (`57752746df4a`) and first person (`029615456b18`). After the hit phase begins the pose differs run to run because the hit reaction group (`hit1` to `hit5`) is chosen at random (`hit4` in run A; `--random-seed` does not fix it).
- **NPCs and creatures:** not reproducible run to run (idle variants, wandering and which creatures are active are random: for example Fargoth plays `idle4` then `idle2` in both runs but for `idle5` in one). Their traces are a characterization of what each group looks like, not an exact golden. Side finding: a mudcrab's `g0` changes between `idle`, `idle2` and `idle3` on consecutive frames for most of the run (baseline behaviour, not investigated).
- Variants run: `vanilla` third person (x2) and first person (x2), `cm` (inertial locomotion plus `creativemotion animation`) third and first person, `cmonly` (`creativemotion animation` alone) third person. With the driver on the trace says `driver=applies` for every actor.
- Player prefix hashes of the baseline: vanilla-tp `57752746df4a`, vanilla-fp `029615456b18`, cm-tp `5c0853f14002`.

#### C0 characterization tests of `MWAnim::Play`

`apps/components_tests/mwanimation/testplaycharacterization.cpp`, 43 tests (this file is new; no existing test was edited): group lookup (whole-name match, the last source wins, prefix text key lookup, group listing order), play and completion, text key firing (the key at the start time fires on play, keys before a start point do not, one long step fires every key it crosses and stops on the stop key), loops (hand-traced counts, loop keys, the `loopfallback` flag, looping switched off), speed, priority and blend-mask arbitration per bone group (equal vectors replace, different vectors coexist, the tie goes to the earlier name), persistent priority, root motion on the accumulated axes and the reset of the root bone, `updateBones = false`, velocity, a source with an unknown bone and no root bone, and two real readers: a generated NIF 4.0.0.2 `.kf` (`vsgAdapters::kf`) and a generated COLLADA model with a `.txt` (`vsgAdapters::daeAnimation`). The expectations were derived by hand from `play.cpp` and checked by running them. **All 43 passed on the unmodified code** after two of my own reading errors were corrected in the tests (not in the code): keys before a start point do not fire (`lowerBound`), and `setAccumulation` does not change the returned movement.
- **A quirk pinned, not fixed:** `Play::setAccumulation` changes only the reset channel of the playing source, not `Play::mAccumulate`, which `updatePosition` uses; so after `setAccumulation({1,0,0})` y is both left on the bone and still returned as movement, until the next `resetActiveGroups` (test `setting_the_accumulated_axes_changes_what_stays_on_the_bone_but_not_the_movement`). `character.cpp` calls it; the effect in game was not investigated.
- **Mutation:** in `Play::resetActiveGroups` the arbitration `active->second.priority[i] < state->second.priority[i]` was changed to `<=` (single line), built, and `PlayCharacterization.equal_priority_on_a_bone_group_goes_to_the_first_state_by_name` failed (42 others passed); restored (`git diff` of `play.cpp` is empty for that line). One mutation only.
- A version of `an_unknown_bone_in_a_source_is_skipped_and_the_rest_play` that used `testing::internal::CaptureStderr` passed alone and ended the whole-binary run with exit code 3 (cause not found); the capture was removed.

#### C1 `Anim::AnimSet` and the shared text key parser

- `components/animation/animset.{hpp,cpp}`: `AnimSet` (immutable, shareable): the `ControllerMap` it came from, its `Tags` (an empty table when there are none, never null) and ONE group table: per group the first key time, and the LAST `start`, `stop` (any key beginning with `stop`, so `stop.` counts), `loop start` and `loop stop` key. Queries: `find`, `hasGroup`, `startTime`, `textKeyTime` (prefix). A source with no tags (which used to be a null dereference in `Play::hasAnimation`) now has an empty table.
- `components/animation/textkeys.{hpp,cpp}`: `addTextKeys` (a .kf record: CR/LF split, trim, lower case) and `addTextKeyLine` (a `.txt` line: key then time after the last space). The `.kf` reader (`handleTextKeys`) and the COLLADA reader (`readTextKeys`) now call them; their bodies were moved, not rewritten. The glTF clips of task 0123 build their `Tags` in code (`group: key` strings), so they join the table through `Play`.
- `MWAnim::Play` builds an `AnimSet` in `addSingleAnimSource` and answers `hasAnimation`, `getStartTime`, `getTextKeyTime` and `listAnimationGroups` from it. `play()`, `runAnimation()`, `reset()`, `getVelocity()` still read the `Tags` (now `set->tags`) and the `ControllerMap` controllers; the pose driver is untouched (C2). **Boundary kept:** the `AnimSet` does not hold clips yet (the spec's "one clip" model is C2).
- **Tests written first and shown red:** `apps/components_tests/mwanimation/testanimset.cpp` (AnimSet 9, equivalence 3, TextKeys 5, two gated tests) against stubs of both new files (a stub header and `.cpp` that found nothing): 15 of 18 runnable tests failed (`c0\c1_red.txt`; the three that passed test absence: an empty source, blank record, non-key lines), then all pass with the code.
- **Equivalence evidence:** for every group of every table, a real `Play` is driven and compared: it knows the group, its start time equals the table's first key and the pre-C1 derivation (`Tags::findGroup`, `Tags::getGroups`), playing it starts at the table's start and a long step ends it at the table's stop (or refuses exactly when a marker is missing or start > stop). Over the main generated source, a source with repeated sections / garbage stops / group-less keys, 60 seeded random sources, **162 real Morrowind `.kf` files in the three BSAs (2,554 groups, all read, none without groups; gated test with `OPENEDEN_ANIMSET_CHECK_DIRS`)** and **the two text key files of the example suite** (gated, `OPENEDEN_ANIMSET_SUITE_DIR`): no difference. Loose and mod `.kf` files were not compared.
- Existing tests: `testdaeanimation.cpp` (the COLLADA reader, which uses the shared line parser now), the kf-related tests and `testsceneryanim` pass unedited.
- **Setting off, compared:** the new executable and the baseline executable give byte-identical player pose lines (before the hit phase) in all three variants run: vanilla third person (1,111 lines), vanilla first person (1,111), `cm` third person (1,111). That is the only compared trace of the engine; NPC and creature traces are not exact (above).
- Whole binaries on the final tree: `components-tests` 9,298 pass, 43 skipped (data-gated), 0 failed, with `*Slang*`, `*Complement*`, `*Property*` left out (very slow); `openmw-tests` 2,032 pass with `*Slang*` left out. `CI/check_layer_rules.sh` 104 exceptions (unchanged), `check_tests_registered`, `check_game_conventions`, `check_mwbase_fakes`, `check_no_osg`, `check_cmake_format` and `tools/testprofile/coverage.sh` pass; `check_file_names` still lists two files from main.

#### Behaviour changes

None intended with every setting at its default. Two edge cases differ from before: a source with no text key table no longer crashes `hasAnimation` and friends; and `getStartTime` finds a group through the table's rule (the text before the first `: ` of a key) instead of the prefix `group + ": "`, which differ only for a group whose own name contains `: `. No setting added.

#### Not run

The Linux gate and GCC `-Werror` (Docker is not to be used), clang-format 14 (not installed; the new code was formatted by hand), clang-tidy, the 32 Lua integration tests, sanitizers, a fresh reviewer, Eden Studio (not built in this configuration), the NPC/creature pose comparison (not reproducible), the hit phase of the player trace, mutations beyond one, loose and mod `.kf` files, the launcher.

#### Open for toddhow

1. C1 stops short of the spec's "clips inside the set" on purpose (that is C2's driver change); say so if you want `AnimSet` to own clips already in C1.
2. The `setAccumulation` quirk above: pinned as it is; fixing it changes root motion in the cases that call it (a decision, not part of this task).
3. `--random-seed` does not fix the player's random hit reaction or NPC idle choice; exact goldens exist only for the player before the hit phase. Do you want the hit variant forced (a developer-only knob) for later stages?
4. 0123's gltf `ControllerMap` is not yet built into an `AnimSet` anywhere but `Play` (it needs no change), so "AnimSet builds from glTF" is true only through `Play::addSingleAnimSource`.
