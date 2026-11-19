# Task 0359: Skyrim fire and effect models, stage a

Design: `docs/research/0148-skyrim-fire-effects.md`. Branch `fire-0359`; local commits, not pushed. Same hand wrote tests and code;
no fresh reviewer; Windows only.

Goal: draw the effect-shader shapes of Skyrim world models (hearths, braziers, torches, candles) with their UV scroll controllers.

Changes: `[Game] neutral fire effects` (default true; `files/settings-default.cfg`, test profile line and README line);
`SkyrimModel::effectShapesWanted`; `NifScene::UvAnimation`/`FloatTrack` in `scene.hpp`, read in `build.cpp`, played by
`components/nifscene/uvanim.*`; `flatten` keeps raw UVs and a `Shape::mScroll` for scrolled effect shapes (`MaterialKey::mScroll`);
`apps/openmw/mwrender/skyrimeffectscroll.*` (driver, `RenderManager::mEffectScroll`, updated after the tree sway); palette effects
of world models ignore vertex RGB and double vertex alpha (`kPaletteAlphaGain`); developer aids `OPENEDEN_NIFSCENE_EFFECT_DEBUG=1|2`,
the `fxcam` scenario of `tools/neutral_session_check`.

Tests (new files only): `NifSceneUvAnim` 14 and `NifSceneFlattenEffectScroll` 4 (12 of 18 red against stubs first; the others pin
absence and zero cases), `SkyrimFireEffects` 4 (2 red first), `SkyrimEffectScroll` 5 (written after the code), gated survey.

Not done: particles, alpha/emissive/colour controllers, real palette, light sync, refraction (see the research note).
