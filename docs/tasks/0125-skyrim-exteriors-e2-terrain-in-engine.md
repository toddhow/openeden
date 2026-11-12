# Task: Skyrim exteriors, stage E2, the ground drawn in the game
Design doc section: `docs/research/0053-skyrim-exteriors-neutral-model.md` sections 4(a), 6 and 8 (E2, and the door half of E4); every section 10 recommendation taken as decided (toddhow's standing approval).
Goal: a Skyrim SE exterior worldspace (WhiterunWorld first, Tamriel's land as the parent fallback E1 already does) is entered through a door from a neutral interior; its ground is drawn, the player stands on it, and the neighbouring cells stream as the player moves.
Inherited code touched: `apps/openmw/mwworld/scene.cpp` (`Scene::loadCell` exterior block, `changeCellGrid`, `changePlayerCell`), `apps/openmw/mwworld/cell.{hpp,cpp}` (a second `Cell` constructor), `apps/openmw/mwworld/worldmodel.{hpp,cpp}` (`getExterior`, `findNeutralCell`), `apps/openmw/mwworld/neutraldoor.*`, `apps/openmw/mwclass/neutral.cpp` (`Neutral::activate`), `apps/openmw/mwrender/rendermanager.{hpp,cpp}` (`updateTerrain`, `getTerrainHeightAt`). Morrowind paths are only entered when a neutral worldspace is registered; `ESMTerrain::Storage`, `Terrain::Builder` and `Terrain::Paging` are untouched.
Inputs / outputs: new `components/neutralterrain/window.{hpp,cpp}` (`terrainEnsureWindow`, `terrainHeightRange`); `MWWorld::neutralWorldspaceId`, `neutralEmptyExteriorCellId`, `makeNeutralExteriorCellRecord`; `MWWorld::NeutralWorldspace`, `WorldModel::neutralWorldspace`, `findNeutralExterior`; `NeutralCellLookup` gains `mWorldSupported`, `mWorldspace`, `mHasGrid`, `mGridX`, `mGridY`; `NeutralDestination::ExteriorWorld`; `RenderManager::useNeutralTerrain`, `ensureNeutralTerrain`, `neutralTerrainCell`.
Constraints: off by default (nothing runs without `[Game] neutral world` and a pack whose source answers `worldInfo`); main thread fills the `CellCache`, workers read snapshots; no per-frame allocation added.
Acceptance criteria:
- [x] Pure helpers and the world model's neutral exterior lookups have tests that failed against stubs first
- [x] A door from the Bannered Mare to WhiterunWorld teleports instead of refusing; the ground is drawn under and around the player
- [x] The player stands on the ground (physics heightfield from the same samples), and neighbouring cells stream (shown by 4096-unit hops, not by walking a full cell)
- [x] With the setting off and on, Morrowind is unchanged (`worldobjects_check` dump and the fixed Balmora frame match the parent build)
Out of scope: water, weather and sky, the LOD baker, Tamriel beyond the parent fallback, the weight-splat shader (base ground texture per quadrant only), saving.

## Stage log
Same hand wrote tests and code; no fresh reviewer. Windows only; branch `e2terrain`, build `I:\Projects\OpenEden-work\e2terrain\build`.

### Design (smallest complete form)
- **World switch:** `Scene::changeCellGrid` calls `selectNeutralTerrain(worldspace)`; for a registered neutral worldspace it releases the terrain preloader's views, then `RenderManager::useNeutralTerrain` builds a `NeutralTerrain::CellCache` and `Storage` for the world, drops the LOD/puddle readers, clears the terrain view and chunk cache and calls `Paging::setStorage` (layers rebuilt on the worker). This runs inside the loading screen of the door's `ChangeCell`.
- **Cache feeding:** `RenderManager::updateTerrain` first calls `ensureNeutralTerrain(eye)`, which loads the `terrainEnsureWindow` (camera cell +- view cells + batch + 2, +1 east/north) into the cache on the main thread; Paging and the preloader then read immutable snapshots on workers. The physics heightfield and navigator heightfield of a neutral exterior cell come from the same snapshot (`Scene::loadCell`).
- **Cells:** a neutral worldspace is `<game>:w<world>` (`isEsm4Ext`, so 4096 units and grid radius 2). `WorldModel::getExterior` builds the pack's cell for the square (or an empty cell with a synthetic id) as an exterior `ESM::Cell` record wrapped by the new `Cell(record, worldspace)`; no water for it (`Scene::loadCell` no longer forces water on a neutral exterior).
- **Doors:** `findNeutralCell` returns `Exterior` + `mWorldSupported` when the source's `worldInfo` knows the cell's world; `Neutral::activate` then enters the square the arrival stands in (`NeutralDestination::ExteriorWorld` -> teleport).

### Red evidence
Stubs for `window.cpp`, `neutralcell.cpp` (three functions), `cell.cpp` (second constructor delegating), `worldmodel.cpp` (two functions returning null) and `neutraldoor.cpp` (the new enumerator refused like Exterior), built into the real test binaries: `components-tests` NeutralTerrainWindow/HeightRange 9 tests, **8 failed, 1 passed**; `openmw-tests` NeutralExterior* 8 tests **5 failed, 3 passed**, ExteriorFixture 8 tests **6 failed, 2 passed** (the passes test absence: an unsupported world, a Morrowind location). With the code all pass, except one test of mine whose expected cell count was wrong arithmetic (12 x 12, not 13 x 13; test fixed, no code change). `openmw-tests` 2,019 pass; `components-tests` `Neutral*:WorldModel*:Skyrim*:Land*` 414 pass. No mutation runs.

### In game (`tools/neutral_session_check/check.sh ... WhiterunBanneredMare 30000 e2terrain`, five Skyrim masters, `game=skyrimse`, `[Game] neutral world`, quiet window)
Frames (viewed): `I:\Projects\OpenEden-work\e2terrain\frames\screenshot000.png` to `screenshot006.png` (run5; 000 and 001 outside the Mare door, 005 after the walk, 002 and 003 in the hop cells).
- The front door teleports (`E2 outside ... cell=WhiterunPlainsDistrict04 exterior=true grid=6,-2`) and `Neutral terrain: skyrimse world 107119 (WhiterunWorld), cells of 4096 units with 33 vertices, 91 ground textures`; 144 cells ensured, 0 chunk reads of cells that were not loaded; 25 neutral exterior cells inserted (5 x 5 grid, 1 to 525 objects each).
- Ground under and around the player is drawn with the base ground texture (brown earth by the well); the hop cells east of the city look very pale (white-blue) in the frames; whether that is snow textures, fog or an unfilled layer was **not established**.
- Collision: a heightmap-only ray under the player (physics) against `core.land.getHeightAt` (the drawn storage) differs by -2.9 to +5.2 units at 10 probes (Jolt quantises heights to 8 bits), so the orientation is right; the player rests 3 to 37 units above the heightfield on the plains probes, the same numbers on every revisit (probably slopes; not separated from other causes), and on objects (the `objects=` ray) in the city.
- Streaming: hops of 4096 units east then back (grids 6 to 10 and back to 6): each probe finds the heightfield and land under the player, no crash, no unexpected error line. Walking was only 300 units (the stub player is slow), so streaming by walking across a border was not shown, the hops were.
- A cold-cache first entry took more than 5 s on the main thread (22 cells of objects in the loading screen) and was ended by the freeze monitor (exit 173); with `OPENMW_DISABLE_CRASH_CATCHER=1` and warm caches it takes 3.7 s from the door to the terrain line. `[Cells] incremental cell build` was not tried.

### Morrowind unchanged
`tools/worldobjects_check` (Balmora, seed 7): the 110-line dump of this build equals the parent build (`integ2\build`, near main), with the setting off and with `[Game] neutral world = true`. Fixed-frame Balmora (`OPENEDEN_FIXED_FRAME_TIME`, frame 600): SHA-256 `1ae72bec0459...` for this build and the parent, setting off and on (4 runs identical).

### Not run / not verified
- Linux gate and GCC `-Werror` (Docker down), clang-format 14 (not installed; new lines hand formatted, `scene.cpp`'s include block is not sorted), clang-tidy, the Lua tests, sanitizers, a fresh review, the `eden-studio` and launcher builds (not configured here).
- Water, weather and sky (the engine's default sky shows), LOD, minimap and world map (none opened), groundcover, the weight-splat shader, saving, a Tamriel start, the way back through a door to the interior (only the exterior leg was run), the 0.2 percent of cell edges whose heights differ (the physics heightfield uses each cell's own array, the drawn terrain the floor rule).
- Terrain chunks built by the preload thread for a predicted position outside the ensured window would read "no land" and stay cached; the window margin (batch + 2 cells) is meant to prevent it and the unloaded-read count was 0 in these runs.


## Follow-up `e2fix` (2026-11-11): three findings of the first E2 report

Same hand; Windows only; branch `e2fix`. Frames: `I:\Projects\OpenEden-work\e2terrain\frames2\` (run21: 000 and 001 outside the Mare, 002 down at the ground of a hop cell, 003 and 004 the city edge and the ground below it, 005 the hop cells, 016 back inside the Mare); earlier looks in `run14` and `run12`.

### 1. The "white ground" was no terrain at all: the terrain vertex shader hard-codes 8192 unit cells
- **Evidence:** the terrain preloader and view built chunks with correct heights for the hop cells (`NTERRAIN chunk ... heights -4584..-4184`, 0 unloaded reads, 94 entries in 16 batches), a forced blend layer changed nothing, and a camera looking straight down at a hop cell showed only the sky colour. In the city the "ground" of the first report was the Skyrim city's own static meshes; the terrain was never seen.
- **Cause:** `files/shaders/terrain/main.vert` (and `terrain.vert.slang`) have `const float cellWorldSize = 8192` and compute `offset = (batch.origin - cameraPos / 8192) * 8192`, with `batch.origin` in cells (`Terrain::Builder::create`, `makeBatchUniform(bounds.min, ...)`). For Skyrim's 4096 unit cells every chunk was drawn at twice its distance from the camera's cell, far outside the view.
- **Fix:** `Terrain::shaderBatchOrigin(origin, cellWorldSize)` (`components/terrain/batchdata.hpp`) rescales the origin to what it is in 8192 unit cells; `Builder::create` uses it. For 8192 it returns the origin unchanged (the same bits), so no shader, interface record or Morrowind frame changes. `Terrain::shaderCellWorldSize` names the shader's number.
- **Tests first:** `TerrainBatchOrigin` (4 tests): 2 failed against the stub (the stub returned the origin), 2 pin Morrowind's bits and bad input; all pass now.
- **Shown:** frames 002 to 005 of `frames2` show textured terrain continuous with the city wall and rocks at the city edge and in the hop cells (rock and grass layers). Not touched: the Slang twin keeps its own 8192 (same fix applies through the uniform).
- **Also found, not fixed:** right after the door the terrain preloader asks for about 40 chunks at cells like (1928, -546) (about 8 million units away; they read "no land": `unloaded 9..359` in the trace). Cosmetic cost of reads; the source of those positions was not traced.

### 2. The cold-cache first entry
Trace (`[Cells] load trace = true`, `load-trace.jsonl`, timestamps in microseconds) of the one 5.5 s frame at the door (run15):
| Where | Time |
| --- | --- |
| `Neutral::activate` -> `findNeutralCell` -> `source->worldInfo` -> `SkyrimWorldView::terrainIndex()` built the terrain index (LAND and LTEX of five plugins) on the main thread | 2,845 ms |
| `Scene::changeCellGrid`, 25 cells, objects built | 1,210 ms |
| `World::update` `world.navigator.wait` (`mNavigator->wait(requiredTilesPresent, &dummy)`: a no-op listener, nothing pumps the window) | 1,515 ms |
- **Fixes:** (a) `SkyrimWorldView::startLoad`'s worker now builds the terrain index right after publishing the cell index (`mTerrainState`, `buildTerrainIndex`, `terrainBuiltOnWorker()`); `terrainIndex()` waits for it if it is still running, builds it on demand for a view with no worker as before. Door cost 2,845 ms -> 0.7 ms. (b) The main thread keeps answering its window: `SDL_PumpEvents()` after each cell of `Scene::changeCellGrid`, and the navigator wait's listener (`World::update`) pumps on every poll (`AsyncNavMeshUpdater::waitUntilJobsDoneForNotPresentTiles` now calls `setProgress` also when no job finished). The monitor is untouched.
- **Tests first:** 4 `SkyrimSeAsyncIndex` tests (2 failed against the stub; 2 pin the on-demand path and a cancelled build). One of them failed after the code because it cancelled before the worker got to the terrain build (test fault, fixed by waiting for readiness first).
- **Result:** the longest frame is 3.7 s in a warm run (was 5.6 s; its contents are now the 25-cell load and the navmesh wait, both pumping), the freeze monitor stays on (default crash catcher, exit 0). To show the pump works, a throw-away 500 ms sleep per cell (about 16 s of load, not committed) was run with the monitor on: no freeze, no dump. A true cold OS cache was not reproduced; the remaining stalls are single cell loads (at most 0.6 s) and single model reads.

### 3. The way back
`e2terrain` scenario, after the hops: the player is put at the arrival spot, the Mare's exterior door (`skyrimse:b152263`, 73 units away) is activated: `NDOOR 108276 in WhiterunPlainsDistrict04 (6, -2): teleport destination 'WhiterunBanneredMare' ... reused, base records 655, resident neutral cells 36`; the Mare is inserted again with the same 694 objects and 3 disabled, no crash, no error line (frame 016 shows the interior, lit). The first door said `built`.

### Morrowind unchanged after the follow-up
`worldobjects_check` dump equals the parent build (110 lines); fixed-frame Balmora SHA-256 `1ae72bec0459...` for this build as before. `openmw-tests` 2,019 and `components-tests` (`Terrain*:Neutral*:Skyrim*:*Navigator*:*NavMesh*:Lod*`) 1,519 pass.

### Not run
Linux gate, clang-format 14, clang-tidy, Lua tests, sanitizers, a fresh review, a real cold OS file cache, a hands-on walk across a cell border (hops only), water, sky, LOD, maps.
