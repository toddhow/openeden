# Task: 0005 Plugin System Phase 5 - CreativeMotion First Client & Shared Jolt Physics Service
Design doc sections: [`docs/design/openeden-extension-system.md`](file:///i:/Projects/OpenEden/docs/design/openeden-extension-system.md#L171-L176), Sections 4.2, 4.4, 5.1, 5.2, 7.2, 10 (Phase 5)
Goal: Implement the shared Jolt `PhysicsService` behind the Host API, define the `openeden.animation` contract, hook up CreativeMotion (`i:/Projects/CreativeMotion`) as OpenEden's first real plugin using the core's shared Jolt world instead of creating a second one, and document the complete plugin implementation guide for future plugins.

Inherited code touched:
- `CMakeLists.txt` - add `plugins/` subdirectory and `sdk/` target
- `components/CMakeLists.txt` - add `physics_service.hpp` and `physics_service.cpp` to `plugin` component
- `apps/components_tests/CMakeLists.txt` - add `test_physics_service.cpp` and `test_creativemotion_plugin.cpp`, and link test dependencies

CreativeMotion repository (`i:/Projects/CreativeMotion`) code touched:
- `CMakeLists.txt` - enable `CMAKE_POSITION_INDEPENDENT_CODE ON` and guard `BUILD_SHARED_LIBS OFF` when included as a subproject so static targets link cleanly into the `creativemotion` shared plugin library on Windows and Linux
- `physics/jolt/src/JoltWorld.cpp` - add `toJoltR` / `fromJoltR` helpers for `JPH::RVec3` so `JoltWorld.cpp` compiles cleanly under both single-precision and double-precision (`JPH_DOUBLE_PRECISION`) Jolt configurations
- `docs/design/openeden-plugin-integration.md` and `CLAUDE.md` - document all CreativeMotion changes and the OpenEden plugin integration architecture for the CreativeMotion session

Inputs / outputs:
- Public SDK interfaces (`sdk/include/openeden/`):
  - `physics.hpp`:
    - Value types: `PhysicsVec3`, `PhysicsQuat`, `Handle<Tag>`, `BodyId`, `JointId`, `CollisionGroupId`, `PhysicsErrorCode`, `CreateResult<Id>`.
    - Shapes & descriptions: `BoxShape`, `SphereShape`, `CapsuleShape`, `PhysicsShape`, `MotionType` (`Static`, `Dynamic`, `Kinematic`), `BodyDesc`, `SwingShape` (`Cone`, `Pyramid`), `JointDesc`, `JointMotor`, `BodyState`, `RayHit`.
    - `PhysicsService` abstract interface:
      - `createBody`, `destroyBody`, `isBodyValid`
      - `createJoint`, `destroyJoint`, `isConstraintValid`
      - `createCollisionGroup`, `setSubGroupsCollide`, `destroyCollisionGroup`
      - `setJointMotor`, `setJointPositionSteps`, `setJointTarget` (with and without velocity feed-forward)
      - `applyImpulse`, `setVelocity`, `setBodyDamping`, `setKinematicTarget`
      - `setMotionType`, `getMotionType`
      - `readBody`, `readBodies`
      - `castRay`
      - `step`, `fixedStepSeconds`
  - `contracts/animation_contract.hpp`:
    - `AnimationContract` (`kId = "openeden.animation"`, `kVersion = {1, 1}`):
      - Queries plugin active/enabled status, loaded rig/profile counts, active character ragdoll count.
      - Ragdoll instance management (`createCharacterRagdoll`, `destroyCharacterRagdoll`, `hasCharacterRagdoll`).
      - Per-character pose driving, root motion application, inbound gameplay event submission, and capsule/pelvis state queries.
- Engine component implementation (`components/plugin/`):
  - `physics_service.hpp` / `physics_service.cpp`:
    - `PhysicsServiceImpl` implementing `openeden::sdk::PhysicsService` over Jolt (`JPH::PhysicsSystem`).
    - Supports both standalone shared Jolt world ownership (for headless host / test host) and attaching to an external `JPH::PhysicsSystem*` (for `MWPhysics::PhysicsSystem`).
    - Handles Jolt double-precision (`JPH::RVec3`) conversions at the boundary.
    - Generation-counted slot tables (`BodySlot`, `JointSlot`, `GroupSlot`) pre-reserved to avoid hot-path allocations.
  - Wired into `HostImpl::getPhysicsService()` and `LifecycleManager`.
- CreativeMotion Plugin (`plugins/creativemotion/`):
  - `plugin.json`: Canonical `openeden.plugin` v1 manifest declaring `id: "creativemotion"`, `provides: [{"contract": "openeden.animation", "version": [1, 1]}]`, `optional: [{"contract": "openeden.body", "version": [1, 0]}]`, and off-by-default setting `enabled: false`.
  - `host_physics_world.hpp` / `host_physics_world.cpp`:
    - `HostPhysicsWorld : public opas::physics::PhysicsWorld` adapting `openeden::sdk::PhysicsService` to CreativeMotion's `PhysicsWorld` interface without creating a second Jolt world.
  - `creativemotion_plugin.hpp` / `creativemotion_plugin.cpp`:
    - Implements `openeden::sdk::Plugin` and `openeden::contracts::AnimationContract`.
    - Registers `AnimationContract`, fixed-step and frame phase callbacks, collision layers (`creativemotion.ragdoll`, `creativemotion.world`), VFS format readers (`opas.rig`, `opas.profile`, `opas.style`, `opas.animgraph`), save chunk handler (`creativemotion`), and Lua module (`openeden.creativemotion`).
    - Exports `openeden_plugin_entry` for dynamic library loading (`creativemotion.dll` / `libcreativemotion.so`).
- Documentation:
  - `docs/design/extension-implementation-guide.md`: Comprehensive guide documenting how to implement and hook up a plugin in OpenEden based on the CreativeMotion implementation.

Constraints & Invariants:
- Single Jolt World: CreativeMotion must NOT instantiate its own `JPH::PhysicsSystem` when running as an OpenEden plugin; all physics bodies and constraints go through `Host::getPhysicsService()`.
- Off-by-default: `plugin.creativemotion.enabled` defaults to `false` in `plugin.json`. When `false`, fixed-step and frame callbacks perform no simulation changes.
- Zero hot-path heap allocations: Stepping ragdolls, driving motors, and reading body states through `HostPhysicsWorld` and `PhysicsServiceImpl` must not allocate heap memory per step.
- Determinism: Identical runs over `PhysicsServiceImpl` produce bit-identical body states.

Acceptance criteria:
- [ ] `PhysicsServiceImpl` implements `openeden::sdk::PhysicsService` over Jolt (`JPH::PhysicsSystem`) with generation-checked handles, cone (`SwingTwistConstraint`) and pyramid (`SixDOFConstraint`) joints, collision groups, velocity feed-forward, motion type switching, and raycasting.
- [ ] A box dropped on a static floor in `PhysicsServiceImpl` comes to rest at the expected height, and two identical runs produce bit-identical results.
- [ ] `creativemotion` builds as a shared plugin library (`creativemotion.dll` / `libcreativemotion.so`) with `OPAS_BUILD_PHYSICS=OFF` (no second Jolt world) and loads via `PluginLoader` and `LifecycleManager`.
- [ ] `CreativeMotionPlugin` provides `openeden.animation` v1.1, registers collision layers, VFS format readers, save chunk handlers, and Lua bindings, and respects the off-by-default `enabled = false` setting.
- [ ] When enabled, CreativeMotion builds its sample humanoid ragdoll inside OpenEden's shared `PhysicsService`, drives joint motors across fixed steps, and reconciles capsule state with zero hot-path allocations.
- [ ] `docs/design/extension-implementation-guide.md` in OpenEden and `docs/design/openeden-plugin-integration.md` in CreativeMotion document the integration and any CreativeMotion changes.

Out of scope:
- Morrowind `.nif` / `.kf` compatibility pack readers (tracked under the Morrowind compatibility pack milestone).
- Eden Studio dockable UI panels (tracked under Eden Studio milestones).
