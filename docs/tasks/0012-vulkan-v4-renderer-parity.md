# Task 0012: Track V stage V4 — Renderer parity

**Status:** CLOSED 2026-11-10 by toddhow (remaining checks are follow-ups, not blockers; listed in `docs/toddhow-checks.md`, "Known open items from Track V"). Earlier status: approved by toddhow (2026-10-29) with the answers below. Depends on task 0011 (V3).
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
- **Alpha to coverage** (`vsgopenmw-alpha-to-coverage`; step V4j). Follows OpenMW at 442eec6e2f: `Shader::ShaderVisitor::createProgram` (when), `lib/material/alpha.glsl` (`alphaTest` with `@alphaToCoverage`, `coveragePreservingAlphaScale`) and `MWRender::shouldAddMSAAIntermediateTarget`.
  - **Settings:**
    - `[Shaders] antialias alpha test` takes effect only with `[Video] antialiasing` above 1, as `shouldAddMSAAIntermediateTarget`.
    - `[Shaders] adjust coverage for alpha test` is independent of it.
    - Both are off by default and read when the resource system is created. With both off, nothing changes.
  - **Which draws** (renderer-free, unit-tested; `Pipeline::coverageModes` in `components/pipeline/graphics.hpp`):
    - `CoverageModes coverageModes(const Options& key, bool alphaToCoverage, bool adjustCoverage)` returns `{ bool alphaToCoverage; bool adjustCoverage; }`.
    - Each is set only when its switch is on and the draw is alpha-tested (`alphaTestMode != 0`), unblended, uses the default shader (empty `shader.path`) and has the `MATERIAL` mode, which holds the alpha cutoff. This matches OpenMW's "alpha func not ALWAYS and no blending".
    - `Pipeline::Graphics` adds the matching new fragment modes, `ALPHA_TO_COVERAGE` and `ADJUST_COVERAGE`.
  - **Why from the shader:** VSG's compile traversal overrides each pipeline's multisample state with the render pass's sample count (`vsg::CompileTraversal::add`). That would drop a pipeline's `alphaToCoverageEnable`, so the default shader writes the coverage itself through `gl_SampleMask`.
  - **Shading:**
    - With `ADJUST_COVERAGE`, the diffuse and dark map alphas are scaled by `1 + mipLevel * 0.25`, with the mip level from the derivatives of `uv * textureSize`, as `coveragePreservingAlphaScale`.
    - With `ALPHA_TO_COVERAGE`, and the view's sample count above 1 (`Scene::groundcoverParams.w`, set for the main view; a field of its own brightened interiors, see the pitfall in `apps/openmw/mwrender/CLAUDE.md`), the alpha test modes `<`, `<=`, `>` and `>=` turn the alpha into coverage `(alpha - clamp(cutoff, 0.0001, 0.9999)) / max(fwidth(alpha), 0.0001) + 0.5` (inverted for `<` and `<=`). The other modes keep testing, as OpenMW did.
    - The coverage covers `round(clamp(coverage) * samples)` samples of the pixel.
    - Views with no sample count set (maps, previews, shadows) keep the plain alpha test.
  - **Known differences:**
    - Hardware alpha to coverage dithers the sample pattern. Here the first samples in order are covered, which can band at partial coverage.
    - The coverage comes from the texture alpha alone. OpenMW multiplied in the material's diffuse alpha first, so an unblended, alpha-tested material with diffuse alpha below 1 gets fuller coverage here; vsgopenmw's plain alpha test already tested before the material alpha.
    - Above 32 samples, only the first 32 can be narrowed.
  - **Review (fresh agent):**
    - Fixed before committing: every path now writes the sample mask (all samples first, then narrowed), since a mask written on some paths is undefined on the others. Those were the plain-test modes and the views without a sample count: shadows, the map and the preview.
    - Also fixed: a shift by 32 at 32 or more samples.
  - **Checked:**
    - Unit tests: `pipeline/testcoveragemodes.cpp` (12), written from this spec by a separate agent.
    - In Seyda Neen, with 4× MSAA, no alpha-tested draw qualifies: its vanilla foliage is alpha-blended, as OpenMW also excluded.
    - With a temporary change (not committed) that turned every blended NIF draw into an unblended alpha test at 0.5, the tree leaves' edges are visibly smoothed with `antialias alpha test` on, against hard steps with it off. `adjust coverage for alpha test` drew without errors.
    - Off by default: the Caldera Mages Guild frame against the previous build differs in 16 pixels (MAE 0.0000), the run-to-run noise.
- **Soft particles** (step V4k). Follows OpenMW at 442eec6e2f: `NifOsg::LoaderImpl::handleParticleSystem` (every NIF particle system, size `partctrl->mInitialSize`, no view falloff), `lib/particle/soft.glsl` (`calcSoftParticleFade`) and `objects.frag` (applied to the final alpha).
  - **Setting:** `[Shaders] soft particles`, off by default, read when the resource system is created. With it off, nothing changes.
  - **Size** (unit-tested): `vsgAdapters::handleInitialParticles` stores the controller's initial size in each particle's `maxAge.y`, which the simulation never changes.
  - **Drawing:**
    - With the setting on, the NIF reader adds a new `SOFT_PARTICLE` pipeline mode to particle systems. The vertex shader passes the particle's `maxAge.y` to the fragment shader.
    - The fragment shader multiplies the alpha by `0.845 * pow(clamp((particleZ - sceneZ) / (size * 0.33), 0, 1), 1.3)`. `particleZ` is the fragment's view-space z; `sceneZ` is the opaque scene's view-space z at the pixel, rebuilt from the depth copy that the water already uses (`reflectDepthMap`, taken after the opaque scene and before the depth-sorted bins) through `scene.invProjection`.
    - The depth copy is kept in the scene when soft particles are on, even with water rendering off.
  - **Not ported:** soft effects named by NIF extra data (`soft_effect` YAML) and Bethesda effect materials (`BGEM`), which Morrowind's NIFs don't have. The rain is not a NIF particle system and stays hard-edged, as in OpenMW.
  - **Checked:**
    - Unit tests: `vsgadapters/testsoftparticlesize.cpp` (6), written from this spec by a separate agent.
    - Off by default: the Caldera Mages Guild frame against the previous build differs in 9 pixels (MAE 0.0000).
    - With the setting on, the Caldera, Arrille's Tradehouse, Balmora Council Club, Vivec and Seyda Neen (forced ash storm) frames draw with no shader errors.
    - With a torch placed 400 units ahead in Seyda Neen, the flame and smoke draw with the setting on. A temporary debug shader (not committed) painted each particle pixel with the rebuilt scene depth: it changed across the smoke where the hillside behind it gets farther, so the depth lookup lines up with the screen. The fade's strength next to nearby geometry was not judged; it needs a look in game by a wall.
  - **Review (fresh agent):**
    - **Fixed before committing:**
      - The depth copy is a fixed 512×512 image, so it is read at `gl_FragCoord / scene.resolution`. Dividing by its own size had wrapped the lookup onto unrelated pixels.
      - The preview, map and shadow views bind a placeholder far-plane depth (`View::dummyReflectDepth`) at the copy's binding, which they had left unwritten. The fade runs only in the main view, marked by its sample count in `groundcoverParams.w`.
    - **Known gaps:**
      - The copy is a nearest-filtered 512×512 version of the depth, so the fade is coarser than OpenMW's full-resolution one.
      - With MSAA on, blitting the multisampled depth into the copy is invalid Vulkan. This was already true for the water, and now also applies with water off and soft particles on; it needs a depth resolve.
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
  - **Step 3 (V4g), chunks built on a worker and cached:**
    - `MWRender::GroundcoverChunkTracker<Chunk>` (header-only, renderer-free, unit-tested; `Cell` is `std::pair<int, int>`) decides what each cell's chunk is doing. Its constructor takes the cache capacity.
    - Every cell is in at most one of four states: unknown, pending (being built), shown, or cached (built but out of range).
    - `std::vector<Cell> setWanted(const std::vector<Cell>& wanted)`: `wanted` is sorted with no duplicates.
      - Shown cells no longer wanted move to the cache as its newest entries.
      - Wanted cells in the cache move back to shown.
      - Wanted cells that are unknown become pending and are returned, in `wanted`'s order.
      - Pending cells stay pending whether wanted or not.
      - The cache is then trimmed to its capacity, oldest entries first.
    - `void finish(const Cell& cell, Chunk chunk)`: a pending cell's build is done. The cell becomes shown if the last `setWanted` wanted it; otherwise it becomes the cache's newest entry, and the cache is trimmed. A cell that is not pending is ignored (a build that outlived a `clear`). A default-constructed (empty) chunk is kept like any other, so cells without groundcover are not built again.
    - `const std::map<Cell, Chunk>& shown() const`, `bool isPending(const Cell&) const`, `std::size_t pendingCount() const` and `std::size_t cachedCount() const`.
    - `void clear()`: forgets every cell. Builds still running are then ignored when they finish.
    - `MWRender::Groundcover` builds a chunk on RenderManager's operation threads: reading the instances and models, and copying the draws. It compiles and attaches the chunk on the main thread in a later `update()`; `update()` collects finished builds every frame, and works out the wanted cells only after 1024 units of movement, as before.
    - Chunks leaving the scene graph are handed to the compile context's `detach`, so frames still queued can finish drawing them.
    - The cache keeps up to 32 chunks (a named constant, chosen here, not OpenMW's).
    - The World's destructor waits for running builds before the store goes, through `RenderManager::setGroundcoverStore(nullptr)`.
    - **Done:** as specified, with these changes after the fresh review:
      - Groundcover builds on its own worker thread. RenderManager's single operation thread also runs cell preloading, so builds there could have turned seamless cell crossings into loading screens.
      - `notifyWorldSpaceChanged` no longer clears groundcover, so chunks survive interiors; `Groundcover::clear` is removed.
      - The destructor detaches the shown chunks.
      - A build that throws logs an error and its cell counts as empty.
      - The inherited `vsgUtil::Operation` (`components/vsgutil/operation.hpp`) now sets `done` under its mutex and waits with a predicate. It could miss its wake-up and hang the waiting thread forever, which the new waits on the main thread would have exposed.
    - Checked in game with a temporary layout of 21 × 21 vanilla grass per cell (not committed): chunks are queued on the worker, collected on the main thread and drawn in Seyda Neen. The first update runs with the camera at the origin and builds four cells there that are then cached unused, as in step 2.
  - **Open:** OpenMW's terrain-quadtree chunk sizes and per-instance near-plane computation are not ported; animated groundcover models do not animate; `[Groundcover]` settings are read when it is created; the loading screen shows no progress while groundcover files load.
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
- **Defaults:** with `distant terrain = false` (the default), OpenMW at the fork point ran no object paging, and groundcover, normal maps, alpha to coverage, soft particles and post-processing are all off by default. The remaining default-on differences are rain ripples on water (with the water item; kept on by default, toddhow 2026-10-30, as OpenMW 0.51 shows them with shader water) and vsgopenmw's accepted water look (`sunlight scattering`, `wobbly shores`).

## Open items (2026-10-30)

Still to do in V4 (toddhow: finish V4 before V5, V6 and Track W). Each is its own sub-stage:

- **Normal and specular maps** (the 8 `[Shaders]` keys listed in research note 0006): **done** for objects (step 1, V4h) and terrain (step 2, V4i). Survey:
  - vsgopenmw has no tangents, no normal-map shader path and no specular lighting at all. The `specular` specialization constant (`components/pipeline/graphics.cpp`) is never used by `files/shaders/default/main.frag`, and `nif.cpp` leaves the material's specular commented out.
  - Point lights are lit per vertex (`pointLightDiffuse` in `main.vert`), so a normal map would also need per-pixel point lighting.
  - NIF material specular matters only for non-Morrowind NIFs: OpenMW at 442eec6e2f zeroed specular for every NIF of Morrowind's version or older, or with `NiSpecularProperty` off (`NifOsg::LoaderImpl::applyDrawableProperties`). So vanilla Morrowind shows no specular, and only specular maps and newer NIFs need the term.
  - **Step 1 (V4h), object normal and specular maps.** Follows OpenMW at 442eec6e2f: `Shader::ShaderVisitor::applyStateSet` (auto-detection), `objects.vert`/`objects.frag` and `lib/light/lighting.glsl` (shading), and OSG 3.6.5's `osgUtil::TangentSpaceGenerator` (tangents).
    - **Settings:** `[Shaders] auto use object normal maps` and `auto use object specular maps` (both off by default), `normal map pattern` (`_n`), `normal height map pattern` (`_nh`) and `specular map pattern` (`_spec`) are read when the resource system is created. With both switches off, nothing changes.
    - **Finding the maps** (renderer-free, unit-tested; `components/vsgadapters/nif/automaps.hpp`):
      - `std::string autoMapPath(std::string_view diffusePath, std::string_view pattern)` replaces the last `.` of the path with `pattern + "."`, as `Misc::StringUtils::replaceLast` did. A path with no `.` gives an empty string.
      - `AutoMaps findAutoMaps(std::string_view diffusePath, std::string_view bumpPath, const AutoMapSettings&, const std::function<bool(std::string_view)>& exists)`, where `AutoMaps` is `{ std::string normalMap; bool normalHeight = false; std::string specularMap; }`.
      - With normal maps on, the normal-height candidate is tried first and gives `normalHeight = true`; then the plain normal candidate.
      - A found normal map equal to `bumpPath` (a non-empty NiTexturingProperty bump texture of the same file) is not used: it's probably not a real normal map.
      - With specular maps on, the specular candidate is used if it exists.
      - An empty `diffusePath` finds nothing.
      - `diffusePath` is the diffuse texture's resolved path (after the texture options' `.dds` correction), as OpenMW used the image's file name.
    - **Two-channel normal maps:** `bool isTwoChannelFormat(VkFormat)` is true for the two-channel 8- and 16-bit formats (`R8G8_*`, `R16G16_*`) and BC5. Such a map reconstructs Z (`normal.z = sqrt(1 - dot(xy, xy))`) and drops parallax, as OpenMW's `mReconstructNormalZ` did.
    - **Tangents** (renderer-free, unit-tested; `components/vsgadapters/tangents.hpp`):
      - `std::vector<vsg::vec4> generateTangents(const std::vector<vsg::vec3>& vertices, const std::vector<vsg::vec3>& normals, const std::vector<vsg::vec2>& texCoords, const std::vector<std::vector<uint16_t>>& indexLists, bool strip)` ports `TangentSpaceGenerator::compute` and the end of `generate`.
      - Without `strip`, each list is a triangle list: consecutive triples, with a trailing partial triple ignored.
      - With `strip`, each list is one NIF strip (OpenMW gave OSG each strip separately): window `i` is (i, i+1, i+2) for even `i` and (i+1, i, i+2) for odd `i`, as OSG's `TRIANGLE_STRIP` case.
      - Results accumulate over all lists.
      - Per triangle and axis, the tangent and binormal increments come from the cross product of (position delta, uv deltas) rows, skipped when its x is 0. Each vertex accumulates `(N×T)×N` and `N×(B×N)` with its own normal; with no normals, the face normal is used.
      - Each result is the normalized accumulated tangent, with `w = -1` when (T×B)·N < 0 (N being the accumulated normal), else `+1`. A vertex in no triangle gives (0, 0, 0, 1).
      - Inputs of mismatched sizes, or indices out of range, give an empty result.
    - **Drawing:**
      - New pipeline modes:
        - `NORMAL_MAP`: a per-vertex tangent input after the bone weights and before the instance inputs, and a normal map at the new `NORMAL_UNIT`.
        - `PARALLAX`: height in the normal map's alpha, offset `eye.xy * (height * 0.04 - 0.02)`.
        - `RECONSTRUCT_NORMAL_Z`.
        - `SPECULAR_MAP`: a specular map at the new `SPECULAR_UNIT`, with colour in RGB and shininess as alpha × 255.
      - The NIF reader adds the maps to the node's texture set, with the diffuse texture's wrap mode and its UV set. It generates tangents for triangle geometry with normals and that UV set; otherwise it drops the normal-map modes. Particles never get the maps.
    - **Shading** with `NORMAL_MAP` or `SPECULAR_MAP` is per pixel, as OpenMW's `PER_PIXEL_LIGHTING`:
      - The view-space normal comes from the normal map through the interpolated tangent frame (T, B = (N×T)·w and N, each transformed like the normal), as `normalToViewMatrix`.
      - Point lights are summed in the fragment shader from the same light grid.
      - With a specular map, the sun (`scene.lightSpecular`) and point lights (their colour, none for negative lights) add `specularColor * pow(max(N·H, 0), shininess)` where N·L > 0. The sun's term is shadowed, and the sum is added after the lit colour, as `objects.frag` does.
      - With a normal map, the env-map coordinates use the per-pixel normal.
    - **Changed while implementing:** the maps read the diffuse texture's UV set through `diffuseUV`, not specialization constants of their own. Two new constants (ids 17 and 18) made every frame white on this machine's driver, with nothing logged; the cause was not found (pitfall in `apps/openmw/mwrender/CLAUDE.md`). `lib/light/equations.glsl` gained an include guard, since per-pixel and directional lighting both include it.
    - **Checked:**
      - Unit tests: `vsgadapters/testautomaps.cpp` (25) and `testtangents.cpp` (24), written from this spec by a separate agent.
      - In game in Seyda Neen, with no normal-mapped content installed: an empty `normal map pattern` makes every object use its own diffuse texture as its normal map, which exercises tangents, the shader path and per-pixel lighting on every object. They draw with per-pixel relief and no shader errors. The same trick with `specular map pattern` and `normal height map pattern` (parallax) also draws without errors.
      - Off by default, in the Caldera Mages Guild (`coc` from Seyda Neen, frame 400):
        - two default-settings frames of the same build differ in 26 of 921,600 pixels;
        - the new fragment shader against the old one differs in 18 to 20 (MAE 0.0000).
      - Seyda Neen's exterior is too noisy for this comparison, since the sky moves: MAE 0.11 between two runs of one build.
      - Real normal-mapped mod content is untested.
    - **Review (fresh agent):** nothing found with the switches off.
      - **Fixed before committing:**
        - The sky's cloud and night vertex shaders read NIF geometry through `lib/attributes.glsl` but did not import `NORMAL_MAP`, so a sky mesh with a found normal map would have read its tangent as texture coordinates. Both now import it.
        - The shadow bias uses the geometry normal, not the normal map's, as OpenMW's shadows did.
        - The scene environment glow (`object.envColor`) keeps the vertex env coordinates; only the NIF env map uses the per-pixel ones, as in `objects.frag`.
        - `removeMode` drops trailing unset modes, so removing modes can't make duplicate pipeline cache keys.
      - **Left as they are, matching OpenMW:**
        - A vertex whose triangles all have degenerate UVs gets a zero tangent, so its normal is undefined.
        - SNORM two-channel maps are still remapped with `* 2 - 1`.
        - The vertex shader still computes per-vertex point light for per-pixel draws.
    - **Not in this step:** terrain normal and specular maps (step 2); `normalMap` and `specularMap` textures named in NIF files newer than Morrowind's; material specular of non-Morrowind NIFs.
  - **Step 2 (V4i), terrain normal and specular maps.** Follows OpenMW at 442eec6e2f: `ESMTerrain::Storage::getLayerInfo` (finding the maps), `compatibility/terrain.vert` and `terrain.frag` (shading).
    - **Settings:**
      - `[Shaders] auto use terrain normal maps` and `auto use terrain specular maps`, both off by default.
      - With the object keys' `normal map pattern` and `normal height map pattern`, and `terrain specular map pattern` (`_diffusespec`).
      - Read when the terrain is created. With both switches off, nothing changes: the terrain shader is compiled exactly as before.
    - **Finding the maps** (renderer-free, unit-tested; `components/terrain/layermaps.hpp`):
      - `TerrainLayerMaps findTerrainLayerMaps(const std::vector<std::string>& layerPaths, const vsgAdapters::AutoMapSettings&, const std::function<bool(std::string_view)>& exists)`.
      - `layerPaths` are each layer's resolved diffuse path. `TerrainLayerMaps` is `{ std::vector<TerrainLayer> layers; std::vector<std::string> normalMaps; }`, and `TerrainLayer` is `{ std::string diffuseMap; int normalMapIndex = -1; bool parallax = false; bool specular = false; }`.
      - Per layer, as `getLayerInfo`: with normal maps on, the normal-height map, else the normal map. A found map is appended to `normalMaps` once, a path shared by several layers reusing its index, and `parallax` is set for a normal-height map.
      - With specular maps on, an existing `pattern` path (`autoMapPath` with the specular pattern) replaces the layer's diffuse map and sets `specular`.
      - Layers keep their order and count; an empty path finds nothing.
    - **Drawing:** when either switch is on, the terrain builder loads each layer's (possibly replaced) diffuse map into the layer array as before, plus:
      - the normal maps as a second array texture (`NORMAL_LAYER_BINDING`), or a 1×1 flat map if none were found;
      - a per-layer table of flags (`LAYER_FLAGS_BINDING`, one `uint` each: the normal map index in the low 16 bits, 0xffff for none, bit 16 parallax, bit 17 specular).
      - The terrain shaders are compiled with `TERRAIN_MAPS` defined.
    - **Shading with `TERRAIN_MAPS`:**
      - The tangent frame is OpenMW's terrain one: `generateTangentSpace((1, 0, 0, 1), N)` with the tangent re-derived as `-normalize(N × B)`, in view space through the normal matrix.
      - Each of the four blended layers takes its own parallax offset (from its normal map's alpha), its diffuse at that offset, its tangent-space normal (or (0, 0, 1)) and its specular strength (its diffuse alpha for a specular layer, else 0).
      - The diffuse colour, normals and specular strengths are blended with the existing blend coefficients, and the blended normal is lit once per pixel. Point lights are summed in the fragment shader, and specular uses shininess 128 and white light times the blended strength, as `terrain.frag`. The shadow bias keeps the geometry normal.
      - **Known difference:** OpenMW drew one pass per layer and blended the lit results. Blending the normals and then lighting differs slightly where layers with different normal maps meet. Layers without maps are also lit per pixel here, where OpenMW lit them per vertex.
    - **Checked:**
      - Unit tests: `terrain/testlayermaps.cpp` (22), written from this spec by a separate agent.
      - In game in Seyda Neen, with a temporary data folder of generated 4×4 flat normal maps, one `_n.dds` for each of `Morrowind.bsa`'s 3,816 textures (not committed):
        - With terrain normal maps on, the frame matches the default frames within the run-to-run noise: MAE 0.14 and 0.21, against 0.11 between two default runs of one build. The small extra comes from per-pixel point lighting and the 8-bit flat normal.
        - Using each layer's diffuse texture as its own normal map (an empty pattern) or as its specular map draws with no shader errors.
      - Off by default: the default frame against the step 1 build's is MAE 0.013 and 0.109, within the same noise.
      - Real normal-mapped terrain content is untested.
      - Tangent orientation against OpenMW: the same generated maps tilted toward +x and toward +y, drawn in Seyda Neen in this build and in the OSG build of `cf3dbd3aa7`. The mean brightness of the terrain in view moved the same way in both builds: +9.9 (OSG) and +6.8 (here) for +x, −3.8 and −3.3 for +y. So the tangent and binormal point the same way as OpenMW's.
    - **Review (fresh agent):** nothing found with the switches off.
      - **Fixed before committing:**
        - The normal map array keeps only maps of its most common size and format, and layers whose map differs are drawn without one, with a logged message. `convertToArrayTexture` would have filled them with grey bytes, which decode to a badly tilted normal rather than a flat one.
        - Every terrain texture is sampled outside branches, with the flags selecting results, since blend map layers vary between neighbouring pixels and implicit derivatives inside such branches are undefined.
      - **Noted:**
        - The shadow pass also runs the per-pixel shader when the maps are on.
        - `components/terrain` has no area guide yet.
    - **Not in this step:** two-channel (Z-reconstructing) terrain normal maps; normal maps of a size or format other than the most common one (not used, see the review).
- **Point lights** (V4m, done 2026-10-31):
  - **The bug:** `lib/inout.glsl` declared `vec2 texCoord[numUvSets]` before other members. Its length is a specialization constant but locations are assigned at compile time, so a model with more UV sets overlapped what followed, `pointLightDiffuse` included. At `ca96e2fdd7` the Caldera Mages Guild frame is identical from frame 300 to 800 (mean 19.7) although the shader reads changing light data, and one unused `const int` in `lib/material/bindings.glsl` makes it 36.8 with the same executable. The array is now last; the frame no longer depends on that text.
  - **OpenMW's falloff, ported:** lights fade between the radius and twice the radius (`lcalcIllumination`), with `[Shaders] classic falloff` turning it off; flicker scales the colour, not the radius (`LightController`); projectile lights have radius 66 (`projectilemanager.cpp`); `[Shaders] minimum interior brightness` applies (`configureAmbient`). `lib/util/quickstep.glsl` had to move out of the commented part of `files/shaders/CMakeLists.txt`: the Windows build folder held an old copy, and the Linux gate caught it.
  - **Visible change with default settings:** interiors now flicker, as in OpenMW. The Caldera frame's mean is 21.2 to 21.6 here, 12.7 in the OSG build of `cf3dbd3aa7`. One pillar pixel matches exactly; the far right wall (33 against 15) and the tapestry stay brighter, as they already did at `ca96e2fdd7`. Not traced. Needs toddhow's verdict.
  - **Review (fresh agent):** projectile light radius, found and fixed.
- **Weather particle occlusion** (V4l, done 2026-10-31, `[Shaders] weather particle occlusion`, off by default): rain and the snow effect are discarded under objects, as OpenMW's `PrecipitationOccluder` at `442eec6e2f`.
  - `MWRender::PrecipitationOccluder` renders objects from above into a 256 x 256 depth map (no terrain, as OpenMW's cull mask); `MWRender::makeOcclusionBox` and `occlusionCoord` hold its geometry (tests: `mwrender/testocclusionbox.cpp`). The map is bound only on occluded particles (`Mode::PARTICLE_OCCLUSION`, `OCCLUSION_UNIT`); the box's size and height travel in `lightDiffuse.w` and `lightSpecular.w`.
  - **Checked:** with a temporary debug shader in Seyda Neen rain, no drop in the open is marked; with terrain in the pass, drops shifted 200 units down turned hidden partway along their length, at the ground; with the box temporarily widened, objects are in the map. Rain with the setting on draws with no shader errors. With the setting off the Caldera frame's mean is 21.2 against 21.5 before this step, within flicker.
  - **Not checked:** a frame under a real roof, the map's left-right and up-down orientation, and snow. Startup scripts could not place an object over the player, rain did not start in the other towns tried, and snow had not started by frame 3600. Needs a look in game.
  - **Not ported:** `weather particle occlusion small feature culling pixel size`.
  - **Review (fresh agent):** the box lagged the camera by a frame, and the pass drew terrain, both fixed.
- **Object paging** (V4n, done 2026-10-31, `[Terrain] object paging` with `distant terrain`, which is off by default): `MWRender::DistantObjects` draws the statics, activators and doors of Morrowind's exterior cells beyond the active grid, one chunk per cell, built on its own worker thread as groundcover is. Rules from OpenMW's `ObjectPaging` at `442eec6e2f`: `distantobjectcells.*` (tests: `mwrender/testdistantobjectcells.cpp`).
  - **Not ported:** merging objects into shared geometry and the merge factor settings, LOD meshes, paging the active grid (`object paging active grid` has no effect) and ESM4. Each object is its own draw, so it costs more than OpenMW at the same settings; not measured.
  - **Checked:** in Seyda Neen at a viewing distance of 40000, the distant rocks and ridges sit where the OSG build's do. No shader or model errors.
  - **Not checked in game:** crossing into a new cell, a disabled or moved object, and a new game after one.
  - **Review (fresh agent), all fixed:** moved actors rebuilt chunks every frame (OpenMW's guards restored); a stale build could win over a newer one (generations); builds could read the store during World destruction (`setGroundcoverStore(nullptr)` destroys this now); particles drew with no controllers (switched off, on a private model cache); disabled state survived a new game (`clear()`); enabling a moved object redrew its old copy (separate sets); the wrong chunk could be forgotten (both cells); a gap when crossing cells (the next ring is built ahead).
- **Worn shields** (V4o, done 2026-10-31): `Npc::addCarriedLeft` had `getShieldMesh` commented out, so every NPC's shield used its ground model on default settings. It uses the armor's body part model again, as OpenMW's `ActorAnimation::getShieldMesh`. Covered by the build and integration tests only: no frame of a shield was compared.
- **Weapon and shield sheathing, quivers (done for NPCs and the player, 2026-10-31):** `[Game] weapon sheathing` and `shield sheathing`, off by default. `MWRender::Npc` adds the custom bones (nodes marked `BONE`) from `animations/<skeleton>/*.nif` through `Anim::Bones::addCustomBones`, as OpenMW's `injectCustomBones`, and attaches the holstered weapon, scabbard, shield and quiver; `Wielding::show` is virtual so each flips with its drawn counterpart. Checked with a generated skeleton file (no game data, not committed): undrawn sword and shield leave the hand for their bones, drawing brings them back, a bow's arrows fill the quiver slots, and with the settings off and the same data the frame matches the normal one.
  - **Not checked:** a real mod's skeleton; `_sh` scabbard and shield models, so the `Bip01 Weapon` and `Bip01 Sheath` paths have run on no data; the shield animations' text keys; the quiver count after firing.
  - **Not ported:** creatures that wield weapons (`WieldingCreature`, whose bones are attached from the start), and enchanted glow on an empty sheath node's shield.
  - **Review (fresh agent), fixed:** `_sh` models were looked up without `meshes/`, so never found; the hand not emptied in first person; a shared model changed when it had an empty sheath node; attached parts left after a failed compile; a full parse on each check of the sheathed shield; no glow on a scabbard's own weapon; arrows attached on a head-only preview.
  - **Known gap:** the quiver updates on drawing, firing and equipment changes, not when equipped ammunition is picked up or dropped (OpenMW's `itemAdded` and `itemRemoved`).
  - No separate test agent ran; `Anim::Bones::addCustomBones` has no unit test.
- **Post-processing** (answer 1; V4p), done 2026-10-31 apart from what is listed as not ported:
  - **What runs:** `MWRender::PostProcess` runs in the main command graph after the scene and before the GUI. It copies colour and (from inside the main pass, before the depth-sorted bins, as the water's copy) depth to images flipped to OpenGL's origin, runs each pass into an offscreen target, and copies the result back. The main render pass does not change. With `[Post Processing] enabled` off (the default) no object is created, and swapchain usage and the `SuspendRenderPass` condition are as before.
  - **Translation:** `Fx::translatePass` turns a parsed pass into Vulkan GLSL, keeping OpenMW's `omw_` helpers: plain uniforms become one anonymous block after `omw`, samplers get bindings, and words reserved since older GLSL (`sample`, `filter` and others) are renamed. Tests: `fx/testvulkanpass.cpp` compile every pass of the four built-in shaders.
  - **Lua:** `openmw.postprocessing` works: `load`, `enable`, `disable`, `isEnabled` and the uniform setters. `load` fails with a clear message when the setting is off.
  - **Checked in game (Seyda Neen, Windows):** `adjustments` at its default values matches the off frame in all but 5 of 192,000 samples; `bloomlinear` and `bloomlinear,adjustments` run with their named targets and no errors; a test shader showed the colour and depth copies are upright and aligned; a test Lua script loaded, enabled and set a uniform on a shader at runtime and the frame changed by the expected amount. `bloomlinear`'s result was judged only by looking at it, with no OSG comparison.
  - **Compiler:** glslang, already linked, behind `MWRender::compileGlsl`. Answer 1 names Slang's GLSL input; research note 0005 found it needs changes for these shaders and that embedding it adds about 27 to 33 MB. Adopting it is a separate step, and toddhow's to confirm.
  - **Antialiasing:** the GUI pass resolves its multisampled colour over the swapchain image, so with antialiasing on the result is drawn into that colour at the start of the GUI pass instead of being copied (`PostProcess::presentNode`).
  - **Antialiasing review (fresh agent), fixed:** the present pipeline changed the main view's shared view ID, and recorded under whichever view ID the GUI pass's command buffer held (both crash with shadows or a preview view on); the viewport was dynamic and unset for the draw; old pipelines freed while queued frames used them; a trailing named target shown as the result; screenshots missing post-processing (the result is now also copied to the swapchain image); the depth resolve's stage missing from the barrier. Checked with 4x antialiasing, shadows and bloom: no crash and the expected screenshot. Screenshots read the swapchain before the GUI pass, so the drawn present itself was only seen earlier in this stage's first check.
  - **Not ported:** custom vertex stages and compute passes (such a technique is logged and left out); blending; render target formats and mipmaps (targets use the swapchain's colour format, a real limit for HDR-style shaders); 1D and 3D samplers; normals, distortion and point lights (`omw_GetNormals` reads the scene colour, the count of lights is 0); HDR and eye adaptation; `Flag_Disable_*` for interiors and underwater; hot reload; `[Post Processing] auto exposure speed`, `transparent postpass`.
  - **HUD:** `MWGui::PostProcessorHud` is back (F2, `A_TogglePostProcessorHUD`, whose binding the V1 merge had dropped): lists the `.omwfx` files in `shaders/`, enables, disables and orders them into `[Post Processing] chain`, shows a technique's parse or compile error, and edits static uniforms, saved to `shaders.yaml` through `Fx::storeValue` and applied after parsing by `Fx::applyStoredValue`. A static uniform is a constant in the shader, so a change rebuilds the passes, at most once a frame (`PostProcess::requestRebuild`). OpenMW's debug mode (`toggleMode`), which made values uniforms and reloaded shaders while the HUD was open, is left out; the controls are always shown. Checked in game with a temporary test patch, since removed, that called the HUD's own functions: opening it, listing, a stored `uContrast` applied, a broken shader's compiler message shown, reset, and activating a technique saved to the chain. F2 itself, the mouse and keyboard controls, the filter box and the up and down buttons have not been used.
  - **HUD review (fresh agent), fixed:** the game did not pause under the HUD; a global logger swap around each compile; a full rebuild every frame while dragging a slider (now after 15 frames with no edit); Lua-set values not saved.
  - **Review (fresh agent), fixed:** a mirrored projection in `omw.projectionMatrix`; `weatherTransition`'s sense; `OMW_API_VERSION`; `.omwfx` filter and wrap ignored; named targets read before being written (cleared once); barrier stages and access masks; values left at zero (`sunVis`, water, underwater, `deltaSimulationTime`); `sunPos`'s orbit and night mirror; a failed pass taken as the final image; `glsl_extensions` dropped.
- **ESM4 terrain** (work item 3), not ported, 2026-10-31: `LandManager` reads only ESM3 land, and `Terrain::Paging` assumes ESM3's cell size and one global layer palette, where ESM4 has half-size cells and per-quad textures. Tried with Skyrim SE's `Skyrim.esm` from the user's own install: the game quits after loading, with a missing localized string as the last message, and no frame was captured, so terrain could not be judged. The root guide lists other games' compatibility packs under "Not yet", so a full port waits for that. An ESM4 worldspace now has no land instead of Morrowind's at the same coordinates. toddhow can overrule this.
- **Map window and menu video** (work item 6), 2026-10-31: the menu background video is present (`MainMenu`). The map window's controller code was restored in `b69400fa24`; nothing was exercised with a controller.
- **The deferred 0.49→0.51 renderer changes** (work item 6), checked against research note 0003 section 5.9 on 2026-10-31:
  - Done: graphic herbalism (`MWRender::findSwitches`); the hit overlay's additive blend (texture shader `blend_add` in `MWGui::WindowManager`, so `AdditiveLayer` is not needed, though `additivelayer.*` is still in the tree and not built).
  - Open: the loading screen (not checked).
  - Replaced: animation blending rules, by CreativeMotion (toddhow, task 0011).
- **The settings audit decisions** (research note 0006; toddhow's calls on the "decide" keys).
- **Gaps listed above:**
  - the hidden torch flame in the Caldera Mages Guild (2026-10-31: **gone**. A torch placed by a Lua script 100 units from the player in front of the tapestry burns on both builds; most likely the interface-block overlap, not re-tested against the old commit. The weak flame there had two causes, both fixed: the colour curve was sampled in seconds rather than as a fraction of a particle's life, and the emitter's box and speed variation were twice OpenMW's. The startup script does not place objects or equip items: use Lua);
  - snow's one-frame shift: **fixed** 2026-10-31, the world matrix no longer advances on a frame the dispatch is skipped; by reasoning and an unchanged frame, the shift itself was not measured;
  - particle size and node scale: by code reading not a gap. `main.vert` normalises the axes in the particle's local space and draws through a modelview that includes the scale, as OSG 3.6.5's `ParticleSystem::drawImplementation` does for `LOCAL_COORDINATES`, which OpenMW sets (`nifloader.cpp` at `442eec6e2f`). Not confirmed by image: the 2026-10-30 captures of a scaled torch did not show the torch;
  - groundcover's terrain-quadtree chunk sizes, animated models, settings read at creation, and its loading-screen progress.
- **toddhow's scene verdicts** (work item 1).
- **Performance re-measure (2026-10-31, commit a7c43f8346, Vulkan only):** the same method as research note 0004 section 4 (last 400 of 900 frames, no frame limit, 1280x720, median / p95 wall ms). The OSG figures are that note's, not re-run. With every optional setting at its default:

  | Scene | OSG median (note 0004) | V2 Vulkan median | V4 Vulkan median / p95 | V4 against OSG |
  | --- | --- | --- | --- | --- |
  | ald_ruhn_ashstorm | 1.09 | 1.66 | 1.64 / 2.27 | +50% |
  | balmora_caius_cosades | 0.66 | 0.66 | 0.67 / 1.04 | +2% |
  | balmora_exterior | 1.26 | 1.84 | 2.09 / 2.87 | +66% |
  | caldera_mages_guild | 0.85 | 0.89 | 0.96 / 1.41 | +13% |
  | ebonheart_council | 1.62 | 1.28 | 1.36 / 1.83 | -16% |
  | pelagiad_night_fog | 1.54 | 1.75 | 1.82 / 2.46 | +18% |
  | sadrith_mora_mushroom | 1.93 | 2.12 | 2.46 / 3.39 | +27% |
  | seyda_neen_coast | 2.12 | 2.23 | 2.80 / 3.75 | +32% |

  **Over the 10% CPU budget:** all five exteriors and, narrowly, Caldera. V4 added 2 to 26% on V2 in most scenes. Not traced. The budget is toddhow's call: every scene is still under 4 ms at p95. `vivec_arena_interior` read 0.22 ms and has no reference in the note, so it is left out.

### Found by task 0014's review (2026-10-31): post-Morrowind NIF materials

`vsgAdapters::nif` does not read Bethesda's later shader properties: `handleProperty` (`components/vsgadapters/nif/nif.cpp`) has no case for `BSShaderPPLightingProperty`, `BSShaderNoLightingProperty`, `BSLightingShaderProperty` or `BSEffectShaderProperty`, which the old loader handled (`nifosg/nifloader.cpp`). Its shape list also has only `BSLODTriShape` of the old loader's `BSTriShape`, `BSDynamicTriShape`, `BSMeshLODTriShape`, `BSSubIndexTriShape` and `BSSegmentedTriShape`. Morrowind's own meshes do not use these. ESM4 content will lose textures and depth flags or not draw.

Decided by toddhow (2026-10-31): this goes after V6 is done. Task 0014 deleted the tests of this behaviour; when it is built, its acceptance list is those cases, in `nifosg/testnifloader.cpp` before `1e921b4ade`: the `bs/` prefix for the four shader types' texture paths, and the prefix and depth flags of `BSLightingShaderProperty`.

### Window resize fix (2026-10-31)

Growing the window left the whole frame blank (the engine's screenshot read back as all zero); shrinking worked, with the GUI mis-scaled. Two causes, found with the Khronos validation layer (VUID-VkRenderPassBeginInfo-pNext-02852: render area wider than the framebuffer) and by forcing render areas from the environment:

- `RenderManager::updateExtents` set both render areas on the SDL event, but the swapchain and its framebuffers are only rebuilt at the next frame's image acquisition. That frame recorded passes with a render area larger than the old framebuffer, and the game stayed blank afterwards. Now `updateExtents` only asks the window for the new size; `RenderManager::syncExtents` (called each frame from `Engine::frame`, in menus too) sets the camera viewport, both render areas and post-processing from `Engine::swapchainExtent`, the extent the images really have.
- `mGuiRenderGraph` kept `vsg::RenderGraph`'s default `windowResizeHandler`, which rescaled the render area by new/old once more when the swapchain changed (new*new/old: too small after a shrink, so the HUD sat at the wrong place). Nulled, as `Engine::createRenderGraph` does for the main graph.

Checked on Windows with captures at the engine's own screenshot, compared by eye with a fresh start at the same size: grow 1280x720 to 1484x861, shrink to 884x481, grow-shrink-grow to 1684x911, maximize to 2560x1369. Normal-size frames (Seyda Neen exterior; Arrille's Tradehouse interior): the exterior matches the build before the change (diffmap sum 187); the interior differs from it exactly as much as two runs of the same build differ from each other. Not checked: the main menu and the intro movie (the engine's auto-screenshot only counts game frames), the editor's own resize path (unchanged), Linux. Pre-existing and separate: with `VSGOPENMW_VALIDATION` set, `vsgUtil::DebugMessenger::message` returns true, which makes the layer abort the call that failed validation; the GUI pipeline created with viewportCount 0 (VUID-VkPipelineViewportStateCreateInfo-viewportCount-04135 and scissorCount-04136) then comes back null and the game crashes in `BindGraphicsPipeline::record` at start-up. The same run also reports vkFreeCommandBuffers on in-use buffers, an acquire with a signalled semaphore and an undefined-layout image at the first frame after a swapchain rebuild; none was investigated.

#### Resize follow-up: shrink order, projection aspect (2026-10-31)

A review found that `syncExtents` ran before the next frame's image acquisition, so on a shrink the frame after the event still recorded the old, larger render area against the rebuilt, smaller framebuffers (the same VUID-VkRenderPassBeginInfo-pNext-02852 for one frame). `syncExtents` now applies `MWRender::clampArea(applied, swapchain, requested)` (`apps/openmw/mwrender/extentsync.hpp`): per axis the smaller of the swapchain's extent and the requested one, nothing for a zero axis. The projection's aspect ratio is now updated on every change and takes the applied extent, and the screenshot rectangle takes `swapchainExtent()`. Tests: `apps/openmw_tests/mwrender/testextentsync.cpp` (7 cases, no GPU); components-tests, openmw-tests and openmw-cs-tests pass.

NOT run, at toddhow's request: the game-level validation-layer check and the frame comparison against fresh starts for this follow-up (toddhow tried the resize himself and it works). Not verified: two swapchain rebuilds in a row, and a minimised window (what SDL and VSG report for it; only the zero-axis case of `clampArea` is unit-tested). Revert with `git revert <hash of this commit>`.

### Crash: particle emitter index one past the end (2026-10-31)

toddhow's crash at 20:56 (access violation reading `0xFFFFFFFFFFFFFFFF` in `vsgUtil::computeTransform` from `ParticleSystem::calculateEmitMatrix`, on an update worker). Cause found by reading: the emitter was chosen by `std::rand() / RAND_MAX * mPathsToEmitters.size()`, which equals the size when `rand()` returns `RAND_MAX` (1 call in 32768 on MSVC), so `mPathsToEmitters[size]` was read past the end of the vector, as a garbage `std::vector` of transform pointers. Every active emitter calls it each frame it emits, so a few minutes near fires and torches reaches it. The lifetime theory was checked and found sound: removals run on the main thread outside `UpdateThreads::waitIdle`, every `Npc` removal of a part with controllers is followed by `clearControllers` in `updateParts`, and the paths hold transforms of the same object. Fix: `vsgAdapters::chooseEmitter` (integer-safe, clamped). Test: `testparticleemitterindex.cpp` (new). Not reproduced in the game (a 1-in-32768 event; no harness can force it without a hook) and not confirmed against the dump's memory, so the conclusion rests on the reasoning and the call-site match. Not verified: that this is the only cause.

### Terrain hairline crack along batch borders (2026-10-31)

toddhow saw a thin winding strip of sky and water through the grass in third person. Found in the Seyda Neen start view as a 1 to 2 pixel dashed line of sky colour across the left hillside (about x 0-330, y 325-350 at 1280x720), absent in the OSG reference build and present in the pre-V5 build, so it came in with the VSG terrain.

**Where.** `Terrain::Paging`'s `DEBUG_CHUNKS` view (`[Terrain] debug chunks = true`) puts the line exactly on a batch border (the red edge of a batch's `texCoord`, with the blue chunk edge): the crack follows one cell border across the hill, which is why it looks like a plan-view line over uneven ground.

**Why.** `files/shaders/terrain/main.vert` read each vertex's height (and normal) with `texture(heightMap, (uv - 0.5) * size / (size + 1) + 0.5)`. The height texture has one texel per vertex, and `Pipeline::Builder::createSampler()` copies the default sampler, which repeats and filters. That coordinate puts the first and last vertex half a texel's worth of `1 / (2 * (size + 1))` short of the texel centre, so the border vertex blended in the texel from the opposite side of the batch: about 0.8% of the batch's height range for a 65 vertex batch (about 8 units across 1000 units of relief), more for coarser batches. Each of the two batches that share a border bled in a different far side, so the border vertex sat at two heights and left a sliver open. A varying height step gives the dashes.

**Fix.** The vertex shader reads its own texel with `texelFetch(heightMap, round(uv * (size - 1)))` (clamped), and the normal from the same texel. Interior vertices now take their own data too, where before they were blurred towards a neighbour by a fraction of a texel.

**Separated from the other hypotheses.**
- (b) LOD stitching and (d) the index buffers: new test `apps/components_tests/terrain/testbordersharing.cpp` builds `CreateIndexBuffer` output for 5 to 65 vertices with every stitched side and every combination of 0 to 2 steps on all four sides, and asserts the open edges (used by one triangle) are exactly the coarse outline and nothing else; no degenerate triangles, none used three times. It passes on the unchanged code, so the index data is not the cause.
- (a) float rounding of the shared position: tried first (position formed in cell units, so both batches round alike); the crack stayed, pixel for pixel, so that change was dropped.
- (c) discards: the terrain fragment shader has no `discard`; the gap shows the sky behind the ground.
- (e) water or sky overlap: not the cause, the gap was along a land border on a hillside, and the height fix removes it.

**Evidence.** Same start view before (`compare/vsg/crk0.png`) and after (three runs, `fxa` to `fxc`, within 4 to 24 pixels of each other): the crop `-X 0 -Y 310 -W 330 -H 60 -Scale 4` shows the dashed line before and none after; the difference image between before and after shows the dashed line and further dots along the same border to the right, and otherwise only the weather, water and NPC motion. Balmora before and after at the same settings: difference at noise level. Frame times (`perf.sh`) did not rise.

**Tests.** The model tests in `testbordersharing.cpp` mirror the shader's sampling (the shader cannot run in a unit test): the old coordinate moves the border height by more than 5 units on a 1000 unit slope, the new one takes the texel exactly, for every vertex of every chunk size. `components-tests` 2368, `openmw-tests` 799, `openmw-cs-tests` 226, 32 Lua tests pass; `openmw-cs` builds.

**Not verified.** The camera of the start views varies from run to run, so only the same-view runs were compared; Caldera's start view was inside a wall and was not usable. The fragment shader's colour map uses the same off-by-half-texel coordinate with a repeating sampler; its effect is a colour (not geometry) difference of under 1% at the borders and was left alone. Editor (`openmw-cs`) rendering of the terrain was not run, only built; its single cell batches are 65 texel textures, which the new read treats as before at the borders.
