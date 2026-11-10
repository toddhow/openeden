# Task 0014: Track V stage V6 — Editor and tools on Vulkan, OpenSceneGraph removed

**Status:** CLOSED 2026-11-10 by toddhow (remaining checks are follow-ups, not blockers; listed in `docs/toddhow-checks.md`, "Known open items from Track V"). Earlier status: approved by toddhow (2026-10-29) with the answers below. Depends on tasks 0012 (V4) and 0013 (V5).
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
- [x] Every test excluded in task 0009 is restored, replaced or deleted with a recorded reason (stage log, A5).
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

## Stage log: tools and tests (survey `docs/research/0007-v6-editor-and-tools-survey.md`, section 4)

**A1, esmtool and benchmarks.** `osg::RadiansToDegrees` became `Misc::VsgMath::radiansToDegrees` (same float and double overloads); the tiles cache benchmark uses `vsg::ivec2` and `vsg::vec3`. Both are in the root `CMakeLists.txt` again. Check: `esmtool dump -C` of `Morrowind.esm`, `Tribunal.esm` and `Bloodmoon.esm` is byte-identical (same SHA-256; 111 MB, 9 MB, 13 MB of text, 357,809 `Rotation:` lines) to the OSG-era `esmtool.exe` left in `build-jolt`, dated 2026-10-28 17:49 (the reference build `OpenEden-osgref-build` has no esmtool; the source commit of that binary is inferred from its date, about `48fc8a1dd9`). All three benchmarks start and finish on Windows with a short `--benchmark_min_time`; their timings were not compared.

**A2, navmeshtool.** VSG types throughout; `Misc::VsgMath::toMatrix`, `setTrans` and `preMultScale` build the world transform as `osg::Matrixd` did; `components/sceneutil/workqueue` is compiled again; the image, material and scene managers are gone from `main.cpp`, as `Resource::PhysicsShapeManager` no longer needs them. Check against the OSG-era `openmw-navmeshtool.exe` left in `build-jolt` (revision `48fc8a1dd9`, dated 2026-10-28; `OpenEden-osgref-build` has none), each with `--threads 1`, a config in a scratch folder, and its own commit's default settings (`[Navigator]`, `default actor pathfind half extents` and `actor collision shape type` are the same in both):
- Ten Morrowind interiors, one worldspace at a time (`Morrowind.esm`, `--process-interior-cells`, `--worldspace-filter` naming the cell): `navmesh.db` byte-identical in all ten.
- Solstheim's exterior (`Bloodmoon.esm` alone, plus a generated plugin holding only the `fSwimHeightScale` GMST it needs without its master): 15,767 tiles and 314 shapes, every row identical but `tile_id`, also identical; the 37 MB files differ in two bytes, SQLite's file change counter and its copy at header offsets 27 and 95.
- Not compared: the full Morrowind exterior, for time, and several worldspaces in one run. The latter differs between two runs of the same binary, because `collectWorldspaceCells` returns an `unordered_map` and the order assigns shape IDs.

The data is the local Morrowind install, so this check is not repeatable in CI.

**A3, essimporter.** `osg::Image` and osgDB gave way to `components/vsgutil/imagepixels` over vsgXchange's stbi writer, already a dependency, so nothing new is recorded in `docs/dependencies.md`. `doubleImageSize` keeps what `osg::Image::scaleImage` did (gluScaleImage: a one-pixel box, wrapping, in 16 bits); `copyRgbSquareToRgba` keeps the old four-byte copy, whose alpha is the next pixel's red, and defines the read past the end of the image as 0. New unit tests: `apps/components_tests/vsgutil/testimagepixels.cpp`, on generated data. Check: a generated `.ess` (no real save was looked for), with header screenshot, 512x512 FMAP map and two exterior CELLs with NAM8, converted by the new binary and the OSG-era one from `build-jolt`:
- Global map PNG: all 1008 rows decode identically, alpha included.
- Fog TGA files: decoded pixels identical in both cells, though the bytes differ (stbi writes run-length encoding).
- Screenshot JPEG: viewed side by side, same orientation and colours. Not byte-identical: stbi's quality is 100, osgDB's was 75 (4,256 and 3,751 bytes here).
- The rest of the save is 16 bytes longer, in `PLAY`: the `DTIM` that `components/esm3/creaturestats.cpp` gained after the old binary, and `WWAT` and `WWSK` differ where the old output held uninitialised bytes.

Not verified: a real Morrowind save, loading a converted save in the game, and `ConvertPROJ::convertBaseState`'s `Misc::VsgMath::makeRotate` (no projectile in the generated save).

**A4, video player.** `extern/osg-ffmpeg-videoplayer` is now `extern/ffmpeg-videoplayer`, target and include path too; `${OSG_LIBRARIES}`, the unused `osg::Texture2D` declaration and the OSG comments went. Its code uses no OSG. Check on Windows with Morrowind's own files: `openmw.exe` without `--skip-menu` opens `bethesda logo.bik` and then `mw_logo.bik` (FFmpeg's log), and a temporary, uncommitted dump in `MWGui::VideoWidget::update` wrote frames 60 and 180 as PNG: the logo fades in correctly, right way up, in the right colours. Not verified: the frame on screen, and sound. `--auto-screenshot` only counts `State_Running`, and with that gate lifted temporarily `A_Screenshot` still does nothing during a video; a window grab is black in play as well, so it cannot see this Vulkan window. toddhow's check: start the game once and watch and hear the logo.

**A5, tests excluded in task 0009.**
- `resource/testresourcesystem.cpp`: ported. 50 threads read a missing model through `vsgUtil::readNode` with the resource system's `nodeOptions`; each gets `Resource::Fallback`'s stand-in, as `SceneManager::getInstance` gave its error marker.
- `nifloader/testbulletnifloader.cpp`: restored as `nifloader/testjoltnifloader.cpp`, all 41 tests with their inputs, expected values and tolerance of 1e-5. The survey's "already replaced by `testjoltnifloader`" was wrong: the CMake list named a file that did not exist, and `file(GLOB)` ignores that silently, so this was still Bullet code and had not compiled since the Jolt port. Comparison is now through the file's `Expected*` types; its header comment says what differs, namely that transform, scale and vertices compare together as model space triangles, and triangle order and static compound child order are not compared. Bullet's BVH flags have no Jolt counterpart.
- `nifosg/testnifloader.cpp`: replaced by `vsgadapters/testnifreader.cpp`, which feeds `vsgAdapters::nif` a NIF built in memory: the default node, a child's transform and record index, the discarded root transform, and two refusals. Deleted with reason: its `bs/...` shader prefix and BSLightingShaderProperty depth cases, as `nifImpl::handleProperty` has no case for those properties. **That is a behaviour gap against 0.51, not only a test change:** those properties are ignored on the Vulkan build. They come from post-Morrowind games' files, so Morrowind is unaffected.
- `shader/parsedefines.cpp`, `parsefors.cpp`, `parselinks.cpp`, `shadermanager.cpp`: deleted. They test `Shader::ShaderManager`'s `@` templates, and no `components/CMakeLists.txt` entry builds `components/shader`. `vsgUtil::readShader` is a wrapper over `vsg::read_cast`, with no parser of its own, so no such test applies; includes and defines are VSG's own shader compiler. `files/shaders/compatibility` still has `@foreach`, unread by the Vulkan build, for the OSG deletion stage to remove.
- `sceneutil/osgacontroller.cpp`: stays deleted. `SceneUtil::OsgAnimationController` and its emulated animations are not compiled, and nothing in the build reads `.osgb` animations. `vsgadapters/dae/daeanim.cpp` merges clips into one timeline instead, and `testdaeanimation.cpp` checks translations, clip placement, merging and per-node controllers. Its sampling and not-found cases have no API to call now.
- `fx/*`: already restored before this stage.

### B1 vsgQt spike (2026-10-31)

vsgQt v0.5.0 is fetched and pinned, and builds against VSG v1.1.16 on Windows (Qt 6.6.3) and Linux (Ubuntu 24.04's Qt 6). A throwaway test program, not committed, ran on Windows only and put two `vsgQt::Window`s in a `QSplitter` on one device:

- Both windows rendered their own scene, read back correctly with `Render::Screenshot`, and were still correct after the main window was resized from 820x320 to 1000x600.
- With `continuousUpdate=false`, 0 frames ran in half a second idle, and 1 frame after `request()` (requests are counted together, so three requests give one frame).
- The container never took focus in this run, with `WA_ShowWithoutActivating` and `WindowDoesNotAcceptFocus` on the main window. Focus in normal use is for toddhow's check list at C3.

Rules for the editor from this:

- A container laid out at zero width gives a window with zero extent, and `Render::Screenshot` crashed the program on it. Lay out before `initializeWindow()`, and never capture a zero-size window.
- `Render::download` refuses an sRGB swapchain format (`!download(VkFormat=50)`, no data), so set the format as the game does (`vsgengine.cpp`, `Render::compatibleColorFormat`).

Not run: any window on Linux. The review of A1 to A5 is below.

### Review of A1 to A5 (2026-10-31)

A fresh reviewer read the five commits and found no conversion bugs. Fixed: a failed fog encode wrote an empty file silently, a dead `detournavigator/serialization.cpp` entry in the test CMake list, and formatting by hand on the lines it named (clang-format 14 is still not run). Left as it is: `shouldLoadFileWithDefaultNode` only checks that nothing is kept for an identity node; the next test checks the structure. The BS shader gap went to task 0012's open items, since the behaviour is missing and not retired, which work item 5 does not allow as a reason for deleting tests alone.

### B2 Scene helpers lifted to components (2026-10-31)

`MWRender::DebugGeometry` and `MWRender::SimpleWater` moved with `git mv` to `components/render` as `Render::DebugGeometry` and `Render::SimpleWater`, code otherwise unchanged. The part of `apps/openmw/mwrender/intersect.hpp` with no game types is now `Render::nearestIntersection` (`components/render/intersect.hpp`); `getIntersectionResult`, the `Mask_*` choice and the `MWWorld::Ptr` lookup stay in the game on top of it. New `components/render/CLAUDE.md`.

Check: frames from the build before and after, each scene captured twice before and twice after to measure the noise. Balmora and Caldera Mages Guild differ by no more than two runs of one build (pixels over threshold: Balmora 116,365 between runs of one build, 49,416 and 100,109 across the change; Caldera 69 between runs, 0 and 0 across). The `tcb` Jolt draw and `togglenavmesh` navmesh draw were viewed and compared too, so `DebugGeometry` is exercised (`tcb` 0 pixels over threshold, navmesh 111,246, the camera's idle sway as in Balmora).

Not checked directly: `SimpleWater` is drawn only in the map renders, and those frames show no open water; `castRay` and `castCameraToViewportRay` have no scripted check of their own, and the only change there is the moved code.

### B3 Render::Engine on a viewer it does not own (2026-10-31)

New `Engine(viewer, window)`: **one Engine per window**, because all but three of its methods are about one window (`components/render/CLAUDE.md`). The two existing constructors, `loop()` and the game's path are untouched; every new branch tests `mHosted`.

- `endFrame()` is the per-frame work `loop()` does beyond the viewer's: advancing the deletion queue. It goes through the new `DeletionQueue::advanceToFrame`, once per frame stamp, so it is safe from a timer while an on-demand viewer draws nothing, and from every Engine of one viewer.
- `setup()` appends the graphs to the viewer's task. `Viewer::addRecordAndSubmitTaskAndPresentation` builds new tasks, and `Device::transferTask` and earlier compile contexts keep the old one, whose uploads nothing runs (VSG 1.1.16, `assignRecordAndSubmitTaskAndPresentation`, `RecordAndSubmitTask::RecordAndSubmitTask`).
- From B1's findings: the constructor refuses a zero-size window and a format other than `compatibleColorFormat`, and hosted `screenshotOperation()` returns null at zero size. The destructor waits for the device, as `loop()` does on leaving.

Check of the game: as B2, frames before and after in Balmora and Caldera, with `tcb` and `togglenavmesh`, all viewed. Balmora's differences stay within two runs of one build (116,365 pixels over threshold between runs, 111,902 and 98,001 across the change). One Caldera pair gave 9,061, so six runs were compared: 0 to 32,509 against the two baseline frames, 16 to 721 among new runs, the same dim flicker across the room. Unit test without a GPU: `vsgutil/testdeletionqueue.cpp`. The Engine itself needs a GPU, so it has no committed test.

A throwaway program, not committed and run on Windows only, ran two hosted Engines on a `vsgQt::Viewer` on one device, the second window added after the first had drawn:
- Both windows read back correctly through `screenshotOperation`, and a box compiled through each Engine's compile context after the second window was added appeared in both, so uploads still run.
- A detached object stayed held through idle time with `endFrame` running every millisecond, and was released on the third frame of a three-image swapchain.
- An sRGB window was refused.

Found by it: `createRenderGraph` has no resize handler, and a render area larger than the swapchain draws nothing, with no error. The editor must set `renderArea` on resize (C3); the program did that on a timer. Recorded in `components/render/CLAUDE.md`.

Not run: the zero-size refusal and null capture, closing one window while others stay (`Viewer::removeWindow` replaces the tasks, so C3 needs its own way), a hosted window on Linux, validation layers (none installed here), `Pipeline::Builder` and the game's shaders in a hosted window (C2).

### Review of B3 (2026-10-31)

A fresh reviewer read `840f9282cd` and found four defects in the hosted path, none in the game's. Fixed:
- A second Engine's window fell out of `Viewer::_windows` when the first `setup()` went through `assignRecordAndSubmitTaskAndPresentation`, so it stayed blank. `setup()` now adds the window back.
- Appending did not restart viewer threading, so later graphs were never recorded. `setup()` now stops threading before appending and restarts it if asked; the last call decides for the viewer.
- A timer's `endFrame()` after a detach could count the frame that last used the object, releasing it before that frame's fence was waited on. Hosted `onDetached` now calls `endFrame()` first; `testdeletionqueue.cpp` shows both orders.
- `setup()` now checks the swapchain's real format, a hosted `loop()` throws, and `useFrameQueue` is documented as ignored when hosted.

Checked with the throwaway two-window program, both windows constructed before either `setup()`: without threading; with `threading` true then true; and true then false. Each time both windows read back correct at every capture, and boxes compiled afterwards appeared in both. The true-then-false run found one more defect: `Viewer::stopThreading` leaves `status` inactive, which stopped drawing and failed every compile, so `setup()` now sets it active again.

Open:
- A collapsed window: what it does at zero extent, including whether `Viewer::acquireNextFrame` spins on it.
- Closing one window while others stay open.
- Hosted windows on Linux, and validation layers.

### B4 Altered heights in the VSG terrain storage (2026-10-31)

`ESMTerrain::Storage::getVertexData` (`components/esm3terrain/storage.cpp`) consults `useAlteration()`, `getAlteredHeight` and `adjustColor` again, in the order of the old `Storage::fillVertexBuffers` (`components/esmterrain/storage.cpp`): offset added to the record height or the default height, normals untouched, `adjustColor` before `fixColour`. `getHeightAt` still reads the record only. The hooks have no cell argument, so they apply in every cell of a multi-cell chunk: the editor must ask for its one cell's bounds alone (C-stage work). Tests: `apps/components_tests/esm3terrain/testalteredheights.cpp`, 18 cases, written from the spec.

Check of the game, which supplies no alterations: two runs each before and after, frames viewed. Seyda Neen, terrain filling the frame: 6 to 9 pixels over threshold across the change, 1 and 15 between runs of one build. Balmora: 104,858 to 117,597 across the change, 49,555 and 119,417 between runs.

Not run: the editor's own subclass, which does not build until the editor is ported.

### Hosted deletion queue fix (2026-10-31)

A review found a use-after-free in B3's `onDetached`, confirmed against the source. `vsgQt::Viewer::render` stamps a frame in `advanceToNextFrame`, runs `handleEvents` and `update`, and only then `recordAndSubmit`, where `RecordAndSubmitTask::start` waits on the fence. So calling `endFrame()` from a detach counted a frame whose wait had not happened: an object last used in frame 7, with 8 and 9 reported, was freed by a detach during frame 10's update, before frame 7's fence was waited on. Separately, a two-image swapchain gave a queue of 2 where the fence ring is 3 (`Viewer::assignRecordAndSubmitTaskAndPresentation`, `numBuffers`).

Fix, all behind `mHosted`: `onDetached` only adds, the host's post-frame `endFrame()` is the only advance, and the queue holds `max(numFrames, 3) + 1` (`DeletionQueue::sizeForPostFrameAdvance`). This corrects B3's review entry above, which added the `endFrame()` call.

Tests changed in `vsgutil/testdeletionqueue.cpp` (coordinator-approved exception): the two tests of the old order, `advanceToFrameBeforeAddKeepsObjectUntilThirdNewFrame` and `advanceToFrameAfterAddCountsTheFrameThatUsedTheObject`, assumed frame 10's submission had already waited when frame 10 was reported from a detach. They are replaced by `postFrameAdvanceKeepsObjectUntilItsFenceHasBeenWaitedOn`, and `sizeForPostFrameAdvanceCoversTheFenceRing` is new. The other tests are unchanged. They test the queue only, not `Engine`.

Check: components-tests (2220), openmw-tests (717) and one game frame of the Caldera Mages Guild, viewed; the game's path has no changed branch. Not run: a hosted window with this fix, which C2 is the first to exercise, and validation layers.

### C1 Editor model on VSG (2026-10-31)

New static library `csmodel`, everything under `apps/opencs/model` plus three plain Qt widgets the model uses (`coloreditor`, `colorpickerpopup`, `completerpopup`). `openmw-cs-tests` links it alone, and `BUILD_OPENCS_TESTS` works again. `extern/osgQt` is no longer built, though its files stay until D1.

- `actoradapter` uses `vsg::vec2`, `cellcoordinates` uses `vsg::dvec3`, `columnimp` uses `Misc::VsgMath::radiansToDegrees` and `inDegrees`, which keep OSG's operand order.
- `CSMWorld::Data` no longer builds a `ResourceSystem` in its constructor. The new one needs a sampler and compressed-image support from a device, and a document has none until a 3D view opens, so the first view calls `getOrCreateResourceSystem`. Before that `getResourceSystem()` is null. Nothing in `csmodel` reads it but `assetsChanged`'s `clearCache`, which now checks.
- The shader defines that `Data` set (`forcePPL`, `clamp`, `preLightEnv`, `radialFog`, `lightingModel`, `reverseZ`, `waterRefraction`, the shadow defines) went with `Shader::ShaderManager`, which no longer exists. They only ever affected drawing, so any look they gave the editor is a C4 question.
- `main.cpp` no longer sets an OpenGL surface format. It is compiled from C2.

Check: `openmw-cs-tests` 154 passed on Windows, and on Linux with `-Werror`. No committed test changed.

### C2 Editor scene host and unattended capture (2026-10-31)

New `CSVRender::RenderContext` (one: viewer, device, render pass, sampler, compile context) and `CSVRender::SceneHost` (one per view, on a hosted `Render::Engine`), and `CS::runCapture` for `--open-cell X --auto-screenshot N --auto-quit M`, with `--camera` and `--capture-views` added for the checks. Rules are in `apps/opencs/CLAUDE.md`. `SceneHost` draws a cell's objects with their lights. The table views now build too, with the scene and preview subviews behind `OPENEDEN_CS_VIEW_RENDER_PENDING`, so `openmw-cs` links.

Shared code touched:
- `components/settings/settings.cpp`: the editor branch also loads `defaults.cfg` and `StaticValues`. The game's branch is unchanged.
- `createDefaultSampler` moved to `components/render/defaultsampler.*`, `readEnv` to `components/view/env.*`, code otherwise unchanged.

Design choices for toddhow to overrule if he likes:
- The compile context and its deletion queue belong to `RenderContext`, not to an Engine, so views can close independently.
- A capture runs before `CS::Editor` exists, so no editor window, dialogue or IPC server appears, rather than teaching `Editor` a second mode.
- `Data`'s resource system is per document, so two open documents share a device but not caches, and a pipeline built for one is rebuilt for the other.
- Interiors use the game's lighting formula, and exteriors the old editor's day mode, until C3 brings the lighting modes back.

Check against the game, same camera taken from a temporary log of `mViewMatrix` (removed), 1280x720, frames viewed:
- **Seyda Neen, Arrille's Tradehouse:** geometry is identical pixel for pixel by eye and in the diff map. With a threshold of 6 summed over RGB and every second pixel sampled, 5,589 of 230,400 are over, all of them the game's HUD, door tooltip and crosshair. The rest differ by low, brightness-proportional amounts; a wall area averages R/G/B 41.3/29.5/15.8 in the editor and 40.0/28.9/15.6 in the game. The editor draws no fog, which may be the cause, not proven. A trial with lights off gave 32.4/24.8/14.7, so point lights do work.
- **Seyda Neen exterior (`#-2 -9`):** buildings, tree and rocks are in the same places; 228,118 of 230,400 sampled are over the threshold, as expected at this stage. Missing: terrain, water, sky, the actor, the neighbouring cells and sun lighting. The grey background and day-mode lighting are the editor's. Foliage looks paler and softer against the grey than in the game's frame. That is not explained yet; check at C4 with terrain and matching light before calling it lighting.
- **Balmora, Caius Cosades' House:** two views on one device (`--capture-views 2`), both written, second viewed, default camera; candlelight visible.
- A cell that does not exist exits with code 1 and a message.

Check of the game: Caldera Mages Guild, across the moves, 0 sampled pixels over the threshold, as between two runs of one build. Balmora not compared.

Tests: Windows components-tests 2219 of 2220 (`DetourNavigatorRecastMeshObjectTest`, the known intermittent one), openmw-tests 717, openmw-cs-tests 154. Linux with `-Werror`: `openmw-cs` and `openmw-cs-tests` build, 154 pass.

Not verified:
- `openmw-cs` started normally: it would open windows that take focus. The table views have only been compiled.
- Any window on Linux.
- Resizing a view (`SceneHost::updateExtent` is there, not exercised), and closing one view while another stays.
- `reference.mScale` of 0 and doors or animated objects, drawn without animation (`useAnim` false) until C4.
- Validation layers, none installed.

### C3 Viewport widgets on vsgQt (2026-10-31)

`RenderWidget` and `SceneWidget` are back on vsgQt. The window is made on the first show with a size, through `createWindowContainer`; what needs the view waits for `hostReady()`. Also back in the build: `WorldspaceWidget`, `PagedWorldspaceWidget`, `UnpagedWorldspaceWidget`, `PreviewWidget`, `EditMode`, `Object`, `Cell`, `CellArrow`, and the Scene and Preview subviews with their factories. `--open-cell` now captures through those same widgets.

What each old part became:
- **Gradient:** a quad drawn first with no depth test, its corners through the inverse of the projection, instead of a second camera. Off, the clear colour (`scene-use-gradient`).
- **Lighting modes:** `lighting.*`, values only. Light straight down, specular none, day, night and bright as `LightingDay`, `LightingNight` and `LightingBright` were, whose files are gone. The cell's ambient is used in day and night.
- **On-demand frames:** `RenderContext`'s timer asks each view for `needsFrame()`. `flagAsModified` sets it once; `setAnimated` sets it always. A view showing objects draws every tick, because particles and flickering lights move. The OSG editor did the same in effect, as `Cell`'s update callback kept `checkNeedToDoFrame` true (`extern/osgQt/CompositeOsgRenderer.cpp`).
- **Resize:** `SceneHost::update` compares the extent and sets `renderArea`, viewport and projection.
- **Input:** an event filter on the window sends mouse, wheel, key, focus and drag events on to the `RenderWidget`, where `ShortcutEventHandler`'s filter and the widget's own handlers expect them. A press calls `requestActivate()`.
- **Closing one view:** `RenderContext`'s `SharedViewer::removeWindow` takes that window out of the tasks and leaves them. `vsg::Viewer::removeWindow` rebuilds them, which leaves compile contexts on a dead `TransferTask`. `appendToViewer` lets a view join a task left with no windows.
- **`RenderContext`** is now one per process (`RenderContext::get()`), with a `Lifetime` in `main.cpp`.

Stand-ins until their stages, so that the widgets build and the toolbars keep their buttons:
- C5, done: see its entry below.
- C6, done: see its entry below.
- C7: `objectmarker_pending.hpp`, a do-nothing selection marker. `Object::setSelected` keeps the flag and draws no outline.
- C7 to C9: the edit mode buttons are the plain `EditMode`.
- The `scene-render-stats` shortcut only logs: `osgViewer::StatsHandler` has no counterpart yet.

Fixes from the review of C1 and C2:
- `defaults.cfg` is installed for the editor (Windows and the `opencs` component). The root's stale `defaults.bin` install line now names `defaults.cfg`.
- Multisampling asks for the same device extensions as the game. One capture with `antialiasing = 4` in `openmw-cs.cfg` ran and wrote a frame.
- The Scene and Preview factories are back, so `table.cpp`, `regionmap.cpp` and `dialoguesubview.cpp` no longer throw.
- `setlocale(LC_NUMERIC, "C")` now runs before the capture branch.
- `Cell::addObjects` compares `RefId`s rather than strings.

Checks, frames viewed:
- **Normal start:** new `OPENEDEN_CS_UI_CHECK=<hint>;<ms>`, with a content file as first argument: `Morrowind.esm` loaded, a references table and a scene subview opened through `ViewManager::addView`, every window closed, exit code 0, no crash dump, windows not activating. This does not click anything.
- **Resize:** Arrille's Tradehouse at 1280x720, then 1400x800, 320x180 (the shrink), 900x500: each frame is complete and at the new size.
- **Lighting:** day, night and bright differ as expected in the same camera.
- **Closing one of two views:** the one left still draws (Caius Cosades' House).
- `openmw-cs-tests` 154 passed.

Not verified:
- Anything a person must do, below.
- Validation layers: none installed.

#### Check list for toddhow

Start `openmw-cs`, choose Edit a content file, open `Morrowind.esm`. There is no World > Scene entry, and there never was in the OSG editor: a scene opens from a table. In World > Cells, right-click a cell and choose View (Ctrl and a double click does the same). Corrected 2026-10-31 after toddhow could not find the entry.

1. [x] **Scene opens.** In World > Cells, right-click "Seyda Neen, Arrille's Tradehouse" and choose View. A dock appears with toolbar, 3D view and status bar, and the interior is drawn in the view.
2. **Choose a cell.** In World > Cells, right-click "Seyda Neen, Arrille's Tradehouse" and choose View. The interior appears. In the Cells table again, drag an exterior such as `#-2 -9` onto a new Scene subview: it appears there. This checks drag and drop into the Vulkan window.
3. [x] **Resize larger.** Drag the main window's corner to make it about twice as wide. The view fills its dock with no black strip and no stale pixels.
4. [x] **Resize smaller.** Drag it as small as it goes. The view stays correct and the editor does not hang. Drag it back.
5. [x] **Splitter.** Open a References table beside the view (dock them side by side). Drag the divider until the view is as narrow as it goes, then back. Nothing crashes, and the view redraws.
6. [x] **Docking.** Drag the Scene dock's title out of the main window so that it floats, then back. Dock it as a tab with a table and switch tabs both ways. The view shows the same cell each time, and no window flashes up on its own.
7. [x] **Two views.** Open a second Scene, of another cell. Both draw. Close the first: the second keeps drawing. Close the second and open a new one: it draws.
8. [x] **Toolbar over the view.** Click the lighting button in the scene toolbar and choose night, then bright, then day. The pop-up must appear in front of the view, not behind it, and the view must change. Hover over a toolbar button: the tooltip must appear in front of the view.
9. **Focus by clicking.** Click in a table and type in its filter box. Click the 3D view, then press `Tab` (`scene-cam-cycle`, or whatever Preferences > Key Bindings shows; report it if Tab moves the focus instead). The camera mode button on the toolbar changes, so the key reached the view. Click the filter box again and type: the letters go in the box, and no scene shortcut fires.
10. **Shortcut held across a focus change.** In the view, hold the `scene-speed-modifier` key, Alt-Tab away, release it, and return. Then press `scene-cam-cycle`: it must act once, with nothing stuck.
11. **Shortcuts over the view.** With the mouse over a paged view of `#-2 -9`, focus it by clicking and press `scene-load-cam-eastcell` (Preferences > Key Bindings names the keys): one more cell loads to the east. Global menu shortcuts such as Ctrl+S's dialog and Ctrl+Z still work with the view focused.
12. **Mouse tracking.** Nothing to see until C5 and C6: move the mouse over the view and confirm that the editor's frame rate does not fall and nothing logs an error.
13. **Quit.** Close every window with a scene open, and start again. The editor exits with no crash, and no `openmw-crash.dmp` appears in the user data folder.
14. **Linux, once:** the same steps 1 to 8 under X11 or with `QT_QPA_PLATFORM=xcb`. vsgQt has no Wayland path.

### C4 Cell contents (2026-10-31)

`Cell` now shows what the OSG editor's did, but for the pathgrid (C9):
- **Objects:** `MWAnim::createObject`, with bones for the record types whose class has `useAnim` in the game (doors, activators, containers, lights), as a skinned model needs them. Night and day switches follow the mode and `scene-day-night-switch-nodes`. With that preference off a switch shows its first child, not its initial index as in OSG, because the VSG reader does not record the initial index.
- **Actors:** `CSVRender::Actor`, a thin caller of `MWAnim`. Creatures through `createCreature`; npcs take bones from the skeleton and body parts from `ActorAdapter`, as `MWRender::Npc::insertBoundedPart` does without equipment logic. Bind pose, as the OSG editor. `animContext`'s options are copied to `scenehost.cpp`, as the game's file names game masks.
- **Terrain:** one `Terrain::Paging` and `TerrainStorage` per cell, asked through `updateViewForCell` with a batch of one cell, because the alteration hooks have no cell argument. `Cell::setAlteredHeight` and its kin are back. Editing them is C8, so no alteration has been seen on screen.
- **Water:** `Render::SimpleWater` with a new `followCamera = false`, per cell. Its alpha comes from `Water_Map_Alpha`, where the OSG editor used a constant 0.5, and it is lit by the shader's normal path.
- **Border, arrows:** `createColouredGeometry` (unlit). **Marker text:** QPainter to an image, on a quad that faces the camera at a fixed pixel size, with no depth test, in a new overlay bin.
- **Neighbouring cells:** exteriors go through `PagedWorldspaceWidget`, so each neighbour is a `Cell` with its arrows. `--neighbours N` selects them for a capture.

Shared code touched: `components/render/simplewater.hpp` (a defaulted parameter and `setPosition`), `components/esm3terrain/storage.cpp` (a negative plugin index in `getBlendmap` now takes the default layer; the game has none), `components/mwanimation/create.hpp` (`createObject` is `inline`), hosted-only code in `engine.cpp`, and `IdCollection<LandTexture>::getIndices`. New land textures of the edited project (plugin -1) show the default layer for now; C8 must settle that. The game's frames below were taken with these in place and viewed. They were not compared pixel by pixel with an earlier build, as none of the changes touches a path the game takes differently.

**Fog and pale foliage, answered.** The OSG editor set no fog: neither its widgets nor its cells set any, and `unpagedworldspacewidget.cpp` says "\todo deal with mSunlight and mFog/mForDensity". So none is added. Foliage still looks paler than in the game with all the editor's content present. Day mode is ambient 0.7 plus white diffuse straight down, as `LightingDay` was, so an upward-facing leaf reaches about 1.7 and saturates, where the game's sunlit ambient is far lower. Night mode (0.2 and 0.2) shows the same trees dark with no paleness, which supports this; it was not measured any further. It is the OSG editor's own lighting and stays. The OSG build cannot be run to compare the old look.

Checks against the game from the same camera, taken from a temporary log of `mViewMatrix` (removed), `--fov 60`. The game's window is 2560x1369, the editor's 1280x720, so the width differs and images are compared by eye:
- **Seyda Neen exterior (`#-2 -9`, `--neighbours 2`):** terrain shape and textures, buildings, tree, rocks, fence, cell borders (green) and marker labels in the right places. Differences: no sky, sun, distant land or fog, by design; water is flat `SimpleWater`, not the game's reflecting water, and paler; roofs and foliage paler in day (above); a tall tan pole and ropes in the game's frame are missing. Those look like the silt strider's legs: the editor's strider stands in its bind pose elsewhere on screen. Not confirmed. Actors stand in bind pose where the game's are animated. One armoured figure looks broken up at a distance; not examined.
- **Arrille's Tradehouse:** same door, posts and walls, in similar brightness. In day the wood is nearly the same; the game's cell lighting is not used, as `LightingDay` used only the cell's ambient. Differences: a bottle shows at the door's handle that the game's frame does not show, unexplained (the door is the animated kind, and the handle looks unchanged behind it), and the fern is brighter.
- **Night and bright:** viewed in both cells; they differ as their values say.
- **Resize with terrain:** `#-2 -9` at 1280x720, 1400x800, 320x180: complete frames each time.
- **On-demand frames:** `--capture-idle-ticks 100`: 0 frames in empty exteriors (`#-10 -9`, `#10 -16`, `#-13 2`), 99 in cells with objects.
- **Normal start:** repeated with this stage's cells in a scene subview.

Tests on Windows: components-tests 2220, openmw-tests 717, openmw-cs-tests 154, all passed. Linux results are in the entry that follows, or noted as not run.

Not verified: everything in C3's list; terrain alteration on screen; the layer-map preview; reloading assets; validation layers; markers and arrows hidden by the visibility toolbar (a person's check, step 15 below).

15. **Visibility toolbar (C4).** In a Scene of `#-2 -9`, use the scene toolbar's visibility buttons to hide and show Instances, Water and Terrain, and the Controls & Guides buttons for cell markers, arrows and borders. Each disappears and comes back, and nothing else changes.

### Linux, C3 and C4 (2026-10-31)

`-Werror` found shadowed names and a `memcpy` onto `vsg::ubvec4`, fixed. At `0af702c8c1`: components-tests 2222, openmw-tests 717, all 32 Lua integration tests. Editor targets with those fixes: `openmw-cs` and `openmw-cs-tests` build, 154 pass. Started under xvfb with Mesa's lavapipe on the example suite's `template.omwgame`, with `QT_QPA_PLATFORM=xcb` and `OPENEDEN_CS_UI_CHECK="c:#0 0;4000"`: table and scene opened, windows closed, exit code 0. No frame from that run was captured or viewed, and the suite is not Morrowind data.

### C4's open items and the review of C3 and C4 (2026-10-31)

Each item from C4's captures, measured or looked up at the same camera (`--camera` from a temporary log of the game's `mViewMatrix`, and 1280x720 in both):

- **a. Broken-up figure: a real bug, fixed.** It was Eldafire, a clothed npc at (-11422, -68238, 308): a skirt lying flat at her waist and a gap below it, with rigid armour on another npc correct. `Actor::attachBodyPart` kept only auto-play controllers, and `Anim::Skin` is not auto-play (`vsgadapters/nif/nif.cpp`), so skinned parts never got bone matrices. The game keeps them as static controllers (`Npc::insertBoundedPart`). It now uses `addController`. After the fix she stands whole, in her skirt, in the game's stance. Actors are in bind pose as expected: the OSG editor's `actor.cpp` loaded a skeleton and parts, and played nothing.
- **b. Bottle at the door: there is no bottle.** No bottle reference lies within 260 units of that door (`esmtool dump -C`). Zoomed to 15 degrees, the "bottle" is the handle of `ex_nord_door_01`, at the same pixels as the handle in the game's frame. Its upward-facing part catches day mode's light from straight above, which the game's interior does not have. Not a bug.
- **c. Tall pole and ropes: not missing, not reproduced.** No reference of any record lies on that bearing but Eldafire and doors (bearing 0.626 from the camera, within 9,000 units; list from `esmtool`). Fresh game frames at the same camera at frames 100, 300, 600, 1000 and 2000 do not show it. `a_siltstrider` is at (-8800, -70531), about 75 degrees off the view axis, and stands in the editor as a compact strider. So the pole was in that one game frame and is no static content: something that moved, which I could not identify. Ruled out: a reference the editor fails to draw. The strider guess stays unconfirmed.
- **d. Idle frames: a real bug, fixed.** `Cell::isAnimated` was `!mObjects.empty()`, so any cell with an object drew every tick. It now asks whether any object has auto-play controllers, each tick (so a reference added later counts, the review's finding), and `SceneHost` draws for it only if the window is exposed, so a hidden tab does not. `--capture-idle-ticks 100`: `#-1 -9` (218 objects, none animated) drew 0 frames. `#-2 -9` (9 of 316: six lanterns, three chimney smokes), `#-5 -9` (3 of 143) and Caius Cosades' House (3 of 57: lantern, candles) still draw 99, because those really move. This also corrects C3's entry: the preview subview never called `Object::frameUpdate` and now does, through a `FrameListener`.
- **e. Paleness: the OSG editor's own lighting, measured and kept.** The OSG editor's `LightingDay` gives ambient 0.7, white diffuse straight down, and material ambient and diffuse of 1 (`scenewidget.cpp` at `124715f58d^`), and its `lightingModel` define was 0; fixed-function lighting clamps at 1. The shader clamps the same (`materialTerms`), as day and bright frames show (mean R of the area, same camera). Bright mode shows the texture itself, as ambient alone reaches 1:

| Area | Bright | Day | Day / bright | Night | Night / bright | Game |
|---|---|---|---|---|---|---|
| Vertical wall | 98.6 | 70.3 | 0.71 (expected 0.70) | 20.4 | 0.21 (expected 0.20) | 37.7 |
| Roof, sloping up | 125.0 | 125.0 | 1.00: clamped | 44.6 | 0.36 | 74.6 |

  So day's roofs and foliage are the unlit texture, nothing is over-bright (0% of pixels at 250 or above), and the game is darker because its sun and ambient are lower. The OSG build of the editor cannot run, so this compares against its code and not its pixels.
- **f. Frames viewed.** The antialiasing frame from C3 (`antialiasing = 4`) is complete and correct, the default view of the Tradehouse. Bright mode at `#-2 -9` is lit evenly, in the same places.

From the review of C3 and C4, read by a reviewer and checked here:
- `Object` no longer stores its mask node in its own root's values, a reference cycle that kept removed objects and their Vulkan resources until exit. It is a member now.
- Each closed view left a compile `Context` in `CompileManager`, which has no remove, holding `viewDependentState` of a dead view, and later compiles ran for it. Now `RenderContext::addCompileTarget` adds one target without a view, once. All views share one view ID and render pass (`vsgUtil::createSharedView`), and pipelines take the viewport dynamically (`viewportStateHint`), so one is enough. Checked: two views with one closed, and a capture of a view opened after that.
- `UnpagedWorldspaceWidget`'s `mCell` is null before `hostReady`, and its callers now check.
- `vsgQt::Window::cleanup` nulls `windowAdapter` when Qt destroys the surface. `SceneHost` now checks (`hasWindow`), and a tick with no live window does not render. New `--capture-reparent`, which moves a view to a new top-level window as floating a dock does: on Windows the view kept its window and drew. If a surface ever is destroyed, that view stays blank until reopened and logs nothing: not exercised, so check-list step 6 stands.

Seen once, not explained: a capture of Arrille's Tradehouse exited with a segmentation fault after its screenshot, before the build with these fixes. About 30 later runs, 12 of them the same command before the fixes, did not repeat it.

### C5 Camera controllers (2026-10-31)

`cameracontroller.*` and `orbitcameramode.*` are back in the build, and the Scene toolbar's navigation buttons drive them. The controllers keep the OSG code's steps. New `camerapose.hpp` holds what `osg::Matrixd` did to a view (`getLookAt`, `*= rotate`, `*= translate`, `orthoNormal`, `osg::Quat(angle, axis) * v`) with no Qt and no scene, tested by `apps/opencs_tests/view/render/testcamerapose.cpp` (13 cases, written first from the old code's calls: which way a positive yaw and pitch turn, which way eye-space axes move the camera). `SceneHost::getViewMatrix` and `setViewMatrix` connect them to the `vsg::LookAt`.

Conventions: eye space is the same in OSG and VSG, x right, y up, looking along -z. VSG's Y flip and reversed depth are in the projection, so no controller knows of them. A frame is asked for only when the view changes.

Differences from the OSG editor:
- **Default view:** `SceneHost::frame` (medians of the references, the C2 pitfall), not `CameraController::setup`, which took 2x a corner of the bounds of everything visible. `SceneWidget::frameUpdate` asks once there is something to frame, and `mCamPositionSet` works as before.
- **`pitch`:** `acos` gets a clamped value, as an up vector a rounding error over length 1 would give NaN. The rest of its limit is the old code's, including that a single step past the limit while level goes up, not down: a very large mouse step shows it (`mouse 0 5000`).
- **Orbit's first centre:** `SceneHost::pickNearest`, in C6. Until then it always took the default start distance.

New `--camera-script "cmd;cmd;..."` for the capture path: `mode`, `pose`, `center`, `slot name 0|1` (invokes the controller's slot as its shortcut does), `mouse dx dy`, `wheel d`, `update dt`, `ortho size`. It calls the controller objects and logs eye, centre and up after each command. It skips Qt's events and `ShortcutEventHandler`, which is for the check list.

Checks at Seyda Neen (`#-2 -9`, 1280x720, fov 60), against a direct `--camera` frame of the same pose, all pixels compared with a threshold of 6 summed over RGB:
- **Free, 1000 units back, `slot forward 1; update 1`:** the logged eye is the target (`navi-free-lin-speed` 1000), 0 of 921,600 pixels over.
- **Free, yaw and strafe and wheel out and back:** `mouse 650 0` turns right by 1 rad, forward (0.84, 0.54, 0) as `sin` and `cos` say; 0 pixels over at the end.
- **Orbit, around a centre 1000 units ahead:** `mouse 300 0` moves the eye to (-11842.6, -69526.1), which is (0, -1000) rotated by +0.46 rad about +z; the wheel changes the distance; strafing moves eye and centre together. The frame has the same size in bytes as the direct one; the logged pose ends exactly at the start.
- **First person, `mouse 195 0` (0.3 rad):** against the game with `player->setangle z 17.1887` from the same place, the buildings, posts and Eldafire stand at the same x to within a pixel (roof edges at 390 and 655, her at 803, posts at 915, 1050, 1172). The game's frame is 10 pixels lower in the picture throughout, as at yaw 0. An even shift at every depth means a small difference of pitch, not of height, as a height difference would shift near things more than far ones (correction by review; the cause was not looked for). The frame C4 logged the pose from has its roof at the same height as the editor's.
- **Ortho, looking down with up +y (`ortho 60`):** the roofs, the dock and the water are south where they are in the game, not mirrored.

Tests: openmw-cs-tests 167.

Check list, for a person (the keys are in Preferences > Key Bindings):

16. **First person, look.** In a Scene of `#-2 -9`, with the `1st` camera button: hold `scene-navi-primary` and move the mouse right. The view turns right. Move it down: the view looks down. It stops before straight down and straight up, and the horizon stays level.
17. **First person, move.** `free-forward`, `free-backward`, `free-left`, `free-right`: each moves that way at a steady speed, and more than one key together works. Hold `scene-speed-modifier`: faster. Press `free-speed-mode`: faster until pressed again.
18. **Strafe and wheel.** Hold `scene-navi-secondary` and move the mouse right: the scene follows the mouse. The wheel moves forward and back, faster with `scene-speed-modifier`.
19. **Free camera.** Same as 16 and 17 with the `free` button, and `free-roll-left` and `free-roll-right` roll the view. Back on `1st`, the horizon levels again.
20. **Orbit.** With the `orbit` button, click on a building, then hold `scene-navi-primary` and move the mouse: the view circles the point that was in the middle of the view. The wheel goes in and stops at the centre without passing through. `scene-navi-secondary` moves eye and centre together. `orbit-up`, `orbit-down`, `orbit-left`, `orbit-right` circle it, `orbit-roll-left` and `orbit-roll-right` roll. Turn on Preferences > 3D Scene Input > "Keep camera roll constant for orbital camera" and circle again: the horizon stays level.
21. **Camera cycle.** Press `scene-cam-cycle`: the toolbar button goes 1st, orbit, free... and the view does not jump.
22. **Feel.** With the mouse: does it feel like the OSG editor in speed and direction, in each mode? Note anything off. In Preferences > 3D Scene Input, change the sensitivity, invert and speed settings: each takes effect at once.
23. **Orthographic.** Preferences > Rendering > "Orthographic projection": the view changes and stays the right way up, and is not clipped at the near plane. The size setting zooms it.

### C6 Picking and tags (2026-10-31)

New `picking.hpp`: `viewRay`, `pick` and `pickNearest` over `vsg::LineSegmentIntersector`, with no window and no GPU. `apps/opencs_tests/view/render/testpicking.cpp` (14 cases on a generated scene, written first from the old `mousePick`) checks: nearest hit and its tag, masks, terrain with no tag, a strip's two triangles, empty sky at the placement distance, a back face not counting, triangle indices, a transformed object, and rays at the top, right and middle of a perspective and an orthographic picture, which is where VSG's Y flip and reversed depth show.

- **Tags:** `Picking::setTag` (`vsg::Object::setObject`) on `Object`'s root, `CellArrow`'s and `CellMarker`'s mask nodes. The tag nearest the root of the hit's node path wins. `TagBase` left `osg::Referenced` in C3.
- **Masks:** the intersector's `traversalMask` against the `Switch` from `SceneHost::maskNode`.
- **`mousePick`:** `SceneHost::pick`, over `contentRoot` so that the gradient never counts. Marker hits and `handleMarkerHighlight` wait for C7. Orbit's first centre is `SceneHost::pickNearest` at the middle of the view, either side of a triangle, as `LIMIT_NEAREST` was.
- **Not `CameraDistanceIntersector` or `Render::nearestIntersection`:** the survey named them, but the first has no use without a range limit, and the second takes the nearest with no back-face test and looks for an `Anim::Transform`, whereas the editor wants a tag.

Found on the way, by the rays:
- **Terrain could not be picked, in the game either: a real bug in shared code, fixed.** `vsgUtil::ArrayState` defined `clone`, the name before VSG 1.1 renamed it `cloneArrayState`, so an intersector got a plain `ArrayState` with no vertices, with no error. Terrain's `HeightArrayState` and `Anim::SoftwareSkin`'s state both use it. So on the game's side, `castRay` with `Mask_Terrain` went through terrain until now, and now does not; software-skinned hits see skinned vertices. New test `vsgutil/testarraystate.cpp`. `HeightArrayState::vertexArray` now returns nothing rather than dereference null if there is no height map. Its data did stay on the CPU: the worry in the survey does not apply there.
- **`vsg::LineSegmentIntersector` reads triangle lists only,** so water and markers, unindexed strips, were never hit. `Picking::Intersector` adds strips.
- **`NullArrayState`** hid arrows, borders and markers from intersection, and water: removed in the editor's geometry; `SimpleWater` gets a defaulted `intersectable`, off for the game.
- **`CellMarker`'s quad was clockwise,** so it counted as a back face, unseen as it does not cull. Now counter-clockwise, and the frame shows the same label.

Scripted rays (`--camera-script "...; wait; ray x y hexmask"`, through `WorldspaceWidget::mousePick`). Seyda Neen, `#-2 -9`, `--neighbours 2`, from the game's camera, 1280x720:

| Pixel, mask | Result |
|---|---|
| 700, 240, references | `ex_nord_house_01` at (-12162, -68324, 442), triangle 30 31 32 |
| 1027, 270, references (a skinned npc) | `eldafire` at (-11420, -68232, 392) |
| 925, 290, references | `Imperial Guard` at (-11438, -67770, 399) |
| 640, 450, terrain | no tag, (-12288, -68837, 75.7), triangle 2118 2184 2119 |
| 640, 650, water | no tag, (-12288, -69219, -1) |
| 640, 650, terrain | no tag, (-12288, -69143, -36): under the water, as it is |
| 900, 30, everything | no hit, 50 units along the ray (the preference `distance`) |
| 640, 370, markers | a marker's tag, at the middle of cell `-2,-8` (-12288, -61440) |
| 300, 100, arrows, top-down view of `#-2 -9` alone | arrow's tag, `Direction: North` |

Orbit's first centre: from Arrille's door, `mouse 650 0` turned the eye by 1 rad around the door hit at (-242, 124), with the logged eye where the sum says. Terrain through `pickNearest`: the eye moved by what a centre about 985 units ahead gives, where the terrain is at about that distance.

Check of the game, which had a shared-code change: Seyda Neen frame 600, before and after: 2,130 of 921,600 pixels over 6, in moving sky and the water's ripples, viewed; Caldera Mages Guild viewed. Not checked by hand: the game features that use `RenderManager::castRay`, which now stop at terrain (`World` in `worldimp.cpp`: the faced object at 1723 and 1737, dropping an item at the cursor at 1911 and 1939, and the ray at 2039, touch spells at 2914; and Lua's `castRenderingRay`). For a faced object this means terrain now hides what is behind it. toddhow's check: in game, an item dropped from the inventory onto open ground lands where the cursor is, and the crosshair's tooltip does not name something behind a hill.

Tests on Windows: components-tests 2222, with `DetourNavigatorRecastMeshObjectTest` failing in one or two cases in some full runs, the known intermittent one, and passing alone; openmw-tests 717; openmw-cs-tests 181. Linux is in the entry that follows.

Not verified: real clicks (below), Qt coordinates at a display scale other than 100% (`localPos` over `width()` should be right, as both are in logical pixels), and pathgrid hits (C9). Selection itself is C7: a click on an object does nothing visible yet.

Check list, for a person:

24. **Tool tips.** In Preferences > Tooltips turn on scene tool tips. Hold the mouse over a building in `#-2 -9` for the delay: its record ID appears. Over a cell arrow: "Direction: North" and the rest. Over open ground and sky: nothing.
25. **Arrow clicks.** With only `#-2 -9` in view, zoom out so that the arrows show. `scene-edit-primary` on the north arrow adds cell `#-2 -8`. `scene-edit-secondary` on an arrow adds that cell and removes the old one. `scene-select-primary` and `scene-select-secondary` do likewise for a row of cells. Check that the arrow hit is the one under the pointer at the middle and at the edges of the window, at an odd window size, and with the Windows display scale above 100% if you have it. Tell me if the Y is mirrored: a click at the top of the view hitting what is at the bottom.
26. **Orbit centre by click.** Press `orbit` over a building in the view's middle: circling goes around the building, not a point 10,000 units away. Over open sky it circles a point 10,000 units ahead.
27. **Ortho.** Repeat 24 with Orthographic projection on.
28. **Picking does not stall.** Sweep the mouse over a large exterior with tool tips on. The editor stays smooth. Say if a hover or a click pauses it: terrain's ray recomputes a chunk's vertices.

### Linux, C5 and C6 (2026-10-31)

At `4a2dc1c8ab`, with `-Werror`: components-tests 2224, openmw-tests 717, all 32 Lua integration tests. `openmw-cs` and `openmw-cs-tests` build, 181 pass, and the editor started and closed under xvfb with lavapipe on the example suite. No Linux frame or ray was captured or viewed. An earlier run had one failure, `CompatMorrowindTest.ManifestValidation`, while another session was using the same build volume; it did not repeat. The seen-once capture crash from C4's items was hunted under a debug runner without a crash, but the runs under it were stopped early, so it stays open.

### Review of C4's fixes, C5 and C6 (2026-10-31)

Read by a reviewer, checked here:

- **Terrain and skinned hits reported the wrong normal: a real bug in the game, fixed.** `vsgUtil::worldNormal` fell back to (0,0,1) for terrain, which has no normal array, so once rays stopped at terrain `World::canPlaceObject` accepted any slope, where OSG's `getWorldIntersectNormal` (`renderingmanager.cpp` at the fork) refused over 30 degrees, and Lua's `castRenderingRay` normals were wrong. Skinned hits read the bind pose's normals. Now `vsgUtil::computedHitVertices` rebuilds the hit's own vertices through its StateGroup's array state (the proxy is overwritten by later draws, so it cannot be read afterwards), and the normal is its triangle's. `HeightArrayState` moved to `components/terrain/heightarraystate.hpp` to be tested. New `vsgutil/testhitnormal.cpp`, with a real intersector and no GPU: slope, flat, a later draw overwriting the proxy, a chunk's vertices, a posed skin, plain geometry. Not done: the drop-on-a-steep-slope script in the game.
- **Facing test:** `Picking::isBackface` reads those same vertices, so posed limbs test correctly and terrain's back faces now count: a pick from below passes through, as old `mousePick` did. Terrain's triangles face up (the test above).
- **Frame time of the game with terrain rays** (`scratchpad/compare/perf.sh`, wall ms, median and 95th percentile of the last 400 of 900 frames; "before" has the pre-fix `clone` name restored temporarily, so rays pass through terrain):

| Scene | Before | After |
|---|---|---|
| Balmora | 2.93, 3.98 | 2.20, 3.03 |
| Seyda Neen | 3.40, 4.66 | 2.73, 3.57 |
| Pelagiad | 2.75, 3.59 | 1.86, 2.53 |
| Ald-ruhn | 1.80, 2.67 | 1.59, 2.28 |
| Sadrith Mora | 2.16, 2.91 | 2.98, 3.79 |
| Vivec Arena | 0.23, 0.49 | 0.23, 0.41 |
| Ebonheart | 1.55, 2.22 | 1.51, 2.04 |
| Caldera Mages Guild | 0.99, 1.37 | 1.04, 1.60 |
| Caius Cosades' House | 1.13, 2.01 | 0.65, 0.92 |

  No cost shows: the differences go both ways and are run-to-run noise, not a saving. So grids are not cached. One pass each, with no input, so GUI mode's 50x range and a sweep across terrain were not measured.
- **Idle redraw:** `SceneHost::setViewMatrix` compared a lossy round trip exactly, and controllers normalize every tick. It now compares view matrices with a tolerance (`CameraPose::sameView`). `--capture-idle-ticks 100` at `#-1 -9`: 0 frames. The bad count was not reproduced first, so whether it was really broken is not shown.
- **NaN view:** `stabilize` skips an up along the view (`CameraPose::canStabilize`), tested in `testcameraposereview.cpp`.
- **The note on yaw's sign:** measured again with `slot naviPrimary 1`. First person `mouse 195 0` moves the centre to +x, a turn to the right, as `setangle z` +17 degrees does; orbit `mouse 300 0` moves the eye from (0, -1000) to (445.3, -895.4) relative to the centre, +0.46 rad about +z, as logged. No wrong sign found, so the note stands but for the pitch correction above.

Not covered by a test: the camera controllers themselves, which need a `SceneHost`.

### C7 Instance mode, selection and object markers (2026-10-31)

`instancemode`, `instanceselectionmode`, `selectionmode`, `instancemovemode` and `objectmarker` are back in the build. `Object::apply`, the macros and every `CSMWorld::` command are the old code's. The sums moved to `instancemath.hpp` and the gizmo's shape to `selectionmarkergeometry.hpp`, both with no window, tested by `testinstancemath.cpp` and `testselectionmarker.cpp` (24 cases, written first).

**Choice for toddhow to overrule: the selection highlight is a white box of lines** around the model's bounds, turning and scaling with the object, drawn over everything: magenta for a selection group, yellow for a snap target, the old colours. `osgFX::Scribe` drew the model's own wireframe, which would need a new pipeline variant for each of the many model pipelines. The box is one small geometry per selected object, and it plainly marks selection. Selecting a whole cell compiles one for each.

What each old part became:
- **`selectionmarker.osgt`:** generated geometry with that file's numbers, checked against its bounds, triangle counts (96, 60 and 1152 per part) and colours. Not hung under the object: one node, which follows the attached object each frame and looks it up by ID, so a deleted object cannot leave it dangling.
- **Clip plane for the rotate rings:** triangles on the far side collapse to a point in a dynamic array, whole triangles rather than an exact cut, so a ring's edge is stepped at 1/48 of a turn. Rays skip them too, and `hitBehindMarker` stays.
- **`marker_error.osgt`:** `files/data/meshes/marker_error.dae`, converted with its five meshes and materials. **This restores the game too:** a broken or missing model showed nothing on the Vulkan build, as no reader knew `.osgt`, and now shows the yellow sign the fork point had. Captured in the editor with a corrupt `x/ex_nord_house_01.nif` in `--data-local`: the translucent sign stands in the house's place. The label inside looks paler than its dark material under day mode's light; the old look cannot be run for comparison.
- **`--camera-script`:** `editmode`, `press`, `drag`, `hover` (a real `QMouseEvent`), `submode`, `modeslot`, `selection`, `undo`, `redo`, `undolog`, through `WorldspaceWidget::scriptPress` and `scriptDrag`, which make the shortcuts' calls on the edit mode. The capture now has a hidden `SceneToolbar`.

**Rotation order, corrected.** C4's `adjustTransform` copied the game's `makeInversedOrderObjectOsgQuat` (x, then y, then z) with a comment calling it the OSG editor's `zr * yr * xr`, which is z first: `vsg::dquat`'s product has OSG's order. The tool's sums assume the OSG editor's, so it is back to that. **So an object tilted on two axes looks different in the editor than in the game, as it did in the OSG editor.** I did not fix that old mismatch, because it would change which rotations an edit writes: for toddhow to decide.

Checks at Seyda Neen, `#-2 -9`, `ex_nord_house_01`, frames viewed:
- **Select:** a press at (640, 330) selects `ref#98681`, and the box fits the house. The first box was far too big, because `vsg::ComputeBounds` trusts node bounds by default: `useNodeBounds` is off now.
- **Move and undo:** `drag edit 613 300 613 200 4` moves it from (-12107.8, -68105.1, 336.4) to (-12089.9, -68017.6, 530.8) in the plane facing the camera; the undo stack shows one `Move Instances`; `undo` restores the position exactly, `redo` repeats it.
- **Rotate mode:** the three rings draw their near halves only.

Not verified: everything on the list below. Scale, group rotation, drop to collision, snap and the box and sphere selection volumes have been compiled and not run or seen, but for the sums' tests. A scripted hover over an arrow did not land on the arrow, so the highlight and an axis lock have not been seen.

29. **Select (C7).** In a Scene of `#-2 -9`, edit mode Instance: `scene-select-primary` on a house. A white box appears around it, with the coloured arrows and squares at its origin. On open ground: the box goes.
30. **Add and toggle.** `scene-select-secondary` on another object adds it, and again removes it. The marker stays at the last selected.
31. **Highlight.** Move the pointer over each arrow and square: its colour brightens, red for X, green for Y, blue for Z, and dims when the pointer leaves.
32. **Move.** `scene-edit-primary` (the right button by default; the left button is `scene-select-primary` and always draws a selection box, also over an arrow, as in the OSG editor) drag on an arrow, which counts when the pointer is within 4 pixels of it, from where the button went down: the object moves along that axis only. On a square: in that plane. On nothing: in the plane facing the camera, following the pointer. `scene-edit-secondary`: in grid steps. The wheel during a drag moves it nearer and further.
33. **Rotate.** Press `scene-submode-rotate`: three half rings. Drag on a ring: the object turns about that axis the way the pointer goes round. Drag elsewhere: it turns freely. The rings' far halves can't be grabbed. Secondary: snaps by the grid angle on release. With two selected, they turn around their common centre.
34. **Scale.** `scene-submode-scale`: arrows end in cubes. Dragging away from the object grows it, between 0.5 and 2.
35. **Axis keys.** `scene-axis-x`, `-y`, `-z`: the axis lights up and locks; the same key again unlocks.
36. **Undo.** Ctrl+Z after each of 32 to 34: the object returns and one step goes off the stack. Ctrl+Y repeats it. The References table shows the same numbers throughout.
37. **Box selection.** With each selection mode (centred cube, corner cube, sphere): drag `scene-select-primary`. A translucent volume grows, and on release what's inside it is selected. It must not get in the way of what is picked.
38. **Snap target.** `scene-select-tertiary` on an object: a yellow box. Then `scene-edit-secondary` drag of another: it moves in steps along the target's axes.
39. **Delete, duplicate, drop.** `scene-delete` with a selection: gone, undo restores it, and the marker returns with no crash. `scene-duplicate`: a copy. Raise an object, press `scene-instance-drop`: it falls to the ground or the object below, not to itself.
40. **Hide.** `scene-toggle-visibility` hides the selection, also from clicks; `scene-unhide-all` brings it back. `scene-toggle-marker` hides the marker only.
41. **Groups.** `scene-save-1` with a selection, clear it, `scene-group-1`: selected again, in magenta.
42. **Drop a record.** Drag a Static from the table into the view: a new instance where you drop it. The cell-outside-view preferences act as named.
43. **Tilted objects.** Compare a rock tilted on two axes with the game: tell me if the editor's mismatch (above) matters to you.

### C8 Terrain shape and texture modes (2026-10-31)

`terrainshapemode`, `terraintexturemode`, `terrainselection`, `brushdraw` and `commands` are back in the build. The edits, limits, macros and `TouchLandCommand` calls are the old code's, with only their math types changed. `terraintoolgeometry.hpp` holds the outlines, selection lines and land normal with no window, tested by `testterraintools.cpp` (12 cases, written first).

- **Heights on screen:** `Terrain::Builder` kept each batch's data by its bounds, so a rebuild showed the old land. `Paging::clearCache` forgets it before a cell's rebuild; the game never calls it.
- **Cost per edit, measured** (`OPENMW_DEBUG_LEVEL=DEBUG` logs each rebuild): about 1 ms a cell mid-session and 3 to 4 ms a cell with the document's commit at the end of an edit, which rebuilds all nine shown cells, so about 35 ms once at a release. No caching was needed. This machine only; the frames' drawing and a very large selection were not timed.
- **Project-new land textures:** `Storage` now keeps plugin -1 in `mProjectLayerIndices`, and `TerrainStorage::enumerateLayers` no longer skips it. Painting touches a land's textures into plugin -1, so without this every painted cell showed the default layer. The game has no negative plugin. A cell's layer array is rebuilt when the land textures table changes. New `esm3terrain/testprojectlayers.cpp`.
- **Brush outline heights:** bilinear from the land records plus alterations, not a ray per point as the OSG editor had. Up to a thousand rays each mouse move would each recompute a chunk's vertices. The line may differ from the drawn triangles by a little, and it has no depth test.
- **Lines:** `OverlayLines`, a kept dynamic array after `Render::DebugGeometry`'s pattern. That class has no way to switch off its depth test, so it is not used itself.

Checks at Seyda Neen, through the real tool code below the Qt events, frames viewed:
- **Raise:** `shapebrush 2 20 1 60; drag edit 640 560 660 560 3`: a mound with the brush's circle lying on it, shaded by the new normals, houses unmoved as the terrain rises under them. The stack has one `Edit shape and normal records`.
- **Undo:** `undo` puts the ground back as it was by eye; the terrain selection grid of a following `drag select` follows the ground over the same area.
- **Paint:** `texbrush 2 4 sand; drag edit 400 560 700 600 6`: a sweep of `Tx_sand_01`, one `Edit texture records` on the stack. That the colour is the right texture is judged by eye and from the record, not against the game.

Not verified: the list below. Smooth, flatten, equalize, the slope limit, the seam at a cell's edge beyond what a large brush crosses, and creating land from a cell with none were not run.

44. **Raise and lower (C8).** Paged `#-2 -9`, Terrain land editing mode. Open the brush tool: choose Circle, size 10. "Drag" tool: `scene-edit-primary` drag up on the ground; the ground rises as you drag, smoothly, without stalls. Release: it stays. Try Paint to raise, Paint to lower: painting rises or sinks where the pointer goes.
45. **Smooth, flatten, equalize.** Each on bumpy ground: it does what it says, and on release stays so.
46. **Seam.** Raise right across a cell's border, then a corner of four cells. No crack, on screen or after moving the camera away and back.
47. **Slope limit.** Try to raise a spike with a point brush: if the slope is too steep the edit limits itself or is dropped with a warning in the log, as before.
48. **Undo and redo.** Ctrl+Z restores the ground and its shading at once; Ctrl+Y brings it back. Repeat with a terrain selection showing: the selection follows the ground.
49. **Brush outline.** Move the pointer: the outline for point, square and circle follows the ground, snapping to vertices, without lag. Tell me if it sits visibly off the ground on a steep slope.
50. **Selection.** `scene-select-primary` drag: a grid of vertices. `scene-select-secondary` drag: toggles. In the brush tool's Custom shape, "use selection": the shape takes it.
51. **Paint textures.** Terrain texture mode, from the Land Textures table drag a texture to the brush tool. Paint: the texture appears where the pointer goes, in the project and after reloading a cell. Ctrl+Z restores it.
52. **New land texture.** Add a new land texture to the project, set its image, and paint with it: **that texture**, not the default. Change its image in the table: the land follows.
53. **Cell with no land.** Paint and raise in an empty cell (prefs: land edit outside cells, outside visible): the cell and land are made as those preferences say.
54. **Save.** Save the plugin, load it in the game: the terrain and textures are there.

### C9 Pathgrid mode and preview (2026-10-31)

`pathgrid`, `pathgridmode` and `pathgridselectionmode` are back, so all of `view/render` builds and `OPENEDEN_CS_VIEW_RENDER_PENDING` is gone. `pathgridgeometry.hpp` holds the old `SceneUtil::createPathgridGeometry` and its wireframe as data, tested by `testpathgridgeometry.cpp` (7 cases, written first). `Pathgrid::update`, once a node's update callback, runs from `Cell::frameUpdate`. Commands are the old code's.

- **A use after free in the old code, fixed here, command calls unchanged.** `applyPosition` kept a reference into the record across `commands.push`, whose `ModifyCommand` replaces the record, and a scripted move crashed with an access violation at `pathgrid.cpp:362` (found with `crashrun` and `dumpstack`). OSG's editor had the same lines and may only have been lucky with freed memory. `applyPosition`, `applyEdge`, `applyEdges`, `applyRemoveNodes` and `applyPoint`'s deleted branch now work on a copy of the record.
- **Preview:** the widget came back at C3, but a preview of a referenceable threw on every tick from `getPosition`, "object does not represent a reference", so it never drew. It was never opened before now. It frames the model's bounds with the new `Object::getBoundsCorners`.
- `--capture-preview ID` captures a `PreviewWidget` beside the cell's view.

Checks at Caius Cosades' House, frames viewed, through the real tool code:
- **Display:** blue diamonds with their cyan edges where the room's nodes stand.
- **Select:** `press select` and `press select2` on two nodes: wireframes round both.
- **Edge:** `drag edit2` from one to the other: one `Add edge between nodes`, a new line in the frame; `undo` and `redo` step the stack.
- **Move:** `drag edit`: one `Move pathgrid node(s)`, and `undo` ran with no crash.
- **Add:** `press edit` on the floor: one `Add node`, a new diamond there.
- **Preview of `chargen boat`:** the whole boat with its sail on the gradient. It is small and a little off centre, as `SceneHost::frame` works from medians, which a preview's eight corners do not suit well. Left as it is: the camera can be moved.

Not verified: the list below. Editing a pathgrid in a paged view and `applyRemoveNodes` were not run.

55. **Pathgrid display (C9).** A Scene of an interior with a pathgrid, edit mode Pathgrid. Diamonds and edges appear. The Pathgrid button of the visibility toolbar hides and shows them.
56. **Select.** `scene-select-primary` on a node: purple wireframe; `scene-select-secondary` adds another; the last has the paler colour. Tool tip over a node: "Pathgrid: <cell> (<number>)".
57. **Add.** `scene-edit-primary` on the floor adds a node where you clicked, or just selects, by the context-select preference.
58. **Move.** Drag with `scene-edit-primary`: selected nodes follow the pointer; release applies, Ctrl+Z restores.
59. **Connect.** Drag `scene-edit-secondary` from one node to another: a purple line shows while dragging, red when not over a node. Release adds an edge; Ctrl+Z removes it.
60. **Remove.** With the selection tool's context menu, remove selected nodes, and remove edges between them. The edges left still join the right nodes, and Ctrl+Z restores them. This ran through no script.
61. **Exterior.** The same in a paged exterior, across two cells: only one cell's grid is selected at a time.
62. **Preview.** In a table, right-click a record, Preview: the object is in view, in orbit mode. Say if the framing is a nuisance. Edit its model field: the preview follows. Delete the record: the preview closes. Preview a Reference too.

### Crash runs, C7 to C9 (2026-10-31)

Running the scripted capture path repeatedly found a real shutdown crash: an access violation in `OverlayLines::~OverlayLines`. Edit modes belong to the toolbar, which Qt destroyed after the view, so a terrain tool's lines touched the dead `SceneHost`. It appeared on all four exterior runs that used a terrain mode, under `crashrun`, while plain runs exited 0 with the bad read unnoticed. `OverlayLines` now checks `SceneHost::aliveToken()`.

After the fix, 12 plain runs, four each of an exterior (select, move, undo, raise, undo), Caius Cosades' House (pathgrid select, move, undo) and Arrille's Tradehouse (select, rotate mode): all exit 0, no crash dump. Under `crashrun`, 6 runs had no exception, but 5 of the 6 did not exit within its 60 seconds after logging `Auto-quit`, and one earlier run exited with code 57005 (0xDEAD); plain runs do neither. Those come from the runner or from running under the debug API, which is not proven, and no stack was taken of them (settled, and 0xDEAD corrected, in "The exit hang" below). The segfault seen once at C4 was on Arrille's interior with no terrain tool, so this fix does not explain it; it was not seen again.

Check of the game, which had `Storage`, `Builder` and hit-normal changes: Balmora and Caldera Mages Guild started and ran 320 frames at this code, Balmora's frame viewed: terrain, sky and HUD as expected. Not compared pixel by pixel, beyond the frame times above.

### Linux, C7 to C9 (2026-10-31)

At `84d1da9858`, with `-Werror`: components-tests 2231, openmw-tests 717, all 32 Lua integration tests. `openmw-cs` and `openmw-cs-tests` with `-Werror` at the commit after the shutdown fix: 226 pass, and the editor started and closed under xvfb with lavapipe on the example suite. `-Werror` found a copying loop variable in `testinstancemath.cpp`, this stage's own test, fixed with no change of inputs or expected values. No Linux frame was captured or viewed. `DetourNavigatorRecastMeshObjectTest`: the fixture's member order was the cause, fixed in `f3fd95cd84` with toddhow's approval; 200 repeats on Windows, 0 failures.

### The exit hang: the harness, not the editor (2026-10-31)

**Cause: Windows' debug heap, which a process started under a debugger gets.** `crashrun` starts the editor with `DEBUG_ONLY_THIS_PROCESS`, which turns on the heap's validation, and every free then costs far more. The process was not hung: it exits with code 0 by itself after about two and a half minutes, longer than `crashrun`'s 60 second limit.

Evidence, on the scripted exterior capture (`#-2 -9`, select, move, undo, raise, undo):
- **Stack of a caught run**, 25 seconds after `Auto-quit`, read with a new real stack walk over every thread (`dumpstack2`), three samples seconds apart: the main thread is past `main`, in `exit` (`__scrt_common_main_seh`, `exe_common.inl:295`), and in `ntdll` with a heap address that changes between samples. Its CPU time rose from 41 s to 98 s across the samples, so it was working, not waiting. The other ten threads are idle thread pool workers and two waits in `WaitForMultipleObjects`, with no thread in editor or VSG code.
- **That same run left alone:** `exit 0`, 145 seconds after `Auto-quit`.
- **Time from `Auto-quit` to exit:** plain runs 250, 483, 252 and 250 ms; under `crashrun` with `_NO_DEBUG_HEAP=1` 3089, 3126, 3095 and 467 ms; under `crashrun` as before 147,603 ms, exit 0. **Open, unexplained:** with `_NO_DEBUG_HEAP=1` the exit under `crashrun` still took about 3 s, some 12 times a plain run's 0.25 s, in three runs of four (the fourth 467 ms). Not looked into further; the debug API's per-event round trips (thread and DLL unload events) are a guess, not shown.

**The 0xDEAD exit was not the runner, as the entry above said in part.** It is OpenEden's own crash monitor (`components/crashcatcher/windowscrashmonitor.cpp`, `CrashMonitor::run`), which terminates the app with that code after `mFreezeAbort`. The freeze dump it wrote, `openmw-cs-freeze.dmp`, is dated 16:22, the time of that run (`crash-ext2`), and has the main thread in `VFS::Manager::~Manager`, under `CSMWorld::Data::~Data` and `DocumentManager::~DocumentManager` (`capture.cpp`): freeing, under the debug heap, while nothing pumps messages, so Windows called the window hung. Nobody pressed Abort. **A hypothesis, by reading only and not run: an inherited bug.** `hideFreezeMessageBox` closes the box with `WM_CLOSE` when the app recovers; SDL's task dialog reports that as `IDCANCEL`, which it returns as `nCancelButton`, initially 0 (`SDL_windowsmessagebox.c`, `WIN_ShowMessageBox`); and 0 is the Abort button's id. So an app that recovers from a freeze the monitor noticed is then killed, in the game too. Not changed here: it is outside this task, and for toddhow to decide. A likely fix is a non-zero id for the button.

Not hung either, since the fixes below: 6 runs of the same three scripts under `crashrun` with `_NO_DEBUG_HEAP=1`, no exception, all exit. The game: 7 captures (`shot.sh`, Caldera Mages Guild 5, Balmora 2) all exit 0 within `timeout`. The seen-once segfault at C4 is still unexplained.

For later crash hunts: set `_NO_DEBUG_HEAP=1` under `crashrun`.

### Review of C7 to C9 (2026-10-31)

A fresh reviewer read the code. Each finding was checked here, and all are real:
- **The game: `marker_error.dae`'s materials.** In the old `.osgt` geometry 2 refers to StateSet 4 and geometry 4 to StateSet 22. The conversion made opaque grey materials of those. `g2` now binds `m1`, `g4` binds `m3`, `e2`, `e4`, `m2` and `m4` are gone. Frame viewed in the editor with a corrupt model: the sign is all translucent yellow around its pale label, with no opaque grey. The label is paler than its dark material, as noted at C7.
- **Use after free at exit, or closing mid-drag:** `~InstanceSelectionMode` called `getWorldspaceWidget().getHost()` with the widget gone. It now keeps the host's `aliveToken()` with `mBaseNode` and checks that first.
- **The game: hit normals on smooth meshes.** `vsgUtil::worldNormal` interpolated a mesh's vertex normals, where its own comments and OSG's `getWorldIntersectNormal` take the triangle's, and a zero sum gave NaN, which `canPlaceObject`'s comparison accepted. It now always takes the triangle's, straight up when it has no area. New `vsgutil/testhitnormalmesh.cpp`, 3 cases. The argument `arrayID` is unused and stays, since the committed tests pass it. Not done: the placement script in the game.
- **`Object::setHidden`** indexed a child that an object with no model does not have: guarded. A model loaded while hidden now stays hidden.
- **Snap target on a selected object drew nothing:** `setSelected(false)` recursed and cleared the flag. The two now clear each other's flag directly. **One change from the OSG editor, for toddhow:** there the yellow was drawn while the flag was false, and selecting a snap target ended with `mSelected` false while drawn white. Now the flags agree with what is drawn, so such an object does act as the snap target.
- **`DeletionQueue`'s size against more than three swapchain images:** not too short. What must pass is the three-fence ring (`Viewer::assignRecordAndSubmitTaskAndPresentation`, `numBuffers`), not the image count, and a larger count only makes the queue longer. The editor uses `sizeForPostFrameAdvance` and is safe at any count. **The game's path can be one frame short with exactly two images:** `Engine::setup` resizes to `numFrames()`, and an object added in frame K's update, last used in K-1, needs K+2's fence wait, so a queue of 2 frees it one frame early. The default asks for 3 (`vsg::SwapchainPreferences::imageCount`, raised or lowered by the surface in `Swapchain::create`), so only a surface limited to 2 images reaches this. Reported, not changed. The headless Engine's queue of 3 waits on its fence every frame.

Scripted runs after the fixes: see the hang entry. Not run: a mid-drag close (check-list step 63 below), drop to collision with an object without a model, the snap target by hand (check-list step 38).

### D1 OpenSceneGraph deleted (2026-10-31)

Before deleting, every file was checked against what the Windows build compiles and includes (`ninja -t deps` and the build's commands, all application targets on): none of the 99 listed files was compiled or included. That check found two OSG lines that did compile, because vcpkg's OSG headers sit in the same include folder as every other dependency: `#include <osg/ref_ptr>` in `worldspacewidget.hpp`, pulled into about twenty editor files, and a forward declaration of `osg::Vec3f` in `unpagedworldspacewidget.cpp`. Both are gone. **So a green Windows build alone proves nothing about OSG here; `CI/check_no_osg.sh` is what does.**

Deleted:
- `extern/osgQt`; the 99 listed files, which empties `components/nifosg` and `components/shader`, leaves `components/sceneutil` with `detourdebugdraw`, `navmesh`, `workqueue` and `lightingmethod.hpp`, and `components/esmterrain` with `gridsampling.hpp`; their unbuilt companions `resource/errormarker.hpp`, `sceneutil/lightcommon.cpp`, `serialize.hpp` (OSG serializers) and `textkeymap.hpp`; `misc/osguservalues`, used only by listed files.
- CMake: the commented OSG block and `CheckOsgMultiview.cmake`, the COLLADA-DOM block, `extern/CMakeLists.txt`'s OSG fetch (it ran whenever `OPENMW_USE_SYSTEM_OSG` was not set), the commented static-OSG link block, `osgyaml`, `WholeArchive.cmake`'s OSG comment, `OSG_PLUGIN_PREFIX_DIR` in the macOS bundle step, and `OpenGL` and `OpenGLWidgets` from the Qt lookup (nothing links them; vsgQt needs `Widgets`).
- **`OPENEDEN_BUILD_VSG` is gone, and with it the system-VSG branch.** It was never an `option()`, only a `-D` value, and without it the build looked for VSG 1.0.9 from the system, which cannot build this tree (pinned 1.1.16, vsgQt, glslang). The fetch is now unconditional. `components-tests` still defines `OPENEDEN_BUILD_VSG=1` unconditionally, because thirteen committed test files test it under `#if`; those guards are now always true, for whoever may edit tests.
- The mesh lists of `ResourcesManager::getMeshExtensions` and `createNodeOptions`'s fallback name only `nif` and `dae`: no reader for `osg`, `osgt`, `osgb`, `osgx` or `osg2` exists.
- `files/shaders`: the OSG build's 58 shaders (`compatibility/`, `core/`, `lib/core`, and the `lib/` files only they included), never installed by the Vulkan build, with their commented install list.

**Found on the way, a latent break: `lib/material/parallax.glsl` was not installed,** though `default/main.frag` and `terrain/main.frag` include it (since task 0012's V4 terrain maps). The build folder held a copy from the OSG era (dated 2026-10-18), so it worked here; a new build folder would have failed to compile those shaders. It is in the install list now. Every `#include` of an installed shader was checked to be installed, apart from the generated `*.settings`.

`CI/check_no_osg.sh` now reads every tracked C and C++ file, with no skip list: OSG includes (`osg*` and `OpenThreads` headers, also in quotes), `osg*::` names and `namespace osg*`, besides the old `*=` rule. Comments and strings still do not count, as Lua's type names (`osg::Vec3f` and `TimeOfDayInterpolator<osg::Vec4f>`) are API that `global.lua` asserts. Tried on a sample file with each case; passes on the tree.

Docs: `vsgopenmw-build.md` gets an OpenEden note; the modding docs say the OSG native format is not read; `shaders.rst`'s "only in `.osg` files" now names the NIF reader's automatic maps; the inertial locomotion design uses `vsg::vec2`. **Not changed, for toddhow:** the same "(.nif or .osg file)" in `files/settings-default.cfg` and the launcher's strings (its `.ts` files would need `lupdate`), and `apps/openmw/mwlua/README.md`'s "OSG Cull traversal".

Checks: Windows, every target built; components-tests 2232, openmw-tests 717, openmw-cs-tests 226. Frames viewed: game Balmora and Caldera Mages Guild; editor `#-2 -9` and Caius Cosades' House. Linux with `-Werror` (`OPENEDEN_BUILD_VSG` and `OPENMW_USE_SYSTEM_OSG` no longer passed): components-tests 2234, openmw-tests 717, 32 Lua integration tests, `openmw-cs` and `openmw-cs-tests` 226, the editor started and closed under xvfb. The Docker image still had OSG packages installed for this run (D2 removes them).

### D2 CI and docs without OpenSceneGraph (2026-10-31)

**CI has not run: where OpenEden is hosted, and so which CI it keeps, is still an open question (guide, Open questions).** The scripts below were changed and checked locally only.

- `CI/install_debian_deps.sh`: `libopenscenegraph-dev`, `libcollada-dom-dev`, `libopenscenegraph161` and `libcollada-dom2.5-dp0` gone; `libqt6opengl6-dev` replaced by `qt6-base-dev`, which the editor, launcher and wizard need, and `libgl-dev`, which the root `find_package(OpenGL REQUIRED)` needs; `libqt6opengl6` gone from the run group, which gains `libvulkan1` and `mesa-vulkan-drivers` so the Vulkan game can start under xvfb. The static-build group keeps only what MyGUI needs. OpenMW's PPAs stay, for other packages.
- `before_script.linux.sh`: no `OPENMW_USE_SYSTEM_OSG`. `before_script.msvc.sh` and `windows.yml`: no OSG plugin folder, no Qt OpenGL DLLs, no multiview help line. `before_script.macos.sh`: no COLLADA-DOM path. Flatpak manifest: the `collada-dom` and `openscenegraph` modules gone; **not checked: a Flatpak build fetches VSG and the rest through CMake, which a sandboxed build without network refuses.** `docker/Dockerfile.ubuntu` (OpenMW's, still Qt 5 era): OSG out, `qt6-base-dev` and `libvulkan-dev` in. `.gitlab-ci.yml`: no `osgQt` target.
- `scripts/osg_stats.py` and `scripts/HOWTO-benchmark.md` deleted: the Vulkan build writes no `OPENMW_OSG_STATS_FILE`, so `run_integration_tests.sh` and `integration_tests.py` no longer ask for it. `Render::Engine::loop` keeps its own per-frame timing output (`frameStats` in `components/render/engine.cpp`).
- Docs: README gets a "Building OpenEden" paragraph; the guide's stack table and pitfalls (`osgPlugins-3.6.5` no longer needed; stale build folders; vcpkg's OSG headers), a changelog row; `docs/dependencies.md` records what was removed.

Local checks: `bash -n` on every changed shell script; `windows.yml` and the Flatpak manifest parse as YAML; `.gitlab-ci.yml` does not parse at HEAD either, because of vsgopenmw's first line (`vsgopenmw-ci-policy(...)=0`), and without that line it parses up to GitLab's own `!reference` tags; `integration_tests.py` parses. `dumpbin /dependents`: `openmw-cs`, the launcher, the wizard and `vsgQt` load no Qt OpenGL module and no OSG DLL.

**Clean configure, Windows:** a new build folder outside the checkout (`I:\Projects\OpenEden-d2clean`), configured once with the options CI's `before_script.msvc.sh` passes for LuaJit and SQLite, built every target with no error (after the Jolt fix below; the folder was deleted and made again for it). Its `CMakeCache.txt` has no OpenSceneGraph, COLLADA-DOM or `OPENEDEN_BUILD_VSG` entry (only vsgXchange's own optional `osg2vsg_DIR`, not found), and `ninja -t deps` shows no OSG or OpenThreads header in any object. `parallax.glsl` is installed, `Jolt.dll` sits next to the executables, and frames from that build were viewed: the game in the Caldera Mages Guild, the same as the old folder's, and the editor at Seyda Neen (`#-2 -9`). Tests from it: openmw-tests 717, openmw-cs-tests 226, components-tests 2231 of 2232; the failure is `CompatMorrowindTest.ManifestValidation`, which opens `../plugins/...` and so assumes a build folder inside the checkout, as the Linux script's symlink already works around. `MyGUIEngine.dll` must be copied next to them, as the guide's pitfall says.

**Found by the clean configures, older than this task, and fixed in `extern/CMakeLists.txt`: how Jolt was built depended on how often the folder had been configured.** Jolt followed `BUILD_SHARED_LIBS` from the cache (`JOLT_STATIC` is declared and read by nothing), and the VSG section forces that on. So a folder's first configure built a static Jolt without `-fPIC`, and every later configure a shared one. On Linux the first kind cannot be linked into `libcompat_morrowind.so` (`relocation R_X86_64_PC32 ... recompile with -fPIC`), which is how the new Linux folder failed; the Windows folder above had been configured three times and so got a shared Jolt, whose `Joltrd.dll` stayed in `_deps/joltphysics-build` and had to be copied by hand (`build-jolt` holds a copy dated 2026-10-28). Every existing build folder, `build-jolt` and the Docker volume's included, had the shared one. Now Jolt is always shared, and its DLL is written next to the executables on Windows, as the VSG DLLs are. The physics code is unchanged; this is build configuration of the physics library, which the guide puts under the full loop, so it wants a reviewer.

**For toddhow, a visible change from D1:** the editor's asset lists (`ResourcesManager::getMeshExtensions`) now name only `.nif` and `.dae`, so `.osg`, `.osgt`, `.osgb`, `.osgx` and `.osg2` meshes in the data no longer appear in the Meshes table or its pickers. Nothing could load them.

**`CI/check_no_osg.sh`, after the review of D1:** it now also fails on `OpenThreads::` names and `namespace OpenThreads`, on `OSG_` macros, and reads `.ipp`, `.tpp`, `.txx`, `.m` and `*.in` files too. A character literal holding a double quote (`'"'`) no longer starts a string that blanks the rest of the line, and raw strings are blanked whole. Tried on seeded files in a scratch copy: each of a `'"'` line with `osg::` after it, `OSG_LIBRARY_STATIC`, `OSG_VERSION_GREATER_OR_EQUAL`, `OpenThreads::Mutex`, `namespace OpenThreads`, an `osgDB::` call after a digit separator, an `<osg/Node>` include in `.ipp`, `osgUtil::` in `.tpp` and an `<OpenThreads/Thread>` include in `.hpp.in` fails; the same names inside a raw string, a string or a comment do not. The committed version missed the `'"'` case (and the macros, OpenThreads names and extensions). The tree passes.

63. **Close mid-drag (C7 to C9).** Start a drag in each edit mode (instance move, box selection, terrain raise, pathgrid move) and, while holding the button, close the Scene subview, or the main window. No crash, no `openmw-crash.dmp`; reopen a Scene and it works.

### Hit normals with no triangle, after the review of D1 (2026-10-31)

`vsgUtil::worldNormal` gave straight up for a hit triangle with no area, so `World::canPlaceObject` accepted it (0 degrees against 30) and Lua's `castRenderingRay` reported an invented normal; a transform with a zero scale also made `inverse_3x3` give infinities or NaN unchecked. **What OSG did:** its intersector never reports such a hit. `IntersectFunctor::intersect` in `osgUtil/LineSegmentIntersector.cpp` returns early when the determinant is within 1e-10 of zero, which a triangle with no area always gives, so the ray went on to the next triangle; for a hit it took `E1 ^ E2`, normalized, and `getWorldIntersectNormal` transformed it by the inverse matrix (header `osgUtil/LineSegmentIntersector`, in the vcpkg set). Read from OpenMW's OSG fork, branch 3.6, not from the exact revision the fork point built against.

Now a degenerate triangle or a non-finite result gives a zero vector, and `canPlaceObject` refuses a zero or non-finite normal. A hit with no triangle vertices to read still gives straight up, as its committed test says (`aHitWithNoVerticesGivesStraightUp`, not changed). Not matched: OSG's skipping to the next triangle; the zero vector means "no normal" instead. Test changed with toddhow's approval ("Yes, change it", that one case): `aDegenerateTriangleGivesStraightUpAndNotNaN` is now `aDegenerateTriangleGivesNoNormalAndNotNaN`, and its expected normal is (0, 0, 0) where it was (0, 0, 1); its NaN check is unchanged. Windows: components-tests 2232, openmw-tests 717, openmw-cs-tests 226.

### The marker's arrows could not be grabbed (2026-10-31)

toddhow: in Instance mode with a house selected, a drag on an arrow of the move marker "just starts doing the selection box". Found by scripted rays and drags from the Seyda Neen start camera (`--open-cell "#-2 -9" --neighbours 2`, `press select 700 240`, a grid of `ray x y 1`, `drag edit`, `drag select`), and read against `cf3dbd3aa7`.

- **What was not wrong:** the marker is hit. Its parts are triangle lists (`ColouredOptions`, `TRIANGLE_LIST`), so the intersector sees them; the tag, the mask (`Mask_Reference`), the switch and the facing are right; `primaryEditStartDrag` reads the marker's axis as the old code did. `drag edit 680 280 680 240` on the Z arrow's head moved the house from z 336.4 to 518.7 with x and y unchanged and one `Move Instances` on the stack; `drag edit` from open ground moved it in the plane; `drag select` from open ground drew a box. **Left button over an arrow is a box selection, by design and as in the OSG editor:** `primarySelectStartDrag` has no marker branch. An axis move is `scene-edit-primary`, the right button by default.
- **The cause of "not working":** an arrow is hard to hit. The marker's size follows its distance (`distanceScale`, times `object-marker-scale`, 5), and from 1400 units a shaft is about 2 pixels wide, a head 5. In the grid of 64 rays at 30 pixels, one hit the marker, and 40 rays 4 pixels apart over the Z arrow hit its shaft in none and its head twice. And a drag starts at the first mouse move (`WorldspaceWidget::mouseMoveEvent`), not at the press, so the ray was already some pixels off the arrow, found the house, and a drag on "nothing" moved it in the camera plane. The OSG editor had the same geometry and the same start, which the scene's hits do not show; its feel with a real mouse cannot be compared here.
- **Fix:** `Picking::pickNear` takes the nearest ray, among those through pixels up to a radius from the pointer, that hits what the caller accepts; `ObjectMarker::pick` uses it against the marker's own node only, with `sPickRadiusPixels` = 4 (cheap: marker triangles only, about 50 rays), and `WorldspaceWidget::mousePick` and `handleMarkerHighlight` call it, so the hover highlight gets the same reach. The scene's hits no longer include the marker's parts. A drag now starts at where the button went down (`mDragPressPos`, set in `handleInteraction`), for every edit mode. New `testmarkerpicking.cpp` (5 cases): the shaft's middle hits, 3 pixels off misses with no radius, 3 pixels off hits with it (this failed before `pickNear` looked around), 12 pixels off misses, the shaft wins. `capture.cpp`'s `ray` now logs a marker's part and axis.
- **After, scripted:** a ray at (684, 305), about 5 pixels from the Z shaft, gives `Z_Axis`; `drag edit 685 305 685 262 4` moves the house along Z only (336.4 to 518.7), one `Move Instances`, `undo` restores it. Scale cubes (`X_Axis_Scale`, `Y_Wall_Scale`) and rotate rings (`X_Axis_Rot`, `Y_Axis_Rot`, `Z_Axis_Rot`) are hit by rays on the grid. The frame after `hover 684 305` shows the Z arrow bright.
- **Not verified:** a real mouse (the feel of 4 pixels, whether it grabs a neighbouring arrow near the origin, where the three arrows meet), the highlight in a real window, and the axis lock of a rotate or scale drag (the script's `submode` changes the marker only, not the edit mode's own submode).
