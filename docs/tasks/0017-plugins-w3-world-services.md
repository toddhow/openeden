# Task 0017: Track W stage W3 — World services on the engine

**Status:** draft, awaiting toddhow's approval. Depends on task 0016 (W2); its debug-draw part depends on Track V's task 0011 (debug geometry on VSG).
**Design doc section:** `docs/design/working-plugin-system.md` (W3, decisions 2 and 3); `docs/design/openeden-plugin-system.md` sections 4.3 (fixed steps, threading), 4.4 (the shared Jolt world), 4.5 (world and entities), 4.6 (event bus), 4.10 (diagnostics, debug draw).
**Goal:** plugins act in the game world through the engine: they create bodies and constraints in the game's single Jolt world with their own named collision layers, run in every physics step, see game objects as stable entity IDs with lifecycle events, receive engine events on the bus, and their debug drawing and profiling show up in the game.
**Workflow:** full loop.

---

## Context (facts this spec relies on)

Cite every engine function again at the commit this task starts from.

- **The game's Jolt world** is `MWPhysics::PhysicsSystem`'s `JPH::PhysicsSystem` (`apps/openmw/mwphysics/physicssystem.{hpp,cpp}`), updated in `PhysicsSystem::stepSimulation` (line 1030) with `mPhysicsSystem.Update(mPhysicsDt, cCollisionSteps, …)` (line 1082); actor simulation steps are scheduled by `PhysicsTaskScheduler::applyQueuedMovements` (`mtphysics.cpp:364`) with the step count from `calculateStepConfig` (line 321 onwards), on worker threads when `[Physics] async num threads` > 0.
- **`PhysicsServiceImpl`** (`components/plugin/physics_service.{hpp,cpp}`) can attach to an external `JPH::PhysicsSystem` (constructor taking `JPH::PhysicsSystem* externalSystem`); in internal mode it uses its own broadphase map and filters (`BroadPhaseLayerMap`, `ObjectVsBroadPhaseFilter`, `ObjectPairFilter`, lines 105–137).
- **Engine collision layers** are bit flags in `MWPhysics::Layers` (`apps/openmw/mwphysics/joltlayers.hpp`): `WORLD` (bit 0) to `SENSOR` (bit 10); broadphase and pair filtering in the same file (`JoltBPLayerInterface`, `JoltObjectVsBroadPhaseLayerFilter`, `JoltObjectLayerPairFilter`). Jolt's object layer is 16 bits in this build (`OBJECT_LAYER_BITS` off; Jolt `Physics/Collision/ObjectLayer.h` defaults to 16), so bits 11–15 are free.
- **`CollisionLayerRegistry`** (`collision_layer_registry.hpp`): up to 32 named layers, one bit each, assigned from 0, with a symmetric pair table.
- **Game objects and cells:** `MWWorld::Scene::loadCell` / `unloadCell` (`apps/openmw/mwworld/scene.cpp`, lines 427 / 364), `addObjectToScene` / `removeObjectFromScene` (lines 1049 / 1064), `changeToInteriorCell` / `changeToExteriorCell` (936 / 1003); deaths and `MWMechanics::Actors::resurrect` (`apps/openmw/mwmechanics/actors.cpp:1767`). Objects are `MWWorld::Ptr`, with persistent `ESM::RefNum`.
- **`EntityManager`** (`entity_manager.hpp`, SDK `entity.hpp`): `EntityId` (32-bit index + 32-bit generation), per-entity plugin data with deleters, `EntityLifecycleEvent` = `Spawned`, `CellLoaded`, `Died`, `Revived`, `CellUnloaded`, `Despawned`.
- **`EventBus`**: immediate and deferred delivery, fixed-size queues, overflow counted; **`DebugDrawService`**: primitive queues with snapshots for a renderer; **`ProfilerService`**: per-plugin scopes aggregated per frame.

## Design

1. **Collision layers** per decision 2 of the plan: build Jolt with 32-bit object layers (proposed), reserve bits 0–10 (and any future engine layers, with a named constant for the first free bit) for the engine, and allocate plugin layers from the first free bit up; the engine's broadphase and pair filters consult the registry for plugin bits. Every existing physics query keeps its result for engine layers (test).
2. **Physics service attached:** the game constructs `PhysicsServiceImpl` with the game's `JPH::PhysicsSystem`; plugin bodies get object layers from the registry; plugin body creation and removal are synchronised with the physics threads (the service documents which thread may call which function); plugin bodies are removed when the plugin stops.
3. **Fixed-step callbacks** per decision 3: `FixedStepPrePhysics` and `FixedStepPostPhysics` run in every physics step around Jolt's `Update`, with `dt` and the step index, in plugin load order; with non-parallel callbacks registered, steps run synchronously (or the alternative toddhow chooses). Zero registered fixed-step callbacks change nothing about stepping or threading.
4. **Entities:** map `EntityId` ↔ `MWWorld::Ptr` (keyed by `RefNum` where it exists, by the engine's live reference otherwise); emit `Spawned`/`Despawned` from `addObjectToScene`/`removeObjectFromScene`, `CellLoaded`/`CellUnloaded` from `loadCell`/`unloadCell`, `Died`/`Revived` from the mechanics code that marks an actor dead and `Actors::resurrect`; free per-entity plugin data on despawn. Queries the plugin doc lists (entities in a cell, near a point, by tag; an actor's skeleton, pose and capsule) are implemented for what the engine can answer at the starting commit, the rest listed as follow-ups.
5. **Engine events on the bus:** publish the lifecycle events above and a short initial set of game events (proposed: game loaded, game saved, cell changed, actor hit) as typed SDK events; deferred queues flush at the phases already wired in task 0015.
6. **Debug draw and profiler:** draw `DebugDrawService` snapshots through the renderer's debug geometry path built in Track V's task 0011 (toggle: a console command and a setting); show per-plugin profiler scopes in the engine's profiler overlay.

## Constraints

- **Engine physics unchanged:** with no plugin bodies, every physics result is identical (scripted movement and combat scenario, Jolt body states compared after N steps); integration tests pass.
- **Single Jolt world:** no second `JPH::PhysicsSystem` is ever created in the game (test).
- **Determinism:** plugin fixed-step callbacks and event delivery in load order; a scenario run twice gives identical results, also with plugins discovered in a different order (plugin doc section 8).
- No allocation per step or frame in callback dispatch, entity lookup, event publishing or debug-draw submission.

## Acceptance criteria

- [ ] With 32-bit object layers (or the chosen alternative), engine layer behaviour is unchanged (test) and a plugin's named layers get bits that never overlap the engine's.
- [ ] A test plugin creates a body in the game world, Morrowind's raycasts and actors collide with it according to its layers, and it is removed when the plugin stops.
- [ ] `FixedStepPrePhysics` and `FixedStepPostPhysics` run once per physics step with the right `dt` and step index, with async physics on and off.
- [ ] Spawned, despawned, cell loaded and unloaded, died and revived events reach a test plugin in the right order for a scripted scene; per-entity data is freed on despawn (test).
- [ ] The listed game events are published and received.
- [ ] Plugin debug drawing appears in game when toggled on; per-plugin profiler scopes appear in the profiler overlay.
- [ ] The determinism and allocation tests pass; all existing gates pass.

## Out of scope

- Packaging, the launcher, and real feature plugins (task 0018).
- The editor's use of `EditorService` (Eden Studio).

## Open questions for toddhow

1. **Initial game events:** game loaded, game saved, cell changed, actor hit (proposed); anything else plugins need from day one?
2. **Entity queries:** implement only what the engine answers directly now (proposed), or design the full query set (by tag, near a point) first?
