# Task: Start a neutral session at a spot in an exterior
Design doc section: `docs/design/neutral-world-model.md` (start path, task 0109) and task 0125 (exteriors). Approved under toddhow's blanket "go with your recommendations". Full loop (it touches the engine start path).
Goal: `--start @<world editor id>,<x>,<y>[,<z>]` starts a neutral (Skyrim SE) session standing in that world's exterior, so tests and toddhow can reach a place without a door.
Inherited code touched: `apps/openmw/mwstate/neutralnewgame.hpp` (`NeutralNewGame::run`), `apps/openmw/mwworld/worldmodel.{hpp,cpp}` (new `findNeutralWorld`, beside `findNeutralCell`/`findNeutralExterior`). Morrowind's start (`NewGame`, `--start <cell>`) is not touched: `NeutralNewGame` is only used when a game pack provides a neutral world.
Inputs / outputs: `--start` text. `MWWorld::parseNeutralStart` (pure): text without a leading `@` is an interior name as before; the `@` form takes a world editor id, x, y (units), optional z, refuses non-finite numbers, more than four fields and values beyond 1e7. `MWWorld::neutralLandHeight(source, world, x, y, error)` reads the pack's `buildTerrain` for the square (walking `WorldSpaceInfo::parent` when `parentUsage` lends land, cycle-safe) and interpolates bilinearly (`neutralCellHeightAt`, col east, row north). z omitted: land height + 20 units, then `adjustPosition` settles the player.
Constraints: main thread; off unless a neutral session (`[Game] neutral world`) is running; refuses with a clear message (shown by the state manager's error path) for an unknown world, a world the pack cannot describe, a spot with no land, or a malformed text.
Acceptance criteria:
- [ ] The parse, height and land-lookup tests pass (written first, red against stubs).
- [ ] In game: `--start @Tamriel,<x>,<y>` puts the player in the Tamriel exterior at that spot; an unknown world and a spot in the sea are refused with a message and the game ends with an error box text in the log, not a crash.
- [ ] With the feature unused: `tools/worldobjects_check` dump and the fixed-frame Balmora frame equal the parent build; `--start <Morrowind cell>` behaves as before; `--start <interior editor id>` in a Skyrim session behaves as before.
Out of scope: saving in a neutral session, a launcher entry, rotation (the player faces rot 0).

## Stage log (2026-11-12; same hand wrote tests and code; no fresh reviewer; Windows only; local commits on branch `skystart`, not pushed)

- **Built:** `MWWorld::parseNeutralStart`, `neutralCellHeightAt`, `neutralLandHeight` (`mwworld/neutralstart.*`), `WorldModel::findNeutralWorld`, `NeutralNewGame::startInExterior`. The text after `--start` that does not begin with `@` is an interior name exactly as before.
- **Tests first, red against stubs:** 14 of 21 `NeutralStart*` tests failed against the stub (the other 7 pin absence: refusals), all 21 pass after. Two mutations were each caught by named tests: the parent-usage check replaced by `true` (1 test), row and column swapped in the bilinear read (2 tests).
- **In game** (`tools/neutral_session_check/check.sh` through `I:\Projects\OpenEden-work\skystart\rungame.ps1`, five Skyrim masters plus `_ResourcePack.bsa`, quiet window): `--start @Tamriel,101843,162` logs `Neutral start: the player stands at (101843, 162, -8098.26) in 'Tamriel' (square 24, 0, land height -8118.26)` and loads the 5 x 5 grid; `@NoSuchWorld,1,2` ends with "no world named 'NoSuchWorld' in the loaded game"; `@Tamriel,9000000,9000000` with "there is no land at ... the world has no land in that square"; `@Tamriel,1e9,2` and `@Tamriel,1,2,3,4,5` with the usage text; `--start WhiterunBanneredMare` still starts the interior (check.sh OK). All refusals reach the state manager's error path (no crash).
- **Morrowind unchanged:** `tools/worldobjects_check` dump (111 lines, SHA-256 prefix `0367aa75e033b45c`) and the fixed-frame Balmora screenshot (`1c5d47ad0b5dc32d`) are identical to the ones of the build of the parent commit `e2c771530f` (`I:\Projects\OpenEden-work\skytrees`, kept from task 0136); `--start Balmora` is the path of both.
- **Found, not fixed (decision for toddhow):** on most exterior spots I tried the player is moved 17,000 to 27,000 units sideways in the FIRST physics step after the load (`PhysicsSystem::moveActors` takes `Actor::getSimulationPosition`; the Jolt actor's own `mPosition` has moved, not the ptr; z is unchanged; temporary logging, removed). Not seen at a sea spot `(24000,-18000)` and at `(60000,-60000)`, seen at `(101843,162)`, `(99000,500)`, `(100000,3000)`, `(103000,2000)`, `(121840,-93749)`, each time on steep mountain land. The destination after the move has no loaded ground, so the player then falls. The cause is not found (the first step after a long loading frame, steep slope, or a Skyrim collision shape are the candidates; Lua shows the player at the start position for two frames first). The start itself is right (the log line and the loaded grid); a test that needs the player to stand there should use a flat spot, or a Lua teleport after the load.
- **Not run:** the Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua integration tests, a fresh review, `components-tests`/`openmw-tests` as wholes (only the new tests; the whole `components-tests` target was built), a launcher entry.
- **Helper:** `apps/components_tests/skyrimse/testskyrimsetreeplacementsreal.cpp` (gated by `OPENEDEN_SKYRIMSE_DATA`, prints `SKYTREE-PLACED` lines: counts and grid squares only): 72,964 exterior cells read, 3,660 hold at least one of the 35 BSTreeNode trees, 42,712 placements; the densest Tamriel squares are (-20, 31) with 67, (29, -23) with 62 and (24, 0) with 54.

## Follow-up (2026-11-12, branch `skystart2`, local commits, not pushed)

- **The sideways throw is gone on main.** With the tree at `981896fb9e` (task 0140's no-stand-in-box fix) `check.sh ... poslog` logs the
  player every half second for 13.6 s: `@Tamriel,101843,162` stays at `101843,162,-8113` the whole time; `@Tamriel,100000,3000` at
  `100000,3000,-8027`; `@Tamriel,121840,-93749` settles from z 11491 to its square 34 units away and stays. Shown to be that change: with
  `standInBoxWanted` temporarily returning the old `!meshAdded && hasBounds` the same start at `101843,162` jumps at t = 2.5 s to
  `81755,8655,-8076` (the earlier throw to the unit) and then falls to -14165; restored afterwards. The mechanism is the one of task 0140
  (`MovementSolver::unstuck` pushing the player out of a stand-in box of a model with no collidable triangles).
- **Pack error `snapshot refused: cell 38320: object 59: ids not strictly ascending` (a pack bug, fixed).** The gated test
  `SkyrimCellIdsRealData` (new) loads every cell of the five masters (73,723) through `SkyrimWorldView::loadCell` and checks the ids:
  5 exterior squares of Tamriel, (-3, -17), (12, -13), (18, 0), (-20, 22) and (8, 27), listed one placement TWICE (equal ids, never
  out of order). Each of the 5 is a placement that one plugin keeps in the world's persistent cell and another plugin moves into the
  square's temporary children (same form id; copies identical in base and position; the square's copy has the persistent flag
  cleared): `loadCell` appended the persistent cell's placements that stand in the square without checking for an id the square
  already had. Now such a persistent copy is skipped, the square's own copy kept. Test first:
  `SkyrimWorldView.aPlacementInBothThePersistentCellAndTheSquareIsListedOnce` (generated plugins; failed before, passes after). Real data after:
  0 of 73,723 cells with bad ids; in game `@Tamriel,75000,2000` (square 18, 0) loads with no error line (before: the error).
  Not known: whether the persistent or the temporary copy is the load order's winner (the copies are identical in the 5 cases;
  `Materialised` keeps no plugin index) - a rule on the persistent flag would need that.
- **Morrowind unchanged:** `worldobjects_check` dump (111 lines, `0367aa75e033b45c`) and the fixed Balmora frame (`1c5d47ad0b5dc32d`)
  equal the earlier results of the parent build; the change is in the Skyrim pack only.
- **Not run:** the Linux gate, GCC, clang-format 14, clang-tidy, Lua tests, the whole test binaries, a fresh review.
