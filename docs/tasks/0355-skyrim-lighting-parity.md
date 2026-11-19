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

## Open questions for toddhow
1. Order: stages b to d first, then e (shader work), as proposed? Or the ambient cube (e) first because it moves the whole look?
2. Reference: can you provide screenshots of the real game (Bannered Mare, Dragonsreach, a dungeon, Whiterun at noon and night, same cameras) so stage b is tuned against something, not memory?
3. Defaults: should the lighting settings stay off by default (the guide's rule for anything changing look) until you have seen them, or may the Skyrim session default to "on" because there is no Morrowind look to preserve there? (They would still be a setting.)
4. The Morrowind `LightAttenuation_*` fallback keys currently set every light's falloff, also in Skyrim sessions: may a game pack override them for its own session (stage b)?
5. HDR/PBR: should these lighting stages assume `[Rendering] hdr` and `pbr` on (the test profile) so brightness above 1 and fade up to 10 are representable, or must they also look right in the default LDR path?
6. Is the interior "show sky" cell class (333 of 929, DATA bit 0x80) to get exterior-style light (sun, weather ambient) later, or stay interior-lit?
7. `light radius` unit: Skyrim and Morrowind share the unit here, but the falloff law differs; do you accept a tuning factor (a setting) found by eye against your reference?
8. The comment in `records.hpp` says 1,740 LIGH records in the five masters; the survey counted 496 per-plugin records. Do you know which is right (the comment may count the mod list)?
