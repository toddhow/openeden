# Task 0156: Skyrim condition functions the neutral world can answer (milestone M-S4)

Status: built (same hand wrote tests and code, no fresh reviewer; Windows only; local commits on branch `skycond`, not pushed).
Design doc section: `docs/design/skyrim-se-implementation-plan.md` M-S4; tasks 0122 (conditions engine and its names
follow-up), 0137 (dialogue), 0139 (quests), 0154 (actor numbers), 0155 (speech checks).

Goal: after 0122 the engine KNOWS every function in the five masters (no unknown condition is left); what is missing
is the WORLD behind it. `SkyrimConditionWorld::answers(c)` says whether a condition's answer comes from data and not
from a default 0 / false. This task makes the world answer, honestly, the functions that rank highest among the
conditions it still left unanswered, and keeps the rule: anything that cannot be answered honestly stays unanswered.

Inherited code touched: `game_packs/skyrimse` only (`dialogue.*`, `dialogue_runtime.*`, `speech.hpp`, `world_view.*`,
`skyrimse_game_pack.cpp`, `extension.json`); no engine, SDK or core file. New: `location.*`, `quest_script.*`.

## 1. Survey (the five masters; aggregates; `answers` as the world had it before this task)

Measured by the gated `SkyrimConditionStateRealData` (a stand-in player, the quests, the speaker Hulda in
`WhiterunBanneredMare`; the old `answers` is reproduced exactly by `oldAnswers`, which switches off only what this task
added). Condition counts per function and run-on come from an independent Python walker over the same files
(`tally.py`, not committed).

- **INFO:** 31,675 INFOs with conditions, 75,150 conditions; 17,762 INFOs (56.1 percent) had every condition answered
  from data, 23,841 conditions (31.7 percent) did not. Largest holes: fn_566 GetIsAliasRef 6,061, **fn_629
  GetVMQuestVariable 5,296**, fn_71 GetInFaction 972, fn_359 GetInCurrentLoc 864, fn_77 GetRandomPercent 844, fn_131
  GetPCIsSex 679, fn_46 GetDead 627, fn_47 GetItemCount 622, fn_606 594, fn_249 IsInDialogueWithPlayer 532, fn_579
  GetEquippedShout 529, fn_560 417, fn_562 360, fn_403 334, fn_286 323, fn_70 321, fn_310 287, fn_35 287, fn_130 281.
- **PACK:** 3,283 packages with conditions, 5,546 conditions; 1,772 packages fully answered, 2,093 conditions not.
  Largest: fn_629 667, fn_14 229, fn_362 HasLinkedRef 205, fn_35 109, fn_72 101, fn_1 82, fn_359 75, fn_46 66.

## 2. What is answered now, where the meaning comes from, what the stand-in world says

Meanings: names from xEdit's table (`survey_names_xedit.cpp`) and the CK wiki pages recorded in 0122's log; the three
the CK wiki did not settle are marked RECALLED-VERIFY.

| function | what the world answers | note |
| --- | --- | --- |
| fn_249 IsInDialogueWithPlayer | true for the speaker while the conversation is open (set by `DialogueRuntime`: open from the greeting, closed when it ends) | the package world never says, so it stays unanswered there |
| fn_77 GetRandomPercent | splitmix64 seeded per conversation by the placement (reference, else base); 0 to 99 | unseeded: 0 and unanswered (the package brain's world is unchanged: a random draw would make a package flip every evaluation) |
| fn_46 GetDead, fn_289 IsInCombat, fn_49 GetSleeping | speaker and stand-in player: alive, not in combat, awake | a corpse does not talk |
| fn_453 GetPlayerTeammate, fn_579 GetEquippedShout, fn_66 GetShouldAttack | speaker: no teammate, no shout, no wish to attack the target | nothing runs that could change it |
| fn_145 IsTrespassing, fn_144 trespass warnings, fn_402 IsBribedbyPlayer, fn_375 / fn_376 / fn_459 crime gold, fn_314 IsActorAVictim, fn_84 GetDeadCount | a fresh game with no crime: false / 0, nobody killed | stated, not computed; the stand-in game never has crime |
| fn_50 GetTalkedToPC | false at the greeting, true after the first turn | `DialogueRuntime` sets it |
| fn_640 GetActorValuePercent | health, magicka and stamina: 1 (full) for the speaker and the player | other actor values stay unanswered |
| fn_131 GetPCIsSex, fn_130 GetPCIsRace, fn_70 / fn_69 run on the player | the stand-in player: male Nord (`PlayerStandIn::mFemale`, `mRace` = NordRace 0x00013746) | new pack setting `player female` (declared in `extension.json`, default false); chargen would decide in a real game |
| fn_300 IsInInterior, fn_310 GetInWorldspace | `DialogueSpeaker::mInterior` from the cell; GetInWorldspace is false in an interior (CK wiki) | `SkyrimWorldView::beginConversation` fills them |
| fn_359 GetInCurrentLoc, fn_562 LocationHasKeyword | the cell's location (XLCN) against the LCTN records: the parent chain is walked (a child location counts as inside; RECALLED-VERIFY, CK wiki), keywords are the location's own | new `location.*`; a cell with no XLCN is "nowhere" (answered false) |
| fn_606 GetKeywordDataForLocation | 0: no script has stored data in a fresh game | answered, not computed |
| fn_629 GetVMQuestVariable | the value before any script ran (see 3); unanswered when it cannot be known | new `quest_script.*` |

Not answered, and why (largest first): fn_566 GetIsAliasRef (alias fills that the data cannot name: find-matching 3,118,
and specific-reference fills 1,134 only because the measurement gives the speaker no reference, in a game the view gives
one); fn_71 / fn_46 / fn_47 / fn_286 / fn_35 / fn_70 / fn_69 / fn_182 on OTHER references (run-on 2 and 5, 3: the world
knows the speaker and the player only; answering needs a table of every placed reference's base and initial state:
decision 1 below); fn_560, fn_403 GetRelationshipRank (the relations of 0154 are NPC to NPC through the SDK, not through
this world); fn_161 / fn_255 (the actor's current package: the conversation does not know it); fn_1 GetDistance (the
conversation has no positions); fn_149 (no weather given); fn_14 on packages (the package world has no player);
fn_362 HasLinkedRef and fn_612 (package brain data).

## 3. fn_629 from the script defaults (what is honest, what is not)

Papyrus does not run in a conversation, so the only honest value is the one the variable has BEFORE any script ran.
`QuestVariables::value(quest, name)` returns it when:
- the quest's QUST VMAD names a script (`parseVmadScripts`: scalar property values read, objects, strings and arrays
  skipped in a way that keeps the walk aligned), and that script or a parent declares the variable or an auto property
  of that name (the CIS2 string is either "Prop" or "::Prop_var" or a plain variable);
- the VMAD property value wins over the declared initial value; the type is int, float or bool;
- the script's `OnInit` function (and functions of the same script it calls on `self`) does not write it. A real game
  runs `OnInit` of start-enabled quests before any dialogue, so a variable it writes is NOT known and stays unanswered.
Writes made through another script's call into this script, and any other event handler, are not seen: no event runs in
this session either (decision 2 below).

Real data: 1,362 QUST records carry a VMAD script (2,274 quests after the merge; 0 damaged VMAD); 282 distinct scripts
were read from the masters' archives (0 missing). **5,262 of 5,296 INFO conditions and all 667 PACK conditions of fn_629
are answered now**; the 34 left are the civil war quest's counters and owners (`CW`, `OwnerRift`, `countHolds...`:
written by `OnInit`) and one name no script of the quest declares.

## 4. Result (INFO and PACK, the same real-data run)

| | before | after |
| --- | --- | --- |
| INFO lists fully answered | 17,762 of 31,675 (56.1 percent) | **22,452 (70.9 percent)** |
| INFO conditions unanswered | 23,841 (31.7 percent) | **12,920 (17.2 percent)** |
| PACK lists fully answered (package world: no player) | 1,772 of 3,283 | **2,391** |
| PACK conditions unanswered | 2,093 | **1,286** |

Per function (INFO, unanswered conditions): fn_629 5,296 to 34, fn_359 864 to 144, fn_77 844 to 0, fn_131 679 to 0,
fn_606 594 to 0, fn_249 532 to 0, fn_579 529 to 0, fn_562 360 to 0, fn_130 281 to 0, fn_84 143 to 0, fn_69 212 to 150,
fn_70 321 to 290, fn_310 287 to 0 (every Mare speaker is in an interior) (the rest are other references). PACK: fn_629 667 to 0, fn_359 75 to 37, fn_300 25 to 12.

The Bannered Mare's greeting (8 of its people; the same topic and the same INFO before and after for every one, with
the random numbers now drawn): infos ahead of the pick that hold an unanswered condition, before to after: Hulda 18 to
13 (of 76 walked), Saadia 23 to 13, Mikael 33 to 13, Jouane Manette 33 to 13, Orgnar 33 to 13, Ysolda 23 to 13, Sinmir
33 to 13, Delphine (UniqueVoicesHello) 1,566 to 729. The 13 left are fn_566 alias questions.

## 5. Decisions for toddhow

1. **Conditions on other references (run-on 2 and 5): about 2,700 INFO conditions** (GetInFaction 580, GetDead 403,
   GetItemCount 605, GetDisabled 299, IsSneaking 298, GetIsSex 182, GetEquipped 146 ...) need a table of every placed
   reference: its base, the initially-disabled flag, whether it starts dead. The pack's `loadCell` already reads
   references per cell; a persistent-reference table over all cells costs a second read of every plugin's references.
   Recommended as the next task if the greetings still choose wrongly in play.
2. **Quest variables are the values before scripts.** That is exact in this session because no Papyrus runs, but it is
   not what a game in progress would say; when the Papyrus runtime (task 0093) runs quest scripts, `QuestVariables`
   should be replaced by the runtime's heap.
3. **Wiring the package world.** The package brain's world (`SkyrimWorldView::conditionsPass`) does NOT get locations
   or quest variables here: 667 PACK conditions of fn_629 would then change NPC package choice in game (they are 0 now).
   The change is two setter calls; it is left off because NPC behaviour is another agent's lane and off-by-default
   settings guard it (`[extension.skyrimse] actor packages`).
4. **The stand-in player is a male Nord** (setting `player female`); chargen would decide. No race setting.
5. The random numbers are seeded by the placement: the same actor greets the same way each time.

## Stage log

2026-11-12, Windows only, local commits on branch `skycond` (from `main` at `6048640cfe`), not pushed. The same hand
wrote tests and code; no fresh reviewer; an advisor was NOT consulted. Own build tree `I:\Projects\OpenEden-work\skycond`
(RelWithDebInfo without debug info).

- **Tests.** `testskyrimseconditionstate.cpp`, 23 tests: the world state (12), the location records and index (3),
  the VMAD reader (2), the quest script index (1), the variable resolver (5) and the world answering fn_629. Hand-built
  `survey::Ctda`, generated LCTN and QUST records, `.pex` made with the test assembler (`pexassembler.hpp`). **Not run
  red against stubs first**: `location.cpp` and `quest_script.cpp` were written before their tests; the world tests
  cannot compile against the old headers (no setters). Instead three single-line mutations, built, each failed named
  tests: `isWithin` not walking the parent (`SkyrimLocations.theParentChain...` and the location world test), the
  OnInit write check turned off (`SkyrimQuestVariables.aVariableThatOnInitWritesIsNotAnswered`), and
  `inDialogueWithPlayer` ignoring the conversation flag (`SkyrimConditionState.theSpeakerIsInDialogue...`); all restored.
  `components-tests` `Skyrim*:NifScene*:Conditions*` pass: 1,102 tests with the gated real-data tests run on the installed masters (1,069 without).
  No existing test was edited.
- **Real data** (`SkyrimConditionStateRealData`, gated; prints counts, function indexes and the editor ids of quests
  whose variables stay unanswered): see sections 1 to 4. A real bug found by the measurement: the first "before" used the
  new `answers`, so the numbers were not the old ones; `oldAnswers` switches off exactly the functions added here.
- **Morrowind unchanged by construction**: nothing under `apps/`, `components/`, `sdk/` or `files/` changed (only a
  test list); `tools/worldobjects_check` and the Balmora frame were NOT run (no executable of this tree was built:
  `openmw` was not built here).
- **Not run:** the Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua tests, `openmw-tests`, the engine
  in game (no `openmw` built in this tree; `DialogueRuntime` and `beginConversation` are exercised by the pack tests
  only), a fresh review. `CI/check_*.sh` (tests registered, layer rules 104, SDK, conventions, no OSG) pass;
  `check_file_names` lists two files of other sessions.
