# Task: 0002 Plugin System Phase 2 - Dynamic Loading and Manifests
Design doc sections: [`docs/design/openeden-extension-system.md`](file:///i:/Projects/OpenEden/docs/design/openeden-extension-system.md#L396-L400), Sections 4.1, 5.1, 5.2, 6.1, 6.2, 7.1, 7.2, 8, 10
Goal: Implement filesystem manifest discovery, canonical JSON manifest parsing and serialization with located errors, platform dynamic library loading (`.dll`/`.so`), entry point handshake validation (`openeden_plugin_entry`), enable list filtering, integration with `LifecycleManager`, and the `openeden_add_plugin()` CMake helper.

Inherited code touched:
- `extern/CMakeLists.txt` - add `nlohmann_json` via FetchContent with `JSON_Diagnostic_Positions ON`
- `components/CMakeLists.txt` - include dynamic library and manifest loader sources and link `nlohmann_json`
- `apps/components_tests/CMakeLists.txt` - add Phase 2 unit tests and test mock plugin targets

Inputs / outputs:
- Manifest data & I/O (`components/plugin/`):
  - `load_error.hpp`: `openeden::plugin::LoadError` (`file`, `line`, `field`, `message`, `toString()`).
  - `manifest.hpp`: Extended `PluginManifest` with `format`, `version`, `kind`, `library` (`windows`, `linux`), `dataPath`, `settings`, `licence`.
  - `manifest_io.hpp` / `manifest_io.cpp`:
    - `parseManifest(std::string_view jsonText, std::string_view filename, LoadError& error) -> std::optional<PluginManifest>`: validates required fields, types, contract version formats (`[major, minor]`), reports exact 1-based line number and JSON pointer field.
    - `writeCanonicalManifest(const PluginManifest& manifest) -> std::string`: canonical serialization with fixed key ordering, 2-space indentation, minimal number representations, no trailing commas, guaranteeing byte-for-byte round trip.
    - `loadManifestFile(const std::filesystem::path& path, LoadError& error) -> std::optional<PluginManifest>`.
- Platform dynamic loading wrapper (`components/plugin/`):
  - `dynamic_library.hpp` / `dynamic_library.cpp`: RAII wrapper around `LoadLibraryW`/`GetProcAddress`/`FreeLibrary` (Windows) and `dlopen`/`dlsym`/`dlclose` (POSIX).
  - Handles unicode paths, provides last error messages, move-only semantics, prevents resource leaks.
- Plugin loader, discovery, and handshake (`components/plugin/`):
  - `plugin_loader.hpp` / `plugin_loader.cpp`:
    - `getHostToolchain()`: returns current compiler/platform toolchain ID (e.g. `"msvc-19.44-x64-release"`).
    - `discoverPluginDirectories(const std::vector<std::filesystem::path>& searchDirs) -> std::vector<DiscoveredPlugin>`: scans plugin directories (e.g. `plugins/`, mod directories), looking for directories containing `plugin.json`.
    - `filterEnabledPlugins(const std::vector<DiscoveredPlugin>& plugins, const std::vector<std::string>& enabledList) -> std::vector<DiscoveredPlugin>`: keeps only enabled plugins; if `enabledList` is empty, all discovered plugins are enabled.
    - `loadPluginLibrary(const DiscoveredPlugin& discovered, const openeden::sdk::HostInfo& hostInfo) -> Result/Expected<LoadedPlugin, PluginLoadError>`:
      1. Validates manifest SDK compatibility against host.
      2. Validates toolchain in Phase A (must match host toolchain).
      3. Resolves platform library path from `manifest.library` (`.dll` on Windows, `.so` on Linux).
      4. Loads shared library via `DynamicLibrary`.
      5. Locates `openeden_plugin_entry` symbol.
      6. Invokes entry point with `HostInfo`.
      7. Validates descriptor non-null.
      8. Validates descriptor `sdkVersion` matches manifest `sdkVersion`.
      9. Validates descriptor `id` matches manifest `id`.
      10. Calls `descriptor->create()` to create `std::unique_ptr<openeden::sdk::Plugin>`.
      11. Returns `LoadedPlugin` holding manifest, plugin instance, and dynamic library handle.
- LifecycleManager integration:
  - Supports adding a dynamically loaded plugin (`addPlugin(manifest, instance, library)`), ensuring the dynamic library remains loaded for the lifetime of the plugin instance, and is unloaded strictly in reverse load order after the plugin instance is destroyed.
- CMake helper (`cmake/OpenEdenPlugin.cmake`):
  - `openeden_add_plugin(<id> SOURCES ... MANIFEST <file>)` macro: creates shared library target, links `openeden_sdk`, sets symbols visibility / export defines, validates manifest file existence.
- Tests (`apps/components_tests/plugin/`):
  - `test_manifest_io.cpp`:
    - Parsing valid manifest: checks all fields parsed accurately.
    - Located error reporting: syntax error, missing required field, invalid type, negative version, unknown format name, wrong format version. Checks 1-based line number and JSON pointer path.
    - Byte-for-byte canonical round trip: `writeCanonicalManifest(parseManifest(canonicalText)) == canonicalText`.
  - `test_dynamic_library.cpp`:
    - Load valid library, lookup symbol, call function.
    - Fail on non-existent library with informative error.
    - Fail on missing symbol.
    - RAII unload.
  - `test_plugin_loader.cpp`:
    - Discovery across multiple search directories.
    - Enable list filtering (enabled, disabled, unknown in enable list).
    - ABI and toolchain validation failure (rejected before loading code).
    - SDK version mismatch rejection.
    - Handshake validation (entry point missing, null descriptor, id mismatch, SDK mismatch).
    - Successful dynamic load and execution through `LifecycleManager` (register -> resolve -> start -> stop -> unload).

Constraints & Invariants:
- Located errors: Every manifest parse error reports 1-based line number and JSON pointer field; never throws unhandled exceptions.
- Deterministic canonical serialization: Field order is fixed, indentation is 2 spaces, UTF-8 strings properly escaped.
- Fail closed, never crash: A missing DLL, corrupted manifest, ABI mismatch, or failed handshake disables that plugin with an explicit reason; core and other plugins proceed.
- Destruction before unload: A plugin's virtual destructor `~Plugin()` must execute while the library code is still loaded in memory. Only after `instance.reset()` may `dynamicLibrary.reset()` unload the `.dll`/`.so`.
- Clean shutdown: In reverse load order, `stop()` is called, then instances destroyed, then libraries unloaded.
- Off-by-default: Without any plugins enabled or discovered, core engine behavior is completely identical to the base game.

Acceptance criteria:
- [x] Manifest parser parses all fields defined in Section 5.2 correctly.
- [x] Manifest parser locates errors with accurate file name, 1-based line number, JSON pointer path, and descriptive message.
- [x] Manifest canonical writer produces byte-for-byte identical output on round-trip with canonical manifests.
- [x] `DynamicLibrary` abstracts `LoadLibraryW`/`dlopen` cleanly with RAII semantics and accurate error reporting.
- [x] `discoverPluginDirectories` discovers plugins containing `plugin.json` in provided directories and ignores invalid or non-plugin folders.
- [x] `filterEnabledPlugins` accurately selects plugins present in the enable list and preserves discovery order.
- [x] ABI and toolchain checking rejects incompatible plugins before calling library entry point.
- [x] Handshake validates entry point descriptor `id` and `sdkVersion` against manifest; mismatches cause clean library unload without leaking resources.
- [x] Integration with `LifecycleManager` ensures instance destruction precedes library unloading in reverse load order.
- [x] `openeden_add_plugin()` CMake helper builds a dynamic plugin targeting the SDK.
- [x] All unit tests pass cleanly in test suite.
