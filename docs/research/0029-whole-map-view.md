# Research note 0029: seeing the whole map from altitude

Date: 2026-11-02. Status: research for task 0066 (draft). Nothing here is built. Every number is derived from the code at the commit named in the task or from the baked packs on this machine; a figure that is an estimate says so, and a fact not read from the code is marked **verify**.

toddhow's goal (2026-11-02, after seeing the fog edge at 40,000 units with `[WorldLod] view distance`): as in Red Dead Redemption 2, from a high enough vantage the whole map is visible. Distant land is basic, never a hard edge, and not "a mountain in full detail".

## 1. How big the map is

The terrain pack baked from Morrowind + Tribunal + Bloodmoon (`lodtest/user/openeden/lod/terrain.edenpak`, 571 tiles, 1540 land cells) lists its tile keys. Decoding them with the layout of `components/lod/tilekey.cpp` (`unpackTileKey`: level in bits 44 to 47, Morton-coded tile x and y below, cell = (tile << level) - 2^21):

| Level | Tile side (cells) | Tiles | Cell bounds x | Cell bounds y |
| --- | --- | --- | --- | --- |
| 1 | 2 | 414 | -28 to 24 | -20 to 30 |
| 2 | 4 | 118 | -28 to 24 | -20 to 32 |
| 3 | 8 | 39 | -32 to 24 | -24 to 32 |

One cell is `ESM::Land::REAL_SIZE` = 8,192 units. The land box is therefore **52 x 50 cells = 425,984 x 409,600 units** (the level 3 tiles cover 56 x 56 cells = 458,752 units square), and its diagonal is about **591,000 units** (about 8.4 km at the usual figure of 1 unit = 1.43 cm, 117 m per cell; a convention, not read from the code). That box includes Solstheim, which lies in the same worldspace. Only 1540 of the 2,600 cells in the box have land, so a long view over the sea sees mostly water and empty horizon.

The camera can be anywhere in that box, so the farthest point it may need is the far corner: up to about 590,000 units away, not the 40,000 of today. `[WorldLod] view distance` (default 40000, `settings-default.cfg` line 1651) is 7% of that.

## 2. Tile levels and what covers the map

The key allows levels 0 to 15 (`Lod::maxLevel`, 4 bits). Baked today: 1 to 3 (terrain), 1 and 2 (clusters, level 3 is skipped by default because "level 3 starts at about 75,000 units", `lodobjectbaker.cpp` line 234).

| Level | Side (units) | Tiles needed for this map (bounding box) |
| --- | --- | --- |
| 3 | 65,536 | 39 baked |
| 4 | 131,072 | 4 x 4 at most, about 10 with land (estimate from the level 3 bounds) |
| 5 | 262,144 | 2 x 2 |
| 6 | 524,288 | 1 to 2 x 2 (the map straddles the origin cell lines, so up to 4) |

Levels 4 to 6 need no code change in the key (`Lod::tileContainingCell`, `tileOriginCell` shift by the level). What they do need: the terrain baker's level list (`LodTerrainBaker`, now 1 to 3) and a coarser geometry/albedo budget: a level 3 tile samples the land at a fixed grid (design doc 3.x, layout version 1 header, 88 bytes, task 0055), so at level 6 the same grid spans 524,288 units: one sample per 4,096 units at 128 samples per side (**verify** the grid size from `components/lod/terraintile.*` `TileHeader`), which flattens Red Mountain's ridge and the coast. The geometric error of a level grows roughly with the cell spacing; measured: level 1 median 190, p95 467, max 1,136 units (task 0055 log). Levels 2 and 3 are in the pack manifest (**read them in stage a**, not guessed here).

## 3. How the selection already scales

`Lod::pixelError` (`components/lod/tileselection.cpp`): `pixels = geometricError * viewportHeight / (2 * distance * tan(fovY / 2))`, distance clamped to at least 1 and measured to the tile's box (`distanceToTile`). A tile is used when `pixels <= threshold` (8 px default, `thresholdLow`), with a return below 0.7 of that (`hysteresisShare`), and it is skipped when its distance exceeds `camera.mViewDistance` or it is outside the view cone (`inViewCone`). So two things limit the far view and neither is a design limit:

1. `distance > camera.mViewDistance`: the tile is simply not drawn. This is the edge toddhow sees.
2. The set of baked levels: when the camera is far from a level 3 tile its error divided by distance is far below 8 px, but a *coarser* tile that would cover the same area cheaper does not exist.

The distance at which a level of geometric error E is accepted (8 px, 720p, 60 degrees) is about 77.9 E units (task 0059 derived 128, 320, 960 for 9,976, 24,941, 74,824). At 1080p the factor is 117. So to be accepted at the far corner (590,000 units) a level needs E of about 7,600 (720p) or 5,000 (1080p). The object cluster levels went 128, 320, 960 (x2.5, x3): extrapolating gives roughly 2,900 at level 4 and 8,700 at level 5. Two more levels (4 and 5) are enough for the objects; terrain needs levels 4 to 5 (maybe 6 for a 4K, 4 px setting). These are extrapolations, not measurements.

## 4. Far plane and depth precision

- `RenderManager::updateProjectionMatrix` (`apps/openmw/mwrender/rendermanager.cpp` line 2682) sets `vsg::Perspective` near = `[Camera] near clip` (default **1**, `settings-default.cfg` line 10) and far = `mViewDistance`. `effectiveViewDistance()` (line 144) is the larger of `[Camera] viewing distance` and `[WorldLod] view distance` when the world LOD and distant terrain are on.
- The depth buffer is `VK_FORMAT_D32_SFLOAT` (`components/render/attachmentformat.hpp`), and the code marks a reversed depth convention (`components/render/depth.hpp`: `nearDepth = 1`, `farDepth = 0`; shadow samplers compare with `VK_COMPARE_OP_GREATER`). **Verify** that the projection matrix the main camera uses is really reversed: `rendermanager.cpp` creates a plain `vsg::Perspective` and I found no reversed-matrix subclass in `apps/openmw/mwrender` or `components/render` (it may live in the VSG fork or in a pipeline state `depthCompare`); read the depth compare op in `components/pipeline` before stage a.
- Why it matters: with a standard (not reversed) float depth, near 1 and far 590,000, the depth resolution at 100,000 units is about z^2 / near * 2^-24 = 10^10 * 6e-8 = **600 units**, enough to make far tiles z-fight with each other and with sea and fog planes. With reversed float depth the resolution is about z * 2^-24 = **0.006 units per unit of range at 100,000**, i.e. under 1 unit at the far corner: no problem. So: reversed depth is a precondition; if it turns out not to be on for the main pass, the far plane cannot be raised past about 60,000 units without raising `near clip` (a visible nearby clipping change) or splitting the view in two depth ranges.
- The camera and the view matrix are double (`vsg::dmat4`), the vertex data of tiles are tile-local (task 0055/0059), so float vertex precision at 500,000 units from the origin (float spacing 0.03 units at 2^19) is not a problem.

## 5. The edge and the fog

`FogManager::configure` (`apps/openmw/mwrender/fogmanager.cpp` lines 46 to 70): without `[Fog] use distant fog` the land fog is linear, **start = viewDistance * (1 - fogDepth)** and **end = viewDistance**, so the fog reaches full strength exactly at the far plane, which with a 40,000 view is the edge toddhow sees. With `use distant fog` the numbers come from settings (`mDistantLandFogStart/End`).

For a whole-map view a linear fog tied to the far plane cannot work: at 590,000 the whole near field would be white. Aerial perspective must be an exponential extinction in distance with a height term (thick air near the ground, thin above), the model used by RDR2-style open worlds and by Hillaire's sky work:

- transmittance `T = exp(-integral of density(h) ds)` with `density(h) = beta * exp(-h / H)` along the view ray (analytic for a straight line: it needs only the camera height, the point height, the distance and the ray's vertical component), plus an inscatter colour from the atmosphere sky (the colour the sky has at the horizon in the sun's hour: `Render::Atmosphere`, `components/render/atmospheremath.hpp`, already used for fog and sky colour with `atmosphere look`).
- Blend `colour = surface * T + horizonColour * (1 - T)`; at the far corner T is small, so the land melts into the sky colour and the geometry's end is invisible.
- Parameters: a ground scale height H (starting guess 2,000 to 8,000 units, **tune by eye**), a beta chosen so T = 0.1 at the far corner at ground level (beta about 2.3 / 590,000), raised by weather (storm and ash raise beta, the existing weather fog values map to it so a storm still limits the view) and by `[Fog]`/the weather record's fog density. From 10,000 units up the ray goes mostly through thin air and T stays high, which is what makes the whole map visible from altitude and hazy from the ground.
- Where: the lighting code that applies fog is GLSL (`files/shaders/lib/...`: **verify** the fog function's name and file; vsgopenmw's `fog` in the default and terrain fragment shaders). Decision 0009: new shaders in Slang, existing GLSL stays; a fog change that must be in every material's fragment shader belongs in the GLSL include, with the formula in a header shared with a C++ twin for tests (as `volumemath.hpp`/`volume.glsl`). A screen-space pass would need the stored linear depth (`gbuffer`, R32F, `components/render/gbuffermath.hpp`), which exists only with `gbuffer` on.
- With the weather volumes (`volumetrics`, froxel fog `volumemath.hpp`) the fog is a local effect over the first N metres; the aerial perspective takes over beyond the froxel far plane (the volume's own far depth, `sliceDepth`) and the two must not double up: exclude the part of the ray inside the volume (**verify** the volume's far limit in `volumegpu.hpp`).

## 6. What it costs: budget from the S0 numbers

Measured (research note 0025; RTX 4070, i7-13700K, 1280 x 720; Balmora; census is an upper bound with no culling):

| View | Distant stuff | Draws | Triangles | CPU median | RSS | Device |
| --- | --- | --- | --- | --- | --- | --- |
| 7168 | none | 7.0k | 0.41M | 2.24 ms | | |
| 30000 | terrain + object paging | 33.8k | 2.42M | 3.87 ms | 793 MiB | 252 MiB |
| 30000 | tiles + clusters (task 0059) | 28.2k | 2.11M | 3.53 ms | 881 MiB | 301 MiB |
| 30000 far field alone, clusters | | 188 | 123k | | | |

So the far field is cheap (188 draws, 123k triangles); the cost is the **live** object paging near the camera, which does not grow with the view if the live zone is held constant. For a whole-map view the far field comes only from tiles and clusters. Per-view estimate (not measured): selection by pixel error keeps the number of tiles per level about constant (the ring around the camera at each level), so with six levels about 60 to 100 terrain tiles and 12 per cluster level (the far ring at 30,000 was 11 clusters, 272 draws, 145k triangles, so about 25 draws per cluster): **at most about 2,500 cluster draws and about 1M triangles** for six levels, in practice far fewer because the near levels already count in the 30,000 case. That is **estimate**; stage a measures it. Memory: every tile and cluster the view touches must be resident. Today the packs hold everything (46 MB terrain, 157 MB clusters); the coarse levels are small by construction, but the *level 1 and 2 clusters are the bulk* and are only loaded near the camera. The whole-map view needs levels 4 to 6 only (a few MiB each, **estimate**); stage a measures it, and the streaming core (task 0060) is what keeps the near levels bounded.

Texture: the terrain tile albedo is 256 x 256 RGBA8 at levels 2 and 3 (task 0055 S1); at level 6 the same 256 px spread over 524,288 units is a texel of 2,048 units: the map looks like a satellite picture at 1/64 detail. That is acceptable for "basic" land, and the textures are 0.25 MiB each (4 tiles at level 5 = 1 MiB).

## 7. What must not apply at distance

- **Shadows:** the cascades end at their own range setting (`[Shadows]`, unchanged); far land is lit without shadow maps, which is correct and cheap. **Verify** that a tile or cluster fragment past the last cascade is lit and not darkened by a clamped lookup (the shadow border colour is "lit": `components/view/shadow.cpp` line 142).
- **Global illumination, AO, wetness, puddles, snow/ash cover, SSS:** all are near-field effects; they sample a baked per-cell field (`openeden.skyvis`, probes) or screen-space data. The tiles and clusters must use the plain ambient from the atmosphere and skip these paths (each is behind its own setting and, in the shaders, a material define: **verify** that the LOD materials do not define them; the cluster material is a default-pipeline variant with `LOD_FADE`).
- **Clouds and sky:** the sky dome is drawn at the sky's own distance and must not be clipped by the larger far plane (the cloud dome sits inside the far plane today: **verify** at 590,000, the dome radius must be set from the far plane, or its clip made infinite).
- **Water:** the sea is a plane drawn out to the far plane; its reflection is a half-resolution pass (`simplewater`): reflection distance is small by design, keep it. The far sea edge needs the same aerial perspective, so water uses the shared fog function (**verify**).

## 8. Low-end hardware

The option set follows the design doc section 8 (graphics options): `[WorldLod] map view` (the whole-map mode, off by default until measured), `far levels` (the highest level baked and drawn: 3 for a weak GPU, 6 for the full map), `terrain tile threshold` (raise to 16 or 32 on weak hardware: fewer, coarser tiles), `texture max size` (already caps the 256 albedo), and the aerial perspective is a handful of ALU per fragment so it stays on everywhere. The weak-GPU emulation in note 0025 (llvmpipe under Docker, `texture max size = 512`) is the check that the setting does not crash; it says nothing about speed.

## 9. A verification plan with frames

Frames are captured with `--auto-screenshot N --auto-quit M` (the project rule: view a frame before committing anything that can affect rendering), at fixed poses so they can be compared across commits:

1. Balmora, 3,000 units up, looking at Red Mountain and at the coast (the pose of the existing `s_h90`, `s_n90`, `s_c90` frames).
2. Red Mountain's rim, 4,500 units up (the highest ground), looking over the whole island.
3. 25,000 units up above Vivec (a levitation/teleport pose set by the console or a test setting), looking down and looking at the horizon: the whole island should be in frame with no hole, no edge and no fog wall.
4. Sea level at Seyda Neen looking toward the island and toward open sea: the haze must end the land softly, and the sea must not end at a line.
5. Solstheim from the island, and the island from Solstheim.
6. The weather set: clear, overcast, rain, ash storm, blight storm: the fog density per weather must still bound the view from the ground.

Checks per pose: an image diff of the near field against the same pose with the whole-map mode off (the near field must be unchanged to within the existing tolerance), the draw and triangle census (`[Rendering] frame counters`), frame time (`tools/benchmark.sh`), RSS and device memory, no crack where two levels meet (zoom on the level boundaries), no pop beyond the hysteresis band during a fast ascent from the ground to 25,000 units (a flight script like the one used for the cell-load hitch note 0026), and a night pose and a sunset pose to check the haze colour against the sky.

## 10. Questions that this note leaves

- Is the main camera's depth really reversed (section 4)? If not, the plan changes (a far plane split or a raised near clip).
- Can the cluster baker's levels 4 and 5 be built at a size that is useful (what is left of the objects at error 3,000 and 9,000: the classifier's "too small" exclusion grows from 15.5k to 46.8k objects between levels 1 and 2 already)? A level 4 and 5 that holds only towers, big rocks and the Vivec cantons might be all the player expects; an **impostor** or a heightfield-only far field is the alternative.
- How much haze is "basic"? A taste decision (section 5 parameters).
