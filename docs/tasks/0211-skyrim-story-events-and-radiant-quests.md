# Task: Skyrim story events the world can raise, radiant quests and scene state in saves (milestone M-S6)
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (quests, story manager, radiant quests); tasks 0187 and 0194
(their "not built" lists are this task's list).
Goal: the story manager gets the events the neutral world can now produce (KILL, exterior location changes, LEVL), quest
aliases that find their match in a location or among loaded references can be filled, and a scene that plays at a save resumes
at its phase after a load.
Status: DRAFT, written first (2026-11-16). The stage log is at the end. Same hand writes tests and code; no fresh reviewer.
Everything is behind `[extension.skyrimse] papyrus` (off by default); the engine half is inert without a pack that implements
the new interface.

Sources allowed for meanings (toddhow): the Creation Kit wiki (unreachable from this machine), xEdit, CommonLibSSE; nothing
from Havok. Every rule that comes from memory is RECALLED-VERIFY and a named option or a counted choice.

## 1. Survey (the five masters, `SkyrimRadiantSurvey`, aggregates; file and function at `613d25f588`)

- **Where the events go.** `PapyrusDriver::raiseLocationChange` (`game_packs/skyrimse/src/skyrimse_game_pack.cpp`) raises CLOC
  for an INTERIOR cell the engine builds; `SkyrimWorldView::papyrusAccess().mStory` walks the story manager
  (`world_view.cpp`, `StoryManager::raise`); `PapyrusHost::raiseStory` starts the chosen quests (`papyrus_host.cpp`). LEVL is
  already raised by `hooks.mLevelUp` of `startProgression` (task 0200) through `host().raiseStoryEvent`. Deaths already reach
  `PapyrusHost::onActorDied` from the death sink of `ScriptCombatBridge` (`papyrus_world.*`, installed in `startPapyrus`), so
  KILL needs no change in `combat*`: it is one more call in that sink's lambda.
- **Quests by event (ENAM):** none 870, ADIA 900, SCPT 262, CLOC 172, KILL 15, CRFT 11, LEVL 8, CAST 5, REMP 5, AIPL 5, ASSU 5,
  JAIL 3, AHEL, ARRT, CHRR, DEAD, ESJA, SKIL 2 each, NVPE 1.
- **KILL's members (VERIFIED on the 8 KILL quests that have event aliases):** the alias of the victim reads event member 'R1'
  in all 8 (`Victim`, `DeadFriend`, `Criminal` of `MGSuspensionQuest` is 'R2' and its `Victim` 'R1'), the alias of the killer or
  attacker reads 'R2' in all 8, `WIKill06`'s location alias reads 'L1'. So KILL is: R1 the dead actor, R2 the killer (the
  player is reference 0x14), L1 the location of the death.
- **Exterior locations.** Only 1,907 of 75,776 exterior CELL records have an XLCN (407 distinct locations; 820 of 929 interior
  cells do). 403 of 831 LCTN records carry a list of exterior cells (LCEC, 413 subrecords; ACEC 9, RCEC 1). That list is how the
  game finds the location of most exterior squares, so the exterior CLOC maps (world, grid) to a location through LCEC when the
  cell has no XLCN. Sizes: LCEC world(4) + 4 bytes a cell (Y i16, X i16); LCSR 16 bytes (reference type, reference, world,
  grid; 664 records); LCPR 12 bytes (reference, world, grid; 319); LCUN 12 bytes (actor, reference, location; 148); the AC*
  and RC* families have the same layouts (xEdit's wbDefinitionsTES5; divisibility and id resolution are checked by the real
  data test below).
- **Alias kinds in all quests:** reference aliases: find matching 2,054, from event 2,053, in location 2,320,
  near alias 353, external alias 152, created object 941, specific reference 3,122, unique actor 3,519; location aliases:
  find matching 476, from event 463, in location (a child) 54, specific location 240, external 4. Quests that have a
  find-matching LOCATION alias: 199; of those, with a reference alias that finds its match inside a location: 99.
- **Conditions run on a candidate location** (location aliases, 2 or more occurrences): fn_562 LocationHasKeyword 265, fn_563
  LocationHasRefType 91, fn_181 HasSameEditorLocAsRefAlias 88, fn_565 GetIsEditorLocation 72, fn_605 LocAliasIsLocation 14,
  fn_651 12, fn_359 GetInCurrentLoc 10, fn_136 8, fn_360 GetInCurrentLocAlias 7, fn_444 5. Only 562 is in the global table
  (and answers for the speaker's location, not for a candidate location); 563 and 565 are new.
- **What the quests are** (editor ids only): the "radiant-like" set (find-matching location plus in-location reference) holds
  BQ01 to BQ04, the Companions' CR02 to CR14, the Dark Brotherhood's DA09, DA10, the Dawnguard's DLC1RH01 to 08 and DLC1RV01
  to 09, `Favor001` to `Favor205`, `FreeformXxx`, the College's MGR*, the Thieves Guild's TGR*, `WE100`, `WERoad06`, the
  `WIKill*` and `WIChangeLocation05`, `JailQuest`, `BYOHHouse*Attack`. `WIKill01..06` are KILL quests whose aliases need a
  relative or a friend of the dead (`HasFamilyRelationship`, voice type, relationship conditions).
- **What is not built before this task** (tasks 0187 and 0194): exterior CLOC, KILL, find-matching aliases among loaded
  references (they took the first reference in id order of ALL placed references), every location alias, aliases in a location,
  external aliases, the start gate of the story manager for anything but ADIA, and scene state in saves.

## 2. Design

### 2.1 Story events (stage A)
- `story_events.*` (pure): `makeKillEvent(victim, killer, location)`, `locationOfPlace(place, locations)` (XLCN of the cell,
  else the location whose LCEC lists the grid square with the fewest cells, ties to the lower id: RECALLED-VERIFY, the data
  does not say), `LocationChange` (remembers the last location; a move to nowhere is remembered and raises nothing, so coming
  back raises; the interior-only version of task 0187 lost the memory of the place in between).
- KILL: the death sink queues the event; the driver raises it in its next `frame()` (not inside the combat update: starting a
  quest runs scripts).
- Exterior CLOC: a NEW SDK interface `openeden/game/player_place.hpp` (`PlayerPlaceSink::onPlayerPlace(PlayerPlace)`, DRAFT 0.1,
  found by `dynamic_cast` on the registered `ActorRules` like `SceneSpeechSource`). The task asked for
  `ActorRequestWriter::playerPosition`; it cannot do the job: the actor bridge's `update()` returns before it calls the pack
  when no neutral actor exists (`NeutralActorBridge::update`, `mEntries.empty()`), which is the case in the wild, and a
  position says neither which worldspace nor whether the player is in an interior. The core reports the PLACE (interior cell
  id, or worldspace and grid square) when it changes: one call site, `World::doPhysics` (`apps/openmw/mwworld/worldimp.cpp`),
  through `PhysicsSystem::notifyNeutralPlayerPlace` and the pure `PlayerPlaceFilter`. Decision for toddhow (section 6).
- `CLOC` no longer needs `cellBuilt`: the NeutralWorldStore builds an exterior cell once per session, so the pack's `cellBuilt`
  fires once per cell, not per visit (reused cells are not rebuilt).

### 2.2 Radiant quests (stage B)
- `LocationRecord` gains the lists above; `LocationIndex` answers `locationOfGrid`, `gridOf`, `specialRefsOfType`, `ids`.
- `SkyrimConditionWorld::setSubjectLocation` makes "run on subject" mean the candidate location; fn_562 asks it, fn_563 and
  fn_565 are extra functions (`location_conditions.*`; the evaluator of the pack's quests uses `storyAndLocationExtraFunctions`).
- `QuestAliasFiller` (rules in its header) fills: SpecificLocation, FromEvent (R and L members), FindMatching locations
  (closest to the player's place, conditions on each candidate), locations inside another alias's location, references in a
  location's lists (by reference type), ExternalAlias, and FindMatching references closest-first and in the loaded area when
  the alias says so. A location alias keeps its location in `AliasFill::mReference` (no save format change: `papyrus_host`
  stores the same three words). `canStart` is the dry-run gate: a required alias of an answered kind that cannot fill blocks the
  start. Created-object and near aliases are counted and never block.
- The story manager's `Startable` gate (`world_view.cpp`, `papyrusAccess().mStory`) asks `canStart` for every event type, not
  only ADIA. This changes which quests start for CLOC and SCPT (a quest whose required find-matching alias finds nothing used
  to start with the alias empty). That is how the game behaves; the Bannered Mare scenarios are compared against the parent.
- "Can start", the number this task reports (`SkyrimRadiantReal`): with the player placed in the Bannered Mare (interior) and,
  second, at Whiterun's gate (exterior), the quests with an ENAM (or any quest with a find-matching location alias) for which
  `canStart` is true with a synthetic event of the quest's own type carrying a placed reference of the world as R1 and a
  location as L2 (so event aliases do not decide the count; they are listed apart). Created-object aliases count as filled
  (a script creates them with PlaceAtMe; they are listed). Before the task: the same count with the old filler (only data kinds
  filled, no location aliases).

### 2.3 Scenes in saves (stage C)
- `SceneRunner::saveState/restoreState/stopAll`, the bytes `encodeSceneState/decodeSceneState` (version 1, caps, canonical), a
  SEPARATE save chunk `papyrus.scenes` (the Papyrus chunk is strict about version and size, so adding to it would refuse old
  saves). The driver holds the bytes and applies them in its next `frame()` after the host restored (chunk read order is not
  ours to control); a load while the host runs first stops the scenes that play. A scene resumes at its phase (entered or not);
  finished actions stay finished; a running line or timer starts again (elapsed times are not saved).

## 3. Stages and acceptance
- [ ] A1 KILL raised on a death (victim R1, killer R2 with the player as 0x14, location L1); quests with required event aliases
  start only when the aliases match. A2 exterior CLOC. A3 LEVL reaches the story manager (a test aid shows it in game).
- [ ] B1 location lists decoded, B2 location aliases and references in locations, B3 closest and loaded-area matching, B4 the
  start gate, B5 the counts, B6 one radiant quest started in game with its journal entry (frames).
- [ ] C scene save and resume (unit tests and a save/load in game).
- [ ] With `papyrus` off the traces equal the parent's; three runs identical; Morrowind unchanged (`worldobjects_check`, the fixed
  Balmora frame).

## 4. Test plan
Red first against stubs for: scene save (`testskyrimsescenesave.cpp`), the location lists (`testskyrimselocationlists.cpp`), the
location subject conditions (`testskyrimselocationsubject.cpp`), the story events (`testskyrimsestoryevents.cpp`), the alias
filler (`testskyrimseradiantfill.cpp`), the engine's place filter (`testneutralplayerplace.cpp`). The survey
(`testskyrimseradiantsurvey.cpp`) and the real-data check are gated on `OPENEDEN_SKYRIMSE_DATA`.

## 5. Stage log (2026-11-16; same hand wrote tests and code; Windows only; local commits on branch `radiant` from `613d25f588`, not pushed)

**Built.** All of section 2: `story_events.*`, `location.*` (lists and index), `location_conditions.*` (563, 565 and the
location subject), `quest_alias.*` rewritten around a plan (location aliases, references in a location, external aliases,
closest-first, loaded area, the dry-run `canStart`), `scene_runner.*` (`saveState`, `restoreState`, `stopAll`, the byte codec),
the SDK `player_place.hpp`, `PlayerPlaceFilter` and the one engine call site, the pack driver (KILL queue, place to CLOC, the
scenes chunk, two developer aids). Everything is behind `[extension.skyrimse] papyrus`; the engine half is inert without a pack
that implements `PlayerPlaceSink`.

**Tests first.** New files `testskyrimsescenesave.cpp` (14), `testskyrimselocationlists.cpp` (9), `testskyrimselocationsubject.cpp`
(6 + 2 written after the code), `testskyrimsestoryevents.cpp` (10), `testskyrimseradiantfill.cpp` (29), engine
`testneutralplayerplace.cpp` (6). Against stubs: **60 of 73 failed red** (scene save 10 of 14, location lists and index 9 of 9,
location subject 5 of 6, story events 9 of 10, alias filler 21 of 28, place filter 5 of 6); the 13 that passed test refusal or
absence (a 4 byte empty state, a hostile count, an unresolved id) or were vacuous against a stub that returns "true"
(`canStart`). Added after the red run: the interior anchor test (red-first not recorded), the story-player tests, and the tests
that replaced three of my own (`canStart` for a find-matching REFERENCE, see decision 3). Green: `Skyrim*:*Papyrus*:*Vmad*:
*Dialogue*:*Quest*:*Story*:*Scene*:*Frozen*:*Extension*:*Location*:*Radiant*` 2,503 and `openmw-tests`
`*Neutral*:*Skyrim*:*Extension*:*Lua*` 835 pass. **No existing test was edited.**

**Data settled (SkyrimRadiantSurvey, five masters).** LCEC order is Y then X: of 1,907 exterior cells with an XLCN, 1,487 are
listed by their own location or a descendant in that order, 0 in the swapped order, 420 by neither (the cell's XLCN names a
location that lists other cells). LCTN list sizes divide exactly (LCEC world + 4n, LCSR 16, LCPR 12, LCUN 12; 0 odd subrecords in
the real test's parse). KILL: R1 victim, R2 killer, L1 location, in all 8 KILL quests that name members.

**Counts (`SkyrimRadiantReal`, Bannered Mare as the player's place, a synthetic event of each quest's own type with a placed
actor as R1, the player as R2 and the Mare's location as L1 and L2; 37 ms for 1,254 dry runs).** 1,425 quests have an event or a
find-matching location alias; 1,292 of them have a required alias of a blocking kind. **386 can start**, 1,039 are blocked
(ADIA 872, the rest mostly quests whose required event alias does not match this event). By event: CLOC 119 of 172, SCPT 172 of
262, KILL 7 of 15, LEVL 8 of 8, CRFT 11 of 11, ADIA 28 of 900. Of the 199 quests with a find-matching LOCATION alias, **107 can
start** (before this task: 0, no location alias was ever filled). The old filler filled 517 aliases at the host start; the new one
646 (location aliases of the quests running at the start).

**In game** (`rad.sh`, five masters, Bannered Mare, `neutral actors`, `papyrus`, `actor packages`, fixed frame time, quiet window,
`[Sound] device = No Device` and `master volume = 0.0`):
- **KILL.** `n5death` with `combat test player swing = 2`: `PAPYRUS death of 0001A66E killer 00000014` then `story event KILL R1
  0001A66E R2 00000014 L1 ... started 0 quests` (no KILL quest matches Hulda's death; the event reaches the story manager).
- **Exterior CLOC.** `e2terrain` (the player leaves the Mare into Whiterun): `story event CLOC to location 0001F86D` (the Mare)
  and then `CLOC to location 00018A56` (the exterior square, by the locations' own cell lists) `started 1 quests` (quest
  02012F92, `DLC1ScrollHandlingChangeLoc`). Before this task nothing was raised for an exterior place.
- **LEVL.** Unchanged: raised by `startProgression` (task 0200) through `raiseStoryEvent`, 8 of 8 LEVL quests pass the gate in the
  count above. Not seen in game here (no level up scenario was run).
- **A radiant quest.** `DLC1RV02` "Culling the Beast" (Dawnguard hunter, kill the boss of a dungeon) started with the developer aid
  `story test quest = 20050b9:10` and `journal = true`: **4 of 4 aliases filled** (QuestGiver the Dawnguard NPC, Dungeon the
  location `00016F85` (the closest dungeon-keyword location to the Mare, found by the location lists), Boss the reference
  `0005B824` placed in that dungeon (base `0005B830`), MapMarker), and the journal shows "Culling the Beast, running, QuestGiver
  has sent me to destroy the Boss at Dungeon. Objectives [ ] Kill the Boss at Dungeon"
  (`I:\Projects\OpenEden-work\combat5\run_rv\user\screenshots\screenshot001.png`, viewed). The journal prints the alias
  NAMES (task 0185's tag rule), not the names of what fills them. **The quest was started by the test aid, not by the story
  manager's walk**: the walk at Whiterun chooses `DLC1ScrollHandlingChangeLoc` first and ends (see "Not built"), and the SCPT
  starts of radiant quests come from dialogue fragments of givers the Mare does not have.
- **Scene resume.** `papyrusdemo.sh` (n7 save and load, save at simulation time 46 while the ambient scene `0001B113` plays): the
  saved run began the scene and was in phase 1; the loaded run logs `SCENE 0001B113 resumes at phase 1`, `SCENE restored 1 scenes,
  dropped 0`, says line 2 again (a running line starts again), then phases 2 to 6 and `ends`.
- **Papyrus on, the Bannered Mare, 6,000 frames.** Compared with the parent (`613d25f588`, same scenario): the same start-up lines,
  the same `story event CLOC` (quest 00037CC2) and ADIA (quest 0001B112) and the same scene lines; the differences are the order of
  two `cell ... loaded` lines, subtitle frame numbers (real time) and `aliases filled 646` against 517. **Three runs**: after the
  frame numbers, timing summaries and cell lines are cut, runs 2 and 3 have the same md5 (188 lines); run 1 is a prefix of them
  (the host started later in real time, the run ended before the scene's last two lines).
- **Papyrus off** (`papyrus = false`), the Mare and `e2terrain`: 0 `PAPYRUS`/`SCENE` lines, 0 crash dumps, as the parent.
- **Morrowind unchanged.** `tools/worldobjects_check` 110 result lines OK (new build against itself and the earlier run of the
  parent: same count), the fixed Balmora frame SHA-256 `1c5d47ad0b5dc32d` for the parent build, this build with the defaults and
  this build with `neutral world`, `neutral actors` and `neutral combat` on. (A first run of the check against the same
  executable ended in the freeze monitor on a loaded machine and was repeated.)

**Not built / not run.**
- The story manager's WALK is unchanged: it ends after a quest node that does not share the event, so at Whiterun the Bounty
  Quest nodes (`BQWhiterunNode`: L2 has the habitation keyword, GetInCurrentLoc Whiterun, GetKeywordDataForLocation) are never
  reached behind `DLC1ScrollHandlingChangeLoc`. GetKeywordDataForLocation (606) is not answered by the world (0). So no
  radiant quest started by itself from the story manager in game; one was started by the test aid.
- Aliases: NearAlias and CreatedObject are not filled (counted; created objects are a script's PlaceAtMe), a find-matching
  REFERENCE alias is not a start condition (decision 3), `LocationAlias.GetLocation` is not a native (the script's
  `locationalias.getlocation` calls are tolerant no-ops; one tolerant call in the Mare run), alias text in the journal is the alias
  name.
- The player's position is the centre of the grid square, or for an interior the first cell of the nearest location up its chain
  (`ActorRequestWriter::playerPosition` would need an actor in the table and says nothing about the interior).
- Story-manager state (the "do all before repeating" rotation, the ADIA once-a-session set) is not saved. No CRFT, CAST or the
  other events are raised (no source of them in the neutral world besides KILL, LEVL, CLOC, ADIA and SCPT).
- LEVL in game, the Linux gate and GCC `-Werror` (hand-checked for the traps), clang-format 14 (formatted by hand to 120
  columns), clang-tidy, the Lua tests, a fresh review, a save made with the scenes chunk loaded by an older build.

## 6. Decisions for toddhow
1. **A new SDK interface for the player's place** (`player_place.hpp`, DRAFT 0.1, one engine call site in `World::doPhysics`)
   instead of `ActorRequestWriter::playerPosition`: the bridge's `update()` returns early with no neutral actor (the wild), and a
   position names neither the worldspace nor the interior. Kept small: one filter, one call.
2. **Exterior location rule**: the cell's XLCN, else the location that lists the grid square with the fewest cells (ties: lower
   id). RECALLED-VERIFY; the data does not say which of several listing locations the game uses. A move to no location is
   remembered and raises nothing, so coming back raises (the interior-only version of task 0187 lost the place in between).
3. **The story manager's start gate is now every event type, but only for required aliases whose data answers**: event aliases
   (R and L members), find-matching LOCATIONS, locations inside a location, references in a location. A find-matching REFERENCE
   never blocks (its conditions ask for distance, voice type and relationships the neutral world cannot all answer; with it
   blocking, the ambient scenes stopped: the Onlooker). Alternative: count it as blocking and answer more conditions first.
4. **KILL uses the player as reference 0x14** and the place the player is in as L1 when the victim's cell has no location.
5. **A location alias keeps its location in `AliasFill::mReference`** (no save format change in `papyrus_host`).
6. **Closest** means the first listed cell of a location against the player's grid square (an interior player: the nearest
   location up the chain); a reference by its position in its cell's world; "loaded area" is the player's square and two rings.
7. **Scenes**: a separate chunk `scenes`; a scene resumes at its phase, finished actions stay finished, a running line or timer
   starts again, the actors' holds are made again; a load while the host runs ends the scenes that play, with no end fragment.
8. **Story keywords in the story walk**: the story manager's subject is the player (`run on subject` = player when no speaker is
   set); `GetInCurrentLoc` for the player is the event's L2 (else L1). Changes which story nodes pass; no existing test depended on it.
9. **Small edits in shared files** (other agents work there): `papyrus_host.hpp/.cpp` (one public function,
   `startQuestForTest(quest, stage)`, 12 lines); `neutral_world.hpp` (`PlayerPlaceSink` base and one setter);
   `dialogue.*` (location subject, story player); `world_view.*` (location helpers and the filler's environment);
   `skyrimse_game_pack.cpp` (driver); `physicssystem.*` and `worldimp.*` (the place call); `extension.json` (three settings).
   No `combat*` file was changed: KILL uses the death sink that task 0198 already installs.
10. **Developer aids**: `[extension.skyrimse] story test event = TYPE[:K1=hex][:L1=hex][:L2=hex][:R1=hex][:R2=hex][:V1=n][:V2=n]`,
    `story test quest = <hex>[:<stage>][,...]`, `story test delay` (script seconds, default 8). Off by default; declared in
    `extension.json`.
