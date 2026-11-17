# Task 0280: Skyrim dragons, second slice

Design doc section: `docs/design/game-packs.md` (the Skyrim SE pack); tasks 0253 (dragons, first slice), 0262 (the dragon in the shared creature table), 0255 (the ridden horse's mover limits), 0254 (the deaths chunk).
Branch `dragons2` from `e7ac7b31d7`. Same hand wrote tests and code, tests first and run red against stubs for every new rule (counts below); no fresh reviewer; Windows only; local commits, not pushed. Behind `[extension.skyrimse] dragons` (the new walk is behind `dragons navmesh`, default false; the save and the mover rules have no setting of their own).

Goal: the six items toddhow chose on 2026-11-17: ground walking, the player's blows, frost and other dragon races, several dragons, a dragon in flight in a save, and a ranked list of what is still missing.

## 1. Ground walking: what the real causes are (found by running it, not by guessing)

Scenario: `I:\Projects\OpenEden-work\papyrus\dragons2\dr.sh` (the 0253 scenario with the cow of `@Tamriel,17311,-17845` replaced by `EncDragon01Fire`, flight off, the player 1,500 units east or west and a camera). The trace lines are `COMBAT dragon actor N at x y z`, `NACTWALK`, `NACTGOTO`.

1. **The old direct walk stopped at the first rock.** Before any change: 335 units, then `blocked edge`. A temporary diagnostic in `walkNeutralActor` showed why: the capsule (radius 150, height 414) had climbed onto a rock whose top was 260 units above the ground and rested on its rim, with the ground under its centre 230 units below, more than the humanoid's 24 unit step down (`kStepDown`), and a floor normal limit of 0.64.
2. **"No path" was a race, not a missing path.** The navmesh of an agent size the session did not build for is built tile by tile on the worker threads; the first answer for a radius 150 cylinder was PARTIAL (two waypoints, 42 units: only the start's tile existed), and `NeutralPath::Follower` takes a partial path that stops short of the goal for "no way to the target". After 20 s the same query answers a full path. Worse, the agent needs over 1,500 tile jobs (measured: `Stats::mUpdater::mJobs` 1,544 to 1,887 with the dragon's agent), which is minutes of work: not usable for a creature that lands and must walk now.
3. **A radius 150 body cannot follow a path made for a radius 32 body near obstacles.** With the navigator answering for the 32 agent (the widest the creature rows already use) the dragon followed a five waypoint path 535 units and then rested on a rock at the second waypoint.

What is fixed (each with its tests):

- `NeutralMover::limitsFor(height, radius, ridden)` (an overload; the pinned two argument form is unchanged): a capsule of radius at least `kLargeCreatureRadius` (120: the dragon's is 150, the mammoth's 100, the bear's 68) has the mount's step up and step down (`limitsForHeight`: 77.6 and 207 for the dragon), a floor normal limit of 0 (`kLargeCreatureMinFloorNormalZ`: a body resting on the rim of a ridge has the ground under its centre on a steep face) and walks over clutter (`Limits::mTrampleHeight`, `mTrampleWidth`: a body no higher than 250 above its feet and no wider than 400 is ignored by the sweeps and the ground probes, `tramplesBody`, `JoltSweeper` with a `BodyFilter`; other actors' bodies are never trampled). Used by `PhysicsSystem::walkNeutralActor`.
- `refineNeutralPathStatus` (`mwworld/neutralnavigation.*`): for a large creature a PARTIAL path while the navigator has update jobs pending is NOT READY, so the follower asks again (it already asks up to 40 times, 20 s) instead of giving up.
- `NeutralPath::navigationRadius`: a large creature's navmesh agent is the cylinder of radius `kMaxNavigationRadius` (32), not 150.
- Pack: `dragons navmesh` (bool, default false, declared in `extension.json`): the dragon's goals go to the navigator first and it walks straight at its goal only for `kDirectFallbackSeconds` (6) after the navigator answered `NoPath` (`SkyrimCombat::dragonNoPath`). Off, as in the first slice, it always walks straight (the pinned `CombatDragon.aDragonsWalkingOrdersAreDirect...` stays true).

In game, same scenario, three versions (player 1,500 units west): before, 335 units then stuck; with the larger limits only, 985 units then perched 100 above the ground; with the limits, the floor rule and the clutter rule **the dragon walks 1,500 units over the plain, the ground under it falling 62 units, and reaches the player** (`keep/01_walk_start.png`, `02_walked_west.png`). **East it still stops** at a rock formation 927 x 1,035 x 586 units (`b690407`): bigger than anything it tramples. With `dragons navmesh = true` it follows a path to the same formation and stops there too. **So a dragon on the ground still needs a path made for its own size, or a flight over such ground; neither exists.**

## 2. The player's blows

No code fault. The reach is measured from the capsule's surface, so a bare-handed swing (reach 96) reaches a dragon whose centre is at most 150 + 96 = 246 units away; 0253's runs stood the player 900 away and the AI keeps its distance anyway. `NeutralSweepLarge` (5 tests, written after the fact, passing at once) pins that geometry for the dragon's capsule. In game with the player 230 units from it, fists hit: `NCOMBAT damage player -> actor amount 2 health after 28 of 905` three times in one run. **A kill by the player's blows was not shown**: in every later run the dragon's AI moved it out of reach (or onto the rock) before the swings (three runs with a 5 to 8 health dragon). The test aid `dragons test kill at` still stands in for it.

## 3. Frost dragons and other races

The breath is read from the shout in the dragon's own spell list, so a frost dragon needs no new code: `E3DemoEncDragonFrost` breathes `VoiceDragonFrost02` of `DragonFrostBreathShout02` (4 spells in its list), 0.5 a tick against `VoiceDragonFire01`'s 0.333 (30 a second against 20: the data's magnitudes), through the same clips (the animation archive has no frost clip: only `exhalefirebreath`, `exhalefireball`, ...). `EncDragonSnowNoScript` breathes FIRE (it lists the same five spells as `EncDragon01Fire`: by its data, not a fault). New trace line `COMBAT dragon actor N breathes <spell> of shout <shout>, K spells in its list`. Not done for frost: the slowing effect (not a vital value effect), the frost projectile's drawn model was not looked at in a frame.

## 4. Several dragons

Two dragons (the chickens at `@Tamriel,30820,-11430`, `dragons test replace = EncChicken@30820,-11430`, `dr2x.sh`) learn their own state, breathe, take off, circle, breathe from the air, approach and land on their own cycles, a second apart (`COMBAT dragon actor 1074835/1074836 flight ...` lines; `keep/04_two_dragons.png`). `CombatDragonSave.twoDragonsBreatheAndFlyEachOnItsOwnAndAreSavedInOrder` and `oneDragonDyingDoesNotStopTheOther` pin it. The player dies quickly to two breaths at 900 units (a run ended: `--auto-quit` counts `State_Ended`), so the run uses `dragons test damage scale = 0` and 1,800 units.

## 5. A dragon in flight in a save

The data decides nothing here (no record holds a dragon's flight); the rule is ours. **The chunk `dragons` v1** (`DragonsChunk`, `encodeDragonSaves`/`decodeDragonSaves`; canonical, capped at 64, strict: u32 count, then per dragon in strictly ascending placement order u32 placement, three f64 and an f32 heading, 32 bytes an entry; a null placement, a non-finite number, a descending or equal placement, a wrong length or a count over the cap refuses the whole chunk and changes nothing) keeps the dragons that are in the air. After a load the pack puts each one back in the cruise at its saved place when the actor of that placement is built (`SkyrimCombat::restoreDragons`; an entry whose cell is not loaded waits) through `Flight::resume`: whatever phase it was in, it comes back as a cruise with the whole air time ahead of it; not engaged (the fight is not saved) it goes into its approach at once and **lands near the player**, engaged it circles for the air time. A dragon on the ground is not saved (as every other actor: the engine puts it back at its placement), a dead one is the deaths chunk's, its health is not saved.

In game (`tools/dragon_check/check.sh`, new): a dragon in the cruise at (18534, -17067, -3594) at the save is back at 18547, -17089, -3594 in a NEW process (`COMBAT dragon actor 267530 resumes its flight at ...`), circling at the same height and breathing (`keep/03_loaded_in_the_air.png`). Three load runs: the first 60 `COMBAT dragon`/`creature` lines have the same md5 (`6bed606d5e23`), one run has an extra trailing line (it ended one update later).

## 6. What is still missing (ranked)

1. A ground route for a creature this size (a navmesh of its own size built ahead of time, or a flight over such ground): the dragon cannot cross a 586 unit rock formation, and the player's reach to it depends on the AI staying put.
2. The kill by the player's blows, shown in game (the AI keeps the dragon out of reach).
3. Terrain under the flight: the cruise circle ignores geometry (a mountain under the circle would be flown through), and the landing point's height is the player's feet.
4. Line of sight for the breath: the cone burns through walls.
5. Interiors (no flight in a cell with a ceiling).
6. A ground dragon's place and a dragon's health are not saved; a dragon killed in a fight that is saved mid-way is back at full health.
7. Frost's slowing effect and the frost projectile model; ice storm, fireball and Unrelenting Force shouts of a dragon.

## 7. Tests (red first, run against stubs)

| group | new tests | red against the stub |
| --- | --- | --- |
| `DragonSaveCodec`, `DragonsChunkTest`, `FlightResume`, `CombatDragonSave` (pack, `testskyrimsedragonsave.cpp`) | 24 | 20 of 24 (the 4 that passed test absence and refusals) |
| `CombatDragonWalk` (same file) | 4 | 3 of 4 |
| `NeutralMoverLarge` (`openmw-tests`, first form, a height rule) | 8 | 4 of 8; the design then changed to a radius rule after `NeutralMoverLimits.onlyTheRiddenActorGetsTheLimitsOfAMount` (pinned: an unridden 376 unit capsule keeps the humanoid's limits) failed, and the tests were rewritten (compile red against the old API) |
| `NeutralMoverTrample` | 3 | 1 of 3 |
| `NeutralPathBusy` / `NeutralNavigationRadius` | 3 + 3 | 1 of 3 and 1 of 3 |
| `NeutralSweepLarge` | 5 | none (characterisation: they passed the first time) |

`components-tests` `*Skyrim*:*Creature*:*Combat*:*Shout*:*Extension*:*Neutral*:*Frozen*:*Dragon*:*Flight*` 3,659 pass; the whole `openmw-tests` 3,146 pass (2 gated skips). No existing test edited. Morrowind: `tools/worldobjects_check` 110 objects OK against `integ2`'s build of `main` (not a build of the exact parent), the fixed Balmora frame `1c5d47ad0b5dc32d` with the settings off and with the neutral and dragon settings on.

## 8. Edits outside the new files

`apps/openmw/mwphysics/neutralmover.{hpp,cpp}` (additive: an overload of `limitsFor`, new `Limits` fields with defaults of 0 that change nothing, `tramplesBody`, `JoltSweeper` constructor gets two defaulted parameters), `neutralpathfollower.{hpp,cpp}` (`navigationRadius`), `physicssystem.cpp` (`walkNeutralActor` builds the limits first and passes them to the sweeper), `mwworld/neutralnavigation.{hpp,cpp}` (`refineNeutralPathStatus`), `worldimp.cpp` (the path finder uses both for a capsule of radius 120 or more: no other actor's path changes), `game_packs/skyrimse` (`dragons.{hpp,cpp}`, `combat.hpp`, `combat_dragon.cpp`, `combat_ai_run.cpp` (one call in `aiGoalFinished`), `skyrimse_game_pack.cpp`, `extension.json` (`dragons navmesh`)), `tools/neutral_session_check/luatest/scripts/n5/wolfglobal.lua` (a dragon that walks out of the player's cell keeps its last seen place), `tools/dragon_check/` (new), `tools/testprofile/profile.cfg` and `README.md`, the two CMake test lists. **`NeutralMover` limits and the sweeper changed additively: nothing but a capsule of radius 120 or more is different.**

## 9. Decisions for toddhow

1. **Default of `dragons navmesh`.** Off keeps the first slice's direct walk (its test is pinned); on asks the navigator (a navmesh of radius 32, up to 20 s before the first complete path while the tiles are built) and falls back to the direct walk. I recommend on once a route for the 586 unit formations exists; today neither helps there.
2. **A radius rule, not a height rule.** The pinned `NeutralMoverLimits.onlyTheRiddenActorGetsTheLimitsOfAMount` says an unridden 376 unit capsule keeps the humanoid's limits, so the mammoth (376, radius 100) and the giant (267, radius 32) are untouched. A height rule would help the mammoth too; it needs that test's list edited (`376.f`).
3. **Dragons walk over clutter and other actors stop them.** Trampling bodies up to 250 high and 400 wide (rocks, bushes, barrels, small trees) is ours; a dragon in a town would still stop at walls and houses.
4. **The flight's save rule** (any phase comes back as a cruise, not engaged it lands near the player; ground dragons and health are not saved).
5. The fix of the path finder's partial answer applies only to capsules of radius 120 or more; every other actor with a partial path still gets "no path" at once.
