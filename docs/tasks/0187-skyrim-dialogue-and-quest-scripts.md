# Task: Skyrim dialogue and quest scripts (milestone M-S6)

Status: BUILT for the first slice (INFO fragments, alias filling, the story manager's SCPT and CLOC events, one real quest moved by play);
Windows only, local commits on branch `questscripts` from `b6bd3c2dcf`, not pushed. toddhow chose "Dialogue and quest scripts" for M-S6
on 2026-11-15. Everything is behind `[extension.skyrimse] papyrus` (off by default); the journal is `[extension.skyrimse] journal`.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.20 (dialogue and quests), the M-S6 row; decision 0014; tasks 0137,
0139, 0141, 0155, 0176, 0181, 0185.
Goal: real quests start and move in play. A line the player picks runs its script fragment; a quest that starts gets its aliases filled;
the story manager starts quests from events; the journal shows the result without the test aid.
Inherited code touched: none.
Sources used for meanings (allowed by decision 0014's amendment): xEdit's record definitions (named from memory, RECALLED-VERIFY: the
Creation Kit wiki still refuses the fetch tool), and the installed data (aggregates only). CommonLibSSE was not consulted. No Havok.

## 1. Survey (read at `b6bd3c2dcf`)

- **INFO fragments.** `dialogue.cpp readInfo` (the `VMAD` case) noted the subrecord (`mHasScript`) and scanned it for a `TIF__` name
  (`findFragmentScriptName`, task 0155); `DialogueRuntime::fillTurn` counted `mScriptsNotRun`. Nothing parsed the script list or the
  fragment section. **Measured on Skyrim.esm** (Python walker of the task, 5,540 INFO VMAD subrecords): after the script list a fragment
  section follows in 5,257 (u8 2, u8 flags: bit 0 begin, bit 1 end, only values 1, 2, 3 occur, wstring file name, then per set bit u8,
  wstring script, wstring function: 4,456 + 801 end exactly), the other 283 have no section at all (the VMAD ends after the scripts).
  Object format 1 in 801, format 2 in 4,456. Functions are `Fragment_0` .. `Fragment_3`, taking `akSpeakerRef` (ObjectReference).
  The real scripts reach their quest through `self.GetOwningQuest()` (`TopicInfo`): of the 13 lines with fragments that the actors of the
  Bannered Mare offer (aggregates in `SkyrimQuestScriptsRealData`), 7 start with GetOwningQuest.
- **Quest start.** `papyrus_host.cpp`: `startQuest`, `setStage` (calls `attachQuest`, runs `runFragments`), `Quest.Start`. **Finding: the
  runtime refuses `loadScript` inside a native** ("a script cannot be loaded from inside a native"), and `attachQuest` loads the quest's
  scripts, so a quest that a SCRIPT starts or stages (and whose scripts nothing had named before) never got its scripts, its fragment or
  its OnStageSet: `runFragments` found no instance and skipped silently. Only the start-enabled quests worked (attached by `startGame`).
- **Aliases.** `QuestState::initial` fills specific-reference (ALFR) and unique-actor (ALUA) aliases from data, but a unique actor was
  only the BASE (`AliasFill::mActor`); `ReferenceAlias.GetReference` read `mReference` only, so it answered None for 3,519 of the data's
  6,641 data-fillable aliases. The 9,110 find-matching aliases stayed empty.
- **Story manager.** Record types SMBN, SMQN, SMEN were counted by `pack_core` and decoded by nothing. **Measured on Skyrim.esm:** 571
  nodes (SMEN 24, SMBN 99, SMQN 448). Layout (see `story.hpp`): PNAM parent, SNAM sibling, CITC, CTDA, DNAM, XNAM, SMEN ENAM (24
  distinct events: CLOC, SCPT, LEVL, KILL, ADIA, ...), SMQN QNAM, NNAM per quest, optional RNAM and MNAM. 36 of 36 distinct
  `GetEventData` (fn_576) member codes fit "function in the low half of param 1, two member characters in the high half" (K1, L1, L2,
  R1, V1, V2 ...); functions 0, 2 and 3 only. SNAM names the previous sibling (the other reading puts the general WEQuests before
  WEPriorityQuests and WESpecificQuestsNode in the world event node). `Keyword.SendStoryEvent(akLoc, akRef1, akRef2, aiValue1,
  aiValue2)` and `SendStoryEventAndWait` are native on `Keyword` (38 vanilla scripts call it).
- **Journal.** Task 0185: a fresh session lists nothing because no quest stage is set in 45 s.

## 2. Design

- `vmad::parseInfo` (strict, whole subrecord used); `DialogInfo::mVmad` (shared, load-order ids); `DialogueIndex::scriptedInfo`.
- `DialogueRuntime::setFragmentSink`: `fillTurn` hands the INFO's fragments to the sink with the speaker. **The begin fragment and the
  end fragment both run when the line is shown, begin first**: the pack does not know when a spoken line ends (decision 1).
- `PapyrusHost::runInfoFragments`: attaches the INFO's scripts to the INFO form (attach key kind Form, the INFO id, the script's
  ordinal; reused next time, saved by the existing ledger), sets their properties, queues `Fragment_n(akSpeakerRef)` on the main thread.
  New native `TopicInfo.GetOwningQuest` (the INFO's QNAM, else its topic's). New classes preloaded at the host's start (Form,
  ObjectReference, Actor, Quest, Alias, ReferenceAlias, LocationAlias, Location, Keyword, TopicInfo, Cell, Spell, Game, Utility, Debug)
  because a class cannot be loaded inside a native (an Actor handed out as akSpeakerRef or a ReferenceAlias reference was unusable).
- **Deferred attach**: `afterNative(work)`: the attach, the fragments and the OnStageSet events of a `SetStage` or `Start` that a native
  asked for run right after the calling script returned (end of `tick` and of `callFunction`, repeated until nothing is left, max 16
  rounds per call and 8 per tick). Calls made outside a native run at once as before.
- `QuestAliasFiller` (`quest_alias.*`, pure): `fillQuest(quest, state, matching)` and `fillAlias`. UniqueActor: the first placed
  reference of the base in id order that is not deleted, not initially disabled (flag 0x80 allows) and not dead (0x10 allows).
  FindMatching: candidates are the references of the base a GetIsID condition names, else actors in id order up to 20,000; the alias's
  conditions are evaluated by the existing `Evaluator` with the candidate as speaker (`SkyrimWorldView` builds the matcher over a
  `SkyrimConditionWorld` of its own); a reference already in another alias of the quest is not taken again unless the alias allows reuse
  (0x08). Other kinds are counted (`mNotAnswered`). The host fills the data aliases of start-enabled quests at `startGame`, ALL aliases
  when a script, a dialogue fragment or the story manager starts a quest or stages a quest that was not running, and one alias on demand
  when `ReferenceAlias.GetReference` finds it empty on a running quest.
- `StoryIndex` / `StoryManager` (`story.*`, pure): decode, order children by the sibling chain (fallback: form id order), walk the event
  node of an event type: conditions of a node (fn_576 through `storyExtraFunctions()` and `SkyrimConditionWorld` as `StoryEventSource`),
  quest nodes start up to MNAM (else 1) quests in order that are not running or completed and whose own start conditions pass; "do all
  before repeating" rotates through the node's quests; a node that started a quest ends the walk unless it shares the event; a random
  branch shuffles its children with a splitmix64 stream seeded by (event number, node). Events: `Keyword.SendStoryEvent` (type SCPT:
  K1 the keyword, L1 akLoc, R1, R2, V1, V2) and a pack-raised CLOC (L1 the last location, L2 the new one) for every INTERIOR cell the
  engine builds whose location (XLCN) differs from the last raised (the pack is not told where the player stands, so exterior ring loads
  are not events). LEVL (level increase) needs a player level up, which does not exist: not raised (decision 4).
- Wiring: `PapyrusAccess::mAliases`, `mStory`; `SkyrimWorldView::setInfoFragmentSink`, `isScriptedInfo`, `questOfInfo`, `cellLocation`;
  the driver sets all three in `tryStart`. Summary line gains dialogue fragments, story events, quests started and aliases filled.

## 3. Stages

1. Spec and survey (this file). 2. Tests first, run red against stubs. 3. Code. 4. Real data (gated) and in game: a quest moved by play.
5. Morrowind and the setting-off checks.

## Acceptance criteria
- [x] `parseInfo` reads begin and end fragments, accepts a VMAD with no section, refuses an unknown flag bit, every truncation and trailing bytes.
- [x] A shown line with fragments reaches the sink once with the speaker; with no sink nothing runs and the old counts stay.
- [x] The host runs the begin then the end function with the speaker; the script is attached once, survives a save and a load.
- [x] A unique-actor alias gets a reference; find-matching aliases follow the conditions, flags and the reuse rule; a script sees them.
- [x] A script's `SetStage`/`Start` on a quest whose scripts were not loaded attaches them and runs the fragment.
- [x] The story manager decodes nodes, orders siblings, starts quests by the rules above; fn_576 reads the event; `SendStoryEvent` reaches it.
- [x] In game, a real vanilla quest is moved by dialogue and shown in the journal without `journal test quests`.
- [x] Settings off: nothing of this runs; Morrowind is unchanged.

## Out of scope
Radiant quests' random fills beyond the conditions, scenes, the other 22 event types (ADIA, KILL, ...), quest save of the story manager's
cycle memory, aliases of the created-object, from-event and near-alias kinds, sound of the NPC's voice ending the fragment (decision 1).

## Stage log (2026-11-15; same hand wrote tests and code; Windows only; local commits on branch `questscripts` from `b6bd3c2dcf`, not pushed)

**Built.** Pack: `vmad::parseInfo`, `DialogInfo::mVmad`, `DialogueIndex::scriptedInfo`, `DialogueRuntime::setFragmentSink`,
`PapyrusHost::runInfoFragments` / `setAliasFiller` / `setStoryEventSink` / `raiseStoryEvent`, natives `TopicInfo.GetOwningQuest`,
`Keyword.SendStoryEvent`, `Keyword.SendStoryEventAndWait`, the deferred attach (`afterNative`), the class preload, `quest_alias.*`,
`story.*`, `SkyrimConditionWorld` as `StoryEventSource`, the wiring in `world_view.*` and `skyrimse_game_pack.cpp` (CLOC for interior
cells, the summary line). Tool: `tools/neutral_session_check/luatest/scripts/q187` (talk scenario) and `q187talk.omwscripts`.

**Tests first, red against stubs.** New file `apps/components_tests/skyrimse/testskyrimsequestscripts.cpp`, 44 tests written first
(INFO VMAD 6, dialogue parse and sink 5, the host running a TIF script 5, alias filler 8 + 2 through the host, story decode and walk 11,
fn_576 5, SendStoryEvent 3; generated plugins and scripts, no game data). Stubs: `parseInfo` returned a failed parse, `scriptedInfo`
null, the sink never called, `runInfoFragments`/`raiseStoryEvent` 0, `QuestAliasFiller` filled nothing, `StoryIndex` empty, no fn_576.
**38 of 44 failed, 6 passed** (they test refusal or absence: trailing bytes refused, no sink, no VMAD, VMAD with no fragments, no story
sink). Later additions: `theOwningQuestOfTheInfo...` red first (1 of 1: "native 'TopicInfo.GetOwningQuest' is declared and not registered"),
`aStageSetByAScriptOnAQuestWhoseScriptsAreNotLoaded...` NOT run red first; its guard was checked by a mutation (deferral switched off:
2 tests fail, `aScriptSeesTheReferenceOfAUniqueActorAlias...` and this one). The first green run found three test faults of mine (a
list that made two quests instead of two aliases; a keyword id with bit 0 set so `id | 1` equalled it; an expectation contradicting
the reuse rule), fixed in the tests, and two real faults: **a class handed to a script from inside a native cannot be loaded** (the
Actor of `akSpeakerRef`) and **a script cannot be loaded inside a native** (a started quest's scripts): both fixed in the host
(preload, deferral). Green: 46 new tests pass; `Skyrim*`, `Papyrus*`, `Vmad*`, `*Dialogue*`, `*Quest*`, `*Extension*`, `*Frozen*`,
`*Neutral*`, `*Journal*` of `components-tests` 1,778 pass (no existing test edited); `openmw-tests` `*Lua*:*Neutral*:*Extension*:*Skyrim*`
782 pass.

**Real data** (`SkyrimQuestScriptsRealData`, gated, ids only): the actors of the Bannered Mare offer 13 lines with fragments (Hulda 2,
Mikael 5, Brenuin 2, Saadia 3, the urchin 1); every one is an end-only fragment. A Python walker (task tool, not committed) parses all
5,540 INFO VMAD of Skyrim.esm to the last byte and all 571 story nodes.

**In game** (`tools/neutral_session_check/check.sh` with `CFG_EXTRA='content=q187talk.omwscripts'`, Bannered Mare, five masters,
`[Game] neutral actors`, `[extension.skyrimse] papyrus` and `journal`, `[GUI] rmlui`, fixed frame time, quiet window; the talk starts at
update 9000, real mouse clicks through `OPENEDEN_RMLUI_INPUT_SCRIPT`, files in `I:\Projects\OpenEden-work\papyrus\q\clicks*.txt`):
- **Saadia, "Any suggestions as to how I find them?" (INFO 0006A050)**: `PAPYRUS dialogue line 0006A050 runs TIF__0006A050.Fragment_0`,
  then `quest 0001CF25 set to stage 25`, `stage 25 runs QF_MS08_0001CF25.Fragment_0`, objectives and two references enabled and
  disabled; the journal (`J`) then lists **"In My Time Of Need" with its real log text ("The barmaid at the Bannered Mare in Whiterun
  is apparently a fugitive noble from Hammerfell ...") and two objectives ("Talk to the Alik'r Prisoner", "Inform the Alik'r of
  Saadia's location")**, without `journal test quests` (frame viewed: `run_q8/user/screenshots`).
- **Mikael, "What do you do around here?" (INFO 000534F2)**: `quest 00042868 (MS05Start) set to stage 10`, its fragment runs and displays
  an objective (the quest has no name, so the journal does not list it; the Bards College line reads "I'm a bard, trained at the Bards
  College in Solitude. Wanderers like you should think about applying.").
- **Story manager**: at the start in the Bannered Mare `story event CLOC to location 0001F86D started 1 quests` (quest 00037CC2,
  DialogueWhiterunArgueScene, a scene quest with no name). 517 aliases filled at the host's start (data kinds); the lines
  "quest N started with k aliases filled" now appear only for quests started later (all aliases).
- **Three runs** of the Saadia scenario: the 177 `PAPYRUS`/`JOURNAL` lines (timestamps cut, without the wall-clock `PAPYRUSTIME` and
  the summary lines) are byte-identical, md5 `57fb11e6038a061a751f74905606e7a0` three times.
- **Setting off** (`papyrus = false`, same scenario): 0 `PAPYRUS` lines, the conversation runs as before (`NTALK ... closed after 2
  turns`), 0 crash dumps. **Morrowind unchanged**: `tools/worldobjects_check` against the `integ2` build (the reference of the earlier
  stages; a parent build of `b6bd3c2dcf` was not made): 110 result lines, OK; the fixed Balmora frame (`--start Balmora --random-seed
  7`, frame 600, `OPENEDEN_FIXED_FRAME_TIME`) has SHA-256 prefix `1c5d47ad0b5dc32d` for the reference, for this build and for this
  build with `neutral world`, `neutral actors`, `papyrus` and `journal` on.

**Not run / not built:** the Linux gate and GCC `-Werror` (the GCC traps were avoided by hand), clang-format 14 (formatted by hand to
120 columns), clang-tidy, the Lua integration tests, sanitizers, a fresh review, a build of the exact parent commit for the off
comparison, a save and load with a dialogue fragment instance in the ledger in game (unit tested: `theInstanceSurvivesASaveAndALoad`),
the story manager's walk against more than the CLOC and SCPT trees (LEVL, KILL, ADIA and the other event types are not raised),
alias kinds other than specific reference, unique actor and find-matching, a find-matching alias whose conditions need more than the
candidate (GetInCurrentLoc and friends answer through the speaker's cell), radiant quests, scenes, the wall-clock cost of a
find-matching fill (not measured: capped at 20,000 candidates per alias), the end fragment timing against the spoken line.

## Decisions for toddhow
1. **Begin and end fragments both run when the line is shown (begin first).** The pack does not know when the NPC's voice ends. All 13
   lines the Mare offers have an end-only fragment, so for them this is "when the line is shown". Alternative: run the end fragment when
   the next turn or Goodbye arrives.
2. **Story walk rules** (RECALLED-VERIFY, in `story.hpp`): SNAM is the previous sibling; a quest node starts MNAM (else 1) quests; a node
   that started a quest ends the walk unless it shares the event; "do all before repeating" rotates through the node's quests in memory
   (not saved); a random branch shuffles children by a seeded stream.
3. **Deferred attach.** A quest that a script starts or stages now gets its scripts, fragment and OnStageSet right after the calling
   script returns (still the same host tick), not inside the native. This is a change of when quest fragments run for quests that
   were not attached at the start; before this task they did not run at all.
4. **Events raised.** SCPT (`Keyword.SendStoryEvent`) and CLOC for interior cells. LEVL cannot be raised (no player level up); the
   other event types need combat, crime or item events the neutral world does not have yet. Exterior location changes are not
   raised because the pack is not told where the player stands.
5. **Find-matching candidates** are the references of the base a GetIsID condition names, else actors, in id order, up to 20,000; an
   alias that names only a keyword on a non-actor stays empty and is counted.
6. **`TopicInfo.GetOwningQuest`** answers the INFO's QNAM, else its topic's quest.
7. **Nameless quests** (MS05Start) are not in the journal (task 0185's rule); the Bards College objective is set but not visible.
8. **Test aid**: the injected `OPENEDEN_NEUTRAL_DIALOGUE_PRESS` leaves the world paused after Goodbye, so the scenario uses real clicks.
   `q187/talk.lua` counts updates (9000) because the dialogue data needs about 15 real seconds.
9. **Existing tests edited: none.**
