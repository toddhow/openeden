# Task: World LOD and streaming, stage S1 (the tile model)
Design doc section: `docs/design/world-lod-and-streaming.md`, sections 3.2 to 3.4 (tile tree, selection by screen-space error, crack-free joins), 9 (derived data is untrusted input) and the staging row S1; `docs/spec/eden-archive-format-v0.1.md` (the container: sections 7.4 to 7.8 entries, keys and provenance, 10.3 and 11 the reserved terrain tile type and key tag, 18 derived packs); measurements in `docs/research/0025-lod-baseline.md` (task 0054).
Goal: prove the tile model end to end on one baked terrain level of Morrowind: a tile key and payload layout (defined here as a proposal), a baker that writes the tiles into a derived Eden Archive, a loader that reads them with the container's keyed lookup and range reads, selection of tiles by pixel error with a threshold chosen from the S0 numbers, and a tile viewer in Eden Studio's mesh viewer so toddhow can see every tile, its level, its error and what the selection does.
Inherited code touched (to be re-read at the commit that starts each stage; nothing is changed by writing this spec): `components/terrain/` (`lod.hpp` `isSufficientDetail`, `getVertexLod`; `paging.cpp` `getLodFlags`; `builder.cpp` `Builder::createBatch`; `bounds.hpp`), `components/esm3/landrecorddata.hpp` (`sLandSize` 65, `sLandTextureSize` 16), `components/misc/constants.hpp` (`CellSizeInUnits` 8192, `UnitsPerMeter`), `apps/openmw/mwrender/` (where the terrain is created; `RenderManager`), `edenarchive/include/edenarchive/archive.hpp` (`Archive::open`, `findKey`, `readRange`, `entry`, `provenance`, `PackInfo::mGeneration`, `mSourceDigest`, `packFlagDerived`; the writer in `writer.hpp`), `apps/opencs/` (the mesh viewer of task 0043, `SceneHost`).
Inputs / outputs: a new `[WorldLod]` settings section (`enabled` false; the system is off and no code of it runs when false); a derived pack `terrain.edenpak` in the user data folder; a baker mode `openmw --bake-lod-terrain` (S8 will turn it into a background process); tile viewer panel in Eden Studio.

Status: DRAFT written 2026-11-02, not approved, nothing built. Full loop when started (renderer, loader, untrusted input, a new derived-data format).

## What S0 told us (numbers from `docs/research/0025-lod-baseline.md`, RTX 4070, i7-13700K, `build-jolt` Release at `c8723f39c1`)

- The long view is CPU-bound, not GPU-bound. At 30000 units with distant terrain and object paging on, a frame takes a median of 3.87 ms (p95 4.86) while the main scene pass takes 0.35 ms on the GPU. The scene graph holds up to 33,823 draw commands and 2.4 million triangles (an upper bound, with no culling); with a 7168 view it is 6,978 draws and 0.41 million triangles. So tiles must win on **draw commands and CPU work**, not triangle count: one draw for a terrain tile and one for an object cluster instead of hundreds.
- A 30000-unit view is a circle of radius 3.66 cells, about 42 cells of area. With one-cell tiles that is at most 8 x 8 = 64 tiles; with two-cell tiles about 16. The design doc's "a few hundred tiles at most" was too high for Morrowind; a CPU selection pass over all tiles is trivially cheap.
- Distant objects are what a long view loses first. Without object paging, at the Balmora test pose, every rock, tree and tower beyond about the loaded cells is missing from the horizon (the difference image of `heavy_pop0_on` against `_off` is white along the whole silhouette band) while the terrain itself reaches the horizon either way. So terrain tiles (this spec) fix cost and quality of the ground, and object clusters (S4) are what the eye misses.
- Memory is small: 723 to 793 MiB resident and 219 to 252 MiB of device memory in these scenes. Budgets are not the first problem on this machine; the weaker-GPU emulation (note 0025) says what a small device would hit.
- Cell crossings hitch: in a fast flight, frames of 19 to 110 ms occur exactly when the count of loaded cells rises. That is S6's problem (streaming), but tiles must not add to it: reading a tile must be a range read off the main thread.

## Design, in plain language

The world is cut into square **tiles** at several sizes ("levels"): level 0 is one cell, level 1 is 2 x 2 cells, level 2 is 4 x 4 cells, and so on. Each tile stores a simplified version of the ground in its square: a grid of heights, a normal per grid point, and a colour picture of the ground (a baked "composite"). Each tile also knows its **error**: how far, in world units, the simplified ground can be from the full-detail ground. For a given camera, a tile is drawn when its error, seen from where the camera is, is smaller than a few screen pixels; otherwise a finer choice is made. Far tiles are big and coarse; near tiles are fine or are the live terrain the engine already draws.

S1 builds only one baked level (level 1, 2 x 2 cells, 16384 units a side) and lets the live terrain stay the fine choice. That is enough to prove the key layout, the file layout, the selection, the loader and the viewer, with no change to what the game does when the setting is off.

### The tile key (proposal; the container leaves the layout open, spec section 10.3)

A key is 64 bits: the top 8 bits are the tag (`0x10` for a terrain tile, as the container reserves), the low 56 are ours:

| Bits | Width | Meaning |
| --- | --- | --- |
| 63 to 56 | 8 | tag: `0x10` terrain tile (S4 will use `0x11` for object clusters with the same remaining 56 bits) |
| 55 to 48 | 8 | worldspace index into the pack's manifest (0 is Morrowind's one exterior world) |
| 47 to 44 | 4 | level, 0 to 15 |
| 43 to 0 | 44 | Morton code of the tile's (x, y): 22 bits each, bit 2k is bit k of x, bit 2k+1 is bit k of y |

The tile coordinate at level L is `(cellX + 2^21) >> L` and the same for y, so negative cells (Morrowind has them) become non-negative and a level-L tile covers cells `x * 2^L ... x * 2^L + 2^L - 1` before the bias is removed. Neighbouring tiles of one level have nearby keys (Morton order), and the container stores keyed entries in key order, so a range of the file holds a region of the world. The key is never 0 because the tag is not. A parent's key at level L+1 is found by dropping the lowest two Morton bits and setting the level field. Tests: round trip, ordering within a level equals Morton order, parent and children relation, the 22-bit limits, and pinned example values.

### The tile payload (proposal)

One entry, type `0x0010`, little endian, every section 8-aligned. Fixed header (88 bytes):

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `ETT1` |
| 4 | 2 | version (1) |
| 6 | 2 | flags: bit 0 has normals, bit 1 has albedo, bit 2 has material channels (task 0044's `_rmaos`, unused in S1) |
| 8 | 4 | grid size N: vertices per side (33 in S1) |
| 12 | 4 | level |
| 16 | 4 | tile side in world units (f32) |
| 20 | 8 | tile origin x, y in world units (f32, f32): the south-west corner |
| 28 | 4 | minimum height (f32) |
| 32 | 4 | maximum height (f32) |
| 36 | 4 | geometric error (f32, world units) |
| 40 | 2 + 2 | albedo width, height in texels |
| 44 | 4 | albedo format (0 = RGBA8, 1 = BC1; S1 writes 0) |
| 48 | 4 | albedo mip count |
| 52 | 4 | offset of heights |
| 56 | 4 | offset of normals |
| 60 | 4 | offset of albedo |
| 64 | 24 | reserved, zero |

Heights: `N * N` unsigned 16-bit numbers, row after row from the south edge, west to east in a row; the height is `min + v / 65535 * (max - min)`. Normals: two signed bytes a vertex (octahedral encoding, derived from the full-detail ground, not from the simplified grid, so lighting matches). Albedo: the mip chain, largest first, each level tightly packed. Entry fields: `hintMemorySize` is the decoded size, `layoutParamA` is N, `layoutParamB` is the albedo width. The container hashes every chunk and the whole entry, so a damaged tile is detected by the container; the reader still checks the header: magic, version, that N is between 3 and 129, that every offset and size lies inside the entry and the sections do not overlap, that min <= max and the error is finite and not negative, that the albedo size is a power of two up to 2048, and that the section sizes add up exactly. Any failure means "no tile": the live terrain is used and one line is logged.

A pack-level **manifest** entry with the path `lod/manifest.json` (canonical JSON, byte for byte round trip, located errors) holds the worldspace table (index, name, cell size 8192), the levels baked, N, the albedo size and the baker version. Provenance of each tile entry: tool id `openeden.lod.terrain`, source digest of the land records it came from and the texture list, parameters bytes of the baker settings. The pack header has the `DERIVED` flag, a `generation` that increases each bake, and a `sourceDigest` of the whole content set (the load order's land data); if it does not match the running game's, the pack is ignored and the live path is used (design 4.5; S2 makes the cache keys and swap-over safe, S1 simply rebakes everything).

### Selection by pixel error

For a tile at distance d from the camera (to the nearest point of the tile's bounding box, from its origin, side, minimum and maximum height), the error on screen is

    pixels = geometricError * viewportHeight / (2 * max(d, 1) * tan(fovY / 2))

(the formula of the design doc, section 3.3). A tile is drawn when `pixels <= threshold`; it is replaced by the live terrain for that square when `pixels > threshold`; once replaced it returns only when `pixels < 0.7 * threshold` (hysteresis, so a camera at a boundary does not flicker). `Terrain::isSufficientDetail` is untouched; the new selection is a pure function in `components/terrain` (`tileselection.hpp`) with tests that need no GPU.

**Threshold, from the S0 numbers.** The design doc's starting values are 2 pixels (High), 4 (Medium), 8 (Low). S0 shows the cost to avoid is draw commands, not triangles, so the threshold is allowed to be generous for tiles: start at **4 pixels** for Medium and let the tile viewer's forced-level pictures (same camera, tile on and off) decide. Calibration method: render the same camera pose with the tile forced on and the live terrain forced on (the viewer's toggle), difference the images (note 0025's method with `imgdiff`), and pick the error at which the 95th percentile of the difference is below one grey level of noise. The numbers go into the stage log. **Not decided here:** the final thresholds; the S0 pops measurement (stepped poses) could not separate level swaps from camera motion at this step size (note 0025, section 7), which is why this spec asks for the forced-level comparison.

### Decisions (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | First baked level | Level 1 (2 x 2 cells, N = 33: a vertex every 512 units, about 7 m), albedo 256 x 256 | Level 2 (fewer tiles, coarser); level 0 (almost the live path) |
| D2 | Key layout | The 56-bit layout above | Hash keys (lose spatial order and range reads) |
| D3 | Heights | 16-bit normalised per tile | 32-bit float (twice the size, no gain at these errors) |
| D4 | Normals | Octahedral bytes from the full-detail ground | Recompute from the coarse grid (lighting would pop at swaps) |
| D5 | Albedo | RGBA8 in S1, compressed (BC) after the container's texture annex and an encoder licence are decided | BC1 at once (needs an encoder) |
| D6 | Where the pack lives | `<user data>/openeden/lod/terrain.edenpak`, rebuilt whole on mismatch | Next to the game data (read-only installs) |
| D7 | Joins between tile and live terrain | Skirts (vertical flaps down the tile's edges, a fixed few units) for S1; geomorphing is S3 | Stitching as `getLodFlags` does (needs both sides at known levels) |
| D8 | Settings | `[WorldLod] enabled` (false), `terrain tile threshold` (pixels, 4), `terrain tile level` (1) | |
| D9 | Where the selection runs | CPU, main thread, once a frame, over a flat array of at most a few hundred tiles | Compute shader (S9) |
| D10 | Reading tiles | `findKey` then `readRange` on a worker thread, a decode budget per read (`DecodeBudget`), results handed to the main thread for upload | Reading on the main thread (hitches) |

## Stages

- **S1a Keys and payload:** `components/lod/tilekey.hpp` and `terraintile.hpp` (pure: key packing, payload write and parse with the checks above), unit tests including truncated, overlapping and over-large inputs, and a fuzz harness in the style of `edenarchive/fuzz`. No behaviour change.
- **S1b Baker:** `--bake-lod-terrain` reads Morrowind's `ESM::Land` records (65 x 65 heights, normals, the 16 x 16 texture grid) through the same path the live terrain uses, builds the level-1 tiles (heights by sampling the full grid, error by the largest difference to it, normals from the full grid, albedo by compositing the land texture layers as `Terrain::Builder` composites them), and writes the derived pack with `edenarchive`'s writer. Deterministic: two bakes of the same data give byte-identical packs (a test on generated land data).
- **S1c Tile viewer:** a panel in Eden Studio's mesh viewer (task 0043) that opens a pack, lists tiles by key, level, error and size, shows one tile's mesh, albedo and normals in the viewport, shows the whole world as a grid with the selection result for a chosen camera, and has the forced-level toggle used for calibration.
- **S1d In game:** with `[WorldLod] enabled` and a valid pack, the selection draws tiles where `pixels <= threshold` and hides the live chunks they cover; with it off, nothing here runs. Needs reading `Terrain::Paging` and `View` first, because the live chunks, the composite maps and `DistantObjects` all key off the same quadtree; the interplay is the main risk of this stage and may change the shape of S1d (an open question below).
- **S1e Measurement:** repeat S0's scenes with tiles on; draws, frame time, memory, hitches; the forced-level calibration; write the numbers to the research note.

## Constraints
- Determinism: bakes are byte-identical for the same input; selection uses no clock; tile order is key order.
- Untrusted input: tile files are derived data that a user can edit or damage; every size and offset is checked; decoded sizes are capped by the decode budget; a bad tile never crashes (design 9, root `CLAUDE.md`).
- Allocation: no allocation in the per-frame selection (a preallocated array of tile states); reads and decodes on worker threads.
- Threading: `Archive` is immutable and thread-safe for reads; GPU upload and scene-graph changes only on the main thread.
- Off means unchanged: `[WorldLod] enabled = false` runs no code of this system, starts no process, opens no file; compared by the benchmark rig's frames and the 32 Lua tests.
- Units: world units (Morrowind), Z up; cell size from the world source (8192 for Morrowind), never assumed.
- No game assets in the repository: bakes are made from the user's data and stay in the user data folder; tests use generated land data.
- New files under `components/` use lowercase letters and digits only.

Acceptance criteria:
- [ ] Key and payload tests (round trip, order, parent/child, pinned values, every malformed-input case in the list above refused with a located error); fuzz harness run clean for a fixed time.
- [ ] A bake of generated land data is byte-identical across two runs and across a different cell order; the pack passes `edenvalidate`; `findKey` finds every tile and `readRange` of the albedo tail reads only the chunks it needs (test counts chunk reads).
- [ ] The selection function: pixel error formula tests against hand-computed values, hysteresis (a camera moving back and forth across the boundary changes the choice once, not every frame), all tiles outside the view skipped.
- [ ] The tile viewer shows a tile's height mesh, normals and albedo, and the selection for a camera; forced-level images exist for at least three distances.
- [ ] In game, with a bake of the real Morrowind data: the Balmora long-view pose with tiles on shows no crack at tile-to-live joins in viewed frames (a viewed frame sequence is described in the stage log), and S0's scenes are re-run with the numbers recorded; the threshold chosen is justified by the forced-level differences.
- [ ] With `[WorldLod] enabled = false` the 32 Lua tests pass and the stats file and a frame of the Balmora pose are unchanged.
- [ ] A fresh reviewer read the code; unit tests, clang-format 14 and the check scripts pass.

## Open questions
1. How the live terrain quadtree (`Terrain::Paging`, `View`, composite maps) hides the chunks a tile replaces, and whether `DistantObjects` should hide its chunks under a tile, which needs a read of `paging.cpp` before S1d is shaped (spec D7 and the "interplay" risk).
2. Whether the albedo should be baked at all in S1 or whether tiles should reuse the live composite maps until S3 (the design doc, S3: "composite map bake replaces runtime composite").
3. The container's tile annex (section 10.3) says "freezing too early" is a risk: this spec's layouts stay a proposal until S1e, then become the annex for version 0.2.

Out of scope: object clusters (S4), geomorphing (S3), the cache's content-hash keys and swap-over (S2), streaming budgets and prefetch (S6), the background baker and its UI (S8), compute culling (S9), Fallout: New Vegas land (S12), texture compression, and any change to how the live terrain looks when the setting is off.

## Stage log (implementer's notes)

### Draft status of the layouts (read this first)

toddhow has NOT approved the tile key and payload layouts above. They are built as **layout version 0 (draft)**, with no promise of stability:

- `Lod::tileKeyLayoutVersion = 0` (`components/lod/tilekey.hpp`) is written into the pack manifest (`layoutVersion`); `TilePack::open` refuses a pack with another value.
- The payload header's version field holds `Lod::terrainTilePayloadVersion = 0`, not the 1 of the table above; the reader accepts only 0. A final layout will use a new number, so a draft tile can never be mistaken for a final one.
- Everything that knows a bit position or an offset lives in `components/lod/` (`tilekey`, `terraintile`, `tilepack`). The rest of the engine uses the functions, never the numbers. Changing a layout later means editing that folder and its tests.
- Nothing is on by default: `[WorldLod] enabled = false` (see stage c and d below).

### S1a and S1b (done 2026-11-02)

- **Where the code is.** `components/lod/`: `tilekey` (key packing, Morton code, parent and children), `tileselection` (pixel error, hysteresis; the spec says `components/terrain/tileselection.hpp`, but the draft is kept behind one folder and the inherited `components/terrain` is untouched), `terraintile` (payload writer and reader), `tilepack` (derived pack: manifest, writer, `TilePack` reader).
- **Choices the spec left open (all can change with the final layout).**
  - A tile coordinate in the key is the biased cell coordinate shifted right by the level, so it must be below 2^(22 - level); `packTileKey` refuses larger ones.
  - The payload is canonical: heights at 88; normals at the next multiple of 8; albedo at the next multiple of 8; total size is the end of the last present section rounded up to 8; padding is zero. A section that is absent has offset 0 and size 0. The reader refuses anything else, so a payload that parses always rewrites to the same bytes.
  - With no albedo flag, the albedo width, height, format and mip count must be 0. The material flag (bit 2) is refused by the reader.
  - Albedo mip level m of a W x H picture is max(1, W >> m) by max(1, H >> m), non-square power-of-two pictures are allowed; the mip count is 1 to log2(max(W, H)) + 1.
  - The manifest is canonical JSON with keys in alphabetical order at every level, no spaces, no newline: `{"albedoSize":256,"bakerVersion":1,"gridSize":33,"layoutVersion":0,"levels":[1],"worldspaces":[{"cellSize":8192,"index":0,"name":"Morrowind"}]}`. The parser accepts only that exact text (it parses, writes again and compares), and reports the offset of the first difference.
  - A tile entry sets `hintMemorySize` to the payload size (the spec says "decoded size"; the decoded GPU size is not known to the pack code), `layoutParamA` to N and `layoutParamB` to the albedo width. The manifest entry is generic (type 0) with provenance tool id `openeden.lod.terrain`.
  - The selection also skips tiles beyond the view distance and outside a conservative view cone (a bounding sphere against the cone that reaches the screen corners). A skipped tile keeps its choice so hysteresis survives looking away.
- **Tests** (written by a separate test-writing agent from the spec text and the declaration-only headers, before the implementation): 107 tests in `apps/components_tests/lod/` (key 16, selection 24, payload 36 including a deterministic mutation test of 20,000 mutated payloads, pack 31). They need no GPU, window, network or game data. The libFuzzer harness of the spec is not added; the mutation test is the form CI can run (GCC has no libFuzzer).
- **Not done in this stage:** a test that `edenvalidate` accepts a built pack (done in stage c on a real file).
- **Reviewer findings handled (S1a/S1b):** a payload stored under a key whose level differs is refused by `readHeader` and `readTile`; the manifest's values are range-checked (grid size 0 or 3 to 129, albedo size 0 or a power of two up to 2048, levels up to 15, worldspace index below 256, cell size above 0) and the JSON reader stops at 4096 values; preconditions of the key functions and the camera are written in the headers. Not done: a decode budget on `readHeader` (the range read decodes one chunk), hoisting the camera's trigonometry out of the selection loop, and checking tile payloads against the manifest at `open` (the baker's job). Pack byte equality is promised only with `TilePackOptions::mStoreOnly = true` (zstd output is promised equal for the same zstd version only, as the container spec says).

### S1c: the terrain bake (done 2026-11-02)

- **What it is.** `openmw --bake-lod-terrain` (optionally `--bake-gi-out <folder>`) starts a new game without the menu like the other bakers, bakes during the first running frames and quits. Output: `<user data>/openeden/lod/terrain.edenpak` (D6), a derived pack. The code is `components/lod/tilebake.*` (pure; the land comes through `Lod::LandSource`) and `apps/openmw/lodterrainbaker.*` (the game side). The land is read through `Terrain::Storage::getVertexData` of the same `MWRender::TerrainStorage` the live terrain uses, with `lod = 2` for the 33-vertex grid and `lod = 0` for the 129-vertex full grid used for the error and the digest, so a tile is made from what the game would draw (normals already fixed across cell borders). The cell size is `Storage::cellWorldSize`.
- **Deterministic and resumable.** Each tile has a source digest (BLAKE3 of its id, the settings and the full-detail samples); the pack digest is the BLAKE3 of the manifest and the tile digests in key order. A bake of the whole of Morrowind takes under a second, so there is no partial state: if the pack on disk has the same digest and manifest, nothing is written (log: "already up to date"); otherwise the generation goes up by one. Two bakes of the real data into two folders gave byte-identical files (same SHA-256). The pack passes `edenvalidate`.
- **Numbers (Morrowind + Tribunal + Bloodmoon, no other plugins).** 1540 land cells, 414 tiles of level 1, 3,512,696 bytes (stored without compression, so the bytes do not depend on the zstd version; the first bake with compression was 2,787,704), 0.83 s, all 414 read back through `TilePack::readTile`. Geometric error of the tiles in world units: min 0 (open sea), median 190, 95th percentile 467, max 1136. Assuming a 60 degree vertical field of view (not checked against the camera code), at 4 px and 720 p the median tile is within threshold only beyond about 29,600 units; the 95th percentile tile beyond about 73,000. So level 1 tiles with 512-unit vertex spacing do not replace live terrain inside the default 7168 view distance; they only matter for long views.
- **Not baked: albedo** (spec open question 2; the safest option). Tiles have `flagHasAlbedo` off and the manifest says `albedoSize` 0. Drawing in stage d takes the colour from the live terrain data.
- **Per-tile `hintMemorySize`** is the payload size; the provenance parameters are five little-endian words: baker version, level, grid size, cell size, worldspace index.

### S1d: tiles in the game (done 2026-11-02, with limits listed below)

- **How it works.** With `[WorldLod] enabled = true`, `RenderManager::setStore` makes a `MWRender::LodTerrain` (`apps/openmw/mwrender/lodterrain.*`; pack path from `RenderManager::setLodTerrainPack`, set by the engine to `<user data>/openeden/lod/terrain.edenpak`). A worker thread (`std::async`) opens the pack, bakes the same tiles in memory from the loaded land and refuses the pack if its digest or manifest differ ("stale", logged as a warning; design 4.5), and reads every tile (D10; each is 4.3 KiB). Then, once a frame on the main thread, `RenderManager::updateTerrain` gives it the camera: `Lod::selectTiles` chooses tile or live for every tile (threshold from `[WorldLod] terrain tile threshold`), newly chosen tiles are built (at most 4 a frame) with a second `Terrain::Builder` whose `BakedStorage` returns the tile's heights and normals but the live terrain's colours and blend maps (this is the spec's open question 2: no albedo is baked, the live composite is reused), and the tiles that are ready go into a `Terrain::Coverage`. `Paging::updateView(..., coverage)` leaves out live chunks inside a covered square, splits chunks that only overlap one, and stitches live chunks next to a tile to the tile's vertex LOD. The preload thread passes no coverage, so its behaviour is unchanged.
- **Changes to inherited code (all in `components/terrain`, all inert without a coverage):** `Paging::updateView` has a new last parameter; `ViewData::coverageVersion`; new `coverage.hpp`. A view is rebuilt when the coverage version or the number of entries changed (`ViewData::add` only notices a chunk that differs at the same index, not a list that got shorter; a reviewer found this would leave a hole or a double draw).
- **Verified (Windows, Morrowind + Tribunal + Bloodmoon, Balmora pose of `tools/benchmark_data/balmora_view.txt`, 30000 view, distant terrain on, 1280 x 720, frames at running frame 300).**
  - The pack is accepted as current (log: 414 tiles loaded) and the staleness check passes on the real data.
  - `enabled = false`, `enabled = true` with no pack, and a second run with `enabled = false` differ from each other by the same amount of run-to-run noise: 25 to 47 percent of pixels by at least 1 grey level, 0.1 to 0.34 percent by more than 4, 0.017 to 0.054 percent by more than 16 (the torch flicker and the sky). With `enabled = true` and the default threshold 4 no tile is chosen in this pose (the median tile error of 190 units needs a distance over 29,600), and the frame differs from `enabled = false` by 0.21 percent over 4, within that noise.
  - Threshold 64: four to seven tiles are drawn. The difference to the live frame is 0.9 percent over 4 and 0.13 percent over 16, and sits on the silhouettes of far ridges (coarser geometry); the difference map (8 times amplified) shows no seam line or hole.
  - Threshold 10^9 (every tile forced): the foreground hillside is visibly coarser (faceted at 512-unit spacing), 36 percent of pixels differ by more than 4. This is the forced-level picture: what a tile looks like when it is far too close.
  - Frame time medians of these runs (3.7 to 4.4 ms) are inside this machine's run-to-run spread (the two `enabled = false` runs gave 3.66 and 4.37 ms); no cost or gain is claimed.
  - A flight at 15000 view and threshold 64 for 10000 frames ran without a crash dump; 5 tiles were drawn at the end.
  - 32 Lua integration tests pass, components-tests and openmw-tests pass.
- **Not verified / known limits.**
  - **Draw count is not reduced.** A tile is one draw like a live chunk of the same size; the saving the spec hopes for comes with S4 (clusters) and from skipping the live chunk's blend and composite work, which was not measured.
  - **Cracks:** a tile is drawn with no stitching flags (D7's skirts are not built), so a tile next to a *coarser* live chunk can show a crack; live chunks finer than a tile stitch to it. No crack was seen in the frames above, but only a few tiles were drawn and no walk along a tile border was done.
  - **Threshold calibration** (forced-level 95th percentile method) was not done: only three thresholds were looked at.
  - **Hitches:** each built tile compiles on the main thread (4 a frame at most); not measured. Tile data is read on the worker (all at start), the digest check bakes the whole world on that worker at every start (about 1 s of one core) and a quit during it waits for it.
  - The second `Terrain::Builder` builds a second copy of the layer texture array; memory not measured.
  - The Eden Studio tile viewer (stage e) was not built.
  - Shadows, picking and the map do not see the tiles (they use the live view only).

### Layout freeze: version 1 (2026-11-02, approved by toddhow, decision 0010 "LOD tile layouts")

The key layout, the 88-byte payload header and the canonical manifest of this spec are approved as written, with one change from the proposal: the header's version field is **1** (the draft built so far wrote 0). `Lod::tileKeyLayoutVersion` and `Lod::terrainTilePayloadVersion` are both 1; the reader accepts only 1; the manifest says `"layoutVersion":1`; `Lod::bakerVersion` is 1. A pack baked with the draft (version 0) is refused with a located error and must be baked again. The "draft" wording is removed from the code, the setting's comment and the module notes. The text of the "Draft status" section above is history.

Tests changed because of the freeze (each fixes bytes or a number of the layout, so it changed with it; nothing else was touched):
- `LodTerrainTile.header_bytes_of_a_tiny_tile_derived_by_hand`: the version bytes 4 to 5 are `01 00` (were `00 00`), and the check of `terrainTilePayloadVersion` is 1.
- `LodTerrainTile.header_words_of_a_tile_with_normals_and_albedo`: the version byte is checked against 1.
- `LodTerrainTile.a_version_other_than_one_is_refused_at_offset_4` (was `..._zero_...`): the refused values are 0, 2, 0x100, 0xFFFF (were 1, 2, 0x100, 0xFFFF).
- `LodTilePack.manifest_writes_the_pinned_canonical_text` and `manifest_parses_the_pinned_text_and_round_trips_byte_for_byte`: the pinned text has `"layoutVersion":1`.
- `LodTilePack.open_refuses_a_manifest_with_another_layout_version`: refused values 0, 2, 4000000000 (were 1, 2, 4000000000).
- `LodTilePack.open_refuses_a_pack_without_the_derived_flag` needed no edit of its own: it builds on the pinned manifest text above.

Re-bake under version 1: two bakes of the real data (Morrowind, Tribunal, Bloodmoon) into two folders are byte-identical (same SHA-256), 414 tiles, 414 read back, `edenvalidate` OK.
