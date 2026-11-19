# Task 0344: Skyrim Companions, fifth slice (why Farkas's own Follow package was never chosen; Dustman's Cairn to the fragment)

Follow-up of task 0340 (`docs/tasks/0340-skyrim-companions-4.md`, section 5, item 1) and task 0334 (alias packages ordered by quest
priority). Same hand wrote tests and code, no fresh reviewer. Windows only, local branch `companions5-0344`, not pushed. Behind
`[extension.skyrimse] companions` and the settings it already needs (`quest alias packages`, `actor cell moves`, `dungeon mechanics`);
no new setting.

## 1. The root cause

0340 suspected the lookup of an alias filled by `ForceRefTo` in the package conditions. **That was not what kept the package out.**
`C01ObserverFollowPackage`'s two conditions are `GetStage(C01) >= 30` and `GetStage(C01) < 50` (read with houseCARL): no alias at all.
`OPENEDEN_PACKAGE_TRACE` (0340's `PKCHOSEN`) shows `C00FarkasHangAroundGiant` chosen with no condition line, so the selection never
evaluated a C01 package: **`C00FarkasHangAroundGiant` (owner quest `C00GiantAttack` 0x0C97D9, quest priority 80) comes before C01's
packages (priority 70) in the alias-package order of task 0334, and it has no conditions.** C00GiantAttack's stage 200 fragment ends
with `Quest.Stop()` (native trace: `quest.stop self 000C97D9`), but the host's `Quest.Stop` only changed the host's own quest state: the
package brain keeps its OWN copy of the quests (`mPackageQuests`, told of a start by `setQuestStarted` since task 0306 and of a stage by
`setPackageQuestStage`, never of a stop). So the stopped quest went on giving Farkas its packages for ever.

Fix (one mechanism, the stop joins the start): `PapyrusHost::setQuestStopped` + `SkyrimWorldView::stopPackageQuest`
(`QuestState::stop`, and the package list revision moves), called by the one `stopQuest` the native uses.

## 2. Other faults found and fixed

| # | Fault | Fix |
| --- | --- | --- |
| 1 | `SkyrimConditionWorld` asked `QuestState::aliasFill` for every alias question (`GetIsAliasRef`, run-on 5, the speaker test, `aliasKnown`), so an alias a script filled with `ForceRefTo` (Farkas as the C01 Observer; it lives only in the Papyrus host's table, which the package world sees as `setAliasView`) answered "not filled" | `SkyrimConditionWorld::fillOf(quest, alias)`: the alias view first for the context quest, then the quest state; the five places use it. NOT the cause of 0340's open item (section 1), but the suspicion was a real fault |
| 2 | `C01QuestScript.FightStart` calls `Actor.ForceAV("health", 1)` on each ambusher, which calls the native `Actor.ForceActorValue`, unregistered (a tolerant zero) | registered: sets the CURRENT value as `SetActorValue` does (health goes through `setHealth`) |
| 3 | developer aid: the package trace said nothing when a package was passed over before its conditions ran | `PKTRACE <package> passed over: <why>` (schedule, undecodable condition, package world not ready) through a new `PackageSource::packagePassedOver` (default nothing) |
| 4 | **0340's kill aid never ran: `summons test kill` is read only with `[extension.skyrimse] summons = true`** (0340's scenario had the key and not the switch), and its names are the ambushers' ACTOR editor ids (`LvlSilverHandAmbush1..5`, lower-cased by the setting), not the alias names `C01SilverHandAmbusher1..5` | scenario only: `summons = true` and `summons test kill = lvlsilverhandambush*@195` |

## 3. Tests (new files only, no existing test edited)

- `testskyrimsealiasforced.cpp` (`SkyrimAliasForced`, 4 tests): **1 of 4 failed** against the unchanged code (the alias view answers
  `GetIsAliasRef` for the speaker); the other 3 pin refusals (another actor, an empty alias, no view).
- `testskyrimsequeststop.cpp` (`SkyrimQuestStop`, 2 tests): **1 of 2 failed** with the listener call taken out (`if (false && ...)`,
  then restored); the other pins a stop with no listener.
- `testskyrimseforceav.cpp` (`SkyrimForceActorValue`, 1 test): **1 of 1 failed** against the unregistered native (a script's
  `ForceActorValue("health", 1)` must reach `setHealth`).
- Not red-first: the pack wiring (`setQuestStopped` in the glue) and the package trace lines; the game run is the evidence.

## 4. In game (`tools/skycompanions_check`; scenario files in `I:\Projects\OpenEden-work\combatai`: `final4.sh`, `r.sh`, `c4/in14.txt`)

NO `follower test recruit` aid any more. The real chain of 0340 to stage 30 (Skjor, Farkas's first line), then:

1. **Farkas follows by his own package**: `PKTRACE C01ObserverWaitOutsidePackage fails [... fn629 ...]`,
   `C01ObserverCheckOutCrapInAmbushSpace fails`, `C01ObserverFollowPackage passes`, `PKCHOSEN ... C01ObserverFollowPackage behavior 6
   activity 2 target 0/20 follows the player`. The harness teleport into `DustmansCairn01` carries him (`Neutral follower 108179
   carried to DustmansCairn01`).
2. The lever, the ambush scene `C01AmbushScene` (11 phases, real voiced lines), `ObserverDoTransform` (the spell and the ten script
   seconds), `ObserverActuallyTransform` -> `FightStart`: the five Silver Hand are enabled, un-pacified, `StartCombat` Farkas, and each
   is left at 1 health by `ForceActorValue`.
3. **The fight is not real**: the ambushers log `COMBAT ai lost ... (no way to the target)` (no path to Farkas or the player in the
   neutral interior, cause not examined) and there is no werewolf body (`SetRace`, `DropObject`, `SetInvulnerable` are tolerant
   zeros). **Stand-in (named): the aid `summons test kill = lvlsilverhandambush*@195` kills them, one an update, at combat second 195.**
4. **Real, the game's own scripts after the kills:** `AmbusherKilled` (all five dead) -> `C01PostAmbush.Start` (scene `000A6F8D`, 2
   phases, `SF_C01PostAmbush.Fragment_0` -> `ObserverTurnBack`, Farkas's package 000A3EEC) -> it ends.
5. **Real: the fragment pickup.** The harness teleports into `DustmansCairn02` and activates the placed `Fragment of Wuuthrad` (REFR
   0x6E807; Lua `activateBy`, not the real key): `PICKUP 0006e807 took 0006428e x1`, and **C01 stage 40 is set by the game's own
   script** (no aid: the stage-40 entry of the aid is dropped from the final scenario), `QF_C01.Fragment_3` starts `C02Kicker`.
6. **Real: Skjor on return** (teleport back to Jorrvaskr's basement, Farkas carried): "Ah, you're back." and the report with the
   fragment in the player's inventory, and **C01 stage 200** when the box closes.

## 5. Not done

- The fight and the werewolf: no path for the ambushers, no `SetRace`, no werewolf body (the data's `FakeFight` is false: the real
  fight kills them), so the kills are the aid's.
- The Dustman's Cairn doors and trigger boxes, the 17-draugr end controller, the radiant quests that start C01, Farkas's dismissal
  and Wait package, the real activate key on the fragment.

## 6. Decisions for toddhow

- A quest that a script stops now stops giving its aliases' packages (every quest, with `quest alias packages`).
- `SkyrimConditionWorld` answers alias questions from the alias view first (for the context quest); only package worlds and the
  bounty context set a view.
- Existing tests edited: none.
