# Working Extension System: Plan (Track W)

**Status:** plan, awaiting toddhow's approval · **Date:** 2026-10-28 · **Specs:** tasks 0019 and 0015 to 0018
**Design:** `docs/design/openeden-extension-system.md` (what the extension system is); this document covers how it starts working inside the game. **Decision 0003** (2026-10-28) applies throughout: extensions read physics but never change it, have no fixed-step phases and no Lua API, and may publish events the core forwards to Lua.

## 1. Where it stands

Tasks 0001 to 0005 built the extension system as a complete, tested **model**: the SDK (`sdk/include/openeden/`), and in `components/extension/` the manifest reader, dependency resolver, registry, lifecycle, dynamic loading and every core service. Its tests run in a test host (`apps/components_tests/extension/`, 21 test files).

None of it runs in the game:

- `apps/openmw` never creates a `HostImpl` or `LifecycleManager`; only tests do. No extension, including the Morrowind compatibility pack, is ever loaded by `openmw`.
- `components/extension` includes nothing from the engine except Jolt. Each service is a standalone model, not a bridge to the engine subsystem it is named after:

| Service | What it is today | What "working" needs (after decision 0003) |
| --- | --- | --- |
| Lifecycle, phases (`LifecycleManager::runPhase`) | Called by tests | Frame phases called by the engine's frame loop; the fixed-step phases are removed (task 0019) |
| `LoggingService` | Own sinks | Bridged both ways with `Debug::Log` / `Debug::setLogListener` (`components/debug/debugging.hpp`) |
| `VfsService` | Own mount table | Extension `data/` folders mounted in the engine's `VFS::Manager` in load order (built in `OMW::Engine::prepareEngine`, `apps/openmw/engine.cpp`) |
| `SettingsService` | Own namespaces with manifest defaults | `[extension.<id>]` sections in `settings.cfg` through `Settings::Manager`, saved with the user's settings |
| `SaveService` | Own chunk format, unread-chunk preservation | **Removed** (decision 0005): extensions do not affect save data |
| `ScriptingService` | Registration callbacks into an abstract `LuaTable` | **Removed** (decision 0003, task 0019); replaced by extension events forwarded to Lua by the core (task 0016) |
| `PhysicsServiceImpl` | Owns a Jolt world in tests, full read and write API; *can* attach to an external one | **Read-only** (task 0019), attached to the game's single Jolt world (task 0017) |
| `CollisionLayerRegistry` | Up to 32 named layers, bits from 0 | **Removed** (decision 0003, task 0019) |
| `EntityManager` | Own IDs | IDs mapped to game objects; lifecycle events (`Spawned`, `CellLoaded`, `Died`, `Revived`, `CellUnloaded`, `Despawned`, `sdk/include/openeden/entity.hpp`) emitted by the engine |
| `EventBus` | Works in isolation | Engine events (including physics events) published; deferred queues flushed at the engine's phases; marked events forwarded to Lua |
| `DebugDrawService` | Snapshots nothing reads | Drawn by the renderer |
| `ProfilerService` | Own timings | Shown with the engine's profiler overlay |
| `EditorService` | Inert | Used by the editor (Eden Studio), not the game |

The guide's P3 to P5 wording has been corrected to say this work runs in the test host.

## 2. Goal

With an extension enabled, the game loads it at startup, gives it the real engine services, calls it in the documented frame phases, lets it query the game's physics, forwards the events it marks to Lua scripts, and unloads it at shutdown. With no extension enabled, the game behaves and performs exactly as without the extension system.

## 3. Stages

| Stage | Spec | Delivers |
| --- | --- | --- |
| W0 | task 0019 | SDK 2.0 per decision 0003: physics read-only with shape casts, collision layers, fixed-step phases and Lua removed, events markable for Lua forwarding. Independent of Track V, can run now |
| W1 | task 0015 | The host in the game: discovery, enable list, load, lifecycle, shutdown; logging bridge; frame phases called from the engine loop; zero-extension cost and behaviour unchanged |
| W2 | task 0016 | Data services on the engine: VFS mounts and format readers, settings in `settings.cfg`, extension events forwarded to Lua (save chunks built, then removed by decision 0005) |
| W3 | task 0017 | World services on the engine: the read-only physics service on the game's Jolt world, physics events, entities mapped to game objects with lifecycle events, engine events on the bus, debug draw and profiler shown |
| W4 | task 0018 | Real clients and packaging: the Morrowind compatibility pack loads in the game, a sample extension exercises every service end to end, install layout, launcher extension list, failure reporting |

Each stage ends with the game building, every gate passing, and a test proving the stage's services work through an extension, not only in the test host.

## 4. Decisions

1. **Timing against Track V.** Decision 0002 makes Track V the next major track; Track V's V1 replaces `engine.cpp` with vsgopenmw's `vsgengine.cpp` and its redesigned `apps/openmw/mwstate` (25 new files). The frame loop, save code and startup are exactly what W1–W3 change. Proposed: **W0 now; W1 onward after Track V's V2** (task 0010), run alongside V3 to V6 (they touch different files from V3's rendering work, except the debug draw in W3, which follows V3's debug-draw path). Alternative: do W1 and W2 now on the OSG engine and let task 0009's merge carry them across (more conflicts in V1).
2. ~~Collision layer bits.~~ Resolved by decision 0003: extensions register no collision layers.
3. ~~Fixed-step callbacks and async physics.~~ Resolved by decision 0003: no fixed-step phases for extensions.
4. ~~Lua contexts.~~ Replaced by decision 0003: no extension Lua tables. Still to decide in task 0016: which Lua contexts receive forwarded extension events (proposed: global scripts, as OpenMW's global events, with the global script free to pass them on).
5. **Enable list.** Proposed: `extension=<id>` lines in `openmw.cfg` (like `content=`), written by the launcher; discovery from the install's `extensions/` and the user data folder's `extensions/`.

## 5. Risks

- **Behaviour without extensions.** Every hook is a no-op with zero extensions, measured: no allocation, no measurable frame time (gate in every stage).
- **Threading.** Physics runs on worker threads when async physics is on, and Lua on its own worker (`mLuaWorker`); physics queries and event forwarding state which thread may call them, and a debug build asserts it.
- **Save compatibility.** Extensions have no part in saves (decision 0005), so a save does not depend on which are installed.
- **Determinism.** Extension order, phase order and event order stay fixed (extension doc 6.3); a test runs a scenario twice with extensions discovered in different orders.
