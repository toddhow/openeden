# Inertial locomotion: the foot sliding, and why the authored clips never play (2026-11-10)

Written for toddhow, who reported two things: with `inertial locomotion` on, feet slide over the ground; and the animations
meant for inertial locomotion "seemingly never got used". Everything below is measured with the tools of task 0107
(`tools/footslip_check`) or read in the code at the commit named in the task; where something is not measured it says so.
Task spec: `docs/tasks/0107-inertial-locomotion-sliding-and-authored-clips.md`.

## Short answers

1. **The authored clips never play because no such clip exists in the game, and, separately, the start clip could not have
   been chosen even if one did.**
   - The design (`docs/design/inertial-locomotion.md` 3.3) and `character.cpp` look for the animation groups `walkstart`,
     `runstart`, `walkstop`, `runstop`, `runskid`, `turnleft90` and `turnright90` (`CharacterController::preAnimation`, through
     `MWAnim::Play::hasAnimation`, which only looks at the text keys of the `.kf` animation sources). None of Morrowind's
     animation files has any of them (searched the three installed BSAs and the 163 loose `.kf` files for those names: no hit),
     and no file in the repository or the test profile is a `.kf` with them. The one place the repository holds clips with
     similar names, `components/creativemotion/data/clips/humanoid-locomotion.glb` (`WalkStart`, `RunStop`, `RunSkid`,
     `TurnLeft90`, `TurnRight90`), is CreativeMotion's own 20-bone sample rig; nothing in the engine loads it, and playing it on
     Morrowind's `Bip01` skeleton would need retargeting, which task 0067 lists as out of scope. So `hasAnimation` is false for
     all seven, every time, and the procedural fallback (the walk or run cycle slowing down) is what you have always seen.
   - The check is silent. Added: one log line per session that says which of the seven clips the player's animation has.
     On the test profile: `Inertial locomotion: authored transition clips found: none; missing: walkstart, runstart, walkstop,
     runstop, runskid, turnleft90, turnright90`.
   - A code fault would have kept the start clip unused even with the data: the choice required
     `LocomotionPhase::Starting`, but the choice is made at the top of the frame, before the physics step that first moves the
     actor. Trace of the first frames of a walk (`CMTRACE`, `loco=(speed ... phase ...)`): frame 1 has speed 0, phase 0 (Idle),
     frame 2 phase 1 (Starting); on frame 1 the movement group is already `walkforward`, so on frame 2 the actor is no longer in
     the "no movement state" the rule also asked for. The two conditions were never true on the same frame. Fixed
     (`InertialMotion::shouldPlayAuthoredStart`). The other clips (stop, skid, turn) were reachable by their rules.
   - Not fixed, because it is data work: nobody has authored the clips for Morrowind's skeleton. toddhow decided on 2026-11-03
     that we author our own (task 0067, answer 3); that still needs the clip authoring and licensing note and a way to put
     the clips into the actor's animation sources (a `.kf` with these text keys, or retargeting of the glTF clips). Names and
     rules a clip must satisfy are in the task spec, "What an authored clip needs".
2. **The foot sliding is real, but it is not in steady walking.** Against the same scenario with every feature off
   (vanilla), steady walking, running, arcs, reversing and strafing have the same stance-foot slip with inertial locomotion
   (the clip itself is not perfectly planted at the ankle: about 20 units per second, 18 percent of the walking speed, in
   vanilla too). What inertial locomotion adds is at the **stop**: with `creativemotion preset = natural` (the profile) the
   stance foot glides back to the idle position for about 0.4 s after the body has stopped, at up to 75 to 107 units per
   second. Cause: the group changes from the walk or run cycle to idle when the smoothed speed falls below 5 units per second,
   and CreativeMotion blends the pose over `creativemotion animation transition x stopping` (0.35 s x 1.5) from the frozen
   mid-stride pose to the idle pose. The body no longer moves, so the foot on the ground moves. The slip is proportional to the
   length of that blend (measured on the stop after walk-then-run, 75th percentile of the stance slip: `stopping = 0.1` 1.7 units per second, the default 35.7, `stopping = 2.5` 33.9 with the animated feet; the three runs were taken before the IK feet were traced).
   Fixed behind a new setting, `[Game] foot lock` (off by default, needs `foot planting`).

## What was measured and how

`tools/footslip_check/check.sh` runs one scripted scenario (`mod/scripts/footslip.lua`) in ToddTest (an interior; a long
corridor and a hall, the player is teleported back to the start of the space before each movement phase) with a fixed frame
time (`OPENEDEN_FIXED_FRAME_TIME=0.0166667`) and `--random-seed 1`: idle, walk, stop, run, stop, walk then run, stop, a walking
arc, a running arc, strafe, walk then reverse, a sharp running turn, each followed by a stop. The player's ankles are traced
in world space on every frame (`CMFEET_L/R` of the `CMTRACE` line: the animated pose; `CMFEETIK`: the ankles after foot
planting and foot lock) and `slip.py` reads the log.

The metric ("stance slip"): per frame, of the ankles that are low (within 1.5 units of that ankle's own low level, the 5th
percentile of the run) the slower one is the stance foot; its horizontal speed over the ground is the slip, zero for a foot that
holds still. Phases are reported by median and 75th percentile. **Limits:** it is a proxy. The ankle bone is not the sole,
the classification by height is ambiguous while a foot sweeps just above the ground (the table's `run` and `sharp_turn` rows
have few low-foot frames, and the 75th percentile there is the swing foot), and the same classification gives a nonzero floor
in vanilla. Read the differences between variants, not the absolute numbers. Positions between two runs of one build differ by
one physics step on a few frames (the same noise note 0022 describes); animation outputs of the "off" runs are identical.

### Steady motion: vanilla against inertial (same scenario, animated pose, stance slip median / p75, units per second)

| phase | vanilla | inertial high | profile (inertial + CreativeMotion natural + IK) |
| --- | --- | --- | --- |
| walk | 21.2 / 63.9 | 22.7 / 55.1 | 20.8 / 27.7 |
| arc walk | 34.8 / 81.7 | 30.0 / 105.4 | 26.5 / 38.3 |
| reverse walk | 13.6 / 34.2 | 21.1 / 46.5 | 19.3 / 28.1 |
| strafe left | 56.7 / 87.0 | 37.6 / 54.4 | 36.8 / 52.9 |

(The profile column is from the run with IK feet; vanilla and inertial from the animated feet: not the same pose stage, see
the limits above. The point is that none of the inertial columns is worse than vanilla in steady motion.)

A cause that does not show: the body moves at the smoothed constant speed while the walk clip's own root speed varies
(113 to 155 units per second at a mean of 128 over a step), so one could expect periodic skating. It is there (the
difference between the body and the root motion has a median of about 12 units per second in a walk) but vanilla's
stance foot slips the same 21 units per second, so it is not what separates inertial locomotion from vanilla; no change
was made for it.

### Stops (profile, `creativemotion preset = natural`, body below 8 units per second; peak = largest 5-frame mean)

| stop after | peak before | peak with foot lock | stance-foot ground covered, before / after (units) |
| --- | --- | --- | --- |
| walk | 19 | 8 | 7.3 / 6.5 |
| run | 18 | 8 | 5.7 / 4.6 |
| walk then run | 75 | 12 | 22.6 / 5.4 |
| walking arc | 14 | 8 | 4.6 / 4.4 |
| running arc | 104 | 94 | 29.8 / 18.8 |
| strafe | 107 | 24 | 18.1 / 6.1 |
| reverse | 75 | 12 | 21.6 / 6.4 |
| turn | 75 | 23 | 22.0 / 10.1 |

Over all eight stops: mean peak 60.8 down to 23.6 units per second, mean ground covered 16.5 down to 7.8 units (second run
of the same comparison: 45.9 down to 26.1 and 14.0 down to 7.7). Moving phases are unchanged within the spread between
runs (walk 20.8 / 21.6, run 8.3 / 8.0, arc walk 26.5 / 26.5, reverse 19.3 / 19.3, strafe 36.8 / 36.8). The remaining case
is the stop after the running arc (peak 94): not investigated.

### How the stop looks

`I:\Projects\OpenEden-work\inertia\run10\before_stop_wr.png` and `after_stop_wr.png` (side camera at foot height following
the actor, one cell every 6 frames from the start of the stop after a walk-then-run; the red ticks are fixed screen
columns). Before: the legs go from the stride to the idle stance within about half a second. After: the feet stay where
they were and the stance closes slowly over about two seconds (it is still not closed in the last cell, 1.5 s).

**This is a trade, and the look is for toddhow to judge.** The glide is gone, and in its place the character stands in a
wide stance for a second or two. The slow settle is `FootLockParameters::mSlideSpeed` (8 units per second);
a larger value shortens the wide stance and brings back a little of the glide. A stop that ends with a short step needs
a stop clip (see above) or a procedural step, which is more work than this task.

## Other findings (measured, not changed)

- **The start is not the problem it looks like.** The first 18 frames of a walk (body 0 to 62 units per second): the stance
  foot slips 4 to 14 units per second in both the inertial and the profile run. The animation speed starts at the floor
  of 0.15 of the intended speed (`animspeed 0.125` for 5 frames) while the body is at 0 to 19 units per second: close. The
  slide of toddhow's video of 2026-11-02 (note 0022) is still not reproduced.
- **The walk cycle is cut mid-stride at the stop.** At 5 units per second the group changes to idle wherever the cycle is;
  in the contact sheets the trailing foot is still in the air at that moment in some stops. A stop that is aligned to the
  cycle (decelerate so that the foot lands as the speed reaches zero) would remove the cut; not built.
- **A run that stops changes to the walk cycle at once** (`stop_run` frame 1: `walkforward` at animspeed 1.5, because the
  smoothed speed 230 is above the walk speed). The same ground speed is consistent with the walk clip at 1.5 times, so
  there is no slip from it, but it is a different gait.
- **Slopes:** `MovementSolver::move` skips the smoothing when the actor is on a slope (`mIsOnSlope`), so the locomotion
  state stays at its last value and the animation speed (read from it) is stale there. Not measured (the scenario is flat).
- The first two scenario locations (Seyda Neen) were unusable: hills (so `mIsOnSlope` switched the smoothing off, which
  falsified the first numbers), a wall the player walked into (the animation kept playing at 96 units per second against a
  blocked body). Those numbers are not used.

## What was changed

- `components/inertialmotion/footlock.hpp` (new): `FootLock`, a pure state machine per foot.
- `apps/openmw/mwrender/actorikcontroller.*`: with `foot planting` and `foot lock` on, the ankle target of the leg solve
  moves toward the held position while the actor stands still and the animated foot is on the ground; the body speed is the
  smoothed position change.
- `components/inertialmotion/locomotionstate.hpp` and `character.cpp`: `shouldPlayAuthoredStart` (start clip rule),
  and the once-per-session log of which authored clips exist.
- `[Game] foot lock = false` (`files/settings-default.cfg`), on in `tools/testprofile/profile.cfg`.
- Trace additions (only with `creativemotion animation debug`): `CMFEET_L/R` and `yaw` in `CMTRACE`, `CMFEETIK`, `CMLOCK`.

## Not run

The Linux gate (no GCC here); clang-tidy; the Lua integration tests; `components-tests` and `openmw-tests` as a whole (the 46
tests of the inertial motion files were built and run privately, with MSVC); the authored start clip in the game (no clip
exists; unit tested only); NPCs (`inertial locomotion npcs` is on in the profile, but the scenario and the trace are the
player's); slopes; first person (the body is not drawn); other hardware; a fresh review.
