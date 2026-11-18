# Task: Skyrim Dawnguard, Fort Dawnguard and the start of "Awakening" (task 0319)

Status: ordered by the coordinator on 2026-11-18, follow-up of task 0305. Windows only. Local commit on branch `dawnguard2-0319` from `5648e037bc`, not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7); tasks 0305 (the start of the questline), 0274, 0292, 0310 (box triggers for the
player), 0194, 0268, 0308 (scenes), 0264 (forced greeting).
Goal: from the road to Fort Dawnguard, through the fort, Isran's introduction and his conversation, to the quest "Awakening" (DLC1VQ01) with Dimhollow Crypt as
its target, through the game's own quests, triggers, scenes, dialogue and Papyrus.

## 1. What the data says (houseCARL on the five masters, scripts decompiled earlier in task 0305)

`DLC1VQ01MiscObjective` ("Dawnguard", quest 0x0200D06C) is driven by FIVE trigger volumes of the game's own script `defaultsetStageTrigSCRIPT` (property
`stage`, `doOnce`, `disableWhenDone`), all boxes except the last, in two places:

| stage | volume | cell | what it does (the quest's fragment) |
| --- | --- | --- | --- |
| 50 | box at (-6342, -10397, -3259) | `DLC1HunterWorldStart`, world `DLC1HunterHQWorld` (the road to the fort) | Fragment_9: Agmaer's scene, Durak moved if not loaded |
| 80 | box at (5547, 2303, -3050) | `DLC1HunterWorldFort01`, same world | Fragment_25: Celann evaluates his packages |
| 100 | box at (6796, 725, -1961) | same cell | Fragment_10: stops Agmaer's scene, starts Isran's intro scene |
| 110 | box at (-1134, -902, 108) | interior `DLC1DawnguardHQ01` (the fort) | Fragment_11: `DLC1VQ01IsranIntroScene` (Celann, Isran, Tolan talk) |
| 115 | sphere at (-1148, 316, -128) | the fort | Fragment_19: stops the scene, Isran's package is re-evaluated |

Isran's package `DLC1VQ01IsranForcegreet` (stage >= 115 and < 140) forces the greeting; the greeting INFO's fragment sets stage 140; the conversation
(`DLC1VQ01IntroTopic` and its links) ends with the quest at stage 150 (`DLC1VQ01IsranTolanDimhollowScene`), and that scene's fragment sets stage 180
(`Fragment_31`: `CompleteAllObjectives`; `Fragment_16`: `DLC1VQ01.SetStage(10)`, the quest "Awakening" 0x0200352A, whose alias `DimhollowEntrance` is the interior side
(REFR 0x02002F71) of the exterior door REFR 0x02002F6E at (6185.85, 64722, -4952.9) in Tamriel).

## 2. What was missing, found by running it

The whole chain ran through the existing machinery (scenes, forced greetings, Papyrus fragments, the conversation, the journal) once three things were in place:

1. **The box triggers did not fire for the Dawnguard quest.** `PapyrusHost::addTrigger` accepts a box only when a pack setting asks for it (`main quest start`, `college`,
   `daedric quests`: the word wall tests pin that a plain box never fires). New pack setting **`[extension.skyrimse] dawnguard`** (off, declared in `extension.json`): it
   turns on the same world as the Daedric quests (`enableDaedric`: boxes for the player, quests move their actors near him, the quest greetings and the fragment flush) and
   adds the flag `mDawnguard`, so there is one mechanism, not two.
2. **`Quest.CompleteAllObjectives`** was declared and not registered (a tolerant no-op), so the "Dawnguard" quest never finished its objective. Now it completes every
   objective the quest has displayed and moves the objective revision (the journal rebuilds): the journal's "Completed" tab lists "Dawnguard".
3. Nothing else: the scenes (0200D928, 0200D92D), the forced greeting and its fragment to stage 140, the four-turn conversation and the stage 150, 180 chain all
   worked unchanged. In the first run without the setting stage 115 fired (a sphere), no box did.

## 3. In game (`tools/skydawnguard_check/fort.sh`; frames and logs `I:/Projects/OpenEden-work/dg305/runs/q1`, `d5`)

New game in `DLC1HunterHQWorld`, level 10, `dawnguard = true`, `story test quest = 0200D06C:5,0200D06C:10@1` (the aid stands in for Durak's approach, which is task 0305's
check): the player is stood in the game's own boxes in order (frames 600, 1500, 2300), then into the fort (frame 3000), then in the sphere (frame 7500); clicks go through the real
mouse path. The log shows: stage 50 (and `Scene.Start 0200D91A`), 80, 100, 110, `SCENE 0200D928 begins (12 phases, 15 actions)` with Celann's "Here to join the Dawnguard? Good.",
Isran's and Tolan's lines (real voice files and subtitles), stage 115 and "stopped", `FORCEGREET the actor ... wants to say topic`, `PAPYRUS dialogue line 0200D8DC` (stage 140),
the conversation (four turns: "So who are you? What do you want?" with the four real choices, "Got a fire in your belly...", ...), `FORCEGREET conversation ended`, stage 150,
`SCENE 0200D92D begins (15 phases, 15 actions)` ("Tolan, tell him about, what was it, Dimhollow?"), stage 180, **`quest 0200352A started with 6 aliases filled`, stage 10
(`QF_DLC01VQ01_0100352A.Fragment_2`: the entrance enabled, `MAP discovered by a script`)**, quest 0200E6A7 started. Frames: the fort's hall with Isran speaking, the conversation
box, and the journal (J): "Awakening - running - I've met Isran, the leader of a group of vampire hunters known as the Dawnguard ... [ ] Find out what the vampires are seeking",
with "Completed (1)" holding "Dawnguard".

Dimhollow Crypt (`DIMHOLLOW=1 fort.sh`, run `d5`): a new game in Tamriel beside the exterior door, `story test quest = 0200352A:10`; the door is activated through
`object:activateBy` (a stand-in for the key, as task 0138's door tests) and the player arrives in `DLC1DimHollowCrypt01` at (-404, 28, 12) (frame viewed: the cave mouth inside the crypt).

**Determinism.** Five runs of the fort scenario: four give the same 61 trace lines (md5 `48b53fcf80e2`, the quest, scene, dialogue, conversation and map lines with the
click-timing numbers `poll N` cut: the conversation is driven by clicks, whose frame differs by a few polls), one (`q3`) ended with exit code 1 right at the conversation's
start with no dump (the crash catcher is switched off in these runs) and did not repeat in the next two runs: not investigated, probably another agent's `taskkill` of an
`openmw.exe` (other sessions were running games), not shown.

## 4. Not built, in rank order (stand-ins named)

1. **The player's walk and the real doors.** The check teleports the player into each trigger box and into the fort (a `cell` step); the exterior door of the fort and the
   worldspace door from Tamriel to `DLC1HunterHQWorld` (REFR 0x020110B5) are not used. Dimhollow's door is activated with `activateBy`, not the activate key.
2. **Agmaer's scene at stage 50** (`Scene.Start 0200D91A`) is refused silently (`SceneRunner::startImpl`, no actor loaded: Agmaer stands in the fort interior). The real game starts it and
   its actors take part when their cell loads; stage 100 stops it anyway, so nothing is lost in this chain. A retry like task 0268's `mRetryQuestStart` for a script's explicit start was not built.
3. **Durak's walk (task 0305 gap 2) is NOT fixed.** Reproduced and narrowed with `OPENEDEN_NEUTRAL_ACTOR_TRACE`: after the CLOC the messenger runs from (21436, -7780, -3590) at 350.87 units a second
   on a navmesh path whose waypoints are (21936.8, -7690.8, -3559.15), (22059.2, -7677.2, -3538.75), (23398.8, -7670.4, -3423.15), (24568.4, -7680.9, -3381.3), and stops at
   **(21972.1, -7687.4, -3526.4) with `blocked edge`**: that is `NeutralMover`'s "no floor within the step down" (`neutralmover.cpp`, the second `mEdge` branch), 87 units before
   the next waypoint, and 12 units ABOVE the navmesh's height there: he rests on the rim of something that the navmesh does not treat as floor (a rock or a stone: the same family as
   the dragon's blocked walk of task 0280, whose fix is for capsules of radius 120 or more). What is there was not identified; a ray probe or a collision debug frame at that spot is the next step.
4. Natives still tolerant and logged in this chain: `actor.GetLevel`, `actor.AllowPCDialogue`, `actor.IsEquipped`, `actor.SetCrimeFaction`, `quest.UpdateCurrentInstanceGlobal`.
5. Stage 130 (`DLC1VQ01IsranAgmaerScene`'s crossbow, Fragment_22/30) and the crossbow and 45 bolts of stage 180 (`AddItem` on the player) were not checked in the inventory.
6. Entering Dimhollow Crypt is as far as this task goes; Awakening's own stages (Serana's sarcophagus, the puzzle) are later work.

## 5. Stage log

- **Red/green.** New `apps/components_tests/skyrimse/testskyrimsedawnguardfort.cpp` (registered in the list), 3 tests: `completeAllObjectivesFinishesTheObjectivesTheQuestHasShown`,
  `anObjectiveTheQuestNeverShowedIsNotCompleted`, `theDawnguardSettingIsDeclaredAndOffByDefault`. Run red against the unchanged host: **1 of 3 failed** (the first; the other
  two pin absence and the setting, which I had already added when I wrote the file: no red run for the setting test). All 3 pass after the native; the 24 `SkyrimDawnguard*`,
  `SkyrimPapyrusObjectives*` and `SkyrimJournal*` tests pass together. The `dawnguard` glue itself (box acceptance, the shared world) has no unit test, its evidence is the game run.
- **Existing tests edited: none.**
- Edits outside the new files: `game_packs/skyrimse/src/skyrimse_game_pack.cpp` (`mDawnguard`, `enableDawnguard`, the settings block), `papyrus_host.cpp` (the native),
  `extension.json` (the setting), `apps/components_tests/CMakeLists.txt`, `tools/testprofile/README.md` (a line, the setting is a pack setting like `daedric quests`),
  new `tools/skydawnguard_check/fort.sh` with `fort_input.txt` and `dimhollow_input.txt`.
- **Morrowind unchanged** against `I:/Projects/OpenEden-work/integ2/build` (a build of `main`, not of the exact parent): `tools/worldobjects_check` 110 objects OK (111 result lines equal),
  the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) SHA-256 prefix `1c5d47ad0b5dc32d` for both, and the save round trip in both directions equal.
- Rebased onto `main` (`bbd3bb965a`, one commit ahead, a docs commit that touches none of these files): the runs above are from the tree before that rebase.
- **Not run:** the Linux gate and GCC `-Werror` (the new code was checked by hand against the trap list), clang-format 14, clang-tidy, the Lua integration tests, `openmw-tests`
  (nothing in the engine changed), the whole `components-tests`, a fresh review, a walk (not a teleport) through the boxes, a crossbow check.
- **Decisions for toddhow:** (1) the setting is a separate `dawnguard` that reuses `enableDaedric` (one mechanism, two names); whether the two should merge into one "radiant quest world"
  setting is yours. (2) `CompleteAllObjectives` completes only DISPLAYED objectives (the engine's own meaning is recalled, RECALLED-VERIFY).
