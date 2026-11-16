# Task: the bounty is offered in dialogue (fn_567, lazy alias fill) (milestone M-S6)
Design doc section: `docs/design/skyrim-se-implementation-plan.md` (quests, story manager, dialogue); finishes section 6 of
task 0226 (`docs/tasks/0226-skyrim-bounty-quests.md`).
Goal: Hulda (the innkeeper) offers the bounty line of the running bounty quest, and choosing it moves the quest to its accepted
stage through the INFO fragment (task 0187), with the log text and objective in the journal.
Status: BUILT on Windows (2026-11-16); same hand wrote tests and code (tests first, run red against stubs); no fresh
reviewer; local commits on branch `bounty2` from `54a3f9e2d0`, not pushed. Behind `[extension.skyrimse] papyrus`.

## 1. What the data says (VERIFIED, Python reader of Skyrim.esm, aggregates and official ids)
- The offering INFOs: BQ01MainTopic INFO 0x95123 needs `GetIsAliasRef(13)` (Steward), INFO 0xcba96 `GetIsAliasRef(10)`
  (Innkeeper); BQ02: 0xbd752 / 0xcba94. All need `GetStageDone(quest, 10) == 0`.
- fn_567 GetIsEditorLocAlias: 416 uses in the five masters, ALL `== 1`, run-on subject, 414 in QUST aliases (27 quests in
  Skyrim.esm, 31 in the merged index), 2 in PACK. Jarl / Steward / Innkeeper: faction test plus fn_567 on alias 4 (Hold) or 0
  (Location).
- Editor locations of the NPCs: the ACHR's own XLCN ("persistent location"): 5,639 of 10,504 placed actors have one; Hulda,
  Proventus and Balgruuf all have XLCN = WhiterunLocation, which lies INSIDE the Hold alias, so the function means "inside".
  LCUN (unique NPC) gives Proventus WhiterunDragonsreachLocation (its third field; CommonLibSSE names it `editorLoc`, read via
  WebFetch of `BGSLocation.h`: the only citation; the CK wiki returned 403, so the meaning is RECALLED-VERIFY).
- Why Jarl and Steward stayed empty after the first attempt: the quest's aliases are not in dependency order. The Hold is found
  from the marker in the same plan, so in round one the Hold was not filled and the Jarl's conditions failed for everyone
  (`Result::Empty`, never retried). Fixed: such an alias now WAITS (`Result::Waiting`) for the alias its fn_567 names.

## 2. Built
- `PlacedEntry::mLocation` (XLCN; a later plugin's record replaces it wholly), `LocationIndex::uniqueNpcLocation`.
- Editor location order (`SkyrimConditionWorld::editorLocationOf`): XLCN, LCUN, the lists (task 0226), the location of the cell.
  fn_181 uses the same order (0226's BQ fills still work: see section 3).
- fn_567 in `aliasLocationExtraFunctions()`; the alias view now also wraps the evaluation of find-matching REFERENCE aliases.
- Lazy fill: `QuestAliasFiller::fillForSpeaker`, called from `SkyrimWorldView::beginConversation`: a running quest's empty
  find-matching reference alias whose conditions call fn_567 takes the speaker when its conditions pass. Narrow on purpose
  (every other empty alias keeps its rule). In practice the aliases are already filled at the quest's start (eager fill), so
  this is the fallback; it was shown in the real-data test (live state, Hulda: 1 alias) and by unit tests, NOT in the game.
- `SkyrimWorldView::setAnswerEditorLocAlias(bool)`: measurement switch (false = as before the task).

## 3. Measurement (real masters, `SkyrimBountyDlgReal`, before = fn_567 unanswered, after = answered)
- 31 quests call fn_567 (419 conditions). Fills changed in 8 quests, 18 aliases (18 gained, 0 lost): BQ01..BQ04 Innkeeper,
  Jarl (BQ01, BQ03, BQ04), Steward and MapMarker (BQ01..03), C01.DungeonMarker, CWFortSiegeCapital.MapMarker,
  CWFortSiegeFort.MapMarker, DA09.GemMapMarker.
- Start gates changed: 0. The story manager's Whiterun arrival picks the same set before and after (BQ01, CRHoldExpansion,
  DLC1ScrollHandlingChangeLoc, DialogueWhiterunHeimskrPreachScene, WIChangeLocation01); `SkyrimRadiantReal`, `Radiant2Real`
  and `SkyrimBountyReal` (0211, 0217, 0226) pass.
- Cost: filling the 31 quests in a loop took 12.4 s before and 16.5 s after (find-matching scans of the actors; about 0.5 s a
  quest on a cold start, +30 percent): decision 3.

## 4. Tests
Written first against stubs of the new functions: `SkyrimBountyDlg*` (21 tests, 15 of the first 20 failed; the other 5 test
absence or controls; the 21st, the waiting alias, was red before its fix). Gated: `testskyrimsebountydialoguereal.cpp`.
Existing tests edited: none.

## 5. In game (`tools/neutral_session_check/check.sh ... bqtalk`, `bq_clicks.txt`, real clicks, Bannered Mare)
BQ02 starts by the story event with Steward = Proventus, Innkeeper = Hulda; the conversation with Hulda lists "I'm looking
for work. Got any leads?"; the click gives a response and runs `TIF__000CBA94.Fragment_0` (log: `dialogue line 000CBA94`);
when the box closes the host sets BQ02 to stage 10 and the journal shows "Bounty: Forsworn", running, objective "Kill the
leader of Serpent's Bluff Redoubt" (frame `I:\Projects\OpenEden-work\combat5\keep230\journal.png`). The stage is set only
after the box closes (script time is paused while it is open): decision 2.

## 6. Edits outside the quest, story and condition files
`placed_refs.*` (XLCN), `location.*`, `world_view.*` (hook, switch), `dialogue.*` (editor location), two CMake test entries,
`tools/neutral_session_check` (scenario `bqtalk`, `bq_clicks.txt`, one line in `check.sh`).

## 7. Not run
The Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review, the lazy fill in game, Jarl / Steward lines (only
Hulda was talked to), BQ01 in game, a save and load of a filled alias.

## 8. Decisions for toddhow
1. Editor-location order (XLCN, LCUN, lists, cell) and "inside" semantics are RECALLED-VERIFY.
2. The stage change after the box closes (host time is paused in conversations): acceptable?
3. The cost of eager find-matching fills (about 0.5 s a quest): add a cheap pre-filter (faction first) or accept?
