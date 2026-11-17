# Task 0264: Skyrim Companions, the start of the questline

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack); tasks 0137, 0187, 0258, 0261 (dialogue and INFO fragments), 0194 and
0211 (scenes, story manager, aliases), 0185 (journal), 0196 and 0239 (followers), 0190 and 0197 (combat AI), 0259 (werewolf).
Branch `companions` from `25cc1c8fe5`. Same hand wrote tests and code; the force-greet, natives and combat code came first and the
tests after it (see "Red evidence": four mutations, not stubs); no fresh reviewer; Windows only; local commits, not pushed.

Goal: ask Kodlak in Jorrvaskr to join the Companions and let the game's OWN quest C00 "Take Up Arms", its scenes, dialogue and
Papyrus scripts run as far as the engine allows, adding the missing natives and engine pieces instead of hand-written quest rules.
Behind the new pack setting `[extension.skyrimse] companions` (default false, declared in `extension.json`, a line in
`tools/testprofile/profile.cfg` and `README.md`) and, as a developer aid, `companions test training` (default false).

## 1. Survey (Skyrim.esm, the five masters' scripts decompiled with houseCARL from `Skyrim - Misc.bsa`; records read by an independent Python walker, `I:\Projects\OpenEden-work\combatai\comp\*.py`)

**The questline.** Quest ids and the names in the data: `C00` 0x4B2D9 "Take Up Arms", `C01` 0x6E803 (Dustman's Cairn with Farkas as
the observer, "Proving Honor"), `C02` 0x6E804 ("The Silver Hand"), `C03` 0x1CEF4, `C04` 0x1CEF5, `C05` 0x6E805, `C06` 0x1CEF6; helpers
`C00JorrvaskrFight` 0xBC0BD, `C00GiantAttack` 0xC97D9, `C00VilkasTrainingQuest` 0xA3EBC, `CompanionsRadiantQuest` and the CR00 to CR13
jobs. **The Beast Form power is not given by C01 or C02 as the task text assumed: `PlayerWerewolfQuest`'s change spell `BeastForm`
(0x92C48) is added by the script of quest 0x1CEF4 (its fragment script `QF_C01_0001CEF4`, the third Companions story quest) and by
`CompanionsHousekeepingScript.CurePlayer` takes it away; C02's own induction scene (`SF_C02InductionScene`) only enables the player
controls and sets C02 stage 20.** (Found by searching the extracted scripts for `BeastForm`: `c03rampagequest`,
`companionshousekeepingscript`, `qf_c01_0001cef4`, `playerwerewolfchangescript`, `werewolfchangeeffectscript`, two DLC1 scripts.)

**C00 "Take Up Arms"** (start-game-enabled, flags 0x0011, priority 70; stages 0, 1, 10, 20, 30, 40, 50, 100, 150; objectives 10
"Speak to Kodlak Whitemane", 20 "Train with Vilkas", 30 "Give Vilkas's sword to Eorlund", 40 "Bring Aela her shield", 50 "Follow Farkas to
your quarters"; 50 aliases: Kodlak 7, Aela 0, Farkas 1, Vilkas 2, Skjor 3, Eorlund 8, the six essential flags, VilkasSword, AelaShield and
quest items, three location reservations, CurrentFollower 40, PlayerWatched 39 ...). The quest script is `CompanionsHousekeepingScript`
(25 functions: `SetUpCompanions`, `PlayerJoin`, `SwapFollowers`, the radiant job bookkeeping). The fragments (`QF_C00_0004B2D9`): stage 0
`SetupCompanions`; 20 starts objective 20, stops `C00GiantAttack` and puts the player and Vilkas in the training faction; 30 gives the
objective for Eorlund, 40 moves Aela and Skjor, 50 Farkas' tour, 100 `PlayerJoin` (adds `CompanionsFaction`, starts the radiant misc
objective quest, trainers' factions, records the original race), 150 cleans up.

**The chain from the data, in order.**

1. `DIAL C00KodlakJoinUpStartTopic` 0xA3E56, INFO 0xA3E7A "I would like to join the Companions." Conditions: not in `CompanionsFaction`,
   speaker is Kodlak (`GetIsID` 0x1A68E), `GetStage(C00) <= 10`. Its fragment `TIF__000A3E7A` starts `KVArgueScene`.
2. Scene `SF_C00KodlakVilkasArgueAboutP` 0xA3EB7: 6 phases, 9 actions (Vilkas and Kodlak argue, five lines), begin fragment forces the
   player into the `PlayerWatched` alias. **Phase 5 gives Kodlak a ForceGreet package** `C00KodlakForceGreetPlayer` 0xA3EC1 (PKDT type 18,
   a package built from a template, `PDTO` topic 0xA3E50 `C00KodlakBackToThePlayerTopic`) and the scene ends when the package does.
3. The forced greeting is the first passing INFO of that topic ("Of course. How are you in a battle, boy/girl?", INFO 0xCC83F or 0xA3E73),
   whose TCLT links are the player's three choices: "I have much to learn." (0xA3E9A, `TIF__000A3E9A`: `VilkasDismissScene.Start()`),
   "You dare question my skill?" (0xA3E65) and "I can handle myself." (0xA3E68), both with the same dismiss scene.
4. Scene `SF_C00KodlakDismissVilkas` 0xBD697 (4 phases, 6 actions): Kodlak "Vilkas, take him out to the yard and see what he can do.",
   Vilkas "Aye.", **phase 1's fragment sets C00 stage 20** (`GetOwningQuest().SetStage(20)`), Vilkas gets a travel package (0xF7957) and
   in phase 3 a ForceGreet package (0xA3EB9, topic 0xA3E4C `C00VilkasTrainPlayerTopic`); its end fragment sets stage 20 if it is not set
   and `PlayerIgnoredVilkasOutside`.
5. Vilkas' topic "So you're supposed to train me?" has two INFOs: 0xA3E98 "Not here. Out in the yard. Come on." when the speaker is
   more than 1,300 units from the training marker (REFR 0x23E55, WhiterunWorld around (29665, -4057, -2734)), and 0xA3E99 "Just have a
   few swings at me ..." otherwise, whose fragment (`TIF__000A3E99`) sets stage 20 if not done and calls `TrainingQuest.Start()`.
6. `C00VilkasTrainingQuest` 0xA3EBC (not start-game-enabled): stage 10 -> `StartTheFight` (`BlockActivation`, `ResetHits`, `EvaluatePackage`,
   `GetAV("confidence")`, `SetAV("confidence", 4)`, `AddToFaction(HateFaction)`, `Vilkas.StartCombat(player)`); the alias script
   `C00TrainerScript` on Vilkas: `OnHit` by the player counts, **3 hits -> stage 150**, a hit by anyone else -> 110, `OnEnterBleedout` or
   `OnMagicEffectApply` -> 125 or 100; stages 100, 110, 125, 150 call `StopTheFight` (`StopCombat`, confidence back,
   `ResetHealthAndLimbs`, forced greet again); stage 200 -> `PassTheSword`, `ParentQuest.SetStage(30)`, `Stop()`.
7. C00 stage 30: Eorlund (Skyforge, the sword; `C00Eorlund*` topics), 40: Aela's shield (`C00AelaIhaveYourShieldTopic`, the ASScene), 50:
   Farkas shows the quarters (`LastFarkasTourScene`, `C00FarkasNarration` scenes), 100: `PlayerJoin`. Parallel quests: `C00JorrvaskrFight`
   (a crowd fight in Jorrvaskr), `C00GiantAttack`, location monitors.
8. C01 starts from the radiant system (`CycleRadiantQuests`, `Kicker`): Farkas becomes `Observer`, `SetPlayerTeammate`, `SwapFollowers`,
   `CurrentFollower.ForceRefTo`, `Shutup`; the dungeon ambush (`C01AmbusherScript`, `C01AmbushStartLever`, `C01MacGuffinScript`: a
   Wuuthrad fragment), `CompanionsBladeFragmentTracking`. C02 `Init` moves the Companions to their start markers (`MoveTo`) and starts the
   ceremony scene.

## 2. What this slice needed from the engine (ranked), and what was done

Ranked by what blocked the chain, from running it (the first run of the join line and its log):

| # | Gap | Found | Done |
| --- | --- | --- | --- |
| 1 | `Scene.Start` fine, but **a ForceGreet package ended at once and never opened a conversation** (the package brain only stands), so the player's three choices never appeared | the log of the first scene run: `SCENE ends` 1.5 s after the package | **built**: `ForceGreet` (pure bookkeeping + bindings `skyrimse.forcegreet` 1.0) + `forcegreet_global.lua` activate the actor by the player + `DialogueRuntime::beginForcedTopic` (the first INFO of the package's PDTO topic, links as choices) + `PackageRecord::mForceGreetTopic` (PDTO decoded) |
| 2 | **`Scene.GetOwningQuest` was declared and not registered**, so every scene fragment that sets a quest stage (`GetOwningQuest().SetStage(20)`) did nothing | `PAPYRUS native 'Scene.GetOwningQuest' is declared and not registered` | **built**: `PapyrusGameData::questOfScene` (additive virtual) and the native |
| 3 | **`(Vilkas as c00trainerscript).ResetHits()`: a cast of a quest ALIAS to its alias script returned None** (an alias handle is a form value; alias scripts are keyed by quest and alias) | `call of 'ResetHits' on None` | **built**: `PapyrusRuntime::setAliasResolver` (additive, default no-op) used by `siblingScript` |
| 4 | `Actor.StartCombat`, `StopCombat`, `GetCombatState`, `ResetHealthAndLimbs`, `GetAV` (and `EvaluatePackage` as a quiet no-op) missing | decompiled `C00VilkasTrainingScript` | **built**: four additive `PapyrusWorldHooks` virtuals, `SkyrimCombat::scriptStartCombat/StopCombat/ResetHealth/InCombat` (a fight against the player only) |
| 5 | An **essential** actor dies when a blow kills it, so the training fight can kill Vilkas; the scripts expect `OnEnterBleedout` | the alias `VilkasEssential` and `OnEnterBleedout` in `C00TrainerScript` | **built**: `SkyrimCombat::setEssentialBleedout` (an essential actor a blow would kill keeps 1 health, no death report, one bleedout sink call -> `PapyrusHost::onActorBleedout`) |
| 6 | **NPCs do not walk through doors and the exterior yard is a different cell**: the training marker 0x23E55 is in WhiterunWorld, Vilkas starts in the basement, so 0xA3E98 "Not here" is all the player hears and 0xA3E99 never passes | Vilkas' forced greeting says "Not here. Out in the yard." | **stubbed**: the test aid `companions test training` starts `C00VilkasTrainingQuest` when the dismiss scene has set stage 20 and ended. NOT built: doors for walking NPCs, the exterior walk |
| 7 | `ObjectReference.GetParentCell`, `GetWorldSpace`, `Actor.IsEquipped`, `Form.Find` (tolerant, from `CompanionsBladeFragmentTracking.Setup`: "the array is None") | the log's tolerant list | not built |
| 8 | `BlockActivation`, `SetNoFavorAllowed`, `AddToMap`: tolerant no-ops (an NPC blocked from activation can still be talked to) | tolerant list | not built |
| 9 | C01: Farkas as the observer and follower (`SwapFollowers`, `CurrentFollower` alias scripts, `SetPlayerTeammate` exist from 0239); the dungeon ambush trigger volumes and levers; Wuuthrad fragment items | survey only | not run |
| 10 | The werewolf gift: not C02 but quest 0x1CEF4 (`BeastForm` is added by its script; 0259's `werewolf test grant` stays) | survey | not run |

## 3. Implementation

- `game_packs/skyrimse/{include/game_skyrimse,src}/forcegreet.*` (new): `ForceGreet` (request, `topicFor`, `began`, `ended`, `finished`
  with a deterministic timeout of 900 checks (the scene runner checks once per update: no clock), `pendingBase`, 16 requests) and the
  Read-only namespace `skyrimse.forcegreet` 1.0 (`pending_base`, `pending_serial`). `files/data/scripts/openeden/skyrimse/forcegreet_global.lua`
  finds the actor by `<game>:b<base>` in the player's cell and activates it (the crime script's pattern, task 0258).
- `SceneGlue` (in `skyrimse_game_pack.cpp`): `setPackage` also asks `ForceGreet` when the package has a PDTO topic and a ForceGreet procedure
  (its own list, else its template's: the real package has none of its own); `packageFinished` is the conversation's end; `clearPackage`
  cancels. `SkyrimWorldView::beginConversation` opens the forced topic through `ForcedGreetHooks`.
- `combat_companions.cpp` and the essential/bleedout lines in `combat.cpp` (melee path only: a spell kill still kills an essential actor).
- `papyrus_host.cpp`: the natives of gap 2, 3 and 4, `onActorBleedout`; `components/script_papyrus`: `setAliasResolver`.
- `skyrimse_game_pack.cpp`: the glue, `companions` and `companions test training` (declared in `extension.json`).
- `tools/skycompanions_check/` (a copy of `tools/skyhouses_check`): `luatest/scripts/cmp/global.lua` plans `talk` (nearest actor of a base),
  `go`, `face base until`, `ids`, `trace`, `save`.

All additions to shared interfaces are additive (a trailing virtual with a refusing default: `PapyrusWorldHooks`, `PapyrusGameData`,
`PapyrusRuntime`); no `CombatRequestWriter` virtual was added; no pinned version moved.

## 4. Red evidence and tests

`apps/components_tests/skyrimse/testskyrimsecompanions.cpp`, 30 tests in five suites (`SkyrimCompanionsForceGreet` 9, `SkyrimCompanionsPackage`
3, `SkyrimCompanionsDialogue` 4, `SkyrimCompanionsNatives` 8, `CompanionsCombat` 7 with the fixture `CompanionsCombat`); no existing test was
edited. **The code was written before the tests, so they were not run red against stubs; four one-line mutations were run instead** (the
timeout bound + 5, the essential collapse `false &&`, the PDTO type compared with 1, a reset that leaves one health missing): 4 tests
failed (`aRequestThatNeverOpensTimesOutAfterAFixedNumberOfChecks`, `theFirstTopicOfAPackagesDataIsItsForceGreetTopic`,
`anEssentialActorThatABlowWouldKillCollapsesInsteadAndTellsTheSinkOnce`, `resettingHealthHealsAnActorAndEndsItsBleedout`), 26 passed; all
reverted. The area filter (`*Skyrim*:*Dialogue*:*Papyrus*:*Scene*:*Package*:*Combat*:*Extension*:*Neutral*:*Frozen*:*Journal*) before the last two
changes: 4,188 pass, 0 fail (see section 5 for the final run).

## 5. In game

Scenario: `tools/skycompanions_check/check.sh` (Jorrvaskr basement, `companions = true`, `companions test training = true`, real clicks through the RmlUi input script, sound 0, quiet window).

- Asking Kodlak to join runs the game's own C00 start: the argue scene (SF_C00KodlakVilkasArgueAboutP, 0xA3EB7) plays, Vilkas forced-greets through the global script, the choice topics are the INFO's link-to topics, the dismiss scene runs, C00 reaches stage 20 and the journal lists it (frames viewed).
- Sparring: the training quest 0xA3EBC is started by a TEST AID (`companions test training`) once the scene has ended, because NPCs walking through the doors to the yard are not built. The fight itself uses the game's StartCombat/StopCombat natives and combat AI; essential actors enter bleedout, which advances the training quest to stage 150.
- Determinism: three runs give identical quest/stage/forcegreet lines (the conversation box pauses the world, so frame numbers drift; compared by line content).
- Save/load in a new process: the Papyrus chunk is accepted (901 instances restored, no PAPYRUSLOAD refusal; before the `attachKeyExists` scene fix it was refused whole after any scene fragment ran); a regression test is red without the fix.
- Morrowind unchanged: the two fixed Balmora frames are byte-identical to the reference build (`1eb68236352dc472`, `c819db116f4af8a6`).
- Tests: area filter 4193 pass; 35 new tests, no existing test edited.

Not built / stubbed: package-driven forced greeting after the fight (VilkasShouldForceGreet), stage 200 and C00 stage 30 ("How was that?" not clicked), walking to the exterior yard, C01 (Farkas, Dustman's Cairn), C02, and the Beast Form ritual (quest 0x1CEF4). Not run: the Linux gate, clang-format 14, clang-tidy, Lua tests, a fresh review.
