# Task: Skyrim followers through the game's own dialogue and commands (task 0239)

Status: ordered by the coordinator on 2026-11-16 as the follow-up of task 0196 (same hand wrote tests and code; tests first,
run against stubs, red counts in the stage log; no fresh reviewer). Windows only. Local commits on branch `followcmd`
from `0f0d72bbaa` (local `main`), not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7); decisions 0013 (game packs), 0016 (neutral
world), 0017 (neutral actors); task 0196 (followers), 0187 (INFO fragments), 0219 (the "carry" window), 0197 (NPC combat).
Goal: a follower is recruited, told to wait, told to follow, traded with and dismissed through the game's OWN dialogue
lines and their Papyrus fragments (`DialogueFollowerScript`, `HirelingQuest`, the `TIF__` scripts), and fights at the
player's side through the combat AI.
Inherited code touched (outside the new files; every edit is listed in the stage log): `components/script_papyrus`
(`Runtime::castTo`), `game_packs/skyrimse` (`papyrus_host.*`, `dialogue.*`, `dialogue_runtime.hpp`, `world_view.*`,
`quest.*`, `package_brain.*`, `actor_table.*`, `skyrimse_game_pack.cpp`).
Constraints: behind the existing `[extension.skyrimse] papyrus` and `actor packages` (and `services` for the trade window);
no new setting; Morrowind unchanged.
Out of scope: "I need you to do something" (the favour menu: `DialogueFollowerFavorStateTopic` and its Show/Agree/Refuse
branches), animals, dismissal after 72 game hours of waiting (`RegisterForUpdateGameTime` stays tolerant), follower
messages (`Message.Show` stays tolerant), several followers at once beyond the one the game allows, housecarls (see 1.4).

## 1. Survey (read at `0f0d72bbaa`; scripts decompiled with houseCARL from `Skyrim - Misc.bsa`, records read by an independent Python walker)

1. **The topics.** Quest `DialogueFollower` (0x750BA, flags 0x19 = start game enabled) owns the DIALs `DialogueFollowerFollowTopic`
   ("Follow me." 0x75083), `...WaitTopic` ("Wait here." 0x75084), `...DismissTopic` ("It's time for us to part ways." 0x5C80C)
   and `...TradeTopic` ("I need to trade some things with you." 0x60020), 21 INFOs each (one per potential follower,
   plus generic ones by voice type). The conditions of a Follow line: `GetInFaction(CurrentFollowerFaction 0x5C84E) == 1`,
   `GetIsID(<the actor>)` and `GetActorValue(0x5F) == 1`; of a Wait line the same with `== 0`; Dismiss and Trade have the
   first two. Index 0x5F is `WaitingForPlayer` (the only actor value those 84 INFOs name).
2. **The fragments.** Follow: `(GetOwningQuest() as DialogueFollowerScript).FollowerFollow()` (`SetAV("WaitingForPlayer", 0)`);
   Wait: `FollowerWait()` (`SetAV("WaitingForPlayer", 1)`, `RegisterForUpdateGameTime(72)` on the alias); Trade:
   `akSpeaker.OpenInventory(false)` (task 0219 already turns that into the carry window); Dismiss: `DismissFollower(0, 0)`
   (message, `AddToFaction(DismissedFollower)`, `SetPlayerTeammate(false)`, `RemoveFromFaction(CurrentHireling)`, `SetAV(..., 0)`,
   `RemoveItem` of the borrowed bow and arrows, `HirelingRehireScript.DismissHireling`, `pFollowerAlias.Clear()`).
   Recruit: a generic line `(pDialogueFollower as DialogueFollowerScript).SetFollower(akSpeaker)` (`RemoveFromFaction`,
   `GetRelationshipRank`/`SetRelationshipRank(player, 3)`, `SetPlayerTeammate(true, true)`, `pFollowerAlias.ForceRefTo`,
   `pPlayerFollowerCount.SetValue(1)`) or a hireling line `HirelingQuest.PayHireling` (`RemoveItem(Gold, HirelingGold)`,
   `AddToFaction(CurrentHireling)`, then the same `SetFollower`).
3. **The alias adds the faction.** The `Follower` alias (id 0) of the quest has three `ALFC` subrecords: CurrentFollowerFaction
   (0x5C84E), PlayerFollowerFaction (0x84D1B) and 0x750B8: a filled alias puts its actor in them (the CurrentFollowerFaction
   the four topics test). No script adds it: the alias does.
4. **Who can be recruited in a fresh session.** The generic recruit INFOs (DIAL `DialogueFavorGenericFollowBranchTopic`,
   quest 0x5A6DC) need `GetInFaction(PotentialFollowerFaction)`, not in CurrentFollowerFaction / PotentialHireling,
   `PlayerFollowerCount == 0`, a voice type and `GetRelationshipRank >= 1` with the player (parameter 0: the player). Of
   the 43 potential followers in the five masters only `HousecarlWhiterun` (Lydia, Breezehome, quest-locked until the house
   is bought) has a RELA record with the player (rank index 1, "Ally"); Uthgerd (after the brawl), the Companions in
   Jorrvaskr (after joining), Faendal and Sven (after their quests) set the rank by script. **No generic candidate is
   reachable in a fresh session.** The **hirelings** are: `HirelingJenassa` stands in the Drunken Huntsman (cell
   `WhiterunDrunkenHuntsman`, placement 0xE1BA9, base 0xB9982 = `skyrimse:b760194`); her line "Consider yourself hired.
   (500)" (INFO 0x1A1CD, quest HirelingQuest 0xBCC94) needs gold of at least the global `HirelingGold`, not in
   CurrentHireling, in PotentialHireling, `PlayerFollowerCount == 0`. Marcurio (Riften), Stenvar (Windhelm), Belrand
   (Solitude), Vorstag (Markarth) and Erik (Rorikstead) work the same way. The recruit run in game below is Jenassa.
5. **A bug found: a faction listed with rank -1 counted as a membership.** Hirelings, Njada, Uthgerd and the other
   potential followers carry `CurrentFollowerFaction` with a stored rank byte of 255 (-1) in their NPC_ records
   (`HirelingJenassa`: CurrentFollowerFaction -1, CurrentHireling -1, PotentialFollowerFaction 0, PotentialHireling 0). That
   means "not a member" (GetInFaction needs rank 0 or more). `SkyrimConditionWorld::inFaction` counted it, so before
   this task every potential follower showed the Wait, Dismiss and Trade topics and "Yes, sera?" greeting of a follower
   (seen in game: frame `r0`). Fixed here with a test (red first).
6. **A runtime gap found: a cast to another script of the same form gave None.** `DialogueFollower` carries three scripts
   in `Update.esm` (`QF_DialogueFollower_000750BA`, `HirelingCommentScript`, `DialogueFollowerScript`); the host answers a
   quest with one of its instances, and `Runtime::castTo` only looked at that instance's class chain, so every
   `(GetOwningQuest() as DialogueFollowerScript)` and `(pDialogueFollower as DialogueFollowerScript)` was None ("call of
   'SetFollower' on None" in game). Now a cast looks at the other instances attached to the same form (and alias).
7. **The Papyrus host had no answer for the follower natives.** `Actor.AddToFaction`, `RemoveFromFaction`, `IsInFaction`
   (a stub that said false), `SetPlayerTeammate`, `SetRelationshipRank`, `GetRelationshipRank`, `SetAV`, `GetActorBase`,
   `ReferenceAlias.GetActorRef`, and the alias fill's factions were missing or tolerant no-ops; a global a script set
   (`PlayerFollowerCount`) never reached the dialogue conditions (they read the data's value).
8. **The conversation box pauses the world**, so the fragments run (and the follower walks) only after Goodbye, as the
   other dialogue services do.

## 2. Design

- `follower_state.*` (new): `FollowerState` keeps what the scripts changed: factions an actor was added to or removed from
  (-1), relationship ranks, the teammate flag, the globals scripts set; it reads WaitingForPlayer from the host's actor
  value offsets (the store `SetAV` and `SetActorValue` write, which the Papyrus chunk already saves). `FollowerOverlay` is
  what the conditions read; `FollowerStateChunk` is the save chunk `followerstate` v1 (strict, capped, ascending).
- `SkyrimConditionWorld::setFollowerOverlay`: `GetInFaction`, `GetFactionRank`, `GetGlobalValue`, `GetActorValue` (for index
  0x5F), `GetRelationshipRank` (parameter 0 is the player) and `GetPlayerTeammate` ask the overlay first and the data second.
- `PapyrusHost::setFollowerState` and the natives of 1.7; the alias fill: `PapyrusGameData::aliasFactions` (from the new
  `QuestAlias::mFactions`, ALFC) adds the alias's factions to the actor `ForceRefTo` puts in and removes them on `Clear`.
- `PackageBrain::setFollowerWaiting` (and `SkyrimActorTable`): a waiting follower cancels its goal and issues none; the game
  pack's per-frame sync reads `FollowerState::waiting` for every follower. Dismissal forgets the wait.
- `components/script_papyrus` `Runtime::siblingScript`: the cast of 1.6.

## 3. Stages

1. Pure parts and natives with tests first, red against stubs.
2. The in-game recruit of Jenassa by her own line, wait, walk away and back, follow, trade, dismiss, save and load while
   waiting, a fight.
3. Morrowind unchanged.

## 4. Stage log

**Order of work.** Same hand wrote tests and code. Tests first, run against stubs of the new functions: the first batch of 33
(`SkyrimFollowerCmd*`, `apps/components_tests/skyrimse/testskyrimsefollowercommands.cpp`) ran with **26 failed and 7 passed** (the
7 pin absence or refusal: an untouched actor has no opinion, a zero id is refused, a damaged chunk is refused, a refusing setter
refuses, a dismissed follower forgets the wait, a non-follower's wait changes nothing, the natives are tolerant without a state;
`I:\Projects\OpenEden-work\bow6\red_cmd.txt`). Five more tests were added later, each shown red against the old code first: the
rank -1 membership (the condition said true), the cast to a sibling script (`hits` stayed 0), the carry list leaving out a waiting
follower, `SetFactionRank`/`GetFactionRank`, and the restore of an INFO's fragment instance (`restoreState` false). **31 of 38 red
first, 7 pin absence.** Existing tests edited: none (no pin test was affected: the Skyrim, Papyrus, dialogue, condition, quest,
extension and frozen groups, 2,795 tests, pass).

**What the real data and the game showed (they changed the design three times).** (1) Without any recruiting the follower topics
and the greeting "Yes, sera?" were already offered by Jenassa (frame `a_bug_rank_minus_one_topics.png`): the faction rank -1 bug
of 1.5. (2) After the hire line (frame `b_hire_line.png`, "Consider yourself hired. (500)") the fragment ran and `SetFollower`
was a call on None: the cast of 1.6. (3) With that fixed the follow still did not happen, because **`Actor.AddToFaction` is a
script function in the game's `Actor.pex`** (`IsInFaction`, then `SetFactionRank(f, 0)`, which was a tolerant no-op), so
`CurrentHireling` and `CurrentFollowerFaction` were never set: `SetFactionRank` and `GetFactionRank` are the natives that matter
(found with `OPENEDEN_PAPYRUS_NATIVE_TRACE=1`, not by reading `AddToFaction`). (4) The save made after a dialogue line was refused as a
whole on load ("instance 880 (form 1A1CD, alias 0): the game no longer has what its attach key names"): the Papyrus chunk (alias fill,
WaitingForPlayer) was lost; `attachKeyExists` now accepts an INFO. Side effect of (2): `CompanionsBladeFragmentTracking.Setup` (a
quest start-up script) now runs one step further and ends in `the array is None` (array properties are not supported): **+1 script
error in a fresh session** (summary "script errors 1", two log lines), where before the cast gave None and the script did nothing.

**In game** (`tools/skyfollow_check/check.sh`, RTX 4070, `OPENEDEN_FIXED_FRAME_TIME`, `[Game] neutral combat/inventory/actors/idles/saves`,
`[extension.skyrimse] papyrus, actor packages, services, inventory`, `player gold = 1500`, `[GUI] rmlui`; every click is a real
mouse event through `OPENEDEN_RMLUI_INPUT_SCRIPT`; frames in `I:\Projects\OpenEden-work\bow6\evidence`). Start in the Drunken Huntsman
(Jenassa, `skyrimse:b760194`, placement 924,585, 573 units away):

| update | what | result |
| --- | --- | --- |
| 3000 | the player activates her; the box opens | greeting "Blade and shadow, silence and death..." and the line "Consider yourself hired. (500)" (it needs gold of at least `HirelingGold`, not in CurrentHireling, `PlayerFollowerCount == 0`) |
| 3310 | click "Consider yourself hired. (500)" (frame `c_hire_response.png`); Goodbye | log: `PAPYRUS dialogue line 0001A1CD runs TIF__0001A1CD.Fragment_0`, then `follower recruited: placement 924585`; the carry window later shows gold x1000 (1,500 less the 500 `PayHireling` removed) |
| 3600 | the player uses the Huntsman's door to Whiterun | `Neutral follower 924585 carried`; she stands 130 units from him in Whiterun's street (`22317,-6892,-3348`) |
| 4400 | talk, click "Wait here." (frame `d_after_hire_topics_in_whiterun.png` shows the follower topics, no hire line) | `TIF__0007A51B.Fragment_0` (Wait); "As you wish." |
| 4700 | the player is moved 313 units away (306 from her) | **she stays**: 306 units at update 5400 (a waiting follower is not carried and walks nowhere) |
| 5400 | talk; the list now offers "Follow me." instead of "Wait here." (frame `e_follow_me_offered_while_waiting.png`); click it | `TIF__0007A517`; "I am the shadow at your back." (`f_follow_me_response.png`); she walks `22317,-6892` to `22384,-6842` and stops at 223 units (inside the 260) |
| 6200 | talk, click "I need to trade some things with you." | `TIF__0007A513`; `NINV service carry 000e1ba9 base 000b9982 'Jenassa' opened`; the carry window with her five stacks beside the player's gold x1000 (`g_carry_window_jenassa.png`); Take of the Iron Sword moves it to the player (`g2_carry_window_after_take.png`); Close |
| 7000 | talk, click "It's time for us to part ways." | `TIF__0007A50E` (`DismissFollower(0, 0)`), "It is a lonely thing, facing all the dangers of Skyrim by yourself..." (`j_dismiss_response.png`); `follower dismissed: placement 924585` |
| 7600 | the player is moved 420 units away | she stays where she is (286 away at update 8800) |

**Three runs** of that whole scenario: the follower's 49 lines (the `FCMD` positions, the recruit, dismiss and carry lines, the
dialogue fragment lines and her `NACTGOTO` goals) are **byte-identical** (md5 `7f26c26a111aa12f3718a7737362dfbe` for all three,
`bow6\fc\fin?.fl`). The other actors of the street differ between runs in their `NACTGOTO` results (their order depends on the
worker threads: seen before, not caused here).

**Save and load while waiting** (`PHASE=run` then `PHASE=load`): the player outside with her waiting, saved by the menu
script (`fcmd.omwsave`); a new process loads it: `PAPYRUSLOAD restored: 986 script instances` (before the fix it was refused), 0 error
lines, she is carried beside the player, the player is moved 313 units away and she **stays** (306 units: the wait was restored from the Papyrus
chunk), the box offers "Follow me." (frame `h_after_load_follow_me_offered.png`), the click takes her to 222 units. The faction
state came back from the `followerstate` chunk (the CurrentFollowerFaction topics pass).

**Fighting at the player's side** (the 0197 combat AI, `combat ai`, `combat factions`, `combat real gear`; `combat test hostile =
Elrindir` makes him attack the player at once; the player moved to `-100,-1000` inside the Huntsman, Jenassa carried beside him):
`COMBAT ai engage hirelingjenassa actor924585/5 (on sight) ... target actor108161/2` at 50.999, then her blows (7.98 and
15.96 power attacks before armour, her Iron Sword), then `COMBAT death actor108161/2` and `COMBAT ai lost hirelingjenassa ... (target
dead)` (frame `i_fight_jenassa_elrindir.png`). Elrindir had already hit the player six times (9.675 each) before she joined: she
engages when her look at the enemy happens, not at once. `combat test attacker` was tried first and attacks the nearest NPC
(Anoriath), never the player: use `combat test hostile`. This fight run was made with the build before the last formatting edits
and the INFO restore fix (same combat code).

**Morrowind unchanged** (against the `combat5` build, the nearest other build of this tree, not the exact parent):
`tools/worldobjects_check` 110 objects OK; the fixed Balmora frame (`--start Balmora --random-seed 7`, frame 600) SHA-256 prefix
`1c5d47ad0b5dc32d` for the reference and twice for this build.

**Tests:** `components-tests` (without the Slang, terrain complement and property groups) 12,744 ran, 12,637 passed, the rest
skipped (data-gated); `openmw-tests` 2,906 ran, 2,904 passed, 2 skipped (data-gated). `CI/check_tests_registered.sh` ok.

**Edits outside the new files** (`follower_state.*`, `testskyrimsefollowercommands.cpp`, `tools/skyfollow_check`):
`components/script_papyrus/papyrusvm.hpp` and `papyrusinterp.cpp` (`siblingScript`, one branch in `castTo`);
`game_packs/skyrimse/CMakeLists.txt` (the new `.cpp` in both lists) and `apps/components_tests/CMakeLists.txt`;
`papyrus_host.hpp/.cpp` (`aliasFactions` on `PapyrusGameData`, `setFollowerState`, `aliasFactionChange`, the natives of 1.7 in
one block `registerFollowerNatives`, the factions in `ForceRefTo`/`Clear`, `GetActorRef`, `GlobalVariable.SetValue/GetValue` through
the state, the `IsInFaction` stub removed, `attachKeyExists` for an INFO); `quest.hpp/.cpp` (`QuestAlias::mFactions`, ALFC);
`dialogue.hpp/.cpp` (`setFollowerOverlay`, the overlay in six condition functions, `isPlayerTeammate`, **the rank >= 0 fix in
`inFaction`**); `dialogue_runtime.hpp` and `world_view.hpp/.cpp` (the overlay forwarded); `package_brain.hpp/.cpp`
(`setFollowerWaiting`, one early branch of `runFollowPlayer`, a dismissal erasing the wait); `actor_table.hpp/.cpp`
(`setFollowerWaiting`, `followerWaiting`, a waiting follower left out of `followers()`); `skyrimse_game_pack.cpp` (the state, its
chunk, the overlay, the host wiring, the per-frame sync, the recruit and dismiss log lines, the alias factions in the adapter).
No SDK header changed, no virtual was added to `CombatRequestWriter`.

**Not run:** the Linux gate and GCC `-Werror` (traps checked by hand), clang-format 14 (formatted by hand to 120 columns),
clang-tidy, the Lua integration tests, a fresh review, mutation checks, a generic recruit by dialogue (no candidate is reachable in
a fresh session, see 1.4: it is unit tested through the natives only), a follower whose home is an exterior, two followers, animals,
"I need you to do something" (out of scope), the dismissal after 72 hours of waiting.

**Decisions for toddhow**
1. **`GetInFaction` now needs rank 0 or more** (a data-wide behaviour change of the dialogue conditions: every potential
   follower, hireling and Companion listed with rank -1 was "in" the faction before). I think this is right; the effect on
   other topics was not surveyed beyond the test groups.
2. **`Runtime::castTo` finds a sibling script of the same form.** It is how Papyrus behaves and is needed here, but it runs more
   of the start-up scripts, and one now ends in a script error on an array property (+1 script error per fresh session).
3. **`GetRelationshipRank` with parameter 0 is the player** in the overlay branch only (the generic recruit lines test it); the
   data branch still treats 0 as "no target" (answer 0), so Lydia and the other generic recruits stay unreachable in a fresh
   session. Changing the data branch changes every INFO that tests a rank.
4. **A waiting follower is not carried through a door** and keeps the player's side; Skyrim leaves it where it is.
5. **A follower standing on a spot the navmesh does not connect** (Jenassa on the Huntsman's raised corner, z 89.8): her follow
   goal ends with `no path` until a door or teleport carries her; the walk is shown in Whiterun's street.
6. **Messages are not shown** (`Message.Show` stays tolerant), **the 72-hour wait dismissal is not built**, and **`FollowerState`
   keeps the globals scripts set** (`PlayerFollowerCount`, `CanRehire*`) in its own chunk, so a load keeps them (the Papyrus
   chunk does not save globals).

**Second pass (checks added after a review of the first commit).**

- **Data-gated tests run.** The first test run had no `OPENEDEN_SKYRIMSE_DATA`, so its 107 skipped tests were exactly the ones that pin
  real numbers. With the Data folder set, the real-data groups that touch what this task changed pass: `SkyrimDialogueRealData`,
  `SkyrimDialogueRuntimeRealData`, `SkyrimConditionStateRealData`, `SkyrimPlacedRefsRealData`, `SkyrimPackagesRealData`,
  `SkyrimQuestRealData`, `SkyrimQuestScriptsRealData`, `SkyrimSeConditionsReal`, `SkyrimBountyReal`, `SkyrimBarksRealData`,
  `SkyrimRadiantReal`, `SkyrimRadiant2Real` (75 real-data tests before the run was ended; none failed; the run was stopped at the
  tree and cell suites, which this task does not touch), and with `OPENEDEN_PAPYRUS_DATA` set `PapyrusRealData.every_installed_script_parses`
  and `PapyrusRealRun` (a sample of the game's own scripts runs and survives a save and restore; the cast change is in that path). No test needs an edit.
- **The save-while-waiting evidence did not separate "stayed" from "was carried".** A second pair of runs saves AFTER the player was moved
  away (306 units from her, waiting, `fc/s3`): after the load `Neutral follower 924585 carried` puts her 130 units from the player
  (`22564,-6581`), not at her old place (`22317,-6892`). **What is saved is that she is a follower and that she waits (the Papyrus
  chunk and `followerstate`); where she stood is not** (task 0196 decision 3: the neutral save rebuilds actors from the pack). The
  first load run's "she stays at 306 units" is only the second half of it: after the load she waits where the carry put her and the
  player moves away from there.
- **The fight now has a control.** The same scenario without the hire (no conversation, `combat test hostile = Elrindir`, the
  player at `-100,-1000`, Jenassa on her perch): Jenassa never engages, Elrindir kills the player (`COMBAT death player` after 10
  hits), 0 `engage hirelingjenassa` lines. With the hire she engages ("on sight": the follower rule of task 0197) and kills him. So
  the fight is the follower's, not an assist. (That control run ended with exit code 139 after the player's death, at the
  save thumbnail of the shutdown: a dead neutral player at quit; not looked into, nothing of this task is on that path.)
- **Corrections to the first log.** The three runs' complete trace files differ (233, 229, 235 lines): only the follower's 49 lines
  are identical, the rest is other street actors' `NACTGOTO` results. The player was **moved** by teleports (`rel`), never walked:
  walking him with W or S was tried and the arrival spot is a nook (28 and 120 units of movement); "back" (walking away and
  coming back) was not shown, the follower's own return is shown by "Follow me." from 306 to 223 units. The Morrowind reference is the
  `combat5` build, not the parent of this branch; its `settings.cfg` was empty, so the three Balmora frame runs ran with sound on
  (no effect on a frame). The fight run was made on the build before the last two fixes.
- **More decisions for toddhow.**
  7. **Every script `GlobalVariable.SetValue` now feeds the dialogue conditions and the `followerstate` chunk** (not follower-only:
     any global a script sets, capped at 256 entries, a 257th is dropped without a message). Before, the dialogue read the data's
     value only. A global set by a script and then read by a condition is now what the game does; a save keeps it.
  8. **Merge hot spots for the other agents working on `papyrus_host.cpp` and `dialogue.cpp`:** the bodies of the `ReferenceAlias`
     `ForceRefTo` and `Clear` natives and of `GlobalVariable.GetValue/SetValue`; the `Actor.IsInFaction` line removed from the
     carriage block (now in `registerFollowerNatives`); `attachKeyExists`; and, in `dialogue.cpp`, `actorValue`, `inFaction`,
     `factionRank`, `globalValue`, `relationshipRank` and the new `isPlayerTeammate`, `overlayReferenceOf`.
- **Recalled, not read, marked RECALLED-VERIFY:** how Uthgerd, the Companions, Faendal and Sven get their relationship rank (by
  script, from memory), what the unnamed faction 0x750B8 of the alias is, the Creation Kit signatures of the natives of 1.7
  (`GetFactionRank` returns -2 for a non-member), that `Actor.AddToFaction` is a script function (seen in the native trace, not
  by decompiling `Actor.pex`), and the rank enumeration (-4 to 4).
- **Start-up cost of the cast change:** in a fresh session (30 s of script time) native calls answered went 457 to 558 and tolerant
  natives 7 to 13, scripts loaded 731 to 732, script errors 0 to 1 (`CompanionsBladeFragmentTracking.Setup`, `the array is None`).
- **Notes, no code change:** `FollowerState` keeps a function (`setActorValueSource`) that points into the Papyrus host's
  `Impl`; it is unreachable once the host is destroyed with the pack (`mPapyrus.reset()` leaves the state alive in the pack), but
  a later change that reads `waiting()` after that reset must clear the source first. The per-frame wait sync
  (`syncFollowerAlias`) allocates once a follower exists (`followerIds()` builds a vector, `avKey` a string per call);
  it runs once a frame, only with a follower, and is not measured.
