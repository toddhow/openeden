# Task: Terrain tile fade (no pop when a far tile changes)
Status: draft spec (2026-11-04), needs toddhow's approval. Nothing built.
Design doc section: `docs/design/` world LOD (tasks 0055, 0060, 0066).
Goal: when the world LOD swaps a terrain tile (a tile replaced by finer tiles, a tile handed to the live terrain, or a
tile appearing), the change dithers over a fraction of a second as the object clusters already do (`LOD_FADE`, task
0059), instead of changing in one frame.

## Why
toddhow, 2026-11-04, playing the test profile: "terrain popping in where my crosshair is" (videos 20-58-36, 21-03-33)
and "a major pop in". Without the world LOD he cannot see that far, so the pop is the LOD. The object clusters fade
(`LOD_FADE`, `files/shaders/default/main.frag`, 0.4 s); the terrain tiles do not: `MWRender::LodTerrain`
(`apps/openmw/mwrender/lodterrain.cpp`) adds and removes the draw in one frame, and `files/shaders/terrain/main.frag`
has no fade define. The log shows no event at the moment of a pop because tile swaps are not logged at all.

## Stages
- **a. Log the swaps (small, no behaviour change).** One Verbose line per tile change, with the frame time, tile key
  (level, x, y), what it changed from and to (`tile -> children`, `tile -> live`, `live -> tile`, `new`), behind
  `[Cells] load trace` like the cluster latency line, so a pop in a video can be matched to a timestamp. Done first:
  it shows whether the pops are tile swaps at all (toddhow's two videos give times to compare).
  **Done (2026-11-04, Windows):** `Lod::formatTileSwap` (`components/lod/tileswap.*`, 4 tests) gives one Verbose line per change from `MWRender::LodTerrain`: `World LOD tile swap: frame N, level L tile (x, y) at cell (cx, cy): <kind>`, kind one of `new`, `refined`, `coarsened`, `to live`, `removed`; with `[Cells] load trace` each swap is also an event `lod.tile.<kind>` (`tolive` for to live) with the level as its value in `load-trace.jsonl`. `new` is logged when a tile is built, `removed` when the residency planner frees it, and the rest when a drawn tile stops being drawn: `coarsened` if a coarser tile now covers its square, `refined` if the selection went to the live terrain and some child tile is drawn, else `to live`. The frame is the count of `update()` calls with tiles in use, not the render frame. Seen in game (scripted climb, tier high, threshold 64): 24 `new` and 5 `coarsened`, matched by the trace's events; threshold 8 gave 61 `new` and no removals in that climb, so `refined`, `to live` and `removed` were not seen in game. With the code present and the trace off a frame (`bal_8000`, fixed frame time) is byte-identical to the build before, 2 runs each. Not run: the Linux gate.
- **b. A fade in the terrain shader.** `LOD_FADE` for `terrain/main.frag` in both languages: the GLSL file and the
  Slang twin (`files/shaders/terrain/*.slang`, 39 listed sets, so the define set gets a new member and
  `SlangPermutations` lists the fade variants), the same 8 x 8 ordered dither as the object shader, driven by a
  per-draw value (a uniform or the material alpha, as the cluster does, so no new descriptor). The depth and G-buffer
  outputs must discard the same pixels (the object path does this; read it first). Off (no fade active) must stay the
  same pixels and the same cost: the fade define is only on for a draw while it fades, or the branch is cheap and
  proven so by a frame comparison.
- **c. The state machine.** Reuse `Lod::stepTileFade`/`Lod::stepCoverage` (`components/lod`): a tile that goes away
  stays drawn, fading out, while the thing replacing it fades in underneath (both drawn for the fade time), then it
  is removed; a tile that appears fades in over the live terrain or the coarser tile. The "no hole" rules of task 0060
  stay: a replaced tile stays until its replacement is drawn. Pure logic, unit tests first (including: a swap in the
  middle of a fade, a reversed direction, a tile that fails to build mid-fade, the tile budget with a fading pair
  drawn).
- **d. Cost and memory.** Two drawn layers during a fade: measure the extra draws and triangles in a scripted climb and
  a walk (tiers minimal to high), and the resident memory with the residency planner (a fading tile counts its bytes
  until it is gone; the 1.25 swap margin and the budget must not make a fade stall).
- **e. Frames.** A scripted flight over the same stretch with and without the fade, a frame every few frames around a
  swap, viewed; and toddhow's own look.

## Open questions for toddhow
1. Fade time: 0.4 s as the clusters, or longer for tiles (they are bigger)?
2. A dithered fade shows a stipple pattern on large flat ground for 0.4 s; with TAA on it blends. Is a stipple
   acceptable, or should the tile blend with alpha (needs sorting, costs more, and the terrain is opaque)?
3. Level changes between tiles of different levels also change the silhouette (the geometry itself differs, not only
   the colour): the fade hides the pop but not the shape change. Accept that, or add geomorphing (vertex blend between
   levels) as a later task?
4. Does the live-terrain hand-over (tile to live chunk when you come close) fade too, or only tile to tile?

## Out of scope
The object clusters (already fade), the live terrain's own paging, the baked data and the pack layout (no change to
version 1), new settings beyond one fade time (put it in the test profile if added).
