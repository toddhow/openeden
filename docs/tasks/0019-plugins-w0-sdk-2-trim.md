# Task 0019: Track W stage W0 — SDK 2.0: plugins read physics, never change it, and have no Lua API

**Status:** implemented on branch `claude/w0-sdk-2-794529`, awaiting toddhow's review. No dependency on Track V: it touches only `sdk/`, `components/plugin/`, plugin tests and documents, none of which vsgopenmw's import (task 0009) changes, so it can run now.
**Design doc section:** `docs/decisions/0003-plugins-do-not-change-physics-or-lua.md`; `docs/design/openeden-plugin-system.md` 4.2 (versioning), 4.3, 4.4, 4.6, 4.10.
**Goal:** bring the SDK and its implementation in line with decision 0003: remove the physics write operations, collision-layer registration, the fixed-step phases and the Lua scripting service; keep a read-only physics query service and the event bus; add a way for plugin events to be marked for forwarding to Lua (the forwarding itself is engine work in task 0016); bump the SDK to 2.0.
**Workflow:** full loop (the SDK and the plugin loader are on the guide's full-loop list).

---

## Context (facts this spec relies on)

All at OpenEden `459bd661f4` (or the commit this task starts from).

- **SDK version:** `openeden::sdk::kSdkVersion{ 1, 0 }` (`sdk/include/openeden/version.hpp:66`); the loader refuses a plugin built for another major (plugin doc 4.1).
- **Phases:** `enum class Phase` in `sdk/include/openeden/phases.hpp` lists `Input`, `PreSimulation`, `FixedStepPrePhysics`, `FixedStepPostPhysics`, `PostSimulation`, `Animation`, `PreRender`, `Ui`; `FixedStepPhaseCallback(float dt, std::uint64_t stepIndex)` in the same file; `Registrar::registerPhaseCallback(Phase, FixedStepPhaseCallback, bool parallel)` in `registrar.hpp:46`; `LifecycleManager::runFixedStepPhase` in `components/plugin/lifecycle_manager.{hpp,cpp}`.
- **Physics service** (`sdk/include/openeden/physics.hpp`), lines 193–245: write operations `createBody`, `destroyBody`, `createJoint`, `destroyJoint`, `createCollisionGroup`, `setSubGroupsCollide`, `destroyCollisionGroup`, `registerCollisionLayer`, `declareCollisionPair`, `setJointMotor`, `setJointPositionSteps`, `setJointTarget` (two overloads), `applyImpulse`, `setVelocity`, `setBodyDamping`, `setKinematicTarget`, `setMotionType`, `step`; read operations `isBodyValid`, `isConstraintValid`, `getMotionType`, `readBody`, `readBodies`, `castRay`, `fixedStepSeconds`. Implemented by `PhysicsServiceImpl` (`components/plugin/physics_service.{hpp,cpp}`), which can own a Jolt world or attach to an external one.
- **Collision layers:** `CollisionLayerRegistry` (`components/plugin/collision_layer_registry.{hpp,cpp}`).
- **Lua:** `ScriptingService` and `LuaTable` (`sdk/include/openeden/scripting.hpp`), `components/plugin/scripting_service.{hpp,cpp}`, `Host::getScriptingService()` (`sdk/include/openeden/host.hpp:54`), wired through `host_impl`, `lifecycle_manager` and `service_registry`.
- **Tests that use the removed API** (`apps/components_tests/plugin/`): `test_physics_service.cpp` (64 uses), `test_scripting_service.cpp` (21), `test_collision_layers.cpp` (16), `test_empty_host.cpp` (5), `test_lifecycle.cpp` (4), `test_event_bus.cpp` (4), `test_compat_morrowind.cpp` (1).
- **Unrelated look-alikes, not part of this task:** CreativeMotion's own `opas::physics::PhysicsWorld` (`components/creativemotion/physics/…`) and engine functions with the same names in `apps/openmw/mwphysics`, `mwworld/projectilemanager.cpp`, `mwsound`, `mwrender/sky*`.

## Design

1. **Version:** `kSdkVersion` becomes `{ 2, 0 }`. Manifests declaring SDK 1.x are refused with a clear message ("built for SDK 1.x; this host provides 2.0"). Update `plugins/compat_morrowind/plugin.json` and any test manifests to `"sdk": {"major": 2, "minor": 0}`.
2. **Phases:** remove `FixedStepPrePhysics`, `FixedStepPostPhysics`, `FixedStepPhaseCallback`, the fixed-step `registerPhaseCallback` overload and `LifecycleManager::runFixedStepPhase`. The remaining phases keep their order: `Input`, `PreSimulation`, `PostSimulation`, `Animation`, `PreRender`, `Ui`.
3. **Physics:** replace `PhysicsService` with a read-only `PhysicsQueryService` (name proposed) containing `isBodyValid`, `getMotionType`, `readBody`, `readBodies`, `castRay`, plus `castShape` (sphere, capsule and box casts, matching what `castRay` returns) for the "shape casts" decision 0003 allows. Remove every write operation, collision groups, `isConstraintValid` and joint types, `step`, `fixedStepSeconds`, and the internal-world mode of `PhysicsServiceImpl`: it only ever attaches to a world the host owns. In the test host, tests create a small Jolt world themselves and attach the service to it. Delete `CollisionLayerRegistry`; queries take an engine-defined query mask (documented in the SDK as opaque values the host publishes, so plugins never assume bit numbers).
4. **Lua:** delete `ScriptingService`, `LuaTable`, `scripting.hpp`, `scripting_service.*` and `Host::getScriptingService()`. Add to the event bus the ability to mark an event type as **forwarded to Lua** at registration (event name and a flat payload of numbers, strings and booleans, so the core can turn it into a Lua event without plugin code running in Lua). The forwarding is implemented by the engine in task 0016; here, the SDK declares it and the test host records which events are marked.
5. **Physics events:** document in the SDK which physics events plugins will receive on the bus (hits, contact begin and end), published by the engine in task 0017; this task adds only their typed declarations.
6. **Tests:** delete `test_scripting_service.cpp` and `test_collision_layers.cpp`; reduce `test_physics_service.cpp` to the read-only service attached to a test-created world (keep every read and cast test, delete write tests, add shape-cast tests); update `test_empty_host`, `test_lifecycle`, `test_event_bus` and `test_compat_morrowind` for the removed phases and services; add tests for SDK 1.x refusal and for Lua-forwarded event marking.
7. **Documents:** update `components/plugin/CLAUDE.md`, `docs/design/plugin-implementation-guide.md` and the SDK header comments; the plugin design doc and Track W documents are updated together with decision 0003 (already done when this task starts, see that commit).

## Constraints

- **No engine behaviour changes:** this task touches only `sdk/`, `components/plugin/`, `plugins/` manifests, plugin tests and documents.
- Determinism, no allocation in phase dispatch and event publishing, and exception containment all stay as tested today.
- Keep `PhysicsServiceImpl`'s attach-to-external-world path working: task 0017 uses it with the game's world.

## Acceptance criteria

- [x] `kSdkVersion` is 2.0; a plugin built for SDK 1.x is refused with a clear reason (test).
- [x] No SDK header declares a physics write operation, collision-layer registration, a fixed-step phase or callback, or anything Lua; a CI grep check enforces it.
- [x] The read-only physics service reads bodies, casts rays and casts shapes against an attached world (tests), and cannot create its own world.
- [x] Event types can be marked as forwarded to Lua with a flat payload; the test host records the marking (test).
- [x] `components-tests` pass with the deleted tests listed in the pull request and every remaining plugin test passing.
- [x] `components/plugin/CLAUDE.md` and `docs/design/plugin-implementation-guide.md` describe SDK 2.0.
- [ ] All existing gates pass. Done on Windows: build, `components-tests` and `CI/check_plugin_sdk.sh`. Not run: Linux `-Werror`, clang-format 14, clang-tidy, sanitizers and Lua integration tests. `DetourNavigatorRecastMeshObjectTest.update_for_compound_shape_with_same_transform_and_changed_child_transform_should_return_true` fails now and then, also on the unmodified `vulkan` build.

## Out of scope

- Loading plugins in the game (task 0015), forwarding events to Lua in the engine (task 0016), publishing physics events and attaching to the game's world (task 0017).
- AdvancedBodies and ComplexClothing themselves (core systems with their own future tasks).

## Decisions taken while implementing, for toddhow to confirm

The spec was still marked draft with two open questions when this was implemented; both took the proposed answer.

1. **Name of the read-only service:** `PhysicsQueryService` in the SDK. The engine class stays `PhysicsServiceImpl` in `physics_service.{hpp,cpp}`, as this spec and task 0017 call it.
2. **Shape casts in this task:** added: sphere, capsule and box.
3. **Query masks:** a `QueryMask` matches a body when `(mask & objectLayer) != 0`, because `MWPhysics::Layers` values are single bits (`apps/openmw/mwphysics/joltlayers.hpp`). The host publishes names with `PhysicsServiceImpl::publishQueryMask`; plugins read them with `queryMask(name)`.
4. **Host with no world:** `HostImpl::getPhysicsQueryService()` is null until `attachPhysicsWorld(JPH::PhysicsSystem&)`.
5. **Forwarded payloads:** events stay trivially copyable, so a string is a fixed char array, and each field records its offset and size for the core to read in task 0016.
6. **Units:** the SDK speaks meters and the game's world is in Morrowind units, so `PhysicsServiceImpl` takes `worldUnitsPerMeter` when attached and is the one conversion boundary (default 1). Task 0017 passes the game's value.
7. **Back faces:** rays and shape casts both count back faces as hits; only a shape cast's normal always faces the cast.
8. **Handshake:** `validatePluginHandshake` now also refuses a library whose descriptor names an SDK the host does not provide.

## Tests removed

Deleted with their features: all of `test_scripting_service.cpp` and `test_collision_layers.cpp`. From `test_physics_service.cpp`, the tests of body creation and validation, stepping, joints, collision groups, driving bodies and structural changes during a step. From `test_lifecycle.cpp`, the fixed-step callback checks. `test_empty_host.cpp` and `test_event_bus.cpp` now use the remaining phases.
