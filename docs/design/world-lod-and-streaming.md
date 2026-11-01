# World LOD and Asset Streaming: Design

Status: **draft**, 2026-11-01. Written at toddhow's request ("start the research note and design doc first"). Nothing here is built, and nothing here is decided until toddhow answers the table in section 14. Companion to `docs/research/0018-world-lod-and-streaming.md` (facts and measurements to take), `docs/design/eden-archive-format.md` (the pack container) and `docs/design/eden-studio-plan.md` (the editor, which will show and debug tiles).

Like the archive doc, every fact that has not been checked against a source is marked **verify**. Words in this document: an **extension** is OpenEden's native code system; a **plugin** is an ESP or ESM data file (root `CLAUDE.md`, decision 0006). A **tile** is one square piece of the world with its own simplified data. A **bake** is generating data ahead of time instead of while playing.

## How to read this document

Sections 1 to 3 say what we want and the shape of the answer. Sections 4 to 9 are the machinery. Section 10 is grass. Section 11 is staging into task specs. Section 12 reviews the archive format draft. Section 13 is the mod organizer tangent. Section 14 lists the decisions that are toddhow's.

## 1. Goals

toddhow's words: seamless world, indiscernible LOD "like RDR2", full-map view distance, grass that does not vanish within about 15 m, better LOD assets, asset streaming. Made testable:

1. **The whole map is visible** from any point, at a quality that depends on the graphics tier (section 8), not on a hard distance limit. Today the default view distance is 7168 units, under one cell (`files/settings-default.cfg`, `[Camera] viewing distance`).
2. **Changes of detail are not noticed.** A person looking at a recorded frame sequence cannot point at the frame where a LOD changed (test method in research note 0018, 5.3). This is a goal to be measured, not a promise; "like RDR2" cannot be used as a spec because no primary source on RDR2's method was found (note 0018, 2.2).
3. **Grass does not disappear abruptly.** No instance of groundcover vanishes in a single frame inside a configurable full-density radius (starting value to be set from measurement, at least the 15 m toddhow named), and beyond it the density thins smoothly to zero over a band.
4. **Better LOD assets**: far objects are simplified from the real models, with materials and colours that match, instead of cut off (today) or hand-made.
5. **Streaming**: the game does not wait for disk on the main thread; memory use stays inside a budget; moving fast (flying, fast travel) degrades detail rather than stalling.
6. **Older GPUs still work**, through graphics options (section 8), not a hard floor.
7. **Unchanged when off**: with the new setting off, OpenEden plays Morrowind exactly as the fork point did, verified by comparison (root `CLAUDE.md`, "Default behaviour is Morrowind's"). The current live terrain paging and distant objects stay as the fallback path.
8. **Game-agnostic core**: the core defines the model and the baker's inputs; Fallout: New Vegas support, when it exists, is a compatibility pack (root `CLAUDE.md`, architecture rules). New Vegas support does not exist yet; no task has made its land drawable (`LandManager::create` returns empty land for ESM4 worldspaces, note 0018, 1.1).
9. **Deterministic and rebuildable**: baked data is derived data, keyed by source hash plus tool version, and can be deleted at any time (archive doc, section 11).

Non-goals for this design: new rendering effects (they are other specs), changing gameplay, saves (extensions do not touch saves, decision 0005, and the cache is not a save), the C ABI, hot reload.

## 2. What exists today, in one paragraph

Read from the code (note 0018, section 1): terrain is a quadtree of chunks chosen by distance only and swapped with no blend; distant objects are one unmerged draw each per cell and vanish at a single distance; grass is cut at a distance by a per-instance test in the vertex shader; cell loading uses blocking stream reads, one preload thread and time-based cache expiry; nothing is cached on disk; the frames on the test machine are CPU-bound. The design keeps what works (the quadtree shape, the worker-thread chunk builds, the cell preloader) and adds a baked hierarchy, transitions and a streaming core beside it.

## 3. The OpenEden world-LOD model

### 3.1 Plain-language idea

Cut the world into square tiles at several sizes. The smallest tile is one exterior cell. Four tiles make one tile of the next size up, and so on until one tile covers the whole map. Every tile stores a **cheaper picture of everything inside it**: a simplified terrain surface, a merged and simplified version of the objects, and billboards for things like trees. Each frame the engine walks down from the top and asks of each tile, "is this tile's picture good enough from here?" If yes, draw it. If no, ask its four children. "Good enough" is a measurement in screen pixels (below), not a distance, so a mountain can be drawn coarsely and a small statue finely at the same distance.

This is the shape of a **hierarchical LOD (HLOD)** tree. Unreal's HLOD merges actors into proxy meshes for long distances (note 0018, 2.2); DynDOLOD and xLODGen do a hand-run version for Skyrim and New Vegas (note 0018, 2.1). OpenEden builds it itself, from the data it already reads.

### 3.2 Tile tree

- **Key.** A tile is named by (worldspace, level, x, y), where level 0 is one cell and each level doubles the side. The key packs into one 64-bit number (worldspace id, level, and the two coordinates interleaved bit by bit, a *Morton code*, so that neighbours sit near each other in numeric order). The existing terrain quadtree already uses power-of-two squares in cell units (`Terrain::Bounds`), so it maps onto this one-to-one; its smallest chunk, 0.125 cell, stays a live-path detail and is not a baked level.
- **Cell size comes from the game.** Morrowind: 8192 units; ESM4 worldspaces: 4096 (`Constants::CellSizeInUnits`, `ESM4CellSizeInUnits`). A tile stores sizes in world units, not cells, so the core never assumes either.
- **Payload per tile** (each part optional, so a tile of open sea has only terrain):
  - *Terrain surface*: a height grid of fixed vertex count per tile (a starting value of 33 x 33 vertices, **to be measured**; Morrowind's native cell grid is 65 x 65, `landrecorddata.hpp`), a normal map, an albedo texture (the baked composite, replacing today's runtime `composite map`) and, where the source has them, packed roughness/AO/metal channels (task 0044's `_rmaos`).
  - *Object cluster*: one merged mesh (or a few, by material) of every static object in the tile, simplified, with a texture atlas, plus a list of the objects it replaced (needed for section 4.5).
  - *Impostors*: billboard sprites for trees and tall landmarks (section 3.5).
  - *Grass hint*: a coverage map the grass system reads (section 10).
- **Error value per tile.** The baker records, for each payload, the **geometric error**: the largest distance in world units between the tile's simplified surface and its children's more detailed one. This is what the selection uses.

### 3.3 Selecting tiles: screen-space error

For a tile at distance d from the camera, the error it would show on screen, in pixels, is

    pixels = geometricError * viewportHeightPixels / (2 * d * tan(fieldOfViewY / 2))

(the standard projection of a length at distance d; written from first principles, no external source). A tile is drawn if `pixels <= threshold`; otherwise its children are considered. Starting values, **to be measured against the frame sequences of note 0018, 5.3**: threshold 2 pixels at the High tier, 4 at Medium, 8 at Low. Using pixels makes the result correct for any resolution and field of view; the current rule (`isSufficientDetail` in `components/terrain/lod.hpp`) knows neither.

**Hysteresis.** A tile is split when `pixels > threshold` and merged back only when `pixels < 0.7 * threshold` (starting value), so a camera hovering at a boundary does not flicker. The present code has a 150-unit reuse distance for the same reason (`View::reuseDistance`).

The selection runs on the CPU for tile counts in the hundreds (a 30000-unit view over Morrowind is a few hundred tiles at most; **verify** by counting in the first spec). With GPU-driven culling (tier High) the same test runs in a compute shader over a flat array of tiles.

### 3.4 Terrain tile crack-free joins

Two neighbouring tiles at different levels would show a crack. The existing code stitches by dropping vertices on the finer chunk's edge (`getLodFlags` in `paging.cpp`). Keep that for the Classic and Low tiers. For Medium and above add **geomorphing** (CDLOD, Strugar; note 0018, 2.2): in the vertex shader each vertex slides smoothly to the position it would have in the coarser level as the tile approaches the distance where it will be swapped, so the swap itself changes nothing. The morph amount is a function of `pixels / threshold`, so it works with the same selection. Because it costs one extra height sample per vertex it needs vertex texture fetch, which every Vulkan GPU has (**verify** on the weakest target).

### 3.5 Objects

- **Where an object goes.** The baker classifies each placed object: *far-visible* (counts toward clusters; statics, large activators, doors), *small* (dropped from clusters beyond a size threshold, the same idea as `object paging min size`), *tree* (becomes an impostor at far levels), *dynamic* (never baked: lights, actors, containers that change).
- **Merging.** For every tile the baker merges the far-visible objects into one mesh per material group, transforms vertices to tile space, and runs a simplifier. The candidate library is meshoptimizer (MIT per its page; simplifier and meshlet builder, note 0018, section 4). **Open**: simplify per object first and merge after, or merge and then simplify; the first keeps silhouettes of small objects, the second removes more triangles (decision in section 14).
- **Textures.** Object textures go into an atlas per tile; atlas size and mip count are baker settings. Atlases are the large cost on disk (the New Vegas guide warns of over 2 GB of object textures with big mods, note 0018, 2.1).
- **Per-model LODs for the active grid.** Models near the camera also get a short LOD chain (full, half, quarter, billboard) made by the same simplifier, chosen by the same pixel error, and cross-faded (section 7). This replaces the all-or-nothing `DistanceLOD` in `distantobjects.cpp`.
- **Impostors.** A tree or tower is rendered from several angles into an atlas by the baker, using the engine's own renderer in a headless mode (the `--bake-gi` baker is a precedent for an engine baking mode, task 0028). The sprite is chosen by view angle and cross-faded between neighbouring angles. Details (grid of views, normal data for lighting) are a later spec. **verify** octahedral impostor technique before choosing.
- **Cluster/meshlet LOD (Nanite-like).** Not in the first generations. It needs compute culling and either mesh shaders or a software path, and its payoff over tile merging is mostly for dense near-field geometry. Recorded as a possible tier-Ultra feature (section 8) once the tile system exists.

### 3.6 Core versus compatibility pack

| Piece | Where | Reason |
| --- | --- | --- |
| Tile tree, keys, selection, transitions, streaming, budgets, tiers, cache keys | core | game-agnostic; rules of root `CLAUDE.md` |
| Baker: simplification, merging, atlasing, impostor rendering, tile writing | core | same |
| World source contract (proposed name `openeden.worldsource`): per worldspace gives cell size, height and normal data per cell, terrain texture layers, placements (model path, transform, flags), model and texture paths resolved through the VFS | defined by the core, **no implementation** | contracts carry no implementation or third-party types (root `CLAUDE.md`) |
| Importers: Morrowind land and references; Fallout: New Vegas land, references, GRAS, optional shipped LOD | compatibility packs, provide the contract | game rules live in packs |
| Morrowind during the transition | a core-side adapter reading `ESM::Land` as `TerrainStorage` does today, to be moved into the Morrowind pack as part of the existing P5 task "Morrowind-specific code moved out of the core" | Morrowind keeps working while the pack grows |

Extensions cannot create physics or touch saves (decisions 0003, 0005); an importer only **reads data and returns geometry**, which fits. How a pack's importer is called from the separate baker process (section 5) is open: the baker either loads the same extension libraries or the importers are compiled into a baker-mode of the engine. **verify** against the extension host (`components/extension`) before the spec.

### 3.7 Shipped LOD or our own? Recommendation: our own

Fallout: New Vegas and Skyrim ship or accept generated LOD files (note 0018, 2.1). Reading them is possible in principle, but:

- Their formats are only known from modder pages; no specification for Skyrim's `.btr`/`.bto` or for New Vegas's tree LOD data was found. Reading them means reverse engineering, which the archive doc and root `CLAUDE.md` forbid us to guess at.
- They are stale as soon as any mod changes terrain or placements (the guide says to regenerate each time); a per-load-order bake is the only correct result.
- They have a fixed four levels with no hierarchy guarantee we can use for crack-free joins, no pixel error values, and textures baked at fixed sizes; Morrowind has none at all, so we need the baker anyway.
- One implementation serves both games; New Vegas support becomes "importer only".

**Recommendation:** build our own from full-detail data for both games. Optionally, much later, read a game's shipped terrain LOD as a *preview* so that a first run has something before the first bake finishes (it is not needed: the live path is the preview, section 5.6). Open as decision D1.

## 4. The background baker and the cache

### 4.1 Plain-language idea

Making all those tiles takes a long time (minutes to hours, **unknown until measured**). The game must never wait for it. So: a **separate low-priority helper process** does the work while the player plays or while the launcher is open, writes its results to a cache folder, and the **next launch** of the game picks them up. If the cache is missing or incomplete, the game simply uses today's live system for the missing parts.

### 4.2 Process, not thread

- **Recommendation: a separate process**, the same executable started in a bake mode (as `--bake-gi` is a command line mode, task 0028), run at below-normal priority with a limited thread count (default: a quarter of the cores, at least one; setting). Reasons: it cannot stall the render thread through locks or allocator contention; a crash cannot take the game down; it can run while the game is closed (started by the launcher). Cost: it loads the ESM store and VFS a second time (memory; **measure**).
- It reads game and mod files **read-only**, never writes beside them, and writes only inside the cache folder.
- It does not touch the GPU for terrain and clusters. Impostor rendering needs a GPU context; it is a later stage and may run only when the game is closed (decision D6).

### 4.3 Work units and resume

- The plan is a list of **work units**, each one tile (or one atlas). Units are independent, so any number of threads give the same bytes (determinism, below).
- **Order:** coarse levels first (the whole map at low detail within minutes), then refinement toward where the player last was (the last cell is stored in the status file), then the rest. A partially finished bake is therefore already useful: the world is visible far away early.
- A finished unit is written as a file whose name contains its **cache key** (4.5), to a staging area, then moved into place with an atomic rename. A killed baker leaves only complete files and a plan; the next run recomputes the plan, sees which keys already exist, and does only the rest.
- The baker pauses when the game reports high frame times or when the machine is on battery (**verify** a portable way; Windows has an API, Linux varies), and it stops on request.

### 4.4 Determinism

No wall-clock time, no unseeded randomness, no iteration over unordered containers where order changes output, stable sort with explicit tie-breaks (root `CLAUDE.md`, Determinism). Thread count and scheduling must not change the bytes: each unit runs on one thread from sorted inputs; compression uses fixed parameters. Verified by a test that bakes a small area twice with 1 and 4 threads and compares file hashes. Floating-point differences between CPU models (for example fused multiply-add) can break byte equality; the baker restricts itself to code compiled with the same flags on one machine, and **cross-machine byte equality is not promised** (a reasoned limit, to be tested).

### 4.5 Cache keys

Per the archive doc, section 11: key = hash of (source content hashes + tool version + parameters).

- **Tile key** = BLAKE3 (the archive doc's choice; licence note in note 0018, section 4) of: the tile's key (worldspace, level, x, y); the content hashes of every input that affects it, **in load order** (the land record data for the cells, each placed object's record and its model and texture files, terrain textures); the baker's tool id and version; the quality tier's baking parameters (vertex count, error settings, atlas size). A tile at a coarse level lists its children's keys instead of repeating their inputs, so a change deep inside invalidates exactly its ancestors.
- Inputs are hashed when read. For files inside archives the pack's per-entry hash is used when it exists (the Eden format has them; BSA does not, so the baker hashes the data once and caches the hash by (archive path, size, modification time) in a small table; **verify** that is safe enough, or hash the bytes every time).
- The whole bake is listed by a **generation manifest**: a small file naming every tile key, a fingerprint of the load order, and the tool version.

### 4.6 What happens when the load order changes

1. At start the engine computes the fingerprint of the content list, data folders and archive list (cheap, from names and file sizes and times).
2. If it matches a complete generation, use it.
3. If not, the engine makes a **new plan** by comparing keys: tiles whose key already exists (from any earlier generation) are reused, others are marked missing. Reuse is by key, so changing one mod rebuilds only the tiles it touches (its placements and ancestors).
4. For missing tiles the game uses the live path (today's terrain paging and `DistantObjects`) until the baker delivers them at a later launch. **Stale tiles are never shown**: a tile whose key does not match is ignored, not displayed.
5. Old generations and unreferenced tile files are removed by the baker under a size limit (setting; default to be set from measured sizes).

### 4.7 Handing over at restart; safe updates while the game runs

- The running game **opens the manifest and tile files once, at start, read-only**, and the baker never replaces or edits an existing file: it only adds new files with new names (the key is in the name). At the end it writes a new manifest under a new name and finally updates a tiny `current` pointer file by atomic rename. The game reads `current` only when it starts. So nothing the running game uses ever changes, which also answers the Windows file-locking pitfall in the archive doc (section 16) without relying on how the game maps files.
- On Windows the game opens cache files with delete sharing so the baker may remove obsolete ones; on Linux deletion of an open file is already safe. The baker removes a file only if no `in-use` lock file from a live process lists it (**verify** the lock approach; a lock file left by a crash must expire).
- Hot-swapping a tile in a running game is explicitly **not** a goal (hot reload is "Not yet" in root `CLAUDE.md`).

### 4.8 Where the user sees it

- **Launcher** (`apps/launcher`, Qt): a "World LOD" page next to the Extensions page (task 0018) showing state (not built / building 42% / up to date / out of date because the load order changed), estimated time, cache size, and buttons Build now, Pause, Delete cache. Strings go through the Qt translation process (`CI/check_qt_translations.sh`).
- **In game**: a one-line status in the options window and a log line; no popups in play. New game UI strings use OpenMW's YAML localisation (root `CLAUDE.md`).
- **Communication** between baker and UIs is a status file the baker rewrites atomically every few seconds (progress, units done, errors, last cell). No sockets.
- **Settings** (proposed, a new `[WorldLod]` section, all off by default): `enabled`, `background bake`, `bake threads`, `cache size limit`, `cache folder`, `quality tier` (`auto` by default once enabled).

## 5. The streaming core

### 5.1 Plain-language idea

Streaming means loading the pieces you will need just before you need them, throwing away pieces you no longer need, and never letting one slow read freeze the screen. Think of a conveyor belt: requests go in at one end ordered by urgency; reads and unpacking happen on helper threads; a small amount of finished data is handed to the graphics card each frame.

### 5.2 Requests and priorities

A request names an entry (by key or path), what it is for and a priority. Priority classes, highest first: **blocker** (the current frame cannot draw correctly without it, for example the root tiles and the coarse ancestor of a tile that is about to be refined), **near** (inside the active grid), **visible** (selected by the tile test for the current camera), **prefetch** (selected for the predicted camera), **background** (the baker's own reads; lowest). Within a class, order by pixel error (bigger on screen first), then by distance. Requests that are no longer needed are cancelled before they start (the camera moved on); a request in flight is allowed to finish and its result goes to the cache.

### 5.3 IO

- Interface: `readRange(file, offset, size, destination)` completing asynchronously (a callback or a future). The present VFS gives only blocking `std::istream` (`VFS::File::open`, `components/vfs/file.hpp`; BSA reads are constrained file streams, `bsafile.cpp`), so streaming needs a **second, range-based interface** added beside it, not a replacement.
- **Baseline implementation: a pool of IO threads (default 2, setting) doing positional reads** (`pread`, or `ReadFile` with an offset), aligned to 4 KiB where the file allows (the Eden archive aligns blocks, archive doc section 6). Portable, no special API. No memory mapping in the first version, so files can be replaced at will.
- **Optional fast paths**, behind the same interface, chosen only after measurement: overlapped IO queues, Linux `io_uring`, Windows DirectStorage. DirectStorage is Windows-only (Microsoft SDK; its licence and redistribution terms are **unknown/verify**), so it can never be required (D8 in section 14).
- Legacy files (BSA, loose) go through the same queue with the stream interface wrapped; their speed is what it is.

### 5.4 Decompression and memory

- zstd (BSD option of its dual licence; note 0018, section 4) in independent chunks of 64 to 256 KiB, per the archive draft, decoded on worker threads (default: cores minus 2, at least 1).
- Buffers come from **preallocated pools**; no allocation in per-frame code (root `CLAUDE.md`, Hot paths). A fixed number of in-flight chunk buffers bounds the memory the pipeline itself uses.
- Compressed chunks may be kept in a RAM cache (LRU) so a recently evicted tile reloads without disk.

### 5.5 Upload to the GPU

Finished data moves to the GPU through a **staging ring buffer** (a reusable block of CPU-visible memory) and copy commands. If the device has a dedicated **transfer queue** family, copies use it, otherwise they share the graphics queue with a **per-frame byte budget** (starting value 8 MiB per frame, a setting, **to be measured**). Whether the device report lists the queue families is **verify** (note 0018, 1.6). How this meets VSG's existing compile and transfer mechanism (`vsgUtil::CompileOp`, used by `Paging::updateView`, which compiles on the main thread) is an open research item for the spec; it must be read from `components/vsgutil` and VSG's source, not assumed. Tile geometry and tile textures are **buffers and images allocated from pools** sized by the tile payload, to avoid per-tile allocation.

### 5.6 Interaction with what exists

- **Fallback path stays.** While a tile's baked data is not resident, the live paging and `DistantObjects` draw that area (one source for any area, never both at once; the switch is a cross-faded swap, section 7). That is why the first run, an incomplete bake and a failed read all degrade to today's look.
- **Cell loader.** `Scene::loadCell` and `CellPreloader` keep working. Their model and texture reads go through the same IO queue with class **near** (later, **blocker** when the cell is under the player) instead of blocking streams. The existing preloader's prediction (`[Cells] prediction time`, `preloadExteriorGrid`) is the model for prefetch.
- **Resource caches.** With streaming on, the time-based expiry of `Resource::ObjectCache` (note 0018, 1.5) is complemented by byte budgets (5.7). Without it, unchanged.
- **Archive format.** Tiles and textures are addressed by key; the format needs what section 12.3 lists.

### 5.7 Budgets and eviction

Three budgets: compressed RAM cache, decoded CPU data in flight, GPU memory for streamed geometry and textures. GPU budget = a fraction of device-local memory (starting value 60%, a setting) or the figure from `VK_EXT_memory_budget` if the device offers it (**verify**; add to the device report). Eviction is least-recently-used, but never evicts **pinned** data: the top levels of the tile tree (starting value: levels that together cover the map at the coarsest tile, a few MiB) and the tail mips of every loaded texture. When over budget: first raise the minimum texture mip globally (lower quality), then raise the tile error threshold (coarser tiles), and only then fail requests. Hysteresis on both so quality does not oscillate.

### 5.8 Texture mip streaming

Each streamed texture keeps its **tail** (the smallest mips, up to a size such as 64 x 64, **to be set**) always resident, and higher mips are separate chunks fetched when the on-screen size demands them. The wanted mip comes from the texture's projected size (texels per pixel from UV density and distance). Raising or lowering the resident mips in Vulkan has one hard problem: without sparse images the allocation is for the full chain, so memory is not saved unless the image is **recreated with fewer mips and the existing ones copied**. Options: (a) pool images by "top mip size" class and move textures between classes with a GPU copy; (b) use sparse residency where the device offers it (optional feature, **verify** in the device report, not on every GPU); (c) per-tile baked textures only (tile albedo atlases are already sized for their distance, so they need streaming by tile, not by mip). **Recommendation:** do (c) first, since tile textures are ours; add (a) for the active grid's textures later; (b) only as a tier-Ultra option. Mip streaming for arbitrary mod textures is the highest-risk part and is the last stage (section 11).

### 5.9 Prefetch from velocity

Run the tile selection twice: for the camera now and for a predicted camera, `position + velocity * T` with `T` growing with speed from the cell preloader's 1 s to a cap (starting values: 1 s walking, up to 5 s when flying or fast-travelling; **to be measured**), clamped to the world. Tiles needed only by the predicted pose enter the queue as **prefetch**. Fast travel and teleport destinations are known in advance and are requested as **near** (the engine already preloads them, `preloadFastTravelDestinations`).

## 6. Threads and the frame

The main thread selects tiles, issues requests and records draws; IO threads read; worker threads decode; the baker is another process. Rules (root `CLAUDE.md`, Threading): say in each spec which thread each callback runs on; GPU resources are created and destroyed only through the render thread's own queue (no raw pointers across threads, IDs/handles only). Frame-time impact is measured with the existing GPU timing and benchmark rig before and after each stage, with draw calls counted (note 0018, 5.1).

## 7. Hiding the transitions

Four layers, from strongest to weakest.

1. **Pick the right level before it is noticeable** (pixel error, section 3.3). The threshold is the main quality knob.
2. **Make the swap invisible**:
   - *Terrain*: geomorphing (3.4).
   - *Objects and clusters*: **dithered cross-fade**. While a tile is changing level, both versions are drawn for a short time, each discarding pixels by a screen-space noise pattern chosen so the two patterns are complementary, so the sum is a solid surface and one fades into the other. No sorting and no alpha blending (technique from memory; **verify** against Unreal's and Unity's descriptions, note 0018, 2.2). It costs overdraw only while fading. With temporal anti-aliasing, if it is ever added, the noise pattern gets blended; the present renderer's anti-aliasing is MSAA (`multisample.hpp`, **verify**), which also softens it.
   - *Fade timing*: time-based (starting value 0.4 s) for objects; terrain morph is distance-based. A fade in progress is never restarted by the hysteresis band; it runs to the end, then the state may flip back.
   - *Active-grid hand-off*: when a cell becomes active the real objects fade in while the cell's cluster fades out (same cross-fade), so the "different look" pop described in note 0018, 1.7 is covered.
3. **Make both versions look alike**:
   - Clusters and tile textures store **unlit albedo, normal and material**, and are shaded at runtime by the same lighting function as near objects, so time of day, weather and the lights of the lighting specs apply to them. Baking lit colours (the old way) would break at dusk.
   - Far terrain uses the same wetness and snow cover inputs as near terrain (tasks 0037, 0039) from the tile's normals and height; the exact interface is for those specs.
   - Ambient and global illumination: exteriors use the flat ambient plus the sky-visibility field of task 0036; far tiles sample the same field at coarse resolution (what 0036 actually delivered is **verify**; interiors' probes of task 0028 do not apply to the open world).
   - Shadows: far tiles are outside the sun's cascades; they carry a baked ambient-occlusion term in vertex colour/texture instead (baker option), and receive cloud shadows from the cloud spec (task 0038) because that is a function of position.
4. **Hide what is left in the air**: distance haze from the atmosphere spec (task 0034) and volumetric fog (task 0041). The far tiles and the near scene must use **one** aerial perspective function, evaluated per pixel with true distance, so a tile boundary has no colour step. The atmosphere/fog tasks decide that function; this design requires only that the far path calls it and does not bring its own fog (the current `[Fog] use distant fog` settings are the inherited alternative).

Measure with the frame-difference method of note 0018, 5.3 and the "LOD forced on/off" comparison; the aim is a difference below what an observer notices, which toddhow judges.

## 8. Graphics options and fallbacks for older GPUs

### 8.1 Idea

One setting, `quality tier`, plus per-feature overrides. The device report (`components/render/devicereport.*`, written by `[Rendering] device report = true`) says what the GPU supports; `auto` picks the highest tier whose **required** features are all present and whose memory fits. A missing optional feature turns off or replaces only that feature, not the whole tier. No tier is a floor: tier 0 is today's renderer with the system off.

### 8.2 Tiers (starting proposal; numbers are **to be measured**)

| Tier | Name | What it does | Hardware features needed | What is off |
| --- | --- | --- | --- | --- |
| 0 | Classic | Current live terrain paging and distant objects, no baked data | none beyond the engine's present requirements | everything in this document |
| 1 | Low | Baked terrain tiles and merged object tiles chosen by pixel error with a coarse threshold (8 px); stitching, no geomorph; swap hidden by haze only; grass thinning without a mid band; no impostors; texture streaming of tile textures only; small budgets | the engine's present requirements; the Vulkan 1.1 floor if note 0009's proposal is adopted | cross-fade, geomorph, impostors, mip streaming for mods, GPU culling |
| 2 | Medium | + dithered cross-fade, geomorphing, impostors, per-model LOD chains in the active grid, grass mid band, 4 px threshold | vertex texture fetch (core); `alphaToCoverage` or discard-based dither (core) | GPU culling, cluster LOD |
| 3 | High | + GPU-driven tile and instance culling with indirect draws, 2 px threshold, larger budgets, mip streaming for active-grid textures | compute shaders; indirect draw with a draw count (Vulkan 1.2 `drawIndirectCount` or the KHR extension; the device report records "draw indirect count" present on the test machine, note 0009); multi-draw indirect (core feature, **verify**); 16-bit storage helps (optional) | cluster LOD, sparse textures |
| 4 | Ultra | + cluster/meshlet LOD for dense geometry, sparse-image texture residency where offered, optional mesh shaders, longest prefetch | `VK_EXT_mesh_shader` (optional, **verify** present on target), `sparseResidencyImage2D` (optional), `VK_EXT_memory_budget` | nothing |

### 8.3 Per-feature fallbacks

| Feature | If the GPU lacks it | What happens |
| --- | --- | --- |
| Indirect draw with count | not present | CPU culling and per-tile draws (tier 2 behaviour), higher CPU time |
| Compute-shader culling | compute weak or absent | CPU culling |
| Mesh shaders | absent | classic vertex path; cluster LOD off or CPU-selected |
| Sparse residency | absent | tile textures streamed; others whole |
| Memory budget extension | absent | budget = fraction of reported heap size |
| Dedicated transfer queue | absent | per-frame byte-budgeted copies on the graphics queue |
| Dither cross-fade | fill-rate limited | shorter fade, then plain swap |
| Impostors | fill-rate limited | cluster stays to a larger distance, then fade to haze |
| Low VRAM | budget small | higher tile error threshold, lower texture mip floor, smaller grass radius |
| Old CPU, few cores | cores under 4 | one IO thread, one decode thread, baker threads = 1 |

The device report must be extended where the table needs a fact it does not yet record (queue families, memory budget extension, sparse residency, mesh shader, `multiDrawIndirect`): a small task of its own (S0 in section 11).

## 9. Determinism, security, and the off switch

- All baked and cached data is derived; deleting the cache is always safe; the game never crashes on a missing or damaged cache (it ignores the damaged tile and uses the live path). Every tile is hashed; a mismatch is a miss.
- Cache files are **untrusted input** to the game: bounds-check every size and offset, cap decompressed sizes, fuzz the reader (root `CLAUDE.md`, full-loop rule; archive doc section 16).
- With `[WorldLod] enabled = false`, no code path of this system runs, no process starts, no file is read; the game matches the fork point. Compared by the benchmark rig's frames and the integration tests (root `CLAUDE.md`, "Off means unchanged").

## 10. Grass

### 10.1 What is wrong now

Instances are kept or dropped by position in a list (`GroundcoverDensity`), and each instance is hard-cut at one distance in the vertex shader (`files/shaders/default/main.vert` line 144). Both produce visible edges (note 0018, 1.3). Groundcover also needs a content file with placements, and it is off by default.

### 10.2 Scheme

Three bands around the camera, with the radii as settings (tier-dependent starting values to be measured; the named requirement is full density to at least about 15 m, the engine-unit value of which is **verify**):

1. **Near band**: every blade, full geometry, wind and stomping as today.
2. **Mid band**: **thinned by a stable per-instance random number**. Each instance gets a fixed hash of its own position/id in [0,1). The instance is drawn only if `hash < density(d)`, where `density(d)` falls smoothly from 1 at the end of the near band to 0 at the end of the mid band. Because the number never changes, the same blades stay and others disappear one at a time as you move, with no pattern (the old "every Nth" scheme shows a pattern). The surviving blades are scaled up slightly (a coverage compensation, starting value to be tuned) so the ground does not look bald, and each blade's height fades to zero over the last tenth of its band so removal is not a pop.
3. **Far band**: no geometry. The terrain tile's albedo carries a grass tint and roughness variation (baked from the grass coverage map of 3.2), so the ground changes character gradually into the distance tiles.

### 10.3 Data

- Where placements exist (Morrowind groundcover mods) the instance list is as today, built per cell on a worker thread (`Groundcover::ChunkBuild`), only now also stored in the cache for reuse.
- For Fallout: New Vegas the ESM4 `GRAS` records exist in the parser (`components/esm4/loadgras.cpp`); how the game scatters grass from them (by landscape texture, density and slope) is **unknown/verify** and is a New Vegas pack task. The core defines only an interface: "a procedural scatter function from the coverage map, the tile and a seed", deterministic.
- Instances live in per-patch GPU buffers; tier High culls instances in a compute pass and issues one indirect draw per patch; lower tiers cull per patch on the CPU and per instance in the vertex shader as now.

### 10.4 Limits to measure

Instances per frame and draws per frame at the three radii (note 0018, 5.5); the vertex cost of the thinning test is negligible next to the instance count.

## 11. Staging into specs

Each stage becomes a task spec in `docs/tasks/` (with the template of root `CLAUDE.md`) after toddhow answers section 14. All are full-loop (renderer, loader, untrusted input). Numbers 0045 onward are free at this commit (the latest spec is 0044); they are not reserved here.

| Stage | Content | Depends on | Notes |
| --- | --- | --- | --- |
| S0 Measure | the scenes and counters of note 0018, section 5; extend the device report (queue families, memory budget, sparse, mesh shader, `multiDrawIndirect`); a second, weaker GPU run | task 0023 (timing, report) | no engine behaviour change |
| S1 Tile model | tile keys, selection by pixel error, one baked terrain level for Morrowind, a simple internal cache file layout, tile viewer in Eden Studio's mesh viewer (task 0043) for debugging | S0; the archive doc's E2/E3 are not required yet | proves the model; tier Low terrain |
| S2 Cache and keys | content-hash cache keys, generation manifest, `current` pointer, determinism test; move onto the Eden container when it exists (archive doc E2 to E5) | S1; archive doc E3, E5 | |
| S3 Terrain transitions | geomorphing, composite map bake replaces runtime composite, tile textures | S1; task 0034 for the haze function | |
| S4 Object clusters | classification, merge, simplify, atlas; active-grid hand-off cross-fade; blacklist/disable overrides fall back to the live chunk for that cell | S1, S2; meshoptimizer licence recorded | needs `DistantObjects` interplay read in detail |
| S5 Dither cross-fade and LOD chains | per-model LOD chains, dithered cross-fade, hysteresis | S4 | |
| S6 Streaming core | range-IO interface, IO and decode pools, priorities, staging ring, budgets, eviction, prefetch | S1; the VFS range interface; archive E3 for chunked reads | first with tile data only |
| S7 Grass | thinning by stable hash, bands, far tint, instance buffers | S1 for the far band; independent of S6 | can start early |
| S8 Background baker and UIs | bake process, work units, resume, status file, launcher World LOD page, in-game status | S2, S4; launcher conventions (task 0018) | |
| S9 GPU-driven culling | compute culling of tiles and instances, indirect draws | S1, S4; device report facts; GPU timing | tier High |
| S10 Impostors | headless render of billboard atlases, view selection, cross-fade | S5, S8 | |
| S11 Texture mip streaming | tail/chunk layout use, pools or sparse | S6; archive E4 | highest risk; last |
| S12 New Vegas world source | ESM4 land drawn (a missing task today: `LandManager::create` returns empty land for ESM4), `openeden.worldsource` provider, GRAS scatter, optional shipped-LOD preview | S1 to S4; the compatibility-pack plan | needs a New Vegas data census (note 0018, 5.7) |
| S13 Cluster LOD | optional | S9 | tier Ultra; may never be built |

Where this touches existing specs: 0034 atmosphere and 0041 fog (the one haze function, 7), 0028 and 0036 (ambient on far tiles), 0037 and 0039 (wetness and snow inputs), 0038 (cloud shadows), 0044 (`_rmaos` channels in tile textures), 0043 (tile viewer), 0023 (timing and report), 0018 (launcher conventions), 0022 (CI gates for new tests).

## 12. Review of the Eden Archive Format draft

The draft is `docs/design/eden-archive-format.md` (committed at `e8530fe6bb`, "written by another agent, placed by toddhow"). I did not edit it; proposed edits are listed in 12.5 for toddhow.

### 12.1 What is sound

- The central idea, a small custom container (index, chunk table, aligned data blocks) that owns exactly the layout streaming needs, is reasonable, and the reasons against ZIP, tar and squashfs for streaming are plausible. The BSA/BA2 details were not independently checked here beyond what the repo's readers show.
- Independent chunks with a **per-chunk codec id**, 4 KiB-aligned data and a 64-bit size/offset design, zstd and BLAKE3 as existing components rather than inventions, and the "do not invent a codec or hash" guardrail are good (zstd and BLAKE3 licences: note 0018, section 4).
- **Derived data is rebuildable and keyed by source hash plus tool version** (section 11 of the draft) is exactly the cache model this design uses (4.5).
- Treating every pack as untrusted input, with limits, fuzzing, a conformance corpus and canonical deterministic output, matches root `CLAUDE.md`.
- Keeping priority and conflicts in the VFS rather than the container is right.
- Mip-aware KTX2 layout with tail mips contiguous suits 5.8, subject to its own **verify** about KTX2's level ordering. (From memory: KTX2 stores the smallest mip first in the file, which would suit a "tail first" read; **verify** against the KTX2 specification before relying on it.)
- Its milestone gate, "proven against the streaming prototype before freezing layouts" (E4), is the right way to avoid freezing early; this design gives it the prototype (S6).

### 12.2 What conflicts with this repository's rules or terms

| Draft says | In this repository | Proposed fix |
| --- | --- | --- |
| "plugin (record) files", "Data plugin ... (the plugin format)", "the manifest declares which packs and scripts belong to a mod", "the plugin format" (sections 2, 4, 15) | `plugin` means an ESP/ESM data file or a Qt/OSG plugin; `extension` is the native code system (root `CLAUDE.md`, decision 0006). Here the draft's use of "plugin format" appears to mean a **new record file format** for OpenEden; no such format or manifest exists in this repo. The only manifest is `openeden.extension` v1 for extensions (task 0002) | Say "record file (ESP/ESM today)". Write "mod manifest" only as a **proposal** with a pointer to where it would be decided; do not call it "the plugin format". |
| Companion to "the Body System Design Guide, and the UI System Design Guide" | `docs/design/` holds `eden-archive-format.md`, `eden-studio-plan.md`, `extension-implementation-guide.md`, `inertial-locomotion.md`, `openeden-extension-system.md`, `working-extension-system.md`; a search of `docs/` and `CLAUDE.md` found no Body System or UI System guide, and found the phrase "body system" only in the archive doc | Remove the two guides from the header or mark them "not in this repository"; the "body system's region maps and soft-tissue profiles" (section 9) and "VRAM...body system" lines refer to a design that is not here. The root guide plans AdvancedBodies as a core system with design in CreativeMotion's repo (`docs/design/advancedbodies-handoff.md` there), **verify** whether that is meant. |
| "Meshes and animation use glTF 2.0 (animation already implemented)" (section 2) | True only in part. CreativeMotion's `components/creativemotion/anim/gltf` reads glTF animation (`opas::anim::readGltf`, cgltf); a unit test feeds it a `.glb` produced from a Morrowind `.kf` by the compatibility pack (`apps/components_tests/extension/test_compat_morrowind.cpp`, around lines 311 to 318). I found no engine code that loads **glTF meshes** for rendering; `docs/dependencies.md` lists vsgXchange as providing glTF loaders, but whether the engine calls them is unverified. cgltf itself is not in `docs/dependencies.md` | Reword to "glTF animation clips can be read by CreativeMotion's reader; mesh loading from glTF is not implemented", and record cgltf's licence in `docs/dependencies.md` (guardrail). |
| "Shaders use Slang"; "compile at runtime only for mod shaders or development mode" (sections 2, 10) | Decision 0009 (accepted): new shaders are Slang compiled to SPIR-V at **build time** by a pinned `slangc`; "the game loads the `.spv` files... it never runs Slang"; vsgopenmw's GLSL stays on runtime glslang until ported; glslang is removed only when nothing is compiled at run time, or the decision is revisited if mod shaders need Slang in the game | Reference decision 0009; say that runtime Slang is **not** adopted and is a separate decision. |
| "Mods may ship shaders" as an open question and "shader blobs as typed pack entries" (sections 10, 17.7) | `.omwfx` post-processing shaders exist for mods today, kept via Slang's GLSL input (root `CLAUDE.md` 2026-10-29 entry); no decision about packs holding SPIR-V | Keep as an open decision, but note the existing mod shader mechanism. |
| Table "Scripts: Lua scripts, declared in the manifest with their permissions" (section 4) | Lua APIs come only from the core; extensions cannot register Lua tables (decision 0003); mod scripts are OpenMW's `.omwscripts` mechanism | Remove the permissions idea or mark it a separate future design. |
| "Derived data lives in a separate cache, not inside packs" (decision table), and "Authors may optionally ship pre-baked derived data in a pack" (section 11) | The two statements disagree with each other; this design needs derived packs **written beside the user's mod packs** by the baker (12.4) | Resolve as in 12.4: derived data is stored in the cache folder, **using the same container**, never inside an author's pack. |
| Milestones "E1" to "E9" | Root `CLAUDE.md` already uses `E1` to `E6` for Eden Studio phases and `M7`-style names for others | Rename (for example `A1` to `A9`, "archive milestones"). |
| "Numbered decision records under `docs/decisions/`" | Latest record is 0009 | The decisions of 12.4 become 0010 onward, after toddhow approves. |
| `MSFT_lod` in the glTF extension table | whether it is ratified or in use is unverified; our LOD data is a different, hierarchical structure | Mark **verify**; do not depend on it. |
| Runtime "load `.glb`" and "convert to an engine-native cache" | consistent with this design, but the NIF path is the only mesh path today | Fine; D6 of the archive doc resolved in 12.4. |
| "Listing the licence of KTX-Software as believed Apache-2.0" | not confirmed from the project page (note 0018, section 4) | Keep **verify** until `docs/dependencies.md` records it. |

Guardrails to carry over: no game assets in the repository or test corpora (reference assets by hash outside the repo), GPL-3.0-or-later for all code, and a dependency's licence and pinned version recorded in `docs/dependencies.md` **before** it is added (root `CLAUDE.md`). The container reader and writer fall under the full-loop rule (untrusted input) and should be added to that list in the root guide.

### 12.3 What the streaming system needs that the draft lacks or leaves open

1. **Keyed entries.** The index is by normalized path hash plus a string table. Tiles are numerous (a hundred-by-hundred-cell map has about 13,000 tiles over all levels, an estimate) and are never looked up by name. Add a second index form: entries addressed by a **64-bit key** (type tag, then the Morton-coded tile key of 3.2), sorted, binary-searched or perfect-hashed, with no strings. Entries of a tile tree should be stored in **Morton order** so spatial neighbours are neighbours on disk.
2. **Cluster and HLOD tile entry types**, as the draft's section 9 and milestone E7 foresee: terrain tile, object cluster, impostor atlas, grass coverage. Typed layouts must be defined *from* this design's payloads (3.2), not before the prototype (the draft's own "do not freeze early").
3. **Mip tail layout** stated precisely: which mips form the always-resident tail (all mips up to a stated size), stored contiguously first; one chunk per larger mip; and the KTX2 level offsets kept valid if the payload is still a standard KTX2 file.
4. **Verified streaming**: the draft hashes whole entries and says "lazily for large entries". For streaming each **chunk** must be verifiable on its own. BLAKE3's tree structure was designed for verified streaming (its page, note 0018, section 4); either use it or store a per-chunk hash.
5. **Content-hash cache keys**: entries already carry content hashes; add for derived entries a **provenance record**: the digest of the source hashes, tool id and version, and parameters, so a loader can decide validity without opening the sources.
6. **Priority and residency hints** per entry: `pinned` (never evict), `prefetch-with` (co-request a group), uncompressed size in memory, and a coarse-first ordering flag.
7. **Derived packs beside the mod's packs**: a pack flag `derived` plus a header field naming the generation and the digest of the packs it was built from; the loader ignores a derived pack whose source digest does not match the current inputs.
8. **A safe update story while the game runs**: packs immutable and content-named; a generation switched by an atomic pointer file read at launch; no memory mapping of files that mod managers may replace; delete sharing on Windows (4.7). The draft's section 16 flags locking but "decide the mapping lifetime and test it" is the only answer; this is a concrete one.
9. **A range-read API** for the reader (thread-safe, no shared mutable state, positional reads) and a VFS interface beside `istream`.
10. **Limits for tile entries** (maximum counts, sizes) in the conformance corpus.
11. **A statement of how a pack is built from many threads deterministically** (fixed zstd level and parameters; whether zstd output is byte-identical across versions and threads is **verify**; the format should store the compressor version but make validity depend on content hashes, not compressed bytes).
12. **Texture payload choice for legacy content**: Fallout and Morrowind textures are DDS in block-compressed formats; wrapping the existing blocks in KTX2 without re-encoding is lossless and avoids a quality loss (the DDS reader already exists, `readimage.cpp`).

### 12.4 Recommended resolution of the draft's eight open decisions (its section 17)

| # | Question | Recommendation |
| --- | --- | --- |
| 1 | Short name and extension | Keep "Eden Archive Format" as the spec name; do the collision check the draft proposes (file-extension databases, a code search) **before** choosing; pick a distinctive extension, for example `.edenpak` (provisional), and magic bytes of at least 6 characters plus a version byte. Do not use `.eaf` unless the check is clean (the draft's own doubt, **verify**). |
| 2 | Packer behaviour | Two modes. **Store** mode (default for the user-facing repack of existing mods): entries stored as provided, only chunked and hashed, no re-encoding. **Optimise** mode (used only to write derived packs): may recompress, reorder, convert. Both record the source hash. Original files are never changed or deleted (13). |
| 3 | DirectStorage and GPU decompression | Plain, portable IO as the baseline (5.3). DirectStorage is Windows-only (Microsoft SDK, terms **unknown/verify**) so it can only ever be an optional fast path behind the range-read interface, added after measurement. Do not choose a GPU codec now; per-chunk codec ids leave the door open. |
| 4 | Texture codec default | Preserve the source block format (BC1, BC3, BC5, BC7) inside KTX2 with no re-encode; use BC7 only for textures we author (baked tiles). Basis Universal later, per-asset, for portability. |
| 5 | Paths or stable IDs | Paths for author-facing references, plus the content hash as the cache and dedupe key. Add stable IDs only if a concrete need appears. Tile addressing is separate (keyed index, 12.3.1). |
| 6 | Runtime mesh loading | Always convert to an engine-native cached form keyed by source hash (the draft's leaning); the tile baker produces that form. |
| 7 | Mod shaders | Defer. For now material parameters and approved modules only; the existing `.omwfx` mechanism stays; SPIR-V in packs is not needed by this design. Consistent with decision 0009. |
| 8 | Permissive licence | Apache-2.0 for the reference library (patent grant; one-way compatible with GPL-3, **verify**), and a documentation licence such as CC BY 4.0 for the specification text (**verify**). MIT is the simpler alternative. Needs toddhow's choice. |

### 12.5 Proposed edits to the archive doc (not made)

1. Replace "plugin" with "record file" and delete "the plugin format"; drop the Body System and UI System companions or mark them absent.
2. Correct the glTF animation sentence as in 12.2 and add cgltf to the dependency list.
3. Point the Slang section at decision 0009 and say runtime Slang is not adopted.
4. Rename milestones E1 to E9 (collision with Eden Studio's E1 to E6).
5. Resolve the "derived data not in packs" contradiction as in 12.4 row 2 and 12.3.7.
6. Add sections for keyed entries, tile entry types, per-chunk verification, derived packs and the update story (12.3).
7. Add an entry under section 19 for the streaming prototype as the gate for E4, naming S6 of this document.

## 13. Built-in mod organizer (launcher)

A **tangent**, design only; a spec comes later.

### 13.1 Purpose

Today a user manages mods by editing `openmw.cfg` lines (`data=`, `content=`, `groundcover=`, `fallback-archive=`, and `extension=` lines from task 0018) with the launcher's Data Files page (`apps/launcher/datafilespage.*`, a content selector) and Extensions page (`extensionspage.*`). The organizer extends the launcher so that a user can keep **profiles** of mod setups, see **what overrides what**, and **repack** mods into the streaming archive for faster loading, **without ever changing the mods' own files** (the idea resembles Mod Organizer 2's virtual file system approach; I did not research MO2's implementation, **verify** before borrowing any behaviour).

### 13.2 What it must do

- **Profiles.** A profile is a named, saved set of: ordered data folders, ordered content files (ESP/ESM), archives, groundcover list, extension list and the World LOD settings. Stored as JSON (canonical writer, byte-for-byte round trip, the project's rule for new formats) in the user's config area. Switching a profile rewrites the launcher-managed lines of `openmw.cfg` (with a backup first) and nothing else.
- **Load order.** Reorder content files and data folders; show the engine's rule that later wins. Drag and drop; sorting by master dependency uses the same reading of ESM headers the content selector already does.
- **Enable and disable** a mod (a data folder with its content) without deleting it.
- **Conflict view.** For two mods that provide the same virtual path, show who wins, and whether the files are byte-identical (same hash, no real conflict; archive doc section 12). Built by scanning each mod's loose files and archive indexes, never by loading the game. Record-level conflicts (two plugins editing one record) are out of scope for the first versions.
- **Repack into the Eden archive** (once the packer exists, archive doc E3): choose a mod; the packer writes one or more packs into a **managed folder inside OpenEden's own user data** (for example `.../openeden/repacked/<mod>-<hash>/`), verifies them by reading back every entry and comparing hashes, and only then offers "Use repacked". Enabling swaps the mod's entries in the profile for the repacked ones; the source folder stays in the profile as the original.
- **Restore originals.** One click removes the swap and, if the user wants, deletes the managed repack.
- **Safety for the user's game data.** Dry-run listing first; free-space check; writes to a temporary name and atomic rename; an undo log of what the launcher did; refuses to run while the game is running; backs up `openmw.cfg` before every change. It never writes inside a mod folder or the game's `Data Files`.
- **No network dependence.** Nothing downloads, checks for updates or talks to a mod site. (houseCARL-style Nexus lookups exist as a separate tool elsewhere and are not part of this.)
- **Integrates with the World LOD page** (4.8): a profile change makes the bake out of date and the launcher can start it.

### 13.3 How it fits `apps/launcher`

A new "Mods" page (or tab group) beside Data Files and Extensions in `maindialog.*`; a non-GUI library (profiles, conflict scan, repack driver) in `components/` or `apps/launcher/`'s utils so it can be unit-tested and reused by Eden Studio; the packer is the archive tool run as a child process, so a bad pack cannot crash the launcher. Strings go through Qt translation. It reads through the same `Config::GameSettings` object the existing pages use (`extensionspage.hpp` shows the pattern).

### 13.4 What it must NOT do

- Modify, move or delete any original mod file or any file of the user's game installation.
- Redistribute or upload mods or repacked output; repacks are local derived copies for the user's own use. Mods have their own licences (**unknown** per mod); the packer records provenance and states this.
- Include any game or mod asset in the repository, tests or CI (use generated or permissively licensed samples, root `CLAUDE.md` guardrails).
- Rewrite `openmw.cfg` without a backup, or touch lines it does not own.
- Depend on a server, an account or a network connection.
- Run extension code (the Extensions page reads manifests only; same rule).

### 13.5 Spec ordering

1. **M0 Profiles and read-only conflict view** (no packer): needs only the launcher and the VFS scan; useful on its own.
2. **M1 Packer command-line tool** (archive doc E3: reference library, deterministic packer, round-trip tests).
3. **M2 Repack and restore in the launcher**, with verification and the undo log (needs M0, M1).
4. **M3 Derived data**: World LOD page and profile awareness (needs S8 of section 11).
5. **M4** (optional) load order sorting helpers and import of other managers' profiles (**unknown** formats; not started).

## 14. Open decisions for toddhow

| # | Decision | Recommendation |
| --- | --- | --- |
| D1 | Shipped LOD or our own for New Vegas | Our own from full detail; shipped LOD at most a later preview (3.7) |
| D2 | Baker as a separate low-priority process | Yes (4.2) |
| D3 | Tile selection by pixel error, with starting thresholds 2/4/8 px | Yes; thresholds set by the S0/S1 measurements |
| D4 | Transition method | Geomorph for terrain, dithered cross-fade for objects, haze for the rest (7) |
| D5 | Per-object simplify then merge, or merge then simplify | Simplify per object first, then merge; revisit after S4 data |
| D6 | Impostors need a GPU bake; allow only when the game is closed | Yes, as a later stage (S10) |
| D7 | Keep the live terrain and distant-object path as tier 0 and as the fallback for missing tiles | Yes |
| D8 | DirectStorage | Not required; optional fast path after measurement |
| D9 | A new `[WorldLod]` settings group, everything off by default | Yes |
| D10 | Which stage to start with | S0 (measure) and S7 (grass) in parallel, then S1 |
| D11 | Does the Eden archive container come first, or does S1 use a private cache layout and migrate | Private layout first, migrate at S2 (avoids blocking on the format) |
| D12 | The eight archive decisions | As in 12.4 |
| D13 | Add the container reader and writer to the full-loop list in root `CLAUDE.md` | Yes |
| D14 | Rename the archive doc's E1 to E9 milestones | Yes (12.2) |
| D15 | Mod organizer scope: start with M0 | Yes, after the launcher has no other large open task (**verify** with toddhow) |
| D16 | A second, weaker GPU for floor testing | Needed before tier thresholds are fixed |
| D17 | Pick a groundcover mod (with its licence recorded) as the grass test content | Needed for S7 |

## Changelog

| Date | Change |
| --- | --- |
| 2026-11-01 | Initial draft with research note 0018: the world-LOD model, baker and cache, streaming core, graphics tiers, transitions, grass, staging, a review of the Eden Archive Format draft, and the mod organizer tangent |
