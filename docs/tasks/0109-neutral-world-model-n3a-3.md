# Task: The neutral world model, stage N3a-3 (a neutral cell in the engine: the Ptr and Class path, boxes, a start path of its own)

Design doc section: `docs/design/neutral-world-model.md` sections 10.1 to 10.4 (R3, R4, R6), 10.6 (this stage) and 10.11 (N3a-2b as built); decision 0016
Goal: with `[Game] neutral world` on and `game=skyrimse`, a session of the Skyrim SE masters ALONE (no `Morrowind.esm`) starts through a start path of its own, the player stub stands in a named Skyrim interior, and the engine holds that cell's objects as `LiveCellRef<NeutralBase>` references of one class (`MWClass::Neutral`) in a real `CellStore`, inserted into the scene (collision as boxes of the objects' bounds; nothing drawn, the model reader is N3a-4).

**Decisions recorded (toddhow's blanket approval of recommendations, 2026-11-10; they answer the open points of the N3a-2b log):**

1. A neutral session gets **its own start path** (no Morrowind-shaped new-game requirements: no `main` script, no chargen, no intro video, no Morrowind start-cell lookups). Built: `MWState::NeutralNewGame`. It still runs the engine's `World::startNewGame(bypass)` (the player, the camera, collision, time): what that needs from the store (the player's skills, the string game settings the windows read) is added as stub records only where missing, and each stub is on the list below.
2. The pack's world-view indexing moves to a **worker** when N3a-3 reads cells. **NOT DONE in this stage, see the stage log**: this stage reads cells through the store's pull interface on the main thread and the indexing is still 2.0 s of the freeze monitor's 5 s on the main thread inside `Game::Game`; the move needs a join before the first `findCellByEditorId`, a different shape from what was built (the view is built in `SkyrimGamePack::start`, before the engine can ask for a cell).
3. The engine's own ESM4 load of the Skyrim masters **stays** for now (retire in N7).
4. The default game-setting table moved from the generated `apps/openmw/mwworld/neutralgmsts.inc` into `components/esm3/defaultgmsts.{hpp,cpp}` (`ESM::DefaultGmsts`), one table that OpenMW-CS and the engine both use. A separate commit, behaviour identical (a test pins the engine's stub values with a hash taken from the old generated copy).

## Inherited code touched (at commit `1a1840c123`)

- `apps/opencs/model/world/defaultgmsts.{hpp,cpp}` (moved to `components/esm3/`, namespace `CSMWorld::DefaultGmsts` to `ESM::DefaultGmsts`), `apps/opencs/model/doc/document.cpp` (`addGmsts`, `addOptionalGmsts`), `apps/opencs/model/tools/gmstcheck.cpp` (`GmstCheckStage::perform`): namespace and include only.
- `components/esm/defs.hpp`: a record tag `REC_NEUT` (never in a file or a save).
- `apps/openmw/mwworld/cellstore.{hpp,cpp}`: `CellRefList<NeutralBase>` last in `CellStoreTuple`, `setNeutralCell`, `loadNeutralRefs`, called from `CellStore::loadRefs()` (the ESM3 part returns at once for a cell with no context list).
- `apps/openmw/mwworld/worldmodel.{hpp,cpp}`: `emplaceNeutralInteriorCellStore`, tried before the engine's own cells in `findInterior`; `mNeutralCells` (a deque of the synthetic `ESM::Cell` records).
- `apps/openmw/mwworld/scene.cpp` `Scene::insertCell`: a count by kind and a log line for a neutral cell.
- `apps/openmw/mwworld/worldimp.cpp` `World::World`: the stub string game settings and skills, only with `[Game] neutral world`.
- `apps/openmw/mwclass/classes.cpp` `registerClasses`: `Neutral::registerSelf()`.
- `apps/openmw/mwphysics/physicssystem.{hpp,cpp}` `addBoxObject`; `components/resource/physicsshape.{hpp,cpp}` `makeBoxShape` (`mwphysics` is a full-loop area: the change is additive and no existing function changed).
- `apps/openmw/mwstate/statemanagerimp.cpp` `newGame`, `bypassNewGame`: the neutral branch only when a neutral world source is registered.
- `apps/openmw/mwbase/extensionmanager.hpp` + `extensionhost.hpp`: `neutralWorldStore()`, `hasNeutralWorldSource()`.
- `components/terrain/builder.cpp` `Builder::setStorage` / `setLayersWithMaps`: `ensureOneLayer` (the shutdown exception).
- `components/worldmodel/store.hpp`: `gameIds()`.

## Design (as built)

- **One class** `MWClass::Neutral` (design 10.3 (a)); **one record** `MWWorld::NeutralBase` (`sRecordId = REC_NEUT`), the base store from N3a-2.
- **The cell**: a neutral `WorldCell` becomes an interior `CellStore` whose `Cell` wraps a synthetic `ESM::Cell` (no context list, the editor id as its name, flat ambient light `kNeutralCellAmbient`). **Deviation from 10.2:** no third alternative in `ESM::CellVariant` (53 lines in 14 files). The synthetic `ESM::Cell` makes every existing cell visitor work unchanged; `CellStore::isNeutral()` tells the neutral cell apart. The variant stays two-way.
- **References**: `ESM::CellRef` with the base id (`neutralBaseId`), a `RefNum` of the placement id with the reserved content file `kNeutralRefNumFile` (negative, so `hasContentFile()` is false; R6), position as floats, the rotation by `neutralRotationAngles` (R3, the exact inverse of `Misc::Convert::makeOsgQuat`), scale.
- **Collision (boxes)**: an object with a collision source other than None and bounds gets a Jolt box of those bounds (`Resource::makeBoxShape`, half extents at least `kMinBoxHalfExtent`) in layer WORLD. Actors, items and leveled lists are inert references with no model. Markers (`marker*` models, the ESM4 classes' rule) get no model and no collision.
- **Rendering**: nothing is drawn in this stage.
- **Start**: `neutralStartPosition` (a named heuristic: the median x and y of the colliding objects, the lowest top over that point); the engine then traces down. The cell is named by `--start <editor id>`.
- **Stubs a neutral session needs from the engine** (only where the content defines none, only with the setting on): the 347 numeric game settings (N3a-2b), the 1,174 string game settings of the editor's table with a readable derived text (`sQuick_Save` gives `Quick Save`), the 27 skills, the `month` global, the `Player` NPC with a stub race and class.

## Acceptance criteria

- [ ] The default game settings table is one table in `components/esm3` used by OpenMW-CS and the engine; a test pins the engine's stub values against the values of the generated copy it replaced.
- [ ] R3: random quaternions, the poles of the middle angle and Skyrim-style angles round trip through `Misc::Convert::makeOsgQuat` within 1e-5.
- [ ] A neutral cell loaded into a `CellStore` lists what the neutral cell lists (count, ids, transforms, kinds, the disabled flag); an object whose base is missing or whose value the engine cannot hold is left out and counted.
- [ ] A ray down onto a floor box hits at the expected height (the shape; and, in the game, the player stands on the floor).
- [ ] A Skyrim-only session (the five official masters, no `Morrowind.esm`, `game=skyrimse`, neutral world on, `--start WhiterunBanneredMare`) starts, the player stands in the cell, the engine inserts the cell's objects (counts by kind logged), runs N frames and exits 0 with no crash dump and no error line (the saving refusal is the one expected error line).
- [ ] Saving is refused in game (a menu-script `menu.saveGame` attempt), with the refusal text in the log.
- [ ] The shutdown exception `convertToArrayTexture(no layers)` is gone: root cause, fix, test.
- [ ] With the setting off and on and `game=morrowind`: `tools/worldobjects_check` dump byte-identical and a fixed-time frame with the same SHA-256 against an executable built from the parent commit.
- [ ] Layer rules stay at 104 exceptions or fewer; `CI/check_*.sh` pass; clang-format 14; GCC 13 `-Werror -fsyntax-only` of changed `.cpp`.

Out of scope: drawing anything (N3a-4), lights (N3a-5), Lua and mechanics safety (N3a-6), doors that lead elsewhere (N4), actors and items as live things (N5), saves (N6), exteriors (R7), the pack's indexing on a worker (decision 2, see above), N3a-4.

## Stage log

### N3a-3 (2026-11-10; same hand wrote tests and code, no fresh reviewer; Windows only; local commits, not pushed)

**Landed in three commits:** (1) the table move (decision 4), (2) the shutdown fix, (3) the start path and the core. The last one is one commit because the start path, the stubs it needs, the cell and the class could only be run together.

**Tests, red first.** The new pure and store tests were run against stubs of every new function (`evidence/red1.txt`, `red2.txt` in `I:\Projects\OpenEden-work
3a3`): 29 of 86 `openmw-tests` Neutral* and 9 of 31 `components-tests` BoxShape*/ArrayTexture tests failed (the rest assert absence or refusals and pass against a stub), then all passed with the code. Not red first: the later additions (`chargenstate`, magic effects, the alive stub player, `getModelsToPreload`), written after the game run showed the need. The rotation first version was wrong (OSG's quaternion product order: the engine's angles are Rx(t0) Ry(t1) Rz(t2) with t = -a, Z applied first); the tests caught it. **No mutations run.** Full runs: `openmw-tests` 1,852 pass, `components-tests` 7,847 pass (without Slang, complement, property, Papyrus and ObScript groups).

**Game run** (`tools/neutral_session_check/check.sh`, quiet window, the five Skyrim SE masters ONLY, no `Morrowind.esm`, `game=skyrimse`, `--start WhiterunBanneredMare`, 400 frames): session content 5 headers, world view 5 of 5 plugins, 73,723 cells, ~1.9 s; the neutral start path ran; "the player stands at (-256.5, -304, 69)"; **"Neutral cell inserted into the scene: 694 objects (Static 319, MovableStatic 4, Furniture 31, Door 6, Activator 2, Container 40, Flora 18, Light 18, Item 237, Actor 6, Other 13), 0 left out"** (697 in the cell; 3 are markers or are skipped by the insert visitor: not investigated); exit 0, 0 dumps, 0 unexpected error lines. A menu script (`menu.saveGame`) asked to save after 200 frames and the engine logged "Saving refused ... no save support yet" (that line is the one E-level line, filtered by the check). The shutdown exception is gone.

**Found by running (each is a stub the engine's Morrowind-shaped new game needs; all only with the setting on and only where missing):** `chargenstate` global; 1,174 string game settings (readable text from the name); 27 skills; 143 magic effects; the stub player needs non-zero health and attributes (a blank record is dead at once and a dead player looks up the dialogue "hit"); the preloader must not read neutral models (`Neutral::getModelsToPreload` is empty).

**Morrowind unchanged:** `tools/worldobjects_check` dump (110 lines) identical to the parent executable (`n3a2b` build of `1a1840c123`) with the setting off and on; a fixed-time frame (`--skip-menu --start Balmora`, seed 7, frame 600) has the same SHA-256 (`1c5d47ad0b5dc32d...`) for parent and new, off and on.

**Decision 2 NOT done:** the pack's indexing is still on the main thread in `Game::Game` (about 1.9 s of the freeze monitor's 5 s). **Not built or not shown:** nothing is drawn; that the player stands ON the floor (the start is a heuristic over boxes; the log gives the spawn, no frame or trace of the settled z was taken, no physics ray test beyond the Jolt shape test); lights, doors, Lua over the cell (`objectAddedToScene` ran for 694 objects with no error); a Skyrim-only new game from the menu (needs `--start`); `convertToArrayTexture` placeholder has no terrain test beyond the pure helper.

**Checks:** `CI/check_tests_registered`, `check_game_conventions`, `check_mwbase_fakes`, `check_no_osg`, `check_cmake_format`, `check_extension_sdk` and `tools/testprofile/coverage.sh` pass. `CI/check_layer_rules.sh` reports 104 exceptions but ALSO a violation that is not mine: `components/extension/session_content.hpp:8` includes `components/contentsource/plugin.hpp` (layer engine to formats; from task 0107, no exception line). **Not run:** the Linux gate (GCC 13 `-Werror -fsyntax-only` and clang-format 14: docker hung and returned nothing, so NEITHER was run), clang-tidy, the Lua integration tests, a fresh review.

