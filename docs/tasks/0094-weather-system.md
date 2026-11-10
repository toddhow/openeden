# Task: Weather system (storm clouds, lightning, better rain)
Design doc section: `docs/research/0048-weather-system.md` (inventory, constraints, options, risks). Builds on `0034` (atmosphere), `0037` (wetness), `0038` (cloud shading), `0039` (cover), `0040` (precipitation lighting, lightning as light, sun glow), `0041` (volumetric fog), `0042` (volumetric clouds), `0075` (TAA, transparents), `0077` (point-light shadows), `0079` (cirrus); decisions 0009 (Slang), 0010, 0012.
Goal: make the weather Morrowind already chooses look and feel far better (storm clouds, visible lightning that lights the clouds, layered rain with splashes and curtains), as opt-in settings, without changing which weather is active, any record, script or save.
Status: DRAFT, docs only, awaiting toddhow's approval (this spec is the place for that line: `docs/toddhow-checks.md` has toddhow's own uncommitted edits, so no line was added there on 2026-11-09). Full loop (Track V renderer area) for every stage.

Inherited code touched (at `main`, 2026-11-09; confirmed again at each stage's start):
- `apps/openmw/mwworld/weather.cpp`: `WeatherManager::update` and the `updateWetness`, `updateCover`, `updateLightning`, `applyAtmosphere` family (a new `updateWeatherState` joins them), `Weather::calculateThunder`, `lightningAndThunder` (read only; the strike event is observed, not changed), `calculateWindSpeed`, `calculateStormDirection`.
- `apps/openmw/mwrender/weatherdata.hpp` (`WeatherResult`: new fields only if the contract cannot get them otherwise), `apps/openmw/mwrender/weather.cpp` (`Sky::applyVolumeClouds`, `Sky::getVolumeClouds`, `VolumeCloudInput`), `apps/openmw/mwrender/rain.cpp` and `rain.hpp` (`RainSimulation`, `Rain`), `weathereffects.cpp`, `precipitationoccluder.*`.
- `components/render/cloudvolumemath.hpp` (`coverageFromThickness`, `blendCoverage`, `volumeShareTarget`, `windStep`, `windDirection`), `cloudvolumegpu.hpp`, `files/shaders/comp/cloud/cloud.slang`, `lightningmath.hpp`, `wetnessmath.hpp`, `covermath.hpp` (names as in the tree; confirm), `files/shaders/lib/light/precipitation.glsl`, `files/shaders/default/main.frag` (rain lighting only), `files/settings-default.cfg` `[Rendering]`, `tools/testprofile/*`.
Inputs / outputs: new `[Rendering]` settings, all false/off by default (list per stage below); pure headers `components/render/weatherstate.hpp` (W0), `rainmath.hpp` (W1), `stormcloudmath.hpp` (W2), additions to `lightningmath.hpp` (W3: bolt generator, thunder delay); Slang shaders for any new pass. No change to weather records, `WeatherManager`'s selection logic, scripts, saves or `Weather::calculateThunder`'s random draws.

## Constraints (all stages)
- **Off by default, and unchanged when off.** With every new setting at its default the frames, the draw/pipeline set and the vanilla random draws are as before (compared by frame diffmaps at a fixed time and weather, a `OPENEDEN_SHADER_TRACE` comparison, and a test that the vanilla PRNG sequence is identical on and off). A new setting in fair weather is also identical (no storm term evaluated).
- Weather selection, regions, `ChangeWeather`, transitions, saves: untouched. Mods that change weather fallbacks keep working because values come from the same `Weather_<name>_*` fallbacks.
- Determinism: game time and seeded hashes only; the strike's place and bolt shape are a hash of (seed, strike index, distance index); no draw from `World::getPrng` beyond the vanilla ones.
- Photosensitivity: the 0040 cap (peak 1) and the 1.5 s minimum interval between flash starts bound every flash-like effect (cloud glow, bolt, sky flash) as one event; tested.
- Performance: no allocation per frame (preallocated buffers); the whole system adds at most about 0.3 ms GPU in the worst storm at medium tier 1080p (proposed, open question 4), and nothing in fair weather; measured per stage with `tools/benchmark.sh` scenes copied for storms.
- Threading: everything updates on the main thread with the weather manager; no new threads. Each stage states which thread each callback runs on.
- New blended draws go into `vsgUtil::DeferredBin` (task 0075) so TAA does not ghost them; they are not part of any temporal history.
- Shaders: new ones in Slang (decision 0009) with GLSL-free interfaces checked by `SlangPermutations`; edits to existing GLSL stay GLSL.
- Interiors: unchanged (the existing `isExterior` gates). Editor: builds and draws; takes none of this.
- No game assets: textures (splash, mist) are generated procedurally or authored here under GPL-3.0-or-later.
- Living test profile: each stage adds its settings to `tools/testprofile/profile.cfg` (or a `variants/*.cfg` if it excludes another setting or is costly) with a `# task 0094: ...` comment and a `README.md` line, or to `exclude.txt` with a reason (debug views, tuning); `tools/testprofile/coverage.sh` must pass.

## Stages
Each stage is independently shippable, off by default, tests first (separate writer, run red against a stub), then frames and measurements, then a fresh reviewer.

### W0 Weather state contract (size S, risk low)
`Render::WeatherState` in `components/render/weatherstate.hpp` (no game types): precipitation kind (`none`, `rain`, `snow`, `ash`, `blight`, `blizzard`) and intensity 0 to 1 (from `mPrecipitationAlpha` and `mRainMaxRaindrops` relative to the heaviest vanilla value), wind vector (direction from `mStormDirection` or the fixed cloud wind, speed from `mWindSpeed`), storminess 0 to 1 (a pure function of the weather's thunder frequency, wind and rain; thunderstorm 1), `lightning`: a strike counter and the strike's vanilla distance index (0 to 3), cloud targets: coverage, base height, thickness, darkness, anvil. Built each frame by `WeatherManager::updateWeatherState` from fields it already has; handed to the renderer with one setter (`RenderManager::setWeatherState`). The strike is observed through a read-only counter added to `Weather` beside `mPendingStrike` (no extra random draw).
Settings: `weather state debug` (log the struct on change; exclude.txt). No visible change: this stage ships no effect.
Acceptance: [ ] pure derivation tests (table-driven for all vanilla weathers, monotone intensity, finite for NaN/negative inputs); [ ] vanilla PRNG sequence identical with the struct built and not built (test on `Weather::calculateThunder`'s draws); [ ] frames identical (0 pixels differ); [ ] zero allocations per frame (counter test).

### W1 Better rain and snow (size M, risk medium)
Settings: `rain layers` (bool), `rain splashes` (bool), `rain curtains` (bool), `rain wind direction` (bool), `rain strength` (0 to 2, 1), `rain quality` (low/medium/high). Needs `hdr`.
- Layered rain: a near layer (big, bright, few, lit by 0040's precipitation light) and a far layer (fine, dense, fading into fog) from one `RainSimulation` with two size/speed classes; capacity budgeted per tier.
- Wind slant from the true wind vector (not +Y only): `rain.cpp` velocity uses the vector; the slant limit stays below 50 degrees (`sWindAngleDivisor` behaviour kept at the vanilla wind).
- Splashes and ripples: a ground-hit sprite at a deterministic subset of drops (hash of drop index and spawn counter), drawn as short expanding rings/spikes near the camera on any upward surface hit found by the existing precipitation occluder depth (so no per-drop ray), and on water via the existing ripple function if it can be shared (RECALLED-VERIFY, note 0048 section 6).
- Curtains: a screen-space term in the fog composite (cloud base height, rain intensity, wind) giving sheets of falling rain at distance; no particles.
- Roof occlusion: with the new rain on, `PrecipitationOccluder` is created without the user changing `[Shaders] weather particle occlusion` (decision at W1 by measurement, option C9).
- Snow: same layering and the true wind vector; flakes tumble (a sine sway from the seed), no splashes.
Acceptance: [ ] pure `rainmath` tests (layer split, slant, splash selection deterministic, capacity bound); [ ] with all W1 settings off, frames and the pipeline set are identical to the build before (diffmap 0, shader trace equal); [ ] frames: heavy rain at noon, dusk, night with a torch, under a roof edge and in a doorway (no drops inside), in wind; [ ] cost per setting and tier recorded, within the W6 budget; [ ] rain is not ghosted under `taa` (streak contrast measured as in task 0075).
Out of scope: GPU rain (option C7), new rain textures from game assets.

### W2 Storm clouds (size M, risk medium-high: touches the heaviest compute pass)
Settings: `storm clouds` (bool), `storm clouds anvil` (bool), `storm clouds strength` (0 to 1). Needs `volumetric clouds`.
Storminess from W0 lowers the layer base (vanilla 1500 m toward about 800 m, chosen by frames), raises the top (anvil: widen the density near the ceiling), darkens the underside (more absorption), and adds a large-scale cell modulation so storms have dark cores and gaps; coverage is blended with the transition factor (`blendCoverage`) and hysteresis so a `ChangeWeather` does not flicker. The volume share rule (`volumeShareTarget`) is extended so a thunderstorm and rain use the volume; ash, blight and blizzard stay vanilla until W5. A rain curtain from W1 is anchored to the new base height. Cirrus stays off in storms (0079).
Acceptance: [ ] tests: storm modulation is the identity at storminess 0 (bit equal to the 0042 maths), monotone, finite, base height never above the vanilla one; [ ] settings off: frames identical, cloud compute pass byte-identical (shader trace); [ ] frames: fair, overcast, rain, thunderstorm, transition sequence (10 frames across a `ChangeWeather`), dusk and night storm; [ ] GPU cost low/medium/high measured; extra cost in fair weather is 0.
Out of scope: a second cloud layer.

### W3 Lightning (size L, risk high: new draw, new light term, flash safety, optional sound)
Settings: `lightning clouds` (bool: glow inside the volume), `lightning bolts` (bool), `lightning bolt detail` (low/medium/high), `lightning thunder delay` (bool, off). Needs `lightning` (0040), `hdr`; clouds glow needs `volumetric clouds`.
- Strike place: from (seed, strike index, distance index) a position in the sky: azimuth uniform, distance by index (0 near, 3 far; ranges chosen at the stage), height inside the cloud layer. The 0040 tilted light direction uses this position so surfaces and puddles are lit from the right side.
- Clouds glow: an in-scatter point source in the existing march (`cloud.slang`) with falloff in metres and the 0040 envelope (double flicker), scaled by the cap; branch-free in fair weather (uniform strength 0).
- Bolt: seeded midpoint-displacement polyline with branches generated at the strike into a preallocated buffer, drawn as a camera-facing additive ribbon in the deferred bin during the first stroke's envelope, only for strikes whose distance index and view allow it.
- Sky and fog flash: the 0040 colour term already exists; the bolt adds a local sky brightening along its path.
- Thunder delay (optional, needs toddhow's sound decision, open question 6): distance divided by a named speed of sound on game time; vanilla instant play stays the default.
Acceptance: [ ] pure tests: bolt generator deterministic, bounded vertex count, no NaN, never below ground, branches within the box; the peak of all flash terms never exceeds 1 and no two flash starts closer than 1.5 s (dense-storm simulation, one-second window has at most two peaks); [ ] the vanilla PRNG sequence and thunder sound calls are unchanged with all lightning settings on; [ ] settings off: frames and shaders identical; [ ] frames: a flash sequence at ages 0.05, 0.14, 0.25, 0.6 s, a near and a far strike, glow in a thick cloud, bolt against a dark sky, wet ground and puddles, a sequence under `taa`; [ ] cost measured; [ ] a fresh reviewer's attention on photosensitivity.
Out of scope: lightning through windows, strikes that hurt (gameplay), changes to the thunder schedule or to sound assets.

### W4 Transitions, wetness and cover coupling (size S, risk low)
Settings: `weather smooth transitions` (bool). One shared easing for the new storm parameters across the weather manager's transition factor (current to next), so rain intensity, base height, storminess and wetness rise together; wetness and cover targets read the W0 intensity (so a light rain wets less than a downpour), still driven only by game time. Hysteresis on rapid `ChangeWeather` calls.
Acceptance: [ ] tests: transition curves continuous and monotone, no jump at factor 0 and 1, hysteresis bound; [ ] off: identical; [ ] a 10-frame transition sequence viewed (fair to thunderstorm and back).

### W5 Extras (size M, risk medium; each item is its own sub-stage and can be dropped)
Settings: `rainbow` (bool), `storm haze` (bool: volumetric haze for ash, blight and snow storms), `storm clouds ash` (bool).
- Rainbow: after rain (wetness above a threshold, rain intensity falling), sun elevation under about 42 degrees (physical limit; RECALLED-VERIFY the number), an analytic arc opposite the sun drawn in the sky pass; fades with wetness.
- Storm haze: the volumetric fog density follows ash/blight/snow intensity and the wind direction (streaming); needs `volumetrics`.
- Volumetric clouds for ash/blight (a red-brown tint of the volume and the existing storm share rule changed for these two only).
- Sun shafts under broken storm cloud: already possible with `volumetric clouds shadows` and `volumetrics`; this item is tuning and presets only.
Acceptance: [ ] pure tests for each condition; [ ] off: identical; [ ] frames per item (rainbow after rain at 18:30, an ash storm with haze, a blight storm).

### W6 Profile, presets, performance tuning and tiers (size M, risk low)
A `weather look` preset (`vanilla`, `enhanced`, `dramatic`) selecting the individual settings (like `creativemotion preset`), tier defaults tied to `volumetric clouds quality` and the hardware tier (task 0066 `Lod::chooseFarTier`), counters (`weather_*` in the frame counters), a storm benchmark scene, the stress run on the minimal tier (Mesa llvmpipe as in the weak-GPU runs), `tools/testprofile/profile.cfg` lines, README lines, `coverage.sh` clean, docs of every setting in `files/settings-default.cfg` with comments.
Acceptance: [ ] budgets met (open question 4) on the three tiers or the heavy items off on lower tiers; [ ] a profile start shows every new feature log line; [ ] the 32 Lua tests and Linux gate pass.

## Acceptance criteria (overall)
- [ ] Each stage's list above is checked by a test or a written, repeatable in-game check.
- [ ] With every new setting at its default, Morrowind plays as before: frame diffmaps in fair, rain, thunderstorm, ash and snow weather (0 pixels differ except moving actors and water), the vanilla thunder/PRNG sequence unchanged, the pipeline set unchanged.
- [ ] No weather record, script, region logic or save format touched (a save written before and after loads in the other direction; checked by a byte comparison of a save made in rain).
- [ ] Linux gate (`-Werror`), clang-format 14, clang-tidy, Lua tests; a fresh reviewing agent per stage.
- [ ] Docs: `components/render/CLAUDE.md`, `files/shaders/CLAUDE.md`, the settings file, the testprofile README, and a changelog row per stage.

Out of scope: new weather types or a weather simulation (regional climate), changing which weather occurs, gameplay effects of weather, sound assets, GPU particle rewrite of rain (option C7), second volumetric layer, lightning through windows, mod-specific compatibility patches, the editor.

## Open questions for toddhow (numbered; my recommendation first)
1. **How dramatic by default?** Recommend: vanilla-faithful defaults (everything off), a `weather look` preset (`vanilla`/`enhanced`/`dramatic`) and `enhanced` as the proposed preset for the living test profile only.
2. **Which extras first?** Recommend order W0, W2 (storm clouds), W3 (lightning), W1 (rain), then W4 to W6; rainbow and storm haze last. Say if rain should come before the lightning.
3. **Lightning and gameplay.** Recommend: flashes and bolts are cosmetic only (no damage, no effect on scripts, AI or sound unless question 6 says otherwise). Confirm that nothing gameplay-relevant (visibility to NPCs, stealth) should depend on the flash.
4. **Performance budgets and tiers.** Proposed: at most about 0.3 ms GPU extra in the worst storm at medium tier, 1080p, RTX 4070 class, nothing in fair weather; heavy items (cloud glow, curtains, splashes) off on the minimal and low tiers. Give your target machine and number.
5. **Photosensitivity.** Keep the 0040 cap and 1.5 s spacing as hard limits (recommended) or allow a `lightning strength` above the cap for those who want it?
6. **Sound.** Allow a thunder delay by distance (off by default) and, later, rain-on-roof muffling? Needs a sound-manager change (RECALLED-VERIFY whether a deferred play exists). Or leave sound completely alone.
7. **Mods.** Is compatibility with existing weather mods (replacers of rain/ash NIFs, `Weather_*` fallback edits, new weather types) required in stage one, or best effort? Recommended: required for records and NIF names (we read the same values); best effort for visual replacers.
8. **Rain texture/art.** OK to generate splash and mist textures procedurally (recommended), or will you provide art under a compatible licence?
9. **Storms in ash, blight and blizzard.** Keep them vanilla-sky (today's D13 rule) until W5, or move them to volumetric clouds earlier?
10. **Rain on the world-map view and LOD.** Should the haze/aerial perspective (task 0066) also thicken in rain (the existing `haze weather gain` does for some weathers), or stay as is?
11. **Presets and settings UI.** One `weather look` preset plus individual settings in `settings.cfg` only, or also a launcher page?
12. **Order against other work.** Start after Track V's open items, or alongside game-pack stage d? (CLAUDE.md "Next" order applies.)

## Answers from toddhow (2026-11-09)

1. **Drama: "enhanced".** The `enhanced` preset is the look he wants. Per the guide's rule that every new feature is off by default and Morrowind plays unchanged, the shipped default of `weather look` stays `vanilla`, and `enhanced` is the preset of the living test profile and the recommended setting. *Reading to confirm:* if he meant `enhanced` as the shipped default, that is a deliberate change to the guide's default rule and needs his explicit word.
2. **Extras order: the recommended one** (W0, W2 storm clouds, W3 lightning, W1 rain, then W4 to W6; rainbow and storm haze last).
3. **Sound may be added, but it must not affect anything else** (no gameplay, AI, script, stealth or save effect; cosmetic only).
4. **Budget: about 0.3 ms GPU extra in the worst storm** at medium tier, 1080p, RTX 4070 class; nothing in fair weather.
5. **Photosensitivity: hard limits** (task 0040's cap and 1.5 s spacing stay hard).
6. **Sound: yes** (thunder delay by distance, off by default; later rain-on-roof muffling), under rule 3.
7. **Mods: as recommended** (required for records and NIF names; best effort for visual replacers).
8. **Art: generate splash and mist textures procedurally.**
9. **Ash, blight and blizzard: "yes"**, read as move them to volumetric clouds EARLIER than W5. *Reading to confirm if it matters.*
10. **World-map haze: thicken in rain.**
11. **Settings: "yes"**, read as the preset plus individual settings in `settings.cfg` (the launcher page stays a later option). *Reading to confirm if it matters.*
12. **Order: put it LAST** (after the other queued work, including the puddle plan's early stages unless he says otherwise).

## Stage log
(empty: not started)

**Confirmed by toddhow (2026-11-09): "yes thats what i meant"** for the readings of answers 1 (preset `enhanced`, shipped default stays `vanilla`), 9 (ash, blight and blizzard move to volumetric clouds earlier) and 11 (preset plus settings.cfg, launcher page later). With his blanket approval and these answers the spec is APPROVED; it is last in the queue (answer 12).
