# CreativeMotion transitions: the baseline of the current build (task 0067, stage 0)

2026-11-03. Written for toddhow. This is the measurement every later stage of `docs/tasks/0067-creativemotion-in-game-runtime.md` is compared against. Everything below comes from running the game; nothing is from memory. Raw reports (JSON, canonical, two runs per variant) are in `docs/research/0031-creativemotion-baseline-data/`.

## What was measured

One scripted scenario, run in the real game, third person, with `[Game] creativemotion animation debug` on. The player stands, walks, runs, stops, turns in place, draws a weapon, attacks, takes a small hit, and puts the weapon away. Each phase is a fixed number of frames (not seconds), so two runs change phase on the same frame; the scenario is `tools/creativemotion_check/mod/scripts/cmcheck.lua` and is run by `tools/creativemotion_check/check.sh`. The game writes one `CMTRACE` line per frame (the group each bone group plays, position, movement, speed and the local rotation of six bones: pelvis, left and right thigh, spine, left upper arm, head) and the script writes a `CMPHASE` line at each change. The program `creativemotioncheck` (`components/animtrace/`, 39 unit tests) reads both and measures, for every event at frame F, over the six bones:

| Measure | Meaning |
| --- | --- |
| `step@F` (M1) | the largest rotation step, in degrees, between the frame before the event and the event's own frame |
| `step8` (M2) | the largest step in the 8 frames after it |
| `jerk` (M3) | the largest change of the turn rate (the angle between one frame's rotation step and the previous frame's), in degrees per frame squared, from F through F+8 |
| `speedstep` (M4) | the largest frame-to-frame change of ground speed (from the trace's `moved`), in units per second, from F through F+8 |
| `settle` (M5) | frames and seconds after F until every bone has a step under 1.5 degrees (note 0022's noise floor) for 8 frames in a row, inside the event's own phase; "never" if it does not happen |

A step is the angle of the rotation that takes the previous frame's local rotation to this frame's. The measures are from the traces' four-decimal quaternions, so steps under about 1.5 degrees are noise.

Variants: `vanilla` (every feature off), `inertial` (`[Game] inertial locomotion = high`, toddhow's setting), `cm` (`inertial locomotion = high` and `creativemotion animation = true`, transition time left at its default). The window is 960x540, 60 frames per second, no vsync, in `Seyda Neen` (open ground, the player walks about 700 units without touching anything).

## The baseline (run 1 of 2 each; frames are the trace's frame number)

`step@F`, `step8` and `jerk` in degrees (jerk per frame squared); `speedstep` in units per second.

| Event | vanilla step@F | vanilla step8 | vanilla jerk | inertial step@F | inertial jerk | CM step@F | CM step8 | CM jerk |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| walk (idle to walk) | 2.40 | 2.21 | 2.78 | 2.70 | 2.82 | **0.00** | 0.46 | 0.24 |
| run (walk to run) | **48.97** | 4.23 | 50.41 | 35.90 | 38.27 | **0.00** | 7.03 | 3.20 |
| stop (run to idle) | **45.54** | 0.07 | 45.56 | 41.59 | 44.39 | **0.00** | 5.24 | 5.69 |
| turn (start turning) | 4.02 | 3.05 | 3.97 | 5.16 | 5.11 | 0.00 | 3.37 | 3.08 |
| stop2 (turn to idle) | 0.00 | **48.59** | 48.60 | 0.00 | 48.60 | 0.00 | 4.43 | 1.54 |
| draw (weapon out) | 5.39 | 5.82 | 5.39 | 5.22 | 5.22 | 0.00 | 6.22 | 5.54 |
| attack | 9.48 | 2.73 | 9.36 | 8.97 | 8.86 | 0.00 | 3.16 | 3.36 |
| hit (random hit1, hit2 or hit4) | **56.22** | 5.64 | 56.22 | 55.82 | 55.82 | 0.00 | 7.85 | 4.29 |
| sheathe | 10.58 | 4.68 | 10.46 | 9.85 | 9.74 | 0.00 | 4.87 | 3.91 |

Settling (`settle`, run 1): vanilla stop 1 frame (0.02 s: it cuts), CM stop 14 frames (0.23 s); vanilla hit 69 frames (1.15 s), CM hit 74 frames (1.23 s, the hit clip itself); walk and run never settle in any variant, because the walk and run clips move the thighs by more than 1.5 degrees per frame all the time. Ground-speed steps at the walk event: vanilla 109 u/s in one frame (movement starts at full speed), `inertial` and `cm` 16 u/s (the ramp over about 0.5 s).

## What this says, in plain words

- **Vanilla cuts.** At a change of animation group the pose jumps by up to 49 degrees on one frame (walk to run 49, run to idle 46, hit 56). After the jump it is calm (`step8` 0.07 for stop). This is Morrowind's own behaviour: `Play::getActiveState` shows it does no blending between groups.
- **Inertial locomotion alone does not blend poses.** It changes speed (the walk event's ground-speed step falls from 109 to 16 units per second) but every pose jump is still there (36 to 56 degrees).
- **CreativeMotion's blend does work at group changes.** With it on, the one-frame jump is 0 at every event, and the largest jerk at any event is 5.7 degrees per frame squared against 56 degrees in vanilla. What it costs: the change is spread over the next frames (`step8` up to 7.9 degrees where vanilla has 0.07 to 5.8 except one 48.6 spike, and a stop that settles in 0.23 s instead of cutting). So the blend removes the pop and replaces it with a short, quiet blend, which agrees with note 0022 ("left thigh single-frame step at a group change: vanilla 17 to 39 degrees, CM 0 to 1.4"; this table reports the worst of six bones, which is why the vanilla numbers are larger).
- **Why it is hard to see.** The thing CM removes is a single-frame pop; the thing left is a ramp of up to 8 degrees per frame lasting about a quarter second, over an animation that has the same poses before and after. There is no anticipation (no lean into a stop), no separate start or stop clip, and idle to walk (`walk` row) has almost nothing to blend because the walk clip starts near the idle pose (vanilla step at the event is already only 2.4 degrees). Stage 1 and 2 of the spec address that, not this stage.

## How repeatable it is

Each variant was run twice. Event frames are identical between runs. Comparing run 1 with run 2, the largest differences over all events except the hit, in degrees: vanilla step@F 0.21, step8 0.72, jerk 0.18; inertial 0.49, 0.17, 0.72; CM 0.06, 0.71, 0.30 (speed step at most 1.4 u/s). The hit differs by up to 1.1 degrees (CM) because Morrowind picks one of several hit animations at random (`hit1`, `hit2` and `hit4` appeared), so a hit is only comparable between runs that played the same one.

So the runs are close but not identical: the spec's acceptance "reproduce within 0.5 degrees, twice, identically" is **met for most events and missed by up to 0.72 degrees** on `step8` (vanilla sheathe, CM stop2) and by the random hit. The cause is that the game has no fixed timestep (the frame limiter gives about 16.7 ms frames that vary by a millisecond or two, and animation time follows them). Making runs byte-identical would need a fixed simulation step in the engine, which is outside this stage; proposal for the spec: accept a tolerance of 1 degree on `step@F`, `step8` and `jerk` between runs of the same build, and treat the hit separately.

Note 0022 contains no table of per-event numbers (only a left-thigh range at group changes and the speed series), so "the table of note 0022" cannot be compared line by line; the numbers here are the new reference, and they agree with its one number for CM (0 to 1.4 degrees at the group change).

## What could not be measured

- **First person.** The body's bones are not updated in vanilla there (note 0022), so no trace.
- **Death and NPCs.** The scenario does not kill the player and records only the player; NPC actors would need `--ref <id>` and a scripted NPC.
- **Turn rate and anticipation.** The scenario turns at a fixed 0.03 radians per frame; whether a smoother stop or turn "looks right" is toddhow's judgement (the spec's recorded-video comparison), not a trace measure.
- **The 0.8 s idle-pose slide of the video.** Not examined by this scenario. The `walk` and `run` `settle` values say little (1 frame or "never"): the walk and run clips themselves move the thighs by more than 1.5 degrees per frame, and the Lua control takes effect a few frames after the event is logged, so "settled" there only means the motion has not started yet. His executable's build date is unknown.
- **Strafing, backwards, sneaking, swimming, jumping and spellcasting** are not in the scenario.
- **A hit during a walk or run.** The hit is taken standing with the weapon drawn.

## Provenance

- Executable: `build-jolt/openmw.exe` built 2026-11-02 22:02. No commit that touches animation code (`apps/openmw/mwmechanics`, `mwrender/creativemotion*`, `actorik*`, `components/creativemotion`, `components/inertialmotion`) was made after that time (checked with `git log --since`), so it matches the tree at `4bb7d989c9` for animation behaviour.
- Config: a copy of the data, content and fallback lines of toddhow's `openmw.cfg` (the copy under `I:\Projects\OpenEden-work\lodtest\config`) plus the scenario mod; a fresh `settings.cfg` per run. His own files were not touched.
- The Ebonheart Grand Council Chambers, named in the spec for its flat floor, are too small: the player runs into a wall after 145 units, which made the run and stop numbers meaningless (found on the first run; the scenario now defaults to `Seyda Neen`).
- Re-run: `tools/creativemotion_check/check.sh I:/Projects/OpenEden/build-jolt <copy of openmw.cfg> <empty work folder> [vanilla inertial cm]`, `REPEAT=2` for the repeat comparison. About 35 seconds per run.
