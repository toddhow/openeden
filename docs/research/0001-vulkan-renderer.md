# 0001 — What a move to Vulkan would take

Date: 2026-10-28 · Asked by: toddhow · Status: decided, see `docs/decisions/0002-vulkan-renderer-on-vulkanscenegraph.md`

Feeds the guide's open question "Renderer direction". toddhow decided on 2026-10-28 for a total move to Vulkan on VulkanSceneGraph, with VSG math everywhere and a transplant from vsgopenmw starting now, as the next major track. The note below is kept as written when the decision was made; where the decision differs from its recommendation ("do not start a port yet", "wait for vsgopenmw's 0.51"), the decision governs.

## Summary

OpenEden renders through OpenSceneGraph (OSG) on OpenGL, and OSG has no Vulkan path. A move to Vulkan means replacing the renderer: about 82,000 lines of rendering code, with VulkanSceneGraph (VSG) as the most natural target. Recommendation: do not start a port yet; first settle what Vulkan is meant to buy, look for the unofficial vsgopenmw fork, and measure where frame time goes today.

## How deep OSG goes

Measured on branch `creativemotion-m12-runtime` (commit e2726604b6), excluding the vendored `components/creativemotion/`.

| What | Size |
| --- | --- |
| C++ files using OSG | about 620 of about 2,700, with about 15,000 `osg::` references (`osg::` 14,107; `osgUtil::` 281; `osgParticle::` 184; `osgDB::` 104; `osgViewer::` 92; `osgAnimation::` 88) |
| Rendering code a port rewrites | about 82,000 lines: `apps/openmw/mwrender` 25,094; `components/sceneutil` 16,332 (lights, shadows, render to texture); `apps/opencs/view/render` 16,328; `components/resource` 6,057; `components/nifosg` 5,409 (NIF to scene graph); `components/terrain` 3,933; `components/fx` 3,848 (post-processing); `components/shader` 2,203; `components/stereo` 2,064 (VR); `components/myguiplatform` 1,532 |
| OSG add-ons with no drop-in Vulkan equivalent | `osgParticle` (NIF particles, 19 files), `osgAnimation` (COLLADA skinning, 20), `osgShadow` and OpenMW's own `mwshadowtechnique`, `osgViewer` (44), `osgUtil` (57), the editor's Qt OpenGL widgets |
| Direct OpenGL calls | 87 files (framebuffer blits, lights, clears, extension checks) |
| Shaders | 61 GLSL files (3,231 lines) in `files/shaders`, plus modders' post-processing shaders (`.omwfx`, `components/fx`) written in OpenGL-style GLSL |
| Code outside rendering | `osg::Vec3f`, `osg::Quat` and `osg::Matrix` are the engine's general math types, used in 106 files in `mwphysics`, `mwmechanics`, `mwworld` and `components/detournavigator` |
| OpenEden's own code tied to OSG | `apps/openmw/mwrender/actorikcontroller.cpp`, `creativemotiondriver.cpp` and `joltdebugdraw.cpp` write OSG nodes |

## Options

### 1. Zink: OpenGL on Vulkan, no code change
Mesa's Zink driver implements OpenGL over Vulkan. It is built by default since Mesa 24.1 and has run on Windows since 2022 through its Kopper window-system work, but it is more proven on Linux than on Windows. The OpenGL version it exposes depends on the GPU's Vulkan features (2.1 at minimum).
- **Effort:** days of testing and packaging.
- **Gain:** "Vulkan underneath" only; no performance or features beyond what OpenGL gives today. Worth it as an experiment or a compatibility route, not as a renderer direction.

### 2. Port to VulkanSceneGraph
VSG is OSG's successor by the same author, MIT-licensed, built on Vulkan; the osg2vsg library converts OSG scene graphs and uses OSG's loaders. Its concepts are close to OSG's (scene graph, visitors, state), but its API differs: OSG's `StateSet` and update-callback model do not carry over, and particles, skinning, text, shadows and the optimizer need rebuilding or replacing.
- **Effort:** a rewrite of the ~82,000 lines above, plus the editor's rendering and MyGUI's backend. The math types can stay on OSG's math for a while, so physics, AI and CreativeMotion need not move in the same step.
- **Prior art:** vsgopenmw already did this for OpenMW 0.49; see "vsgopenmw" below.
- **Precedent:** OpenMW's previous renderer swap (Ogre3D to OSG) was announced in February 2015 and shipped in 0.37 in December 2015, about ten months, mostly by one strong core developer, on a much smaller engine (no shadows, post-processing, object paging, groundcover, VR or Lua UI then).

### 3. Own abstraction with a Vulkan backend
A renderer interface of OpenEden's own with a backend such as bgfx, Diligent, NVRHI or raw Vulkan. The most control, and it could keep an OpenGL fallback. The most work: everything in option 2 plus designing the abstraction, and running two backends is the cost OpenMW's own developers declined, saying they lack the manpower to support both and that dropping OSG would make OpenMW incompatible with much of the hardware it supports.

## vsgopenmw (found 2026-10-28)

https://github.com/vsgopenmw-dev/vsgopenmw, a GitHub fork of OpenMW/openmw, GPL-3.0 like OpenEden. State as of its last push on 2026-09-12, read from its own documents:

- **What it is:** "a rewrite (not a port) of OpenMW using Vulkan/VulkanSceneGraph" (`vsgopenmw-readme.md`), following VSG's "minimal and complete" philosophy. Its scope is limited to Morrowind (TES3); support for later games is to move to another project. That is the opposite of OpenEden's compatibility-pack direction.
- **Activity:** active. Recent work is by Bret Curtis (psi29a), an OpenMW core maintainer, including a written plan (`VSG_MERGE_PLAN.md`) to merge upstream OpenMW one version at a time, 0.49 → 0.50 → 0.51, validating each step with scripted screenshot comparisons against stock OpenMW.
- **Base and state:** `master` is OpenMW 0.49 + VSG and renders (Balmora loads with no fallbacks), but movement is broken, a known fork bug. The 0.50 and 0.51 merges have not started; an earlier direct attempt at 0.51 was abandoned. Physics is still Bullet (`components/nifbullet`, `bullethelpers`). It needs VulkanSceneGraph 1.0.9, vsgXchange and OSG headers (for math). No releases or packages.
- **What it replaced:** new components `animation`, `mwanimation`, `render`, `pipeline`, `view`, `vsgutil`, `vsgadapters` and `esm3terrain`; `apps/openmw/mwrender` has 127 files against OpenEden's 92.
- **Gains over OpenMW** (`vsgopenmw-features.doc`): clustered shading (more lights, fewer lighting artifacts), percentage-closer soft shadows, screen-space reflections and point lights on water, GPU skinning, morphs and particles, multisampled map and preview textures.
- **Gaps** (`vsgopenmw-roadmap.doc`): before feature parity it still needs weather particles, water ripples, paged instanced objects (object paging), normal maps, alpha to coverage, several NIF features, and physics and navmesh debug draw. Known issues: some applications (the editor among them) do not build; the navigator is untested and should be disabled.

**What it means for OpenEden:**
- **Rebasing OpenEden onto vsgopenmw is not viable.** OpenEden is on 0.51 with Jolt, the plugin system and CreativeMotion in the engine, and its own changes sit exactly where vsgopenmw rewrote: `apps/openmw/mwrender` (IK controller, M12 pose driver, Jolt debug draw) and NIF animation (`NifOsg::KeyframeController`, which vsgopenmw's `mwanimation` replaces). Its TES3-only scope also conflicts with compatibility packs.
- **Transplanting its renderer is the realistic Vulkan route,** once it reaches 0.51. GPL-3.0 allows taking the code with attribution. The work becomes: bring in its rendering components, re-do OpenEden's rendering touchpoints on VSG (the IK controller and pose driver write bones through its animation system instead of OSG nodes; the Jolt debug draw on VSG), close its parity gaps, and get the editor building for Eden Studio. Large, but far smaller than writing a Vulkan renderer from scratch.

## Costs beyond the code

- **Hardware:** Vulkan drops older GPUs; macOS would need MoltenVK (ties to the open question on macOS support).
- **Mods:** modders' post-processing shaders target OpenGL GLSL and would break or need translating.
- **Upstream merges:** OpenMW's rendering fixes stop merging cleanly; the fork's divergence becomes far larger than anything so far.
- **OpenEden work:** the IK controller, the M12 pose driver and the Jolt debug draw need porting. Eden Studio phase 1 (the shared viewport) depends on the chosen renderer. CreativeMotion's core and the plugin SDK's debug draw are already renderer-independent.

## Recommendation

Do not start a port yet. First answer what Vulkan is meant to buy (performance, PBR or ray-traced visuals, a modern renderer for Eden Studio, or long-term maintenance); each points to a different option, and none needs work to start now. Useful in every case:

1. ~~Find and evaluate vsgopenmw.~~ Done 2026-10-28 (section above): the realistic route is transplanting its renderer once it reaches OpenMW 0.51, not rebasing onto it.
2. Follow vsgopenmw's 0.50 and 0.51 merges (`VSG_MERGE_PLAN.md`); when it reaches 0.51, compare its rendering components with OpenEden's file by file to size the transplant.
3. Measure where frame time goes today. If the CPU side of the engine is the bottleneck, Vulkan alone will not make it faster.
4. Add a guide rule: new OpenEden code keeps rendering behind data or thin adapters (as the plugin debug draw already does), and new code that writes bones goes through one engine-side interface rather than OSG nodes directly, so a later transplant touches fewer places.
5. Record the outcome as a decision, closing the guide's renderer open question.

## Sources

- vsgopenmw repository and its documents (`vsgopenmw-readme.md`, `VSG_MERGE_PLAN.md`, `vsgopenmw-features.doc`, `vsgopenmw-roadmap.doc`, `vsgopenmw-build.md`), read through the GitHub API on 2026-10-28: https://github.com/vsgopenmw-dev/vsgopenmw

- GamingOnLinux comments on OpenMW 0.49 (vsgopenmw, a core developer on VSG and hardware support): https://www.gamingonlinux.com/2025/07/openmw-0-49-arrives-to-enhanced-morrowind-and-theyre-looking-to-support-later-bethesda-games/comment_id=280866 and https://www.gamingonlinux.com/2025/07/openmw-0-49-arrives-to-enhanced-morrowind-and-theyre-looking-to-support-later-bethesda-games/comment_id=280757
- VulkanSceneGraph: https://github.com/vsg-dev/VulkanSceneGraph ; vsgXchange: https://github.com/vsg-dev/vsgXchange
- OpenMW forum, Vulkan support: https://forum.openmw.org/viewtopic.php?p=45613
- OpenMW's switch to OSG: https://openmw.org/2015/announcing-switch-openscenegraph/ and https://www.phoronix.com/news/OpenMW-0.37-Released
- Zink: https://docs.mesa3d.org/drivers/zink.html ; https://www.phoronix.com/news/Zink-Windows-Kopper-Progress ; https://Phoronix.com/news/Mesa-24.1-Zink-D3D12-Default
