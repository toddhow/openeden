## 0362a Image-space contrast on the scene's average, night exposure and interior fog re-tuned (branch `lightgrade-0362a`)

**Contrast pivot.** The HDR path has no average luminance (no auto exposure; exposure is one fixed scale, `sceneColor` has one mip and a nearest sampler;
bloom's chain exists only with bloom on). So `gradePivot()` in `files/shaders/post/tonemap.frag` and `.slang` (GRADE only) samples 6 x 6 taps of
the scene colour, averages in linear light, applies the picture's own exposure, curve and encode, takes the luma and holds it between 0.02 and 0.8;
contrast is `(c - pivot) * contrast + pivot` (mid grey stays the pivot for contrast 1, so nothing changes without contrast). CPU twin:
`Render::ImageSpace::Grade::pivot` (default 0.5, so every pinned test value is unchanged). Cost: 36 taps per pixel only in a graded Skyrim session
with a contrast other than 1 (not measured on the GPU). No push constant, binding or `.interface` change; the grade is not a built Slang permutation.
**Existing tests pin `kCellGradeContrastShare == 0` and the default share**, so the constants stay as the pure functions' defaults and the two call sites
(`NeutralCellAtmosphere` cell grade, `WeatherManager` weather grade) pass the new `kContrastShareWithAveragePivot = 1`.
Frames (`I:\Projects\OpenEden-work\combat3\sc62a\keep`): `diag` pose 3 (cliff-shadowed ground) stays textured; Whiterun by day and night are not crushed;
Barrow start view readable. (The other Barrow shots of the group are flat fog colour: the group's relative poses leave the room, not caused here.)
Tests: `ImageSpacePivot` 5 (new file; written after the shader, not run red: they pin default pivot, dark pivot, pivot fixed point, bad pivot, both shaders
use the pivot), `SlangPermutations`, `SlangInterfaceRecords`, `ImageSpace*`, `Tonemap*` pass.

**Night exposure and interior fog.** Mean luma of Whiterun street (hours61, 1600x900, SkyrimClear): with 1.5 stops night read 72 to 79 (day 83, the
same as day: not night); 0.8 gives 54 to 61 at 20 to 02 h (19 h 70), 0.4 gives 46 to 62 with more black (7 to 10 percent below 12). Chose
`neutral weather night exposure = 0.8`. Bannered Mare (`mare` group, hour 20): density/lamps 0.5/0.3 vs 0.3/0.2, mean luma 93.7/125/101 vs 96/114/101;
the thinner fog keeps depth and warm light but less milky; chose 0.3 and 0.2. Run to run noise at fixed settings was up to +-7 mean luma at night (three
runs at 1.5), so these are judged by frames too (`sheet_night.png`, `sheet_mare.png`).

Morrowind: Balmora fixed frame `1c5d47ad0b5dc32d` and hdr `b451a8a6ca771e79` equal for `integ2` and this build. Not run: `worldobjects_check`, Linux gate,
clang-format 14, clang-tidy, Lua tests, GPU cost of the 36 taps, a before frame of the pivot against the old mid-grey (no toggle exists), Night Eye
contrast (still neutral, task 0257).

### 0362a follow-up: cost of `gradePivot()` and worldobjects_check
`tools/worldobjects_check` against `integ2`: 110 objects, OK, exit 0, no dumps.
GPU cost (1920x1080, `gpu timing`, `diag` pose, graded, pivot on against `gradePivot()` forced to 0.5 in the build's shader copies, interleaved, 5 pairs):
the machine's GPU was shared (85 percent busy, about 28 GPU processes), so single runs ranged 0.33 to 1.3 ms for the tonemap pass. The uncontended floors
were: pivot on 0.393 ms (4 runs, identical), pivot off 0.334 ms (1 run): about +0.06 ms (+18 percent of the pass); contended pairs are not usable. Analytic
estimate: 36 cached fetches x 2 M pixels = 75 M texel fetches. Left as is (36 taps); a once-per-frame reduction was not built (needs a new compute or
fragment reduction pass; the tonemap's vertex stage cannot see sceneColor).
# Task 0362 (lighting finish), section 0362b: fog far colour, light movement, light fade and clip frames
Branch `lightmisc-0362b` from `803799d67c`, local commit, not pushed; Windows only. Same hand wrote code then tests (no red runs: evidence is the compile-red of the new APIs and the checks below); no existing test edited; no fresh reviewer.

## 1. Fog far colour (`[Game] neutral fog gradient`, default true, Skyrim only; needs `neutral ambient cube` and, for interiors, `neutral interior fog`)
- Survey (five masters): XCLL offset 72 (92 byte record; LGTM DATA the same) is the fog FAR colour (xEdit layout, RECALLED-VERIFY). It differs from the near colour in 222 of 928 interiors' own data and, with the template (far colour inherits with the FogColor flag, RECALLED-VERIFY), in 408 of 928 by more than 30 summed levels; 95 of 125 templates. Weathers: the pack already used Fog FAR as the weather fog colour; Fog NEAR differs from it at some hour in 158 of 165 weathers (median day difference 155 summed levels, max 403). The earlier note "equal" held only for the three look cells.
- Design: `scene.fogColor` is unchanged on every path (an interior's is the near colour, a weather's the far colour), so every path that does not blend is as before (volumetric fog, water, particles, aerial haze, exponential fog, atmosphere, under water). The fog's SECOND colour rides in the ambient-cube header entries (`[base+0].yzw` and `[base+1].w` = +1 far end / -1 near end / 0 none; no new vec4, scene uniform untouched). The object and terrain shaders (GLSL and Slang twins, `applyFog` overload with `fogStart`, `fogStartColour()`) blend by the fog's LINEAR share. The weather skips the gradient while the atmosphere rewrites its far colour.
- Plumbing: `CellFogSource::fogFarColorForCell` (SDK 0.2, trailing, default none), `WeatherDescription::hasFogNear/fogNear` (trailing; `kWeatherSourceVersion` stays 0.5 because an existing test pins it), `CellLightingData::mFogFarColor`, `Cell::setNeutralFogFarColor`, `FogManager` other colour, `RenderManager::configureFog` extra arguments, `Render::AmbientCube::write` 5 argument form, `Weather::mFogNearColor` and `WeatherResult::mHasFogNear`.
- Existing test `RenderFogCurve.theShaderTwinsHold...` pins the exact `fogValue = fogCurve(clamp(...))` line, so the linear share is computed on a second line.
- Frames (`I:/Projects/OpenEden-work/papyrus/fog362/keep/fog_*_off_left_on_right.png`, left gradient off, right on): CrystaldriftCave01 (near (60,82,100), far (191,223,240)): the depth haze is paler and bluer with the gradient, up to 408,841 pixels differ by more than 8 levels; GraywinterWatch01 (near (62,77,98), far (223,232,236)) up to 111,874. No golden `.interface` record changed.

## 2. Light movement amplitude (`[Game] neutral light movement`, default true; needs `neutral light flicker`)
- Data: LIGH DATA +36 (RECALLED-VERIFY: xEdit "movement amplitude", units): 216 of 250 animated lights have one, median 5, 90th percentile 64, maximum 512 (effect lights, radius 256); amplitude over radius median 0.0195, 90th percentile 0.25, max 2.0.
- A flickering or pulsing neutral light wanders in a seeded cube of that half-size, a new point every period, eased (own generator, so brightness sequences are unchanged). Cuts: a quarter of the radius, 64 units, and a SPEED limit of 40 units/s (amplitude at most 20 x leg length: a 0.083 s light moves under 2 units). `ESM::Light::mNeutralMovement` (not a file field), `NeutralFlicker::Params::mMovement`, `Walk::offset()`, `NeutralLightController` moves `PointLight::position`.
- **toddhow's decision: a light that holds a point shadow slot does not move** (checked against `Anim::pointShadowOwner` each frame, so the shadow cache is not redrawn every frame); it keeps its place and moves on when it loses the slot. Unit tested only.
- Trace (`OPENEDEN_LIGHT_FLICKER_TRACE=1`, CrystaldriftCave01, 1,800 samples, period 0.333 s): offsets reach 6.56 units at most, e.g. `off -0.26,-0.87,0.74`, `0.50,0.21,-0.74`, `0.72,0.42,0.84`.

## 3. Light fade and clip distance in frames
- Engine `NLIGHTFJ` lines: SolitudeCastleDourEmperorsTower `fade 400 to 6000 clip 2200`, ShroudHearthBarrow02 `fade 9000 to 10000 clip 3000`, SolitudeHalloftheDead `fade 100 to 2000 clip 5000`. Of 928 interiors only two have a light fade end under 2,500 (SolitudeHalloftheDead, WindhelmHalloftheDead).
- Frames (`keep/fade_*_off_left_on_right.png`, `neutral light fade` false left, true right, fixed camera, fixed frame time): the Emperor's Tower shows the lights further down the hall dimmer with the fade on (up to 214,535 pixels differ by more than 8 levels); SolitudeHalloftheDead differs in two views (90,843 and 92,114); ShroudHearthBarrow02 shows no difference from its start spot (0 pixels): its rooms are shorter than the fade begin.
- The clip distance is NOT visible in any frame: in the cells tried the visible sightlines are shorter than the clip, or the fog already hides what lies beyond it. No vanilla interior was found where the clip cuts a visible object (clip well inside fog far in 18 cells).

## Morrowind unchanged
`worldobjects_check` against `integ2/build`: 110 objects OK. Balmora fixed frame (`--start Balmora --random-seed 7`, fixed frame time, frame 600, harness base config) `0d6dd39995b927fa` for both builds, and with `hdr = true` `0f7f7b30684efaea` for both (the hashes this harness config gives; the task text's `1c5d47ad0b5dc32d` / `b451a8a6ca771e79` came from another config).

## Tests
New files only: `components-tests` `FogStart*` (5), `SkyrimFogFar*` (4), `LightMovement*` (9); `openmw-tests` `NeutralFogFar*` (4), `NeutralLightMovement*` (3). Filter `*Slang*:*Skyrim*:*Render*:*Pipeline*:*Light*:*Fog*:*AmbientCube*:*Weather*:*Settings*:*Extension*:*Frozen*:*Neutral*` of `components-tests` 5,683 pass and `*Neutral*:*Skyrim*:*Extension*:*Fog*:*Settings*:*Cell*:*Light*:*Weather*` of `openmw-tests` 1,382 pass (2 data-gated skips); SlangPermutations and SlangInterfaceRecords pass with no golden change; `check_layer_rules` 104; `coverage.sh` OK (217).

## Not run
The Linux gate (`-Werror` traps checked by hand), clang-format 14, clang-tidy, the Lua tests, mutation checks of the new tests, the Slang switch in a frame, a volumetrics-off frame, an exterior frame of the weather gradient, a moving light with the shadow cache on in game, a fresh review.

## Decisions for toddhow
(1) The far colour inherits with the FogColor flag (RECALLED-VERIFY). (2) The gradient is skipped under the atmosphere and for every path that does not read the header. (3) Movement caps (a quarter of the radius, 64 units, 40 units/s) are mine. (4) `kWeatherSourceVersion` not bumped.
