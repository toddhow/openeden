# Task 0015: Track W stage W1 — The plugin host runs in the game

**Status:** implemented, awaiting toddhow's review (started at toddhow's request on 2026-10-30, alongside Track V's V4). Depends on task 0019 (SDK 2.0). Scheduling per `docs/design/working-plugin-system.md`, decision 1 (proposed: after Track V's task 0010).
**Design doc section:** `docs/design/working-plugin-system.md` (sections 1–3, W1); `docs/design/openeden-plugin-system.md` sections 4.3 (phases), 6 (registration and loading), 6.8 (failure handling).
**Goal:** `openmw` creates the plugin host at startup, discovers and loads enabled plugins, runs register → resolve → start, calls the frame phases from the engine loop, bridges plugin logging to the engine log, and stops and unloads plugins at shutdown, with zero plugins costing nothing and changing nothing.
**Workflow:** full loop.

---

## Context (facts this spec relies on)

- **The host exists only in tests:** `HostImpl` (`components/plugin/host_impl.{hpp,cpp}`) and `LifecycleManager` (`lifecycle_manager.{hpp,cpp}`) are created in `apps/components_tests/plugin/` only; nothing in `apps/openmw` includes `components/plugin`.
- **Loader pieces exist:** `discoverPluginDirectories(searchDirs)`, `filterEnabledPlugins(discovered, enabledList)` ("if `enabledList` is empty, all discovered plugins are returned") and `loadPluginLibrary(discovered, hostInfo)` (`components/plugin/plugin_loader.hpp`); manifest parsing with located errors (`manifest_io.hpp`); deterministic order (`dependency_resolver.hpp`).
- **Engine startup and loop on the OSG engine** (OpenEden `442eec6e2f`; after Track V's task 0009 the equivalents are in vsgopenmw's `apps/openmw/vsgengine.cpp`, `game.hpp`, `boot.hpp`, and must be cited at the commit this task starts from):
  - `OMW::Engine::prepareEngine()` (`apps/openmw/engine.cpp`, line 723) builds the VFS, resource system, Lua, sound, world and window manager;
  - `OMW::Engine::go()` (line 934) runs the main loop and calls `Settings::Manager::saveUser` at the end (line 1075);
  - `OMW::Engine::frame()` (line 190) per frame: input update (`mInputManager->update`), sound, `mLuaManager->synchronizedUpdate()`, `mStateManager->update`, time advance, `mMechanicsManager->update`, `mWorld->updatePhysics(...)`, `mWorld->update`, `mWindowManager->update`, then `mViewer->eventTraversal()`, `updateTraversal()`, `mLuaWorker->allowUpdate`, `renderingTraversals()`, `mLuaWorker->finishUpdate`.
- **The phases** (plugin doc 4.3, `sdk/include/openeden/phases.hpp`), after task 0019 removes the fixed-step phases (decision 0003): `Input`, `PreSimulation`, `PostSimulation`, `Animation`, `PreRender`, `Ui`. This task wires all of them.
- **Engine logging:** `Debug::setLogListener(LogListener)` (`components/debug/debugging.hpp:28`) receives every engine log line; `LoggingService` (`components/plugin/logging_service.hpp`) has `addSink`/`removeSink`.

## Design

- **Ownership:** a new `OMW::PluginHost` (name proposed, `apps/openmw/pluginhost.{hpp,cpp}`) owned by the engine object, holding `HostImpl` and `LifecycleManager`. Created after settings and the configuration are loaded, before the VFS is built (task 0016 mounts plugin data into it); destroyed after the world and before the VFS and logging are torn down.
- **Discovery:** search directories in order: the install's `plugins/` beside the executable, then `<user data>/plugins/` (`Files::ConfigurationManager`'s user data path). Enable list: `plugin=<id>` lines in `openmw.cfg`, read through the existing config machinery like `content=` (decision 5 in the plan; adjust if toddhow decides otherwise). Installed but not enabled plugins are listed in the log, never loaded.
- **Startup sequence:** discover → read manifests → resolve order → load libraries and handshake → `registerServices` → `resolve` → `start`, exactly as plugin doc section 6, using the existing `components/plugin` code. Every failure is logged with plugin, stage and reason; a failed plugin and its dependants are disabled; the game continues (plugin doc 6.8).
- **Frame phases in the loop:** call `LifecycleManager::runPhase` at these points (cite the exact lines at the starting commit): `Input` right after input is updated; `PreSimulation` before mechanics update; `PostSimulation` after the world and physics update; `Animation` after mechanics' animation update and before the scene traversal; `PreRender` before rendering; `Ui` after the window manager update. Each call is skipped entirely when no plugin registered a callback for that phase.
- **Paused game:** phases run when paused exactly as the engine's own systems do in the same place (input and UI run, simulation does not); document which phases run when paused.
- **Logging:** plugin log lines go to the engine log with the `[pluginId]` prefix the service already adds; engine log lines reach plugin sinks through `Debug::setLogListener`, chained so the existing listener (the in-game log viewer, `apps/openmw/mwgui/debugwindow.cpp`) still receives everything.
- **Shutdown:** `stop` in reverse load order, destroy plugin objects, then unload libraries (already implemented in `LifecycleManager`), before world teardown.
- **Command line:** `--no-plugins` disables loading for debugging; `--plugin=<id>` adds to the enable list for one run (proposed).

## Inputs / outputs

- `apps/openmw/pluginhost.{hpp,cpp}`, engine changes, option parsing, `openmw.cfg` key `plugin`.
- A test plugin target under `apps/components_tests/plugin/` or `apps/openmw_tests/` built as a shared library that records which phases it saw, for the integration check.
- `apps/openmw/CLAUDE.md` (create) with the plugin host's place in startup, the loop and shutdown.

## Constraints

- **Zero plugins, zero change:** with no plugin enabled the game's behaviour is identical (Lua integration tests, scripted gameplay comparison) and the loop adds no allocation and no measurable frame time (measured on toddhow's PC with the profiler overlay; the allocation counter in a test).
- No allocation per frame in phase dispatch (already a property of `runPhase`; keep it).
- No exception crosses into the engine (already contained in `LifecycleManager`; keep it).
- The headless test host behaviour is unchanged.

## Acceptance criteria

- [x] With a test plugin installed and enabled, `openmw` logs its discovery, load, register, resolve and start, calls every frame phase in the documented order each frame, and stops and unloads it at exit; an automated check (Lua integration test or `openmw-tests`) reads the order the plugin recorded.
- [x] A plugin that is installed but not enabled is listed and not loaded; `--no-plugins` loads none.
- [x] A broken manifest, a missing library, a failed handshake and an exception thrown in `start` or in a phase callback each disable only that plugin and its dependants, with the reason in the log; the game keeps running (one test per case).
- [x] Plugin log lines appear in `openmw.log` with their prefix; a plugin sink receives engine log lines; the in-game log viewer still works (its listener slot is tested; the viewer's window was not opened).
- [ ] With zero plugins: the 32 Lua integration tests pass unchanged, no allocation is added per frame (test), and frame time is unchanged within measurement noise. Done on Windows: the Lua tests and the allocation test. Frame time not measured on toddhow's PC; with zero plugins each phase call is one flag check.
- [ ] All existing gates pass. Done on Windows: build, `components-tests`, `openmw-tests`, Lua integration tests. Not run: Linux `-Werror`, clang-format 14, clang-tidy, sanitizers.

## Out of scope

- Engine-backed VFS, settings, saves and Lua event forwarding (task 0016); read-only physics, entities, events, debug draw and profiler display (task 0017); the launcher and packaging (task 0018).
- Hot reload (plugin doc 6.9: not in the first version).

## Decisions taken while implementing, for toddhow to confirm

1. **Enable list:** `plugin=<id>` in `openmw.cfg`, as proposed.
2. **Command-line switches:** `--no-plugins` and `--plugin=<id>` added, as proposed.
3. **The engine this was built on:** Track V replaced the loop this spec cites, so the phases are in `Engine::frame` (`vsgengine.cpp`) and `Game::run` (`game.hpp`); `apps/openmw/CLAUDE.md` has the table.
4. **Skipping a phase:** the spec skips a call when no plugin registered for that phase. The host skips only when no plugin is loaded, because `runPhase` also flushes that phase's deferred events and `Ui` does end-of-frame resets.
5. **Starting plugins:** libraries load before the VFS is built, as the spec says, but `start` runs after it, so that a plugin's `start` can read the game's VFS in task 0016.
6. **Faults at runtime:** a plugin that throws now also disables its dependants, in `start` and in phases, and the services stop calling it (events, entity listeners, save handlers, editor extensions). Its format readers and log sinks carry no plugin ID, so they stay until shutdown. Before this, only the plugin's phase callbacks stopped.
7. **Debug:** `Debug::setExtraLogListener` is new, because the in-game log viewer replaces the listener `setLogListener` holds.

## Notes for toddhow

- **One abnormal game run, cause unknown.** During the check of an installed but not enabled plugin (2026-10-30, 23:57, no plugin loaded), `openmw.exe` ended with exit status 127 about a second after `Context::record() ... Need to implement copying of data` was logged, without reaching "Quitting peacefully". Runtime libraries came only from `build-w0` and `deps`, but another session's game may have been running on the same GPU. An `openmw-crash.dmp` from that run's config folder may have been overwritten by a later, unrelated crash (the sink recursion fixed in this task), so it was not read. Four further runs with no plugin loaded and two with one exited normally, as did the later check runs. No plugin code ran, and the host does nothing but discovery in that case, but nothing shows the cause is outside this change.
- **Tests edited by the implementer:** a test of leftover sinks (`sinkLeftByAPluginWhoseStartThrewIsDroppedBeforeItsLibraryUnloads`) and two test plugin modes were added after review; `runPhaseAllocatesNothingWithZeroPlugins` was appended to `apps/openmw_tests/mwrender/testbonepose.cpp`, which owns the allocation counter. No test was changed or removed.
- **No direct test:** the services no longer calling a faulted plugin (`HostImpl::disablePlugin`), and the in-game log viewer's window.
