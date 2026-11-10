# Extension System Component Engineering Guide

`components/extension/` and `sdk/include/openeden/`

## Purpose & Boundaries

This component implements the engine groundwork for OpenEden's runtime and static extension system:
- `sdk/include/openeden/`: The public SDK headers (`version.hpp`, `phases.hpp`, `service_locator.hpp`, `registrar.hpp`, `host.hpp`, `extension.hpp`, the service headers such as `physics.hpp` and `event_bus.hpp`, and `physics_events.hpp`). Extensions compile against this SDK only.
- `components/extension/`: The engine's manifest models, deterministic dependency resolver, service registry, and lifecycle manager.

## Core Rules & Architecture

1. **Contracts Only:** Extensions never include each other's headers or link each other's libraries. They interact exclusively through versioned contracts looked up via `ServiceLocator`.
2. **Version Compatibility:** Provider major must match consumer major; provider minor must be greater than or equal to consumer minor (`isCompatible(required, provided)`).
3. **C++20 Keyword Notice:** `requires` is a C++20 keyword (for concepts/constraints). Manifest structures and helpers use `requiredContracts`, `providedContracts`, and `optionalContracts` in C++, mapping to JSON keys `"requires"`, `"provides"`, and `"optional"`.
4. **Deterministic Resolution:**
   - Graph validation detects duplicate providers for contract majors, missing contracts, version mismatches, and dependency cycles.
   - Topological sorting uses Kahn's algorithm with an `std::set` ready-queue to guarantee strictly deterministic, lexicographical tie-breaking by extension ID across any input permutation.
5. **Zero Allocations in Hot Paths:**
   - `runPhase()` iterates over const references to pre-registered callbacks.
   - Any fault-driven callback withdrawals or cleanups are deferred until after phase execution completes, ensuring no vector copies or heap allocations occur per frame.
6. **Reverse Load Order Teardown & Destruction Before Unload:**
   - `stop()` is called in strict reverse load order (consumers stop before providers).
   - Extension instances are destructed in strict reverse load order (`mLoadOrder.rbegin()` to `mLoadOrder.rend()`).
   - Dynamic libraries are unloaded only AFTER extension instances are destroyed, ensuring virtual destructors run while code is mapped.
7. **Exception Containment:**
   - No exception crosses the core/extension boundary in either direction.
   - Exceptions inside extension callbacks, entry points, or lifecycle methods are caught, logged with extension ID, stage, and reason, and isolate the faulted extension safely while unaffected extensions continue.
8. **Manifest Canonical Serialization & Located Errors:**
   - Manifest parser reports located errors (`LoadError`: `file`, `line`, `field`, `message`) without throwing.
   - Canonical writer preserves fixed key ordering, 2-space indentation, and round-trips byte-for-byte.
9. **Dynamic Loading & Handshake:**
   - Platform dynamic library loading (`LoadLibraryW` on Windows, `dlopen` on POSIX) wrapped in RAII `DynamicLibrary`.
   - Entry point handshake verifies `sdkVersion` and `id` match manifest before calling `descriptor->create()`.
   - `openeden_add_extension()` CMake macro compiles shared libraries targeting the SDK.
10. **Core Host Services (Phase 3):**
    - `EventBus`: Immediate typed/raw publishing and phase-deferred queues. Pre-allocated circular ring buffers and pre-sorted subscribers guarantee zero dynamic heap allocations on frame simulation hot paths. Overflow is counted and logged. Event types can be marked as forwarded to Lua with a flat payload (`markForwardedToLua`); the bus only records the marking, and the engine does the forwarding (task 0016).
    - `EntityManager`: Stable `EntityId` (32-bit index + 32-bit generation) handles with generation recycling. Per-entity extension data automatically destructs via custom deleters upon despawn. Handle liveness invalidation protects against re-entrant despawn double-frees.
    - `SettingsService`: Per-extension namespace isolation with transparent comparator string_view lookups and strict off-by-default manifest defaults.
    - `PhysicsServiceImpl` (SDK `PhysicsQueryService`): Read-only queries on a Jolt world the host owns; it cannot create one. `HostImpl::attachPhysicsWorld` makes it available and `getPhysicsQueryService()` is null until then. `BodyId` carries Jolt's body index and sequence number, so a handle goes stale when the world's owner destroys the body. Query masks are matched against object layers as bit patterns (`MWPhysics::Layers`); the host publishes them by name. The service is the one boundary between the SDK's meters and the world's length unit (`worldUnitsPerMeter`). Shape casts build their Jolt shape on the stack with `SetEmbedded()`, so they do not allocate.
    - In the game (task 0016), `SettingsServiceImpl::useEngineSettings`, `VfsServiceImpl::setEngineVfs` and `EventBusImpl::setForwardSink` connect services to the engine; a test host calling none of them keeps the models.
    - `VfsService`: Overlaid data mount hierarchies pre-sorted by load order, supporting multi-dot asset format extension readers and located error reporting.
    - `HostImpl`: Concrete engine host wiring core services and deferred event flushing at phase boundaries (`runPhase`).
11. **Diagnostics & Memory Services (Phase 4):**
    - `LoggingService`: Per-extension logger caching prefixing messages with `[extensionId]`. Atomic log levels (`mLevel`, `mGlobalLogLevel`) protect against multi-threaded data races. Log sink exceptions are caught and contained at the boundary.
    - `ProfilerService`: Named scope timing using RAII `ProfileScope` (`std::string_view` keys). Pre-allocated active scope pool (512 entries) with thread ID identification isolates parallel worker threads. Pre-reserved scope statistics (1024 entries) achieve strictly zero dynamic heap allocations on the hot path. Aggregated metrics reset per frame at `Phase::Ui`.
    - `ScratchMemoryService`: Pre-allocated frame-scoped linear bump allocator. Validates non-zero power-of-two alignments and safely prevents integer overflow during bounds checks. Memory reuses cleanly across frames via `LifecycleManager::runPhase(Phase::Ui)`.
    - `DebugDrawService`: Decoupled primitive queues (points, lines, boxes, spheres, text) pre-reserved with 4096 capacity and protected by `MaxPrimitivesPerCategory = 32768`. Thread-safe submission across worker threads. Transient primitives clear at frame boundaries; persistent primitives decay cleanly. Snapshot extraction provides render data without renderer coupling.
    - **Shown in the game (task 0017 stage 5):** `LifecycleManager::setFrameEndHook` (a plain function pointer, no `std::function`) is called at the end of `Phase::Ui`, after the phase's callbacks and BEFORE the end-of-frame reset, so the engine can copy what extensions submitted. `DebugDrawFrame::collect` copies the service's primitives (`DebugDrawServiceImpl::withPrimitives`, under the service's lock, no copy of the vectors) into lists reserved to fixed caps (4096 points, 16384 lines, 2048 boxes, 1024 spheres, 256 labels of at most 63 characters); the rest are dropped and counted (`dropped()`, `overflowFrames()`), nothing grows. `expandDebugDrawFrame` (header only, pure) turns the frame into line segments through a sink: lines as they are, points as three-axis crosses (half length `size` x 5 cm), boxes as 12 edges, spheres as three 16-segment great circles, labels as small crosses (text is not drawn; there is no in-world text path yet), scaled once by the world's units per meter. `ProfilerServiceImpl::snapshot` copies the timed scopes into a fixed `ProfilerSnapshot` (128 rows) and `formatProfilerReport` writes them as text into a caller's buffer. `LifecycleManager::setPhaseTiming(true)` records every extension callback as the scope `phase.<Phase>` (two clock reads and one profiler sample per callback; off by default). The service's own lists still allocate if an extension submits more than 4096 primitives of one kind in a frame (they are reserved to that size; the cap is 32768), and a primitive's `std::string` extension ID allocates when it is over 15 characters: the per-frame work this task adds does not.
    - The runtime-side `EditorService` hooks were removed in SDK 3.0 (task 0031 stage C, decision 0007): Eden Studio's tools come from editor modules (`sdk/include/openeden/editor_module/`), which the game never loads. `Host` has no editor accessor.
12. **SDK 2.0 (decision 0003, task 0019) and SDK 3.0 (task 0031 stage C):**
    - Extensions read physics and never change it, run only in the per-frame phases, and have no Lua API. A system that must step or change physics or extend Lua goes into the core.
    - The loader and resolver refuse a manifest from another SDK major, saying which SDK it was built for and which the host provides.
    - SDK 3.0 removed `Host::getEditorService` and the `EditorService` hooks (a change to `Host`'s layout, so a major bump). A manifest or library built for 2.x is refused like a 1.x one: `Built for SDK 2.0; this host provides 3.0`, naming the extension, at stage `sdk`.
    - `CI/check_extension_sdk.sh` fails if an SDK header declares a removed operation, or one of the removed editor hooks, again.
13. **The manifest's optional `editor` entry (task 0031):** `{ "library": "<file name>", "editorApi": { "major", "minor" } }` names an editor module library that only Eden Studio loads. The parser checks it with located errors (the library must be a plain file name), the canonical writer writes it last and only when present, and the loader and resolver ignore it: the game never loads editor code.
14. **SDK 4.0 and the game-pack tier (task 0084, decision 0013):**
    - Manifest format 2 checks `kind` (`feature`, `compatibility`, `game`); `game` needs format 2. `LifecycleManager` hands each extension a `PackScopedHost` (`pack_host.*`): the five game services (save chunks, record types, content loaders, script host, windows; headers in `sdk/include/openeden/game/`) are returned only to `kind: "game"`, any other kind gets `nullptr` and a `CapabilityDenial`. Decisions 0003 and 0005 stay in force for every other kind.
    - One game pack per session (the first by ID); a second is refused with a failure naming both. A game pack's failure is a session error and refuses saving. Its callbacks run before the other kinds' in every phase; `GameSimulation` and `GameUpdateUi` are for game packs only.
    - `SaveChunkServiceImpl` (`save_chunk_store.*`) keeps chunks keyed `{pack id, name, version}`, preserves unknown ones, and checks versions before any handler reads; main thread only. `HostImpl` owns the services; `releaseExtensionObjects` clears them before a library unloads.
    - `CI/check_extension_sdk.sh` allows the save chunk types only under `sdk/include/openeden/game/` and forbids `extensions/` to include a game header. A new game service belongs there and in `PackScopedHost`.
    - Script bindings (task 0097): `BindingsStore` (`bindings_store.*`) keeps the namespaces a game pack declared through `PackScopedHost::getBindingsService()` (game packs only, names `<pack id>.<area>`). Namespaces are never freed while the host lives; a retired pack only loses its handlers, so a script holding a function gets an error. A new service that keeps pack handlers must join `HostImpl::disableExtension` and `releaseExtensionObjects`.
