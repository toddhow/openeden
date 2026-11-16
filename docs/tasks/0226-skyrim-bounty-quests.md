# Task: the bounty quests (BQ01 to BQ04) start (milestone M-S6)
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (quests, story manager, radiant quests); finishes decision 1 of
task 0217 (`docs/tasks/0217-skyrim-radiant-quests-start.md`, "Not built / not run").
Goal: a Whiterun arrival starts a radiant bounty quest through the story manager with its Hold, BountyLocation and Bounty
aliases filled with a real hold, a real dungeon and its boss, and the journal names them.
Status: BUILT on Windows (2026-11-16); same hand wrote the code and the tests (tests written AFTER the code: see section 3);
no fresh reviewer; local commit on branch `bounty` from `7e4167dfff`, not pushed. Behind `[extension.skyrimse] papyrus`.

Sources: xEdit names fn_181 `HasSameEditorLocAsRefAlias` (already in `survey_names_xedit.cpp`); the Creation Kit wiki page for it
returned HTTP 403 from this machine, so its meaning is RECALLED-VERIFY. Everything the data settles is marked VERIFIED
(Python reader of Skyrim.esm, scratch only, aggregates).

## 1. What the data says

- BQ01 to BQ04 (CLOC quests, node `BQWhiterunNodeSHARES`) have these aliases: `Location` (from event L2), `LocationCenterMarker`
  (a reference in `Location`, reference type LocationCenterMarker), `Hold` (location, ALFA = alias 3, KNAM = LocTypeHold 0x16771),
  `BountyLocation` (find matching location; conditions `fn_181(3, LocTypeHold)`, `fn_562` keyword, `fn_563` ref type Boss),
  `Bounty` (reference in alias 6, ref type Boss).
- VERIFIED: every location alias with ALFA in all QUST records (48) names a REFERENCE alias, never a location alias, and 46 carry
  KNAM; KNAM occurs only on those. So "in the parent of alias 3" is the only direction the data uses; the old child search is
  dead for the data (kept, now keyword filtered).
- VERIFIED: all 123 uses of fn_181 are `== 1`, run-on subject, in QUST records (so answering it 0 for a non-location subject
  equals the old fail-closed). BQ04's BountyLocation needs LocTypeDragonLair (0x130e0): Whiterun Hold has none, so it stays
  blocked (data-correct, not a fault). The BQ aliases Jarl, Steward, Innkeeper are find-matching references (not filled at start).

## 2. Built (rules are RECALLED-VERIFY unless said)

- `QuestAlias::mKeyword` (KNAM, quest.cpp).
- `LocationIndex::ancestorWithKeyword`, `descendantWithKeyword`, `editorLocationOf(reference)` (the deepest location whose special
  or persistent list names it, ties to the lower id; the reference's own location field is not decoded).
- `QuestAliasFiller::pickLocationOfReference`: a location alias with ALFA naming a reference alias is the nearest ancestor of the
  reference's editor location with the KNAM keyword, else the nearest descendant, else (no keyword) the location itself; the
  environment's `mReferenceLocation` (the location of its cell) is the fallback. The keyword also filters the old child search.
  A find-matching location alias whose conditions call fn_181 returns Waiting until that alias is filled (the 3-round loop then
  resolves the quest's order: Hold and BountyLocation come before the marker in the record).
- fn_181 `HasSameEditorLocAsRefAlias(alias, keyword)` for a candidate location: true when the candidate and the editor location of
  the reference in `alias` have the same nearest ancestor-or-self carrying `keyword`. Reads the filler's plan through
  `Environment::mSetAliasView` -> `SkyrimConditionWorld::setAliasView` (set around each evaluation, cleared after).
- Test aid: `story test quest = id:stage` now also sets the stage of a quest the story manager already started (before: refused);
  it starts nothing then.

## 3. Tests

`apps/components_tests/skyrimse/testskyrimsebounty.cpp` (21 tests, suites `SkyrimBountyParse/Locations/Cond/Fill`) and the gated
`testskyrimsebountyreal.cpp` (`SkyrimBountyReal`). **Honest red evidence: the code came first.** Against the code before the task the
test file does not compile (60 errors: no `mKeyword`, `editorLocationOf`, `mSetAliasView`). A stub build (new bodies return
null/Empty/0, KNAM not stored) ran **17 of 21 failed** (the 4 that passed test absence); the first run of the real code also had 6
failing tests whose cause was my rig (a matcher that refused every alias, a count, a wrong comment), fixed in the tests. Final: 21 pass.

## 4. In game and real data

- `SkyrimBountyReal` (five masters, CLOC Mare -> Whiterun): the story manager chooses BQ01 (plus CRHoldExpansion, WIChangeLocation01
  ...); BQ01: Hold = Whiterun (WhiterunHoldLocation), BountyLocation = Halted Stream Camp, Bounty = ACHR 0x540a1 "Bandit Chief"
  (LvlBanditBossOrcM); BQ02: Serpent's Bluff Redoubt, boss ACHR 0xd281e (a Hagraven template actor with no name of its own);
  BQ03: Bleakwind Basin, boss 0x33f9f (LvlGiant); BQ04 blocked (no dragon lair in the hold).
- Real game walk (`e2terrain`, the player leaves the Mare into Whiterun): `STORY started 000BD77F BQ02 by CLOC: Location=Whiterun
  LocationCenterMarker=00029AAF Hold=00016772(Whiterun) BountyLocation=000192C4(Serpent's Bluff Redoubt) Bounty=000D281E`.
- Journal (frame `I:\Projects\OpenEden-work\combat5\keep226\journal_bq02.png`, viewed): "Bounty: Forsworn", running, objective
  "Kill the leader of Serpent's Bluff Redoubt". The stage was set by the test aid (`story test quest = bd77f:10`), standing in for
  the offering dialogue line (section 6).
- Three runs (`e2terrain`, 11,000 frames, `story trace`): the `STORY started ... BQ` lines are identical in all runs (BQ02 then BQ01 at
  two Whiterun arrivals); 3 of 4 runs have identical `STORY started` sets, the others add late CRHoldExpansion / BardSongStarter starts
  from a later arrival (timing, older, task 0194). Logs: `I:\Projects\OpenEden-work\combat5\keep226\story_*.txt`.
- Papyrus off: 0 PAPYRUS and 0 STORY lines, exit 0. Morrowind: `tools/worldobjects_check` 110 objects OK, Balmora frame
  `1c5d47ad0b5dc32d` (same build as reference, as in earlier rows).
- Tests: `SkyrimBounty*` 21 pass; area filter (`*Skyrim*:*Radiant*:*Location*:*Quest*:*Story*:*Papyrus*:*Journal*`) 2,412 pass. One existing test
  (`LocationSubject.theCombinedTableHasTheStoryFunctionToo`) pins the size of the location extra-function table, so fn_181 went in its
  own table `aliasLocationExtraFunctions()`; no existing test edited. Gated `SkyrimRadiantReal*`/`Radiant2Real` pass.

## 5. Edits outside the story and quest files

`papyrus_host.cpp` (the aid, 10 lines), `dialogue.hpp/.cpp` (fn_181 support: three overrides, two setters), `location.*`,
`location_conditions.*`, `world_view.cpp` (environment wiring), `quest.*`, `quest_alias.*`, two CMake test entries. No combat,
engine or SDK file.

## 6. Not built / not run

- **The dialogue that offers it.** BQ0xMainTopic INFO 0x95123 needs the speaker to fill alias 13 (Steward), INFO 0xcba96 alias 10
  (Innkeeper, Hulda). Those aliases need fn_567 `GetIsEditorLocAlias` (not implemented), editor locations for unique NPCs (LCUN
  lists) and the lazy alias fill from a conversation; implementing fn_567 would change the fills of many other quests. Not cheap; left.
- The Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review, a boss name for template actors (the journal says
  "leader", the tag is not used by BQ02's text), BQ04.

## 7. Decisions for toddhow

1. Do fn_567 and the dialogue next (it also fills Jarl/Steward/Innkeeper for every radiant quest)?
2. The test aid now sets stages of running quests; acceptable?
