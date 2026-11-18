# Task 0325: Skyrim Dark Brotherhood, the sleep abduction (DB02 "With Friends Like These...")

Status: DONE FOR THE ABDUCTION AND THE FIRST CONVERSATION, 2026-11-18 (section 4). Follow-up of task 0294 (DB01 runs end to end).
Same hand wrote the tests and the code (the tests mostly AFTER the code: the new natives and events did not exist; see section 5 for
the mutation checks that stand in for red runs). No fresh reviewer. Windows only. Local commit on `darkbrotherhood2-0325` from
`9852336cc7` (local `main`), not pushed. Behind the existing pack settings; NO new setting (the aids and the harness turn on `rest`,
`fast travel` with `map`, `actor cell moves`, `companions`, `quest alias packages`, `papyrus`).

## 1. What the game does (decompiled `.pex` of `Skyrim - Misc.bsa`, records of `Skyrim.esm`)

* `DarkBrotherhood` (QUST 0x1EA5C, start-game-enabled) runs `QF_DarkBrotherhood.Fragment_0` -> `RegisterForUpdate(5)`; its script's
  `OnUpdate` starts `DBEntranceQuest` (0x50F2E) at stage 20 once DB01 is at 200 or more, DB02 is below 10, `pDBEntranceQuest.pSleepyTime`
  is 0 and the player is not in the Windhelm location.
* `DBEntranceQuest` stage 20 (`QF_DBEntranceQuest_00050F2E`): `Alias_AstridAlias.GetReference().MoveTo(pShackMarkerRef)` (the abandoned
  shack, interior cell `AbandonedShackInterior` 0x50F1E), `pSleepyTime = 1`, `RegisterForSleep()`, the courier letter; stage 20's second
  fragment restrains the three captives.
* `pDBEntranceQuestScript.OnSleepStart` (pSleepyTime 1): `DisablePlayerControls`, `ForceFirstPerson`, `Game.GetPlayer().MoveTo(
  pPlayerShackMarker)` (0x50F2A in the shack), `Woozy.Apply`, `PlayIdle(WakeUp)`, `Utility.Wait(3)`, `pSleepyTime = 3`.
* Astrid's alias packages (`DBAstridSleepGreet`: GetInSameCell(player), DB02 stage below 10, `pSleepyTime == 3`) are a ForceGreet of
  the topic `DBAstridSleepGreetBranch` ("Sleep well?"). Every response of that branch links to the topic `DB02AstridWalkAwayTopic` and
  names it as its walk-away topic (TWAT). The last response ("Pick your guest, and send the poor fool to the Void") sets
  **DB02 stage 10** (`TIF__00020BE0`: `pDB02.SetStage(10)`, controls back, `pSleepyTime = 4`).
* DB02 stage 10 (`QF_DB02_0001EA51.Fragment_3`): objectives "Kill one of the captives" and "Speak with Astrid" are shown.

## 2. What was missing, found by running it (ranked in the order the run reached them)

1. **`Form.RegisterForSleep` and the sleep events** did not exist (a tolerant zero: `form.registerforsleep=2` in every summary) and
   nothing told the scripts the player slept. New: `Form` and `Alias` `RegisterForSleep` / `UnregisterForSleep`
   (`PapyrusHost::mSleepers`, at most 256, not saved), `PapyrusHost::onSleepStart(startDays, desiredEndDays)` and `onSleepStop`, raised by
   the rest service's apply hook when a SLEEP is accepted (`startRest`: the clock in game days, the desired end `hours / 24` later; the
   rest is instant here, so the stop follows at once). The registrations of `PlayerSleepQuest` and `DLC1VampireSleep` now hear it too:
   their handlers ran with no error (the "You awaken feeling Rested" message of the sign/ability script appears).
2. **`Form.RegisterForUpdate` reached only the calling script.** `QF_DarkBrotherhood` registers the update and `DarkBrotherhood` (the
   quest's OTHER script) handles `OnUpdate`, so the quest never started `DBEntranceQuest`. In the engine an update is registered for
   the FORM: now `RegisterForUpdate`, `RegisterForSingleUpdate` and `UnregisterForUpdate` of a `Form` act on every non-alias script of the
   calling script's form (`formUpdateTargets`). **This changes which scripts get `OnUpdate` for every quest that has more than one script
   with an update registration** (the aliases keep their own registrations). The wider Skyrim/Papyrus filter of `components-tests` still
   passes (3,600).
3. **The player's `MoveTo` into another cell** was refused (a player cannot be moved by the world's object moves). New:
   `PapyrusHost::setPlayerMoveSink` (cell of the target reference, position with the offsets, heading), installed by the pack when the
   travel service runs; `TravelService::queueMove` keeps one move ("cell;x;y;z;yaw", an interior cell by editor id, found by the new
   `SkyrimWorldView::cellEditorIdOf`) and `skyrimse.travel` 1.3 (additive: `move_seq`, `move_place`) hands it to `travel_global.lua`,
   which teleports the player (one global script owns every player teleport: carriages, the ship, this). The target's heading is read
   from the data (`referenceRotation`) when no script turned it. **Needs `[extension.skyrimse] fast travel` (and so `map`).**
4. **`GetInSameCell` was never answered** (the virtual returned false): Astrid's package would never pass. `SkyrimConditionWorld::
   inSameCell` compares the subject's cell (speaker, placed actor, or the player's interior cell) with the reference's (the player or a
   placed reference). Exteriors have no cell id there (false).
5. **A walk-away topic was a menu button.** `DB02AstridWalkAwayTopic` has the editor id as its name and every response of the branch
   links to it, so the box showed "DB02AstridWalkAwayTopic" beside the real choices. `DialogueRuntime::hidesWalkAway`: with walk-away
   topics on (`companions`, task 0315) the walk-away topic the shown response names is not offered as a choice, in `offerTopics` (links)
   and in `offerLinks` (the forced greeting's path). A topic that is not the response's own TWAT is offered as before. (The game's rule
   is not in the data I could read: the evidence is that Astrid's INFOs carry both the link and the TWAT and the game shows no such
   button; RECALLED-VERIFY.)
6. Test aid: `summons test kill` takes a comma separated list (`Grelod,DB02Captive1@175`), each entry killed once (it killed one actor
   and cleared itself before).

## 3. Harness

`tools/skydb_check/db02.sh <name> [frames]` (DB01 to the reward, then the sleep; env `KILL`, `PLAN2`, `INPUT`, `PKTRACE`) over
`run.sh`; `luatest/scripts/hse/global.lua` has the new plan step `beds` (every furniture of the cell the pack's rest rule calls a bed,
with the placement id and position). The input script clicks the real conversation box and the real sleep window; positions of the
buttons were read from frames (the box is centred, so a button moves when the text above it grows). `OPENEDEN_PACKAGE_TRACE=114100`
(Astrid's base) and `conditions debug quest = 00050F2E` were the aids that found items 4 and 5.

## 4. Results (Windows, five Skyrim masters, `I:\Projects\OpenEden-work\combat5\build`)

Harness `tools/skydb_check/db02.sh` (real mouse clicks on the sleep window and on every button of the conversation box, quiet window,
volumes 0, fixed frame time, 15,600 frames, about 13 minutes). The chain, all through the game's own quests, scenes, dialogue and
Papyrus (trace lines of one run, timestamps cut):

1. DB01 as in task 0294 (stage 200 by the reward line). Right after it `DBEntranceQuest` is set to **stage 20** by `DarkBrotherhood.
   OnUpdate` (before the fix of item 2 it never was) and Astrid is moved by the world into the shack (`actor cell moves`).
2. The harness teleports to the Bannered Mare, activates the free bed (`NINV bench ... 'Bed'`), the real click on "Sleep, 8 hours":
   `REST slept 8 hours ... Rested`, then `PAPYRUS MoveTo of the player to 00050F2A`, `[skyrimse travel] script move to
   abandonedshackinterior at (1764.380, 1049.330, 7519.382)`, `Loading cell AbandonedShackInterior` (frame: the shack).
3. `FORCEGREET the actor placed as 114152 (base 114100) wants to say topic 337224`, the conversation opens with Astrid's voiced
   "Sleep well?" (frame `astrid_greeting_sleep_well.png`). Six turns by real clicks: "What? Where am I? Who are you?", ... "You want
   me to murder someone else? Who?", Astrid's "If you turn around, you'll notice my guests ... Make your choice. Make your kill"
   (`astrid_guests.png`) with the choices "I'll have no part of this insanity." / "All right, I'll do it. I'll kill one of them." /
   "(Remain silent)", and "Pick your guest, and send the poor fool to the Void" (`astrid_pick_your_guest.png`). `DB02AstridWalkAwayTopic`
   is no longer a button.
4. Closing the box: **DB02 stage 10** (`QF_DB02.Fragment_3` and `Fragment_22`); the journal lists "With Friends Like These... [ ] Kill
   one of the captives".
5. The test aid kills `DB02Captive1` (`COMBAT test kill actor 189425`): the game's own death handling sets **DB02 stage 30**
   (`Fragment_7`), the journal shows "[x] Kill one of the captives [ ] Speak with Astrid" (`journal_db02.png`), and Astrid's talk gives
   "Brother." (`astrid_brother.png`).

Three runs started at once (the machine's fixed frame time makes them comparable): the 63 lines of the filter `NTALK|FORCEGREET|PAPYRUS
quest 50F2E/1EA51/1EA50|COMBAT test kill|COMBAT death|REST|script move|MoveTo of the player|JOURNAL|MRG` are byte-identical (md5
`08d2fb189d9b55d7c719569dec4baef7`); each run: 0 errors, 0 crash dumps, 19 screenshots. The repository's own `db02.sh` (a run apart,
after the last build) is compared in section 4.1. Calibration of the kill time: the aid's clock is the combat clock (it stops while a
box is open); `@95` killed the captive BEFORE stage 10, `@120` after it (13 s later), `@145` not within 16,000 frames.
Frames: `I:\Projects\OpenEden-work\combat5\db02\keep`. Morrowind unchanged against `integ2`'s build: `tools/worldobjects_check` 110
objects OK, the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) `1c5d47ad0b5dc32d`.

Unit tests: `components-tests` filter `*Skyrim*:*Papyrus*:*Dialogue*:*Travel*:*Carriage*:*WalkAway*:*Summons*` 3,661 pass (data-gated
tests skip), no failure; the 21 new tests pass. The comma list of the test kill has no test of its own.

### 4.1 The committed script
One run of the repository's `tools/skydb_check/db02.sh` (the defaults of its plan, kill list and input file, a run alone) after the final build:
0 errors, 0 dumps, 19 screenshots, and the same 63-line trace as the three runs above but for ONE number: the journal's revision counter
at its first rebuild reads 970 instead of 969 (a counter, not a state: the journal has 1 running quest in both). Not investigated
(a run alone against three at once; probably one more quest start at the first frames). The same single-run result after the rebase
onto `872713d15a`: identical to this one.


## 5. Tests

`apps/components_tests/skyrimse/testskyrimsedb02.cpp` (11: sleep 4, update 2, player move 1, travel 2, same cell 2) and
`testskyrimsewalkaway3.cpp` (5; named walkaway2 on its branch, renamed at the merge with task 0326). Evidence that they test something (the tests were written after the code, against a build, so there is no
red run against stubs): six one-line mutations (the sleep event off, the form-wide update reduced to the calling script, the player's MoveTo branch off, `queueMove` refusing, `inSameCell` false, `hidesWalkAway` false) made 9 of the 21 fail by name (`SkyrimDb02Sleep` x3, `SkyrimDb02Update` x1, `SkyrimDb02PlayerMove`, `SkyrimDb02Travel` x1, `SkyrimDb02SameCell` x1, `SkyrimWalkAwayMenu` x2); the other 12 pin absence, refusals and the old behaviour; the mutations were reverted. No existing test edited. Not covered by a unit test: the pack glue (the rest hook, the move
sink installation, `cellEditorIdOf`), `travel_global.lua`, the comma list of the test kill (the aid only).

## 6. Not built or not shown (ranked)

* The captive kill by the player, the captives' own dialogue ("Who are you?", pay to kill: `DB02Captive*` topics), `DB02Script`'s
  `OnDeath` chain to stage 30 and Astrid's talk after it: the captive dies by the test aid (`summons test kill`, the pack's death report: the same path a real kill takes), not by the player; stage 30 follows from the game's own scripts; "Brother." is the post-kill line, after which Goodbye did NOT set stage 40 in the run (not investigated).
* Stage 40 (the door key, `DBAstridShackLeaveTriggerScript` moving Astrid), the Sanctuary door and DB02a/DB03: not reached.
* The wake-up idle (`Idle_1stPersonWoozyGetUpFromBed`), the Woozy image space (`ImageSpaceModifier` values come out as plain `Form`: "class
  'Form' has no function 'Apply'" is logged and harmless), the force first person: no-ops or tolerant.
* A save between DBEntrance stage 20 and the sleep loses the `RegisterForSleep` registration (not in the host state).
* The courier letter (`WICourierScript.AddRefToContainer`) is a script call that ran; nothing was checked in the world.
* Not run: the Linux gate, clang-format 14, clang-tidy, the Lua integration tests, `openmw-tests`, a fresh review.

## 7. Decisions and edits for toddhow

* Update registrations are per FORM now (item 2): correct as far as the engine goes, but it touches every quest with two scripts.
* The walk-away topic is hidden from the menu only with `companions` on (the existing switch of task 0315's walk-away topics).
* `fast travel` is needed for the player's MoveTo (the travel service owns player teleports).
* `skyrimse.travel` is 1.3 (additive). No existing test edited, none pins 1.2.
* The sleep: OnSleepStop follows OnSleepStart at once (instant rest); `interrupted` is always false.
