# Task 0012: Track V stage V4 — Renderer parity

**Status:** draft, awaiting toddhow's approval. Depends on task 0011 (V3). Has decisions to make before implementation (open questions 1–3).
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
