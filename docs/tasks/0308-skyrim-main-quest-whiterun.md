# Task 0308: Skyrim main quest, the Whiterun part (MQ102, MQ103, MQ104)

Branch `mainquest2-0308` from `f9031c2885`. Windows only, not pushed. Parts 1 and 2 run in game through real dialogue; part 3 is partial (sections 3 and 4).
Behind the new pack setting `[extension.skyrimse] main quest whiterun` (off by default; needs `papyrus`, `quest alias packages`,
`actor packages`). The Helgen opening is parked: the chain starts with the existing aid `story test quest = <hex quest>:<stage>[,...]`
(task 0226, generic: it starts the quest if needed and sets each stage in order; no new aid was needed, task 0305's note that none exists
was about DLC form ids).

## 1. Survey (houseCARL on Skyrim.esm, scripts decompiled from `Skyrim - Misc.bsa`)

- **MQ102 "Before the Storm"** (QUST 0x04E50D, priority 82, RunOnce). Objectives 10 talk to the Riverwood friend, 20 travel with the
  faction friend, **30 "Talk to the Jarl of Whiterun"**. Stage -> fragment (QUST VMAD): 5 F81 (PerksQuest, MQ00), 10 F83 (MQ101 stage 900,
  objective 10), 30 F117 (objective 10 done, 30 shown), 75 F88 (`DragonsreachScene.Start`), 80 F90 (empty: scene phase 0 waits for it),
  110 F95 (scene 2 starts), 150 F115 (MQ103 stage 8), 160 F45 (**MQ103 stage 10, Balgruuf's relationship 1, the Jarl's reward armour
  `JarlRewardLight/HeavyArmor`, `SetStage(200)`**), 200 F107 (stop).
- **Scene MQ102DragonsreachScene** (SCEN 0x0D39C2, 10 phases): phase 0 "Wait for stage 80" (Proventus/Balgruuf/Irileth talk), phases 1 to 4
  Balgruuf, Proventus, Irileth lines, **phase 5 "Wait for Irileth to forcegreet or player to talk to Balgruuf"**: Irileth's package
  `MQ102IrilethForcegreetPlayer` (0x0D39C4, template ForceGreet), Balgruuf's `MQ102BalgruufForcegreetPlayer` (0x0D39C6, ForceGreetFromSitting).
- **Both forced greetings name no topic: their PDTO is type 1 with the subtype `HELO`** (raw bytes `01 00 00 00 'HELO'` read from the
  plugin; the earlier code knew only type 0 = a topic). The actor says its Hello. Irileth's and Balgruuf's Hello here is the start topic of a
  **Blocking branch** (DLBR `MQ102IrilethForcegreet` start `MQ102IrilethForcegreetTopic` "I need to speak to the Jarl...", DLBR `MQ102BalgruufIntro`
  start `MQ102BalgruufIntroTopic` "About the Helgen rumors...", both flagged Blocking: task 0298's mechanism). 32 packages in the five masters
  carry a HELO-subtype PDTO (Skyrim 17, Update 2, Dawnguard 3, HearthFires 1, Dragonborn 9), which is why the use is gated.
- **MQ103 "Bleak Falls Barrow"** (QUST 0x0D0800): stage 8 F80 (Irileth scene), 10 F69 (objective 10, `IntroScene` 0x0D50F6 with Farengar),
  50 F7/F88 (objective 50, Bleak Falls Barrow on the map), **180 F18 (objective 50 done, 80 "return to Farengar")**, 182 F81 (`OutroScene`),
  **190 F20 (the stone leaves the player, MQ104 stage 10)**, 200 F23 (done).
- **MQ104 "Dragon Rising"** (QUST 0x02610C, aliases dragon = reference 0x05A6CA `MQ104BDragonRef`, fort target 0x05A6D2, 32 aliases):
  10 F9 (Irileth to the captain's marker, `IntroScene` 0x058F93), 40 F17 (objective 5 done, fort on the map, courtyard scene, armour reward),
  45 F19, 48 F74 (fort intro scene), 60 F25 (the dragon enabled, forced landing marker), 65 F28 (the dragon is the player's enemy), **80 F31
  (objective 20 done, `DragonDeathScene`)**, **85 F33**, **90 F35 (`TeachWord(WordFus)`, `UnlockWord`, shouting on, `OutroScene`)**,
  150 F43 (MQ105 stage 10). The dragon alias script `MQ104DragonScript`: OnCombatStateChanged sets stage 65, **OnDeath sets stage 80 and
  registers an update that sets stage 85 when the dead dragon is within `DragonActorScript.deathFXrange` of the player**.

## 2. What was built (all behind `main quest whiterun`, except the aid)

1. **A ForceGreet package whose topic is the subtype `HELO`** (Irileth's `MQ102IrilethForcegreetPlayer`, Balgruuf's `MQ102BalgruufForcegreetPlayer`,
   Farengar's intro greeting) is a request to say the actor's greeting: `kForceGreetHelloTopic` (a pseudo id), the scene glue and the brain ask, the
   world view opens a plain `begin` (Blocking branches first; the setting turns `setBlockingBranches` on).
2. **A scene phase whose start conditions fail is skipped** (`SceneRunner::setSkipFailedPhases`): MQ102DragonsreachScene's phase 7 needs stage 82, which
   only the "Whatever you have to say to the Jarl" path sets; without the skip the scene never reaches Balgruuf's greeting (phase 8).
   **RECALLED-VERIFY: the CK's meaning of a start condition** (decision 1).
3. **An NPC_ whose template use flags include Script runs its template's scripts** (`ActorIndex::scriptSource`, used by `baseScriptsOf`): Mirmulnir
   (`MQ104Dragon`, template `lvlMQ104Dragon`) now carries `dragonActorSCRIPT` (OnDeath went to the alias script and an unrelated one only).
4. **The aid `story test quest` takes a delay per entry**, `<hex>:<stage>@<seconds>` (`mainquest::parseStoryTestQuests`), because the chain needs stages set later than
   the conversation before them.
5. A harness, `tools/skywhiterunmq_check` (a copy of the Thieves Guild check, started in `WhiterunDragonsreach`, with dragons, shouts and combat ai on).

## 3. In game (real mouse clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`, the five masters, player level 10; frames in `I:\Projects\OpenEden-work\mq308\keep`)

**Part 1, MQ102 "Before the Storm", from `story test quest = 04E50D:30,04E50D:75,04E50D:80`** (the Helgen opening is parked): the game's own scene
runs (Proventus, Balgruuf and Irileth, with voice and subtitles). **Irileth force-greets** ("What's the meaning of this interruption?", the real choices); the click on
"I have news from Helgen" runs `TIF__000D39AE`, her answer follows, and Goodbye sets stages 90 and 100. Phases 5 and 6 pass, phase 7 is skipped, **Balgruuf force-greets**
("So. You were at Helgen?..."; "The dragon destroyed Helgen..." runs `TIF__000D50E0`, "By Ysmir, Irileth was right!"), Goodbye sets 110, scene 2
(`MQ102DragonsreachScene2`, eleven phases) plays through, stage 150 starts MQ103 stage 8 (Irileth's scene), then **Balgruuf's reward greeting** ("Well done...",
"What else can I help you with?", "Here, take this as a small token of my esteem...", Goodbye) sets **stage 160: item `000CD9EE` (the Jarl's armour) goes to the player,
Balgruuf's relationship becomes 1, MQ103 stage 10, MQ102 stage 200 (stopped)**. The journal lists "Bleak Falls Barrow: Talk to Farengar", "Before the Storm" completed.
Frames: `irileth_greeting.png`, `irileth_reply.png`, `balgruuf_greeting.png`, `balgruuf_reward.png`, `balgruuf_token.png`, `journal_bleak_falls.png`.

**Part 2, MQ103 "Bleak Falls Barrow", from `story test quest = 0D0800:10,0D0800:15`:** stage 10 starts `MQ103IntroScene`; stage 15 (a stand-in for the lab trigger, gap 2)
lets it go on: Balgruuf says "Farengar, I think I've found someone...", stage 20, then **Farengar force-greets** ("So the Jarl thinks you can be of use to me?...", `MQ103FarengarIntroForcegreetTopic`).
"All right. Where am I going and what am I fetching?", then "So what do you need me to do?" (`TIF__000E0EBA`) and Goodbye set **stage 50** (Bleak Falls Barrow discovered on the map, objective shown).
The Dragonstone: `inventory test items = 000DF202x1` and `0D0800:180@60` (stage 180: objective 50 done, 80 "return to Farengar"; stand-ins for the dungeon, which tasks 0289, 0299 and 0302
already run). Farengar's greeting is then "Ah! The Dragonstone of Bleak Falls Barrow!..." (`TIF__000E0EB9`, the `MQ103FarengarIntroHaveStone` branch) with "So what about my reward?" and
"I got you the Dragonstone. What next?" (`farengar_dragonstone.png`). Those answers set no stage (gap 2), so the stand-ins `0D0800:182@50` and `0D0800:190@75` run the outro start and **stage 190
(the stone leaves the player, MQ104 stage 10)**: the journal shows **"Dragon Rising: A dragon has been sighted near Whiterun. Irileth, the Jarl's housecarl, asked me to come with her..."** with the
stone recovery as the earlier entry (`journal_dragon_rising.png`), and MQ104's `IntroScene` (29 phases) runs to phase 8 (Irileth: "Farengar, you need to come at once. A dragon's been sighted nearby.").

**Part 3, MQ104 "Dragon Rising", partial.** `story test quest = 02610C:60`, the player at Mirmulnir's place (`--start @Tamriel,7795,-37461,5800`, `combat test hostile = MQ104Dragon`):
the quest's alias dragon (reference `0x05A6CA`, base `MQ104Dragon`) is enabled by `Fragment_25`, **is recognised by the dragon system (health 1860, breath, swing, tail clips) and engages**. Killed by
`dragons test kill at`, **the alias script `MQ104DragonScript.OnDeath` sets MQ104 stage 80 (`Fragment_31`: objective 20 done, `DragonDeathScene` starts)**; `dragonActorSCRIPT.OnDeath` now runs (item 3),
enters its state `deadAndWaiting` and calls the game's `MQKillDragonScript` (`ShouldMiraakAppear`, four tolerant natives). The soul is given by **task 0253's hand-written absorption** ("souls 1", the message),
NOT by the game's script, and **stage 90 (`TeachWord(Fus)`, `UnlockWord`, the outro scene) is never reached** (gap 3).

## 4. Gaps, ranked

1. **Helgen** (parked by the order) and the cart; MQ102 starts with the aid.
2. **Box triggers and stages that stand in for them:** the lab trigger (MQ103 stage 15) and the outro trigger (stage 182) are volumes the player walks into; with `college` on, a teleport into the lab did not fire it
   (`ObjectReference.GetTriggerObjectCount` is a tolerant no-op, 134 calls in 10 s: a probable cause, not shown). Farengar's "What next?" and "reward" answers set no stage by themselves. The dungeon and the real pickup of the
   stone are not in the chain (stage 180 by the aid, the stone by `inventory test items`; the real pickup would raise the alias script's `OnContainerChanged`).
3. **The game's dragon death sequence does not finish:** after `ShouldMiraakAppear` no further native is called (no `AddItem`, `SetGhost`, `ModActorValue`). The prime suspect is the state's wait loop
   `while GetDistance(player) > deathFXrange`: `PapyrusGameData::referencePosition` answers the placement's first position (1,774 units up), not where the dragon fell; reading the brain's place instead did not help
   (tried, removed). Needs a live place for a dragon from the combat (`ActorRequestWriter::actorPosition`), `Actor.ModActorValue("DragonSouls")` into `ShoutState`, and task 0253's own absorption to step aside
   (it would give a second soul). Not built, not proved.
4. The messenger, Irileth's walk to the Western Watchtower, the fort, the dragon's arrival (`pf_mq104dragonpatrol`; `Actor.SetForcedLandingMarker` is a tolerant no-op) and "Dragonborn" from the guards
   (`MQ104OutroScene` after stage 90) are not reached.
5. Delphine (Riverwood) takes part in MQ104's intro scene; her line waited until the park time ran out.

## 5. Decisions and test edits for toddhow

1. A scene phase whose start conditions fail is skipped, only with `main quest whiterun` (RECALLED-VERIFY).
2. A HELO-subtype ForceGreet package (32 in the five masters: Skyrim 17, Update 2, Dawnguard 3, HearthFires 1, Dragonborn 9) greets only with the setting; Blocking branches speak instead of Hello with it (as `civil war`).
3. NPC_ template scripts are followed with the setting (level 30 for a leveled template, ours); the actor index is built when a cell loads.
4. **Existing tests edited: none.** New tests were added to two existing files (`testskyrimsescenes.cpp`, `testskyrimseactors.cpp`) and one new file (`testskyrimsemq308.cpp`).

## 6. Stage log

- **Red/green.** `SkyrimMq308Package` 3 tests: **1 of 3 failed against the stub** (the others pin absence and precedence); `SkyrimMq308StoryAid` 2 tests: **2 of 2 failed**; `SceneRunnerSkip` 2 tests: **1 of 2 failed** (the other pins
  the default); `SkyrimActorsScriptSource` 4 tests: **2 of 4 failed** (the others test absence and the cycle). Afterwards 46 tests of `SkyrimMq308*:SkyrimActorsScriptSource*:SceneRunner*:SkyrimDawnguard*:SkyrimCivilWar*:SkyrimBlocking*` pass.
  **The glue has no unit test** (the scene glue, the world view's begin hook, the setting read, `baseScriptsOf`'s template step): the game runs are its evidence. Same hand wrote tests and code, no fresh reviewer.
- Edits outside new files: `package.{hpp,cpp}`, `scene_runner.{hpp,cpp}`, `actors.{hpp,cpp}`, `world_view.{hpp,cpp}`, `main_quest.hpp`, `skyrimse_game_pack.cpp`, `extension.json`, `apps/components_tests/CMakeLists.txt`, the test profile comment and README.
- Rebased onto local `main` `b34600a4fd` (three keep-both conflicts). A run died silently once (exit 127, no dump, crash catcher off, memory was low); not reproduced.
- **Not run:** the Linux gate, GCC, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, the whole `components-tests` and `openmw-tests`, a fresh review, saves in the middle of the chain, the real dungeon and lab trigger.
