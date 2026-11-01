# 0018 — World LOD and asset streaming: what OpenEden does today, what others do, what to measure

Date: 2026-11-01 · Asked by: toddhow ("seamless world, indiscernible LOD like RDR2, full-map view distance, grass that does not vanish within about 15 m, better LOD assets, asset streaming"; "start the research note and design doc first") · Status: **research, nothing decided and nothing built.** The design that follows from this note is `docs/design/world-lod-and-streaming.md`. Written at commit `e8530fe6bb` on `vulkan`; no build was run to write it.

## How to read this note

- Section 1 is read from the code at this commit and cites file and function. Section 2 to 4 come from web pages opened while writing it; each says what the page supports. Anything from memory or from a secondary source is marked **verify**, and "unknown" means nobody found out yet.
- Plain words first: LOD ("level of detail") means drawing a cheaper version of something when it is far away. A "pop" is the moment the cheap version is swapped for the better one and you see it jump. A "stall" is a frame that takes much longer than the others because the game waited for disk or for a build.
- Units: Morrowind and OpenEden use game units; a Morrowind exterior cell is 8192 units (`components/misc/constants.hpp`, `Constants::CellSizeInUnits`). How many units make a metre is not stated anywhere in this repository's code that I read; the community figure of about 70 units per foot (about 21.3 per metre) is **verify**. Under that figure 15 m is about 320 units and the groundcover default of 6144 units is about 290 m.

## 1. What OpenEden does today (read from the code)

### 1.1 Terrain: a quadtree of chunks (`components/terrain/`)

- **Cell grid and sizes.** A `Bounds` is a square in cell units, with a smallest size of 0.125 cell (`bounds.hpp`, `Bounds::minSize`). Morrowind's land is `ESM::Land::LAND_SIZE` vertices a side (`components/esm3/landrecorddata.hpp`, `sLandSize = 65`).
- **Choosing what to draw.** `Paging::updateView` (`paging.cpp`) walks a quadtree: for each square within the view distance (plus a 150-unit reuse margin and, when preloading, one extra cell) it asks `isSufficientDetail(size, distance, lodFactor)` (`lod.hpp`). Detail is chosen by comparing the base-2 logarithm of the square's size with that of distance divided by `[Terrain] lod factor`. So a chunk's level is a function of distance only. Nothing looks at screen-space error, the height variation of the chunk, or the camera's field of view.
- **Batches.** Chunks are drawn in batches of up to `mMaxBatchSize = 16` cells a side (`Paging::getBatchSize`), sorted front to back, with a per-chunk vertex LOD (`getVertexLod`, controlled by `[Terrain] vertex lod mod`).
- **Seams between levels** are stitched with `lodFlags`, four 4-bit fields saying how many levels coarser each neighbour is (`getLodFlags` inside `updateView`), so the finer chunk drops vertices along the shared edge. That removes cracks; it does not blend.
- **No blending between levels.** When the camera moves, `View::reuseDistance` (150 units, `view.hpp`) is the only hysteresis: the view is rebuilt after that much movement, and the new set of chunks replaces the old (`CompileOp`, `paging.cpp`). I found no cross-fade, no geomorph and no dither in `components/terrain` (searched the directory by reading `paging.cpp`, `lod.hpp`, `builder.hpp`, `viewdata.hpp`; **verify** the shader side in `files/shaders/terrain/`).
- **Textures.** Far chunks switch from per-layer blended textures to a baked "composite map" (`[Terrain] composite map level`, `composite map resolution` = 512 by default, `max composite geometry size` = 4.0; `files/settings-default.cfg`, `[Terrain]`; `layermaps.hpp`, `TerrainMapSettings`). Where this switch happens is a likely visible colour or sharpness step; not yet measured.
- **Caching.** `Builder` keeps `indexCache` and a `dataCache` keyed by batch (`builder.hpp`); `Paging::pruneCache` trims it. Nothing is stored on disk: every session rebuilds batch data from the ESM's `Land` records.
- **Data source.** `MWRender::TerrainStorage` reads `ESM::Land` for Morrowind. `LandManager::create` returns an empty `LandObject` for any ESM4 worldspace (`apps/openmw/mwrender/landmanager.cpp`: "ESM4 terrain is not ported (task 0012)"). So the ESM4 record parsers exist (`components/esm4/loadland.cpp`, `loadwrld.cpp`, `loadcell.cpp`; `Constants::ESM4CellSizeInUnits = 4096` and `ESM4CellGridRadius = 2` in `components/misc/constants.hpp`) but nothing draws ESM4 land. `components/esmterrain/` holds only `gridsampling.hpp`; the storage code is in `components/esm3terrain/`.
- **Default view distance.** `[Camera] viewing distance = 7168.0` (under one cell) and `[Terrain] distant terrain = false` (`files/settings-default.cfg`). So by default the world ends a cell away. The benchmark scenes set 30000 units and `distant`.

### 1.2 Distant objects (`apps/openmw/mwrender/distantobjects*`)

- `DistantObjects` draws statics, activators and doors (`isDistantObjectType`: `ESM::REC_STAT`, `REC_ACTI`, `REC_DOOR`, in `distantobjectcells.cpp`) of cells outside the active grid, one chunk per cell, built on its own worker thread, compiled on the main thread by a later `update()` (class comment in `distantobjects.hpp`).
- The class comment lists what is **not** ported from OpenMW's object paging: merging objects into shared geometry, the merge-factor settings, LOD meshes, paging the active grid and ESM4. "So each object is its own draw, which costs more than OpenMW at the same settings." The `[Terrain] object paging merge factor` setting exists in the defaults file but is not read by this class (**verify** by grep before a task relies on it).
- **Cut-off.** Each object is wrapped in a `vsgUtil::DistanceLOD` whose far limit is `distantObjectMaxDistance(radius, scale, minSize) = radius * scale / minSize` (`distantobjectcells.cpp`; `distantobjects.cpp` around line 186), with `minSize` from `[Terrain] object paging min size` (0.01). An object therefore vanishes completely at one distance, with no fade. A 100-unit-radius rock disappears at 10000 units.
- `Scene::removeFromPagedRefs` is an empty function and `mPagedRefs` stays empty (`apps/openmw/mwworld/scene.cpp`, comment "vsgopenmw's renderer has no object paging").
- Objects of the **active grid** (the 3 x 3 cells around the player, `Constants::CellGridRadius = 1`; 5 x 5 for ESM4, `getHalfGridSize` in `scene.cpp`) are the full game objects with animation, physics and lights, loaded by `Scene::loadCell`.

### 1.3 Groundcover (grass)

- Chunks are built per cell around the camera on a worker thread (`Groundcover`, `groundcover.hpp`). The cells in range are recomputed after the camera moves 1024 units (`sUpdateDistance`, `groundcover.cpp`).
- `[Groundcover] density` thins instances with a running counter (`GroundcoverDensity`, `groundcoverinstances.hpp`): every Nth instance in cell order is kept, the same everywhere.
- **The cut-off is a hard test per instance in the vertex shader:** `main.vert` line 144 compares the distance of the instance to `scene.groundcoverParams.x` (`[Groundcover] rendering distance`, default 6144) and drops it past that. The header comment calls this OpenMW's `groundcoverFadeEnd`, but the code has no fade over a range.
- So there are two separate reasons grass can "vanish": (a) the distance cut-off, 6144 units by default (about 290 m on the **verify**-marked scale), and (b) groundcover is **off** by default (`enabled = false`). The report of grass vanishing within about 15 m is not explained by these defaults; it may describe New Vegas, a mod's own settings, or another engine's behaviour. Section 5 includes a measurement for it. Unknown.
- Grass shape comes from the model (an alpha-tested quad cluster; alpha test 128/255, class comment) so far grass is thinned by nothing but the density counter: no per-instance fade-by-density with distance.

### 1.4 Cell loading and the preloader

- `Scene::changeCellGrid` unloads and loads cells when the player crosses a border (`scene.cpp` lines 593 to 640). `Scene::update` calls `mPreloader->updateCache()` and `preloadCells(duration)` (`scene.cpp` around 330).
- `preloadCells` (around line 998) runs `preloadTeleportDoorDestinations`, `preloadExteriorGrid` and optionally `preloadFastTravelDestinations`, using the player's **predicted position** `[Cells] prediction time` (1 s) ahead; the `CellPreloader` class (`mPreloader`) keeps 12 to 20 cells (`preload cell cache min/max`) for 5 s after use (`preload cell expiry delay`). `CellPreloader` is used from `apps/openmw/mwworld/scene.hpp` (its class body was not read). `preload num threads = 1`, so there is one preload thread by default.
- A terrain preloader (`Terrain::Preload`, `components/terrain/preload.cpp`) is told the predicted positions each frame (`mTerrainPreloader->setPositions`).
- `Scene::preload(const std::string&, bool)` is an empty stub (comment: "Single-mesh preloading returns with task 0010").

### 1.5 Files, archives and caches

- **VFS.** `VFS::File::open()` returns a `std::istream` (`components/vfs/file.hpp`); `VFS::Manager::getNormalized` looks a path up (`manager.hpp`). A file inside a BSA is opened by `Bsa::BSAFile::getFile` as a constrained stream over the archive file at an offset (`components/bsa/bsafile.cpp`, `openConstrainedFileStream`). There is no memory mapping, no asynchronous read and no request priority in this layer; every read is a blocking stream read on the calling thread.
- **BSA reader.** `components/bsa` has `BSAFile` (Morrowind), `CompressedBSAFile` with `Version_TES4 = 0x67`, `Version_FO3 = 0x68`, `Version_SSE = 0x69` (that is 103, 104 and 105; `compressedbsafile.hpp`), and BA2 readers (`ba2gnrlfile`, `ba2dx10file`). It includes zlib and lz4 (`compressedbsafile.cpp`). Whether Fallout: New Vegas uses version 104 is **verify** (the UESP BSA page I opened documents version 103 and gives no game table for 104).
- **Resource caches.** `Resource::ObjectCache` expires entries not referenced for an `expiryDelay` (`components/resource/objectcache.hpp`, `update`); managers expose `setExpiryDelay`. The delay comes from `[Cells] cache expiry delay = 5` (`Settings::cells().mCacheExpiryDelay`, used in `apps/openmw/mwphysics/physicssystem.cpp` line 290 and elsewhere). The policy is **time-based, not memory-budget-based**: nothing evicts to stay under a number of megabytes (searched the resource directory; **verify** for textures held by VSG).
- **Images.** `vsgUtil::readOptionalImage` / `readImageFromMemory` read images through vsgXchange (`components/vsgutil/readimage.cpp`; texture reads strip the sRGB flag). I found no mip streaming or texture residency logic: a texture is read in full at its stored size, with the mips it has.
- **Models.** NIFs are read through `NifFileManager` and instantiated per cell. Collision shapes come from `physicsshapemanager`.

### 1.6 Other pieces that matter

- **Small feature culling.** The only "too small to draw" rule found is the distant-object cut-off above (`radius * scale / minSize`). A general screen-size cull for active-grid objects was not found in `apps/openmw/mwrender` or `components/view` (searched for "small feature", "min size", "screen size"; **verify** in `components/vsgutil`).
- **Occlusion.** `apps/openmw/mwrender/occlusionbox.*` exists; what it does was not read. **verify**.
- **Device report.** `components/render/devicereport.hpp`: `collectDeviceReport` writes a key and value list to `device-report.json` when `[Rendering] device report = true`; note 0009 section 4 lists what it records (timestamps, subgroup operations, descriptor indexing, 16-bit storage, push descriptors, multiview, ray query, buffer device address, draw indirect count). Not in that list, so unknown until read: mesh shader support, `multiDrawIndirect`, sparse residency, device-local memory budget extension (`VK_EXT_memory_budget`). **verify** and add to the report if absent.
- **GPU timing and the benchmark rig.** `tools/benchmark.sh` runs the `heavy_*` scenes of `tools/render_scenes.txt`, writes frame stats and per-pass GPU times (note 0009). On the RTX 4070 test machine the frames are **CPU bound** (wall 1.4 to 3.8 ms against 0.02 to 0.27 ms GPU main-scene time; note 0009 section 3). That matters for this work: more draw calls or more per-frame CPU culling lands on the scarce resource.
- **An offline baker already exists** for light probes (`--bake-gi`, task 0028; `components/render/probebake.*`), with file naming and JSON format in `components/render/probes.*`. It is a precedent for the baker here, but it runs as a command line mode, not in the background of a playing game.

### 1.7 Where pops, cut-offs and stalls come from (summary of 1.1 to 1.6)

| Symptom | Source in the code | Strength of evidence |
| --- | --- | --- |
| Terrain pops as the camera moves | quadtree level by distance, nodes swapped with no blend (`Paging::updateView`) | read from code; visual size not measured |
| Terrain texture step at distance | composite map switch (`composite map level`) | read from code; not measured |
| Objects pop in and out at their cut-off distance | `DistanceLOD` with a single range, no fade (`distantobjects.cpp`) | read from code |
| Objects pop when a cell enters the active grid | distant chunk replaced by the real objects (`Scene::loadCell`, `DistantObjects::setActiveGrid`) | read from code; the two forms look different (no LOD meshes) |
| Few objects can be shown far away | each object is its own draw, no merging | class comment; CPU-bound frames per note 0009 |
| Grass cut at a distance | per-instance hard test, `main.vert` line 144 | read from code |
| Stall when crossing into new cells | blocking stream reads on loader threads, one preload thread, cells loaded on the main thread after preloading (`Scene::changeCellGrid`) | read from code; hitches not measured |
| Memory use grows with view distance | time-based expiry, no budget | read from code |
| Start-up and first view cost | batch data rebuilt from records every session; nothing cached to disk | read from code |
| No land at all in ESM4 worldspaces | `LandManager::create` | read from code |

## 2. How other games and tools do distant land

Each item says which page supports it. "Memory" means I wrote it from general knowledge and no page was opened.

### 2.1 Bethesda's own LOD, as modders document it

- **Skyrim.** Terrain LOD lives in `Meshes\Terrain\<Worldspace>\` as `.btr` files, with textures in `Textures\Terrain\<Worldspace>\`; object LOD is `.bto` under `...\Objects\`; tree LOD is `.btt` files plus a `<Worldspace>.lst` and an atlas texture; `Data\lodsettings\<worldspace>.lod` sets the grid origin, the stride and the LOD range, and the generators can only produce LOD for worldspaces that have such a file. File names encode level and position, for example `tamriel.32.-32.0.btr`. Four LOD levels are seen in a modder's listing, the coarsest used for the 3D map. A forum report says `.btr` meshes are ordinary NIFs. Sources: search results from the Beyond Skyrim wiki (https://wiki.beyondskyrim.org/wiki/Arcane_University:LOD_Generation), UESP LOD settings page (https://en.uesp.net/wiki/Tes5Mod:LOD_Settings_File_Format, its field-level text did not load) and Nexus pages; **all secondary, none Bethesda's.** No format specification for `.btr`/`.bto` was found.
- **So ".lod" is not a terrain-LOD file in Skyrim, it is a settings file.** The brief for this note suggested `.lod` as a terrain LOD data file; the sources found say otherwise for Skyrim. For Fallout: New Vegas no `.lod` file was confirmed at all (**verify**).
- **DynDOLOD** (Skyrim) is a generator run after the load order is final. It produces object LOD (merged meshes and textures of static objects), tree LOD, "dynamic" LOD for objects that change, grass LOD, occlusion data and a terrain underside; its helper TexGen prepares textures and billboards; it takes terrain LOD from xLODGen. Its page lists TES5, SSE, VR and Enderal as supported and says nothing of New Vegas, TexGen also lists FO4. Source: https://dyndolod.info/Generation-Instructions. The lesson for us: object LOD is a **per-load-order bake** of merged geometry and texture atlases, because mods change what stands where.
- **Fallout: New Vegas.** The Viva New Vegas guide (https://vivanewvegas.moddinglinked.com/lod.html) describes: xLODGen (build 130 or newer; "FNVLODGen" is the older name) generates terrain LOD first and object and tree LOD second; terrain LOD has four levels (LOD4, 8, 16, 32) with a texture size option (512 for the finest, 256 for coarser is "usually enough"), baked normal maps and mipmaps; the generated files must load last; terrain LOD must be regenerated when terrain changes and object LOD when objects change; packing LOD into BSAs reduces stutter when entering new areas, but only object textures should be compressed, because meshes and landscape data "are streamed". A Nexus search summary (https://nexusmods.com/newvegas/mods/58562) says terrain LOD sits in `Meshes\Landscape\LOD` and `Textures\Landscape\LOD`, object LOD in large NIFs under `Meshes\Landscape\LOD\<Worldspace>\Blocks`, built from per-model `_lod.nif` meshes beside the full-detail models, and tree LOD is an atlas plus binary placement data (".dtl" in that page's wording, **verify**). I have not opened a New Vegas install, so none of this is checked against files.
- **TES4/FO3/FNV exterior cell size.** The repository uses 4096 units (`Constants::ESM4CellSizeInUnits`). One search summary of the GECK wiki called exterior cells "4000 unit grids" (https://geck.uesp.net/wiki/Bethsoft_Tutorial_Finalize_and_Connect); I could not open the GECK page that would settle it (HTTP 403). Treat 4096 as the engine's working assumption, **verify** against a real worldspace.

### 2.2 Engine-level techniques

- **Hierarchical LOD (HLOD).** Unreal's HLOD "groups several Static Mesh Actors into one proxy mesh and Material that displays at long view distances", reducing draw calls (https://dev.epicgames.com/documentation/en-us/unreal-engine/hierarchical-level-of-detail-in-unreal-engine, the part that loaded). How World Partition builds HLOD layers per streaming cell could not be read from the page that loaded; **verify** from Epic's World Partition pages. This is the model closest to what DynDOLOD does by hand.
- **Nanite-style cluster LOD.** The SIGGRAPH 2021 "Deep Dive into Nanite" slides by Brian Karis are at https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf (I read only a search summary of them: triangles chosen over voxels and others, a GPU-driven pipeline, a visibility-buffer approach for tiny triangles). From memory, **verify**: meshes are cut into clusters of about 128 triangles, grouped and simplified into a DAG so that any cut through the graph is crack-free, the choice of cut is by projected error under a pixel threshold, and cluster data streams from disk on demand. Needs compute culling, and its fast path wants mesh shaders or a software rasteriser; as a floor it is too high for older GPUs.
- **meshoptimizer** (https://github.com/zeux/meshoptimizer, MIT by the page's sidebar) offers a mesh simplifier, a meshlet builder, vertex/index/meshlet compression, and a companion `clusterlod.h` "clustered continuous LOD" header. It is the realistic open-source building block for cluster LOD and for baking simplified meshes.
- **Terrain.** *Geometry clipmaps* (Losasso and Hoppe, 2004; memory, **verify**) keep nested square rings of the height map around the camera. *CDLOD* (Strugar, "Continuous Distance-Dependent Level of Detail for Rendering Heightmaps", memory plus forum threads at gamedev.net) uses a quadtree like ours but morphs each vertex towards the coarser level as it nears the switch distance, so a swap is invisible; a thread I opened says the sample projects use vertex-texture fetch (Shader Model 3), and that the predecessor is Röttger et al. 1998 (geomorphing on a quadtree). I did not open Strugar's paper itself. Our existing quadtree is already the right shape for CDLOD; the missing piece is the morph.
- **Impostors / billboards.** A far tree or building drawn as a camera-facing picture (or several, rendered from different angles into an atlas). Skyrim's tree LOD and DynDOLOD's billboards are this idea (sources above). Octahedral impostors (memory, **verify**) store views over a hemisphere in a texture and interpolate between them.
- **Virtual texturing / texture streaming.** From memory, **verify**: *mip streaming* keeps the small mips resident and fetches larger ones by on-screen need; *virtual (sparse) texturing* splits a huge texture into pages and a feedback pass says which pages are visible. Vulkan sparse images exist but are an optional feature; a software page table works on any GPU. KTX2 stores mips separately (see the archive doc), which suits the first form; the second needs more.
- **Dithered cross-fade.** Draw both LOD versions for a short time and discard pixels by a screen-space noise pattern in complementary patterns so the two blend over a few frames, with no sorting and no transparency (memory, **verify** against Unreal's "dithered LOD transitions" and the Crytek/Unity descriptions). It costs a little overdraw only during the fade and works with temporal anti-aliasing.
- **GPU-driven culling.** A compute pass tests every instance or cluster against the frustum (and optionally a depth pyramid) and writes indirect draw commands; the CPU stops looping over objects. Needs indirect draw (and ideally `multiDrawIndirect` or draw-indirect-count, which the device report records as present on the test machine). Memory; relevant source is the Nanite slides above. Because OpenEden's frames are CPU-bound (note 0009), this is the technique with the largest likely win and also the one that raises the hardware floor.
- **Grass.** Common schemes (memory, **verify** each): density that falls off with distance (instances are removed by a hash of their position, not by every Nth, so the change cannot be seen as a pattern), cross-fade by shrinking the blade width or height to zero over a range, replacing blade geometry by a flat ground-colour patch baked into the terrain texture beyond a radius, and one draw per patch with GPU instancing. DynDOLOD's "grass LOD" is a baked billboard version (page above).
- **RDR2 and Rockstar.** I searched for a Rockstar talk on terrain LOD and streaming and found **none**. Rockstar's GDC 2020 talks on RDR2 were about wildlife and player locomotion (https://gdconf.com/article/learn-how-rockstar-breathed-life-into-the-wildlife-of-red-dead-redemption-2-at-gdc/). Everything said about RDR2's LOD comes from tech reviews and players; there is no primary source, so this project cannot copy "how RDR2 does it" and should not claim to. Terrain talks from other studios exist and are the nearest primary sources: EA DICE's "Terrain in Battlefield 3" (https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/gdc12-terrain-in-battlefield3.pdf, not read), and the GDC Vault sessions "Landscape Creation and Rendering in REDengine 3" and "Terrain Rendering in Far Cry 5" (titles only). **verify** by reading them before borrowing from them.

### 2.3 What this says for OpenEden

- The Bethesda-engine mod scene's answer to "see the whole map" is a **baked, per-load-order, hierarchical** set of LOD terrain tiles, merged object blocks and tree billboards. That is the same shape as engine HLOD. OpenEden can do that bake itself, in-engine, from the data it already reads, and then also add what the old engines never had: smooth transitions, streaming and GPU culling.
- "Indiscernible LOD" in the sense of no visible pops needs three things together: transitions that are hidden (cross-fade or morph), LOD meshes whose look matches the near version (same silhouette, colour, lighting), and fog or atmosphere that hides the remaining difference. The design doc takes each in turn.

## 3. New Vegas facts that matter (all marked)

| Fact | Status |
| --- | --- |
| Engine family: Gamebryo, TES4-style ESM records, BSA archives, NIF, DDS, KF | stated by the task brief; consistent with the repo having `components/esm4`, a `Version_FO3` BSA path and `Constants::ESM4CellSizeInUnits` |
| Exterior cell size 4096 units | repo constant; one GECK summary said 4000; **verify** on data |
| Active cell grid 5 x 5 for ESM4 | repo constant `ESM4CellGridRadius = 2`; New Vegas's own `uGridsToLoad` is **unknown**; **verify** |
| Main worldspace `WastelandNV`, coordinates go negative (example cell -22, -19) | https://geck.uesp.net/wiki/GetExteriorCell via search summary; extent unknown |
| Number of exterior cells, world size in km, objects per cell | **unknown**; to be counted by a tool over a real install (section 5.7) |
| Texture sizes (landscape, object, LOD atlas) | **unknown** for vanilla; xLODGen defaults are 512 or 256 for terrain LOD, atlas sizes up to the tool's limit (guide above) |
| BSA version used by the game (104?) | **verify**; reader supports 103/104/105 |
| Terrain LOD: 4 levels LOD4/8/16/32, generated by xLODGen/GECK; object LOD in `Meshes\Landscape\LOD\<Worldspace>\Blocks`; `_lod.nif` per model | Viva New Vegas guide and a Nexus summary; not checked on files |
| Whether vanilla shipped terrain LOD for every worldspace | one Nexus summary said terrain LOD is rarely regenerated; **verify** |
| LOD data is in BSAs or loose | both seen in the guide (it packs generated LOD into BSAs); vanilla layout **verify** |
| Tree LOD data format | binary placement file per Nexus summary; format **unknown** |

Nothing is known about Tale of Two Wastelands or other big-world mods beyond the guide's remark that object textures can exceed 2 GB.

## 4. Libraries and licences to evaluate

No dependency is added by this note. A licence is **verified** only where I opened the project's page and read the stated licence there. GPL-3.0-or-later compatibility is my reading of well-known licence terms and is **verify** before `docs/dependencies.md` records anything (root `CLAUDE.md`, Guardrails).

| Library | What for | Licence as shown on the project's page | GPL-3 compatibility |
| --- | --- | --- | --- |
| meshoptimizer | simplify, meshlets, compress meshes | MIT (repository sidebar; LICENSE.md text not read) | MIT is generally compatible; **verify** |
| KTX-Software (libktx, `ktx` tool) | KTX2 read/write, Basis Universal, zstd supercompression | not stated on the page I opened; points to LICENSE and a LICENSES folder (believed Apache-2.0 for most files, **verify**; the archive doc says the same) | Apache-2.0 is compatible with GPL-3 (one-way); **verify** every file under LICENSES |
| zstd | chunk compression | "dual-licensed under BSD OR GPLv2" (README as summarised; GitHub's detector shows GPL-2.0) | the BSD option is compatible; **verify**; format is RFC 8878 |
| BLAKE3 | content hashes | CC0-1.0 or Apache-2.0 or Apache-2.0 with LLVM exception (repository files LICENSE_CC0, LICENSE_A2, LICENSE_A2LLVM) | compatible under CC0/Apache; **verify**; official C implementation exists, SIMD optional |
| lz4 | already in the tree (`compressedbsafile.cpp` includes `lz4frame.h`) | BSD-2 (memory) | already used; **verify** |
| zlib | already in the tree | zlib licence (memory) | already used |
| Basis Universal | transcodable textures | Apache-2.0 (memory) | **verify**; reached through KTX-Software |
| cgltf | glTF reading, already compiled via CreativeMotion (`components/creativemotion/anim/gltf/`) | MIT (memory); **not recorded in `docs/dependencies.md`** | **verify**, and record it |
| vsgXchange | image and model loaders (DDS, KTX, glTF) | MIT (`docs/dependencies.md`) | recorded |
| Vulkan Memory Allocator | GPU memory sub-allocation and budget | MIT (memory) | **verify**; VSG has its own allocator, check before adding |
| DirectStorage | Windows fast IO and GPU decompression | Microsoft SDK, licence **unknown**, Windows only | optional fast path only |
| xxHash, meshoptimizer's `clusterlod.h`, Draco | hashing, cluster LOD helper, geometry compression | not opened | **verify** |

## 5. Measurement plan on Morrowind (to be run by a person; no build was run for this note)

All of it uses the existing rig: `tools/benchmark.sh`, `[Rendering] gpu timing = true`, `OPENEDEN_FRAME_STATS_FILE`, and `[Rendering] device report = true` (note 0009 sections 1 and 4). New scenes go in `tools/render_scenes.txt` as `heavy_*` lines. Record machine, driver, commit and settings with every table, as note 0009 does. Take a second machine with a weaker GPU as soon as one exists: the 4070 hides GPU limits.

### 5.1 Baseline of what exists (before any change)

| Measure | How | Why |
| --- | --- | --- |
| Frame time median and p95, and wait time | `tools/benchmark.sh` on the existing four `heavy_*` scenes, plus new scenes in 5.2 | one table to compare every later stage with |
| Draw calls per frame and per pass | add a counter to the frame stats, or a debug capture; **not present today, verify** | decides how much merging and GPU culling can win |
| CPU time split: update, cull, record, submit | profiler scopes; the extension doc lists per-extension profiling, the engine's own scopes were not read, **verify** | note 0009 says CPU-bound, but not on what |
| Resident memory: process working set, VSG allocations, GPU device-local used | OS counters plus `VK_EXT_memory_budget` if the device offers it (**verify** in the device report) | needed for budgets in the design |
| Load times | time from start to first frame, and from "Load game" to control, with `distant terrain` and `object paging` off and on | cold and warm cache |
| Terrain batch build time | log time in `Builder::createBatch` for a 30000-unit view | the cost a cache would remove |
| Distant object chunk build time and count of draws per chunk | log in `DistantObjects` builds | same |

### 5.2 Scenes to add

1. `heavy_flight_fast`: fly at a fixed fast speed (for example 2000 units per second) in a straight line across Vvardenfell for 60 s with collision off, to stress streaming. The existing `walk` option is slow; this is a new Lua script in `tools/benchmark_data`.
2. `heavy_ground_walk`: walk (with collision on) across the bitter coast to Seyda Neen with groundcover on and a groundcover mod installed, to test grass; needs a groundcover content file (`[Groundcover] enabled = true`), none ships with the engine, **a mod must be chosen and its licence recorded; no game or mod assets in the repository.**
3. `heavy_view_sweep`: stand still and rotate 360 degrees at three heights (ground, 200 units, 3000 units).
4. `heavy_cell_border`: cross a cell border back and forth ten times at walking speed.
5. Each scene at view distances 7168 (default), 15000 and 30000 and at `object paging` on/off.

### 5.3 Pops: measure with frame sequences, not by eye

- Capture frames with `--auto-screenshot N --auto-quit M` on consecutive frames while the camera moves in small steps (root `CLAUDE.md` pitfalls say to capture frames this way; and a frame must be viewed before committing anything that can affect rendering).
- For each pair of consecutive frames compute a difference image; a "pop" is a connected region whose difference stands out from the background difference due to camera motion. Simpler and more robust: render the same camera pose twice with a LOD bias forced on and off and measure the per-pixel difference between the two LOD versions at each distance. That gives the **visible error of each LOD level**, which is also what the screen-space-error tree needs to calibrate. Report the 95th percentile and maximum difference by distance band.
- Also log every LOD change event (terrain chunk added or removed, distant chunk shown or hidden, groundcover chunk shown) with frame number and distance, so counts of events per second can be set beside the image differences.

### 5.4 Stalls

- Per-frame wall time over a route (5.2 items 1 and 4); count frames over 2 x median and over 25 ms and list the preceding events (cell loaded, chunk compiled, texture read). The log needs timestamps on cell load/unload and chunk build completion; **add them if missing, verify**.
- Disk: record the bytes read and the number of reads per second (an OS tool such as Windows Resource Monitor, or counters in the VFS), cold and warm.
- Compile cost: time of `CompileOp` on the main thread per chunk, because compiled uploads happen there (`paging.cpp`).

### 5.5 Grass

- Walk with density 1.0 and the default 6144-unit distance; log the count of instances drawn and draws per frame; photograph the cut-off line at 3 distances; measure instance and draw counts at 1500, 3000, 6144 units. Also test the case in the brief (grass gone within about 15 m): find which setting or mod could produce it; the engine default does not.

### 5.6 Memory and cache

- Memory after 5 minutes of flight at default and at 30000 view distance; resource-cache statistics (`components/resource/cachestats.*` exists and is the natural source).
- GPU memory use of the terrain composite maps (512 x 512 each by default) times the number of chunks.

### 5.7 New Vegas data census (later, when New Vegas data and an ESM4 reader pass exist)

A read-only tool counting, for a real install: exterior cells per worldspace, references per cell (median, p95, max), distinct models, texture sizes histogram, BSA versions and sizes, presence of LOD folders and files. Output as numbers only; no asset is copied into the repository. This fills the **unknown** rows of section 3.

## 6. Questions this note leaves open

1. Which one of "read shipped LOD" and "build our own" is wanted; the design doc recommends building our own, with reasons.
2. Where exactly the CPU time goes (5.1), before choosing between merging, GPU culling or both.
3. Whether a second, weaker GPU is available for the floor measurements.
4. Which groundcover mod to use for testing, with its licence.
5. How many units make a metre in the engine's own terms (for the "15 m" requirement).

## 7. Sources opened

- https://vivanewvegas.moddinglinked.com/lod.html (opened, read)
- https://dyndolod.info/Generation-Instructions (opened, read)
- https://dev.epicgames.com/documentation/en-us/unreal-engine/hierarchical-level-of-detail-in-unreal-engine (opened; only the summary quoted)
- https://github.com/zeux/meshoptimizer, https://github.com/KhronosGroup/KTX-Software, https://github.com/facebook/zstd, https://github.com/BLAKE3-team/BLAKE3 (opened; licence lines as stated above)
- https://en.uesp.net/wiki/Tes4Mod:BSA_File_Format (opened; versions 103 documented)
- https://en.uesp.net/wiki/Tes4Mod:Mod_File_Format/WRLD (opened; no cell size)
- Search results only (pages not read in full): Skyrim LOD file names (Beyond Skyrim wiki, UESP LOD settings, Nexus), Nanite slides, CDLOD forum threads, Rockstar GDC talks, GECK exterior cell pages.
- Not reachable: UESP Skyrim LOD data page (404), GECK Exterior Cells page (403), Unreal World Partition HLOD page (empty).
