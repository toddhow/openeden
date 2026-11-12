# Task 0130: Skyrim LOD terrain tiles from our baker (milestone M-S2, stage 1)

Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.3 (exteriors), 4.6 (LOD, option (b): our own baker on the neutral model), 5.2 (M-S2 "LOD tiles from our baker"). Related: tasks 0055, 0059, 0060, 0066, 0088 (the LOD system), 0120 and 0125 (neutral terrain), decision 0016.
Status: stage 1 (terrain tiles) built; stage 2 (object clusters) not started, what it needs is recorded below.
Approval: toddhow's blanket "go with your recommendations" (2026-11-11/12); full loop (D13).

Goal: `openmw --bake-lod-terrain` and the far-terrain draw (`MWRender::LodTerrain`) work for a game pack's neutral world (Skyrim SE: WhiterunWorld, Tamriel) through the neutral terrain storage, with the pack keyed by game and world, and Morrowind's pack, bake and draw byte-for-byte as before.

## 1. Audit: where the LOD system assumes Morrowind

Method: read every file of the LOD stack (`components/lod`, `apps/openmw/lodterrainbaker.*`, `lodobjectbaker.*`, `mwrender/lodterrain.*`, `mwrender/lodclusters.*`, the LOD parts of `mwrender/rendermanager.cpp`) and searched for `ESM::`, `ESMStore`, `REAL_SIZE`, `8192`, `65`, `64`, `"Morrowind"`.
"Fixed" means changed in this stage; "stage 2" means object clusters; "kept" means Morrowind-only on purpose or harmless.

| # | Place (file, function) | Assumption | Status |
| --- | --- | --- | --- |
| 1 | `components/lod/tilebake.hpp` `landVerticesPerCell = 65`; `tilebake.cpp` `vertexStepLod`, `bakeTerrainTile` (`fineN`), `hashNormalsWithoutCorners` (`cornerSpacing`) | a cell has 64 intervals | Fixed: `TileBakeSettings::mVerticesPerCell` (default 65); all three read it. Digest and provenance parameters grow only for a value other than 65, so Morrowind packs are byte-identical |
| 2 | `components/lod/tilebake.hpp` `TileBakeSettings::mCellSize = 8192` | cell size default | Kept (a default; both bakers pass the storage's size) |
| 3 | `apps/openmw/lodterrainbaker.cpp` `LodTerrainBaker::update` | land cells from `store->get<ESM::Land>()`, storage is `MWRender::TerrainStorage` (ESMStore) | Fixed: a neutral branch (`beginNeutral`) uses `NeutralTerrain::Storage` over a cache of its own that holds the whole land, and finds the land cells by scanning |
| 4 | `lodterrainbaker.cpp` `worldspaceName = "Morrowind"`, output path `openeden/lod/terrain.edenpak` | one world, one fixed path | Fixed: the neutral manifest name is `<game>:<world>:<fingerprint>`, the path `openeden/lod/<game>/<world>/terrain.edenpak` (`Lod::terrainPackPath`); an empty game id gives the legacy path |
| 5 | `mwrender/lodterrain.cpp` `terrainBakeSettings`, `LodTerrain::State::start` | settings built from the cell size alone (65 vertices) | Fixed: also takes `Terrain::Storage::cellVertices` |
| 6 | `terrainBakePlan()`: albedo 256 px for tiles of level 2 and up | tuned for 8192-unit cells (128 units a texel) | Fixed: `Lod::albedoSizeForCell` (256 at 8192, 128 at 4096: the same ground per texel, and the same pack size per tile); Morrowind's plan is unchanged |
| 7 | `lodterrain.cpp` `loadPackUnchecked`: `bakeTerrainLevels(..., "Morrowind", ...)` and the staleness check that samples the whole land again on a worker | the land can be read from any thread; the pack's worldspace is "Morrowind" | Fixed for neutral worlds: the check cannot run (a neutral cell is built by the main thread, a window at a time), so the pack is compared against the world's facts (`Lod::neutralFingerprint`: game, world, cell size, vertices, default height, `cellCount()`) carried in its manifest name, plus cell size, grid size, albedo size, versions (`Lod::describeNeutralPackMismatch`). Weaker than Morrowind's digest check: a plugin edit that changes heights but not the cell count is NOT detected (see decisions) |
| 8 | `mwrender/rendermanager.cpp` `RenderManager::setStore`: LOD terrain created from `store.get<ESM::Land>()` and `mLodTerrainPack` | one ESM world | Kept for Morrowind; neutral worlds start theirs in `useNeutralTerrain` -> `startNeutralLodTerrain` (pack found by game and world; none: one Info line) |
| 9 | `rendermanager.cpp` `setStore`: `mLandDiagonal` from `ESM::Land` and `ESM::Land::REAL_SIZE` (for `[WorldLod] view distance = auto` with `map view`) | the land extent is the ESM's | Not fixed: in a neutral session it is 0 and `auto` falls back as it does without land. Use a numeric `view distance`. Recorded for the map-view stage |
| 10 | `rendermanager.cpp` `setStore`: `setup.mCellSize = ESM::Land::REAL_SIZE` for `LodClusters` | cluster ground test uses 8192 | Stage 2 (object clusters) |
| 11 | `rendermanager.cpp` lines using `ESM::Land::REAL_SIZE` for the puddle map and the player's cell | 8192 | Out of scope (not LOD) |
| 12 | `components/lod/residency.cpp` `replaceableMargin` (quarter cell 2048), `residency.hpp` `groundRetestDistance` (512 = 8192/16) | margins in Morrowind units | Kept: tuning in world units, not tied to the cell grid for correctness; revisit with stage 2 |
| 13 | `components/lod/clusterbuild.hpp` `mCellSize = 8192`, `lodobjectbaker.cpp` (`cellSize = REAL_SIZE`, `store->get<ESM::Cell>()`, `ESM::REC_STAT/REC_ACTI`, `collectDistantReferences`, `distantObjectModel`, "Morrowind" name, `objects.edenpak`) | the whole object bake reads the ESMStore | Stage 2 (see section 4) |
| 14 | `mwrender/lodclusters.cpp` `LodClusters::overrideReference(ESM::RefNum)`, `DistantObjects::setClusteredCells` | cluster/live hand-over keyed by ESM reference numbers | Stage 2 |
| 15 | `mwrender/lodterrain.cpp` `BakedStorage::getVertexData` | reads the live storage first, whatever the tile covers | Kept: with the neutral storage a tile outside the loaded window reads "no land" (counted as `unloadedReads`) and is then overwritten by the tile's heights; level 1 (no baked ground picture) takes its blend map and vertex colours from the live storage, so level-1 tiles show the default ground texture wherever the window did not reach. They are chosen inside the paging range, which the window covers (see risks) |
| 16 | `apps/openmw/vsgengine.cpp` `setLodTerrainPack(.../terrain.edenpak)` | one path | Kept for Morrowind; `setLodRoot` added for the keyed paths |
| 17 | `components/lod/tilekey.hpp` `TileId::mWorldspace` (uint8), `TileBakeSettings::mWorldspace = 0` | up to 256 worldspaces per pack | Kept: a neutral pack holds one world (index 0); worlds are separate files |

Not an assumption: `components/lod/tileselection.*`, `terraintile.*`, `tilepack.*`, `terrainfade.*`, `viewlimits.*` (take the cell size from the manifest or the camera), `Terrain::Builder` and `Paging` (take `cellWorldSize` and `cellVertices` from the storage since task 0125).

## 2. What was built (stage 1)

(Filled in as the work lands; see the stage log.)

## 3. Stage 2: what object clusters need (recorded, not built)

1. **A neutral object source for the baker.** `LodObjectBaker` walks `ESM::Cell` exteriors and calls `collectDistantReferences` / `distantObjectModel` (the same functions `DistantObjects` uses) over the ESMStore. A neutral bake walks `WorldCellSource::buildCell` for every exterior cell of the world (main thread, one cell a slice; the neutral store keeps only 64 cells resident, so the baker reads and drops) and takes `WorldObject` placements whose kind is a static-like (`ObjectKind`) with a model path.
2. **A Skyrim model flattener.** `Lod::flattenNifModel` (`nifflatten.cpp`) reads Morrowind NIFs through `Nif::Reader`. Skyrim models are `BSTriShape` scenes: `NifScene::Flat::flatten` (`components/nifscene/flatten.*`, task 0110) already produces a baked-transform, merged-by-material `FlatModel`; a thin adapter to `Lod::SourceObject`'s input arrays is the work, plus the foliage and alpha rules (`Lod::textureLooksTranslucent`, `blendCutoutThreshold`) over Skyrim's alpha flags.
3. **Identity.** Clusters list the references they stand in for by `ESM::RefNum` (`objectcluster.hpp`, the "ESM::RefNum" note) so the live chunk can skip exactly those. Neutral objects have a 64-bit placement id; the layout would carry it in the same two words (a pack-level flag or a layout minor version, to be approved by toddhow: the cluster layout is frozen at version 1).
4. **Live/cluster hand-over.** `DistantObjects` (object paging) exists only for ESM cells. Neutral exteriors insert objects through `Scene::insertCell`; the cluster client needs a neutral `staticsReady`/masked-cell provider (`DistantObjects::setClusteredCells` equivalent) or a neutral object-paging layer first.
5. **Cell size** in `LodClusters::Setup::mCellSize` (`rendermanager.cpp`, currently `ESM::Land::REAL_SIZE`) and the residency margins (audit rows 10 and 12).
6. **Size of the job:** the density of Skyrim exteriors (hundreds of statics a cell, instanced trees, grass) makes this the larger half of M-S2's LOD item; the estimate in plan 4.6 (15 to 30 days for option (b)) stands, of which terrain tiles were the smaller part.
