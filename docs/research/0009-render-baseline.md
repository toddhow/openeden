# 0009 — Render baseline: GPU time per pass, and what this machine's Vulkan supports

Date: 2026-11-01 · Task 0023 (F0a, F0b, F0c) · Status: measurement. Nothing in the game was changed by taking it (every new setting was at its default except `gpu timing`, which only reads timestamps).

## 1. Machine and method

- **GPU:** NVIDIA GeForce RTX 4070 (12 GB), driver 617.42 (Vulkan 1.4.351 on the device, loader 1.4.341), Windows 11, MSVC build of `vulkan` at the commit that carries this note, `build-jolt`, Release.
- **Window:** 1280 x 720, windowed, vsync off, frame limit off, shadows off, no post-processing, no MSAA, `gpu timing = true`.
- **Scenes:** `tools/render_scenes.txt`, the lines named `heavy_*`, run by `tools/benchmark.sh`. 400 frames at the end of each run (earlier frames are loading screens); median and 95th percentile (nearest rank). Times are milliseconds.
- **What the numbers are:** `wall_ms` is the frame's wall-clock time (`OPENEDEN_FRAME_STATS_FILE`), `wait_ms` the part of it spent waiting on the swapchain or fence. The `gpu_*` columns are the GPU's own time between the start and the end of a pass, from `VK_QUERY_TYPE_TIMESTAMP` queries (top of pipe to bottom of pipe), read three frames later. Passes overlap and the GPU can work on one while another waits, so the `gpu_*` columns do not add up to a frame's GPU time. They are a per-pass cost, to compare before and after a change.

## 2. Baseline

| Scene | wall_ms median / p95 | wait_ms | main scene | reflection and depth copy | compute | gui | gui render textures |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `heavy_balmora_longview` (3000 units up, 30000 view distance, distant terrain, object paging) | 3.759 / 4.610 | 0.150 | 0.274 / 0.279 | 0.013 / 0.014 | 0.023 / 0.028 | 0.005 / 0.006 | 0.003 / 0.003 |
| `heavy_aldruhn_longview` (same, from the Ald-ruhn cell's corner) | 2.173 / 2.902 | 0.121 | 0.120 / 0.157 | 0.010 / 0.012 | 0.008 / 0.011 | 0.004 / 0.005 | 0.002 / 0.003 |
| `heavy_ebonheart_crowd` (interior, many NPCs) | 1.383 / 1.890 | 0.096 | 0.020 / 0.021 | not drawn | 0.018 / 0.023 | 0.004 / 0.005 | 0.002 / 0.003 |
| `heavy_balmora_walk` (flies north over the town for 3,000 frames, view distance 15000) | 2.028 / 2.717 | 0.107 | 0.138 / 0.144 | 0.015 / 0.016 | 0.023 / 0.028 | 0.005 / 0.006 | 0.003 / 0.004 |

Full table: the output of `tools/benchmark.sh` on that day (kept in the task's stage log).

## 3. What it says

- **The GPU is not the limit in any of these.** The main scene takes 0.02 to 0.27 ms on the GPU while a frame takes 1.4 to 3.8 ms of wall time, and the CPU waits on the GPU for only 0.1 to 0.15 ms of that. The frame is CPU time: recording, culling, animation, physics. That agrees with research note 0004 (section 4) and with note 0008's remark that the exterior overrun cannot be told from new GPU cost without these numbers: now it can.
- **So a new GPU feature has room to spend on this machine, and none in the CPU.** An effect that costs 1 ms of GPU would still leave the frame CPU bound at the slowest scene (3.8 ms), but a feature that adds draw calls, passes with new command recording, or per-frame CPU work lands directly on the number that is already over budget.
- **This is one GPU, a fast one, at 720p.** A mid-range card or 1440p would move the GPU numbers up several times. The floor question (section 5) needs at least one slower device; the benchmark and the device report are the tools for that, and a run on another machine is the next step.
- **Caveats:** the walk moves the player with `tcl` (collision off) so that it cannot be stopped by a wall, and it covers only a few hundred units; it streams little. The Vivec plaza (an interior) was not a long view, so the long views are Balmora and Ald-ruhn. The Ebonheart interior's main-scene time (0.02 ms) is small because it is a small room.

## 4. Device report (F0c)

Written by `[Rendering] device report = true` to the log and to `device-report.json` in the user data folder. The probe makes its own instance, so it is not limited by what the game asked for. On this machine:

- **API and driver.** Device Vulkan 1.4.351, driver NVIDIA 617.42, discrete GPU, 12,226 MiB of device-local memory.
- **The game's instance is Vulkan 1.4, not 1.0.** `vsg::WindowTraits`' constructor asks the loader for its version and VSG creates the instance at that version (`src/vsg/app/WindowTraits.cpp`, `vkEnumerateInstanceVersion(&vulkanVersion)`; `Window.cpp`, `Instance::create(..., _traits->vulkanVersion)`), and the report's `instance.requestedByGame` says 1.4.341. Research note 0008, sections 2 and 4, and research note 0005 call the instance Vulkan 1.0; that was the default of the field, not what runs. **Raising the instance version is therefore not a decision to make: it is already as high as the loader goes.** What stays at Vulkan 1.0 level is what the game enables on the device: it requests only `samplerAnisotropy`, `occlusionQueryPrecise`, `depthClamp` and `fillModeNonSolid`, plus `create_renderpass2` and friends with antialiasing (`apps/openmw/vsgengine.cpp`). The floor is the set of features and extensions that OpenEden will require the device to have.
- **Items named by note 0008:**

| Item | This machine |
| --- | --- |
| Timestamps | present; 64 valid bits, 1 ns per tick, compute and graphics |
| Subgroup operations | size 32; stages vertex, tessellation, geometry, fragment, compute; basic, vote, arithmetic, ballot, shuffle, shuffle relative, clustered, quad |
| Descriptor indexing | present (core 1.2); runtime arrays, partially bound, variable count, non-uniform sampled image indexing all present |
| 16-bit and float16 | 16-bit storage (buffer, uniform and buffer, push constant) present; `shaderFloat16`, `shaderInt8`, `shaderInt16` present |
| Push descriptors | present (extension) |
| Multiview | present (core 1.1) |
| Ray query and acceleration structures | both present; features `rayQuery` and `accelerationStructure` present |
| HDR colour spaces | `VK_EXT_swapchain_colorspace` present; the window's surface offers `srgb_nonlinear`, `extended_srgb_linear` and `hdr10_st2084`, and the `R16G16B16A16_SFLOAT` format |
| Depth-stencil resolve, create_renderpass2 | present (core 1.2) |
| Also | host query reset, timeline semaphore, buffer device address, draw indirect count present |

## 5. Proposal on the Vulkan floor (for toddhow; nothing is changed)

One machine cannot set a floor; a report from a weaker one can. What this machine and Vulkan itself say:

- **Vulkan 1.1 as the floor costs almost nothing and buys the most.** It makes subgroup operations, multiview, 16-bit storage and `vkGetPhysicalDeviceFeatures2` part of the core, and every Vulkan device from 2018 onward has a 1.1 driver (note 0008 names subgroup operations for the compute passes as the example). Because the instance is already created at the loader's version, the change is not to the instance but to what OpenEden requires of a device and checks at start.
- **Vulkan 1.2 would add** descriptor indexing, `create_renderpass2`, depth-stencil resolve (all three used or wanted already), timeline semaphores and 8/16-bit arithmetic. It excludes drivers older than about 2020, which still includes a good number of integrated GPUs and macOS through MoltenVK (open question in the guide).
- **Optional tiers, not a floor:** ray query and acceleration structures (device-gated, note 0008 R3), HDR10 output, `shaderFloat16`.
- **Suggestion:** require 1.1, treat everything above it as an optional tier chosen by the device report at start, and decide on 1.2 once a second, weaker machine has produced a device report. The report writes the same JSON everywhere, so comparing machines is a diff.

## 6. Reproducing

```
tools/benchmark.sh build-jolt/openmw.exe            # all heavy scenes, prints the table
BENCH_FILTER=balmora tools/benchmark.sh ...         # a subset
# device report: set  [Rendering] device report = true  and start the game once
```
The benchmark needs a Morrowind install that the user's `openmw.cfg` points at (set `OPENMW_CFG_DIR` to use another).

## 7. F3 cost: the stored G-buffer (task 0026)

Same machine and method as section 1; `tools/benchmark.sh` with `BENCH_RENDERING='hdr = true'` against `'hdr = true\ngbuffer = true'`.

| Scene | `gpu_main_scene` median, `hdr` | with `gbuffer` (normals and linear depth, stage a) |
| --- | --- | --- |
| `heavy_balmora_longview`, two runs of `hdr`, first run of `gbuffer` (stage a: two outputs) | 0.328 and 0.335 ms | 0.362 ms |

- **Reading:** the two extra outputs cost about 0.03 ms of GPU time (10% of the main pass) at 1280 x 720 on this GPU, and the frame is CPU-bound as before (note 0009 section 3). A second `gbuffer` run was disturbed by another process building on the machine and read 0.469 ms; it is not used. Stage b: the velocity pass is a fullscreen triangle and takes 0.019 ms (`gpu_velocity`, `heavy_balmora_walk`). Stage c (all three extra outputs, `heavy_balmora_longview`): `hdr` 0.318 and 0.349 ms, `hdr` + `gbuffer` 0.375 and 0.373 ms, so the three outputs cost about 0.03 to 0.06 ms (10 to 17% of the main pass), and `gpu_velocity` is 0.015 ms; wall-clock frame times did not move (3.5 to 3.9 ms, CPU-bound).
- The default path and the HDR path without `gbuffer` have no extra attachment, pass, image or define, and their frames match the build before the stage (task 0026, stage a log).
