# Task: Skyrim exteriors, stage E1, terrain source and storage
Design doc section: `docs/research/0053-skyrim-exteriors-neutral-model.md` sections 4(a), 5, 8 (E1, and the storage half of E2) and 10 (toddhow's "go with your recommendations" adopted for every recommendation).
Goal: the Skyrim SE pack hands the core one cell's land through the SDK, and a `Terrain::Storage` subclass serves it (heights, normals, colours, base ground texture per quadrant) from a thread-safe cache, proved on the real Tamriel data.
Inherited code touched: none changed. `components/terrain/storage.hpp` (the base class) and `components/esm3terrain/storage.cpp` (the model for `getVertexData`, `getBlendmap`, `getHeightAt`) were read only. `ESMTerrain::Storage`, `Terrain::Builder` and `Terrain::Paging` are untouched, so Morrowind's terrain is unchanged by construction.
Inputs / outputs: new `sdk/include/openeden/game/world_terrain.hpp` (1.0); `world_source.hpp` 1.1 (three trailing virtuals: `worldInfo`, `buildTerrain`, `listGroundTextures`, defaults "not supported"); new `components/neutralterrain/` (`CellCache`, `Storage`, layer `engine`); new `game_packs/skyrimse` `terrain_source.*` (`SkyrimTerrainIndex`), forwarded by `SkyrimCellSource` through `SkyrimWorldView::terrainIndex()`; `ExteriorLimits::mDiscardLandData`.
Constraints: off by default by construction (nothing in the engine creates a `NeutralTerrain::Storage` yet); the source is main thread only, workers read immutable `shared_ptr<const TerrainCell>` snapshots; hostile cells are validated and refused, not published; no per-vertex allocation beyond the output arrays.
Acceptance criteria:
- [x] SDK terrain types and the three source virtuals, additive (`kWorldSourceVersion` 1.1, `kWorldTerrainVersion` 1.0), pinned by `CI/check_extension_sdk.sh`
- [x] The pack builds `TerrainCell`, `WorldSpaceInfo` and the ground texture list from the E0 decoders in load order, with null, dangling and unusable textures as id 0
- [x] The storage serves heights, normals and colours with a floor rule for cell edges and a blend map of each quadrant's base layer
- [x] A gated real-data test: Tamriel heights, colours and normals equal the independently decoded LAND, neighbouring chunks share edges exactly, WhiterunWorld borrows Tamriel's land
- [x] Morrowind unchanged (see the stage log for what was and was not compared)
Out of scope (coordinator's trim, 2026-11-11): the alpha-layer weight splat and its shader mode, water, physics height field, the world switch, exterior `CellStore`, any engine wiring, `world_model.hpp` (frozen, untouched). Layers (ATXT/VTXT) are decoded by E0 and not carried in E1's SDK types; they arrive as additive members with the weight-splat stage.

## Stage log (2026-11-11)

Same hand wrote tests and code. Scope trimmed by the coordinator during the task (smallest complete change): base ground texture per quadrant only,
no alpha-layer types in the SDK, no `groundTexture`/`listWorlds` SDK virtuals, no water/bounds/flags fields. The E0 decoders still decode layers.

### Red evidence
Stub `cellcache.cpp`, `storage.cpp`, `terrain_source.cpp` (every function answered "nothing") linked with the tests in a private rig
(`I:\Projects\OpenEden-work\e1terrain\rig`, output `red.txt`): **44 tests, 37 failed** (12 `NeutralTerrainCache`, 17 `NeutralTerrainStorage`,
8 `SkyrimTerrainIndex`); the other 6 pass (they pin versions and absence: `WorldModelFrozen`, `WorldSourceFrozen`) and the gated real test skipped. With the
code: 44 of 44 pass, with `OPENEDEN_SKYRIMSE_DATA` set the real-data test runs (7.5 s). Two test faults were found by running (an unescaped
backslash in a path literal; a chunk also reads the cells to its east, north and, when empty, south-west) and fixed in the tests; no code change for them.
No mutation runs.

### Real data (five official masters, aggregates)
Index: 5 plugins, 49 worlds, 52,181 LAND, 91 ground textures, 0 errors. Tamriel cells x 3..7, y -4..-1 (20 with land): 20,480 vertices checked
against `parseExteriorFile`'s LAND (later master wins): 0 height, 0 colour, 0 normal mismatches; all 20,480 normals have z > 0 and the mean angle to
the slope from central differences of the heights is 1.63 degrees (VNML are signed bytes, z up, as ESM3). 1,023 shared edge vertices between
neighbouring chunks: 0 differ (in this block the stored edges of the cells also all agree; the floor rule matters for the 0.27 percent of pairs
elsewhere and is unit tested with a differing edge). A 2-cell chunk at lod 2 is continuous with the cell chunks. WhiterunWorld (parent Tamriel,
land usage bit set): 7 own cells, 13 borrowed from Tamriel, 0 empty in a 5 x 4 block, matching the note's 7 own LAND.

### Decisions
- `WorldSpaceInfo`, not `WorldInfo` (name taken in `world_view.hpp`).
- The parent fallback is the core's (`CellCache`), following `parentUsage & Land` (0x01, UESP's order; 0x01 and 0x02 are never apart in the data).
- Cell edges: a vertex on a cell edge is the next cell's local 0 (north-east), falling back to the south-west neighbour that has land. Callers
  ensure one cell more to the east and north than the chunks they ask for.
- Morrowind-style `fixNormal`/`averageNormal` are not ported: with the floor rule shared vertices are identical by construction.
- The index re-reads a LAND from its file on every `buildTerrain` (11 KiB each; 54,000 of them would be 600 MB decoded).

### Not run / not verified
- `openmw` was not built: **Morrowind unchanged is by construction only** (no engine, `ESMTerrain`, `Builder` or `Paging` file changed; nothing
  includes the new component), not shown by the `worldobjects_check` dump or a Balmora frame.
- No draw: nothing creates a `NeutralTerrain::Storage` in the engine, so the blend map (base layer per quadrant) is unit tested only, not seen.
- Linux gate and GCC `-Werror` (Docker hung), clang-format 14, clang-tidy, the Lua tests, the whole `components-tests` and `openmw-tests`, a CMake
  configure of the edited lists (the rig compiled the files with another build's flags), a fresh review, `SkyrimCellSource` forwarding (compiled
  with `world_view.cpp` and `neutral_world.cpp`, not tested), a mod list.
- `testfrozen.cpp` line edited: `kWorldSourceVersion.minor` 0 to 1 (toddhow to approve).
