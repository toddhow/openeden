# Task: Rendering weather, stage 4: precipitation, lightning and the sun glow in the fog
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R6: "lightning flashes light the scene"); `docs/tasks/0034-rendering-weather-atmosphere.md` ("Not started": precipitation changes, lightning flashes, a sun glow in the fog); builds on `0025-rendering-f2-linear-hdr.md`, `0026-rendering-f3-stored-gbuffer.md` and `0037-rendering-wetness-and-puddles.md`.
Goal: three weather effects that are each small but change the feel: precipitation that is lit by the scene and shows motion, lightning that actually lights the world for a moment, and a warm glow in the fog and haze around the sun.
Inherited code touched: not yet confirmed; to be named at the commit that starts each stage. Expected: `apps/openmw/mwrender/rain.*` and `weathereffects.*` (precipitation particles), `apps/openmw/mwrender/weather.*` (the `Sky` class: thunder and lightning flash, which today is a colour change), `apps/openmw/mwworld/weather.cpp` (thunder frequency, flash timing, the lightning colour from the weather record), `files/shaders/sky/*` and `files/shaders/lib/fog.glsl` (or the fog function in the default shader; confirm the file), `files/shaders/lib/light/*`, `components/render/atmospheremath.hpp`.
Inputs / outputs: new `[Rendering]` settings `precipitation lighting` (bool, false), `lightning` (bool, false), `lightning strength` (0 to 2, default 0.7 since 2026-11-01; was 1), `sun glow` (bool, false), `sun glow strength` (0 to 2, 1); pure-maths headers `components/render/lightningmath.hpp` and additions to `atmospheremath.hpp`; no change to weather records, scripts, sounds or saves.

Status: built 2026-11-01, stages a to e (see the stage log). Full loop (Track V renderer).

## Design, in plain language

**Precipitation lighting.** Rain and snow streaks are particles drawn with a fixed colour. They should pick up the light of the scene: nearly black on a dark night away from lights, bright where a lamp or torch lights them, and warmer near the sun at dusk. We add a light term to the precipitation shader from the sun colour (task 0034) and the light grid's nearby lights, plus a fade with distance so far streaks blend into the fog and a stretch along the motion so fast rain reads as streaks. The number of particles and the weather rules are unchanged.

**Lightning.** Today a thunderstorm's flash lightens the sky colour. With `lightning` on, the flash is also a light: for the few frames of the flash the scene gets a brief boost of directional light from the cloud direction (random but seeded, so reproducible) with a cold white-blue colour from the weather record, a short bright sky, and a fast decay curve with the double flicker real lightning has. It lights surfaces (so shadowed walls show the flash) and shows in the puddles of task 0037. The timing comes from the weather manager's existing thunder schedule, so the thunder sound still lines up.

**Sun glow.** In haze and fog the air near the sun looks warm and bright because of forward scattering (light bending slightly forward around the particles). We add a forward-scattering lobe (a Henyey-Greenstein phase function) to the fog colour in the direction of the sun, using the atmosphere model's sun colour and the fog density, so a foggy sunrise glows around the sun. It needs no volume data; the true froxel version is task 0041.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | Precipitation light | Per-particle colour from the sun colour and the light grid at the particle position, with distance fade and motion stretch | Keep the fixed colour |
| D2 | Lightning as light | A short extra directional light with seeded direction, the weather record's colour, a double-flicker decay; shown only outdoors and, attenuated, through windows is out of scope | Only brighten the ambient (cheap, flat) |
| D3 | Seed | The flash direction comes from a fixed-seed generator advanced by the thunder schedule's flash count, so identical runs match | Wall clock or unseeded randomness (refused by the determinism rule) |
| D4 | Sun glow | A Henyey-Greenstein lobe added to the fog colour toward the sun, strength from the fog density and the atmosphere model | A texture-based glow |
| D5 | Requirements | `hdr` for all three; sun glow also needs `atmosphere`; lightning light benefits from `pbr` and `gbuffer` but works without | |
| D6 | Photosensitivity | Lightning flash intensity is limited by `lightning strength`, with a hard cap on peak luminance and a minimum interval, so flashes cannot strobe | |
| D7 | Interiors | Unaffected, except that lightning does not light interiors | Through windows (later) |

## Stages

- **a. Maths.** The flash curve and cap, the fixed-seed direction, the Henyey-Greenstein lobe and the precipitation light function, each with a shader twin; tests including the photosensitivity cap and the minimum interval.
- **b. Precipitation lighting** behind `precipitation lighting`; frames at night with a torch, at dusk and at noon in rain and snow.
- **c. Sun glow** behind `sun glow`; frames in foggy weather at sunrise and sunset.
- **d. Lightning** behind `lightning`; frames across a flash sequence (capture several frames in a row with a fixed schedule), including the puddle highlights when `wetness` is on.
- **e. Cost, debug views, notes.**

## Constraints

- Off by default and frame-identical when off; with the settings on and clear weather, identical too.
- Determinism: the flash and the glow use game time and the seeded generator only.
- No allocation per frame; the weather manager's thunder schedule stays the source of timing, so sounds line up.
- Safety: the lightning cap and minimum interval of D6 are tested and documented; a flash cannot exceed the cap whatever the strength setting.
- Weather records, sounds, scripts and saves untouched. The editor keeps building and drawing; it does not take these.

## Acceptance criteria

- [x] The pure maths and shader twins agree (constants read back from the shader files by tests; formulas compared by a reviewer line by line); the flash never exceeds the cap and never repeats faster than the minimum interval (tests, including dense storms and the one-second window).
- [x] Rain streaks are lit by the torch near them, dark in the dark, brighter against the low sun (night and dusk frames); snow is dark at night and brighter by day (frames). [ ] "Warm at dusk" for snow not framed; lamps do not light snow.
- [x] A foggy sunset glows around the sun and not elsewhere (debug view and diff maps, sky and distant objects). [ ] The sunrise glow is weak in frames (the sun was behind a wall) and the effect at strength 1 is subtle by design; warmth was seen in the debug view only.
- [x] A flash lights outdoor surfaces and decays with a double flicker (frames at ages 0.05, 0.14, 0.25 and 0.6 s, at night and in a thunderstorm with puddles); the same seed gives the same directions (unit test). [ ] The vanilla random schedule driving a flash in the game was not watched; only the debug-age aid was framed.
- [x] All three settings off, or on in clear weather, are frame-identical within noise (diff maps: only moving people and water); an interior with them on is identical (difference 0).
- [x] Cost note recorded below. Unit tests and the 32 Lua tests pass on Windows. [ ] Linux gate and format check: see the stage log. A fresh reviewer read the whole change once, not each stage (one change set).
Out of scope: wetness (0037), cloud shading (0038), snow and ash cover (0039), volumetric fog and light shafts (0041), lightning through windows, new precipitation types, thunder sound changes, a weather simulation, the editor.

## Decisions made while building (2026-11-01)

- **D2 changed: one merged light, not a second light.** The weather manager adds the flash (capped, cold, double flicker) to the sun, ambient and fog colours in place of the vanilla white one, and the renderer tilts the sun's direction (and so the shadow cascades) towards the flash by its share of the light (`Render::Lightning::mergedDirection`, `RenderManager::updateSceneData`). No shader code or buffer is needed, and the flash casts shadows. The price: one direction for all surfaces, and the sun's own direction is lost for the length of a flash.
- **The colour is not from the weather record:** Morrowind's records hold only thunder frequency, threshold and flash decrement. The flash colour (0.80, 0.90, 1.0) is a chosen constant.
- **D3:** the direction is a hash of a fixed seed and the strike count (no generator state, no clock). Test aid `lightning debug age` holds one flash at an age, since the vanilla schedule is random.
- **D6 as built:** cap 1 (the closest vanilla strike), minimum interval 1.5 s between flash starts, two strokes 0.22 s apart: at most two peaks in any second (WCAG 2.2 criterion 2.3.1 allows three). Flashes this size at night are strong; toddhow may want a lower default `lightning strength` (0.7 reads more gently).
- **D1 changed for snow:** snow already took the shader's ambient light; its NIF material has a constant emissive of 0.3 that lit it at night. With the setting on that emissive is replaced by the sun's part (one colour for the whole fall). Lamps do not light snow. Rain is lit per pixel with lamps (`PRECIPITATION_LIT`).
- **D4:** haze weight is `1 - Glare_View` (the weather's clearness); brightness follows the fog's, colour is the sun's after the air (warm when low); also applied to the sky dome. Not on the cloud layer, the water or the sun disc.
- **Slang or GLSL:** all GLSL. The only new shader file, `lib/light/precipitation.glsl`, is an include of the existing GLSL `default/main.frag` (decision 0009 only requires new shaders to be Slang; porting `main.frag` is a later task).
- **Known interaction:** with `sun glow` and `lightning` both on in haze, the glow follows the tilted light and the flash-brightened colour during a flash (a warm halo around the flash direction, even at night). Left as is.

## Stage log

### 2026-11-01, stages a to e (Windows, RTX 4070, `build-jolt`)

Tests first (46 then 51 new unit tests: lightning, sun glow, precipitation), then the code. Frames compared with an older executable built from the same shaders at HEAD (settings off).

- **a/c Sun glow:** debug view and diff maps show a warm halo round the sun in fog (sky and fogged distance); at strength 1 the real frame is subtly brighter; clear weather identical.
- **b Precipitation:** rain at night with a torch (streaks lit by the torch, long streaks), at dusk looking towards the sun (brighter streaks); snow at night (dimmer than before) and noon (brighter). The first version lit rain with the NIF's fixed normal; replaced by the scattering light.
- **d Lightning:** frames at ages 0.05, 0.14, 0.25 and 0.6 s at night (flash, dip, second stroke, gone) and in a thunderstorm with wetness puddles (puddles and wet ground brighten). Interiors unchanged.
- **e Cost** (`tools/benchmark.sh` method, copied scenes: rain at dusk, fog at sunset, a thunderstorm, 1280 x 720, main scene GPU median): rain 0.118 to 0.131 or 0.146 ms; fog 0.118 to 0.131-0.132 ms; storm 0.118 or 0.190 to 0.147 or 0.203 ms (the storm's own run-to-run spread is 0.07 ms). All three on cost about 0.01 to 0.03 ms on this machine. The debug view is `sun glow debug`; the lightning aid is `lightning debug age`.
- **Not verified:** a flash driven by the game's own thunder schedule, a sunrise glow frame facing the sun, Linux gate and clang-format (see the final report), lightning through a window (out of scope).
- **Review notes:** a fresh reviewer found no way around the cap or interval; fixed its minor points (NaN clamp, stale `ambientPart` under `GBUFFER_AMBIENT` for rain, comments). Open: no `WeatherManager` integration test for the cap (the maths is tested; the manager is not unit-testable without the game).

**For task 0041 (volumetric fog):** the sun glow lives in `applyFog(src, viewPos)` and the sky dome, brightness from the fog's colour; a froxel fog should replace both terms and keep `cSunGlow` and `1 - Glare_View` as the haze weight. `scene.lightViewPos` is tilted during a lightning flash, and the scene's light colour includes the flash.

### 2026-11-01, `lightning strength` default 0.7 (decision 0010)
- `[Rendering] lightning strength` now defaults to 0.7 (was 1) in `files/settings-default.cfg`, because toddhow found the flash too bright. The cap (1, the closest vanilla strike) and the 1.5 s minimum spacing are untouched and do not depend on the setting; strength scales the flash below the cap, so a peak is now at most 0.7 of what it was for the same strike. `lightning = false` (the default) is unchanged. Tests are unaffected (they pass a strength explicitly).
