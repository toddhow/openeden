# Task: 0004 Plugin System Phase 4 - Scripting, Diagnostics and Editor Hooks
Design doc sections: [`docs/design/openeden-extension-system.md`](file:///i:/Projects/OpenEden/docs/design/openeden-extension-system.md#L204-L210), Sections 4.10, 6.4, 10
Goal: Implement the scripting, diagnostics, scratch memory, debug drawing, and editor extension points behind the Host API:
1. Per-plugin logging with levels and plugin ID tagging.
2. Per-plugin named profiling scopes with per-frame aggregation and RAII timing scopes.
3. Zero-allocation frame-scoped scratch memory bump allocator resetting at frame/step boundaries.
4. Debug-draw queueing interface (`opas::debug::Overlay` pattern: points, lines, boxes, spheres, text) submitted as data without direct renderer calls.
5. Per-plugin Lua scripting registry exposing `openeden.<pluginId>` tables to mods via the Lua host.
6. Editor extension points for Eden Studio (dockable workspaces, inspectors, viewport gizmos), safely ignored or no-oped by game and headless hosts.
7. Wire services into `Host`, `HostImpl`, and `LifecycleManager`.

Inherited code touched:
- `components/CMakeLists.txt` - add Phase 4 source files to `plugin` component
- `apps/components_tests/CMakeLists.txt` - add Phase 4 unit tests

Inputs / outputs:
- Public SDK interfaces (`sdk/include/openeden/`):
  - `logging.hpp`:
    - `LogLevel` (`Trace`, `Debug`, `Info`, `Warning`, `Error`, `Fatal`).
    - `Logger` interface (`log`, `trace`, `debug`, `info`, `warning`, `error`, `fatal`).
    - `LoggingService` interface (`getLogger(pluginId)`).
  - `profiling.hpp`:
    - `ProfilerService` interface (`beginScope`, `endScope`, `recordSample`).
    - `ProfileScope` RAII helper for named timing blocks.
    - Per-plugin timing query API for telemetry and metrics.
  - `memory.hpp`:
    - `ScratchMemoryService` interface for linear frame-scoped bump allocation.
    - Zero hot-path heap allocations during per-frame or fixed-step callbacks.
    - Frame boundary reset.
  - `debug_draw.hpp`:
    - Color and geometry structures (`Vector3`, `Color4f`).
    - `DebugDrawService` interface (`drawLine`, `drawPoint`, `drawBox`, `drawSphere`, `drawText`, `clear`).
    - Channel/plugin attribution and depth test toggles.
  - `scripting.hpp`:
    - `ScriptingService` interface.
    - `LuaModuleRegistrar` callback interface (`openeden.<pluginId>`).
    - Binding registration in plugin load order.
  - `editor.hpp`:
    - `EditorWorkspace`, `EditorInspector`, `EditorGizmo` abstract interfaces.
    - `EditorService` registration interface (`registerWorkspace`, `registerInspector`, `registerGizmo`).
    - Headless / game host ignores editor registrations cleanly without errors.
  - `host.hpp`:
    - Extended with `getLoggingService()`, `getProfilerService()`, `getScratchMemoryService()`, `getDebugDrawService()`, `getScriptingService()`, and `getEditorService()`.

- Engine component implementation (`components/plugin/`):
  - `logging_service.hpp` / `logging_service.cpp`:
    - Per-plugin logger caching and formatting (`[pluginId] message`).
    - Custom sink delegation and severity filtering.
  - `profiler_service.hpp` / `profiler_service.cpp`:
    - Microsecond/nanosecond scope tracking.
    - Per-plugin scope statistics (call count, total time, average time, min/max).
    - Double-buffered or per-frame aggregation.
  - `scratch_memory.hpp` / `scratch_memory.cpp`:
    - Pre-allocated linear bump memory buffer (e.g. configurable 1MB-4MB).
    - Aligned pointer advancing with capacity enforcement.
    - Reset invoked at `LifecycleManager` phase/frame boundaries.
  - `debug_draw_service.hpp` / `debug_draw_service.cpp`:
    - Thread-safe / frame-cleared submission queues.
    - Fixed capacity storage to prevent runaway allocations.
    - Snapshot extraction for render pipeline consumption.
  - `scripting_service.hpp` / `scripting_service.cpp`:
    - Registry for per-plugin Lua modules.
    - Dispatching registration callbacks into Lua state.
  - `editor_service.hpp` / `editor_service.cpp`:
    - Workspace, inspector, and gizmo registry.
    - Flag indicating editor active state (true in Studio host, false in game/test host).
  - Integration with `HostImpl` and `LifecycleManager`:
    - Reset scratch memory buffer and debug-draw frame queue at frame boundaries.
    - Aggregate profiling metrics per frame.

Unit test suites (`apps/components_tests/plugin/`):
- `test_logging_service.cpp`:
  - Per-plugin logger isolation, level filtering, log sink callback invocation.
- `test_profiler_service.cpp`:
  - Scoped timing measurement, per-plugin aggregation, RAII scope correctness.
- `test_scratch_memory.cpp`:
  - Linear allocation, alignment guarantees, boundary reset, capacity exhaustion handling.
- `test_debug_draw.cpp`:
  - Point, line, box, sphere, text submission, retrieval by renderer, frame clearing.
- `test_scripting_service.cpp`:
  - Plugin Lua module registration, namespace formatting (`openeden.<pluginId>`), execution callback.
- `test_editor_service.cpp`:
  - Extension registration, inactive editor host no-op/containment, workspace/inspector query.

Constraints & Invariants:
- Zero allocation in hot paths: Debug draw queues, scratch allocators, and profiling scopes must not allocate dynamic heap during active frame execution.
- Exception containment: Exceptions thrown within log sinks, profiler observers, or Lua initializers must be caught at the core boundary and isolated.
- Off-by-default behavior: If no editor or scripting extensions are used, engine performance and behavior are unaffected.
- Thread safety: Debug draw and logging methods callable from workers must be concurrency-safe.

Acceptance criteria:
- [x] `LoggingService` provides isolated per-plugin loggers prefixing output with `[pluginId]` and filtering by level.
- [x] `ProfilerService` measures named scopes per plugin with RAII helpers and aggregates per-frame statistics without heap allocation during measurement.
- [x] `ScratchMemoryService` provides linear bump allocation aligned to requested alignments and resets cleanly at frame boundaries.
- [x] `DebugDrawService` accepts points, lines, boxes, spheres, and text without direct renderer coupling and produces clean frame snapshots.
- [x] `ScriptingService` registers per-plugin Lua tables under `openeden.<id>` and exposes them to the scripting environment.
- [x] `EditorService` accepts workspaces, inspectors, and gizmos; game and test hosts ignore them safely.
- [x] `Host` interface and `HostImpl` expose all Phase 4 services.
- [x] All unit tests pass cleanly in test suite.

Review Findings & Remediation:
- Critical 1.1 (Scratch Memory Integer Overflow): Fixed bounds check in `ScratchMemoryServiceImpl::allocate()` using subtraction against remaining capacity (`padding > mCapacity - mOffset || size > (mCapacity - mOffset - padding)`). Added unit test `IntegerOverflowRejection`.
- Critical 1.2 & 1.3 (Profiler Hot-Path Allocations & Worker Data Races): Redesigned `ProfilerServiceImpl` with pre-allocated active scope pool (`std::array<ActiveScopeEntry, 512>`), pre-reserved stats vector (`1024` entries), thread-id tracking to isolate concurrent worker threads, and fixed char buffers (`ZeroAllocationScopeTimingHotPath` and `ConcurrentWorkerThreadsProfiling`).
- Critical 1.4 (Missing Profiler Reset in Lifecycle): Added `profiler->resetFrame()` to `LifecycleManager::runPhase(Phase::Ui, dt)`.
- Major 2.1 (Debug Draw Capacity Limit & Pre-reservation): Pre-reserved `4096` capacity per category in `DebugDrawServiceImpl` constructor and enforced `MaxPrimitivesPerCategory = 32768` limit.
- Major 2.2 (ProfileScope String Allocations): Stored `std::string_view` in `ProfileScope` RAII helper, eliminating string heap copies.
- Major 2.3 (Logging Data Race): Made `mLevel`, `mExplicitLevel`, and `mGlobalLogLevel` atomic (`std::atomic<LogLevel>`, `std::atomic<bool>`).
- Major 2.4 (Scratch Power-of-Two Alignment Validation): Added check `(alignment & (alignment - 1)) != 0` returning `nullptr` for non-power-of-two alignments.
- Major 2.5 (Scripting Initializer Lifecycle Wiring): Invoked `sc->initializeModules()` after all active plugins are started in `LifecycleManager::initialize()`.
