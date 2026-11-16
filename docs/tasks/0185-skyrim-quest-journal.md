# Task: Skyrim quest journal (milestone M-S6)

Status: BUILT, including objective display (follow-up on branch `journal2`, see the last section); Windows only, not pushed; see the stage log at the end. toddhow chose "Quest journal" for M-S6 on 2026-11-15.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.20 (dialogue and quests), 4.24 (UI, row "journal/quests"),
the M-S6 row; decision 0015 (UI system), task 0097 (script bindings for game packs), tasks 0139 (quests), 0176 and 0181
(Papyrus), 0180 (HUD) and 0182 (inventory) as the patterns.
Goal: in a Skyrim neutral session the player presses `J` and sees a journal window: the running quests by name, each one's
current log entry text, its displayed objectives, and the completed quests in a separate list. The pack owns the state, the
core only draws it.
Inherited code touched: none. (`J` is OpenMW's own journal key: see "The J key" below.)
Constraints: behind a new pack setting `[extension.skyrimse] journal` (off) plus `[GUI] rmlui`; nothing is created otherwise;
Morrowind's journal is unchanged (the scripts carry `REQUIRES=core.skyrimse.journal>=1.0`, so they never start without the
pack's namespace). Does not touch `papyrus_host*`, `vmad*`, `quest_script*` (another agent), `inventory*` (another agent),
the `openeden.ui` components or the themes.

## 1. Survey (read at `4f665dcd82`)

- **Quest data.** `QuestRecord` (`quest.hpp`): `mName` (FULL, `.STRINGS`), `mStages` with `QuestLogEntry::mText` (CNAM) and
  `mFlags` (QSDT: 1 complete, 2 fail), `mObjectives` with `mText` (NNAM) and `mIndex`, `mType`, `mPlugin` (the index of the
  plugin whose record won; `QuestIndex::add` is called once per plugin in `buildDialogueData`, `world_view.cpp` 569 to 581, so
  it equals the dialogue runtime's plugin index that the text resolver takes). Names and texts are `LString`: a localised
  plugin stores a 4 byte id, the text is in `.STRINGS` / `.DLSTRINGS` / `.ILSTRINGS`.
- **Quest state.** `QuestState` (`quest.hpp`, `quest.cpp` 696 to 880): `exists/running/completed/stage/stageDone`. It is the
  dialogue runtime's own (`DialogueRuntime::questsMutable`); `SkyrimWorldView::papyrusAccess` hands the index and the state
  to the Papyrus host, which changes it from `Quest.SetStage`, `Start`, `Stop` on the main thread (`PapyrusDriver::frame`,
  the game phase). `QuestState` has no change counter: a reader has to compare snapshots (`snapshot()` builds a vector of
  every quest, about 2,400 with sets) to notice a change. This task adds `QuestState::revision()` (a counter every mutator
  increments).
- **Objective flags.** Objectives are shown by `Quest.SetObjectiveDisplayed` and `SetObjectiveCompleted`;
  `PapyrusHost` keeps the flags in two private maps (`mObjectivesDisplayed`, `mObjectivesCompleted`,
  `papyrus_host.cpp` 754 to 773, 872) with no public reader. Another agent is editing `papyrus_host*` right now, so this task
  does NOT edit it: the journal reads them through two hooks and the patch that exposes them is in
  `docs/tasks/0185-papyrus-host-objective-accessors.patch` (applies to `4f665dcd82`; see decision 1).
- **Strings.** The resolver that turns an `LString` into text is a lambda inside `SkyrimWorldView::ensureDialogue`
  (`world_view.cpp` 636 to 673) that reads the plugin's string table through the session's VFS and caches it; it is main-thread
  only. `PapyrusAccess` (`world_view.hpp`) does not carry it; this task adds a field.
- **Bindings.** `BindingsService::declareNamespace` (task 0097): the HUD (`CombatHudBindings`, `hud.cpp`) and the inventory
  (`InventoryService::declare`, `inventory_service.cpp` 229 to 447) are the two users. Read functions run on the script's
  thread, which in this tree is the main thread (the Lua worker is unused); arrays are flat, capped at
  `kMaxBindingArrayElements`; ids that need more than 31 bits go as text.
- **UI.** `openeden.ui` (`files/data/openeden/ui.lua`): `Document`, `Column`, `Row`, `Text`, `Button`, `List` (virtualised,
  keyed, a row is built once, explicit width). The inventory windows (`openeden/skyrimse/inventory_windows.lua`) are the
  pattern: a plain module over `ui` and the package, tested over the headless backend.
- **GUI mode.** `inventory_player.lua`: `I.UI.setMode('Interface', { windows = {} })` is GUI mode with the cursor and the
  pause and no MyGUI window; the mode is watched by `I.UI.getMode()`.
- **The J key.** `A_Journal` is bound to `J` and "handled in Lua": `scripts/omw/input/playercontrols.lua` adds the engine mode
  `Journal` (the MyGUI journal window) when `getMode()` is nil and the Magic window is allowed, and removes it when `getMode()`
  is `Journal`. `Journal` is not a replaceable window (`registerWindow` knows Map, Inventory, Magic, Stats). So pressing `J` in a
  Skyrim session already makes a mode change; the journal script treats "the top mode became `Journal`" as its key: when the
  pack's journal is ready and nothing of ours is open it replaces that mode with ours (`setMode('Interface', {windows = {}})`);
  when ours is open it leaves (a second `J` puts `Journal` on top of our mode again). No second key handler, so the two
  cannot race.

## 2. Design

**Pack** (`game_packs/skyrimse/{include/game_skyrimse,src}/journal.*`):

- `buildJournal(Inputs, JournalSnapshot&)` is pure. For every quest of the index that the state knows and that is running or
  completed, with a non-empty name: the quest's log is the first non-empty entry text of every stage that is done (ascending
  stage), the current entry is the entry text of the current stage (else the last log text), and the objectives are those the
  hook reports displayed (index order), each with its completed flag. A quest is listed when it has a log entry or a displayed
  objective. Status: `completed` when the state says so, `failed` when the current stage's entry has the fail flag
  (completed wins), else `running`. Order: case-insensitive name, then form id (deterministic). Caps: 256 quests per list,
  64 log entries and 32 objectives per quest, 4,096 bytes per text.
- `JournalService` owns the snapshot and rebuilds it lazily when `revision()` (the state's revision plus the objective hook's
  revision) differs from the snapshot's, so a frame where nothing changed costs two integer reads. Declares namespace
  `skyrimse.journal` 1.0, all Read:
  `ready() -> bool`, `revision() -> int`, `quests(completed: bool) -> string[]` (ids, 8 hex digits), `name(id)`, `entry(id)`,
  `status(id)` (`running`, `completed`, `failed`), `log(id) -> string[]` (oldest first, the last is the current),
  `objectives(id) -> string[]`, `objective_done(id) -> bool[]` (same order).
- Hooks (the service has no engine or world-view type): `mAccess(Access&)` (the index, the state, a text function, filled once
  the world view is ready), `mObjective(quest, index) -> {displayed, completed}` and `mObjectiveRevision()` (no hook: no
  objectives), `mLog`.
- Wiring (`skyrimse_game_pack.cpp`): `startJournal` makes the service when `[extension.skyrimse] journal` is on and declares the
  namespace (one block and one member, nothing else of the file changes).

**Lua** (`files/data/openeden/skyrimse/journal_windows.lua`, `scripts/openeden/skyrimse/journal_player.lua`,
`builtin.omwscripts` line with `REQUIRES=core.skyrimse.journal>=1.0`): one document `skyrim-journal`: a title, two list
buttons `Quests` and `Completed`, a `List` of quest names (click selects), a detail pane with the quest's name, status,
current entry, objectives (`[x]` or `[ ]` before the text), earlier log entries, and `Close`. Values are re-read only when
the revision moved; a frame with nothing changed makes no protocol call.

## 3. Stages

1. This spec.
2. Pack: `QuestState::revision()` (tests), `buildJournal` and the service over hand-made quests and generated plugins (tests
   written first, red against stubs); gated real-data aggregates on the five masters.
3. Bindings tests over a stub bindings service; the wiring and the setting.
4. Lua window module and script over the headless backend (tests first, red against a stub module).
5. In game: Bannered Mare, Papyrus on, the real `J`, frames, three runs equal; Morrowind unchanged.

## Acceptance criteria
- [ ] `QuestState::revision()` changes on every mutation and only then.
- [ ] The snapshot lists named, running or completed quests with a log or a displayed objective, separates running from
      completed, orders them deterministically and obeys the caps.
- [ ] The bindings declare `skyrimse.journal` 1.0 with the listed functions; a frame with nothing changed rebuilds nothing.
- [ ] The window makes no protocol call in a frame where nothing changed.
- [ ] In game with Papyrus on: `J` opens the journal with real quest names and log texts; `J` or Escape closes it; three runs
      give the same trace.
- [ ] With the settings off, Morrowind and Skyrim sessions behave as before (`worldobjects_check`, the fixed Balmora frame).

## Out of scope
Quest tracking markers and the compass, quest type tabs and icons, notes and the stats pages, the Papyrus-free way to learn
objective flags (see decision 1), log entry conditions (the first non-empty entry of a stage is used), quest start events,
the Morrowind journal, gamepad, saving of anything (the state is Papyrus's and the dialogue runtime's already).

## Stage log (2026-11-15; same hand wrote tests and code; Windows only; local commits on branch `journal` from `4f665dcd82`, not pushed)

**Built.** Pack: `journal.*` (pure `build`, `expandTags`, `applyTestQuests`, `JournalService`, the namespace `skyrimse.journal` 1.0),
`QuestState::revision()` (`quest.*`), `PapyrusAccess::mText` (`world_view.*`: the dialogue runtime's string resolver, kept in
`DialogueState::mResolver`; the runtime is now built from it), `startJournal` in `skyrimse_game_pack.cpp`, settings
`[extension.skyrimse] journal` (off) and `journal test quests` (0, a developer aid) in `extension.json`. Lua:
`openeden/skyrimse/journal_windows.lua`, `scripts/openeden/skyrimse/journal_player.lua`, one `builtin.omwscripts` line with
`REQUIRES=core.skyrimse.journal>=1.0`. Test profile: `journal = true` and a README line. Check tool: `tools/journal_check`.

**Tests first, red against stubs.** Pack and service (`testskyrimsejournal.cpp`, hand-made quests): 27 of 32 failed against a
stub `journal.cpp` and a `QuestState::revision` that never moved (the 5 that passed test absence: unknown quests change
nothing, a quest with no name or no text is left out, no inputs give an empty snapshot, a localised text with no resolver is
empty); `applyTestQuests` 1 of 2 red (the other tests that nothing moves without text), `expandTags` 2 of 2 red. Window module
(`testskyrimsejournalwindows.cpp`, over the headless backend and a fake package): 8 of 8 red against a stub module, then the
line-breaking test added later, 1 of 1 red. Total 39 new tests, 33 were red first, 6 test absence; all pass. The first green
build showed a real bug the tests caught: `log()` returned elements pointing into a temporary copy of the quest's log (a `?:`
between an lvalue and a prvalue); it answers by reference now. `components-tests` area subset (Skyrim, Ui, Quest, Lua, Papyrus,
Dialogue, Inventory, Hud: 2,025 tests, 50 skipped for data) and `openmw-tests` (Lua, Neutral, Extension, Skyrim: 784, 2 skipped)
pass. Existing tests edited: none.

**In game** (`tools/journal_check/check.sh`, Bannered Mare, five masters, `[Game] neutral world`, `neutral actors`, `[GUI] rmlui`,
`[extension.skyrimse] papyrus` and `journal`, `journal test quests = 6`, fixed frame time, build
`I:\Projects\OpenEden-work\combat5\build`, runs `run_jra`, `run_jrb`, `run_jrc`): the real `J` key (a synthetic SDL key through
`OPENEDEN_RMLUI_INPUT_SCRIPT`) opens the window (GUI mode, the world pauses), real mouse clicks select quests and switch to the
`Completed` list, a second `J` closes it and the crosshair and HUD come back. Frames viewed: six running quests by name (A Daedra's
Best Friend, A Night To Remember, Kyne's Sacred Trials, Telrav's Request, The Forsworn Conspiracy, The Shill Job), each with its
real log entry (for example "I ran in to a talking dog named Barbas who said he wanted my help. He's waiting outside Haemar's
Shame."), the `Completed (1)` list with The Blessings of Nature and its text. Three runs: the `JOURNAL` and `PAPYRUS started` log
lines are identical (the Papyrus summary's script-time label moves by one second between runs because the host starts when the
dialogue worker finishes, which depends on real time: existing behaviour), and the journal region of the four window-open
screenshots differs by at most 8 grey levels (the scene behind the translucent window flickers), 0 pixels over 8.
**Without the test aid a fresh session lists nothing:** after 45 s of script time the Papyrus host has set no quest stage
(`stage sets 0`); 418 quests are running, 137 of them with no name and 281 with a name but neither log text nor a displayed
objective (`JOURNAL rebuilt: ... (of 418 active quests ...)`). The first real stage sets need the Papyrus load events (the other
agent's task 0181 slice 2b) or dialogue.
Cost: `revision()` is two integer reads; a rebuild (418 active quests, 2,274 in the index) happens only when the state changed;
the overlay's frame cost was not measured separately.

**Morrowind unchanged.** `tools/worldobjects_check` 110 objects, `OK` against the parent build (`combat4`); the fixed Balmora
frame (`--start Balmora --random-seed 7`, frame 600, fixed frame time) has SHA-256 prefix `1c5d47ad0b5dc32d` for the parent, for
this build with the settings off, and for this build with `neutral world`, `neutral actors`, `[GUI] rmlui` (debug panel off),
`papyrus` and `journal` on. No log line about the journal script appears in a Morrowind session.

**Findings.** (1) The engine's own `J` handler already makes the mode `Journal`; the script uses that as its key (no second
handler). When the pack is not ready the Morrowind journal window shows as before. The replacement happens one frame after the
mode appears, so the MyGUI journal may be built for a moment (not seen in a frame; a "book open" sound may play: not checked, no
audio device). (2) **The bitmap font draws only the first line of a text element that wraps** (a wrapped `ui.Text` showed one line
and no more, in a box that was two lines tall); the window breaks lines itself (52 characters). That is a defect of
`components/ui_rml`'s font or render path, not touched here. (3) Skyrim log and objective texts contain `<Alias=...>` and
`<Global=...>` tags and the UI takes `<` for markup: `expandTags` handles it. (4) Which string table holds a QUST log or objective
text is from memory (DLSTRINGS): the code tries the expected table, then the other two; in the data the log texts of the five
masters resolved.

## Decisions for toddhow
1. **Objectives are not displayed yet.** `PapyrusHost` keeps the displayed and completed flags in private maps and another agent is
   editing `papyrus_host*`, so this task could not read them. `docs/tasks/0185-papyrus-host-objective-accessors.patch` (applies to
   `4f665dcd82`, checked with `git apply --check`) adds `objectiveState(quest, index)` and `objectiveRevision()`; then
   `startJournal` needs two lines: `hooks.mObjective = [this](FormId q, std::uint16_t i) { const auto s = mPapyrus->host().objectiveState(q, i); return journal::ObjectiveFlags{ s.mDisplayed, s.mCompleted }; };`
   and `hooks.mObjectiveRevision = [this] { return mPapyrus->host().objectiveRevision(); };` (`mPapyrus` can be null: guard
   it). The window, the bindings (`objectives`, `objective_done`) and the pure `build` are written and tested with a fake hook, so
   nothing else changes. Apply the patch when the other agent's work has merged, or say so and I apply it now.
2. **Which quests are listed.** Named, running or completed, with a log text or a displayed objective. The real game also hides quests
   until their first stage; RECALLED-VERIFY.
3. **Which entry of a stage.** The first entry with a text; the entries' own conditions are not evaluated (a stage with two
   conditional entries may show the wrong one).
4. **Alias tags** become the alias's editor name (`<Alias=Staff>` shows `Staff`), not the reference's name; other tags become `...`.
   Filling them properly needs the alias fills and the display names.
5. **Failed quests** (a stage whose entry has the fail flag) go in the Completed list with the status `failed`.
6. **Order** is by name, case-insensitive; the game sorts by quest type and recency. Quest types and tracking markers are out of scope.
7. **Test aid.** `journal test quests = N` moves real quests to a stage with a text, so a fresh session has something to show; it
   is a developer aid like the `combat test ...` settings and is not in the test profile.
8. **Text wrapping** needs a fix in `components/ui_rml` (finding 2); until then every wrapped Lua text shows one line.
9. The journal is main-thread only with no lock, like the inventory (the Lua update runs on the main thread in this tree).
10. **Not run:** the Linux gate, clang-format 14 (formatted by hand to 120 columns), clang-tidy, the Lua integration tests, a fresh
    review, a gamepad, saving a game with the journal open, a quest with 64 or more log entries in game, the real objective flags.

## Follow-up: objectives (2026-11-15, branch `journal2` from `31ecf3a8ff`)

Decision 1 done. `PapyrusHost::objectiveState` and `objectiveRevision` (the patch file, plus `++mObjectiveRevision` when a restored
state replaces the flag maps); `startJournal` sets `hooks.mObjective` and `hooks.mObjectiveRevision` (both answer "nothing" when the
Papyrus host is off). Tests first: `SkyrimPapyrusObjectives` (2: a script's `SetObjectiveDisplayed` is read back with a revision that
moves only when a native is called; the flags and a non-zero revision come back after a restore) were red against stub accessors
(2 of 2), then pass. `journal test quests` now also logs each moved quest's id and objective indices (`JOURNAL test quest <id> objectives: ...`).
In game: `SkyrimPapyrusDemoObjectives` (gated by `OPENEDEN_PAPYRUS_DEMO_DIR_OBJ`, `OPENEDEN_DEMO_QUEST`, `_OBJ`, `_OBJ2`) wrote
`oedemotick.pex`; with the existing `oedemo.esp` (activators in the Bannered Mare that carry it) `tools/journal_check/check.sh`
(`DEMO_DIR`, `DEMO_ESP`, input `pick_quests.txt`) showed The Shill Job (quest 89380 as a decimal form id, objectives 10 and 20) with
`[ ] Plant the Evidence in WealthyHome in City` and `[x] Return to Vex`; the other five quests have no displayed objective and show
none. Three runs: identical `JOURNAL`/`PAPYRUS started` lines, 0 pixels over 8 in the window region. Morrowind unchanged as before
(`1c5d47ad0b5dc32d` three times, `worldobjects_check` OK). No real start-up script displays an objective in the first 45 s of
script time, so a fresh session still shows none by itself.
