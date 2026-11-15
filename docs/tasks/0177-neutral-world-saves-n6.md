# Task: Neutral world saves (stage N6, milestone M-S6 first part)

Status: BUILT, Windows only, not pushed (toddhow approved starting on 2026-11-15: "Yes, start both").
Design doc section: `docs/design/neutral-world-model.md` stage N6 (section 5 table) and sections 2.3 (identity) and 10;
`docs/design/skyrim-se-implementation-plan.md` 4.23; decisions 0013 (game packs write save chunks), 0016 (neutral
world model), 0005 (extensions never touch saves; game packs do).
Goal: a session of a game pack with a neutral world (Skyrim SE) can be saved and loaded: the player comes back to the
same cell and position, objects the player moved, disabled, enabled or deleted stay that way, opened locks and held keys
come back, and the pack's own chunks go through the `GPCH` record. Morrowind saves stay byte for byte as they were.
Inherited code touched: see the survey.

## 1. Survey (read at `312e2b81c1`)

What exists, with where:

- **The refusal.** `OMW::ExtensionHost::savingRefusal()` (`apps/openmw/extensionhost.cpp`) returns "Saving is refused:
  the game pack's world has no save support yet" while `mHost.worldSources().sourceCount() > 0`;
  `MWState::StateManager::saveGame` (`mwstate/statemanagerimp.cpp`) shows it and writes nothing. A backstop:
  `ESMStore::write` (`mwworld/esmstore.cpp`) throws while `mNeutralStubs` (the stub player of task 0105).
  Pinned by `apps/openmw_tests/extension/testneutralworldsaving.cpp` and `mwworld/testneutralstub.cpp`
  (`the_stub_never_reaches_a_save`).
- **What a save writes.** `SaveGame::threadLoad` (`mwstate/savegame.hpp`) runs on a worker, writes the header record
  (`ESM::SavedGame`: content files, player name, cell name, game pack id and version `GPCK`/`GPVR`), then the
  journal, dialogue, Lua, `World::write`, global scripts, mechanics, input, windows, and last the game pack's chunks as
  ONE `GPCH` record (`mwstate/gamepackchunks.cpp`, task 0084 I4). The chunks are collected on the main thread just
  before the worker starts (`ExtensionHost::collectGamePackSave` -> `SaveChunkServiceImpl::collect`) and handed back on
  the main thread in `LoadGame::postLoad` (`restoreGamePackSave`). A record count in the header must match.
- **`World::write`** (`mwworld/worldimp.cpp`): `REC_RAND`, `mStore.write` (the dynamic records, the player's `NPC_`
  among them after `movePlayerRecord`), `mWorldModel.write` (one `REC_CSTA` per cell with `hasState()`), `mPlayer->write`
  (`REC_PLAY`: the player's `ObjectState`, the cell id, the last known exterior position), globals, weather,
  projectiles, `REC_ENAB`, `REC_CAM_`.
- **Loading.** `LoadGame::threadLoad` switches on the record name on the worker and calls `World::readRecord`;
  `Player::readRecord` looks the player's cell up with `WorldModel::findCell(player.mCellId)` and only warns when it is
  absent; `LoadGame::postLoad` (main thread) calls `World::saveLoaded`, `setupPlayer`, `renderPlayer`, then, if the player
  Ptr is in a cell, `changeToCell(cellId, position, false, false)`, else falls back to Morrowind's exterior (0, 0).
  An unknown record is skipped with a warning (the default branch), so an older build reads a newer neutral save
  (the record is ignored) and a newer build reads an old save.
- **Neutral cells.** `WorldModel::emplaceNeutralCellStore` / `findNeutralCell` / `findNeutralExterior`
  (`mwworld/worldmodel.cpp`) build a `CellStore` whose `Cell` wraps a synthetic `ESM::Cell`
  (`makeNeutralCellRecord`: id `<game>:c<cell id>`, editor id as the name), keep the pack's `WorldCell`
  (`CellStore::setNeutralCell`, `neutralObject(placement)`) and load references as `LiveCellRef<NeutralBase>`
  (`CellStore::loadNeutralRefs`). A reference's `RefNum` is the placement id with the reserved content file
  `kNeutralRefNumFile` (`neutralRefNum`, `neutralcell.cpp`). `CellStore::get<NeutralBase>()` (non-const) sets
  `mHasState`, so a loaded neutral cell always "has state" and `WorldModel::write` would have written it through the
  Morrowind cell writer (which cannot read it back: the `NeutralBase` list is not a save type).
- **Session state outside cells.** `MWWorld::neutralLockState()` (`mwworld/neutrallock.*`): keys held and locks opened.
  Not saved so far (its header says so).
- **Identity** (design doc 2.3, `neutralBaseId`, `neutralRefNum`): a placement is `(game id, cell id, placement id)`; the
  pack's ids are `plugin index in the load order << 24 | local form id` style values resolved by the load order, so they are
  stable for a fixed load order, not across load orders. The save therefore needs the load order (see stage 3).

## 2. Design

The neutral world is rebuilt from the pack at every load; a save keeps only the difference.

**One engine record, `NWLD`** (subrecord `NWDA`), written only when a neutral session saves with `[Game] neutral saves`.
Its bytes are `MWWorld::NeutralSaveState` (`mwworld/neutralsave.hpp`): a canonical, versioned, capped little-endian blob.

- *Identities.* An interior cell is `(game id, the pack's cell id)`; an exterior cell is `(game id, the pack's world id,
  grid x, grid y)`; a placement is the pack's placement id inside its cell. None of these is run-specific: they are what
  the plugins fix (plugin plus local form id, through the load order).
- *What a cell saves.* For each loaded neutral cell, for each placement: the current state (`RefData` position and
  rotation, scale, enabled, count 0 meaning deleted) compared with what `makeNeutralCellRef` builds from the pack's
  object (`diffNeutralObject`). Only differences are saved: moved (position and rotation), scaled, enabled changed,
  deleted. An unchanged cell writes nothing. **Neutral actors are left out**: they walk by the pack's schedule and are
  placed again at every load (actor state is out of scope).
- *Session state.* The opened locks and held keys (`NeutralLockState`), sorted.
- *The player.* The player's cell key (kind, ids, grid). The player's position, rotation and stats are the normal
  `REC_PLAY` and dynamic `NPC_` records; `ESMStore::write` no longer refuses the stub player when the setting is on
  (`allowNeutralStubWrite`): the stub is static content that every neutral session creates the same way.
- *Writing.* `World::write` writes the record between the store and the world model; `WorldModel::write` and
  `countSavedGameRecords` skip neutral cells. `World::countSavedGameRecords` adds one.
- *Loading.* `World::readRecord` keeps the bytes (it runs on the loading worker; cells are built on the main thread
  only). `World::saveLoaded` (main thread, from `LoadGame::postLoad`) decodes, rebuilds the cells from the pack
  (`WorldModel::restoreNeutralSave`), applies the changes, restores locks and keys and gives the player its cell
  (`Player::setCell`), so `postLoad`'s `changeToCell` finds it. A damaged record or a player cell the pack no longer
  has fails the load with a message; a changed placement the pack no longer has is dropped with a warning.
- *Refusals.* With the setting off nothing changes (saving stays refused, the pinned tests hold). With it on, saving is
  refused only for a session error. **Combat and actor state are out of scope:** a fight or a live ragdoll is not saved
  and is not needed to refuse for: the neutral actors are rebuilt at every load (a fight in progress ends, a corpse is
  alive again). This is recorded in the final report as a decision for toddhow.

**The pack's chunks (stage 3).** The existing API is `Host::getSaveChunkService()` -> `SaveChunkService::registerHandler`
(`sdk/include/openeden/game/save_chunks.hpp`); the host writes all handlers' bytes as one `GPCH` record and reads them
back on the main thread after the world is in place. The Skyrim SE pack's first user is the `loadorder` chunk
(`game_packs/skyrimse/include/game_skyrimse/load_order_stamp.hpp`): the plugin names in order, refused on load when the
session's list differs (so a changed load order is detected and the load refused, which the engine's own
content-file prompt does not catch when only the order changes). **This is also the API the Papyrus wiring (task 0176)
uses for its script state chunk**: derive from `SaveChunkHandler`, register in `start()`, write in `write()`, return
false from `read()` to refuse. Nothing here writes the Papyrus chunk.

## 3. Stages

1. Setting `[Game] neutral saves` (off), the pure blob, the diff and the apply (`neutralsave.*`, tests first).
2. The engine: `World::write` / `readRecord` / `saveLoaded`, `WorldModel` collect and restore, `ExtensionHost` refusal,
   `ESMStore` stub write; the player and one cell round trip in game (interior and exterior).
3. Changed objects and lock state round trip in game; the load order chunk; `GPCH` round trip in game.
4. Refusals: saving stays refused with the setting off and for a session error; what is not saved is listed.

## 4. Acceptance

- [x] The pure tests pass (red against a stub first): round trip, canonical bytes, every truncation refused, mutation
      fuzz, diff and apply.
- [x] Save and load in the Bannered Mare and in Whiterun's exterior: same cell, position, object states (frames viewed),
      each three times.
- [x] A Morrowind save written by the parent build loads in the new build and the other way round; `worldobjects_check`
      and the fixed Balmora frame are unchanged.
- [x] With `[Game] neutral saves` off, saving in a neutral session is refused as before.

## 5. Stage log

Built on 2026-11-15 by one hand (tests and code), Windows only, local commits, not pushed. Build tree
`I:\Projects\OpenEden-work\n6saves\build`.

**Red runs.** Pure tests `testneutralsave.cpp` (24): against a stub that answers nothing, 21 failed and 3 passed (they test
absence); 24 of 24 pass with the code (private MSVC rig and the real `openmw-tests`). `testskyrimseloadorderstamp.cpp`:
against a stub that accepts everything, 4 of 9 failed (the others are vacuous against a stub that writes nothing); 10 of 10
pass (one test added later, written after the code). `testneutralsaveworld.cpp` (3) and `testneutralsavesetting.cpp` (2)
were written after the code (they would not have compiled before it): NOT run red. `openmw-tests` 2,631 pass (without the
Slang, Papyrus and ObScript groups), `components-tests` Skyrim, Extension, Neutral and Settings groups 1,172 pass.
No existing test was edited.

**In game** (five Skyrim masters, `tools/neutral_session_check/saveload.sh`, quiet window): the save run changes five objects
of the player's cell (one disabled, two moved, one deleted, one enabled), logs every object, saves, and a NEW process loads the
save and logs again; the two logs must be equal. Bannered Mare: three full cycles, ROUNDTRIP-EQUAL 697 lines each, no
crash dump, no error line; Whiterun's exterior (`@WhiterunWorld,25670,-7633`): three full cycles, 378 lines each. Frames of
the save run and the load run at the same moment viewed for both (the inn's pillar and ladder; Whiterun's ground at night):
the same picture. Save size 135 to 141 kB; the neutral part is two records, `NWLD` 135 bytes and `GPCH` 131 bytes; the save
takes 7 to 25 ms on the worker. Locks: the Angarvunde01 key scenario (`N6_SCRIPTS=n6lock.omwscripts`,
`OPENEDEN_NEUTRAL_KEYS=skyrimse:8715F`) saves "1 opened locks, 1 keys" and the load restores them. A second generation
(`N6_CYCLE=1`): a loaded save, one more object moved, saved again, and loaded again without leaving the process
(`menu.loadGame`): CYCLE-EQUAL 697 lines, one line differs from the first generation (the moved object), no error line.
A load over another plugin order (the last two masters swapped) is refused: "the save is refused: this save was made with
another load order: position 4 was 'HearthFires.esm', it is 'Dragonborn.esm'" and then "Failed to load saved game".

**Morrowind unchanged.** `tools/worldobjects_check/check.sh` new against `I:\Projects\OpenEden-work\combat4` (the build
the other lanes use as reference; NOT confirmed to be exactly `312e2b81c1`): 111 lines each, OK. The fixed Balmora frame
(`OPENEDEN_FIXED_FRAME_TIME`, seed 7, frame 600) has SHA-256 prefix `1c5d47ad0b5dc32d` for both. Save compatibility with
`mwsaveload.sh` (Balmora, a save through the menu script, then a load, cell, object count, disabled count and gold equal):
written by combat4, read by the new build: equal; written by the new build, read by combat4: equal; no unknown-record
warning. A save is not byte-comparable across runs (time, thumbnail, loaded cells), so the record list of a combat4 save and a
new-build save was compared instead: the same record names in the same order but for three more `CSTA` cells in one save (it
had visited more cells), no `NWLD` or `GPCH` in either.

**Not run.** The Linux gate (GCC `-Werror`, clang-format 14, clang-tidy), the Lua integration tests, the whole
`components-tests` (the Slang, complement and property groups were left out), a fresh review, a real fight or ragdoll
followed by a save and an in-process load (see decision 3 below), a locked door whose lock is picked, saving through the
real F5 key (the menu script's `saveGame` is the same call), a save made in one session loaded in a session with other
mods (only a reordered one was refused).

**Decisions for toddhow.**
1. `[Game] neutral saves` is a new setting (off by default, on in the test profile). It exists because two existing tests
   pin the refusal (`testneutralworldsaving.cpp`, and `testneutralstub.cpp` `the_stub_never_reaches_a_save`) and I may not edit
   tests. Making saves the default needs those two tests changed (and a decision on the stub player below).
2. The stub player now reaches the save (as the dynamic `Player` NPC, with its stub race and class, which every neutral
   session creates the same way). This reverses design doc 10.4 R4's "must never reach a save". Needed so that the player's
   own state (`REC_PLAY`, journal and the rest) is saved by the engine's normal path.
3. Actors and combat are NOT saved and nothing refuses saving during a fight or while a ragdoll is live. Reason: the engine
   rebuilds the neutral actors at every load. RISK (found by review, not tested): the game pack is not restarted on a load
   inside a running process, so pack state (the combat table, the dead list, furniture occupancy, the actor table) may
   survive an in-process load of an older save (Hulda dead in the pack, alive in the world). A fresh process is fine. Either
   the pack clears that state when the world is cleared, or saving is refused while combat is on; I did not touch
   `combat*`.
4. `NWLD` is an engine record separate from the pack's `GPCH` chunks; a neutral actor saves only enabled, disabled and
   deleted (never its position).
5. The `loadorder` pack chunk is restored AFTER the world, so a reordered load can fail first with a "cell missing" message
   instead of the load-order one when the order change drops a cell.
6. `Player::readRecord` logs "Player cell ... no longer exists" on every neutral load (harmless: the cell is found later).

