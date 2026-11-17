# Task: Skyrim marriage, second slice: game-time updates and the wedding day (task 0276)

Status: ordered by the coordinator on 2026-11-17 (toddhow chose "Marriage: wedding day"). Follow-up of task 0270. Same hand wrote the tests
and the code (the game-time tests were written first and run red against a stub; the rest was driven by running the scenario, see the stage
log); no fresh reviewer. Windows only. Local commits on the worktree branch from `34beb9d459`, not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (M-S7); tasks 0270 (the courting chain), 0194 (scenes), 0264 and 0271
(forced greeting, alias packages), 0268 (relocation of actors near the player), 0210 (the game clock of the rest windows), 0187/0211/0217
(story manager, aliases).
Goal: the wedding runs through the game's own quest, scene, dialogue and Papyrus: the wedding day comes by the game clock, the lover moves
into the temple, the ceremony plays with its real lines, the vows are the priest's forced greeting, and stage 100 gives both rings.
Constraints: everything behind `[extension.skyrimse] marriage` (off by default, no new setting); Morrowind unchanged.

## 1. Survey (read from the data and the decompiled scripts)

The wedding quest `RelationshipMarriageWedding` (0x7404E) maps its stages to fragments by the VMAD list: stage 0 (start-up) `Fragment_5`
(`MarriageDay = floor(GameDaysPassed) + 1`, `RegisterForUpdateGameTime(1)`), 10 `Fragment_0` (the wedding day: moves the priest, the lover and six
witnesses into the temple with `MoveTo`, starts `WeddingScene`), 20 `Fragment_2` (player AI driven), 30 `Fragment_6` (controls back), 100
`Fragment_3` (the marriage: courting quest to stage 100, `PlayerMarriedFaction`, both rings, `RelationshipMarriageFIN.SendStoryEvent`, stage
500), 150 `Fragment_8` (scene stopped, 30 and 200), 200 `Fragment_1` (failed: rank -2), 500 `Fragment_15` (`RegisterForSingleUpdateGameTime(1)`),
1000 `Fragment_17` (`WaitGameTime(3)`, `Stop`). `RelationshipMarriageWeddingScript.OnUpdateGameTime` (an hourly check) sets stage 10 when
`floor(GameDaysPassed) == MarriageDay`, stage 150 when the day has passed with the ceremony not attended (stage 20 not done), and 1000 from 500.
The scene `RelationshipMarriageWeddingSceneView` (0x74791): 11 phases; phase 1 ends when the player is within 1,000 units of the Altar alias
(0xEB385, CTDA GetDistance with the "use aliases" flag); phases 3 to 7 are the priest's five lines; phase 8 the lover's "I do"; phase 9 the
priest's `MarriageWeddingMaramalForceGreetPackage` (a forced greeting of topic 0x75C9E: the player's "I do. Now and forever." choice, then the
priest's declaration); the vows' INFO `TIF__00075CA2.Fragment_1` sets stage 100.
The married state `RelationshipMarriageFIN` (0x21382): alias LoveInterest from the story event (SCPT, event data 'R1', condition
`GetInFaction(PotentialMarriageFaction)`), `LoveInterestFINScript.OnUpdateGameTime` adds `GoldEarnedAmount` (100) to `MarriageGoldEarned` once a
day while the spouse is in `JobMerchantFaction`; `GiveGold` pays it out (from the store dialogue line).

## 2. What was built

- **Game-time updates in the Papyrus host** (`papyrus_host.{hpp,cpp}`): `Form`/`Alias` `RegisterForUpdateGameTime`,
  `RegisterForSingleUpdateGameTime`, `UnregisterForUpdateGameTime` and the `OnUpdateGameTime` event, driven by a game clock the pack gives each frame
  (`setGameClock`). Off unless `enableGameTimers(true)` (the natives then do nothing, as before). A repeating timer that a long wait jumped over
  fires once and goes on at its next multiple; a repeating and a single registration of one script live side by side and each kind replaces its
  own; a registration made before the first clock waits for it; timers of a quest that stopped are dropped; timers are saved in the host's state
  (version 5, only when there are any: a state without them keeps version 3 byte for byte); the quest of the vampirism service is ignored
  (the pack runs its updates itself).
- **The clock feed** (`skyrimse_game_pack.cpp`, `feedGameClock`): the rest windows' game clock in game seconds, and the GLOB `GameDaysPassed`
  (0x39) from it, so `RelationshipMarriageWeddingScript` reads the real day. Skipped while the rest clock is not known yet, so the two clocks never
  switch under a timer.
- `ObjectReference.IsInLocation` (the reference's location, or its cell's for a created reference, is the location or inside it).
- **GetDistance with the "use aliases" flag** for the player (`SkyrimConditionWorld::distanceToAlias`, `setPlayerPosition`): the scene's first phase.
- **Created references in conditions and aliases**: a reference a script created (a `MoveTo` of an actor near the player makes a new one,
  task 0268) answers conditions as an actor of its base (`SkyrimConditionWorld::findReference`), can be named by a story event
  (`QuestAliasFiller::Environment::mCreatedBase`) and is a script `Actor` (not an `ObjectReference`): without it `Alias_LoveInterest.GetActorRef()`
  after the lover moved into the temple was an `ObjectReference`, every `Actor` function on it was a call on None, and the married-state quest could
  not start.
- `ObjectPlaceResult::actor` (additive): the relocation hook says whether it made an actor.
- Relocation of actors (task 0268's rule, 7,000 units from the player) is switched on only when the wedding quest runs, not for the start-up quests.
- A warning when `marriage` is on without `actor packages` and `quest alias packages` (the travel packages and the forced greeting).

- **A call through a handle** (`components/script_papyrus`, `Runtime::instanceDefining`): a script-typed property that holds an alias (or
  reference) handle calls the function of the script attached to it. `TIF__000D0652` calls `LoveInterest.GiveGold()` where the property is a
  `LoveInterestFINScript` and the value an alias handle; the VM answered "class ReferenceAlias has no function GiveGold". The only VM edit: the
  "no such function" branch of a call on a form value tries the scripts attached to that object first.
- `Form`/`Alias` game-time registrations made on another object (`Alias_LoveInterest.RegisterForUpdateGameTime(24)` on a property) are for the
  scripts attached to that object (`gameTimerTargets`).
- Test aid `marriage test fin stage = N` (declared in `extension.json`): sets the married-state quest to stage N when the wedding quest reaches
  stage 500 (10 makes the spouse take a job and registers the daily income; the real stage comes from a dialogue line not reached here).
- `tools/skymarriage_check/wedding_input.py` and `wedding.sh`: the scenario as it was run (change the folders).

## 3. In game

`wedding.sh` with `[extension.skyrimse] marriage`, `papyrus`, `actor packages`, `quest alias packages`, `rest`, `journal`, `inventory`, `services`,
`crime`, five masters, real clicks and the real rest key T through `OPENEDEN_RMLUI_INPUT_SCRIPT`, fixed frame time, quiet window, volumes 0;
frames in `I:\Projects\OpenEden-work\combatai\marriage2\keep` (viewed). The courting chain of task 0270 to the wedding quest, then:

- the rest window (T, 23 clicks on +, Wait) waits 24 game hours in the Temple: `game-time update of relationshipmarriageweddingscript`, stage 10
  runs `Fragment_0`; Ysolda (the lover, 0x1A69A) is moved into the temple by `MoveTo` (`relocate ... made FF800001`; the priest is already in the
  temple location, so `IsInLocation` keeps him; the six optional witness aliases are unfilled: "call of IsInLocation / MoveTo on None", harmless);
- the scene `RelationshipMarriageWeddingSceneView` runs phases 0 to 9: phase 1 ends when the player is within 1,000 units of the altar (GetDistance
  with the alias flag), stage 20, then Maramal's five lines with real voice and subtitles, the lover's "I do. Now and forever.", then the priest's
  forced greeting: the box offers "I do. Now and forever." / "Stop the wedding!" / "Goodbye"; the click runs `TIF__00075CA2` (stage 100), the
  declaration ("...matching rings...") follows and after Goodbye stage 100 runs: **the ring is in the player's inventory** (frame), the courting
  quest goes to 100, `PlayerMarriedFaction` is given, stage 500, and the story event starts `RelationshipMarriageFIN` ("started with 31 aliases
  filled");
- the spouse (the relocated Ysolda) now offers "What have you got for sale?" (the job faction), "Would you mind cooking something for me?" and "Has
  the store made any money?" (frame); an hour later the single update takes the wedding quest to stage 1000 and it stops (its timers are dropped);
  with the test aid at FIN stage 10 and two more days waited (24 hours twice) `LoveInterestFINScript.OnUpdateGameTime` fires on the alias script
  (73.57 and 97.59 game hours) and "Has the store made any money?" gives **200 gold (player gold 300 to 500)** through the game's own `GiveGold`.

**Determinism:** three runs of the whole scenario (frames 21,500, started concurrently) give the same 220 trace lines (quests, fragments,
game-time updates, scene, forced greeting, `MARRIAGE`, `NTALK`, item and relocate lines; md5 `6dafc4bdccc5`) once one unrelated start-up quest line
(`quest 0004B2D9 set to stage 1`, whose place among the others depends on the records worker, also without this task) is left out.
**Morrowind unchanged:** `tools/worldobjects_check` 111 lines OK against `integ2`'s build (a build of main; no engine file changed here and the pack
is not loaded in a Morrowind session) and the two frames of `tools/skymap_check/morrowind_check.sh` are `1eb68236352dc472` and `c819db116f4af8a6`
for both executables. Script error lines of the scenario: 2, the same two (`CompanionsBladeFragmentTracking.Setup`) as before this task.

## 4. Tests

New: `apps/components_tests/skyrimse/testskyrimsegametime.cpp` (13 tests: single, repeating, a long wait, a registration before the first clock,
unregister, a repeating and a single registration side by side, the ignored form, the off state, the saved state and its old version,
registration on another reference, `IsInLocation`, a call through a handle) and `testskyrimseconditionalias.cpp` (2: GetDistance with the alias
flag). **Red against a stub of `fireGameTimers`: 7 of the first 9 failed (the 2 that passed test absence); the registration-on-another-reference test
failed before its fix.** The `IsInLocation`, call-through-a-handle and alias-distance tests and the created-reference code were written AFTER their
code (the created-reference code has no unit test: its evidence is the married-state quest starting in game); a mutation of the long-wait rule
(`missed = 1`) fails its test and was reverted. Area runs: components `*Skyrim*:*Papyrus*:*Marriage*:*Extension*:*Dialogue*:*Quest*:*Frozen*:*Neutral*`
3,427 pass, openmw `*Papyrus*:*Neutral*:*Extension*:*Lua*` 873 pass; no existing test edited, none fails. Not run: whole binaries, the Linux gate,
clang-format 14, clang-tidy, the Lua tests, a fresh review.

## 5. Ranked gaps and decisions for toddhow

1. **With `marriage` on every quest's game-time registration fires** (before: no `OnUpdateGameTime`; the vampirism quest is excluded). In the
   scenario the only other registrant is `BYOHRelationshipAdoptableScript` (Hearthfires, hourly); the follower scripts' 72 hour dismissal now runs.
   Script errors did not change (2 before and after).
2. The wedding is attended in the temple (relocation of actors works within 7,000 units of the player, task 0268, switched on when the wedding quest
   runs). The missed-wedding rule (stage 150 after the day) comes from the game's script but was NOT run.
3. The player is not AI driven (`SetPlayerAIDriven`, `ForceThirdPerson`, controls: counted no-ops) and stands 411 units from the altar in the scenario.
4. `Utility.WaitGameTime` is not built (stage 1000's three hour wait is skipped).
5. **The spouse moving into a house and the house choice dialogue are NOT built or run** (topics 0xC57DF to 0xC57EE, `MoveSpouse`, packages that walk
   the spouse to the house; needs the package brain across cells).
6. FIN stage 10 (the spouse's job and the daily income) comes from a dialogue line that was not reached: the test aid sets it. Break-up and rival
   quests, `Game.AddAchievement`, the sleep ability and `MarriageFINPlayerScript` were not read.
7. A topic with no response (`RelationshipMarriagePostWeddingLoveInterestBlockingTopic`, FULL = its editor id in the data) appears in the spouse's
   list until FIN stage 10; not investigated.
8. Decisions: (a) the VM edit is in `components/script_papyrus`, outside the pack; (b) `ObjectPlaceResult` gained a trailing `actor` field,
   `PapyrusGameData` two virtuals with defaults, the host state version 5 (only with timers); (c) the repeating/single replacement rule and "a stopped
   quest drops its timers" are RECALLED-VERIFY.

## 6. Edits outside the new files

`papyrus_host.{hpp,cpp}` (game-time timers, `IsInLocation`, the class of created actors, `gameTimerTargets`; additive), `dialogue`, `conditions`,
`quest_alias`, `world_view`, `skyrimse_game_pack.cpp` (clock feed, relocation switch, test aid), `marriage.hpp`, `extension.json`,
`components/script_papyrus/papyrusvm.hpp` and `papyrusinterp.cpp`, `apps/components_tests/CMakeLists.txt`. Nothing in `scene*`, `package_brain*`
or `forcegreet*`.
