# Task: navmesh for neutral Skyrim cells (milestone M-S3, stage 1)

Status: **APPROVED** under toddhow's blanket "go with your recommendations" of 2026-11-11 (decision D8: Recast first, NAVM import only if Recast fails on real stairs; D13: full loop). Branch `skynav`, not pushed.
Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.4 (navmesh), 8 (M-S3).
Goal: the engine's navigator builds a usable navmesh for a game pack's neutral interiors and exteriors, so a path query across the Bannered Mare (door, bar, stairs) and across a Whiterun street succeeds.
Inherited code touched: `apps/openmw/mwworld/scene.cpp` (`addObject` navigator part, `Scene::loadCell` neutral water), `apps/openmw/mwworld/worldimp.cpp` (`World::getPathfindingAgentBounds`), `apps/openmw/mwlua/nearbybindings.cpp` (default agent of `nearby.findPath`, `findRandomPointAroundCircle`, `castNavigationRay`, `findNearestNavMeshPosition`). `components/detournavigator` is NOT changed.
Constraints: main thread; no allocation in per-frame code; the neutral path only when the cell is neutral, so Morrowind is unchanged (new setting `[Game] neutral pathfind half extents`, used only in neutral cells).

## What the navigator receives today (read at commit 2fe3ba1cbd)

- **Objects:** `Scene::insertCell` calls the file-local `addObject(...)` (`scene.cpp`), which, for any `Ptr` with a physics object (`PhysicsSystem::getObject`), calls `Navigator::addObject(ObjectShapes(object->getShapeInstance(), ...))` unless its `mVisualCollisionType` is not `None`. A neutral object gets its physics object from `MWClass::Neutral::insertObjectPhysics` (`addModelObject`: one `JPH::MeshShape` of the model's visual triangles, or `addBoxObject`: the bounds box). `NavigatorImpl::addObjectImpl` -> `RecastMeshBuilder::addObject` walks any Jolt shape through `GetTrianglesStart`, so mesh, scaled and box shapes all reach recast. Doors: `isDoor()` is false for neutral objects, so a neutral door is an ordinary obstacle, with no off-mesh connection (interiors are separate cells; fine for this stage).
- **Land:** the neutral branch of `Scene::loadCell` calls `Navigator::addHeightfield(cell, 4096, HeightfieldShape{ heights })` with the same samples the terrain draws (task 0125). The cell grid range uses `ESM::getCellSize(worldspace)`, which is 4096 for every non-default worldspace (`components/esmbridge/util.hpp`), so tile ranges are right.
- **Water:** nothing: a neutral exterior leaves the global water state alone (task 0129), so the navigator got no water. Fixed here.
- **Agent:** `World::getPathfindingAgentBounds`: interior actors with a physics actor use its half extents, otherwise `[Game] default actor pathfind half extents` (Morrowind: 29.28 x 28.48 x 66.5). The player's navmesh is the only one built; Lua's `nearby.findPath` defaults to the same default extents.

## Measurements (Windows, RelWithDebInfo, `tools/neutral_session_check/check.sh ... navpath`, Bannered Mare then Whiterun)

- Recast builds a navmesh for a neutral interior and exterior without any change: the debug view (frames viewed) shows the Mare's floor, bar platform, stair treads and the Whiterun plaza, steps and paving.
- **Stairs, agent radius sweep** (`WRIntFloorDirtStairs01`, read by rays: 13 steps, rise 20.5 units, tread 18, flat tread 78 wide, 1:1 overall slope, the stringers slope at the edges; door to foot of stairs, foot to top, top to door, door to top, door to bar, bar to door):

| agent half extent x/y | result |
| --- | --- |
| 29 (Morrowind default) | stairs and the route to them: PartialPath (stairs not on the mesh) |
| 26 | stairs: PartialPath |
| 22 | foot to top: Success; door to foot of stairs: PartialPath (a passage between barrels) |
| 18 | all 6 queries Success (door to top: 11 nodes, 1,913 units) |
| 15 | all Success |

  So **Recast works on real Skyrim stairs; D8 holds, no NAVM import is needed for this.** The failure is the agent size: Morrowind's human is too wide for Skyrim interiors. Step height 20.5 is under `max climb` 34; the 45 degree slope of the nosing line is not a problem because recast tests the flat treads. Skyrim's real NPC capsule is not measured here (RECALLED-VERIFY).
- Interior ring of queries round the player (r = 150, 300, 600, 24 targets, agent 10 s retry): 22 of 24 Success at once, 2 PartialPath (targets behind furniture) at agent 29 and 18.
- **Exterior: NOT solved.** The navmesh exists and is drawn over the Whiterun plaza, steps and paving (frames viewed, also from 1,500 units up), but it is broken into small islands. Ring of 16 targets (r = 300, 700) from the Mare's porch: 1 Success, 15 PartialPath ending 21 to 337 units from the start; from a spot 300 units west of the door: 1 of 16 Success (the way back to the porch), the others end 57 to 338 units out. The results do not change after waiting 120 s (so it is not build latency), with agent radius 15, 18 or 29, or with flora, items and lights left out of the navigator (before and after rings are the same within a few units). The navmesh seen from the ground is a crumpled sheet with vertical shards. Not yet found: the likeliest causes are the bumpy cobbled-paving and terrace meshes (visual triangles with 10 to 40 unit relief against a 34 unit max climb, a 46 degree slope limit and 6.8 unit voxels), or those meshes overlapping the terrain heightfield. Next step (not done): write the recast mesh to a file (`[Navigator] enable write recast mesh to file`) and look at the street's raw triangles; try `cell size` 0.1 on one tile; test with paving meshes excluded.
- Tile build cost (debug log, `OPENMW_DEBUG_LEVEL=DEBUG`, one worker thread): 1,192 jobs for the Mare plus a 25-cell Whiterun grid, mean 6 ms, median 4 ms, longest 51 ms, 6.9 s of worker time in all. Interior: the navmesh answers the first query at once (t = 0.0 s after the cell loads; the cell loads in 0.5 s).

## What is built

1. `[Game] neutral pathfind half extents` (default `18 18 66.5`, test profile): the agent in neutral cells, in `World::getPathfindingAgentBounds` and the Lua default agent while the player is in a neutral cell. Pure rule `MWWorld::choosePathfindingAgentBounds` (`neutralnavigation.*`).
2. Neutral exterior water goes to the navigator (`neutralNavigatorWater`: only a square with water and land, finite level, positive size), so swim areas exist as in Morrowind.
3. Flora, item and light objects of a neutral cell are left out of the navigator (`neutralKindBlocksNavigation`); physics is unchanged. This did not change the exterior result (see above) and is kept because those objects are not obstacles in the original game; say if you would rather drop it.
4. `tools/neutral_session_check` scenario `navpath` and `luatest/scripts/navpath/`.

## Tests

`apps/openmw_tests/mwworld/testneutralnavigation.cpp` (agent choice, kinds, water) written first and run red against a stub (see the stage log); `apps/components_tests/detournavigator/navigator.cpp` `find_path_should_lead_a_narrow_agent_up_an_open_riser_stair` and `..._should_not_lead_a_morrowind_sized_agent_up_the_narrow_stair` (a mesh stair of the measured size at the game's own voxel size, tile size and slope; agent 18 passes, 29.3 x 28.5 does not) are characterization tests of unchanged navigator code: they pass, they were not red. The agent is chosen from the session (`ESMStore::hasNeutralStubs`), not the actor's cell, because the player's agent is added before it stands in a cell (found in game: with a cell test the Lua default agent did not match and every query said NavMeshNotFound).

## Out of scope

NAVM import, door off-mesh connections for neutral doors, cover, per-NPC agent sizes (needs Skyrim actors, N5), navmesh saving/baking, ledge links, performance work for the whole exterior grid.

## Stage log

Built in `I:\Projects\OpenEden-work\skynavuild` (RelWithDebInfo, branch `skynav`, same hand wrote tests and code, no fresh reviewer; Windows only).

- Tests first: `testneutralnavigation.cpp` against a stub implementation: 5 of 7 failed (the 2 that passed pin absence), log `I:\Projects\OpenEden-work\skynaved_nav.txt`; after the real code 7 of 7 pass. No mutation checks run. `openmw-tests` 2,101 pass (2 skipped, real data), `components-tests` filtered to `DetourNavigator*:Settings*` 225 pass; the whole `components-tests` was not run.
- In game (`tools/neutral_session_check/check.sh ... WhiterunBanneredMare 200000 navpath`; `OPENEDEN_QUIET_WINDOW=1`): with the new default agent all 6 Mare queries Success at t = 0.0 s (door to bar 716 units, door to top of stairs 1,890 units, 10 nodes) and 22 of 24 ring queries; frames of the navmesh at the foot and top of the stairs viewed (`run4`/`run6` screenshots).
- Tile build cost, from `OPENMW_DEBUG_LEVEL=DEBUG` job lines: 1,192 jobs, mean 6 ms, median 4 ms, max 51 ms (one worker thread).
- Morrowind unchanged: `tools/worldobjects_check/check.sh` OK against the older reference build `ms0closeefdir` (110 lines equal); the fixed-frame Balmora screenshot has the same SHA-256 prefix `1c5d47ad0b5dc32d` for the new build and the reference. A Morrowind navmesh or Balmora path query was NOT compared in game: the agent rule for non-neutral sessions is the old code path (unit tested with no neutral override).
- **Not run:** the Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua integration tests, a fresh reviewer, a Skyrim exterior in the acceptance list, door off-mesh links, `navmeshtool` for Skyrim.

### Decisions for toddhow

1. Default `[Game] neutral pathfind half extents = 18 18 66.5`: the largest radius tried that passes every Mare query (22 passes the stairs but not the route to them). Skyrim's real capsule is unmeasured; the physics player still has Morrowind's 29 x 28 box, so the navmesh lets paths through gaps the player body may not fit. Reconcile with N5.
2. The exterior needs its own investigation (next steps above) before M-S3's "path across Whiterun". Recast on real stairs works, so D8 (no NAVM import) stands for interiors; the exterior failure is not evidence for NAVM yet.
3. Flora/item/light filter kept or dropped.
