# Working Plugin System: Plan (Track W)

**Status:** plan, awaiting toddhow's approval · **Date:** 2026-10-28 · **Specs:** tasks 0015 to 0018
**Design:** `docs/design/openeden-plugin-system.md` (what the plugin system is); this document covers how it starts working inside the game.

## 1. Where it stands

Tasks 0001 to 0005 built the plugin system as a complete, tested **model**: the SDK (`sdk/include/openeden/`), and in `components/plugin/` the manifest reader, dependency resolver, registry, lifecycle, dynamic loading and every core service. Its tests run in a test host (`apps/components_tests/plugin/`, 21 test files).

None of it runs in the game:

- `apps/openmw` never creates a `HostImpl` or `LifecycleManager`; only tests do. No plugin, including the Morrowind compatibility pack, is ever loaded by `openmw`.
- `components/plugin` includes nothing from the engine except Jolt. Each service is a standalone model, not a bridge to the engine subsystem it is named after:

| Service | What it is today | What "working" needs |
| --- | --- | --- |
| Lifecycle, phases (`LifecycleManager::runPhase`, `runFixedStepPhase`) | Called by tests | Called by the engine's frame loop and physics steps |
| `LoggingService` | Own sinks | Bridged both ways with `Debug::Log` / `Debug::setLogListener` (`components/debug/debugging.hpp`) |
| `VfsService` | Own mount table | Plugin `data/` folders mounted in the engine's `VFS::Manager` in load order (built in `OMW::Engine::prepareEngine`, `apps/openmw/engine.cpp`) |
| `SettingsService` | Own namespaces with manifest defaults | `[plugin.<id>]` sections in `settings.cfg` through `Settings::Manager`, saved with the user's settings |
| `SaveService` | Own chunk format, unread-chunk preservation | Plugin chunks inside `.omwsave` files as a new ESM record, written and read by `MWState::StateManager` |
| `ScriptingService` | Registration callbacks into an abstract `LuaTable` | Tables in the real Lua host (`MWLua::LuaManager`, `LuaUtil::LuaState::addCommonPackage`) as `openeden.<id>` |
| `PhysicsServiceImpl` | Owns a Jolt world in tests; *can* attach to an external one ("owned by `MWPhysics::PhysicsSystem`", `physics_service.hpp`) | Attached to the game's single Jolt world, with the engine's collision filtering |
| `CollisionLayerRegistry` | Up to 32 named layers, bits from 0 | Bits that do not collide with the engine's layers (see section 4) |
| `EntityManager` | Own IDs | IDs mapped to game objects; lifecycle events (`Spawned`, `CellLoaded`, `Died`, `Revived`, `CellUnloaded`, `Despawned`, `sdk/include/openeden/entity.hpp`) emitted by the engine |
| `EventBus` | Works in isolation | Engine events published; deferred queues flushed at the engine's phases |
| `DebugDrawService` | Snapshots nothing reads | Drawn by the renderer |
| `ProfilerService` | Own timings | Shown with the engine's profiler overlay |
| `EditorService` | Inert | Used by the editor (Eden Studio), not the game |

The guide's P3 to P5 checkboxes describe the model, and their wording ("wired into the engine loop", "CreativeMotion loads as a plugin") overstates it. The guide is corrected when this plan is approved.

## 2. Goal

With a plugin enabled, the game loads it at startup, gives it the real engine services, calls it in the documented phases and physics steps, saves and loads its data with the player's game, exposes its Lua table to mods, and unloads it at shutdown. With no plugin enabled, the game behaves and performs exactly as without the plugin system.

## 3. Stages

| Stage | Spec | Delivers |
| --- | --- | --- |
| W1 | task 0015 | The host in the game: discovery, enable list, load, lifecycle, shutdown; logging bridge; frame phases called from the engine loop; zero-plugin cost and behaviour unchanged |
| W2 | task 0016 | Data services on the engine: VFS mounts and format readers, settings in `settings.cfg`, save chunks in `.omwsave`, Lua tables for mods |
| W3 | task 0017 | World services on the engine: the physics service on the game's Jolt world with collision layers, fixed-step callbacks, entities mapped to game objects with lifecycle events, engine events on the bus, debug draw and profiler shown |
| W4 | task 0018 | Real clients and packaging: the Morrowind compatibility pack loads in the game, a sample plugin exercises every service end to end, install layout, launcher plugin list, failure reporting |

Each stage ends with the game building, every gate passing, and a test proving the stage's services work through a plugin, not only in the test host.

## 4. Decisions this plan needs

1. **Timing against Track V.** Decision 0002 makes Track V the next major track; Track V's V1 replaces `engine.cpp` with vsgopenmw's `vsgengine.cpp` and its redesigned `apps/openmw/mwstate` (25 new files). The frame loop, save code and startup are exactly what Track W changes. Proposed: **start Track W after Track V's V2** (task 0010), when the engine loop and save flow are settled, and run it alongside V3 to V6 (it touches different files from V3's rendering work, except the debug draw in W3, which follows V3's debug-draw path). Alternative: do W1 and W2 now on the OSG engine and let task 0009's merge carry them across (more conflicts in V1).
2. **Collision layer bits.** This build uses 16-bit Jolt object layers (`OBJECT_LAYER_BITS` off, so Jolt's default 16 applies, `Jolt/Physics/Collision/ObjectLayer.h`), and the engine already uses bits 0 to 10 as flags (`apps/openmw/mwphysics/joltlayers.hpp`). Plugins would get 5 bits. Proposed: build Jolt with 32-bit object layers and give plugins bits 11 to 31 (21 layers), with the registry's bit allocation starting after the engine's. Alternative: change the engine's layers from flags to indices with a collision table (larger change, touches all physics queries).
3. **Fixed-step callbacks and async physics.** OpenEden runs actor physics on worker threads when `[Physics] async num threads` is above 0 (`apps/openmw/mwphysics/mtphysics.cpp`, `applyQueuedMovements`, steps computed by `calculateStepConfig`). The plugin design says fixed-step callbacks run on the main simulation thread unless registered as parallel. Proposed: plugin fixed-step callbacks run inside each physics step on the thread that runs the step, after actor simulation and before Jolt's `Update` (`FixedStepPrePhysics`) and after it (`FixedStepPostPhysics`), with the engine joining worker threads around them; registering any non-parallel fixed-step callback forces the step to run synchronously. Alternative: run all of a frame's plugin fixed steps on the main thread before or after the physics batch (simpler, loses true per-step interleaving).
4. **Lua contexts.** OpenMW's Lua has global, local, player and menu scripts with different permissions. Proposed: `openeden.<id>` is available in global and player contexts by default, and a plugin declares in its manifest if it also wants local or menu scripts.
5. **Enable list.** Proposed: `plugin=<id>` lines in `openmw.cfg` (like `content=`), written by the launcher; discovery from the install's `plugins/` and the user data folder's `plugins/`.

## 5. Risks

- **Behaviour without plugins.** Every hook is a no-op with zero plugins, measured: no allocation, no measurable frame time (gate in every stage).
- **Threading.** Physics steps and Lua run on worker threads (`mLuaWorker`, async physics); every service callable from a plugin states which thread may call it, and a debug build asserts it.
- **Save compatibility.** A save with plugin chunks must load in a build without the plugin (chunks preserved) and in OpenMW-format readers that do not know the record (skipped); tested both ways.
- **Determinism.** Plugin order, phase order and event order stay fixed (plugin doc 6.3); a test runs a scenario twice with plugins discovered in different orders.
