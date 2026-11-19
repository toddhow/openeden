# Task: Skyrim Dawnguard, "Awakening" inside Dimhollow Crypt (task 0330)

Status: ordered by the coordinator on 2026-11-18, follow-up of task 0319. Windows only. Local commit on branch `dawnguard3-0330` from `733daa4081`, not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7); tasks 0319 (the fort and the start of the quest), 0305, 0289 (dungeon
mechanics), 0299, 0302 (ambushes), 0293, 0291 (interior water), 0254 (deaths), 0194 (scenes), 0310 (box triggers, alias scripts).
Goal: from the crypt's door to Serana's conversation and the walk out, through the game's own quests, scenes, dialogue and Papyrus.

## 1. What the data says (houseCARL on the five masters; scripts decompiled earlier)

`DLC1VQ01` "Awakening" (0x0200352A) runs in two interior cells, `DLC1DimHollowCrypt01` (the vampires, 0x02000EB8) and `DLC1DimHollowCrypt02`
(the puzzle room, 0x02000EB7), joined by the doors 0x020010C9 (-271, 3054, -10) and 0x020010D8.

| stage | what sets it | what it does (the quest's fragment) |
| --- | --- | --- |
| 10 | the quest's start (task 0319's chain; here the developer aid) | `Fragment_2`: the dungeon enabled (the marker whose enable CHILDREN are the vampires), the map marker, `DLC1VQ01RNPC` |
| 15 | box `DLC1VQ01Stage15Trigger` (397, 136, 220), `defaultsetStageTrigSCRIPT`, prerequisite stage 10 | `Fragment_9`: `Scene.Start DLC1VQ01Stage15VampireScene1` (0x0201880E, 8 phases), the vampires hurt |
| 17 | a trigger at the second cell's arrival | `Fragment_23`: `DLC1VQ01PuzzleRoomScene` (0x0200FB20, 13 phases; phase 0 waits for `HasLoaded3D` of Adalvald and the boss) |
| 18 | the scene's phase 5 fragment | `Fragment_25`: `Adalvald.Kill()` |
| 19 | the button (`DLC1VQ01PuzzleButtonScript`, 0x02006892): not while the player is in combat | `Fragment_27`: `SendAssaultAlarm`, stage 18 again; the controller goes to state `Solution` |
| 20, 30 | the coffin activator (`DLC1VQ01PuzzleCoffinScript`, 0x0200F7F4), after the five braziers | `Fragment_3`: `SeranaExitScene` (0x02019F16, a package for Serana), objective 30 done, 40 shown |
| 200 | INFO fragments of Serana's talk (`DLC1_TIF__02011973`, `...0100B669`, `...01019F12`) | `Fragment_1`: objective 40 done, `DLC1VQ02.SetStage(5)` (the quest `02002F65`) |

The puzzle (`DLC1VQ01PuzzleControllerScript`, five `DLC1VQ01PuzzleBrazierScript` objects of ONE base at ONE place, brazier ids 1 to 5) is
solved when the braziers 1 and 4 are in state `outer` and 2, 3 and 5 in `inner` (a brazier goes to the side OPPOSITE the marker the
activator is nearer to: the harness stands on the side of the marker the data names), in order, each activation advancing the controller's `Line0n`.

## 2. What was missing, found by running it

1. **The vampires were not there**: a placement's enable PARENT enables its children (task 0274's rule), but only with `main quest start`.
   `DLC1VQ01LvLVampire1/2` and the other crypt actors are children of the marker stage 10 enables. `pushEnabled` now also propagates with
   `dawnguard`.
2. **Explicit scene start with no loaded actor was silent** (task 0327 meanwhile added the same line, kept: `SCENE x not started:
   no actor of it is loaded`. (Without fix 1 this was the whole symptom: the scene never began.)
3. **`HasLoaded3D` of anyone but the player was false**, so the puzzle room scene's phase 0 (a completion condition on the aliases of Adalvald and
   the boss) never ended. `SkyrimConditionWorld::setLoadedLookup` (the same rule as the `Is3DLoaded` native: an actor is loaded when the actor
   system holds it) and `otherAnswers` answers fn_555 when a lookup exists. Behind `dawnguard`. Log line `HASLOADED3D <ref> yes|no` on a change.
4. **`OnRead` reached only the scripts of the book's own reference**, not the script of a quest alias that holds it (`DLC1VQ01BookScript` on the alias
   `Book`). `PapyrusHost::onRead` also sends to `aliasInstancesHolding`; a log line says how many scripts heard it. (The book is not needed for the
   chain: the second cell's arrival trigger sets stage 17. The fix is general and found by reading the world book.)
5. **The player drowned in the puzzle room.** Both cells have the water flag and an XCLW of 0.0 with no water type (Skyrim's editor default), the
   second cell's content lies down to -2,500 and its entrance at 0, so task 0291's rule (some content under the plane, no entrance deeper than
   128) believed a plane at 0 and the player lost 3 health a second (drowning). New rule, in `interiorWaterPlausible`: a plane that lies more
   than `kMaxQuarterSubmersion` (300) over the height under which a QUARTER of the cell's placed content lies is not believed (42 percent of this
   cell's content is more than 300 under 0). And a START cell is asked about its water before it is built, which task 0291's code answered from
   the record alone (its own test pins that), so with `dawnguard` the view reads the cell for the question (`setJudgeWaterEarly`).
6. **Corpses fought.** 6 `TreasCorpseSkeletonRigid` placements in the puzzle room carry the ACHR record flag "starts dead" (0x200) and engaged the
   player on sight (1 damage a blow). The view records the flag as it reads a cell (`startsDead`), the actor table's add hook gives the keeper a
   ledger entry at the clock's hour (`DeathKeeper::noteStartsDead`) and task 0254's machinery then marks the actor dead in its death pose. With
   `dawnguard`.
7. Diagnostics worth keeping: the host's native trace (`OPENEDEN_PAPYRUS_NATIVE_TRACE`) names the instance's state in each event line and says
   what a tick ran (stacks, finished, waiting, errors).

## 3. In game (`tools/skydawnguard_check/awaken.sh`, real clicks on the conversation box; frames in `I:\Projects\OpenEden-work\dg330\keep`)

A new game in `DLC1DimHollowCrypt01` (five masters, `dawnguard`, `dungeon mechanics`, `summons`, `combat ai`, `combat factions`; the quest is put at stage 10 by the
aid `story test quest = 0200352A:10`). The chain, all by the game's own data and scripts: the box sets stage 15 and the vampires' scene
(`0201880E`, voiced) plays; the door to the second cell is activated by the harness; the arrival sets stage 17 and `DLC1VQ01PuzzleRoomScene`
(`0200FB20`, 13 phases, Lokil and the female vampire kill Adalvald: stage 18); the harness kills the vampires with the aid `summons test kill =
DLC1LvlVampire*@92,LvlVampire*@92` (the button refuses while the player fought lately: the game's own rule) and activates the button (stage 19),
the five braziers in the data's order (the controller's `Line0n` state) and the coffin: stages 20 (`SeranaExitScene`, Serana leaves the bed with
`bed_leftexit`) and 30; Serana's conversation by the real mouse (25 turns, real voiced lines, frames `serana_talk.png`): stage 200 and
`DLC1VQ02` started at stage 5; a last bottom-up sweep clicks Goodbye; the exit door puts the player in Tamriel at (14808, 51106, -1098)
(`screenshot011.png`, snow forest). **Three runs give a byte-identical 92 line trace** (quest stages, scenes, NTALK turns without the poll counters,
box triggers, deaths; md5 prefix `77f9130bffc5`, file `keep/trace_k4_k5_k6_identical.txt`), 0 errors, 0 dumps.

## 4. Not built, in rank order (stand-ins named)

1. **Serana does not follow the player.** Her package `DLC1VQ02SeranaAbandoned` (VQ02 stage 5, stage 10 not set, not in `CurrentFollowerFaction`) is above the follow
   packages, and the recruit dialogue path (task 0239) was not examined in the crypt. The walk out is the player's.
2. The puzzle's animations (braziers, coffin lid, doors): `PlayAnimation`/`PlayAnimationAndWait` on these models are tolerant no-ops here (no
   Havok project of task 0289's kind was found for them), so the scripts' states and stages run, nothing moves.
3. Stand-ins: the key press (the harness activates the doors, the button, braziers, coffin and Serana by `activateBy`: a real Space press found nothing in
   the dark room), teleports into the boxes and between the cells (`abs`), the vampires' death (the aid; the player never fights them), the way out.
4. `Faction.SendAssaultAlarm` is unregistered (tolerant).
5. The Dimhollow rooms past the exit door, Serana's follower AI, the VQ02 chain after stage 5.
6. A segmentation fault at shutdown seen once in a run that died early (exit 139 at `Save thumbnail`, after auto-quit; not reproduced in the later runs; not investigated).

## 5. Stage log and tests

- Tests: `apps/components_tests/skyrimse/testskyrimsedawnguardawake.cpp`, 9 new (`OnRead` reaching an alias script: red 1 of 2 against the old host; the
  `HasLoaded3D` lookup, the quarter-height water rule (3), `noteStartsDead` (2) and the early water judgement: compile-red, the APIs did not exist). Green with
  the `SkyrimInteriorWater*` and `SkyrimDeath*` suites. No existing test edited. One existing test pinned the old early-water answer
  (`SkyrimInteriorWater.aCellThatWasNotBuiltYetFallsBackToTheFlagAndHeight`): kept, the new judgement is opt-in (`setJudgeWaterEarly`, only with `dawnguard`).
- No new setting (all behind `[extension.skyrimse] dawnguard`), so no test profile line.
- Decisions for toddhow: (1) the water quarter rule (300 units) is ours, tuned on this crypt and the 71 cells of task 0291; (2) a placement with the "starts dead"
  record flag becomes a corpse with `dawnguard` (the flag's name is RECALLED-VERIFY: CK wiki); (3) `HasLoaded3D` of an actor is "the actor system holds it".
- After the rebase onto `3fcb5a7fa0` (task 0329 took the scene-start log line, kept): `components-tests` filter `*Skyrim*:*Papyrus*:*Scene*:*Death*:*InteriorWater*:*Dialogue*:*DawnguardAwake*` 4,044 pass, 0 failed; a fourth game run gives the same trace (md5 prefix `77f9130bffc5`).
- Morrowind unchanged against `integ2`'s build: `worldobjects_check` OK, Balmora frame `1c5d47ad0b5dc32d` for both.
