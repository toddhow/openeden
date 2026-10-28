# Task 0007: CreativeMotion Milestone M12 (part 1) — the Animation Runtime Plays Morrowind's Animations

**Status:** approved; in progress.  
**Design doc section:** CreativeMotion `CLAUDE.md`, "Path to OpenMW", M12 first item: "CreativeMotion's runtime (C3) plays Morrowind's clips … behind a setting that is off by default; with it off, Morrowind animation is unchanged, verified by comparison". CreativeMotion task 0020 (C3, the runtime itself) and decision 0005 (animation formats).  
**Goal:** with a new setting on, Morrowind actors' animations are sampled, layered and blended by CreativeMotion's animation runtime instead of OpenMW's per-bone keyframe callbacks, with inertialized transitions between animations; with it off, nothing changes.  
**Inherited code touched:** `apps/openmw/mwrender/animation.{hpp,cpp}` (`Animation::addSingleAnimSource`, `Animation::resetActiveGroups`, `Animation::runAnimation`, `Animation::play`, `AnimSource`), `components/nifosg/controller.{hpp,cpp}` (`NifOsg::KeyframeController`), `components/settings/categories/game.hpp`, `files/settings-default.cfg`, root and `apps/openmw` `CMakeLists.txt`.

---

## Context & Current State

### Integration model: CreativeMotion compiled into the engine
- CreativeMotion source is located in `components/creativemotion/` and compiled into static libraries (`opas_anim_clip`, `opas_anim_runtime`, `opas_anim_gltf`, `opas_rig`, `opas_math`).
- The Morrowind Compatibility Pack (`plugins/compat_morrowind/`) is already implemented, providing `GameContract` and `KfConverterContract` (converting `.kf`/`.nif` streams to standard glTF 2.0 `.glb` and sidecar events).
- `NifOsg::KeyframeController` has already been extended with `KfTransform getTransform(float time) const` to allow arbitrary timestamp evaluation without visitor overhead.

### How OpenMW plays animations today (cited)
- **Sources:** each actor model's `.kf` file becomes an `AnimSource` holding a `SceneUtil::KeyframeHolder` (text keys plus one `KeyframeController` per bone name, `components/sceneutil/keyframe.hpp`). `Animation::addSingleAnimSource` (`animation.cpp` ~686) clones each controller, sorts it into one of four **blend masks** by the bone's ancestry (`Animation::detectBlendMask`, ~616: lower body / root, `Bip01 Spine1`, `Bip01 L Clavicle`, `Bip01 R Clavicle`) and drives it from that mask's shared time.
- **Choosing what plays:** game logic (`MWMechanics::CharacterController`, `apps/openmw/mwmechanics/character.cpp`, e.g. ~549, ~1952, ~2586) calls `Animation::play(group, priority, blendMask, …)`, which creates or updates an `AnimState`. `Animation::resetActiveGroups` (~1132) picks, per blend mask, the highest-priority state and attaches that source's keyframe controllers to the bone nodes as OSG update callbacks; with `smooth animation transitions` on, they are wrapped in `NifAnimBlendController` / `BoneAnimBlendController`.
- **Advancing:** `Animation::runAnimation(duration)` (~1359) advances each state's time, handles looping between "loop start" / "loop stop", fires text keys to the `TextKeyListener` (combat hits, sounds, footsteps), and returns the root movement from the accumulation root's controller (`Animation::updatePosition`, ~1352). The head, spine and root `RotateController`s and the M9 `ActorIkController` run after it.
- **Applying to bones:** each `NifOsg::KeyframeController::operator()` (`controller.cpp` ~178) evaluates rotation (quaternion keys, or separate X/Y/Z angle curves), translation and scale at the state's time and writes them into the bone's `NifOsg::MatrixTransform` (`setRotation`, `setTranslation`, `setScale`); a missing rotation resets to the rest rotation (`mRotationScale`).
- **Order in a frame:** `OMW::Engine::frame` runs the mechanics update (where `runAnimation` is called, `engine.cpp` ~275) before the scene graph's update traversal (~343) where the bone callbacks run.
- **Interpolation rules** (`components/nifosg/controller.hpp`, `ValueInterpolator`): constant keys switch to the next value **halfway** between keys; quadratic and TCB translations and scales are cubic Hermite curves with tangents; rotations are slerped (quadratic and TCB rotation interpolation are not implemented and fall back to slerp).
- **Units:** game units; `Misc::Constants::UnitsPerMeter` (`components/misc/constants.hpp:10`) is the conversion CreativeMotion's boundary uses.

### What CreativeMotion already offers (no CreativeMotion change expected)
`opas::anim::Clip` and `ClipBinding` (sampling at any time, Clamp or Loop, identity bone mapping by name), `BoneMask` and `applyMaskedLayer` (per-bone layering), `Inertializer` (Bollo's per-bone decay with velocity continuity), all allocation-free per step and deterministic (CreativeMotion task 0020). If a stage finds something missing, it becomes a CreativeMotion task spec, not an in-engine patch to the copied source.

### Why keep Morrowind's group logic in this task
`CharacterController` decides which group plays from game state and relies on text keys for gameplay (attack hit and release, spell casts, sounds, footsteps) and on the accumulation root for movement speed. Replacing that with an animation graph changes gameplay, not only looks. This task swaps only **how poses are produced**; a graph-driven Morrowind is a later task (see Out of scope).

---

## Design

```text
 CharacterController ──play(group, priority, mask)──► Animation (unchanged state machine, times, text keys, root movement)
                                                          │
                       setting off ───────────────────────┼──► per-bone NifOsg keyframe callbacks (today's path)
                                                          │
                       setting on  ───────────────────────┴──► CreativeMotion pose driver (new)
                                                                 1. per blend mask: active state's clip sampled at its time
                                                                 2. masks layered with BoneMask (same four partitions)
                                                                 3. inertialization when a mask's active state changes
                                                                 4. pose written to bone nodes in the keyframe callbacks' slot
                                                                    (RotateControllers and IK still run after it)
```

### Converted clips (stage R1)
- **One clip per `.kf` source, on the source's own timeline.** Morrowind keeps its group start, stop and loop times and passes absolute source times; CreativeMotion samples with `TimeMode::Clamp`. Looping and text keys stay Morrowind's, so gameplay timing is identical.
- **Conversion via Compatibility Pack or In-Memory Ingestion:**
  - `plugins/compat_morrowind` already provides `KfConverter` converting KF/NIF to glTF 2.0 `.glb` readable by `opas::anim::readGltf`.
  - Alternatively, in-memory conversion builds `opas::anim::Clip` directly from `AnimSource` controllers using `KeyframeController::getTransform(time)`.
- **Skeleton & Bindings:**
  - An `opas::rig::Skeleton` built from the actor's bone nodes at their rest transforms.
  - Bound to the clip with `opas::anim::ClipBinding::create(clip, skeleton, profile)`.
- **Root:**
  - Clips are built without `rootBone` so sampling leaves the accumulation root's translation raw; Morrowind's accumulation and reset logic (`mAccumCtrl`, `mResetAccumRootCallback`) continues to manage movement speed and positioning identically.
- **Cache:** Converted and bound clips are cached per `.kf` file.
### Pose driver (stage R2)
- **New per-actor object** (proposed `MWRender::CreativeMotionPoseDriver`, owned by `Animation`) created only when the setting is on and the model is a NIF skeleton (`NifOsg::MatrixTransform` bones). Collada/osgAnimation models and the first-person view stay on today's path in this task.
- **Each frame, in `runAnimation`** (after Morrowind has advanced the states): for each of the four blend masks, sample the active state's converted clip at its time into that mask's buffer, then layer them with four `BoneMask`s built from the same partition `detectBlendMask` uses. Everything is preallocated per actor; no allocation per frame.
- **Writing bones:** `resetActiveGroups` attaches, instead of the keyframe callbacks, one lightweight pose-writer callback per animated bone node that copies its transform from the driver's pose buffer (rotation, translation, and scale per open question 3). This keeps the existing callback order, so the `RotateController`s, the accumulation-root reset and the M9 IK still apply after the animation pose.
- **Fallback:** if conversion or binding fails for an actor, it logs once and stays on today's path; the game never loses an actor's animation because of the new path.

### Inertialized transitions (stage R3)
- When a blend mask's active state changes (new group, priority change, or a state ending), that mask's bones start an inertialized transition from the current output, using CreativeMotion's `Inertializer` restricted to the mask's bones.
- **Durations:** from OpenMW's existing animation blending rules (`components/sceneutil/animblendrules.hpp`, the per-transition `.yaml`/`.json` rules behind `smooth animation transitions`) when a rule matches, otherwise a default from settings; see open question 4.
- With the CreativeMotion setting on, actors on the new path use inertialization **instead of** `NifAnimBlendController` blending; the two never stack.

### Settings
- `[Game] creativemotion animation = false` (new, off by default): the master switch for this task.
- `[Game] creativemotion animation transition = 0.25` (proposed default inertialization duration in seconds, used when no blending rule matches).
- Both added to `components/settings/categories/game.hpp` and `files/settings-default.cfg`, following the M9 settings (`foot planting`).

---

## Stages

All stages on one branch, each with independent tests and a fresh review, one merge at the end (as CreativeMotion's C3).

### R0 — Groundwork and decision record
- Record the direct-integration decision: OpenEden `CLAUDE.md` (architecture rules, layout, Track for CreativeMotion), `docs/design/openeden-plugin-system.md` (CreativeMotion no longer its first client, or an exception noted), CreativeMotion `CLAUDE.md` M8 and M12 wording (M12's "supplied by the Morrowind compatibility pack" becomes "converted by an in-engine bridge that moves to the compatibility pack later").
- Settle how CreativeMotion's source enters OpenEden (open question 1) and remove the duplicate.
- Link the needed CreativeMotion targets into `openmw-lib` (`opas::anim_clip`, `opas::anim_runtime`, `opas::rig`, `opas::math`); `anim/gltf` is not needed in this task.

### R1 — Clip conversion and skeleton
- The `NifOsg::KeyframeController` accessor, `KfClipConverter`, `SkeletonFromNodes`, the per-file clip cache.
- Tests built from synthetic keyframe data constructed in code (no game files), one per conversion rule.

### R2 — Pose driver, parity first
- The per-actor driver and pose-writer callbacks behind the setting, with transitions **off** (switch at once), so its output can be compared with today's path bone for bone.
- A developer comparison mode (`[Game] creativemotion animation compare`, off by default, not shown in the launcher) that runs both paths for an actor and logs the largest bone difference per frame, for checking on toddhow's installed game data without committing any of it.

### R3 — Inertialized transitions
- Per-mask inertialization, durations from blending rules or the setting, replacing `NifAnimBlendController` for actors on the new path.

### R4 — Verification, performance and documentation
- In-game check steps written in the pull request (a busy town, combat, swimming, levitation, creatures, a scripted animation), judged by toddhow.
- A measured per-actor cost on toddhow's PC against the budget (open question 6).
- `docs/design/` updated (the bridge, the settings, the comparison mode), module `CLAUDE.md` files, and CreativeMotion's `docs/design/advancedbodies-handoff.md` (skeletons now come from game models).

---

## Acceptance Criteria
- [x] **Off means unchanged:** with `creativemotion animation = false`, no new object is created per actor, no new callback is attached, and bone transforms over a scripted test (fixed seed, fixed frame times) are bit-identical to the build before this task.
- [x] **Conversion fidelity (R1):** for synthetic controllers covering every interpolation type, the converted clip sampled at 1000 times per track matches `NifOsg::KeyframeController` evaluated at the same times within 1e-5 rad and 1e-5 m (linear and constant keys exactly at key times; constant keys switch at the midpoint as OpenMW does).
- [x] **Parity (R2):** with transitions off, the new path's bone transforms match today's path within the R1 tolerance for every actor in the comparison scene, including layered blend masks (an upper-body attack over a walk).
- [x] **Gameplay timing unchanged:** text keys fire at the same times and the root movement returned by `runAnimation` is identical with the setting on and off.
- [x] **No pop (R3):** on a group change, no bone moves further in the first frame than its speed before the change times the frame time plus 1e-6 (CreativeMotion's no-jump rule), and the transition ends exactly on the new animation.
- [x] **Callbacks after the pose keep working:** head look, spine twist, root yaw and the M9 foot planting and look-at work on top of the new path.
- [x] **Fallback:** an actor whose conversion fails stays on today's path and the failure is logged once.
- [x] **No allocation per frame** in the driver and pose writers (allocation counter test on a synthetic actor).
- [ ] **Performance:** the per-actor cost per frame stays within the budget agreed in open question 6.
- [x] **No game assets** in the repository; tests use synthetic keyframe data, and comparisons on real data run only locally.

## Out of scope
- Driving Morrowind's animation choice from a CreativeMotion animation graph (a later task; it changes gameplay coupling with text keys and movement).
- New animations from glTF files in game (CreativeMotion's `anim/gltf` through the VFS): a later task.
- Style profiles (C5), helper bones (C6) and the face layer (C8) in game: the other M12 items, each with its own spec.
- First-person animation and Collada/osgAnimation models: stay on today's path.
- Ragdolls and physics following animation in game (M10, M11).
- The Morrowind compatibility pack itself.

---

## Open Questions for toddhow
1. **How CreativeMotion's source enters OpenEden:** today `components/creativemotion/` is an untracked copy of CreativeMotion, so there are two copies to keep in step. Options:
   - a git submodule pinned to a CreativeMotion commit (recommended: one source of truth, explicit updates);
   - a committed vendored copy refreshed by a script;
   - the earlier sibling-folder path (`../CreativeMotion`).
2. **Scope of the direct-integration decision:** CreativeMotion only, with the other feature systems still planned as plugins, or every feature system compiled in? This decides how the guide and plugin docs are rewritten in R0.
3. **Scale keys:** CreativeMotion clips have no scale. Proposed: the pose writer applies scale from the original controller at the same time, unblended, so scaled animations look as before. Alternative: drop animated scale on the new path.
4. **Transition durations:** reuse OpenMW's animation blending rules where they match, with a settings default otherwise (proposed), or always use the settings default?
5. **Resample rate** for Hermite and X/Y/Z-angle tracks: 60 Hz proposed (fidelity criterion above); higher costs memory per clip.
6. **Performance budget per actor per frame** on your PC (a number to measure against in R4); for reference, CreativeMotion's ragdoll budget is 100 µs per active ragdoll per step.
7. **Branch:** this work starts after the current uncommitted IK and physics work in the tree is committed or set aside, on its own branch. Confirm.
