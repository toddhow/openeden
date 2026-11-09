# Task: Puddles and wetness v2 (baked puddle potential, a water level that rises from the lowest points)
Design doc section: `docs/research/0049-puddles-and-wetness.md` (sections 4 to 10 are the design; this spec is its staging). Builds on `docs/tasks/0037-rendering-wetness-and-puddles.md` (the wetness value, darkening, gloss, the noise puddles), `0036-rendering-gi-exterior-sky-visibility.md` (shelter and the sky image), `0045` (parallax height maps), `0039-rendering-snow-and-ash-cover.md`, `0055`/`0059` (derived data packs, digests), `0061-lod-s8-background-baker.md` (work units, status contract) and `0058-launcher-world-lod-page.md`. Weather inputs: see "Relationship to the weather plan" below.
Goal: puddles that form where water really collects, grow from the lowest points as rain falls, follow the ground's contours at their edges, differ by material, and shrink back in the reverse order as the ground dries, using a map of puddle capacity baked on the user's machine from their own game data, with today's noise puddles as the fallback.
Inherited code touched (to be re-read at the commit that starts each stage; writing this spec changes nothing): `apps/openmw/mwworld/weather.cpp` (`WeatherManager::updateWetness`, new water-supply integrator beside `mWetness`), `apps/openmw/mwrender/rendermanager.cpp` and `.hpp` (`setWetness`, the scene block, the wetness setting block near line 584, `terrainMaps.wetness`, a new puddle-map owner), `files/shaders/lib/material/wetness.glsl` and `wetness.slang` (`wetSurfaceAt`, `wetPuddleMask`, `wetPuddleNormal`, `wetRipple`), `files/shaders/default/main.frag` and `files/shaders/terrain/main.frag` (the wetness blocks), `components/render/wetnessmath.hpp` (`Render::Wetness`, C++ twin), `components/terrain/storage.hpp` and `components/esmterrain/esmterrain.*` (`getVertexData`, `getHeightAt`, `getLayers`, read only), `apps/openmw/skyvisbaker.cpp` (`JoltSkyWorld`, the pattern for static capture), `apps/openmw/main.cpp` and `options.cpp` (`--bake-puddles`), `components/lod/tilebake.*` (digest-only pass, pattern), `tools/testprofile/`.
Inputs / outputs: new `[Rendering]` settings `wetness puddle map` (bool, false), `wetness puddle depth` (strength 0 to 2, default 1), `wetness water supply` (bool, false: the `W` integrator; with it off the map is driven by `wetness` as scaled by a fixed curve so a user can try the map alone), `wetness puddle debug` (enum off/capacity/depth/basins/catchment); baker settings `puddle bake spacing` (64), `puddle bake capacity cap` (12), `puddle bake statics` (bool); new derived data `<user data>/openeden/puddles/` (`puddles.edenpak` and `puddles.manifest.json`, format `openeden.puddlemap` v1, research note 0049 section 10); new command `openmw --bake-puddles`; new pure maths `components/render/puddlefill.hpp` (priority flood, components, accumulation) and `puddlelevel.hpp` (level rise, depth, coverage); data file `files/wetness_materials.json` (names only). No change to any save, Lua API, extension contract, weather record, `ChangeWeather` behaviour or sound.

Status: DRAFT written 2026-11-09 at toddhow's request, awaiting his approval. Nothing is built. **Full loop** when it starts (Track V renderer shader changes, derived data read as untrusted input, a published format). Tests first for every stage; a fresh reviewer per stage.

## Relationship to the weather plan

The weather plan (`docs/research/0048-weather-system.md`, `docs/tasks/0094-weather-system.md`; its W0 is a weather state struct) owns which weather, rain drawing, wind, lightning and storm clouds. This spec owns what rain does to the ground. Coupling is one-way and small: this spec's water-supply integrator needs a rain rate (and later a wind vector). Until W0 lands, it reads `mResult.mPrecipitationAlpha` exactly as `updateWetness` does today; when W0 lands, only the inputs of the integrator change (one commit). Stages a to c here do not depend on weather at all. **Which lands first:** W0 can land first or in parallel (small, no visible change); stage d here should consume it if it exists. Neither blocks the other; both edit `WeatherManager::updateWetness`, so whoever lands second rebases and keeps both.

## Design, in plain language

Today a puddle is "flat enough here and a noise value is high here" (research 0049 section 3). The new idea: bake, once, for every exterior cell, a map saying how deep a puddle could be at each point if water collected there (the **capacity**), which pit it belongs to, and how much ground drains into it. At run time one number, the water supplied by the rain minus what the ground absorbed, becomes a water level inside each pit. The deepest point wets first, the level rises, the edge creeps outward along the ground's contours, and when the rain stops the level falls and the edge retreats the same way. Material decides how much the ground soaks up before any water stands. Where no map exists, the noise puddles stay as they are.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | Source of the ground shape | Terrain heights from the ESM land data (`Terrain::Storage`), plus top-most static surfaces from Jolt ray casts (stage c) | CPU rasterisation of flattened meshes; GPU top-down capture (research 0049 4.3) |
| D2 | When the bake runs | Ahead of time with `openmw --bake-puddles`, and later as a work unit of task 0061's background baker; never during play without being asked | Always in the background |
| D3 | Resolution | 64 units (0.9 m), a baker setting | 128 (smaller, coarser), 32 (larger) |
| D4 | What fills a pit | Free water `W - P` times the pit's catchment ratio, capped at its capacity; nested pits merged in v1 | A full depression hierarchy (later); one global fill fraction |
| D5 | Fallback | Today's noise puddles when no map or a stale map; runtime topographic fallback in stage e | Nothing |
| D6 | Interiors | No puddles (as now) | Leaks and drips (stage e, optional) |
| D7 | Realism vs readability | A strength setting; defaults tuned from frames, judged by toddhow | Fixed |
| D8 | Requirements | `hdr`, `pbr` (as 0037); `gi exterior` still gives shelter | Own shelter data |

## Stages

Each stage is independently shippable, off by default, tests first, and ends in a commit that builds and passes the gates. **Size** is a rough effort (S under a day, M a few days, L about a week or more, in agent sessions) and **risk** is for the unknowns, both guesses.

### W-a. Pure maths (size M, risk low)
Header-only, no engine types, C++ unit tests written first and run red against stubs.
- `Render::Puddle::fillDepressions(heights, sinks, n, m)` returning filled height and capacity (priority-flood, fixed tie-break by texel index), `labelBasins` (4-connected components of capacity above 0, with area, max capacity, outlet), `accumulateFlow` (D8 on the epsilon-flooded surface) and `catchmentRatio`.
- `Render::Puddle::levelOf(freeWater, kappa, basinMax)`, `depthAt(level, basinMax, capacity)`, `coverage(depth, edge)`, `damp(level, basinMax, capacity, range)`, quantisation helpers (capacity, `log2 kappa`, height) with round-trip bounds.
- Tests on synthetic height grids with known answers:
  - a **bowl** (paraboloid, rim above its centre) fills from the bottom: depth at the centre equals the level, the zero contour is a circle of the analytic radius within one texel, the contour grows monotonically with water;
  - a **slope** (plane) has capacity 0 everywhere and drains to the sink; a tilted plane with one hollow has exactly one basin;
  - a **flat plate with a raised rim** fills evenly (every texel equal depth); a plate with no rim has capacity 0;
  - **two bowls joined at a saddle**: the lower rim spills first (the basin component and its `Cmax` are as hand-calculated);
  - a **nested pit** is merged (pins the v1 behaviour);
  - sea/sink texels get capacity 0 and drain neighbours;
  - **determinism:** the same grid gives identical bytes in repeated runs, in a transposed/rotated copy (rotated outputs equal rotated inputs' outputs), and with the order of equal-height texels permuted (index tie-break makes it independent);
  - **region independence:** splitting a large grid into tiles with a halo of one cell gives the same capacity as the whole grid for basins that lie inside a tile;
  - **properties (random grids, fixed seeds):** capacity is never negative or above the cap; `depth <= capacity`; depth is monotone in water supplied; the contour at zero water is empty; a texel with a non-ascending path to the sink has capacity 0; NaN/inf heights are refused with a located error.
- **Off means unchanged:** nothing is wired; no shader or setting exists.

### W-b. Terrain-only bake, pack, manifest, runtime data path (size L, risk medium)
- The terrain DEM: sample each cell's `ESM::Land` heights through the same path and triangulation as `Terrain::Storage::getVertexData` (lod 0) at the baker's spacing, with a one-cell halo from neighbours; water bodies (cell water height, sea) and cells without land are sinks; `capacityCap` applied.
- `openmw --bake-puddles` (all land cells, or `"<x>,<y>"` and a range form), resumable, deterministic, time-sliced on the main thread like the sky-visibility baker until task 0061 exists; one pack entry per cell (key tag proposed `0x14`, verify against 0061), canonical payload and strict reader (research 0049 section 10), manifest, parameter and input digests, staleness check, atomic publish, "up to date" on re-run.
- Runtime: `RenderManager` owns the cell window (7 x 7) in a GPU array texture with a cell index table, loaded on a worker, ignoring stale entries with one warning; debug view `capacity` and `basins` (flat colour per basin) in `wetness puddle debug`; the shading is **not** changed yet (this stage only loads and shows the map).
- Tests first: payload round trip byte for byte; strict reader against truncations, wrong counts, out-of-range quantisation (a fuzz like the LOD pack's); manifest; digest changes when a height changes and not when an unrelated cell changes; two bakes of a generated land are byte-identical; a killed and resumed bake equals an uninterrupted one; the loader falls back on a stale entry. Generated land only; no game data in tests.
- Measurements recorded in the stage log (all on the user's machine, aggregate numbers only): bake time for the Morrowind exterior at 64 units, pack size raw and compressed, run-time upload cost, memory for the window.
- **Off means unchanged:** with `wetness puddle map` off the frame equals the previous build (diffmap in rain at a fixed time and weather); with it on and no pack the frame equals off.

### W-c. Static geometry: roads, bridges, floors, multi-level (size L, risk high)
- Stage-c captures the top-most static surface per column (Jolt downward ray casts with a static-only mask, first hit, as in `JoltSkyWorld`), merges `H = max(terrain, top static)`, applies the size/slope filters (research 0049 4.3), marks texels as "static surface" in the flags channel and stores `top` Z for the run-time column test. Reruns of stage b's digest including placed references and model hashes.
- Stage decision gate before shading: a debug view of the captured top surface at named places (Seyda Neen docks and roofs, a Balmora bridge, Vivec canton platforms, Ald'ruhn, Telvanni tower base, a ghostfence); toddhow looks, and the stage ends in "go / adjust filters / drop statics" (setting `puddle bake statics` stays off unless go).
- Tests first: a generated scene of boxes (a flat roof, a ramp, a bridge over a ditch, a stack of two floors, a thin fence, a column of tiny objects) through a fake `SkyRayWorld`-style interface (as `skyvisbaker.cpp` abstracts its ray source) gives the expected top surface; filters remove the fence and the clutter; the bridge deck drains at its edges; the terrain under the bridge is not given the deck's capacity; determinism and resume.
- **Off means unchanged:** with `puddle bake statics` off the pack is byte-identical to stage b's (compare); the setting at run time is irrelevant to the frame.

### W-d. Run-time shading (size L, risk medium-high)
- The water-supply integrator `Render::Wetness::stepWater` (pure, tested: rise with rain, absorption below `P`, evaporation, clamp, game time only, never negative) in `WeatherManager::updateWetness`, passed through `RenderManager::setWetness` (extra scene value; a spare field of the scene block or the existing alpha encoding extended by a step, decided at the commit: the block is capped at 640 bytes, task 0037 notes).
- Shader (GLSL and its Slang twin and the C++ twin): sample capacity, basin max, catchment and (with statics) top Z; level and depth as in research 0049 5.2; coverage with a wet edge, damp ring outside the puddle, depth tint, optional parallax shift, roughness by material, puddle normal and ripples only where deep, sky reflection through `wetSkyReflection`; where no map covers the pixel (no pack, stale, beyond the window, Z mismatch with the baked top) the **old noise path** runs unchanged. Select by a uniform or specialization-constant branch, not a new define (research 0049 5.5, R8); update `.interface` records and `SlangPermutations` expectations if a define is added anyway (golden data: awaiting toddhow's review).
- Material porosity from `files/wetness_materials.json` and the terrain layer weights (class parameters blended per pixel); static default class stone; PBR JSON `porosity` override.
- Terrain, statics, groundcover share the path; actors keep darkening/gloss only; skin and glowing surfaces stay exempt.
- Snow/ash coupling: cover above a threshold masks the puddle; melt adds to `W`.
- Tests first: the C++ twin of the new shader maths against hand values (the bowl numbers of stage a), the quantisation round trip through the shader maths, `stepWater` curves, the material blend; shader text/interface tests as for 0037; the "twin equals the C++ numbers by machine" check that 0037 never had (a GPU read-back test in the style of `SlangLightGridGpu`, skipped without a device).
- Frames (viewed, `diffmap` where stated): Seyda Neen and a Balmora street in steady rain at a fixed time and weather, `wetness puddle map` off against on with the pack; the same place at `W` fractions 0.1, 0.3, 0.6, 1.0 (the contour grows from the hollows); after the rain stops at 25, 50, 90 percent of the dry time (the contour retreats); a dirt road against a cobbled one in the same weather; under an eave and a bridge (dry); on a flat rock top (no puddle where the pack says capacity 0). Compare with the previous noise puddles at the same pose and keep the pair in the stage log.
- **Off means unchanged:** setting off: frames equal the previous build in rain within the noise bar used by 0037 (static scene, `diffmap`); setting on and no pack: equal to off; on in clear weather: equal to off.

### W-e. Fallback and extras (size L in total, risk medium; each sub-item can ship alone, all off by default)
- e1. Runtime topographic fallback for cells with no valid pack (top-down height window around the camera, `blur(H, r) - H`, `kappa` 1): needs a renderer capture that includes terrain; measurement of its GPU cost; compare with the baked result in a cell that has both.
- e2. Ripples where deep (`d` above a minimum) with strength from the rain intensity, optionally the water shader's ripple texture; splash rings and drips (deferred bin, preallocated, drip points from the pack); footstep rings from the player and NPCs (fixed ring buffer in a uniform block, visual only).
- e3. Flow streaks on slopes from the stored flow directions; mud tint on dirt; drying order from sky visibility.
- e4. Optional screen-space reflection of nearby geometry in puddles, with its own measurement and an off default.
- e5. Interior leaks/drips if toddhow wants them (open question).
- Tests first for each pure part (ring buffer, drip spawn determinism, streak direction); frame checks for each visual.

### W-f. Test profile, debug views, cost notes, docs (size S, risk low; but every earlier stage adds its own line as it lands, per the living-profile rule)
- `tools/testprofile/profile.cfg` gets `wetness puddle map = true` (next to `wetness = true`, line ~81) with a `# task 0095: ...` comment and a feature line in `tools/testprofile/README.md`; `prepare.sh` bakes the puddle map beside the LOD, body and light probe bakes (a step with the same freeze-retry guard) and the profile points the pack's folder; debug views, the bake spacing, cap and statics flag go on `tools/testprofile/exclude.txt` with a reason; `tools/testprofile/coverage.sh` must exit 0 (run it).
- A cost note `docs/research/00NN-puddle-cost.md`: GPU time of the new shader path in rain (against 0037's +0.07 ms), sampling cost, memory, bake time and pack size on the user's data, run-time load cost.
- Module guides: `components/render/CLAUDE.md`, `apps/openmw/mwrender/CLAUDE.md`, `files/shaders/CLAUDE.md`; CLAUDE.md changelog row.

## Constraints

- Everything is off by default and "off means unchanged" is compared by frames (`diffmap` at a fixed time and weather, task 0034's notes), not assumed. Default behaviour is Morrowind's.
- Derived data is baked on the user's machine from their own game data, never committed, never shipped, tests use generated land and generated scenes. A mod's own cells are baked the same way.
- Determinism: no wall clock; fixed-order priority queue with an index tie-break; no iteration over unordered containers in the baker; quantised outputs; two bakes byte-identical, also across Windows and Linux (digest excludes values that differ in the last float bit across compilers, the lesson of the 2026-11-04 `averageNormal` fix).
- No heap allocation per frame in the sampling, upload and integrator paths; the baker may allocate.
- Threading: the baker runs on the main thread in slices (as the sky-visibility baker) until task 0061; the pack loader runs on a worker and publishes to the render thread with the existing deferred-delete patterns; state which thread each new callback runs on in the pull request.
- Untrusted input: the pack is derived data that may be corrupt or from another version; the reader is strict and bounded (see W-b tests).
- No game assets, no proprietary code; the classification file contains words, not game data.
- The editor's `SceneHost` keeps building and drawing; it does not take puddles.
- Shader changes: GLSL and Slang twins change together; `SlangPermutations`, interface records and the sets lists are updated in the same change; new texture/buffer bindings are bound to dummies in every view-set creator (the pitfall in 0028's notes).
- Never invent engine facts: each stage names the file and function it touches at its commit; the RECALLED-VERIFY items in research 0049 section 12 are resolved by reading the code at the start of the stage that needs them.

## Acceptance criteria

- [ ] W-a: all pure-maths tests pass, written first and run red; a bowl fills from the bottom, a slope drains, a plate with a rim fills evenly; determinism, rotation and tile-independence hold.
- [ ] W-b: `openmw --bake-puddles` writes a pack and manifest for the user's Morrowind data; two bakes are byte-identical; re-run reports up to date; a stale or corrupt entry is ignored with one warning; timing and size recorded; the debug capacity view shows hollows in the right places (frames viewed at the Seyda Neen road, the Bitter Coast, a mountain valley).
- [ ] W-c: the captured top surface is correct at the named places (frames of the debug view viewed and judged by toddhow); the bridge and multi-level tests pass; statics off gives a byte-identical pack to W-b.
- [ ] W-d: with the pack, puddles sit in hollows, grow from the bottom with water supplied, edges follow contours, shrink in reverse when drying, differ between dirt, cobbles, sand and grass, stay dry under cover, and ripples appear only where deep (frames viewed); without a pack the old puddles show unchanged.
- [ ] Setting off, or on with no pack, or on in clear weather, is frame-identical to the previous build within noise (`diffmap`, method written in the stage log).
- [ ] Each stage: cost recorded (GPU in rain, CPU, memory, bake time); unit tests, the 32 Lua tests, the Linux gate (GCC `-Werror`), clang-format 14 on changed lines and the whole-tree check pass; a fresh reviewer read the stage; `tools/testprofile/coverage.sh` exits 0.
- [ ] Docs: the cost note, module guides and a CLAUDE.md changelog row exist; the golden data touched (`*.interface`, sets lists) is listed for toddhow's review.

Out of scope: the weather system (rain particles, wind, lightning, clouds, thunder, sound; `docs/tasks/0094-weather-system.md`), water shading and the sea, snow and ash cover itself (0039; only the coupling), terrain deformation and gameplay effects of wet ground (slipping, footprints that persist, sound), saves (nothing is written to a save), Lua, extension contracts, a depression hierarchy (later), planar or ray-traced reflections, interiors (unless toddhow asks), the editor.

## Open questions for toddhow (decisions only he can make)

1. **Bake time budget and mode:** is a one-off ahead-of-time `--bake-puddles` (minutes for vanilla, my estimate) acceptable, or must it run in the background as part of task 0061's baker? Do you want it in the launcher's bake page (0058)?
2. **How realistic vs readable:** physically derived puddles are fewer and more local than the current 40 percent coverage; the world will look drier in light rain. Do you want a readability bias (more and bigger puddles than physics gives), and how much?
3. **Statics:** is stage c (roads, bridges, roofs) worth its risk now, or do we ship terrain-only puddles first and judge the look? (I recommend terrain-only first, then the stage c gate.)
4. **Interiors:** none (as now), or leaks/drips and open-floor interiors later?
5. **Mod terrain:** a mod that changes land or adds large static structures makes affected cells stale; rebake those units only (task 0061) or rebake all? Mods that ship their own bakes?
6. **Resolution:** 64 units (recommended) or 32 for finer shapes at four times the data?
7. **Priority against the weather system plan:** weather W0 first, in parallel, or this first? (Independent for stages a to c.)
8. **Realism extras:** how far to go on reflections of nearby objects (screen-space reflection, costly, optional), flow streaks, mud, and footstep splashes.
9. **Material classification:** by texture-name words in a committed data file (names only), or generated at bake time from the user's layer list with a per-user override file?
10. **Water supply model:** accept the simple rain rate minus absorption minus evaporation model (D4), or keep `wetness` as the only input and map it to a level directly (less physical, no new state, no `wetness water supply` setting)?

## Stage log

Not started.
