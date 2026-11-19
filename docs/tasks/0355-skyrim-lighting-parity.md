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
