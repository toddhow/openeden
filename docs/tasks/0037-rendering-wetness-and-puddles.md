# Task: Rendering weather surfaces, stage 1: rain wetness and puddles
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R6, "visual weather state": "wet means darker and glossier, puddles on flat ground, sheltered spots stay dry"); builds on `docs/tasks/0026-rendering-f3-stored-gbuffer.md` (stored normals and depth), `0027-rendering-f4-material-model.md` (roughness, reflectance), `0029-rendering-ao.md` and `0034-rendering-weather-atmosphere.md`.
Goal: while it rains, and for a while after, outdoor surfaces get darker and glossier, flat ground collects puddles that reflect the sky, and places under a roof stay dry, all driven by Morrowind's existing weather so scripts, records and sounds are unchanged.
Inherited code touched: not yet confirmed; to be named at the commit that starts each stage. Expected: `apps/openmw/mwworld/weather.cpp` and `weatherdata.hpp` (the weather type and transition, where a wetness value is derived), `apps/openmw/mwrender/weathereffects.*` and `rain.*` (precipitation), `apps/openmw/mwrender/precipitationoccluder.*` (what is sheltered), `apps/openmw/mwrender/rendermanager.*` (scene constants), `files/shaders/lib/material/terms.glsl` and `pbr.glsl` (roughness and reflectance), `files/shaders/default/main.frag`, `files/shaders/terrain/main.frag`, `components/render/atmospheremath.hpp` (the pattern for pure maths).
Inputs / outputs: new `[Rendering]` settings `wetness` (bool, false), `wetness strength` (0 to 1, 1), `wetness puddles` (bool, true), `wetness dry time` (seconds of game time for a surface to dry, with a default and a comment on its source); new pure-maths header `components/render/wetnessmath.hpp`; a wetness value (0 dry to 1 soaked) in the scene constants; no change to any save, Lua API, extension contract, weather record or `ChangeWeather` behaviour.

Status: draft, written 2026-11-01 at toddhow's request. Nothing is built. Full loop (Track V renderer) when it starts.

## Design, in plain language

Real wet surfaces look different in three ways, and each has a cheap version:
- **Darker.** Water filling the pores makes a surface absorb more light. We multiply the surface's diffuse colour by a factor below one (stronger for porous things such as dirt and plaster than for polished things).
- **Glossier.** A thin water film is smooth, so roughness drops towards a low value and reflections get sharper. Task 0027's material model already has roughness per pixel, so wetness blends it towards a wet roughness.
- **Puddles.** In dips and on flat ground water collects. We only know a surface's slope (its stored normal) and not its dips, so v1 puddles form on near-horizontal surfaces, broken up by a tiling noise texture so they are patches and not a sheet, and they reflect the sky.

A **wetness** number (0 to 1) rises while the weather is raining (the weather manager already knows the type and how far a transition has gone) and falls slowly after, so the ground is still damp for a while when the rain stops. It is a property of the weather, not of each surface, with one exception: a surface *under cover* stays dry. The game already finds covered places for rain particles (`precipitationoccluder.*`); we reuse the same idea (a top-down depth of the sheltering geometry) as a per-pixel mask.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | What drives wetness | The weather manager's current weather type and transition (rain and thunderstorm wet, snow and ash do not, clear and cloudy dry), easing up over about 20 seconds of game time and down over `wetness dry time` | A separate state machine for weather (this is the "weather simulation" part of R6, a different spec) |
| D2 | Where wetness is applied | In the lighting shader from the scene constant, per pixel, modified by the shelter mask | A material override per model (data-heavy) |
| D3 | Shelter | A top-down depth map of the covering geometry around the camera (the same data the rain occluder needs), refreshed when the camera moves; a pixel below the map's depth is covered | Per-object flags; ray tests per pixel (too expensive) |
| D4 | Puddles | Near-horizontal normal and a tiling noise mask, with the sky reflected through the sky environment image (needs `pbr`); a ripple normal from the existing water ripple texture while rain falls | No puddles (darker and glossier only) |
| D5 | What stays dry | Interiors, covered places, and surfaces flagged skin or emissive; water itself is untouched | Everything gets wet |
| D6 | Requirements | `hdr`, `pbr`, `gbuffer` (stored normals) for puddles; darker and glossier work with `hdr` and `pbr` only | Fewer |
| D7 | Grass, trees and groundcover | Get the darker and glossier terms only (no puddles) | Nothing |
| D8 | Actors | Get the darker and glossier terms outdoors in the rain, not under cover | None |

## Stages

- **a. Maths and the wetness value.** `wetnessmath.hpp` (the wetness curve from weather type, transition and elapsed time; the darkening, roughness and puddle-coverage functions as pure functions with C++ twins of the shader code). The weather manager computes the value, kept in `Render` scene constants, with no effect yet.
- **b. Darker and glossier.** Apply the two terms in the lighting shader for statics, terrain, groundcover and actors, behind `[Rendering] wetness`. Frame check in rain against dry at the same fixed time (task 0034's notes say how to fix time and weather).
- **c. Shelter mask.** The top-down shelter depth map and the covered-pixel test; check under eaves, bridges, doorways and trees.
- **d. Puddles and sky reflection.** The noise-broken patches on near-horizontal surfaces, reflecting the sky from the exterior sky image of task 0036 when it exists and from the atmosphere model's sky colour otherwise, plus rain ripples.
- **e. Cost, debug view, notes.** GPU timing and `tools/benchmark.sh` into a note; a debug view showing wetness and the shelter mask; module guide entries.

## Constraints

- Off by default and frame-identical when off, compared by `diffmap` against the previous build at a fixed time and weather. With `wetness` on and a dry weather, frames are also identical.
- No heap allocation per frame; the shelter map is allocated at start.
- Determinism: no wall-clock time; the wetness curve uses game time from the weather manager.
- The weather records, `ChangeWeather`, the sounds, the ash storm and blight rules and Lua are untouched; a weather mod keeps working because the type, not the colour tables, drives wetness.
- Never invent engine facts: each stage names the file and function it touches at its commit.
- The editor's `SceneHost` keeps building and drawing; it does not take wetness.

## Acceptance criteria

- [ ] The wetness curve and the shader twins agree (unit tests of the pure maths, including the rise, fall and no-negative cases).
- [ ] In rain, outdoor ground and walls are darker and glossier than at the same time and place when dry (frames viewed, compared by `diffmap`).
- [ ] Surfaces under a roof, bridge or doorway stay dry (frames).
- [ ] Puddles appear on flat ground only, reflect the sky, and show ripples while it rains (frames).
- [ ] After the rain stops the surfaces dry over `wetness dry time`.
- [ ] Setting off, or on in dry weather, is frame-identical to the previous build within noise.
- [ ] Cost note recorded; unit tests, the 32 Lua tests, the Linux gate and the whole-tree format check pass; a fresh reviewer read each stage.
Out of scope: snow and ash cover (task 0039), precipitation particles and lightning (0040), cloud shadows (0038), volumetric fog (0041), new weather states or a weather simulation, water shading, sound changes, the editor.
