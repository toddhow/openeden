# Task 0333: Skyrim side quest "Blood on the Ice" (MS11, the Windhelm murders), first slice

Follow-up of task 0269 (which marked MS11 done by a test aid for Hjerim's sale). Same hand wrote tests and code, tests first
and run red against stubs, no fresh reviewer. Windows only. Branch `bloodice-0333` from `3fcb5a7fa0`, local commits, not
pushed. Behind the new pack setting `[extension.skyrimse] blood on the ice` (off; declared in `extension.json`; needs
`papyrus`, `actor packages`, `quest alias packages`, `actor cell moves`).

## 1. The survey (houseCARL records, decompiled `.pex` of `Skyrim - Misc.bsa`)

- `MS11` "Blood on the Ice" (QUST 0x1F7A3, side quest, RunOnce, priority 60): 21 stages (0, 1, 10 to 250), 15 objectives, 65
  aliases (witnesses Helgird 0x14124, Calixto 0x1414A, Silda 0x14121; Jorleif the authority 0x14135; Viola, Tova, Ulfric,
  Wuunferth, the victims; 36 investigation props: chest, mead, pots, papers, amulet, wardrobe; the crime-scene
  investigator is a find-matching guard of the Windhelm location with a guard voice type). Scripts `ms11questscript`
  (conditional properties, `Setup`, `MoveBody`, `SetupStakeout`, ...), `QF_MS11_0001F7A3` (stage fragments), `ms11cluescript`
  (the amulet: `OnContainerChanged` to the player sets stage 70), trigger scripts of the investigation spots
  (`MS11InvestigateTriggerScript`: a message and `TryToClear`).
- **The start is not in MS11.** The quest has no start-up stage and no event. The story manager's CLOC node
  `MS11WindhelmEntranceCounting` (0xBAC26: NewLocation Windhelm, OldLocation not Windhelm and with no keyword 0x130DC)
  starts the quest `MS11Kicker` (0xBAC24, no flags) on every entry. Its fragment: `EnterWindhelmCount += 1`; at 4 or more and
  `GameHour >= 19 || < 7` it calls `MS11.Start()`; if stage 100 is done and the follow-up day has come it sets stage 105; then
  `Stop()`. (`MS11KickoffActivator` is a second start by activating an object; not used.)
- **The opening scene** `MS11OpeningCrimeScene` (SCEN 0x206AE, begin on quest start) has one phase and **no actions**; its
  phase fragment (`Fragment_5`) kills and disables the first victim, enables the victim's body and the blood trail, and
  `MoveTo`s Witness1 to 3 and the investigator to the crime scene's markers. The witnesses live in three interiors (Hall of the
  Dead, Calixto's House of Curiosities, Candlehearth Hall); the guard is a Sons patrol guard of the neighbouring cell.
- Stage 0's fragment (`Fragment_58`): `Setup` (the Investigator perk, Hjerim's door locked at level 100), stops the
  pre-quest and sets stage 1.

## 2. Faults found by running it, and fixes

| # | Fault | Cause | Fix |
| --- | --- | --- | --- |
| 1 | The kicker ran at every entry and never started MS11, at night too | The global `GameHour` (GLOB 0x38) is the record's value (12) for every Papyrus script; the pack never fed it (`SetValueInt`, `GetValueInt` and the counting were fine: `SetValueInt` is a script wrapper over the native `SetValue`) | `SkyrimCellSource::lastClock()` keeps the core's `ActorClock` of the last update; the pack's phase callback feeds `GameHour` from it **before** the Papyrus frame (`feedGameHour`, `bloodice::gameHourValue`: the hour, or nothing for an invalid clock or an hour outside [0, 24)); only with the setting (a live GameHour changes every script that reads it) |
| 2 | The opening scene never began: "not started: none of its actors is loaded" | `SceneRunner::startImpl` refuses a scene with nobody loaded; this scene has no action and only runs a fragment, and its actors stand in unloaded houses | `SceneRunner::Options::mActionlessStartsUnloaded` (setter `setActionlessStartsUnloaded`): a scene with **no actions** begins without a loaded actor; a scene with an action still needs one |

## 3. What runs now

New game at Windhelm's docks, `tools/skybloodice_check/check.sh` (see its header), game hour set to 22 by the plan (the game
also starts at hour 0, night), `blood on the ice` and `quest alias packages` on. Entering Windhelm (the story manager's CLOC
from a place that is not Windhelm: Tamriel grid (32, 8), (33, 8) and (34, 8) are the Windhelm location, (32, 9) and (31, 8)
are not) six times starts the kicker each time; at the fourth, MS11 starts (`quest 0001F7A3 started with 18 aliases filled`),
stage 0 runs `Fragment_58`, **the opening scene begins and ends**, `Fragment_5` calls Kill, Disable, Enable and four `MoveTo`s
(the world logs `A game pack moved the placement ... into Wilderness (32, 9)` for Helgird, Calixto, Silda and the guard: this is
the log line for the moves in Tamriel grid terms; **the crime scene is in WindhelmWorld (WRLD 0x1691D, Lua `skyrimse:w92445`),
a different worldspace from Tamriel, so a player standing in Tamriel sees no actor row and 450 units of height difference; the
actors are real placements in WindhelmWorld. There is no move fault: the harness's `world` step goes there**), and
MS11 is at stage 1. Three runs (`r333/f1..f3`) give the identical 35-line story trace (`r333/trace333.sh`, md5
`42a8a5219abd`: kicker starts, quest start, scene begin and end, the fragment's moves, the world's four moves, the GameHour
lines).

## 4. Stand-ins and aids (hand-written, named)

- **The four entries into Windhelm are Lua teleports** (`go` steps between grid (32, 9) and (32, 8), then to the crime scene);
  the story manager's own CLOC event and the kicker's own counting do the rest. The game's real way is to walk or ride in
  four times at night; the pack's player-place event follows the player's grid square either way.
- The harness's `hour` step sets the engine's `gamehour` global (night), the pack's feed then gives it to Papyrus.
- `tools/skybloodice_check/luatest/scripts/cmp/global.lua` has `go x y 'land'` (the terrain height plus 120), `hour`, `near`
  and a neighbour-cell search for `talk`.
- No test aid was added or used for MS11 itself (`story test quest = 01F7A3` was used once to survey the start and is not part
  of the check).

## 5. Not reached (ranked), what blocks it

1. **The witnesses, the guard and the first victim's body do not appear at the crime scene.** The world logs the moves, but in
   the frames at the marker (and a Lua listing of the neighbour cells) no actor stands there, and the actor system shows no row
   for them (`NACTHOOK` lists only animals). Cell (32, 9) holds 2 objects after the moves. A move of a placement from an unloaded
   interior (the world builds it for the move) into an exterior cell while the player's grid was changing lands in a store whose
   objects are not inserted into the scene afterwards. Cause not found (hypotheses: the destination store was not the active
   one at that moment, or the actor was moved into the store but never inserted because the scene had not yet loaded the grid).
   **Blocks everything below.** (A second oddity: the marker's z, about -12,590, lies 450 units above the heightmap at that
   place; whether the crime scene stands on a city platform that is not in the exterior cell's objects was not checked.)
2. The guard's own talk: the investigator's alias carries the ForceGreet package `MS11GuardHangAroundCrimeSceneFG` (0x21675)
   and the first conversation sets stage 10; not run (needs the guard to exist in the brain).
3. The investigation clues (the amulet's `OnContainerChanged` is a stage 70 trigger and the other props are triggers of
   `MS11InvestigateTriggerScript`), Wuunferth's arrest scene, Calixto, the stakeout and the second murder: not built; no stage
   after 1 was set.
4. The journal: no frame (no stage with a log text was reached; the objective of stage 1 is not shown in a frame).
5. Windhelm's location: grid squares (32, 9), (33, 9) and (34, 9), where the city centre marker stands, have no location in the
   pack's answer (only y = 8 squares do); the data was not checked against the Creation Kit; it matters for `GetInCurrentLoc`
   conditions of later packages.

## 6. Tests

`apps/components_tests/skyrimse/testskyrimsebloodice.cpp` (5 tests), written first and run red against stubs: **3 of 5 red**
(the option on an actionless scene, the begin-on-quest-start case, the hour; the other two pin that an action-bearing scene
still needs a loaded actor and the constants). The glue (the clock cache in the cell source, `feedGameHour`, the driver's
option) has no unit test of its own; the game run is its evidence. No existing test edited. `components-tests` filter
`*Skyrim*:*Scene*:*Papyrus*:*Dragonborn*:*Quest*`: 4,005 pass.

## 7. Decisions for toddhow

- A live `GameHour` is only on with the setting (it changes every script that reads it, e.g. the radiant quests' hour tests);
  giving it to all scripts is a one-line change in the pack's phase callback and would be more faithful.
- A scene with no actions begins with nobody loaded, only with the setting; the engine's own rule for such scenes is
  RECALLED-VERIFY.
- Not run: the Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, `openmw-tests`, a fresh review.


## 7. Follow-up: the root cause of the missing actors, and the conversation chain (same task)

- **Cause:** worldspace, not move machinery. Helgird, Calixto, Silda and the guard (base 1035914) stand at
  (132744, 37560) in WindhelmWorld. The harness gained `world <id> x y z|'land'` (a cross-world teleport) and
  `talk <base> [x y]` (activates the nearest actor of that base, with a reference point).
- **Reached through the game's own data (real clicks, voiced):** the guard's forced greeting ("Hold it there! Keep your
  distance." then "Another girl killed. This is Susanna, from Candlehearth Hall..."), the three witness topics (their TIFs
  set `QuestionedWitness1..3`), then **MS11 stage 20** (`QF_MS11_0001F7A3.Fragment_2`), the journal listing the objectives
  (frame viewed), and the guard's next line ("Just like always -- nobody saw anything useful. The bastard's escaped again.",
  choice "There might be more to this, if you'll let me help.").
- **Stand-in:** MS11 stage 10 (which the witness topics need) is set by the aid `story test quest = 01F7A3:10@40`; no script
  of the data was found that sets it (cause in the data not established).
- **Not reached:** stage 30 and later (the guard's "help" choice was shown, not shown to set a stage), the clues, the amulet
  (stage 70), Wuunferth's arrest. Tamriel grid (32, 9) and (33, 9) have no location (a data gap of the CLOC mapping, not
  changed).
- `OPENEDEN_PAPYRUS_NATIVE_TRACE=<number>` now raises the trace's 100,000 call cap.
