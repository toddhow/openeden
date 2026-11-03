# Task: CreativeMotion's animation runtime in the game (draft)

Design doc section: `docs/research/0022-creativemotion-in-game-status.md` (what runs today), `docs/tasks/0053-creativemotion-transitions-and-style.md` (style layer and transition rules, done), `docs/tasks/0007-*` (animation runtime adapter, R0 to R5), `components/creativemotion/anim/CLAUDE.md` (the runtime's rules), decision 0001 (CreativeMotion is compiled into the engine) and 0010 (CreativeMotion is part of OpenEden and edited here).

Goal: make the CreativeMotion (CM) runtime do visibly more than a blend at animation changes, in a way that is measured by traces, off by default, and unchanged when off.

Inherited code touched (at `HEAD` of 2026-11-03): `apps/openmw/mwmechanics/character.cpp` (`CharacterController::createPoseControllers`, `runPoseDriver`, `refreshMovementAnims`, the movement block of `update`, `traceAnimation`), `apps/openmw/mwrender/creativemotiondriver.{hpp,cpp}` (`CreativeMotionPoseDriver::update`), `apps/openmw/mwrender/creativemotionstyle.{hpp,cpp}` (`classifyGroup`, `transitionSeconds`, `styleWeights`), `components/mwanimation/play.{hpp,cpp}` (`Play::getActiveState`, read only), `components/inertialmotion/*` (read only unless a stage says so), `components/creativemotion/anim/{runtime,graph,style}` (edited here, decision 0010), `files/settings-default.cfg` and `components/settings/categories/game.hpp` (new settings).

## 1. Findings (what is true today)

These come from reading the code at `HEAD` and from research note 0022's traces. Note 0022's table of "what the game wires" is out of date for the style layer: task 0053 wired it (`creativemotionstyle.*`, `CreativeMotionPoseDriver::setStyle`, `[Game] creativemotion style layer`).

### 1.1 What CM does in the game today

| CM feature | In the game? | Where |
| --- | --- | --- |
| Clip playback (own sampler over `opas::anim::Clip`) | Wired, for every actor with a skeleton when `creativemotion animation` is on | `CreativeMotionPoseDriver::registerSource`, `sampleClip` |
| Inertialized transition when a bone group's animation group changes (`Inertializer`) | Wired, per bone group (4 groups) | `CreativeMotionPoseDriver::update`, duration `creativemotion animation transition` (0.25 s default) |
| Transition lengths by kind of motion (`classifyGroup`, `transitionSeconds`) | Wired, off by default (`creativemotion transition rules`) | `creativemotionstyle.cpp` |
| Style layer (posture, idle life, cadence, additive layers) | Wired, off by default (`creativemotion style layer`) | `CreativeMotionStyleLayer::apply`, applied before the inertializer |
| Animation graph / state machine (`AnimGraph`, `GraphInstance`: parameters, Blend1D states, conditioned transitions, `Inertialize` or `Crossfade`) | **Not wired**: nothing in `apps/openmw` includes it | `components/creativemotion/anim/graph` |
| `AnimationPlayer` / `ClipPlayer` (play with a transition mode, helper solver) | **Not wired** (the driver has its own sampling and one `Inertializer` per group) | `anim/runtime` |
| Pose blend and bone masks (`PoseBlend`, `BoneMask`) | **Not wired** (the four vanilla blend masks pick one group per bone) | `anim/runtime/PoseBlend.hpp` |
| Swing-twist and `HelperSolver` (driven helper bones) | **Not wired**; Morrowind's skeleton has no twist bones, so a rig definition would be needed first | `anim/runtime` |
| `ContextController` (health, encumbrance, alertness, cold to effective style) | **Not wired**; `ActorState` feeds only speed, turning, hit and weapon drawn | `anim/style/ContextController.hpp` |
| IK, lean | The engine's own `ActorIkController` (uses CM's two-bone solver); lean uses `InertialMotion::LeanSmoother` | `actorikcontroller.*` |
| Authored transition clips (`enable authored transitions`) | Setting exists (default true); no Morrowind data has such clips, so the procedural fallback always runs | `character.cpp` `enableAuthored`, `isTransitionMovementState` |

### 1.2 Why transitions are hard to see (each cited)

1. **Everything that would make a difference is off by default.** `creativemotion animation` false, `creativemotion transition rules` false, `creativemotion style layer` false, `inertial locomotion` off and `inertial locomotion npcs` false (`files/settings-default.cfg`). With only `creativemotion animation` on, the one visible change is a 0.25 s blend at each animation group change.
2. **Vanilla (vsgopenmw's `MWAnim::Play`) does not blend at all**, so the group change is a cut: `Play::getActiveState` returns whichever group is active per bone group, with no blend time. CM's inertialization is therefore the only blend, which is why the spike table in note 0022 goes from 17 to 39 degrees (CM off) to 0 to 1.4 (CM on). It works; it is just a short, quiet effect.
3. **The driver only sees group names.** `CharacterController::runPoseDriver` passes the group, source and time of each bone group, plus `ActorState` for the style. It does not pass intent (target speed, acceleration, stopping, starting, turning rate, stance change). So `idle` to `walkforward` is the same event whether the player started walking now or stopped 2 s ago, and nothing can anticipate or follow through.
4. **Idle to walk shows nothing from the blend.** The walk clip's first pose equals the idle pose (note 0022, row "idle to walk": 0.0 degrees), so an inertialized blend between them has nothing to blend. What the eye sees at the start of walking is how fast the clip then plays and how fast the body moves: the animation speed floor (`character.cpp`, "Layer 3.B: Dynamic Playback Scaling", `max(smoothedSpeed, 0.15 * speed) / mMovementAnimSpeed`), the movement request (`InertialMotion::chooseMovementRequestSource`, fixed 2026-11-01) and the lean (`LeanSmoother`, fixed 2026-11-02). Those are engine and inertial-locomotion code, not CM.
5. **Walk to run already keeps its foot phase in the engine**: `CharacterController::refreshMovementAnims` passes `startpoint` from the old cycle when `runStateToWalkState` matches (character.cpp lines 755 to 765). Strafe, turn-in-place, stop and direction changes restart at `start`, so the legs can pop to the clip's first pose even though the inertializer hides the position step.
6. **The style layer is subtle by design** (`cm_default`: small idle life; `cm_heavy_test`: cadence 0.6 and a slouch) and is off by default. With it off, a player cannot tell CM from vanilla except at a change of group.
7. **NPCs get inertial locomotion only when `inertial locomotion npcs` is on** (default false), so with the default their movement is vanilla's.

### 1.3 Does the 0.8 s idle-pose slide explain "transitions missing"?

Only partly, and the evidence is incomplete. Note 0022's follow-up shows the slide as 24 frames (about 0.8 s) where the ground scrolls while legs and back keep the idle pose, then the legs step from about frame 120. That matches the old stall (animation speed 0.125 of normal, which a walk clip at 1/8 speed shows as almost no leg motion) and the movement fix `1ff6c1f38b` landed before the video, but his executable's build date cannot be identified, and on the current code the legs move from about frame 10. So: a slide at the start of walking is an animation-speed and movement problem (engine side), not a missing CM transition; a missing visible blend at group changes is item 1.2.1 to 1.2.4. Both are real; they need different fixes, and only the second is CM's. Not verified: no build of his has been run.

## 2. Plan

### Settings (all new settings off by default; none changes anything off)

- `[Game] creativemotion preset` (`off`, `natural`; default `off`). `natural` switches the existing pieces on together for a visible result: transition rules, the style layer with `cm_default`, and (stage 2 on) the intent state. It writes no setting; code reads the preset and the individual settings (the individual settings still win when true).
- Stage-specific settings are named in each stage.

### Stage 0: the scenario and the metrics (tools only; lighter loop is allowed)

- `tools/creativemotion_check/`: a Lua player script (static third-person camera, in `Ebonheart, Grand Council Chambers`, flat floor, fixed steps, scripted input at fixed frame numbers) and a script `check.sh` that runs the game twice (CM off, CM on) and writes the `CMTRACE` lines. The scenario is the one in note 0022: idle 2 s, walk 2 s, run 2 s, stop, turn right and left in place, draw weapon, attack, sheathe, take a hit (a scripted `cmHit` or a damage call), death.
- `tools/creativemotion_check/measure.py` (or a shell and awk script; there is no Python on this Windows setup, so a small C++ tool `apps/…` is not wanted: use `awk` in Git Bash): computes for every scripted event at frame F, for the six traced bones:
  - M1 `step@F`: largest rotation step in degrees in frame F;
  - M2 `step8`: largest step in frames F+1 to F+8;
  - M3 `jerk`: largest second difference of the angle in frames F to F+8 (degrees per frame squared);
  - M4 `speed continuity`: largest frame-to-frame change of ground speed from the `moved=` field, in units per second per frame;
  - M5 `time to settle`: first frame after F at which every traced bone's step stays under 1.5 degrees for 8 frames (the noise floor from note 0022).
- The baseline table of note 0022 is reproduced by the tool before any other stage starts (numbers within 0.5 degrees).

Acceptance: the scenario runs twice with identical traces (deterministic, fixed seed, no wall clock), and the baseline numbers match note 0022's table.

### Stage 1 (first stage with a visible result): the intent state and the `natural` preset

Visible result: with `creativemotion preset = natural`, starting to walk, stopping, turning and drawing a weapon blend over times chosen per kind of motion, idle has breathing and sway, and a stop settles over about 0.4 s instead of cutting. Everything it uses already exists (`transitionSeconds`, `CreativeMotionStyleLayer`); this stage makes the combination the default experience and adds the intent that stage 2 needs.

- `MWRender::ActorState` gains intent: `mTargetSpeedMps`, `mAccelerationMps2` (from the engine's smoothed speed: `LocomotionState::getSmoothedSpeed`, or the measured speed when inertial locomotion is off), `mStarting`, `mStopping`, `mTurnRate` (rad per second), `mStanceChanging`. Set in `CharacterController::runPoseDriver` from values the controller already has (`mMovementState`, `mIdleState`, `mTraceLocomotionPhase`). No new physics calls.
- `classifyGroup` stays group-based; `transitionSeconds` additionally takes `mStarting` and `mStopping` so a walk-to-idle with a stop intent (the player let go) differs from one caused by a state flip (blocked by a wall) by `sStopping` versus the base time.
- The `natural` preset is read where `createPoseControllers` reads `mCreativeMotionTransitionRules` and the style layer setting.

Acceptance (measured with stage 0, third person, CM on with `natural` against CM on with defaults, both against CM off; numbers are proposed starting values, toddhow decides):
- [ ] M1 `step@F` at every scripted event with a group change is at most 1.5 degrees (noise floor) for all six bones, as in note 0022's CM-on column.
- [ ] M3 `jerk` at walk-to-idle (left thigh and spine) is at least 30 percent lower than CM on with defaults (the stop is longer and smoother), and M5 `time to settle` after the stop is between 0.3 s and 0.8 s.
- [ ] Idle with `natural`: the spine and head rotate at least 0.3 degrees peak to peak over 4 s (idle life visible) and at most 2 degrees; with CM off they are 0.
- [ ] Starting from idle: M1 at the first frame of `walkforward` is unchanged from CM on defaults (the walk's first pose equals idle: nothing to improve there; the start of walking is engine side, finding 1.2.4).
- [ ] The preset adds at most 0.05 ms per actor per frame on this machine (measured with `[Rendering] frame counters` and `[Cells] load trace`-style scopes; 20 NPCs in view).
- [ ] With `creativemotion preset = off` and every other setting at its default, the traces of the scenario are byte for byte identical to the fork point's traces (the unchanged-when-off check; see below).

### Stage 2: a locomotion graph for the lower body (decision for toddhow first, see open questions)

Use `GraphInstance` as the state machine for locomotion cycles on the lower body only, while vanilla's `CharacterController` still selects groups (so scripts, text keys, combat timing and AI stay unchanged). Parameters: `speed`, `turnRate`, `stance`, `sneaking`, `inWater`. States: idle, walk, run, sneak blended by a Blend1D on speed from the existing walk and run clips (`Blend1D` samples at the clips' own speeds), strafe variants, turn in place. Transitions with conditions and per-transition `durationS` and mode (`Inertialize` for start and stop, `Crossfade` for walk to run). Output replaces the lower body bone group's sampled pose; the other three groups stay as today.

Acceptance:
- [ ] Walk to run and back: M1 under 1.5 degrees, and the foot phase of the lower body (left and right ankle height in the trace, a new traced field `foot` from the bone transform's height above the actor root) differs by at most 10 percent of a cycle across the change.
- [ ] Idle to walk to stop with intent: the lower body's leg motion starts within 3 frames of the speed rising above 0.1 m/s (no idle-pose slide: leg angle step above 1 degree within 3 frames), and the legs settle to the idle pose within 0.8 s of the stop.
- [ ] Strafe and direction changes: M1 under 1.5 degrees and no leg pop (M2 under 6 degrees), where note 0022's baseline for `turn right to idle` is 1.4 and `walk to run` M2 is 4.4.
- [ ] Bit-for-bit repeat runs (determinism); the graph files merge in load order and are loaded from the VFS (`graphs/<id>.animgraph.json`), validated with located errors.

### Stage 3: pose blend and masks for layered actions

Upper-body actions (attack, cast, draw weapon, hit reaction) are layered over the locomotion with `PoseBlend` and a bone mask in place of the four blend masks' hard bone sets, so a hit can lean the spine over a running lower body and ease out. Swing-twist and `HelperSolver` need a Morrowind rig definition with twist helper bones; stage 3a writes that definition (`opas.skeletonprofile` v2 and a rig file, shared with AdvancedBodies' bone map, `docs/design/advancedbodies-in-openeden.md`), and 3b uses it for forearm and thigh twist only if the profile names real helper bones; otherwise 3b is dropped and recorded.

Acceptance: hit reaction while running leaves the lower body's M1 under 1.5 degrees and the spine's response peaks within 6 frames and settles within 0.5 s; attack start and end M1 under 3 degrees on the upper body with transition rules; no change to the group the controller reports (`CMTRACE g0..g3`).

### Stage 4: NPCs and context

`ContextController` (health, encumbrance, alertness, cold) feeds the style; NPCs with `inertial locomotion npcs` get the same intent; crowds are measured (cost per actor, 50 actors in view, one cell border crossing) and capped by distance (actors beyond a setting's distance use vanilla's pose, with a blend when they come back, since `Play` writes no bones while the driver is on).

Acceptance: 50 actors cost at most 1.0 ms per frame on the main thread and 2.0 ms on animation threads; no allocation per frame per actor (an allocation counter test, `apps/openmw_tests/support/allocationcounter.hpp`); with `natural` on, a cell crossing adds no hitch above the incremental-build baseline (`docs/research/0028-*`).

## 3. Constraints

- **Off by default and unchanged when off.** With every `creativemotion *` setting at its default, `CharacterController` creates no pose driver and `Play::updateBones` stays true (existing rule, `apps/openmw/mwrender/CLAUDE.md`). The comparison: run the scenario with all new settings absent and with each new setting explicitly false, diff the `CMTRACE` text and a captured frame (`--auto-screenshot`) at five frames of the scenario against the fork point (`git stash`-free: build the parent commit of stage 1 in `I:\Projects\OpenEden-work\cm-fork-point`); traces must be byte identical, images identical (0 pixels over the threshold).
- **Determinism.** No wall clock, no unseeded randomness: the style noise uses `mStyleSeed` (actor seed) and accumulated frame time; the graph steps by the frame time passed in. Two runs of the scenario give identical traces; with extensions discovered in a different order, the same.
- **Allocation.** `update`, `apply`, graph steps and style application allocate nothing per frame once an actor's driver is built (`components/creativemotion/anim/CLAUDE.md` rule); buffers are sized in `createPoseControllers`/`setStyle`. Tests count allocations with `allocationcounter.hpp`.
- **Threading.** `CharacterController::runAnimation` may run on an animation thread, one actor per call. A driver and its graph instance touch only their own actor; the clip registry (`mClips`) is per driver; graph definitions and style profiles are shared read-only after load (StyleLibrary and a new GraphLibrary are filled on the main thread before use and never freed, as `StyleLibrary` already). Each new class says in a comment which thread calls it. Intent is written by the main thread (`CharacterController::update`) and read on the animation thread: the existing pattern (`mStyleDirty` atomic, `mStyleCadence` atomic) applies; no new locks in the per-frame path.
- **Units.** CM is metres; conversion stays at `CreativeMotionPoseDriver`'s one boundary (`Constants::UnitsPerMeter`); the graph's speed parameter is in metres per second.
- **Authority.** CreativeMotion is edited here (decision 0010) through this spec; `components/creativemotion/` changes keep its code free of engine types.
- **Full loop is required** for every stage that touches `apps/openmw/mwrender/creativemotion*`, `character.cpp` pose-driver code or `components/creativemotion/` (CLAUDE.md "Full loop required": the CreativeMotion adapters): the test-writing agent writes tests from this spec alone, without seeing the implementation; the implementing agent may not edit them; a fresh reviewing subagent checks lifetimes, threading, determinism, allocation and the off behaviour. Stage 0 (tools only) may use the lighter loop if toddhow marks it.
- **Formatting and warnings:** clang-format 14 on changed lines, GCC `-Werror -Wshadow` clean (brace every `if`/`else` body that contains a macro).
- **No game assets** in the repository: traces and videos stay in `I:\Projects\OpenEden-work\creativemotion\`.

## 4. Tests (written first, from this spec)

- Unit tests (`apps/components_tests/mwrender/test_creativemotion_*.cpp`, `apps/openmw_tests/`): `ActorState` intent to `transitionSeconds` mapping (starting, stopping, flip without intent); preset resolution (`off`, `natural`, individual settings winning); graph load errors with located messages; a fixed scenario of `GraphInstance` steps gives the same pose twice and allocates nothing; the Blend1D at a speed between two samples weights as specified; phase continuity of a walk-to-run `Crossfade`; `PoseBlend` with a mask leaves unmasked bones bit-identical.
- Scenario tests (`tools/creativemotion_check/check.sh`): the metrics M1 to M5 against the acceptance numbers; the unchanged-when-off diff.
- Frame checks for anything that draws differently (it should not): a captured frame of CM on versus CM off at idle shows the same silhouette except the style offsets, viewed before commit (project rule, `frame-check-before-render-commits`).

## 5. Acceptance criteria (whole task)

- [ ] Stage 0 scenario and metrics reproduce note 0022's baseline within 0.5 degrees, twice, identically
- [ ] Stage 1 criteria above are met and toddhow, watching a recorded scenario with `natural` against `off`, can tell the two apart (his judgement; the traces alone do not close this)
- [ ] Stages 2 to 4 criteria above, each after toddhow's go
- [ ] With every new setting at its default, traces and frames equal the fork point's (byte and pixel comparison above)
- [ ] The Linux gate passes (unit tests, 32 Lua tests, `-Werror`); extension host and `openmw-tests` pass with zero extensions
- [ ] `apps/openmw/mwrender/CLAUDE.md` and note 0022's table are updated (style layer wired; graph or pose blend as built)

## Out of scope

Animation retargeting between skeletons; motion matching; ragdoll and the push sandbox (task 0065); replacing Morrowind's group selection or text keys; facial animation; changes to AI, combat or physics; the engine-side start-of-walk speed floor and movement request (they are inertial locomotion's, already fixed or tracked in note 0022); VR.

## Open questions for toddhow

1. **Stage 1 default:** should `creativemotion preset = natural` become the default once it passes, or stay opt-in? (It changes how every actor looks while CM is on.)
2. **Stage 2 scope:** run the graph for the lower body only while vanilla keeps selecting groups (recommended: scripts, combat timing and text keys keep working), or let the graph own the whole body's state? The second is a much larger change and risks every script that plays or checks animation groups.
3. **Authored clips:** may the task add a few clips of our own (a start step, a stop settle, a turn-in-place) as CC0 or hand-authored `.kf`/glTF data in the repo, or must stage 2 use only vanilla clips plus procedural offsets? (The setting `enable authored transitions` already exists; no clip exists.)
4. **Speed floor at the start of walking:** the animation speed floor (`0.15 * speed`, character.cpp "Layer 3.B") makes the legs crawl for the first frames. Is that wanted (it gives the "heavy start" feel) or should the walk start at a normal speed with the lean and acceleration providing the weight? This is not CM but decides what stage 1 can look like.
5. **Metrics thresholds:** the acceptance numbers above (1.5 degrees, 30 percent, 0.3 to 0.8 s, cost per actor) are proposals; confirm or replace them. Is a recorded video comparison by you the real gate?
6. **Twist helper bones (stage 3b):** are we willing to add helper bones to Morrowind's skeleton at load (a rig definition), or drop twist?
7. **Which build of yours showed the slide?** A build date or git hash would close finding 1.3 (run it in the scenario).
8. **NPC scope and budget:** all actors, or only those within N metres and the player's group; what frame-time budget for 50 actors?
