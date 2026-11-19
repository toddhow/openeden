# Task 0349: Skyrim main quest "A Blade in the Dark" (MQ106, Delphine), first slice

Branch `blade-0349` from `69f549168d` (task 0343), local commits, not pushed. Windows only. Behind a NEW pack setting `[extension.skyrimse] blade in the dark`
(off by default, declared in `extension.json`, line in `tools/testprofile/README.md`; it needs `papyrus`, `quest alias packages`, `actor packages`, `[Game] neutral combat`).
Same hand wrote the tests and the code: the new tests were written first in NEW files and run red against stubs (section 5); no fresh reviewer.

## 1. Survey (data, read with decompiled `.pex` and housecarl)

- MQ106 (QUST 0x32926, RunOnce, priority 80, 34 stages, 21 aliases). MQ105's Ustengrav fragment sets **MQ106 stage 5** (`Fragment_4`: random dragons off, the dragon map shown).
  The player's rented room is in `RiverwoodSleepingGiantInn`; the box `MQ106InnRoomTriggerScript` (REFR 0x3FB03, `fireTriggerEvent`) sets **stage 20** when the player is
  inside (stage 10 enables it); stage 20 starts `MQ106IntroScene` (11 phases); its first phases make Delphine force-greet (`MQ106DelphineIntroTopic`), her chained INFO
  0xC65A2 (flag Goodbye) hands over the horn and sets **stage 30** (`TIF__000C65A2`); the scene goes on ("We need to talk", "Follow me", Delphine walks to the secret door), phase 6
  waits for `GetOpenState(RiverwoodInnDelphineRoomDoor) == 3` AND `bAllTargetsInTrigger` of `MQ106DelphineBedroomTrigger` (0xB3A3F, `defaultOnEnter`, targets Delphine and the
  player), phase 7 ("Now we can talk") sets **stage 32** (`Fragment_65`: secret door unlocked and opened). Her exclusive conversation (topics `MQ106DelphineIntroB*`, `...Exclusive*`)
  sets **stage 35** by `TIF__0003290B`; its end topics lead to stages 45 to 50 ("Let's go kill a dragon"): **stage 50** (`Fragment_13`) starts `MQ106LeaveRiverwoodScene`, enables the dragon
  (`MQ106DragonREF`, base Sahloknir) and Alduin (`MQ106Alduin`), `DragonMound.isDragonHome = 1`.
  `MQ106PlayerScript` (player alias) turns location changes into **stage 58** (left Riverwood), **65** (Kynesgrove; `Fragment_15`: `SetStage(60)`, `KynesgroveScene.Start`) and **75** (the
  burial mound location; `Fragment_42`: `AlduinScene.Start`, weather). `AlduinScene` sets stages 90, 100 and 110; stage 100 calls `FXDragonMoundScript.StartResurrection`
  (`Activate(self)`; its `waiting` state is entered only by the mound's `OnCellLoad`, then a chain of `Utility.Wait` calls ends in `myQuest.SetStage(doneStage)`). `MQ106DragonScript` (alias)
  sets 110 (combat) and **150 on the dragon's death**, 155 when the player is near the body, `MQ106Script.OnUpdate` 160; stage 200 ends the quest and starts MQ201.
- Real positions used: Kynesgrove marker (135130, 9904), the mound and Sahloknir (146239, 12759, -7153), Delphine's room (-797, -700), the secret-door side (-729, 790). The data's actor
  ids are base NPC_ 0x13478 (Delphine, placement 0x13485) and 0x13479 (Orgnar, placement 0x13486).

## 2. Faults found by running it, and what was done

| # | Fault (cause measured with `OPENEDEN_PACKAGE_TRACE`, `NACT*` traces, `OPENEDEN_SCENE_WAIT`, `conditions debug quest`, `OPENEDEN_PAPYRUS_NATIVE_TRACE`) | Fix |
| --- | --- | --- |
| 1 | Delphine's alias package `MQ106DelphineToRoom` (and ToGearUp, ToGearUp2, ToRiverwoodEx, ToOrgnar) is a Travel to places behind LOCKED doors: the navigator answers `no path`, so Delphine never reached the box trigger and the quest stood still | `PackageBrain::setSnapPackages` (a named set of package ids whose failed walk places the actor at the target), a **HAND-WRITTEN stand-in**; the ids are named in the pack glue |
| 2 | A scene's Travel (`MQ106DelphineToSecretDoor`) with no path ended nowhere | `enableSceneWalks()` (task 0277's mechanism) is switched on by the new setting |
| 3 | `GetOpenState` (function 157) was not a condition function: every list containing it failed closed (MQ106IntroScene phase 6) | function 157 answered (`ConditionWorld::openState`, `SkyrimConditionWorld::setOpenStateLookup` from the Papyrus host's open states, closed = 3 when no script touched the reference); the scene world and the conversation world both get it |
| 4 | `defaultOnEnter` waits for Delphine AND the player (`TriggerActor1/2`); the pack fed box triggers only the player's place (and followers/horses) | `addTriggerSubject` (the setting names Delphine's placement 0x13485): her place from the brain goes to `updateSubjectTriggers` |
| 5 | `ObjectReference.GetBaseObject` of the PLAYER was None, so `CheckTriggerRef` never counted him | the player's base is the NPC_ `Player` (0x7) |
| 6 | When her walk ended the scene let go of the package and Delphine walked off to her sandbox, so phase 6's wait (door closed + both in the box) never held | `SceneRunner::Options::mHoldFinishedPackages` (a finished package action keeps its package on the actor until the scene ends) |

Also with the setting: one order of alias packages by quest priority (task 0341), Blocking branches, quest greetings and the fragment flush (tasks 0304, 0308), live actor places, box triggers and the player's location events (task 0318).

## 3. In game (harness `tools/skyblade_check`, work folder `I:/Projects/OpenEden-work/papyrus`, five masters, player level 15)

**Run 1, Delphine's room (3 parallel runs of 15,200 frames, 80 normalised lines each, identical, md5 `8351b4839c55`):** stages 5 and 10 by the aid (stand-in for MQ105's end); the player is placed in
the room box by a Lua teleport (a stand-in for walking): the game's own box sets **stage 20**, `MQ106IntroScene` begins, Delphine force-greets by her package (voiced; frame
`keep349/not_here_follow_me.png` shows "Not here, follow me."), her chained INFO hands over the horn and sets **stage 30** (`TIF__000C65A2`: horn to the player, MQ105 stage 220),
the scene runs phases 1 to 10 and ends (lines "We need to talk.", "Follow me.", "Close the door.", "Now we can talk." with their voice files) and sets **stage 32**; with real clicks on the conversation box
(`inputs/intro_talk.txt`) Delphine's exclusive conversation runs 13 turns ("Surprised? ...", "You can't be too careful ...", "I didn't go to all this trouble ...", then the trust and horn topics;
frames `keep349/intro_greeting_menu.png`, `keep349/exclusive_menu_trust.png`), setting **stage 35** (`TIF__0003290B`).
**Run 2, Kynesgrove (3 parallel runs of 12,500 frames, 24 lines, identical, md5 `b746352d5597`):** start on the plain, stages 5 to 35 and **stage 50 (`Fragment_13`) by the aid**; two Lua teleports (to
Kynesgrove's marker, then to the mound) stand in for the ride: the game's own `MQ106PlayerScript` sets **stages 58, 65 (-> 60) and 75** from the player's location changes, `AlduinScene` (17 phases)
begins and Alduin speaks "Sahloknir! Ziil gro dovah ulse!" (voiced, subtitled), phase 2 gives Delphine the package `0004E837` and the scene waits there.

## 4. Stand-ins (all named)

The aid `story test quest` for stages 5, 10 (run 1) and 5 to 50 (run 2); the room box entered by a Lua teleport (no walk); the player teleports to Kynesgrove and the mound; the snap set (fault 1);
Delphine as a named trigger subject (fault 4); the click path through the exclusive conversation ends in the trust/horn loop.

## 5. Tests

- `testskyrimseblade349.cpp` (3 tests: a named Travel package that cannot be walked ends with the actor placed at the target; one not in the set does not; an arrival is not placed): **1 of 3 red** against the stub (the other two pin absence).
- `testskyrimseopenstate349.cpp` (1: function 157 answers 1 open and 3 closed on a reference): **1 of 1 red** against the unregistered function.
- `testskyrimsescenehold349.cpp` (2: a finished package stays until the scene ends; without the option it is cleared at once): **1 of 2 red** against the stub option.
- The glue (the setting block, `addTriggerSubject`, the lookup wiring, the player's `GetBaseObject`) has no unit test: the game runs are its evidence. No existing test edited.
- `components-tests` filter `*Skyrim*:*Dialogue*:*Papyrus*:*Scene*:*Quest*:*Package*` 4,266 ran, 4,135 passed, 131 data-gated skips, 0 failed; `openmw-tests` filter `*Neutral*:*Skyrim*:*Extension*` 1,192 ran, 1,190 passed, 2 skipped.
- Morrowind unchanged against `I:/Projects/OpenEden-work/integ2/build` (not a build of the exact parent): `tools/worldobjects_check` 110 objects OK, `tools/skymap_check/morrowind_check.sh` frames `1eb68236352dc472` and `c819db116f4af8a6` equal for both.

## 6. Not reached, ranked

1. The rest of Delphine's conversation (D1 "So what's the part you're not telling me?", the end topic "So where are we headed?", stages 42 to 50 by real clicks): the horn topic is not consumed after its answer and
   repeats (cause not found); stage 50 is the aid.
2. `MQ106LeaveRiverwoodScene` in its own right and the walk or ride to Kynesgrove (the player is teleported); Delphine's package `0004E837` of AlduinScene's phase 2 (she is in the inn's cell, unloaded at the mound): the scene waits.
3. Stage 90 and later: the mound's resurrection (`FXDragonMoundScript` enters `waiting` only from `OnCellLoad` with `isDragonHome == 1`, so the mound must load after stage 50; it did not in the runs), the
   fight with Sahloknir (a kill aid like `dragons test kill at` was not tried), stages 150 to 200, the soul.
4. `Actor.SetForcedLandingMarker` (a tolerant no-op), `RegisterForSingleLOSGain` and `OnGainLOS` (stage 70) are untried.

## 7. Decisions and notes for toddhow

1. Delphine's Travel packages are placed at their targets when the walk fails (the locked doors have no navmesh): a stand-in until NPCs open locked doors with their keys.
2. A finished scene package now keeps the actor where it ended until the scene ends (only with the setting); the engine's own rule is RECALLED-VERIFY.
3. The player's base for scripts is 0x7; Delphine is a trigger subject by a named placement id (0x13485).
4. Existing tests edited: none; three new test files were added to `apps/components_tests/CMakeLists.txt`.
5. **Not run:** the Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, a fresh review.
