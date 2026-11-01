# Task: Rendering weather, stage 4: precipitation, lightning and the sun glow in the fog
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R6: "lightning flashes light the scene"); `docs/tasks/0034-rendering-weather-atmosphere.md` ("Not started": precipitation changes, lightning flashes, a sun glow in the fog); builds on `0025-rendering-f2-linear-hdr.md`, `0026-rendering-f3-stored-gbuffer.md` and `0037-rendering-wetness-and-puddles.md`.
Goal: three weather effects that are each small but change the feel: precipitation that is lit by the scene and shows motion, lightning that actually lights the world for a moment, and a warm glow in the fog and haze around the sun.
Inherited code touched: not yet confirmed; to be named at the commit that starts each stage. Expected: `apps/openmw/mwrender/rain.*` and `weathereffects.*` (precipitation particles), `apps/openmw/mwrender/weather.*` (the `Sky` class: thunder and lightning flash, which today is a colour change), `apps/openmw/mwworld/weather.cpp` (thunder frequency, flash timing, the lightning colour from the weather record), `files/shaders/sky/*` and `files/shaders/lib/fog.glsl` (or the fog function in the default shader; confirm the file), `files/shaders/lib/light/*`, `components/render/atmospheremath.hpp`.
Inputs / outputs: new `[Rendering]` settings `precipitation lighting` (bool, false), `lightning` (bool, false), `lightning strength` (0 to 2, 1), `sun glow` (bool, false), `sun glow strength` (0 to 2, 1); pure-maths headers `components/render/lightningmath.hpp` and additions to `atmospheremath.hpp`; no change to weather records, scripts, sounds or saves.

Status: draft, written 2026-11-01 at toddhow's request. Nothing is built. Full loop (Track V renderer) when it starts.

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

- [ ] The pure maths and shader twins agree; the flash never exceeds the cap and never repeats faster than the minimum interval (tests).
- [ ] Rain and snow streaks are dark in the dark, bright near a torch or lamp, warm at dusk (frames viewed).
- [ ] A foggy sunrise and sunset glow around the sun and not elsewhere (frames).
- [ ] A lightning flash lights outdoor surfaces for a few frames and decays with a double flicker (a frame sequence viewed), with the same seed giving the same flashes.
- [ ] All three settings off, or on in clear weather, are frame-identical within noise.
- [ ] Cost note recorded; unit tests, the 32 Lua tests, the Linux gate and the format check pass; a fresh reviewer read each stage.
Out of scope: wetness (0037), cloud shading (0038), snow and ash cover (0039), volumetric fog and light shafts (0041), lightning through windows, new precipitation types, thunder sound changes, a weather simulation, the editor.
