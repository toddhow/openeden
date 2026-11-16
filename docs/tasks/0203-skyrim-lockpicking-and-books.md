# Task: Skyrim lockpicking and books (milestone M-S6, follow-up of tasks 0184, 0177 and 0200)

Status: BUILT (Windows only, local commits on branch `lockbooks` from `07ea289d15`, not pushed). toddhow asked for
"as much Skyrim work as possible overnight" on 2026-11-15.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.17 (inventory, items), M-S6; tasks 0115 and 0128 (locks),
0177 (saves), 0182 and 0184 (the inventory windows, keys), 0186 (spells), 0200 (skills and levelling).
Goal: a locked Skyrim door or container with no key can be picked with a lockpick in an RmlUi minigame (a successful pick
is saved and trains Lockpicking); a BOOK in the inventory can be read in a window, a skill book raises its skill once, a
spell tome teaches its spell and is used up.
Constraints: behind `[Game] neutral inventory` + `[extension.skyrimse] inventory` + `[GUI] rmlui`, plus the new pack
setting `[extension.skyrimse] lockpicking and books` (false by default, declared in `extension.json`). Every tuning number
is RECALLED-VERIFY and lives in a named settings struct. With the setting off nothing changes.

## 1. Survey (read at `07ea289d15`)

**Locks.** `openeden::sdk::game::LockComponent{locked, level, key}` (`sdk/include/openeden/game/world_model.hpp`);
`game_packs/skyrimse/src/neutral_world.cpp` fills it from XLOC (a lock that is present is locked, the level byte is raw).
The engine decides in `MWClass::Neutral::activate` (`apps/openmw/mwclass/neutral.cpp`): `decideNeutralLock`
(`mwworld/neutrallock.hpp`) returns `Locked` with no key and a `RefusalAction` follows; an opened lock stays open through
`NeutralLockState::unlock` (saved by task 0177). Lock picking was out of scope in task 0128 (decision 1). Keys from the
inventory come through `PlayerItemSource` (task 0184), found by `dynamic_cast` on the game's registered source.
**XLOC, measured on `Skyrim.esm` (1,277 locks, `I:\Projects\OpenEden-work\combat5\lb\locks.py`): every XLOC is 20 bytes;
level byte 0 (278), 1 (15), 25 (278), 50 (342), 75 (144), 100 (54), 255 (166); flag byte 0 (1,135) or 4 (142); 447 name a
key.** The tiers (Novice 0 to 24, Apprentice 25 to 49, Adept 50 to 74, Expert 75 to 99, Master 100 to 254, "requires key"
255) are what the values cluster on; 255 means the lock cannot be picked (RECALLED-VERIFY, the data only shows that 255
exists and 166 locks have it).

**Lockpick.** MISC `Lockpick`, form 0x0000000A (it is in the Bannered Mare's NPC inventories in the 0184 log). The inventory
service already has the item and the model (`InventoryModel::count`, `consume`).

**BOOK.** Measured on the five masters (`lb\scan.py`; 821 + 5 + 65 + 15 + 148 records): DATA is always 16 bytes
`flags u8, type u8, unused u16, teaches u32, value u32, weight f32`; DESC is always present (a 4 byte id into
`<plugin>.DLSTRINGS`, length-prefixed entries, `StringTable` in `records.hpp`); flag bit 1 "teaches skill" (90 books in
`Skyrim.esm`, exactly 5 for each of the 18 skills, the `teaches` value is the actor value 6 to 23 = skill slot + 6), bit 4
"teaches spell" (94 + 8 + 9, `teaches` is a SPEL form id: `SpellTomeThunderbolt` teaches 0x10F7EE), bit 2 "cannot be taken"
(16, Dragonborn), type is 0 in all; other flag values 0. `inventory.cpp`'s `decodeData` already reads value and weight at
offsets 8 and 12 and calls the record "decoded" at size 16 (nothing else of the BOOK is kept: no DESC, no teaches).

**Skills.** `progression::PlayerProgression::useSkill(slot, amount)` (experience only); there is no "raise one level" call.
`ProgressionService::skillUsed` is what other parts call; `Slot::Lockpicking` is 8.

**Spells.** The player has no spell list: `SkyrimCombat::setTestPlayerSpell(editorId)` (task 0186) names one spell. A
learned spell is kept in the new model and, when no test spell is set, becomes the player's cast spell through that call.

**Windows.** `files/data/openeden/skyrimse/inventory_windows.lua` draws rows with an optional extra button
(`useLabel`); the global/player script pair pattern of `inventory_global.lua` / `inventory_player.lua`; the engine tells
scripts about a container through `sendExtensionEvent` (`OpenInventoryAction`). Mutate functions run only from a global
script on the main thread in call order (`bindings.hpp`).

## 2. Design

**Pure model (`lockbooks.*`).**
- `LockTier tierOf(level)`, `bool pickable(level)` (false for 255 and for negative or absurd values), `tierName`.
- `PickSettings` (all named, all RECALLED-VERIFY): window half-width in degrees per tier (20, 14, 9, 5, 2.5), plus 0.05
  degrees per Lockpicking point (so skill 100 adds 5), at least 1; `falloff` 60 degrees (how fast the turn limit shrinks
  outside the window); `turnSeconds` 0.6 (a full turn inside the window); `relaxPerSecond` 2.0; `strainSeconds` 0.9 (a pick
  held against the lock breaks after this long); `strainRecover` 2.0; experience on success `10 x (tier + 1)`.
- `Attempt` (the minigame, pure, no allocation): `begin(placement, level, skill, settings)`, `update(dt, angle, turning)`
  with angle in [-90, 90] degrees and a turn flag. The sweet spot is a deterministic function of the placement id
  (`sweetSpotOf`), so it is the same for every attempt on one lock (as in the game). The turn the lock allows is 1 inside
  the window and falls linearly to 0 over `falloff` degrees outside it; while turning, progress rises towards that limit
  (at 1/`turnSeconds`); a pick held turning against a limit it cannot pass takes strain; strain at 1 breaks the pick
  (`Broke`); progress 1 opens the lock (`Opened`); not turning, progress relaxes and strain recovers. State:
  `Idle, Picking, Opened, Broke, Cancelled, Refused`.
- Books: `BookRecord{id, plugin, flags, type, teaches, value, weight, name, desc}` with `decodeBook`, `BookIndex` (later
  plugin wins), `parseBookRecords(plugin, order, limits, out)` (the pack's usual hostile-input caps), `BookKind
  {Plain, Skill, Spell}` from the flags (skill wins over spell if both are set, a data error counted).
- Book text: `renderBookText(raw, pageChars) -> vector<string>`. Subset of the markup: `<p>` and `</p>` (paragraph break),
  `<br>`, `<div>`/`</div>` (paragraph break), `<font ...>`/`</font>` (dropped, the text is kept), `<img ...>` (becomes
  `[picture]`), `[pagebreak]` (page break), the entities `&nbsp; &amp; &lt; &gt; &quot; &apos; &#N;`, CR/LF normalised,
  any other tag dropped, Windows-1252 converted to UTF-8, runs of blank lines collapsed. Pages are cut at paragraph
  boundaries near `pageChars` (1,100) and, for a longer paragraph, at the last space. NOT rendered: alignment (`align=`),
  font size, face and colour, pictures, links.
- `LockBooksModel`: the picked locks (placement ids, sorted set), the books read (form ids), the spells learned (in
  order), a counter of picks broken. `LockBooksChunk` is the save chunk `lockbooks` version 1: u32 locks then u64 each
  (ascending), u32 books then u32 each (ascending), u32 spells then u32 each (order kept), u32 broken; counts capped
  (4096 locks, 4096 books, 1024 spells); anything damaged or newer refuses the whole chunk and changes nothing.

**Service (`lockbooks_service.*`).** `LockBooksService` over the inventory service, the book index (read on a worker like the
item table, never waited for) and hooks: `mInputs`, `mResolveText(plugin, kind, id)` (string tables through the VFS file
source, parsed lazily on the main thread and cached), `mName`, `mSkill()` (Lockpicking), `mSkillUsed(slot, amount)`,
`mRaiseSkill(slot)`, `mSpellKnown(spell)`, `mLearnedSpell(spell)`, `mLog`. Namespace `skyrimse.lockbooks` 1.0:
- Read: `ready()`, `revision()`, `picks()` (Lockpicks held), `lock_state()` ("none", "picking", "opened", "broke",
  "cancelled", "refused"), `lock_reason()`, `lock_tier()`, `lock_level()`, `lock_progress()`, `lock_strain()`,
  `lock_window()` (the sweet window in degrees, for the art), `lock_placement()`, `lock_picked(placement) -> bool`,
  `lock_serial()` (counts every `pick_begin`, also a refused one: the window waits for it to move, so an old refusal is
  not taken for the answer to a new begin), `book_known(item) -> bool`,
  `book_title(item)`, `book_kind(item)` ("book", "skill", "spell"), `book_teaches(item)` (skill name or spell name),
  `book_pages(item) -> string[]`, `book_result(item)` (what reading did, "" if nothing), `book_read(item) -> bool`,
  `spells() -> string[]`.
- Mutate: `pick_begin(placement, level)`, `pick_input(angle, turning, dt_ms)`, `pick_cancel()`, `read_book(item)`.
  `pick_begin` refuses with a reason when there is no Lockpick, the level is 255 or the service is not ready.
  A broken pick removes one Lockpick from the inventory (the attempt ends `Broke`; `pick_begin` starts the next).
  Success records the placement as picked, trains Lockpicking (`mSkillUsed(8, amount)`), and the chunk keeps it.
  `read_book`: a plain book or note only shows; a skill book (first time) calls `mRaiseSkill` and is marked read (a
  second reading shows the text and changes nothing); a spell tome (first time) learns the spell (`mLearnedSpell`), marks it
  read and removes one copy from the inventory (a second tome of a known spell is read and kept, nothing learned).

**Engine.** `PlayerItemSource` 0.2 (additive, trailing virtuals with default bodies): `lockpickingOn()` and
`lockPicked(placement)`; `SkyrimCellSource` implements them from two lookups the pack sets. `MWWorld::decideNeutralLockpick`
(pure, `neutralinventory.*`) decides: the lock is `Locked`, the player activates, `[Game] neutral inventory` is on and the
source says lockpicking is on: send the event `openeden_skyrimse_lock_activated` (placement, base, kind, level, name) and
do not refuse; otherwise as before. A lock the source says was picked is `alreadyUnlocked` (and is unlocked in the engine's
own state too, so the engine's save keeps it as well).

**Lua.** `lockpick_global.lua` (the event, `pick_begin`, the minigame inputs) and `lockpick_player.lua` +
`openeden/skyrimse/lockpick_window.lua` (the window: the pick angle, keys A and D (and Left and Right) turn the pick, Space
turns the lock (the keys are polled with `input.isKeyPressed` each frame; see the stage log), buttons for the mouse
(`<`, `>` nudge by 5 degrees, one click on `Turn` turns for 0.4 s), Esc and `Cancel` leave, a status line, a progress bar, the picks left) and
`books_global.lua`, `books_player.lua` + `openeden/skyrimse/book_window.lua` (title, page text, Previous and Next, Close,
the effect line). `inventory_windows.lua` gets one hook: a row with `category == 'book'` and an `env.read` shows a `Read`
button. `builtin.omwscripts` lines `REQUIRES=core.skyrimse.lockbooks>=1.0`.

## 3. Stages
1. This spec.
2. Pack pure model: tiers, attempt, books, text rendering, model, chunk (components-tests, generated data and real data).
3. Pack service and bindings; pack wiring, `extension.json`, the skill hook.
4. Engine decision, event and SDK source (openmw-tests).
5. Lua windows (headless backend tests).
6. In game: pick `Angarvunde01`'s door (or a locked chest), read a real book, learn a spell tome; saves; three runs equal;
   Morrowind unchanged.

## Acceptance criteria
- [x] Tiers, the attempt rules and the sweet spot are as the spec says and deterministic; a lock at 255 is refused.
- [x] A broken pick costs one Lockpick; success trains Lockpicking, is saved and survives a save and load.
- [x] A skill book raises its skill by one level once; a spell tome teaches its spell once and is used up; both survive
      a save and load; reading again changes nothing.
- [x] Book text renders the stated subset; a hostile string cannot crash or allocate without bound.
- [x] With the new setting off (or the inventory or GUI off) nothing changes: no namespace, no event, the old refusal.
- [x] Morrowind unchanged (`tools/worldobjects_check`, the fixed Balmora frame).

## Out of scope
Reading a book lying in the world by activating it (no neutral item can be activated yet: items have no tooltip; the
inventory's Read button is the way in), pickpocketing, lock-level perks (Novice Locks and so on), the skeleton key,
unlocking by the Papyrus `Unlock`, enchantment on tomes, the `openeden.ui` components and themes, other agents' files.

## Stage log (2026-11-16; Windows only; local commits on branch `lockbooks` from `07ea289d15`, not pushed; same hand wrote tests and code; no fresh reviewer)

**Built.** Pack: `lockbooks.*` (tiers, `Attempt`, BOOK records, `renderBookText`, `LockBooksModel`, chunk `lockbooks` v1),
`lockbooks_service.*` (the service and the namespace `skyrimse.lockbooks` 1.0), `ProgressionService::raiseSkill` (the one
hook in `progression*`), the pack wiring in `skyrimse_game_pack.cpp` (`startLockBooks`, `resolveBookString`,
`syncLearnedSpell`), the setting `lockpicking and books` in `extension.json`. SDK: `PlayerItemSource` 0.2 (`lockpickingOn`,
`lockPicked`, trailing virtuals with default bodies). Engine: `decideNeutralLockpick`, `neutralLockpickEventFields`,
`neutralLockAlreadyOpen` (`neutralinventory.*`), `LockpickAction` and the picked-lock check in `Neutral::activate`. Lua:
`lockpick_window.lua`, `book_window.lua`, `lockpick_global/player.lua`, `books_global/player.lua`, one `Read` hook in
`inventory_windows.lua` (`env.read`) and in `inventory_player.lua`, four lines in `builtin.omwscripts`. Tool:
`tools/lockbooks_check` (a copy of `tools/inventory_check` with the setting on, sound off, LB lines).

**Data (aggregates, the five masters; `testskyrimselockbooksreal.cpp`, `lb/scan.py`, `lb/locks.py`).** 1,054 BOOK records, DATA
always 16 bytes, 0 odd, 0 failed; 90 skill books (exactly 5 for each of the 18 skills, `teaches` = actor value 6 to 23), 111
spell tomes (`teaches` a SPEL form), 853 plain, 16 with the cannot-be-taken flag, 0 with both flags; 1,045 with DESC text; tags
in the texts: p 2,018, font 1,303, img 475, b 456, br 332, i 144, alias 178, div 32, u 20, global 15, hr 2, others 1 each;
text uses `\r\n` line ends and `<br>\r\n<br>` for an empty line (found by looking at a real skill book, the rule of
`<br><br>` came from it). 1,042 distinct books are in the service index in game after the load-order merge (the five masters' 1,054 records minus the
ones a later plugin overrides; not broken down further). XLOC (1,277
locks of `Skyrim.esm`): 20 bytes, levels 0, 1, 25, 50, 75, 100, 255, 447 name a key. Lock level 255 means "requires key"
(166 locks): the pack refuses it and the engine shows the pack's reason; `Angarvunde01`'s door (185905) is such a lock
(key 0x8715F, level 255), so it is refused and cannot be picked.

**Tests first, red against stubs.** Pure model `testskyrimselockbooks.cpp`: 53 of 54 failed against stub bodies (the one that
passed tests absence); service `testskyrimselockbooksservice.cpp`: 17 of 17 failed; `raiseSkill` (`...skill.cpp`): 4 of 6
failed (the 2 that passed test refusals); engine `testneutrallockpick.cpp`: 4 of 5 failed (1 tests absence). All green after.
NOT run red first: `testskyrimselockbookssource.cpp` (4 tests; the setter existed first), the real-data test, the Lua
window tests (`testskyrimselockpickwindow.cpp` 16, `testskyrimsebookwindow.cpp` 10: written after the modules; 4 one-line
mutations of the modules each failed a named test, a fifth did not apply), and the `<br><br>` rule (a test added after the
real data showed the need). Final counts (after the review round): `components-tests` without the Slang, complement, property, Papyrus and ObScript
groups and with no `OPENEDEN_SKYRIMSE_DATA`: 10,785 tests, 10,707 passed, 78 skipped, 0 failed; the real-data test
`SkyrimBooksRealData` passed in its own run with the data; `openmw-tests` 2,763 (2,761 passed, 2 skipped: the two gated
real-data tests). The Lock/Book/Raise/Lockpick groups: 111 passed.

**In game** (Bannered Mare, `OPENEDEN_QUIET_WINDOW=1`, `[Sound] master volume = 0.0`, fixed frame time, real mouse clicks
through `OPENEDEN_RMLUI_INPUT_SCRIPT`; `lb/in_full.txt`, `lb/full.sh`; frames in `lb/keep`). The activation of the strongbox
`TGRStrongbox` (placement 960311 = 0xEA737, level 25, no key) is a Lua `activateBy` at update 1000 (the strongbox cannot be
reached by the activate key from the scenario's stand: the same compromise as the key scenario of task 0184); everything
else is real clicks. The engine logs `NLOCK ... -> 3` then `NLOCKPICK container 000ea737 level 25 'Strongbox' offered`; the
window opens (`pick_0..pick_5`): a Turn at the wrong angle shows the strain bar fill and the pick breaks (`a lockpick broke,
5 left`, a new pick starts), twelve `<` clicks put the pick at -60 degrees (the sweet spot of that lock is -66.2, window
14.75), Turn clicks fill the lock bar and the window says "Unlocked" (`lock 000ea737 picked, experience 20`, Lockpicking
15 -> 17 through the progression); the second activation logs `NLOCK ... -> 1 (open)` and the strongbox window shows
Gold x68 (`pick_6`). Books (`book_0..book_5`, the I key then clicks on Read): a skill book (Alchemy 15 -> 16, "Teaches
Alchemy", 17 pages), the third page, a plain book, Twin Secrets (Enchanting 15 -> 16), the tomes of Incinerate and
Thunderbolt (each learned and removed from the inventory; the player then casts Thunderbolt, `LOCKBOOKS the player casts
Thunderbolt`, which is logged once the inventory window is closed: the game phase does not run while the interface mode
pauses the game).
**Saves.** The combined scenario saved by the menu script (`neutral saves`), loaded in a new process with no test items:
`picks=5`, `picked(ea737)=true`, `spells=[0010f7ed,0010f7ee]`, `0001afc4=true/1` (Alchemy book read), tomes `true/0`,
`lockpicking=17 alchemy=16 enchanting=16`, and `the player casts Thunderbolt` again (`lb/keep/load_log.txt`). **Three runs**
of the combined scenario give identical LOCKBOOKS, LB, NLOCK and PROGRESSION lines (28 lines, md5 `affc857f...` three times).
**Off:** with `lockpicking and books = false` the four scripts are not started ("the session's game pack does not provide
core.skyrimse.lockbooks"), the same activation logs `NLOCK -> 3`, no `NLOCKPICK`, nothing else. **Morrowind unchanged:**
`tools/worldobjects_check` (111 lines, OK) against the `combat4` build (another agent's reference build, NOT a build of
`07ea289d15`), and the fixed Balmora frame `1c5d47ad0b5dc32d` for the
`combat4` build, this build with the defaults, and this build with `neutral world`, `neutral actors`, `neutral combat`,
`neutral inventory`, `[GUI] rmlui` (no debug panel) and the pack's `inventory` and `lockpicking and books` on (with the debug
panel on, as the first attempt had it, the frame differs by the panel).

**Found on the way.** In the scripted runs the key events of the input script (Space, A) did not reach the lock window's
script while the interface mode was open (the cause was not found; the `I` key did reach the inventory script, which is how
the inventory was closed), so the window now polls `input.isKeyPressed` each frame. **Keyboard control of the lock window has
never been seen working: only its mouse buttons (`<`, `>`, Turn) were run in game; it is a hands-on check for toddhow.**
`onFrame(dt)` and `core.getRealFrameDuration()` were both 0 in the interface mode under `OPENEDEN_FIXED_FRAME_TIME` (the
game is paused); the window therefore steps by the wall clock (`core.getRealTime`, at most 0.1 s): a decision for toddhow,
because the guide forbids wall-clock time in simulation code (this is UI time, not simulation, and the pack only gets the
frame's milliseconds). The scripted timeline of `lb/in_full.txt` is tuned to frames of about 4 ms and can fail on a busy
machine (the outcome lines were equal in 3 of 3 runs). The `.dlstrings` book texts are Windows-1252 and the markup is
inconsistent (a `<font face'$HandwrittenFont'>` with a missing `=`): any unknown tag is dropped.

**Review round (an advisor read of the first commit; same hand).** Fixed: (1) a stale refusal: the pack's `refused` flag
stayed set until the next successful begin and the window took it for the answer to its own begin, so after any refusal the
next lock closed at once with the old reason; now `lock_serial` counts every begin and the window waits for it to move (tests
first, red: `everyBeginCountsSoAWindowCanTellItsAnswerFromAnOldRefusal` and `anOldRefusalIsNotTheAnswerToANewBegin`, 2 of 3
failed, the third tests the fallback for a pack without the function); (2) `syncLearnedSpell` compared the list's size only:
it now compares the size and the last spell and clears the cast spell when a load leaves no spell (no unit test: it is in the
pack's main file); (3) a dangling-else trap and an unused variable in the tests, lines over 120 columns. **Not done:** nothing
calls `LockBooksModel::clear()` (the inventory and progression services have no new-game reset either; a save without the
chunk keeps the process's state); one apprentice lock gave two skill levels in a run (15 to 17: the experience reported
for a pick is `10 x (tier + 1)` through the Lockpicking use numbers, RECALLED-VERIFY); a skill book does nothing (and is not
marked read) when `player progression` is off.

**Checks run:** `CI/check_tests_registered.sh` ok, `check_layer_rules.sh` 104 exceptions (unchanged), `check_extension_sdk.sh`
exit 0 (an SDK header changed); `check_file_names.sh` lists two files that are not mine. No existing test was edited.

**Not run.** The load phase has no activation (`place.lua` returns early, so no `NLOCK ... -> 1` after a load); the Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua integration tests, a fresh review,
keyboard control of the lock window in a real session (only the mouse path was run in game), a lock picked on a door
(the strongbox only; the door path is the same `Neutral::activate` branch and is unit-tested), a world book (not
activatable), notes, a second tome of a spell already known in game (unit-tested), an exterior lock, other languages.

## Limits and decisions for toddhow
1. **Numbers.** Every tuning number (windows 20, 14, 9, 5, 2.5 degrees; +0.05 per skill point; falloff 60; turn 0.6 s;
   strain 0.9 s; recover and relax 2 a second; experience 10 x (tier + 1); pages of 1,100 characters) is mine,
   RECALLED-VERIFY, in `PickSettings` and `kDefaultPageChars`. The sweet spot is fixed per lock (a hash of the placement).
2. **Requires key (255) and the Angarvunde door.** A level-255 lock is refused with the pack's reason in the window; it
   cannot be picked. Say if you want it pickable at a penalty.
3. **A skill book raises the skill by one level** (the experience that level needs, through the same rules as a use); a
   spell tome is used up on first reading, a second tome of a known spell is read and kept; a plain book is never marked.
4. **Reading a world book is not possible** (nothing neutral on the ground can be activated yet); the inventory's Read
   button is the way. Notes read the same way (they are BOOK records).
5. **The learned spell becomes the player's cast spell** (the last one learned) through the combat's `combat test player
   spell` channel when that setting is empty; there is no spell list or menu yet.
6. **`ProgressionService::raiseSkill`** is the one hook added to `progression*`; `PlayerItemSource` grew two virtuals
   (0.2, additive).
7. **Wall-clock time in the lock window** (see the stage log) and **keyboard control not seen in game**.
8. **Not rendered in book text:** alignment, font size, face, colour, pictures (`[picture]`), links, `<Alias=...>` and
   `<Global=...>` (dropped), tables.
9. **Not done:** perks of Lockpicking, the skeleton key, pickpocketing, lock picking in the dialogue or Papyrus (`Unlock`),
   "already picked" feedback on a door that was never activated since the load.
