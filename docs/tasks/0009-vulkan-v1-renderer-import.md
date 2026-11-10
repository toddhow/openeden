# Task 0009: Track V stage V1 — Import vsgopenmw's renderer

**Status:** CLOSED 2026-11-10 by toddhow (remaining checks are follow-ups, not blockers; listed in `docs/toddhow-checks.md`, "Known open items from Track V"). Earlier status: draft, awaiting toddhow's approval. Depends on task 0008 (V0).
**Design doc section:** `docs/decisions/0002-vulkan-renderer-on-vulkanscenegraph.md` ("Starting point", "vsgopenmw is a second upstream"); `docs/research/0001-vulkan-renderer.md`, "vsgopenmw".
**Goal:** merge vsgopenmw's Vulkan renderer (branch `vsg-import` from task 0008) into the `vulkan` branch so that OpenEden builds on Windows and Linux with VulkanSceneGraph instead of OpenSceneGraph rendering, starts, shows the main menu and loads an exterior and an interior cell. Making the game fully playable is task 0010; OpenEden's own rendering features return in task 0011.
**Workflow:** full loop. The merge itself is reviewed hunk by hunk by a fresh reviewing subagent before toddhow.

---

## Context (facts this spec relies on)

- **Shared history:** OpenEden and vsgopenmw both descend from OpenMW. `git merge vsg-import` on `vulkan` computes the merge base `f724b05c57ee2f32e37a1087053a01dbc0847ea9` (OpenMW master, 2023-10-27) by itself. vsgopenmw's side is 18 commits (945 files, +87,235 / −7,524); OpenEden's side is 5,201 upstream commits to OpenMW 0.51 plus OpenEden's own work.
- **Conflict surface (measured 2026-10-28 against OpenEden `442eec6e2f`):** 259 files modified by vsgopenmw; 233 of them also changed on OpenEden's side, so they will conflict. By area: `apps/openmw/mwgui` 47, `mwworld` 28, `mwmechanics` 21, `mwlua` 17, `mwrender` 10, `mwphysics` 10, `components/resource` 8, `mwclass` 8, `mwinput` 7, `components/detournavigator` 6, `CI` 6, `components/vfs` 5, `mwscript` 5, and smaller counts elsewhere. Six files vsgopenmw modified are deleted or renamed on OpenEden's side, including `components/nifbullet/bulletnifloader.cpp` and `components/resource/bulletshape.{hpp,cpp}` (OpenEden replaced Bullet with Jolt).
- **What vsgopenmw replaced:**
  - new components `animation` (53 files), `mwanimation` (35), `pipeline` (31), `render` (18), `view` (14), `vsgutil` (62), `vsgadapters` (`nif`, `mygui`, `osg`, `sdl`; 27), `esm3terrain` (2); 83 new shaders under `files/shaders`;
  - new `apps/openmw/mwrender` files built around `rendermanager.{hpp,cpp}`, `animcontext`, `npc`, `bodyparts`, `wieldingcreature`, `face`, `scene`, `weather`, `map`/`worldmap`, `preview`, `projectiles`, `effects`, `shadow`, `simplewater`; a new engine `apps/openmw/vsgengine.{hpp,cpp}` with `game.hpp`, `boot.hpp`, `arguments.hpp`; 25 new files in `apps/openmw/mwstate` (a redesigned game-state flow);
  - **removed from the build** (still in its tree, commented out in `components/CMakeLists.txt`): `sceneutil`, `shader`, `fx` (post-processing), `stereo` (VR), `esmterrain`; `nifosg` is gone; in `apps/openmw/CMakeLists.txt` its `mwrender` no longer builds `bulletdebugdraw`, `navmesh` (the navmesh debug draw), `objects`, and its `mwgui` no longer builds `postprocessorhud`; `mwworld` no longer builds `groundcoverstore`;
  - **math stays OSG:** no OSG libraries are linked; OSG headers are bundled in `extern/osg` and `components/vsgadapters/osg` implements `Matrixf`, `Matrixd`, `Quat` and `MatrixDecomposition`. OpenEden keeps this arrangement until task 0013 (V5) replaces OSG math.
- **Newer code that depends on what vsgopenmw dropped** (files added on OpenEden's side since 2023 that include `sceneutil`, `nifosg`, `fx`, `stereo`, `shader`, `esmterrain`, OSG utility libraries or `mwrender` headers): `apps/openmw/mwrender/animblendcontroller.{hpp,cpp}`, `distortion.hpp`, `actorikcontroller.cpp`, `creativemotiondriver.{hpp,cpp}`, `joltdebugdraw.cpp`, `apps/openmw/mwlua/landbindings.cpp`, `components/resource/animblendrulesmanager.{hpp,cpp}`, `components/resource/physicsshapemanager.cpp`, `apps/opencs/view/render/objectmarker.cpp`, and the tests `apps/components_tests/mwrender/test_creativemotion_runtime.cpp`, `plugin/test_compat_morrowind.cpp`, `sceneutil/osgacontroller.cpp`. A merge does not flag these; they fail to compile afterwards.
- **OpenEden's rendering-side features** that are built on what vsgopenmw replaced: foot IK, look-at, smoother turns and compound hitbox sampling (`apps/openmw/mwrender/actorikcontroller.cpp`, 734 lines, using `osg::MatrixTransform`, `osg::NodeVisitor`, `SceneUtil::PositionAttitudeTransform`, `SceneUtil::NodeCallback`); the CreativeMotion animation runtime (`creativemotiondriver.cpp`, using `NifOsg::KeyframeController`, `NifOsg::MatrixTransform`, `SceneUtil::KeyframeHolder`); the Jolt debug draw (`joltdebugdraw.cpp`, OSG geometry); the inertial lean in `ActorIkController`; the Morrowind compatibility pack's `.kf` converter (`plugins/compat_morrowind/src/kf_converter.cpp`, using `NifOsg::Loader` and `NifOsg::KeyframeController`); RotateController changes (`rotatecontroller.cpp`).
- **The plugin host is not wired into the game:** only tests create `HostImpl` / `LifecycleManager` (`apps/components_tests/plugin/`); `apps/openmw` does not. There is no plugin wiring to carry across to `vsgengine.cpp`.

## Merge policy

Every conflicted or broken file is resolved by these rules, in this order. The pull request lists every file with the rule applied.

1. **Physics is OpenEden's.** Jolt wins everywhere: `apps/openmw/mwphysics`, `components/nifjolt`, `components/physicshelpers`, `components/resource/physicsshape*`. Drop vsgopenmw's Bullet code (`nifbullet`, `bullethelpers`, `bulletshape*`) and its Bullet debug draw. Where vsgopenmw changed physics files to talk to its renderer (for example a scene-node pointer it no longer has), keep OpenEden's physics and adapt only the call to the new renderer.
2. **Rendering is vsgopenmw's.** In `apps/openmw/mwrender`, `components/{animation,mwanimation,pipeline,render,view,vsgutil,vsgadapters,terrain,esm3terrain,myguiplatform}`, `files/shaders`, and the engine (`vsgengine.*`, `game.hpp`, `boot.hpp`), take vsgopenmw's version, then re-apply on top only the OpenMW 0.49→0.51 renderer-side changes that still make sense on VSG, each named in the pull request; renderer changes with no VSG equivalent are listed for task 0012 (V4) rather than ported now.
3. **Everything else is OpenEden's (OpenMW 0.51) behaviour with vsgopenmw's glue.** For `mwgui`, `mwworld`, `mwmechanics`, `mwlua`, `mwclass`, `mwinput`, `mwscript`, `mwsound`, `mwbase`, `components/{esm*,vfs,files,settings,detournavigator,nif,lua*}` and the rest: start from OpenEden's file and apply only vsgopenmw's changes that connect it to the new renderer (types and calls of `mwrender`, `mwanimation`, `vsgutil`, `MWBase` interfaces). Never take vsgopenmw's older version of game logic, Lua bindings, ESM loading or settings: those are 5,201 upstream commits newer on OpenEden's side.
4. **Upstream fixes are not lost.** After resolving, run the guide's merge audit ("Upstream merges and the Jolt branch") with the merge base above: list the lines OpenMW added between `f724b05c57` and OpenEden's base that OpenEden had but the merge result lacks, per file, and account for each one (kept, ported to VSG, deliberately dropped with reason, or deferred to task 0012 with reason).
5. **Scope stays OpenEden's.** ESM4 and later-game code stays (vsgopenmw's TES3-only scope is rejected, decision 0002).
6. **OpenEden's settings stay.** Keep every setting in `components/settings/categories` and `files/settings-default.cfg` that OpenEden has, including M9, inertial locomotion and CreativeMotion settings; renderer settings that the VSG renderer does not read are kept, listed, and handled in task 0012.
7. **Shims are a last resort.** Prefer adopting the newer signature at the call site over adding compatibility layers (vsgopenmw's own `VSG_MERGE_PLAN.md`, "Rules of engagement"). Every unavoidable shim is marked with a `// openeden-vsg:` comment naming the task that removes it.
8. **Origin is kept.** Files taken from vsgopenmw keep their notices; the merge commit message names vsgopenmw commit `2830e7e2b4` plus the `vsg-import` compatibility commits; vsgopenmw's authors are credited (`AUTHORS.md` or a new `CREDITS` entry, as toddhow prefers).

## OpenEden features during V1

The features listed in "OpenEden's rendering-side features" cannot work until task 0011 rebuilds them on vsgopenmw's animation system. In V1 they are **compiled out** behind a CMake option `OPENEDEN_VSG_PENDING_FEATURES` (default `OFF` on `vulkan`), not deleted:

- `actorikcontroller.*`, `creativemotiondriver.*`, `joltdebugdraw.*`, `animblendcontroller.*`, `distortion.hpp`, `rotatecontroller.*` changes, and the compatibility pack's `kf_converter` are excluded from the build; their call sites are guarded by the same option.
- Their settings stay in place and do nothing while excluded; the log says once, at startup, which settings are inactive on this build and why.
- Their tests (`test_creativemotion_runtime.cpp`, `test_compat_morrowind.cpp`, the IK and inertial-lean parts) are excluded with the same option, listed in the pull request; tests for removed OSG components (`sceneutil/osgacontroller.cpp`, `nifosg/testnifloader.cpp`, `shader/*`, `fx/*`) are excluded and listed for task 0012 or 0014 to restore or replace.
- `components/inertialmotion` (no rendering) and CreativeMotion's own libraries stay built; the inertial movement itself (`movementsolver.cpp`, `character.cpp`) stays active, since it does not render.

## Inputs / outputs

- **Branch:** `vulkan`, with one merge commit of `vsg-import` plus follow-up commits that fix the build, each small and described.
- **Build:** the root `CMakeLists.txt` no longer finds or links OpenSceneGraph libraries (OSG headers come from `extern/osg`, as in vsgopenmw); VSG, vsgXchange and glslang come from task 0008's `extern` setup; the editor (`openmw-cs`) and other tools may be disabled in this stage if they do not build (vsgopenmw's own known issue, `vsgopenmw-roadmap.doc`, "vsgopenmw-apps"); disabled targets are listed and return in task 0014.
- **Notes:** `docs/research/0003-vsg-import-merge.md` with the conflict list and the rule applied to each file, the merge audit table, the shims added, and everything deferred to later stages.

## Constraints

- One merge, not a re-implementation: vsgopenmw's history is kept so later vsgopenmw fixes can be merged again.
- No game assets; screenshots from Morrowind data stay outside the repository.
- Windows build rules from the guide's Pitfalls; do not push.
- Do not fix vsgopenmw's known gameplay bugs here (movement, navigator); note them for task 0010.

## Acceptance criteria

- [ ] `vulkan` contains the merge of `vsg-import`; `git log` shows vsgopenmw's history and the merge commit names the vsgopenmw commits.
- [ ] `openmw` builds on Windows (MSVC) and Linux (GCC, `-Werror` as CI) with no OpenSceneGraph library linked (checked with `dumpbin /dependents` on Windows and `ldd` on Linux: no `osg*` or `OpenThreads` libraries).
- [ ] With toddhow's Morrowind data, the game starts, reaches the main menu, starts a new game or loads a save, and renders an exterior cell (Balmora) and an interior cell; `tools/render_compare.sh` runs on the Vulkan build and its images are recorded (differences from the OSG references are expected and not yet judged).
- [ ] `components-tests` and `openmw-tests` build and pass, apart from the tests excluded and listed above.
- [ ] Every one of the 233 conflicted files and the 15 newer dependent files appears in `docs/research/0003-vsg-import-merge.md` with the rule applied.
- [ ] The merge audit accounts for every upstream line the merge dropped.
- [ ] Every `// openeden-vsg:` shim names the task that removes it.
- [ ] The features in "OpenEden features during V1" are excluded behind `OPENEDEN_VSG_PENDING_FEATURES`, their settings log as inactive, and nothing about them is deleted.

## Out of scope

- Movement, combat, AI pathing, saving, integration tests passing (task 0010).
- Rebuilding foot IK, look-at, the CreativeMotion pose driver, the Jolt debug draw and the compatibility pack converter on VSG (task 0011).
- Renderer parity: post-processing, VR, groundcover, object paging, weather particles and the rest (task 0012).
- Replacing OSG math (task 0013); the editor and tools (task 0014).

## Open questions for toddhow

1. **Credit for vsgopenmw's authors:** entries in `AUTHORS.md`, or a separate `CREDITS.md` section for code taken from vsgopenmw?
2. **Editor during the transition:** acceptable for `openmw-cs` not to build on `vulkan` until task 0014? (Proposed yes; the OSG editor stays available on the branches outside Track V.)
3. **vsgopenmw's redesigned game states** (`apps/openmw/mwstate`, 25 new files: background loading, abort loading, emergency save): take them with the renderer (proposed, they are tied to its engine), or keep OpenEden's state flow and adapt?
