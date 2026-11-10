# 0002 — The renderer moves entirely to Vulkan, on VulkanSceneGraph

Date: 2026-10-28 · Decided by: toddhow

## Decision
OpenEden replaces OpenSceneGraph and OpenGL with Vulkan, completely:

- **Foundation:** VulkanSceneGraph (VSG), OpenSceneGraph's MIT-licensed successor by the same author, with vsgXchange for asset loading.
- **No abstraction layer and no second backend.** OpenGL support ends; no DirectX backend is planned.
- **Math:** VSG's math types (`vsg::vec3`, `vsg::quat`, `vsg::mat4` and related) replace OSG's (`osg::Vec3f`, `osg::Quat`, `osg::Matrix`) everywhere in the engine, not only in rendering, so no OSG dependency remains.
- **Starting point:** the renderer is transplanted from vsgopenmw (https://github.com/vsgopenmw-dev/vsgopenmw), starting now from its OpenMW 0.49 `master` and adapted to OpenEden's 0.51 base, rather than waiting for vsgopenmw's own 0.50 and 0.51 merges.
- **Priority:** the Vulkan move is the next major track. Other features wait for it, so they are built on Vulkan once instead of being ported twice.

## Why
toddhow's reasons:
- **OpenEden's work leans on OSG math and the renderer.** The changes already made (IK, the CreativeMotion pose driver, the Jolt debug draw, compound hitboxes) and the ones planned use them heavily, so the move is total rather than partial.
- **The project's scope makes it the sensible option.** OpenEden's primary focus is being a modern game engine, and OpenGL cannot serve that.
- **Older hardware is an accepted cost.** Vulkan drops older graphics cards; that is acceptable for a modern engine.
- **An abstraction layer would only be worth it for DirectX,** which adds work with no real gain for an independent project.

Background and measurements: `docs/research/0001-vulkan-renderer.md`.

## Consequences
- **Hardware:** OpenEden requires a Vulkan-capable GPU and driver (VSG needs Vulkan 1.1 or later, per its build documentation). macOS needs MoltenVK or is dropped; that stays the guide's open question on macOS.
- **Scale of change:** about 82,000 lines of rendering code are replaced or rewritten, and OSG math leaves about 620 files, including about 106 in physics, AI, world and the navigator. CreativeMotion's own code is unaffected (it uses `opas::math`); its OpenEden adapters change.
- **vsgopenmw is a second upstream.** Its code is GPL-3.0 like OpenEden's: transplanted files keep their notices, and vsgopenmw's authors are credited. Its scope is Morrowind only; OpenEden takes its renderer, not its scope, and keeps the compatibility-pack direction. Transplants name the vsgopenmw commit they came from.
- **OpenMW upstream merges get harder.** OpenMW's rendering changes stop applying; merges of non-rendering fixes continue under the guide's merge rules, with the math change adding conversions at many call sites.
- **Mods:** modders' post-processing shaders (`.omwfx`, OpenGL-style GLSL) no longer work as they are; how they are handled is decided in the Vulkan track.
- **The OpenGL build is not kept alongside.** During the transition the OpenSceneGraph renderer stays only until the Vulkan renderer reaches the milestones in the guide's Track V; then OSG is removed.
- **Other work waits.** CreativeMotion's in-game milestones from M10, Eden Studio and plugin phase P6 start after Track V, unless toddhow decides otherwise for a specific item. *(Update 2026-11-10: toddhow closed Track V; its unrun checks are follow-ups, not blockers, so this wait no longer applies. See the root `CLAUDE.md`, Track V.)*
