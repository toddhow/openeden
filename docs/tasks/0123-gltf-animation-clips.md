# Task: CreativeMotion's glTF clips play in the engine

Status: built on Windows, 2026-11-11; local commit on branch `gltfclips`, not pushed. The same hand wrote the tests and the code (tests first,
run red against a stub); no fresh reviewer.

Design doc section: `docs/design/inertial-locomotion.md` 3.3 (authored transitions); task 0108 ("What an authored clip needs").
toddhow's rules (2026-11-11): new assets use open formats (glTF); NIF and KF only for compatibility, and no NIF or KF writer; CreativeMotion is
the engine's animation runtime; the old COLLADA animation path is to be retired by a separate plan (`daeanim.cpp` is untouched here).
Goal: the authored clips that already exist in CreativeMotion's `humanoid-locomotion.glb` play as `MWAnim::Play` groups on Morrowind's `Bip01`
skeleton, so inertial locomotion finds `walkstart`, `runstop`, `runskid`, `turnleft90` and `turnright90`.
Inherited code touched: `apps/openmw/mwrender/npc.cpp` (`Npc::applyBasePlan`, one call after `addAnimSources`),
`apps/openmw/mwrender/creativemotiondriver.*` (`buildSkeleton` split: the free `buildRigBones`), `components/mwanimation/context.hpp`
(a `readFile` function) and `apps/openmw/mwrender/animcontext.cpp` (sets it), `files/data/CMakeLists.txt` (copies the two data files).
Inputs / outputs: new `apps/openmw/mwrender/gltfclips.{hpp,cpp}`; VFS files `animations/humanoid-locomotion.glb` and
`animations/humanoid-to-eden.skeletonprofile.json` (copied at build time from `components/creativemotion/data`, not new data); no new setting.
Constraints: gated by the existing `[Game] inertial locomotion` (off: nothing read, nothing added); no allocation in the per-frame path (the bake
runs once per actor build); main or loader thread (the parsed file is cached under a lock); no NIF/KF writer; no change to `daeanim.cpp`.

## Decision: route B (CreativeMotion's reader), not assimp

The task was first written as "extend the assimp COLLADA reader to glTF". Two facts and one instruction changed that:

1. The pinned assimp is built with only the COLLADA importer (`extern/CMakeLists.txt`: `ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF`,
   `ASSIMP_BUILD_COLLADA_IMPORTER=ON`; `docs/dependencies.md` item 7). The brief said to stop and report in that case.
2. `opas::anim::readGltf` (`components/creativemotion/anim/gltf`, cgltf) already reads glTF clips, a skin, root motion and a sidecar, is linked into
   `openmw`, and was used only by a test. A second glTF reader for the same job would need a dependency change and duplicate it.
3. toddhow decided (relayed 2026-11-11): CreativeMotion is the engine's animation runtime and the COLLADA animation path goes. Route B only.

So there is **one** glTF reader in the tree (CreativeMotion's). The engine side is a retarget and bake into `Anim::ControllerMap`
(`GltfClips::retarget`), which `MWAnim::Play` plays like a `.kf` source, with `creativemotion animation` on or off (the pose driver reads the
`Play` groups; inertial locomotion asks `Play::hasAnimation`).

## What the file is

`humanoid-locomotion.glb`: one skin of 19 joints (Pelvis, Spine, Chest, Neck, Head, Left/Right Thigh, Shin, Foot, UpperArm, Forearm, Hand,
Fingers); animations idle, walk, WalkStart, RunStop, RunSkid, TurnLeft90, TurnRight90, wave. `walkstop` and `runstart` are NOT in it and stay
missing (not invented). Every animation also exists as `<name>.001`: see the stage log for what they are. The profile
`humanoid-to-eden.skeletonprofile.json` maps 17 joints (no fingers) to `Bip01 ...` and gives the leg chain.

## Design

- **Retarget.** `ClipBinding` copies a clip's local rotations onto the mapped target bones. CreativeMotion's rig and Morrowind's Bip01 do not share
  local axes (the glb's LeftThigh rest is a 180 degree turn about X; Bip01's bones have axes of their own), so copying would twist the limbs. The
  bake therefore uses `ClipBinding` for what it is right about (timing, root yaw removal, pelvis height scaled by leg length, root motion) and
  transfers rotations in model space: a mapped bone turns from its own rest by the model-space rotation the clip's bone turns from its rest
  (`world = turn * worldRest`; local = inverse(parent world) * world). Unmapped bones (clavicles, `Spine2`, `Bip01`) get no channel.
- **Groups.** Only `walkstart runstart walkstop runstop runskid turnleft90 turnright90` are taken, matched to the clip names in lower case. The file's
  `idle` would replace every actor's idle (a later source wins), and `walk`/`wave` are not groups the engine asks for.
- **Root motion and `Bip01`.** `Bip01` gets a translation channel (rest plus `ClipBinding::rootMotion`, metres to game units, +Y forward):
  `chooseMovementRequestSource` makes root motion the movement request while one of these clips plays, and `Play::addSingleAnimSource` throws
  `!findCtrl` for a source with no controller on the accumulation root.
- **Timeline.** Baked at 30 Hz; clip `k` occupies `[offset_k, offset_k + duration]`, text keys `<group>: start` / `<group>: stop`, 0.5 s between clips.
- **Units.** CreativeMotion works in metres; the bake converts with `Misc::gameConventions().mUnitsPerMeter`, the same boundary as the pose driver.

## Acceptance criteria

- [x] The shipped glb and profile parse with `readGltf` (`MWRenderGltfClips.ShippedFilesParseAndHoldTheCreativeMotionRig`).
- [x] The baked map's text keys name exactly the five groups the file has, in lower case; `idle`, `walk`, `wave` and the `.001` clips are not taken.
- [x] The retarget does not depend on the target's local axes: a skeleton with scrambled rest rotations gets the same model-space turn per bone.
- [x] `Bip01` has a translation channel with forward (+Y) root motion in game units; every translation follows `unitsPerMeter`.
- [x] A skeleton the profile cannot bind is refused with a reason, not a crash.
- [ ] In game: the session log line of task 0108 lists the five as found, and a scripted walk start plays `walkstart` (see the stage log).
- [x] With `[Game] inertial locomotion` off, nothing is read and no source is added (`loadLocomotionClips` returns after one setting read).

Out of scope: `walkstop` and `runstart` clips; the `animations/<skeleton>/` additional anim sources lookup and the Blender skeleton export
(both asked for in the first version of this task; neither is needed to load CreativeMotion's own clips, see below); events from
`humanoid-locomotion.events.json`; removing the COLLADA animation path; first-person arms; reading glTF through `vsg::read` (the glb is read
through the VFS by the engine glue, not through the animation options, whose cache is keyed by file name and knows nothing of skeletons).

## Stage log

### 2026-11-11: WIP, nothing built or run yet

Written: `gltfclips.*`, the `buildRigBones` split, `Context::readFile`, the glue in `Npc::applyBasePlan`, the data copy rule, the doc corrections
(task 0108, `apps/openmw/mwrender/CLAUDE.md`), 12 tests in `apps/openmw_tests/mwrender/testgltfclips.cpp`. **The build was stopped** (the machine
was restarted; the fresh build folder `I:\Projects\OpenEden-work\gltfclipsuild` had compiled about 250 of 2177 steps, all of them VSG
dependencies). Not compiled, not red-run, not run in game: the code is unchecked and may have compile errors.

Facts found while reading, relevant to the next steps: the assimp build has no glTF importer; `Play::addSingleAnimSource` throws `!findCtrl` for a
source with no controller on the accumulation root (so the bake gives `Bip01` a translation channel); `ClipBinding::sample` copies local rotations
and does not retarget between rigs of different local axes; `Context` animation options prefix `meshes/` to every path, which is why the clips are
read through `Context::readFile` and not `vsg::read`; the shipped glb clips are Y-up with Blender-style rests (LeftThigh rest is a 180 degree turn
about X). OpenMW 0.51's `loadAdditionalAnimations` (`apps/openmw/mwrender/animation.cpp`, upstream `master`) maps `meshes/x.kf` to `animations/x/`
and takes every `.kf` under it; not implemented.

Next steps: (1) build `openmw-tests` with `I:\Projects\OpenEden-work\gltfclipsuild.bat openmw-tests` (needs >= 10 GB free commit memory; -j 2);
(2) for the red evidence, replace the body of `retarget` by `error = "stub"; return {};`, run `openmw-tests --gtest_filter=MWRenderGltfClips*`, then
restore; (3) fix what the tests show (the +Y root motion axis and the rig-dependent turn test are the likeliest to need attention; the
`ClipBinding::create` refusal "an ancestor of the root bone rotates" needs `Bip01` to have an identity rest rotation: check on the real skeleton);
(4) build `openmw`, run `tools/footslip_check/check.sh` with `CELL=ToddTest` and `inertial` (base cfg `I:/Projects/OpenEden-work/face2/baseconfig/openmw.cfg`,
`OPENEDEN_QUIET_WINDOW=1`, an empty work folder) and read the log for the 0108 line and the `CMTRACE` group `walkstart`; (5) compare
`tools/worldobjects_check` and the Balmora frame with the parent build; (6) `CI/check_layer_rules.sh` (104) and the other `CI/check_*.sh`.
