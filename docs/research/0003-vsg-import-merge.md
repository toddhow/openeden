# Research Note: Track V Stage V1 - vsgopenmw Renderer Merge and Conflict Resolution

**Document ID:** 0003  
**Date:** October 2026  
**Status:** Approved Reference  
**Parent Task:** [0009-vulkan-v1-renderer-import.md](../tasks/0009-vulkan-v1-renderer-import.md)  
**Parent Decision:** [0002-vulkan-renderer-on-vulkanscenegraph.md](../decisions/0002-vulkan-renderer-on-vulkanscenegraph.md)  
**Task Track:** Track V (Stage V1 / Task 0009)

---

## 1. Overview and Merge Strategy

Stage V1 integrates upstream `vsgopenmw` (commit `2830e7e2b4`, branch `vsg-import` commit `be8cd7a064`) into OpenEden's `vulkan` branch. The merge establishes VulkanSceneGraph (VSG 1.1.16 / vsgXchange 1.1.14) as OpenEden's Vulkan rendering backend while maintaining OpenEden's core systems:
- Jolt physics engine
- Detour navigation mesh subsystem
- Unified settings system
- ESM4/Bethesda format support and multi-content management
- Custom Lua bindings and gameplay extensions
- Pending feature toggles (`OPENEDEN_VSG_PENDING_FEATURES=OFF`)

The merge was conducted directly via `git merge --no-commit vsg-import` to preserve full upstream commit history.

---

## 2. Merge Policy Rules & Application

All conflicts were resolved according to the 8 Merge Policy Rules specified in Task 0009:

| Rule | Area | Resolution Directive | Applied In |
|---|---|---|---|
| **Rule 1** | Physics | Jolt wins everywhere. Reject Bullet collisions and shape readers. | `mwmechanics/`, `mwworld/`, `components/resource/`, `components/CMakeLists.txt` |
| **Rule 2** | Rendering | `vsgopenmw` wins. Replace OSG scene nodes, rendering passes, and materials with VSG equivalents. | `mwrender/`, `components/pipeline/`, `components/render/`, `components/view/` |
| **Rule 3** | Game Logic & Lua | OpenEden wins. Retain OpenEden's Lua scripting extensions, Detour navigation bindings, and gameplay fixes. | `mwlua/`, `mwmechanics/charactercontroller.cpp`, `mwinput/` |
| **Rule 4** | World & ESM4 | OpenEden wins on data structures (`ReferenceVariant`, ESM4 cell references). Update rendering and preloading calls to VSG. | `mwworld/esmstore.*`, `mwworld/cellref.*`, `mwworld/scene.*`, `mwworld/worldimp.*` |
| **Rule 5** | Settings | OpenEden's unified settings system wins. | `files/settings-default.cfg`, `components/settings/` |
| **Rule 6** | No Dead OSG Code | Drop unused OSG includes, visitors, and nodes. Rely on `extern/osg` for zero-DLL header compatibility. | `CMakeLists.txt`, `components/CMakeLists.txt`, `apps/openmw/CMakeLists.txt` |
| **Rule 7** | Pending Features | Feature-gate advanced subsystems (IK, CreativeMotion, Jolt debug draw, blend controllers) behind `OPENEDEN_VSG_PENDING_FEATURES=OFF`. | `apps/openmw/vsgpendingfeatures.hpp/.cpp`, `apps/openmw/main.cpp` |
| **Rule 8** | Documentation | Document all conflict categories and resolutions in research notes. | `docs/research/0003-vsg-import-merge.md` |

---

## 3. Detailed Conflict Resolution by Subsystem

### 3.1 Build System (`CMakeLists.txt`, `components/CMakeLists.txt`, `apps/openmw/CMakeLists.txt`)
- **Root `CMakeLists.txt`:**
  - Preserved OpenEden's modern Boost configuration (`find_package(Boost 1.70.0 CONFIG REQUIRED COMPONENTS iostreams program_options)`), Apple deployment options, and `defaults.bin` packaging.
  - Linked VSG (`vsg::vsg`, `vsgXchange::vsgXchange`) and removed OSG library dependencies.
- **`components/CMakeLists.txt`:**
  - Added VSG components: `terrain`, `vsgadapters/nif`, `vsgadapters`, `vsgadapters/sdl`, `vsgadapters/mygui`, `pipeline`, `render`, `view`, `animation`, `mwanimation`, `vsgutil`.
  - Replaced `nifosg` and `nifbullet` with `nifjolt` (`joltnifloader`).
  - Commented out OSG postprocessor `fx` and `std140` behind block comments (`#[[ ... ]]`).
  - Updated `sdlutil` to use VSG windowing (`init`, `inputwrapper`, `videowrapper`, `events`, `cursormanager`, `sdlmappings`).
  - Linked `vsg::vsg`, `vsgXchange::vsgXchange`, `Jolt::Jolt`, Boost, SDL2, and removed all `${OSG...}` libraries.
- **`apps/openmw/CMakeLists.txt`:**
  - Swapped `engine.cpp` with `vsgengine.cpp`, added `vsgpendingfeatures.cpp` and `arguments.hpp`.
  - Configured `mwrender` sources to match VSG modules (`rendermanager`, `terrainstorage`, `landmanager`, `animcontext`, `bodyparts`, `wieldingcreature`, `npc`, `player`, `scene`, `camera`, `itemlight`, `effect`, `effects`, `projectiles`, `spellcastglow`, `transparency`, `env`, `weather`, `map`, `worldmap`, `preview`, `shadow`, `water`, `simplewater`, `screenshotinterface`).
  - Maintained `openmw-lib` static library target required by `openmw-tests`.
  - Removed `<osgViewer/Viewer>` from MSVC precompiled header list.

### 3.2 Game Mechanics (`apps/openmw/mwmechanics/`)
- **`activespells.cpp`:** Retained OpenEden spell reflection tracking; replaced OSG reflect effect creation with `MWRender::RenderManager::addEffect`.
- **`aicombat.cpp`:** Replaced OSG `PositionAttitudeTransform` with `<components/mwanimation/position.hpp>` and `extern/osg` math types.
- **`actors.cpp`:** Maintained parallel actor update loop (`update(charactersToUpdate, duration)`) and used VSG effects node (`fx->mModel`).
- **`obstacle.cpp`:** Updated actor forward direction calculation from `MWAnim::forward(actor.getRefData().getPosition())` instead of removed `RefData::getBaseNode()`.
- **`mechanicsmanagerimp.cpp`:** Removed OSG viewer stats reporting calls.

### 3.3 World Systems (`apps/openmw/mwworld/`)
- **`refdata.hpp` & `refdata.cpp`:** Dropped `mBaseNode` (`osg::PositionAttitudeTransform*`), `getBaseNode()`, and `setBaseNode()`. Rendering handles are now owned by `mwrender` and `mwmechanics/charactercontroller`.
- **`cellref.hpp`, `cellref.cpp`, `livecellref.cpp`, `manualref.cpp`, `cellstore.hpp`:** Retained OpenEden's `ReferenceVariant` and ESM4 support.
- **`esmstore.hpp` & `esmstore.cpp`:** Updated loading API signature to accept `MWState::Loading&` for VSG loading progress integration while preserving ESM4 loading routines.
- **`weather.cpp`:** Preserved particle model settings while adopting VSG spherical theta calculation for sun direction and `toVsg(...)` color vectors.
- **`projectilemanager.hpp` & `projectilemanager.cpp`:** Swapped OSG scene nodes and light lists for `mRender->getProjectiles()->add(...)` and `ProjectileHandle` while keeping Jolt raycasting and collision queries.
- **`scene.hpp` & `scene.cpp`:** Completely wired `MWRender::RenderManager`, `Terrain::Preload`, and `preloadcell.cpp`. Preserved Detour navigation mesh update guards and Jolt physics queries.
- **`worldimp.hpp` & `worldimp.cpp`:** Integrated VSG render manager constructor and loading state, wired Jolt raycasting and navigation.

### 3.4 GUI and Lua (`apps/openmw/mwgui/`, `apps/openmw/mwlua/`)
- **`mwgui/`:** Updated window manager and HUD to interact with VSG `RenderManager` and `SDLUtil::CursorManager`. Replaced OSG video widget with `VideoWidget` / `videowindow` backed by `vsgadapters/mygui`.
- **`mwlua/`:** Preserved all OpenEden Lua bindings (camera, cell, object, UI, worker, and Detour navigation) while adapting render and camera calls to VSG.

---

## 4. Verification and Validation Plan

1. **Conflict Verification:** Zero conflict markers in entire repository (`git diff --check` and `Select-String "<<<<<<<"`).
2. **Zero OSG Linkage:** Run `powershell tools/verify_no_osg_deps.ps1 build-jolt/openmw.exe` to guarantee zero OSG dynamic libraries are linked.
3. **Unit Tests:** Compile and run `components-tests.exe` and `openmw-tests.exe`.
4. **Runtime Smoke Test:** Execute `openmw.exe --auto-quit 5` and `--auto-screenshot` to confirm main menu launch and clean exit.

---

## 5. Completion work after the merge commit (2026-10-29)

The merge commit `e3957be0b2` did not build: several files had been resolved by dropping OpenMW 0.51 logic, ESM4
was commented out, and physics and resources still depended on OSG classes that are no longer linked. This section
records what was done to finish Stage V1 on top of it. All changes are in the working tree of `vulkan`; shims are
marked `// openeden-vsg:`.

### 5.1 Re-merged files

These were re-merged file by file with `git merge-file` (OpenEden 0.51 as ours, vsgopenmw as theirs, after renaming
`MWRender` animation types to `MWAnim`), resolving each conflict under the rules in section 2:
`mwmechanics/character.*`, `mwworld/scene.*`, `mwworld/projectilemanager.*`, `mwgui/windowmanagerimp.*`,
`mwgui/race.*`, and the state files `mwstate/statemanagerimp.*`, `askloadrecent.hpp`, `loadgame.hpp`,
`savegame.hpp`, `newgame.hpp`. ESM4 support was restored in 36 places (`cellstore`, `esmstore`, `esmloader`,
`worldimp`, `worldmodel`, `refdata`, `store`, the `esm4base`/`esm4npc`/`light4` classes and `components/esmloader`).

### 5.2 Physics and resources without OSG

`osg::Referenced` and `osg::Object` are not linked, so the physics shape and resource caches moved to `vsg::Object`
and `vsg::ref_ptr`: `components/resource/` (`objectcache`, `multiobjectcache`, `niffilemanager`, `physicsshape`,
`physicsshapemanager`, `foreachphysicsobject`, `cachestats`), `components/nifjolt/joltnifloader`, the detour
navigator's shape holders and `apps/openmw/mwphysics/` (`object`, `heightfield`, `mtphysics`, `physicssystem`).
`PhysicsSystem` owns its `NifFileManager`. The bundled `extern/osg/osg/Config` no longer defines
`OSG_USE_FLOAT_BOUNDINGBOX` or `OSG_USE_FLOAT_BOUNDINGSPHERE`: vsgopenmw's bounding-volume headers do not include it,
so the type depended on include order (an ODR violation that MSVC exposed as link errors).

### 5.3 Windows and MSVC build fixes

- 39 forward declarations whose `class`/`struct` key differed from the definition (MSVC mangles the key).
- `Array2D<T>::accept` needs `vsg/core/Visitor.h` and `ConstVisitor.h` in `components/esm3terrain/storage.cpp`.
- `files/settings-default.cfg` gains vsgopenmw's settings that 0.51 lacks (`[Shadows]` PCSS and filtering,
  `[Models] write nif debug log`).
- `main.cpp` no longer reads `--script-blacklist`, which 0.51 removed.
- `components/files/conversion.cpp` restored to 0.51: vsgopenmw's `path.string()` breaks non-ASCII paths on Windows.

### 5.4 VSG 1.1.16 behaviour changes in vsgopenmw code

vsgopenmw was written for an older VSG. These differences made the game crash, hang or draw nothing:

| Symptom | Cause | Fix |
|---|---|---|
| Crash in `ViewDependentState::init` | VSG 1.1 calls `init`, `update`, `bindDescriptorSets` and `traverse(RecordTraversal&)` on view-dependent state | `View::CollectLights` overrides them; its `traverse` calls `pack()`, which replaces the removed `pack()` hook |
| Terrain and statics invisible, player falls | A `vsg::Context` uploads data only through a `TransferTask`; views were added to the compile manager before `Engine::setup` created one | `RenderManager` adds its compile views after `mEngine.setup()` |
| Female and first-person skeletons lose `Bip01`; camera inside the body | `vsg::read` with `sharedObjects` clones "dynamic" objects by default, turning `Anim::Transform` roots into groups | `findDynamicObjects` and `propagateDynamicObjects` cleared in vsgopenmw's options (`resourcesystem.cpp`, `animcontext.cpp`) |
| Exteriors unlit | `vec3 * mat4` now treats the vector as a point and divides by the projective term | `View::Scene::setLightPosition` transforms the sun as a `vec4` direction |
| No image once point lights exist | Per-vertex lighting indexes the light grid with off-screen coordinates | `selectTile` clamps the grid coordinate (`files/shaders/lib/light/select.glsl`) |
| Terrain black | Layers were sized from the first image (the 64x64 BC1 `_land_default.dds`), rejecting Morrowind's 256x256 RGBA layers; compressed layers lost their mipmaps | `vsgUtil::convertToArrayTexture` uses the most common layer format, resamples or decodes (BC1-3) the others, and keeps compressed mip chains through a `MipmapLayout` |
| Cursor and window icon errors | vsgXchange reads PNG as `_SRGB` | SDL surface and MyGUI texture code accept `_SRGB` |
| Terrain normals not sampleable on Windows drivers | `R32G32B32_SFLOAT` images | Normals stored as `R32G32B32A32_SFLOAT` |

### 5.5 Engine order fixes from combining 0.51 and vsgopenmw

- The Lua manager is created by vsgopenmw's `Game` state after the World. `World` no longer calls
  `contentFilesLoaded` (`LuaManager::init` does), permanent storage loads before `init`, the player is set up before
  Lua initialises (as in 0.51), post-load Lua contexts receive the render manager, and the Engine owns the Lua manager
  so it outlives the window manager (0.51's destruction order).
- `~Game` destroys the World while Lua still exists, detaches the L10n GMST lookup from the World's store and clears
  the Lua UI elements.
- `GameState::updateSound` is false while a video plays, as 0.51's blocking video loop never updated sounds;
  otherwise the end of a video deadlocked the movie's audio decoder and `SoundManager::update`.
- The Lua `_getUiModeStack` omits GUI modes that have no Lua name (`GM_Video`, `GM_Settings`), so Lua sees the same
  mode stack as in 0.51.
- `mapwindow.cpp` used an empty worldspace for exterior lookups; it uses `ESM::Cell::sDefaultWorldspaceId`.

### 5.6 Runtime dependencies on Windows (not yet in the build)

- OpenMW's prebuilt vcpkg SDL2 is built without Vulkan. A Vulkan-enabled SDL2 2.32.10 was built with vcpkg's
  `sdl2[vulkan]` into `deps/sdl2-vulkan` and copied next to `openmw.exe`. The CI and dependency scripts need this.
- The build does not copy the rebuilt `vsg-17rd.dll` and `vsgXchangerd.dll` next to `openmw.exe`.

### 5.7 Tests

`components-tests` (1434/1434) and `openmw-tests` (505/505) pass. Test changes, approved by toddhow on 2026-10-29:
the detour navigator tests and `resource/testobjectcache.cpp` use `vsg::ref_ptr` and `vsg::Object`;
`mwworld/teststore.cpp` passes a dummy `MWState::Loading` to `ESMStore::load`. Excluded as tests of OSG components the
VSG build does not compile: `shader/*`, `fx/*`, `nifosg/testnifloader.cpp`, `sceneutil/osgacontroller.cpp` and
`resource/testresourcesystem.cpp` (OSG `SceneManager`); they are listed in `apps/components_tests/CMakeLists.txt` for
tasks 0012 and 0014. The Lua integration tests were not run (Linux only, task 0010).

### 5.8 In-game verification

Run on Windows with Morrowind, Tribunal and Bloodmoon: logo video, main menu, new game via `--start`, an interior
(Arrille's Tradehouse) with candle lighting and NPCs, exteriors (Balmora, Skaal Village) with sunlight and textured
terrain, the first-person camera, and a clean quit, captured with `--auto-screenshot` and `--auto-quit`.
`--start "Seyda Neen, Census and Excise Office"` places the player behind a wall: the first door into that cell is
the jail-release `PrisonMarker`, whose destination in `Morrowind.esm` lies outside the room, and 0.51 picks the same
marker.

### 5.9 Merge audit and deferrals

Of the 12,863 lines OpenEden added since the merge base in the 235 files both sides changed, 1,018 are absent from
the working tree. The largest gaps and where they go:

| File | Missing / added | Disposition |
|---|---|---|
| `mwgui/mapwindow.*` | 240 / 324 | 0.51 map features on vsgopenmw's map renderer: task 0012 |
| `mwstate/statemanagerimp.cpp` | 125 / 148 | Moved into vsgopenmw's game-state files (`loadgame.hpp`, `savegame.hpp`, `newgame.hpp`, `askloadrecent.hpp`) |
| `mwworld/scene.cpp` | 54 / 186 | Paged-object preloading (`Scene::preload`) and non-NIF collision shapes: task 0010 |
| `mwmechanics/character.cpp` | 53 / 546 | Renames only (`MWRender` to `MWAnim` animation API) |
| `mwlua/postprocessingbindings.cpp`, `mwrender/water.cpp` | 77 / 99 | Post-processing (`.omwfx`) and water: task 0012 |
| `mwgui/mainmenu.*`, `mwgui/loadingscreen.cpp` | 74 / 113 | Main menu background video and loading screen on vsgopenmw widgets: task 0012 |
| `components/fontloader/fontloader.cpp` | 22 / 242 | `--export-fonts` writes the `.xml` but not the `.png` (osgDB): task 0010 |

Other deferrals: shield sheathing, herbalism visuals, animation blending rules and the additive GUI layer
(task 0012), and the `OPENEDEN_VSG_PENDING_FEATURES` features (task 0011). Re-run the per-file audit (lines OpenEden
added between the merge base and `HEAD^1` that the working tree lacks) before merging `vulkan`.

### 5.10 Fresh review (2026-10-29)

A fresh reviewing subagent checked the working tree against task 0009 and the guide. Fixed after the review:

- `projectilemanager.cpp` again orients projectiles with 0.51's `lookAt` (the shortest-arc quaternion had brought
  back in-flight roll, gave NaN at zero velocity and left `lookAt` unused, which fails `-Werror` on Linux).
- `World` passes the content encoder to `PhysicsSystem`, so physics decodes NIF strings as 0.51 did.
- `convertToArrayTexture` resamples a layer only when its format matches the array's, decodes BC layers only into an
  RGBA8 array, keeps the RGB-only BC1 formats opaque, sets the allocator type explicitly and rejects an empty list.
- `selectTile` replaces NaN grid coordinates before clamping.
- `extern/osg/osg/ref_ptr` marks why it includes `osg/Config`.

Recorded for toddhow or later stages:

- With `OSG_USE_FLOAT_BOUNDINGBOX` and `_BOUNDINGSPHERE` undefined, `osg::BoundingBox` and `osg::BoundingSphere` are
  double precision engine-wide (float in 0.51). Their game-side uses are bounds queries (focus and tooltips,
  `PhysicsSystem::getBoundingBox`), not simulation.
- `test_compat_morrowind.cpp` and `test_creativemotion_runtime.cpp` build only with `OPENEDEN_VSG_PENDING_FEATURES`,
  as task 0009 specifies for the pending features; they return in task 0011.
- `MWPhysics::Object` caches raw pointers into the animation's scene graph for animated collision; they stay valid
  while the `MWAnim::Object` does, but should be re-resolved if an animation is rebuilt (task 0010, with a
  moving-platform check).
- No `MWLua::Worker` thread is created, so `[Lua] lua num threads` is ignored and Lua runs on the main thread
  (task 0010).
- `LoadGame::threadLoad` formats the save-version error message through L10n on the loader thread (task 0010).
- `PhysicsSystem::pruneCache` uses `steady_clock` for cache expiry only; simulation results do not depend on it.
- `apps/openmw/engine.cpp`/`.hpp` (0.51's OSG engine) remain in the tree but are not compiled; task 0014 removes them.
