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

## Stage log

### Stage a, 2026-11-03: facts and the budget (no behaviour change; the commit that adds `components/render/depthmath.hpp`)

**Depth is already reversed everywhere; nothing was converted.** Read from the code (VSG sources are those fetched into `build-jolt/extern/fetched/vsg`, v1.1.16):

| Site | What it does | Reversed? |
| --- | --- | --- |
| Main camera projection, `RenderManager::updateProjectionMatrix` (`rendermanager.cpp`, `mProjectionMatrix` is a `vsg::Perspective`) | `vsg::perspective` in `vsg/maths/transform.h`: "Reverse depth perspective matrix, convention 1 to 0": depth `n (f - d) / ((f - n) d)`, 1 at near, 0 at far | yes |
| Pipelines' depth compare | `Pipeline::Graphics` and `Pipeline::terrain` build a `vsg::DepthStencilState` and set only `depthTestEnable`/`depthWriteEnable` (`components/pipeline/graphics.cpp:452`, `terrain.cpp:142`); VSG's `depthCompareOp` default is `VK_COMPARE_OP_GREATER` (`vsg/state/DepthStencilState.h:30`). No code in `components` or `apps` sets another compare op | yes (GREATER) |
| Main and HDR render graphs' depth clear | `setClearValues(color)` keeps VSG's default depth clear `{0.0f, 0}` (`vsg/app/RenderGraph.h:66`; `rendermanager.cpp:813, 1984`; the HDR graph copies it, `hdrtarget.cpp`) | yes (0 = far) |
| Shadow cascades | `components/view/shadow.cpp:144-169`: sampler `compareOp = GREATER`, border colour opaque black ("reversed"), graph clear `Render::farDepth` (0); `components/render/cascade.hpp:52-54` frustum corners use `nearDepth = 1`, `farDepth = 0` (`components/render/depth.hpp`); GLSL `lib/view/shadow.glsl:62` `zBlocker > zReceiver` | yes |
| Precipitation occluder | `precipitationoccluder.cpp:37, 49`: clear `farDepth`, black border (reversed); `occlusionbox.hpp:24` says "reversed depth" | yes |
| Dummy depth images | `components/view/descriptors.cpp:56, 73`: filled with `farDepth` | yes |
| Sun disc | `sky/sun/main.vert:15` `gl_Position.z = 0.001` (farDepth + epsilon), same in the Slang ports | yes |
| Water volume | `water/main.vert:24-31` uses `nearDepth`/`farDepth` for the frustum corners | yes |
| G-buffer, AO, velocity, SSS, tonemap | store and read the LINEAR depth (`-viewPos.z`, `main.frag:554`, `terrain/main.frag:373`), a colour output, not the depth buffer: no convention at all | n/a |
| Reconstruction from the depth buffer | `lib/view/screencoord.glsl` (`screenCoordToViewPos`, used by the water's reflection/refraction and soft particles in `default/main.frag:542`) and `lib/view/depthbuffer.glsl` (`depthBufferValueToViewZ`, the screen-space ray tracer) with the float inverse projection: the numbers below | yes, accurate (table below) |
| Fullscreen passes (post-process, AO, bloom, tonemap, velocity, MyGUI) | `depthTestEnable = false` | n/a |
| `fx` post-processing, `components/fx/vulkanpass.cpp:19` | `OMW_REVERSE_Z 1` | yes |
| Editor viewport | `apps/opencs/view/render/scenehost.cpp:253, 445` use `vsg::Perspective`; `picking.hpp:60-64` checks `projection(2, 2) > 0` for reversed | yes |
| Volume fog and clouds (Slang) | build rays from `invProjection` and the froxel slices from distance, not from depth-buffer values | n/a |

So toddhow's choice (reversed, float D32) is what the renderer has had since the vsgopenmw import; stage a made no change to any render file and the frames before and after are the same program (0.000 percent difference by construction). Not checked by reading: the depth of the MyGUI and the map/preview views (they do not test depth).

**Precision** (`Render::DepthMath`, `components/render/depthmath.hpp`, 11 tests in `apps/components_tests/render/testdepthmath.cpp`; near 1, far 600,000; the smallest change of distance that changes the stored float depth; the last column is the error of the shaders' reconstruction with the float inverse projection):

| Distance | Reversed (now) | Conventional 0..1 (for comparison) | Float inverse reconstruction error |
| --- | --- | --- | --- |
| 100 | 0.00001 | 0.001 | 0.0000 |
| 7,168 | 0.00075 | 3.06 | 0.0005 |
| 40,000 | 0.0029 | 95.8 | 0.0000 |
| 100,000 | 0.0091 | 598 | 0.0000 |
| 300,000 | 0.0102 | 5,455 | 0.031 |
| 590,000 | 0.0006 | 46,221 | 0.0625 |

All in world units. A far plane of 600,000 is fine for depth: no second depth range, no infinite projection needed. The near clip (default 1) could rise to 10 with the same result.

**Cost of the whole-map view with the baked levels** (Balmora start cell, camera at (-20480, -12288, 25000) looking down at 35 degrees, 1280 x 720, RTX 4070, `hdr` and `pbr` on, terrain tiles levels 1 to 3 and clusters levels 1 and 2, median of the last 150 frames, `[Rendering] frame counters`; the machine was shared with other agents, so wall times are rough; the draws and triangles are the census upper bound):

| `[WorldLod] view distance` | wall ms median / p99 | draws | triangles | resident MiB | device MiB |
| --- | --- | --- | --- | --- | --- |
| 40,000 | 3.46 / 5.09 | 38,175 | 3.0 M | 1,107 | 316 |
| 100,000 | 3.66 / 5.08 | 111,360 | 8.9 M | 1,535 | 492 |
| 600,000 | 4.17 / 13.07 | 29,818 | 6.3 M | 6,897 | 3,047 |

Frames: `I:\Projects\OpenEden-work\depth\hi_a.png` (40,000), `hi_c.png` (100,000), `hi_b.png` (600,000), ground poses `gr_a.png`, `gr_b.png`. Findings from viewing them:
- **The sky is not clipped by the far plane** (ground pose, 600,000 against 40,000: same sky, same horizon; the dome fits inside the plane).
- **No depth artefacts** (no z-fighting, no flicker) at 600,000, as the table says.
- **The land fog follows the far plane** (`FogManager::configure(viewDistance)`), so at 600,000 the haze is gone: crisp dark terrain to the coast, no atmosphere (stage c's job). At 40,000 and 100,000 the fog hides everything past a few thousand units from this height.
- **Open water far away is speckled with blue and white noise at 600,000** (top corners of `hi_b.png`). Not depth precision (the table). **Unverified hypothesis:** `water/main.frag:97` passes `scene.zFar` as the screen-space reflection's maximum distance, which is now 600,000 (`reflect.glsl`'s stride and jitter follow it). A stage c item: cap the reflection distance, and fade the water to the haze colour with distance.
- **At 100,000 white wavy strips follow some far ridges** (upper right of `hi_c.png`), probably water planes or tile edges; not investigated.

**Go / no-go.**
- Depth and far plane: **go**; nothing to build.
- The whole-map view with today's baked levels: **no-go**: 6.9 GiB resident and 3.0 GiB of device memory (against about 1.1 GiB and 0.3 GiB at 40,000), p99 13 ms (against 5 ms), because everything out to 600,000 is chosen at levels 1 to 3 and nothing is evicted. This is the budget stage d (levels 4 to 6) and the streaming core (task 0060) have to bring down: the target remains the spec's far field of at most 1.5 ms CPU and 150 MiB resident over the 30,000 view.
- `view distance = auto` must not become the default before those two; the cap setting (`max view distance`) is needed from the start.

**Not verified:** a true before/after pixel comparison (not needed, no render code changed); `fast ascent` pop at level boundaries (needs levels 4 to 6); the sky dome radius value (confirmed only by frames); the shadow cascades and the editor viewport at 600,000 (read, not run); the weak-GPU run; the clouds and volumetric fog at 600,000 (not run; both build rays from the projection and have their own reach limits: `volumegpu.hpp` `farDistance(fogEnd)`).

### Stage b, 2026-11-03: the limits taken apart (the commit that adds `components/lod/viewlimits.*`)

**What changed.** The one view distance is now four numbers, computed by one pure function (`Lod::computeViewLimits`, `components/lod/viewlimits.*`) and kept in `RenderManager` (`mViewDistance` = the paging range, `mTileRange`, `mFarPlane`, `mFogRange`, `apps/openmw/mwrender/rendermanager.cpp`, `applyViewLimits`):

| Number | Used for | Before |
| --- | --- | --- |
| tile range | `LodTerrain` and `LodClusters` camera (`camera.mViewDistance`: tiles beyond it are skipped) | the one number |
| paging range (`getViewDistance()`) | `mTerrain->updateView`, `DistantObjects::update`, the map, the preloader (`scene.cpp`) | the one number |
| far plane | `updateProjectionMatrix`, `setDepthRange`, the post-process `far` | the one number |
| fog range | `FogManager::configure` (land fog ends there; stage c gives the fog its own model) | the one number |

Rules (tests in `apps/components_tests/lod/testviewlimits.cpp`): with the world LOD inactive (`enabled`, `[Terrain] distant terrain`, not a bake run) all four are `[Camera] viewing distance`; active with `map view` off all four are the larger of that and `[WorldLod] view distance` (`auto` counts as 40000), exactly the rule of commit `ed53100fd3`; with `map view` on the tile range reaches `view distance` (a number, not capped) or, for `auto`, the diagonal of the box of the loaded world's land cells capped by `max view distance`, the far plane and fog follow the tile range, and the paging range stays at the old number. `auto` is camera independent (the diagonal is the farthest any camera can be from land) so the projection does not change every frame; the land cells come from the ESM store in `RenderManager::setStore`.

New settings (in `files/settings-default.cfg` and so in `defaults.cfg`, which the editor loads): `[WorldLod] map view = false`, `max view distance = 600000`; `view distance` is now text (`40000`, any number, or `auto`), parsed by `Lod::parseViewDistanceSetting`; text that is neither logs one warning and counts as 40000. A Lua `camera.setViewDistance` still sets all four numbers until a setting changes (`mViewLimitsOverridden`).

**Tests, written before the code** (`testviewlimits.cpp`, 21 tests; written from this spec while `viewlimits.hpp` did not exist, so they were never run red: the first build compiled them against the new header and all 21 passed): parsing (`auto` in any case, numbers, 15 refused texts), the land extent from cells (empty, one cell, negative and positive, order independent) and its diagonal, the limits in every combination (world LOD off, map view off, `auto` with and without an extent, the cap, a number overriding `auto`, invariants over a grid of inputs, purity), and the tile selection out to 600,000 units (the accepted level is non-decreasing with distance from 20,000 to 600,000 and reaches level 5 at the far end; a tile is skipped beyond the tile range and chosen inside it with no far plane involved; hysteresis at 0.7 holds for a level 4 tile at 60,000 to 560,000; selection is a pure function). Not covered here: tile keys of levels 4 to 6 and the baker's determinism for them (stage d). Windows: `components-tests` 4735, `openmw-tests` 859, `eden-studio-tests` 578, all pass.

**Frames: map view off is the same picture as the commit before.** Method: a second `openmw.exe` was built from the stage-a commit's `rendermanager.{cpp,hpp}` (the old one-number code) with the same settings category, then both were run with the same config copy (`I:\Projects\OpenEden-work\mapview`, a fresh user dir, `--skip-menu --start Balmora --script-run` with `tcl` and, for the high pose, `setpos` to (-20480, -12288, 25000) looking 35 degrees down, frame 300, 1280 x 720, `hdr`, `pbr`, `object clusters`). Differences by ffmpeg (`psnr` average, `blend=difference` maximum of luma):

| Pair | PSNR | max luma difference |
| --- | --- | --- |
| old exe against itself, high pose, `view distance = 40000` (run-to-run noise) | 72.2 dB | 48 |
| old against new, high pose, 40000, map view off | 70.5 dB | 45 |
| old against new, ground pose, 40000 | 68.7 dB | 1 |
| old against new, ground pose, 600000 | 66.5 dB | 1 |
| old against new, `enabled = false` (the world LOD off), second pair | identical (inf) | 0 |
| new with `view distance = auto`, map view off, against old 40000, high / ground | 78.1 dB / 66.9 dB | 19 / 1 |
| old against itself and new against itself, high pose, 600000 | 32.7 / 29.1 dB | 156 / 159 (the tiles load asynchronously: the pose is not repeatable at that distance) |

So the frames are identical within the run-to-run noise of the same binary; they are not byte-identical with the world LOD on (the noise is the terrain, clouds and water animating on real time, and at 600,000 the order tiles finish loading); with the world LOD off the pair is byte-identical. `map view = true` with a number gives the same frame as `map view = false` (the number sets all four).

**Map view on** (`view distance = auto`, map view on, the same high pose, `terrain tile threshold` 8, clusters on): the whole land out to the coast and the second island are drawn, from 25,000 units up, with no fog (the fog end is the 590,000 tile range: stage c adds the haze) and no gap at the edge of the live terrain in this frame. The frame is dark because the pose looks down and the fog no longer lightens it; the lighting is unchanged. The cost with today's levels is the stage-a table (6.9 GiB resident at 600,000): `auto` is not the default.

**Stale-exe lesson applied:** `eden-studio` was rebuilt after the defaults changed and starts (`defaults.cfg` carries `map view`, `max view distance`). **Not verified:** the Lua tests (run at the next Linux gate), a frame with `[Cells] incremental cell build` on, `view distance = auto` in the editor viewport (the editor has no world LOD), a Lua `setViewDistance` call at run time, weak GPUs, and the launcher (which does not show these settings).

### Stage c, 2026-11-03: aerial perspective (the haze of the whole-map view)

**What it is.** With `[WorldLod] map view` on (and `enabled`, `[Terrain] distant terrain`, and the new `haze`, default true), the land, objects, tiles, clusters and water fade into the air's colour by how much air the ray crosses, in place of the linear fog end (which stage b had moved out to the tile range, 590,000, so it did nothing). The air's density is `beta * exp(-h / H)` with `h` the height above world z = 0 and `H` the scale height; a straight ray from the camera at height `h0` with direction z `dz` and length `s` crosses the optical depth `beta * exp(-h0/H) * (1 - exp(-s dz / H)) / (dz / H)` (the closed form; a ray that descends to height 0 adds `beta` per unit from there on), and the fog's share is `1 - exp(-depth)`. Thick near the ground, thin at height: from 25,000 units up the ray to the far sea crosses little air.

**One shared function.** Everything that fogs goes through `applyFog` in `files/shaders/lib/view/fog.glsl` (objects and clusters through `default/main.frag`, terrain and tiles through `terrain/main.frag`, water through `water/main.frag`), so the haze is written once, there. Its C++ twin is `components/render/aerialmath.hpp` (`Render::Aerial`: `baseBeta`, `weatherBeta`, `fogDepthFromRange`, `opticalDepth`, `transmittance`, `fogAmount`, `sceneFogRange`, `isAerial`); a test reads the shader's three constants back. The sky keeps its own colours; the cloud and sun shaders do not use `applyFog`. It is GLSL, not Slang: the object, terrain and water pipelines are GLSL and take their fog from a GLSL include; a Slang function could not be included there (decision 0009 applies to new shaders, and this is an edit of an existing include).

**How the shaders are told (no new uniform).** `Scene` cannot grow (past 640 bytes interiors brightened) and `fogColor.w` is the linear-view marker. The scale is stored NEGATIVE (`View::Scene::setFogRange(start, end, aerial)` -> `Render::Aerial::sceneFogRange`): its size is the linear fog's, so the weather's fog depth is read back from the range (`depth = 1 - start / end`; FogManager stores start = range * (1 - depth), end = range). Only the main view's exterior above water sets it (`mRenderModes[Render_Sky]`, not under water, not `[Fog] use distant fog`, not a bake run, no Lua `setViewDistance` override), and only when the shaders were built with it (`fog.settings`: `cAerialHaze`, `cAerialVisibility`, `cAerialScaleHeight`, `cAerialWeatherGain`, read at start, kept as `RenderManager::mAerialShader`). Interiors and under water keep the linear fog. The volumetric fog and cloud compute shaders read `fogScale <= 0` as "no fog", so with the haze on the volumetric fog is off (not supported together yet: the aerial path is checked first in `applyFog`).

**Weather.** Beta is `baseBeta(haze visibility) * exp(gain * clamp(depth - 0.7, 0, 0.42))`: clear, cloudy and overcast (Land_Fog_Day_Depth 0.69 to 0.72) have the base density; rain 0.8, storm and fog 1.0, blizzard 2.8 (capped). Vanilla's linear fog ends at the view distance in every weather and only its start moves, so the settings were calibrated to the vanilla feel (gain 10, cap 0.42: the distance that keeps 10 percent of the light, horizontally at the ground: clear 100,000, rain about 30,000, storm about 5,000, blizzard and ash about 1,500). The first try (gain 12, cap 0.5) made ash storms about 250 units deep: far too thick, seen in a frame, corrected.

**Settings** (`[WorldLod]`, defaults in `files/settings-default.cfg`, and so in `defaults.cfg`): `haze = true`, `haze visibility = 100000` (T = 0.1 for a horizontal ray along the ground in clear weather; 590,000 as first planned was invisible in every frame: a view from 25,000 units up crosses almost no low air, so it came down to 100,000), `haze height = 4000`, `haze weather gain = 10`. Starting values chosen by eye; tune further when toddhow has flown it.

**Tests, written first and run red.** `apps/components_tests/render/testaerialmath.cpp` (14 tests) was written against a stub header whose functions returned wrong values on purpose: 9 of 13 then failed, the other 4 passed vacuously (they compare relations that hold when the density is 0). Then the implementation: one test failed at first, my own bound: at 25,000 units up a horizontal ray gives a fog share of 0.0053, not under 0.001 as I wrote; the closed form is right and the test now compares against it. They cover: T = 0.1 at the visibility, T = 1 at zero distance, T in [0, 1] and falling with distance for 3 densities and many rays, the optical depth against Simpson integration (200,000 steps) for 240 rays within 1 percent (including `dz = 0`, tiny `dz`, up, down, a camera 80,000 up), a descending ray reaching the ground, thinner air from altitude, weather (base density at and below 0.7, rising with depth, capped, a storm sees less than a tenth of clear, a gain of 0), `fogDepthFromRange`, the shader's constants, and "off is exactly vanilla": `sceneFogRange(start, end, false)` returns `start` and `1 / max(0.001, end - start)` bit for bit for 30 combinations and is never marked; with it on the scale is negative with the same size. All 14 pass, with the 121 of `AerialMath*:LodViewLimits*:*Settings*`.

**Frames** (`I:\Projects\OpenEden-work\hazefog`, 1280x720, RTX 4070, `OPENEDEN_QUIET_WINDOW=1`, the baked terrain and object packs of `lodtest/user/openeden/lod`, a config copy). "lin" is the same `map view` with `haze = false` (the linear fog at the tile range). All frames were viewed.
- **From 25,000 units up over Balmora, looking north** (`F_b25k_n`, `h100_b25k_n`): the far ridges and the far sea pale into the sky colour and the horizon blends; `lin_b25k_n` is crisp to the horizon. At `haze visibility = 590000` (`haze_b25k_n`) the two are nearly the same: too weak.
- **From 3,000 units up looking over Balmora** (`h100_b3k_e`): the far trees and the sea at the horizon are lightly hazed, the town is clear.
- **Over the swamp near Vivec, 25,000 up** (`h100_v25k`): depth cueing across the far ridges, nothing washed out.
- **Weather at Seyda Neen** (`G_storm`, `G_ash`, `G_rain`; `F_storm_lin` and `F_ash_lin` for the linear fog under `map view`): the linear fog at the tile range shows NO weather (a storm or an ash storm looks like a clear day with rain or a red sky); the haze shortens the view as vanilla does: the storm hides the far trees and hills, the ash storm hides everything but the near house, rain is a mild haze.
- **Off is unchanged.** `map view` off (`enabled = false`; poses `F_off_ground`, `F_off_b3k_e`): the same exe was run with the OLD `fog.glsl` (the stage b file, copied over the build's resource copy and restored) and the new one: PSNR 37.1 dB (ground pose; NPCs and water move between runs) and 64.6 dB (3,000 up), max luma difference 129 and 28; two runs of the NEW shader against each other give 40.1 dB and 61.7 dB, max 115 and 26. So the old-against-new difference is the run-to-run noise. Not byte-identical, as in stage b (the game animates on real time): "0 pixels differ" cannot be shown with these poses; the code path is identical by the unit test above and by the diff (`mixFog` is the old `mix`; the aerial branch is behind `cAerialHaze`, a compile-time false).

**What looks wrong (not hidden by the haze).**
- **Far water speckle:** open water far away is a dotted white and blue noise out to the horizon in every `map view` frame, haze or not (`lin_b25k_n`, `h100_*`). The haze lowers its contrast near the horizon but does not remove it. It is a water shader problem (not studied here; the unverified hypothesis from stage a is that a water distance uses `scene.zFar`), so it needs its own task.
- **White strips on far ridges** (stage a, at 100,000) were not seen in these poses; not investigated.
- **Volumetric fog and the haze are exclusive** for now (see above).
- **Interiors and under water** keep the linear fog by design; a doorway between an interior and an exterior is a hard cut, as before.
- **Fog depth 0:** an exterior with `fogDepth == 0` stores `end = 3.4e38`, from which the shader reads depth 1 (maximum weather). No Morrowind weather has depth 0; a mod weather with 0 would be hazed as a blizzard. Not handled.

**Not verified.** A frame at 600,000 from 100,000 up (the highest taken was 25,000); weather transitions as a sequence; the horizon colour against far land at dawn and dusk (all frames are noon); water in a storm from altitude; the editor viewport (it never turns `map view` on); the Lua tests and the Linux gate (clang-format 14 was run in Docker on the changed files, the unit tests on Windows only); a weak GPU; the cost of the haze (an `exp` and a few multiplies a fragment on the fogged draws, no measurement taken).

Stage c's acceptance item "from the ground the horizon ends in haze at every weather; storms and ash still shorten the view" is seen at noon at Seyda Neen in rain, storm, ash and blizzard (`G_rain`, `G_storm`, `G_ash`, `G_blizzard`; the blizzard hides everything past the near house). The other items belong to later stages.
