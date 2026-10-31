# OpenEden Plugin Implementation Guide

This guide documents everything required to implement, build, test, and integrate a native C++20 plugin into OpenEden against **SDK 2.0**. Its lessons come from the first plugin work; CreativeMotion has since moved into the engine (decision 0001), and the Morrowind compatibility pack ([`plugins/compat_morrowind/`](../../plugins/compat_morrowind/)) is the first real client. **Start from the sample plugin, [`plugins/example/`](../../plugins/example/),** which uses every service in the running game.

Use this guide when implementing upcoming plugins, such as **Reflectron** and further compatibility packs. A system that must change physics or extend Lua is not a plugin: it goes into the core (decision 0003), as AdvancedBodies and ComplexClothing are planned to.

---

## 1. Architecture Overview & Non-Negotiables

Every OpenEden plugin adheres to the architecture defined in [`openeden-plugin-system.md`](openeden-plugin-system.md):

1. **SDK-Only Dependency (`openeden::sdk`)**:
   - Plugins include headers from [`sdk/include/openeden/`](../../sdk/include/openeden/) and [`sdk/include/openeden/contracts/`](../../sdk/include/openeden/contracts/) only.
   - Plugins **never** include headers from `apps/` or `components/`.
2. **Off-by-Default Feature Plugins**:
   - Feature plugins (`"kind": "feature"`) must declare an `"enabled"` boolean setting defaulting to `false` in `plugin.json`.
   - When `"enabled"` is `false`, the plugin must perform **zero work** in frame callbacks and must not alter host state.
3. **Physics Is Read-Only**:
   - Plugins **never** instantiate their own `JPH::PhysicsSystem` or link Jolt. They query the core's world through `host.getPhysicsQueryService()` (`openeden::sdk::PhysicsQueryService`): `isBodyValid`, `getMotionType`, `readBody`, `readBodies`, `castRay`, `castShape`. It is null when the host has no world, so check it.
   - They cannot create, move or step bodies, make joints or collision groups, or register collision layers. They run only in frame phases; there are no fixed-step phases.
   - Query masks are opaque: get them with `queryMask("<name the host publishes>")`, never assume bit numbers.
   - Physics events (`openeden/physics_events.hpp`) arrive on the event bus on the main thread.
4. **No Lua API**:
   - Plugins cannot register Lua tables or call into Lua. To let mods react, mark an event type with `eventBus->markForwardedToLua<MyEvent>("<plugin_id>", "<eventName>", fields)` in `start`, with a flat payload of numbers, fixed char arrays and booleans; the core delivers it to Lua scripts (task 0016).
5. **Zero Hot-Path Heap Allocations**:
   - Callbacks registered on `Phase::PreSimulation`, `Phase::PostSimulation`, `Phase::Animation`, and `Phase::PreRender` must not allocate heap memory during normal execution. Pre-allocate vectors and scratch buffers during `start(Host&)` or asset load.
6. **No Part in Saves** (decision 0005):
   - Plugins are like script extender plugins: a plugin cannot write to or read from a saved game. Keep what must last in your settings. A feature that must live in a save belongs in the core. Decision 0005 lists the two limits: a `content=` line for a file in your data folder is recorded in saves, and a script may store what your events tell it.

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
    class MyDomainContract
    {
    public:
        static constexpr std::string_view kId = "openeden.my_domain";
        static constexpr openeden::sdk::Version kVersion{ 1, 0 };

        virtual ~MyDomainContract() = default;

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
7. `"sdk"` (`{"major": 2, "minor": 0}`; a manifest for another major is refused)
8. `"toolchain"` (optional toolchain string)
9. `"library"` (`{"windows": "<plugin_id>.dll", "linux": "lib<plugin_id>.so"}`)
10. `"provides"` (array of `{"contract": "...", "version": [1, 1]}`)
11. `"requires"` (array of `{"contract": "...", "version": [1, 0]}`)
12. `"optional"` (array of `{"contract": "...", "version": [1, 0]}`)
13. `"loadAfter"` (array of plugin IDs)
14. `"data"` (`{"mount": "data"}`, optional)
15. `"settings"` (map of setting key to `{"type": "bool"|"int"|"float"|"string", "default": ...}`)
16. `"licence"` (`{"spdx": "..."}`, optional)

Verify your manifest with `writeCanonicalManifest(manifest)` in a round-trip test.

### Step 3: Adapt External Subproject CMake

When integrating an external standalone repository (such as `Reflectron`) via `add_subdirectory(...)`:

1. **Position-Independent Code (`-fPIC`) & Static Internal Libraries**:
   - External repositories that build internal `STATIC` libraries linked into a plugin `SHARED` library must have `CMAKE_POSITION_INDEPENDENT_CODE ON`.
   - In OpenEden's root `CMakeLists.txt`, `option(BUILD_SHARED_LIBS ... ON)` is enabled globally on Windows. If the external project calls `add_library(foo src.cpp)` without an explicit `STATIC` keyword, CMake will build `foo` as a `.dll` with no exported symbols unless `BUILD_SHARED_LIBS` is `OFF` inside the subproject.
   - **Rule for external plugin repos**: Add the following to the external repo's top-level `CMakeLists.txt`:
     ```cmake
     set(CMAKE_POSITION_INDEPENDENT_CODE ON)
     if(NOT PROJECT_IS_TOP_LEVEL)
       set(BUILD_SHARED_LIBS OFF)
     endif()
     ```
2. **Disable Standalone Tests and Viewers**:
   - When `PROJECT_IS_TOP_LEVEL` is `FALSE`, disable the subproject's standalone unit test runner and GUI viewer, and any Jolt it links on its own, linking only its core libraries.
3. **Reuse OpenEden's `nlohmann_json` Target**:
   - Guard `FetchContent_Declare(nlohmann_json ...)` in the external project with `if(NOT TARGET nlohmann_json::nlohmann_json)`. OpenEden already provides `nlohmann_json` with `JSON_Diagnostic_Positions ON`.

### Step 4: Query Physics If You Need To

If the plugin needs to know about the world:
- In `start(Host&)`, keep `host.getPhysicsQueryService()` and look up the masks you need with `queryMask(...)`.
- Cast rays and shapes and read bodies from frame phase callbacks. Handles can go stale between frames: a stale handle reads as invalid, it never crashes.
- Subscribe to `PhysicsHitEvent`, `PhysicsContactBeginEvent` and `PhysicsContactEndEvent` on the event bus instead of polling.

### Step 5: Implement `openeden::sdk::Plugin` Lifecycle

In `plugins/<plugin_id>/<plugin_id>_plugin.cpp`:

1. **`registerServices(Registrar& registrar)`**:
   - Provide contracts: `registrar.provide<MyContract>(*this);`
   - Subscribe to frame phases (`registrar.registerPhaseCallback(Phase::..., PhaseCallback(...))`).
2. **`resolve(const ServiceLocator& locator)`**:
   - Look up required and optional contracts (`locator.find<OtherContract>()`). Return `false` only if a required contract is missing.
3. **`start(Host& host)`**:
   - Read initial settings from `host.getSettingsService()->getBool("<plugin_id>", "enabled", false)`.
   - Keep the services you use: `host.getPhysicsQueryService()`, `host.getEventBus()`, etc. The host attaches its world before plugins start and detaches it after they stop, so a kept pointer stays valid until `stop()` returns.
   - Mark events for Lua via `host.getEventBus()->markForwardedToLua<MyEvent>(...)` if mods should react to them.
   - Register VFS format readers via `host.getVfsService()->registerFormatReader(".ext.json", reader)`.
   - Register editor inspection panels via `host.getEditorService()->registerInspector("<plugin_id>", inspector)` if `host.getEditorService()->isEditorActive()`.
4. **`stop()`**:
   - Unregister all VFS format readers (`vfs->registerFormatReader(ext, nullptr)`) so no `std::shared_ptr` or `std::function` with a vtable/deleter inside the plugin DLL outlives `DynamicLibrary` unload.
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
- `openeden_add_plugin(<plugin_id> SOURCES ... MANIFEST plugin.json DATA data)` builds `<plugin_id>` (`SHARED`) into `${CMAKE_BINARY_DIR}/plugins/<plugin_id>/` with its manifest and data, and installs the same layout. To enable it, add `plugin=<plugin_id>` to `openmw.cfg`, tick it in the launcher, or pass `--plugin=<plugin_id>`.

---

## 3. Verification Checklist for Every New Plugin

Before marking a plugin integration complete, verify all of the following in `apps/components_tests/plugin/test_<plugin_id>_plugin.cpp`:
- [ ] `plugin.json` loads without error and round-trips identically through `writeCanonicalManifest`.
- [ ] `LifecycleManager` initializes the plugin, resolves its provided contract for a downstream consumer plugin, and shuts down in reverse topological order.
- [ ] **Off-by-default invariant**: Immediately after `initialize()`, `isEnabled()` is `false`, and running all frame phases leaves host state completely untouched.
- [ ] Enabling the plugin at runtime activates its service in the shared host environment.
- [ ] VFS format readers and any events marked for Lua forwarding all function and round-trip cleanly.

