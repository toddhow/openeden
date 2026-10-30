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
    - Rain: OpenMW drew it as an OSG particle system, not a NIF; it needs its own VSG implementation.
    - The wrap-around that kept non-storm particles in a box around the camera (`WrapAroundOperator`).
    - The precipitation alpha fade during weather transitions: vsgopenmw's dithered `object.alpha` needs normals, and particles have none.
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
