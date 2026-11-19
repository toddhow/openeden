# Task 0329: Skyrim Dragonborn, Raven Rock's arrival scene to its end, the Earth Stone, Neloth, the Temple of Miraak and Frea

Follow-up of task 0320 (Raven Rock). Same hand wrote tests and code, tests first where they could be (section 4), no fresh
reviewer. Windows only. Branch `solstheim3-0329` from `733daa4081`, rebased onto `e400a9f4ec` (task 0327), local commits, not
pushed. Behind the existing `[extension.skyrimse] dragonborn`; no new setting (one test aid got a list syntax).

## 1. What runs now (the game's own scenes, dialogue and Papyrus)

After the ship lands the player at Raven Rock (tasks 0312 and 0320):

1. `DLC2RRArrivalGoScene` runs to its end: phases 6 to 14 (Adril and Gjalund's talk about the prices), the scene ends, its
   fragment sets `DLC2RRArrivalScene` stage 200 and the quest stops. Phases 0 to 5 were task 0320's.
2. `DLC2MQ01` stage 10 (`Fragment_1`) moves Neloth (`DLC2Neloth`, placement 0x040177DA) from Tel Mithryn's interior to
   `DLC2MQ01NelothMarker` at the Earth Stone (23458, 31375, 637), stage 20 follows. With the player in the Earth Stone's cell,
   Neloth's ForceGreet package `DLC2MQ01WizardForceGreet` ("You there... You don't seem to be in quite the same state as the
   others here.") opens his conversation; "I'm looking for someone named Miraak." and "What does that mean?" end in
   `DLC2_TIF__0201FF51` (`DLC2MQ01NelothGlobal` 1 and **DLC2MQ01 stage 30**: objective 30, the temple's map marker).
3. The end trigger `DLC2MQ01EndTriggerRef` (a 1,741 unit cube at the Temple of Miraak, 56365, 62368, 9621; script
   `defaultSetStageTrig` with stage 200 and prerequisite stage 5) sets **DLC2MQ01 stage 200**: `Fragment_4` completes the
   objectives, **starts DLC2MQ02 "The Temple of Miraak" at stage 10** (`Fragment_0`: objective 10, `FreaHold` 1, four Seekers
   created) and stops MQ01.
4. At the Temple Frea (`DLC2Frea`, from the MQ02 scene quest `DLC2MQ02FreaTempleSceneQuest`, which MQ01 stage 5 starts)
   force-greets: "You there. What brings you to this place?", "Who are you?" and three more turns; her last line is
   `DLC2_TIF__0201CB5D` (**DLC2MQ02 stage 30**).

## 2. Faults found by running it, and fixes

| # | Fault | Cause | Fix |
| --- | --- | --- | --- |
| 1 | The arrival scene stood in phase 5 for the whole run | A phase's completion conditions are "the 120 s timer OR the forced greeting is done" (`IsSceneActionComplete` of actions 5 and 6, which are action INDEXES); the runner ended a phase only when its conditions held AND every action ending there was done, and the scene's other package (Gjalund's, phases 0 to 5) and the timer never finish | `PhaseEndRule::ConditionsWhenPresent`: a phase that lists completion conditions ends by them (its other actions are stopped), one that lists none ends when its actions do. Set with `dragonborn` only (`PapyrusDriver::enableDragonbornScenes`); the other scenes keep `ActionsAndConditions` |
| 2 | Neloth never greeted at the Earth Stone | His package conditions run on the cell the RECORD places him in (Tel Mithryn, an interior), whose location is not the Earth Stone's, so fn 359 (`GetInCurrentLoc`) failed (`PKTRACE` showed `fn359 ... no`, stage and `GetDisabled` ok) | `movedActorStandsOutdoors` / `gridSquareOf` (`dragonborn.hpp`): an actor whose recorded cell is an interior, the player outdoors and a live place from the brain takes the location of the grid square he stands on (`SkyrimWorldView::conditionsPass`, `locationOfGridSquare`). `dragonborn` now also gives the world the brain's live places (task 0277's hook, shared: `useLiveActorPlaces`, used by `companions`, `college` and `dragonborn`) |
| 3 | The aid could not start the quest the way the note does | `dragonborn test stage = 10` jumped over stages 5 and 7, so the Frea scene quest (started by stage 5's `Fragment_13`) never ran and the end trigger's prerequisite (stage 5 done) failed | `dragonborn test stage = 5+7+10,70`: the stages are set in order (each runs its fragments), the number after the comma is still the script second; one number is as before (`dragonborn::parseTestStages`) |

## 3. Checks

`tools/skysolstheim_check` with `solstheim3_input.txt` (see the header of `check.sh`): the ship's talk (task 0312), Adril's
greeting (Goodbye clicks), a `go` to the Earth Stone, Neloth's three turns (real clicks on the conversation box), a `go` to the
Temple, Frea's four turns. `OPENEDEN_QUIET_WINDOW=1`, volumes 0, fixed frame time, five masters. The two `go` steps (Lua
teleports in the same world) stand in for the walks: the Earth Stone is 6,600 units and the Temple about 40,000 from the
landing; the greetings and every quest stage after them are the game's own.

In game (three runs, `r320/fin1..3`, five masters, quiet window, volumes 0, fixed frame time, real clicks on the conversation
box; frames viewed in `I:\Projects\OpenEden-work\combat3\keep329`: `neloth_greeting.png`, `frea_box.png`, `fin1_s12.png`):
the arrival scene runs phases 0 to 14 and ends (quest `DLC2RRArrivalScene` stage 200, then stopped); Neloth greets at the Earth
Stone and DLC2MQ01 reaches 30, 200 (end trigger) and DLC2MQ02 starts at stage 10 with 8 aliases filled; Frea's four turns end
with DLC2MQ02 stage 30 (`Fragment_12`). **Three runs give the identical story trace** (`trace329.sh`: scene phases and lines,
forced greetings, conversation turns with their button counts and message sizes, quest stages, subtitles: 100 lines, md5
`a61014a37b4c` in all three; the harness `CMP` position lines equal too). The first order of those lines can differ by one
harness line (it comes from another thread), so the md5 leaves `CMP` out of the ordered list. 0 error lines, 0 dumps.

Morrowind unchanged against `I:/Projects/OpenEden-work/integ2/build` (a build of main, not this commit's exact parent):
`tools/worldobjects_check` 110 objects OK, fixed Balmora frame `1c5d47ad0b5dc32d` for both.

Unit tests: `*Skyrim*`, scene, dragonborn, college, companion and follower filters 521 pass (before the last formatting edit; the
three Solstheim files, scene and dragonborn filters pass after it).

## 4. Tests

`apps/components_tests/skyrimse/testskyrimsesolstheim3.cpp` (10 tests), written first against stubs and run red (counts here): the scene rule (4: 1 red, the other three pin the old rule, the stay-while-failing case and the no-conditions
case), the moved actor's place (3: 2 red), the aid's list (3: 3 red): **6 of 10 red** against stubs (the passing four pin the
old rule, the failing-conditions case, the no-conditions case and the missing-fact cases of the stub). The pack glue (`enableDragonbornScenes`, the live place
hook, the location substitution in `conditionsPass`) has no unit test of its own (the pure parts it calls do); the game run is
its evidence. No existing test edited.

## 5. Not built or not reached (ranked)

1. MQ02 beyond stage 30: Frea's follow package, the temple's cultists, the Black Book, the Apocrypha scene (stages 40 to 200).
   Frea's talk ends the run. The four Seekers MQ02 creates at stage 10 are not fought.
2. Skaal village: no quest here sends the player there before MQ02's later stages; nothing was built for it.
3. The Earth Stone's own scene (`DLC2PillarNelothReaction`, the workers in a trance, `DLC2Pillar` quest) was not run: MQ01's
   chain does not need it and the pillar quest is not started by anything in this slice. Neloth's greeting stands in the
   stone's circle (the player lies in a strange pose in the frames: not investigated).
4. The walks (to the Stone, to the Temple) are teleports of the harness; NPC walks there (Neloth is placed, not walked).
5. Scene lines use the subtitle path only (no audio device).

## 6. Decisions for toddhow

- `ConditionsWhenPresent` is chosen from the data (the phase lists "timer OR package done" as a condition and keeps running
  actions); Skyrim's real rule is RECALLED-VERIFY. It is on only with `dragonborn`.
- An interior actor moved outdoors takes the location of his grid square (live place) only with `dragonborn`; the brain's live
  places now also feed `GetDistance` conditions of scenes and packages with `dragonborn` (as with `companions` and `college`).
- The aid syntax `5+7+10` (stage 5 runs `Fragment_13`: the Frea scene quest starts, `SetActive`).
- No existing test edited.
