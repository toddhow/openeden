# Task 0213: the neutral game clock after a load, trespass, and crafting benches only

Status: see the stage log at the end. Windows only; local commits on branch `cleanups` from `400520e49f`; not pushed.
Design doc section: decision 0018 (combat, crime), tasks 0177 (neutral saves), 0203 (lockpicking), 0204 (crime), 0210 (wait and sleep), 0199 (crafting).
Approval: toddhow, 2026-11-16, "as much Skyrim work as possible overnight"; the three issues are the open items of tasks 0204 (decision 3: trespass not built), 0210 (decisions 2, 5, 6) and 0203.
Inherited code touched: `apps/openmw/mwworld/worldimp.cpp` (`ensureNeededRecords`: the default globals move out unchanged but for one value) and `apps/openmw/mwworld/globals.cpp` (`readRecord`: one coercion). `neutralsave.*` is NOT touched (see 1.2).
Constraints: trespass behind `[extension.skyrimse] crime` (default off); the clock fix applies to every neutral save (it fixes a wrong default); Morrowind saves stay byte-identical (it defines `GameHour` itself, as a float).
Out of scope: owner warnings and "you are trespassing" speech (INFO conditions fn_144/145 stay unanswered), leaving the cell to clear a warning, factions the player joins (the stand-in is in `PlayerFaction` only), bed use by the player's pose, a guard that comes for trespass alone beyond the existing bounty rules.

## 1. The game clock after a load

### 1.1 Survey (citations at `400520e49f`)

- The date lives in `MWWorld::DateTimeManager` (`mwworld/datetimemanager.cpp`): `mGameHour` is a float, `advanceTime` writes it back with `globalVariables[Globals::sGameHour].setFloat(mGameHour)`; a load calls `setup(Globals&)`, which reads `getFloat()` of the global (`World::readRecord` default branch, `Globals::readRecord`).
- A neutral (Skyrim-only) session has no `Morrowind.esm`, so `World::ensureNeededRecords` inserts `generateDefaultGlobals()` (anonymous namespace, `worldimp.cpp` line ~199). Its `gamehour` entry is `ESM::Variant(0)`: an INTEGER variant (`VT_Long`). `timescale` beside it is `30.f`.
- `ESM::Variant::setFloat` is `std::visit(SetValue(value), mData)` and `SetValue::operator()(int32_t&)` is `value = static_cast<int32_t>(mValue)` (`components/esm3/variant.cpp` line 44). So every frame the clock writes the hour TRUNCATED into the global; the save writes that global (`Globals::write`, `REC_GLOB`); the load reads 16 for 16.7.
- Morrowind defines `GameHour` in `Morrowind.esm` as a float, so it never meets the defaults (they are inserted only when missing).

### 1.2 Cause and fix

Cause: the default `gamehour` is an integer global. It is the engine's, not the neutral save path's (`neutralsave.*`, `loadgame`) and not the pack's. Proof: the `rest_sv0` save of task 0210 (no rest at all, save at 0.43 loads at 0.00) and the unit test below, which writes and reads the global.

Fix: `defaultglobals.{hpp,cpp}` (new, `apps/openmw/mwworld`) holds the table (moved out of `worldimp.cpp` so a test reaches it) and `gamehour` is `ESM::Variant(0.f)`. A SAVE made before the fix holds an integer `gamehour`, and `Globals::readRecord` replaces the whole record, type included, so a load of an old save would bring the truncation back for the rest of the game. `Globals::readRecord` therefore keeps a FLOAT definition when the saved value is an integer (the integer is converted, the type stays float). That cannot change a Morrowind save: its globals have the type the content file gives.

Tests (`apps/openmw_tests/mwworld/testneutralclock.cpp`): the default is a float; advance 16.7 hours, write the globals, read them into a fresh store, `setup`: the hour equals 16.7 within a second of game time (0.0003 h); the day counters survive; a saved INTEGER `gamehour` (an old save) loads into a float global and keeps its type; a Morrowind-style store (float `gamehour` defined by content) round-trips byte-identical (the globals' bytes of a before and an after write are equal).

## 2. Trespass

### 2.1 Survey

- `Kind::Trespass` exists and `bountyFor` returns the CRVA trespass number (`crime.cpp`); nothing raises it (task 0204 decision 3). Real values: the 8 hold factions 5, others 0.
- `CrimeSystem` keeps `mPending` and evaluates each pending crime once in the next `update` (`evaluate`: witnesses by `witnessSees` and an eye height ray, one bounty per witnessing crime faction).
- Owners: `SkyrimWorldView::ownerOfPlacement` (REFR XOWN); the player's things are NPC_ 7 and `PlayerFaction` (`kPlayerFactionId`, `actor_mechanics.hpp`). `onContainerTaken` and the rest bed hook (`startRest`) both test that.
- **Cells.** `CellRecord` has `mFlags` (DATA; `CellFlags::PublicArea` 0x20 is already named) but NO owner: XOWN is read for REFR only (`records.cpp` ~line 874). Data (Skyrim.esm, `I:\Projects\OpenEden-work\ragdoll5\py\cellown.py`): interior cells: 314 private and unowned, 143 private and owned, 46 public and unowned, 87 public and owned; exterior: 16,978 private, none owned. XOWN is 4 bytes in all 230. **The Bannered Mare is PUBLIC (flags 0xa1) and owned by 0x9e1b4**: entering it is not trespass; its beds are. `WhiterunHouseGrayMane` (0x165b2) is private, owned by 0x93b1c, and holds Fralia and Avulstein with the Whiterun crime faction (`I:\Projects\OpenEden-work\ragdoll5\py\cellwit.py`; 20 such homes are listed there).
- **Where the player is.** The pack is not told the player's cell (task 0187's note). `SkyrimCellSource::buildCell` is called when a cell is BUILT, not when it is entered: a cell reused on the way back is not rebuilt (task 0115), a load rebuilds, and the jail teleport builds. So the cell hook cannot use it. A global Lua script sees `player.cell` every frame (`crime_global.lua` already teleports the player for jail).
- Rest: `rest_global.lua` answers `openeden_skyrimse_bench_opened`; `rest.bed_state` says `owned` for a bed whose owner is not the player (`rest.cpp bedStateOf`); the player is refused ("This bed is not yours.") and nothing else happens.
- Lockpicking (task 0203): a lock only gates the DOOR; a picked or unlocked door ends in the same teleport into the destination cell. So "a picked lock into an owned interior" is cell entry, whatever opened the door.

### 2.2 Rules (OURS unless a number says it is the data's)

1. **Cell entry.** The player enters an INTERIOR cell whose CELL has an owner (XOWN, latest plugin that has one) that is not the player's (NPC_ 7 or `PlayerFaction`; the stand-in is in no other faction) and whose PublicArea flag is clear: trespass. A public cell is never trespass, whoever owns it; an unowned cell is never trespass. Exterior cells are never trespass. Not at the first cell of a session or a load (the global script sees no previous cell), not for the jail cell the jail sends the player to (the pack knows its destination), not while the player is jailed.
2. **Bed.** Using a bed (the rest event) whose placement owner is not the player's: trespass, in ANY cell, public or not (the Mare). The refusal stays as task 0210 built it ("This bed is not yours."); the ATTEMPT is the offence. A bed the player may use freely raises nothing. (`rest test ignore owner` is a test aid that makes the bed free for the rest rules; it does not raise trespass either.)
3. **Charged only if witnessed.** The existing witness rule (`witnessSees` + the eye-height ray, a crime faction of the witness gets the bounty `CRVA trespass`). A pending trespass is evaluated a short time after the event (`mTrespassDelay`, 1.5 s: a new cell's actors need a moment to exist), then retried every `mTrespassRetry` (0.5 s) until it is witnessed (charged once, dropped) or `mTrespassWatch` (30 s for a cell, 2 s for a bed) passes (dropped, "not witnessed"), or the player leaves (any later cell entry or `left_cell` cancels the cell visit). ONE charge per visit; a bed attempt on the same placement within `mTrespassRepeat` (120 s) is not charged again.
4. Everything is behind `[extension.skyrimse] crime`; the new bindings exist only with it.

### 2.3 Design

- `records.*`: `CellRecord::mOwner` (XOWN, resolved), decoded like the REFR one; `SkyrimWorldView::cellAccessOf(cell, crime::CellAccess&)` merges the load order (latest source with a DATA or XOWN) and `findCellByEditorId` is the existing lookup.
- `crime.*` (pure): `CellAccess`, `isPlayersOwner`, `cellEntryIsTrespass(access)`; `CrimeSystem::onTrespass(place, Place)`, `onLeftPlace()`, the pending fields and the retry; `Settings` gains the five numbers above (appended, so `Settings{}` users are unchanged).
- `crime_service.*`: `ServiceHooks::mCellAccess(editorId, CellAccess&)`, `mPlacementOwner(placement)`; the service tracks the visit and the jail cell; bindings (additive, version stays 1.0 so no pinned test changes): Mutate `entered_cell(editorId)`, `trespass_bed(placement)`.
- Lua: `crime_global.lua` polls the player's cell each frame and calls `entered_cell` when it changes (never for the first cell); `rest_global.lua` calls `trespass_bed` through a `pcall` of the crime namespace when the bed is `owned`.
- Shared edits (focused): `crime.hpp/.cpp`, `crime_service.hpp/.cpp` (crime files); `records.hpp/.cpp`, `world_view.hpp/.cpp`, `skyrimse_game_pack.cpp` (hooks); `files/data/scripts/openeden/skyrimse/crime_global.lua`, `rest_global.lua`; `apps/components_tests/CMakeLists.txt`. No `neutralsave*` edit (it is not the cause).

## 3. Benches only

Survey: `crafting_global.lua` answers `openeden_skyrimse_bench_opened` for every furniture and sends `SkyrimCraftingOpen` to the player, whose window then opens (or shows the refusal "that is not a crafting bench" of `CraftingService::tryOpen`). `crafting.open` is a Mutate call (queued, runs on the main thread later), so the handler cannot read its result in the same call. `CraftingService::mTable.benchesOf(furniture)` is empty for a chair or bed.

Fix: a Read binding `crafting.is_bench(base) -> string` ("yes", "no", or "pending" while the recipe table is not built); the global script opens the window for "yes" and "pending" (the pack refuses itself later in the pending case, as before) and does nothing for "no". The script feature-detects the function (`crafting.is_bench ~= nil`), so an old pack still opens.

## 4. Stages

A. This document. B. Stubs and tests, run red (record counts). C. Code. D. In game: a save at a fractional hour; a chair opens no crafting window; trespass by the bed in the Mare and by a teleport into `WhiterunHouseGrayMane`; three identical runs. E. Morrowind unchanged (`worldobjects_check`, the fixed Balmora frame, a Morrowind save round trip). F. Changelog row.

## Stage log

2026-11-16; same hand wrote tests and code; no fresh reviewer; Windows only; branch `cleanups` from `400520e49f`; not pushed.

**1. The clock.** Cause found by the red test (no guess): the default `gamehour` global was `ESM::Variant(0)`, an integer; `Variant::setFloat` truncates on an integer. Fix: `defaultglobals.{hpp,cpp}` (the table, moved out of `worldimp.cpp`) with `gamehour` as `0.f`, and `Globals::readRecord` keeps a float definition when a save holds an integer (old saves). **Red (`openmw-tests` built with the old table behind the new seam): 5 of 9 `NeutralClock` failed**, the 4 that passed are controls (other defaults, content-defined float, integer stays integer, double insert); green 9 of 9. In game (`tools/skytrespass_check`, preAdvance 11.7 h, save, `--load-savegame`): old executable 11.8003 saved, **loads at 11.0001**; new executable 11.8003 saved, **loads at 11.8005** (one second of game time later). Morrowind: it defines `GameHour` itself as a float (never meets the defaults): fixed-time Balmora frame `1c5d47ad0b5dc32d` for the parent and the new executable (two runs each), `tools/worldobjects_check` 111 lines OK (new against the parent, hard-linked copy of the build with the parent's `openmw.exe`), the Morrowind save round trip (`mwsaveload.sh`) equal in all three pairs (new to new, parent to new, new to parent; the first new-to-new try wrote no save and a second try did: unexplained, 1 of 4 tries). **Not touched: `neutralsave*`** (not the cause).

**2. Trespass.** `crime.{hpp,cpp}` (`CellAccess`, `isPlayersOwner`, `cellEntryIsTrespass`, `Place`, `CrimeSystem::onTrespass/onLeftPlace`, a pending trespass with delay, retry and watch time, `evaluate` returns whether a witness saw), `crime_service.{hpp,cpp}` (`enteredCell`, `trespassBed`, bindings `entered_cell` and `trespass_bed`, additive, version stays 1.0), `records.{hpp,cpp}` (`CellRecord::mOwner`, CELL XOWN), `world_view.{hpp,cpp}` (`cellAccessOf`, `cellAccessByName`), `skyrimse_game_pack.cpp` (two hooks), Lua `crime_global.lua` (reports the player's cell), `rest_global.lua` (calls `trespass_bed` for an owned bed). **Tests: 46 new in `testskyrimsetrespass.cpp`, 27 failed against stubs and 19 passed (absence, refusals and settings defaults), all 46 pass; 359 of 359 selected crime, crafting and rest tests pass; `openmw-tests` 2,793 and `components-tests` Skyrim, Neutral, Extension, Papyrus and Esm subset 2,678 pass; no existing test edited.** In game (five masters; `crime`, `rest`, `crafting`, `combat ai` on, `[Sound] master volume = 0.0`, `OPENEDEN_QUIET_WINDOW=1`): (a) the real activate key on the Mare's owned bed (REFR 0x9f255, owner 0x44050): "This bed is not yours." (frame viewed), `CRIME trespass: bed`, **`trespass witnessed by saadia faction 000267ea bounty +5 total 5`**, three runs: the 3 `CRIME` and rest lines are byte-identical. (b) Teleport from the Mare (public, owned: no trespass) into `WhiterunHouseGrayMane` (private, owned by 0x93b1c): `trespass: entered`, **witnessed by eorlundgraymane and fraliagraymane, +5**; going back adds nothing; three runs byte-identical. (c) The unwitnessed, wall, hostile, repeat, watch, leave and null cases are unit tests only.

**3. Benches.** `CraftingService::benchAnswer` and the Read binding `is_bench(base)` ("yes", "no", "pending"); `crafting_global.lua` returns on "no" and feature-detects the function. **Red: 7 of 7 `SkyrimBenchFilter` failed (no binding); green 7 of 7.** In game (the script sends the engine's furniture event itself, because the neutral chair was not reached by a key in the time I had): the old script logs `CRAFT open Chair refused: that is not a crafting bench` (and sent the window the open event), the new one logs nothing for the chair and `CRAFT open Forge (forge)` for a forge (base 0xbf9e1).

**Decisions for toddhow.**
1. Trespass numbers are ours: first judged 1.5 s after the event, retried every 0.5 s, a cell visit watched 30 s and a bed attempt 2 s, the same bed charged again only after 120 s. A trespass is charged once per visit, to each witnessing crime faction (the CRVA trespass number: 5 for the holds).
2. The bed: the refusal stays; the ATTEMPT is the offence, charged only if witnessed. `rest test ignore owner` makes the bed free for the rest rules and raises no trespass.
3. Cell entry: private owned interior, any witness; a public cell (the Mare) never counts even though it has an owner; an unowned cell never counts; the first cell of a session or a load, the jail cell and the way back from the jail never count. The player is only in `PlayerFaction` (no joining exists), so any other owner is "not his".
4. A picked or unlocked door leads to the same cell entry: the lock is not looked at. Not run through a real locked door in game.
5. The cell is detected by the global Lua script reading `player.cell.name` each frame (the pack is not told where the player stands). A game with `crime` on but no Lua UI still works; the bed attempt needs `rest`.
6. The window that refuses the bed pauses the simulation, so the trespass is judged when the player dismisses it (the run needed an Escape).
7. A guard of the faction now comes for a trespass bounty like for any bounty (the existing guard rules); in the Mare there is none.
8. `[extension.skyrimse] crime` also needs the roster (`combat ai`) for witnesses.

**Not run:** the Linux gate and GCC `-Werror` (hand-checked), clang-format 14, clang-tidy, the Lua tests, a fresh review, a locked door picked in game, a chair reached by the real key, an old neutral save loaded by the new executable in game (unit tested), the clock through a Morrowind save with an integer `GameHour` (cannot happen), mutation checks.
