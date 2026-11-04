# Task: the exterior local map draws its terrain and water

Design doc section: none (a rendering bug found during the Slang port, task 0072; the local map is inherited behaviour)
Goal: in exterior cells the HUD minimap and the map window's local map show the terrain, the water and the objects, as
they do in OpenMW.
Inherited code touched (at `4f64df13f5`): `apps/openmw/mwrender/map.cpp`, `Map::requestExteriorMap`;
`components/terrain/paging.cpp`, `Paging::updateViewForCell`. Both came with the vsgopenmw import (`paging.cpp`'s
batch lines: vsgopenmw `762695a52f`, 2023-12-12).
Inputs / outputs: the order of `ViewData::sceneChildren` in the map's render-to-texture view; the batch a cell's
terrain draw is cut from. New `components/terrain/batchindex.hpp` (`Terrain::batchIndex`).
Constraints: no new setting (this restores what the code meant to do); no change to the interior map, the world map
or the main view; nothing per frame.

Acceptance criteria:
- [x] `Terrain::batchIndex` rounds down for negative cells, and every cell lies inside its batch for batch sizes 1 to
  16 (`TerrainBatchIndex` in `components-tests`, written first and seen failing)
- [x] Seyda Neen's map window (fog of war toggled off) shows the land, the sea channels, the buildings and the trees;
  the HUD minimap shows the land and the sea
- [x] The same map is drawn at the default view distance, at `[Camera] viewing distance = 30000` (batches of two
  cells) and with `[WorldLod] view distance = 40000`
- [x] Pelagiad shows its terrain, road and buildings, the hills near Balmora their rock; explored and unexplored areas
  still differ in the map window
- [x] The interior map (Caldera, Guild of Mages) and the world map (`[Map] global = true`) draw as before
- [ ] With every setting at its default nothing else changes: the fix touches only the map's own view and the terrain
  of a single map cell; the main view's terrain uses `Paging::updateView`, and Eden Studio's cells
  (`CSVRender::Cell`) pass one cell's width, so their batch holds one cell and `batchIndex` returns the cell itself;
  not compared frame by frame with the build before the fix

Out of scope: the map's fixed lighting (it looks dark on the Bitter Coast's dark ground; the constants are OpenMW's),
the map's near and far planes, which come from the objects' bounds and not the terrain's (no clipped mountain top
was seen), and the tree canopies that differ with the view distance (below).

## Cause

Two faults, both inherited from vsgopenmw; neither is in OpenMW, whose `LocalMap` draws the whole scene graph with its
render bins.

1. **Draw order.** The map scene (`RenderManager::createMapScene`) is a group of the objects and a `Render::SimpleWater`
   plane, which is blended and writes no depth (`components/render/simplewater.hpp`). `requestExteriorMap` then
   appended the cell's terrain after it. `SimpleWater` is a plain StateGroup, so it and the terrain draw in traversal
   order: the sea was drawn, then the opaque terrain drew over it, and sea cells showed the dark seafloor, or the black
   clear colour where the seafloor lies below the far plane. That is the "background only" seen in the HUD at Seyda
   Neen. (Corrected 2026-11-04 after the review: the first version of this text said the map view has no bins. It has
   two, `Bin_DepthSorted` and a null compute bin (`map.cpp`, the `Map` constructor); alpha-blended NIF and DAE shapes
   are `vsg::DepthSorted` (`nif.cpp`, `dae.cpp`) and draw last from that bin whatever the children's order. So blended
   objects below the sea surface still draw after the water, untinted: that is older than this task and unchanged.)
   Evidence (one binary, a debug switch that changed only the order): with the terrain appended the sea north of
   Arrille's tradehouse is dark; with it first the channel is blue-grey. With the clear colour set to magenta,
   removing the water changed nothing and removing the terrain showed the magenta, so the water was never visible.
2. **Batch index.** `Paging::updateViewForCell` cuts the cell's terrain from the batch that holds it, with
   `std::floor(cell.x / batchSize)`: integer division rounds toward zero before the floor is taken. A batch holds one
   cell while the reference view distance is at most 2 cells (16,384 units; `getBatchSize`, `paging.cpp`, is
   `min(16, max(1, nextPow2(ceil(d / 2))))` for `d` cells), so at the default 7168 units the fault was hidden. At
   30000 units a batch holds two cells, and Seyda Neen's cell (-2, -9) took its terrain from the batch of cells -8
   to -7 (logged batch bounds y -69,056 to -45,632; the cell is -73,728 to -65,536), so no terrain was drawn there.
   Most of Vvardenfell has negative coordinates. toddhow's `[WorldLod] view distance = 40000` gives batches of four.

The earlier claim in research note 0013 that the local map draws correctly is wrong: its own frame of 2026-11-01
shows the dark minimap.

## Fix

- `Map::requestExteriorMap` inserts the terrain node before the map scene: terrain, objects, then water.
- `Paging::updateViewForCell` uses `Terrain::batchIndex`, which rounds down.

## Stage log

- Fixed in `20b4949567`. Reproduced with the GLSL shaders on a build of 2026-11-04 before the fix (copy of the lodtest config, a new user folder under
  `I:\Projects\OpenEden-work\minimap`, frames 240 to 900, `[Map] global` true and false): Seyda Neen's HUD minimap dark
  but for the roofs; the interior map and the world map fine.
- Tests: `TerrainBatchIndex` 4 tests; against a stub that truncated, 2 failed (`negative_cells_round_down`,
  `every_cell_lies_inside_its_batch`); after the fix the 50 terrain tests pass.
- The batch fault on screen, at 30000 with the terrain already first: run `k` (before) shows the water plane over the
  whole explored area with only the buildings above it, no ground; run `l` (after) shows the shore and the dark ground.
- Frames after the fix (`I:\Projects\OpenEden-work\minimap\runs\`): `l`, `m` (Seyda Neen, HUD and map window, 30000
  and default), `s` and `s2` (map window with `tfow`, default; identical, 0 pixels differ), `u` (30000), `v`
  (`[WorldLod]` 40000), `t` (Pelagiad, frame 900), `n` and `o` (near Balmora, frame 900), `q` (Caldera, Guild of
  Mages), `r` (world map). The maps of `u` and `v` match `s` on the terrain and the water; 6 percent of the map
  window's pixels differ, all in tree canopies, which the view distance changes for a reason not investigated.
- Not verified: the Slang shaders (the bug is the same with both, and the fix touches no shader), Linux, a frame by
  frame comparison of the main view before and after (no code it runs changed), the map's batch size with
  `[WorldLod]` (not logged).

### Review fixes (2026-11-04)

The fresh review (findings 1 to 6) found the stated cause of fault 1 wrong (above, corrected), the batch-size
threshold misstated (corrected) and a gap older than this task: the map camera's height range came from the objects
alone (`ComputeBounds` over the map scene; the water adds nothing, its draw has a `NullArrayState`). OpenMW's local map
took it from the whole scene root, terrain included (`localmap.cpp` at `d35e66dc64^`, `mSceneRoot->getBound()`).

- **Fix:** `MWRender::mapDepthRange` (`apps/openmw/mwrender/mapdepthrange.*`) takes the objects' box, the cell's
  terrain range and the water level, ignores what is not finite, and gives no range when nothing is known (the map
  then draws nothing for the cell, as the interior map does for an empty cell). The terrain range comes from the batch
  data's height ranges, the same values a smaller draw's cull node is made from (`Terrain::Builder::heightRange`,
  factored out of `createCullNode` with no change to it; `Paging::updateViewForCell` writes it to a new optional out
  parameter). `requestExteriorMap` now fills the terrain view before it sets up the camera.
- **Tests:** `MapDepthRange`, 9 tests in `apps/openmw_tests/mwrender/testmapdepthrange.cpp`; against a stub that gave
  no range, 8 failed (the "nothing known" case passed vacuously). After the fix all pass; the 50 terrain tests too.
- **What the log shows** (`OPENMW_DEBUG_LEVEL=DEBUG`, a Debug line per map render): the empty grid of the review is
  real: at (-14, -22), open sea, "objects none, terrain -2048 to -2048, water -1; camera -2048 to -1" (the old code
  converted an empty box's +-inf to float there). In the cells looked at, the grid's objects already spanned the
  terrain: Seyda Neen objects -2887 to 5494 against terrain -872 to 1480; Red Mountain (2, 6) objects 1181 to 16727
  against terrain 5520 to 11528. So a cut mountain top was not seen on screen; the case needs land higher than every
  object of the 3 x 3 grid.
- **Frames viewed** (`I:\Projects\OpenEden-work\minimap2\runs\`): `seyda` (map window equal by eye to the earlier
  run `l`), `redmtn` and `redmtn2` (Red Mountain (2, 6): rock terrain drawn), `sea` (open sea at (-14, -22): water
  over the sea floor), `interior` (Caldera, Guild of Mages: equal to the earlier run `q`). No frame of the open sea
  with the old code (its build is gone).
- **Canopies:** two runs at 30000 (`c30a`, `c30b`) give the same map window (0 pixels differ by more than 4 of
  104,364, max 1), and 30000 against the default view distance (`seyda`) differs in 1 pixel (max 5). The 6 percent
  of the first stage log does not reproduce after this change; its cause is unknown (both runs then had other
  agents' builds and games on the machine).
- **Recorded, not fixed:** the map's `mReferenceViewDistance` is taken when the `Map` is built (`rendermanager.cpp`,
  the constructor), before `applyViewLimits` (world LOD's land diagonal) and any Lua `setViewDistance`, so after
  either the map can use a batch size different from the main view's and miss the batch-data cache it is meant to
  share (`paging.hpp`).
- **Not verified:** a cell where land rises above every object of the grid, Linux and GCC, the Slang shaders (no
  shader changed), the main view (no code it runs changed apart from `Builder::createCullNode`, which now calls the
  factored function with the same loop).
