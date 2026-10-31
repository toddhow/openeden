# Task 0017: Track W stage W3 — World services on the engine

**Status:** implemented except debug draw and the profiler overlay (see Notes), awaiting toddhow's review. Depends on tasks 0016 (W2) and 0019 (SDK 2.0); its debug-draw part depends on Track V's task 0011 (debug geometry on VSG).
**Design doc section:** `docs/decisions/0003-plugins-do-not-change-physics-or-lua.md`; `docs/design/working-plugin-system.md` (W3); `docs/design/openeden-plugin-system.md` sections 4.4 (the shared Jolt world), 4.5 (world and entities), 4.6 (event bus), 4.10 (diagnostics, debug draw).
**Goal:** plugins see and observe the game world through the engine: they query the game's single Jolt world read-only (rays, shapes, body states), receive physics events, see game objects as stable entity IDs with lifecycle events, receive engine events on the bus, and their debug drawing and profiling show up in the game. Plugins never change physics (decision 0003).
**Workflow:** full loop.

---

## Context (facts this spec relies on)

Cite every engine function again at the commit this task starts from.

- **The game's Jolt world** is `MWPhysics::PhysicsSystem`'s `JPH::PhysicsSystem` (`apps/openmw/mwphysics/physicssystem.{hpp,cpp}`), updated in `PhysicsSystem::stepSimulation` (line 1030) with `mPhysicsSystem.Update(mPhysicsDt, cCollisionSteps, …)` (line 1082); actor simulation runs on worker threads when `[Physics] async num threads` > 0 (`mtphysics.cpp`, `applyQueuedMovements`, line 364). The engine's own queries go through `PhysicsSystem::castRay` and related functions with `MWPhysics::Layers` masks (`joltlayers.hpp`).
- **After task 0019:** the read-only physics query service (`isBodyValid`, `getMotionType`, `readBody`, `readBodies`, `castRay`, `castShape`) attaches only to a world the host owns (`PhysicsServiceImpl`'s external-world constructor), with queries taking host-published opaque masks; physics event types (hits, contact begin and end) are declared in the SDK; there are no fixed-step phases and no collision-layer registration. The SDK speaks meters and the game's world is in Morrowind units (`MWPhysics::PhysicsSystem` sets its gravity from `Constants::UnitsPerMeter`, `physicssystem.cpp`): pass that as `HostImpl::attachPhysicsWorld(world, worldUnitsPerMeter)`, the one conversion boundary. Physics events carry meters too, and `EventBusImpl::publishDeferred` is not synchronized, so Jolt contact callbacks on physics threads must hand their events to the main thread before publishing. Attach and publish masks before plugins start; detach after they stop. `Phase::Ui` resets debug draw and runs before `MWRender::RenderManager::onFrame` (task 0015), so take the debug-draw snapshot before `Ui`.
- **Game objects and cells:** `MWWorld::Scene::loadCell` / `unloadCell` (`apps/openmw/mwworld/scene.cpp`, lines 427 / 364), `addObjectToScene` / `removeObjectFromScene` (lines 1049 / 1064), `changeToInteriorCell` / `changeToExteriorCell` (936 / 1003); deaths and `MWMechanics::Actors::resurrect` (`apps/openmw/mwmechanics/actors.cpp:1767`). Objects are `MWWorld::Ptr`, with persistent `ESM::RefNum`. Physics actors and objects map to Jolt bodies inside `MWPhysics` (`Actor::getPhysicsBody`, `Object::getPhysicsBody`).
- **`EntityManager`** (`entity_manager.hpp`, SDK `entity.hpp`): `EntityId` (32-bit index + 32-bit generation), per-entity plugin data with deleters, `EntityLifecycleEvent` = `Spawned`, `CellLoaded`, `Died`, `Revived`, `CellUnloaded`, `Despawned`.
- **`EventBus`**: immediate and deferred delivery, fixed-size queues, overflow counted; **`DebugDrawService`**: primitive queues with snapshots for a renderer; **`ProfilerService`**: per-plugin scopes aggregated per frame.

## Design

1. **Read-only physics on the game world:** the game constructs the query service attached to `MWPhysics::PhysicsSystem`'s Jolt world. Query masks map to `MWPhysics::Layers` values the host publishes by name (world, door, actor, heightmap, projectile, water, …), so plugins never see bit numbers. Queries are safe to call from the plugin phases on the main thread; with async physics on, they read a consistent state (use the same locking the engine's own `castRay` uses; cite it). Body IDs a plugin can read come from entities (design 2): `EntityId` → the object's body, or no body.
2. **Entities:** map `EntityId` ↔ `MWWorld::Ptr` (keyed by `RefNum` where it exists, by the engine's live reference otherwise); emit `Spawned`/`Despawned` from `addObjectToScene`/`removeObjectFromScene`, `CellLoaded`/`CellUnloaded` from `loadCell`/`unloadCell`, `Died`/`Revived` from the mechanics code that marks an actor dead and `Actors::resurrect`; free per-entity plugin data on despawn. Queries the plugin doc lists (entities in a cell, near a point, by tag; an actor's skeleton, pose and capsule) are implemented for what the engine can answer at the starting commit, the rest listed as follow-ups.
3. **Physics events:** publish the event types declared in task 0019 (hits; contact begin and end for actors and projectiles, as the engine already detects them, cite where) as deferred events, delivered in the next frame phase on the main thread, never from physics worker threads directly.
4. **Engine events on the bus:** publish the lifecycle events above and a short initial set of game events (proposed: game loaded, game saved, cell changed, actor hit) as typed SDK events; deferred queues flush at the phases wired in task 0015. Events the core marks as forwarded follow task 0016's Lua forwarding.
5. **Debug draw and profiler:** draw `DebugDrawService` snapshots through the renderer's debug geometry path built in Track V's task 0011 (toggle: a console command and a setting); show per-plugin profiler scopes in the engine's profiler overlay.

## Constraints

- **Engine physics unchanged:** plugin queries never change the world; every physics result in a scripted movement and combat scenario is identical with and without a querying plugin (Jolt body states compared after N steps); integration tests pass.
- **Single Jolt world:** no second `JPH::PhysicsSystem` is ever created in the game (test).
- **Determinism:** event delivery in load order; a scenario run twice gives identical results, also with plugins discovered in a different order (plugin doc section 8).
- No allocation per frame in entity lookup, event publishing, query dispatch or debug-draw submission.

## Acceptance criteria

- [x] A test plugin casts rays and shapes into the game world and reads the bodies of entities it sees, with correct results against known scene geometry, with async physics on and off.
- [x] No plugin-facing function can change the game's physics (SDK check from task 0019 holds; a test confirms body states are identical with a querying plugin). Compared by the Lua log, including the engine's own ray, with and without the plugin, and all 32 Lua tests with it; Jolt body states were not dumped and compared.
- [x] Physics events reach a test plugin on the main thread in a scripted collision scene. Hit and contact events were seen during the Lua integration tests with the plugin loaded (one hit); queue behaviour is tested in `testpluginworld.cpp`. No dedicated scripted scene: Lua cannot launch a projectile, and actors on the ground give no contact.
- [x] Spawned, despawned, cell loaded and unloaded, died and revived events reach a test plugin in the right order for a scripted scene; per-entity data is freed on despawn (test).
- [x] The listed game events are published and received.
- [ ] Plugin debug drawing appears in game when toggled on; per-plugin profiler scopes appear in the profiler overlay.
- [ ] The determinism and allocation tests pass; all existing gates pass. Done on Windows: build, unit tests, the 32 Lua tests with and without a plugin, `tools/plugin_world_check/check.sh`, ray and shape fractions identical with async physics on and off. Not done: a per-frame allocation test of the bridge, discovery in a different order in the game, Linux `-Werror`, clang-format 14, clang-tidy, sanitizers.

## Out of scope

- Any way for plugins to change physics or step the world (decision 0003: such systems go into the core).
- Packaging, the launcher, and real feature plugins (task 0018).
- The editor's use of `EditorService` (Eden Studio).

## Decisions taken while implementing, for toddhow to confirm

1. **Initial game events:** game loaded, game saved, cell changed, actor hit, plus cell loaded and unloaded by name (`openeden/game_events.hpp`).
2. **Entity queries:** only what the engine answers directly: `playerEntity`, `recordId`, `bodyOfEntity`, `entityOfBody`. By tag, near a point, in a cell, skeleton and pose are follow-ups.
3. **Plugins start in the `Game` state's constructor,** not during engine startup as task 0015 did, because plugins keep the services they get at start and the world only exists by then. So no plugin code runs before content files have loaded, and a plugin cannot take part in loading content.
4. **Threads:** engine hooks can run on a loading worker and contacts on physics threads, so `OMW::PluginWorld` queues them under a lock and delivers on the main thread just before `Phase::Input`. Delivery is immediate rather than through the deferred queues, because a cell load has hundreds of events and those queues hold 64.
5. **Cell lifecycle kinds** reach listeners with an invalid entity ID: a cell is not an entity. `CellLoadedEvent` and `CellUnloadedEvent` carry the name.
6. **`Died` fires when the death starts** (`Result_DeathAnimStarted`), not when its animation ends as Lua's `actorDied` does (`notifyDied`), because an actor can be revived before that.

## Notes for toddhow

- **Not done, on purpose: debug draw and the profiler overlay.** Both need changes in `apps/openmw/mwrender` or the overlay, and this work was told to stay out of the renderer while it changes on `vulkan`. So one acceptance criterion and a part of another stay open, and W3 stays unticked. To do it: give `RenderManager` a third `DebugGeometry` for plugins, fill it from `DebugDrawService::snapshot` before `Phase::Ui`, which clears it, and add the toggle.
- **`Revived` is mostly the player's.** `OpResurrect` on a dead creature or NPC usually resets it by disabling and enabling it (`mwscript/statsextensions.cpp`), so plugins see `Despawned`, then `Spawned`, and a new entity. `Revived` comes from `Actors::resurrect`, as this spec names.
- **Contacts are rare in practice.** Actors are kinematic (`mwphysics/actor.cpp`) and Jolt reports nothing between kinematic and static bodies, so standing on the ground gives no contact. Projectile contacts and hits do arrive.
- **A moved object** keeps its entity only if its reference does: anything the engine moves between cells by making a new reference becomes `Despawned` and `Spawned`.
- **A body made later** (postponed physics) is not picked up: `bodyOfEntity` is read when the object joins the scene.
- **Melee in the scene check:** `tools/plugin_world_check` reports its swing only, since it does not connect reliably. The real check is the Lua integration test `player weapon attack` with the plugin loaded, which gave `actor-hit` events. That run is not scripted in the repository.
- **A plugin calling `destroyEntity` on a game entity** leaves `PluginWorld`'s tables stale until the object goes. Nothing stops it today.
- **SDK version:** these functions were added at the end of the interfaces with the SDK still at 2.0, because W0 to W3 are unreleased on one branch and no plugin was built against an earlier 2.0.
- **`projectile.cpp:118`** takes `mBaseOffset` as its hit point, which is body 1's centre of mass, not a contact point. Inherited, so left alone; it may deserve its own look.
- **Reviewers should look at** the `MWBase::PluginManager` hooks in `mwworld/scene.cpp`, `mwmechanics/actors.cpp` and `LuaManager::onHit`: every one returns at once with no plugin loaded.
