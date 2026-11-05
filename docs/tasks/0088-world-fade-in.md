# Task: Everything fades in (world fade-in)
Status: draft spec with stage a (survey) done, 2026-11-05. Stages b onwards are in the stage log below as they land.
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
(see below as stages land)
