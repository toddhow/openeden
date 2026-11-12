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

- Tests first: `testneutralnavigation.cpp` against a stub implementation: 5 of 7 failed (the 2 that passed pin absence), log `I:\Projects\OpenEden-work\skynav
ed_nav.txt`; after the real code 7 of 7 pass. No mutation checks run. `openmw-tests` 2,101 pass (2 skipped, real data), `components-tests` filtered to `DetourNavigator*:Settings*` 225 pass; the whole `components-tests` was not run.
- In game (`tools/neutral_session_check/check.sh ... WhiterunBanneredMare 200000 navpath`; `OPENEDEN_QUIET_WINDOW=1`): with the new default agent all 6 Mare queries Success at t = 0.0 s (door to bar 716 units, door to top of stairs 1,890 units, 10 nodes) and 22 of 24 ring queries; frames of the navmesh at the foot and top of the stairs viewed (`run4`/`run6` screenshots).
- Tile build cost, from `OPENMW_DEBUG_LEVEL=DEBUG` job lines: 1,192 jobs, mean 6 ms, median 4 ms, max 51 ms (one worker thread).
- Morrowind unchanged: `tools/worldobjects_check/check.sh` OK against the older reference build `ms0close
efdir` (110 lines equal); the fixed-frame Balmora screenshot has the same SHA-256 prefix `1c5d47ad0b5dc32d` for the new build and the reference. A Morrowind navmesh or Balmora path query was NOT compared in game: the agent rule for non-neutral sessions is the old code path (unit tested with no neutral override).
- **Not run:** the Linux gate and GCC `-Werror`, clang-format 14, clang-tidy, the Lua integration tests, a fresh reviewer, a Skyrim exterior in the acceptance list, door off-mesh links, `navmeshtool` for Skyrim.

### Decisions for toddhow

1. Default `[Game] neutral pathfind half extents = 18 18 66.5`: the largest radius tried that passes every Mare query (22 passes the stairs but not the route to them). Skyrim's real capsule is unmeasured; the physics player still has Morrowind's 29 x 28 box, so the navmesh lets paths through gaps the player body may not fit. Reconcile with N5.
2. The exterior needs its own investigation (next steps above) before M-S3's "path across Whiterun". Recast on real stairs works, so D8 (no NAVM import) stands for interiors; the exterior failure is not evidence for NAVM yet.
3. Flora/item/light filter kept or dropped.

## Stage 2: why Whiterun's navmesh is islands (root cause found and fixed)

Branch `skynav2`, built in `I:\Projects\OpenEden-work\skynav2uild` (RelWithDebInfo; same hand wrote tests and code, no fresh reviewer; Windows only). The investigation was by experiment, with the navmesh read back as data: `[Navigator] enable write nav mesh to file` writes the Detour tiles (`all_tiles_navmesh.bin`), and a small parser (`I:\Projects\OpenEden-work\skynav2
avbin.py`, not committed) reads polygons and `dtLink` connections, finds the connected components and draws them. `enable write recast mesh to file` gave the raw object triangles of a tile (`tri.py`).

### Findings, in the order they were established

1. **The navmesh is not broken into small islands by tiles or build latency; the player's own pocket is cut off.** Street start (25382,-7629,-3352): the nearest polygon belongs to a component of **9 polygons**; the plaza beside it is a component of 1,134 polygons, with a gap of about 200 units of no navmesh between them (frame of the polygons, height band round the start).
2. **Not the heightfield overlap.** The terrain heightfield lies 130 to 170 units *below* the city's street meshes (ray scans from the street start: terrain z about -3516, static ground -3368 to -3392), so the two layers do not touch. Removing the heightfield from the navigator (experiment `noheight`) changes nothing for the pocket (ring results identical to baseline).
3. **Not paving bumps or voxel size.** The raw triangles along a line west of the start (`tri.py`) are flat (normal y = 1.0) at -3369, -3385 to -3393 over the whole gap: continuous floor with steps of 8 to 17 units, under the 34 unit climb.
4. **What fills the gap is a box.** The same line shows an up-facing surface at -3263 and a down-facing one at -3414 over exactly the gap (40 to 175 units west): a 150 unit tall solid. `OPENEDEN_NEUTRAL_COLLISION_TRACE` names it: `StockadeBarricade01Activator` (`stockadebarricade01.nif`) at (-90,-37,-3414) relative to the start, collision `box`. Those are the stand-in boxes of `MWClass::Neutral::insertObjectPhysics`: an object whose model gives no collision triangles (effect and marker models, a barricade) keeps the box of its bounds as its physics body (stage N3a-3). In the 25-cell Whiterun grid 38 fire effects, 35 chimney smokes, 21 smoke clouds, waterfalls, 9 barricades and many markers are such boxes. The navigator received them like real geometry, so each is a pillar the agent cannot cross.
5. **Proof by removal.** Experiment (env hook, not kept): the navigator is not given neutral objects whose shape is a plain box. Ring of up to 16 targets (r 300 and 700; 15 had ground in these runs) from the street start, one run each, same scene:

| | Success | PartialPath | start component | components in the whole mesh |
| --- | --- | --- | --- | --- |
| before (stage 1; two runs: 1 of 15 and 1 of 13 targets found ground) | 1 | 14 (ending 57 to 338 units out) | 9 polygons | 1,265 |
| without stand-in boxes | 7 | 8 | 1,729 polygons | 969 |
| without boxes and without the heightfield | 10 | 5 | 1,642 polygons | 811 |
| without boxes and without trees (kind `Tree`) | 13 | 2 | not read | not read |

   The remaining partial paths are two other causes, found the same way: **trees** (`BaseKind::Tree`): their visual triangles (trunk plus leaf cards, thickets, shrubs) are given to the navigator; leaving them out is worth 6 more Successes (second row from the bottom to the last), see below; and a few targets at r = 700 whose ground ray lands on the terrain layer under the city (the heightfield row).

### The fix (commit 1)

`MWWorld::neutralShapeIsStandInBox` (`neutralnavigation.*`, a Jolt box, maybe shifted by a RotatedTranslatedShape) and one branch in `scene.cpp`'s navigator part of `addObject`: a neutral object whose physics shape is the stand-in box is not given to the navigator. Physics is not changed (the box still stops the player; whether the stand-in should exist for effects and markers at all is a separate question, noted below). Morrowind: the branch is only taken for `NeutralBase` objects (`ptr.getType() == NeutralBase::sRecordId`), a record type that exists only in a game pack session.

### Tests

`NeutralNavigationShapes.theStandInBoxIsRecognisedWhereverItsCentreIs` and `..aModelsOwnShapeIsNotAStandInBox` in `apps/openmw_tests/mwworld/testneutralnavigation.cpp`, written first and run red against a stub that returns false (the box test failed with `Actual: false  Expected: true`, the sphere test passes against the stub as it pins absence); green with the real function. Existing tests are not edited.

### Before and after (game, `tools/neutral_session_check/check.sh ... navpath`)

See the changelog row in CLAUDE.md (not edited: the guide is owned by toddhow's review). The tool gained `scan` (ground heights by ray per layer) and `objects` (what stands near a point, with `enabled`) in `scenarios.lua`.

### Not done / for toddhow

1. **Trees.** Giving the navigator no `Tree` objects (commit 2, flagged: it edits the pinned kinds test `plantsCarriedThingsAndLightsDoNotBlockButSolidThingsDo`) takes the ring to 13 of 15. The cost is that paths may run through a trunk (the physics body still stops the walker). A trunk-only collision needs the model's own collision data (`bhk`), which the pack does not read.
2. Whether effect and marker models should keep a physics box at all: they collide for the player today.
3. The 2 remaining partials and the heightfield row are not run to ground (ground rays that hit the terrain layer under the city are a likely reason).
