# Task 0286: neutral actors and objects move between cells for real

Design doc section: `docs/design/neutral-world-model.md` (N3a, the engine's neutral cells), `docs/design/game-packs.md`.
Replaces three workarounds: the Helgen relocation glue (task 0268, `PapyrusWorldHooks::relocateObject`: a NEW reference near
the player and the old one hidden, created actors not saved), the Companions carry through the player's doors for a scene's
Travel package (task 0277) and, for the same reason, the follower carry stays (task 0196: it follows the PLAYER through a door,
a different thing). toddhow chose it on 2026-11-17.
Branch: this worktree's branch from `19d7dd9d38`. Same hand wrote the tests and the code (see section 6 for what ran red
first); no fresh reviewer; Windows only; local commits, not pushed.

Behind one new pack setting, `[extension.skyrimse] actor cell moves` (off by default, declared in `extension.json`, a comment
in the test profile). With it off nothing in the engine or the pack changes behaviour (the new engine code is only reached
through the new `ObjectMove::hasCell` field, which only the pack sets, only with the setting).

## 1. What was found

- `World::moveObject(ptr, newCell, position, movePhysics, keepActive)` already moves a neutral placement between cells, loaded or
  not (it is what the follower carry uses): `CellStore::moveTo` keeps the SAME `LiveCellRef` (so the placement id, which is the
  reference number, the inventory the pack keeps by it, the package brain's row and the quest aliases all stay) and tracks the
  move in `mMovedHere` / `mMovedToAnotherCell`. A cell stays `State_Loaded` once built (the engine never clears a neutral cell's
  list), so a moved actor survives its destination being unloaded from the scene and loaded again.
- What was missing: (1) the host's op queue refused an actor's move ("actors are placed by the actor system") and, for objects,
  refused another cell; (2) it could only find a placement in a LOADED cell, by the raw per-cell list, so a placement that had
  moved away was found with its home cell; (3) nothing saved a move (`NWLD` keeps an actor's enabled/deleted state only);
  (4) `Scene` unloaded a neutral grid cell that held a moved-in actor when the player's grid moved on.

## 2. What was built

| Layer | Change |
| --- | --- |
| SDK | `ObjectMove` (`world_source_service.hpp`) gets `hasCell`, `cell`, `hasFrom`, `from` at the END of the struct: a move INTO a cell (the pack's id; none or a cell with no grid means the exterior square that holds the position), `from` for a persistent reference whose cell id does not say where it stands. The service's version number is NOT changed (additive, like `placeActor`). `WorldSourceServiceImpl::moveObject` refuses a destination with no position and a non-finite `from`. |
| Engine | `ExtensionHost::applyNeutralObjectChanges`: a Move op with `hasCell` finds the placement wherever it is (`WorldModel::findNeutralObjectForMove`: the loaded neutral cells first by the CURRENT ptr, else the cell the pack names or the square of `from`, built and loaded, never activated), resolves the destination (`resolveNeutralDestination`), calls `World::moveObject(..., true, true)` and notes it (`noteNeutralRelocation`). The order of the queue is kept (MoveTo then Enable). |
| Engine | `WorldModel::findLoadedNeutralObject` now returns the ptr of the cell the placement stands in NOW (`CellStore::forEachNeutralPtr`, which uses `getCurrentPtr`); an enable or disable after a move reaches it. `CellStore::originOf`, `holdsMovedHereNeutralActor` (new): `Scene` keeps a neutral grid cell that holds a moved-in, enabled actor, as it keeps one that holds an actor a script created (task 0274). |
| Save | `NWLD` version 4 (`neutralsave.*`): after the poses (their count may be 0 there) a list of moved placements: game id, placement, origin cell key, destination cell key, position, rotation. Written only when there is one (versions 1 to 3 keep their bytes); canonical (sorted, no duplicates, origin differs from destination), capped (4,096), strict decoder, every refusal of a version 4 save names "version 4". `MWWorld::NeutralRelocations` (pure) is the world's list; `collectNeutralSave` writes the actor's position and heading AS IT STANDS at save time, `restoreNeutralSave` moves each placement into its destination (both cells built and loaded, before the player's cell is entered) and puts it at the saved place. |
| Pack | `PapyrusWorldHooks::moveToCell` (default false); `PapyrusHost::setCellMoves`: `MoveTo` asks it before the relocation glue and before the in-cell move; the host keeps the cell a moved reference stands in. `PapyrusDriver::pushMoveToCell` fills `ObjectMove` (an object that stays in its cell is left to the plain move). |
| Pack | `PackageBrain::setTravelMoveHook`: a scene's Travel package whose target stands in another cell (`targetIsElsewhere`) asks the hook first; accepted: the actor is moved to the target's record place and cell, the package is Done, nothing is added to `travellerRefs` (no carry through the player's doors). The old carry and the snap stay as the fallback. |

## 3. Tests that keep the old glue, and why

The glue paths (`tryRelocate`, `relocateObject`, the traveller carry, `placeActor` snap) are NOT removed: they stay as the
fallback when the setting is off or the world refuses. Tests that pin them and would need an edit if they were removed:
`SkyrimMainQuestRelocation.*` (8, `testskyrimsemainquest.cpp`), `SkyrimTravellers.*` (12) and `SkyrimLiveDistance.*`
(`testskyrimsecompanions2.cpp`), `NeutralActorPlace.*` (4, `testneutralactorplace.cpp`), `NeutralKeepCell.*` (4, which pin
`unloadAfterGridChange`: the new rule is an extra argument value, the function is unchanged). No existing test was edited.

## 4. Red evidence (honest)

Same hand wrote tests and code. Red against stubs (a stub `NeutralRelocations`, the codec ignoring relocations): the selection
`*Relocat*:*MovedIn*` in `openmw-tests` failed 15 tests (13 of the 19 store and codec tests, 2 of the world ones that need the
store); green after the real store and codec (3,195 of 3,195 `openmw-tests`, 2 data-gated skips). The tests of the pack side
(`SkyrimTravelMove` 6, `SkyrimCellMoves` 5) and of the SDK validation (`ObjectMoveCell` 5) were written with the code, so they
are compile-red only (they do not compile against the code before); they have no stub run. `components-tests` Skyrim,
Extension, Frozen, Neutral, ObjectMove and WorldSource filter: 3,220 pass. No one-line mutation checks were run.

## 5. In game (Windows, quiet window, master volume 0, fixed frame time, five Skyrim masters)

Setting `[extension.skyrimse] actor cell moves = true` (off by default, test profile).

* **Helgen** (`tools/skyactormoves_check/check.sh`, `main quest start`, `quest alias packages`): the quest's own `MoveTo`s of
  Hadvar (placement 180130) and Ralof (180126) from the persistent reference place (4, -20) arrive as `A game pack moved the
  placement 180130 from Wilderness (4, -20) into Wilderness (5, -24) at 22716, -95376, 13940`; 29 moves in all (the rest of the
  quest's start-up scripts: the prisoners, carts, Tullius' people from other cities), **0 `relocate ... made FF80...` lines** (the
  old glue: 22), the aliases keep the original ids, the actors stand at the cart (`MQW` samples near (22700, -94700)).
  Three runs: the 66 lines of moves, `MoveTo` and the first watch samples are byte-identical; later watch samples during the ride
  differ with the Papyrus host's start time (as in task 0268).
* **Save while moved, load in a NEW process**: saved at update 1,090 with both at the cart; the load logs `Neutral world
  restored: ... 27 moved into other cells` and the first samples are Ralof (22766, -94783, 13903) and Hadvar (22716, -95376,
  13926), their saved places (the records say (20044, -79478)).
* **Vilkas** (`tools/skycompanions_check`, the whole walk of task 0277 with `actor cell moves = true`): `A game pack moved the
  placement 108181 from WhiterunJorrvaskrBasement into Wilderness (7, -1) at 29664.9, -4057.3, -2733.98`, no `carried to` line,
  three runs give the same 29 moves, the same move of Vilkas and the same forced greeting and engagement; the clicked fight reached stage 150 in one run of three (the clicks and the fight timing vary, as in task 0277) (frame `keep/vilkas_yard.png`: the fight in the yard with Vilkas' bar).

Found on the way, and fixed: **(a)** the pack's cell for a persistent reference reports the grid (0, 0), so the first version
put every move into square (0, 0) (`Wilderness (0, 0)` in the log): the position now decides the exterior square (source `from`,
destination `position`); the unit fake now reports (0, 0) for the persistent cell as the real data does. **(b)**
`files/data/scripts/openeden/skyrimse/forcegreet_global.lua` at HEAD used `ticks` and `WIDE_EVERY`, which no line declared (a merge of 0273b and 0277): the script
failed every update (900 errors a run) and no forced greeting could open; both are declared now (`WIDE_EVERY = 30`, a value
of mine; the old value is not in history).

Side effect to know: the game's start-up scripts `MoveTo` people into far squares (for example Winterhold, Morthal, Darkwater
Crossing), so the world now builds those exterior squares (resident, never activated); the old glue ignored a far move.

## 6. Not run

The Linux gate and GCC `-Werror`, clang-format 14 (formatted by hand), clang-tidy, the Lua integration tests, a fresh review,
mutation checks, the follower carry of task 0196 through the new path (it keeps `carryNeutralFollowersWithPlayer`), a
Papyrus-cell-override save (the host's `mCellMoved` is not saved: after a load a moved reference reads its pack cell until
it moves again), a move of an actor that is dead or seated, a move into an interior of a not-yet-known world.
