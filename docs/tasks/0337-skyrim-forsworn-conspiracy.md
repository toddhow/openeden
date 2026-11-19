# Task 0337: Skyrim side quest "The Forsworn Conspiracy" (MS01, Markarth), first slice

Follow-up of tasks 0319 (box triggers for the player), 0333 (a city entry, scenes with a quest start), 0298 (scenes, blocking
branches) and 0271/0275 (forced greetings). Same hand wrote tests and code, tests first and run red (compile or link red where
the API did not exist; one real red run, see section 6), no fresh reviewer. Windows only. Branch `forsworn-0337` from
`748641d7d3`, rebased onto `eb7e9353a1`, local commits, not pushed. Behind the new pack setting
`[extension.skyrimse] forsworn conspiracy` (off; declared in `extension.json`; needs `papyrus`, `actor packages`, `quest alias
packages`; `combat ai` and `combat factions` for the fight).

## 1. The survey (houseCARL records, decompiled `.pex` of `Skyrim - Misc.bsa`)

- **The market scene is its own quest, `MS01IntroWorldScene`** (QUST 0xAF75F, priority 30, flags 0x1000, 8 aliases, stages 10,
  12, 100, 200, 300, 400, 500), not `MS01` (QUST 0x18B4B, "The Forsworn Conspiracy", 36 aliases, 38 stage fragments).
- **The start is a box trigger**, no story node: `MarkarthWorldIntroSceneTrigger` (REFR 0x753B4 in `MarkarthWorld`, WRLD 0x16D71,
  Lua world id `skyrimse:w93553`, at (-174144, 5603, -3253), box 806 x 765 x 189) carries `defaultsetStageTrigSCRIPT`: when the
  player enters it, it starts the quest and sets stage 10.
- Stage 10 (`Fragment_0`) starts the scene `MS01...WorldIntro` (SCEN 0xAF760, 3 phases, 4 actions: Kerah's "A bit of jewelry for
  your journey home?", Margret's pendant line, Weylin's "The Reach belongs to the Forsworn!") and makes the actors ghosts; the
  scene's end fragment sets stage 12 (`Fragment_5`: `Weylin.PlayIdleWithTarget(SneakKillMove, Margret)`) and 100 (`Fragment_1`:
  Weylin is put in the enemy faction, Kerah and Hogni `StartCombat(Weylin)`, ghosts off). `MS01WeylinScript.OnDeath` sets
  stage 200 (`Fragment_4`: starts `MarkarthIntroEndCombatScene`, the guard's "Everyone stay back ..." scene, 9 actions); its
  end sets stage 300 (`Fragment_7`: Eltrys `EvaluatePackage`, `RegisterForUpdateGameTime(1)`; the update sets stage 400,
  `Fragment_8`, and a script of the quest stops it at 500).
- **Eltrys's arrival is a package.** `MS01EltrysForceGreetPackage` (PACK 0x806CD): ForceGreet, conditions
  `GetDead == 1` run on the actor's LINKED REFERENCE (Eltrys's REFR 0x198FD is linked to `WeylinREF` 0x9C8AB, no keyword),
  `GetStageDone(MS01IntroWorldScene, 10) == 0`?? and `GetStageDone(..., 300) == 1` (the houseCARL dump gives the two stage conditions as stage 10 == 0 and stage 300 == 1; the pairing of operator and stage is read from the dump, not independently checked). The force
  greet topic `MS01EltrysForceGreetEntryTopic`: "Gods. A woman attacked right on the streets." Its choices lead to Eltrys's
  note: `MS01MiscObjectives` (QUST 0xD1958) stage 10 enables the note and gives it to the player (`MS01EltrysNoteScript.OnRead`
  sets stage 20), and `MS01` stage 10 starts the whole conspiracy (`Fragment_10`).
- Where the actors stand: Margret and Hogni in `MarkarthSilverBloodInn`, Kerah in `MarkarthEndonsHouse`, Eltrys and Weylin in
  `MarkarthWarrens`, all interiors (not loaded when the player stands in the market). The Markarth guard of the end scene
  (`00036F33`) is a placed guard near the market.

## 2. Faults found by running it, and fixes

| # | Fault | Cause | Fix |
| --- | --- | --- | --- |
| 1 | The box fired no scene: no `box trigger` line at all | The setting was not in `extension.json` (the pack's settings service never gave it) and, before that, only the Daedric, College, Dawnguard, main quest and Dragonborn paths accepted box volumes | the setting is declared and switches on `enableDaedric()` (box triggers for the player, a quest may move actors near him), the same world as the Daedric quests: one mechanism |
| 2 | `Actor.PlayIdleWithTarget` was a tolerant zero: Margret never died | the native was not registered | `PapyrusHost::setPairedKillMoves` and a native: an idle whose editor id contains `killmove` (every kill-move IDLE of the data: `KillMove...`, `SneakKillMove`, `pa_...KillMove...`) kills its target through `killActor`; the clip is not played (the death is at once, not at the clip's end); any other idle is a tolerant play. Off elsewhere |
| 3 | Kerah and Hogni never fought Weylin: `Actor.StartCombat` ignored every target but the player (task 0264: "only a fight against the player is built") | the native's rule | `PapyrusHost::setActorFights`, `PapyrusWorldHooks::startCombatAt`, `SkyrimCombat::scriptStartCombat(handle, target)`: both actors are engaged against each other (`aiBegin` with an actor target, the combat AI's own rules from task 0197) |
| 4 | Eltrys's ForceGreet package never passed after Weylin's death | two gaps in the condition world: (a) `GetDead` of another reference read only the record's initially-dead flag, never a death of this session; (b) a condition run on the actor's LINKED REFERENCE (run-on 4) was not answered | (a) `SkyrimConditionWorld::isDead` also asks the dead lookup (the pack's `mKilledRefs` and the death ledger), given to the dialogue AND the package world; (b) `SkyrimConditionWorld::setLinkedRefLookup` and `resolveRunOn(4)`: the speaker's reference's plain or keyworded link (`SkyrimWorldView::linkedRef`) |

## 3. What runs now

In game (`tools/skforsworn_check`, real clicks, frames viewed): the player is teleported into the trigger box; `MS01IntroWorldScene`
starts with 8 aliases filled, the scene plays Kerah's and Margret's voiced lines and Weylin's rant; stage 12's fragment kills
Margret by the kill move; stage 100 starts the real fight (Kerah, Hogni, Margret and Eltrys against Weylin, combat AI lines);
Weylin's death (test aid) sets stage 200, the guard's scene plays, stage 300 follows after the scene's 60 s, Eltrys's ForceGreet
package passes and opens his real conversation: "Gods. A woman attacked right on the streets. Are you all right? Did you see what
happened?" with the choices "I was right there. He almost killed me.", "I heard some shouting about the Forsworn. That was
it.", "(Walk away)", "I must have missed it. Sorry." and Goodbye (frame viewed).

## 4. Stand-ins and aids (hand-written, named)

- **The market actors are brought by hand.** Margret, Kerah, Hogni, Weylin and Eltrys stand in interiors that are not loaded; in
  the game their day schedules put them in the market (the pack has no off-screen schedule). The harness's `bring` step (a Lua
  teleport of the placement into the exterior near the box) stands in.
- **The player teleports into the box** (`world 93553 x y z`); the game's way is to walk in.
- **Weylin's death at the end of the fight is `summons test kill = ms01weylin@125`** (a developer aid): in the game the Markarth
  guards, who are not near here, kill him; Kerah and Hogni (civilians, health 15,821 for Kerah by the data's garbage totals)
  and Margret do not.
- The paired kill move kills at once, no clip is played.

## 5. Not reached (ranked)

1. The clicks through Eltrys's choices, his note (`MS01MiscObjectives` stage 10), reading it (stage 20) and `MS01` stage 10: the
   chain is built from data and nothing in it was changed, but it was NOT run. MS01 itself, the journal entry, the Shrine of
   Talos, Margret's journal, Weylin's room and the Warrens are not reached.
2. Weylin's travel package has no target (`LocationFallback` and alias package data are not resolved by the package brain), so scene
   phase 1 can wait about 60 s.
3. The kill move plays no clip (death at once); Kerah (15,821 health by the data's garbage totals) and Hogni cannot kill Weylin.
4. Three-run determinism and the Morrowind comparison were not run after the last change; the wider test filters were not rerun
   with the final headers (see section 6). The Linux gate, clang-format 14, clang-tidy and a fresh review were not run.

## 7. Decisions for toddhow

- The setting reuses the Daedric box-trigger world (`enableDaedric`): one mechanism for box triggers and actor moves near the player.
- A paired kill move kills at once (the idle's editor id decides); `GetDead` of another reference now also reflects deaths of the
  session (with the setting on); a package or INFO condition run on LinkedReference is answered through the unkeyworded XLKR link.
- No existing test was edited.

## 6. Tests

`apps/components_tests/skyrimse/testskyrimseforsworn.cpp`, 14 tests, written first. Evidence of red: the first nine did not link
against the old code (`PapyrusHost::setPairedKillMoves` unresolved) and, once the stub of the pure rule and the host API existed,
the GetDead test failed 1 of 9 (a death of this session was not dead for another reference). The three linked-reference tests
and the two fight tests did not compile against the old headers. No existing test edited. The glue (`enableDaedric` for the
setting, the combat's `scriptStartCombat(handle, target)`, the package world's lookups) has no unit test of its own; the game run
is its evidence.
