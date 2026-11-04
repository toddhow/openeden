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
   appended the cell's terrain after it. The map view has no bins, so its children draw in order: the sea was drawn,
   then the opaque terrain drew over it, and sea cells showed the dark seafloor, or the black clear colour where the
   seafloor lies below the far plane. That is the "background only" seen in the HUD at Seyda Neen.
   Evidence (one binary, a debug switch that changed only the order): with the terrain appended the sea north of
   Arrille's tradehouse is dark; with it first the channel is blue-grey. With the clear colour set to magenta,
   removing the water changed nothing and removing the terrain showed the magenta, so the water was never visible.
2. **Batch index.** `Paging::updateViewForCell` cuts the cell's terrain from the batch that holds it, with
   `std::floor(cell.x / batchSize)`: integer division rounds toward zero before the floor is taken. Batches hold one
   cell up to a reference view distance of about 3 cells (7168 units, the default), so the fault was hidden. At
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
- Frames after the fix (`I:\Projects\OpenEden-work\minimap\runs\`): `l`, `m` (Seyda Neen, HUD and map window, 30000
  and default), `s` and `s2` (map window with `tfow`, default; identical, 0 pixels differ), `u` (30000), `v`
  (`[WorldLod]` 40000), `t` (Pelagiad, frame 900), `n` and `o` (near Balmora, frame 900), `q` (Caldera, Guild of
  Mages), `r` (world map). The maps of `u` and `v` match `s` on the terrain and the water; 6 percent of the map
  window's pixels differ, all in tree canopies, which the view distance changes for a reason not investigated.
- Not verified: the Slang shaders (the bug is the same with both, and the fix touches no shader), Linux, a frame by
  frame comparison of the main view before and after (no code it runs changed), the map's batch size with
  `[WorldLod]` (not logged).
