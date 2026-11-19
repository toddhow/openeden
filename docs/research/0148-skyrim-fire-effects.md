# Research 0148: Skyrim SE fire and effect models (task 0359)

Aggregates from the five masters' mesh archives (22,091 meshes), `SkyrimSeFireSurvey` in
`apps/components_tests/skyrimse/testskyrimsefiresurvey.cpp` (gated by `OPENEDEN_SKYRIMSE_DATA`; prints no paths).
Model classes are by path substring (effects, magic, fire/flame/hearth/brazier/forge, torch/candle/lantern/sconce, smoke).

## What the models hold

| class | files | with BSEffectShaderProperty | effect shapes | palette (greyscale to colour) | falloff cone | soft depth | lighting-shader shapes that refract |
| --- | --- | --- | --- | --- | --- | --- | --- |
| fire props | 227 | 69 | 150 | 92 | 150 | 149 | 21 (17 files) |
| torch/candle/lantern | 123 | 35 | 105 | 37 | 105 | 105 | 15 |
| smoke | 58 | 45 | 71 | 16 | 71 | 71 | 9 |
| effects folder | 651 | 580 | 1,280 | 434 | 1,280 | 1,279 | 85 (82 files) |
| magic folder | 294 | 283 | 857 | 584 | 857 | 857 | 93 |
| other | 20,738 | 1,882 | 3,958 | 916 | 3,958 | 3,946 | 160 |

- **No effect-shader shape refracts.** Heat haze (flags 1 bits 15 and 16) is on BSLightingShaderProperty shapes only; the
  flattener already leaves every refracting shape out (`flatten.cpp`, `refracts`).
- Particles: fire-prop files hold NiParticleSystem 73, BSStripParticleSystem 0, NiPSys* emitters/modifiers in 73,
  BSPSysSimpleColorModifier 73, BSPSysSubTexModifier 45 (flipbook atlases); candle class 6; smoke 52; everywhere else 658
  NiParticleSystem and 147 BSStripParticleSystem files. **A `Nif::Scene` holds no particle system, so none is drawn.** The
  hearth's big flames are particles.
- Controllers: BSEffectShaderPropertyFloatController in 88 fire-prop files (variables seen: 5 alpha 35 blocks, 6 U offset 59,
  8 V offset 80, 0 emissive 32, 22 lighting-shader 16), BSEffectShaderPropertyColorController 54, NiControllerManager/Sequence
  50, NiVisController 19. Across all meshes variable 5 (alpha) 3,000+, 8 (V offset) 2,400+, 6 (U offset) 1,600+, 0 (emissive) 1,000+.
  All were seen as own-clock controllers.
- Example (Bannered Mare kitchen fire, `fireplacewood01burning`): two flame cards with a flame flipbook atlas
  (`FXFireAtlas04`: 128 x 1024, eight frames), alpha blend, vertex alpha 0..0.8, a V offset controller 0..1 over 4.3 and 5.7 s
  (loop), palette `GradFireExplosion` (white-hot at high values, orange to brown at mid); one more card with an emissive/colour
  controller; the logs carry glow shapes. The central hearth (`campfire01burning`) is glow shapes plus three particle systems.

## What OpenEden drew before task 0359

`skyrimmodel.cpp` (`makeSceneReader`): `limits.mEffects` only for `meshes/magic/` behind `[Game] neutral spell effects` (task 0191);
every other effect shape was left out (`flatten.cpp`, `mSkippedEffect`); no controller was played; the palette is one texel.

## Stage a (this task, built)

Effect shapes of world models draw (`[Game] neutral fire effects`, default true, neutral sessions only) and their U/V offset and
scale controllers play: `NifScene::ShaderMaterial::mUvAnimation` (read in `build.cpp`), raw UVs kept by `flatten`, a texture matrix
per shape written once a frame by `MWRender::SkyrimEffectScroll::Driver` (`TEXMAT` pipeline mode, no shader change). Palette effects
ignore the dim vertex RGB and double the vertex alpha (by eye). Seen: the kitchen fire's flame card scrolls through its frames. It
is faint: the flame is mostly particles.

## Ranked next stages

1. **Particles** (`NiParticleSystem` and `BSStripParticleSystem` with emitters, `BSPSysSimpleColorModifier`, `BSPSysSubTexModifier`
   flipbooks): 73 fire-prop and 658 other files; the main visible gain for hearths and braziers. Needs a scene representation.
2. **Alpha and emissive controllers** (variables 5 and 0, colour controller): the most common controllers; flicker and fade.
3. Real **palette lookup** (bake the lookup into the source texture, row from alpha) instead of one hue.
4. **Light sync**: the fire's light (task 0355) flickers from the LIGH record; tie to the emissive controller.
5. NiControllerSequence-managed effect controllers and NiVisController (19 fire files).
6. Lighting-shader refraction (heat haze): 21 fire-prop shapes; needs a refraction pass.
