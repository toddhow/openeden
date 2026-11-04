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

## Answers of toddhow, 2026-11-03

1. Vanilla stays the default. A first-run prompt asking how the game should be configured (presets such as `natural`) is planned: it needs its own task spec (first-run configuration), not part of this one.
2. Lower body first (legs, hips, locomotion); upper body keeps the current clip playback for combat, spells and gestures until stage 3. Explained to toddhow in chat, accepted as "for now".
3. Yes: we author our own clips, all of them (start, stop, turn, strafe, idle variants and more). Needs a clip authoring and licensing note (our own work, GPL-3.0-or-later, no game assets) before any clip is added.
4. Asked what the animation speed floor means; answered in chat (the walk cycle's minimum playback speed while the actor accelerates from standstill). Open until toddhow replies.
5. Acceptance numbers confirmed (group-change joint step at most 1.5 degrees, stop settling in 0.3 to 0.8 s, jerk 30 percent lower).
6. toddhow's own video comparison is a second check; automated trace tests remain the first gate so that comparisons are repeatable.
7. Yes: twist helper bones may be added to Morrowind's skeleton (as part of our own skeleton data, stage 3 and later; compatibility with vanilla meshes to be checked).
8. NPC scope: NPCs should be interactable the way Red Dead Redemption 2 does it (approach, eye contact and look-at, greeting and reaction gestures, contextual prompts). This widens stage 4 into its own spec (NPC interaction), to be written after stage 2.
9. toddhow does not know which build showed the slide; settled by testing the current build.

## Stage log

### Stage 0 (2026-11-03): scenario and metrics, built and run on the current build

Built: `components/animtrace/` (the maths and the log parser, 39 unit tests: `AnimTrace*` in `components-tests`), the program `creativemotioncheck` (`apps/creativemotioncheck/`), and `tools/creativemotion_check/` (`check.sh`, the Lua scenario `mod/scripts/cmcheck.lua`). Measures M1 to M5 as in the Stage 0 section, with these definitions written down in `components/animtrace/tracemetrics.hpp`: a step is the angle of the rotation from the previous frame's local rotation to this one (atan2 form, so tiny angles keep their precision); M3 jerk is the angle between a frame's rotation step and the previous frame's; M4 uses the length of `moved` in x and y over dt; M5 settles inside the event's own phase. The scenario counts frames, not seconds (phase lengths in frames, 60 frames per second): idle, walk, run, stop, turn, stop2, draw, attack, hit (a small `Actor._onHit`, below the knockdown threshold), sheathe.

Changes from the spec's text: the measure program is C++ (`creativemotioncheck`), not awk, so that the maths has unit tests; the scenario cell is `Seyda Neen`, not the Ebonheart Grand Council Chambers (the chamber is too small: the player runs into a wall after 145 units and the run and stop numbers were meaningless); the scenario script is Lua run through `--skip-menu --start` and a content file, with no console script.

Baseline (full table and reading in `docs/research/0031-creativemotion-baseline.md`; raw reports in `docs/research/0031-creativemotion-baseline-data/`): at a change of animation group, vanilla jumps by up to 49 degrees on one frame (run 48.97, stop 45.54, hit 56.22, jerk up to 56); inertial locomotion alone leaves those jumps (35.9 to 55.8); CreativeMotion's blend removes the one-frame jump at every event (0.00) and keeps the jerk at 5.7 degrees per frame squared or less, spreading the change over the next frames (`step8` up to 7.9; a stop settles in 0.23 s where vanilla cuts).

Acceptance of stage 0:
- [x] The scenario runs, the report is deterministic for a given log (same bytes twice, unit-tested), the runner needs no Python.
- [~] "Twice, identically / within 0.5 degrees": event frames are identical between runs; values differ by at most 0.72 degrees (step8 of vanilla sheathe and CM stop2, jerk of inertial stop) and by 0.5 or less in the other values, and by up to 1.1 degrees at the hit, where the engine picks one of several hit animations at random. The game has no fixed timestep, so byte-identical runs are not possible without an engine change. Proposed: a tolerance of 1 degree between runs of one build, the hit compared only when the same hit animation played. For toddhow's decision.
- [~] "Reproduces note 0022's baseline": note 0022 has no per-event table; its one number for CM (left thigh 0 to 1.4 degrees at a group change) agrees with 0.00 here. A line-by-line comparison is not possible.

Not run: the Linux gate (the container was in use; `components/animtrace/` and the tests are plain C++20 with no platform code, written to `-Werror -Wshadow` rules but not compiled with GCC); clang-format was checked with clang-format 23 (14 is only in the container), and the three files it changed were fixed.

### Stage 1 (2026-11-04): actor intent and `creativemotion preset = natural`, built, reviewed, fixed and re-measured

**Built (commit `95a1b50bfb`, fixed after the fresh review in the commits that carry this log).** `IntentTracker` (smoothed speed, acceleration, `mStarting`, `mStopping`, `mStanceChanging`, turn rate; `apps/openmw/mwrender/creativemotionstyle.*`, vsg-free, it includes opas), `ActorState` with the intent fields, `transitionSeconds` with a `TransitionIntent` and `TransitionTimes` (the durations as data: the `[CreativeMotion Transitions]` category of settings.cfg, keys `stopping`, `flip`, `starting`, `fast turn`, `slow turn`, `fast turn rate`), the driver's `setIntentTransitions`, `[Game] creativemotion preset` (`off` default, `natural`) and `resolveCreativeMotionFeatures` / `loadCreativeMotionFeaturesFromSettings`. `natural` switches on creativemotion animation (the pose driver: the spec's list did not name it, the preset needs it), the transition rules, the style layer (default style `cm_default`) and the intent rules. **Inertial locomotion is NOT part of the preset**; the scenario's `natural` variant sets `inertial locomotion = high` itself, as the `cm` variant does.

**What the fresh review found and what was fixed** (review of `95a1b50bfb`, `931c67d7e4`, `3d09451d31`):
1. **The intent never reached the rules in the game (blocking).** `runPoseDriver` read `getMovementSettings(ptr).mPosition`, which `preAnimation` clears on the main thread before the animation runs on the update threads, so "wants to move" was false every frame, `mStarting` never true and `mStopping` only "the smoothed speed is above 0.2 m/s". The unit tests fed the tracker directly and could not see it. Fixed: `preAnimation` stores what the intent needs in members on the main thread (`mIntentWantsToMove` from the `isMoving` it computes, `mIntentSpeedMps` from `cls.getCurrentSpeed`, `mIntentMeasuredMps` from inertial locomotion's smoothed speed when it drives the actor, `mIntentStanceChanged` from the sneak and run stances), and `runPoseDriver` reads those. The spec's `mStanceChanging` was missing: added (held 0.3 s, `sStanceChangeHoldS`; no rule uses it before stage 2). A start no longer needs the speed to rise in the frame of the key press (the first frame's measured speed is still 0). The trace line now carries `intent=(wants start stop stance turn)`, and the check script FAILS a `natural` run whose trace never shows wants, start and stop frames: that check would have caught it. Re-measured: in every `natural` run 240 of 1,400 frames want to move, 59 start, 36 stop, 37 have a stance change.
2. **The stop measurements did not contain the blend.** With inertial locomotion the stop goes run to walk at the key release (frame 530, base time) and walk to idle about 22 frames later (frame 552); M2, M3 and M5 used frames F to F+8 of the scripted event, so they measured the walk cycle, and "a longer blend does not lower the jerk" was true trivially. New measures (below) are anchored at the frame G where each bone group's animation really changes.
3. **Unsupported numbers in the first log.** "The cm run differs only at the hit event and by 0.01 degrees elsewhere" was false (stop step8 moved by 0.96 against stage 0 with no animation commit in between); "best of three runs: 5.30 to 5.38" mixed three configurations (defaults, `stopping = 2.5`, and an experiment that was removed); "start of walking unchanged" was measured while the start rule could never fire; the idle-life method was not stated. Those claims are withdrawn; the numbers below replace them.
4. **`natural` made a turn out of a turn in place slower than the existing rules** (rate 0 in that frame always picked "slow", 1.0, against the rules' 0.6), and the start factor 0.8 and the fast/slow turn split were not in the spec. Fixed: the defaults of the intent rules now equal what `transitionSeconds` without intent gives (`starting = 1.0`, `fast turn = slow turn = 0.6`, `stopping = 1.5`), except `flip = 1.0`, which the spec asks for. The other values stay as data (open questions below).
5. **Precedence was justified with a false engine fact.** "A false setting cannot be told apart from its default" is wrong: `Settings::Manager::mUserSettings` holds the user's settings.cfg lines and the values set at run time. Now a setting the user writes wins over the preset, true or false (`FeatureSetting::mExplicit`): `natural` with `creativemotion style layer = false` keeps the style layer off.
6. **Validation.** Factors are numbers from 0 to 4 (`sMaxTransitionFactor`: four times the default base of 0.25 s is 1 s, above the longest blend the spec asks for), `fast turn rate` above 0 and at most 50; text, `2,5`, inf, nan, `1e39` (which overflowed the float before) and out-of-range values are refused with the default kept, an unknown key (a typo such as `stoping`) is reported; each problem is logged once with category, key and value. The category is read once per session (`transitionTimes()` in `character.cpp`; a change needs a restart), not per actor.
7. **Tests.** `OtherChangesAreAsTheOldRulesSay` now covers Turn and the pairs with Idle, with non-default data and every intent; new `WithTheDefaultDataOnlyAFlipDiffersFromTheRulesWithoutIntent` pins finding 4; `TransitionTimesDefaultsMatchTheSpec` (untrue for four of its six values) is now `TransitionTimesDefaultsEqualTheRulesWithoutIntent`; the tautological `SameSequenceGivesSameIntent` and `PresetNaturalKeepsWhatIsAlreadyOn` were replaced by `TheFirstFrameOfAStartCountsWhileTheSpeedIsStillZero`, `AStanceChangeCountsForTheHoldTimeThenEnds`, `AnExplicitFalseKeepsAPieceOffUnderNatural` and the validation tests. Process: the intent tests were rewritten in the same session as the fixes, before the first build but after the code edits, so their red run was shown by putting three of the reviewed behaviours back (the start that needed a rising speed, the 0.8 start default, "a true setting stays true"): 4 tests failed, then the fixes were restored and all passed. The new trace tests (`AnimTraceChanges`, 17) were written first and run against stubs: 16 failed, 1 passed vacuously (a line without the new fields). Neither set was written by a separate test-writing agent (a fork cannot start one), as stage 1 itself was not.
8. **Not fixed, recorded:** `isMoving` includes `MotionKind::Sneak`, which `classifyGroup` also gives `idlesneak` (standing), so standing up from a crouch takes the stop/flip rule; a stop with a weapon drawn goes to `idle1h`/`idlehh` (`Ready`) and never gets the stop rule (both predate stage 1 and also apply to the rules without intent; a fix changes `classifyGroup`, which the plain transition rules share: for toddhow).

**The new measures** (`components/animtrace`, `creativemotioncheck`): every change of every bone group's animation is reported at its frame G with the duration D the driver chose there (`trans=…/D` in the trace), the frames it ran, the step at G, the offset (the largest angle between a traced bone and its un-offset target: the trace's new `tgt=[…]`, written only with the debug trace on) at G, its peak and what is left after D, the decay (seconds until the offset is under a tenth of its peak) and the excess jerk (the bone's second difference above its target's own, from G to G+D). `--summary` gives each measure's range over several runs; a difference between two variants counts only where the ranges do not overlap. Every report carries the build's commit (with the number of changed tracked files: the build held other agents' uncommitted work) and openmw's link time, the variant's settings and `EXTRA_SETTINGS`.

**Measured** (Seyda Neen, third person, 3 runs per variant, openmw linked 2026-11-04 00:58:29 from `75bdf761eb` plus 36 changed tracked files, reports in `docs/research/0031-creativemotion-baseline-data/stage1-review-*-summary.json`; `cm` = inertial locomotion high and creativemotion animation; `cmoff` = `cm` with the preset off and transition rules and style layer written explicitly false; `natural` = inertial locomotion high and the preset). Lower body (g0), min..max over the runs:

| change at G | variant | D (s) | step at G | offset at G | decay (s) | excess jerk |
| --- | --- | --- | --- | --- | --- | --- |
| idle -> walkforward (290) | vanilla | 0 (cut) | 2.38..2.39 | - | - | - |
| | cm | 0.250 | 0.00 | 2.69..2.70 | 0.20 | 0.11..0.13 |
| | natural | 0.250 | 0.00 | 2.69 | 0.20 | 0.12..0.13 |
| walkforward -> runforward (410) | vanilla | 0 | 48.97..49.06 | - | - | - |
| | cm | 0.250 | 0.00 | 35.74..35.82 | 0.20 | 0.91..1.03 |
| | natural | 0.200 | 0.00 | 35.71..35.88 | 0.17 | 1.46..1.77 |
| walkforward -> idle, the stop (552) | vanilla (runforward -> idle, 530) | 0 | 45.55..45.57 | - | - | - |
| | cm | 0.250 | 0.00 | 41.15..41.17 | 0.20 | 1.12..1.47 |
| | cmoff | 0.250 | 0.00 | 41.15..41.16 | 0.20 | 1.25..1.26 |
| | natural | 0.375 | 0.00 | 21.27..41.17 | 0.28..0.30 | 0.31..0.88 |
| turnright -> idle (773) | vanilla | 0 | 48.59 | - | - | - |
| | cm | 0.250 | 0.00 | 48.59 | 0.20 | 1.37..1.41 |
| | natural | 0.150 | 0.00 | 48.59 | 0.12 | 3.45..3.73 |
| idlehh -> hit (1100) | cm | 0.250 | 0.00 | 56.23..56.29 | 0.20 | 1.41..1.68 |
| | natural | 0.075 | 0.00 | 51.39..56.26 | 0.07 | 11.00..14.36 |

The intent fires (finding 1): 240 frames want to move, 59 start and 36 stop in each `natural` run (0 in the others: no tracker). The stop in `natural` takes 0.375 s (the stop rule) and its excess jerk is 0.31..0.88 against 1.12..1.47 (`cm`): the ranges do not overlap, so that difference is real; part of it comes from the transition rules' own stop factor, which plain `transition rules = true` also gives (the intent changes a stop only when the actor did NOT let go: a flip, base time; the scenario has no blocked stop, so the flip is untested in the game). Idle life (largest angle between any two frames of the idle phase, frames 2 to 289, about 4.8 s): `bip01 spine1` 0.00 in vanilla, cm and cmoff, 1.72 degrees in natural (every run); `bip01 head` 1.89 degrees in every variant (the engine's own head motion, so the spec's "with CM off they are 0" does not hold for the head, and the style's head part cannot be told apart by this measure).

**Acceptance, judged with the new measures:**
- **M1, no step at a group change: met.** The step at G is 0.00 at every group change of every CM variant (vanilla: up to 56.5 degrees).
- **The stop is longer and smoother than CM with defaults: met in the measure that sees the blend.** D 0.375 s (inside the spec's 0.3 to 0.8 s), excess jerk 40 to 75 percent lower than `cm` with non-overlapping ranges; the offset decays to a tenth in 0.28 to 0.30 s (the lower end just under 0.3 s). The spec's original M3/M5 over F..F+8 cannot judge it (finding 2) and are not used: their values (stop jerk cm 5.40..5.47, natural 4.07..6.20) overlap.
- **Idle life 0.3 to 2 degrees: met for the spine (1.72), not judgeable for the head** (1.89 in every variant, engine motion).
- **Start of walking unchanged: met, now with the start rule able to fire** (D 0.250, excess 0.12..0.13 in both; the default start factor equals the rules').
- **Preset adds at most 0.05 ms per actor: NOT measured** (no 20-NPC scene).
- **Unchanged when off: met within the variation between runs, not byte for byte.** Byte-identical traces are not possible: the game has no fixed timestep and frame times differ between two runs of one build. No build of the parent commit was made (a second full build tree; not done), so the comparison is: (a) code reading: with the preset off and the three settings at their defaults no tracker exists, `preAnimation` skips the new block (`mIntentTracker == nullptr`), and the trace adds nothing; (b) `cmoff` (everything written explicitly off) against `cm` (absent): the group-change measures overlap at every change; (c) `vanilla` and `cm` against the stage 0 reports of the build `4bb7d989c9` (`vanilla-1.json`, `cm-1.json`): every event's step@F equal within 0.3 degrees except sheathe (10.58 against 9.70..9.84). Caveat: the old window measure differs between `cm` and `cmoff` by up to 0.5 degrees (stop jerk 5.40..5.47 against 5.71..5.90) although both run the same code, so three runs underestimate the spread of that measure; differences below about 0.5 degrees in it are noise.
- **What the new measures show beyond the stop:** the transition rules (task 0053, part of the preset) make turning out of a turn in place (0.15 s), walk to run (0.2 s) and hits (0.075 s) shorter than CM's base 0.25 s, and their excess jerk is higher (turn out 3.45..3.73 against 1.37..1.41; hit 11..14 against 1.4..1.7). That is the rules' design (snappy hits and turns), now visible: for toddhow.

**Not verified:** toddhow's judgement of a recorded comparison (the spec's real gate), NPCs (the intent runs for every actor with a pose driver; only the player was measured), first person, a frame check (nothing draws differently by design), the cost per actor, the preset in the launcher, a blocked stop (flip) in the game, the Linux gate, clang-tidy. The windows build of `components-tests` could not be linked during this work because other agents' unfinished test files did not compile; the tests named here ran in a test executable linked from the same objects (`AnimTrace*` and `CreativeMotion*`: 206 pass).

**Open questions for toddhow (stage 1):**
1. Intent rule data beyond the spec: try a start factor below 1 (the first draft used 0.8) and a fast/slow split for turns in place (0.6 / 1.0), or keep the defaults equal to the plain rules?
2. The plain transition rules' short blends (turn out 0.15 s, walk to run 0.2 s, hits 0.075 s) now measure as more jerk than CM's base 0.25 s: keep them (snappy) or lengthen?
3. Should standing up from a crouch, and a stop with a weapon drawn (to `idle1h`/`idlehh`), count as stops? Both need a change to `classifyGroup`, which the plain rules share.
4. Accept the new acceptance measure for stage 1 (D at the real change, excess jerk against the target, decay of the offset) in place of the window measures M3/M5?
