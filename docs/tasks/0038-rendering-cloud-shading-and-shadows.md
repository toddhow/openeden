# Task: Rendering weather, stage 2: cloud shading and cloud shadows
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R6: "clouds cast moving shadows"; R5's closing line: "Raymarched volumetric clouds are a later, optional addition"); builds on `docs/tasks/0034-rendering-weather-atmosphere.md` (cloud tint, stage b) and `0025-rendering-f2-linear-hdr.md`.
Goal: make the clouds read as thick layers lit by the sun (bright edges against the sun, dark undersides, a silver lining) and let them throw soft moving shadows on the ground, driven by the weather's existing cloud texture, speed and colour.
Inherited code touched (as built): `apps/openmw/mwrender/weather.cpp` and `weather.hpp` (`Sky::update`, `Sky::setWeather`, `Sky::getOrCreateCloudTexture`), `apps/openmw/mwrender/rendermanager.cpp` and `.hpp` (the header buffer binding in the constructor, `onFrame`), `files/shaders/sky/clouds/main.frag`, `main.vert`, `inout.glsl`, `files/shaders/lib/view/shadow.glsl` (`shadow()` now wraps the old function, `shadowMaps()`), `files/shaders/lib/view/skyvis.glsl` (leaves out its header declaration with `CLOUD_SHADOW`), `components/resource/shadersettings.cpp` (two generated includes), `components/view/descriptors.cpp` (the stand-in header is as long as the real one), `components/pipeline/material.hpp` (`setCloudShare`). `apps/openmw/mwworld/weather.cpp` and `lib/light/directional.glsl` are NOT touched (see the decisions logged below).
Inputs / outputs: new `[Rendering]` settings `clouds` (bool, false), `clouds look` (`flat` or `lit`, default `lit` when on), `cloud shadows` (bool, false), `cloud shadow strength` (0 to 1); a new pure-maths header `components/render/cloudmath.hpp`; no change to weather records, saves, Lua or contracts.

Status: stages a to d built 2026-11-01 (see the stage log at the end); stage e is notes only. Full loop (Track V renderer).

## Design, in plain language

Morrowind's sky has two flat cloud layers, textured with a repeating cloud picture that scrolls, tinted by one colour per weather (task 0034 changed that tint to follow the sun). Two improvements are cheap and keep the weather records meaningful:

- **Lit clouds.** A cloud is brighter where it is thin and the sun shines through its edge, and darker where it is thick. From the same cloud texture we already have (its brightness is a stand-in for thickness) and the sun's direction, we compute a thickness-based colour: edges towards the sun glow, the side away from the sun is shaded, and a thin bright "silver lining" appears when the sun is behind a cloud. It needs no new textures and no ray marching.
- **Cloud shadows.** The same cloud texture, projected onto the ground along the sun's direction and scrolled at the cloud speed, darkens the *direct sun only* (not the ambient light) where a cloud stands between the sun and the ground, with a soft edge. It follows the real cloud movement because it samples the same texture at the same offset, so the shadow and the cloud overhead agree.

This is a faked shading, not a volumetric simulation. Real volumetric clouds (a 3D density field marched against the sun) are listed as a later option below, and the spec leaves a seam for them.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | How clouds are shaded | A thickness estimate from the cloud texture's brightness, lit by the sun direction and the atmosphere model's sun colour (Beer-Lambert-style transmittance plus a forward-scattering term for the silver lining) | Ray-marched volumetric clouds (later; needs 3D noise, a lot of GPU time and quality tiers) |
| D2 | Cloud shadows | Project the existing cloud texture along the sun direction at the ground, scrolling with the cloud layer; darken the direct sun term only, soft edge, strength from coverage and the weather's cloud opacity | Shadow-map the clouds (needs geometry we do not have) |
| D3 | Weather keeps its say | Cloud opacity, speed, texture and colour come from the same weather records and fallbacks as today; overcast weathers shade less and shadow more evenly; fog, ash and blight keep their look | New cloud parameters per weather (mods would not know them) |
| D4 | Requirements | `hdr`; shadows also need `atmosphere` for the sun colour and elevation | Fewer |
| D5 | Interiors | Unaffected | |
| D6 | Night | Moon-lit clouds get the lit shading with the moon's direction and a dim colour; no shadows from the moon | No night clouds change |
| D7 | Volumetric option | Not built here; stage e records what it would need and the data path | Build it now |

## Stages

- **a. Maths.** `cloudmath.hpp`: thickness from brightness and opacity, the lit colour (edge glow, thick shading, silver lining), the shadow factor from a cloud sample, with C++ twins of the shader code (GLSL: they are parts of existing GLSL shaders, see "Shader language"); tests.
- **b. Lit clouds.** The sky cloud shader gains the lit mode behind `[Rendering] clouds`, using the sun direction and colour from the weather manager; frames at four hours and several weathers.
- **c. Cloud shadows.** A shadow term in the sun's lighting (statics, terrain, groundcover, actors) from the projected cloud texture, scrolling with the layer; check that the shadow position matches the clouds overhead and moves with them.
- **d. Cost, debug view.** GPU timing into a note, a debug view of the cloud shadow mask.
- **e. Notes.** What a volumetric cloud pass would need (a froxel or 3D noise path, the sun shadow cascades, F3's velocity for temporal smoothing) recorded in the module guide; no code.

## Constraints

- Off by default and frame-identical when off (diffmap at fixed time and weather; mind the shader pitfalls in `apps/openmw/mwrender/CLAUDE.md`: a new shader file must be listed in `files/shaders/CMakeLists.txt`; keep conditions on specialization constants to one comparison; check a frame after every shader change).
- No allocation per frame; the cloud scroll offset comes from game time, not the wall clock.
- Weather records, `ChangeWeather`, sounds and Lua untouched.
- The editor keeps building and drawing; it does not take the new clouds.

## Acceptance criteria

- [ ] The pure maths and its shader twins agree (unit tests: thickness limits, edge glow towards and away from the sun, silver lining only with the sun behind, shadow factor in [0, 1]).
- [ ] Clouds show bright edges towards the sun and dark undersides, in frames at dawn, noon and dusk in cloudy and overcast weather, viewed.
- [ ] Cloud shadows move across the ground with the clouds and agree with their position overhead (frames a few seconds apart).
- [ ] Fog, ash and blight weathers keep their vanilla look (frames); interiors unchanged.
- [ ] Off is frame-identical to the previous build within noise.
- [ ] Cost note recorded; unit tests, the 32 Lua tests, the Linux gate and the format check pass; a fresh reviewer read each stage.
Out of scope: ray-marched volumetric clouds (D7), new cloud types or per-weather cloud settings, a weather simulation, rain and wetness (0037), snow and ash (0039), lightning (0040), volumetric fog (0041), the editor.
