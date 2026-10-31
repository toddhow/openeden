# Task: 0003 Plugin System Phase 3 - Core Services Behind the Host API
Design doc sections: [`docs/design/openeden-extension-system.md`](file:///i:/Projects/OpenEden/docs/design/openeden-extension-system.md#L401-L408), Sections 4.3 - 4.9, 6.6, 8, 10
Goal: Implement the core groundwork services exposed to plugins through the `Host` API: event bus with fixed-capacity preallocated queues, entity management with stable IDs and per-entity plugin data, settings namespaces, save game chunks with unread chunk preservation, VFS data mounting and file format registration, named collision layer assignment over shared physics, and deferred event processing at engine frame phase boundaries.

Inherited code touched:
- `components/CMakeLists.txt` - add Phase 3 source files to `plugin` component
- `apps/components_tests/CMakeLists.txt` - add Phase 3 unit tests

Inputs / outputs:
- Public SDK interfaces (`sdk/include/openeden/`):
  - `event_bus.hpp`:
    - `EventId` type identification and registration.
    - `EventBus` interface supporting `publishImmediate(event)` and `publishDeferred(event, targetPhase)`.
    - `subscribe(eventId, callback)` with subscriber ordering by plugin load order.
  - `entity.hpp`:
    - `EntityId` (stable 64-bit index + generation handle, never raw engine pointers).
    - `EntityLifecycleEvent` (`Spawned`, `Despawned`, `CellLoaded`, `CellUnloaded`, `Died`, `Revived`).
    - `EntityLifecycleListener` interface.
    - Per-entity plugin data slot attachment API (`attachData`, `getData`, `removeData`) with automatic cleanup on entity despawn.
  - `settings.hpp`:
    - `SettingsService` interface for `[plugin.<id>]` namespace.
    - Typed accessors: `getBool`, `getInt`, `getFloat`, `getString` with default fallback.
  - `save.hpp`:
    - `SaveChunkReader` & `SaveChunkWriter` interfaces.
    - `SaveChunkHandler` interface with versioning and migration support.
  - `vfs.hpp`:
    - `VfsService` interface for resource reading and plugin `data/` mounting.
    - `FileFormatReader` interface returning located errors without throwing.
  - `physics.hpp`:
    - `PhysicsService` interface: `BodyId`, `ConstraintId`, ray cast and shape cast queries.
    - Named collision layer registration and pair filtering.
  - `host.hpp`:
    - Extended `Host` interface exposing references to `EventBus`, `EntityManager`, `SettingsService`, `SaveService`, `VfsService`, and `PhysicsService`.
- Engine component implementation (`components/plugin/`):
  - `event_bus.hpp` / `event_bus.cpp`:
    - Fixed-size ring buffer queues for deferred events.
    - Zero dynamic allocation on hot-path publishing.
    - Overflow counting and logging (`droppedCount`).
    - Processing queued events at phase boundaries in publish order, notifying subscribers in load order.
  - `entity_manager.hpp` / `entity_manager.cpp`:
    - Stable entity ID generation, validation, and recycling.
    - Per-entity plugin data storage with custom deleters, automatically released on entity despawn.
    - Lifecycle event dispatching to registered listeners in load order.
  - `settings_service.hpp` / `settings_service.cpp`:
    - Per-plugin settings namespace management.
    - Initialization from `PluginManifest::settings` with "off by default" semantics.
  - `save_service.hpp` / `save_service.cpp`:
    - Serialization and deserialization of plugin save chunks.
    - Preservation of unread chunks from missing/disabled plugins byte-for-byte across save/load round trips.
    - Migration invocation when chunk version is older than current plugin version.
  - `vfs_service.hpp` / `vfs_service.cpp`:
    - Plugin directory mounting overlay.
    - Format reader dispatch for asset extensions.
  - `collision_layer_registry.hpp` / `collision_layer_registry.cpp`:
    - Deterministic collision bit assignment for named layers registered by plugins.
    - Collision mask computation for declared colliding pairs.
  - `physics_service.hpp` / `physics_service.cpp`:
    - Shared physics service implementation managing bodies, constraints, raycasts, and step callbacks.
  - Integration with `LifecycleManager`:
    - Processing deferred events at the start of each frame and fixed-step phase.
- Unit tests (`apps/components_tests/plugin/`):
  - `test_event_bus.cpp`:
    - Immediate event synchronous delivery.
    - Deferred event queuing and dispatch at designated phases.
    - Zero dynamic heap allocation on hot path.
    - Deterministic subscriber order (plugin load order).
    - Ring buffer capacity saturation and overflow logging (`droppedCount`).
  - `test_entity_manager.cpp`:
    - Stable EntityId generation and stale handle invalidation.
    - Lifecycle event dispatch (`Spawned`, `Despawned`, `CellLoaded`, etc.).
    - Per-entity plugin data attachment, retrieval, and automatic destruction on despawn.
  - `test_settings_service.cpp`:
    - Per-plugin namespace isolation (`[plugin.<id>]`).
    - Default values from manifest settings.
    - Off-by-default behavior validation.
  - `test_save_service.cpp`:
    - Plugin chunk write and read.
    - Preserving unread chunks for missing plugins byte-for-byte across save round trips.
    - Version migration callback execution.
  - `test_collision_layers.cpp`:
    - Named collision layer registration and deterministic bit assignment.
    - Symmetric pair collision mask calculation.
  - `test_vfs_service.cpp`:
    - Plugin data folder mounting in load order.
    - File format reader registration and located error reporting.

Constraints & Invariants:
- Zero allocation in hot paths: Event publishing, phase callbacks, and entity data lookups must not allocate heap memory during active simulation/frame phases.
- Determinism: Deferred events deliver strictly in publish order; subscribers called in plugin load order; collision layer bits assigned deterministically.
- Unread save preservation: Saves containing data for plugins not currently active must preserve that data completely unchanged upon saving back out.
- Off-by-default: Without explicit settings changes, core behavior remains 100% identical to the base game.
- Memory safety: Entity data slots must cleanly destroy stored components when an entity despawns.

Acceptance criteria:
- [x] `EventBus` provides immediate delivery and phase-deferred delivery with fixed-size queues and overflow logging.
- [x] Deferred events deliver in publish order to subscribers in load order without hot-path heap allocations.
- [x] `EntityManager` generates stable `EntityId` handles and invalidates stale generations.
- [x] Per-entity plugin data attaches to entities and is guaranteed destroyed when the entity despawns.
- [x] Entity lifecycle events (`Spawned`, `Despawned`, `CellLoaded`, etc.) dispatch to listeners in load order.
- [x] `SettingsService` isolates settings per plugin namespace and guarantees off-by-default defaults.
- [x] `SaveService` saves and loads plugin chunks, invokes migration on version changes, and preserves missing plugin chunks byte-for-byte.
- [x] `CollisionLayerRegistry` registers named layers and declared pairs, computing deterministic collision masks without number collisions.
- [x] `VfsService` mounts plugin `data/` directories in load order and dispatches to registered format readers.
- [x] `Host` interface provides access to all core groundwork services.
- [x] All unit tests pass cleanly in test suite.

Review Status:
- Adversarial Code Review completed and all 18 findings resolved:
  1. UB & bit overflow in `CollisionLayerRegistry` resolved with 32-layer cap and 0 return.
  2. Hot-path heap allocations eliminated via pre-allocated circular ring buffers and pre-sorted subscriber arrays.
  3. Re-entrancy double-free in `EntityManagerImpl::destroyEntity` eliminated by instant liveness invalidation.
  4. `LifecycleManager` updated to flush deferred events at `runPhase` and `runFixedStepPhase` boundaries.
  5. Boundary exception containment added across `EventBus`, `EntityManager`, `SaveService`, and `VfsService`.
  6. Transparent comparison (`std::less<>`) applied to `EntityManagerImpl` and `SettingsServiceImpl`.
  7. Queue overflow warning logs added to `EventBusImpl`.
  8. Missing plugin save chunk preservation warning logged.
  9. Safe chunk retention added in `SaveServiceImpl::registerHandler`.
  10. Pre-sorted mounts and multi-dot format extension matching in `VfsServiceImpl`.
  11. `HostImpl` concrete class implemented and wired to `LifecycleManager`.
- 57 Phase 3 and integration tests passing; 92 total plugin tests passing; 1,468 full regression tests passing (0 failures).
