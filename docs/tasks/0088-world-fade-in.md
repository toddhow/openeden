# Task: Everything fades in (world fade-in)
Status: draft spec; stages a and b done, c not feasible without a decision, d is a ranked list (2026-11-05).
Design doc section: `docs/design/` world LOD (tasks 0055, 0060, 0066, 0087).
Goal: nothing in the world appears or disappears in a single frame while the player moves; each piece dithers in or out
over the `[WorldLod] terrain fade time` (0.4 s), as the object clusters (task 0059) and the LOD terrain tiles (task 0087)
already do.

## Why
toddhow, 2026-11-05: "Everything should have a fade in." In the Ashlands region (thick ash fog) distant terrain around the
mountains pops in as he walks closer. Task 0087 (pushed) fades the LOD tiles. The live terrain (`Terrain::Paging`) and
everything else that streams in still changes in one frame.

## Finding 1: "there isn't a far tile" in the Ashlands (stage a, 2026-11-05)
toddhow: "the fact there isn't a far tile is a problem": from far away the land around the mountains is simply absent.

**Cause: not a missing tile. The ash storm's aerial haze (`[WorldLod] haze`) hides everything past about 3,000 units.**
Evidence:
- Pack coverage is fine. The profile run (`I:/Projects/OpenEden-work/fadein/`, his profile config copy, Windows, build of
  `6e2acacb4e`) logs "593 tiles of levels 1 to 6 loaded (layout version 1)", no stale-pack warning, and "35 visible tiles
  use the live terrain, 151 covered, 369 outside the view" at the first frames (the 369 are outside the view cone). His
  `[Camera] viewing distance` is 30000, so the live terrain also reaches 30,000 units. In clear weather (frame
  `w0a0.png`, Ald-ruhn at +7000 units) the land is drawn to the horizon.
- The haze maths (`components/render/aerialmath.hpp`, `Render::Aerial::weatherBeta`): density at the ground is
  `ln(10) / 100000 * exp(gain * excess)` with `gain = haze weather gain = 10` and `excess = min(fog depth - 0.7, 0.42)`.
  Morrowind's fog depths (his `openmw.cfg`): ash storm and blight 1.1 by day (1.2 by night), foggy 1.0 (1.9 by night),
  thunderstorm 1.0, snow 1.0. An ash storm's excess is 0.4, a factor of 54.6, so a horizontal ray at the ground keeps
  10 % of the light at **1,830 units**, 1 % at 3,660 and 0.1 % at 5,500. Foggy day: a factor of 20, 10 % at 5,000 units.
  Whatever lies past about 3,000 units in an ash storm is more than 98 % the fog's colour, tile or live terrain alike.
- A frame in a natural ash storm (`q100000.png`, Ashlands Region, 4,500 units up, the default settings): the ground is
  visible for a few ridges and then is the air's colour; no ridge past that shows.
- Why it reads as "pops in": the land is not missing, it comes out of the fog as he walks, and what enters the thin
  fog range (under about 3,000 units) is exactly the things that change in one frame: live terrain LOD splits, object
  chunks, cells. His 0.4 s tile fade cannot help there, it only covers tile swaps beyond the live range.
- Not reproduced: a scripted forced ash storm. `ChangeWeather` in a `--script-run` file did not take (the weather is
  rolled by the region after the commands run); the one ash-storm frame was a natural roll. The same fog was
  reproduced by maths, not by a frame with the setting changed.

**Options (decision for toddhow, nothing changed):** (1) leave it: it is the intended ash storm, but it is much thicker
than vanilla's linear fog, whose end is at the view distance (OpenMW's linear fog at 30,000 units shows land at 3,000 units
with about 18 % fog); (2) lower `haze weather gain` (the setting exists): gain 5 gives a factor of 7.4, 10 % at 13,500 units;
gain 3 gives 3.3, 10 % at 30,000; (3) cap the excess lower than 0.42 for ash and blight only (a code change in
`weatherBeta`, a new constant). Not measured: frames at gains 5 and 3 (every run of a script that teleports froze at the
LOD adoption, 5 s without a message pump, on a loaded machine; the freeze monitor ended them).

## Survey (stage a): what appears or disappears in one frame while moving
Thread: where the add or remove happens. "Per-draw fade": can a fade value reach the shader per draw today.

| # | Piece | Added / removed by | Thread | Per-draw fade possible? | Fades now |
| --- | --- | --- | --- | --- | --- |
| 1 | Live terrain chunks (`Terrain::Paging::updateView`, `components/terrain/paging.cpp`) | when the view point moves past `View::sqrReuseDistance`, the view distance changes or the coverage changes, EVERY batch child is detached and the batches and draws are rebuilt (`view.rootNode->children`); a chunk is a `vsg::DrawIndexed` whose `firstInstance` packs lod, shift and grid position | main (the player's view, `RenderManager::updateTerrain`); the preload thread owns other views that are never in the scene | **Yes.** The batch uniform is shared by every chunk of a batch (a batch is 1 to 16 cells wide), so a uniform fades a whole batch only; but bits 4 to 7 of the lod byte and 12 to 15 of the shift byte of `firstInstance` are free (lod, shift and grid never pass 15), so a per-chunk value can ride in the draw and be read by the vertex stage | no |
| 2 | LOD terrain tiles (`MWRender::LodTerrain`) | selection by pixel error | main (builds on a worker) | uniform per tile (`batch.fade`) | **yes, task 0087** |
| 3 | Live terrain under a tile (coverage hand-over) | `coverage` squares come and go with the tile fade | main | n/a | a covered square is left out only once the tile is solid (0087), a given-back square draws the live terrain at once beneath the fading-out tile |
| 4 | LOD object clusters (`MWRender::LodClusters`) | selection | main (builds on a worker) | material `specular.a` + `LOD_FADE` | **yes, task 0059** |
| 5 | Object paging chunks (`MWRender::DistantObjects::setShownChunks`, `distantobjects.cpp`) | one chunk (a `CullGroup` of every static of one cell, built on a worker, `buildChunk`) per cell in range, attached when built, detached when the cell leaves the range or enters the active grid | main (builds on operation threads) | **No, not cheaply.** The chunk's children are the shared model prototypes (`readNode`), with the models' own pipelines and materials; `LOD_FADE` reads `material.specular.a`, a per-material value shared by every instance of the model. A per-chunk fade needs either an object shader permutation with a push constant (every static pipeline doubles) or a per-chunk copy of every material (loses sharing). See stage c | no |
| 6 | Cell objects and actors on a cell load (`Scene::insertCell`, incremental build `Scene::buildPendingObjects`) | the 3 by 3 grid around the player (4,096 to 12,288 units out) | main | no (same material problem; actors have skinned, per-actor pipelines) | no |
| 7 | Groundcover (`MWRender::Groundcover`, instance chunks) | chunks by distance | main (builds on a worker) | not read in this stage | no |
| 8 | Water (`MWRender::Water`) | a plane that follows the camera; not a streamed piece | main | n/a | n/a |
| 9 | Distant object chunk to live cell hand-over | `DistantObjects::update`: a cell entering the active grid drops its chunk the frame the cell is built (`mUnbuilt`) | main | no | no |
| 10 | Sky, clouds, weather | continuous | main | n/a | n/a |

Pops confirmed by the swap log: the existing `World LOD tile swap` lines (`[Cells] load trace`) cover the tile pops only;
the live terrain rebuild has no log. Stage b adds a Verbose line per live chunk change.

## Stages
- **a. Survey and the Ashlands finding.** Done (this document).
- **b. Live terrain chunks fade in and out** (per chunk, bits in `firstInstance`). Pure `Terrain::ChunkFader`
  (`components/terrain/chunkfade.*`), shader decode in both languages, `Paging` integration, the same
  `[WorldLod] terrain fade time` (0 or less keeps the old code path; the live terrain pays nothing beyond one more flat
  varying and a multiply).
- **c. Object paging chunks.** Written down below, not built unless toddhow chooses an option.
- **d. Anything small and safe from the survey; a ranked list for the rest.**

## Open questions for toddhow
1. The ash fog (Finding 1): leave, lower `haze weather gain`, or cap ash and blight?
2. Object paging chunks (row 5) and cell objects (rows 6, 7): see the ranked list in the stage log.

## Out of scope
Geomorphing (decided 2026-11-05: no), the baked data, the object clusters and tiles (already fade).

## Stage log

**Stage b (2026-11-05, Windows): the live terrain's chunks fade (`c25402e64f`).** On only with `[WorldLod] enabled` and
`terrain fade time` above 0 (the same setting as the tiles; with the world LOD off, which is the default game, nothing runs).
- *Pure part.* `Terrain::ChunkFader` (`components/terrain/chunkfade.*`) remembers the live view's chunks across its
  rebuilds by `Bounds`. A new chunk fades in; a chunk that is replaced (split into finer chunks, merged into a coarser one)
  stays at full, and a half-faded one rises to full, until every chunk that overlaps it is solid, and is then dropped in one
  frame; a chunk nothing replaces fades out; a chunk a LOD tile already covers is dropped at once; a new chunk where a tile
  drew just before (the hand-back of task 0087) and every chunk of a view that shares no area with the old one (a
  teleport, the first build) start solid. 14 tests, 8 red first against a do-nothing stub (the other 5 pass against it
  trivially), a 300-seed property test: no cell that was drawn solid and is still wanted ever loses its solid cover before
  its replacement is solid. Not mutation-tested.
- *Shader part.* A chunk's `firstInstance` packs lod (byte 0), shift (byte 1) and grid x, y (bytes 2, 3). All four never pass 15,
  so the high nibbles of bytes 0 and 1 hold the share of the dither that is DROPPED (255 minus the fade in 255ths):
  `Terrain::encodeChunkFade`. A solid chunk and every other user of the terrain pipeline (the map, the editor, the tiles)
  keep the old value exactly. The vertex stage (GLSL and Slang) masks lod and shift with 0xf and writes a `flat float
  chunkFade` varying (last in the block, so no other location moves); the fragment stages multiply it into `batch.fade`. 8
  tests (the encode functions, the shader text in both languages; red first: the file did not compile). `SlangPermutations`
  passes (all 39 listed fragment sets and the 4 vertex sets). No new define, no new permutation.
- *Paging.* `Paging::updateView(..., const ChunkFadeParams*)`: with parameters the main thread steps the fader, writes the
  fades into the `DrawIndexed::firstInstance` of the draws in the scene (the command graph is recorded every frame), and
  rebuilds the view when a leaving chunk was dropped; the leaving chunks are drawn again in each rebuild (their batch is
  created if the batch left the range). Preload views and the map pass nothing and run the old code.
- *Not changed by it:* the batch uniform stays one per batch; the cost of a rebuild is that of any rebuild (batch data is cached).
- **Off is unchanged:** with `terrain fade time = 0` and the world LOD on, old and new executables give the same frames as two
  runs of the old one (a flight, PSNR old-vs-old 45 to 50 dB, old-vs-new 46 to 50, new-vs-new 49 to 52). With `[WorldLod]
  enabled = false` (the default game, a still pose, the changed terrain shader): old-vs-new 59.5 to 59.9 dB, new-vs-new 58.8.
  Neither pair is byte identical (cloud and NPC animation).
- **What I saw, plainly.** The log shows the fade working (`Terrain chunk fade: 129 chunks, 32 fading in, 8 leaving`, Verbose),
  and with a 60 s fade, tiles off and a flight the pictures differ from the old executable only in far ground and at edges,
  amplified difference `dnt14.png`; I could NOT make a frame in which the live fade is visibly different to the eye: the
  chunks that appear at the edge of the live range mostly lie over a tile (they start solid by rule) or over the sky past
  the fog. No before/after pair of an obvious live pop was obtained. The fade is therefore proven by tests and logs, not
  yet by a picture.
- **What it can and cannot fix** (read this before relying on it). A split or merge keeps the old chunk opaque under the new
  ones, depth tested: during the fade the screen shows the nearer of the two surfaces, not a blend, and when the old chunk drops
  where its coarser surface was in front of the finer one still changes in one frame. The fade helps chunks that appear at
  the edge of the range or over sky, and squares a tile hands back; it does little for geometry LOD changes. The 0087 tiles
  share that limit. A *complementary* dither (the leaving chunk keeps exactly the pixels the incoming one drops) would give an
  exact per-pixel hand-over: one flag in the encoding (dropped values of 128 or more meaning "inverted", 7 bits of
  precision). Not built; offered.
- **Two `updateTerrain` callers:** `RenderManager::changeCellGrid` calls it with the player's position and dt 0, the frame loop
  with the camera and dt; at a cell crossing the two builds can want different chunk sets (seen: 105 / 129 chunks 3 ms apart,
  fades continuous, no pop). Both pass the fade parameters, so neither rebuilds without the fader.
- **Not measured:** the CPU and GPU cost (`load-trace.jsonl` was not written, the runs ended by a timeout; the number of rebuilds
  at fade 6: 338 `Terrain chunk fade` lines in a 25 s flight), a ground walk, rain, TAA, `slang shaders` on, Linux gate,
  `clang-tidy`, clang-format 14 (only a 23 was available; new files formatted with it, changed lines kept under 120 columns by hand),
  Lua tests, a play-through. `components-tests` 6409, `openmw-tests` 1058 pass, `eden-studio` builds.

**Stage e (2026-11-05, Windows): the complementary dither, tiles and live chunks (toddhow's decision).** The leaving draw keeps
exactly the pixels the incoming one drops, so each pixel is drawn by one of the two instead of two opaque layers.
- *Encoding.* One "fade code" float reaches the fragment stage: the fade (0 to 1), or, below 0, `-1 - incoming fade` (keep the
  pixels that fade drops). A live chunk's code rides in its `firstInstance`: bit 23 (above the four bits of the grid x) is
  the "inverted" flag and the same two nibbles hold the incoming fade (`Terrain::ChunkShare`, `encodeChunkShare`,
  `decodeChunkShare`, `fadeCode`); a solid chunk is still the old value. A tile's code is the batch uniform's `fade`
  (`Builder::setBatchShare`, both numbers rounded to 1/1024 so that the pair reads the same float). The fragment stages (GLSL and
  Slang): `code = chunkFade < 1 ? chunkFade : batch.fade; if (code < 0) discard where dither8(-1 - code) != 0; else if (code < 1)
  discard where dither8(code) == 0` still as the LAST statement, no new define or permutation (`SlangPermutations` passes).
- *Pure parts.* `ChunkFader::shareOf`: a SOLID leaving chunk that wanted chunks replace COMPLETELY (their overlap areas add up to
  its area) shows the complement of the weakest of them; one only partly replaced, still fading in, or replaced by nothing keeps its
  old behaviour. `Lod::stepTerrainFades` gained `TerrainFadeState::mComplementOf` (a solid finer tile under a coarser tile that is
  fading in shows the complement of that tile's fade) and `TerrainFadeInput::mReplacementFade` (the caller, for a refined tile that
  waits for four drawn finer tiles: `LodTerrain::replacementFade`, their weakest fade of the last frame).
- *Tests, run red first:* 13 `TerrainChunkComplement` (3 of the 10 that can fail did against a `shareOf` stub; the rest are the
  encoding and rule tests, written with the code; a 40-seed property test at PIXEL level, 64 pixels a unit cell: no pixel of a wanted
  cell is ever lost across changes and steps; a split of four equal children has every pixel drawn by exactly one chunk; the dither table
  in the C++ twin equals both shader files) and 5 `TerrainFadeComplement` (2 of 5 red against fields with no logic); the batch
  uniform test (`setBatchShare`) was written together with the code (not run red). `components-tests` 6449 and `openmw-tests` 1058
  pass; `eden-studio` builds.
- **Existing tests changed (mine, from stage b, one file, awaiting your approval):** `apps/components_tests/terrain/testchunkfadeinstance.cpp`, the
  test `the_fragment_stages_multiply_the_batch_fade_by_the_chunks`, 4 lines: the old expected text `float fade = batch.fade *
  frag_in.chunkFade;` and `if (fade < 1.0 && dither8(... fade) == 0.0)` (GLSL) and `float fade = data.batch.fade * input.io.chunkFade;`
  and `dither8(..., fade) == 0.0` (Slang) became the fade-code lines above. The test's name is stale. No test of an earlier task changed.
- **Limits, plainly.** (1) The hand-over is exact (every pixel by exactly one draw) only where the replacing chunks share one fade;
  the leaving draw follows the WEAKEST replacement so that no pixel is ever lost, and a replacement that is further along than the
  weakest overlaps the leaving draw (depth decides which wins). In a flight the children of one tile appear at different times, so
  the complement mostly sits at the weakest one's value. (2) Only the replaced-by-live-chunk and tile-replaced-by-tile hand-overs are
  complementary. A tile that fades out OVER the live terrain (to live) and live chunks that appear under it still use the old two layer
  rule (the tile dithers out over solid live chunks); coupling the two systems' fades was not done. (3) A fade that already runs is
  not re-timed.
- **Frames.** A temporary build tinted every complement-mode (leaving) pixel red (the tint was removed again, not committed):
  a 6 s fade flight (`keep/screenshot060.png`, `keep/sheet-tint.png` crops of four times): the leaving chunks are red and
  the incoming ground interleaves with them in the dither pattern where the weakest fade is above 0 (fine lines at the red region's
  edges and inside it; no black or doubled pixel seen); large areas stay all red for seconds (limit 1). I did not get a plain
  (untinted) before/after pair: old and new ground have the same colours, so the interleave is invisible without the tint, which is
  what the hand-over should look like. `exe-new2` frames of a 25,000 to 16,000 descent (6 s fade) show nothing to the eye either way.
- **Cost, not measured:** no GPU or CPU numbers for the second branch in the fragment stage (it replaces the old one with the
  same count of texture-free instructions) and for `shareOf` (one map lookup per draw each frame; leaving draws scan the map once
  each). Not run: Linux gate, Lua tests, clang-tidy, clang-format 14.
- **Found, not mine:** with `[WorldLod] terrain tile threshold = 100000` a low flight (`FROM 0,0,2500 TO 5000,0,2500`) crashes the game
  (`openmw-crash.dmp`) in the old executable before this task as well (`exe` of 11:41, and the stage b one). One huge tile covers 589 of
  591 tiles ("589 covered"). Not investigated.

**Stage c: object paging chunks. Not feasible without a decision.** A chunk is a `CullGroup` of the SHARED model prototypes (`readNode`) with
their own pipelines and materials; `LOD_FADE` reads `material.specular.a`, one value per material shared by every instance of the model, and
a push constant per chunk needs a new object shader permutation for EVERY static pipeline (every `default/main.frag` set doubles: 55 listed
Slang sets plus the GLSL). Options, cheapest first: (1) a push constant `fade` read by a runtime branch in the object shader (no new
permutation, but every object draw of the game pays a read and a branch, the shader is the hottest one; needs a frame comparison for "off"); (2) a
per-chunk copy of the prototypes with their own materials (loses sharing: memory and descriptor sets grow by the number of cells in range; the
LOD clusters already are such a copy); (3) let the clusters do it: bake or start the clusters closer so object paging covers less of the far view.

**Stage d: ranked list for toddhow (cost, risk).**
1. The complementary dither for tiles and live chunks (small, a flag in the encoding and the discard test; makes the hand-over exact).
2. Object paging chunks by a push-constant branch (option 1 above; medium cost, touches every object draw).
3. Cell objects and actors on a load (rows 6, 7): the incremental build already spreads them; a fade needs the same per-draw value as (2).
4. Groundcover chunks (row 7): not read; probably a per-instance array, a fade needs a per-chunk uniform.
5. The ash fog (Finding 1): a one-constant change in `weatherBeta` (or `haze weather gain`), the biggest visible effect of the four.
