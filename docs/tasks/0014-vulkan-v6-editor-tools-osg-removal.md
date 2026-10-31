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
- C5: the camera stays where the default view put it, and the navigation buttons do nothing. `--camera` moves it for captures.
- C6: `mousePick` always misses, so cell arrows cannot be clicked and scene tooltips do not show. The shortcuts that load cells work without a pick.
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

Start `openmw-cs`, choose Edit a content file, open `Morrowind.esm`. In the main window use World > Scene.

1. **Scene opens.** A dock appears with toolbar, 3D view and status bar, and the view has a grey gradient. Expect the view to be empty until a cell is chosen.
2. **Choose a cell.** In World > Cells, right-click "Seyda Neen, Arrille's Tradehouse" and choose View. The interior appears. In the Cells table again, drag an exterior such as `#-2 -9` onto a new Scene subview: it appears there. This checks drag and drop into the Vulkan window.
3. **Resize larger.** Drag the main window's corner to make it about twice as wide. The view fills its dock with no black strip and no stale pixels.
4. **Resize smaller.** Drag it as small as it goes. The view stays correct and the editor does not hang. Drag it back.
5. **Splitter.** Open a References table beside the view (dock them side by side). Drag the divider until the view is as narrow as it goes, then back. Nothing crashes, and the view redraws.
6. **Docking.** Drag the Scene dock's title out of the main window so that it floats, then back. Dock it as a tab with a table and switch tabs both ways. The view shows the same cell each time, and no window flashes up on its own.
7. **Two views.** Open a second Scene, of another cell. Both draw. Close the first: the second keeps drawing. Close the second and open a new one: it draws.
8. **Toolbar over the view.** Click the lighting button in the scene toolbar and choose night, then bright, then day. The pop-up must appear in front of the view, not behind it, and the view must change. Hover over a toolbar button: the tooltip must appear in front of the view.
9. **Focus by clicking.** Click in a table and type in its filter box. Click the 3D view, then press `Tab` (`scene-cam-cycle`, or whatever Preferences > Key Bindings shows; report it if Tab moves the focus instead). The camera mode button on the toolbar changes, so the key reached the view. Click the filter box again and type: the letters go in the box, and no scene shortcut fires.
10. **Shortcut held across a focus change.** In the view, hold the `scene-speed-modifier` key, Alt-Tab away, release it, and return. Then press `scene-cam-cycle`: it must act once, with nothing stuck.
11. **Shortcuts over the view.** With the mouse over a paged view of `#-2 -9`, focus it by clicking and press `scene-load-cam-eastcell` (Preferences > Key Bindings names the keys): one more cell loads to the east. Global menu shortcuts such as Ctrl+S's dialog and Ctrl+Z still work with the view focused.
12. **Mouse tracking.** Nothing to see until C5 and C6: move the mouse over the view and confirm that the editor's frame rate does not fall and nothing logs an error.
13. **Quit.** Close every window with a scene open, and start again. The editor exits with no crash, and no `openmw-crash.dmp` appears in the user data folder.
14. **Linux, once:** the same steps 1 to 8 under X11 or with `QT_QPA_PLATFORM=xcb`. vsgQt has no Wayland path.
