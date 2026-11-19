# Task 0356: one `quest rules` switch for the general rules of the Skyrim quest slices

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack), the quest slices of tasks 0261 to 0350.
Decided by toddhow on 2026-11-19 ("go with all recommendations"). Same hand wrote tests and code (tests first, run red; no fresh
reviewer); Windows only; local branch `questrules-0356` from `ca52ec4f0b`, not pushed.

Goal: ONE pack setting, `[extension.skyrimse] quest rules` (default **true**, needs `papyrus`), turns on every general rule that the
quest slices each switched on under a setting of their own; the old per-quest settings keep working (the new switch is an OR);
hand-written stand-ins stay under their own quest setting.
Inherited code touched: `game_packs/skyrimse/src/skyrimse_game_pack.cpp` (`PapyrusDriver::enableQuestRules`, `wirePapyrusWorld`, the
pack `start()`), `game_packs/skyrimse/src/dialogue_runtime.cpp` (`begin`, `beginForced`, `beginForcedTopic`),
`game_packs/skyrimse/extension.json`, new `game_packs/skyrimse/include/game_skyrimse/quest_rules.hpp`.
Out of scope: removing a per-quest setting, changing a rule's meaning, the Linux gate.

## 1. The rules and who turned them on (survey of `skyrimse_game_pack.cpp`)

"was" = the settings that turned the rule on before this task. All of them are now ALSO turned on by `quest rules`
(`PapyrusDriver::enableQuestRules()` for the driver's flags, `qr ||` in the blocks of `wirePapyrusWorld`, `mQuestRulesWanted` in the
functions that run before the Papyrus host exists).

| Rule | What it does | was (task) |
| --- | --- | --- |
| box triggers fire (`setPrimitiveTriggers`, `mAcceptBox`) | a placed box a quest script owns fires `OnTriggerEnter` for the player; followers of a quest package and cart horses are subjects | `daedric quests` (0310), `college` (0292), `main quest start` (0274), `dragonborn`, `dawnguard` (0319), `thieves guild` (0314), `main quest whiterun` (0328), `side quests` (0341) |
| the player enters a box with his body (`setPlayerBodyTriggers`) | feet to 128 units above the box count (0346: the gate's box hangs above the road) | `solitude` |
| quests move actors near the player (`setRelocation`) | `MoveTo` of an actor within 7,000 units makes a new reference there | `college`, `daedric quests`, `main quest start`, `dawnguard` |
| enable children follow their parent | a script that enables a marker enables its XESP children | `main quest start`, `dawnguard`, `side quests` |
| player events reach alias scripts (`enablePlayerEvents`) | `OnLocationChange`, `OnObjectEquipped`; also the stage-entry check | `thieves guild`, `main quest whiterun` (0318) |
| quest greetings (`setQuestGreetings`) | the nameless start of a quest's branch that names its speaker is the greeting; a nameless link goes on | `daedric quests` (0304, 0310), `college`, `main quest whiterun`, `solitude`, `forsworn conspiracy`, `dark brotherhood`, `bards college`, `companions`, `blade in the dark` |
| Blocking branches (`setBlockingBranches`) | a Blocking branch's first passing line says the greeting instead of Hello | `civil war` (0298), `main quest whiterun`, `companions`, `daedric quests`, `solitude`, `dark brotherhood`, `blade in the dark` |
| walk-away topics and infos (`setWalkAwayTopics`, `setWalkAwayInfos`) | the walk-away topic (TWAT) is not a button and its INFO runs as the player leaves | `civil war`, `companions` (0315), `bards college`, `daedric quests`, `dark brotherhood` |
| fragments run before the next menu (`setFragmentFlush`) | queued INFO fragments run before a menu or greeting is judged | `thieves guild`, `main quest whiterun`, `college`, `daedric quests`, `bards college`, `companions`, `solitude`, `forsworn`, `blade in the dark`, `dark brotherhood` (0304 and later) |
| hello greetings (`setHelloGreetings`) | a ForceGreet package whose topic is subtype HELO says the actor's greeting | `main quest whiterun` (0308), `dragonborn` (0320) |
| live GameHour (`mBloodIceOn`) | the Papyrus global GameHour is the core's hour | `blood on the ice` (0333), `laid to rest` |
| quest updates reach alias scripts (`setQuestUpdatesReachAliases`) | a quest's `RegisterForUpdate` reaches its alias scripts; exterior player location for `IsInLocation` | `laid to rest` (0342) |
| the Player alias holds the player (`setPlayerAliasFill`) | a unique-actor alias of the Player NPC_ is the player (`OnWardHit`) | `college` (0348) |
| failed scene phases are skipped (`enableSkipFailedPhases`) | a phase whose start conditions fail is skipped | `college` (0348; 0308 did the same for HELO) |
| begin-fragment scenes start unloaded (0350), actionless scenes start unloaded (0333) | a scene with a begin fragment or no actions starts with its actors unloaded | `daedric quests`, `blood on the ice`, `laid to rest` |
| finished scene packages are held (0349) | a finished walk keeps the actor where it ended until the scene does | `blade in the dark` |
| scene walks end at their targets, live actor places (`enableSceneWalks`, `useLiveActorPlaces`, `setLivePlaces`, `setCarryTravellers`) | a walk the mover cannot do ends at its target; conditions read where an actor stands; travellers are carried through doors | `companions` (0277), `main quest whiterun` (0328), `college` (0327), `blade in the dark`, `side quests`, `dragonborn` (0329), `daedric quests` (0350) |
| alias packages in ONE priority order (`setAliasPriorityOrder`) | a quest's alias packages go in one order by quest priority | `side quests` (0341), `blade in the dark` (0349), (0334 for `dawnguard`) |
| quest alias packages (`setAliasPackages`, ForceGreet, `scene park seconds` 5, `setApproachGreeters`) | the alias packages and forced greetings of quests | `quest alias packages` (0271), `companions`, `dragonborn` (approach) |
| quest-package followers are carried (`setFollowPlayerPackages`) | Barbas, Farkas, Saadia follow by their own packages | `daedric quests` (0310), `companions` (0340), `side quests` |
| dead lookup (`setDeadLookup`) | `GetDead`, `GetDeadCount`, `Location.GetRefTypeAliveCount` count deaths of the session | `civil war` (0326), `side quests`, `solitude`, `forsworn conspiracy`, `dark brotherhood` |
| game-time updates (`enableGameTimers`, `mGameClockOn`) | `RegisterForUpdateGameTime` on the rest clock | `marriage` (0276, its own feature, not changed), `solitude` (0346) |
| actors detect the player (`setDetectsPlayer`) | `GetDetected` of the player holds for an actor in his cell | `golden claw` (0335) |
| created items in inventories (`enableCreatedInInventory`) | an ALCA "In" alias creates its item in the holder's inventory | `golden claw` |
| actor cell moves (`setCellMoves`) | a quest's `MoveTo` into another cell moves the real placement | `college` (0327); the setting `actor cell moves` (0286) stays its own |
| package begin hook, UseMagic hook, ward source | a package's OnBegin fragment, a UseMagic package casts, `GetActorValue(WardPower)` | `college` (0348) |
| spell events reach the scripts | `OnSpellCast`, `OnHit` with a spell, `OnMagicEffectApply`, `OnWardHit` | `college`, `main quest whiterun` |
| actors fight, paired kill moves (`setActorFights`, `setPairedKillMoves`) | `Actor.StartCombat` between NPCs, a kill-move idle kills its target | `forsworn conspiracy` (0337) |
| essential bleedout, stand on engage | an essential actor falls into bleedout (and stands up when it engages) | `companions` (0264), `thieves guild` (0347) |
| dragon souls by the game's script (`mWhiterunQuest`, `setDragonSoulByScript`) | `ModActorValue("dragonsouls")` and the dragon's own death script give the soul | `main quest whiterun` (0313) |
| rank -1 members, actor editor location | JobJarlFaction, JobInnkeeper rank -1 count; `GetIsEditorLocation` on an actor | `laid to rest` (0342), `houses` |
| loaded lookup (`HasLoaded3D`), judge water early, starts-dead lookup, moved actors outdoors, ship cross-world travel hours | the world answers `HasLoaded3D`, a start cell is judged by its content, a "starts dead" corpse does not fight, an actor a script moved outdoors reads its brain's place | `dawnguard` (0330), `dragonborn` (0329, 0312) |
| the player's worn and counted items, scripted activators | conditions on the player's items; a scripted activator (Azura's altar, the Black Door) is activated by the real key | `marriage`, `golden claw`, `daedric quests`, `dark brotherhood` |
| begin-on-quest-start scenes wait for their actors (`setRetryQuestStart`) | such a scene is asked again every second | `main quest start` (0268) |
| stage entry check | a stage's fragment runs only when its log entry's conditions hold | `houses`, `main quest start`, `dragonborn`, player events |

`quest_rules.hpp::kRules` lists these (29 names) and the pack logs them once: `QUESTRULES on: ...`.

## 2. What stays under its own setting (hand-written stand-ins and features)

- `blade in the dark`: named snap packages of Delphine (`setSnapPackages`) and her placement as a trigger subject (`addTriggerSubject`).
- `side quests`: `setSnapMustComplete` (a MUST COMPLETE Travel the mover cannot walk ends at its target).
- `dragonborn`: the moored Raven Rock ship kept enabled (`setKeepEnabled`), `dragonborn test stage`, `dragonborn test prerequisites`.
- `main quest start`: the Helgen start place, the cart rides, the quest start (MQ101), the test stage aid.
- `houses`, `marriage`, `house building`, `ambush`, `companions test training`, `dungeon mechanics` and every `... test ...` aid: features or aids, not rules.
- **`solitude` and `dark brotherhood` keep `setPromptStartsAreMenus` for themselves** (a conflict, section 4).

## 3. Part B: the Bersi race (task 0347)

A fragment queued by an earlier line (`TIF__00021E7A` sets `pTG01BersiWarned`) could run after the next greeting or menu was judged,
so a choice appeared or disappeared with the timing. The flush existed for `offerTopics`, `offerLinks` and the nameless links, but
NOT before the greeting of a new conversation (`begin`, `beginForced`, `beginForcedTopic`). All three now run the flush first
(`dialogue_runtime.cpp`), and the flush is one of the general rules, so it is on in every session with `quest rules`.
Tests: `apps/components_tests/skyrimse/testskyrimsequestrules.cpp` (`SkyrimQuestRulesFlush`, 3 tests; run red first: the greeting test
failed against the old code ("Fresh." for "Warned."); the other two pin the old behaviour).

## 4. Rules whose meaning conflicts between quests (reported, not guessed)

1. **Prompt-only starts as menu buttons (`setPromptStartsAreMenus`, 0343 and 0346).** It adds buttons to other quests' talks:
   Aventus's DB01 conversation offered three more buttons at its end (topics `0001F330`, `00050C98`, `00050C99`, run `db-a`), so the
   scripted clicks of the DB01 scenario broke. It is therefore NOT in the general switch; `solitude` and `dark brotherhood` still turn
   it on. Decision for toddhow: find which condition hides those topics in the game (they are probably reachable only by a link) and
   then make it general.
2. **Walk-away topics hide a button.** With the rule on, the topic the last response names as its walk-away (TWAT) is not a button. In
   the Blade in the Dark, `main quest whiterun` and Sinding's talks the old settings did not turn it on, so the menus there had one
   more button than now. Proved (section 6): `main quest whiterun` + `bards college` (walk-away only) gives the same menus as
   `quest rules`. The new menu is the game's (the walk-away line is spoken as the player leaves), but input scripts that click a fixed
   button position of the old menu change their path.
3. **Quest greetings and Blocking branches for quests that had neither** (the Dragonborn ship scenario): Gjalund of Windhelm and Adril
   greet with a quest line (99 and 111 bytes, 2 topics each) where `dragonborn` alone gave Hello (4 bytes, 1 topic). Which is the game's
   is not checked.
4. **`setSnapMustComplete`** (a stand-in of 0341) would place any actor whose MUST COMPLETE Travel fails; kept under `side quests`.
5. **`GetDetected` always true (`setDetectsPlayer`)** applies to every actor in the player's cell when the rule is on.
6. **Stage entry conditions** now apply in every session (they were only on for `houses`, the main quest, `dragonborn` and player
   events): a stage's fragments run only for the log entries whose conditions hold.
7. **Side effects seen in the runs:** `MS14` (Laid to Rest) goes to stage 80 about 10 script seconds after the start in a new game
   (its alias scripts now get `RegisterForUpdate`; before it stayed at stage 1), and more world-encounter quests (`WE01` ...) start at
   the start of a new game once their conditions are answerable. Both are the quests' own scripts running.
8. **Dragon souls by the game's script** replace task 0253's hand-written soul absorption for a dragon that has `dragonactorscript`.

## 5. Tests

New file `apps/components_tests/skyrimse/testskyrimsequestrules.cpp` (5 tests): the manifest declares `quest rules` as a bool with
default true; the rule names are unique and not empty; the greeting is judged after the queued fragments ran; with no flush it is not;
the flush runs before every greeting. Red first against the old code: 2 of 5 failed (the manifest test and the greeting test), the
other three pin absence or uniqueness. No existing test edited.
Wide filters after the change: `components-tests` `*Skyrim*:*Dialogue*:*Papyrus*:*Scene*:*Package*:*Quest*:*Extension*:*Neutral*:
*Frozen*:*Journal*:*Follower*` 4,587 ran, 4,456 pass, the rest data-gated skips, 0 failed; `openmw-tests` `*Neutral*:*Skyrim*:*Extension*`
1,192 ran (1,190 pass, 2 skips; the binary was NOT rebuilt for this task).
`tools/testprofile/coverage.sh` ok; profile line and README line added (the setting is a pack setting, on by default).

## 6. Part C: the quest harnesses, before and after

Method: the same binary, two settings sets. **before** = the harness with its own per-quest settings and `quest rules = false`;
**after** = the harness with those per-quest settings (and `quest alias packages`) removed, so only `quest rules` (default) plus
the infrastructure settings (`papyrus`, `actor packages`, `inventory`, the neutral ones, `actor cell moves`, ...) and the test
aids. Copies of `tools/` with the quest settings stripped were used; run folders were
`I:\Projects\OpenEden-work\papyrus\qr56\runs`, they were deleted at the end (the stage lists above are what is kept).
"Furthest stage" is the highest stage whose fragment ran per quest (`PAPYRUS quest X stage N runs` or `set to stage`); the talk
shape is the number of buttons of each turn of the scripted real-click conversation (`NTALK turn`).
The input scripts of most of the documented long scenarios (the in8, in345, in349 files of earlier tasks) were NOT in the repository,
so only the scenarios whose inputs are committed (or that need no input) were run; a run with no `NTALK` line is a start-up run.
Every run: exit code 0, 0 crash dumps, 0 error lines (the Solitude s1 run has 6 error lines in both settings: the navigator's
`rcBuildRegions` and a Papyrus instruction budget, older than this task).

| Harness / scenario (frames) | before: furthest | after: furthest | Result |
| --- | --- | --- | --- |
| `skymoonlight_check` DA05 Ill Met by Moonlight (10,200; rerun on the final binary) | DA05 (`0002A49A`) stage 20, Sinding's escape scene; talk 2,2,2,4,5,1 | same, same talk | EQUAL; side effect: MS14 (`00025F3E`) 1 to 80 (section 4) |
| `skydb_check/db02.sh` DB01, the Aventus talk (15,600; final binary) | stages as `after`, talk 2,4,2,1 | same, same talk | EQUAL (136 quests). Before `setPromptStartsAreMenus` was taken out of the general set: Aventus offered 3 more buttons, 10 turns |
| `skycompanions_check` C01 Proving Honor (20,800) | C00 (`0004B2D9`) 150, C01 (`0006E803`) 200 | C00 150, C01 200, same talk (2,2,3,1,2,8,2,1) | EQUAL (135 quests) |
| `skydawnguard_check/bloodline.sh` Awakening (14,000) | DLC1VQ01 (`0200352A`) 200, talk 4,3,3,3,1 | same | EQUAL; MS14 side effect only |
| `skycivilwar_check` join chain, Castle Dour (14,800) | CW00A (`000D3C5F`) stage 7 | CW00A stage 10 (further) | after is further; more world-encounter quests start at the start of the game |
| `solitude_check` s1 execution (4,000), s2 throne room (3,000) | SolitudeOpening scene quest (`000B2FD9`) stage 20; MS06 stage 5 | same | EQUAL (138 quests). s2's click input is not in the repository, so Falk's menu was not exercised |
| `skymytime_check` mt1 Saadia (9,500) | MS08 (`0001CF25`) 25, talk 3,3,2,4,4,3,2 | with `side quests` kept for its stand-in, after the root-cause fix (section 6.3) and new click positions: MS08 25 | EQUAL stage; two Goodbye click positions changed (section 6.3) |
| `skymytime_check` mt3 Kematu (5,800) | MS08 200 | MS08 200 (side quests kept; one click position changed, section 6.3) | EQUAL stage |
| `skywhiterunmq_check` mq318 outro (17,000) | same stages (133 quests); talk 4,3,2,2,1 | same stages; talk 4,3,2,1 | EQUAL stages, one button fewer (walk-away topic) |
| `skyblade_check` intro (15,200) | MQ106 (`00032926`) 35, talk of 13 turns | with `blade in the dark` kept for its stand-ins: same stages (148 quests), talk 1,1,4,4,4,3 | EQUAL stages; the walk-away topic hides one button, so the fixed clicks stop at turn 4 |
| `skysolstheim_check` ship (10,200; final binary) | DLC2MQ01 (`04017F8E`) 20, Gjalund 5 turns | with `dragonborn` kept (ship stand-in, `dragonborn test stage` aid): same stages (157 quests), other greetings | EQUAL stages; quest greetings change the first lines |
| `sknight_check`, `skybloodice_check` (default plans, 10,200; final binary) | start-up only, no talk | same stages (131 and 133 quests) | EQUAL; their own plans and inputs are not in the repository, so the DA14 and MS11 chains were NOT re-run |
| `skycollege_check`, `skythieves_check` | not run: no input script in the repository (the clicks of tasks 0348 and 0347 were in work folders) | | NOT RUN |

### 6.1 Regressions found and what was done

1. **A bug of mine**: the rules in the part of `start()` that runs BEFORE the Papyrus host exists (alias packages, approach greeters,
   the starts-dead lookup) tested `mPapyrus->questRulesOn()`, which is null there, so `quest alias packages` was not on and Sinding's talk
   had 1 button at turn 4 against 4. Fixed: `mQuestRulesWanted` (the setting and `papyrus`, read first).
2. **`setPromptStartsAreMenus` is not general** (section 4): it added three buttons to Aventus's DB01 talk and broke the scripted
   clicks. Taken out of the general set; `solitude` and `dark brotherhood` keep it.
3. The Dragonborn and Blade in the Dark runs first failed because the stand-ins live under the quest setting (the ship, Delphine's snap
   packages, the `dragonborn test stage` aid): the "after" runs keep that one setting, as the task asks.

### 6.2 Menus that differ because MORE rules are on (not fixed, reported)

- Walk-away topics: the blade and mq318 menus lose one button. Proved by running the old settings plus `bards college` (walk-away topics,
  quest greetings and the flush only): blade 1,1,4,4,4,3 and mq318 4,3,2,1,1,2, the same as `quest rules`.
- The MS08 Saadia regression is explained and fixed: section 6.3.

### 6.3 Root cause of the MS08 (Saadia) regression

Dialogue trace of mt1 (`NTALK`, `NQUESTGREET`, CONDDEBUG of MS08), old settings plus `quest rules = false` against `quest rules`:

- First divergent turn: the GREETING. Without the rules Saadia says her plain Hello (4 bytes) and the menu holds MS08's topic. With the
  rules (before the fix) `NQUESTGREET quest greeting topic 000DEE82: 1 considered, 1 passing, chosen 000DEE86` made her say "You want a
  drink?" (17 bytes) and the links of that line (`000215AA`, `0007F6BB`) replaced the whole menu.
- `000DEE82` is `WITavernServerPlayerTopic` of the radiant quest WITavern (`0DEE92`): a nameless CUST branch start that names its speaker
  (GetIsAliasRef Server1 = Saadia). Package `0DEE98` `WITavernServerGreetPlayer` (ForceGreet) says it through its PDTO topic. The quest
  greeting rule of task 0304 (a nameless start no INFO links to that names its speaker is the greeting) took it for a greeting, but the
  game says it from the package, not from the Hello. The rule is part of `quest rules` and so applied to every quest; per quest setting
  it never applied to WITavern.
- The rule was WRONG for this case. Fix at the root, one mechanism: a nameless start that a package (`PackageIndex::greetTopics`, PDTO of a
  ForceGreet package) or a scene (`SceneIndex::dialogueTopics`, a dialogue action's topic) says on its own is not a quest greeting
  (`DialogueRuntime::setReferencedTopics`, built in `world_view.cpp`). Tests: `SkyrimQuestGreeting2` (3, new file, written first).
- With the fix mt1's first talk is Hello again, 3 buttons. Two things remain different, both the game's: the INFO's end fragment now runs
  when the line is said (the fragment flush, part B), so MS08 goes to stage 20 DURING the conversation and the topics gated on stage 10
  vanish (turn 2 has only Goodbye, a frame was viewed); in the second talk the same rule makes one button disappear. The harness's fixed
  click positions were updated (`tools/skymytime_check/scenarios.sh`: Goodbye of the first talk 640,442 to 640,404, the "How am I supposed
  ..." button 640,448 to 640,431, s3's topic 640,341 to 640,366). With only `quest rules` (and `side quests` for its stand-in): mt1
  reaches MS08 stage 25, mt3 reaches stage 200. Text of one reply is 227 bytes against 261 before: not looked into.
- The walk-away explanation for the blade and mq318 menus stands (proved by old settings plus `bards college`).

Morrowind unchanged against `I:\Projects\OpenEden-work\n6saves\build` (main, not the exact parent): `tools/worldobjects_check` 110 objects OK
(111 result lines in both), `tools/skymap_check/morrowind_check.sh` Balmora frames `1eb68236352dc472` and `c819db116f4af8a6` identical for both
builds and the plain Balmora frame (`--start Balmora --random-seed 7`, fixed frame time, `--auto-screenshot 600`, this base config) is `0d6dd39995b927fa` for both builds (`1c5d47ad0b5dc32d` belongs to another config).

## 7. Decisions for toddhow

1. `setPromptStartsAreMenus` stays out of `quest rules` (it adds buttons to other quests' talks). Find the condition that hides
   Aventus's three topics in the game and make it general, or keep it per quest. Until then `quest rules` alone does not make Falk's
   request (MS06) or Astrid's DB02 line a menu button.
2. Walk-away topics, quest greetings and Blocking branches now hold for every quest: some old click paths (MS08 Saadia (done), the blade intro,
   the Dragonborn ship) need new input scripts. Accept, or keep those rules per quest.
3. With the rules on MS14 (Laid to Rest) goes to stage 80 about 10 script seconds after a new game starts, and more world-encounter
   quests start; both come from the quests' own scripts but are visible in a default session.
4. `setSnapMustComplete`, the Blade in the Dark's snap packages and trigger subject, the Raven Rock ship and the test aids stay under
   their quest settings (stand-ins). `quest rules` therefore does NOT make a quest playable that needs one of them.
5. `quest rules` is on by default; with `papyrus` off nothing changes.
6. The flush before every greeting is installed by `quest rules`, so it is on in every session with it.

## 8. Not run

The Linux gate, clang-format 14 (formatted by hand), clang-tidy, the Lua tests, `openmw-tests` after the change (not rebuilt), a fresh
review, the long click scenarios of tasks 0345, 0347 and 0348 (their inputs are not in the repository), the Solitude Falk menu and the
DB02 and DB03 chains under `quest rules` alone (their prompt-start menus need `solitude` and `dark brotherhood`), the cause of the
Saadia menu difference.

