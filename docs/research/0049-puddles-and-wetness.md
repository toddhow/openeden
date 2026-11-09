# Research note 0049: where puddles form, and how wetness should look (puddles and wetness, v2)

Status: DRAFT, docs only, 2026-11-09. Nothing built, run or measured. Written for toddhow ("We also need to work on how puddles and wetness look and are done. Is there a way we can figure out where puddles will form so they look real."). The task spec is `docs/tasks/0095-puddles-and-wetness-v2.md`.

Evidence rules: facts about the repository cite a file and function at the working tree of 2026-11-09 (branch `main`, `ba3f79b670` plus uncommitted work in other areas). Anything recalled from outside the repository, any number I did not measure, and anything about shader internals I did not open is marked **RECALLED-VERIFY** or **estimate**. No game asset was read, no game was run, nothing was built.

## 1. Short answer

Yes. Where water collects is a property of the ground's shape, and the shape is known: Morrowind's land is a height grid in the ESM (`ESM::Land`), and the static world is in the game's Jolt physics world. The standard tool is **depression filling** (priority-flood): pour water on a height grid and ask where it stays. That gives, for every point, how deep a puddle there can get (its **capacity**), which pits it belongs to, and how much ground drains into it. It is computed once from the user's own game data (a bake, like the LOD, light-probe and body bakes), stored as derived data, and used at run time by turning a single "how much water has fallen" number into a water level that rises from the lowest points, so puddles grow from the bottom of hollows, edges follow the ground's contours, and they shrink back the same way as it dries.

Two honest limits, both measured from the code, not guessed:
1. Morrowind's terrain has one vertex every 128 units (about 1.8 m; section 2.2), so a terrain-only bake finds hollows of a few metres, not cart ruts or the gaps between cobbles. Those need micro relief (section 5.4: blurred height from the `_p` height maps of task 0045, or a noise term, only inside a baked basin).
2. Static geometry (roads, bridges, flat roofs, steps) is not in the terrain grid. It has to be captured separately (section 4.3). That is the riskiest stage.

## 2. Inventory: what exists today

### 2.1 Where wetness comes from

- **State.** `MWWorld::WeatherManager::updateWetness` (`apps/openmw/mwworld/weather.cpp`, line ~838): if `[Rendering] wetness` is on, `mWetness` moves toward `Render::Wetness::target(!mResult.mRainEffect.empty(), mResult.mPrecipitationAlpha)` with `Render::Wetness::step(wet, target, gameSeconds, wetnessDryTime)` (`components/render/wetnessmath.hpp`): rise over `riseSeconds` = 20 s of game time, dry over `wetness dry time` (`defaultDrySeconds` 900 s of game time). It is 0 in interiors (`updateWetness(duration, paused, false)`, line ~954). Snow and ash do not wet. It is a function of weather type and the clock only; there is no notion of how much rain has fallen, of the ground soaking it up, or of evaporation depending on sun or shade.
- **To the renderer.** `RenderManager::setWetness` (`apps/openmw/mwrender/rendermanager.cpp`, line ~1901) stores it; the scene block carries it as `sceneData.ambient.w = 1 + mWetness * mWetnessStrength` (line ~2675; cover rides in the same alpha in steps of 2, see `wetSurfaceAt`). One float for the whole world.
- **Requirements** (`rendermanager.cpp` line ~584): `hdr` and `pbr`; puddles are the extra define `WETNESS_PUDDLES` (`graphics.wetnessPuddles`). Terrain gets the same through `terrainMaps.wetness` (line ~858).

### 2.2 What the shader does

`files/shaders/lib/material/wetness.glsl` (twin `wetness.slang`; C++ twin `Render::Wetness` in `components/render/wetnessmath.hpp`, which documents each constant):

- `wetSurfaceAt(worldPos, worldNormal)`: `raw = max(scene.ambient.w - 1, 0)` minus the cover steps; with `GI_SKY` it asks `skyVisUp(worldPos)` (`lib/view/skyvis.glsl`, task 0036) and multiplies by `wetExposure` (smoothstep from `wetCoverBelow` 0.2 to `wetCoverAbove` 0.6): this is the **shelter** test. Without a baked sky-visibility cell the exposure stays 1: everything outdoors is wet, including under eaves. Puddle: only if `s.wet > wetPuddleStart` (0.5), `s.puddle = wetPuddleMask(wet, worldNormal.z, wetNoise(worldPos.xy / wetPuddleNoiseCell))`.
- `wetPuddleMask`: `wetFlatness(normalZ)` (smoothstep 0.94 to 0.99, roughly under 20 degrees to under 8 degrees) times a smoothstep of a **value-noise** threshold at `1 - wetPuddleFill(wet)` where `wetPuddleFill` goes from 0 at wet 0.5 to `wetPuddleMaxCoverage` 0.4 at wet 1. `wetNoise` is a two-octave hashed lattice at `wetPuddleNoiseCell` 120 units.
- Application in `files/shaders/default/main.frag` (line ~295 onward): the puddle blends the view normal to `wetPuddleNormal` (up, tilted by `wetRipple`), multiplies colour by `wetDarkening` and `wetPuddleDarkening` (0.7), sets roughness toward `wetPuddleRoughness` (0.03), and adds `wetSkyReflection(...)` times `wetFresnel` times `wetPuddleReflection` (0.5) (line ~451). Skin and glowing surfaces are exempt (`wetKeep`). Terrain does the same in `files/shaders/terrain/main.frag`; groundcover and actors take the lit path of statics (task 0037 decisions D7, D8).
- **Sky reflection:** `wetSkyReflection` uses `giReflection` (the exterior sky image of task 0036 stage d, `lib/view/reflection.glsl`) when `giReflectionEnabled()`, else a fog-to-sky gradient. It reflects the sky only: no buildings, trees or the player.
- **Ripples:** `wetRipple` is procedural (rings on a 48 unit lattice, `wetRippleCell`, radius 16, period 1.1 s, `scene.rainIntensity`). It is not the water shader's ripple texture (`files/shaders/water/main.frag` includes `rainripples.glsl`; I did not open that file: **RECALLED-VERIFY** that the two look different).
- **Test profile:** `tools/testprofile/profile.cfg` line 81, `wetness = true` ("task 0037"). Debug view: `wetness debug` (yellow wet and open, white puddle, black covered).
- Numbers recorded by task 0037's stage log: +0.07 ms GPU in rain, none in clear weather (`docs/research/0017-wetness-cost.md`); tuning after the first frames: coverage 55 to 40 percent, patch 220 to 120 units, reflection times 0.5.

### 2.3 Other pieces that matter

- **Rain particles and shelter:** `PrecipitationOccluder` (`apps/openmw/mwrender/precipitationoccluder.*`) renders a top-down depth map of `Mask_Object` around the camera (`rendermanager.cpp` line ~1293: "does not draw terrain"), only while it rains; task 0037 D3 rejected it for shelter. It proves a camera-relative top-down capture exists in the renderer, which matters for the fallback in section 6.
- **Terrain heights:** `Terrain::Storage::getVertexData(lodLevel, bounds)` returns `heights`, `normals` and vertex colours; `Terrain::Storage::getHeightAt(worldPos, worldspace)` (`components/terrain/storage.hpp`); the ESM side is `ESM::Land` (`components/esm3/loadland.hpp`: `LAND_SIZE`, `REAL_SIZE` = `Constants::CellSizeInUnits`). The cell is 8192 units and the land has 65 vertices a side, so one vertex per 128 units; Morrowind's 1 metre is about 69.99 units (`CLAUDE.md`, `Misc::GameConventions` unit), so **a vertex every 1.83 m**. (The 65 and 8192 are from memory of `LandRecordData`; **RECALLED-VERIFY** the numbers in `loadland.hpp` before stage b.)
- **Baked LOD tile heights** (`components/lod/tilebake.*`, `terraintile.*`) are coarser than the live terrain; they are not an input here.
- **Static world:** the Jolt world owned by `MWPhysics::PhysicsSystem` (`apps/openmw/mwphysics/physicssystem.hpp`: `castRay(from, to, mask, group)`). The sky-visibility baker already ray-casts it: `apps/openmw/skyvisbaker.cpp`, `JoltSkyWorld` (uses `mPhysics.castRay(from, to, bakeMask)`), with cell-by-cell, resumable, deterministic baking (`openmw --bake-gi-exterior "<x>,<y>"`, task 0036).
- **Bake infrastructure:** derived data under `<user data>/openeden/` with a manifest, digest and staleness check (`docs/tasks/0055`/`0059`); task 0061 (draft) defines work units, a generation folder, `status.json`/`command.json`, and a unit table with key tags (`0x10` terrain tile, `0x11` cluster, `0x12` impostor atlas, `0x13` grass coverage); a puddle map is a new unit kind. Task 0058 is the launcher page (draft).
- **Texture height maps:** task 0045 (parallax) reads `_p` height maps; **RECALLED-VERIFY** how terrain layers expose them (I did not open the terrain parallax code).

## 3. Why today's puddles do not look physical

All of this follows from reading `wetPuddleMask` and its inputs; none was measured, but each is visible in the code.

1. **The pattern is not tied to the ground.** The only inputs are the surface normal and a noise of world X/Y. A puddle sits where noise is high on any flat enough surface: the flat top of a rock, a roof ridge, a step, the middle of a rise that is flat for 8 degrees, and nothing sits in the real hollow beside it. The same noise cell gives the same blob on every cell of the world.
2. **Coverage is a fraction of flat area, not a volume of water.** `wetPuddleMaxCoverage` 0.4 means about 40 percent of every flat region goes wet at wet = 1, whatever its area, depth, material or what drains into it. Real puddles are a few deep pools and many absent ones.
3. **Edges are noise contours, not terrain contours.** Water has a flat surface and its edge is where that plane meets the ground. Here the edge is a smoothstep of a noise value (`wetPuddleEdge` 0.06), so it ignores the slope of the bank; no wet-edge gradient, no shore.
4. **The water surface is not intersected with the ground.** A puddle is drawn as the same ground pixel with a flat normal and roughness 0.03; there is no depth, so no depth tint (Beer-Lambert darkening toward muddy brown), no refraction of the ground under it, no parallax, no ground poking out.
5. **No material.** Cobbles, packed dirt, sand and grass get the same darkening (`wetDarkening` varies only with roughness) and the same puddle chance. Water does not pool on sand or grass.
6. **Growth is a threshold sweep.** As wetness rises, a noise threshold moves, so puddles bloom everywhere at once and shrink by the reverse. Real puddles start in the lowest points, merge, and dry from the shallow edges in.
7. **Shelter depends on a bake that most of the world will not have.** Without a baked `gi exterior` cell (`wetExposure` with `dataWeight` 0) there is no shelter at all, and with it the field is 256 units coarse (task 0037 decision note), so a doorway recess is the limit.
8. **Reflections are the sky only** (see 2.2), so a puddle beside a wall shows sky where the wall should be; the sun highlight is absent.
9. **Ripples are an unrelated procedural lattice** with the wrong size relationship to the rain (fixed 48 unit cells whatever the rain intensity), shown on every puddle pixel including its shallow rim.
10. **One global float.** Wetness cannot say "this ground is soaked and that sand has drunk it"; and it is capped at wet 0.5 to start puddles, so a light rain never puddles and a heavy one puddles the same way.

## 4. The approach: compute where water collects

### 4.1 The model, in plain language

Treat the ground as a bowl-shaped landscape on a grid. If you poured water onto it evenly, water would run downhill and collect in pits. A pit holds water up to its **spill height**, the lowest point on its rim; beyond that it overflows into the next pit or runs off. The difference between the spill height and the ground in the pit is the **capacity** at each point. Computing spill heights for every pit at once is a solved problem: **priority-flood depression filling** (Barnes, Lehman and Mulla, 2014, "Priority-Flood: An optimal depression-filling and watershed-labeling algorithm for digital elevation models", Computers & Geosciences; **RECALLED-VERIFY** details). It starts from the edge of the map (and from the sea) and floods inward lowest-first; each new cell's filled height is the larger of its own height and the height of the cell it was reached from.

### 4.2 The algorithm (per region, on the CPU)

Inputs: a height grid `H` (floats, world Z), `n x m`, spacing `s` (default 64 units, section 4.5), a **sink mask** (texels where water leaves the model: sea and water bodies, texels beyond the loaded land, and the grid edge), and a **material class** per texel (section 5.3).

1. **Priority queue** keyed by height; ties broken by the texel index (a fixed total order, for determinism: CLAUDE.md "no iteration over unordered containers"). Push every sink and border texel with key `H`, mark it closed with `F = H`.
2. **Pop** the lowest texel `c`. For each of its 8 neighbours `q` not closed: `F[q] = max(H[q], F[c])`, mark closed, push with key `F[q]`. (This is the plain Priority-Flood; the epsilon variant that makes the filled surface strictly drain is used only for step 5.) Cost O(n log n) with a binary heap; Barnes also gives an O(n) form for integer heights, not needed here (**estimate**: 128 x 128 texels per cell, one cell and a halo of 1 cell is 9 x 16,384 = 147,456 texels, well under 100 ms per cell on one core; the whole 1,559-cell Morrowind exterior is about 25 M texels, a minute or two single-threaded; **estimates**, not measured).
3. **Capacity** `C[i] = F[i] - H[i]`, clamped to `capacityCap` (default 12 units, about 17 cm; deeper hollows are ponds, which the water shader's baked water level covers or which stay partly filled; section 9, risk R3). Water bodies (texels at or below a cell's water height) are sinks, so they get `C = 0` and no puddle.
4. **Pits.** Connected components (4-neighbour) of `C > 0`: each is a **puddle basin** with id, area `A` (texels), `Cmax = max C`, and pour height. Nested depressions inside a bigger one merge into one component in v1 (a known simplification; the exact answer is a depression hierarchy, Barnes, Callaghan and Wickert 2020, "Computing water flow through complex landscapes, Part 2", Earth Surface Dynamics; **RECALLED-VERIFY**; risk R2).
5. **Flow and catchment.** Run the epsilon-flood (`F[q] = max(H[q], nextafter(F[c]))` style, so flats drain), derive each texel's D8 flow direction toward its lowest-filled neighbour, accumulate upslope area. For each basin, `Acatch` = its own area plus all area draining into it from outside. Ratio `kappa = Acatch / A`, stored as `log2(kappa)` clamped to [-2, 3] in 8 bits. Flow directions are also kept for flow streaks (section 8) in 3 bits per texel.
6. **Flatness and slope** per texel from the grid gradient (not needed in the file: the shader already has the true surface normal; used by the baker only to decide "this texel is a plate").

What this gives that noise cannot: **where** (basins), **how deep at most** (`C`), and **how much water it takes to fill** (via `kappa`).

### 4.3 Capturing static geometry: roads, bridges, floors, multi-level

Terrain alone puts puddles in hollows. Static surfaces need the same treatment, and the key simplification is that **only the top-most surface in a column catches rain**: anything under a roof or bridge is sheltered anyway (the shelter test already exists, 2.2). So the bake needs one height per column, the top-most surface, not a layered model:

- `H(x, y) = max(terrain height, top-most static surface height in the column)`.
- **Multi-level** is handled by that rule: a bridge deck is the top surface of its columns, its edge is a drop to the terrain below (priority-flood sees that as a cliff and the deck edge drains), and the ground under the bridge is covered. At run time a pixel only takes the baked puddle if its world Z is within a tolerance of the baked `H` of its column (`|z - H| < epsilon`, epsilon about 1 unit), so walls, the underside of a bridge, and a lower floor visible through an opening never take a puddle belonging to the surface above or beside them.
- **Overhangs / bridges seen from below:** `H` is the top; the pixel's Z mismatch keeps them dry. Rain falling on terrain that a bridge overhangs is stopped by the bridge: the DEM column has `H` = deck, so the terrain under it has no `C` entry of its own (it is under cover, and shelter dries it).
- **Thin objects:** fences, rails, posts, trees, rocks small than a few texels would raise `H` in spikes that fence puddles in. The capture should keep only surfaces that are plausibly walkable/landable: min horizontal extent (morphological opening at the grid scale) and an upper slope limit; small items, actors and clutter excluded by the collision mask.

How to get the top-most surface (options, my recommendation first):

| Option | How | For | Against |
| --- | --- | --- | --- |
| **A. Jolt ray casts** (recommended for v1) | Downward `castRay` from a high Z at each texel centre with a static-only mask, take the first hit; same structure as `JoltSkyWorld` in `skyvisbaker.cpp`. | Reuses infrastructure, deterministic, uses exactly what the player collides with; cell loading is handled by the baker as for the sky-visibility bake. | Needs cells loaded (so the baker runs in the game, slowly); collision shapes may differ from visuals (invisible collision, missing collision on decorations); 16,384 rays per cell at 64 units (**estimate**: 25 M rays for all of Morrowind; at a few microseconds a ray that is minutes of single-thread time, **not measured**). |
| B. CPU rasterisation of flattened meshes | Reuse the LOD cluster baker's NIF flattening (`components/lod/nifflatten.*`, `clusterbuild.*`) and rasterise triangles top-down with a max-Z buffer. | No physics world and no loaded cells; fully offline and parallel. | Sees render geometry, including decoration without collision (grass tufts, signs), so the filter has more to do; a second code path to maintain; I did not open the flattener (**RECALLED-VERIFY**). |
| C. GPU top-down depth capture | An orthographic depth pass per cell, like `PrecipitationOccluder`. | Fast. | Needs a window/device in the baker, deterministic output across GPUs is not guaranteed, `Mask_Object` excludes terrain today. Not recommended for a baked, hashed result. |

Moved, disabled or script-created objects: the bake uses placed references as loaded (disabled initial state read from the cell data the baker sees). Scripts that move things later are ignored (the staleness digest cannot see them); the run-time Z test (above) limits the damage to a missing or a slightly wrong puddle, never a puddle floating in air.

### 4.4 Data sizes (estimates, not measured)

Per cell at spacing 64 units (128 x 128 = 16,384 texels):

| Channel | Meaning | Bytes/texel |
| --- | --- | --- |
| `capacity` | `C`, 8 bits at 1/16 unit (range 16 units, above the cap of 12) | 1 |
| `basinMax` | `Cmax` of the texel's basin, same quantisation | 1 |
| `catch` | `log2 kappa`, 8 bits | 1 |
| `flags/dir` | 3 bits flow direction, 1 bit sink, 1 bit "static surface", 3 bits reserved | 1 |
| `top` | `H`, 16 bits (world Z, 1/8 unit over a 8,000 unit range: **verify range**) | 2 |

That is 6 bytes a texel, 96 KiB a cell raw; Morrowind's 1,559 exterior cells with land: about 150 MB raw (**estimate**). Most of the world is flat-ish or sloped and compresses very well (many texels have `C = 0`); the LOD packs are zstd 15 inside an Eden Archive and "stored uncompressed so the bytes do not depend on the zstd version" (task 0055). I expect 4 to 10 times smaller (**guess**; measure at stage b). At spacing 128 units it is a quarter of that (24 MB raw per 1,000 cells), at 32 units four times more. Spacing is a setting of the baker and part of the parameter digest.

Run-time memory: the cells around the player (a 7 x 7 window is 49 cells) about 4.7 MiB raw, as an array texture or atlas plus a small cell index table.

### 4.5 Why 64 units, and the limit that remains

At 128 units (the terrain's own grid) each ground hollow is at least a few texels wide and puddles smaller than 1.8 m cannot exist in the data. 64 units (0.9 m) resamples the terrain so that bilinear heights are smooth contours, and gives the static capture twice the terrain's resolution (stairs, walkways, kerbs). Finer than that only helps once statics are captured and micro relief exists. The terrain mesh is made of triangles with a diagonal, not a bilinear surface; the bake must sample the same triangulation as `Terrain::Storage::getVertexData` so that the water plane intersects the surface that is drawn, not a slightly different one (**RECALLED-VERIFY** the diagonal rule; I did not open the terrain builder).

## 5. Run-time shading design

### 5.1 From "wetness" to "water on the ground"

Keep today's `mWetness` for darkening and gloss (it already ramps correctly). Add a second, physically motivated scalar, **water depth supplied** `W` (units of water depth that rain has deposited per unit area and not yet lost), integrated on the CPU in `WeatherManager::updateWetness` next to `mWetness` (so pure maths in `components/render/` and the single float goes to the renderer, in the style of `Render::Wetness::step`):

`dW/dt = rainRate(precipitationAlpha) - infiltration - evaporation(sun, shade)`, clamped to [0, Wmax].

- `rainRate` comes from the precipitation intensity the manager already has (`mResult.mPrecipitationAlpha`), later from the weather state struct (section 11).
- Per material (section 5.3), the first `P` units of `W` are absorbed by the ground (porosity storage), only the excess `W - P` is free water.
- Evaporation is slow, faster in sun than in shade: use the sky visibility already sampled for shelter (an open sky means sun) or just a constant at first. Game time, not wall time (existing rule).

### 5.2 Level rise inside a basin

For a pixel at world position `p` whose column has baked `capacity C`, `basinMax Cmax`, `catch kappa`: free water `Wf = max(W - P, 0)`; the basin's water depth above its floor is `L = min(Cmax, Wf * kappa)`; the water depth at the pixel is

`d = clamp(L - (Cmax - C), 0, C)`.

This gives: a bowl fills from the bottom (the deepest texel, `C = Cmax`, gets `d = L`), contour edges where `d` crosses 0 follow `C` (smooth when `C` is bilinearly sampled), shallow edges dry first (as `Wf` falls `L` falls and the contour retreats toward the bottom), basins with large catchments fill sooner than small ones, `d <= C` always (never spills past the baked spill level), and the function is monotone in `W` (the same `W` always gives the same picture, so drying reverses growth exactly; no hysteresis state to store).

Visual use of `d` (in cm-like units; Morrowind units, 1 unit is 1.43 cm):
- **coverage** `smoothstep(0, edge, d)` with `edge` about 0.3 units: a thin water film at the shore;
- **wet-edge gradient** outside the puddle: darkening that fades with `L - (Cmax - C)` negative up to a damp range of about 4 units above the water level (no extra data);
- **depth tint**: multiply the ground colour by `exp(-absorption * d)` toward a water tint (muddy brown on dirt, clear on stone), so shallow water is nearly transparent and the deep part dark;
- **refraction/parallax**: shift the ground texture lookup by `d` along the view direction in tangent space (small, optional; needs the view vector, which the shader has);
- **normal**: up, with ripples only where `d` exceeds a minimum (section 8), rather than on the film's rim;
- **roughness**: toward `wetPuddleRoughness` with `d`, but not to 0.03 on dirt (mud keeps 0.1 or so);
- **reflection:** sky as today (`wetSkyReflection`); nearby objects would need screen-space reflection (section 8).

### 5.3 Material porosity

Per material class: paved/stone (`P` about 0.3 unit, little darkening), packed dirt/road (about 1.5, strong darkening, mud tint), sand (about 4, absorbs most light rain, almost no puddles), grass/groundcover (about 3, darkening but no standing water except in deep basins), rock (about 0.2). **All numbers above are placeholders to tune from frames, not facts.**

- **Terrain:** the terrain layer array (`Terrain::Storage::getLayers()`) gives a texture path per layer; a small data file `files/wetness_materials.json` (canonical JSON, in the repository, **names only, no game data**) classifies layers by words in the texture path (stone, cobble, road, dirt, sand, mud, grass, rock; user mods keep working because the classifier is overridable). The layer blend weights already in the terrain shader weight the class parameters per pixel: no bake needed. **RECALLED-VERIFY** that Morrowind texture file names contain such words consistently; a classification table keyed by the user's actual layer list, produced at bake time, is the safe version (stored in the manifest, derived data, not committed).
- **Statics:** the PBR JSON overrides of task 0027 (`pbr`) already attach roughness/metalness per model; add a `porosity` field there for authored overrides; default class "stone" for NIF statics.
- **Actors/skin/emissive:** unchanged (D5/D8 of task 0037).

### 5.4 Micro relief (what makes cobbles and ruts)

A basin with `d` of 1 unit over a 1.8 m grid would be a perfectly smooth sheet. Add relief only inside the water: let the pixel's height used for the level test be `H + h_micro` where `h_micro` is the blurred height of the layer's `_p` map (task 0045) scaled to about plus or minus 0.5 unit, or, when there is none, a fine noise. The cobbles then poke through at the puddle's edge and the cart ruts of a dirt road fill separately. This is only visual and never feeds back into the bake. (**RECALLED-VERIFY** that the `_p` height is reachable from the terrain fragment shader; if not, noise.)

### 5.5 Applying, sharing, and the runtime data path

- A new puddle-map binding in the per-view descriptor set, following how the sky visibility field is bound (`lib/view/skyvis.glsl`; **RECALLED-VERIFY** exact binding and the "every view-set creator must bind dummies" pitfall from task 0028's notes). One shader path for statics, terrain and groundcover, as `wetness.glsl` is today.
- `WETNESS_PUDDLES` is a define; adding a second define (`WETNESS_PUDDLE_MAP`) doubles the sets that `main.frag.sets` and `SlangPermutations` list and changes `*.interface` records (golden data). Prefer a **uniform or specialization-constant branch** (the terrain fade used a uniform branch for the same reason: `PipelineTerrainDefines` counts 26,040 reachable fragment sets, task 0087). Decision for the implementing stage, noted in the spec.
- Slang twins: `wetness.slang` changes with `wetness.glsl`; the C++ twin `wetnessmath.hpp` gets the same maths and unit tests (the 0037 pattern).

## 6. Alternatives, for what the bake cannot cover

Places with no bake: modded cells added after the bake, interiors with an open floor, dynamically moved statics, anything when the baker is not run. Options, cheapest first:

1. **Fall back to today's noise** (the safe default; no regression, frame-identical to the previous build when no bake exists).
2. **Runtime topographic position (recommended fallback).** Capture a top-down height window (256 x 256 around the camera at 32 to 64 units, terrain plus static objects; this is the `PrecipitationOccluder` idea extended to terrain, **RECALLED-VERIFY** it can be reused), then compute, in a compute pass, `concavity = blur(H, r) - H` with `r` of 3 to 4 m, clamp it to the same capacity range, and feed the same level formula with `kappa = 1`. Cost: one capture plus a 256 x 256 blur when the window moves (hysteresis, a quarter window), tens of microseconds on the GPU (**estimate**). Quality: shows hollows, not true spill-point basins, and can fill a hollow that actually drains through a gap; acceptable as a fallback.
3. **Screen-space cavity** from the G-buffer depth and normals (task 0026): view-dependent (puddles move with the camera), noisy at grazing angles, fails behind occluders. A debug view at most; not recommended as the look.
4. **Per-object bakes** (vertex colour or a second UV set with capacity for models that have a flat top, e.g. fountains, troughs, floor panels): high effort per model, no data to start from; only if a specific asset class matters.
5. **GPU depression filling at run time** (iterated Planchon-Darboux relaxation, tens of passes over a window): exact-ish but the cost and the sync are high; rejected unless the runtime window turns out to be needed.

## 7. Interiors

Rain does not fall indoors, and `updateWetness` already passes 0 to the renderer when `isExterior` is false. Nothing here changes that. Cases to ask about (open question): drips and leaks (a cave mouth, a ruin with a roof hole) and floors near an open door; for those the sky visibility shelter test would let wetness through, and the runtime fallback (section 6, item 2) would handle open-floor interiors if the capture includes them. Not in the first stages.

## 8. Extras (each independent, all visual only)

- **Ripples where it is deep:** replace the global lattice with ring density scaled by `rainIntensity` and masked by `d > rippleMinDepth` (about 1.5 units); or reuse the water shader's ripple texture (`rainripples.glsl`) so puddles and lakes match (**RECALLED-VERIFY** interface).
- **Splash rings and drips:** small particle quads (preallocated, deferred bin of task 0075 so they do not ghost under TAA) at random puddle points; drips from roof edges: edge points are in the DEM (a top surface next to a drop of more than 1.5 m): store a short list per cell in the bake (a second table in the payload, a few hundred bytes), spawn drips along it while it rains. Snow/ash has no drips.
- **Footstep ripples and splashes:** the player and NPCs in water deeper than a few units spawn ring entries in a fixed ring buffer (16) in a uniform block. Source of the foot events: the character controller's footstep code (`apps/openmw/mwmechanics/character.cpp`, the footstep sound path, **RECALLED-VERIFY**). Purely visual, no gameplay.
- **Flow streaks on slopes:** where wetness is high and the flow accumulation is large, an anisotropic darkening streak texture aligned to the stored flow direction. Subtle; late.
- **Mud on dirt:** class `dirt` with a free-water surplus gets a brown tint and a duller roughness (no mirror).
- **Drying order:** shade stays wet longer (evaporation from sky visibility), basins with big `kappa` keep water longer, sand dries first.
- **Snow and ash melt coupling (task 0039):** `cover` amount above a threshold suppresses the puddle (frozen/buried), and melting adds to `W` at the melt rate, so a thaw makes puddles. `WeatherManager::updateCover` already has the melt step; the coupling is a one-line input to the `W` integrator.
- **Screen-space reflections** of nearby geometry in puddles (uses the HDR colour and depth, task 0026): improves "reflects properly" most but costs a pass and fails at the screen edge; an optional late stage, off by default, with its own measurement.
- **Debug views:** potential map (capacity), basin ids, water depth, catchment ratio, flow direction (added to the existing `wetness debug`; on `tools/testprofile/exclude.txt` as debug views).

## 9. Risks

| # | Risk | Likelihood / impact | Mitigation |
| --- | --- | --- | --- |
| R1 | Terrain resolution (128 units) gives hollows too coarse to read as puddles | high, medium | Resample to 64 units, micro relief (5.4), statics add detail; judge on frames before building stage c |
| R2 | Nested basins merged in v1, so inner pits fill late | medium, low | Accept; hierarchy later; test with synthetic nested bowls to pin behaviour |
| R3 | Deep hollows (ponds, ruins) never filled to their spill level because of the cap | medium, low | Cap and tint; ponds belong to water records |
| R4 | Static capture: invisible collision, missing collision on decoration, many tiny objects, roofs with decoration | high, high | Stage c is separate, off by default, with a debug view of the captured top surface; an "ignore statics" flag; opening/size filters |
| R5 | Multi-level places (Vivec canton platforms, Ald'ruhn, Telvanni towers, bridges) mis-assigned | medium, medium | Z-match test at run time, top-most-only rule, frames at named locations |
| R6 | Bake time and size on big mod lists (Tamriel Rebuilt style) | medium, medium | Work units of task 0061, per-cell digests, spacing setting, background mode |
| R7 | Bake depends on the Jolt world's cell loading and so on the load order; digest misses script-moved objects | medium, low | Digest from placed references as loaded, documented limitation |
| R8 | Shader permutation growth and `.interface` golden data churn | medium, medium | Uniform/spec-constant branch instead of a define |
| R9 | Cost of one more fetch and the extra maths in every wet pixel | low, low | Measure like 0037's note (+0.07 ms); only in rain |
| R10 | Looks too realistic to read in a game with flat art (toddhow's call) | medium | Strength and a `readability` bias setting: scale `L` up and the damp range |
| R11 | Mismatch between terrain triangulation and the sampled DEM leaves water floating or buried by up to a few units | medium, low | Sample the same triangles; a 0.5 unit bias; frames |
| R12 | A non-deterministic baker (thread order, float differences between compilers) breaks the staleness check across Windows/Linux | medium, medium | Fixed-order priority queue, tie-break by index, quantise outputs, digest excludes the floating inputs the way the sky-vis fix did for corner normals (CLAUDE.md changelog 2026-11-04: `Terrain::Storage::averageNormal` summed in float and differed in the last bit) |

## 10. Cache format sketch (`openeden.puddlemap` v1, a proposal)

Follows the pattern of the LOD tile payload and cluster payload (task 0055/0059): a canonical, versioned binary payload with a strict reader (every size bounded before allocation, located errors, no exception out of the loader), stored as an entry in an Eden Archive pack (`<user data>/openeden/puddles/puddles.edenpak`) or, once task 0061 exists, as a unit of kind `puddle` in the unit store (key tag **proposed `0x14`**, **verify** against 0061's table at the time), published by generation. The project rule that authored data is JSON does not apply: this is derived data like the LOD packs and light probes; the manifest is JSON.

- **Entry key:** `(tag 0x14, cell x, cell y)`.
- **Payload header (little-endian):** magic `EDENPUD`, version 1, cell x/y, grid `n` (texels a side, default 128), spacing (units, float), `capacityStep` (units per step), `topScale`, `topBias`, flags (statics captured, sink present), counts: basins, drip points; parameter digest (baker version, spacing, cap, quantisation, statics flag, material table version) and input digest (BLAKE3 of land records' heights and textures in the cell, water height, and, with statics, the placed references and their model hashes; same recipe as the cluster digest); then the five channels, then a basin table (id, area, `Cmax`, `kappa`) for debug, then drip points.
- **Manifest:** `puddles.manifest.json`, canonical, with the parameter digest, the list of cells baked, the material classification table (layer name to class), the baker and layout versions.
- **Staleness:** the runtime recomputes each loaded cell's input digest cheaply (the terrain tile digest path already exists as a digest-only pass, task 0061 "A digest-only pass exists for staleness") and ignores a stale or unreadable entry with one warning per session, falling back to the noise path. A pack from a different layout version is refused. The `provenance` listing (`edenlist --provenance`) shows the digests.
- **Determinism:** two bakes are byte-identical (CLAUDE.md); tested on a generated land, never on game data.
- **Command and contract:** `openmw --bake-puddles` (a cell range form like `--bake-gi-exterior "<x>,<y>"`, all cells with no argument), resumable, one unit per cell, status through task 0061's `status.json` when it exists; until then a main-thread time-sliced baker as the skyvis and LOD bakers are today. Nothing is committed; no game data is used by tests.

## 11. Relationship to the weather plan, and order

Another agent's weather plan (`docs/research/0048-weather-system.md`, task spec `docs/tasks/0094-weather-system.md`, **written in parallel; I have not read the spec, only the note**) proposes a weather state contract ("A. A weather state contract", pure header `components/render/weatherstate.hpp` filled by `WeatherManager`, its W0) and lists wetness and cover as "reuse as is". This work does not duplicate it:

- Weather owns: which weather, precipitation kind and intensity, wind, lightning, storm clouds, rain drawing and sound.
- This work owns: what rain does to the ground (water supply `W`, puddle geometry, material response, ripples on puddles).
- The only coupling is the **input**: rain rate, wind (for later flow streaks and rain slant on ripples), and the lightning flash on puddles (already works, task 0040). Until the weather state struct exists, the `W` integrator reads `mResult.mPrecipitationAlpha` as `updateWetness` does today; when W0 lands, only its inputs change.
- **Order I recommend:** the weather plan's W0 (the state struct, small, no visual change) can land at any time; stages a to c of this work (maths, terrain bake, static capture) do not depend on it and can start now; stage d (shading with `W`) should take the weather struct if W0 has landed, else the current inputs. Neither blocks the other. Both touch `WeatherManager::updateWetness`: a merge conflict risk to coordinate in the commits.

## 12. What I could not verify (all RECALLED-VERIFY)

- The exact land constants (`LAND_SIZE` 65, `REAL_SIZE` 8192, height scale) in `components/esm3/loadland.hpp`, and the terrain triangulation diagonal.
- Priority-Flood paper details and the depression hierarchy paper (Barnes et al.), cited from memory.
- That `_p` height maps are reachable from the terrain fragment shader, and how terrain layers name them (task 0045 code not opened).
- The water shader's ripple include (`rainripples.glsl`) and whether the two ripple looks differ.
- The binding mechanism for a new per-view map (`skyvis.glsl`), the unit-store key tag, whether the LOD cluster flattener can rasterise top-down, whether `PrecipitationOccluder` can be reused for terrain, and the character controller's footstep path.
- Texture file naming for the material classifier.
- Every number in sections 4.4 and 5.3 (sizes, times, porosity, depths) is an **estimate or placeholder**, not a measurement.
- Whether `worldNormal` passed to `wetSurfaceAt` includes normal-map perturbation (`main.frag` line 298 uses `viewNormal` after the normal-map step? I did not trace it); if it does, the flatness test reacts to normal-map noise, one more reason today's puddles look random.
- Nothing was built or run. The weather plan's task spec was not read.
