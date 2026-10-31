# V6 survey (task 0014), read-only, at feda4093f4. Nothing was built; "fits" means by reading source.

## 1. vsgQt: fits on paper, needs one build spike
- Tag `v0.5.0`, 2026-08-21, same day as VSG v1.1.16 (https://github.com/vsg-dev/vsgQt/tags; docs/dependencies.md:13). MIT (https://github.com/vsg-dev/vsgQt/blob/v0.5.0/LICENSE.md).
- VSG: `find_package(vsg 1.1.13)`; Qt: `QT_PACKAGE_NAME` cache var, default Qt5, set to Qt6; links only `Widgets`, no private Qt modules (https://raw.githubusercontent.com/vsg-dev/vsgQt/v0.5.0/CMakeLists.txt, .../src/vsgQt/CMakeLists.txt). `Window.cpp` has a `QT_VERSION_MAJOR == 6` branch. No stated Qt 6 minor limit, so 6.6.3 is expected to work; untested.
- Size: 3 sources (KeyboardMap, Window, Viewer). `vsgQt::Window : QWindow`, embedded by `createWindowContainer`; the `vsgqtmdi` example shares one device through `traits->device` across windows of one `vsgQt::Viewer` (.../examples/vsgqtmdi/main.cpp). Timer-driven with on-demand mode: `continuousUpdate=false` + `request()` (.../include/vsgQt/Viewer.h), a match for the editor's `flagAsModified`.
- Fetch: `FetchContent_Declare` by tag + SHA512, `SYSTEM`, `add_dependencies(vsgQt vsg)`, as vsgXchange (extern/CMakeLists.txt:462-469); `VSGQT_BUILD_EXAMPLES=OFF`, `QT_PACKAGE_NAME=Qt6`; add it to the DLL output loop (:475); guard with `BUILD_OPENCS`. Record in docs/dependencies.md. Its `find_package(vsg)` should resolve as vsgXchange's does; I did not trace how.
- Limits: Linux is XCB only, no Wayland path in `Window.cpp`, so `QT_QPA_PLATFORM=xcb`/XWayland. Container limits per Qt: opaque box on top, platform-dependent focus, many containers hurt performance, native parents under QMdiArea/scroll areas (https://doc.qt.io/qt-6/qwidget.html#createWindowContainer). The editor's toolbars overlaying the viewport must be checked; `SceneWidget` is a `QWidget` so today's `mouseMoveEvent`/`wheelEvent` move to the `QWindow`.
- Fallback, QVulkanWindow: it owns device, queue, swapchain and depth; the docs offer no way to pass in an existing VkDevice (https://doc.qt.io/qt-6/qvulkanwindow.html), which conflicts with VSG owning them and with one device across views. A better fallback is vsgQt's own method: a plain `QWindow` + `winId()` into `WindowTraits::nativeWindow` + `vsg::Window::create` (about 300 lines, no dependency).

## 2. Editor dependencies (apps/opencs: 60 files, 1,144 `osg*::` uses, in 79,520 lines; view/render 16,328 lines)
| Subsystem | OSG API now | VSG replacement | Gap |
|---|---|---|---|
| Scene widget, scenewidget.cpp:17-119 | osgQt, `osgViewer::View`, `GraphicsWindowEmbedded`, `StatsHandler`, gradient `osg::Camera` :236 | vsgQt, `Render::Engine` pieces, `vsgUtil::*` | `Engine` creates its own `vsg::Viewer` for one window (engine.cpp:27-31) with a blocking `loop` (:114-159); needs injected viewer/multi-window. Stats overlay has no equivalent |
| Cell, object loading, cell.cpp, object.cpp:132 | `SceneManager::getInstance`, `LightListCallback` object.cpp:192, `osgFX::Scribe` :194 | `vsgUtil::readNode` + `ResourceSystem::nodeOptions`, `MWAnim::Object` | **Selection outline:** Scribe has no equivalent in pipeline/view (grep finds none) |
| Actors, actor.cpp | `SceneUtil::Skeleton`, `attach`, visitors | `MWAnim::Actor`, `MWAnim::attach`; `MWRender::Npc` is game-bound (npc.cpp:31-40 includes mwmechanics, mwworld) | Body-part assembly from `ActorAdapter` needs a thin new caller |
| Terrain, cell.cpp:148, terrainstorage.hpp:26 | `Terrain::TerrainGrid`, `ESMTerrain::Storage` | `Terrain::Paging::updateViewForCell` (paging.hpp), `esm3terrain` | **Altered heights are commented out** (esm3terrain/storage.cpp:135,189,222; hpp:85,87); per-cell rebuild/cache clear per edit unproved; layer-map preview unchecked |
| Terrain tools, brushdraw.cpp(70 uses), terrainselection.cpp(22) | dynamic `osg::Geometry`, render bin 11 | `MWRender::DebugGeometry` pattern (debuggeometry.hpp:42-58) | That class lives in apps/openmw; lift to components |
| Instance, object markers, instancemode.cpp(126), instanceselectionmode.cpp(80), objectmarker.cpp | `Geometry`, `ComputeBoundsVisitor`, `ClipPlane` :125, cull callback `ToCamera` :42, render bin 1000 :103 | `vsgUtil::bounds`, `viewrelative`, `Pipeline::Builder` | No clip-plane pipeline mode; **selectionmarker.osgt** (files/opencs, read at objectmarker.cpp:92-99) is an OSG file: convert to generated geometry or glTF. Same for files/data/meshes/marker_error.osgt |
| Cell markers, arrows, borders | `osgText::Text`, `AutoTransform` cellmarker.cpp:13,80; Geometry | Geometry as above | **No text in the VSG build** (no `vsg::Text` use, freetype off in extern). Proposal: QPainter to image to billboard |
| Water, cellwater.cpp:175-183 | `SceneUtil::createWaterGeometry` | `MWRender::SimpleWater` (simplewater.hpp, components-only includes) | Lift out of apps/openmw |
| Pathgrid, pathgrid.cpp:229-243,560,589 | `SceneUtil::createPathgridGeometry` | None: game's own `mPathgrid` is commented out (rendermanager.hpp:388) | Port pathgridutil geometry (data part is simple) |
| Lighting, lighting*.cpp | `osg::LightSource`, `Switch` day/night visitor, `SceneUtil::LightManager` | `View::Scene`, `collectlights`, `lightgrid`, `MWAnim::light` | Sun as view data; `Switch` night/day via game's existing `setNightDayMode` path (rendermanager.cpp:949) to copy |
| Picking, worldspacewidget.cpp:392-452,648 | `LineSegmentIntersector`, tag on `getUserData`, node masks | `vsg::LineSegmentIntersector`, `vsgUtil::CameraDistanceIntersector`, `intersect.hpp` pattern | Tags: `vsg::Object::setValue`/own node type. Game's pick returns `Ptr`, so editor writes its own. Masks: VSG `Switch`/`view->mask` do similar, and `tagbase.hpp` moves off `osg::Referenced`. Needs CPU-side vertex data kept after upload; verify on terrain |
| Camera, cameracontroller.cpp(54) | Matrixd, `getViewMatrixAsLookAt`, bounds | `vsg::LookAt`, `Perspective`, `Misc` vsgmath | Mechanical, but interactive to verify; Y-flip/depth conventions are the trap |

**RenderManager: not usable from the editor.** It reaches `MWBase::Environment` (rendermanager.cpp:663,858,949), `MWWorld::Ptr/CellStore/ESMStore` in its API (rendermanager.hpp:121,159-176), builds sky, shadows, weather, post-processing and owns the engine's single render pass (:111-125). Also `MWRender::Scene` is Ptr-keyed.
Lift or copy: main render-pass setup, `createCompileContext`, view descriptors (`View::*`, already in components), `DebugGeometry`, `SimpleWater`, intersect helpers; make an `Editor scene host` of its own (about 800 lines, the real new design: one per `SceneWidget`, sharing device, `Pipeline::Builder`, compile context and resource caches). Also `model/world/data.cpp:152-169` uses the old `ResourceSystem`/`getShaderManager` API; new ctor is resourcesystem.hpp:46.

## 3. Everything else OSG
- **navmeshtool** (6 files, 1,280 lines): 3 files use OSG; `osg::Vec2i` to `vsg::ivec2` = `TilePosition` (tileposition.hpp:9), ref_ptr, stale managers at main.cpp:235-239 (`PhysicsShapeManager` ctor is now `(vfs, nif, delay)`, physicsshapemanager.hpp:29). `sceneutil/workqueue.*` has no OSG, yet is not in components/CMakeLists.txt:229, so add it. CMake off at CMakeLists.txt:715.
- **essimporter** (56 files): converter.cpp:23-28,107-175, importer.cpp:45-73, converter.hpp:6-7,337 use `osg::Image`, osgDB read/write (TGA in, PNG, JPG out), `osg::Quat` :506. `vsgUtil::writeImageToMemory` exists (imageio.hpp:9). JPG writer through vsgXchange to confirm.
- **esmtool**: only `osg::RadiansToDegrees`, 2 files (esmtool.cpp:256-287, record.cpp:185,1319). **benchmarks**: 5 lines in navmeshtilescache.cpp:28-37,135.
- **Video:** `extern/osg-ffmpeg-videoplayer` (2,012 lines) uses no OSG API: only its name, 2 comments, and `${OSG_LIBRARIES}` in CMake; it yields raw pixels to `MWGui::VideoWidget` (videowidget.cpp:58-65). The logo video was run on Windows (research 0003:173); the menu video is present (task 0012:343). Only a rename remains, touching 4 mwsound + 1 mwgui includes, apps/openmw/CMakeLists.txt:179, root :584, .gitlab-ci.yml:462. Check intro/Bink end-to-end once.
- **extern/osgQt** (360 lines): delete after the viewport; root :587, apps/opencs/CMakeLists.txt:233, `Qt::OpenGL*` at :238.
- **CMake:** `#[[...]]` block root :425-443, multiview check cmake/CheckOsgMultiview.cmake (26 lines), `extern/CMakeLists.txt:76-150`, `components/CMakeLists.txt:772`, cmake/WholeArchive.cmake:2 (comment). Old `OPENMW_USE_SYSTEM_VSG` branch :131 stays a question. `OPENEDEN_BUILD_VSG` could go too. No FindOSG module left.
- **CI:** CI/install_debian_deps.sh:45,48-58,75,82; before_script.linux.sh:32; before_script.msvc.sh:179,314-321,660,800-804; windows.yml:122; org.openmw.OpenMW.devel.yaml:79-95; docker/Dockerfile.ubuntu; run_integration_tests.sh:14 + scripts/osg_stats.py. `libqt6opengl6` may go. `CI/check_no_osg.sh` only scans apps/openmw, components, components_tests, for math types: must widen to all `osg` headers and namespace, all dirs, drop the skip list. It runs in GitLab only (.gitlab-ci.yml:208).
- **Tests:** `fx/*` already restored (components_tests/CMakeLists.txt:138-149). `testbulletnifloader` already replaced by `testjoltnifloader` (:33): record it. `resource/testresourcesystem.cpp` on disk, excluded (:152): port. `shader/*` (622 lines): `Shader::` manager unbuilt, parsers gone: delete with reason, after checking vsgopenmw's `readshader` needs no such test. `nifosg/testnifloader` (303 lines, from parent of d35e66dc64): replace against `vsgadapters/nif`. `sceneutil/osgacontroller` (131): OSG `.osgb` animation format is no longer read: delete with reason, `testdaeanimation` covers animation. opencs_tests has no OSG but links `openmw-cs-lib` (opencs_tests/CMakeLists.txt:15).
- **Task list (99 files, 28,674 lines):** none compiled. Delete once editor and navmeshtool stop including them. Port first: `pathgridutil`, parts of `waterutil`, `lightcommon`; `esmterrain/storage` alteration logic (:263,313,353,664) into `esm3terrain`. Note: `components/terrain` keeps its VSG files (CMakeLists:107), `nifosg`, `shader` go entirely; `sceneutil` keeps only `detourdebugdraw`, `navmesh`, `workqueue`, and more non-listed unbuilt ones (60 tracked files; audit at deletion). `serialization/osgyaml.hpp` goes. `model/world/resourcesmanager.cpp:27` lists osg mesh types. Docs, `vsgopenmw-*.md`, files/shaders mentions, compat_morrowind comments: sweep.

## 4. Stage order (each builds and tests alone)
| # | Stage | Check | Person needed? |
|---|---|---|---|
| A1 | esmtool, benchmarks math | build; esmtool dump diff against an OSG-era binary | No |
| A2 | navmeshtool | build; DB compared with OSG build (acceptance) | No |
| A3 | essimporter images | unit test on generated image; convert a save | Save needs own .ess |
| A4 | Rename video player | build; `--auto-screenshot` during logo | Hearing audio, yes, briefly |
| A5 | Tests: restore/replace/delete with reasons | components-tests | No |
| B1 | vsgQt spike: fetch, build on Win + Linux, standalone test exe | build; run example | Glance at the window |
| B2 | Lift DebugGeometry, SimpleWater, intersect helpers to components | game image compare (render_compare) unchanged | No |
| B3 | `Render::Engine` accepts injected viewer and several windows; game unchanged | game frames, integration tests | No |
| B4 | Altered heights in esm3terrain, off in the game | unit test on `getVertexData` | No |
| C1 | Model layer on VSG (`data.cpp`, actoradapter, cellcoordinates, columnimp, main) plus a `csmodel` split so opencs_tests builds before views | `openmw-cs-tests` | No |
| C2 | Editor scene host + `--open-cell X --auto-screenshot N --auto-quit` | captured frame vs game frame of same cell | No |
| C3 | Widgets: RenderWidget/SceneWidget on vsgQt, gradient, lighting modes, resize | capture per mode; on-demand frames | Resize, focus, docking: **yes** |
| C4 | Cell: objects, actors, terrain, water, border, arrow, marker text | frame compare, exterior + interior | No for frames |
| C5 | Camera controllers | scripted capture from fixed poses | Fly/orbit feel: **yes** |
| C6 | Picking + tags, unit test via scripted ray | ray test on known cell | Clicking: **yes** |
| C7 | Instance mode, selection outline, markers | capture with scripted selection | Drag, rotate, box select: **yes** |
| C8 | Terrain shape + texture modes, brush | scripted alter + capture | Painting, undo: **yes** |
| C9 | Pathgrid mode, preview widget | capture | Editing: **yes** |
| D1 | Delete OSG: osgQt, listed sources, CMake, widen check_no_osg | full build, `CI/check_no_osg.sh` | No |
| D2 | CI + docs: packages, MSVC, README, guide, dependencies | CI green; hosting question open blocks M7 | toddhow: hosting |

**Capture path:** simple. `Render::Screenshot(window)` exists (screenshot.hpp:32) and the game's hook is short (vsgengine.cpp:435-451); copy it. New: `--open-cell`, about 100 lines in apps/opencs/editor.cpp, as there is no way today to open a scene from the command line. Also `Engine`'s headless ctor (engine.hpp:45) can render the scene host without Qt, for C2 in CI, if a GPU exists. All editor input (C3, C5-C9) needs toddhow's written check list; an agent can only test ray/edit logic beneath the Qt events.

## 5. Sizes (rough, estimates; changed lines) and risks
| Stage | Files | Lines |
|---|---|---|
| A1-A5 | 25 | 800, plus 2,000 deleted tests |
| B1-B4 | 20 | 1,000 |
| C1 | 12 | 300 |
| C2 | 6 | 1,000 new |
| C3 | 8 | 900 |
| C4 | 20 | 2,000 |
| C5-C6 | 6 | 800 |
| C7-C9 | 16 | 3,000 |
| D1-D2 | 130 + CI | 30,000 deleted, 200 changed |

Risks:
- Largest: `Engine` and vsgopenmw's descriptor/pipeline setup assume one window and one view graph; several `SceneWidget`s on one device may break render pass compatibility, compile contexts or swapchain sizes (see the pitfall on compile views before `Engine::setup`).
- Dynamic add/remove of nodes per edit: VSG requires compile before insert, and the game does this on threads; editor edits need a synchronous path with no stalls and safe deletion (`vsgUtil::DeletionQueue`).
- Picking needs vertex data on CPU; vsgopenmw may free it, and instanced/terrain batches may not intersect as OSG did.
- Terrain alteration at interactive rate: `Paging` batches rebuild per cell, may be slow.
- Selection outline in Vulkan needs a new pipeline variant; toddhow may accept a simpler highlight (constraint requires agreement).
- createWindowContainer focus and shortcut behaviour on Windows, QMdi-free dock layouts; XCB-only on Linux.
- Mid-port nothing in view/render compiles: C2-C9 need a staged CMake list, or the editor stays off until C4, keeping each piece testable through captures.
- Memory rule: capture and view a frame before committing anything render-related, including B2, B3. Guide: Track V stages use the full loop.
- Not read in depth: `apps/opencs/view/widget`, preview widget internals, `MWAnim::Actor` fit for ActorAdapter, game shaders' fit for editor-only pipelines.
