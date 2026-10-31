# Plugin System Component Engineering Guide

`components/plugin/` and `sdk/include/openeden/`

## Purpose & Boundaries

This component implements the engine groundwork for OpenEden's runtime and static plugin system:
- `sdk/include/openeden/`: The public SDK headers (`version.hpp`, `phases.hpp`, `service_locator.hpp`, `registrar.hpp`, `host.hpp`, `plugin.hpp`, the service headers such as `physics.hpp` and `event_bus.hpp`, and `physics_events.hpp`). Plugins compile against this SDK only.
- `components/plugin/`: The engine's manifest models, deterministic dependency resolver, service registry, and lifecycle manager.

## Core Rules & Architecture

1. **Contracts Only:** Plugins never include each other's headers or link each other's libraries. They interact exclusively through versioned contracts looked up via `ServiceLocator`.
2. **Version Compatibility:** Provider major must match consumer major; provider minor must be greater than or equal to consumer minor (`isCompatible(required, provided)`).
3. **C++20 Keyword Notice:** `requires` is a C++20 keyword (for concepts/constraints). Manifest structures and helpers use `requiredContracts`, `providedContracts`, and `optionalContracts` in C++, mapping to JSON keys `"requires"`, `"provides"`, and `"optional"`.
4. **Deterministic Resolution:**
   - Graph validation detects duplicate providers for contract majors, missing contracts, version mismatches, and dependency cycles.
   - Topological sorting uses Kahn's algorithm with an `std::set` ready-queue to guarantee strictly deterministic, lexicographical tie-breaking by plugin ID across any input permutation.
5. **Zero Allocations in Hot Paths:**
   - `runPhase()` iterates over const references to pre-registered callbacks.
   - Any fault-driven callback withdrawals or cleanups are deferred until after phase execution completes, ensuring no vector copies or heap allocations occur per frame.
6. **Reverse Load Order Teardown & Destruction Before Unload:**
   - `stop()` is called in strict reverse load order (consumers stop before providers).
   - Plugin instances are destructed in strict reverse load order (`mLoadOrder.rbegin()` to `mLoadOrder.rend()`).
   - Dynamic libraries are unloaded only AFTER plugin instances are destroyed, ensuring virtual destructors run while code is mapped.
7. **Exception Containment:**
   - No exception crosses the core/plugin boundary in either direction.
   - Exceptions inside plugin callbacks, entry points, or lifecycle methods are caught, logged with plugin ID, stage, and reason, and isolate the faulted plugin safely while unaffected plugins continue.
8. **Manifest Canonical Serialization & Located Errors:**
   - Manifest parser reports located errors (`LoadError`: `file`, `line`, `field`, `message`) without throwing.
   - Canonical writer preserves fixed key ordering, 2-space indentation, and round-trips byte-for-byte.
9. **Dynamic Loading & Handshake:**
   - Platform dynamic library loading (`LoadLibraryW` on Windows, `dlopen` on POSIX) wrapped in RAII `DynamicLibrary`.
   - Entry point handshake verifies `sdkVersion` and `id` match manifest before calling `descriptor->create()`.
   - `openeden_add_plugin()` CMake macro compiles shared libraries targeting the SDK.
10. **Core Host Services (Phase 3):**
    - `EventBus`: Immediate typed/raw publishing and phase-deferred queues. Pre-allocated circular ring buffers and pre-sorted subscribers guarantee zero dynamic heap allocations on frame simulation hot paths. Overflow is counted and logged. Event types can be marked as forwarded to Lua with a flat payload (`markForwardedToLua`); the bus only records the marking, and the engine does the forwarding (task 0016).
    - `EntityManager`: Stable `EntityId` (32-bit index + 32-bit generation) handles with generation recycling. Per-entity plugin data automatically destructs via custom deleters upon despawn. Handle liveness invalidation protects against re-entrant despawn double-frees.
    - `SettingsService`: Per-plugin namespace isolation with transparent comparator string_view lookups and strict off-by-default manifest defaults.
    - `SaveService`: Versioned plugin chunks with migration callbacks. Missing plugin save chunks are preserved unread byte-for-byte across save round-trips. Little-endian serialization.
    - `PhysicsServiceImpl` (SDK `PhysicsQueryService`): Read-only queries on a Jolt world the host owns; it cannot create one. `HostImpl::attachPhysicsWorld` makes it available and `getPhysicsQueryService()` is null until then. `BodyId` carries Jolt's body index and sequence number, so a handle goes stale when the world's owner destroys the body. Query masks are matched against object layers as bit patterns (`MWPhysics::Layers`); the host publishes them by name. The service is the one boundary between the SDK's meters and the world's length unit (`worldUnitsPerMeter`). Shape casts build their Jolt shape on the stack with `SetEmbedded()`, so they do not allocate.
    - `VfsService`: Overlaid data mount hierarchies pre-sorted by load order, supporting multi-dot asset format extension readers and located error reporting.
    - `HostImpl`: Concrete engine host wiring core services and deferred event flushing at phase boundaries (`runPhase`).
11. **Diagnostics, Memory & Editor Services (Phase 4):**
    - `LoggingService`: Per-plugin logger caching prefixing messages with `[pluginId]`. Atomic log levels (`mLevel`, `mGlobalLogLevel`) protect against multi-threaded data races. Log sink exceptions are caught and contained at the boundary.
    - `ProfilerService`: Named scope timing using RAII `ProfileScope` (`std::string_view` keys). Pre-allocated active scope pool (512 entries) with thread ID identification isolates parallel worker threads. Pre-reserved scope statistics (1024 entries) achieve strictly zero dynamic heap allocations on the hot path. Aggregated metrics reset per frame at `Phase::Ui`.
    - `ScratchMemoryService`: Pre-allocated frame-scoped linear bump allocator. Validates non-zero power-of-two alignments and safely prevents integer overflow during bounds checks. Memory reuses cleanly across frames via `LifecycleManager::runPhase(Phase::Ui)`.
    - `DebugDrawService`: Decoupled primitive queues (points, lines, boxes, spheres, text) pre-reserved with 4096 capacity and protected by `MaxPrimitivesPerCategory = 32768`. Thread-safe submission across worker threads. Transient primitives clear at frame boundaries; persistent primitives decay cleanly. Snapshot extraction provides render data without renderer coupling.
    - `EditorService`: Decoupled Eden Studio extension points (workspaces, inspectors, gizmos). Off-by-default and inert in game and headless host modes (`editorActive = false`), no-oping without heap allocations or side effects.
12. **SDK 2.0 (decision 0003, task 0019):**
    - Plugins read physics and never change it, run only in the per-frame phases, and have no Lua API. A system that must step or change physics or extend Lua goes into the core.
    - The loader and resolver refuse a manifest from another SDK major, saying which SDK it was built for and which the host provides.
    - `CI/check_plugin_sdk.sh` fails if an SDK header declares a removed operation again.
