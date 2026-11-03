# CreativeMotion in the game: what runs today, and the idle-to-walk stutter (2026-11-01)

Written for toddhow, who reported that CreativeMotion's (CM's) transitions are missing, that "it is hard to tell this is even a different animation runtime", and that there is a stutter when the character starts to walk. All numbers below come from the developer trace added in this task (`[Game] creativemotion animation debug = true`, one `CMTRACE` log line per actor per frame; see "How it was measured"). Nothing here is from memory: where a statement is about code, the file and function are named.

## Short answers

1. **The idle-to-walk stutter is not CreativeMotion.** It happens with `creativemotion animation = false`. It is caused by `inertial locomotion` (any preset except `off`) together with the way the player's movement is taken from the animation. Details in section 1.
2. **CM's pose driver runs for the player and for every NPC** when `creativemotion animation` is on, and its transitions between animation groups work: they are inertialized over `creativemotion animation transition` seconds. Details in section 2.
3. **What CM does is small.** It plays the same Morrowind clips as vanilla and adds only the inertialized transition between groups. The animation graph, the style layer, swing-twist and pose blending exist in `components/creativemotion` but nothing in the game calls them. So with the style layer absent, a player can only see a difference at the moment an animation group changes. Details in section 3.

## 1. The stutter at the start of walking

### What happens

With `inertial locomotion = high` (his setting), the player's smoothed ground speed rises to 16.4 units per second within two frames and then **stays there for about 1.7 seconds** (about 100 frames), while the walk animation plays at 0.125 of its normal speed (slow motion). Then the speed climbs to the real walking speed in about 0.3 seconds. That is a crawl followed by a jump in pace: the stutter. Trace (flat floor, third-person camera, `creativemotion animation = false`, only `inertial locomotion = high`; `loco speed` is `LocomotionState::getSmoothedSpeed`, `animspeed` is the walk group's speed multiplier):

| frames after the group changes to walkforward | loco speed (u/s) | animspeed | player moved per frame (units) |
| --- | --- | --- | --- |
| 1 to 8 | 16.4 | 0.125 | about 0.27 |
| 40 to 90 | 16.4 to 17.1 | 0.125 | about 0.27 |
| about 100 | 25.5 | 0.166 | 0.43 |
| about 110 | 42.0 | 0.272 | 0.70 |
| about 118 | 69.0 | 0.448 | 1.15 |
| about 126 | 104.3 | 0.677 | 1.7 |

With every feature off (vanilla settings) the same scenario gives the instant start Morrowind has: 112 u/s on the first frame of the walk group.

### Why (a feedback loop)

1. `CharacterController::update` (`apps/openmw/mwmechanics/character.cpp`, "Layer 3.B: Dynamic Playback Scaling") sets the movement animation speed to `max(smoothedSpeed, 0.15 * speed) / mMovementAnimSpeed`. At the start `smoothedSpeed` is 0, so the floor `0.15 * speed` (16.4 for a speed of 109) applies: animspeed 0.125.
2. `CharacterController::postAnimation` takes the player's requested movement from the animation's root motion (`movement = movementFromAnimation`, because `isMovementAnimationControlled()` is true for a movement group that has movement; `player movement ignores animation` is false by default). At animspeed 0.125 the root motion is 1/8 of normal.
3. `MWPhysics::MovementSolver` passes that request to `InertialMotion::LocomotionState::update` (`components/inertialmotion/locomotionstate.cpp`). The smoother only limits how fast the speed may change; it has no reason to accelerate, because the request itself is 16.4.
4. `smoothedSpeed` is therefore about 16.4 again next frame, which gives animspeed 0.125 again. The speed can only leave the floor when the walk cycle's uneven root motion nudges it (about 1.7 seconds in this run).

Frames are not the problem: the longest frame in every window was 18.5 ms (no hitch), the pose and the movement start on the same frame.

### Bisect by his settings (CM off, third person, flat floor, first 45 frames after the switch)

`speed-jerk` is the largest frame-to-frame change of ground speed in u/s; a smooth start has a small jerk, and a start that does not move at all also has a small jerk, so read it together with the speed series.

| settings | result |
| --- | --- |
| all off (vanilla) | instant 112 u/s, as Morrowind |
| foot planting only | same as vanilla |
| look at only | same as vanilla |
| smoother turns only | same as vanilla |
| procedural lean only | same as vanilla |
| actor collision shape type 4 only | same as vanilla |
| **inertial locomotion = high only** | **crawl at 16.4 u/s for about 1.7 s (the stutter)** |
| CM only | same as vanilla (instant start, CM adds no stutter) |
| everything on, CM on or off | the same crawl; in one run the player also flickered between `idle` and `turnleft` while standing (not investigated, see "Open")

### Fix

See the commit list in the task report. In short: while inertial locomotion drives an actor that is walking or running, the requested velocity is the intended velocity (direction times `getCurrentSpeed`), not the animation's root motion; the animation speed still follows the smoothed speed. With `inertial locomotion = off` nothing changes.

## 2. Is the CM pose driver running?

Yes, for every actor that has a skeleton, when `creativemotion animation` is on:

- Created in `CharacterController::createPoseControllers` (`apps/openmw/mwmechanics/character.cpp`); it then sets `Play::updateBones = false` so vsgopenmw's own bone writes are off.
- `CharacterController::runAnimation` calls `runPoseDriver`, which feeds the driver the active animation source, group name and time of each of the four bone groups (`MWAnim::Play::getActiveState`), then `CreativeMotionPoseDriver::update` and `apply` (`apps/openmw/mwrender/creativemotiondriver.cpp`).
- Trace (player, CM on): `g0=walkforward(prev=idle,trans=0.000/0.350)` on the frame the group changes, then `trans` grows by the frame time each frame to 0.350 and the transition ends. The same for `idle -> turnright`, `idle -> handtohand`, `handtohand -> idlehh` and back. NPCs show the same (`driver=applies`). Objects with no skeleton create no driver.

The driver converts each `.kf` source to an `opas::anim::Clip` once, samples it at the group's time, and on a change of source or group name starts an `opas::anim::Inertializer` (components/creativemotion/anim/runtime) from the last frame's pose and velocity; each bone group has its own inertializer.

## 3. What CreativeMotion provides, and what the game wires

| CM feature | In the game? | Where |
| --- | --- | --- |
| Clip playback (`opas::anim::Clip`, own sampler) | **Wired** | `CreativeMotionPoseDriver::registerSource`, `sampleClip` |
| Inertialized transitions between groups (`Inertializer`) | **Wired** | `CreativeMotionPoseDriver::update`; duration `creativemotion animation transition` |
| State changes fed to CM | **Wired, group level only**: the driver sees the group name and source per bone group each frame; it is not told about movement, stance, weapon or hits as such, it infers a change from the group changing | `CharacterController::runPoseDriver` |
| Animation graph / state machine (`AnimGraph`, `GraphInstance`) | **Not wired** (no include in `apps/openmw`) | `components/creativemotion/anim/graph` |
| Clip player, pose blend, swing-twist | **Not wired** | `components/creativemotion/anim/runtime` |
| Style layer (`.style.json`: posture, cadence, additive layers, idle generator, noise) | **Not wired**: nothing loads a style file, nothing evaluates a style, no actor has a style | `components/creativemotion/anim/style` |
| IK | **Wired through the engine's own `ActorIkController`**, which uses CM's two-bone solver; it is not part of the pose driver | `apps/openmw/mwrender/actorikcontroller.*` |
| Ragdolls | Wired for the debug console command `cmRagdoll` only | `apps/openmw/mwphysics/ragdollhost.*` |

What a player sees with `creativemotion animation` on, honestly: the same poses as vanilla, except for a short inertialized blend at each change of animation group (0.35 s in his settings). Vanilla Morrowind cuts between groups. Measured with his settings (foot planting, look at, smoother turns, inertial locomotion high with the fix of section 1, procedural lean, collision shape 4, transition 0.35), third person, the same scripted scenario twice (`creativemotion animation` false, then true). Each cell is the rotation step of one bone in the single frame of the change, in degrees (the frames just before and after are 0 to 4); the "after the change" column is the largest step in the next 8 frames:

| change (bone) | CM off: step at the change | CM on: step at the change | CM on: largest in next 8 frames |
| --- | --- | --- | --- |
| walk to run (left thigh) | 20.2 | 0.5 | 4.4 |
| walk to run (spine) | 30.3 | 0.4 | 1.6 |
| run to walk (left thigh) | 39.4 | 0.0 | 2.2 |
| run to walk (spine) | 28.5 | 0.6 | 1.8 |
| walk to idle (left thigh) | 28.7 | 0.0 | 1.2 |
| turn right to idle (left thigh) | 17.5 | 1.4 | 1.4 |
| idle to walk (left thigh) | 0.0 (the walk's first pose equals the idle pose) | 0.0 | 0.3 |
| idle to hand-to-hand (left thigh) | 2.9 | 0.0 | 3.3 |

So vanilla cuts: one frame with a 17 to 39 degree jump. With CM on there is no spike at the change; the pose then eases over the transition. This is the one thing CM visibly adds today. (Rows are the traces `his_off` and `his_on`; quaternions have four decimals, so steps under about 1.5 degrees are noise.)

## How it was measured

- Setting `[Game] creativemotion animation debug = true` (new, off by default): `CharacterController::traceAnimation` logs `CMTRACE ref=<actor> dt=<frame> g0..g3=<group>(prev=<previous group>,trans=<elapsed>/<duration>) pos=(x y z) moved=(x y z) loco=(speed, phase) animspeed=<n> driver=<applies|compare|off>` and the local rotation (quaternion) of six bones, every frame, for every actor. It works with the driver off, so vanilla and CM can be compared.
- Scenario: a Lua player script (scratchpad, not in the repository) steers the player: idle 2 s, walk 2 s, run 2 s, stop, turn, draw weapon, attack, sheathe, in `Ebonheart, Grand Council Chambers` (flat floor) with a third-person camera. In first person the body's bones are not updated in vanilla mode, so first-person traces are not usable for A/B.
- Reading the bone traces: the quaternion is printed with four decimals, so two equal poses can differ by up to 1.4 degrees in a computed angle; steps under about 1.5 degrees are noise.

## Open

- The idle-to-turnleft flicker seen once with all features on (and `actor collision shape type = 4`) while the player stood still.
- The style layer and the animation graph are not wired: see `docs/tasks/0053-creativemotion-transitions-and-style.md` for the staged plan.
- Only the group-level state is fed to CM; a graph or a style needs movement, stance and weapon state as inputs.

## Follow-up (2026-11-02): the start-of-walk video, and the lean step

toddhow recorded the stutter (`OpenMW 2026-11-02 19-08-00.mp4`, 2560x1440, 306 frames, about 30 fps, third person, Balmora in rain, the character seen from behind). Frames were read with a small scratch decoder (not in the repository). What the video shows, in plain words:

- Frames 0 to about 95: the character stands. Frames 96 to about 119: the ground scrolls (he moves), but legs, arms and back keep the standing pose for about 24 frames (0.8 s): he slides.
- From about frame 120 the legs start to step, and over the next 10 to 15 frames the shoulders roll to one side and one arm swings out, then it settles into a normal walk.

What was measured (his settings, `creativemotion animation = false`, the same scripted walk start, third person, current build; log lines `CMTRACE` and the new `CMLEAN`):

| thing | result |
| --- | --- |
| ground speed | rises in a straight line from 0 to 128 u/s over 29 frames (0.47 s), as `inertial locomotion = high` intends |
| walk animation speed | 0.125 for 6 frames, then follows the speed up to 0.83; the legs move visibly from about frame 10 |
| old procedural lean | **0 to 3.8 degrees in one frame** on the first frame of the walk, held for 0.45 s, then **3.8 to 0 in one frame** when the speed stops rising |
| bank (sideways lean) | always 0: `LocomotionState::updateYaw` is never called by the game, so there is no angular velocity |
| every other bone | at most 1 to 3 degrees per frame (the walk clip's own motion) |

Cause of the visible lurch: the lean follows the acceleration, and inertial locomotion accelerates at a constant rate, so the acceleration steps from zero to full on one frame and back on another. The spine popped by 3.8 degrees (his lean scale is 1.0; a bigger scale pops more) twice per start. The other half of the video, the long idle-pose slide, is not reproduced on the current code (the idle-to-walk loop fix of section 1 is in HEAD since 2026-11-01 21:16, before the video). The executable he ran cannot be identified: `build-jolt/openmw.exe` has been rebuilt many times since 19:08, so it is possible his build was older than that fix, but this was not shown.

Fix: `InertialMotion::LeanSmoother` (`components/inertialmotion/leansmoother.hpp`), a critically damped spring with a speed cap, eases the lean pitch and bank toward their targets (`ActorIkController::applyPostAnimation`). Result with the same scenario: the largest per-frame change of the lean is 0.31 degrees (was 3.8), no overshoot. With `procedural lean` off nothing is written; with it on but `inertial locomotion` off the target is 0, the lean stays 0 and no bone is written, as before (traced: no `CMLEAN` line in a vanilla run, speed 0 and no lean in a lean-only run).

Open: the slide and the arm swing in the video may still come from the walk clip's own first frames at low animation speed (the clip plays at 0.125 to 0.3 speed for the first 0.2 s); a build identified as his is needed to separate that from the old stall.
