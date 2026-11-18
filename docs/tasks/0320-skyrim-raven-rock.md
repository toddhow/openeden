# Task 0320: Skyrim Dragonborn, Raven Rock's arrival scene and Adril's greeting

Follow-up of task 0312 (the ship). Same hand wrote tests and code; tests first only where they could be (see section 4);
no fresh reviewer. Windows only. Branch `solstheim2-0320` from `5648e037bc`, local commits, not pushed. Behind the existing
`[extension.skyrimse] dragonborn`; no new setting.

## 1. What runs (the game's own data and Papyrus)

After the ship (task 0312) lands the player at Raven Rock's landing:

1. The box trigger `DLC2RRESKickoffTriggerRef` (0x025823, placed at (28994, 33469, 401), a box of 293 x 259 x 227 turned
   1.0854 rad) fires `DLC2RRESKickoffScript.OnTriggerEnter`, which sets `DLC2RREstablishingScene`, `DLC2RRArrivalScene`
   (stage 10: `Fragment_0` starts the scene `DLC2RRArrivalGoScene`, 0x02ADDB), `DLC2RR01` (10 then 20) and `DLC2RR03`.
2. The scene runs its 15 phases with the game's own conditions: phase 0 waits for `GjalundShouldTalk` (set by the ride
   script after its 7 s wait), phase 2 for `RidingTheBoat` to be false (`FinishBoatRide`), phase 4 gives Adril Arano the
   package `DLC2RRASAdrilForcegreetPlayer` (a ForceGreet whose topic is the subtype HELO).
3. Adril force-greets the player: "I don't recognize you, so I'll assume this is your first visit to Raven Rock,
   outlander. State your intentions." with the real choices; a line's fragment (`DLC2_TIF__02032147`) runs.

## 2. Faults found by running it, and fixes

| # | Fault | Cause | Fix |
| --- | --- | --- | --- |
| 1 | The kickoff trigger never fired (0312's "arrival scene not started") | `TriggerVolume::mAcceptBox` was `main quest start`, `college` or `daedric quests` only; the kickoff is a box | `acceptsBoxTriggers(..., dragonborn)` (`triggers.cpp`), used by the pack's trigger registration |
| 2 | The scene stuck in phase 5 | Adril's package is a ForceGreet by subtype HELO; the pack accepts that only with task 0308's `main quest whiterun` (`setHelloGreetings`, `enableHelloGreetings`) | `dragonborn` now switches the same two on |
| 3 | The player stood on the sea floor at the dock, no deck | `FirstTimeTravel` disables `ShipNorthwindREF002` (the ship moored at the dock, 0x016FE2) for the boat animation; there is none here, and the player set down on `BoatRideTarget` (z 312, on that deck) fell to z 127 | `PapyrusHost::setKeepEnabled`; with `dragonborn` the pack keeps that reference enabled (HAND-WRITTEN: every other reference in the script's DisableList is disabled as in the game). The Lua arrival of a ship travel (`travel_global.lua`) no longer uses `onGround` for a landing in another world (a height probe reached the seabed) |

In game after the fixes the player stands at z 308 on the deck; frames show the docks and Adril's box.

## 3. Checks

`tools/skysolstheim_check` (default plan extended in the run, `raven_input.txt` is the new input script: Gjalund's lines,
then Adril's box). Run command (as in `check.sh`): `SETTINGS_EXTRA='[extension.skyrimse]\ndragonborn test stage = 10,230'`
(the aid starts DLC2MQ01 late, after Adril's greeting: with it earlier Adril's greeting is another line).
Frames viewed (in `I:\Projects\OpenEden-work\combat3\keep320`): the dock with Adril's first greeting.

## 4. Tests

`apps/components_tests/skyrimse/testskyrimseravenrock.cpp` (4 tests): `acceptsBoxTriggers` was written first with the old
rule as a stub (1 of 3 failed red: `dragonborn` alone). The ship-keep helper `keepsEnabledDuringTheRide` is header only and
was written with its test (not run red; the in-game fall to z 127 is the red evidence). The host veto, the Hello switch and
the Lua change have no unit test (the Papyrus host rig of the older tests is file-local).

## 5. Not built or not reached (ranked)

1. Adril's conversation does not end by a clicked Goodbye in the runs (the input script loops through his menu), so scene
   phases 6 to 14 (the rest of his talk with the guard) were not seen; DLC2RRArrivalGoScene's end (quest stage 200,
   `DLC2Init` 100) not reached.
2. DLC2MQ01 stages 25 and 30, the Earth Stone, Frea and Skaal village, DLC2MQ02: not reached (the walk to Skaal village
   and its triggers is far; MQ01 stage 10 and 20 still come from the test aid and 0312's log-entry rule).
3. The boat animation, fades, `WaitForAnimationEvent` stay tolerant no-ops; the ship stays enabled all the ride.
4. The player's journal frame for DLC2MQ01 was not captured.
5. Scene timing follows script time, which follows a real-time worker: the greeting opens 24 to 60 s after landing, so
   input scripts keyed by frame are loose (clicks may land before or after the box).

## 6. Decisions for toddhow

- Keeping the moored ship enabled during the ride (hand-written, section 2 fault 3).
- `dragonborn` turns on the HELO greeting rule and `skipFailedPhases` of 0308's mechanism (one mechanism, two settings).
- No existing test edited.
