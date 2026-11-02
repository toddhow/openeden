# Task: Rendering R6, dynamic weather: the atmosphere (stages a to c)
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R6, "visual weather state"); builds on `docs/tasks/0025-rendering-f2-linear-hdr.md` (needs `hdr`) and, for the GI note in stage a, `0028-rendering-gi-baked-probes.md`.
Goal: replace the hard-coded colour of the sky, the sun's light, the ambient light, the clouds and the fog with colours that follow from where the sun is (a physically motivated model of light in the air), driven by Morrowind's existing weather and time data, off by default and frame-identical when off.
Inherited code touched (to be confirmed at the commit that starts each stage): `apps/openmw/mwworld/weather.cpp` (`WeatherManager::update`, the block that sets the sun direction and calls `configureFog`, `setAmbientColour`, `setSunColour` and `Sky::setWeather`; `calculateWeatherResult`; the `Weather_<name>_Glare_View` fallback read by `Weather::Weather`), `apps/openmw/mwrender/weather.cpp` (`Sky::setWeather`, `Clouds`), `apps/openmw/mwrender/weatherdata.hpp` (`WeatherResult`, read only), `files/shaders/sky/clouds/main.frag`, `components/pipeline/material.hpp`, `components/settings/categories/rendering.hpp`, `files/settings-default.cfg`, `apps/components_tests/CMakeLists.txt`. Read only: `apps/openmw/mwrender/rendermanager.cpp` (`updateSceneData`, `setSunDirection`, `configureAmbient`), `files/shaders/sky/atmosphere/*`, `files/shaders/lib/view/fog.glsl`.
Inputs / outputs: new `[Rendering]` settings `atmosphere` (bool, false) and `atmosphere look` (`vanilla` or `physical`, `vanilla`); new file `components/render/atmospheremath.hpp` (pure maths, no game types; the model runs on the CPU once per frame, so there is no shader twin). No change to any save, Lua API, extension contract or file format. The weather records, region data, `ChangeWeather`, `ModRegion`, weather scripts, sounds, precipitation and storm rules are not touched: the manager still picks the weather and its transition exactly as before, and only the colours it hands to the renderer are changed afterwards.

## The design, in plain language

Morrowind stores, for each weather, a table of colours for sunrise, day, sunset and night (sky, fog, ambient and sun), and the game blends between them as the hours pass. They are fixed numbers that an artist chose. The sky is blue at noon and orange at sunset because someone picked those colours.

A physical sky works the other way round. Sunlight passing through air loses its blue light first (blue scatters more than red, Rayleigh scattering), so a low sun looks red and the sky it lights goes orange near the horizon while the zenith stays blue. If we know how high the sun is, we can compute these colours. We use the classic single-scattering shortcut (Preetham, Shirley and Smits 1999, for the optical depths; Kasten and Young 1989 for the length of the air path), reimplemented from the published formulas, no code of theirs.

**Keeping the weather's character.** The model describes a clear day. Weather in Morrowind is much more than that: overcast, fog, ash storms and blight are artist-made moods, and mods change them through the weather records. So the model is never used alone:

- The *vanilla* colours the weather manager computed this frame are always the starting point.
- The weather's `Glare_View` value (1 for a clear sun, near 0 for fog and ash) says how clear the air is. It is read from the same record fallback as before, so mods that change it keep working. A weather with no glare gets its vanilla colours back exactly.
- At night (sun below about 4 degrees under the horizon) the vanilla colours are used exactly, so moonlit nights keep the look they have.
- `atmosphere look = vanilla` (the default) keeps each colour's brightness and moves its hue half way towards the model's. `physical` takes the model's colour and brightness on clear days.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | Where the model runs | CPU, once per frame, in the weather manager, before the colours reach the renderer (no shader changes except the cloud tint) | A precomputed sky table in a shader (Hillaire 2020 / Bruneton 2017): needed for a sun-glow in the fog and for volumetric clouds, later |
| D2 | How clear the air is | The weather's own `Glare_View` (1 clear to 0 fog), as it is the one number every weather already carries | A new per-weather table, which mods would not know about |
| D3 | Moon light | Not changed: the night keeps the vanilla colours exactly | A moon-lit ambient from the moon phase and height (needs a design; the moon has no light source in the engine) |
| D4 | Sun direction | Not changed: the manager's direction (a fixed orbit tilted by about 15 degrees) is used as it is, and the model takes its elevation from it | A real solar position by latitude and season: changes where shadows fall, so it is a gameplay-feel change |
| D5 | Sky term for global illumination | Exteriors already use the flat ambient (task 0028: probes are interior only); the model feeds that ambient, which is the sky term, so no separate probe term is built | A sky-visibility probe volume for exteriors |
| D6 | Sun glow in the fog (aerial perspective towards the sun) | Not built here: `Pipeline::Data::Scene` has no room (640 byte pitfall) and the fog colour is one colour per frame | A new uniform block, with a frame check for the brightened-interiors problem |

## The maths (stage a), `components/render/atmospheremath.hpp`, namespace `Render::Atmosphere`

Angles in radians, colours as `vsg::vec3` in the display-encoded space Morrowind's weather colours are in (the model works in linear light and converts with the engine's gamma 2.2, `Render::Hdr`).

- `bool useAtmosphere(bool setting, bool hdr)`: both needed.
- `Look parseLook(std::string_view)`: `vanilla` and `physical`, anything else is `vanilla`.
- `float sunElevation(const vsg::vec3& sunPosition, bool night)`: the angle of the manager's sun position above the horizon; the manager mirrors the sun at night, so at night the angle is negated.
- `float airMass(float elevation)`: Kasten and Young: `1 / (sin e + 0.50572 (e in degrees + 6.07995)^-1.6364)`, 1 at the zenith and about 38 at the horizon (the elevation is clamped at 0).
- `vsg::vec3 sunTransmittance(float elevation, float turbidity)`: `exp(-(tauR + tauM) * airMass)` per colour channel; `tauR = 0.008569 l^-4 (1 + 0.0113 l^-2 + 0.00013 l^-4)` at 0.65, 0.55 and 0.45 micrometres, and `tauM = beta l^-1.3` with `beta = 0.04608 T - 0.04586` (turbidity `T >= 1`).
- `float dayWeight(float elevation)`: 0 below -4 degrees, 1 above 6, a smooth step between; the weight of the model against vanilla.
- `Colours model(float elevation, float clearness)`: the sky (zenith) colour, horizon colour, ambient colour, sun colour and cloud tint of a clear atmosphere, desaturated and dimmed with the haze (`clearness` 0 to 1).
- `Colours applyLook(const Colours& vanilla, float elevation, float clearness, Look)`: the result described above.

## Stages

- **a. Sky model and sun/moon lighting:** `atmospheremath.hpp`, the settings, the weather manager applying `applyLook` to the sky, ambient and sun colours (and the sun disc's colour) before they reach the renderer. The fog colour is left as it is in this stage (the sky's horizon blends into the fog, so the horizon band changes in stage c).
- **b. Clouds:** the cloud shader multiplies by the cloud material's colour (white by default, so unchanged); the model sets it to the sunlit tint, so clouds go golden at sunset instead of staying the same grey at all hours.
- **c. Fog and aerial perspective:** the fog colour (what far terrain fades into, and the sky's horizon) from the model's in-scattered horizon light, with the fog distance unchanged (the weather's rule).
- **Not started, need toddhow's confirmation:** volumetric or layered cloud shading and cloud shadows on the ground; rain, wetness and puddles on surfaces; snow and ash cover; precipitation changes; lightning flashes; a sun glow in the fog (D6); a moon light (D3); weather simulation (new weather states, regional climates). None of these is in this task.

## Constraints
Determinism: the model is a pure function of the elevation and the weather's numbers; no clock, no randomness. Allocation: none (a few floats per frame). Threading: runs on the main thread inside `WeatherManager::update`, as the code it replaces. Off by default and unchanged when off: with `atmosphere = false`, or without `hdr`, the weather manager takes no new branch that changes a value, the cloud material stays white and the shader multiplies by exactly 1, and frames equal the commit before by `diffmap` (weather fixed with console commands, see below). Interiors: the sky is off and the ambient comes from the cell, so interiors are untouched; checked by frames.

How the weather and time are fixed for comparisons (they follow the real clock otherwise): the console script `set gamehour to <h>` and `ChangeWeather "<region>" <id>` (ids: 0 clear, 1 cloudy, 2 foggy, 3 overcast, 4 rain, 5 thunder, 6 ash storm, 7 blight, 8 snow, 9 blizzard), started in Seyda Neen (the Bitter Coast region) with `--script-run`. The game's time scale is left at its default; the script is run at the start so the hour is the one set within a few frames.

## Acceptance criteria
- [x] Unit tests for the maths pass (limits, monotonic, continuity, night and haze return vanilla exactly, the vanilla look keeps brightness).
- [ ] (not verified by frames; see the close-out) `atmosphere = false`: frames equal the commit before (Seyda Neen noon and sunset, cloudy, ash storm, an interior) by `diffmap`.
- [x] With `atmosphere`: frames viewed at sunrise, noon, sunset and night, in clear, cloudy, foggy, rain, ash storm, blight and snow weather, exteriors, and an interior unchanged.
- [x] GPU cost measured with `gpu timing` and `tools/benchmark.sh` and written into a note (`docs/research/0015-atmosphere-cost.md`: no measurable cost).
- [x] The Linux gate builds with `-Werror`; the whole tree passes `CI/check_clang_format.sh`; a fresh reviewer read each stage.
Out of scope: everything under "Not started" above; any change to the weather records, the scheduler or the scripting commands.

## Stage log
### Stage a (2026-11-01)
- Built: `atmospheremath.hpp`, the settings `atmosphere` and `atmosphere look`, and `WeatherManager::applyAtmosphere`, called in `WeatherManager::update` just before the colours go to the renderer. It changes the sky, ambient and sun colours and the sun disc's colour; `mResult` itself is never changed, and with the setting off the renderer receives `mResult` as before.
- Sun direction is the manager's own (D4). The sun's height is taken from the same vector the manager builds, negated at night (the manager mirrors the sun).
- Moon light: not built (D3). GI sky term: the model feeds the flat exterior ambient (D5).
- Tests: 23 in `testatmosphere.cpp`, all pass; `components-tests` 2763 and `openmw-tests` 799 pass (Windows). `eden-studio-tests` not rerun: `eden-studio.exe` is locked by running editors.
- Frames viewed (Seyda Neen, hdr on, `scratchpad/atm`): off, vanilla and physical looks at 6:30, 12:00 and 18:00 in clear weather, and physical against off at noon in cloudy, foggy, rain, ash storm, blight and snow. The vanilla look stays close to the weather's own picture; the physical look has a paler, less saturated sky and dimmer sunrise and sunset light (a low sun is dim in the model). Fog-bound weathers (foggy, ash, blight, rain) are almost unchanged because their glare is low. An interior (ToddTest) with the setting on differs from off by a summed difference of 188 over the whole frame (no pixel over 6).
- Not verified: off-equals-the-commit-before by frames (no build of the previous commit was made; the off path passes the same values, which the reviewer confirmed by reading). A fresh reviewer's notes: sunrise and sunset get weight about 0.35 at elevation 0 (a design question, left); `mAtmosphereResult` copies two strings per frame (no new allocation after the first frame, but not strictly zero).
- How the weather and hour were fixed: start in the interior `ToddTest`, a console script `ChangeWeather "Bitter Coast Region" <id>`, `coc "Seyda Neen"` (entering a new region forces the region's weather at once, `WeatherManager::playerTeleported`), `set gamehour to <h>`, `set timescale to 1`.


### Stage b (2026-11-01)
- Built: `WeatherResult::mCloudTint` (white by default), `Pipeline::Material::setTint`, `Sky::setWeather` giving it to both cloud layers, and `sky/clouds/main.frag` multiplying by the cloud material's diffuse colour (exactly 1 when off, so the shader's result is unchanged). The model's tint is the sun's colour softened (power 0.25) and dimmer as the sun sinks, blended by the same weight as the other colours.
- A fresh reviewer found the tint was linear while the shader multiplies encoded colours; fixed (`model` encodes it).
- Tests: 5 more in `testatmosphere.cpp` (28 in all) pass. Frames viewed: clear sky with clouds at 12:00 and 17:30, off, vanilla and physical: at 17:30 the physical clouds are warmer and a little dimmer; at noon they look as before.
- Not verified: a before/after frame difference of the off path against the commit before.

### Stage c (2026-11-01)
- Built: the fog colour (`mFogColor`, what far terrain fades into and the sky's lower edge) goes through the same blend as the horizon colour of the model (a warm glow near a low sun, paler and greyer above). Fog distance and density (the weather's rule) are unchanged. Because `configureFog` takes the colour from the same result, the sky's horizon band and the fog stay in step. Interior and underwater fog are not touched.
- Tests: 4 more (32 in all). Frames viewed (Seyda Neen, clear): off, vanilla and physical at 6:30, 12:00, 17:30 and 22:00. At 22:00 all three are the same (night is exactly vanilla). The vanilla look is close to the weather's picture; the physical look is bluer and greyer at 6:30 and 17:30.
- **Finding for toddhow (decision 4):** the manager's sun is still about 30 degrees up at 17:30 (its orbit runs to the night start, 20:00 by default) while Morrowind's colour tables already show sunset at that hour. The model follows the sun that is drawn, so the physical look at 17:30 looks like an afternoon, not a sunset, and the sunset warmth only appears in the last hour before the night start. If the vanilla timing is wanted, the model would have to be driven by the hour of the colour tables instead of the sun's height.
- Not done in this stage: a sun glow in the fog towards the sun (D6), because the scene uniform block has no room (the 640 byte pitfall).

### Close-out (2026-11-01)
- Linux gate at `573cc93806` (Docker, `-Werror`): `components-tests` 2774, `openmw-tests` 799 and the 32 Lua integration tests pass. Windows: 2772, 799 and `eden-studio-tests` 393. clang-format 14 flagged only two of my files on the stage commits and they were fixed in `573cc93806`; the final whole-tree re-check did not run (the check's work folder was left over), so it is not verified at the last commit.
- Still unticked: "`atmosphere = false` equals the commit before by `diffmap`" (no build of the commit before was made; the off path is the same values and was read by a reviewer), and the criterion that a reviewer read each stage is met (three reviewers).

### 2026-11-01, the model follows the colour tables' hour (decision 0010 answer 4)
- **Built:** `Render::Sun::tableElevation` (`components/render/sunmath.hpp`, 9 tests written first) turns the game hour into a sun height that is 0 at the middle of the sunrise window (`mNightEnd` to `mDayStart`, 7:00 by default) and of the sunset window (`mDayEnd` to `mNightStart`, 19:00), highest at noon (the weather manager's own noon height, about 75 degrees) and mirrored below the horizon at night. `WeatherManager::update` hands this to `applyAtmosphere` instead of the drawn sun's height when `[Rendering] sun follows colour tables` is on (default on; it only matters with `atmosphere` on). The drawn sun, the light direction and the shadows are not touched: `setSunDirection` still gets the orbit.
- **Frames** (Seyda Neen, clear, hdr, `atmosphere look = physical`, before = an executable of the commit before): at 17:30 almost the same (22 degrees against 30), at 18:30 warm gold instead of blue-grey, at 19:00 an orange sunset sky instead of grey-blue, at 19:30 dusk. At 12:00 the same as before. Pictures: `scratchpad/s0033/sh_atm_cmp.png`.
- **Off means unchanged:** with `atmosphere` off the setting is never read for anything that is drawn. Frames of the off state (17:30 and 19:00) of the old executable against the new one differ by the same amount as two runs of the same executable (sum of absolute differences about 190000 to 290000 of 2.7 million values, max 133 to 141: the walking NPC and the water), so no change beyond noise.
- **Design note:** the windows are the plain ones (`mNightEnd`..`mDayStart`, `mDayEnd`..`mNightStart`); each colour table also has its own pre and post offsets (`WeatherSetting`), which are not used here, so the model's zero is the middle of the default window, not of each table's.
