# Task 0011: Track V stage V3 — OpenEden's rendering features on VSG

**Status:** CLOSED 2026-11-10 by toddhow (remaining checks are follow-ups, not blockers; listed in `docs/toddhow-checks.md`, "Known open items from Track V"). Earlier status: approved by toddhow (2026-10-29) with the answers below. Depends on task 0010 (V2).
**Design doc section:** `docs/decisions/0002-vulkan-renderer-on-vulkanscenegraph.md` ("Consequences": the IK controller, pose driver and Jolt debug draw need porting); tasks 0006 (M9 IK), 0007 (M12 animation runtime); `docs/design/inertial-locomotion.md`; decision 0001 (CreativeMotion in the engine).
**Goal:** rebuild every OpenEden feature that task 0009 compiled out (`OPENEDEN_VSG_PENDING_FEATURES`) on vsgopenmw's animation system, with the same behaviour, the same settings and the same "off by default, unchanged when off" guarantees as on the OSG build; then remove the option.
**Workflow:** full loop. The test-writing subagent writes the tests from this spec and tasks 0006/0007's acceptance criteria, before implementation.

---

## Context (facts this spec relies on)

**What each feature uses today (OSG build, OpenEden `442eec6e2f`):**

| Feature | Files | OSG/OpenMW types it is built on |
| --- | --- | --- |
| Foot planting, look-at, smoother turns, compound hitbox bone sampling, inertial lean | `apps/openmw/mwrender/actorikcontroller.{hpp,cpp}` (734 lines) | `osg::MatrixTransform`, `osg::computeLocalToWorld`, `osg::NodeVisitor`, `SceneUtil::PositionAttitudeTransform`, `SceneUtil::NodeCallback`, `MWRender::Animation`; writes bone matrices after the animation pass and calls `SceneUtil::Skeleton::markBoneMatricesDirty()` |
| RotateController changes (head, spine, root yaw; non-keyframed bones) | `apps/openmw/mwrender/rotatecontroller.{hpp,cpp}`, `animation.cpp` | OSG update callbacks |
| CreativeMotion animation runtime (M12) | `apps/openmw/mwrender/creativemotiondriver.{hpp,cpp}` (455 lines), `animation.cpp` hooks | `NifOsg::KeyframeController` (`getTransform(time)`, interpolator accessors), `NifOsg::MatrixTransform`, `SceneUtil::KeyframeHolder`; pose writer callbacks in the keyframe callbacks' slot |
| Smooth animation blending (OpenMW's, which M12 replaces when on) | `animblendcontroller.{hpp,cpp}`, `components/resource/animblendrulesmanager.*` | OSG callbacks |
| Jolt physics debug draw | `apps/openmw/mwrender/joltdebugdraw.{hpp,cpp}` (339 lines) | `osg::Geometry`, `osg::DrawElementsUInt`, `osg::PolygonMode`, `osg::Material` |
| Navmesh debug draw (OpenMW's, dropped by vsgopenmw) | OpenMW `mwrender/navmesh`, `components/sceneutil/detourdebugdraw` | OSG geometry |
| Compatibility pack `.kf` → glTF converter | `plugins/compat_morrowind/src/kf_converter.cpp` (621 lines) | `NifOsg::Loader`, `NifOsg::KeyframeController`, `SceneUtil::KeyframeHolder` |
| Distortion (OpenMW 0.51) | `apps/openmw/mwrender/distortion.hpp` | OSG |

**What vsgopenmw offers in their place** (vsgopenmw `2830e7e2b4`, as merged in task 0009):

- `Anim::Transform` (`components/animation/transform.hpp`): a `vsg::Transform` with `translation` (`vsg::vec3`), `rotation` (`vsg::mat3`), `scale`, `setAttitude(vsg::quat)`; bones are `Anim::Transform` nodes.
- `Anim::Bones` / `Anim::Bone` (`components/animation/bones.hpp`): bone hierarchy lookup by normalized name (`search(name)`), with a `BonePath` per bone.
- `Anim::Channel<T>` (`components/animation/channel.hpp`): `value(float time)`, the keyframe track abstraction; `components/animation/{interpolate,extrapolate,path,controller,transformcontroller,skin,morph}` evaluate them.
- `vsgAdapters::kf` (`components/vsgadapters/nif/kf.hpp`): the `.kf` reader producing VSG objects; `components/vsgadapters/nif/{nif,anim,channel,particle}` convert NIF data to `Anim` channels and controllers.
- `MWAnim::Play` (`components/mwanimation/play.hpp`): Morrowind's animation state per actor: `addSingleAnimSource(ControllerMap)`, `play(group, priority, blendMask, …)`, text-key events (`encounteredTags`), accumulation (`setAccumulation`), `isRotationControlled(bone)`.
- `MWAnim::Actor` (`components/mwanimation/actor.hpp`): head yaw and pitch, upper-body yaw, legs yaw, body pitch (the RotateController equivalents), `manualAnimation(pitchSpine, dt)`, spine bones.
- The order in which animation, manual bone changes and skinning run per frame is defined by vsgopenmw's update traversal (`components/animation/update*`, `apps/openmw/mwrender/animcontext*`); read it at the merged commit and cite it in the pull request.

## Design

- **One engine-side bone interface.** Add `apps/openmw/mwrender/bonepose.{hpp,cpp}` (name proposed): read a bone's local and world transform and write a local rotation/translation after the animation pass, on top of `Anim::Bones` and `Anim::Transform`. The IK controller, the inertial lean and the CreativeMotion pose driver use only this interface, never VSG nodes directly, so a later renderer change touches one file (research note 0001, recommendation 4).
- **Pass order:** animation sampling (Morrowind's `MWAnim::Play`, or CreativeMotion's runtime when `creativemotion animation = true`) → head/spine/root rotations (`MWAnim::Actor`) → foot IK and look-at → inertial lean → compound hitbox sampling → skinning. Same order as on the OSG build (task 0006 and 0007 specs); cite where each runs in vsgopenmw's update traversal.
- **Foot IK, look-at, smoother turns, hitbox sampling, lean:** port `ActorIkController` onto the bone interface. The algorithm, settings and early-out (no raycasts, no bone writes when every relevant setting is off) stay as they are; only the scene access changes. Raycasts still go through `MWPhysics::PhysicsSystem` (Jolt), unchanged.
- **CreativeMotion pose driver:** convert clips from `Anim::Channel` data instead of `NifOsg::KeyframeController` (the conversion rules and fidelity criteria of task 0007, stage R1, still apply, now against `Anim::Channel::value(time)` as the reference); drive per-mask sampling from `MWAnim::Play`'s groups, times and priorities; write the pose through the bone interface in place of `MWAnim::Play`'s keyframe controllers for actors on the new path, falling back to vsgopenmw's path on any conversion failure (logged once). Inertialized transitions replace vsgopenmw's blending for those actors, never stacking with it.
- **Animation blending (OpenMW's smooth transitions):** decide whether vsgopenmw already provides equivalent blending; if not, port OpenMW 0.51's `animblendcontroller` rules (`animblendrulesmanager`) onto `MWAnim::Play`, so `smooth animation transitions` works when CreativeMotion's runtime is off.
- **Jolt debug draw and navmesh debug draw:** reimplement as debug geometry submitted as data and drawn by the VSG renderer (points, lines, triangles, colours), in the spirit of the plugin SDK's debug draw (`sdk/include/openeden/debug_draw.hpp`); `tcb` toggles the Jolt draw, the navmesh draw follows OpenMW's `[Navigator]` debug settings. This also closes vsgopenmw's roadmap items `vsgopenmw-physics-debug-draw` and `vsgopenmw-navmesh-debug-draw`.
- **Compatibility pack converter:** move `.kf` conversion off the renderer entirely: read keyframe data from `components/nif` records (`NiKeyframeController`, `NiKeyframeData`, text keys) and produce glTF, with no dependency on `NifOsg` or `vsgAdapters`, so the pack only needs the SDK and `components/nif` (decision 0001's direction: game readers belong in the pack). The output must be identical to the OSG build's for the existing tests' synthetic inputs.
- **Distortion:** port or drop with a reason, deciding with toddhow (open question 2).

## Inputs / outputs

- Code on `vulkan`; `OPENEDEN_VSG_PENDING_FEATURES` and every guard using it removed at the end.
- Tests restored and adapted: `test_creativemotion_runtime.cpp` (driver tests on synthetic `Anim` channels), `test_compat_morrowind.cpp` (converter on synthetic NIF records), IK unit tests on synthetic bone hierarchies, the allocation tests (no allocation per frame in the driver, pose writers and IK).
- Module `CLAUDE.md` files for `apps/openmw/mwrender` and `plugins/compat_morrowind` describing the bone interface and the pass order.

## Constraints

- **Off means unchanged:** with every OpenEden setting at its default, the Vulkan build renders and animates exactly as it did at the end of task 0010 (compare with `tools/render_compare.sh`, bit-identical bone transforms in a scripted test with fixed seed and fixed frame times, as in task 0007's first criterion).
- No allocation per frame in the bone interface, IK, lean, pose driver or debug draw submission (when the debug draw is off).
- CreativeMotion's vendored copy (`components/creativemotion`) is not edited (decision 0001); anything it lacks becomes a CreativeMotion task spec.
- Physics stays Jolt and unchanged.

## Acceptance criteria

- [ ] Foot planting, look-at, smoother turns, compound hitboxes and the inertial lean work on the Vulkan build with the same settings; the in-game checks from task 0006 pass again (written steps in the pull request).
- [ ] With all of them off, bone transforms over a scripted test are bit-identical to the end of task 0010, and no raycast or bone write happens (counted in a test).
- [ ] CreativeMotion's runtime (`creativemotion animation = true`) plays Morrowind's animations on VSG; task 0007's criteria hold again: conversion fidelity against `Anim::Channel` within 1e-5 rad and 1e-5 m; parity with vsgopenmw's path with transitions off; text keys and root movement unchanged; no pop on transitions; fallback logs once.
- [ ] `smooth animation transitions` works when the CreativeMotion runtime is off (or the decision that vsgopenmw's blending replaces it is recorded with toddhow's approval).
- [ ] `tcb` draws Jolt collision shapes, including compound hitboxes following bones; the navmesh debug draw works.
- [ ] The compatibility pack builds with no `NifOsg` or `vsgAdapters` dependency and its converter output matches the OSG build's on the test inputs.
- [ ] `OPENEDEN_VSG_PENDING_FEATURES` no longer exists; every test excluded under it in task 0009 runs again and passes.
- [ ] All gates pass: unit tests, 32 Lua integration tests, allocation tests, clang-format.

## Out of scope

- New behaviour for any feature (this task moves features, it does not change them).
- Renderer parity items (task 0012); OSG math (task 0013; the bone interface may convert to OSG math internally until then); the editor (task 0014).

## After this task

With OpenEden's features back, `vulkan` is feature-complete against the OSG build except for task 0012's parity items. toddhow decides whether `vulkan` becomes the main line now (proposed) or after task 0012.

## Open questions for toddhow

1. **Blending:** if vsgopenmw already blends animations its own way, keep its blending as the default (with CreativeMotion's runtime as the alternative), or port OpenMW 0.51's rules-based blending as well?
2. **Distortion** (`mwrender/distortion.hpp`, OpenMW 0.51): port it to VSG now, move it to task 0012, or drop it?
3. **When `vulkan` becomes the main line:** after this task (proposed) or after task 0012?

### Answers (toddhow, 2026-10-29)

1. **Blending:** CreativeMotion handles blending. OpenMW 0.51's rules-based blending is not ported; with CreativeMotion's pose driver off, vsgopenmw's existing blending stays (Morrowind behaviour unchanged when off).
2. **Distortion:** moves to task 0012 (V4).
3. **Main line:** `vulkan` becomes the main line after task 0012 (V4); toddhow merges it.

## Progress (2026-10-29)

Every feature in this task is back on the Vulkan build. Changes are uncommitted until the fresh review is done.

- **Bone interface:** `MWRender::BonePose` (`apps/openmw/mwrender/bonepose.*`) is the one bone interface. It uses OSG math internally until task 0013. Its tests are in `apps/openmw_tests/mwrender/testbonepose.cpp`.
- **Foot IK, look-at, smoother turns, inertial lean and compound hitboxes:** `ActorIkController` is rebuilt on `BonePose` and created per actor by `CharacterController`. It runs in `postAnimation` after `MWAnim::Actor::manualAnimation`. With every setting off it binds no bones.
- **CreativeMotion pose driver:** `CreativeMotionPoseDriver` is rebuilt on `Anim::Bones` and `Anim::ControllerMap`.
  - Conversion rules: `Linear` and quaternion keys convert exactly; `Flip` keys become step keys switching at the midpoint; everything else is resampled at 60 Hz, as task 0007 decided.
  - It samples per bone group from `MWAnim::Play::getActiveState` and inertializes changes. When "creativemotion animation" is on, it writes the bones in place of Play's controllers (`Play::updateBones = false`).
  - If a source can't be converted, the actor falls back to vsgopenmw's path and a warning is logged.
  - **In game, Seyda Neen, compare mode with transitions at 0:** no bone differs from vsgopenmw's pose by more than 5 mm or 0.02 rad (zero reports in 1600 frames). With the default 0.25 s transitions, differences appear only during transitions, as intended.
  - Tests: `test_creativemotion_runtime.cpp`, rewritten from the spec by a separate agent, 35 tests.
- **Blending:** per the answer to question 1 above, CreativeMotion handles blending and 0.51's rules-based blending is not ported. With the driver off, vsgopenmw's behaviour is unchanged. The criterion "`smooth animation transitions` works when the CreativeMotion runtime is off" is replaced by that decision.
- **Debug draws:**
  - Both are drawn by `MWRender::DebugGeometry`: lines plus translucent triangles, unlit, no fog. That needed colour mode 3 in the default shader's `materialTerms`, and fog skipped for it in `main.frag`.
  - `tcb` toggles the Jolt draw (`joltdebugdraw.*`).
  - The navmesh draw (`navmesh.*`, plus `components/sceneutil/{detourdebugdraw,navmesh}`, now renderer-free behind `SceneUtil::DebugPrimitiveSink`) follows `[Navigator] enable nav mesh render` and `nav mesh render mode`, the `togglenavmesh` command, `setNavMeshNumberToRender` and Lua's `debug.setNavMeshRenderMode`. Tiles are redrawn only when their version changes, on the main thread.
  - Navmesh lines match the OSG build's positions. The fill uses Recast's colours and is more visible than the OSG build's (research note 0004).
  - Tests: `sceneutil/testdetourdebugdraw.cpp`, 28 tests.
- **Compatibility pack:** the `.kf` reader `compat_morrowind::KfTrack` / `loadKfAnimation` (`kf_animation.*`) reproduces `NifOsg::Loader::loadKf` and `NifOsg::KeyframeController::getTransform` at `cf3dbd3aa7` with no renderer. The pack builds again and needs only the SDK, `components/nif` and small utilities. Tests: `plugin/test_compat_morrowind.cpp`, adapted, plus new reader tests.
- **`OPENEDEN_VSG_PENDING_FEATURES`:** removed with its registry (`apps/openmw/vsgpendingfeatures.*`) and that registry's own test. The excluded tests run again.
- **Results (Windows):** components-tests 1578/1578 and openmw-tests 514/514 pass. The Linux and Lua integration results are recorded at commit time.

## Foot planting fix (2026-10-31)

- **What was wrong:** with `[Game] foot planting = true` an NPC's mesh exploded into huge stretched polygons. It had been broken on screen since V3 (and in the OSG build before it) and had only ever been checked in its off state, so no earlier check saw it.
- **Cause:** `ActorIkController::solveLeg` solved the leg in world space and wrote each bone back as a full local matrix (`inverse(parent) * newWorld`, then `BonePose::setLocal`, which decomposes it into translation, rotation and scale). Actors are scaled non-uniformly (race weight scales x and y), so a world rotation of a scaled skeleton is a shear in local space, and the decomposition turned it into wild local scales (a foot scale of 1e32 was logged). The animation rewrites rotation and translation every frame and never the scale, so the bad scales stayed and compounded from frame to frame. Actors with weight 1 were fine, which hid it.
- **Fix:** the leg maths moved out of the controller into `mwrender/footikmath.*` (pure functions, no world or GPU). The IK now changes only the thigh, calf and foot rotations, computed on the bones' scale-free rotation chain (`BonePose::getWorldRotation`, `getParentWorldRotation`, written with `setLocalRotation`); translation and scale stay as animated. Joint positions still come from the full world matrices.
- **How it is checked now:** `apps/openmw_tests/mwrender/testfootik.cpp` (8 tests, no GPU): a target at the current ankle keeps the pose, bone lengths hold and every value stays finite, the ankle follows the ground and the knee stays between hip and ankle, the foot keeps its orientation plus the tilt, and a 60-frame run on a skeleton with a non-uniform root and bone scale leaves every translation and scale exactly as animated. In game: a Seyda Neen frame with foot planting on shows the NPCs standing with no distortion (`compare/vsg/fp_fix1.png`). Not checked: a partial animation write (a track that writes translation but not rotation) could still leave the IK rotation in a bone's saved base.
- **Creatures:** the controller is created for every actor, but it binds only bones named `bip01 pelvis`, `bip01 l/r thigh`, `calf` and `foot`; creatures without them (the siltstrider) bind nothing and are not touched.

### Foot planting fix, review follow-up (2026-10-31)

- **Tests that can fail:** the first tests copied the new write-back and so could not fail on the old one. `FootIk::applyLeg`, `applyLean`, `restoreBase` and `markRotationWritten` / `markTranslationWritten` are now the controller's own write path (the controller calls them), and the tests call them. `OldWriteBackBlowsUpScalesOnAScaledSkeleton` runs the old write-back (kept in the test only) and asserts the scale blow-up. `NewWriteBackKeepsEveryLocalMatrixARotationTimesItsScale` fails when `solveAndWrite` is switched to the old write (checked) and passes on the new one. What stays uncovered: `ActorIkController::update` and `applyPostAnimation` themselves (they need the world and physics); the restore test for translation-only animation was written after the change and not run against the old whole-matrix compare.
- **Spine lean:** it overwrote the whole 3x3 block with a unit rotation, dropping any bone scale (and then taking it for the base), and took the parent rotation from a sheared matrix. It now rotates the bone's own rotation through `applyLean` on the scale-free chain. The spine bone's scale in a real skeleton was not logged.
- **Restore:** rotation and translation are saved, compared and restored separately, as the exact stored fields (no `decompose`, no scale orientation). So an animation that writes only a thigh, calf or foot translation no longer leaves the IK rotation as the base (the tilt grew every frame), and the pelvis (translation only) never touches rotation or scale. This also answers the `decompose` / `setLocal` worry: foot IK, lean and pelvis no longer call `setLocal`.
- **Limits accepted:** a mirrored (negative) scale is not handled by the scale-free chain (it would bend about the wrong axis); positive non-uniform scale is approximate within a few degrees at 8% anisotropy. Pelvis translation is correct (`inverse(parentWorld)` includes the scale).
- **Biped creatures** (Dremora, Golden Saint with `bip01` thigh, calf and foot bones) get the IK; their proportions and the fixed 0 to 2.8 hinge limit were not verified, as no creature was easy to place in the game.
- **Frames (Seyda Neen, 400 frames):** foot planting alone and with lean show the NPC standing; lean alone differs from the clean frame by 1256 (a stationary player does not lean).
