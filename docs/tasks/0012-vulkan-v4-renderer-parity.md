# Task 0012: Track V stage V4 — Renderer parity

**Status:** approved by toddhow (2026-10-29) with the answers below. Depends on task 0011 (V3).
**Design doc section:** `docs/decisions/0002-vulkan-renderer-on-vulkanscenegraph.md` ("Mods", "The OpenGL build is not kept alongside"); `docs/research/0001-vulkan-renderer.md`, "vsgopenmw" (gaps).
**Goal:** close every gap between the Vulkan renderer and the OSG renderer OpenEden had at `442eec6e2f`, so nothing a player or modder relied on is silently lost: vsgopenmw's own parity items, the features vsgopenmw dropped from the build, the OpenMW 0.49→0.51 renderer changes deferred in task 0009, and every renderer setting.
**Workflow:** full loop. Large: split into sub-stages (V4a…) on the same branch, each with its own tests and review, as toddhow prefers.

---

## Context (facts this spec relies on)

- **vsgopenmw's open parity items** (`vsgopenmw-roadmap.doc`, "vsgopenmw-feature-parity"): weather particles, water ripples, paged instanced objects (object paging), normal maps, alpha to coverage, `NiLODNode`, NIF internal textures, NIF built-in switch nodes. (The physics and navmesh debug draws on the same list are done in task 0011.)
- **Dropped from vsgopenmw's build** (task 0009, "What vsgopenmw replaced"): post-processing (`components/fx`, `apps/openmw/mwrender/postprocessor*`, `pingpong*`, `luminancecalculator`, `transparentpass`, the post-processing HUD in `mwgui`, Lua `openmw.postprocessing`), VR (`components/stereo`, settings `[Stereo]` and `[Stereo View]`), groundcover (`apps/openmw/mwworld/groundcoverstore`, `mwrender/groundcover`, `[Groundcover]`), OpenMW's `esmterrain` (vsgopenmw has `esm3terrain`; ESM4 terrain must keep working, since OpenEden keeps ESM4, decision 0002).
- **Built-in post-processing shaders:** `files/data/shaders/adjustments.omwfx`, `bloomlinear.omwfx`, `debug.omwfx`, `internal_distortion.omwfx`, in OpenMW's `.omwfx` format (OpenGL-style GLSL inside a technique description; parser in `components/fx`). Modders' shaders use the same format.
- **Renderer settings in `files/settings-default.cfg`** (count of keys per section): `[Camera]` 7, `[Terrain]` 14, `[Fog]` 12, `[Map]` 7, `[Shaders]` 25, `[Water]` 9, `[Shadows]` 20, `[Models]` 24, `[Groundcover]` 5, `[Stereo]` 6, `[Stereo View]` 24, `[Post Processing]` 3, `[Video]` 11. The launcher's graphics pages (`apps/launcher`) expose many of them.
- **Deferred OpenMW 0.49→0.51 renderer changes:** the list recorded in `docs/research/0003-vsg-import-merge.md` by task 0009.
- **vsgopenmw's own improvements** (clustered shading, soft shadows, screen-space water reflections, GPU skinning and particles) stay; parity means nothing is lost, not that the old look is restored (unless toddhow decides otherwise in task 0010's open question 1).

## Work

1. **vsgopenmw parity items:** implement each item listed above on VSG, with an image scene in `tools/render_scenes.txt` that shows it (rain and snow, water ripples around the player, a distant town with object paging, a normal-mapped mod asset, alpha-tested foliage, a NIF with LOD and switch nodes).
2. **Groundcover:** port to VSG, likely as instanced geometry in object paging; `[Groundcover]` settings work.
3. **ESM4 terrain:** confirm ESM4 worldspaces still render (there is ESM4 code in OpenEden's tree); if `esmterrain` is needed beyond vsgopenmw's `esm3terrain`, port it.
4. **Post-processing**, per open question 1: implement the chosen approach, including the Lua API `openmw.postprocessing` and the in-game HUD, or remove them with a migration note for modders.
5. **VR**, per open question 2.
6. **Deferred 0.49→0.51 changes:** implement each one listed in research note 0003, or drop it with toddhow's agreement and a reason.
7. **Settings audit:** every key in the sections above either works on the Vulkan renderer (with a scene or test that shows it), is replaced by a new key (with migration from the old one in the settings parser), or is removed (with a reason, and removed from the launcher). The launcher's graphics pages and translations follow (`CI/check_qt_translations.sh` must pass).
8. **Performance:** object paging and groundcover change costs most; re-measure every scene against task 0008's baseline and task 0010's numbers.

## Constraints

- Every new or changed renderer setting is documented in `files/settings-default.cfg` and `docs/source` (OpenMW's settings documentation lives there).
- No game assets: parity scenes needing Morrowind data are run locally; test assets for mod features (normal maps, NIF node types) are generated or permissively licensed with the licence recorded.
- OpenEden's features from task 0011 keep working; off still means unchanged for them.

## Acceptance criteria

- [ ] Each vsgopenmw parity item works and has a scene in `tools/render_scenes.txt` with toddhow's verdict recorded.
- [ ] Groundcover works with its settings.
- [ ] ESM4 worldspaces render (checked with whatever ESM4 content toddhow has, or recorded as untestable).
- [ ] Post-processing and VR are implemented or removed per the decisions below, with Lua, HUD, settings and documentation consistent.
- [ ] Every deferred 0.49→0.51 renderer change is done or dropped with a reason.
- [ ] Every key in the listed settings sections is accounted for in a table in `docs/research/0005-vulkan-parity.md` (works / replaced with migration / removed with reason); the settings parser migrates replaced keys; the launcher and its translations match.
- [ ] All gates pass, including `CI/check_qt_translations.sh` if launcher strings changed.
- [ ] Performance per scene is within task 0010's agreed budget.

## Out of scope

- New renderer features beyond parity (PBR, ray tracing): after Track V (guide, "Not yet").
- OSG math (task 0013); the editor (task 0014).

## Open questions for toddhow

1. **Post-processing for modders** (`.omwfx`): (a) port OpenMW's post-processing framework to VSG and translate `.omwfx` GLSL to Vulkan GLSL at load time, so existing mod shaders keep working; (b) a new Vulkan-native format with a converter for the four built-in shaders and a migration guide; or (c) drop post-processing for now and bring a new system later.
2. **VR (`components/stereo`):** port to Vulkan (via OpenXR or multiview on VSG), or remove it and its 30 settings for now?
3. **Order of sub-stages:** proposed: weather particles, ripples and object paging first (most visible), then groundcover, normal maps, NIF features, then post-processing and VR, then the settings audit.

### Answers (toddhow, 2026-10-29)

1. **Post-processing:** OpenEden moves to Slang for shaders. Existing mod shaders (`.omwfx`) keep working through Slang's GLSL input path; the built-in shaders are later ported to native Slang or replaced by the shaders that come with OpenEden's advanced graphics features (subsurface scattering, lighting improvements and similar). Background: research note 0005 (Slang spike).
2. **VR:** removed for now, with its settings, each with a recorded reason; a Vulkan VR port is a later task.
3. **Order:** as proposed. Distortion (moved from task 0011) joins the NIF features sub-stage.

## Progress (2026-10-29)

- **VR removed** (answer 2). The `[Stereo]` and `[Stereo View]` sections (30 keys) are gone from `files/settings-default.cfg`, from `components/settings` (`categories/stereo*`, `Settings::stereo()` and `stereoView()`) and from `docs/source`, and the dead `components/stereo` code is deleted.
  - **Reason:** the Vulkan renderer has no stereo path, and a Vulkan VR port (OpenXR or multiview on VSG) is a later task.
  - Old user `settings.cfg` files that still have those sections start normally; the keys are ignored.
  - The launcher had no VR controls, so it needs no change. The only remaining users are OSG-era files that are no longer compiled (`apps/openmw/engine.cpp` and others), which task 0014 deletes.
- **Weather particles (V4a):**
  - `MWRender::WeatherEffects` loads the weather's NIF effect (`[Models] weatherashcloud`, `weatherblightcloud`, `weathersnow`, `weatherblizzard`) as a looping vsgopenmw effect. It is drawn in the main scene, so walls and roofs hide it, at the camera, turned to the storm direction (with the blizzard special case), and hidden underwater.
  - Checked by forcing each effect in Seyda Neen: snow, ash cloud and blizzard draw with their textures.
  - The texture failure first seen here was a general bug, now fixed: the texture lookup did not pass `.dds` to OpenMW 0.51's `correctResourcePath`, so any model naming a `.tga` that only exists as `.dds` showed the fallback texture.
  - **Gaps:**
    - Rain: done separately (below).
    - The wrap-around that kept non-storm particles in a box around the camera (`WrapAroundOperator`): done for snow (V4c below).
    - The precipitation alpha fade during weather transitions: done (V4e below).
    - `[Shaders] weather particle occlusion`.
    - The NIF particle systems simulate in world space, so already-emitted particles stay behind when the camera moves fast or teleports, until new emission catches up.
    - Effects use `Mask_Effect`; OpenMW had a separate `Mask_WeatherParticles`.
  - Review (fresh agent): no blocking bugs. Fixed before committing: a per-frame string copy, and the storm orientation now keeps its last angle after a storm, as OpenMW's `SkyManager::update` did.
- **NIF LOD nodes** (`vsgopenmw-nif-nilodnode`):
  - `vsgUtil::DistanceLOD` selects children by eye-space distance to the NiLODNode centre, using the node's min/max ranges, as OpenMW's `osg::LOD` in `DISTANCE_FROM_EYE_POINT` mode. Before this, vsgopenmw drew every level at once.
  - `vsg::LOD` can't express NIF ranges: it selects by projected size and draws only the first match.
  - Cloning keeps the node type and its ranges.
  - Not yet applied: the LOD scale OpenMW took from the cull visitor.
  - Tests: `vsgutil/testdistancelod.cpp`.
- **Built-in switch nodes** (`vsgopenmw-nif-builtin-switch-nodes`):
  - `NightDaySwitch` shows child 0 by day and 1 at night, following `World::getNightDayMode`, with `[Game] day night switches`.
  - `HerbalismSwitch` shows child 1 once an organic container is empty, with `[Game] graphic herbalism`. That happens when the container is placed and when it is harvested; `Container::canBeHarvested` works again.
  - Harvestable plants get their own copy of the switch, since vsgopenmw shares static models between instances (`MWRender::makeSwitchesUnique`).
  - Vanilla Morrowind has no such nodes (they come from mods such as Graphic Herbalism), so they're covered by unit tests only (`mwrender/testswitches.cpp`).
- **NIF internal textures** (`vsgopenmw-nif-internaltexture`):
  - `vsgAdapters::readInternalTexture` converts NiPixelData: RGB and BGR gain alpha, RGBA and BGRA are kept, 8-bit palettes expand, and DXT1/3/5 become BC1/2/3.
  - Known difference from OpenMW: only the first mip level is kept, and the renderer generates the rest.
  - Tests: `vsgadapters/testinternaltexture.cpp`.
- **Alpha to coverage** (`vsgopenmw-alpha-to-coverage`): not started. It needs MSAA and shader support for `[Shaders] antialias alpha test`, and both are off by default.
- **Review of the batch above (fresh agent, 2026-10-29):**
  - Fixed: every switch with a built-in name is handled, not just the first. Night/day switch registrations are moved or removed one per object when objects move or are removed. LOD distance is in the node's own units (divided by the scale above it, as `osg::CullVisitor::getDistanceToViewPoint`). Empty NiLODNode children are skipped, as OpenMW did. DXT internal textures keep every NIF mipmap level VSG can lay out.
  - Left as they are:
    - A night/day switch is set to the current mode when its object is placed, where OpenMW left the NIF's initial index until the mode first changed.
    - `Container::canBeHarvested` searches the object's graph on each call.
    - The warning for multi-face internal textures is not restored.
- **Settings audit** (work item 7): `docs/research/0006-vulkan-renderer-settings.md`. Of 143 keys, 86 are read by the Vulkan build and 57 are not. Each unread key has a proposed disposition: implement, remove with a reason, or decide with toddhow. Nothing has been removed yet. `[Models] load unsupported nif files` works again.
- **Rain** (V4a):
  - `MWRender::RainSimulation` (renderer-free) reproduces OpenMW's procedural rain: emission rate, box placer, wind-tilted velocity, one-second life and wrap-around around the camera.
  - Drop sizes grow from 5 to 15 over their life and each quad spans twice its size, as osgParticle drew them (OSG 3.6.5 `ParticleSystem::drawImplementation`).
  - `MWRender::Rain` draws each drop as a textured streak (`tx_raindrop_01`), lit by the ambient light and the sun and fogged (colour mode 2 with `NifOsg::ParticleSystem`'s constant normal), at 0.6 times the precipitation alpha (`WeatherAlphaOperator`), and hidden underwater. Point lights still reach it; OpenMW's `simpleLighting` excluded them.
  - Without the raindrop texture, rain is skipped with a warning; drops beyond 4096 alive are dropped and logged once.
  - `Sky::hasRain` works again, and `getRainIntensity` follows OpenMW 0.51's `getRainRipplesEnabled`: the water shader's rain ripples show for rain (`Weather_Rain_Ripples`) and for the snow effect (`Weather_Snow_Ripples`, off in Morrowind's values), at the precipitation alpha.
  - Checked in Seyda Neen with Morrowind's rain values: streaks fall over the scene.
  - Found on the way: an OR of two comparisons of the specialization constant `colorMode` made every frame render nothing on this NVIDIA driver (pitfall in `apps/openmw/mwrender/CLAUDE.md`).
  - Not ported: drops keep their place relative to the camera, not the world, while hidden underwater (invisible in practice: one-second life and wrap-around).
- **Water ripples** (V4b):
  - `MWRender::RippleParticles` (renderer-free) and `updateRippleEmitter` reproduce OpenMW's particle `RippleSimulation`, the path the fork point used with its default `[Water] shader = false`. Actors standing or swimming in water (not submerged), or walking on it, leave a ripple every 1.5 s or every 10 units moved, and projectiles hitting the water leave one. Ripples grow from 15 to 180, fade from 0.7 to 0 and turn over `Water_RippleLifetime`, at most `Water_MaxNumberRipples` + 1 at once.
  - `MWRender::WaterRipples` draws them lit at the water height and flips through the `Water_RippleTexture` frames every 0.3 s. Missing frames skip ripples with a warning.
  - Checked in the sea off Seyda Neen against the OSG build: rings of the same size around the swimming player.
  - Not ported: the shader water's ripple map (`RipplesSurface`, a GPU simulation used only with `[Water] shader = true`); vsgopenmw's water shader keeps its own procedural rain ripples.
- **Bloodmoon land textures** (V4d): every `Tx_BM_*` terrain layer (snow, grass, rock) drew flat grey on VSG. They match Morrowind's layers in size and format (256x256 BC1) but carry a full mip chain down to 1x1, where Morrowind's stop at 8x8, and `vsgUtil::convertToArrayTexture` chose the array by size, format and mip count, so they fell through to its grey fill. It now chooses by size and format only and keeps as many levels as the shortest chain among those layers, which each copy from their own chain, so neither Morrowind's nor full-chain textures go grey whichever kind is the majority (a review pointed out that a load order dominated by full-chain textures would otherwise grey out Morrowind's). Each copied level's size is checked against the layer's data. Checked at Skaal Village against the OSG build. Tests: `vsgutil/testarraytexture.cpp`.
- **Weather effect fade** (V4e): ash, blight, snow and blizzard fade in and out with the precipitation alpha during weather transitions, as OpenMW's `AlphaFader` did: every material of the effect gets diffuse (0, 0, 0, alpha) every frame. The NIF loader shares material uniforms between models, so `MWAnim::cloneWithOwnMaterials` gives the effect its own dynamic copies before it is compiled, through a data filter on `Anim::cloneIfRequired`: the materials keep their controllers, and the rest of each descriptor set (such as the particle buffer the compute pass simulates) is cloned as usual. A first version replaced whole descriptor sets, which the fresh review caught: the draw would have read the cached, never-simulated particle buffer. The fade is written every frame after the effect's controllers run, as OpenMW's AlphaFader ran after them. Checked with the ash effect forced in Seyda Neen at alpha 1 and 0.3. Not ported: `WeatherAlphaOperator`, which also set each particle's alpha; VSG's NIF particles take their alpha from the material when the NIF has no vertex colour property, which holds for all four vanilla weather meshes (`AshCloud`, `BlightCloud`, `Snow`, `Blizzard`). Tests: `mwanimation/testclonematerials.cpp`.
- **Groundcover** (V4f; work item 2). Design, following OpenMW's `MWRender::Groundcover` and `groundcover.vert` at 442eec6e2f:
  - **Instances** (renderer-free, unit-tested): per chunk of cells, read the groundcover content files' references through `MWWorld::GroundcoverStore`, thin them with OpenMW's `DensityCalculator` (`[Groundcover] density`), keep those inside the chunk (`isInChunkBorders`), and group them by model.
  - **Drawing:** a new `Pipeline::Mode::INSTANCE` adds two per-instance vertex inputs, offset and scale (`aOffset`) and rotation (`aRotation`); `default/main.vert` places each instance with them and applies OpenMW's wind and stomp displacement (`[Groundcover] stomp mode`, `stomp intensity`, the weather's wind speed and the player position). Groundcover models are read through a `Pipeline::Override` that adds the mode and forces OpenMW's unified alpha (alpha test at 128/255, no blending); each chunk clones the model's draws, appends its instance arrays and sets the instance count.
  - **Chunks:** square chunks of cells around the camera up to `[Groundcover] rendering distance`, built on a worker thread and cached, culled by their bounds.
  - **Done (step 1):** `GroundcoverStore` builds again and `World` loads groundcover files when `[Groundcover] enabled` (off by default); `MWRender::collectGroundcoverInstances` with tests (`mwrender/testgroundcoverinstances.cpp`).
  - **Done (step 2):** `--groundcover` files reach `World` again (`OMW::Arguments::groundcoverFiles`); `Pipeline::Mode::INSTANCE` with its per-instance inputs and `lib/groundcover.glsl` (wind, stomping, rotation); `MWRender::Groundcover` draws one chunk per cell within `[Groundcover] rendering distance`, using `density`, `stomp mode` and `stomp intensity`. RenderManager creates it on the main thread when the World hands over its store and `[Groundcover] enabled` is on, and hides it in interiors. Checked with a temporary layout of 441 vanilla `flora_bc_grass_01` instances around the player in Seyda Neen: placed, rotated, alpha-tested and lit. No groundcover mod is installed here, so real groundcover content is untested.
  - Review (fresh agent) found and fixed before committing: the scene data read the player at the main menu, where none exists (a crash on a normal launch whatever the setting; now taken in update(), which runs only in game; checked by sitting 60 s at the menu); LOD, billboard and matrix-transform nodes now keep their type in the chunk copy; particle draws are left out of chunks (their pipelines get no instance mode); groundcover reads models through its own shared-object cache, since every `Pipeline::Override` compares equal and the first-person or preview models could otherwise be swapped; other worldspaces get no Morrowind groundcover.
  - **Open:** chunks are built on the main thread when they come into range, and dropped as soon as they leave it (a hitch with large mods; OpenMW built them on its work queue and cached them); OpenMW's terrain-quadtree chunk sizes and per-instance near-plane computation are not ported; animated groundcover models do not animate; `[Groundcover]` settings are read when it is created; the loading screen shows no progress while groundcover files load.
- **NIF particles and snow** (V4c):
  - Snow wraps around the camera: its particles stay fixed in the world and are kept in a 1024 x 1024 x 800 box centred on the camera, as OpenMW's `WrapAroundOperator` did. OpenMW also wrapped a NIF effect in a weather that had rain too; no Morrowind weather has both, so that case is not ported. Done in the particle compute shader (`comp/particle/simulate.comp`) with a per-frame matrix from `vsgAdapters::ParticleSystem::wrapAround`, set by `WeatherEffects`.
  - Four inherited vsgopenmw particle bugs fixed, found while comparing snow with the OSG build:
    - Quads were half OpenMW's size: osgParticle puts corners at plus or minus the size, so a particle is twice its size across (`default/main.vert`); the culling radius grows to match.
    - The emission rate was scaled by 0.01, and its carried fraction was stored in an `int`, so it was always lost. `vsgAdapters::particlesToEmit` now follows osgParticle's `ConstantRateCounter`.
    - The compute shader's `<=` emitted one particle too many per frame, so every system emitted at least one particle per frame, even when its emitter was off.
    - Particle controllers ran on the object's animation time unless the NIF node had the AutoPlay flag, so on an object with no animation of its own (a placed torch) the time never moved and its particles never simulated. They now always run on frame time, as osgParticle did, and the first update no longer counts the controller's random start phase as elapsed time.
  - Checked with the snow effect forced in Seyda Neen: the flakes now match the OSG build's in size and density (they were sparse dots).
  - **Gaps found, not fixed yet:**
    - Torch flames (`light_com_torch_01`: `NiRotatingParticles` with `tx_firealpha10`) now burn, a little dimmer than the OSG build's, with a more visible smoke column, but one placed against the tapestry in the Caldera Mages Guild shows no flame at all, where the OSG build shows it. At the same distance in the open, next to the door and over the rug it burns. The tapestries (`furn_com_tapestry_*`, `furn_de_tapestry_*`) are blended and depth-sorted in both builds (`NiAlphaProperty` flags 0x00ed, no alpha test), so sorting alone does not explain it; the whole flame vanishes, which suggests its emitter sits behind the tapestry surface. Not yet traced.
    - Snow shifts by one frame of camera movement each time the effect loops (the compute dispatch is skipped for that frame while the world offset advances); hidden by the wrap. Also affects world-space particles generally.
    - Particle size ignores the node's scale (`main.vert` normalises the view axes); OpenMW scaled particles with it (`LOCAL_COORDINATES`).
- **Defaults:** with `distant terrain = false` (the default), OpenMW at the fork point ran no object paging, and groundcover, normal maps, alpha to coverage, soft particles and post-processing are all off by default. The remaining default-on differences are rain ripples on water (with the water item) and vsgopenmw's accepted water look (`sunlight scattering`, `wobbly shores`).
