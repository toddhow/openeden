# Research Note: vsgopenmw Baseline State and Integration Blueprint

**Document ID:** 0002  
**Date:** October 2026  
**Status:** Approved Reference  
**Parent Decision:** [0002-vulkan-renderer-on-vulkanscenegraph.md](../decisions/0002-vulkan-renderer-on-vulkanscenegraph.md)  
**Task Track:** Track V (Stage V0 / Task 0008)

---

## 1. Upstream Source & Reference Branch

The reference Vulkan renderer implementation for OpenMW is hosted by the `vsgopenmw-dev` project:
- **Repository:** `https://github.com/vsgopenmw-dev/vsgopenmw.git`
- **Tracked Branches:** `master` and `vsg_v116`
- **Pinned Reference Commit:** `2830e7e2b4f18ef08ee24eff45a13568ec917061`
  - Merged upstream feature work including `VSG_MERGE_PLAN.md`, `--auto-screenshot`, and `--auto-quit` support.
- **OpenEden Reference Branch:** `vsg-import`
  - Base: `2830e7e2b4`
  - Compatibility commit: `be8cd7a064` (`vsgopenmw: update to VSG 1.1.16 and vsgXchange 1.1.14 compatibility`)

> [!IMPORTANT]
> The `vsg-import` branch is a read-only reference branch within OpenEden. It is never rebased. If upstream updates are pulled in the future, they will be brought in via fresh merge commits or new clean import branches.

---

## 2. VSG 1.1.16 & vsgXchange 1.1.14 Compatibility Layer

Upstream `vsgopenmw` was originally authored against VSG 1.0.x and early 1.1.x previews. The following minimal compatibility delta was applied on `vsg-import` to compile cleanly against pinned VSG 1.1.16 and vsgXchange 1.1.14:

1. **Light Node Hierarchy Reorganization:**
   - VSG 1.1.x moved light classes out of `vsg/nodes/` into `vsg/lighting/`.
   - Updated `#include <vsg/nodes/Light.h>` to `#include <vsg/lighting/Light.h>` and `#include <vsg/lighting/PointLight.h>` in:
     - `components/mwanimation/light.cpp`
     - `components/mwanimation/lighteffect.cpp`
     - `apps/openmw/mwrender/projectiles.cpp`
     - `components/view/collectlights.cpp`
2. **Standard Library Headers:**
   - Included `<list>` in `components/mwanimation/effects.hpp` and `apps/openmw/mwrender/projectiles.hpp`.
3. **State Commands Type Migration:**
   - `vsg::StateGroup::StateCommands` typedef was deprecated and relocated to `vsg::StateCommands` in:
     - `components/terrain/builder.hpp`
     - `components/terrain/builder.cpp`
4. **MyGUI Adapter Resource Hints:**
   - In VSG 1.1.x, `minimum_maxSets` was redesigned in `vsg::Context`. Commented out obsolete manual minimums in `components/vsgadapters/mygui/render.cpp` and added `#include <vsg/state/DescriptorSet.h>`.
5. **ArrayState Virtual Signature:**
   - In `components/vsgutil/arraystate.hpp`, removed `override` specifier from `clone(vsg::ref_ptr<vsg::ArrayState>)` to match the VSG 1.1.x base class signature.
6. **Explicit Math Vector Constructors:**
   - Standardized brace initialization of `vsg::vec4(...)` and `vsg::ubvec4(...)` across `apps/openmw/mwrender/map.cpp`, `preview.cpp`, `weather.cpp`, and `storage.cpp`.
7. **Pinned Dependencies in `extern/CMakeLists.txt`:**
   - VSG pinned to `v1.1.16` with SHA-512 hash.
   - vsgXchange pinned to `v1.1.14` with SHA-512 hash.

---

## 3. Architecture and Functional State of the Baseline

### 3.1 What Works in Baseline `vsgopenmw`
- **Core Vulkan Scenegraph Traversal:** Record traversal, command graph dispatch, and frame submission using Vulkan 1.2/1.3 constructs via VSG.
- **Terrain Builder & Heightfield Mesh Generation:** ESM3 terrain storage, normal map generation, vertex and index buffer staging.
- **NIF Mesh Adapter (`components/vsgadapters/nif`):** Basic geometry extraction, skeleton transformation nodes, and material binding.
- **MyGUI UI Layer:** Complete Vulkan rendering backend for MyGUI (`components/vsgadapters/mygui`), blitting font textures and UI geometry onto the presentation render target.
- **Lighting Pipeline (`components/view`):** Directional ambient/sunlight and point light collection into uniform descriptor sets.

### 3.2 Known Gaps to Address in Track V
- **CreativeMotion Animation Runtime Integration (Stage V3):** The baseline uses legacy OpenMW animation paths. Track V Stage V3 will connect the new high-performance CreativeMotion runtime and bone matrices to the VSG skeleton node visitor.
- **Advanced Shaders & Materials (Stage V4):** Dynamic water surface with planar reflections/refractions, distance fog, soft particles, and physically based rendering material parameters.
- **Shadows & Post-Processing (Stage V5):** Cascaded shadow maps (CSM) and compute-based post-processing chain (bloom, tone mapping, ambient occlusion).
- **Parity & Performance Verification (Stage V6):** Systematic frame rate comparison and visual fidelity testing using `tools/render_compare.sh` and the 10 reference scenes.

---

## 4. Build and Verification Instructions

### Windows (MSVC 2022 / Ninja)
```powershell
# In OpenEden build directory:
cmake -G Ninja -S . -B build -DOPENEDEN_BUILD_VSG=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --target components-tests openmw-tests openmw
```
The build automatically:
1. Fetches and builds `glslang` 14.3.0 statically.
2. Generates the Windows `vulkan-1.lib` import library against the system Vulkan runtime if LunarG SDK is not installed.
3. Builds `vsg` 1.1.16 with runtime shader compilation enabled.
4. Copies `vsg-17rd.dll` to the runtime directory.

### Linux (Debian / Ubuntu)
```bash
./CI/install_debian_deps.sh
cmake -G Ninja -S . -B build -DOPENEDEN_BUILD_VSG=ON
cmake --build build
```

---

## 5. Verification Tools

- `tools/render_scenes.txt`: Registry of 10 standard reference scenes (5 exterior, 5 interior across day/night and weather variations) for screenshot automation.
- `tools/render_compare.sh`: Automated capture and image difference runner using `--auto-screenshot` and `--auto-quit` with MAE (Mean Absolute Error) threshold verification.
- `apps/components_tests/vsg/testshadercompile.cpp`: Automated unit test verifying runtime SPIR-V shader compilation via `vsg::ShaderStage` and `glslang`.
