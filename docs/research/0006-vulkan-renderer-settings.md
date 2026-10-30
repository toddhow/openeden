# Research Note 0006: Renderer settings on the Vulkan build (Track V, stage V4)

**Date:** 2026-10-29
**Status:** Draft. The removals proposed here need toddhow's agreement before any key is removed.
**Parent task:** [0012-vulkan-v4-renderer-parity.md](../tasks/0012-vulkan-v4-renderer-parity.md), work item 7 (settings audit). The spec names this note `0005-vulkan-parity.md`, but 0005 is the Slang note.

## Method

For each key in the sections task 0012 lists, the audit finds its member in `components/settings/categories/*.hpp`. It then searches the sources compiled into `openmw.exe` (`ninja -t inputs openmw.exe`: 851 `.cpp` files plus their headers, excluding `components/settings`) for that member name, or for the key string through the older string API.

- **"Read"** means some compiled code reads the key. It does not prove the key behaves as on the OSG build: that needs a scene or a test per key, which is the next step of the audit.
- **"Not read"** means nothing in the Vulkan build reads the key, so changing it does nothing today.

**Caveats:**
- Two member names are shared across categories (`mEnabled`, `mSmallFeatureCullingPixelSize`), so `[Post Processing] enabled` counts as read through other categories' `mEnabled`.
- `[Stereo]` and `[Stereo View]` were removed earlier in V4 (VR).

**Result:** 143 keys; 91 are read and 52 are not (after restoring `load unsupported nif files`, `xbaseanim`, `xbaseanim1st`, `xargonianswimkna`, `radial fog` and `exponential fog`). The proposals for the 58:
- **Implement:** most of them, as separate V4 items: fog modes, groundcover, object paging, normal and specular maps, alpha to coverage, soft particles, weather particle occlusion, base animation overrides, and NIF loader options.
- **Remove with a reason** (the Vulkan renderer works differently):
  - `reverse z`: always on in vsgopenmw.
  - `force shaders`: always shaders on Vulkan.
  - the composite map keys: vsgopenmw terrain has no composite maps.
  - OSG's LiSPSM shadow tuning: replaced by cascaded shadow maps with PCSS.
- **Decide with toddhow:** the water look keys, `sunlight scattering` and `wobbly shores`.

## Table

| Setting | Status | Proposal |
| --- | --- | --- |
| `[Camera] near clip` | Read | |
| `[Camera] small feature culling` | Not read | Implement (cull by projected size) or remove; vsgopenmw has no small-feature culling |
| `[Camera] small feature culling pixel size` | Not read | With the key above |
| `[Camera] viewing distance` | Read | |
| `[Camera] field of view` | Read | |
| `[Camera] first person field of view` | Read | |
| `[Camera] reverse z` | Not read | Remove: vsgopenmw always renders with reversed depth |
| `[Fog] use distant fog` | Read | |
| `[Fog] distant land fog start` | Read | |
| `[Fog] distant land fog end` | Read | |
| `[Fog] distant underwater fog start` | Read | |
| `[Fog] distant underwater fog end` | Read | |
| `[Fog] distant interior fog start` | Read | |
| `[Fog] distant interior fog end` | Read | |
| `[Fog] radial fog` | Read | Restored in V4 (`fog.settings` shader constant). vsgopenmw always used radial distance; OpenMW's default is planar, which halves the Seyda Neen image difference to the OSG build |
| `[Fog] exponential fog` | Read | Restored in V4 (`fog.settings`, OpenMW's formula) |
| `[Fog] sky blending` | Not read | Implement in the fog and sky shaders |
| `[Fog] sky blending start` | Not read | With sky blending |
| `[Fog] sky rtt resolution` | Not read | Remove: sky blending would not need a render-to-texture pass on VSG (confirm when implementing) |
| `[Groundcover] enabled` | Read | |
| `[Groundcover] density` | Read | Groundcover (task 0012, V4f) |
| `[Groundcover] rendering distance` | Read | Groundcover (task 0012, V4f) |
| `[Groundcover] stomp mode` | Read | Groundcover (task 0012, V4f) |
| `[Groundcover] stomp intensity` | Read | Groundcover (task 0012, V4f) |
| `[Map] global map cell size` | Read | |
| `[Map] local map hud fog of war` | Read | |
| `[Map] local map resolution` | Read | |
| `[Map] local map widget size` | Read | |
| `[Map] global` | Read | |
| `[Map] allow zooming` | Read | |
| `[Map] max local viewing distance` | Read | |
| `[Models] load unsupported nif files` | Read | Restored in V4 (`RenderManager` sets `Nif::Reader::setLoadUnsupportedFiles`, as OpenMW did) |
| `[Models] xbaseanim` | Read | Restored in V4: NPC animation sources (`npc.cpp`), as OpenMW's NpcAnimation |
| `[Models] baseanim` | Read | |
| `[Models] xbaseanim1st` | Read | Restored in V4: NPC animation sources (`npc.cpp`), as OpenMW's NpcAnimation |
| `[Models] baseanimkna` | Read | |
| `[Models] baseanimkna1st` | Read | |
| `[Models] xbaseanimfemale` | Not read | OpenMW only preloaded this file at startup (`RenderingManager` work item); implement with a preload step |
| `[Models] baseanimfemale` | Read | |
| `[Models] baseanimfemale1st` | Read | |
| `[Models] wolfskin` | Read | |
| `[Models] wolfskin1st` | Read | |
| `[Models] xargonianswimkna` | Read | Restored in V4: NPC animation sources (`npc.cpp`), as OpenMW's NpcAnimation |
| `[Models] xbaseanimkf` | Not read | OpenMW only preloaded this file at startup (`RenderingManager` work item); implement with a preload step |
| `[Models] xbaseanim1stkf` | Not read | OpenMW only preloaded this file at startup (`RenderingManager` work item); implement with a preload step |
| `[Models] xbaseanimfemalekf` | Not read | OpenMW only preloaded this file at startup (`RenderingManager` work item); implement with a preload step |
| `[Models] xargonianswimknakf` | Not read | OpenMW only preloaded this file at startup (`RenderingManager` work item); implement with a preload step |
| `[Models] skyatmosphere` | Read | |
| `[Models] skyclouds` | Read | |
| `[Models] skynight01` | Read | |
| `[Models] skynight02` | Read | |
| `[Models] weatherashcloud` | Read | |
| `[Models] weatherblightcloud` | Read | |
| `[Models] weathersnow` | Read | |
| `[Models] weatherblizzard` | Read | |
| `[Models] write nif debug log` | Not read | Not read by the OSG build either (only `niftest` sets the flag); inherited, no change |
| `[Post Processing] enabled` | Read | |
| `[Post Processing] chain` | Read | |
| `[Post Processing] auto exposure speed` | Not read | Post-processing sub-stage (Slang) |
| `[Post Processing] transparent postpass` | Not read | Post-processing sub-stage |
| `[Shaders] force shaders` | Not read | Remove: the Vulkan renderer always uses shaders |
| `[Shaders] force per pixel lighting` | Read | |
| `[Shaders] clamp lighting` | Read | |
| `[Shaders] auto use object normal maps` | Not read | Normal maps item |
| `[Shaders] auto use object specular maps` | Not read | Normal maps item |
| `[Shaders] auto use terrain normal maps` | Not read | Normal maps item |
| `[Shaders] auto use terrain specular maps` | Not read | Normal maps item |
| `[Shaders] normal map pattern` | Not read | Normal maps item |
| `[Shaders] normal height map pattern` | Not read | Normal maps item |
| `[Shaders] specular map pattern` | Not read | Normal maps item |
| `[Shaders] terrain specular map pattern` | Not read | Normal maps item |
| `[Shaders] apply lighting to environment maps` | Not read | Remove: in OpenMW it only forced the shader path for env-mapped objects (`ShaderVisitor`, cf3dbd3aa7), and the Vulkan renderer always uses shaders |
| `[Shaders] lighting method` | Read | |
| `[Shaders] classic falloff` | Read | |
| `[Shaders] match sunlight to sun` | Read | |
| `[Shaders] light bounds multiplier` | Read | |
| `[Shaders] maximum light distance` | Read | |
| `[Shaders] light fade start` | Read | |
| `[Shaders] max lights` | Read | |
| `[Shaders] minimum interior brightness` | Read | |
| `[Shaders] antialias alpha test` | Not read | Alpha-to-coverage item |
| `[Shaders] adjust coverage for alpha test` | Not read | Alpha-to-coverage item |
| `[Shaders] soft particles` | Not read | Implement (depth-faded particles) |
| `[Shaders] weather particle occlusion` | Not read | Weather particles item |
| `[Shaders] weather particle occlusion small feature culling pixel size` | Not read | Weather particles item |
| `[Shadows] percentage closer soft shadows` | Read | |
| `[Shadows] blocker samples` | Read | |
| `[Shadows] pcf samples` | Read | |
| `[Shadows] filter shadow maps` | Read | |
| `[Shadows] enable shadows` | Read | |
| `[Shadows] number of shadow maps` | Read | |
| `[Shadows] maximum shadow map distance` | Read | |
| `[Shadows] shadow fade start` | Read | |
| `[Shadows] split point uniform logarithmic ratio` | Read | |
| `[Shadows] split point bias` | Not read | Remove: vsgopenmw uses cascaded shadow maps with PCSS, not OSG LiSPSM shadows |
| `[Shadows] enable debug hud` | Read | |
| `[Shadows] enable debug overlay` | Not read | Remove: vsgopenmw uses cascaded shadow maps with PCSS, not OSG LiSPSM shadows |
| `[Shadows] compute scene bounds` | Not read | Remove: vsgopenmw uses cascaded shadow maps with PCSS, not OSG LiSPSM shadows |
| `[Shadows] shadow map resolution` | Read | |
| `[Shadows] minimum lispsm near far ratio` | Not read | Remove: vsgopenmw uses cascaded shadow maps with PCSS, not OSG LiSPSM shadows |
| `[Shadows] polygon offset factor` | Not read | Remove: vsgopenmw uses cascaded shadow maps with PCSS, not OSG LiSPSM shadows |
| `[Shadows] polygon offset units` | Not read | Remove: vsgopenmw uses cascaded shadow maps with PCSS, not OSG LiSPSM shadows |
| `[Shadows] normal offset distance` | Not read | Remove: vsgopenmw uses cascaded shadow maps with PCSS, not OSG LiSPSM shadows |
| `[Shadows] use front face culling` | Read | |
| `[Shadows] actor shadows` | Read | |
| `[Shadows] player shadows` | Read | |
| `[Shadows] terrain shadows` | Read | |
| `[Shadows] object shadows` | Read | |
| `[Shadows] enable indoor shadows` | Read | |
| `[Terrain] distant terrain` | Read | |
| `[Terrain] lod factor` | Read | |
| `[Terrain] vertex lod mod` | Read | |
| `[Terrain] composite map level` | Not read | Remove: vsgopenmw terrain blends layers in the shader, no composite maps |
| `[Terrain] composite map resolution` | Not read | Remove (as above) |
| `[Terrain] max composite geometry size` | Not read | Remove (as above) |
| `[Terrain] debug chunks` | Read | |
| `[Terrain] object paging` | Read | |
| `[Terrain] object paging active grid` | Not read | Object paging item |
| `[Terrain] object paging merge factor` | Not read | Object paging item |
| `[Terrain] object paging min size` | Not read | Object paging item |
| `[Terrain] object paging min size merge factor` | Not read | Object paging item |
| `[Terrain] object paging min size cost multiplier` | Not read | Object paging item |
| `[Terrain] water culling` | Not read | Implement or remove (terrain chunks below water) |
| `[Video] resolution x` | Read | |
| `[Video] resolution y` | Read | |
| `[Video] window mode` | Read | |
| `[Video] screen` | Read | |
| `[Video] minimize on focus loss` | Read | |
| `[Video] window border` | Read | |
| `[Video] antialiasing` | Read | |
| `[Video] vsync mode` | Read | |
| `[Video] framerate limit` | Read | |
| `[Video] contrast` | Read | |
| `[Video] gamma` | Read | |
| `[Water] shader` | Read | |
| `[Water] rtt size` | Read | |
| `[Water] refraction` | Read | |
| `[Water] reflection detail` | Read | |
| `[Water] rain ripple detail` | Read | |
| `[Water] small feature culling pixel size` | Not read | Remove with the reflection technique change, or implement with small feature culling |
| `[Water] refraction scale` | Read | |
| `[Water] sunlight scattering` | Not read | toddhow: vsgopenmw water look accepted; port or remove |
| `[Water] wobbly shores` | Not read | toddhow: port or remove |
