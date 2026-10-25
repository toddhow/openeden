# Task: 0001 Plugin System Phase 1 - Registry and Lifecycle (In-Process)
Design doc section: [`docs/design/openeden-plugin-system.md`](file:///i:/Projects/OpenEden/docs/design/openeden-plugin-system.md#L391-L395), Sections 4.1-4.3, 5.2, 6.2-6.8, 8, 10
Goal: Implement the OpenEden SDK skeleton, in-memory manifest data structures, service registry, deterministic dependency resolution, and plugin lifecycle manager with in-process/static plugins.

Inherited code touched:
- `components/CMakeLists.txt` (commit `5459a65292`) - add `plugin` component
- `apps/components_tests/CMakeLists.txt` (commit `5459a65292`) - add plugin component unit tests

Inputs / outputs:
- SDK public headers (`sdk/include/openeden/`):
  - `version.hpp`: `openeden::sdk::Version` struct with major/minor, comparison operators, and compatibility checking (provider major == consumer major && provider minor >= consumer minor); SDK version constant `kVersion`.
  - `phases.hpp`: `openeden::sdk::Phase` enum for the 8 named frame/simulation phases (`Input`, `PreSimulation`, `FixedStepPrePhysics`, `FixedStepPostPhysics`, `PostSimulation`, `Animation`, `PreRender`, `Ui`); callback typedefs.
  - `service_locator.hpp`: `openeden::sdk::ServiceLocator` interface with `find<C>()` and `require<C>()`.
  - `registrar.hpp`: `openeden::sdk::Registrar` interface with `provide<C>(C& impl)` and `registerPhaseCallback(Phase, callback, bool parallel)`.
  - `host.hpp`: `openeden::sdk::HostInfo` and `openeden::sdk::Host` interface.
  - `plugin.hpp`: `openeden::sdk::Plugin` abstract base class (`registerServices`, `resolve`, `start`, `stop`) and `openeden::sdk::PluginDescriptor`.
- Engine core plugin component (`components/plugin/`):
  - `manifest.hpp` / `manifest.cpp`: In-memory manifest structures representing `plugin.json` schema (plugin ID, name, version, SDK version, toolchain, provided contracts with versions, required contracts with versions, optional contracts, loadAfter hints).
  - `dependency_resolver.hpp` / `dependency_resolver.cpp`: Deterministic dependency graph resolution:
    - Verifies SDK compatibility.
    - Validates contract version compatibility (major match, provider minor >= required minor).
    - Checks that every required contract has exactly one provider (or errors on missing / duplicate).
    - Detects and handles optional contracts (linked if available and compatible).
    - Honors `loadAfter` constraints.
    - Detects dependency cycles, naming every plugin in the cycle.
    - Topologically sorts into deterministic load order, breaking ties by plugin ID in lexicographical order.
    - Failure propagation: if a plugin fails resolution, it is disabled along with all dependent plugins, while independent plugins remain valid.
  - `service_registry.hpp` / `service_registry.cpp`: Concrete implementation of `Registrar` and `ServiceLocator`.
    - Stores non-owning pointers to contract implementations keyed by contract ID and major version.
    - Resolves `find<C>()` and `require<C>()` by `C::kId` and `C::kVersion`.
    - Manages registered phase callbacks per phase, ordered by plugin load order and registration order.
  - `lifecycle_manager.hpp` / `lifecycle_manager.cpp`:
    - Coordinates plugin registration (`registerServices` in load order).
    - Coordinates plugin resolution (`resolve` in load order; on `false`, disables plugin and dependents, withdrawing their services).
    - Coordinates plugin startup (`start` in load order).
    - Runs frame and fixed-step phases deterministically in load order.
    - Catches exceptions at boundaries, marks faulty plugins, and isolates failures.
    - Coordinates plugin shutdown (`stop` in REVERSE load order) and destruction in reverse load order.
    - Supports empty host operation (zero plugins).
- Tests (`apps/components_tests/plugin/`):
  - `test_version.cpp`: Version comparisons, compatibility checks.
  - `test_resolution.cpp`: Dependency resolution from manifests alone (load order, missing provider, version mismatch, optional contracts, duplicate provider error, cycle detection, loadAfter ordering, deterministic tie-breaking by id, cascading disable of dependents).
  - `test_lifecycle.cpp`: Full lifecycle verification with mock plugins (register -> resolve -> start -> frame phases -> stop in reverse order; resolve failure disabling dependents; exception containment).
  - `test_empty_host.cpp`: Core lifecycle manager runs all phases and shuts down cleanly with zero plugins.

Constraints:
- Determinism: Topological sorting breaks ties strictly by plugin ID lexicographically; phase callbacks run deterministically in load order and registration order; no wall-clock time or unseeded randomness.
- Allocation: No dynamic heap allocation in the hot path of executing frame phases or fixed-step callbacks once registered.
- Threading: Phase callbacks execute on the main thread; parallel callbacks flagged appropriately.
- Exception safety: No exceptions cross the plugin boundary; errors are recorded as status codes/logs, and faulted plugins are disabled safely.
- Off-by-default: With zero plugins loaded, the engine behaves identically to the fork point.

Acceptance criteria:
- [x] Version comparisons and compatibility rules adhere to Section 4.1/4.2 (major must match, provider minor >= consumer minor).
- [x] Dependency resolver produces bit-identical deterministic load order given any permutation of input plugins.
- [x] Dependency resolver detects missing required contracts, version incompatibilities, duplicate contract providers, and dependency cycles with clear diagnostics naming affected plugins.
- [x] Dependency resolver properly attaches optional contract dependencies when present and ignores them when absent without failure.
- [x] Dependency resolver properly enforces `loadAfter` ordering between plugins.
- [x] Failed resolution or `resolve()` returning false disables the plugin and transitively disables all plugins depending on it, allowing unaffected plugins to continue.
- [x] Lifecycle manager calls `registerServices`, `resolve`, and `start` in load order, and `stop` in reverse load order.
- [x] Frame and fixed-step phases execute registered callbacks in load order and registration order without hot-path heap allocations.
- [x] Empty host: lifecycle manager initializes, runs N frames/steps, and shuts down with zero plugins without errors or leaks.
- [x] With the feature off / zero plugins loaded, core engine behavior is completely unchanged.

Out of scope:
- Dynamic shared library loading (`.dll` / `.so`) and filesystem manifest discovery (Phase 2).
- Native C ABI wrappers (Phase 6).
- Jolt physics integration, entity component storage, Lua bindings, and save chunks (Phases 3-4).
