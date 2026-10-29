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
