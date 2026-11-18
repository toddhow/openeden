# Task 0287: Thieves Guild, third slice (follow-up of 0271 and 0275)

Status: partly done, 2026-11-17, Windows only. Branch `worktree-agent-abf3c6eaef2728dc0`, local commits, not pushed.

## 1. Why Brynjolf's greeting after the plant never came (cause found)

Brynjolf's packages after the plant: `TG00BrynjolfForceGreetPlayerAfter` is only the FIRST in his list when TG00SP has
no package in front of it. Reading the game's scripts (decompiled `.psc`) and the traces (`OPENEDEN_PACKAGE_TRACE`,
`OPENEDEN_PAPYRUS_NATIVE_TRACE`) gave this chain:

1. TG00 stage 10 starts TG00SP ("spectator quest"). Its Brynjolf alias package `TG00SPBrynjolfCauseDistractionStatic`
   (no conditions) goes in front of his own list as long as TG00SP runs.
2. TG00SP leaves only at its stage 200, set by the END scene's fragment (`SF_TG00DistractionSceneEnd`, scene
   0x20E13), or by TG00 stage 200.
3. Stage 20 of TG00SP (set by TG00 stage 30, the plant) runs `Fragment_15`: it stops the MAIN scene (0x20DF4) or the
   EXTEND scene and starts the END scene.
4. The harness planted within 2 s of stage 10, before the setup scene (0x20E0A) had ended, so the main scene never
   played, `Fragment_15` found nothing playing and no end scene started (what the real game does in that order is not
   known). With the plant after the setup scene (about 25 s), the main scene plays, stage 20 stops it and the end scene
   starts.
5. **The end scene's phase 2 has a line by Brand-Shei (0x19DDC), who lives in the bunkhouse and is not loaded.** The
   scene runner waited 120 s (`mParkSeconds`) for him, holding Brynjolf in his scene package and TG00SP at stage 20.
   Only then did TG00SP reach 200 and the greeting follow.

## 2. Fix

* New pack setting `[extension.skyrimse] scene park seconds` (float, default 0 = the runner's own 120 s; declared in
  `extension.json`): how long a scene line waits for an unloaded speaker. `SceneRunner::setParkSeconds` /
  `clampParkSeconds` (0.5 to 600 s, NaN gives 120). The scenario uses 5.
* Cherry-picked `b470a1b2a0` (the coordinator's fix of `forcegreet_global.lua`: `WIDE_EVERY` and `ticks`, lost in a
  merge) before the game runs.

Tests (`testskyrimsescenes.cpp`, 3 new `SceneRunnerPark*`): written first against a stub header (`clampParkSeconds`
returns its input, `setParkSeconds` does nothing): **2 of 3 failed red** (the third pins the default 120); all pass
with the code. The scene filter of `components-tests` has no failure. No existing test edited.

## 3. In game (`tools/skythieves_check`, `thieves3/env_d.sh`, real controller input)

With the plant 1,500 updates after stage 10 and `scene park seconds = 5`: setup scene ends, stage 13 starts the main
scene (its line 4 waits for Brand-Shei), the plant sets TG00 stages 20 and 30 and TG00SP 20, the main scene is stopped,
the end scene begins and ends 5 s after its parked line, TG00SP reaches 200, **Brynjolf's `ForceGreetPlayerAfter`
greeting begins by itself** (`FORCEGREET conversation began`, voiced "tg00_tg00brynjolfoutropreb"), three real turns
later **TG00 stage 200 and TG01 stage 10** (nine aliases filled, `QF_TG01 Fragment_2` ran); a further talk offers
"Can't we talk here?" and "How do I find the Ratway?" (frame `d2/user/screenshots/screenshot008.png` viewed).

## 4. Not done, ranked

1. TG01 debt collection (Keerava, Bersi, Haelga, Drifa): not reached in game. It needs the player to walk through the
   Ratway door to the Ragged Flagon, which means door travel into `RiftenRatway01/02` and `RiftenRaggedFlagon`
   (door destinations exist, the route was not run).
2. The harness plants within 2 s by itself when the plant is early (the quest then waits on the 120 s park unless the
   setting is used); what the real game does for an early plant is unknown.
3. Brand-Shei is never loaded (he sleeps in the bunkhouse); the arrest scene (`TG00SPArrestScene`, quest TG00SPArrest)
   has the same wait and is not exercised.
4. Package order across quests is by quest id here, not by quest priority: not confirmed against the game.

## 5. Decisions for toddhow

* The setting's default is 0 (unchanged behaviour); I recommend 5 whenever `quest alias packages` is on.
* Not run: the Linux gate, clang-format 14, clang-tidy, Lua tests, a fresh review, `worldobjects_check`.
