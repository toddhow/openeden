# Task 0006: CreativeMotion Milestone M9 — IK for Morrowind

## Goal
Implement Milestone M9 (IK for Morrowind actors) by locating engine animation hook points, exposing and integrating CreativeMotion's two-bone IK solver (`opas::ik::solveTwoBone`) for foot planting, look-at tracking, and smoother turns behind off-by-default settings, and introducing improved actor collision geometry (capsule collision shape option and per-limb ragdoll collision volumes) so actors can approach terrain, boulders, and obstacles closely without getting blocked by oversized bounding box corners.

---

## Context
- **Roadmap Citation**: `CreativeMotion/CLAUDE.md` ("Path to OpenMW", Milestone M9: "IK for Morrowind, no new physics") and `OpenEden/CLAUDE.md` ("Track J", "CreativeMotion in game (M8 to M13)").
- **Prior Work**:
  - Phase 5 ([Task 0005](0005-plugin-phase-5-creativemotion-first-client.md), commit `bd1f802829` in OpenEden and `8f54889` in CreativeMotion) established CreativeMotion as OpenEden's first native plugin (`plugins/creativemotion/`) providing `openeden.animation` v1.1.
  - CreativeMotion's standalone `kinematic/ik` module ([`TwoBoneIk.hpp`](file:///i:/Projects/CreativeMotion/kinematic/ik/include/opas/ik/TwoBoneIk.hpp)) provides an analytic two-bone IK solver with hinge limits on the middle joint, preserving bone lengths and handling all degenerate cases deterministically with zero heap allocations.
- **Upstream Engine Behavior & The "Giant Box" Collider Issue**:
  - In vanilla OpenMW, actor collision defaults to `actor collision shape type = 0` (Axis-aligned bounding box) in `files/settings-default.cfg`. The bounding box is a giant, flat-bottomed rectangular prism (~60 cm wide by 60 cm deep).
  - As observed in in-game debug collision visualization (`tcb`), the sharp corners and wide extents of the bounding box prevent the character from getting close to rock faces, ledges, and steep slopes. The box snags on slopes far before the actor's feet can reach the ground, causing feet to hover and blocking foot-planting IK from engaging with nearby ledges.
  - Furthermore, actor legs play fixed keyframe animations with no ground adaptation: on slopes and stairs, feet float in the air or penetrate geometry.
  - Head and spine pitch/yaw are driven abruptly via `Animation::setHeadPitch` and `Animation::setHeadYaw`, and actors snap to target headings during turns without rotational inertia.

---

## OpenMW Hook Points Analysis (Files & Functions)

1. **Skeleton Hierarchy & Bone Node Lookup**:
   - **File**: [`apps/openmw/mwrender/animation.hpp`](file:///i:/Projects/OpenEden/apps/openmw/mwrender/animation.hpp#L124-L127) & [`animation.cpp`](file:///i:/Projects/OpenEden/apps/openmw/mwrender/animation.cpp#L1954-L1983)
   - **Functions**: `Animation::getNodeMap()` and `Animation::addRotateController(std::string_view bone)`
   - **Relevance**: Maps bone names (`"bip01 l thigh"`, `"bip01 l calf"`, `"bip01 l foot"`, `"bip01 r thigh"`, `"bip01 r calf"`, `"bip01 r foot"`, `"bip01 head"`, `"bip01 spine1"`, `"bip01"`) to scene graph `osg::MatrixTransform*` nodes.
2. **Animation Frame Step & Controller Evaluation**:
   - **File**: [`apps/openmw/mwrender/animation.cpp`](file:///i:/Projects/OpenEden/apps/openmw/mwrender/animation.cpp#L1355-L1470)
   - **Function**: `Animation::runAnimation(float duration)`
   - **Relevance**: Evaluates playing animation state time, accumulates root movement, and updates `mRootController`, `mSpineController`, and `mHeadController`. Post-animation callback point for IK adjustment.
3. **NPC Animation Customization**:
   - **File**: [`apps/openmw/mwrender/npcanimation.cpp`](file:///i:/Projects/OpenEden/apps/openmw/mwrender/npcanimation.cpp#L687-L710)
   - **Function**: `NpcAnimation::runAnimation(float timepassed)`
   - **Relevance**: Specializes `Animation::runAnimation` for humanoid actors, updating first-person neck controllers and weapon controllers.
4. **Actor Character Mechanics & Turn Heading**:
   - **File**: [`apps/openmw/mwmechanics/character.cpp`](file:///i:/Projects/OpenEden/apps/openmw/mwmechanics/character.cpp#L2449-L2486) & [line 3145-3150](file:///i:/Projects/OpenEden/apps/openmw/mwmechanics/character.cpp#L3145-L3150)
   - **Functions**: `Character::update(float duration)`, `Character::updateTurning(float duration)`, and head orientation updates.
   - **Relevance**: Drives actor movement from animation velocities, applies character turn angles, and sets head pitch/yaw.
5. **Terrain & Collision Raycasting**:
   - **File**: [`sdk/include/openeden/physics.hpp`](file:///i:/Projects/OpenEden/sdk/include/openeden/physics.hpp#L314-L318) and [`apps/openmw/mwphysics/physicssystem.cpp`](file:///i:/Projects/OpenEden/apps/openmw/mwphysics/physicssystem.cpp#L417-L440)
   - **Functions**: `PhysicsService::castRay(originM, directionTimesLengthM, ignore)` and `PhysicsSystem::castRay(from, to, ...)`
   - **Relevance**: Fast narrowphase raycasts against the static world and terrain to find exact ground surface height and normal under each foot.

---

## Implementation Details

### 1. Contract Extension (`openeden.animation` v1.2)
Update [`sdk/include/openeden/contracts/animation_contract.hpp`](file:///i:/Projects/OpenEden/sdk/include/openeden/contracts/animation_contract.hpp):
- Bump `AnimationContract::kVersion` to `{ 1, 2 }`.
- Add pure kinematic IK types and methods:
  ```cpp
  struct TwoBoneChain
  {
      sdk::PhysicsVec3 root;
      sdk::PhysicsVec3 mid;
      sdk::PhysicsVec3 end;
  };

  struct HingeLimits
  {
      float minBendRad = 0.0f;
      float maxBendRad = 3.14159265f;
  };

  struct TwoBoneResult
  {
      bool valid = false;
      bool reached = false;
      sdk::PhysicsVec3 mid;
      sdk::PhysicsVec3 end;
      float bendRad = 0.0f;
      sdk::PhysicsQuat rootRotation;
      sdk::PhysicsQuat midRotation;
  };

  struct FootPlacementInput
  {
      TwoBoneChain legChain;
      float groundDistanceM = 0.0f;      // Raycast distance to ground surface
      float nominalHeightM = 0.0f;       // Nominal ankle height from ground in rest pose
      sdk::PhysicsVec3 groundNormal{ 0.0f, 0.0f, 1.0f };
      sdk::PhysicsVec3 poleDirection{ 0.0f, 1.0f, 0.0f }; // Forward knee pole
      HingeLimits kneeLimits{ 0.05f, 2.8f };
  };

  struct FootPlacementResult
  {
      TwoBoneResult ik;
      sdk::PhysicsQuat footAlignment;    // Rotation to align foot sole with ground normal
      float pelvisOffsetZM = 0.0f;       // Pelvis dip required if leg is over-extended
  };

  struct LookAtInput
  {
      sdk::PhysicsVec3 eyePosition;
      sdk::PhysicsVec3 headForward;
      sdk::PhysicsVec3 targetPosition;
      float currentYawRad = 0.0f;
      float currentPitchRad = 0.0f;
      float maxYawRad = 1.2217f;         // 70 degrees
      float maxPitchRad = 0.7854f;       // 45 degrees
      float speedRadPerS = 8.0f;
      float dt = 0.0f;
  };

  struct LookAtResult
  {
      float yawRad = 0.0f;
      float pitchRad = 0.0f;
      bool inRange = false;
  };

  struct TurnSmoothingInput
  {
      float currentYawRad = 0.0f;
      float targetYawRad = 0.0f;
      float halfLifeS = 0.08f;           // Exponential convergence half-life
      float dt = 0.0f;
  };

  // Pure kinematic interface methods
  [[nodiscard]] virtual TwoBoneResult solveTwoBone(
      const TwoBoneChain& chain, sdk::PhysicsVec3 target, sdk::PhysicsVec3 poleDirection, const HingeLimits& limits) const = 0;

  [[nodiscard]] virtual FootPlacementResult solveFootPlacement(
      const FootPlacementInput& input) const = 0;

  [[nodiscard]] virtual LookAtResult solveLookAt(
      const LookAtInput& input) const = 0;

  [[nodiscard]] virtual float solveTurnSmoothing(
      const TurnSmoothingInput& input) const = 0;
  ```

### 2. CreativeMotion Plugin Implementation
In [`plugins/creativemotion/creativemotion_plugin.cpp`](file:///i:/Projects/OpenEden/plugins/creativemotion/creativemotion_plugin.cpp):
- Implement `solveTwoBone` delegating directly to `opas::ik::solveTwoBone` in `kinematic/ik/src/TwoBoneIk.cpp`.
- Implement `solveFootPlacement`:
  - Calculate target ankle position clamped to reachable ground contact.
  - Call `opas::ik::solveTwoBone`.
  - Calculate foot sole surface orientation matching ground normal with roll/pitch limits.
  - Return pelvis downward compensation if one leg cannot reach the ground without over-extension.
- Implement `solveLookAt`:
  - Compute direction from eye to target point in local character frame.
  - Extract target yaw and pitch angles.
  - Clamp to `[ -maxYaw, +maxYaw ]` and `[ -maxPitch, +maxPitch ]`.
  - Smoothly interpolate towards target angles using exponential decay: `angle = target + (current - target) * exp(-dt * speed)`.
- Implement `solveTurnSmoothing`:
  - Compute shortest-arc angular difference: `delta = wrapPi(targetYaw - currentYaw)`.
  - Apply exponential smoothing with half-life: `decay = exp(-ln(2) * dt / halfLife)`.
  - Return `currentYaw + delta * (1.0f - decay)`.

### 3. Settings & Manifest Schema
Update [`plugins/creativemotion/plugin.json`](file:///i:/Projects/OpenEden/plugins/creativemotion/plugin.json):
- Update `"provides"` to `[{"contract": "openeden.animation", "version": [1, 2]}]`.
- Add settings (all default `false`):
  ```json
  "settings": {
      "enabled": { "type": "bool", "default": false },
      "footPlanting": { "type": "bool", "default": false },
      "lookAt": { "type": "bool", "default": false },
      "smootherTurns": { "type": "bool", "default": false }
  }
  ```

### 4. Engine Integration Hook Points
In `apps/openmw/mwrender/`:
- Create an `ActorIkController` class (in `apps/openmw/mwrender/actorikcontroller.hpp` / `.cpp`) attached to `Animation`:
  - Caches bone node pointers (`bip01 l thigh`, `bip01 l calf`, `bip01 l foot`, `bip01 r thigh`, `bip01 r calf`, `bip01 r foot`, `bip01 head`, `bip01 spine1`, `bip01`).
  - Reads settings from `SettingsService`:
    - If `footPlanting` is enabled: performs downward raycasts from thigh/hip positions via `PhysicsService::castRay`, executes `solveFootPlacement`, and applies solved rotation offsets to thigh, calf, and foot nodes via `RotateController` update callbacks.
    - If `lookAt` is enabled: smoothly updates head and spine controllers toward the target entity / player camera using `solveLookAt`.
    - If `smootherTurns` is enabled: applies `solveTurnSmoothing` to actor orientation updates.
  - **Invariance**: When settings are `false` (default), `ActorIkController` early-exits immediately, dispatching zero raycasts and leaving bone matrices 100% identical to vanilla.

### 5. Improved Actor Collision Shapes (Capsule & Multi-Body Volumes)
To allow actors to get close to obstacles, ledges, and slopes for IK foot-planting without snagging:
1. **Capsule Shape Option (`actor collision shape type = 3`)**:
   - Add `DetourNavigator::CollisionShapeType::Capsule = 3` in `components/detournavigator/collisionshapetype.hpp` and `.cpp`.
   - In `apps/openmw/mwphysics/actor.cpp`, construct a `JPH::CapsuleShape` oriented vertically with radius $R = \min(extents.x, extents.y)$ and cylinder half-height $H = \max(0, extents.z - R)$.
   - The rounded hemispherical bottom glides smoothly over slope transitions and stair steps, preventing box-corner snagging.
2. **Detailed Per-Bone Ragdoll Collision Volumes**:
   - For detailed limb/body interaction, utilize CreativeMotion's ragdoll layer (`creativemotion.ragdoll` collision layer in the shared Jolt world) with per-bone capsules matching the actor's actual skeleton hierarchy.
   - Ground raycasts for foot planting cast downward directly from foot bone world coordinates (`bip01 l foot`, `bip01 r foot`), ignoring the actor's own movement collider.

---

## Acceptance Criteria

1. **Automated Unit & Property Tests (`components-tests`)**:
   - `TwoBoneIkSolvesStandardChains`: Solves known reach scenarios with exact geometric accuracy.
   - `TwoBoneIkHandlesDegenerateInputsWithoutUB`: Returns `valid = false` or safe fallbacks for zero-length bones, target on root, parallel pole, or non-finite inputs.
   - `FootPlacementSolvesUnevenTerrain`: Accurately computes knee bend and ankle placement on +15°, -15° slopes and 20 cm steps.
   - `LookAtClampsAndSmooths`: Clamps gaze to angular limits and smoothly transitions over time intervals.
   - `TurnSmoothingContinuous`: Reaches target yaw smoothly without overshoot or wrap-around discontinuities across $-\pi \leftrightarrow +\pi$.
   - `ActorCollisionShapeTypeCapsule`: `CollisionShapeType::Capsule` creates a valid `JPH::CapsuleShape` in `Actor` with correct dimensions and orientation.
   - **Off-by-Default Invariant**: With `footPlanting=false`, `lookAt=false`, and `smootherTurns=false`, no raycasts or bone adjustments occur.
   - **Dynamic Library Loading**: `creativemotion.dll` loads via `PluginLoader` and resolves `AnimationContract` v1.2 with all IK methods callable across the dynamic library boundary.
2. **Quality Gates & Independent Review**:
   - Build compiles cleanly with zero warnings under MSVC `/W4`.
   - `components-tests` passes 100%.
   - Standalone CreativeMotion tests (`2,887 / 2,887`) continue to pass 100%.
   - Independent test-writer subagent authors tests prior to implementation.
   - Independent code reviewer subagent audits all code against this spec.
3. **Documentation & Changelog**:
   - `docs/design/plugin-implementation-guide.md`, `CLAUDE.md` in both OpenEden and CreativeMotion, and `docs/design/openeden-plugin-integration.md` updated with Milestone M9 notes.
