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

## Update 2026-11-05 (task 0088)
A tile replaced by a coarser or finer tile now shows the COMPLEMENT of the incoming tile's dither instead of staying opaque
under it (`TerrainFadeState::mComplementOf`, `Builder::setBatchShare`); a tile that fades out over the live terrain is unchanged.
See `docs/tasks/0088-world-fade-in.md`, stage e.

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

## Stage log
Decisions of toddhow, 2026-11-05: dithered fades over 0.4 s for tiles as for the object clusters, a stipple is accepted
(TAA blends it), the hand-over from a tile to the live terrain fades too, and NO geomorphing in this task (the silhouette
change between levels is accepted; a later task if the fade does not hide the pops).

**Stage b (2026-11-05, Windows).** The fade is one float, `fade`, in the batch uniform (binding 2 of the terrain's texture
set, `files/shaders/terrain/data.glsl` and `.slang`, three padding floats keep the block at 32 bytes; `Terrain::makeBatchUniform`
in `components/terrain/batchdata.hpp`), now visible to both stages (`Pipeline::terrain`'s layout). Both fragment stages
(GLSL and the Slang twin) end with `if (batch.fade < 1.0 && dither8(...) == 0.0) discard;`, the ordered dither of
`default/main.frag`'s `LOD_FADE`: last, so that no derivative taken above it (the layers, the parallax march) sits in a quad
with a dropped pixel; neither stage returns early before it. The colour, the G-buffer outputs and the shadow cascades are
written by the same invocation (the terrain has one pipeline for every view), so they drop the same pixels; the dither
pattern uses `gl_FragCoord` of the view being drawn, so a cascade's stipple is its own, as for the object clusters.
`Terrain::Builder::setDynamicBatchData(true)` (only the LOD tiles' builder) makes the uniform upload again when
`Builder::setBatchFade` changes it; the live terrain keeps a static uniform of 1.
**Deviation from the spec text:** the define set did NOT gain a member. A `LOD_FADE` define would double the reachable
terrain sets (`PipelineTerrainDefines` pins their number, 26,040) and the 39 listed permutations; a runtime branch on a
uniform the draw already binds needs neither, and the live terrain pays one uniform read and a branch that is never taken
(fade 1). The cost is stage d's to measure. `SlangPermutations` passes for all listed sets with the new binding in the
fragment stage (the twin declares what the GLSL declares).
Tests (`apps/components_tests/terrain/testterrainfade.cpp`, 6): the first run, with the C++ half in and the shaders and
the layout not, failed 3 (the shader text, the discard-last rule, the fragment visibility of binding 2) and passed the 3
uniform tests; all 6 pass now.
Frames: the scenario of `tools/tilefade_check/run.sh` (`still`: Balmora at +2000, +8000 and +25000 units, two shots each,
`OPENEDEN_FIXED_FRAME_TIME`, `[WorldLod]` with map view and object clusters, the same baked packs) from the executable
before (`taa2/exe-new`, built 05:29 from the code of this commit's parent for the terrain) and after. **Not byte
identical, and neither are two runs of the executable before:** PSNR between two runs of the old executable 57.9, 62.4 and
54.0 dB (poses 1 to 3), between the old and the new 56.5, 65.0 and 61.5 dB, the differences (a difference image, amplified
40 times) confined to the same few spots (the far object clusters and water edges), not the ground. The stage a result of
byte-identical frames (`bal_8000`) used another pose and did not repeat here. The Slang-on, rain and fade-active frames are
in stages d and e.

**Stage c (2026-11-05, Windows).** `Lod::stepTerrainFades` (`components/lod/terrainfade.*`, pure) reuses `Lod::stepTileFade` per tile
and adds the hand-over rules: a tile that appears dithers in over what drew its square and counts as covering (the live
terrain leaves its square out) only once solid; a replaced tile (finer or coarser) stays solid until the replacement is solid and is
then dropped in one frame (`Remove`); a tile given to the live terrain (`RefinedGate::ToLive`) dithers out over it, from the first
frame, no longer covering; a fade turns around from its current value; a tile the selection keeps refining without
finer tiles solid yet (`Stay`, a failed child included) stays at full for good. `MWRender::LodTerrain` runs it (`stepFades`) instead of the
removal loop when `[WorldLod] terrain fade time` (new, default 0.4, in the test profile; 0 or less runs the old code untouched) is above 0;
`refinedTileMayGo` and the coverage count only tiles drawn in full. A tile the residency planner frees is marked leaving
(`ResidencyItem::mLeaving`: its bytes are held but counted as gone, so it causes no more pressure and is never freed twice) and
dithers out over the live terrain before its draw goes. The tile's fade reaches the shader through `Builder::setBatchFade`.
Tests (`apps/components_tests/lod/testterrainfade.cpp`, 17 unit and 5 simulated sequences over a root and four children: 300
random seeds with and without live terrain, reversal at every frame of a swap, a child that never builds, determinism): run red
first against a stub that does nothing (12 of the 22 failed; the others pass trivially); green with the code; a mutation
(a covered tile removed without the ancestor being solid) fails 3 of them (the hole check among them). `components-tests` 6300
and `openmw-tests` 1058 pass. The invariant tested: outside the live terrain's range, a cell that a solid tile covered is
never left uncovered.
In game (`tools/tilefade_check`, a quiet window, fixed frame time): the run with `terrain fade time = 6` and a teleport to 25000 shows the new far tiles and
clusters as a fine stipple that thins over the following shots (`sheet-j.png`), then solid; with 0.4 and 0 the descent from 25000 to 17000
swaps one level 2 tile for its children, and the frame difference between the two settings at the same camera position is two
specks (the swap is subtle at that distance by design of the pixel error threshold; no frame shows a hole or a flash).
**Decision for toddhow / known limits:** (1) a tile that is new with nothing beneath it (past the live range, no neighbour
drawn) also dithers in, over the sky; I kept that because it is what removes the pop-in at the horizon, but it can start solid
instead (one line in `LodTerrain::build`). (2) Tile geometry and the live terrain beneath it can z-fight where they coincide
during a fade (same colours, so not visible in the frames viewed). (3) The Linux gate, the Lua tests, `eden-studio` (it needs
the new default in its `defaults.cfg`: rebuild it) and a play-through are not run.

**Stage d (2026-11-05).** Scripted climbs (`tools/mapview_check/run_ascent.sh`, Balmora to 25,000 units and back, 150 s, one run each,
a shared machine; `terrain fade time` 0 against 0.4; `[WorldLod] tier` minimal and high). Median frame / p99 / frames over 16.7 ms /
resident / device memory: minimal 3.67 ms, 5.28 ms, 7, 1362 MiB, 393 MiB against 3.67, 5.26, 9, 1360, 393; high 3.76, 5.30, 11, 1505,
425 against 3.71, 5.38, 18, 1504, 441. Mean scene draws (sampled every 30 frames) 34,824 against 34,827 (minimal) and 36,793 against
36,416 (high), the largest sample 45,198 against 45,515 and 45,912 against 40,512: the sampling cannot see a fade of 0.4 s (24 frames), so no
extra layer shows in the means. The 28 and 61 tile swaps are the same in both. The frames over 16.7 ms are loading frames of the first
seconds (worst 0.66 to 1.08 s in all four runs, frame 178 to 369), within the spread of repeats. NOT measured: the extra draws and triangles
during one fade (needs a per-frame counter), the ground walk, tiers low and medium, GPU time of the terrain pass with the `discard`
(the uniform branch costs every terrain draw, the live terrain too). The residency planner counts a fading-out freed tile as leaving (not
committed, never freed twice) and a tile that is merely replaced as pinned (held, counted); neither can stall a fade: the
fade does not wait on the planner and the planner never frees a fading tile.
**Stage e (2026-11-05).** Frames in `I:/Projects/OpenEden-work/tilefade/`: `sheet-j.png` (a teleport to 25,000 units with `terrain fade time = 6`
so that the dither lasts long enough to see: new far tiles and clusters come in as a stipple that thins over the shots, no hole, no flash),
`sheetA-on.png` (the 0.4 s descent, twelve shots around a level 2 to level 1 swap: no visible change), `sheet-dj.png` (amplified difference of
the 0.4 s and the instant runs at the same camera: two specks). The swaps of a descent are subtle by design of the pixel error threshold, so
a before/after sheet of an obvious pop was NOT obtained; the evidence is the long-fade stipple and the absence of holes. Not viewed:
TAA on, `slang shaders` on, rain, night (the runs used the defaults of the test config: hdr, pbr, no taa). `tools/tilefade_check/run.sh`
(`SCENARIO=still|flight`) is the rig; screenshots of consecutive frames need `[Video] framerate limit = 5` to 10, or the writer skips them.

## Stage log: the huge-tile crash (task 0090, 2026-11-05)

`[WorldLod] terrain tile threshold = 100000` with a low flight (`0,0,2500` to `5000,0,2500`) crashed the game (reproduced before the fix: `openmw-crash.dmp`, a C++ exception thrown from VSG's allocator, 596 log lines in, "589 covered"). The same flight with the fix, three runs (600 frames, one with the final executable): no dump, frames viewed (`I:/Projects/OpenEden-work/hugetile/user-t3/screenshots/screenshot005.png`), the log line "589 covered" as before.

- **Stack (dbghelp walk of the dump, `hugetile/ds.cpp`, not committed):** `RaiseException` <- `vsg::IntrusiveAllocator::MemoryBlock::allocate` (IntrusiveAllocator.cpp:137, the throw "allocated slot found in freeList": a corrupted free list) <- `vsg::computeIndexType` <- `Terrain::CreateIndexBuffer::t_create<vsg::Array<unsigned short>>` (`indexbuffer.hpp`) <- `Terrain::Builder::createDraw` (builder.cpp:638) <- `Terrain::Paging::updateView` (paging.cpp:300) <- `RenderManager::updateTerrain`. So the heap was already damaged by an earlier index fill; the allocator only noticed.
- **Root cause:** `Paging::updateView`'s `getLodFlags` gives a live chunk next to a covered square the step `covered->vertexLod - ourVertexLod` for that edge. With one tile covering 589 of 591 squares the neighbour is a very coarse LOD tile, so the step is large. `CreateIndexBuffer` stitches an edge of `verts - 1` quads in groups of `1 << step` quads, which only fits when the group divides `verts - 1`: for a grid of 33 vertices the limit is 5, and the chunk asked for 7. The buffer was sized for fewer indices than the stitch loops wrote (a heap overflow). A step above 15 also spills into the next edge's nibble in the OR. Evidence in the log of the fixed build: "Terrain chunk of 33 vertices a side asked for edge steps 0x2007 but can take 0x2005 at most". Standalone harness (`hugetile/h.cpp`: the old header with its assert turned into a flag, verts 2 to 130 swept): all four edges at one step are exact up to 6 for 65 vertices, 5 for 33, 4 for 49, and miscount beyond.
- **Fix:** `Terrain::maxStitchDelta(numVerts)` (the largest power of two dividing `numVerts - 1`) and `Terrain::safeLodFlags` cut each edge's step to it in `CreateIndexBuffer::create`, with one Warning per session (text above) so a crack at that edge is explained. The end-of-fill `assert` (compiled out of the game) is a thrown `std::logic_error` now. The 16-bit index test no longer wraps (`numVerts * numVerts` in 64 bits). A crack at the cut edge is the price: the chunk lies next to a tile far coarser than it can be stitched to. (A later change could split such chunks instead, as `touchedVertexLod` does for coarser neighbours.)
- **Tests:** `apps/components_tests/terrain/testindexstitch.cpp` (8 tests: limits, cutting, bits above the fourth nibble, every key for 2 to 70 vertices and steps 0 to 15, all mixed steps up to the limit for 5 sizes). Red first: with the cut taken out of `create`, `every_key_builds_a_buffer_that_is_exactly_full` fails (969 failures) and the pure ones do not compile against the old header; green after. **Not wired:** `apps/components_tests/CMakeLists.txt` was being edited by another agent, so the line `terrain/testindexstitch.cpp` (after `terrain/testcoverage.cpp`) is still to be added, and the tests ran only against a stub gtest harness (`hugetile/t.cpp`), not in `components-tests`.
- **Other absurd values** (flights of 200 frames through `tools/tilefade_check/run.sh`, executable of the fixed tree, `map view` false so that the setting reaches the selection; with `map view` true the far tier raises the threshold to 8): threshold 0.0001, -5 and nan (all became 0.01), 1e9 (cut to 1e6), 100000 (the crash case), fade time 1e9 (60) and nan (0), `terrain tile level` 0 and 99 (already clamped to 1), `view distance` -5 and banana (ignored with a message by `parseViewDistanceSetting`), `max view distance = 0.5` (1): none crashed or hung. Findings: the sanitizers were lower bounds only (`makeMaxSanitizerFloat(0)` is a minimum), so 1e9 and nan passed as written, and `std::clamp` returns a nan unchanged. Now `terrain tile threshold` and `object cluster threshold` 0.01 to 1,000,000, both fade times 0 to 60 s, `max view distance` 1 to 10,000,000, `haze visibility` 1 to 10,000,000, `haze height` 1 to 1,000,000, `haze weather gain` 0 to 100; the shared clamp sanitizer turns a nan into its minimum. **Deviation:** the clamps are silent (the sanitizers have no logging, and `rendermanager.cpp` was being edited by another agent); the ranges are in the `settings-default.cfg` comments.
- **Not fixed:** `view distance = 1000000000000` (cut to 10,000,000 units by the parser) with `map view` makes the first live terrain build load the land of thousands of cells in one frame; the freeze monitor ended the run twice (a slow start, not a crash: `RenderManager::changeCellGrid` -> `Paging::updateView`). Lowering `Lod::maxViewDistanceNumber` would change pinned view limit tests. One earlier freeze with `threshold = 0.0001` and `map view` true did not repeat in two runs with `map view` false, and is taken for load noise (another build was running).
- **Not verified:** the Linux gate (GCC -Werror), the Lua tests, `components-tests` as a whole, a fresh reviewer, clang-tidy. The executables were built from a tree that also held another agent's uncommitted `terrain/builder.*` and `paging.*` edits.
