# Task: World LOD, the whole map from altitude (no visible edge)
Design doc section: `docs/design/world-lod-and-streaming.md` sections 3 (the OpenEden world-LOD model), 5 (the streaming core), 7 (hiding the transitions), 8 (graphics options and fallbacks); research note `docs/research/0029-whole-map-view.md` (the numbers and the open facts below come from it).
Goal: from a high enough vantage the whole map is in frame, and from the ground the land ends in haze, never at a line: far land is drawn as basic tiles and clusters out to the map's edge, with a fog model that does not depend on the far plane.
Status: DRAFT written 2026-11-02 from toddhow's request (the RDR2 goal), not approved, nothing built. Full loop when started (a renderer change, the projection and the fog every material uses, new baked data levels, a settings default). Every fact is cited from the code at the current commit or marked **verify**.
Builds on: `docs/tasks/0055-lod-s1-tile-model.md` (terrain tiles, layout version 1, frozen), `docs/tasks/0059-lod-s4-object-clusters.md` (clusters, layout version 0, draft), `docs/tasks/0060-lod-s6-streaming-core.md` (budgets and eviction), `docs/tasks/0061-lod-s8-background-baker.md` (the baker that will make the extra levels), and commit `ed53100fd3` (`[WorldLod] view distance`).

Inherited code touched (at the commit that starts each stage; writing this spec changes nothing): `apps/openmw/mwrender/rendermanager.cpp` (`effectiveViewDistance`, `updateProjectionMatrix`, `processChangedSettings`, line numbers 144, 2682, 2700 at `ed53100fd3`), `apps/openmw/mwrender/fogmanager.{hpp,cpp}` (`FogManager::configure`, `getFogStart`, `getFogEnd`), `components/lod/tileselection.*` (`pixelError`, `distanceToTile`, the view-distance cut), `components/lod/tilekey.*` (levels above 3 need no change), `apps/openmw/lodterrainbaker.*` and `lodobjectbaker.*` (the level lists; the object baker's `levelErrors` {128, 320, 960} and `mManifest.mLevels = {1, 2}`), `components/lod/clusterbuild.*` (classification thresholds), `apps/openmw/mwrender/lodterrain.*`, `lodclusters.*`, `components/render/depth.hpp`, `attachmentformat.hpp`, the fog function in the shader library (**verify** its file under `files/shaders/lib/`), `components/render/atmospheremath.hpp` (the horizon colour), the sky and cloud dome radius (**verify**), `components/render/simplewater.*` (far sea edge), `files/settings-default.cfg` `[WorldLod]`.

Inputs / outputs: new `[WorldLod]` settings, all off or inert by default: `map view` (bool, false), `far levels` (the highest tile level drawn, 3), `view distance` accepts `auto`; a fog/aerial-perspective formula with a C++ twin; terrain tile levels 4 to 6 and cluster levels 3 to 5 in the baker (manifest `levels` extended; layout version of terrain stays 1, clusters stay the draft 0 until toddhow approves); a debug readout of the selected tiles per level; no change to saves.

## What the code says today (research note 0029, with the derivations)

- **The map:** Morrowind + Tribunal + Bloodmoon land cells span x -28 to 24 and y -20 to 30 (level 1 tile bounds from the baked pack's keys), 1540 land cells in a 52 x 50 cell box of 8,192 units per cell: **425,984 x 409,600 units, diagonal about 591,000**. The farthest point from any camera in the box is at most that. Today's `[WorldLod] view distance` is 40,000 (7% of it).
- **Where the edge comes from:** `Lod::pixelError` accepts a tile when its geometric error at its distance is at most the threshold in pixels, but `evaluateTile` (`components/lod/tileselection.cpp`) discards any tile with `distance > camera.mViewDistance`, `updateProjectionMatrix` sets the far plane to `mViewDistance`, and `FogManager::configure` makes the linear land fog end at `viewDistance` (start at `viewDistance * (1 - fogDepth)`). Three limits, all equal to one number. Raising the number raises all three, which is what commit `ed53100fd3` does; none of them is a design property of the LOD.
- **Levels baked:** terrain tile levels 1 to 3 (571 tiles, 46 MB), clusters levels 1 and 2 (458 clusters, 157 MB, level 3 skipped because it only starts at about 75,000 units). The tile key allows levels up to 15 (`Lod::maxLevel`); a level L tile is 2^L cells on a side (65,536 units at 3, 262,144 at 5, 524,288 at 6).
- **Depth:** `D32_SFLOAT` and a reversed-depth convention is marked in `depth.hpp` and the shadow samplers. **Verify** that the main camera's projection and depth compare are reversed: the projection is a plain `vsg::Perspective` (`rendermanager.cpp` line 632). With near = 1 (`[Camera] near clip` default) and far 590,000 a non-reversed float depth would resolve about 600 units at 100,000 units; a reversed one under 1 unit.
- **Cost today (task 0054/0059, 1280 x 720, RTX 4070):** the whole far field at a 30,000 view is 188 draws and 123k triangles with clusters; the near live objects are the expense (28.2k draws, 2.11M triangles, 3.53 ms CPU in Balmora). The far field is not what a bigger view costs.

## Design, in plain language

1. **Take the far plane, the fog end and the tile range apart.** Three numbers instead of one: the *tile range* (how far tiles are chosen: the map's far corner, or `view distance`), the *far plane* (set from the tile range plus the sea/sky needs, with reversed depth required), and the *fog* (an exponential model with its own scale, not tied to either). With `map view` off the three stay the old single number and the game is unchanged.
2. **More levels, same code.** Add terrain tile levels 4 to 6 and cluster levels 3 to 5 to the baker; the selection already picks the coarsest level whose error is under the threshold at that distance (the distance at which a level is accepted is about 77.9 x its error at 720p, 117 x at 1080p, so a level reaches the far corner when its error is at least about 5,000 to 7,600 units). Terrain levels 4 to 5 (and 6 only if the 4 px setting needs it) cover the map in about 10 + 4 + 1 tiles; cluster levels 3, 4, 5 use errors from the same 2.5 to 3 times growth (about 960, 2,900, 8,700, **starting values to be calibrated**), with the "too small" classifier excluding everything that no pixel at that distance could show.
3. **Aerial perspective instead of the linear fog end.** Transmittance `exp(-beta * integral(exp(-h/H)) ds)` along the ray (a closed form for a straight line), blended toward the atmosphere's horizon colour (`atmospheremath.hpp`), with the weather's fog density raising beta so a storm still shortens the view from the ground. Starting values: H 2,000 to 8,000 units, beta so that T = 0.1 at 590,000 units at ground level; **tune by eye in stage c**. One shared function (GLSL include for the material shaders, C++ twin with tests as `volumemath.hpp` has) used by the default, terrain, cluster and water materials, so the sea and the land end in the same haze. With `[Fog] use distant fog` the old behaviour stays selectable.
4. **Distance means basic.** Tiles and clusters above `near levels` skip every near-field effect: no shadow sampling past the last cascade, no GI/AO/wetness/cover/SSS paths (**verify** the LOD materials already omit them), plain ambient from the atmosphere, no parallax, no normal-map-heavy paths. The cluster material stays the default pipeline variant with `LOD_FADE`.
5. **`view distance = auto`.** The tile range becomes the distance from the camera to the farthest corner of the baked pack's bounds (read from the manifest), capped by a `max view distance` setting for low-end hardware. The default stays 40000 until toddhow approves `auto`.
6. **Memory and streaming.** The whole-map view needs only the coarse levels resident; the streaming core (task 0060) keeps the near levels bounded. Pin the top levels (design doc section 5). A weak GPU lowers `far levels` or raises `terrain tile threshold`; section "Low-end" below.

## Stages (each its own commit; every stage with its frames viewed before it is committed)

- **a. Facts and the budget (no behaviour change).** Verify the depth convention (read the main pass's pipeline depth compare and the projection) and the sky/cloud dome radius; read tile geometric errors for levels 2 and 3 from the manifest; bake terrain levels 4 to 6 and cluster levels 3 to 5 into a test pack; run a flight to 25,000 units with the tile range forced to 600,000 and `[Rendering] frame counters` on; record draws, triangles, CPU median and p99, RSS and device MiB (the table in note 0029 section 6 with real numbers). **Go/no-go:** the far field costs at most 1.5 ms CPU and 150 MiB resident more than the 30,000 view, or the numbers are used to set the level count.
- **b. The three limits.** `map view`, `view distance = auto`, the tile range separate from the far plane, reversed depth confirmed or fixed; with the setting off the frame is identical (0 pixels differ, as for earlier stages).
- **c. Aerial perspective.** The shared fog function, its C++ twin and tests (monotonic in distance, T in [0, 1], T = 1 at zero distance, ray-height integral against numerical integration within 1%, weather raises beta), applied to terrain, objects, clusters, water and sky; frames at the six poses of the verification plan.
- **d. The baker levels** (with task 0061's baker): terrain levels 4 to 6, cluster levels 3 to 5 with calibrated errors, the manifest's `levels`, the staleness digest includes the level list.
- **e. Calibration and fallbacks.** Threshold per hardware tier, `far levels`, `max view distance`, the weak-GPU run (llvmpipe, note 0025) for correctness only.
- **f. Close-out.** Docs, the changelog row, the checklist of toddhow's own flight.

## Constraints

- **Off means unchanged.** `map view = false` and `view distance` numeric: no new code runs, no new file is opened, the frame at the benchmark pose is identical to the commit before (0.000 percent of pixels differ, the comparison used in tasks 0055/0059). With `enabled = false` for the whole world LOD, the same.
- **Determinism.** The baker's output for levels 4 to 6 is byte-identical across runs and across thread counts (the existing tile/cluster pack tests, extended).
- **Threads and allocation.** Selection and the fog uniforms are per-frame on the main thread: no allocation (reuse the tile state vectors, as `LodTerrain` does now); tile loads stay on the worker (`LodTerrain`'s loader).
- **No magic numbers.** H, beta, the level errors, the thresholds are settings or named constants with their source (this note).
- **Shaders.** The fog function is a GLSL include edit (existing materials stay GLSL, decision 0009); any new full-screen pass is Slang.
- **Licences and assets.** Baked packs of Morrowind are never committed (guardrail); no new dependency.

## Acceptance criteria

- [ ] The depth buffer is reversed for the main pass (read from the pipeline state, not assumed), or the far plane is documented as limited with the alternative chosen by toddhow.
- [ ] A frame at 25,000 units up over Vivec shows the whole island, Solstheim and the coast with **no visible edge, hole or fog wall** (toddhow's verdict on the frames).
- [ ] From the ground (Seyda Neen, Balmora) the horizon ends in haze at every weather; storms and ash still shorten the view.
- [ ] No crack or pop at a level boundary during a fast ascent from the ground to 25,000 units (a scripted flight), beyond the existing hysteresis behaviour.
- [ ] Cost: draws, triangles, CPU median/p99, RSS and device memory at the 25,000 pose recorded in the stage log; the far field adds at most 1.5 ms CPU and 150 MiB resident over the 30,000-unit view, or the stage-a numbers say what the budget becomes.
- [ ] The near field at the benchmark pose is unchanged with `map view` on (the difference image is below the tolerance the earlier LOD tasks used).
- [ ] With `map view = false` the frame is identical to the commit before, and the 32 Lua integration tests pass.
- [ ] Unit tests: the fog function and its twin, the tile selection at distances to 600,000 (levels chosen are monotonic with distance, hysteresis holds), tile keys of levels 4 to 6, the baker's determinism for the new levels.
- [ ] The weak-GPU run (software device, `far levels = 3`, `texture max size = 512`) starts and draws a frame at the 25,000 pose.

## Low-end hardware

`far levels` (3 means today's reach, 6 the whole map), `max view distance`, `terrain tile threshold` raised (16 or 32), `texture max size` capping the 256 px albedo, the aerial perspective stays on (a few ALU per fragment). A weak GPU lowers the level count first, then raises the threshold; no setting changes the near field. The software-device run proves it starts and the draws are valid; it says nothing about speed.

## Risks

- **Depth precision (section 4 of the note):** if the main pass is not reversed the far plane cannot exceed about 60,000 units without a visible near-clip change; the alternative is rendering the far field in a second depth range (two passes, a larger change). Stage a decides.
- **Sky and cloud dome clipped by the far plane or too small.** Stage a verifies; the fix is the dome radius from the far plane or infinite clip for the sky.
- **Far clusters at errors of 3,000 to 9,000 may be mostly empty** (the classifier removes everything below a few pixels), so the far field may be terrain only plus a few landmarks. That is acceptable ("basic"); an impostor or heightfield landmark layer is a later task (design doc S10).
- **Aerial perspective double-counting with the froxel fog** (`volumetrics`): the two must split the ray at the volume's far depth (**verify** in `volumegpu.hpp`).
- **Aspect of the map:** Solstheim shares the worldspace; the box includes empty sea. The tile range `auto` uses the pack's bounds, so it does not shrink for a smaller game; mods that extend the land (Tamriel Rebuilt) need the pack's bounds, not constants (they come from the baked manifest).
- **A very tall view and the active grid hand-off:** the near objects/live terrain are unchanged; a high camera sees the live chunk boundary as a visible seam unless the tile hand-off (task 0055/0059 fades) holds at distance (**verify** from altitude in stage a frames).

## Open questions for toddhow

1. Is the target the **whole Morrowind island from a height of about 25,000 units** (a Levitate/teleport flight), or also a camera in the sky much higher than the game allows? It sets the number of levels (4 to 5 vs 6).
2. **Default `view distance = auto` for everyone** once measured, or an explicit graphics option (Low, Medium, High, Whole map)?
3. **How much haze from the ground?** RDR2 hides the far field at about a few km on a clear day; Morrowind's own look is thick fog. Starting values give T = 0.1 at the far corner; an alternative is T = 0.3 at 100,000 units. Your taste decides H and beta.
4. **Are far objects needed at all** beyond a few landmark clusters (Vivec towers, Ghostgate, Red Mountain's rocks), or is "terrain with its albedo plus landmarks" the intended far look?
5. **Approve cluster layout version 0** as version 1 (the object clusters are still a draft; levels 3 to 5 are baked into a layout that might still change).
6. **Reversed depth:** if the main pass turns out not to be reversed, accept the larger change (it touches every pipeline's depth compare) or the split-range far field?

Out of scope: real-time terrain tessellation, ray-marched terrain, impostors (S10), grass at distance (S7), any change to the live cell grid or loading, Lua, saves, shadows past the existing cascades, night sky changes, sea rendering beyond using the shared fog, other games' worldspaces.

## Answers of toddhow, 2026-11-03

1. The reach is not a fixed distance. It follows the loaded game's world: the extent of the loaded worldspace (land cells and their bounds from the content files) decides how far the view can go, so a large future world (Project Tamriel, once it is released) fits without a settings change. The spec's fixed altitude target (open question 1) is dropped; the verification poses are taken relative to the loaded world's extent instead.
2. `view distance = auto` is accepted as the way to say this. Auto means: the farthest point of the loaded worldspace's land from the camera, capped by what the hardware budget allows (stage a measures the cost). A number still overrides it.
3. The haze follows Red Dead Redemption 2's aerial perspective: distance and altitude dependent, tinted toward the sky and sun colour, thick near the ground and thin at altitude (details tuned by eye in stage d; the starting values in the spec are guesses).
4. The goal is human vision at far distances: beyond the distance where an eye can tell detail apart, grass, single trees and small objects are not drawn as objects; they blend into the ground colour (baked into the far terrain's albedo and haze), so far land is basic terrain plus a small set of large landmarks. This answers open question 4 (far objects beyond landmarks are not needed) and shapes open question 7: the baker's coarse levels keep only objects whose projected size would still be visible, and everything else is represented by the terrain picture.

Still open: approval of cluster layout v0 as version 1 before baking more levels (5), whether the depth buffer is reversed (6, checked in stage a), and the exact budget cap for auto (the hardware tiers).

5. Approved by toddhow (2026-11-03): cluster layout version 0 becomes the frozen layout. Freezing it needs a small task like tile layout S1 (header version 0 to 1, version 0 packs refused, a re-bake byte-identical, the pinned layout tests updated in one reviewed change) before the coarser levels are baked.
6. Asked what "reversed depth" means; explained in chat. Stage a checks the code and decides.

6 (decision). toddhow chose the reversed depth setup (near = 1, far = 0, float depth), 2026-11-03. Stage a first checks what the main camera, the shadow cascades, the G-buffer, SSAO/GI/fog/cloud passes and the editor viewport use today; if any is not reversed it is converted in one reviewed change (every pipeline's depth compare and clear value, every shader that reads depth, projection matrices, the linear-depth reconstruction), with the full loop and an off-means-unchanged image comparison (rendered frames identical within noise before the far plane grows).
