# Task: Rendering R5, volumetric fog and light shafts
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R5 volumetrics); builds on `docs/tasks/0025-rendering-f2-linear-hdr.md`, `0026-rendering-f3-stored-gbuffer.md` (depth and velocity), `0029-rendering-ao.md`, `0034-rendering-weather-atmosphere.md` and `0040-rendering-precipitation-lightning-sun-glow.md` (the cheap sun glow this replaces where it is on).
Goal: let fog be a real volume that light passes through: sun shafts through trees and doorways, local fog banks, and lit fog around lanterns and torches, with interiors' own fog colour and density as a data source, at a quality the player can choose.
Inherited code touched: not yet confirmed; to be named at the commit that starts each stage. Expected: `components/render/hdrtarget.*` and the render graph in `apps/openmw/mwrender/rendermanager.*` (a compute pass between the scene and the tonemap, as the AO pass does), `components/vsgutil/computebin.*` and `components/pipeline/*` (compute pipelines), the sun's shadow data (`apps/openmw/mwrender/` shadow code and `files/shaders/lib/shadow*.glsl`; confirm the files), `files/shaders/lib/light/grid.glsl` (the light grid read by the volume), `files/shaders/lib/fog.glsl` or the fog function in the default shader (confirm), `apps/openmw/mwworld/weather.cpp` and `rendermanager.cpp` (`configureFog`, the interior and exterior fog values).
Inputs / outputs: new `[Rendering]` settings `volumetrics` (bool, false), `volumetrics quality` (`low`, `medium`, `high`; default `medium`), `volumetrics density` (scale), `volumetrics anisotropy` (the scattering lobe, -1 to 1), `volumetrics temporal` (bool, true); new compute shaders under `files/shaders/comp/volume/` (each listed in `files/shaders/CMakeLists.txt`); a new pure-maths header `components/render/volumemath.hpp`; no change to weather records, saves, Lua or contracts.

Status: draft, written 2026-11-01 at toddhow's request. Nothing is built. This is the heaviest of the GPU costs in note 0008, so quality tiers and measurement are part of the spec. Full loop (Track V renderer) when it starts.

## Design, in plain language

Ordinary fog in a game is a colour that mixes in with distance. A *volumetric* fog is a 3D fog density, and every point in it can be lit: where sunlight reaches the fog it glows (a shaft), where a tree or wall shadows it, it is dark, and around a lantern it forms a lit halo.

We fill a **froxel grid**: a 3D grid of small boxes (voxels) that follow the camera's view frustum (so "froxel": frustum voxel), wide near the screen and deep along the view (for example 160 x 90 x 64 at `medium`), with depth slices spaced so that near ones are thin. A compute shader runs once per froxel and works out:
1. the fog **density** there (from the weather's fog value, a height falloff, and for interiors the cell's own fog colour and density, which is the natural data source the note names),
2. how much **sun** reaches it (looked up in the sun's shadow cascades the game already renders), and
3. how much light the **point lights** around it add (read from the light grid).
Then a second compute pass walks each view ray front to back through the froxels accumulating the light that scatters towards the camera and how much the fog dims what is behind it. The result is two small 3D textures (scattered light and transmittance) that the ordinary forward shaders and the tonemap read, so transparent things, particles and water fog correctly too. A temporal step blends the previous frame's froxels (reprojected with the stored velocity of task 0026) and jitters the sample positions each frame, so the result is smooth and cheap; it is why noisy effects want task 0026's velocity.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | Technique | A froxel grid filled by compute (Wronski 2014), then an integration pass, read by forward shaders | Raymarching in the fragment shader per pixel (simpler, much more expensive at high quality); screen-space god rays only (cheap, wrong in many views) |
| D2 | Grid size by tier | `low` 80 x 45 x 32, `medium` 160 x 90 x 64, `high` 240 x 135 x 96, depth distributed exponentially, depth range limited to the fog's visible distance | One size |
| D3 | Sun light | The existing sun shadow cascades, sampled at each froxel centre; with no shadow setting on, shafts come only from the cascades the volume asks for (a cost to measure: the volume may need the cascades even if the player turned shadows off, so a decision is recorded at stage a) | Its own low-resolution shadow map |
| D4 | Point lights | The light grid (`lib/light/grid.glsl`), the same lights the surfaces use; a few brightest per froxel cluster | Only the sun |
| D5 | Density sources | Weather fog value (`configureFog`), a height falloff, interior cell fog, and later local fog volumes from a data file; no per-object fog | Artist-placed volumes only |
| D6 | Temporal | On by default at `medium` and `high`, using task 0026's velocity; off at `low` | Always off |
| D7 | Interaction with other fog | With `volumetrics` on, the volume replaces the cheap sun glow of task 0040 and the fragment shader's distance fog for the range the volume covers; beyond it the old fog continues so the horizon blends | Both stacked |
| D8 | Water and transparency | Read the volume in the same shaders; underwater fog is untouched in v1 | Full integration |
| D9 | Requirements | `hdr`, `gbuffer` (for velocity); better with `atmosphere` | Fewer |
| D10 | Budget | A tier is acceptable if the pass costs under 1.0 ms (`low`), 2.0 ms (`medium`), 3.5 ms (`high`) on the benchmark machine's GPU; measured in stage e and recorded, not promised | |

## Stages

- **a. Maths and grid.** `volumemath.hpp` (the slice depth function and its inverse, the phase function, the transmittance and integration step, the jitter sequence), the grid sizes, a test of integration against a closed form for uniform fog; the cascade question of D3 settled by a measurement.
- **b. Fill and integrate with fog only.** The compute passes with uniform and height fog, no lights; the forward shaders and the tonemap read the result; frames of an outdoor view, with `volumetrics` off the same as before.
- **c. Sun shafts.** Sample the cascades; frames through a doorway, under trees and at a low sun.
- **d. Local lights and interiors.** The light grid in the fill pass and the interior fog source; frames in a torchlit interior and around a lantern.
- **e. Temporal and quality tiers, cost.** Reprojection with velocity and jitter, the three tiers, GPU timing and `tools/benchmark.sh`, a cost note, a debug view of a froxel slice.
- **f. Notes.** A module guide entry; what a later volumetric cloud pass could share (the froxel grid, the sun shadow lookup).

## Constraints

- Off by default and frame-identical when off (diffmap at a fixed time and weather). The compute pipelines and 3D images are created only when the setting is on.
- No allocation per frame after start; the grid is resized only when the tier or the window changes.
- Determinism: the jitter sequence is fixed (a low-discrepancy sequence indexed by the frame counter), no wall-clock time.
- A new shader file must be listed in `files/shaders/CMakeLists.txt`; new view-set bindings need dummies in every view-set creator (task 0028's notes); do not enable `DebugGeometry` in the `RenderManager` constructor; check a frame after every shader change.
- The editor's `SceneHost` keeps building and drawing; it does not take volumetrics.
- Weather records, saves, Lua and contracts untouched.

## Acceptance criteria

- [x] The integration maths matches a closed form for uniform fog within a stated tolerance (unit tests), and the slice depth function round-trips.
- [ ] Outdoors in fog, distant things fade into a lit fog volume, not a flat colour (frames viewed), and the horizon blends with the old fog.
- [ ] Sun shafts appear through a doorway and between trees at a low sun and move as the sun moves (frames a few game minutes apart).
- [ ] Lit fog forms around lanterns and torches in an interior and a night street (frames).
- [ ] The temporal option removes visible grid noise and does not ghost on a turning camera (a frame sequence viewed).
- [x] The three tiers differ in smoothness and cost; the costs are recorded against the D10 budgets with the GPU named (`docs/research/0021-volumetrics-cost.md`; smoothness not compared by eye).
- [x] `volumetrics` off is frame-identical to the previous build within noise (diff map against the task 0040 build: only the flickering lamp and moving water differ).
- [ ] Unit tests, the 32 Lua tests, the Linux gate and the whole-tree format check pass; a fresh reviewer read each stage.
Out of scope: ray-marched volumetric clouds, cloud shadows (0038), wetness (0037), snow and ash (0039), lightning (0040), local fog volume authoring tools, underwater volumes, particles that write density, ray tracing, the editor.

## Decisions made while building (2026-11-01)

What reading the code showed, and what changed against the table above.

- **D1 as recommended, with one image fewer.** The froxel grid is filled by compute and integrated front to back by a second pass. The result is one RGBA16F 3D image (rgb: the light scattered towards the eye up to the far side of each slice, a: the transmittance), not two textures. The forward shaders read it in `applyFog(src, viewPos)` (`lib/view/fog.glsl`, `lib/view/volume.glsl`).
- **How the volume is mixed (new).** The stored scattered light and transmittance are turned into a fog tint (`S / (1 - T)`) and mixed in the display encoding exactly as the old fog is (`mix(src, tint, 1 - T)` on encoded colours), so a volume that holds only the weather's fog colour looks like the old exponential fog. The fog's density is the old exponential fog's (`fogExtinction`), constant in space apart from a mild fall with height above the camera (`defaultHeightScale`).
- **D2 as recommended;** the grid reaches `farDistance(fogEnd)` (at least 256, at most 8192 units). Past it the same density goes on towards the same tint, so the horizon joins the old fog without a seam.
- **D3 settled (measurement not needed):** the volume uses the sun's shadow cascades only when `[Shadows] enable shadows` is on, and never turns them on. Reading the code showed that `enable shadows` alone makes no shadows: `object shadows`, `terrain shadows`, `actor shadows` and `player shadows` all default to off and decide what is drawn into the cascades. With no cascades (`cascadeSplits` zero) the sun lights the whole fog, as an even glow round the sun and no shafts. A volume that rendered its own cascades would cost a second shadow pass of the whole scene; not built.
- **D4 as recommended, with a simpler falloff.** The compute pass reads the light grid of the main view (the same lights and clusters as the surfaces) but uses a smooth falloff of its own, to twice the lamp's radius, instead of the surfaces' Morrowind falloff, so the Slang shader needs none of the `LightAttenuation` fallback values.
- **D6 changed.** The temporal blend reprojects with the previous frame's camera matrices (`toPreviousClip`, as the velocity pass forms it), not with the velocity image: the velocity image is made after the scene, and the froxels must be final before the scene's forward shaders read them. D9 therefore no longer needs `gbuffer`: the requirements are `hdr` only.
- **D7:** with the volume on, `applyFog(src, viewPos)` returns the volume's fog and skips the old fog and the sun glow of task 0040 (the sun's term in the volume replaces it: same Henyey-Greenstein lobe, weighted by the fog colour's brightness and the weather's haze, which keeps half of its effect in clear weather). The sky dome keeps the glow, since the sky is not in the volume; water's own fog and underwater are untouched (D8).
- **Shader rule (decision 0009):** the three compute passes are one Slang file, `files/shaders/comp/volume/volume.slang`, built by `openeden_add_slang_shader()` to `main.fill.spv`, `main.integrate.spv` and `main.temporal.spv`. They have no GLSL, so the game loads the `.spv` whenever `[Rendering] volumetrics` is on, whatever `slang shaders` says (that setting swaps the sun shader, which is unrelated); a missing file (no slangc in the build) leaves the volume off with a log line. The changes inside the existing GLSL (`applyFog`) stay GLSL. The Slang shader repeats the shadow lookup of `lib/view/shadow.glsl` (one hardware comparison, no soft shadows, a fixed bias) and the light grid indexing of `lib/light/grid.glsl`; cloud shadows (task 0038) are not in it.
- **Order of work on the GPU.** Command graphs with the same `submitOrder` are submitted in whatever order their threads finish recording, so with `volumetrics` on the shadow cascades are submitted at -3, the light grid's compute graph at -2, the volume's own graph at -1 and the scene at 0; with it off every order is as before.
- **New view bindings:** 14 (the volume, a combined image sampler) and 15 (the parameter block, a uniform buffer, also read by the compute shaders); the shadow map and its sampler (6, 7) gained the compute stage. All creators of a view set bind stand-ins whose parameters say "off".


## Stage log (2026-11-01, Windows, RTX 4070)

Tests first for the maths (51 tests, `testvolumemath.cpp`) and the parameter block and shader twins (18, `testvolumegpu.cpp`), both written by a subagent from the contract alone; five more for the camera-jump rule (`testvolumecamera.cpp`). `components-tests` 3623, `openmw-tests` 830, `eden-studio-tests` 520 and the 32 Lua tests pass. Fresh reviewers read stages b and c, then d and e; their findings were fixed (half-float storage of density apart from light, the first-frame barrier, the barrier's source stages, the history thrown away when the grid's reach changes or the game paused).

- **a:** `volumemath.hpp` and its tests (round trip of the slice depth, the exact integration step against `L (1 - exp(-sigma D))` for uniform fog to 1e-5, Henyey-Greenstein normalised, jitter, tiers). The cascade question of D3 was settled by reading the code (above), not by a measurement.
- **b:** frames at foggy dusk, volumetrics off and on: the volume gives an exponential fog close to the old one, distant things fade into the fog tint. Frames of the default settings against the task 0040 build: identical but for animated water and the flickering lamp.
- **c:** verified: the lookup works (a forced bias of -1 turns the debug view dark; real terrain and actor shadows show as darker patches; coordinates in range). **Not verified: shafts between trees and through doorways.** Casting buildings and trees into the cascades needs `object shadows`, and `object shadows = true` crashes the game on this machine in `vsg::TransferTask::_transferBufferInfos` (the task 0040 build crashes the same way: not caused by this task; dump read with the stack walker).
- **d:** the lamps' code runs (no errors, no black frame) but **no frame showed a lit halo**: the interior test scenes did not frame a torch. Not verified.
- **e:** temporal on and off differ only where the water and an NPC move (1917 pixels of 230 000); no ghosting seen in still frames. **Not verified: a turning camera** (the benchmark and screenshot tools take one frame). Cost in the research note: 0.03, 0.07 and 0.22 ms at low, medium and high.
- **Housekeeping found on the way:** tools run from a shared scratchpad collide on one work folder (use one per run); a game running from `build-jolt` stops the link of `openmw.exe`.
- **Open for toddhow:** tier look by eye; the sun gain (0.2) and lamp gain (0.5) are chosen by a few frames; whether to keep `volumetrics density` default 1.

