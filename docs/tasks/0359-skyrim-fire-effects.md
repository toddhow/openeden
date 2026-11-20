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

Not done after stage a: particles, alpha/emissive/colour controllers, real palette, light sync, refraction (see the research note).

## Stage b: analytic particles (branch `particles-0359b`, local commits, not pushed)

Goal: draw the NiParticleSystems of Skyrim SE static models (hearth and campfire flames, smoke, sparks).

**Why not Morrowind's compute simulation (decision for toddhow).** The Skyrim scene reader returns one VSG node that every
placement of a model shares. Morrowind's particles are simulated by a compute dispatch inside the model's subgraph, state per
particle in a buffer, driven by `Anim::` controllers that need the NIF's record paths (`LinkParticleSystem`). Shared, that would
dispatch once per visible placement (the fire would run N times too fast) and every placement would share one buffer; the
flattened model has no `Anim::Transform` paths to link. So a particle here is a pure function of (scene time, slot): slot i of M
restarts every `period` seconds, staggered, and all of one life (start, velocity, size, spin, flipbook frame) is drawn from a
hash of (system seed, slot, cycle), so there is no state, no compute pass, no CPU per frame, and any number of placements cost
the same. Everything is in `components/nifscene/particleanalytic.*` (the layout and `evaluate`) and its twins in the vertex
shader (`lib/particle/analytic.glsl` and `.slang`). Cost: every placement of a model shows the same flame at the same moment.

What changed: `Nif::Scene::mParticleSystems` (read in `build.cpp`: first emitter of box, cylinder, sphere kind, mesh emitters
start at a point; birth rate from the system's `NiPSysEmitterCtlr` interpolator, mean of its keys; scale, simple-colour,
sub-texture, gravity (planar), drag and rotation modifiers; `BSStripParticleSystem`, mesh particle systems and master-driven
systems are not read); `Flat::Limits::mParticles` and `FlatModel::mParticles` (placement baked: emitter centre, rotation, scales);
`SkyrimModel::buildParticleSystem` (one draw per system, the `PARTICLE` storage buffer holds 5 header entries and one entry per
slot, draw starts at the first slot, depth sorted and blended as the alpha property says); a palette effect has its
greyscale-to-palette lookup baked into a copy of the atlas (row 0.55 of the gradient, by eye) and takes no base colour on top;
`Pipeline::analyticParticleColorMode` 5 (unlit, the particle's own colour and alpha times the texture, fogged: `main.frag`,
`terms.glsl`, `terms.slang`, `default.frag.slang`); the vertex stage branch on `velocityAge.w < -0.5` (Morrowind particles never
have a negative age, so their path is untouched). New setting `[Game] neutral fire particles` (default true, needs `neutral fire
effects`; test profile and README), developer aids `OPENEDEN_PARTICLE_DEBUG=1|2|3|4|5|6|7`, trace lines `NIFSCENE particles` and
`NIFSCENE palette bake` under `OPENEDEN_NIFSCENE_TRACE`.

Survey (`SkyrimSeParticleSurvey`, gated, aggregates): fire props 84 systems in 35 files (29 box, 44 cylinder, 8 sphere, 3 mesh
emitters; scale modifier 84, simple colour 84, sub-texture 52, drag 138, gravity 64, rotation 57), smoke 37, light props 10,
everything else 689 in 380 files. All have effect shaders. Tuning numbers that are ours (RECALLED-VERIFY, by eye): drag
percentage times 30 a second, emitter axis Z with declination from it, particle radius is the quad half side, the flame alpha gain
3, palette row 0.55, cylinder emitter centred on its height.

Tests (new files only): `NifSceneParticleAnalytic` 21 (19 red against a stub first, the 2 that passed pin absence), `NifSceneFlattenParticles` 8
(written after the code; one failed on first run, a dangling reference of the test itself), gated `SkyrimSeParticleSurvey`.
`default/main.vert.interface-exceptions` lost its two `binding=0` exception lines (the scene uniform is read by the PARTICLE sets
now); no recorded `.interface` file changed (the vertex stage's declared interface is the same).

Not done: spell models under `meshes/magic` (they move: they would need the world-space path), strip particle systems (103 files),
mesh emitters as meshes, `NiPSysModifierActiveCtlr`, bomb and collider modifiers, the soft-particle depth fade, a per-placement phase
(all placements in step), light sync with the fire, real palette lookup along the alpha, `Slang` permutations for the new
sets (the existing PARTICLE sets are listed and build; no new define was added).
