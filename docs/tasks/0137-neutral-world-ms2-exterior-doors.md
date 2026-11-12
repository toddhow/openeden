# Task 0137: Neutral world M-S2, doors that lead to an exterior (leave and enter an interior from the street)

Status: APPROVED under toddhow's blanket "go with your recommendations" (2026-11-12). Full loop (decision D13: doors, cell
changes and the engine's world model are rules the loop covers). Same hand wrote tests and code; no fresh reviewer.

Design doc section: `docs/design/skyrim-se-implementation-plan.md`, milestone M-S2, criterion "leave and enter an interior
from the street". Builds on task 0115 (interior to interior doors; `MWClass::Neutral::activate`,
`apps/openmw/mwworld/neutraldoor.*`), task 0125 (exterior cells of a game pack's world, `WorldModel::findNeutralExterior`)
and task 0128 (locks).

Goal: a neutral teleport door whose destination is an exterior cell of a world the pack can describe works both ways: it
arrives at the XTEL position and rotation, the exterior grid around the arrival is loaded, a lock is still honoured, and
interior to interior doors behave exactly as before.

Inherited code touched (at `988f7b4346`): `MWClass::Neutral::activate` (`apps/openmw/mwclass/neutral.cpp`), the
destination lookup `WorldModel::findNeutralCell` (`apps/openmw/mwworld/worldmodel.cpp`), `decideNeutralDoor`
(`apps/openmw/mwworld/neutraldoor.cpp`).

## What was found before any change

Reading the tree at `988f7b4346`: the 0125 stages already wired the exterior path. `findNeutralCell` answers `Exterior`
with `mWorldSupported`, the worldspace and the grid when `WorldCellSource::worldInfo` describes the cell's world;
`Neutral::activate` then builds (or reuses) the exterior store with `getExterior` from the square the arrival stands in
and the door decision returns `Teleport`. The refusal "a door to an exterior" (`NeutralDoorOutside`) is now only the
answer for an exterior of a world the pack cannot describe. So this task is not "build the feature" but: pin the feature
with tests, harden the one piece of arithmetic that was inline (`floor(arrival / cell size)` cast straight to `int`),
check the data facts on the user's plugins, and run the round trips in game, which the 0125 follow-up ran in one
direction only (exterior to interior).

## Changes

1. `MWWorld::neutralExteriorSquare(x, y, cellSize)` (`neutraldoor.*`): the grid square an arrival stands in, floor in
   double, no result for a position or cell size that is not finite and positive or a square beyond +-1,000,000 (the
   inline version cast a double to `int` with no check, which is undefined behaviour for a hostile XTEL number).
   `Neutral::activate` uses it; a door whose arrival gives no square leads nowhere (`Nowhere`) instead of building a
   cell at a garbage square.
2. The data survey (`testskyrimseexteriordoorsreal.cpp`, gated by `OPENEDEN_SKYRIMSE_DATA`): how the pack marks a
   teleport to an exterior.
3. A game scenario `extdoors` in `tools/neutral_session_check`.

## Acceptance criteria

- [ ] `neutralExteriorSquare` has tests (negative coordinates, boundaries, the worldspace's own cell size, hostile
      numbers), written first and shown red against a stub.
- [ ] The door decision with an exterior destination: arrival position and rotation equal the XTEL ones, a lock is refused
      before the exterior is looked up, an opened lock lets it through, an exterior of an undescribable world still says
      outside.
- [ ] Interior to interior doors are unchanged: `testneutraldoor.cpp` and `testneutralexterior.cpp` are not edited and
      pass.
- [ ] In game: leave the Bannered Mare by its front door onto Whiterun's street and come back in; arrival positions equal
      XTEL; frames viewed; time per transition recorded.
- [ ] With every setting at its default (neutral world off), Morrowind is unchanged: the `worldobjects_check` dump equals a
      parent build's, a fixed-frame Balmora frame hash equals it, and a Morrowind door lands where it did.

Out of scope: door sounds and animations, followers, load screens, saving, keys by lock picking, a travel to Tamriel by
map.

## Stage log

Windows only, one build tree (`I:\Projects\OpenEden-work\skyextdoors build dir`), local commit on `skyextdoors`, not pushed.

**Data facts (VERIFIED on the five official masters, `testskyrimseexteriordoorsreal.cpp`, aggregates):** 12,060 cells read
(every interior plus every exterior cell of WhiterunWorld and Tamriel), 2,452 teleports, 0 with an unknown destination
cell, 1,542 to an interior, 910 to an exterior; every exterior destination is a cell of a world the pack describes
(`worldInfo` true for all 910). 80 exterior-to-exterior teleports cross worlds (city gates: Riften, Whiterun, DLC worlds).
Whiterun's gate is door base 330099 on both sides (WhiterunWorld side: `WhiterunOrigin`, Tamriel side:
`WhiterunExterior04`), the Tamriel side carries an XLOC (key 888588 = form `D8F0C`, level 255).
**Finding:** for exterior destinations that are Tamriel's persistent doors the destination CELL's grid reads 0,0 (901 of
910 disagree with the square the XTEL arrival stands in; only 9 agree), so the cell's grid must not be used to place the
arrival: the engine takes the square from the arrival position (as 0125 already did), which is right (the arrival is where
the player is put). The pack's `WorldCell::gridX/gridY` of such a persistent cell is not a place (not changed here).
No exterior destination lacked a grid flag.

**Tests:** 13 new in `apps/openmw_tests/mwworld/testneutraldoorexterior.cpp` (7 `neutralExteriorSquare`, 6 chained door
decisions); against the stub 6 failed (the 6 that need the real function; the other 7 pin behaviour that existed). One
mutation (floor to trunc) failed the negative-coordinates test, the limit mutation (1,000,001) was not caught until a test
at one square past the limit was added. `openmw-tests` 2,193 pass; `testneutraldoor.cpp` and `testneutralexterior.cpp`
unedited. Layer exceptions 104; `check_tests_registered` ok.

**In game** (`tools/neutral_session_check/check.sh ... doorsstreet` and `doorsgate`, `OPENEDEN_QUIET_WINDOW=1`, five Skyrim
masters only, frames viewed):
- Street: Mare front door -> `WhiterunPlainsDistrict04` (6,-2), arrival 25669.7,-7632.93,-3237.44 and the player reports
  25670,-7633,-3236; the 144-cell terrain window is built in 20 ms and the 25 neighbouring cells are inserted within 2.1 s
  (first cell after 0.9 s); back in through the exterior door: `WhiterunBanneredMare`, arrival -125.115,-1101.73,68, the
  player reports -125,-1102,69, cell reused, the same 694 objects, under 1 s; two full round trips, exit 0, no dump, no
  error line. Frames: night street with the well and stalls (magenta patches are the missing Skyrim textures every
  neutral frame has); the inn lit from inside.
- Gate: from the street, placed before the gate at WhiterunWorld, activate door base 330099 -> Tamriel
  `WhiterunExterior04` (4,-2), arrival 19243.5,-7427.34 (player 19244,-7427), Tamriel terrain built (144 cells); the Tamriel
  side is locked (key 888588): refused ("NLOCK ... -> 3") without the key, with `OPENEDEN_NEUTRAL_KEYS=skyrimse:D8F0C` it opens
  ("-> 2") and arrives in WhiterunWorld at 19516,-7428.37.
- Not understood: after the second arrival the player drifted 1,271 units in y in 12 s and fell 200 (and after the
  `tp` step the first time 1,000 units in x): physics on the slope at the foot of the gate, not a door fault (arrival
  coordinates are exact); not investigated.

**Morrowind unchanged:** `tools/worldobjects_check` (new against `n4doors\parent`, an earlier main build, not one built
from this commit's parent) 111 lines equal, OK; the Balmora fixed-frame hash `1c5d47ad0b5dc32d` is equal for the parent and
this build, `[Game] neutral world` off and on; a Morrowind teleport door (Balmora Guild of Mages) arrives at
-22066.8,-14040.2,501.1 in all four runs.

**Not run:** the Linux gate and clang-format 14 / clang-tidy (no Docker allowed), the Lua integration tests, sanitizers,
`components-tests` as a whole (only the gated survey), a fresh reviewer, Dragonsreach (no door scenario; Dragonsreach's
front door is a door like the Mare's), a real keypress (activation goes through `object:activateBy`), saving.

**Decisions for toddhow:** (1) the pack's persistent-cell grid 0,0 for Tamriel persistent destinations (the engine ignores
it; fix in the pack or leave); (2) the Tamriel side of Whiterun's gate is locked all day in the data (XLOC present = locked),
so the player cannot enter the city from the plains until a time-of-day / key rule exists; (3) the physics drift near the gate.

