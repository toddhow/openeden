# Task: World LOD and streaming, stage S0 (measure)
Design doc section: `docs/design/world-lod-and-streaming.md`, section 11 (staging table, row S0) and section 8 (graphics tiers); the measurement plan is `docs/research/0018-world-lod-and-streaming.md`, section 5. Decisions already taken: `docs/decisions/0010-rendering-and-studio-answers-2026-11-01.md` (item 24: the container track first, done; item 27: the weaker GPU is emulated, not bought; item 28: no groundcover mod chosen yet) and the design doc's section 14.
Goal: put numbers on what the engine does today with a large view, streaming and grass (draw counts, triangles, memory, frame time and hitches, the visible pops), and record what a weak machine would lack, so that the thresholds and budgets of stages S1 onward are set from measurements and not from guesses.
Inherited code touched: `components/render/devicereport.*` (`collectDeviceReport`, added in task 0023), `components/render/engine.*` (`Engine::loop`, the frame stats file), `apps/openmw/mwrender/rendermanager.*` (counters registered only when asked), `components/settings/categories/rendering.hpp`, `files/settings-default.cfg`, `tools/render_scenes.txt`, `tools/benchmark.sh` (only if a scene option needs it). At commit `c8723f39c1`.
Inputs / outputs: new `[Rendering]` setting `frame counters` (bool, false) that adds columns to the file named by `OPENEDEN_FRAME_STATS_FILE`; new device report entries; new `heavy_lod_*` scenes; `docs/research/0025-lod-baseline.md` (the numbers); frame sequences stay in the scratchpad (not committed).

Status: started 2026-11-02 at toddhow's word (the LOD and streaming track). Full loop for code (renderer); the document parts are plain writing.

## Why this stage exists, in plain language

Before we build tiles, caches and a streaming system, we need to know what is slow or ugly today and by how much. "The long view is CPU bound" (note 0009) is one fact; we do not yet know how many draw commands and triangles a 30000-unit view holds, how much memory it uses, how many frames hitch while walking across cell borders, or how far away objects and grass visibly pop. Those numbers set the pixel-error threshold of S1, the memory budgets of S6 and the grass bands of S7. This stage changes no behaviour: it adds a ruler.

## Design

1. **Device report extension.** New entries, each `present`, `absent` or `unknown (...)` as before, never throwing:
   - queue families: how many, each one's capability flags and queue count; whether a compute-only family and a transfer-only family exist (async compute and async transfer need them); whether any family supports sparse binding;
   - `ext.memory_budget` (`VK_EXT_memory_budget`) and `ext.memory_priority`, and, when the budget extension is present, the budget and usage of each heap at the time of the report;
   - `feature.sparseBinding`, `feature.sparseResidencyImage2D`, `feature.sparseResidencyBuffer`, `feature.sparseResidencyAliased`;
   - `ext.mesh_shader` and the features `meshShader` and `taskShader`;
   - `feature.multiDrawIndirect`, `feature.drawIndirectFirstInstance`, `feature.pipelineStatisticsQuery`;
   - limits the tiers need: `limit.maxMemoryAllocationCount`, `limit.maxImageDimension2D`, `limit.maxStorageBufferRange`.
   The pure parts (turning queue family properties and heap numbers into entries) are plain functions with unit tests that need no GPU; the probe code that asks the driver is a thin layer over them.
2. **Counters behind `[Rendering] frame counters`** (off by default; when off the stats file is byte for byte as before and nothing below runs): extra columns `ctr_<name>` in the frame stats file: `rss_mib` (the process's working set), `gpu_used_mib` (device-local usage from `VK_EXT_memory_budget` when the device has it, else -1), `cells` (loaded exterior cells, from `RenderManager::exteriorCellLoaded/Unloaded`), `cells_loaded_total` (cumulative, so a hitch can be matched to a cell load), `scene_draws` and `scene_triangles`. **This engine cannot count the draw commands that are really issued** (VSG's record traversal is not overridable and no capture layer is installed), so the last two come from a census of the scene graph (`Render::SceneCensus`, a visitor over the scene root): the draw commands and triangles of every node that is reachable and not switched off, with no frustum culling. It is an upper bound of what is submitted and the note names it so. It runs every 30 frames and the value is carried between runs, to keep its cost off the frame.
3. **Scenes.** `heavy_lod_*` lines in `tools/render_scenes.txt`, run by `tools/benchmark.sh`: a long view over Balmora at view distance 7168, 15000 and 30000, with `[Terrain] distant terrain` and `object paging` off and on, a walk across cell borders, a 360 degree sweep.
4. **Pops** are measured as stepped camera poses (the engine takes one screenshot per run, so a walk is a set of runs, each at a pose a fixed step further on); the frames are looked at, and written down in words with distances.
5. **A weaker GPU, emulated** (decision 0010, item 27): the Linux image's Mesa lavapipe software Vulkan device under `xvfb`, plus settings that cap what a small card would have (`texture max size`, `texture memory budget` in MiB, view distance, object paging). Frame times from a software device mean nothing; what is recorded is whether the game runs, what the device report says a software device lacks, and how memory behaves under the caps.

### Decisions (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | Draw calls | A scene-graph census, named an upper bound | A patched VSG with a counter (a fork of a dependency); a Vulkan layer (none installed) |
| D2 | Counters in the existing stats file | Extra columns, only with the new setting | A second file |
| D3 | Pops | Stepped poses, one run each, looked at | A multi-screenshot option in the engine (a behaviour addition; propose it for S1 if the poses prove too coarse) |
| D4 | Groundcover | Not measured with a mod (none chosen, decision 0010 item 28); vanilla has none, so the grass baseline is zero and spec 0056 designs the rest | Pick a mod now |
| D5 | Second GPU | Emulate (decision 0010 item 27) | Buy |

## Constraints
- Determinism: nothing here touches simulation. The census runs on the main thread between frames.
- Allocation: the census visitor allocates nothing per node; the counter columns write into the `ofstream` the loop already has. Counters off: no code runs.
- Threading: counters are read on the main thread; `cells` uses atomics because cell loads come from a worker.
- Off by default: `frame counters = false`. Device report entries appear only when `device report` is on, as before.
- New files under `components/` use lowercase letters and digits only.
- No game assets in the repository. Frames and raw logs stay in the scratchpad; the note holds numbers and words.

Acceptance criteria:
- [x] Unit tests (no GPU, window or data): queue family summarising (graphics-only, compute-only, transfer-only, sparse binding), heap budget text, the report JSON round-trips byte for byte with the new keys, the real probe reports every new key, the scene census counts a hand-built graph (draw nodes, indexed triangles times instances, a switched-off child, nested groups), the working-set parser, the frame counters' header and line writer with and without counters.
- [x] With `frame counters` off, the stats file header and lines are exactly as before (test of the writer's column logic).
- [x] `docs/research/0025-lod-baseline.md` names the machine (RTX 4070, i7-13700K), the commit and the settings, and has tables for: draw commands and triangles (census), memory, frame time median/p95/p99 and counts over 2 x median and over 25 ms per scene, cell loads and the hitches near them, view distance and distant terrain and object paging on/off, and the pops in words with distances.
- [x] The weaker GPU emulation run is reported (works or not, the software device's report, memory under caps).
- [x] With every new setting at its default the game is unchanged (no code path changes: with `frame counters` off nothing is registered and the stats file writer adds only empty text).
- [x] A fresh reviewer read the code (fixed: number precision, census cost charged to the next frame, `memory.budget` unknown case, `stoull` throw); components-tests, openmw-tests, eden-studio-tests and the 32 Lua tests pass on Windows; clang-format 14, `CI/check_file_names.sh`, `check_extension_sdk.sh`, `check_no_osg.sh` pass.
Out of scope: any LOD or streaming change, a GPU-side counter of real draw calls, groundcover mod selection, a New Vegas census (note 0018, 5.7), the Linux gate (toddhow runs it before pushes), a pop-detecting tool inside the engine.

## Stage log

### 2026-11-02, built and measured (Windows, RTX 4070)
- Built: device report keys (queue families, memory budget, sparse, mesh shader, multiDrawIndirect, limits), `[Rendering] frame counters` (rss, device memory in use, cells, cell loads, scene census), `heavy_lod_*` scenes and `fly_fast.txt`. Numbers and what was not measured: `docs/research/0025-lod-baseline.md`.
- Verified: components-tests 4050, openmw-tests 830, eden-studio-tests 520 pass; 32 Lua integration tests pass; `CI/check_file_names.sh`, `check_extension_sdk.sh`, `check_no_osg.sh` pass; clang-format 14 (Docker) clean on every changed C++ file. The Linux gate was not run (toddhow runs it before pushes); the weak-GPU run used the Linux build already in the work volume.
- **Found:** the flight with distant terrain and object paging on hangs the game at the fourth border crossing, three runs of three (research note section 5; minidumps in the scratchpad). Not fixed (S0 changes no behaviour).
- **Found:** a reviewer's point that I fixed: the census cost was charged to the next frame's wall time every 30 frames; the frame table was re-measured after the fix.
- Not done: the stepped-pose pops could not separate LOD swaps from camera motion (spec 0055 asks for the forced-level comparison); draw commands actually issued cannot be counted; no groundcover; no real second GPU.
