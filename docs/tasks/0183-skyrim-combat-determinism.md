# Task 0183: Skyrim combat determinism

Design doc section: decision 0018 (combat), tasks 0171 (review fixes, S5-3b) and 0179 (stage log: "Determinism of the perk scenarios").
Goal: with a fixed frame time and seed, the whole combat trace (the pack's `COMBAT` lines and the core's `NCOMBAT` lines, unsorted) is byte-identical from run to run, without bringing back the main-thread read of the combat records or a long stall at the first combat.
Inherited code touched: none (all OpenEden code).
Constraints: only timing and ordering change; no formula, no perk result. Records still read off the main thread. toddhow asked for the fix ("Yes, fix it", 2026-11-15).

## Causes (found by experiment)

Runs: `tools/neutral_session_check` `n5trade` and `n5bow` (settings in the work folder `I:\Projects\OpenEden-work\bow6\det\*.set`), `OPENEDEN_FIXED_FRAME_TIME=0.0166667`, `OPENEDEN_NEUTRAL_COMBAT_TRACE=1`; the trace is every `COMBAT` line of the log with its timestamp prefix cut (not sorted). Six to eight runs started AT THE SAME TIME (so the workers compete) were the way to make a worker's speed vary.

1. **Which frame combat starts on (the records worker).** `SkyrimCombat::combatUpdate` advanced its clock on the waiting updates too, so every test schedule (`mNextPlayerAuto` 1 s, `mNextSwing` 3 s, `mNextPlayerBow` 1 s), the regeneration "in combat" test and the linger times were counted from the session's first update, while the records worker (`SkyrimWorldView::combatReady`) finishes after a wall time that differs from run to run. Evidence: a pack test (`CombatDeterminism`) with the data not ready for 0, 3, 17, 45 and 400 updates: the player's first test swing came on a different ready update and the player's stamina after 60 ready updates was 13.96 against 13.81 (red: 2 of 3 tests failed on the old code). In the game the scenarios did not show it in 17 loaded runs of the old build (the swing at clock 1 s is always already past when the records are ready), so the one-frame stamina step of the stage log of 0179 (45.5 against 45.529) most probably came from cause 2, which moves the frame of a hit event. The clock is fixed all the same because the unit test shows it can move a schedule.
2. **Which frame an event is delivered on, and the order of events in a frame (the clip meta).** The meta (events and length) of a clip started by the pack is read on a worker after the request. `NeutralCombatBridge::update` counts the clip's time from the request but can deliver an event only once the meta is visible; an event the wait has passed is delivered in the frame the meta arrives, with its true clip time. So the `frame clock` of the `SoundPlay` at clip time 0 (and of every event the wait passed) differs from run to run, and when two clips' metas arrive in different frames the `NCOMBAT event` lines of the two clips swap places. Evidence: six parallel `n5bow` runs of the unchanged build gave 3 different traces (`det\b1..b6`: `SoundPlay ... frame clock 0.015625` against `0.0312501`, and `token 2 arrowRelease` and `SoundPlay` in the other order in `b4`); with only the clock fix of cause 1 eight parallel runs gave eight different traces (`det\c1..c8`, arrival 1 to 12 frames late). The same cause delays the player's first swing, whose timing comes from the sidecar through `PlayerClipMetaCache`.
   The order of the `NCOMBAT` lines in one frame is NOT an unordered container: the bridge walks `mPlaying` (a vector in request order) and the pack's lines come after it. It changes only because the events of a clip arrive in a different frame.

## Fix

1. Pack: `combatUpdate` does not advance `mClock` while the data is not ready; the waiting updates are still counted (`updatesWaitingForData`). Combat time starts on the update on which combat became ready.
2. Bridge: `NeutralCombatBridge::setMetaWaiter(fn)`. When set, the first poll of a clip whose meta is not there yet is answered by one waiting call (`neutralActorClipProgressWaiting`, then `SkyrimPose::Controller::progress(wait)`, up to 2 s), so the clip's events are delivered on the frames the clip clock gives. `PlayerClipMetaCache::begin(..., wait)` does the same for the player's swing timing (`Scene::neutralClipMeta`).
3. **The waiting is used only when `OPENEDEN_FIXED_FRAME_TIME` is set** (every repeatable test run sets it; read in `World::World` and `Scene::neutralClipMeta`). A game that is played never blocks on a clip; its events follow the meta's arrival as before (a few frames late on a cold read). Decision for toddhow: waiting always would cost a decode (about 5 to 50 ms) at every swing; a shared meta cache with a prefetch at the first ready update would make play deterministic after the first use of each clip, but needs the pose controller to take a meta from a cache (not done).

## Tests (written first, run red)

- `components-tests` `CombatDeterminism*` (3, new file `apps/components_tests/skyrimse/testskyrimsecombatdeterminism.cpp`): 2 of 3 failed against the old clock (the third pins that waiting updates are still counted).
- `openmw-tests` `CombatMetaWaiter*` (3, new file `apps/openmw_tests/mwphysics/testneutralcombatdeterminism.cpp`): against a stub `setMetaWaiter` that did nothing 2 of 3 failed (the third, "without a waiter the events still follow the arrival", pins the old behaviour).
- `openmw-tests` `PlayerClipMetaWait*` (2, appended to `testplayerclipmeta.cpp`; no existing test changed): 1 of 2 failed against a stub that ignored the wait (the other pins that a timed-out wait leaves the read running).

No existing test was edited or deleted; none pins the old timing.

## Evidence (build `I:\Projects\OpenEden-work\bow6\build`, traces in `bow6\det`)

Five runs each, started together, unsorted, md5 of the whole trace:

| scenario | lines | md5 | runs |
| --- | --- | --- | --- |
| `n5trade` (melee trade, ragdolls on) | 360 | `725d114808210a2c253f763d9e45d229` | 5 of 5 (and 14 more in earlier runs of this task) |
| `n5bow` (bow, player 300 units east) | 476 | `5e5df221166e5607add7f49442281dc3` | 5 of 5 (14 more earlier) |
| `n5trade` + S5-4 (`power every 2`, `bash every 3`, `test block Hulda`) | 423 | `7d7fd8fb698169c1d3a943796e49417d` | 5 of 5 |
| `n5trade` + perks (`Armsman00`, `FightingStance`, power every 2) | 376 | `b76f218852cf48e68a9a02d3538a9a29` | 5 of 5 |

Before: `n5bow` gave 3 different traces in 6 parallel runs (and 8 different in 8 with the clock fix alone). The trade trace is byte-identical to the trace of the build before this task (`u1` against `e1`), so the combat formulas and results are unchanged; the bow trace differs from the old one only in the `frame clock` of events the old wait delayed.

Morrowind unchanged: `tools/worldobjects_check` new against `perks\parent` (the S5-7 parent) 110 objects, OK; the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) SHA-256 `1c5d47ad0b5dc32d` for the parent and the new build. `openmw-tests` 2,679 (2,677 pass, 2 gated skips) and the Skyrim, Extension, Combat, Havok, Frozen and Neutral groups of `components-tests` 1,561 pass.

## Not run / limits

The Linux gate and GCC `-Werror` (hand-checked), clang-format 14, clang-tidy, the Lua tests, a fresh review, other scenarios than the four above (n5death, tradewall, the real-key bow), a slower machine than this one (the wait bound is 2 s; a clip that needs longer is delivered late as before). A records worker that FAILS still leaves combat waiting for ever (unchanged).
