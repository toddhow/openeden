# Task: Moving Morrowind's rules into the Morrowind packs, first three moves
Design doc section: `docs/design/game-packs.md` stage e of section 10 and section 11 (risks); `docs/research/0043-separating-formats-from-game-rules.md` section 3; extension doc phase P5 ("Morrowind-specific code moved out of the core into the pack step by step, with Morrowind verified to play the same after each step").
Goal: move three self-contained pieces of Morrowind code out of the core, one at a time, each behind a contract from task 0084, and prove after each that Morrowind plays the same. This spec covers **only the first three moves**; each later move gets its own spec after the third is reviewed (the rest is about 83,000 lines).
Inherited code touched, at commit `671c7676e9`: per move, below. Common to all three: `apps/openmw/mwbase/` interfaces they cross, `apps/openmw/CMakeLists.txt`, `extensions/compat_morrowind/` (`CMakeLists.txt`, `extension.json`), a new `game_morrowind/` directory (path **verify**: recommend `game_packs/morrowind/`, a new top-level folder beside `extensions/`, because a game pack is not an extension; add to the layout in the root `CLAUDE.md` when approved).
Inputs / outputs: the new `game_morrowind` pack (static first); contracts of task 0084; per move the files listed below.
Status: draft for toddhow's approval; depends on tasks 0081, 0083 and 0084. **Full loop** for every move (anything under `apps/openmw` that crosses the extension seam, plus saves in move 3).

## The three moves (smallest risk first), each with its risks

### Move 1: Morrowind's data conventions out of generic code (into `compat_morrowind`)
- **What:** the places that name Morrowind's skeleton files, units and node rules while living in generic code: `base_anim` in `apps/openmw/mwrender/animcontext.cpp`, `mwrender/npc.cpp`, `components/vsgadapters/nif/nif.cpp`, `apps/opencs/model/prefs/values.hpp`; `Constants::UnitsPerMeter` uses (27 files outside tests mention `UnitsPerMeter`; how many are the Morrowind constant and how many generic: **verify**, the first task of the move is that list); the `bip01` rule via task 0082's `SceneOptions`. They are asked of `openeden.game` (`GameContract`: units, default skeleton; extended with the few other conventions found: **minor version** of the contract, 1.1).
- **Risk:** low per file, wide in count; a missed site silently keeps the Morrowind default for another game. The `Constants` value may be used by tuning and physics values that must not change.
- **Verification:** unit tests; the Lua tests; a frame comparison in a Morrowind interior and exterior with the setting at its default (viewed before commit); `grep` check that no listed file names the Morrowind constant.

### Move 2: crafting rules (alchemy, enchanting, repair, recharge)
- **What:** `apps/openmw/mwmechanics/alchemy.*` (632 lines in the `.cpp`), `enchanting.*` (417), `recharge.*` (110), `repair.*` (115): 1,274 lines of `.cpp`, with no `mwrender`, `mwgui` or `mwphysics` include in `alchemy.cpp` and `enchanting.cpp` (grep at the commit above; the headers and the other two files are **verify**). They are called by `mwclass/apparatus.cpp`, `ingredient.cpp`, `potion.cpp` and `mwgui/alchemywindow.cpp` and the equivalent windows, and from `mwscript`.
- **Contract:** `GameRules` gets `craft(...)` entry points (design doc 5.5), derived by removing Morrowind-only data, not by copying the Morrowind classes; the windows call the contract.
- **Risk:** medium. The windows (`mwgui`) still include the types; the contract must carry enough for the UI without leaking `ESM::` structs, which means a first record-view type (`openeden.records`). GMST-based formulas read the store; they keep their source (root guide: Morrowind values keep their existing source). Random numbers: the existing `Misc::Rng` use must keep the same draw order (determinism).
- **Verification:** the existing tests for these classes if any (**verify**: none were found by name at the commit above, so the task writes characterization tests first, from a fixed-seed scenario, before the move); a recorded scenario that brews, enchants and repairs with a fixed seed gives identical results before and after (numbers in the stage log).

### Move 3: dialogue and journal (with their save state)
- **What:** `apps/openmw/mwdialogue/` (3,340 lines: `filter.*`, `selectwrapper.*`, `dialoguemanagerimp.*`, `journalimp.*`, `quest.*`, `topic.*`, `scripttest.*`, `keywordsearch.*`) and the save records they write: `ESM::DialogueState`, `ESM::JournalEntry`, `ESM::QuestState` (`components/esm3/dialoguestate`, `journalentry`, `queststate`; used in `dialoguemanagerimp.cpp` and `journalimp.cpp`). It is the first move that needs the **save chunk handler**; `MWBase::DialogueManager` and `MWBase::Journal` are the existing interfaces it implements.
- **Includes today:** `mwbase` 26, `esm3` 20, `mwworld` 18, `compiler` 14, `mwmechanics` 7, `mwscript` 6 (research note 0043): the smallest outward web of the rule directories, but it calls the script compiler, so the script host contract (design doc 5.6) must exist in a first form (compile and run a result script) before this move; if it does not, this move waits and the order becomes 2 then 3.
- **Risk:** high of the three. Saves must keep the same bytes; the dialogue UI (`mwgui/dialoguewindow`) and the Lua bindings (`mwlua` `dialogue` and journal types) call it.
- **Verification:** save round trip (load an existing save made at the fork point, save, compare the dialogue/journal/quest records byte for byte; load the result); the 32 Lua tests; a scripted conversation (topics, a persuasion, a journal update) with a fixed seed gives the same results before and after.

## Constraints
- One move per branch; each move ends with the tree building and every test passing; a move is **reverted whole** if its comparison fails.
- No behaviour change, no new setting. If a move cannot keep Morrowind identical it stops and asks.
- The layer rules checker (0081) gains no new exception; each move removes the ones it fixes.
- No file is rewritten for style; the code moves with its history (`git mv`) and its OpenMW notices and AUTHORS entries (root guide guardrails, GPL).
- Namespace: the moved code keeps `MWMechanics`/`MWDialogue` names in this task; renaming is a separate, later change (no drive-by changes).

## Acceptance criteria
- [ ] Per move: the files above live in the Morrowind pack's build target and not in `openmw-lib`; the core builds and starts with `game=morrowind`
- [ ] Per move: unit tests (`components-tests`, `openmw-tests`, `eden-studio-tests`) and the 32 Lua tests pass on Windows and Linux (`-Werror`)
- [ ] Per move: the named comparison (above) is identical and its numbers are in the stage log
- [ ] Move 3: the `.omwsave` bytes of the moved records are identical after a load and a re-save, and a save made before the move loads after it
- [ ] The game with the pack disabled: refuses to start a game with a clear message (design doc 7), Eden Studio still starts
- [ ] No new third-party dependency; no game asset added
- [ ] A fresh reviewer reads each move's diff against the spec, with attention to lifetimes, threading, determinism, allocations and the behaviour with the pack present (the only mode that plays)
Out of scope: `mwclass`, `mwworld`, `mwscript`, `mwgui`, `mwlua` and the rest of `mwmechanics` (later specs); the editor's use of the records (it stays on the Morrowind structs until record descriptors exist); Lua descriptors; renaming.

## Open questions (recommendation first)
1. Order 1, 2, 3 as above? Recommend yes; move 3 only after the script host contract has a first form.
2. Where does `game_morrowind` live? Recommend `game_packs/morrowind/`, built as a static target `game_morrowind` linked into `openmw`.
3. Do the moved files keep `MWMechanics`-style names and `m` member prefixes? Recommend yes (inherited-code rule); only new code follows the SDK style.
4. Which formula tests exist? Recommend writing the characterization tests first (red against a stub is not possible for existing behaviour: they must pass on the old code and on the new).
