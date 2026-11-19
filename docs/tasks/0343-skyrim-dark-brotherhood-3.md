# Task 0343: Skyrim Dark Brotherhood, third slice (DB02 to its end, the Black Door, the Sanctuary, Nazir, DB03)

Status: DONE for the slice (DB02 to its end, the Black Door, the Sanctuary, Nazir's first contracts, DB03 stage 10); see sections 6 to 8 for what is not run. Follow-up of tasks 0294 (DB01) and 0325 (DB02 to stage 30). Same hand wrote the
tests and the code (the one pure rule test first and run red; the rest by the game, see section 5). No fresh reviewer. Windows only.
Branch `darkbrotherhood3-0343`, from the merge of tasks 0338 and 0340 (`979bbc98db`, local; the local `main` was still at
`31579ff7ea` when I started, so the talking-activator and Invisible Continue work of 0338 would have been missing), not pushed.
Behind the NEW pack setting `[extension.skyrimse] dark brotherhood` (off, declared in `extension.json`, README line in
`tools/testprofile`).

## 1. Survey (the records of `Skyrim.esm` read with a small Python parser, the `.pex` of `Skyrim - Misc.bsa` decompiled with houseCARL;
nothing of either is committed)

* **DB02 stages.** 10 (sleep greeting done), 30 (a captive died: `pDB02CaptiveNScript.OnDeath`), **40** (Astrid's key: `QF_DB02` Fragment_0
  completes objective 30, shows "Enter the Dark Brotherhood Sanctuary", registers an update), **50** (the player entered the
  Sanctuary), **200** (Astrid's welcome: `TIF__00020BDD` sets DB02 200 and DB02a 10), 220 (Astrid killed).
* **Who sets 40.** At stage 30 Astrid's second greeting is the Blocking branch `DBAstridSecondGreetBranch` (DLBR flags 0x02, start topic
  `DBAstridSecondGreetForcegreet` 0x2B831; no package forces it, the name is old). Its INFOs choose by `GetDeadCount(captive base)`
  (fn 84, one INFO per combination of dead captives). The player's reply `DBAstridSecondGreetResponse1..3` links to
  `DBAstridThirdGreetBranch` whose INFO `TIF__00022F34` (the "So... I'm free to go?" line) sets **stage 40**, moves the shack key
  from Astrid to the player, sets `DarkBrotherhood.BlackDoorPass = 1` and enables the Sanctuary's map marker.
* **Astrid leaves.** `DBAstridShackLeaveTriggerScript` is on the base ACTI 0xFE52A (the box REFR 0xFE52B in `AbandonedShackExterior`,
  at the shack's door): `OnTriggerEnter` by the player at DB02 stage 40 sets `AstridMove = 1` and `MoveTo`s Astrid to the Sanctuary.
* **The Black Door** (TACT `DBBlackDoorActivator` 0x25BFA, REFR 0x2B83B at (-42175, -83965, -4440) in Tamriel, voice type
  `MaleUniqueDBBlackDoor`): its Blocking branch `DBBlackDoorSanctuary` speaks "What is the music of life?" with four answers;
  answer 1 ("Silence, my brother.", shown while `BlackDoorPass == 1`) runs `TIF__0002B836`: the door's sound, `Game.GetPlayer().MoveTo`
  the Sanctuary marker, `BlackDoorPass = 2`, the doors swap. Its line "Welcome home." follows.
* **Stage 50.** No quest script sets it: it is the base ACTI `DB10SancTrigger` 0x4DD9E (script `DB10EntranceScript`, a box REFR
  0x4DD9F inside `DarkBrotherhoodSanctuary` at (3199, -411, -283)): `OnTriggerEnter` by the player at DB02 stage 40 sets stage 50.
  The package `DBAstridSanctuaryFirstGreet` (ForceGreet, topic `DBAstridSanctuaryFirstArrive` 0x2B83E, DB02 stage 40 to 50) walks Astrid to him.
* **Stage 200.** Astrid's "So what happens now?" opens `DBAstridSanctuaryGreet` and its three replies `DBAstridNextStep1..3`; the first
  (`TIF__00020BDD`, DB02 stage 50) sets DB02 stage 200 and DB02a stage 10. DB02a ("Sanctuary", `QF_DB02a` Fragment_2) gives the faction and
  the four armour pieces.
* **Nazir** (NPC_ 0x1C3AB): `DBNazirFirstGreetBranch` (Blocking) when DB02a >= 10; its replies lead to `DBNazirContractBranchTopic` and
  `DBNazirAnythingElseTopic`; `DBNazirContract1ReadyBranch` (`TIF__00020017`) sets DB02a 200 and the first three side contracts (stage 10).
* **DB03 "Mourning Never Comes"** (QUST 0x1EA52): Astrid's Blocking branch `DBAstrid03GiveBranch` (DB02 >= 200, DB03 < 10) and its two
  replies end in `TIF__0001FED8`: DB03 stage 10.


## 2. What was missing, found by running it (in the order the run reached them)

| # | Fault | Cause | Fix |
| --- | --- | --- | --- |
| 1 | After the captive's death Astrid said her plain "Hello" ("Brother."), not the second greeting | the INFOs of `DBAstridSecondGreetBranch` test `GetDeadCount(captive base)` (fn 84), which the condition world answered 0 for everybody | `SkyrimConditionWorld::deadCount(base)`: the placements of that base the dead lookup says the session killed; the pack installs the lookup (`mKilledRefs`, the death ledger) with `dark brotherhood` |
| 2 | The game could not be saved and loaded in the middle of the chain | the courier letter DBEntranceQuest creates (a created reference with a script) was refused by `attachKeyExists`: "the game no longer has what its attach key names" | a form key that is a created reference the host restored is accepted. Task 0342 added the same fix under its own switch while I worked; **one rule now** (the created table, or 0342's switch, keeps the old answer) |
| 3 | At the Sanctuary Astrid answered with her advice on murder ("Try not to? ... a very high bounty") instead of offering the next job | `DBAstridCaughtBranchTopic` is a nameless branch start whose only INFO names its speaker, so task 0304's greeting rule took it for the greeting; its INFO has a PROMPT (RNAM): it is a menu topic, the player's own words | `DialogueIndex::questGreetingTopics` leaves out a start whose every INFO carries a prompt (134 such starts in the five masters; 3 mixed ones keep being candidates). The Blocking branch `DBAstrid03GiveBranch` then speaks |

Not faults (they ran with what was already built): the shack's leave box (`DBAstridShackLeaveTriggerScript` is on the base ACTI, the box is
the REFR's XPRM: the Daedric quests' box mechanism), the Black Door (a TACT: the talking-activator mechanism of task 0338), the player's
`MoveTo` into the Sanctuary (the travel service's move sink of task 0325), the entrance box that sets stage 50 and the ForceGreet walk
(task 0305's approach), the Blocking and walk-away branches (tasks 0298, 0315).

The new setting `dark brotherhood` only switches those existing mechanisms on (`enableDaedric`: primitive boxes and the relocation world; quest
greetings; Blocking branches; walk-away topics; the fragment flush; the NQUESTGREET trace), installs the dead lookup, the player's item
source (the shack key) and the activation of placed activators (the Black Door).

Update after the first fix round: the prompt-only quest-start rule (fault 3) is now behind `dark brotherhood`
(`DialogueIndex::questGreetingTopics(skipMenuStarts)`, `DialogueRuntime::setPromptStartsAreMenus`); with the setting off nothing
changes. The earlier 134-starts count was measured over the five masters.

## 3. Stand-ins (hand-written, named)

* DB01 is stood in by the story aid (`story test quest = 01EA50:200@1`); the sleep, the shack and Astrid's wake-up talk are the real chain.
* The captive's death (`DB02Captive1`) and Narfi's death are the test kill aid (`summons test kill`), not a kill by the player.
* Teleports: to the Black Door (`ext` step), into the Sanctuary (`cell` step; the game's own `MoveTo` through the Black Door also runs),
  and a `furn` step puts the player in the bed in the Bannered Mare.
* Hand-set: nothing else; every stage after 30 is set by the game's own dialogue fragments and scripts.

## 4. Results

Phase 1 (new game, sleep, shack, captive dies, DB02 stage 30, save) three parallel runs: each exit 0, 0 dumps, 0 errors; the filtered
100-line traces are byte-identical (md5 `69137d9bccfe`). Phase 2 (load in a new process, real mouse clicks) three parallel runs from one
save: each about 20,800 frames, exit 0, 0 dumps, 0 errors; the filtered 128-line traces (stage sets and fragments, NTALK, JOURNAL,
SCENE, NLOCK, deaths, rest) are byte-identical (md5 `a92b2e1c2d02`). The chain: DB02 stage 40 (key), 50 (entrance box), 200 (Astrid's
welcome), DB02a 10, 20 and 200, the three side contracts (stage 10), DB03 stage 10 (`QF_DB03` Fragment_3). Frames viewed: the Black
Door with its tooltip, the journal listing "Mourning Never Comes" with the contracts and "Delayed Burial".

## 5. Tests and red counts

`SkyrimDb03DeadCount` 3 tests (2 red first against the stub), `SkyrimDb03Greeting` 5 tests (2 red first; the setting-off test was added
with the gating). No existing test edited. A unit test of the merged `attachKeyExists` rule was attempted in a new file and removed: the
fake host did not save (empty blob), cause not found in the time available; the rule is covered only by the in-game save and load of
the courier letter (fault 2).

## 6. Not reached / not run

Linux gate, clang-format 14, clang-tidy, the Lua tests, `openmw-tests` pass count, a fresh review, a kill by the player, the captives'
own dialogue, DB03 beyond stage 10, the unit test of `attachKeyExists`.

## 7. Morrowind unchanged

See the changelog row (worldobjects_check and the Balmora frame).

## 8. Decisions for toddhow

* `attachKeyExists` accepts a created reference the host restored (shared with task 0342, one rule, ungated): confirm.
* Prompt-only quest starts are menu topics only with `dark brotherhood` on; the general rule could be made default.
