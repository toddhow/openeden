# Task 0306: Skyrim Dragonborn, the start of the questline (the cultist ambush, the orders, the journal)

Branch `dragonborn-0306` from local `main` at `a703ddd3bc`. Windows only, not pushed. Same hand wrote tests and code (tests first, run
red against stubs), no fresh reviewer. Behind the existing settings (`papyrus`, `quest alias packages`, `actor packages`, `combat ai`)
and one developer aid, `[extension.skyrimse] dragonborn test prerequisites` (off, declared in `extension.json`).

## 1. What the data says (houseCARL reads of `Dragonborn.esm` and `Skyrim.esm`, decompiled `.pex` from `Skyrim - Misc.bsa`)

This install has no `Dragonborn.bsa`: the DLC's scripts, meshes and voices are inside the `Skyrim - *.bsa` archives (research
note 0297). The scripts were extracted to `I:\Projects\OpenEden-work\dragonborn0306\misc` and decompiled to `...\psc` (not committed).

**The quest `DLC2MQ01` "Dragonborn" does NOT run from the first frame.** It is started by a radiant quest:

1. **`DLC2CultistAmbushNode`** (SMQN 0x35472, parent the CLOC event node 0x1320E of `Skyrim.esm`, `SharesEvent`). Its conditions:
   `GetInCurrentLoc(Solstheim) == 0` run on the player, `GetStage(MQ105 "The Way of the Voice") >= 160`,
   `GetStage(DLC2MQ01) < 5`, `GetStageDone(DLC2WE09, 1) == 0`. **There is no player level condition in the node** (the "level 10"
   of the game's wiki is not in these records; MQ105 stage 160 is the gate). It starts `DLC2CultistAmbush` (QUST 0x3546D,
   `Event CLOC`, priority 50, `Filter DLC2\Radiant\`).
2. **`DLC2CultistAmbush`**: alias `Location` (event `CLOC`, `NewLocation`, conditions: `LocationHasKeyword(LocTypeHabitation)` AND
   (`CWCapital` OR `LocTypeTown`)), alias `CenterMarker` (`ALFA` in alias 5, ref type `LocationCenterMarker`), aliases
   `Cultist1` (creates an `DLC2LvlCultistNonHostileSummoner` AT alias 6, faction `dunPrisonerFaction`, package
   `DLC2CultistAmbushForcegreet`), `Cultist2` (a `DLC2LvlCultistNonHostile`, package 0x3546F), `Note` (creates the book
   `DLC2CultistOrders` AT alias 6, `InitiallyDisabled`, stores text) and `Player`. Stage 1, 10 (keep instance data), 255 (shut down).
3. The forced greeting: the package is a `ForceGreet` template; the topics `DLC2CultistAmbushBeginTopic`, `A2` to `A5` and `End` of
   the quest; `TIF__0203546B/C` set stage 1, `TIF__0203545C/D` set stage 10. Fragment of stage 10 (`DLC2_QF_DLC2CultistAmbush_0203546D.
   Fragment_0`): both cultists leave `dunPrisonerFaction` and `WEPlayerFriend`, join `WEPlayerEnemy`, Aggression 1; the note goes into
   Cultist1's inventory and is disabled; `DLC2MQ01.Start()`, wait until it runs, `DLC2MQ01AliasForObjective.ForceRefTo(Cultist1)`,
   `DLC2MQ01.SetStage(5)`, `DLC2WE09Chance.SetValue(5)`. Stage 255 (`Fragment_3`) removes them.
4. **`DLC2MQ01`** (QUST 0x17F8E, `RunOnce`, not start-game-enabled): objectives 5 "Find out who sent the Cultists", 7 "Read Cultists'
   Orders", 10 "Travel to Solstheim", 20 "Search for information about Miraak", 25 "Investigate the shrine", 30 "Reach the Temple of
   Miraak". Stage 5 (`Fragment_13`): objective 5 displayed, `DLC2MQ02FreaTempleSceneQuest.Start()`, `SetActive(true)`. Stage 7
   (`Fragment_15`): objective 5 completed, objective 7 displayed. Stage 10 (`Fragment_1`, `_16`, `_18`): objectives 5 and 7 completed,
   Neloth moved to his marker, objective 10 displayed. Stages 20, 25, 30, 200 follow on Solstheim.
5. **The note** (BOOK 0x331C2 "Cultists' Orders"): its script `DLC2CultistOrdersScript` (an object reference script on the BOOK):
   `OnContainerChanged` into the player's container with DLC2MQ01 below stage 7 forces the handler alias and sets stage 7, `OnRead`
   sets stage 10. Text: "Board the vessel Northern Maiden docked at Raven Rock. Take it to Windhelm, then begin your search. Kill the
   False Dragonborn known as `<Alias=Player>` before `<Alias.Pronoun=Player>` reaches Solstheim. Return with word of your success, and
   Miraak shall be most pleased."
6. **The ship from Windhelm** (not reached, section 5): `DLC2DialogueRRQuestScript.SetSail(speaker, costFlag)`, called by the
   Gjalund Salt-Sage topics (`DLC2_TIF__02039DD2`): disable player controls, wait until the dialogue ends, fade out, pay
   (`DLC2CostToSail`), the first time `FirstTimeTravel()` (a sail scene with the boat, not read), later `Game.FastTravel` to the Raven
   Rock landing marker, then fade in and an autosave.

## 2. What was missing (found by running it, each behind `dragonborn` unless noted)

| # | Gap | Fix |
| --- | --- | --- |
| 1 | The story manager's node needs MQ105 at stage 160, which nobody plays here | test aid `dragonborn test prerequisites` marks MQ105 stage 160 done once the host runs (`markStageDoneForTest`: no fragment, no event) |
| 2 | **Created-object aliases (ALCO, ALCA, ALCL) were never filled**: the cultists and the note did not exist | `QuestAlias::mCreateObject/mCreateAt/mCreateLevel` parsed; `PapyrusHost::createAliasObjects` makes them at the reference of the alias ALCA names (the town's centre marker) when a quest starts, on a spiral round it (two capsules in one spot blocked each other), through PlaceAtMe's placement (`placeNear`); an actor goes through the actor system (`ObjectPlaceRequest::allowActor`; PlaceAtMe still refuses actors); created actors are script `Actor`s; ALFC factions join; the alias holds the new reference |
| 3 | The quest's alias packages (ALPC: the ForceGreet package) only reached unique actors and specific references | `PapyrusHost::setQuestStarted/setAliasCreated` tell `SkyrimWorldView::startPackageQuest/addAliasPackageReference`, so a created reference gets its alias's packages |
| 4 | A ForceGreet package asked for its conversation from where the actor stood (4,600 units off: the Lua activation reaches 3,000) | `PackageBrain::setApproachGreeters`: the actor walks (runs beyond 700) to a point 140 from the player and asks only within 380 units (`approachPlayer`) |
| 5 | `SetAV("Aggression")` reached the combat AI only with `ambush` | also with `dragonborn` |
| 6 | The note's script (`DLC2CultistOrdersScript`, on the BOOK base) never ran: scripts of a created reference were not attached, and `OnContainerChanged` went to base-form instances and aliases only | created references get their base's scripts (`SkyrimWorldView::scriptsOfBase`, attached in `flushPending`); `onItemMoved` also sends it to scripts on created references of the item's base |
| 7 | `AddItem` on a created actor failed (the inventory owner is opened from a placed reference only) | `ensureOwner` also opens created references (base from `createdObjects()`) |
| 8 | Reading a book from the inventory never sent `OnRead` | `LockBooksService::Hooks::mBookRead` -> `PapyrusHost::onItemRead(base)` sends it to scripts on created references of that base |
| 9 | Book text `<Alias=Player>` / `<Alias.Pronoun=Player>` showed as nothing | the book resolver expands alias tags (the player's own name or "the Dragonborn", he/she by `player female`) |
| 10 | Stage 10 of DLC2MQ01 ran all three fragments (one per log entry: any, in Solstheim, not in Solstheim), so `SetStage(20)` fired in Whiterun | the stage-entry check (task 0261, before only `houses` and `main quest start`) also runs with `dragonborn` |
| 11 | `Quest.SetActive` was an unregistered native | registered, no marker kept |
| 12 | `summons test kill` killed at the first chance, before the cultist could talk | `summons test kill = <editor id>@<combat second>` (test aid only) |

## 3. Tests

`apps/components_tests/skyrimse/testskyrimsedragonborn.cpp`, 15 tests, all new, none of the existing ones edited:

- Parser and creation (`SkyrimDragonbornParse` 1, `SkyrimDragonbornCreate` 4): written first, **2 of 6 failed** against the stub (the 4 that passed pin refusals and absence; the check that created references are not on top of each other was added with the code and not run red).
- The note (`SkyrimDragonbornNote` 2): **2 of 2 failed** (stub `onItemRead`, no event to created references).
- The reports (`SkyrimDragonbornReport` 2): **2 of 2 failed**.
- The greeter (`SkyrimDragonbornGreet` 3): **1 of 3 failed** (the two that passed are controls: a greeter that stands near asks at once; with the setting off nothing changes).
- Names (`SkyrimDragonbornNames` 1): constants only.

Not covered by a unit test: `SkyrimWorldView::startPackageQuest/addAliasPackageReference` (they need the dialogue data; the game run shows `PKTRACE ref ... has alias package DLC2CultistAmbushForcegreet`), the pack's attach of created references, `ensureOwner` for created references, the book tag hooks, the `@seconds` test-kill syntax and `Quest.SetActive` (the game run shows each).

Runs: `components-tests` `*Skyrim*:*Papyrus*:*Extension*:*Frozen*:*Follower*:*Package*:*Quest*:*Combat*:*Inventory*:*Journal*:*Dialogue*` 4,175 pass; `openmw-tests` `*Neutral*:*Extension*:*Lua*:*Skyrim*:*Combat*` 1,206 pass.

## 4. In game (`tools/skydragonborn_check/check.sh`, five masters, Windows, real mouse and key events through `OPENEDEN_RMLUI_INPUT_SCRIPT`)

New game at `@WhiterunWorld,25670,-7633`; MQ105 marked; `dragonborn = true`.

1. The CLOC event for Whiterun passes `DLC2CultistAmbushNode`; the quest starts with Location, CenterMarker (00029AAF), Cultist1 (FF800001), Cultist2, Note and Player filled: `PAPYRUS quest 0403546D alias Cultist1 holds the new 040365F6 FF800001`.
2. The player is moved next to the centre marker. The cultist runs up and the game's own forced greeting opens ("You there! You're the one they call Dragonborn?", choices "Yes, I am Dragonborn.", "The Greybeards seem to think so...", "I don't know what you're talking about.", "(Walk away)", Goodbye; frame `keep/ambush_greeting.png`), then "Your lies fall on deaf ears, Deceiver. The True Dragonborn comes... You are but his shadow." (`keep/ambush_turn2.png`) and a third line.
3. `TIF__0203546B` sets stage 1 and `TIF__0203545D` stage 10 when the box closes: the cultists leave `dunPrisonerFaction`, join `WEPlayerEnemy`, Aggression 1, the note goes into Cultist1's inventory, **`DLC2MQ01` starts and is set to stage 5** (objective 5 displayed, the Frea temple scene quest starts).
4. The test aid kills Cultist1 (`COMBAT test kill`); activating the corpse opens the loot window (`keep/loot_window.png`); **Take all** moves the Cultists' Orders: `OnContainerChanged` to `dlc2cultistordersscript` -> **DLC2MQ01 stage 7**.
5. The inventory (I) lists the note with a Read button; Read shows the game's text with the player's name and pronoun (`keep/orders_read.png`: "Kill the False Dragonborn known as the Dragonborn before he reaches Solstheim."); `OnRead` -> **stage 10** once the windows are closed (the host's tick waits while a window pauses the world, as for a conversation: task 0294).
6. The journal (J) lists **Dragonborn** (running): "I've been attacked by a group of people claiming to work for someone named Miraak ... look for the boat in Windhelm they used to travel from Solstheim.", objectives [x] Find out who sent the Cultists, [x] Read Cultists' Orders, [ ] Travel to Solstheim (`keep/journal_stage10.png`).

**Determinism:** three runs (`d1`, `d2`, `d3`) give 34 byte-identical lines of the filtered trace (DRAGONBORN, STORY started, holds the new, FORCEGREET, NTALK turns, dialogue lines, quest stages, the test kill, OnContainerChanged, OnRead, LOCKBOOKS read, INV take_all), md5 `0ba8d1c98245` (timestamps and poll numbers cut); a fourth run with the final input (windows closed before the journal) has the same lines. Frames: `I:\Projects\OpenEden-work\dragonborn0306\keep\`.

**Morrowind unchanged:** `tools/worldobjects_check` OK (110 objects) against `I:/Projects/OpenEden-work/integ2/build` (a build of `main`, not this commit's exact parent) and the fixed Balmora frame `1c5d47ad0b5dc32d` for both.

## 5. Not built / not shown (ranked)

1. **The ship from Windhelm (item 4) was not attempted**: Gjalund's `DLC2DialogueRRQuestScript.SetSail` needs fades (`ImageSpaceModifier.PopTo/Apply`), `Game.DisablePlayerControls`, `Actor.StopCombatAlarm`, `IsInDialogueWithPlayer`, the first-time boat scene (`FirstTimeTravel`, not read) and a Solstheim travel (`Game.FastTravel`, task 0223's plumbing). Stage 10's `Fragment_16` (`SetStage(20)`, for a player already on Solstheim) is gated correctly, so nothing past stage 10 ran.
2. The fight is a test aid (`summons test kill = ...@45`): the cultists' hostility (`WEPlayerEnemy`, Aggression 1) is set, but with `combat factions` on they did not engage the player in a run (they stood 300+ units away after the talk); the player's real attack key never reached them. Cultist2 follows Cultist1 (package 0x3546F).
3. Hand-written: the player's placement near the centre marker (a Lua teleport; the real game has the cultists find the player at the gate: a 4,600 unit run across the city was not shown, the navigator gave "no path" from the marker in this build), the spiral offsets (110 units, ours), the 380 unit greeting distance and 140 unit stand-off (ours), the `@seconds` kill time.
4. The CLOC event comes from the player's place when the host starts; a player already near the centre marker then gets none (the harness moves him after the quest started).
5. The Frea temple scene (`SCENE 04033262 not started: no speaker is loaded`) and the note's `enable(false)` (the note stays a world object with no model drawn) were not examined.
6. The speaker's name in the conversation box is the editor id (`DLC2LvlCultistNonHostileSummoner`: the actor has no FULL).

## 6. Decisions and test edits for toddhow

- No existing test edited. New settings: `[extension.skyrimse] dragonborn` (off) and the aid `dragonborn test prerequisites` (off), declared in `extension.json`, noted in `tools/testprofile/profile.cfg` and its README (pack settings: not set in the profile, like the other questline settings).
- `ObjectPlaceRequest` gained `allowActor` (default false; additive). `PlaceAtMe` now goes through `placeNear` (same behaviour; its body was moved).
- With `dragonborn` on: every ForceGreet package walks to the player first (this changes Brynjolf and Vilkas too when the setting is on; off by default), fragments follow their log entries' conditions, `SetAV Aggression` reaches the combat AI.
- The `@seconds` suffix of `summons test kill` is parsed inside the string (no new setting).
