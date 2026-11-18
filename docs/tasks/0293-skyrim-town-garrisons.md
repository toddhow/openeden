# Task 0293: Skyrim towns start with fights (enable parents)

Status: stage log (same hand wrote tests and code; Windows only; local branch from `0f51491cc4`, not pushed).

## Problem

Task 0288 (item 11) saw Imperial guards, Stormcloak soldiers and bandits fight on arrival in Kynesgrove, Helgen,
Karthwasten, Darkwater Crossing, Shor's Stone, Ivarstead and Rorikstead, and guessed missing story state.

## Cause (from the data, Skyrim.esm read with houseCARL)

The faction relations are not the cause. In the data each of these towns holds BOTH garrisons as ordinary placements:

- `GuardKynesgroveImperial1` (0799FE, base `GuardDarkwaterCrossingImperial`, CWImperialFaction) and
  `GuardKynesgroveSons1` (0799F9, base `GuardDarkwaterCrossingSons`, CWSonsFaction), and the same pair per town
  (Kynesgrove 02E70B / 02E70A, and so on). The placements carry no InitiallyDisabled flag of their own.
- Each has an **enable parent (XESP)**: a marker (`CWGarrisonEnableMarkerImperialDarkwaterCrossing` 0799F1, and the Sons
  marker 0799F2; Kynesgrove 02E700 / 02E701). The Imperial marker has record flag InitiallyDisabled (0x800); the Sons
  marker does not. So in the game only the Sons garrison is there at the start, until the Civil War scripts change it.
- `SkyrimWorldView::makeObject` read only the placement's own InitiallyDisabled flag, so every guard was enabled and the
  two factions (enemies by FACT XNAM, `LvlGuard...` templates, `IsGuardFaction`) met and fought, which is correct
  behaviour for the data we gave the combat AI.
- The towns' other placements (chickens, goats: `EncChicken`, `EncGoatDomestic`, `LvlAmbientCreatures`) are unaggressive
  and not involved.

`PlacedRefIndex::initiallyDisabled` already folded an enable-parent chain, but only for the dialogue and map code, and
only once the dialogue data is built (a worker), so it is not usable at cell build.

## Fix

`SkyrimWorldView::foldEnableParents` (called from `loadCell`): a placement with an XESP starts in its parent's state
(opposite when the XESP flag bit 0 is set), a chain of up to 8 parents follows its root, parents are looked for in the
cell and in the world's persistent cell (both already materialised, so deterministic and no wait); a parent not held there
(the player, a marker in another square) leaves the placement as its own flags say. `Materialised::mEnableParents`
records the XESP of each placement. The lookup that makes a cell built after a script enabled a parent follow that parent
is no longer limited to `houses` / the main quest (`skyrimse_game_pack.cpp`).

No per-town special case.

## Tests

`apps/components_tests/skyrimse/testskyrimseenableparent.cpp` (5 tests, generated plugins): see the log below.

Red run: with `foldEnableParents` stubbed to return at once, 4 of the 6 tests failed (the other 2 pin a placement whose
parent is not held and a parent loop); the first version's opposite-flag test passed by accident and was strengthened
(own flag 0) before the red run was recorded. Green after: 6 of 6.

## In game (old = `n6saves` build of main, new = this build; five masters, `combat ai`/`combat factions`, 4,000 frames)

| Town | old guards / fighters | new |
|---|---|---|
| Kynesgrove | 3 Imperial + 3 Sons, 7 engage lines | 3 Sons, no fight (frame viewed: Stormcloak guards standing) |
| Darkwater Crossing | 2 Imperial + 2 Sons | 2 Sons |
| Ivarstead | 4 Imperial + 4 Sons patrols | 4 Sons |
| Rorikstead | 2 Imperial + 2 Sons | 2 Imperial |
| Shor's Stone | (old run exited 126) | 3 Sons patrols, no fight |
| Karthwasten | 2 Imperial + 2 Sons | none: both markers are disabled in the data |
| Helgen | 6 bandits + the MQ101 cast, 19 engage lines | bandits gone, MQ101 cast only, 7 engage lines |

Kynesgrove, runs 1 to 3 at 6,000 frames: identical 68 trace lines. A very aggressive bandit wizard at Halted Stream Camp
still engages the player (`COMBAT ai engage lvlbanditwizard ... (on sight)`).

## Not fixed (separate causes)

- Helgen: Hadvar and Ralof are enabled placements, Aggressive, in enemy factions; they fight when MQ101 is not running
  (`[extension.skyrimse] main quest start` runs the real opening).
- Two bears of one leveled stand-in (`LvlAnimalCanyonPredator`) engage each other on sight at Kynesgrove (also in the
  old build). Cause not examined.
- Aggressive (1) bandits never attack the player: the data has no BanditFaction to PlayerFaction relation and the pack's
  rule attacks only Enemy relations at aggression 1 (very aggressive ones do). Out of scope here; for toddhow.
- A parent in another square than its child is not folded (the player and unknown parents leave the own flags).
- A script that enables a marker for a cell already built does not enable the children (`pushEnabled` follows children
  only with the main quest; a children index would be needed).

## Decisions for toddhow

- The fold is on for every Skyrim placement with an enable parent, with no setting (it is a bug fix that Morrowind never
  reaches).
- Test aids: no setting added, so nothing in the test profile.
