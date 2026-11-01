# Task: Rendering weather, stage 3: snow and ash cover
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R6: "snow and ash gather on upward-facing surfaces, sheltered spots stay dry"); builds on `docs/tasks/0026-rendering-f3-stored-gbuffer.md` (stored normals), `0027-rendering-f4-material-model.md`, `0037-rendering-wetness-and-puddles.md` (the wetness value and the shelter mask, reused) and `0034-rendering-weather-atmosphere.md`.
Goal: while it snows or ash falls, and for a while after, upward-facing outdoor surfaces gather a layer of snow or ash that fades back to bare surfaces when the weather clears, driven by Morrowind's existing weather types and sheltered places staying clear.
Inherited code touched: not yet confirmed; to be named at the commit that starts each stage. Expected: `apps/openmw/mwworld/weather.cpp` and `weatherdata.hpp` (weather types: the snow weather and Red Mountain's ash storm and blight types; their ids and transitions), `apps/openmw/mwrender/weathereffects.*` and `rain.*` (precipitation particles), `apps/openmw/mwrender/precipitationoccluder.*`, `files/shaders/lib/material/terms.glsl`, `files/shaders/default/main.frag`, `files/shaders/terrain/main.frag`, the cover textures (see D2).
Inputs / outputs: new `[Rendering]` settings `cover` (bool, false), `cover strength` (0 to 1, 1), `cover build time` and `cover melt time` (seconds of game time, defaults commented with their source); a pure-maths header `components/render/covermath.hpp`; cover colour and roughness values; no change to saves, Lua, contracts, weather records or `ChangeWeather`.

Status: stages a to d built 2026-11-01 (see the stage log); not merged. Full loop (Track V renderer).

## Design, in plain language

Snow and ash do not lie on a wall; they lie on things that face upwards. We have each pixel's normal (task 0026), so how "upward" a surface is is known: the Z component of its world normal (the game is Z up). A **cover amount** (0 bare to 1 buried) builds while the weather is snowing or ashing, melts after, and is held back where a surface is sheltered (the same shelter mask as task 0037). In the shader, the surface colour is blended towards a cover colour by `cover amount × upwardness`, thresholded and broken up with a tiling noise so edges are ragged, not a smooth gradient, and roughness is set to the cover material's roughness (snow is rough and bright; fresh ash is rough and dark grey; wet snow, later, is glossier).

The types matter: snow weather builds snow (white, subsurface-soft, bright), ash storm and blight build ash (dark grey, fine, not bright), and rain, clear and cloudy build nothing. Mods that add weather types keep working: a weather with the glare and particle properties of snow is not special-cased by name; the *type* the weather manager already uses for its particles (snow versus rain versus ash) drives the cover kind.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | What drives cover | The weather manager's precipitation kind (snow, ash) and transition, building over `cover build time` and melting over `cover melt time` (snow melts, ash does not melt but fades slowly) | A per-region state (a weather simulation: separate spec) |
| D2 | Textures | None required: cover colour, roughness and the edge noise are computed in the shader from constants and a tiling noise made at start; optional detail texture later through the VFS | New art assets (we have none and the no-game-assets rule applies to Bethesda files only; licence recorded if added) |
| D3 | Upwardness | The world normal's Z with a smooth threshold, so slopes up to about 45 degrees gather cover and walls do not | Hard threshold |
| D4 | Shelter | The mask from task 0037 (under roofs there is no cover) | None |
| D5 | Ash and the existing rules | Cover is visual only: ash storm and blight damage, sounds and scripts are untouched | |
| D6 | Terrain and statics | Both; groundcover and trees get tinted tops only; actors and held items none | Everything |
| D7 | Requirements | `hdr`, `pbr`, `gbuffer`; shares `wetness`'s shelter code, so that task's stage c comes first | Fewer |

## Stages

- **a. Maths.** `covermath.hpp`: build and melt curves, upwardness, ragged-edge blend, per-kind colour and roughness, with shader twins; tests.
- **b. Snow cover on statics and terrain** behind `[Rendering] cover`, using the shelter mask; frames in snow weather and after it clears at fixed times.
- **c. Ash cover** and the blight case; frames on Red Mountain's weathers where available, or by `ChangeWeather`.
- **d. Cost, debug view** (cover amount and upwardness), notes.

## Constraints

- Off by default and frame-identical when off and when on in non-snow weather (diffmap at fixed time and weather).
- No allocation per frame; game time, not the wall clock; no unseeded randomness (the noise is a fixed tiling texture).
- Weather records and all gameplay effects untouched.
- The editor keeps building and drawing; it does not take cover.

## Acceptance criteria

- [x] The pure maths and its shader twins agree (tests: no cover on walls, full on flat tops, build and melt monotonic and bounded, kinds differ; the shader's constants are read back from `cover.glsl` by a test, its functions are compared by reading and by frames).
- [x] In snow weather, flat and gently sloped outdoor surfaces become white with ragged edges while walls stay clear (frames). [ ] After the snow stops it melts over `cover melt time` (unit-tested curve only, not seen in the game).
- [x] In ash storm and blight weather surfaces gather dark grey ash (frames); the slow fade is unit-tested only.
- [ ] Sheltered places stay clear (frames under roofs and arches): not verified. The code is task 0037's shelter test, which that task saw working in the debug view for one doorway.
- [x] Rain and clear weather unchanged with cover on, and off is frame-identical to the previous build (diffmaps at a fixed time and weather; only snow flakes, rain drops and moving people differ). [ ] Cloudy and interiors not framed (an interior never receives cover: the weather manager tells the renderer none).
- [x] Cost note recorded (`docs/research/0020-cover-cost.md`); unit tests, the 32 Lua tests, the Linux gate and the format check pass; a fresh reviewer read the whole change once, not each stage on its own (one change set).
Out of scope: rain wetness (0037), precipitation particles (0040), a weather simulation, footprints or deformable snow, new weather types, sound changes, the editor.

## Decisions made while building (2026-11-01)

- **D7 changed: `gbuffer` is not needed.** The spec asked for `hdr`, `pbr` and `gbuffer` because the stored normals were the source of upwardness. Reading the shaders, `default/main.frag` and `terrain/main.frag` already have the pixel's own normal (`viewNormal`) when they light, so the cover is applied there, before the lighting, and needs only `hdr` and `pbr` (the same as wetness, task 0037). Nothing is read from the G-buffer.
- **D2 as built:** no texture. The ragged edge is the wetness task's value noise (`wetNoise`, hash based, so deterministic) at 36 game units, not a texture made at start: it is the same code in C++ and GLSL and needs no upload.
- **D1 as built:** the kind comes from the weather result's particle file (`mParticleEffect`), compared with the `[Models]` settings `weathersnow` and `weatherblizzard` (snow) and `weatherashcloud` and `weatherblightcloud` (ash), so a mod weather that uses those particles builds cover and any other weather builds none. Blizzard counts as snow (it is Solstheim's snow storm). The amount follows the weather result's `mPrecipitationAlpha` through a transition. `cover build time` default 1200 s, `cover melt time` 7200 s of game time (chosen values); ash fades four times slower (`Render::Cover::ashFadeFactor`).
- **Where the value rides:** `Scene.ambient.w` shares the wetness's alpha: `1 + wetness + 2 x code`, the code 1 to 128 for snow and 129 to 256 for ash in steps of 1/128 of the cover (`Render::Cover::pack`). Without cover the alpha is exactly the old `1 + wetness`, and `wetness.glsl` removes the code before reading the wetness. Only the larger of snow and ash on the ground is drawn at a time.
- **D6 as built:** statics, terrain, groundcover and actors share the lit path. Skin and glowing surfaces stay bare, as for wetness; there is no way in the shader to tell armour on an actor from a static, so an actor's upward-facing armour gathers cover. Trees are drawn the same as other statics.
- **Terrain keeps its shading, not its hue:** the land colour map's green and brown tint is pulled to grey under cover, or snow came out olive in the first frame.
- **Colours** (linear albedo): snow 0.78, 0.80, 0.84, roughness 0.75; ash 0.075, 0.072, 0.07, roughness 0.92. The first ash colour (0.13) read as snow-like pale caps on dark rocks in the blight frame, so it was darkened.
- **Slang or GLSL:** everything is GLSL. No new shader file exists: `lib/material/cover.glsl` is an include for the existing GLSL shaders `default/main.frag` and `terrain/main.frag` (decision 0009 allows edits and new includes of existing GLSL shaders to stay GLSL), and it also includes `lib/material/wetness.glsl`. Porting those shaders is a later task.

## Stage log

### 2026-11-01, stages a to d (Windows, RTX 4070, `build-jolt`; commits `1ef4625797` and `67cfd27d1b`)

**Done and verified.**
- a: `covermath.hpp` and 34 tests written first (curves bounded and monotonic, ash slower than snow, upwardness zero on walls and one on flat tops, ragged mask, kinds differ, packing round trip and "no cover is exactly the old wetness alpha", bad inputs, and the shader's constants read back from `cover.glsl`). `WeatherManager::updateCover`.
- b: snow on statics and terrain behind `[Rendering] cover`. Frames at Seyda Neen, snow, noon, `cover build time = 2`: roofs, rocks and the hill gather white with walls clear; at `cover build time = 6` a frame caught the ground half covered and the edge is ragged patches. The first frame showed olive terrain (the land colour map's tint): fixed by pulling that tint to grey under cover.
- c: ash and blight: dark grey ash on the ground and roofs; the first ash colour was too light on dark rocks and was darkened. Blizzard builds snow.
- d: the debug view (`cover debug`: cover amount red, upwardness green, covered share blue; checked on the snow frame, roofs and rocks pink and white, walls red), the cost note, the module guides.
- Off means unchanged: a frame of the previous build's executable (its own defaults) against the new one with `cover` off at the same time and weather: the difference is snow flakes and walking people only (diffmap viewed). `cover` on in clear weather and in rain (with `wetness` on): the same as off apart from rain drops. An interior (`ToddTest`) with `cover` on: identical to off (difference 0).
- Gates: `components-tests` 3070, `openmw-tests` 799, `eden-studio-tests` 520, `openmw-cs-tests` 231 on Windows; all 32 Lua tests on Windows; on Linux (commit `3821a64ec8`, CI's flags with `-Werror`) the build, `components-tests` 3046, `openmw-tests` 799 and 32 Lua tests pass; `CI/check_clang_format.sh` (clang-format 14), `check_file_names.sh`, `check_extension_sdk.sh` and `check_no_osg.sh` pass. A fresh reviewer read the diff and found no defects (notes below).

**Not done / not verified.** The melt and the slow fade of ash in the game (only the curves' unit tests: the check script runs at start and the weather cannot be changed later in one run); a frame under a roof, bridge or arch (the shelter code is task 0037's, and needs `gi exterior` with a baked cell; without one, cover also falls on exterior roofs' undersides, which face down and so get none, but on ground under a roof); cloudy weather and Red Mountain's own region (a script used `ChangeWeather` in the Bitter Coast); the editor (it does not take cover: its views leave the ambient alpha at 1 or 0); the shader's functions compared with the C++ ones by a machine (only the constants are, and the packing is unit-tested on the C++ side only); actors and groundcover specifically. Cost: +0.03 ms of GPU in snow in one scene.

**Notes from the review.** Cover under a roof needs the sky visibility file as wetness does. Waiting or sleeping advances cover by only a few frames, as with wetness. The new shader code has two literals without a source comment (the glow and skin tests copied from the wetness code). The packing is by the spare alpha, so a later task that needs that alpha must read `lib/material/cover.glsl`'s `coverUnpack` and `wetness.glsl`'s decode first.

**For task 0040 (precipitation, lightning, sun glow).** `mWetness` and `mCover` (`WeatherManager`) are the places for weather state; the scene block's ambient alpha is now full (wetness, 0 to 1, plus 2 times the cover code). Do not add another field to it: use the cloud header buffer (binding 13, `cloudgpu.hpp`) or push constants. The kind of particles that builds cover is decided in `WeatherManager::updateCover` by comparing `mResult.mParticleEffect` with the `[Models]` particle files.
