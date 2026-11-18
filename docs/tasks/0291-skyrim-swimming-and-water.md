# Task: Skyrim swimming and water (interior water, floating neutral actors)
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.5 (water); `docs/tasks/0129-skyrim-exteriors-water.md` (the drawn water and the swimming follow-up this builds on); `docs/tasks/0288-skyrim-world-sweep.md` item 13 (swimming at harbours and canals).
Goal: in a Skyrim neutral session the player swims, dives and drowns in water as in Morrowind, in exteriors and in interiors that have water, and neutral actors that land in deep water float instead of standing on the bed.
Inherited code touched: `apps/openmw/mwworld/neutralphysicswater.{hpp,cpp}`, `neutralcell.{hpp,cpp}` (`makeNeutralCellRecord`), `worldmodel.{hpp,cpp}` (`emplaceNeutralCellStore`), `worldimp.cpp` (one lambda), `apps/openmw/mwphysics/neutralactor.{hpp,cpp}` and `physicssystem.{hpp,cpp}` (`updateNeutralActors`), `sdk/include/openeden/game/world_water.hpp` (0.1 to 0.2, additive), pack `records.*`, `world_view.*`, `neutral_world.*`.
Constraints: no new setting (everything is reached only with `[Game] neutral world` and a game pack); Morrowind unchanged; main thread; no per-frame allocation.
Acceptance criteria:
- [x] The pack answers an interior cell's water from the data's rule (flag 0x0002 and a usable XCLW), the core builds the cell with water, and the physics plane is on for it (tests that failed against stubs first)
- [x] A neutral actor that lands in water deeper than half its height rests at the swim float height, in an exterior square or an interior (tests that failed against stubs first)
- [x] In game: the player swims at the surface, dives to the bed, comes up and drowns (Riften canal/lake); in `RiftenRatway01` he swims where the bed is below the water; an NPC floats in the Riften canal; three runs give identical traces
- [x] Morrowind unchanged (`worldobjects_check`, the fixed Balmora frame, a Morrowind swim)
Out of scope: NPC swimming locomotion and animations, creatures, WATR colours and noise parameters, NPC drowning, water walking in a neutral session.

## Stage log
Same hand wrote tests and code; no fresh reviewer. Windows only; local commit on the worktree branch from `99a0fcacff` (not pushed), build `I:\Projects\OpenEden-work\combatai\build`.

### What the survey found
- **The player already swam in exteriors.** Task 0129's swimming stage (physics plane follows the player's square; `ActorFrameData` reads the actor's own cell's water level; `World::isUnderwater` is per cell) works at Riften: from the lake trace he falls from z 11500, `swimming=true` at 10493.5 (depth 130.5 under the surface 10624), swims forward at the swim speed, dives to the bed (10416), comes up, and drowns: with god mode off the health is 100 until about 20 s under water, then falls 6 every 2 s (the engine's own `fSuffocationDamage`, `Actors::updateDrowning`). The sweep's "no swim or drowning logic" came from the world sweep not entering water. The breath bar and the underwater tint show.
- **Interiors had no water at all.** `makeNeutralCellRecord` set no `HasWater`, and the player's cell change called `mPhysics->disableWater()` for every neutral interior (`neutralPhysicsWaterLevel` answered nothing for an interior). In `RiftenRatway01` the player fell to a bed 200 units under the water level and walked on it (`swimming=false`).
- **Neutral actors are not `MWPhysics::Actor`s** (`NeutralActor`, task 0135): they fall to the floor under gravity and stand on it, whatever water is above.
- Interior water in the data (Skyrim.esm, `tools` survey by a script, aggregates): 590 interior cells carry an XCLW; 175 have the cell flag 0x0002; of those 71 have a real height (all 0.0) and 104 the -2^31 sentinel; 415 without the flag have 0.0 or the sentinel. One interior (flag clear) has a real height. The rule built: the flag AND a usable height. The flag's name ("has water", `CellFlags::HasWater` in `records.hpp`) is recalled from the file format, not read from a source: RECALLED-VERIFY.

### Built
- SDK `world_water.hpp` 0.2: `CellWaterSource::interiorWater(WorldId cell, CellWater&)`, a trailing virtual with a default that says none.
- Pack: `CellRecord::interiorWaterHeight()`, `CellEntry::mInteriorWater` (the latest source that speaks of water wins), `SkyrimWorldView::findInteriorWater`, `ComponentSource::findInteriorWater`, `SkyrimCellSource::interiorWater`.
- Core: `makeNeutralCellRecord(gameId, cell, interiorWater)` (HasWater and the height), `WorldModel::emplaceNeutralCellStore` takes the answer (`neutralInteriorWater`), `neutralPhysicsWaterLevel` answers neutral interiors too (the existing `Scene::loadCell` interior-water code already draws the plane and enables the physics level), `neutralWaterLevelAt(cell, x, y)`.
- Actors: `NeutralActor::swimRest` (pure: water deeper than `kSwimDepthShare` 0.5 of the height above the floor is swimming; the feet rest at `kSwimFloatShare` 0.8 of the height below the surface, never below the floor), applied when an actor comes to rest (`updateNeutralActors`), with the water from `PhysicsSystem::setNeutralWaterLookup` (set by `World`: an interior's level, or the grid square of the position through `WorldModel::getExterior(location, false)`). `NACTSWIM` trace line (`OPENEDEN_NEUTRAL_ACTOR_TRACE`). Developer aid `OPENEDEN_NEUTRAL_ACTOR_DROP` (see `docs/debug-environment-variables.md`).
- Scenario: `tools/neutral_session_check/check.sh ... swim291` (`luatest/scripts/swim291/`): `SW_X/Y/Z`, `SW_WATER`, `SW_KEEP`, `SW_PROBE`, phase lengths, `SW_SHOTS`.

### Red evidence
Stubs for `makeNeutralCellRecord` (ignored the water), `swimRest`, `neutralWaterLevelAt` and `CellRecord::interiorWaterHeight`, built into the real binaries: `openmw-tests` new tests **8 of 17 failed** (7 of 15 in the first run, plus the interior physics plane test added later, which failed 1 of 2); `components-tests` `SkyrimInteriorWater*` **4 of 8 failed**. The passing ones test dry and absent cases (a record without water, a source without the interface, bad numbers, wading). Not run red one by one: the engine wiring in `updateNeutralActors` and the `World` lookup (game checks only).

### In game (`check.sh ... swim291`, five Skyrim masters, quiet window, fixed frame time, master volume 0)
Frames and logs in `I:\Projects\OpenEden-work\combatai\runs\` (`riften1..3`, `ratway3`, `npc3`); the frames were viewed, nothing is kept in the repository.
- **Riften lake/canal** (`@RiftenWorld,170000,-96256`, water 10624, bed 10416): table in the changelog row; `riften1`, `riften2` and `riften3` give the same md5 for the 103 `S291 f` and `NWATER` lines.
- **Ratway** (`RiftenRatway01`, dropped at (500, -6750, 200), water 0.0): new build swims (`swimming=true` from depth 156, dives to 200.8, rises); the sibling build (`combat5`) `swimming=false` at depth 202 (control).
- **NPC** (`npc3`..`npc5`, `OPENEDEN_NEUTRAL_ACTOR_DROP=0,169000,-97856,11500,400`): `NACTSWIM b78705 floor z 10392 water 10624 floats at 10521.1`; a natural case `b78678` (placed over the canal) `floor z 10457.5 ... floats at 10521.6`, against `rest z 10457.5` in the sibling build (stands on the bed). The frame shows the NPC at the surface with its shoulders out and its legs seen through the water. Three runs: the `NACTSWIM`, `NACTDROP` and `S291 f` lines have the same md5.

### Morrowind unchanged
`tools/worldobjects_check` (Balmora, seed 7): 110 lines, OK, new build against the sibling reference `combat5` (a build of `main` from the same day, not the exact parent). The fixed Balmora frame (`OPENEDEN_FIXED_FRAME_TIME`, `--auto-screenshot 600`) is `1c5d47ad0b5dc32d` for the reference and for this build, two runs. A Morrowind swim (Seyda Neen, a Lua scan drops the player into the sea): `swimming=true` at depth 132.7 in both, and the same dive depth; the whole path is NOT repeatable even between two runs of the reference build (a Morrowind session's timing varies by a few frames), so it was compared by value, not byte for byte. No Morrowind path changed: every new branch needs `CellStore::isNeutral()`.

### Decisions and open points for toddhow
1. `neutralPhysicsWaterLevel` now answers interiors. It was the cause of the physics plane being switched off in a neutral interior. No existing test pinned the old answer (`testneutralwater.cpp` has none for an interior).
2. The interior rule (flag AND usable XCLW) leaves interiors whose water is placed as water-plane objects (most dungeons' pools) as they were: they are statics, drawn but with no swim state. All 71 interior cells with the rule have the height 0.0; whether Skyrim's interiors with water mean "water at z 0" for those is not shown by anything here except that the Ratway frame shows water where the cell dips below 0.
3. NPC float numbers (0.5 and 0.8 of the height) are ours, from Morrowind's `fSwimHeightScale` default 0.5 and a head-out look. The actor falls to the bed first and rises in one frame (a pop, not a swim). A floating actor given a walk request is stopped by the mover (the bed is more than 24 units below).
4. The pack's own player health (combat) was not checked against the engine's drowning damage: the engine health stat drops (seen in `S291` lines); whether the pack's HUD bar follows it is not known.
5. `CellWaterSource` is 0.2 (additive). No test pinned 0.1.

### Not run
The Linux gate and GCC `-Werror` (the traps were checked by hand), clang-format 14, clang-tidy, the Lua integration tests, `eden-studio-tests`, the whole `components-tests` and `openmw-tests` (area filters only: 3,229 and 1,148 tests), sanitizers, a fresh review, NPCs swimming across a square border in game, a neutral actor in an interior with water in game.
