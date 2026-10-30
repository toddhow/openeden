# Research Note 0004: Morrowind on Vulkan — playability, images and performance (Track V, stage V2)

**Date:** 2026-10-29
**Status:** Draft for toddhow's verdicts
**Parent task:** [0010-vulkan-v2-game-on-vulkan.md](../tasks/0010-vulkan-v2-game-on-vulkan.md)

This note gathers what task 0010 asks to be recorded: test results, the image comparison of the reference scenes, the performance comparison against the OSG build, and the differences that need toddhow's verdict. All numbers are from one Windows machine (MSVC RelWithDebInfo, 1280×720 window, vsync off, no frame limit) and the Linux container described in `CLAUDE.md`.

## 1. Tests

| Gate | Result |
| --- | --- |
| Linux build with CI's `-Werror` flags (GCC, Ubuntu 24.04 container) | Builds (after the warning fixes in this stage; third-party headers are included as system headers, CreativeMotion's own `-Werror` relaxed, see `CMakeLists.txt`) |
| `components-tests` (Linux) | 1485 / 1485 pass |
| `openmw-tests` (Linux) | 520 / 520 pass |
| Lua integration tests (Linux, xvfb, Mesa lavapipe) | 32 / 32 pass, including all five navigation tests |
| Lua integration tests (Windows) | 32 / 32 pass (2026-10-29, earlier in this stage) |
| `openmw.postprocessing` | `load` fails with "post-processing is not available on this build yet (Vulkan renderer, OpenEden task 0012)" instead of crashing |

## 2. Bug found and fixed by the comparison: textures read as sRGB

The first comparison showed terrain, boulders and the shaded sides of actors nearly black in daylight (Seyda Neen ground: RGB 8,6,3 on Vulkan against 95,90,79 on OSG). Shader instrumentation narrowed it down:

1. Scene ambient (0.53), sun direction and normals were correct.
2. The shadow term was 1 and the lighting term was normal.
3. The sampled terrain texture itself was dark.

**Cause:** vsgXchange 1.1's stb_image reader (TGA, PNG, JPG and BMP) creates 8-bit images with `VK_FORMAT_R8G8B8A8_SRGB`, so the GPU converts them to linear when sampling, which darkens everything. vsgopenmw predates VSG 1.1 and its shaders work on stored values, as OpenMW's did. Most Morrowind terrain textures are TGA.

**Fix:** `vsgUtil::readOptionalImage` and `vsgUtil::readImageFromMemory` now reinterpret 8-bit sRGB formats as UNORM (`vsgUtil::removeSrgb`), with the same bytes and no conversion. DDS files keep their own format. After the fix the Seyda Neen ground matches OSG closely.

**Pitfall:** this joins the "vsgopenmw predates VSG 1.1" list in `CLAUDE.md`.

## 3. Image comparison

The reference scenes in `tools/render_scenes.txt` were captured at noon with `--auto-screenshot 300` on both builds. The OSG build is OpenEden at `cf3dbd3aa7`, built in a separate worktree. "MAE" is the mean absolute difference per channel (0–255) over every fourth pixel; the last column is the share of sampled pixels differing by more than 40.

| Scene | MAE | > 40 | What differs | Verdict (toddhow) |
| --- | --- | --- | --- | --- |
| ald_ruhn_ashstorm | 8.7 | 0.1% | Nearly identical | |
| balmora_caius_cosades | 16.2 | 1.2% | Clustered lighting falloff differs | |
| balmora_exterior | 44.6 | 29.3% | The camera starts a little lower on Vulkan (where the player lands at start), so the near rock covers a different area; lighting matches | |
| caldera_mages_guild | 23.7 | 8.5% | Interior lighting (clustered lights) | |
| ebonheart_council | 7.4 | 0.0% | Nearly identical | |
| pelagiad_night_fog | 4.8 | 1.0% | Nearly identical | |
| sadrith_mora_mushroom | 8.6 | 3.2% | Water | |
| seyda_neen_coast | 30.1 | 25.1% | Water: vsgopenmw's water is a flat pale blue, without OSG's reflections | |
| vivec_arena_interior | — | — | Not captured: the scene list names a cell that does not exist ("Vivec, Arena Canton"); `tools/render_scenes.txt` needs the right cell name | |

No scene shows missing objects, black textures, wrong materials or wrong geometry after the fix in section 2. The water look is the accepted vsgopenmw look (toddhow, 2026-10-29); parity work on it is task 0012's.

The scenes start at each cell's default position, which often puts the camera close to a rock (Balmora, Ald-ruhn). Before toddhow's final judgement, the scene list should give each scene a camera position and direction (`tools/render_scenes.txt` has no field for them yet).

## 4. Performance

Frame times are per frame over the last 400 of 900 frames in each scene. For OSG this is "Frame duration" from `OPENMW_OSG_STATS_FILE` with `OPENMW_OSG_STATS_LIST=times`; for Vulkan it is wall time from `OPENEDEN_FRAME_STATS_FILE`. On Vulkan the time spent waiting for the GPU is under 0.1 ms per frame, so the Vulkan frame time is almost all CPU time.

| Scene | OSG median / p95 (ms) | Vulkan median / p95 (ms) | Median change |
| --- | --- | --- | --- |
| ald_ruhn_ashstorm | 1.09 / 1.65 | 1.66 / 2.46 | +52% |
| balmora_caius_cosades | 0.66 / 0.79 | 0.66 / 0.93 | +0% |
| balmora_exterior | 1.26 / 1.67 | 1.84 / 2.49 | +46% |
| caldera_mages_guild | 0.85 / 1.08 | 0.89 / 1.18 | +5% |
| ebonheart_council | 1.62 / 1.98 | 1.28 / 1.67 | −21% |
| pelagiad_night_fog | 1.54 / 2.01 | 1.75 / 2.84 | +14% |
| sadrith_mora_mushroom | 1.93 / 2.44 | 2.12 / 2.90 | +10% |
| seyda_neen_coast | 2.12 / 2.49 | 2.23 / 3.08 | +5% |

**Against the budget** (CPU at most 10% slower than OSG; GPU may rise):

- **Within budget:** interiors, Seyda Neen and Sadrith Mora.
- **Over budget:** the Ald-ruhn and Balmora exteriors (about +50%) and Pelagiad (+14%).

**Caution:** every scene runs at 450–1500 frames per second, so per-frame fixed costs dominate and these numbers say little about heavy scenes. A heavier test should come first: a long view over Balmora or Vivec with many NPCs, and a scripted walk. It needs camera positions in the scene list (section 3).

**Likely places for the exterior cost** (not yet profiled):

- the terrain paging traversal, which runs every frame
- vsgopenmw's per-frame scene data update
- the record traversal over the larger exterior scene graph

## 5. Playthrough checklist

Not run as a whole yet. The automated runs cover:

- starting in exterior and interior cells, with daylight, fog and ashstorm weather
- the Lua tests' walking, running, diagonal walking, rotation, teleport, save and load, crime, and navigation queries
- a weapon attack on another actor

The interactive items still need a person at the keyboard: new game through the census office, swimming, levitation, doors, silt strider and boat travel, ranged and magic combat over water, dialogue, barter, inventory, maps, journal, rest, and day and night.

## 6. Decisions needed from toddhow

1. A verdict per scene in section 3.
2. Whether the exterior CPU cost in section 4 blocks V2, or becomes a profiling task to run with V4 (renderer parity) once heavier test scenes exist.
3. Who runs the interactive playthrough items in section 5, and when.
