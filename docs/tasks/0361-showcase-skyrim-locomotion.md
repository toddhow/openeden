# Task 0361e: Skyrim showcase, animation (audit items 8 and 9: walk and run barely read, starts and stops, guards stand alike)

Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.11 and 4.12; follow-up of tasks 0148 (locomotion clips), 0145 (idles), 0222 (the player's body), 0108 and 0067 (inertial locomotion, CreativeMotion in the game) and of the showcase audit (`docs/research/0149-skyrim-showcase-audit.md`, items 15 to 17).
Status: written 2026-11-19, one hand wrote tests and code, no fresh reviewer; Windows only; local branch `skyanim-0361e`, not pushed.

Goal: find out why a Skyrim walk and run "barely read" and guards stand alike, and fix what the evidence shows.

Inherited code touched: `apps/openmw/mwrender/skyrimlocomotion.*` (`Controller::run`), `apps/openmw/mwrender/scene.cpp` (`insertNeutralActor`, the controller's construction), `components/havokpack/locomotion.*` (`Params`, `wantRun`), new `components/havokpack/idlefidget.*`.

## What the evidence shows

Showcase harness `tools/showcase_check` (group `anim`, new), 1,600 x 900, fixed frame time, `[Video] framerate limit = 20` (so that 0.1 simulation seconds is 0.3 real seconds and the screenshot queue, at most 8 copies, keeps up: at 60 it skipped about 100 of 210 frames and `rename.py` then misnamed the rest), a side camera (`tp = { 0, 50, 70, 260 }`, the new fourth number is the sideways offset), 10 pictures a second. New harness aids: `turn`, `stopAt` (shots), `tp` side offset.

1. **The audit's walk and run frames were taken before the player's Skyrim body existed, and from behind.** The pack builds the player body only after its actor records are read on a worker (`SkyrimWorldView::playerBody` answers revision 0 until then): `PLAYERBODY built` is logged about 30 s after the start (23:07:24 for a start at 23:06:51; 20 to 33 s in three runs). The harness shot at 8 s real. Until then the player is drawn as the stub with Morrowind's parts and Morrowind's animation system (the `missing: runstart, walkstop` line of task 0108 belongs to that body), which is what "after the first step the pose hardly changes" saw. A camera straight behind a walking person cannot show a leg cycle either. With the body built and a side view the run is a readable cycle (frames `keep/fid_on` p_run, p_runstop, p_runturn).
2. **The gait phase advances and the clips play** (`OPENEDEN_NEUTRAL_LOCO_TRACE`, which now names the actor): for the player, 26 to 30 lines per shot, `phase` advancing by `cps` a second, one gait at a time. What differs is the SPEED: the player's ground speed is Morrowind's. Walk: mean 149 units/s (sd 20, range 95 to 182) against the clip's 82.45, so the walk cycle plays at 1.77 times its natural rate; run: 260 to 272 against 350.87, rate 0.75. The speed also jitters 130 to 180 from sample to sample at 20 frames a second (position over frame time; the 0.12 s smoothing absorbs most of it).
3. **The walk gave way to the run only at 216 units/s (the half way speed)**, so a walk at 200 played at 2.4 times its rate; the geometric mean (170) plays walk and run at the same ratio.
4. **Skyrim has no start or stop clips.** Survey of `Skyrim - Animations.bsa`, `actors/character/animations/<male|female>/`: `mt_idle`, `mt_walk*`, `mt_run*` (forward, backward, left, right and four diagonals), `mt_sprintforward`, `npc_turnleft90`, `npc_turnleft180`, `npc_turnright90`, `npc_turnright180`, `npc_turntowalkleft90/180`, `npc_turntowalkright90/180`, and the idle variants `mt_idle_a_*`. No `walkstart`, `walkstop`, `runstart` or `runstop`: the game's graph blends idle and cycle by speed, which is what `Gait::weights` does (the idle fades out between 5 and 30 units/s, 0.12 s smoothing); the stop frames (`p_runstop`) show the figure back at the idle in about 0.4 s. The turn clips exist but need a turn-in-place state; not built here.
5. **Guards were never in the same idle phase**: the idle's start is `SceneryKeyframes::phase(hash(ref id))` (task 0145). What makes a row of guards "indistinguishable" is the clip: `mt_idle` is a very slow sway (over 20 s of pictures the standing player does not visibly change), so a different phase of the same sway is no difference.

## What is built

- **Idle fidgets** (`[Game] neutral actor idle fidgets`, default true, Skyrim people only; `components/havokpack/idlefidget.*`): a standing person plays one of ten one-shot variants of the game (`mt_idle_a_sway_fast`, `_leg_shift`, `_shoulders`, `_soft_right`, `_left_long`, `_sigh_var1`, `_sigh_var2`, `_shakeout`, `mt_idlelookinga`, `_b`) over its idle with a 0.35 s fade in and out, the first 2 to 10 s in and then one every 12 to 24 s; variant and schedule come from the reference alone (splitmix64, no clock, no random numbers), so a row of guards does not move as one and a visit repeats. Not while walking (the variant takes the idle's share of the blend only).
- **Walk to run at 170** (same setting): `Gait::Params::mSwitchSpeed` (0 = the half way speed, so every older caller and test is unchanged), `Gait::balancedSwitchSpeed` = sqrt(walk speed x run speed). Creatures keep their own rule.
- Trace: `NLOCO <actor>` and `NFIDGET <actor>` lines (`OPENEDEN_NEUTRAL_LOCO_TRACE`).

Constraints: pure functions, nothing allocates per frame (the variant's clip is decoded once on the idle's workers and cached per skeleton, sex and clip name), deterministic.

## Stage log
