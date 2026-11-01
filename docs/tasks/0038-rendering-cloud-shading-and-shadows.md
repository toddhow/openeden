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

- [x] The pure maths and its shader twins agree (35 unit tests pass; the twins were compared constant by constant by a fresh reviewer reading, not by a machine; unit tests: thickness limits, edge glow towards and away from the sun, silver lining only with the sun behind, shadow factor in [0, 1]).
- [x] Clouds show bright edges towards the sun and dark undersides, in frames at dawn, noon and dusk in cloudy and overcast weather, viewed. (Cloudy at 06:30, 12:00 and 17:30, overcast at 12:00 and 17:30; overcast at dawn was not taken.)
- [ ] Cloud shadows move across the ground with the clouds and agree with their position overhead (frames a few seconds apart).
- [ ] Fog, ash and blight weathers keep their vanilla look (frames); interiors unchanged. (Ash storm at noon equals the off frame; blight was not taken; the foggy weather is shaded mildly, see D3 as built; interiors have no sky so the cloud code never runs there, not framed.)
- [x] Off is frame-identical to the previous build within noise. (Default settings, cloudy and ash, with this build's shaders against the shaders of HEAD copied over them: 684 pixels of 230,000 differ by more than 6, the noise of two frames of the same scene.)
- [x] Cost note recorded (`docs/research/0019-cloud-cost.md`); unit tests (3010, 799, 520), the 32 Lua tests (Windows) and the format check pass; a fresh reviewer read stages a and b, and c and d. The Linux gate (`-Werror`, commit `b7e64d6a44`): builds, `components-tests`, `openmw-tests` and the 32 Lua tests pass.
Out of scope: ray-marched volumetric clouds (D7), new cloud types or per-weather cloud settings, a weather simulation, rain and wetness (0037), snow and ash (0039), lightning (0040), volumetric fog (0041), the editor.

## Shader language (toddhow's decision 0009, received during the work)

No whole new shader was written. `sky/clouds/lit.glsl` and `lib/view/cloudshadow.glsl` are include files of existing GLSL shaders (the cloud shader and every shader that includes `lib/view/shadow.glsl`), which cannot mix with Slang modules; the edits to `main.frag`, `main.vert`, `shadow.glsl` and `skyvis.glsl` stay GLSL. Settings reach them as generated includes (`cloud.settings`, and two defines in `shadow.settings`), as the shadow settings already did, because the shaders that read them are compiled at run time. A Slang twin of the cloud shader behind `slang shaders` was not written (the sun's was a port of a whole shader; the clouds' change is a small edit of one that the decision says to leave).

## Decisions as built (changes to the table above, all open to toddhow)

- **D2 changed: no separate shadow pass, no new view binding.** The shadow is a factor inside `shadow()` (`lib/view/shadow.glsl`), the one function every sun term takes its shadow from, so there is no hunk in `default/main.frag`, `terrain/main.frag` or `lib/light/directional.glsl` (the 0044 agent's files). The data (a 64 by 64 thickness grid per cloud picture, scroll, rotation, strength) rides at the end of binding 13, the sky-visibility header buffer, so no view creator (the editor's `SceneHost` included) needed a new binding. Cost: the stand-in header is 2,100 vec4 long for every view.
- **The ground is shaded by the cloud the sky shows in that direction.** The ground point is sent along the sun's ray to a layer 15000 units above the camera, looked at from the camera, and that direction goes through the same dome mapping, scroll and rotation as the sky (`domePoint`, `domeUv`; the dome's UVs were read from `meshes/sky_clouds_01.nif`: a flat projection of X and Y, 0.0024975 per unit). A real cloud layer would be about 70000 units up and one picture repeat would then be a kilometre across; 15000 makes it about 270 m (by eye). toddhow may want to change `layerHeight`.
- **D3 as built: the weather's share of the effect is `weatherShare(glare, isStorm)`:** storms (ash, blight and blizzard have winds over the storm speed) 0; the rest from 0.5 (no glare, overcast) to 1 (glare 1, clear and cloudy). The foggy weather (glare 0.25) therefore gets 0.62, so it is shaded a little: nothing in a weather record distinguishes fog from overcast except fog depth, which thunderstorms and snow share. If fog must be exactly vanilla, the rule needs a new record field, which D3 forbids. The share eases to a new weather's value over 3 s.
- **D6 as built:** at night the light is the manager's mirrored, dim light; lit clouds and shadows scale by its luminance (`strength`, 0.08 to 0.5), so the night is unshaded and has no shadows from the moon.
- **Thickness:** from the picture's alpha, made thinner where it is bright (`thickness`, pivot 0.55, gain 2): the overcast picture is opaque everywhere and its structure is in its brightness.
- **Strengths by eye:** `maxShade` 0.65, `silverGain` 1.6, `extinction` 4 (lit clouds); shadow extinction 2 and setting `cloud shadow strength` 0.75.
- **Frames cannot look up:** the console cannot set the player's pitch after a cell change (`player->setangle` and `setpos` after `coc` are ignored), so frames use `[Camera] field of view = 110` to show more sky, and a Balmora start does not force the weather. The frame check is therefore less precise about the sun's position than a camera aimed at the sky would be.

## Stage log

### 2026-11-01, stages a to e (Windows, RTX 4070, `build-jolt`)

**Done and verified.**
- a: `cloudmath.hpp` and `cloudgpu.hpp`, 35 tests written first (4 corrected after the first run: a ring radius tolerance, the mesh's exact UV scale, a colour order test that saturated, a sampling edge); all pass.
- b: the lit cloud look (`sky/clouds/lit.glsl`, `main.frag`, `main.vert`, `Sky::update` easing the share, `Pipeline::Material::setCloudShare`). Frames of cloudy weather at 06:30, 12:00 and 17:30 and overcast at 12:00 and 17:30, `clouds` on against off: dark undersides under the thick parts of the cloud and a bright edge at the dawn sun; overcast gets more structure. The first version was too weak (the cloud pictures are mostly thin) and was tuned up, then down a little.
- c: `lib/view/cloudshadow.glsl`, `shadow()`, `Sky::getCloudShadow`, `RenderManager::updateCloudShadow`. Debug view (`cloud shadow debug`, stage d) and a ratio picture of it against off, at two times 20 s apart: the ground's ratio changes between the two frames and varies over the hill, the water and the distance, and the sky does not change.
- d: the cost note (within noise); `cloud shadow debug`; the upload throttle (found by the cost run: 0.55 ms of wall time when the header was uploaded every frame).
- e: notes in `components/render/CLAUDE.md`.
- Review: a fresh reviewer for a and b found a NaN risk with a zero sun direction (fixed), a per-frame material upload (fixed with `update`) and a fade-in at start (fixed); one for c and d found night shadows (fixed: the strength follows the light's luminance) and the scroll's precision (fixed: its fraction is uploaded).
- Unit tests 3010, 799 and 520 (Windows), the 32 Lua tests (Windows), `CI/check_file_names.sh`, `check_extension_sdk.sh`, `check_no_osg.sh`, and the whole-tree clang-format 14 check pass. Commits `e3f3ce01df`, `2d60c7be36` and `b7e64d6a44`.

**Not verified.**
- That the shadow of a cloud lies where that cloud is overhead, by a frame: the unit tests prove the mapping at the camera (`shadowAndSkyAgreeAtTheCamera`) and against two mesh vertices, and the shadow's change in time was seen, but no frame shows a shadow beside the cloud that casts it (no way to aim the camera at the sky).
- The dome mesh's own transform in the game (a rotation of the mesh by its parents would shift the mapping); the storm-direction rotation is applied as the sky applies it.
- Overcast at dawn, blight, fog exactly vanilla (see the decision), interiors by frame, the editor (it never takes the setting, and its stand-in header grew).
- A frame with `cloud shadows` on and `gi exterior` on together (the two share the header; the declaration order was reviewed by reading).

**Note on a white or red-skied frame seen by the 0044 agent while this work was uncommitted:** the shaders are copied into `build-jolt/resources` by the build, and the new cloud shader includes `cloud.settings`, which only an executable built after `shadersettings.cpp` changed provides. With the executable and shaders from one build, a default-settings frame of cloudy and ash weather equals one made with the shaders of HEAD copied over them (white cloudy and red ash skies are the default non-HDR look in both).
