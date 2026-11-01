# Task: Rendering weather, stage 3: snow and ash cover
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R6: "snow and ash gather on upward-facing surfaces, sheltered spots stay dry"); builds on `docs/tasks/0026-rendering-f3-stored-gbuffer.md` (stored normals), `0027-rendering-f4-material-model.md`, `0037-rendering-wetness-and-puddles.md` (the wetness value and the shelter mask, reused) and `0034-rendering-weather-atmosphere.md`.
Goal: while it snows or ash falls, and for a while after, upward-facing outdoor surfaces gather a layer of snow or ash that fades back to bare surfaces when the weather clears, driven by Morrowind's existing weather types and sheltered places staying clear.
Inherited code touched: not yet confirmed; to be named at the commit that starts each stage. Expected: `apps/openmw/mwworld/weather.cpp` and `weatherdata.hpp` (weather types: the snow weather and Red Mountain's ash storm and blight types; their ids and transitions), `apps/openmw/mwrender/weathereffects.*` and `rain.*` (precipitation particles), `apps/openmw/mwrender/precipitationoccluder.*`, `files/shaders/lib/material/terms.glsl`, `files/shaders/default/main.frag`, `files/shaders/terrain/main.frag`, the cover textures (see D2).
Inputs / outputs: new `[Rendering]` settings `cover` (bool, false), `cover strength` (0 to 1, 1), `cover build time` and `cover melt time` (seconds of game time, defaults commented with their source); a pure-maths header `components/render/covermath.hpp`; cover colour and roughness values; no change to saves, Lua, contracts, weather records or `ChangeWeather`.

Status: draft, written 2026-11-01 at toddhow's request. Nothing is built. Full loop (Track V renderer) when it starts.

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

- [ ] The pure maths and its shader twins agree (tests: no cover on walls, full on flat tops, build and melt monotonic and bounded, kinds differ).
- [ ] In snow weather, flat and gently sloped outdoor surfaces become white with ragged edges while walls stay clear (frames); after the snow stops it melts over `cover melt time`.
- [ ] In ash storm and blight weather surfaces gather dark grey ash, without melting quickly (frames).
- [ ] Sheltered places stay clear (frames under roofs and arches).
- [ ] Rain, clear and cloudy weather and interiors are unchanged; off is frame-identical within noise.
- [ ] Cost note recorded; unit tests, the 32 Lua tests, the Linux gate and the format check pass; a fresh reviewer read each stage.
Out of scope: rain wetness (0037), precipitation particles (0040), a weather simulation, footprints or deformable snow, new weather types, sound changes, the editor.
