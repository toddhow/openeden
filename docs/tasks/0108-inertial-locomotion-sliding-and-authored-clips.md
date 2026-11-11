# Task: Inertial locomotion sliding and the unused authored clips

Status: built and measured on Windows, 2026-11-10; not pushed. Full loop in form only (the same hand wrote the tests and the
code; no separate test writer, no fresh reviewer). Research and numbers: `docs/research/0052-inertial-locomotion-sliding-and-unused-clips.md`.

Design doc section: `docs/design/inertial-locomotion.md` 3.3 (authored transitions and the procedural fallback) and 3.4 (foot
stance planting: "the IK absorbs residual stance displacement" was never horizontal).
Goal: find why feet slide with inertial locomotion and why the authored start, stop and turn clips never play; fix what is a
code fault and what the evidence makes measurable. Two causes of sliding were found: the stop (CreativeMotion's blend into idle,
fixed by `foot lock`) and the constant body speed against the clip's varying root speed while walking (fixed by `inertial stride sync`).
Inherited code touched: none of OpenMW's. OpenEden code: `apps/openmw/mwmechanics/character.cpp`
(`CharacterController::preAnimation`, the movement state choice; `traceAnimation`), `apps/openmw/mwrender/actorikcontroller.*`
(`applyPostAnimation`, `solveLeg`), `components/inertialmotion/` (`locomotionstate.hpp`, new `footlock.hpp`),
`apps/openmw/mwphysics/` (`Actor::setStrideShape`, `ActorFrameData::mStrideShape`, `MovementSolver::move`),
`components/settings/categories/game.hpp`, `files/settings-default.cfg`.
Inputs / outputs: new settings `[Game] foot lock` and `[Game] inertial stride sync`; trace lines `CMFEET_L/R` and `yaw` (in `CMTRACE`), `CMFEETIK`, `CMLOCK`
(only with `creativemotion animation debug`); one Info line per session about the authored clips.
Constraints: off by default; with `foot lock` false (and with `foot planting` false) the pose and movement are unchanged; no
allocation in the foot lock (a value type); main thread (`ActorIkController::applyPostAnimation` runs on the main thread, see
its comment); determinism: the state machine has no clock or random input.

## Acceptance criteria

- [x] The root cause of "the authored clips never get used" is established with evidence (research note, section "Short answers" 1).
- [x] The root cause of the sliding is established with evidence and a repeatable measurement (`tools/footslip_check`).
- [x] The start clip can be chosen on the first frame of input (`InertialMotionAuthoredStart` tests; was impossible).
- [x] A missing clip is no longer silent (one Info line per session, seen in the game's log).
- [x] With `foot lock` on, the stance foot's slip after a stop is lower: mean peak over the eight stops of the scenario 60.8 to 23.6
  units per second, ground covered 16.5 to 7.8 units (second run 45.9 to 26.1, 14.0 to 7.7). One stop (after a running arc) stays at a peak of 94.
- [x] With `inertial stride sync` on, the median difference between the body's speed and the clip's root speed in walking
  falls from 11.8 to 3.5 units per second (vanilla 1.9), run 2.3 to 0.1, arc walk 9.7 to 4.0, strafe 18.2 to 7.9; the mean ground speed is unchanged within 2 percent.
- [x] With `foot lock` off the animation outputs are identical to the build before: the `CMTRACE` lines of the player (without the
  new fields) of two builds over the whole scenario differ only in the physics position of 1 to 6 frames of 1893 (one physics step,
  the noise two runs of one build have), not in group, animation speed, locomotion state or bone rotations.
- [ ] A pass of toddhow's own eyes on the walk with `inertial stride sync` (not judged by anyone).
- [ ] A pass of toddhow's own eyes on the stop with `foot lock = true` (the wide stance for a second or two, see the research note).
- [ ] Authored clips exist and play (not done: data work, see below).

Out of scope: authoring the clips; retargeting `humanoid-locomotion.glb`; a stop that ends with a procedural step; slopes;
first person.

## What an authored clip needs (for whoever authors them)

The engine chooses a clip when `MWAnim::Play::hasAnimation(<group>)` is true, that is when one of the actor's `.kf` animation
sources has the group in its text keys (`<group>: start` / `<group>: stop`, lower or any case), on Morrowind's `Bip01` skeleton.

| group | chosen when | then |
| --- | --- | --- |
| `walkstart`, `runstart` | movement input on the first frame, the actor stands (or turns in place) and its smoothed speed is below 20 units per second | plays once (no loop; `autodisable`), then the walk or run cycle |
| `walkstop`, `runstop` | movement input released while the smoothed speed is above 5 units per second | plays once; ends early if input returns or the smoothed speed falls to 5 |
| `runskid` | movement input against the smoothed velocity (a reversal) while the locomotion phase is Skidding | plays once |
| `turnleft90`, `turnright90` | no movement input and the actor rotates in place, `[Game] inertial locomotion` on and `enable authored transitions` | while it rotates (any angle: the 40 to 70 degree thresholds of the design are not implemented) |

While one of these plays, `chooseMovementRequestSource` makes the animation's root motion the movement request (the clip must
carry its own root motion) and the cycle's speed scaling does not apply. The log line at start says which of the seven exist.

## Stage log

### 2026-11-10: investigation, tests, foot lock

Method and numbers are in the research note. Summary of what was done, in order:

1. Read the design, `character.cpp`, `movementsolver.cpp`, `locomotionstate.*`, notes 0022 and tasks 0053 and 0067. Searched every
   data source for the seven group names: none (note, "Short answers" 1). A copy of the CM clips exists only as a glTF of CM's
   20-bone rig.
2. Built the measurement: `CMFEET_L/R` and `yaw` added to `CMTRACE` (a `BonePose` is built per call, not kept: the first version
   cached it and the player's trace stopped when the camera mode changed and the animation object was replaced); `tools/footslip_check`
   (`check.sh`, `mod/scripts/footslip.lua` and `footslip_global.lua`, `slip.py`, `sheet.py`). First locations (Seyda Neen: hills, a
   wall) invalidated; ToddTest with teleports between movement phases used.
3. Found the stop glide (above), the unreachable start clip, the silence. Tried `[CreativeMotion Transitions] stopping = 0.1`
   and `2.5` to confirm that the glide scales with the blend length.
4. Tests first for `shouldPlayAuthoredStart` and `FootLock` (stubs that never choose / never lock): 10 of the 19 new tests red against the
   stubs (the other 9 pass trivially against a stub that does nothing or never locks, for example "does nothing while the body
   moves"); all 45 (26 existing and 19 new) pass with the code. **Two of the foot lock tests were corrected after they failed against the implementation** (the hold starts
   within one frame's drift of the shown position, not exactly at it; the time to slide is measured from the end of the
   engage frames): the tests had assumed a stricter behaviour than the design needs; the behaviour was not changed to fit them.
   A third test (a shown position far from the animated one, a teleport) was **written after** the game run showed the fault (a
   stale position from before a teleport pulled the foot 16 units; run `profilelock`, `arc_run` start) and was not run
   red against the code before the fix.
5. Integration in `ActorIkController`, measurements with the IK ankles (`CMFEETIK`): the first measurements read the
   animated pose and showed no effect of the lock (the lock acts after them); the metric was moved to the final pose.
6. The stride sync (added after a review of the first result, which pointed out that the ankle proxy could not
   see a constant-speed skate): tests first (`InertialMotionStrideShape`, 3 of 5 red against a stub that returned 1), the
   shape in `CharacterController::updateStrideShape`, `Actor`, `ActorFrameData` and `MovementSolver::move`; measured with the
   direct body-against-root-motion comparison. The first build of it failed with the compiler out of heap space on four
   other files (parallel builds with other work on the machine; a rerun at `-j 2` built); no code issue.
7. `hasAnimation` was asked for every actor with movement input every frame (an eager argument); now only when the rest of
   the start rule holds.
8. clang-format 14 (docker image `openeden-cf14`) on the changed lines.

Private test build (the whole `components-tests` was not built): `cl` with gtest from the build folder, the five inertial motion
test files and `locomotionstate.cpp`: 46 tests pass (26 existing, 20 new: `InertialMotionAuthoredStart` 4, `FootLock` 16).
`CI/check_tests_registered.sh` ok; the two new test files are registered in `apps/components_tests/CMakeLists.txt`.

Decisions for toddhow:

1. Whether `foot lock` should be on by default with `foot planting` (it changes how a stop looks: a wide stance for 1 to 2 s instead of a quick glide).
2. `FootLockParameters::mSlideSpeed` (8 units per second) and `mStillSpeed` (8): faster looks closer to the old glide, slower holds the stance longer.
3. Whether to author stop clips first (the right fix), and who retargets `humanoid-locomotion.glb`.
4. The stop after a running arc still has a peak of 94 units per second; wanted, or follow up?
5. `inertial stride sync` and `foot lock` on by default with their prerequisites? (both are on in the test profile only)
6. `slip.py` is Python (3.15 on this machine); the repository's other check tools are shell and C++. Keep, or port to `creativemotioncheck`?

Not run: see the research note ("Not run").
