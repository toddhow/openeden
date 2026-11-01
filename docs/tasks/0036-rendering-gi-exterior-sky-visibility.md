# Task: Rendering GI for exteriors, a baked sky-visibility field lit by the live sky
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R3 probe lighting, R6 weather) and section 6; builds on `docs/tasks/0028-rendering-gi-baked-probes.md` (decision D8: exteriors keep the flat ambient), `0029-rendering-ao.md`, `0034-rendering-weather-atmosphere.md` and `0027-rendering-f4-material-model.md`.
Goal: give exterior cells ambient light that is shaded by the world (darker under bridges, trees and eaves and between buildings, brighter on open ground) and that follows the sky, sun and weather through the day, by baking only what never changes (how much sky each place can see) and combining it at run time with the live sky.
Inherited code touched: not yet confirmed; to be named at the commit that starts each stage. Expected: `apps/openmw/mwrender/rendermanager.cpp` (`configureAmbient`, the probe loading added in task 0028), `apps/openmw/mwworld/weather.cpp` (where task 0034's `applyAtmosphere` sets the ambient and sun colours), `files/shaders/lib/material/terms.glsl` (the ambient line), `files/shaders/lib/view/gi.glsl`, `files/shaders/terrain/main.frag` (terrain gets the field in this task, unlike interiors), `components/render/probes.*`, `probebake.*`, `probegpu.hpp`, `reflectionprobes.hpp`, `components/render/atmospheremath.hpp`.
Inputs / outputs: new `[Rendering]` settings `gi exterior` (bool, false), `gi exterior strength` (0 to 2, default 1), the baker's `gi bake sky rays` and `gi bake sky spacing`; new baker mode `openmw --bake-gi-exterior "<x>,<y>"` (and a range form); new file kind `openeden.skyvis` v1 in the VFS next to the other probe files (`openeden/probes/`); a runtime sky environment image built each time the atmosphere model's inputs change enough. Nothing is written to a save, no Lua API, no extension contract changes.

Status: stages a to e built 2026-11-01 (see the stage log at the end); some acceptance criteria are not verified (listed there). Written at toddhow's request after he asked why exteriors have no GI and how Red Dead Redemption 2 handles a large world. Full loop (Track V renderer).

## Why interiors worked and exteriors do not (what this spec answers)

An interior is lit by a few fixed lights, so the light that reaches each point can be baked once. An exterior is lit by the sun and the sky, whose direction, colour and strength change with the hour and the weather, so a bake made at noon is wrong at dusk. The fix is to split the problem:

- **Baked, never changes:** for each point, *how much of the sky can it see, and from which directions*. That depends only on the buildings, rocks, trees and terrain. We store it as a low-order spherical-harmonics (SH) function of direction, the same "nine numbers" form task 0028 uses, but holding a *visibility* (0 blocked, 1 open) and not a colour.
- **Live, changes every frame or slowly:** the colour and brightness of the sky in each direction and of the sun, from task 0034's atmosphere model and the weather. The ambient at a point is the sky's own SH colour multiplied by that point's visibility SH, summed over directions (a dot product of two SH vectors, nine multiplications per colour channel in the shader).

This is the split the sources on Red Dead Redemption 2 point to (baked large-scale occlusion plus a live sky); we do not claim to copy how RAGE does it, only the idea. Everything except the bake already exists in some form: the probe file format and baker (0028), the sky and sun colour model (0034) and the screen-space occlusion (0029) that handles small contact shadows the field is too coarse for.

## Design, in plain language

A **sky-visibility probe** is a point in the open world where the baker shoots rays in many directions through the static collision geometry (the game's Jolt world, as in 0028) and records which of them reach the sky. The result is a nine-coefficient SH function of direction for the *visibility*, not for light. Probes sit on a regular grid per exterior cell, with a coarse vertical spacing, denser near the ground, because the interesting shading is within a few metres of where the player walks.

At run time the shader looks up the eight probes around a pixel, blends their visibility SH (with the same normal bias and cell-border handling as the interior probes), and multiplies it by the current **sky SH**: the atmosphere model's sky colour evaluated over the sphere and projected to nine coefficients once per frame on the CPU (so there is no per-pixel sky maths). The sun is not part of this ambient: it stays the direct directional light with its cascaded or existing shadows, and the field only replaces the *flat ambient* term, exactly as the interior probes do.

A second, smaller piece makes reflections work outdoors: a **sky environment image** (a small cube or octahedral map of the current sky, rebuilt when the time of day or the weather has moved enough, not every frame) used for the specular ambient on `pbr` surfaces, replacing the "sky colour is not baked" gap that task 0028 recorded. It is built from the same atmosphere model, so it needs no bake.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | What is baked | Sky visibility as SH (L2), one volume per exterior cell | Full irradiance at a fixed hour (wrong at other hours); a bake per hour (too much data) |
| D2 | Probe spacing | Horizontal 256 game units (about 3.6 m), vertical 128 within 1024 units of the ground and none above, so a cell is about 32 x 32 x 8 probes before pruning; probes inside solid geometry and far from any walkable surface are dropped | Uniform 128 (4x the data); a coarser 512 (less shading detail) |
| D3 | File format | JSON, canonical writer, round-trips byte for byte, located errors, `openeden.skyvis` v1, floats as shortest round-trip text, one file per exterior cell named by its grid coordinates (the naming 0028's format already supports for exteriors); a cell is about 1 MB before compression, so quantise to 8-bit SH in v1 if the size is a problem, decided at stage a by measurement | Binary (breaks the project's rule for authored data) |
| D4 | Where bakes live | Read from the VFS; the baker writes to the user data folder; **no bake is ever committed** (a Morrowind cell's bake is derived from the user's installed game data); a mod may ship bakes for its own cells | Ship bakes in the repo (refused by the no-game-assets guardrail) |
| D5 | No bake for a cell | Flat ambient as today (a runtime guess would be a second, untested look); neighbouring cells with bakes blend at the border | A coarse runtime estimate from the terrain height field |
| D6 | Sky model | Task 0034's atmosphere model, evaluated to SH once per frame on the CPU; **requires `atmosphere` on**, since the colours come from it | Use the weather's vanilla colours as a uniform sky (works without atmosphere, but loses the day cycle) |
| D7 | Terrain | Receives the field (a new `GI` variant of `terrain/main.frag`); the terrain's own ambient-occlusion exclusion from 0029 stays | Leave terrain flat |
| D8 | Vegetation and moving objects | Groundcover and trees use the probes at their root; actors sample at their position, per frame, so they get the shade of the place they stand in | Per-pixel only |
| D9 | Distance | The field covers the loaded cells (the exterior cell grid and nearby distant-terrain chunks); beyond that, flat ambient | Always on for all loaded terrain chunks |
| D10 | Baker runs | In the game, like the interior baker, one cell at a time, resumable, deterministic with a fixed seed; a range form bakes a rectangle of cells in a background-friendly order | A separate tool |
| D11 | Requirements | `hdr`, `pbr` and `atmosphere` | Fewer |
| D12 | Bounce light | None in v1 (a sunlit wall does not light its neighbour); documented as the main look gap. A later stage may add a cheap runtime bounce term from the lit surface colour | Bake bounce for a fixed hour (wrong at other hours) |

## Stages

Each stage commits working and is checked by frames, tests and the Linux gate, with a fresh reviewer.

- **a. Format and maths.** `openeden.skyvis` v1 (reader, canonical writer, located errors, grid-coordinate file names, quantisation decision by measurement), the SH visibility maths, the SH-by-SH dot product (a C++ twin of the shader's), and the sky-to-SH projection of the atmosphere model's output. Tests: round trip byte for byte; a fully open point gives a known SH; a fully blocked point gives zero; the dot product of a uniform sky and an open point equals the uniform sky's ambient; projection of a uniform sky is the DC coefficient only.
- **b. The baker.** `openmw --bake-gi-exterior "<x>,<y>"` and a range form: for each exterior cell shoot `gi bake sky rays` through the Jolt world (as in `probebake.cpp`), keeping only the sky hit flag per ray; deterministic with `gi bake seed`; resumable; the interior baker's time-slicing. Check: two bakes give identical bytes; a killed and resumed bake equals an uninterrupted one; a cell with a bridge and one open field give visibly different fields in a debug view.
- **c. Runtime sampling.** `[Rendering] gi exterior`: load a cell's file in `configureAmbient` (and for neighbouring cells), a storage-buffer binding beside the interior probes (every view-set creator must bind dummies: see 0028's notes), the shader term `sky SH . visibility SH` replacing the flat ambient for statics, actors and, with D7, terrain; cell-border blending; the CPU sky SH uploaded per frame. Frame check at several hours (dawn, noon, dusk, night) and weathers: the shade under a bridge or tree follows the clock, an open field matches the old look, and `gi exterior = false` is frame-identical to before.
- **d. Sky environment image and reflections.** Build the sky image from the atmosphere model at a low resolution, refresh it when the sun has moved a set angle or the weather changes, and use it for the specular ambient on `pbr` surfaces outdoors (wet stone, metal armour, water if the water shader can take it later).
- **e. Cost and cleanup.** GPU timing and `tools/benchmark.sh` into a cost note; a debug view of the field (marker per probe, visibility as brightness); decisions recorded; module guide notes in `apps/openmw/mwrender/CLAUDE.md` and `components/render/CLAUDE.md`.

## Constraints

- Everything is off by default; with every new setting at its default the frames are identical to before (diffmap against the previous build, torch and weather flicker aside; fix the time and weather as task 0034's notes say).
- No heap allocation per frame in the sampling and upload paths; the sky SH is a fixed 27-float array.
- Determinism: a fixed seed, no wall-clock time, no unordered iteration where order matters (the baker's cell and probe order is sorted).
- Bakes are never committed and the spec's tests use generated data only.
- The editor's `SceneHost` must keep building and drawing; if it is to show the field it needs its own spec.
- No change to the weather records, Lua API or any save.

## Acceptance criteria

- [x] `openeden.skyvis` v1 round-trips byte for byte and reports located errors; file names follow the grid coordinates (stage a tests).
- [ ] The baker is deterministic and resumable (byte comparison: done, in unit tests and in the game on a wilderness cell), and its result differs visibly between an open field and a cell with a bridge, checked in a debug view (the difference is in the numbers and in a unit test with a slab; one debug frame was viewed, no bridge side by side).
- [ ] With `gi exterior` on and a bake present, the shade under a bridge or tree follows the time of day and weather in frames at four hours, and an open field matches the flat-ambient look within a measured tolerance. (Not met as written: a house wall was compared at dusk with the hour and the camera fixed, night on open ground matched, noon's weather varied between runs and dawn was only measured; the open-field tolerance was not measured because no place in the baked cell sees more than 0.84 of open ground's sky.)- [ ] With the setting off, or on with no bake, frames equal the previous build within run-to-run noise.
- [ ] Terrain, statics, groundcover and actors all take the field with no seams at cell borders (frames across a border).
- [x] The sky environment image drives specular ambient on `pbr` surfaces outdoors; metal armour no longer reads dark outside (frames).
- [x] The cost note records GPU time for the lookup and the sky update on this machine, with the GPU named.
- [ ] components-tests, openmw-tests, eden-studio-tests, the 32 Lua tests and the Linux gate pass; the whole tree passes `CI/check_clang_format.sh`; a fresh reviewer read each stage. (Windows: all pass and a reviewer read each stage; the format check passed at the stage c commit; the Linux gate is in the log below.)
Out of scope: bounced light and time-of-day-specific bakes (D12), real-time ray tracing (this field is the thing a ray-traced path would replace or refine; `RayWorld::cast` stays the seam, as task 0028 noted), volumetric clouds and fog, rain and wetness, interior changes, distant terrain beyond the loaded cells, the editor's view of the field, and anything that changes weather records or saves.


## What the next features must know
- **The ambient of exteriors is `flat * kept`.** `skyVisAmbient` (the shader and `skyvisgpu.hpp`) returns the flat ambient scaled per channel; it only ever darkens. Wetness (0037) should darken the diffuse and raise the specular, and can use the field as a "is it under cover" test: rain falls from above, so the cosine weighted share of the sky that a point sees around `+z` (`visCosine(V, up)`) is what a rain mask needs. The visibility SH is the one thing the baker stores.
- **The sky SH has height only.** `skyFromColours` gives a sky with no compass bearing and no sun. A sun term (a sun lobe, cloud shadows from task 0038, a lit horizon) would add azimuth to the `Sh9` and needs no change to the baked data; the header already holds all nine coefficients.
- **Bindings 12 and 13 exist, and `GI_SKY`.** Any new creator of a view descriptor set must add `View::dummySkyVis()` and `View::dummySkyVisSky()`. The reflection buffer (binding 11) now also holds the sky image outdoors (`RenderManager::mSkyEnvOwns`).
- **Terrain and groundcover are lit through the same lookup.** A new surface shader that wants the field includes `lib/view/skyvis.glsl` under `GI_SKY` and calls `skyVisAmbient(worldPos, worldNormal, flat)`.
- **Frame comparisons outdoors:** `tcl`, `player->SetPos`, `SetAngle z` and a fixed hour keep the camera still when the game starts in the exterior (`--start "Seyda Neen"`); starting in `ToddTest` and using `coc` raced the placement. Weather at noon still differed between runs (fog); dusk and night were steady (`scratchpad/gi36/run.sh`, `shot.sh`).

## Decisions toddhow can change (made while building)
- **The formula (the main one).** The spec's plain dot product of the sky's SH and the visibility SH gives half the flat ambient on open ground (the lower half of the sphere is blocked by the ground) and has no direction. The result is instead the flat ambient scaled by how much of the cosine weighted sky the surface facing a normal sees, against open flat ground, with the sky's colour entering as a shift. Open ground keeps the old look. The alternative is the plain dot product with a hand scale.
- **The lattice.** Columns follow the terrain height, 8 levels from 64 to 960 units above it, not a world aligned lattice with pruning. A cell is always 33 x 33 x 8 probes.
- **The file.** SH numbers are 16-bit integers over 4096 (floats at 8 digits would be about twice the size; a cell is about 450 KB measured). `grid.spacing` may be 128 to 2048.
- **Strength default 1.** At 1 the ambient of a place under a hillside or between houses drops to a third or less of the flat ambient; Seyda Neen's open ground reads about 0.8. A default of 0.5 to 0.7 may look closer to the old Morrowind; judge it by eye. The strength scales the darkening only.
- **Enclosed probes are filled from their neighbours** when no ray escapes and all stop within 2048 units, so a building's dark inside does not leak onto its walls. A hall seen from below with a thick roof is not distinguishable from this and is also filled.
- **Requirements** as D11 plus `atmosphere` (D6). `gi exterior` works without `gi`.
- **File names:** `openeden/probes/exterior/<x>_<y>.skyvis.json`; the baker writes `.partial` and `.progress` beside it while it runs.

## Stage log

### 2026-11-01, stages a to e (Windows, RTX 4070, `build-jolt`)

**Done and verified.**
- a: `skyvisibility.hpp` and the file in `probes.cpp`, `skyvismath.hpp`; 22 tests written first (round trip, located errors, hostile input, SH maths). A fresh reviewer found a file size cap that was too large and an unguarded spacing (fixed).
- b: `skyvisbake.*` (the pure baker, 17 tests with a made-up world: determinism, slicing, resume, border columns bit equal between cells, invalid probes filled) and `OMW::SkyVisBaker` (`openmw --bake-gi-exterior "x,y"` or `"x0,y0:x1,y1"`): a cell takes about 3 s; two bakes of Seyda Neen are byte identical; a wilderness cell killed after its 30 s save and resumed is byte identical to an uninterrupted bake; the two cells of a pair agree on all 33 border columns (game data). Seyda Neen bakes differ in a few columns between a resumed and an uninterrupted run: something in the village moves during a bake (not found; an uninterrupted pair is identical). Reviewer: an overflow in the cell count check (fixed), an unchecked stream write (fixed), a ground ray that can miss on a terrain edge (retries a unit to each side added).
- c: the runtime (two buffers, `GI_SKY`, terrain and objects, cell hooks), the C++ twin, 22 tests. Frames: with `gi exterior strength = 0` the frame equals the `off` frame (summed difference 245, two `off` frames 379); dusk, a house wall: darkened by the field with its detail kept; night on open ground: equal within noise. The frame check found that a building's sealed probes leaked darkness onto its wall (the wall nearly black): enclosed probes are now filled in (a first limit of 512 units left the wall too dark; 2048 is what the last frames show). Reviewer: a pool too small for a 5 by 5 grid (now 27 cells), file reads under the lock (moved out).
- d: the sky image (6 tests). Frames: houses marked as metal by a model override read black outdoors without it and show the sky with it (`scratchpad/gi36/m18off.png`, `m18on.png`). Reviewer: the ambient drift rebuilt the image every frame (now only its header is patched).
- e: `gi debug` shows markers around the camera (4 tests, one frame viewed), the cost note `docs/research/0016-gi-exterior-cost.md` (+0.18 to 0.27 ms GPU in a terrain heavy scene), the module guides.
- Linux gate (Docker, `-Werror`) at `2db79c8379`: components-tests 2911, openmw-tests 799 and the 32 Lua tests pass; `CI/check_clang_format.sh`, `check_file_names.sh`, `check_no_osg.sh` and `check_extension_sdk.sh` pass.
- Windows: components-tests, openmw-tests, eden-studio-tests and the 32 Lua tests pass (the Lua tests were run after stage c; stages d and e touch only `gi exterior` paths).

**Not done / not verified.** `gi exterior` off against the previous build by frames (only strength 0 against off was compared); a baked file present while a cell has none; frames across a cell border, with groundcover and with actors specifically; the four hour comparison with a fixed weather; a bridge beside open ground in the debug view; the stall of parsing and uploading at a cell crossing; bounce light, a sun lobe and cloud shadows (not built); the editor's view of the field.
