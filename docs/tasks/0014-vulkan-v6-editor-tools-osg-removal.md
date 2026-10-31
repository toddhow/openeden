# Task 0014: Track V stage V6 — Editor and tools on Vulkan, OpenSceneGraph removed

**Status:** approved by toddhow (2026-10-29) with the answers below. Depends on tasks 0012 (V4) and 0013 (V5).
**Design doc section:** `docs/decisions/0002-vulkan-renderer-on-vulkanscenegraph.md` ("The OpenGL build is not kept alongside … then OSG is removed"); `docs/design/eden-studio-plan.md`, Phase 1 (the editor's viewport becomes Eden Studio's).
**Goal:** every application builds and runs on the Vulkan build, the editor's 3D views render through VSG, and no OpenSceneGraph code, header or library remains anywhere in the repository or the build. This ends Track V.
**Workflow:** full loop.

---

## Context (facts this spec relies on)

- **The editor (OpenMW-CS) renders with OSG in Qt:** 60 files in `apps/opencs` use OSG; `apps/opencs/view/render` is 69 files, 16,328 lines (render views for cells, terrain editing, object and instance markers, pathgrids, lighting); the Qt viewport is `apps/opencs/view/render/scenewidget.{hpp,cpp}` through `extern/osgQt`. The OpenEden-era file `apps/opencs/view/render/objectmarker.cpp` depends on components vsgopenmw dropped (task 0009). vsgopenmw does not build the editor (`vsgopenmw-roadmap.doc`, "vsgopenmw-apps"), so there is no code to transplant here.
- **vsgQt** (https://github.com/vsg-dev/vsgQt, MIT, latest tag `v0.5.0`, active 2026-08) integrates VSG with Qt; check whether it supports OpenEden's Qt 6.6.3 before relying on it.
- **Other tools using OSG** (OSG build, `442eec6e2f`): `apps/navmeshtool` (4 files), `apps/essimporter` (3, for map images), `apps/esmtool` (2), `apps/benchmarks` (1). `apps/physicsobjecttool`, `niftest`, `bsatool`, `launcher`, `wizard`, `mwiniimporter` and `opencs_tests` use none.
- **Tests excluded in task 0009** for removed OSG components: `apps/components_tests/sceneutil/osgacontroller.cpp`, `nifosg/testnifloader.cpp`, `shader/*` (4 files), `fx/*` (2 files), plus any recorded there later.
- **OSG still in the tree after task 0013:** `extern/osgQt`, `extern/osg-ffmpeg-videoplayer` (video playback, which vsgopenmw modified, 4 files), the source directories vsgopenmw stopped building but kept (`components/sceneutil`, `nifosg` if present, `fx`, `stereo`, `shader`, `esmterrain`), OSG-related CMake (`cmake/` find modules, `OPENMW_USE_SYSTEM_OSG`, `OSG_STATIC`, `CheckOsgMultiview.cmake`), CI scripts installing OSG (`CI/install_debian_deps.sh` `libopenscenegraph-dev`, `libopenscenegraph161`, `libcollada-dom*`; the MSVC deps set), and OSG mentions in documentation.

## Work

1. **Editor viewport on VSG:** replace `scenewidget` and `extern/osgQt` with a Qt + Vulkan viewport (vsgQt if it fits, otherwise Qt's own Vulkan window support) hosting a VSG view; camera controls, selection, picking and resizing as today.
2. **Editor render views on VSG:** port `apps/opencs/view/render` onto the game's VSG renderer components (`components/render`, `pipeline`, `view`, `mwanimation`, terrain) so the editor draws cells exactly as the game does; terrain editing, object and instance markers, pathgrid editing, lighting modes and the other view features work as before.
3. **Tools:** `navmeshtool`, `essimporter`, `esmtool`, `benchmarks` build without OSG (math from task 0013; image work in `essimporter` through vsgXchange or a small image library already in the dependency set).
4. **Video playback:** `extern/osg-ffmpeg-videoplayer` either becomes a VSG video player (vsgopenmw already modified it; check what it uses at the merged commit) or is replaced; Morrowind's intro and logo videos play.
5. **Tests:** every test excluded in task 0009 is restored against the VSG equivalent or replaced by a test of the same behaviour, or deleted with the reason recorded (only for behaviour that no longer exists).
6. **Delete OSG:** remove the unbuilt OSG source directories, `extern/osgQt`, OSG CMake options and find modules, OSG packages from CI scripts and the Windows deps set, and OSG from `docs/dependencies.md`; update the guide's tech stack, layout and pitfalls (the vcpkg OSG crash pitfall no longer applies) and the README's dependency list.
7. **CI:** Windows and Linux CI build every target and run every gate on the Vulkan build (this also closes the guide's M7 item "The fork builds in CI" if the hosting question is settled by then).

## Constraints

- The editor keeps every capability it has on the OSG build; anything that cannot be kept is listed with a reason and toddhow's agreement.
- No OSG in any form at the end: no headers, libraries, CMake options, CI packages or source directories (except historical mentions in changelogs and decision records).
- No game assets; editor checks with Morrowind data run locally.

## Acceptance criteria

- [ ] `openmw-cs` builds on Windows and Linux, opens Morrowind's content, renders exterior and interior cells through VSG, and its terrain, instance, pathgrid and lighting editing work (written check steps in the pull request); `openmw-cs-tests` pass.
- [ ] Every application target builds on Windows and Linux; `navmeshtool` generates a navmesh database identical to the OSG build's for the same content (or differences are explained).
- [ ] Morrowind's videos play in game.
- [ ] Every test excluded in task 0009 is restored, replaced or deleted with a recorded reason.
- [ ] A CI check fails if any file includes an `osg` header or names an `osg` namespace, and it passes; `extern/osgQt`, `extern/osg` and the unbuilt OSG directories are gone; no OSG package is installed by CI scripts.
- [ ] `CLAUDE.md`, `README.md` and `docs/dependencies.md` describe the Vulkan-only build; the guide's Track V items are all ticked.
- [ ] All gates pass on Windows and Linux.

## Out of scope

- Eden Studio's new shell and workspaces (Eden Studio plan, phases 1 onward) beyond giving it a working Vulkan viewport.
- New editor features.

## Open questions for toddhow

1. **Editor viewport technology:** vsgQt (proposed if it supports Qt 6.6.3) or Qt's own Vulkan window with VSG embedded by OpenEden?
2. **Eden Studio timing:** port the existing OpenMW-CS views here (proposed, keeps the editor usable), or skip porting the old views and build Eden Studio's viewport directly as its phase 1?

### Answers (toddhow, 2026-10-29)

1. **Editor viewport:** vsgQt, if it supports Qt 6.6.3; otherwise Qt's own Vulkan window with VSG embedded by OpenEden.
2. **Eden Studio timing:** port the existing OpenMW-CS views here, keeping the editor usable.

## OSG source left by task 0013 (2026-10-31)

Task 0013 deleted every file under `apps/openmw`, `components` and `apps/components_tests` that the build does not compile or include and that still used OSG, apart from what the uncompiled `apps/opencs`, `apps/navmeshtool`, `apps/esmtool`, `apps/essimporter` and `apps/benchmarks` include, directly or through another such file. Those stay, and this task ports or replaces each one. `tools/` and CI's OSG grep (`CI/check_no_osg.sh`) know them by this list:

<!-- task-0014-osg-files -->
- `components/esmterrain/storage.cpp`
- `components/esmterrain/storage.hpp`
- `components/nifosg/controller.cpp`
- `components/nifosg/controller.hpp`
- `components/nifosg/fog.cpp`
- `components/nifosg/fog.hpp`
- `components/nifosg/matrixtransform.cpp`
- `components/nifosg/matrixtransform.hpp`
- `components/nifosg/nifloader.cpp`
- `components/nifosg/nifloader.hpp`
- `components/nifosg/particle.cpp`
- `components/nifosg/particle.hpp`
- `components/resource/bgsmfilemanager.cpp`
- `components/resource/bgsmfilemanager.hpp`
- `components/resource/errormarker.cpp`
- `components/resource/imagemanager.cpp`
- `components/resource/imagemanager.hpp`
- `components/resource/scenemanager.cpp`
- `components/resource/scenemanager.hpp`
- `components/sceneutil/attach.cpp`
- `components/sceneutil/attach.hpp`
- `components/sceneutil/clone.cpp`
- `components/sceneutil/clone.hpp`
- `components/sceneutil/controller.cpp`
- `components/sceneutil/controller.hpp`
- `components/sceneutil/depth.cpp`
- `components/sceneutil/depth.hpp`
- `components/sceneutil/extradata.cpp`
- `components/sceneutil/extradata.hpp`
- `components/sceneutil/glextensions.cpp`
- `components/sceneutil/glextensions.hpp`
- `components/sceneutil/keyframe.hpp`
- `components/sceneutil/lightcommon.hpp`
- `components/sceneutil/lightcontroller.cpp`
- `components/sceneutil/lightcontroller.hpp`
- `components/sceneutil/lightmanager.cpp`
- `components/sceneutil/lightmanager.hpp`
- `components/sceneutil/lightutil.cpp`
- `components/sceneutil/lightutil.hpp`
- `components/sceneutil/morphgeometry.cpp`
- `components/sceneutil/morphgeometry.hpp`
- `components/sceneutil/mwshadowtechnique.cpp`
- `components/sceneutil/mwshadowtechnique.hpp`
- `components/sceneutil/nodecallback.hpp`
- `components/sceneutil/optimizer.cpp`
- `components/sceneutil/optimizer.hpp`
- `components/sceneutil/pathgridutil.cpp`
- `components/sceneutil/pathgridutil.hpp`
- `components/sceneutil/positionattitudetransform.cpp`
- `components/sceneutil/positionattitudetransform.hpp`
- `components/sceneutil/riggeometry.cpp`
- `components/sceneutil/riggeometry.hpp`
- `components/sceneutil/riggeometryosgaextension.cpp`
- `components/sceneutil/riggeometryosgaextension.hpp`
- `components/sceneutil/shadow.cpp`
- `components/sceneutil/shadow.hpp`
- `components/sceneutil/shadowsbin.cpp`
- `components/sceneutil/shadowsbin.hpp`
- `components/sceneutil/skeleton.cpp`
- `components/sceneutil/skeleton.hpp`
- `components/sceneutil/statesetupdater.cpp`
- `components/sceneutil/statesetupdater.hpp`
- `components/sceneutil/texturetype.hpp`
- `components/sceneutil/util.cpp`
- `components/sceneutil/util.hpp`
- `components/sceneutil/visitor.cpp`
- `components/sceneutil/visitor.hpp`
- `components/sceneutil/waterutil.cpp`
- `components/sceneutil/waterutil.hpp`
- `components/serialization/osgyaml.hpp`
- `components/shader/removedalphafunc.cpp`
- `components/shader/removedalphafunc.hpp`
- `components/shader/shadermanager.cpp`
- `components/shader/shadermanager.hpp`
- `components/shader/shadervisitor.cpp`
- `components/shader/shadervisitor.hpp`
- `components/terrain/buffercache.cpp`
- `components/terrain/buffercache.hpp`
- `components/terrain/cellborder.cpp`
- `components/terrain/cellborder.hpp`
- `components/terrain/chunkmanager.cpp`
- `components/terrain/chunkmanager.hpp`
- `components/terrain/compositemaprenderer.cpp`
- `components/terrain/compositemaprenderer.hpp`
- `components/terrain/heightcull.hpp`
- `components/terrain/material.cpp`
- `components/terrain/material.hpp`
- `components/terrain/quadtreenode.cpp`
- `components/terrain/quadtreenode.hpp`
- `components/terrain/quadtreeworld.cpp`
- `components/terrain/quadtreeworld.hpp`
- `components/terrain/terraindrawable.cpp`
- `components/terrain/terraindrawable.hpp`
- `components/terrain/terraingrid.cpp`
- `components/terrain/terraingrid.hpp`
- `components/terrain/texturemanager.cpp`
- `components/terrain/texturemanager.hpp`
- `components/terrain/world.cpp`
- `components/terrain/world.hpp`
<!-- /task-0014-osg-files -->

Tests in Context's list that task 0013's rule deleted rather than left: `apps/components_tests/nifosg/testnifloader.cpp`, `apps/components_tests/sceneutil/osgacontroller.cpp`, and with them `apps/components_tests/nifloader/testbulletnifloader.cpp`. Work item 5 restores them from git history (the parent of the deletion commit) or replaces them. The bundled math headers (`extern/osg`) are gone too, so a kept file that names an OSG math type moves to VSG's when it is ported; `extern/osgQt` and `extern/osg-ffmpeg-videoplayer` stay for this task.
