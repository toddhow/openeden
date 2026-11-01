# Task: Rendering R5/R6, ray-marched volumetric clouds
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R5: "Raymarched volumetric clouds are a later, optional addition"; R6: continuous cloud coverage, clouds cast moving shadows); builds on `docs/tasks/0038-rendering-cloud-shading-and-shadows.md` (the cheap lit-cloud version this can replace), `0041-rendering-volumetric-fog-and-light-shafts.md` (the sun shadow lookup, temporal reprojection and the froxel grid this can share), `0036-rendering-gi-exterior-sky-visibility.md` (the sky image and sky SH), `0034-rendering-weather-atmosphere.md` (sun direction and colour, weather-driven values), `0026-rendering-f3-stored-gbuffer.md` (depth and velocity).
Goal: draw the clouds as a real volume, a layer of cloud with thickness that is lit by the sun through its own body (bright billowing tops and edges, dark bases, soft light shafts at breaks, a thin bright lining against the sun), whose coverage and darkness follow Morrowind's weather, and whose real density casts the ground shadows of task 0038.
Inherited code touched: not yet confirmed; to be named at the commit that starts each stage. Expected: `apps/openmw/mwrender/weather.cpp` and `weather.hpp` (the `Sky` class: cloud layers, `setWeather`, texture, speed, opacity), `apps/openmw/mwworld/weather.cpp` (cloud opacity, speed and texture from the weather record; transitions between weathers), `components/render/hdrtarget.*` and `apps/openmw/mwrender/rendermanager.*` (a compute or full-screen pass writing an image the sky and tonemap use, as the AO pass does), `components/vsgutil/computebin.*` and `components/pipeline/*` (compute pipelines), `files/shaders/sky/*` (the existing cloud shader, kept as the fallback), `files/shaders/CMakeLists.txt` (every new shader file must be listed), `components/render/atmospheremath.hpp` (sun and sky colour), the sun shadow lookup added by task 0041.
Inputs / outputs: new `[Rendering]` settings `volumetric clouds` (bool, false), `volumetric clouds quality` (`low`, `medium`, `high`; default `medium`), `volumetric clouds coverage scale` (scale on the weather's coverage), `volumetric clouds density` (scale), `volumetric clouds shadows` (bool, true when on); new compute shaders under `files/shaders/comp/cloud/`; a new pure-maths header `components/render/cloudvolumemath.hpp`; generated 3D noise textures (made at start by a compute pass, from a fixed seed, no art files); no change to weather records, saves, Lua or contracts.

Status: draft, written 2026-11-01 at toddhow's request (he asked whether volumetric clouds were on the list; they were not, 0038 explicitly leaves them out). Nothing is built. The heaviest GPU effect on the list, so quality tiers and measurement are part of the spec. Full loop (Track V renderer) when it starts; it should follow 0041, which supplies the sun shadow lookup and temporal machinery.

## Design, in plain language

Task 0038 shades Morrowind's two flat cloud sheets as if they had thickness. Real volumetric clouds are a layer of space, for example 1500 to 4000 metres up, filled with a 3D "density" (how much cloud there is at each point). To draw one pixel we walk a ray from the camera through that layer in small steps, and at each step: sample the density; if there is cloud, work out how much sunlight reaches that point (by walking a short second ray towards the sun and adding up the density it crosses, Beer's law: light fades exponentially with the amount of cloud it goes through); add that light, scattered towards the camera; and dim whatever is behind it. The result is the familiar look: bright where the sun shines in from the side or through thin parts, dark and grey where the cloud is thick, and a bright rim when the sun is behind it.

The density is built from noise. A low-frequency 3D noise (Perlin-Worley, a mix of smooth and cell-like noise) shapes the big cumulus forms, a higher-frequency noise erodes the edges into wisps, and a **height gradient** makes bases flat and tops rounded. A 2D **coverage** value (0 clear to 1 overcast) from the weather decides where there is cloud at all, and wind scrolls everything. The noise is generated once at start by a compute shader from a fixed seed, so there are no art assets to license.

It is expensive, so the standard tricks are all part of the design: march at **a quarter of the screen's resolution** (one ray per 4x4 block, a different pixel of the block each frame), **reproject the previous frame** with the stored camera and velocity data and blend, so the picture is full resolution and stable after a few frames; take **few steps** with larger steps where the ray is empty; end the ray when the remaining light is almost gone; and use **cheaper lighting samples** (about 5 towards the sun with growing step length).

Weather keeps its say: the cloud opacity, speed and colour fields of each weather record, and the weather manager's transitions, map to coverage, density and wind speed, so overcast weather is a thick grey deck, cloudy is broken cumulus, clear is sparse wisps, fog and ash and blight keep their own sky (the volume steps aside), and a mod's weather works through the same fields. The vanilla two-layer clouds stay as the fallback for everything that cannot or should not use the volume (interiors do not draw sky anyway).

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | Technique | A ray-marched density layer between two heights, low-resolution with temporal reprojection (Schneider, Guerrilla Games, 2015, the approach used in many open-world games), computed in a compute pass | The cheap lit sheets of 0038 only (already on the list); Hillaire's 2020 sky-and-atmosphere LUT for the clear-sky part (later, see D10) |
| D2 | Layer | One cumulus layer at 1500 to 4000 m above sea level in metres converted to game units at one named boundary; a second thin high layer (cirrus) as a flat textured sheet, the existing cloud texture | Two volumetric layers (twice the cost) |
| D3 | Noise | Generated at start on the GPU from a fixed seed: a 128-cubed Perlin-Worley shape texture and a 32-cubed detail texture, plus a 2D curl noise for turbulence; no image files | Shipped noise textures (licence record, art assets) |
| D4 | Coverage and wind | From the weather record's cloud opacity and speed through a documented mapping table (stage a), scaled by the two settings; transitions blend coverage smoothly so clouds grow and thin instead of popping | A new per-weather cloud table (mods would not know it) |
| D5 | Lighting | Sun (and moon at night) through the density with Beer's law plus a powder term, a two-lobe Henyey-Greenstein phase (forward and back scattering), a cheap multiple-scattering approximation (octaves with reduced extinction), ambient from the sky SH of task 0036 | Single scattering only (flat, too dark) |
| D6 | Ground shadows | The same density gives the shadow: sample the cloud transmittance along the sun direction for the ground point (a 2D shadow map of the layer rebuilt when the sun or the clouds have moved enough), replacing the texture-based shadow of 0038 when on; `volumetric clouds shadows` can turn it off | Keep 0038's |
| D7 | Resolution and tiers | `low`: 1/8 resolution, 24 steps; `medium`: 1/4, 40 steps; `high`: 1/2, 64 steps | One quality |
| D8 | Budget | Acceptable if the pass costs under 1.0 ms (`low`), 1.8 ms (`medium`), 3.0 ms (`high`) on the benchmark machine; measured at stage e and recorded, not promised | |
| D9 | Interaction with fog | The volume draws into an image the tonemap composites over the sky, with the froxel fog of task 0041 applied to it by distance (clouds in the distance fade into the haze); without 0041 the weather's fog colour is used | Ignore fog |
| D10 | Sky | The atmosphere model's sky colour stays as the background; a precomputed sky-view table (Hillaire 2020) is a later change | Replace the sky now |
| D11 | Requirements | `hdr`, `atmosphere`, `gbuffer` (velocity for reprojection); `volumetrics` (0041) recommended | Fewer |
| D12 | Night | Moon-lit clouds with a dim blue light and a darker ambient; stars show through thin clouds (the sky texture composited under) | Dark flat night clouds |
| D13 | Weathers that keep the vanilla sky | Fog, ash and blight (and snow if its look needs the old sheet); decided per weather by the same glare value task 0034 uses | All weathers volumetric |

## Stages

Each stage commits working, with a fresh reviewer and frames viewed.

- **a. Maths and mapping.** `cloudvolumemath.hpp`: the height gradient, the density function over the noise values, Beer-Powder, the two-lobe phase function, the transmittance sum along a ray, the weather-to-coverage mapping table (with the transition blend), the metre-to-game-unit boundary, the low-discrepancy sample jitter; each with a shader twin and a test against a closed form (uniform slab). Tests are pure CPU maths: no Vulkan device, window or game data (the Windows runner has no GPU).
- **b. Noise generation.** A compute pass that builds the shape, detail and curl noise from the fixed seed at start; a debug view of a slice; a determinism check (a CPU reference for a few texels in a test; the image hash stable between runs on this machine).
- **c. The march at low resolution.** The cloud pass with sun lighting only, no temporal step, composited over the sky behind `volumetric clouds`; frames at noon, dusk and under a cloudy weather.
- **d. Temporal reprojection and quality tiers.** The quarter-resolution jitter and reprojection with the previous frame and the stored velocity, the history rejection when the camera cuts or the weather changes, and the three tiers; a frame sequence on a turning camera with no ghosting.
- **e. Shadows and fog.** The 2D layer shadow map and the ground shadow, the composite with 0041's fog, the night moon lighting; frames at a low sun showing the cloud shadows move with the clouds overhead.
- **f. Cost, debug views, notes.** GPU timing and `tools/benchmark.sh` for the three tiers against the D8 budgets with the GPU named; a debug view of density, transmittance and the shadow map; module guide entries.

## Constraints

- Off by default and frame-identical when off (diffmap at a fixed time and weather; the vanilla cloud layers keep drawing). The compute pipelines, noise textures and history images are created only when the setting is on.
- No allocation per frame after start; the history images are resized only when the window or tier changes.
- Determinism: the noise seed is fixed, the jitter is a fixed low-discrepancy sequence indexed by the frame counter, wind and coverage use game time, never the wall clock.
- A new shader file must be listed in `files/shaders/CMakeLists.txt`; every new view descriptor set needs the dummies of the earlier tasks (`dummyProbes()`, `dummyReflections()`, `dummySkyVis()`, `dummySkyVisSky()` plus any this task adds); keep conditions on specialization constants to one comparison; check a frame after every shader change (`apps/openmw/mwrender/CLAUDE.md`).
- The unit tests must pass on a runner without a GPU.
- Weather records, `ChangeWeather`, sounds, Lua and saves untouched; a weather mod keeps working through the same fields.
- The editor's `SceneHost` keeps building and drawing; it does not take volumetric clouds.

## Acceptance criteria

- [ ] The density, transmittance, phase and mapping maths match their closed forms and shader twins (CPU unit tests, no GPU).
- [ ] The noise build is deterministic (reference texels in a test; the same image hash on repeated runs here).
- [ ] Clouds read as thick volumes lit by the sun in frames at dawn, noon and dusk in cloudy and overcast weather: bright tops and edges, dark bases, a rim against the sun (viewed).
- [ ] Clear weather shows sparse clouds, overcast shows a thick grey deck, and a weather change grows or thins the clouds without popping (a frame sequence).
- [ ] The temporal pass is stable on a turning camera (no trailing ghosts) and recovers within a few frames after a cut.
- [ ] Cloud shadows on the ground move with the clouds and agree with their position overhead (frames a few seconds apart).
- [ ] Fog, ash and blight keep their vanilla sky (frames); interiors are unchanged.
- [ ] The three tiers' costs are recorded against the D8 budgets with the GPU named.
- [ ] With the setting off, frames equal the previous build within noise.
- [ ] Unit tests, the 32 Lua tests, the Linux gate, the CI check scripts and the whole-tree format check pass; a fresh reviewer read each stage.
Out of scope: a second volumetric layer, a precomputed full sky-and-atmosphere table (D10), rain and wetness (0037), snow and ash cover (0039), precipitation and lightning (0040, though lightning inside a cloud would be a natural later extension), fog volumes (0041), clouds in the editor, new weather types or a weather simulation, ray tracing.
