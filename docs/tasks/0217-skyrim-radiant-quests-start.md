# Task: radiant quests that start by themselves and read right in the journal (milestone M-S6)
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (quests, story manager, radiant quests); finishes the open
items of task 0211 (`docs/tasks/0211-skyrim-story-events-and-radiant-quests.md`, "Not built / not run").
Goal: the story manager's walk reaches the radiant nodes (favours, world interaction, bounty) so that a quest starts by
itself from an event, the journal names what fills an alias, and the story manager's state is in the saves.
Status: BUILT on Windows (2026-11-16); same hand wrote tests and code; no fresh reviewer; local commits on branch `radiant2`
from `122265c303`, not pushed. Everything is behind `[extension.skyrimse] papyrus` (off by default).

Sources allowed for meanings (toddhow): xEdit (`wbDefinitionsTES5`), the Creation Kit wiki (unreachable from this machine),
CommonLibSSE. Every rule that comes from memory is RECALLED-VERIFY; what the data settles is marked VERIFIED.

## 1. What was wrong (found with the real data and a trace)

1. **The node flags were read from the wrong bits.** The story manager's DNAM is two 16 bit halves (xEdit: "Node Flags" u16,
   then "Quest Flags" u16). The pack read "shares event" from 0x2 and "do all quests before repeating" from 0x1. VERIFIED on the
   five masters (aggregates, a Python reader of the records; task scratch only): every one of the 27 quest nodes whose editor id
   says "shares" has 0x20000; 0x40000 is set on exactly the 18 nodes that have an MNAM; 0x1 is set on 77 quest nodes that name
   several quests (the radiant BQ and WIChangeLocation nodes are 0x30001: random, do all, shares). So **no node ever shared the
   event**: the walk ended at the first quest it started. Now `StoryFlags`: random 0x1 (children, and a quest node's quests),
   do all 0x10000, shares 0x20000, number to run 0x40000.
2. **The children of an event node were in form id order** because the sibling chains of the plugins do not form one chain
   (VERIFIED: under the CLOC event node two nodes have SNAM 0, Skyrim.esm's WIChangeLocationNode and Dragonborn's
   DLC2CultistAmbushNode, and two nodes name CWGarrisonNode). The id order put `QuestNode` and its Dawnguard child
   `DLC1ScrollHandlingChangeLocNode` (flags 0x200, shares nothing) ahead of the BQ nodes. Now the children are the forest of
   the chains in pre-order: heads ascending (the master's chain first), at a fork the lower form id first (RECALLED-VERIFY: the
   engine's own order is not known).
3. **The alias filler's dry run cleared the world's story event.** `QuestAliasFiller::canStart` evaluates conditions on candidate
   locations and actors and sets the quest world's event to none and its speaker to the candidate. The first quest the walk
   tried therefore blinded every later node: all `GetEventData` (fn_576) conditions read "no event" and failed, which is why
   Favors, BQ and every `L2 has keyword` node failed after the first quest tried. The walk now puts the event back after the
   dry run and resets the speaker and subject location before each evaluation (`world_view.cpp`, the `mStory` function).
4. `GetKeywordDataForLocation` (fn_606) was already in the global table (task 0156 follow-ups) answering 0; it now reads what
   scripts store (`Location.SetKeywordData`), as does fn_651 for the current location.

## 2. Built

- `story.*`: the flags, the forest order, quest nodes with the random bit (a seeded shuffle of the node's quests, the event
  counter is the seed), `saveState`/`restoreState`, `setTrace` (one line per node and quest, a failed list is re-evaluated one
  condition at a time to name the function: `STORY node quest 000CBAA2 BQWhiterunNodeSHARES passes`).
- `story_state.*` (new): `LocationKeywordData` (location, keyword, float; capped at 65,536), `StoryState` and its canonical byte
  body (u64 event counter; the "do all" rotations; the ambient quests that ran; the keyword data; every list sorted, caps, every
  refusal located by byte), the save chunk `story` v1 (own chunk: the Papyrus chunk is strict about its version).
- Papyrus natives (`papyrus_host.cpp`): `Location.SetKeywordData`, `Location.GetKeywordData` (RECALLED-VERIFY signatures),
  `LocationAlias.GetLocation`, `ForceLocationTo`, `Clear` (a location alias keeps its location in the fill's reference word,
  decision 5 of task 0211), `Alias.RegisterForSingleUpdate/RegisterForUpdate/UnregisterForUpdate`,
  `Form.RegisterForAnimationEvent` (accepted, nothing raises animation events), `ObjectReference.GetCurrentDestructionStage`
  (0), `Debug.OpenUserLog` (true), `ObjectReference.GetCurrentLocation` and `GetEditorLocation` (the location of the cell of a
  placed reference; the player's site for 0x14). The pack data names an LCTN form a `Location`
  (`PackPapyrusData::formClass`): before, a property typed Location was a plain Form.
- Conditions: `SkyrimConditionWorld::keywordDataForLocation` and `keywordDataForCurrentLocation` (fn_606, fn_651) over the store.
- Journal: `<Alias=X>` and `<Alias.ShortName=X>` become the display name of what fills the alias X (`journal::expandTags(text,
  resolver)`, `Inputs::mAliasName`, `Access::mAliasName`); an unfilled alias or a nameless filler keeps `...`; no resolver keeps
  the alias name as before. The name of a reference is its base record's FULL (`displayNameOf`), of a location the LCTN's FULL
  (`LocationRecord::mName`, new; `LocationIndex::add` numbers the plugins the way the other indexes do).
- Developer aids: `[extension.skyrimse] story trace` (default false: the walk's lines, the dry run's blocking aliases, and for
  every quest the manager started a line `STORY started <id> <editor id> by CLOC: <alias>=<id>(<name>) ...`).

## 3. Tests

Written first and run against stubs / the old code: `apps/components_tests/skyrimse/testskyrimseradiant2.cpp` (new file, the
test names start `SkyrimRadiant2`). **Red run: 18 of the first 27 failed** (against the old story code and stubs of
the new classes: the 5 walk tests that pin the data's flags and the forest order, 3 keyword data tests, 2 condition tests, 2
natives tests, 2 journal tests, 4 state tests); the 9 that passed test absence or defaults (a null form or a NaN stores nothing, no
store answers 0, a hostile count and every truncation are refused by a stub that refuses everything, "no resolver keeps the alias
name", an unfilled location alias gives none, and the random quest node and the random do-all node, which the old code passed by
reading bit 0 as "do all": the first was strengthened afterwards with "not file order", reasoned red, not run red). Then three
natives tests written first for the extra natives (2 of 3 red, the third pins absence). The two gated real-data tests were
written with the code (the second red/green against the old walk: before the flag fix and the event restore the walk chose
`DLC1ScrollHandlingChangeLoc` alone). Final: **30 new unit tests and 2 gated real-data tests pass** (they print the official
editor ids the walk tries, assert that Whiterun has the keyword the nodes ask for, and that arriving from outside starts a favour).
The area subset (`Skyrim*`, `Papyrus*`, `Vmad*`, `Dialogue*`, `Quest*`, `Story*`, `Scene*`, `Frozen*`, `Extension*`, `Location*`,
`Radiant*`, `Journal*`, `Neutral*`, `Lua*`, `Condition*`) 2,926 passed before the later additions. **No existing test was
edited.**

## 4. In game (five masters, Bannered Mare start, `neutral actors`, `papyrus`, `story trace`, fixed frame time, quiet window,
`[Sound] device = No Device`, `master volume = 0.0`, `music volume = 0.0`; scenario `e2terrain` of `tools/neutral_session_check`)

Started by the story manager, NOT by `story test quest` (the lines are the log's, `STORY started ...`):
- Leaving the Mare (CLOC to the Mare, then Mare to Whiterun): `WIChangeLocation01`, `DialogueWhiterunArgueScene`
  (Olfrid, Idolaf, Fralia, Olfina, Commander Caius, Jon filled), `BardSongStarter` (Location = The Bannered Mare, Karita,
  Lurbuk), `DLC1ScrollHandlingChangeLoc`, `DialogueWhiterunHeimskrPreachScene` (Heimskr), **`WITavern`** (a world-interaction
  radiant quest: Location = The Bannered Mare, Innkeeper = Hulda, Server1 = Saadia, Bard = Mikael, Patron1 = Brenuin, Patron2 =
  Sinmir, Patron3 = Lucia, Player), `WIGreeting`; then **`WIChangeLocation08`** (NewLocation = Whiterun, five hide markers).
  Before this task the walk stopped at `DLC1ScrollHandlingChangeLoc` (or `WIChangeLocation01`).
- Walking out onto the plains and back (the scenario's first hop east, no location left): **`Favor013`** and **`Favor017`**
  started by `FavorChangeLocationWhiterun` with Location = Whiterun (the favour node needs the location left to have no
  "dwelling" keyword, so leaving the inn does not start favours: that is the data's own condition, `L1 has LocTypeDwelling ==
  0`). Their other aliases (the giver and the item holder) are find-matching references: not filled at the start.
- The journal with a quest that has text (`DLC1RV02`, started by the aid `story test quest = 20050b9:10` because the quests the
  walk starts have no log text at their start): the frame
  `I:\Projects\OpenEden-work\combat5\keep217\journal_names.png` (viewed) reads "Fura Bloodmouth has sent me to
  destroy the ... at Bloodlet Throne." and the objective "Kill the ... at Bloodlet Throne" (QuestGiver and Dungeon name what
  fills them; Boss stays `...`: the dungeon the filler found lists no boss reference). Before: "QuestGiver has sent me to
  destroy the Boss at Dungeon".
- **Save and load** (`saveload_story.sh`, a copy of `tools/neutral_session_check/saveload.sh` that saves at 48 s real time so the
  story manager exists): the loading process logs `STORY restored: event 25, 8 rotations, 1 ambient quests, 0 keyword data`.

(Determinism, papyrus off and Morrowind: see section 6.)

## 5. Not built / not run

- **The bounty quests (BQ01 to BQ04) start no quest yet**: their node passes (`BQWhiterunNodeSHARES passes`) but the quests are
  blocked by three aliases the filler cannot fill: `Hold` (a location "in the PARENT of alias 3 with the keyword
  LocTypeHold": the pack treats an in-location alias as a CHILD search and does not read the alias's KNAM keyword at all),
  `BountyLocation` (its conditions call fn_181 `HasSameEditorLocAsRefAlias`, not implemented, so the list fails closed) and
  `Bounty` (a reference in that location). The data shows both directions exist (the alias named `Hold` 26 times, `Habitation`,
  `City`, `Dungeon`, `Location`... all with ALFA and KNAM): a symmetric search (ancestors and descendants) with the keyword
  filter is the likely rule, not verified. Left for a task of its own (decision 1).
- Other cheap natives: `ObjectReference.BlockActivation` (6 calls), `Actor.IsInFaction` (6), `SetFactionRank` (6), `AddPerk`
  (7), `HasSpell`, `EvaluatePackage`, `Is3DLoaded`, `SetDestroyed`, `ClearDestruction`, `Lock`, `AddToMap`,
  `GetTriggerObjectCount`, `RegisterForTrackedStatsEvent` stay tolerant: they change game state this build does not model.
  Tolerant calls in the e2 scenario run: 121 before (19 natives) and see section 6 after.
- The Linux gate and GCC `-Werror` (hand-checked for the traps), clang-format 14 (formatted by hand to 120 columns),
  clang-tidy, the Lua tests, `openmw-tests` (no engine file changed; only built, not run), a fresh review.
- A quest the walk starts is not shown in the journal until a script sets a stage with text (as in the game); a natural
  in-game journal frame with a walk-started quest could not be made for that reason.

## 6. Checks and numbers

- `components-tests` area subset (filter in section 3) 2,929 pass, 0 fail (Windows, built with `-j 8`, no existing test edited);
  `SkyrimRadiant2*` 32 pass with `OPENEDEN_SKYRIMSE_DATA` set. `openmw-tests` was not run (no engine file changed).
- The walk, on the five masters (`SkyrimRadiant2Real`): leaving the Mare into Whiterun chooses `WIChangeLocation01`,
  `DLC1ScrollHandlingChangeLoc`, `DialogueWhiterunHeimskrPreachScene` (before: `DLC1ScrollHandlingChangeLoc` alone); arriving at
  Whiterun from no location chooses `WIChangeLocation08`, `Favor013`, `Favor017`, `DLC1ScrollHandlingChangeLoc`,
  `DialogueWhiterunHeimskrPreachScene`. (Task 0211's "can start" counts are unchanged: the dry run is the same function.)
- **Three runs** (`e2terrain`, 11,000 frames, `story trace`): the story manager's events and the quests it started (`STORY event`
  and `STORY started` lines other than the ambient ADIA ones, timestamps cut) have the same md5 in all three runs
  (`de9ab13c1fb3`), and so do the 19 `PAPYRUS story event ... started quest` and `started with N aliases filled` lines (`3891dc86c192`).
  The ambient (ADIA) events tried differ from run to run in WHICH pair of actors is asked (the actors the core tells the pack
  about arrive in a timing dependent order: older, task 0194; first seen here because the trace prints each one); what they start
  is the same in all three runs.
- **Papyrus off** (`papyrus = false`, same scenario): 0 `PAPYRUS` and 0 `STORY` lines, 0 crash dumps.
- **Morrowind unchanged**: `tools/worldobjects_check` 110 result lines OK against the same build, and the fixed Balmora frame
  (`--start Balmora --random-seed 7`, frame 600) has the SHA-256 prefix `1c5d47ad0b5dc32d` for the reference run, the new build and
  the new build with the neutral settings on (the same value as the rows before). No Morrowind file changed.
- Tolerant natives, the `e2terrain` scenario: **before** (this task's first game build, before the new natives; `run_r2c`, 144 s of script time, with
  the `story test quest` aid on) 19 natives and 121 calls (`form.registerforanimationevent` 30, `objectreference.getcurrentlocation`
  28, `debug.openuserlog` 11, `getcurrentdestructionstage` 9, `actor.addperk` 7, `actor.isinfaction` 6, `setfactionrank` 6,
  `alias.registerforupdate` 6, `blockactivation` 6 ...); **after** (`run_f217_1`, the same scenario without the aid, about 70 s of
  script time) 12 natives and 35 calls: the five natives above are answered, what is left is `actor.addperk` 7,
  `actor.isinfaction` 6, `actor.setfactionrank` 6, `objectreference.blockactivation` 6 and six others (listed in section 5). The
  two runs differ in length and in the aid, so the numbers show which natives went, not an exact ratio.
- Layer exceptions 104 (unchanged), `check_tests_registered` ok, `tools/testprofile/coverage.sh` OK (the new `story trace` is a
  developer trace, not a feature).

## 7. Decisions for toddhow

1. **Bounty quests (BQ) and the in-location aliases**: implement KNAM (the matching keyword) and the ancestor search, and fn_181,
   in a task of their own? The alternative is to leave radiant bounties off until the dialogue that offers them exists.
2. **The order of the children of one node** (heads ascending, forks by form id) is mine; if the game's order is known to you,
   the `StoryIndex::rebuild` rule is the one place.
3. **The favour nodes only fire for an arrival from a place that is not a dwelling** (the data's own condition). Leaving an inn
   or a house therefore starts WI quests but no favours; if you want favours when leaving a house, that needs the game's
   `Location Changed` semantics (what the player's previous location is while he is inside) to be checked.
4. **One chunk, `story`** (v1), holds the manager's state, the ambient set and the keyword data; the Papyrus chunk is unchanged.
5. **Small edits in shared files** (other agents work there): `papyrus_host.hpp/.cpp` (natives and two setters),
   `dialogue.hpp/.cpp` (fn_606/651 and `setKeywordData`), `dialogue_runtime.hpp` (one forwarder), `location.*` (FULL, plugin),
   `world_view.*` (access, alias names, story state, the walk's fix), `quest_alias.*` (a trace only), `journal.*`,
   `skyrimse_game_pack.cpp` (the chunk, the data class), `extension.json` (one setting), the two CMake lists. No `combat*`
   file and no engine file changed.
