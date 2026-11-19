# Task 0347: Skyrim Thieves Guild, fourth slice: TG01 debts by the game's own brawl

Design doc section: docs/design/ (Skyrim plan, milestone M-S6); follow-up of task 0314 (TG01 first slice).
Goal: collect the three debts of TG01 "Taking Care of Business" through the game's own dialogue, scripts and DGIntimidate brawl.
Inherited code touched: game_packs/skyrimse combat.cpp, combat_companions.cpp, combat_ai_run.cpp, papyrus_host.cpp, quest.cpp, world_view.cpp, skyrimse_game_pack.cpp; components/script_papyrus (a call-trace dev aid).

## What the data does
- TG01 stage 30 (Keerava paid), 40 (Bersi paid), 45 (Haelga paid), 50 (go back to Brynjolf; set by CheckJobs at GotCash >= 3), final fragment starts TG02.
- Keerava and Bersi offer a Brawl choice; its TIF fragment sends the story event FavorBrawlEvent, which starts DGIntimidateQuest (node 047AE9): stage 10 sets Confidence 4 and StartCombat on the player, stage 15 (OnEnterBleedout) forces the victory greeting (topic 047AC6), stage 100/200 restore the bleeding actor's health.
- Haelga pays through her own dialogue once the other debts are in.

## Faults found by running it (each one mechanism)
1. `Actor.StartCombat` from a quest fragment was refused while the combat records were still being read (`ready()` was false for any non-player): it waits for the records now.
2. `Actor.SetAV Confidence` did not reach the combat AI (the actor fled at 4 percent health and the fight never ended in bleedout): `setConfidence`.
3. Bleedout was invisible to scripts: new native `Actor.IsBleedingOut`, health reads 0 while bleeding, `Actor.RestoreActorValue health` stands the actor up (clears the bleedout).
4. A bleeding fighter kept fighting (and re-engaged): it leaves the AI and cannot engage until it stands.
5. A story quest started a second time kept its old stages (stage 15 "already done", no second brawl): `QuestState::resetStages` on a story restart and the package brain's own copy is told (`setQuestReset`, `resetPackageQuest`).
All behind `[extension.skyrimse] thieves guild` (and `companions` where it already applied); no new setting.

## Tests (apps/components_tests/skyrimse/testskyrimsetg01b.cpp, new file)
Written first and run red against stubs/the old code (counts in the changelog row); no existing test edited.

## Result in game (tools/skythieves_check, key-driven steps `keysteps = true`)
Keerava and Bersi brawls run by the game's scripts and the real clicks/presses; Haelga pays; TG01 reaches stage 50 (log lines `QF_TG01 Fragment_8, _13, _26, _19`).
## Not reached
Brynjolf's after-quest conversation and the TG02 start were not observed. The Bersi Brawl choice is sensitive to when the dialogue fragment flush runs (a fragment of an earlier line sets pTG01BersiWarned and removes the choice if the press comes after it); the scenario is therefore timing-dependent and NOT byte-identical across runs. Stand-ins: teleports between cells, harness placement and facing of the actor, `combat test player perks`.
