# Task: Skyrim lighting parity
Status: DRAFT (stage a, the survey, is done; stages b to k await toddhow's approval). Numbers are estimates by the
author. Research: `docs/research/0147-skyrim-lighting-parity.md`.

Design doc section: `docs/design/neutral-world-model.md` (cell lighting, N3a-5); weather `docs/tasks/0127-skyrim-weather-source.md`.
Goal: a Skyrim session is lit as the game's own data says, in interiors first, behind off-by-default settings, with Morrowind unchanged.
Inherited code touched: `apps/openmw/mwworld/neutralcell.cpp` (`makeNeutralCellRecord`), `neutrallight.cpp` (`makeNeutralEsmLight`),
`apps/openmw/mwrender/rendermanager.cpp` (`configureAmbient`), `fogmanager.cpp` (`configure`), `scene.cpp` (`insertModel`, neutral light);
`files/shaders/lib/light/*`, `default/main.frag`, `terrain/main.frag` (+ Slang twins) at commit `7874e00d73`. New code: pack
(`game_packs/skyrimse`: `records.*`, `world_view.cpp`, `neutral_world.*`), SDK `world_model.hpp` trailing fields (additive).
Inputs / outputs: `CellLightingData` / `LightData` (pack) -> `CellLighting` / `LightComponent` (SDK world model, trailing
fields, version minor bump) -> engine.
Constraints: every stage off by default behind a setting (`[Game] neutral lighting ...`, in the test profile with a README line);
determinism (no clock, no unseeded randomness); no allocation per frame; shader changes need both GLSL and Slang twins and
regenerated `.interface` records (golden data, toddhow reviews); full loop (shaders, render path).
Out of scope: Skyrim's BSLightingShader material model (specular, rim/backlight, subsurface, environment maps: a separate
"Skyrim materials" task), volumetric lighting (WTHR HNAM VOLI), sun shadows in "show sky" interiors, light LOD, creature/actor
lighting, mods.

## Acceptance criteria (every stage)
- [ ] With the stage's setting off, the Skyrim frames of the stage's scenario equal the frames of the build before (byte for byte where the machine allows; else within run noise stated), and a Morrowind Balmora frame (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600) and `tools/worldobjects_check` are identical to the build before.
- [ ] Unit tests first, run red against stubs, for every pure rule (decoders, inherit rules, unit conversions).
- [ ] A frame check: the four cells of the survey (Bannered Mare, Dragonsreach, Bleak Falls Barrow, Whiterun exterior noon/night), setting off against on, viewed; 3 runs give identical log lines.
- [ ] Layer rules, SDK check, `coverage.sh`, test profile line.

## Stages, ranked by visible gain per effort (estimates, days of one hand with tests and frame checks)

| # | Stage | What | Gain | Effort | Setting |
| --- | --- | --- | --- | --- | --- |
| b | Why local lights do not show | **Measure first, then fix.** Log, per interior, lights created vs placed (15,175 placed light refs, 496 bases), their engine radius, colour and the attenuation the light grid gives at 0.5 radius; compare a lantern frame with the light off/on by writing a test cell colour. Then the unit conversion: Skyrim intensity falls to 0 at the radius (RECALLED-VERIFY), FNAM fade and falloff exponent shape it; map to the engine's attenuation without the user's Morrowind `LightAttenuation_*` fallbacks (a per-light attenuation, or a setting-driven radius scale). Raise the 8 bit fade clip (fade up to 10) with a float multiplier in `LightComponent` | the biggest: warm pools under lanterns, hearths, braziers; contrast in every interior | 3 to 5 | `[Game] neutral light attenuation` (off = today's mapping) |
| c | Cell image space (XCIM) | Apply the cell's IMGS through the existing weather image space driver (`NeutralImageSpaceDriver`, `Render::ImageSpace::combine`): saturation, brightness, contrast, tint per interior (893 of 929 interiors name one). Pack: `SkyrimCellSource` hands `ImageSpaceGrade` for a cell, SDK additive | cool dungeons, warm inns, right tonal feel | 1 to 2 | `[Game] neutral cell image space` |
| d | Interior fog | Feed XCLL fog near/far/power/max, far colour and the template inherit flags 3 to 4 and 8 to 9 to the fog manager's near/far path instead of density 0 (`makeNeutralCellRecord`, `FogManager::configure`); blue-green dungeon fog, distance darkening in big halls. Pack decodes the full XCLL (the struct has the room: add fields), SDK `CellLighting` trailing fields | dungeons, caves, Dragonsreach depth | 2 to 3 | `[Game] neutral interior fog` |
| e | Ambient cube | Decode XCLL cube (24 bytes) and LGTM DALC; template inherit of the ambient (flag 0 gives the cube). Engine: a per-cell 6-colour ambient uniform, used where the shaders add the flat ambient (`lib/light/*`, object and terrain fragment), sum by the normal squared; the flat colour = the cube's average when the setting is off. Both GLSL and Slang twins, golden `.interface` | right directional ambient (blue skylight from above, warm bounce from below), best interior realism | 4 to 6 | `[Game] neutral ambient cube` |
| f | Interior sun direction and fade | XCLL rotation XY/Z and directional fade (inherit bits 5 and 6) set the interior directional light direction instead of `(-0.15, 0.15, 1)`; scale by fade | shafts and gradients in cells with a tilted key light (a minority: p90 of the rotation is 0) | 1 | `[Game] neutral interior sun` |
| g | Per-light shadow and spot data | Light type from the flags (omni-shadow 216, hemisphere-shadow 110, spot 39, spot-shadow 12): point shadow slots chosen by shadow flag first (today: nearest), spot lights as cones with FOV (`mFov`, 51 lights, near clip), falloff exponent; engine side needs a spot term in the light grid (the clustered grid has point lights only) | correct shadows on the lights that should have them; spot cones | 4 to 7 | `[Game] neutral light types` |
| h | Flicker and pulse data | Per-light flicker period, intensity and movement amplitudes (LIGH DATA +28, +32, +36) instead of the engine's fixed `ESM::Light` flicker | candles and fires look right | 1 to 2 | `[Game] neutral light flicker` |
| i | Placed light extras | XRDS radius override (14,072 refs), XLIG fade/FOV/shadow bias (12,540), XEMI emittance (9,326) : the reference changes its base light. Pack: per placement light overrides in `PlacementComponents`; the engine takes a per-object `LightComponent` | per-reference tuning (many torches and windows) | 3 to 4 | `[Game] neutral light overrides` |
| j | Light fade distances and clip | XCLL light fade begin/end and clip distance (inherit bits 10 and 7): fade lights and the cell's far clip by distance; also a performance gain in big cells | big interiors | 1 to 2 | `[Game] neutral light fade` |
| k | Exterior directional ambient | WTHR DALC (4 times of day x 6 directions) into the weather description (`WeatherDescription` trailing field) and the same ambient cube path as stage e for exteriors, blended by the time of day like the other colours; WTHR fog power/max/far colour into the fog | exterior shading: sky blue from above, warm ground bounce | 3 to 5 | `[Game] neutral weather ambient cube` |

Ordering rationale: b is the largest visible gain and is a prerequisite for judging every later stage (a flat unlit room shows
nothing of fog or grading); c and d are cheap and reuse existing machinery; e is the big shader piece, done once and reused by k;
f, h and j are small pack/engine data hookups; g and i need engine features (spot light, per-object lights). Total estimate:
about 22 to 36 days; b to d alone about 6 to 10.

Suggested first approval: stages b, c, d.

## Evidence stage a
Survey and frames: `docs/research/0147-skyrim-lighting-parity.md`; throwaway scripts and six frames in
`I:\Projects\OpenEden-work\light55\keep`. Not run: the Linux gate, any unit test, a reference frame from the real game (all
"looks wrong" statements are from memory).

## Stage b log (2026-11-19): why Skyrim's local lights make no pools, and the fix
Branch `lights-0355b` from `a36e6f8fed`. Same hand wrote the tests and the code; the radius tests were written first and
run red against a stub (3 of 11 failed, the others test absence or the shader maths), the gain tests were written after the
code and checked by a mutation (the gain line removed: 2 of 5 failed, the rest test absence). No fresh reviewer. Windows only,
not pushed. Developer aids added: `OPENEDEN_NEUTRAL_LIGHT_TRACE=1` (`NLIGHTOBJ`: every neutral light the scene creates, with
radius, colour, fade, flags, position) and `OPENEDEN_LIGHT_COUNT_TRACE=1` (`NLIGHTCOUNT`: the lights the view collects and
packs, and the first six with their eye-space position).

**Measured (Bannered Mare, defaults plus `neutral actors`, `neutral weather`):** 18 placed light references become 18 engine
lights (`NLIGHTOBJ` 18, `NLIGHTCOUNT collected 18 packed 18`, none lost to the 256 capacity or to culling); their eye
positions are right and their colours flicker between 0.25 and 1 of the base as designed. So the lights ARE created, packed
and lit. With the cell ambient and directional colour set to about 0 (a throw-away environment switch, removed) the room is
black without lights (mean 9) and lit with only the hearth light (mean 43): the lights work and are shaded per vertex (no
`PER_PIXEL_LIGHTING` without a normal map, PBR or specular map; forcing it per pixel changed the frame only slightly).

**Cause 1, the falloff law.** The engine's point light (`lib/light/equations.glsl` with the Morrowind `LightAttenuation_*`
fallback keys: linear, value 3.0, no classic falloff) is at full strength up to a third of the radius, still a third at the
radius, and ends at TWICE the radius. Skyrim's falls in a line (exponent 1 in 90 percent of the records, RECALLED-VERIFY) to
nothing at the radius. Over a flat floor the engine's light spreads 2.7 times the light Skyrim's gives for the same radius (the
C++ twin, `Render::Gi::attenuate`), so every lantern lights its whole room nearly evenly and the pools do not separate from
the ambient. Frames: the far door of the Mare is 66 (mean of its pixels) with the record's radius and 31 with the mapped one,
the fire pit region 45 and 40; the whole frame 70 to 49 (Dragonsreach 61/51/33 to 44/38/25) with fix 1 alone. With both fixes (the final build): far door 66 to 35, fire pit 45 to 48, left floor 76 to 81, whole frame 70 to 65, Dragonsreach 61/51/33 to 49/42/28, Bleak Falls Barrow 26/25/16 to 25/24/15: the pools stand out and the total brightness is nearly kept.

**Fix 1: `[extension.skyrimse] light falloff` (default TRUE; the pack only runs in a Skyrim session; test profile, README).**
The pack gives the engine light 0.6 of the record's radius (`kLightFalloffRadiusScale`, `NeutralLimits::mLightRadiusScale`,
`SkyrimWorldView::setLightFalloff`). 0.6 is the value that gives the SAME total light on a floor as Skyrim's falloff (0.98 of
it; the least-squares shape fit gives 0.61): the share at 0, 0.2, 0.4, 0.6, 0.8 and 1.0 of the record's radius is 1, 1.0, 0.57,
0.34, 0.21, 0.07 against Skyrim's 1, 0.8, 0.6, 0.4, 0.2, 0. A first try with 0.5 (zero at the radius) kept 68 percent of the
light and made the rooms too dark. No shader, no `.interface` record and no Slang twin changed. The falloff exponent is not
used (10 percent of the records have another one, up to 10).

**Cause 2, the fade clip.** LIGH FNAM ("fade", 0.1 to 10) multiplies the light's colour, but the engine's light colour is
one byte a channel and `neutralLightColor` cut the colour at 255: **194 of the 496 light records of the five masters have a
colour times fade over 255 (up to 5.07 times)**; weighted by radius squared (the light a record gives) only 55 percent of the
intended light was kept. **Fix 2: `[Game] neutral light fade gain` (default TRUE; engine setting, read only in the neutral
light path, so Morrowind never sees it).** The part the byte cannot hold (`neutralLightGain`: fade over the scale
`neutralLightColor` applied) is stored in `ESM::Light::mData.mWeight` with a new flag `ESM::Light::NeutralGain` (0x10000, not
a bit of the file format) and `MWAnim::addLight` multiplies the light's colour by it. The light sum in the shaders is not
clamped before the diffuse term, so a pool core can exceed 1 as in Skyrim. `ESM::Light::load` clears the bit after reading the flags, so no file can set it (test `LightNeutralGain` in `apps/components_tests/esm3/testlightneutralgain.cpp`, written first, 2 of 2 failed red before the clear, 2 of 2 pass after).

**Finding 3, not changed: XRDS.** 10,810 of 12,148 placed light references of `Skyrim.esm` carry an XRDS float that is NOT the
base radius (for base 256: 825, 415, 143, 35, 5, -13, -80, ... p10 -0.29 and p90 2.1 times the base): if it is the reference's
radius override (the CK has one per light) most placed lights have a different size from what is used; the negative values
are unexplained. Its meaning must be settled (CK wiki / xEdit) before stage i uses it.

**Other findings.** (a) The hearth, braziers and torches show no flame: the fire models' effect shapes are left out of the
static draw (refraction/effect shapes, task 0110), so the light has no visible source; the logs of the Mare's pit are black
textures. (b) The ambient is still flat and the interior directional light is fixed from above (stages e and f). (c) `[Shaders]
force per pixel lighting` is declared in the settings but read by nothing in the Vulkan renderer; non-normal-mapped Skyrim
surfaces (most of them) are lit per vertex, so a big floor triangle with far corners loses a nearby light's pool: per pixel
lighting for neutral models is a candidate follow-up (a GLSL/Slang change).

**Frames** (`I:\Projects\OpenEden-workollowers\l55b\keep`, before left and after right): Bannered Mare (three camera spots),
Dragonsreach, Bleak Falls Barrow (almost unchanged: few lights, mean 26/25/16 to 23/22/13). **Morrowind unchanged:** Balmora
fixed frame `1c5d47ad0b5dc32d` for the n6saves build and this build, `tools/worldobjects_check` 110 objects OK, and a Morrowind
interior with torches (Seyda Neen, Arrille's Tradehouse, `morrowind_interior_ref.png` and `_new.png`) differs between the two
builds by no more than two runs of one build differ from each other (at most 19 of 921,600 pixels over 8 levels; the runs are
not byte repeatable even with fixed frame time). **Not run:** the Whiterun exterior at night (the pack setting changes only
interiors and exteriors alike but no frame was taken), Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review,
the three-run determinism of the traces, a comparison with the real game (all "looks right" is judged from memory).

## Stage e log (2026-11-19): the interior ambient cube
Branch `ambcube-0355e` from `a4238aabd6`, local commit, not pushed. Approved by toddhow. Same hand wrote tests and code; the
tests were written first in NEW files (`apps/components_tests/skyrimse/testskyrimseambientcube.cpp`, 11 tests;
`apps/components_tests/render/testambientcube.cpp`, 15) and could not build before the code existed (compile-red by
construction). Mutation evidence afterwards: removing the cube from the template inherit failed 2 of the 3 inherit tests and the
view test; renaming the shader's `ambientCubeAmbient` call failed `AmbientCubeShaders.theObjectShader...`. No existing test edited.

**Data (five masters, Python, not committed):** 907 of 929 interior XCLL carry a non-zero cube (bytes 40 to 63 of the 64 and 92 byte
forms). Slot mean luminances: +X 51/45, -X 48/41, +Y 51/45, -Y 48/42, +Z 24/24, -Z 75/74 (show-sky cells / others); -Z is the
warmest, so the order +X -X +Y -Y +Z -Z reads as "ceiling dark, floor bounce bright" (RECALLED-VERIFY from xEdit). The mean of the
six over the non-inherited cells is 0.99 of the flat ambient colour at the median (p10 0.61, p90 1.07). Of 698 cells that inherit
their ambient only 220 own cubes equal the template's DALC (39 equal its DATA cube; a DALC is present in all 698 templates), so the
cube comes from the DALC when flag 0 is set. DALC is 32 bytes in 126 templates and 24 in 5 (the first 24 bytes are the cube).

**Design.** Pack: `CellLightingData::mCube/mHasCube` (XCLL at 40, either size; the LGTM DALC joined to the template's lighting),
`applyLightingTemplate` takes the template cube with the ambient flag, `SkyrimWorldView` keeps the cube of each interior it loaded
(capped 65,536) and answers the new optional `AmbientCubeSource` (SDK `ambient_cube_source.hpp` 0.1, found by `dynamic_cast` like
`CellImageSpaceSource`; `SkyrimCellSource` forwards). Engine: `MWWorld::applyNeutralAmbientCube` (called after `configureAmbient`
for an interior, and for exteriors where it clears) hands the cube to `RenderManager::setAmbientCube`, which brightens it to the
minimum interior luminance as the flat ambient (on the average, all faces together) and writes it, in the view's colour space and
only when it changed, to the sky-visibility header buffer at `Render::AmbientCube::base` (2117 = the end of the cloud data; 8 vec4:
flag, baseline flat ambient, six colours). The scene uniform may not grow (640 bytes), hence the header buffer. The buffer is real
when `[Game] neutral world` and `neutral ambient cube` are on (also without hdr) and a zero stand-in sized for it otherwise, so a
Skyrim pipeline in the editor's or the preview's view reads "no cube". Shader: `Pipeline::Mode::AMBIENT_CUBE` (a fragment mode a
loader requests; `skyrimmodel.cpp` adds it to both shape builders), `lib/view/ambientcube.glsl/.slang` (the header buffer is declared
there only when neither `CLOUD_SHADOW` nor `GI && GI_SKY` did), `default/main.frag` and `default.frag.slang` call
`ambientCubeAmbient(world normal, flat)` before the probes and use `materialTermsAmbient`; the result is
`max(cube(n) + flat - baseline, 0)`. Morrowind pipelines never get the define, so their shader text and SPIR-V are unchanged. Stage k
reuses `Render::AmbientCube` for exteriors (the carrier and the shader function are not interior-only).

**Golden data for toddhow:** `files/shaders/default/main.frag.interface` (the `defines` line gains `AMBIENT_CUBE`, two records for the
two new sets of `default/main.frag.sets`: `AMBIENT_CUBE COLOR DIFFUSE_MAP MATERIAL NORMAL TEXCOORD UVSETS_1` and the one without
`COLOR`), regenerated by `SlangInterfaceRecords.write_the_records_when_asked`; every other record file is unchanged. The Slang twin
compiled (slangc and spirv-val) for both sets, and `SlangPermutations` and `SlangInterfaceRecords` pass.

**Frames** (`I:\Projects\OpenEden-work\combatai\l55e2\keep`, hdr, 800 x 600, setting off left, on right): Mare, Dragonsreach, Barrow.
The Mare and Dragonsreach show direction in their shading (Dragonsreach mean 53 to 64 and 57 to 72; the Mare's two shots differ
in view as well, the scenario turns by real time), the Barrow changes little (20.8 to 19.6). **Judgement against the real game is
from memory.** The log shows `NAMBIENTCUBE <cell> cube` for all three.

**Morrowind unchanged** against the `n6saves` build: `tools/worldobjects_check` 110 objects OK; Balmora fixed frame
`1c5d47ad0b5dc32d` and with hdr `b451a8a6ca771e79` equal for both; Caius' house (hdr) differs between the builds by 4 to 2,939 pixels
over 8 levels against 1,518 to 1,754 between two runs of one build. `coverage.sh` OK (217).

**Not run:** the Linux gate, clang-format 14, clang-tidy, the Lua tests, `openmw-tests` (built only), frame cost (the shader adds
up to four header-buffer reads per lit pixel with the define), the real game for comparison, a Skyrim exterior with the setting on
(the cube is cleared there by design), `[Rendering] gi` together with the cube (the cube is applied before the probes; not seen in a
frame). Per-pixel lighting (`FORCE_PER_PIXEL_LIGHTING`) was not in main when this was made.

**Decisions for toddhow.** (1) The ambient cube is ON by default for Skyrim sessions (a setting, needs a restart). (2) The template's
DALC wins over the cell's own cube when the ambient inherit flag is set (the data supports it; the real game's rule is unverified).
(3) The cube is brightened to the interior minimum luminance on its average, as the flat ambient is. (4) Dragonsreach comes out
brighter on average than with the flat ambient; no tuning applied.

## Stage k log (2026-11-19): the weather's directional ambient on exteriors
Branch `extambient-0355k` from `f29fa57e7d` (the stage e tip, not yet on main; main is still `a4238aabd6`, so the planned rebase
was empty), local commits, not pushed; Windows only. Approved by toddhow. Same hand wrote tests and code; the tests are in NEW
files (`apps/components_tests/skyrimse/testskyrimseweatherambientcube.cpp` 5, `render/testambientcubeexterior.cpp` 6,
`vsg/testambientcubecompile.cpp` 6, `apps/openmw_tests/mwworld/testneutralweatherambientcube.cpp` 6); no existing test edited.
**Red evidence:** the render file and the `openmw-tests` file could not compile against the old code (`flat` and `lerp` of
`Render::AmbientCube` and `blendAmbientCubes` did not exist: compile errors in both); the pack file compiled (the SDK fields were
added first) and was run against a build with the pack's fill switched off: 1 of 5 failed (the other 4 pin absence); the
compile test failed 2 of 6 when the old header-guard condition of `ambientcube.glsl` was put back (the terrain imports GI_SKY
without GI, so the buffer was declared twice).

**Data (five masters, Python, not committed):** every one of the 177 WTHR records has exactly 4 DALC subrecords (708 in all: 704
of 32 bytes, 4 of 24 bytes; the first 24 bytes are the six colours), in the order sunrise, day, sunset, night as NAM0 has. Cube
means against the flat NAM0 ambient (SkyrimClear day: cube mean (112, 150, 162), flat (180, 192, 197); cloudy day (123, 146, 142)
against (203, 220, 220)): **the cube is darker than the flat colour by about a third at noon**, and brighter at dusk, so
exteriors change in brightness, not only in direction. The face order +X -X +Y -Y +Z -Z is RECALLED-VERIFY (xEdit) and agrees with
the data as in stage e (the +Z face is the darkest and -Z the brightest: 191/214/221 against 77/129/136 for SkyrimClear's day).

**Design.** SDK `weather_source.hpp` 0.5 (additive trailing `hasAmbientCube` and `ambientCube` of `WeatherDescription`, default
none); the pack fills it from the four DALC (fewer than four: none). Engine: `WeatherParams::ambientCube`
(`[face][time of day]`), `Weather::mAmbientCube`; `calculateResult` interpolates each face by the same "Ambient" transition
windows as the flat colour; `calculateTransitionResult` blends two weathers' cubes with the pure `blendAmbientCubes` (a weather
with no cube counts as its own flat ambient on all six faces; neither: no cube); `WeatherResult` carries the cube and the flat
ambient it was made for (`mAmbientCubeBase`, taken BEFORE the lightning flash is added, so a flash adds to every face through the
shader's `flat - baseline` term, as it adds to the flat colour; Night Eye and the atmosphere model do the same). The weather
manager tells the renderer each frame (`RenderManager::setWeatherAmbientCube`: null for a weather with none, with the setting
off and indoors); the renderer writes the interior cube if there is one, else the weather's, into the stage e header-buffer
entries (same carrier, same shader function, no new binding or push constant). Objects need nothing new (their pipelines already
ask for `AMBIENT_CUBE`). **The terrain takes it too:** `terrain/main.frag` and its Slang twin `terrain.frag.slang` gained an
`AMBIENT_CUBE` path in the PBR branch and the plain branch (cube first, then the sky-visibility field where there is one),
`Pipeline::terrainDefines(..., ambientCube)` (a new defaulted trailing parameter), `TerrainMapSettings::ambientCube`, set by the
renderer when the cube setting and the weather setting are on. **A header-buffer fix:** the terrain imports GI_SKY without GI,
which the cube file's declaration guard did not allow for, so `skyvis.glsl/.slang` now `#define SKY_HEADER_DECLARED` and
`ambientcube.glsl/.slang` test that macro (no SPIR-V of any existing set changes). Setting `[Game] neutral weather ambient cube`
(default true, needs `neutral ambient cube` and a restart; profile and README lines added, `coverage.sh` checks it).

**Fog.** Nothing was added: the pack already maps FNAM's near, far, power and max (day and night) onto the engine's fog depth
(`fogDepthOf`: the fog amount `min(max, ((7168 - near) / (far - near)) ^ power)` at the reference distance; the day value for
sunrise and sunset, the night value for night) and the FogFar colour table; the engine's ramp is linear, so a power curve in
the exterior fog needs the fog shader and golden records. The interior fog curve this task was to reuse does not exist (stage d
applies near and far only: "fog power and maximum are not applied"). Left as one follow-up for both (decision for toddhow).

**Golden data for toddhow:** `files/shaders/terrain/main.frag.interface`, the `defines` line only (`AMBIENT_CUBE` added); no
set was added to `terrain/main.frag.sets` (the existing `PipelineTerrainDefines` test requires every listed line to be one of the
sets its model of the settings makes, and its model does not know the new define), so a terrain set with the cube draws with the
GLSL unless `slang shaders` compiles it at run time. In a run with `slang shaders = true` the run-time compiler compiled the
terrain set `AMBIENT_CUBE GBUFFER GBUFFER_MATERIAL` (259 ms on the worker) and accepted it against the GLSL's interface, so the
twin compiles and matches. Every other record file was regenerated by the write test and restored (line endings only).

**Frames** (`I:\Projects\OpenEden-work\followers\l55k\keep`: `sheet_SkyrimClear.png`, `sheet_SkyrimCloudy.png`; left off, right
on; Whiterun from the Bannered Mare's door, hdr, 800 x 600; noon, dusk (19:00) and night (23:00); forced weather SkyrimClear and
SkyrimCloudy; viewed): mean luma off to on, clear: noon 72.6 to 69.7 and 73.4 to 70.3, dusk 47.2 to 56.6 and 45.4 to 46.6, night
42.0 to 47.1 and 53.1 to 44.1; cloudy: noon 82.0 to 67.4 and 80.8 to 66.9, dusk 58.9 to 60.0 and 58.6 to 50.2, night 45.8 to 45.6
and 40.0 to 42.2 (the player falls to the ground in the first seconds of a run, so the pairs differ a little in pose). Noon is
darker and a little cooler with the cube (the sun carries more of the light), the undersides of timber and stairs take the
warmer, brighter ground bounce, vertical faces differ by direction, dusk is brighter and warmer. **Judgement against the real game
is from memory.**

**Cost** (gpu timing, hdr, SkyrimClear noon, two pairs): main scene median 0.824 ms off against 0.822 on in the first pair; the
second pair read 0.603 and 0.290 (the machine's GPU clock changed between runs, and the off run was the slower one), so no cost is
measurable beyond the stage e shader's extra header reads. The terrain adds the same up to four header reads per lit pixel with
the define.

**Morrowind unchanged** against the `n6saves` build: the Balmora fixed frame (seed 7, fixed frame time, frame 600) hashes equal
for both builds in six settings: default config (no `[Video]` lines) `0d6dd39995b927fa` and with hdr `0f7f7b30684efaea`; at
1280 x 720 the same two, and dusk (gamehour 18.7, timescale 0) `0aad4b07a29cfa05`, dusk with hdr `a1cc5cdbcd20b9fc` (the base
config of this machine gives these hashes, not the `1c5d47ad0b5dc32d` and `b451a8a6ca771e79` of other bases);
`tools/worldobjects_check` 110 objects OK against the same reference build.

**Tests run:** `components-tests` filter `Slang*:Pipeline*:*Weather*:*Skyrim*:*Terrain*:AmbientCube*:*Settings*:*Extension*:*Frozen*`
4,176 pass (the real-data ones skip without their gate); `openmw-tests` filter `*Weather*:*Neutral*:*Settings*:*Terrain*:*Render*:*Fog*`
1,201 pass; `CI/check_extension_sdk.sh`, `CI/check_tests_registered.sh`, `tools/testprofile/coverage.sh` pass.

**Not run:** the Linux gate, clang-format 14 (wrapped by hand to 120 columns), clang-tidy, the Lua tests, a fresh review, GI_SKY and
CLOUD_SHADOW together in a game frame (the compile of those combinations is a unit test; the in-game runs used neither), a
weather transition seen in frames (the blend is unit tested; a transition happens in a frame only by the clock), a thunder
flash with the cube, Winterhold, the whole of `openmw-tests` and `components-tests`.

**Decisions for toddhow.** (1) The cube is ON by default for Skyrim sessions; noon exteriors come out about 4 to 18 percent
darker than with the flat ambient (the data's DALC is dimmer than NAM0's ambient colour), dusk brighter. (2) The terrain takes the
cube (a shader path); without it the ground would stay flat-lit beside cube-lit objects. (3) Weathers with fewer than four DALC
records get none (the data has none). (4) Fog power and maximum are still not applied to exteriors or interiors. (5) No Slang set of
the terrain cube is listed in `main.frag.sets` because an existing test forbids it; say whether that test may be edited.

## Open questions for toddhow
1. Order: stages b to d first, then e (shader work), as proposed? Or the ambient cube (e) first because it moves the whole look?
2. Reference: can you provide screenshots of the real game (Bannered Mare, Dragonsreach, a dungeon, Whiterun at noon and night, same cameras) so stage b is tuned against something, not memory?
3. Defaults: should the lighting settings stay off by default (the guide's rule for anything changing look) until you have seen them, or may the Skyrim session default to "on" because there is no Morrowind look to preserve there? (They would still be a setting.)
4. The Morrowind `LightAttenuation_*` fallback keys currently set every light's falloff, also in Skyrim sessions: may a game pack override them for its own session (stage b)?
5. HDR/PBR: should these lighting stages assume `[Rendering] hdr` and `pbr` on (the test profile) so brightness above 1 and fade up to 10 are representable, or must they also look right in the default LDR path?
6. Is the interior "show sky" cell class (333 of 929, DATA bit 0x80) to get exterior-style light (sun, weather ambient) later, or stay interior-lit?
7. `light radius` unit: Skyrim and Morrowind share the unit here, but the falloff law differs; do you accept a tuning factor (a setting) found by eye against your reference?
8. The comment in `records.hpp` says 1,740 LIGH records in the five masters; the survey counted 496 per-plugin records. Do you know which is right (the comment may count the mod list)?

## Stages c and d: log (2026-11-19; branch `imgfog-0355cd`, local commits, not pushed; Windows only)
Approved by toddhow: stages c and d, each behind its own setting that is ON by default for Skyrim sessions only
(Morrowind unchanged). Same hand wrote tests and code; the new tests were written first, in new files only, and run red
against stubs; no fresh reviewer; no existing test edited.

**Stage c, the cell's image space (`[Game] neutral cell image space`, default true; reads only neutral cells).**
- Data: CELL XCIM names an IMGS (893 of 929 interiors; Bannered Mare and Dragonsreach `0x36ED2`: saturation 0.95,
  brightness 0.95, contrast 1.22; Bleak Falls Barrow `0x2EDEB`: saturation 0.95, brightness 1.0, contrast 1.2, a tint of
  amount 0.25).
- One mechanism: `describeImageSpaceGrade` (pack, `weather_source.cpp`) is the conversion the weather's four image
  spaces already used (refactored, not copied); the engine hands the result to the SAME renderer path
  (`RenderManager::setImageSpaceStops` / `setImageSpaceGrade`, the tonemap's GRADE define, the 0257 modifier still
  combines on top). New optional SDK interface `openeden/game/cell_image_space_source.hpp` 0.1 (`CellImageSpaceSource`,
  found by `dynamic_cast` like the weather and display-name sources: the frozen world model is untouched).
- When the engine applies it: on entering a neutral interior (`Scene::changeToInteriorCell`, after the fog call), and an
  exterior of a neutral world resets the renderer's grade to "off" (the weather sets its own every frame when
  `neutral weather image space` is on). Exteriors keep the weather's image space.
- **A neutral interior that names no image space gets NO grade** (decision: it does not keep the grade the last exterior
  weather left behind; before this task an interior kept the street's last grade).
- The grade shader (GRADE) is now also wanted by this setting, but only in a neutral world
  (`neutralGradeShaderWanted`): the setting is on by default, and a Morrowind hdr session must keep its tonemap pipeline.
- **Needs `hdr`** (no tonemap, no grade). Brightness goes in as exposure stops (log2, floor 0.25, clamp +-2: the weather
  manager's rule).
- **Contrast is NOT applied (`kCellGradeContrastShare` = 0).** The engine's contrast pivots on mid grey, the game's on
  the scene's average; a contrast of 1.2 crushed Bleak Falls Barrow to near black (`keep/barrow_fullcontrast_crushed.png`
  against `keep/barrow_off.png`). The same reason task 0257 left Night Eye's contrast neutral. Decision for toddhow.

**Stage d, interior fog (`[Game] neutral interior fog`, default true; reads only neutral cells).**
- Applied: the cell's fog NEAR and FAR (units, with the lighting-template inheritance of task 0303 already in
  `CellLighting::fogNear/fogFar`) as the interior's LINEAR fog start and end, in the cell's fog colour (the near colour).
  `MWWorld::Cell` has an optional neutral fog (`setNeutralFog`), set by `makeNeutralInteriorCell` when the cell is built;
  `FogManager::configure(viewDistance, cell)` takes it before the `[Fog] use distant fog` path (the cell's own distances
  win over Morrowind's) and keeps the underwater fog as the interior path gives it.
- Exact where fog power is 1 and fog max is 1 (the median). NOT applied: fog power (Barrow 0.7), fog max, the far colour,
  the clip distance. A fog far beyond the view distance is left as the data says. No fog when near < 0, far <= near,
  or a distance is not a number (the data writes 0 and 0 for none). The `NLIGHT` log line now also prints fog near/far.
- Data of the three cells (XCLL): Barrow near 0, far 12,000, power 0.7, fog colour (75, 99, 97); Bannered Mare near 128
  (from the template `0x6175D`; its own 512 is inherited away), far 2,000, power 1; Dragonsreach near 0, far 6,300.
- Morrowind cells never have a neutral fog, so the old code path is the only one for them.

**Tests (all new files).** Red first: `components-tests` `SkyrimCellImageSpace*` 6 of 7 failed against stubs (the one
that passed pins the refusal and the neutral `out`); `openmw-tests` `NeutralCellGrade*`, `NeutralInteriorFog*`,
`NeutralCellFog*` 7 of 11 failed (the 4 that passed pin absence and refusals), `NeutralGradeShader*` 2 of 3. One test
(`theContrastShareScalesTheDistanceFromNeutralAndDefaultsToNone`) was added, and one expectation given an explicit
share, after the first frames showed the contrast crush (not run red first). Green: 7 + 15 new tests;
`components-tests` filter `*Skyrim*:*Weather*:*Extension*:*Settings*:*ImageSpace*:*Frozen*` 3,729 pass, `openmw-tests`
filter `*Neutral*:*Skyrim*:*Extension*:*Fog*:*Settings*:*Cell*` 1,278 pass (the real-data ones skip without their gate).
`check_extension_sdk`, `check_layer_rules` (104), `check_tests_registered`, `check_game_conventions`, `coverage.sh`
(217) pass; `check_file_names` lists three files that are not mine.

**Frames** (`I:\Projects\OpenEden-work\combatai\l55cd\keep`, 800 x 600, `hdr = true`, the `look` scenario, settings
off against on): `barrow_off`, `barrow_on` (final), `barrow_fogonly`, `barrow_fullcontrast_crushed`; `mare_off`,
`mare_on`; `dragonsreach_off`, `dragonsreach_on`. On: a faint teal haze at depth in the Barrow, a soft grey-warm haze at
the back of the Mare and a little desaturation; the frames differ between runs (the scenario turns by real time), the log
lines (`NLIGHT`, `NIMAGESPACE`) are identical in 3 of 3 runs. The minimap in the corner still draws in fogged interiors.
**Judgement against the real game is from memory (no reference capture).** The Mare looks hazier and less warm than with
the settings off: the data's own fog (far 2,000, max 1) is strong for a room 1,500 units wide, and the real game's fog
power and maximum (not applied here) may soften it.

**Cost (fixed in a follow-up):** the first version read the weather records of every plugin on the main thread at the first
lookup (`lookup 1792 ms` warm). Now they are read on a worker (`BackgroundValue<T>`, `background_value.hpp`: one build
on its own thread, `ready()` / `tryGet()` never block, `take()` waits only for the old blocking weather path). The
optional SDK method `CellImageSpaceSource::imageSpacePending()` says "not yet"; `imageSpaceForCell` never waits. The
engine asks on entering the interior and again every frame in `Scene::update` while the answer is "not yet" (simplest
route that grades the cell the player starts in: the grade is applied on the first frame the pack is ready, a few
seconds into the first Skyrim interior of a session, while the cell still loads; later interiors are answered at once).
Measured: the main-thread ask now logs `NIMAGESPACE ... lookup 772 us` (was 1,792,000 us). New test file
`testskyrimsebackgroundvalue.cpp` (5 tests, 4 failed red against a stub; "asking while the build is still running returns
at once" holds the build shut and asserts nothing waited). The engine polling (`mCellGradePending`) has no unit test.
The worker starts at the first ask, so a session whose settings never ask reads nothing extra.

**Morrowind unchanged:** `tools/worldobjects_check` against `n6saves`' build: 111 result lines, 110 objects, OK;
`tools/skymap_check/morrowind_check.sh` frames `1eb68236352dc472` and `c819db116f4af8a6` equal for both builds; the
fixed Balmora frame (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME=0.0166667`, frame 600, empty
settings) `1c5d47ad0b5dc32d` for both; with `[Rendering] hdr = true` (the check that would see a GRADE leak)
`b451a8a6ca771e79` for both; a Morrowind interior (`coc "Balmora, Caius Cosades' House"`, hdr) gives `c03e0431af3f5e86` in
2 of 3 runs of each build (the third run of each differs: run noise, the same in both). No `NLIGHT` or `NIMAGESPACE`
line appears in a Morrowind log.

**Not checked:** the Linux gate, clang-format 14 (not installed; long lines wrapped by hand), clang-tidy, the Lua tests,
a cold-cache first entry, an interior with water in a frame, exteriors with the setting (the weather path is unchanged),
a Morrowind interior that has a fog density in a frame (starting by name in an interior did not finish under the harness,
on the reference build either; the `coc` route was used), a fresh review, brightness other than 0.95 and 1.0 in a frame.

**Decisions for toddhow.** (1) Contrast left out of the cell grade (share 0, `kCellGradeContrastShare`): set it to 1 for
the full value, or make the contrast pivot the scene's average luminance (a tonemap shader change). (2) The defaults are
ON, which is invisible in a default Skyrim session: the grade needs `hdr` (off by default). (3) A neutral interior with
no image space is not graded. (4) The cell's fog wins over `[Fog] use distant fog`. (5) Fog power, max and far colour as
a follow-up (a per-cell fog curve needs the fog shader, golden `.interface` records). (6) done: the weather read is on a worker.


## Follow-up (toddhow, 2026-11-19): per-pixel lighting for Skyrim models

**Cause:** the object shader sums the point lights per VERTEX unless a draw has a normal or specular map or the material
model (`PER_PIXEL_LIGHTING` in `default/main.frag`); `[Shaders] force per pixel lighting` was declared and read by nothing.
Most Skyrim surfaces have no wired normal map in the neutral loader at default settings (`pbr` off), so a local light made
a vertex smear on a big floor or wall triangle. (With `pbr` on, the test profile, the lights were already per pixel.)

**Change.** New pipeline mode and fragment define `FORCE_PER_PIXEL_LIGHTING` (`Pipeline::Mode`, appended to the enum; the
fragment shader imports it and `PER_PIXEL_LIGHTING` is defined for it too, in `main.frag` and its twin
`default.frag.slang`). (1) `SkyrimModel` (`skyrimmodel.cpp`, the static and the skinned shape path) asks for it, so EVERY
neutral model takes the per-pixel path: this is the neutral-only default. (2) `[Shaders] force per pixel lighting` is now
wired (`ResourceSystem` sets `Pipeline::Graphics::forcePerPixelLighting`; default false): it gives the same mode to every
lit draw, particles and debug geometry excepted. Morrowind's default is unchanged (the setting is off). (3)
`Pipeline::Graphics::create` leaves the define out where the material model already lights per pixel (`pbrLook > 0`), so
no define set exists twice and the listed Slang sets of the test profile stay listed. The vertex stage still computes
`pointLightDiffuse` (unused in the new path; skipping it needs a vertex define, left out).

**Slang / golden data.** The twin has the same one-line condition. The new define makes no new LISTED permutation: a
fragment set that contains it is not in `default/main.frag.sets`, so with `slang shaders` on it draws with the GLSL (or is
compiled at run time). Golden data for toddhow's review: one line of `files/shaders/default/main.frag.interface`
(`defines` gained `FORCE_PER_PIXEL_LIGHTING`; regenerated with `OPENEDEN_RECORD_INTERFACES=1`, the 41 other files the
writer touched were only line endings and were restored). `SlangInterfaceRecords`, `SlangPermutations`, `ShaderDefines`,
`ShaderTwinChoice`, `Pipeline*`, `*Settings*`, `MergedBody*` 288 tests pass.

**Tests (new file `apps/components_tests/render/testforceperpixel.cpp`, 8 tests, no existing test edited).** Red first
against the stub (enum value only): 3 of 8 failed (the loader's define, the wired setting, the loader's two call sites);
the two shader text tests passed against the stub because the shader edit was already in (not red); two tests pin
absence (no define without the request; none for a particle). All 8 green after.

**Frames** (`I:\Projects\OpenEden-work\combatai\l55f\keep`, 800 x 600, `hdr = true`, before = the build of `a4238aabd6`,
after = this change; `campair_N.png` is before on the left, after on the right; `cam_*` are fixed-camera spots of the
Bannered Mare with pitch 0.9 on the floor, `mare_*`, `dr_*`, `st_*` the turning `look` scenario): the floors and walls near
the lamps keep the same warm light but the vertex smear is gone: a smoother, more even gradient on boards (spots 4, 6, 7),
no change where no light reaches (spots 0, 1, 5: mean difference 0.0 to 0.5 of 255). The effect in the Mare is subtle (the
inn is lit by many small lights on a finely tessellated mesh); a round pool is clearest on large flat triangles. The Whiterun
street at hour 22 from the start spot has no light in view (dark in both). Viewed, not measured against a reference game.

**Cost** (GPU main pass median / wall median, RTX 4070, 800 x 600, one run each of before and after, the machine shared with
two other games): Bannered Mare `look` 0.113 to 0.153 ms / 3.505 to 3.501 ms; Dragonsreach 0.117 to 0.133 / 3.504 to
3.505; Whiterun street (hour 22) 0.269 to 0.326 / 3.518 to 3.559; fixed Mare spots 0.053 to 0.064 / 3.504 to 3.503. About
+0.01 to +0.06 ms of the main pass, the wall time within noise.

**Morrowind unchanged:** `tools/worldobjects_check` against `n6saves`' build: 110 objects, OK; `morrowind_check.sh` frames
`1eb68236352dc472` and `c819db116f4af8a6` equal for both builds; fixed Balmora frame `1c5d47ad0b5dc32d` (empty settings) and
`b451a8a6ca771e79` (`hdr = true`) equal for both; a Morrowind interior with torches (`--start "Balmora, Caius Cosades'
House"`, hdr) gives the two hashes `d72322a95bc86c82` and `dc449f89f3eb768c` in both builds, in a different order (run noise,
the same set).

**Not checked:** the Linux gate, clang-format 14, clang-tidy, the Lua tests, `openmw-tests`, a fresh review, `slang shaders`
on with the new define in a frame, `[Shaders] force per pixel lighting = true` on Morrowind in a frame (wired, unit tested
only), the skinned (actor) path in a close-up, a round pool in a frame on a very large triangle.

**Decisions for toddhow.** (1) Neutral models always take the per-pixel path (no setting to turn it off; the old build is
the comparison): say if you want a `[Game]` switch. (2) `force per pixel lighting` now does something for Morrowind when
set. (3) The vertex stage still sums the lights it no longer needs (a vertex define would drop it). (4) The listed Slang
permutation sets do not cover the new define, so `slang shaders` on falls back to the GLSL for Skyrim draws without `pbr`.
## Follow-up (2026-11-19; branch `lightfog-0355e`, local commits, not pushed; Windows only): HDR default for Skyrim, interior fog power and maximum
Same hand wrote the code and then the tests (the new test files were checked by a mutation build: five one-line mutations made 6 of the new tests fail; one clamp mutation survived and got a test); no fresh reviewer; no existing test edited.

**1. `hdr` on by default in a Skyrim (neutral) session only.** `Settings::NeutralDefaults::apply` (`components/settings/neutraldefaults.hpp`), called by `Settings::Manager::load` after the user settings.cfg is read and before the setting values are made: when `[Game] neutral world` is true (user value, else default) and the user has NOT written `[Rendering] hdr` (and no other config folder's settings.cfg changed the default), the DEFAULT of `hdr` becomes true. Nothing is written to the user settings, so nothing is saved to settings.cfg at exit; an explicit `hdr = false` or `hdr = true` wins; Morrowind (`neutral world` false) keeps `hdr = false`; the editor's load is untouched. No new setting (task 0357's navmesh thread count is a second setting with its own default read by a chooser; `hdr` is read in nine places, so one default swap at load was the smaller change). Log: `HDR: rendering the scene in 1 sample(s) to a 16-bit float target` appears in a default Skyrim run and not with `hdr = false`.

**2. Interior fog power and maximum.** XCLL offset 36 is the fog power and offset 76 (92 byte XCLL only) the fog max; template inherit bits 0x100 and 0x200 (the existing test pins 0x7e0 as rotation, fade, clip, power, max, fade). Field order is from xEdit (RECALLED-VERIFY); checked against the five masters' 969 XCLL and LGTM records: power is 1.0 in 625, 0.4 to 0.9 in 331, 1.5 to 2.5 in 5 (so it is a power of the linear share); max is 1.0 in 786, 0.2 to 0.9 in 147, 0.0 in 22 ("not set") and 6000, 13000, 8 in 4 (garbage). The engine makes both safe (`neutralFogCurve`: a power outside [1/64, 255/64] or not a number is 1; a max that is not in (0, 1] is 1). The fog share is `pow(linear, power) * max`.
- Pack: `CellLightingData::mFogPower/mFogMax`, the inherit rule, new optional SDK interface `openeden/game/cell_fog_source.hpp` 0.1 (`CellFogSource::fogCurveForCell`, found by `dynamic_cast` like the image space source; the frozen `CellLighting` (1.1, pinned by `testfrozen`) is untouched), implemented by `SkyrimWorldView` (from the cell index, no file read) and forwarded by `SkyrimCellSource`.
- Engine: `Cell::setNeutralFog(start, end, power, max)`, `FogManager::getFogCurve` (never under water, `{1,1}` for everything but a pack interior). The curve rides in the FRACTION of `scene.fogColor.w` (the scene uniform may not grow): `Render::FogCurve::encodeMarker` (components/render/fogcurve.hpp) gives marker 1 or 2 plus (power code * 256 + max code) / 131072, a code below 0.5 so every reader of `> 1.5` is unchanged; code 0 (the plain marker, bit for bit) means none. `lib/view/fog.glsl` and `fog.slang` apply `fogCurve()` only in the linear branch of `applyFog`. No change to the scene block, the descriptor interface or any `.interface` record (`SlangPermutations` and `SlangInterfaceRecords` pass unchanged); no golden data touched. Developer log `NFOGCURVE <cell> data power p max m -> ...`.
- Not applied: the far colour (no room in the scene uniform; in the three cells below it equals the near colour anyway: Barrow (75,99,97) both, Mare and Dragonsreach (88,83,71) both), the fog clip distance.

**What the three cells do (XCLL after the lighting template):** the Bannered Mare (power 1, max 1) and Dragonsreach (power 1, max 1) are LINEAR in the data, so the new code changes nothing for them (the Mare's grey haze is its own near 128 / far 2,000 linear fog in (88,83,71), unchanged: decision 3 below). Bleak Falls Barrow has power 0.7 (its own, not inherited) and max 1.0 (its template, which it does not inherit, has 0.7 and 0.9): the fog comes in earlier and is denser at mid distance.

**Frames** (`I:\Projects\OpenEden-work\followers\l55e\keep`, 800 x 600, fixed frame time, the `look` scenario): `barrow_after` against `barrow_before_linearfog` (same executable but for `neutralFogCurve` made to return nothing, same camera): a visibly stronger teal haze in the mid distance of the Barrow with the curve; mean luma of the six shots 30.0, 23.7, 18.6, 22.2, 37.0, 39.1 against 24.0, 20.3, 16.6, 19.2, 32.2, 34.0 (+2 to +6). `*_hdroff` against `*_after` for the Mare, Barrow and Dragonsreach: with hdr off (the old default) the pictures are brighter and flatter, with the default hdr the tonemap and the cell image space (saturation 0.95, brightness 0.95 in the Mare and Dragonsreach, a tint of 0.25 in the Barrow) show. `NIMAGESPACE` lines now appear in a default Skyrim run.

**Morrowind unchanged:** `tools/worldobjects_check` against `n6saves`' build: 111 result lines, 110 objects, OK; the fixed Balmora frame (`--start Balmora --random-seed 7`, fixed frame time, frame 600, only `[Sound]` volume 0 in settings.cfg) `1c5d47ad0b5dc32d` for both builds, and with `[Rendering] hdr = true` (the check that sees the shaders) `b451a8a6ca771e79` for both, the HDR log line present in exactly the hdr runs; a Morrowind interior with fog (`coc "Balmora, Caius Cosades' House"`, hdr) gives frame hashes that vary run to run in BOTH builds (3 runs each: reference `ba0ba975..`, `4f400c82..`, `73582115..`; new `02433196..`, `4f400c82..`, `ba0ba975..`), the new build's values among the reference's: run noise, not a difference. The Slang twin (`fog.slang`) is built by the same slangc commands and covered by `SlangPermutations`; the frames above ran the GLSL path (the default), the Slang switch was not run in a frame.

**Tests (new files only).** `components-tests`: `RenderFogCurve*` 9 (exact code by hand, round trip over a grid of codes at both markers, the 1.5 split, other views decode as linear, the shader text of both twins), `NeutralDefaults*` 5, `SkyrimFogCurve*` 5 (generated plugins: own values, each inherit flag, a 64 byte XCLL, no lighting, the cell source forwards); `openmw-tests`: `NeutralFogCurve*` 5 (sanitising by hand, the cell and the fog manager carry it, never under water, reset by the other configure). Mutations: the share without max, a max code clamped to 254, the user-value check removed, the power read at offset 32, the template max not inherited; 6 tests failed, and the 254 mutation passed until `aMaxOfOneAndAPowerOfOneAreExactAfterTheRoundTrip` was added.

**Not checked:** the Linux gate, clang-format 14 (not installed; lines wrapped by hand), clang-tidy, the Lua tests, a frame with the Slang shaders switched on, an exterior (the curve is for interiors only), the other 900 interiors (only the three), a template that supplies power or max through the inherit bits in a frame (unit tested), fog under water in a pack interior (no curve by rule, not seen), a fresh review.

**Decisions for toddhow.** (1) `hdr` is a default swap at load time keyed on `[Game] neutral world`; a user who wants the old look in Skyrim writes `hdr = false`. (2) A max outside (0, 1] is 1: the 22 zeros and the three huge values in the data become linear (RECALLED-VERIFY whether the game reads max 0 as "no fog"). (3) The Bannered Mare and Dragonsreach are linear in the data, so their haze is the data's own near and far and fog colour; if it still looks wrong against the real game the cause is elsewhere (the colour space the fog colour is mixed in, the near value 128 inherited from the template against the cell's own 512, or the fog being applied to a part of the light only), not the curve. (4) The far colour is not applied.
## Stage h log (2026-11-19): each Skyrim light's own flicker and pulse
Branch `flicker-0355h` from `a4238aabd6`, local commit, not pushed; Windows only. Approved by toddhow 2026-11-19. Same hand
wrote tests and code; tests first, in new files only, run red against stubs (components-tests 16 of 23 failed, the 7 that
passed test refusals and constants; openmw-tests 4 of 6), then green (23 and 6; `*Light*` 136 and `*Light*:*Neutral*` 927
pass); no existing test edited; no fresh reviewer.

**Data (the five masters, 496 LIGH records, Python survey; layout DATA +28 period, +32 intensity amplitude, +36
movement amplitude is RECALLED-VERIFY from xEdit, the values fit it):** flicker 190 lights (period 0.05 to 1.0 s, median
0.33; intensity amplitude 0 to 1.5, median 0.4, 12 are 0; movement 0 to 512, median 4, 14 are 0), flicker-slow 11 (period
1.0, amplitude 0, movement 0), pulse 49 (period 0.07 to 1.0, median 0.33; amplitude 0 to 5, median 0.3, 2 are 0), pulse-slow
0, no animation 246.

**What changed.** Before, every Skyrim light ran Morrowind's one rule (`LightController`: a random walk between 0.25 and 1
at 15 steps a second, speed 0.1) whatever its record said. Now (`[Game] neutral light flicker`, default true, neutral
lights only): the pack hands the three numbers on (`LightComponent::flickerPeriod/Intensity/Movement`, SDK trailing
fields, version stays 1.1); `makeNeutralEsmLight` packs period (ms, low 16 bits) and amplitude (thousandths) into
`ESM::Light::mData.mTime`, a per-placement seed into `mValue` and sets the OpenEden flag `ESM::Light::NeutralFlicker`
(0x20000, cleared by `ESM::Light::load`, like `NeutralGain`); `MWAnim::addLight` then runs `NeutralLightController`
(`components/mwanimation/lightflicker.*`, pure `Walk`): the brightness stays in [1 - amplitude, 1] (amplitude capped at
0.95); a flicker picks a splitmix64 target every period and goes there in a straight line, a pulse goes between the two ends
in half a period each, eased. Seed = splitmix of the placement's content file and reference index (`neutralLightSeed`): no
clock, no global generator, the same on every run. A light whose record has an amplitude or period of 0 (the 12 flickers, 11
slow flickers and 2 pulses of the masters) keeps Morrowind's rule. Morrowind lights never carry the flag, so their path and
their draws from `Misc::Rng` are untouched.

**Per-frame trace (`OPENEDEN_LIGHT_FLICKER_TRACE=1`, new developer aid; Bannered Mare, 700 frames at a fixed 1/60 s,
brightness of the light colour over its base; before = setting false):**
| light | rule | min | max | mean | turns in 600 frames | mean abs step |
| hearth `b491470` (radius 461, period 1.0, amplitude 0.25) | Morrowind | 0.338 | 0.888 | 0.599 | 56 | 0.0249 |
| | own | 0.752 | 0.995 | 0.863 | 5 | 0.0017 |
| candle `b760807` (radius 77, period 0.333, amplitude 0.3) | Morrowind | 0.296 | 0.896 | 0.596 | 53 | 0.0249 |
| | own | 0.704 | 0.998 | 0.847 | 22 | 0.0040 |
Start of the first second (every second frame): Morrowind hearth `0.68 0.69 0.69 0.65 0.61 0.56 0.51 0.46 0.41 0.36 0.36 ...`
(a fast random walk), own hearth `0.86 0.86 0.86 0.86 0.85 0.85 ...` (a slow shallow drift), own candle `0.71 0.71 0.72 ...
0.76 0.76 ... 0.73` (a slow rise and fall over a third of a second).

**Morrowind unchanged:** `worldobjects_check` against a copy of the n6saves build: 111 lines each, OK (110 objects);
Balmora fixed frame `1c5d47ad0b5dc32d` and `hdr = true` `b451a8a6ca771e79` for both builds; Arrille's Tradehouse (flickering torches):
12 lights run Morrowind's rule, 0 the new one, and the frame differs from the reference by 0 pixels over 8 levels (two runs
of the new build differ from each other by 75 pixels over 8 levels, as two runs of one build do). Test profile: line and README entry added; `coverage.sh` OK.

**Decisions for toddhow.** (1) The mean brightness is higher with the record's numbers (0.85 to 0.86 against 0.60): the walk
stays between 1 - amplitude and full instead of a quarter and full. If the old average is to be kept, the colour needs a
factor or the range centred below 1 (a one-line change in `Walk`). (2) The movement amplitude (a light that moves) is not used:
a point light in this renderer has no wobble; 14 lights with 512 would need it. (3) Pulse is a smooth eased swing between the two ends;
Skyrim's own pulse curve is not known. (4) Period meaning (seconds between new targets) and amplitude meaning (a fraction
of the brightness) are RECALLED-VERIFY.
**Not run:** the Linux gate, clang-format 14, clang-tidy, the Lua tests, a fresh review, Whiterun exterior, a frame sequence
(the trace stands in; light changes are small between frames in a still image), a comparison with the real game.
## Stages f and j: log (2026-11-19; branch `sunfade-0355fj` from `a4238aabd6`, local commits, not pushed; Windows only)
Approved by toddhow: stage f (interior sun direction and fade) and stage j (light fade distances and clip distance),
smallest complete change each. Same hand wrote tests and code; the new tests were written first, in NEW files only, and
run red against stubs (`components-tests`: 16 of 25 failed, the 9 that passed pin absence, a version constant and
refusals; `openmw-tests`: 8 of 12 failed, the 4 that passed pin absence). One of my own tests had a helper fault (a 64 byte
XCLL padded to 80 bytes) found on the first green run and fixed in the same new file. No fresh reviewer. No existing test
edited.

**Data (XCLL layout of xEdit, RECALLED-VERIFY; the five masters agree with it).** Offsets 20 and 24 rotation XY and Z
(i32 degrees), 28 directional fade, 32 clip distance, 80 and 84 light fade begin and end, 88 inherit flags (rotation 0x20,
fade 0x40, clip 0x80, light fade 0x400; the same words in LGTM DATA). Of 928 interiors with a 92 byte XCLL (the survey
script `light55/fj.py`, throwaway): after the lighting template, rotation (XY, Z) is (0, 90) in 415, (0, 0) in 390,
(270, 240) in 31, (200, 90) in 29, (0, 270) in 23; fade 0.0 in 614 (66 percent), 1.0 in 265, 0.1 to 0.5 in 49, 5.0 in 5;
clip distance 0 in most (the template's 1,800 to 100,000 where set); light fade (0, 0) in most, (4000 to 13000, +1000)
in most of the rest, (11000, 11000) in 5, (100, 5000 or 50000) in 14.

**What the numbers mean, derived from the data.**
- *Rotation.* The lighting templates' own default is (0, 90) (92 of 126 LGTM records), which is the engine's present
  assumption "from above": so Z is the ELEVATION of the light above the horizon (90 above, 270 and 240 below: the
  (270, 240) cells are fort and cave interiors lit from below with fade 1). XY is the heading; with Z at 90 or 270 it does
  not matter, so 87 percent of the cells are not affected by its sign. Toward-light vector = (cos Z sin XY, cos Z cos XY,
  sin Z), XY clockwise from north (+Y): **RECALLED-VERIFY, the sign and the zero of XY are a guess** (about 50 cells
  have a Z that is not 90 or 270, 12 of them with XY other than 0).
- *Fade.* **toddhow's decision, 2026-11-19: a fade of 0 means the FULL key light (1); only a fade strictly between 0 and 1 dims it; the rest clamps to 0..1 (a 5.0 is 1, a negative is 0).** (My first reading, fade as plain intensity with 0 = none, is replaced; the paragraph below is the evidence I gave for it.) First reading: the key light's INTENSITY, clamped to 0..1. Why: (0, 0, fade 0) is 349 cells and (0, 0, fade 1) only 41,
  i.e. the unconfigured cells (rotation zero) are also the ones with fade zero, and the configured ones (templates,
  (0, 90) or (270, 240)) have fade 1: fade behaves like "is there a directional light". The directional COLOUR does not
  tell (mean luma 75 at fade 0, 79 at fade 1, so authors fill the colour either way). Not proven; the intensity reading
  has the large effect that **all three survey cells (Bannered Mare, Dragonsreach, Bleak Falls Barrow) state fade 0, so
  they lose the engine's flat key light completely** (and 66 percent of interiors do). A 5.0 (five cells) is clamped to 1.
- *Light fade.* Lights are full inside `begin` from the camera, fall in a line to nothing at `end`; an `end` that is not
  past `begin` is a cut at `begin`; (0, 0) none. Today the engine has no distance rule for a light, only its radius
  (`View::CollectLights::pack`, `components/view/collectlights.cpp`, and the clustered grid by radius).
- *Clip distance.* The far plane of the projection (`RenderManager::updateProjectionMatrix`, which used `mFarPlane` only)
  is the cell's clip distance when that is between 256 and the far plane (smallest in the masters: 1,800).

**Built.** SDK: new optional interface `openeden/game/cell_light_source.hpp` 0.1 (`CellLightSource::lightSettingsForCell`,
found by `dynamic_cast` like the image space and fog sources; the frozen world model is untouched). Pack: `CellLightingData`
gains the six words, `applyLightingTemplate` takes them by the four inherit bits, `SkyrimWorldView::lightSettingsForCell`
and the cell source forward (no file read: the lighting is in the index). Engine: `mwworld/neutralcelllight.*` (pure
`neutralInteriorSun`, `neutralClipDistance`, `neutralLightFade`; `applyNeutralCellLight`, called after the fog in
`Scene::changeToInteriorCell` and in `changeToExteriorCell`, so a street never keeps an interior's values and a Morrowind
cell gives the renderer exactly the values it starts with), `Render::LightFade` (`components/render/lightfade.hpp`:
`factor`, `effectiveFarPlane`), `RenderManager::setNeutralInteriorSun / clearNeutralInteriorSun / setNeutralLightFade /
setNeutralClipDistance` (the sun's intensity multiplies the linear light colour when the scene data is built; the light
direction is `mLightPosition`, reset by `configureAmbient` for each interior; the light fade is passed to
`View::CollectLights::setDistanceFade` and applied in `pack()` AFTER the linear conversion, nothing at all when both
distances are 0). **No shader, no Slang twin, no `.interface` record changed.** New settings `[Game] neutral interior sun`
(f) and `[Game] neutral light fade` (j: both the light fade and the clip distance), both default TRUE, read only for a
neutral world's interior cells; settings default, test profile and README line added, `coverage.sh` passes (217). Log
line `NLIGHTFJ <cell> sun <x,y,z> intensity <i> fade <b> to <e> clip <c>`.

**Frames** (`I:\Projects\OpenEden-work\combat3\l55fj\keep`, 800 x 600 `look` scenario, hdr on, off against on): Bannered
Mare, Dragonsreach and Bleak Falls Barrow (`mare_sheet`, `dr_sheet`, `bb_sheet`: three of six turns each, left off, right
on; the camera turns by real time so the pairs are not the same frame) and Fort Neugrad 02 (`fn_sheet`: rotation 270, 240,
fade 1: lit from below). With the key light gone the rooms are flatter and a little darker on surfaces that faced up
(mean luma over the six frames of a run, off to on, with the camera timing noise: Mare 70, 66, 73, 87, 62, 60 to 66, 65,
80, 74, 71, 59; Barrow 24 to 23 and 20 to 19; Dragonsreach 53 to 51); nothing breaks. The light fade has NO visible case in
the three cells (their fade distances are 0): it is shown by the unit tests (pack decode, the factor, `pack()` on real
`PointLight`s) and by the log line of `LargashburShagrolsLonghouse` (`fade 3000 to 4000`, a fade 1 cell lit from above).
The clip distance is applied in the Mare (2,000, the same as its fog far), Dragonsreach (7,000) and the Barrow (12,000): no
visible clipping. Three runs of the Mare give the same `NLIGHT` and `NLIGHTFJ` lines (md5 `376f9752dd1e`).

**Morrowind unchanged:** `tools/worldobjects_check` against `n6saves`' build 110 objects, OK; the fixed Balmora frame
(`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600) is `1ae72bec04593e1b` for the `n6saves` build and
this build and `60ef9c87d5ab5a3e` with `hdr = true` for both (the harness' base config gives these, not the numbers
written in the task text, in both builds); a Morrowind interior (`coc "Balmora, Caius Cosades' House"`, hdr, frame 900,
two runs each): the pixels that differ by more than 8 levels between the two builds all lie in one 125 x 90 pixel patch
(the same patch that differs between two runs of ONE build: 203 and 11 pixels over 8 levels within a build against 1,880
and 733 between builds, in that patch), everywhere else the frames are pixel for pixel equal. No `NLIGHTFJ` line in a
Morrowind log. **Not exact:** the interior frame is not byte repeatable even in one build (run noise), and `n6saves` is an
older tree than the parent: no build of the exact parent was made.

**Tests.** New: `components-tests` `RenderLightFade*` (8), `ViewCollectLightsFade*` (5), `SkyrimCellLight*` (12);
`openmw-tests` `NeutralInteriorSun*`, `NeutralClipDistance*`, `NeutralLightFade*` (12). The filter
`*Skyrim*:*Weather*:*Extension*:*Settings*:*ImageSpace*:*Frozen*:Render*:View*:*Light*` of `components-tests` 5,276 pass and
`*Neutral*:*Skyrim*:*Extension*:*Fog*:*Settings*:*Cell*:*Light*` of `openmw-tests` 1,307 pass (data-gated tests skip).
`check_extension_sdk`, `check_layer_rules` (104), `check_game_conventions`, `coverage.sh` pass.

**Not checked:** the Linux gate, clang-format 14 (not installed; long lines wrapped by hand), clang-tidy, the Lua tests, a
fresh review, the light fade or a clip in a frame that shows them, an exterior to interior to exterior walk in game (the
exterior reset is by code reading and the equal Balmora exterior frame), the fade effect on point light shadows (the
shadow snapshot keeps the unfaded colour), `neutral interior sun` with `hdr` off (the multiplier is in the scene data
either way), other cells than the five.

**Follow-up (toddhow, 2026-11-19):** fade 0 is the full light, so the Mare, Dragonsreach and the Barrow keep their key light (now in the cell's direction: the Mare logs `sun 1.2e-16,-1,0 intensity 1`). One change in `neutralInteriorSun`; test `NeutralInteriorSun.aFadeOfZeroIsTheFullLightAndOnlyBetweenZeroAndOneDimsIt` (the renamed fade test of my new file, expectation changed first: red, then green). Mare frames off against on viewed (`keep/mare_fade1_sheet.png`): the room is lit as before apart from the new direction. Filters again: components 5,276 pass, openmw 1,307 pass; Balmora frames `1ae72bec04593e1b` and hdr `60ef9c87d5ab5a3e` equal for `n6saves` and this build; `worldobjects_check` 110 OK. The earlier frames of this log (key light off) are from the first reading.

**Follow-up 2 (toddhow, 2026-11-19): rotation exactly (0, 0) is "not set"** (349 cells) and keeps the engine's old fixed direction from above, (-0.15, 0.15, 1) normalised; any other rotation is the cell's own. One change in `neutralInteriorSun`; my test `zeroRotationIsAHorizontalLight...` was replaced by `rotationZeroZeroIsNotSetAndKeepsTheEnginesOldDirectionFromAbove` and `zeroXyAtElevation90IsAboveAndZ270IsFromBelow` (red first: 1 failed; then green). **The Mare's rotation is (180, 0), not (0, 0)** (own and template, checked in the data), so it still logs `sun 1.2e-16,-1,0 intensity 1`: a horizontal light from the south that its data really states; a (0, 0) cell now logs `sun -0.146,0.146,0.978`. Filters: components 5,276, openmw 1,308 pass. Mare frames viewed (`keep/mare_unset_sheet.png`). Balmora frames `1ae72bec04593e1b` / hdr `60ef9c87d5ab5a3e` equal the values recorded before for `n6saves`; `worldobjects_check` 110 OK, but against this build itself as reference, because `n6saves`' build was being rebuilt by another agent and its `openmw.exe` no longer starts with its new `defaults.cfg`.

**Decisions for toddhow.** (1) DECIDED, see the follow-up. (2) The XY heading convention and zero are a guess. (3) The light fade multiplies the colour but
keeps the light in the list: no saving in the cluster grid (a possible follow-up with the per-light stage). (4) One
setting, `neutral light fade`, covers the light fade and the clip distance. (5) A cell's clip distance now shortens the
far plane of a neutral interior (nothing beyond it is drawn).
## Stage g: per-light shadow flags and spot cones (2026-11-19; branch `spotshadow-0355g`, local commits, not pushed; Windows only)
Approved by toddhow: stage g, smallest complete change, behind `[Game] neutral light types` (ON by default, Skyrim sessions only).
Same hand wrote tests and code; the new tests are in new files only and passed at first run, so their evidence is six
one-line mutations (spot not packed, shadow rule ignored, cone not set, Never not kept, file load not clearing the cone,
shadow rule always Casts: 8 named tests failed), not a red run against stubs; no existing test edited; no fresh reviewer.

**Correction of stage a's flag bits.** Stage a took SpotLight 0x400, SpotShadow 0x800, Hemisphere 0x1000, Omnidirectional
0x2000 (RECALLED-VERIFY). The Mutagen Skyrim schema (read through houseCARL) and the editor ids of the records say:
SpotLight 0x200 (in none of the five masters), ShadowSpotlight 0x400, ShadowHemisphere 0x800, ShadowOmnidirectional 0x1000,
PortalStrict 0x2000 (a portal culling hint on records of every kind, NOT a shadow type). In Skyrim.esm (a houseCARL scan
of the 400 lights that have a flag, a FOV and a radius): spot 33, hemisphere 10, omnidirectional 78. `LightFlags` now has
these values; the stage a text "omni 216, hemisphere 110, spot 39, spot-shadow 12" counted the shifted bits.

**What is built.**
- Pack: `makeLightComponent` sets `LightComponent::spotFov` (the FOV, 1 to 170, only with the spot bit 0x200 or 0x400) and
  `LightComponent::shadow` (`Casts` with any of 0x400, 0x800, 0x1000, else `Never`). SDK `world_model.hpp`: two trailing
  fields and the `LightShadow` enum; the version stays 1.1 (a pinned test).
- Engine record: `makeNeutralEsmLight(light, fadeGain, lightTypes)`; `ESM::Light` gains `NeutralSpot`, `NeutralShadowCasts`,
  `NeutralShadowNever` (never loaded from a file) and `mNeutralSpotFov` (outside the file struct; `blank()` and `load()`
  reset it). Developer aid `OPENEDEN_NEUTRAL_LIGHT_FORCE_FOV=<degrees>`: every neutral light becomes a spot of that angle.
- Node: `View::TypedPointLight` (`components/view/typedlight.hpp`, a `vsg::PointLight` with `shadowRule`, `spotCosHalf`,
  `spotAxis` = local +X); only a record with the neutral bits makes one, so every Morrowind light is a plain node.
- Light buffer (`components/view/lightdata.hpp`): the buffer keeps its two vec4 per light and, from two lights up, gets
  one more vec4 per light after them (the spot axis in eye space and the cos of the half angle, 0 for no cone); the SECOND
  light's position.w holds `maxLights` as the marker (0 or absent: a buffer of any other origin, no cone), so the struct
  and every recorded shader interface are unchanged (`SlangInterfaceRecords`, `SlangPermutations`, `SlangLightGridGpu` pass,
  **no golden `.interface` file changed**). `createLightingDescriptors` finds the light count with `maxLightsOf`.
- Shaders (GLSL and Slang twins): `pointLightSpot` and `pointLightCone` in `lib/light/pointlights.*`, used by
  `pointdiffuse`, `pointspecular`, `pixellighting` and `pbrlighting`. The cone is smooth between cosOuter and
  cosOuter + 0.3 (1 - cosOuter) (`spotConeSoftness`, ours). NOT applied in `precipitation` (rain) and `volume` (fog).
- Shadow slots: `PointShadows::update` asks `Render::PointShadow::lightMayCast(colour, snapshot.shadowRule)`: a light whose
  record says `Never` takes no slot; `Unspecified` (Morrowind) and `Casts` keep the old nearest-first rule. The snapshots
  carry the rule (`LightSnapshot::shadowRule`).

**Tests** (new files): `testskyrimselighttypes.cpp` (8), `testneutrallighttypes.cpp` (5), `testlighttypes.cpp` in view (9)
and in mwanimation (5), `testlightneutraltypes.cpp` in esm3 (2). `components-tests` filter
`*Light*:*Skyrim*:*Weather*:*Extension*:*Frozen*:*WorldModel*:*PointShadow*:*Slang*:*Collect*:*Esm*` 4,292 pass;
`openmw-tests` filter `*Neutral*:*Skyrim*:*Light*:*Extension*:*Settings*` 1,230 pass; `check_layer_rules` 104;
`coverage.sh` ok.

**Frames** (`I:\Projects\OpenEden-work\papyrus\l55g\keep`; harness `tools/neutral_session_check/check.sh ... l55g` with
`L55G_SPOTS`): `mare_spot_off_on_0..2.png` (Bannered Mare, hdr, every light forced to a 50 degree spot: types off left, on
right: the wall shows one lit disc with a soft edge and the rest dark), `mare_spot_glsl_slang_2.png` (GLSL left, Slang right:
0 pixels over 8 apart), `barrow_start_types_false_true_0..3.png` (Bleak Falls Barrow, same camera, types off and on, point
light shadows on: slots 0 and 1 go to the spot sunlight at (3224, -3328, -543) and a torch at (2241, -4803, -2390); with the
setting off the four nearest candles took them), `morrowind_arrille_new_old.png`. A real spot lamp was not seen: the cells
tried have their spots 400 to 1000 units above a floor with reach 300 or less, and Lua cannot create a neutral light.

**Morrowind unchanged** (against `integ2`'s build; `n6saves`' build was being rebuilt by another session and stopped at
start-up): `worldobjects_check` 110 objects, OK; Balmora fixed frame `1c5d47ad0b5dc32d` (empty settings) and
`b451a8a6ca771e79` (hdr) equal for both builds; Arrille's Tradehouse (`coc`) with hdr, with hdr, gbuffer and pbr, and with
point light shadows added: 0, 0 and 1 pixel over 8 apart (one build against itself: 0 to 924 in the plain hdr case, noise).

**Cost.** Each point light loop iteration now reads one more word (the marker) and, once the buffer holds a spot region,
the light's spot vec4. Main scene GPU time, Arrille's with hdr, pbr and point light shadows, five interleaved runs each,
shared machine: new 0.153 0.165 0.169 0.245 0.263 ms, old 0.149 0.149 0.163 0.188 0.358 ms (median per run): not
separable from the noise; the minimum is +0.004 ms. The Skyrim Barrow figures are noisier still (0.25 to 0.77 ms for the same
build). Not measured: a crowded Skyrim interior with the cone on many lights.

**Not checked / limits.** The Linux gate, clang-format 14 (long lines wrapped by hand), clang-tidy, the Lua tests, a fresh
review; the direction of a real spot lamp in a frame (+X is from the data and the pack's rotation convention); a controlled
shadow comparison frame (the differences in the Barrow pair are small); rain and volumetric fog ignore the cone; a buffer
with fewer than two lights has no cone; hemisphere and spot shadows are cube maps.

**Decisions for toddhow.** (1) The corrected bit meanings replace stage a's (a data fact, not a taste). (2) The default is
on; with it on only about 338 of the 1,740 lights can take shadow slots, so most torches and candles in a dungeon no longer
cast point light shadows (slots are scarce: 4). (3) `spotConeSoftness` 0.3 and the +X axis. (4) A world model SDK change (two
trailing fields) without a version bump.
