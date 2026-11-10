# 0008 — Modern rendering features: a proposal

Date: 2026-10-31 · Asked by: toddhow · Status: **proposal, nothing decided.** *(Update 2026-11-10: toddhow closed Track V, so the wait below is lifted; the proposal is still undecided and each stage still needs an approved spec.)* Implementation waits for Track V (root `CLAUDE.md`, "Not yet": new renderer features beyond what the Vulkan move brings wait until Track V is done). Each stage below still needs a task spec that toddhow approves, and most need a decision (section 9).

The ask: subsurface scattering, volumetric atmospherics, physically based rendering, reflections, ambient occlusion, dynamic weather and time of day, plus any other recommendations. Facts below cite a file at the current commit (`vulkan`, after `ca9f6283d8`). Technique names come from memory and no licence has been checked (section 7).

## 1. Summary

Every one of these needs the same four things first, and none of the four exists yet. They are the foundations (section 4), and they are where most of the work and risk is.

| Stage | What | Needs | Gain on stock Morrowind assets | Size (a guess, not measured) |
| --- | --- | --- | --- | --- |
| F0 | GPU timing and a heavy benchmark | nothing | none, but it decides everything after | small |
| F1 | Shader toolchain (Slang), and a decision on the Vulkan floor | decision 0004 draft | none | medium |
| F2 | Linear HDR render path, chosen as a whole | F1 | none alone | large |
| F3 | Stored depth and normals (and velocity) | F2 | none alone | medium |
| F4 | Material model | F1 | none alone | medium |
| R1 | Physically based lighting | F2, F4 | modest alone, large with R2 and R3 | large |
| R2 | Ambient occlusion | F3 | large in interiors | medium |
| R3 | Reflections and probe lighting | F2, F3 | moderate (water already reflects) | large |
| R4 | Atmosphere and time of day | F2 | large outdoors | large |
| R5 | Volumetric fog and light shafts | F2, F3, shadows | large | large |
| R6 | Dynamic weather | R4, F4 | large | medium to large |
| R7 | Subsurface scattering | F2, F3, R1 | small on stock skins | medium |

**Order.** The request names subsurface scattering first (and toddhow's answer in task 0012 names it as a reason for moving to Slang). I recommend it last, because it needs the most other stages and gains the least on Morrowind's low-detail skin textures; the ranking is his call. Shadows for point lights (section 6) come first of all, because they need no new pipeline.

## 2. What the renderer has today

Verified by reading; "not verified" marks what I did not trace.

- **Forward renderer, 8-bit.** The main pass renders to `VK_FORMAT_B8G8R8A8_UNORM` with a `D32_SFLOAT` depth (`components/render/attachmentformat.hpp`; `createMainRenderPass`, `apps/openmw/mwrender/renderpass.hpp`). The formats are fixed by render pass compatibility with the swapchain. The main scene has no HDR, no exposure and no tonemap. The ported post-processing chain also draws into targets of the swapchain's colour format and leaves OpenMW's HDR support off: `apps/openmw/mwrender/postprocess.hpp` (lines about 55-58) lists HDR among the parts not ported, `components/fx/vulkanpass.cpp` sets `OMW_HDR 0` so `omw_GetEyeAdaptation()` returns 1.0, the technique flag `hdr` is parsed in `components/fx/technique.cpp` and nothing acts on it, and the setting `auto exposure speed` is documented as having no effect without HDR.
- **Lighting is computed on display-encoded values.** The shaders contain no sRGB or gamma conversion, and image reads strip the sRGB flag (`vsgUtil::removeSrgb`, root `CLAUDE.md` pitfalls). So lighting adds up gamma-encoded numbers, as the OSG build did. Moving to linear lighting changes how every surface looks, whether or not PBR is used.
- **Lighting terms.** Ambient plus diffuse plus emission (`lib/material/terms.glsl`). Point-light specular raises a dot product of a reflected vector to a power (`lib/light/pointspecular.glsl`), which is Phong-style. Per-pixel lighting with normal and specular maps is in `default/main.frag` (lines about 15-19 and 131-172). Light falloff uses the game's `LightAttenuation_*` fallback values (`lib/light/equations.glsl`).
- **Many lights are already cheap.** A compute shader assigns point lights to a grid of tiles (`comp/light/assign.comp`, `lib/light/grid.glsl`, `components/pipeline/lightgrid.*`).
- **Shadows: the sun only.** Cascaded shadow maps (`lib/view/cascades.glsl`, `apps/openmw/mwrender/shadow.cpp`), **off by default** (`enable shadows = false`, `files/settings-default.cfg`). I found no point-light or spot-light shadows.
- **Depth is not kept.** The depth attachment is not stored past the main pass, so depth is copied inside it, and only when water reflection, soft particles or post-processing is on (`apps/openmw/mwrender/postprocess.cpp` line about 432; `rendermanager.cpp` lines about 447-451). There is no normal buffer and no velocity buffer.
- **Water and reflections.** `Reflect` renders reflection colour and depth textures that the shaders bind (`rendermanager.cpp` lines about 154-155; `reflect.hpp`; `files/shaders/water`). A screen-space ray march helper exists (`lib/view/raytrace.glsl`) and is included only by `lib/view/reflect.glsl`. What it traces, and whether it is in use, is not verified.
- **Sky and weather are colour-driven.** `Sky` has atmosphere, night, cloud, sun and moon passes (`mwrender/weather.hpp`). They are driven by per-weather colours and fog numbers (`WeatherResult` in `weatherdata.hpp`: sky, fog, ambient and sun colours, fog depth, cloud texture and blend, wind). Nothing is physically simulated. The sun's glare uses an occlusion query (`weather.cpp` line about 220). Rain, ripples and weather particles are in `rain.*`, `waterripples.*`, `weathereffects.*`, `precipitationoccluder.*`. Weather scripting commands `ChangeWeather` and `ModRegion` exist (`apps/openmw/mwscript/skyextensions.cpp`).
- **Post-processing.** The `.omwfx` technique chain is ported (`postprocess.cpp`; `Settings::postProcessing().mChain`). Mods write shaders for it and expect today's picture. Techniques that set `hdr` expect a floating-point scene and do their own tonemapping; this port does not offer that yet.
- **Compute is in use.** Light assignment, particle simulation, fog of war and the world overlay run as compute shaders (`files/shaders/comp/*`).
- **Shaders and API level.** GLSL compiled by glslang to SPIR-V 1.0 with no optimiser, on a Vulkan 1.0 instance with only `create_renderpass2` and `depth_stencil_resolve` requested (`apps/openmw/vsgengine.cpp` lines about 207-212; research 0005). Slang is not yet adopted (research 0005; a draft decision 0004 exists on another branch).
- **Performance.** Scenes run at 450 to 1,500 frames a second, so fixed costs dominate and these scenes say little about heavy views (research 0004, section 4). All five exteriors are already over the 10% budget against the OSG build, by 18% to 66% (task 0012, performance re-measure at `a7c43f8346`: Balmora +66%, Ald-ruhn +50%). These are median wall-clock frame times, and every scene is still under 4 ms at p95. There is no GPU timestamp profiling; the only query pool is the sun glare's occlusion query.
- **Data the engine already has.** Skin body parts are flagged (`ESM::BodyPart::MT_Skin`, `mwrender/bodyparts.cpp` lines about 30 and 109). Interior cells carry ambient, sunlight and fog colours and a fog density (`components/esm3/loadcell.hpp` lines about 105-106).

## 3. Rules every stage keeps

These come from the guide, not from this proposal.

- **Off means today's path.** With every new setting at its default, the picture must match the fork point, compared by frames. A linear HDR path with an identity tonemap is not today's picture (8-bit quantisation and gamma differ), so the new pipeline must be chosen as a whole: when it is off, the current render pass and shaders run unchanged. This means two render paths for a while, with a maintenance cost. Shared shader code behind defines, as `Pipeline::Mode` does today, keeps that cost down.
- **Original behaviour stays.** A change to vanilla look or mechanics is a new setting that is off by default, not a fix.
- **Mods keep working.** `.omwfx` shaders run after the tonemap and receive what they receive today; techniques that ask for HDR are the one special case (F2). Weather scripts and region changes keep their effect.
- **Full loop.** Renderer work follows the Track V loop: tests from the spec, implementation, a fresh reviewer, and a frame captured and viewed before any commit that can change rendering.
- **Shared code goes in `components/`.** New passes, pipelines and shader libraries live in `components/render`, `components/pipeline`, `components/view` and `files/shaders/lib`, not in `apps/openmw/mwrender`, so Eden Studio's own scene host can use them (it cannot use `mwrender`; see `apps/opencs/CLAUDE.md`).
- **Authored data** such as material overrides is JSON with a canonical writer.

## 4. Foundations

**F0 Measure first.** Add GPU timestamp queries per pass, read a few frames later, and a heavier benchmark set: a long view over Balmora or Vivec with many NPCs, and a scripted walk (the gap research 0004 names). Without this, every cost claim below is a guess, and the existing exterior CPU overrun cannot be separated from new GPU cost. Zero risk to the game, and it can start at any time.

**F1 Shader toolchain and floor.** New shaders in Slang compiled at build time to SPIR-V, as research 0005 recommends; existing GLSL stays until a stage touches it. That needs a decision record (draft 0004). Slang does not force a higher Vulkan version: research 0005 compiled a Slang port of the sun shader to SPIR-V 1.0 with the same layouts as the GLSL (Slang's default target, SPIR-V 1.5, would need Vulkan 1.2, but `-profile spirv_1_0` is honoured with a warning). A higher **minimum Vulkan version** would come from the features and not the compiler: subgroup operations for the compute passes (Vulkan 1.1), for example. Raising the instance from Vulkan 1.0 sets the oldest GPU that runs OpenEden, so it is toddhow's decision, and F0's numbers should inform it.

**F2 Linear HDR path.** Render the scene in a 16-bit float target, light in linear space (textures read as sRGB again on this path only), then apply exposure, a tonemap and bloom, then write to the swapchain through the existing 8-bit pass. It touches every shader's output: lighting, fog, particles, sky, shadows, transparency. The tonemap goes before the `.omwfx` chain, with one exception: a mod technique that sets `hdr` expects a float scene and tonemaps for itself (section 2), so when the chain contains one, the built-in tonemap should step aside and not be applied twice. The exact rule needs deciding when F2 is specified. This is the largest foundation and the one most likely to change the game's look; a "vanilla look" curve that approximates today's picture is part of it, but it cannot be pixel identical, which is why the path is chosen as a whole (section 3).

**F3 Stored depth, normals and velocity.** Ambient occlusion, reflections, subsurface scattering, volumetrics and temporal anti-aliasing all read depth and normals after the main pass. Two ways:
- **Extra outputs from the main pass** (normals written alongside colour; depth stored). One geometry pass, but it complicates multisampling.
- **A prepass** that draws opaque geometry first. Simple, but a second pass of draw calls, on a renderer whose exteriors are already over its CPU budget.

Recommendation: extra outputs first, a prepass only if F0 shows overdraw needs it. For temporal effects, actors need the previous frame's bone matrices kept (they skin through `Anim::Skin`, `components/animation/skin.hpp`), which is not built.

**F4 Material model.** Morrowind assets have a diffuse texture, a colour-and-shininess material and, with some mods, `_n` normal and specular maps. They have no roughness, metalness or ambient occlusion. Three tiers:
1. **Derive from what exists:** shininess to roughness, specular colour to reflectance, no metals, so everything renders plausibly with no new data.
2. **Data-driven overrides:** JSON material files in the VFS, keyed by texture or model path, so mods can ship them.
3. **Optional extra texture files** for roughness, metalness and occlusion. The names should follow what existing mods already use; I have not surveyed that, and I will not invent a convention.

Skin needs no new data for NPC bodies and heads: the body part type marks it (section 2). Creatures and props need overrides.

## 5. Features

**R1 Physically based lighting.** Replace the Phong-style specular with a microfacet one, an energy-conserving diffuse and Fresnel, keeping Morrowind's light falloff and the light grid. The risk is calibration: Morrowind's light radii and intensities were never physical, and stock art was authored under the old look, so a "look preset" that stays close to vanilla matters more than strict correctness. Alone it gains little, because the textures carry no material information.

**R2 Ambient occlusion.** A ground-truth ambient occlusion class technique (GTAO, Jimenez and others, 2016) at half resolution from depth and normals, filtered across space and, with temporal anti-aliasing, time. It darkens ambient light only. Interiors gain most, since Morrowind interiors have one flat ambient colour per cell. Contact shadows from a short screen-space ray are a cheap companion.

**R3 Reflections and probe lighting.**
- Keep the planar water reflection.
- Add box-projected cubemap probes captured when a cell loads: a handful in interiors, and a sky capture outdoors from R4. They give specular reflections and, as low-order spherical harmonics, a cheap diffuse ambient.
- Screen-space reflections with probe fallback, for wet and shiny surfaces.
- Hardware ray queries (`VK_KHR_ray_query`) as a later, device-gated tier; the guide lists ray tracing as a later feature.

**R4 Atmosphere and time of day.** A physically based sky and aerial perspective in the style of Hillaire (2020), with precomputed transmittance and multiple-scattering tables and a per-frame sky view, replacing the colour-driven passes when the new path is on. Inputs are the sun direction from the game hour, as now, and per-region atmosphere settings. Morrowind's weather colours stay as the artist's targets, blended in, so Vvardenfell's look can be anchored and not lost. Outputs are the sky, aerial perspective on distant land (replacing the fog colour and depth), sky irradiance for R1 and R3, and the sun colour. It changes rendering only; time-of-day rules for the game stay as they are.

**R5 Volumetrics.** A camera-aligned grid of volume cells (a "froxel" grid, Wronski 2014) filled by a compute shader with fog density, the sun through the shadow cascades, and point lights from the light grid, then read by the ordinary forward shaders so transparent things work too. It gives sun shafts, local fog and lit fog around lanterns. Interior cells already carry a fog colour and density, which makes a natural data source. Stable results want temporal reprojection (F3's velocity, and anti-aliasing). Raymarched volumetric clouds are a later, optional addition. Heaviest of the GPU costs; needs quality tiers.

**R6 Dynamic weather.** Two parts, kept apart on purpose:
- **Visual weather state:** continuous cloud coverage, precipitation, wind, wetness and snow or ash cover, driven by the existing weather manager's type and transition, so the rules, sounds, ash storms, blight and `ChangeWeather` scripts stay exactly as they are. Surfaces respond: wet means darker and glossier, puddles on flat ground, snow and ash gather on upward-facing surfaces, sheltered spots stay dry (`precipitationoccluder.*`), clouds cast moving shadows, lightning flashes light the scene.
- **Weather simulation:** new weather states, regional climates and procedural fronts. This changes what the player experiences, so it is a separate setting, off by default, with the vanilla scheduler kept. Whether it belongs in scope is toddhow's decision.

**R7 Subsurface scattering.** Three tiers, cheapest first:
1. **Wrap or pre-integrated lighting** for skin: no extra pass, usable with R1.
2. **Separable screen-space scattering** (Jimenez and others, 2015) on skin marked by body part type, applied to diffuse light only. This needs diffuse and specular light kept apart (two outputs from the main pass), a mask (stencil or an attachment), F2 and F3.
3. **Transmission** for thin things (leaves, ears, paper lanterns, candles) from a thickness estimate.

Stock skin textures are small and heads are low-poly, so the visible gain is modest unless mods bring better ones. Tier 1 could come with R1; the rest should wait.

## 6. More recommendations

1. **Point-light shadows first.** Shadows are off by default and cover only the sun. Torches, lanterns and candles light most Morrowind interiors and cast nothing. Shadow maps for the few nearest point and spot lights, contact shadows and better filtering lift every interior, need no new pipeline (they can go into today's path), and R2, R5 and R7 look wrong without them.
2. **GPU-driven culling and indirect draws, with threaded command recording.** Exteriors are already 18% to 66% over the frame-time budget against the OSG build (task 0012), and a depth prepass or more shadow passes multiply draw submissions. This pays for the stages above.
3. **Temporal anti-aliasing with motion vectors**, and optionally an upscaler. Noisy effects (ambient occlusion, volumetrics, soft reflections) need the temporal smoothing. MSAA stays as an option; the two do not combine cheaply.
4. **Probe-based diffuse lighting for interiors** (spherical-harmonic light probes, built when a cell loads). With R2 it is the largest single upgrade to Morrowind interiors. Screen-space or hardware-ray-traced global illumination can come later.
5. **Water:** waves (Gerstner or FFT), refraction with depth fade, caustics, foam, shoreline blending, underwater fog. Reuses R3 and R5. Rain ripples stay on, as toddhow said.
6. **HDR display output** (HDR10 or scRGB swapchain), automatic exposure, bloom. Whether VSG exposes swapchain colour spaces is not verified.
7. **Terrain:** height- and slope-based layer blending, displacement, material layers. Modest gain, moderate work.
8. **Development workflow:** install Vulkan validation layers (none are installed; root `CLAUDE.md`, pitfalls), per-pass debug markers (`VK_EXT_debug_utils` is already requested with the debug layer, `vsgengine.cpp` lines about 216-217), a frame comparison gate per stage using the existing `--auto-screenshot` rig, and quality presets (low to ultra) in the launcher with video memory budgets.
9. **Mod guidance:** a note on which built-in features replace which common shader mods, and where in the chain `.omwfx` shaders run.

## 7. Risks and unknowns

- **Look.** Morrowind's art was made for the old look. "Different" is easy and "better" is not, so every feature needs an A/B preset and toddhow's eye.
- **Two render paths** for as long as the old one is kept, and the editor's scene host must stay in step with whatever moves to `components/`.
- **MSAA with several outputs** is costly, and ambient occlusion, volumetrics and subsurface scattering all want extra outputs.
- **Slang's GLSL front end is not drop-in** (research 0005). Using it only for new shaders avoids that.
- **VSG's render graph** is hand-built; barriers between passes, compute-to-graphics hand-offs and extra attachments each need care. Not verified for these cases.
- **Hardware floor and memory.** Probes, volumes and HDR targets cost video memory. The floor is a decision, not a finding.
- **Licences, unchecked.** I name these for orientation: GTAO (Jimenez and others 2016), Hillaire's sky and atmosphere (2020), Bruneton's precomputed atmospheric scattering (2017), froxel volumetric fog (Wronski 2014), separable subsurface scattering (Jimenez and others 2015), temporal anti-aliasing (Karis 2014), and AMD's FidelityFX upscalers. The guide requires a licence check before adding any dependency, and no code or constants are to be taken from a reference before that. Reimplementing a published technique from its paper is a different matter from using a vendor's code, and each use should be recorded in `docs/dependencies.md`.
- **macOS and MoltenVK** (an open question in the guide) may limit compute and subgroup features. Not verified.
- **VR** was removed for now (task 0012), so stereo is not a constraint.

## 8. Sequencing against other work

- The guide held new renderer features until Track V was done. V2, V4 and V5 still have open items, and V6's hands-on checks are unrun; toddhow closed Track V on 2026-11-10 anyway, so those are follow-ups, not blockers.
- Three things are now marked "next": the Track W merge, Eden Studio (after the extension system) and this. They compete for the same attention and, for Eden Studio, the same renderer code.
- **F0 can start any time:** it only measures, and it supplies the numbers the decisions below need.
- **Suggested order:** finish V6, merge Track W, then F0 and F1 together with Eden Studio's first phase, then point-light shadows, then F2 to F4, then R1 to R7 by section 1.

## 9. Decisions needed from toddhow

1. **Order of the three next tracks:** rendering, Eden Studio, and the Track W merge. (The only question that cannot wait; the rest wait for Track V.)
2. **Hardware floor:** whether to raise the Vulkan 1.0 instance, and the GPU class to target.
3. **Slang:** approve the draft decision 0004 for new shaders.
4. **Default look:** the new pipeline stays off by default, as the guide requires. Should it ever become the default, and by what test?
5. **Dynamic weather scope:** visual only, or also a new weather simulation behind a setting.
6. **Feature priority:** your order of importance among the seven features. Section 1 gives the dependency order and why subsurface scattering comes last in it; the list in your request may have been only examples, not a ranking.
7. **Ray tracing:** whether a hardware ray-query tier is in scope at all, and when.
8. **Anti-aliasing:** temporal anti-aliasing alongside MSAA, or MSAA only.
