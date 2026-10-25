# OpenEden Plugin Implementation Guide

This guide documents everything required to implement, build, test, and integrate a native C++20 plugin into OpenEden, based on the reference implementation of OpenEden's first feature plugin: **CreativeMotion** ([`plugins/creativemotion/`](../../plugins/creativemotion/)).

Use this guide when implementing upcoming feature and compatibility plugins:
- **AdvancedBodies** (`openeden.body` v1.0)
- **ComplexClothing** (`openeden.clothing` v1.0)
- **Reflectron** (`openeden.audio` v1.0)
- **Morrowind Compatibility Pack** (`openeden.compat.morrowind` v1.0)

---

## 1. Architecture Overview & Non-Negotiables

Every OpenEden plugin adheres to the architecture defined in [`openeden-plugin-system.md`](openeden-plugin-system.md):

1. **SDK-Only Dependency (`openeden::sdk`)**:
   - Plugins include headers from [`sdk/include/openeden/`](../../sdk/include/openeden/) and [`sdk/include/openeden/contracts/`](../../sdk/include/openeden/contracts/) only.
   - Plugins **never** include headers from `apps/` or `components/`.
2. **Off-by-Default Feature Plugins**:
   - Feature plugins (`"kind": "feature"`) must declare an `"enabled"` boolean setting defaulting to `false` in `plugin.json`.
   - When `"enabled"` is `false`, the plugin must perform **zero work** in frame/fixed-step callbacks and must not alter host state or physics worlds.
3. **One Shared Jolt Physics World**:
   - Plugins that perform physics simulation (CreativeMotion, ComplexClothing) **never** instantiate their own `JPH::PhysicsSystem` or link their own standalone Jolt backend when hosted in OpenEden.
   - Instead, they adapt `host.getPhysicsService()` (`openeden::sdk::PhysicsService`) to their internal physics interface (see [`HostPhysicsWorld`](../../plugins/creativemotion/host_physics_world.hpp)).
4. **Zero Hot-Path Heap Allocations**:
   - Callbacks registered on `Phase::FixedStepPrePhysics`, `Phase::FixedStepPostPhysics`, `Phase::PreSimulation`, `Phase::PostSimulation`, `Phase::Animation`, and `Phase::PreRender` must not allocate heap memory during normal execution. Pre-allocate vectors and scratch buffers during `start(Host&)` or asset load.
5. **Save-File Resilience**:
   - Removing or disabling a plugin must never prevent an existing save file from loading. The core's `SaveService` automatically skips unknown chunk IDs with a warning.

---

## 2. Step-by-Step Checklist for New Plugins

### Step 1: Define the Versioned Domain Contract

If the plugin provides a domain contract consumed by other plugins or optional systems, define it in `sdk/include/openeden/contracts/<domain>_contract.hpp`:

```cpp
#pragma once

#include <openeden/version.hpp>
#include <string_view>

namespace openeden::contracts
{
    class AnimationContract
    {
    public:
        static constexpr std::string_view kId = "openeden.animation";
        static constexpr openeden::sdk::Version kVersion{ 1, 1 };

        virtual ~AnimationContract() = default;

        [[nodiscard]] virtual bool isEnabled() const noexcept = 0;
        virtual void setEnabled(bool enabled) = 0;
        // Domain-specific pure virtual methods...
    };
} // namespace openeden::contracts
```

- Every contract class **must** define `static constexpr std::string_view kId` and `static constexpr openeden::sdk::Version kVersion`.
- Contract versions follow major.minor compatibility (`provider.major == required.major && provider.minor >= required.minor`).

### Step 2: Author `plugin.json`

Create `plugins/<plugin_id>/plugin.json` following the canonical key order enforced by `manifest_io.cpp`:
1. `"format"` (`"openeden.plugin"`)
2. `"version"` (`1`)
3. `"id"` (lowercase `[a-z0-9_]+`)
4. `"name"`
5. `"pluginVersion"`
6. `"kind"` (`"feature"` or `"compatibility"`)
7. `"sdk"` (`{"major": 1, "minor": 0}`)
8. `"toolchain"` (optional toolchain string)
9. `"library"` (`{"windows": "creativemotion.dll", "linux": "libcreativemotion.so"}`)
10. `"provides"` (array of `{"contract": "...", "version": [1, 1]}`)
11. `"requires"` (array of `{"contract": "...", "version": [1, 0]}`)
12. `"optional"` (array of `{"contract": "...", "version": [1, 0]}`)
13. `"loadAfter"` (array of plugin IDs)
14. `"data"` (`{"mount": "data"}`, optional)
15. `"settings"` (map of setting key to `{"type": "bool"|"int"|"float"|"string", "default": ...}`)
16. `"licence"` (`{"spdx": "..."}`, optional)

Verify your manifest with `writeCanonicalManifest(manifest)` in a round-trip test.

### Step 3: Adapt External Subproject CMake (Lessons from CreativeMotion)

When integrating an external standalone repository (such as `CreativeMotion`, `AdvancedBodies`, `ComplexClothing`, or `Reflectron`) via `add_subdirectory(...)`:

1. **Position-Independent Code (`-fPIC`) & Static Internal Libraries**:
   - External repositories that build internal `STATIC` libraries (`opas_math`, `opas_rig`, etc.) linked into a plugin `SHARED` library must have `CMAKE_POSITION_INDEPENDENT_CODE ON`.
   - In OpenEden's root `CMakeLists.txt`, `option(BUILD_SHARED_LIBS ... ON)` is enabled globally on Windows. If the external project calls `add_library(foo src.cpp)` without an explicit `STATIC` keyword, CMake will build `foo` as a `.dll` with no exported symbols unless `BUILD_SHARED_LIBS` is `OFF` inside the subproject.
   - **Rule for external plugin repos**: Add the following to the external repo's top-level `CMakeLists.txt`:
     ```cmake
     set(CMAKE_POSITION_INDEPENDENT_CODE ON)
     if(NOT PROJECT_IS_TOP_LEVEL)
       set(BUILD_SHARED_LIBS OFF)
     endif()
     ```
2. **Disable Standalone Tests, Viewers, and Duplicate Physics Backends**:
   - When `PROJECT_IS_TOP_LEVEL` is `FALSE`, disable the subproject's standalone unit test runner, GUI viewer, and standalone Jolt physics backend (`OPAS_BUILD_TESTS OFF`, `OPAS_BUILD_PHYSICS OFF`, `OPAS_BUILD_VIEWER OFF`), linking only its pure interface and core libraries.
3. **Reuse OpenEden's `nlohmann_json` Target**:
   - Guard `FetchContent_Declare(nlohmann_json ...)` in the external project with `if(NOT TARGET nlohmann_json::nlohmann_json)`. OpenEden already provides `nlohmann_json` with `JSON_Diagnostic_Positions ON`.
4. **Double-Precision Jolt (`JPH_DOUBLE_PRECISION`) Compatibility**:
   - OpenEden compiles Jolt Physics with `DOUBLE_PRECISION ON` (`JPH::RVec3` is `JPH::DVec3`), whereas standalone sandboxes often compile Jolt in single precision (`JPH::RVec3` is `JPH::Vec3`).
   - Whenever code interacts with Jolt world positions (`BodyCreationSettings`, constraint world anchors `mPosition1`/`mPosition2`, `AddImpulse` world point, `MoveKinematic` target position, `GetPositionAndRotation`, `RRayCast` origin), use explicit `JPH::RVec3` conversion helpers (`toJoltR` / `fromJoltR`).

### Step 4: Implement the Shared Physics Adapter (For Physics-Using Plugins)

If the plugin steps or queries rigid bodies/constraints:
- Implement the plugin's internal physics world interface over `openeden::sdk::PhysicsService&` (see [`plugins/creativemotion/host_physics_world.hpp`](../../plugins/creativemotion/host_physics_world.hpp) and [`host_physics_world.cpp`](../../plugins/creativemotion/host_physics_world.cpp)).
- **Do not call `PhysicsService::step()` from the plugin during normal engine ticks.** Instead:
  - In `Phase::FixedStepPrePhysics`, run pre-step drivers/listeners (`runPreStepListeners(dt)`) to update joint motor gains, joint targets, and kinematic targets.
  - The host steps the shared Jolt world once for all systems.
  - In `Phase::FixedStepPostPhysics`, run post-step listeners (`runPostStepListeners(dt)`), read body states, and reconcile character capsules or cloth meshes.

### Step 5: Implement `openeden::sdk::Plugin` Lifecycle

In `plugins/<plugin_id>/<plugin_id>_plugin.cpp`:

1. **`registerServices(Registrar& registrar)`**:
   - Provide contracts: `registrar.provide<MyContract>(*this);`
   - Subscribe to frame phases (`registrar.registerPhaseCallback(Phase::..., PhaseCallback(...))`) and fixed-step physics phases (`registrar.registerPhaseCallback(Phase::FixedStepPrePhysics, FixedStepPhaseCallback(...))`).
2. **`resolve(const ServiceLocator& locator)`**:
   - Look up required and optional contracts (`locator.find<OtherContract>()`). Return `false` only if a required contract is missing.
3. **`start(Host& host)`**:
   - Read initial settings from `host.getSettingsService()->getBool("<plugin_id>", "enabled", false)`.
   - Construct adapters over `host.getPhysicsService()`, `host.getEventBus()`, etc.
   - Register named collision layers and collision pairs via `host.getPhysicsService()->registerCollisionLayer(...)` and `declareCollisionPair(...)`.
   - Register VFS format readers via `host.getVfsService()->registerFormatReader(".ext.json", reader)`.
   - Register save chunk handlers via `host.getSaveService()->registerHandler("plugin_id", handler)`.
   - Register Lua scripting tables via `host.getScriptingService()->registerModule("<plugin_id>", ...)`.
   - Register editor inspection panels via `host.getEditorService()->registerPanel(...)` if `host.getEditorService()->isEditorActive()`.
4. **`stop()`**:
   - Destroy all physics bodies, joints, and collision groups created by the plugin in the shared `PhysicsService`.
   - Unregister all VFS format readers (`vfs->registerFormatReader(ext, nullptr)`), save chunk handlers (`save->unregisterHandler(chunkId)`), and Lua modules (`scripting->unregisterModule(pluginId)`) so no `std::shared_ptr` or `std::function` with a vtable/deleter inside the plugin DLL outlives `DynamicLibrary` unload.
   - Release all host pointers and reset state cleanly.
5. **Export Entry Point (Shared Target Only)**:
   Guard `openeden_plugin_entry` with `#ifdef OPENEDEN_SHARED_PLUGIN` (defined on the `SHARED` target via `target_compile_definitions(<plugin_id> PRIVATE OPENEDEN_SHARED_PLUGIN)`) so `<plugin_id>_plugin_static` does not export duplicate `openeden_plugin_entry` symbols when multiple static plugin libraries are linked into `components-tests`:
   ```cpp
   #ifdef OPENEDEN_SHARED_PLUGIN
   extern "C" OPENEDEN_PLUGIN_EXPORT const openeden::sdk::PluginDescriptor* openeden_plugin_entry(
       const openeden::sdk::HostInfo* host)
   #endif
   ```

### Step 6: Build Both Static (for Unit Tests) and Shared (for Runtime) Targets

In `plugins/<plugin_id>/CMakeLists.txt`:
- Build `<plugin_id>_plugin_static` (`STATIC`) so `components-tests` can directly instantiate and test the plugin class and its adapters in-process without needing `dllexport` on internal classes.
- Build `<plugin_id>` (`SHARED`) outputting to `${CMAKE_BINARY_DIR}/plugins/<plugin_id>/` alongside a copied `plugin.json` for dynamic loading via `PluginLoader`.

---

## 3. Verification Checklist for Every New Plugin

Before marking a plugin integration complete, verify all of the following in `apps/components_tests/plugin/test_<plugin_id>_plugin.cpp`:
- [ ] `plugin.json` loads without error and round-trips identically through `writeCanonicalManifest`.
- [ ] `LifecycleManager` initializes the plugin, resolves its provided contract for a downstream consumer plugin, and shuts down in reverse topological order.
- [ ] **Off-by-default invariant**: Immediately after `initialize()`, `isEnabled()` is `false`, and running all fixed-step and frame phases leaves the shared physics world and host state completely untouched.
- [ ] Enabling the plugin at runtime activates its simulation/service in the shared host environment.
- [ ] Collision layers, VFS format readers, save-game chunk serialization/deserialization, and Lua module (`openeden.<plugin_id>`) all function and round-trip cleanly.
