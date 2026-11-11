# Inertial Locomotion (RDR2-style movement)

Status: **Approved Design, Ready for Planning.** Owner: project owner (toddhow). Date: 2026-10-27.

> **Status update, 2026-11-10 (task 0107, research note 0052):** no authored start, stop, skid or turn clip exists in any animation source (Morrowind has none; CreativeMotion's glTF clips are on another rig and are not loaded), so section 3.3 B, the procedural fallback, is all that has ever run. The start clip could not be chosen even with data (the rule waited for the Starting phase, which exists only one frame after the choice): fixed. Section 3.4's "the IK absorbs residual stance displacement" was vertical only; the horizontal slide of the stance foot at a stop is now held by `[Game] foot lock` (off by default).

## 1. Goal

Give actors weighted, believable ground movement in the style of Red Dead Redemption 2:
- Velocity ramps smoothly with realistic inertia instead of snapping instantly.
- The body turns into travel arcs over time with damped yaw tracking.
- The pelvis and spine lean into accelerations and turns.
- Animation playback rate and blending couple directly to true physical ground speed to eliminate foot sliding.
- Support dedicated **authored animation transitions** (starts, stops/skids, turns-in-place) when available, while providing seamless **procedural fallbacks** when using standard vanilla animations.
- Offer a scalable **Strength Setting** ranging from `Low` (subtle inertia with high responsiveness) to `High` (full heavy RDR2 momentum and weight).

Foot IK planting (`ActorIkController`) in OpenEden core serves as the physical anchor that prevents ground penetration and sliding during all transitions.

**Non-goals (v1):** Full ragdoll-driven locomotion (active balance ragdolls for active walk cycles; ragdolls remain for hit/fall/death), underwater swimming alterations, flying/levitation physics alterations.

## 2. Current State

| Area | Today | File |
|---|---|---|
| Horizontal movement | Applied directly from animation-derived velocity each frame; instantaneous acceleration/stop | `apps/openmw/mwphysics/movementsolver.cpp` |
| `Actor::mInertia` | Airborne and vertical motion only (gravity, jumps, falls, slow fall) | `movementsolver.cpp`, `mwphysics/actor.hpp` |
| Body yaw | Snapped directly by movement/camera code | `mwmechanics/character.cpp` |
| Bone rotation | `RotateController` rotates bones during head/spine look | `mwrender/rotatecontroller.cpp` |
| Foot IK & Pelvis Drop | Full zero-lag ground probing, ankle floor, and two-bone IK in core | `mwrender/actorikcontroller.cpp` |
| Animation speed | Walk/run cycles tied to static speeds in character controller | `mwmechanics/character.cpp` |

---

## 3. Architecture & Design

The system is organized into four complementary layers:

```
[ Input / AI Intent ]
        │
        ▼
[ Layer 1: Inertial Locomotion State (Physics) ]
  • Velocity smoothing (accel / decel / turn acceleration)
  • Strength presets (Low / Medium / High)
        │
        ├───► Applied to MovementSolver (World Collision & Terrain)
        │
        ▼
[ Layer 2: Yaw Arc & Turn Smoothing ]
  • Smooth heading trajectory in motion
  • Turn-in-place detection & root pivot
        │
        ▼
[ Layer 3: Animation State Machine & Clip Transitions ]
  • Start transitions (step-off)
  • Stop / Skid transitions (braking / 180° turnaround)
  • Turn-in-place transitions (stationary pivots)
  • Procedural fallback if authored clips absent
        │
        ▼
[ Layer 4: Procedural Lean & IK Coupling ]
  • Pelvis & spine tilt from longitudinal & lateral acceleration
  • Animation playback rate matched to real ground velocity
  • TwoBoneIK foot planting absorbs residual stance displacement
```

---

### 3.1 Layer 1: Locomotion State (Physics Engine)

Add a per-actor `LocomotionState` owned by [`MWPhysics::Actor`](file:///I:/Projects/OpenEden/apps/openmw/mwphysics/actor.hpp):

- `vsg::vec2 smoothedVelocity` (horizontal world units/s)
- `float smoothedYaw` (current smoothed body heading, radians)
- `float lastSpeed` (for calculating linear acceleration $a = \Delta v / \Delta t$)
- `float angularVelocity` (yaw turn rate, rad/s)
- `LocomotionPhase phase` (`Idle`, `Starting`, `Moving`, `Stopping`, `Skidding`, `Pivot`)

#### Ground Step Integration
On every physics tick on the ground:
```cpp
vsg::vec2 target = requestedVelocity.xy; // from player input or AI
vsg::vec2 delta   = target - smoothedVelocity;

// Select acceleration limit depending on whether speeding up, slowing down, or reversing
float maxRate = 0.0f;
if (target.length2() < 1e-3f)
    maxRate = decelRate; // Stopping
else if (delta * target < 0.0f)
    maxRate = skidDecelRate; // Reversing / abrupt directional change
else if (smoothedVelocity.length2() < target.length2())
    maxRate = accelRate; // Accelerating
else
    maxRate = turnAccelRate; // Cornering / redirecting momentum

smoothedVelocity += clampLength(delta, maxRate * dt);
```

- **Speed Scaling:** Acceleration rates automatically scale with the actor's current max speed (Speed attribute, Athletics, encumbrance, active spells) so heavy armor and agile builds feel distinctly weighted.
- **Airborne Transition:** When jumping or falling, `smoothedVelocity` is preserved as the initial horizontal momentum in `mInertia`. Upon landing, `smoothedVelocity` is re-seeded from remaining ground momentum without snapping.
- **State Resets:** Swimming, levitation, knockdowns, paralysis, and teleports instantly reset `smoothedVelocity` to 0.

---

### 3.2 Layer 2: Body Yaw & Trajectory Arcs

- **In Motion:** Body yaw does not snap to camera look direction instantly; it smooths along the velocity trajectory vector with a critically damped turn rate.
- **Strafing & Combat:** When in combat stance or actively aiming (bow, magic, or targeting lock), the actor maintains facing toward the crosshair/target while velocity momentum shifts laterally.
- **Stationary Turning:** When moving below a speed threshold ($< 5\text{ units/s}$):
  - Minor camera movements rotate the head and spine via look-at IK.
  - If camera angle relative to body exceeds the turn threshold (e.g., $45^\circ$–$60^\circ$), a **Turn-in-Place** transition is triggered.

---

### 3.3 Layer 3: Authored Animation Transitions & Procedural Fallback

To achieve full RDR2 fidelity, the engine supports dedicated transition clips while maintaining a 100% stable procedural fallback for vanilla assets:

#### A. Authored Clips (When Available)
If modern animation packs provide matching transition keys:
1. **Starts (`WalkStart`, `RunStart`, `SprintStart`):**
   - Directional step-off clips ($0^\circ, \pm 90^\circ, 180^\circ$).
   - Plays immediately upon input; root motion or matched curve pushes `smoothedVelocity` up to cycle speed.
2. **Stops (`WalkStop`, `RunStop`):**
   - Triggered when movement input drops to zero.
   - Character plants lead foot and decelerates over 1–2 settling steps rather than freezing mid-stride.
3. **Skids & 180° Turnarounds (`RunSkid`, `QuickTurn180`):**
   - Triggered when reversing stick/keys at high velocity.
   - Character drops center of mass, skids with lead heel, then launches in opposite direction.
4. **Turn-in-Place (`TurnLeft90`, `TurnRight90`, `Turn180`):**
   - Plays when stationary yaw error exceeds threshold. Steps foot around and resets root heading.

#### B. Procedural Fallback (Vanilla Morrowind Animations)
If an actor or creature lacks authored transition clips:
- **Dynamic Playback Scaling:** The cycle animation playback speed is scaled dynamically:
  $$\text{playbackRate} = \frac{\|\mathbf{v}_{\text{smoothed}}\|}{v_{\text{nominal}}}$$
  Feet step exactly as fast as the body is actually traveling over the ground.
- **Procedural Pivot:** For turns-in-place without clips, the root yaw rotates at a capped rate while `ActorIkController` plants the stance feet. When foot twist reaches limit, feet unclamp and step in place.
- **Procedural Skid/Brake:** The pelvis drops slightly ($2\text{--}4\text{ cm}$) during sharp deceleration to simulate weight absorption.

---

### 3.4 Layer 4: Procedural Lean & IK Coupling

In [`MWRender::ActorIkController`](file:///I:/Projects/OpenEden/apps/openmw/mwrender/actorikcontroller.cpp):
1. **Longitudinal Lean (Pitch):**
   - Proportional to forward/backward acceleration: $\theta_{\text{pitch}} \propto a_{\text{longitudinal}} / g$.
   - The torso leans forward when accelerating and back when braking.
2. **Lateral Lean (Roll / Bank):**
   - Proportional to centripetal acceleration in turns: $\theta_{\text{roll}} \propto (v \cdot \omega_{\text{yaw}}) / g$.
   - Character banks into curves.
3. **Yaw-Invariant Application:**
   - Rotations are applied locally to Pelvis and Spine nodes in character local coordinates prior to bone world matrix evaluation.
   - Zero jitter when rotating or changing heading.
4. **Foot Stance Planting:**
   - Two-bone IK calculates ankle position from world raycasts after lean is applied, ensuring soles remain planted flat on terrain regardless of torso lean.

---

## 4. Strength Setting & Presets

The movement feel is governed by a global setting: `locomotion strength`.
Users can choose between predefined presets or configure custom values:

| Parameter | Off (Vanilla) | Low | Medium | High (Full RDR2) |
|---|---|---|---|---|
| **Feel** | Instant snap | Crisp, subtle weight | Balanced momentum | Heavy, deliberate, cinematic |
| `accel time` (0 to full speed) | 0.00 s | 0.12 s | 0.25 s | 0.45 s |
| `stop time` (full speed to 0) | 0.00 s | 0.08 s | 0.18 s | 0.35 s |
| `skid turn time` (180° turnaround) | 0.00 s | 0.10 s | 0.25 s | 0.50 s |
| `turn arc half-life` (heading lag) | 0.00 s | 0.05 s | 0.12 s | 0.22 s |
| `turn-in-place threshold` | None (snaps) | 70° | 50° | 40° |
| `max lean angle` | 0° | 3.5° | 7.0° | 12.0° |
| `stop animations enabled` | No | Optional | Yes (if present) | Yes (forced / skids) |
| `turn animations enabled` | No | Procedural | Yes | Yes |

### Configuration File (`settings-default.cfg`)
```ini
[Inertial Locomotion]
enabled = false
strength = medium                     # low, medium, high, custom
apply to npcs = false                 # enable for NPCs / creatures
enable authored transitions = true   # use start/stop/turn clips if present in animation file
procedural lean = true
lean scale = 1.0

# Custom tuning overrides (active when strength = custom)
accel time = 0.25
stop time = 0.18
turn half life = 0.12
max lean degrees = 7.0
```

---

## 5. Implementation Roadmap

### Phase 1: Pure Mathematical Locomotion Model & Unit Tests
- Create `components/inertialmotion/locomotionstate.hpp` and `.cpp`.
- Pure C++20, zero external renderer/engine dependencies.
- Implement rate-limited vector acceleration, deceleration, and turn arcs.
- Unit tests in `components-tests`:
  - Convergence & overshoot checks under varying $\Delta t$.
  - 180° reversal passing through zero velocity.
  - Multi-frequency step tests (30 Hz, 60 Hz, 144 Hz invariance).
  - Presets (Low, Medium, High) mathematical verification.

### Phase 2: Engine Movement Integration & Velocity Decoupling
- Add `LocomotionState` to `MWPhysics::Actor`.
- Hook into `MWPhysics::MovementSolver::move()`.
- Substitute smoothed velocity for direct input when actor is on ground.
- Implement state transitions (jump launch preserves momentum in `mInertia`; landing re-seeds smoothly; swimming/teleport resets).
- Add `CharacterController` playback rate coupling: scale animation frame time with true ground velocity.

### Phase 3: Yaw Arcs & Procedural Turn-in-Place
- Smooth actor body heading in `mwmechanics/character.cpp`.
- Decouple camera look angle from instantaneous root yaw.
- Implement stationary pivot logic: trigger root turn when look error exceeds angle threshold.

### Phase 4: Procedural Pelvis & Spine Lean
- Integrate pitch and bank calculations in `MWRender::ActorIkController`.
- Apply inertial offsets to pelvis and spine bones with low-pass damping.
- Validate that two-bone IK foot planting continues to pin soles securely without clipping or jitter.

### Phase 5: Authored Animation Transition Support
- Add detection for transition text keys/groups in `MWRender::Animation` (`WalkStart`, `RunStop`, `TurnLeft90`, etc.).
- Implement transition blending: seamlessly cross-fade between start/stop clips and cyclic walk/run loops.
- Graceful degradation: if clips are not found in the character mesh/skeleton, fall back to Phase 2–4 procedural logic.

### Phase 6: AI Adaptation, NPC Support & Settings
- AI waypoint navigation adjustment: add arrival braking distance proportional to current speed so NPCs don't orbit waypoints.
- Expose settings in `settings-default.cfg` and Settings UI.
- Verify combat responsiveness and strafing feel.

---

## 6. Verification & Test Plan

1. **Automated Unit Tests (`components-tests`):**
   - Acceleration curve tests across all presets (Low, Medium, High).
   - Reversal / skid physics calculation tests.
   - Zero-allocation per-frame safety tests.
2. **In-Game Playtest Verification:**
   - **Stationary to Full Sprint:** Verify acceleration ramp and step frequency progression.
   - **Full Sprint to Full Stop:** Verify stopping distance and absence of foot skating.
   - **180° Direction Reversals:** Verify skid deceleration and weight shift.
   - **Slopes & Stairs:** Verify smooth traversal without bouncing or launching.
   - **First-Person vs. Third-Person:** First-person maintains instant camera tracking with optional subtle weapon inertia; third-person showcases full body turning arcs.
   - **Preset Switch Test:** Verify switching in settings between Low, Medium, High, and Off takes effect dynamically without game restart.
