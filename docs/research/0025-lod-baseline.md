# 0025 - LOD and streaming baseline: what the engine does today with a large view

Date: 2026-11-02 · Task 0054 (S0) · Status: measurement. Every number below is from a run on this machine on this date; what was not measured is listed in section 8.

## 1. Machine and method

- **Machine:** Windows 11, Intel Core i7-13700K, 31.7 GiB RAM, NVIDIA GeForce RTX 4070 (12 GB), driver 617.42 (device Vulkan 1.4.351), `build-jolt` Release built from `c8723f39c1` plus the S0 changes (device report entries, `[Rendering] frame counters`). The machine is shared with other sessions, so treat differences of a few percent as noise.
- **Game data:** the user's Morrowind with Tribunal and Bloodmoon (`openmw.cfg` copied, never edited; `Morrowind.esm`, `Tribunal.esm`, `Bloodmoon.esm`, no other plugins, **no groundcover**). Settings per run were written fresh: 1280 x 720 windowed, vsync off, frame limit off, shadows off, `[Rendering] gpu timing` and `frame counters` on, no HDR or other OpenEden features.
- **Scenes:** the `heavy_lod_*` lines of `tools/render_scenes.txt`, run by `tools/benchmark.sh`. The "air" scenes place the camera 3000 units above Balmora (`tools/benchmark_data/balmora_view.txt`, pitch 12 degrees down, heading 0); the "ground" scenes start where `--start Balmora` puts the player. 400 frames at the end of each 600-frame run are measured (the earlier ones are loading). View distance is `[Camera] viewing distance`; "distant" means `[Terrain] distant terrain` and `object paging` both on.
- **Counters** (new, `[Rendering] frame counters`, extra columns `ctr_*` of the frame stats file): `rss_mib` is the process working set; `gpu_used_mib` is the device-local memory this process uses, from `VK_EXT_memory_budget`; `cells` is loaded exterior cells; `scene_draws` and `scene_triangles` come from a **census of the scene graph with no culling**: an upper bound of what a frame submits, not a count of real draw commands. This engine cannot count issued draw commands (VSG's record traversal is not hookable and no capture layer is installed); the census sees the VSG draw nodes (`VertexIndexDraw`, `VertexDraw`, `Geometry` commands, `Draw`, `DrawIndexed`), not instanced or indirect draws. It is refreshed every 30 frames.

## 2. Draw commands, triangles, memory (the long view)

| Scene | View | Distant | Census draws | Census triangles | RSS MiB | GPU MiB | Cells |
| --- | --- | --- | --- | --- | --- | --- | --- |
| air, 3000 up | 7168 | off | 6,978 | 410,970 | 723 | 219 | 9 |
| air | 7168 | on | 6,978 | 410,970 | 732 | 219 | 9 |
| air | 15000 | on | 12,469 | 889,295 | 749 | 220 | 9 |
| air | 30000 | off | 7,033 | 737,882 | 751 | 235 | 9 |
| air | 30000 | on | 33,823 | 2,416,310 | 793 | 252 | 9 |
| ground (Balmora start) | 7168 | off | 6,548 | 459,719 | 716 | 219 | 9 |
| ground | 15000 | on | 10,524 | 915,163 | 757 | 252 | 9 |

- At the default 7168 view distance, distant terrain and object paging change nothing (identical census): the loaded cells already reach the fog distance. They only matter beyond it.
- At 30000 units with distant terrain and object paging the scene holds about **4.8 times the draw commands** (33.8k against 7.0k) and 5.9 times the triangles of the 7168 view. The census is an upper bound; the true number depends on culling.
- Memory is modest: the process holds 716 to 793 MiB, and 219 to 252 MiB of device memory is in use by this process, in all scenes. The distant terrain and objects at 30000 add about 40 to 60 MiB on the host and 33 MiB on the device against the same view without them.

## 3. Frame time

| Scene | View, distant | Wall ms median / p95 / p99 | Frames over 2 x median / over 25 ms | GPU main scene ms median |
| --- | --- | --- | --- | --- |
| air | 7168, off | 2.32 / 3.27 / 3.84 | 0 / 0 | 0.069 |
| air | 7168, on | 2.24 / 2.94 / 3.16 | 0 / 0 | 0.069 |
| air | 15000, on | 2.55 / 3.38 / 3.79 | 0 / 0 | 0.178 |
| air | 30000, off | 2.42 / 3.24 / 3.55 | 0 / 0 | 0.180 |
| air | 30000, on | **3.87 / 4.86 / 5.20** | 1 / 0 | 0.347 |
| ground | 7168, off | 3.10 / 4.01 / 4.50 | 0 / 0 | 0.064 |
| ground | 15000, on | 2.77 / 3.97 / 4.58 | 1 / 0 | 0.088 |

- The table is a second run, made after a fix: the first run charged the census (every 30 frames) to the next frame, which raised p99 by up to 1.2 ms (for example 6.40 against 5.20 at 30000 distant); medians moved by 0.04 ms or less except where noted by noise (15000: 2.88 then 2.55).
- **Wall time is CPU time:** the GPU's main pass takes 0.35 ms at the worst scene and the whole frame 3.9 ms, and the wait on the GPU is about 0.1 ms (`wait_ms`, as in note 0009). A 30000 view costs about 1.6 ms of CPU a frame more than a 7168 view (3.87 against 2.24), in step with the draw census.
- On the way to the horizon the frame time is not the problem on this machine; what a weaker CPU or GPU would do is not measured here (section 6).

## 4. Hitches along a flight (cell loads)

`heavy_lod_flight_*`: collision off, 1500 units up, the player's Athletics raised to 1500 so that it crosses cell borders in seconds (`tools/benchmark_data/fly_fast.txt`), the walk script turning every 4 seconds, 15000 view.

| Run | Cell loads seen | Frames over 25 ms | Worst frame | p99 / p99.9 of frames after loading |
| --- | --- | --- | --- | --- |
| nothing distant | 18 borders crossed (54 cells loaded) | 11 | 110.0 ms | 4.68 / 12.85 ms |
| distant terrain and object paging | 3 borders crossed, then the game hung (section 5) | 2 (29.1 ms, 99.4 ms) | 99.4 ms | 3.80 / 5.30 ms (first 5,228 frames only) |

- **Every frame over 25 ms after the first loading screens sits on a rise of `ctr_cells_loaded_total`**, one burst of three cells per border crossing (the flight crosses a border on its way into new cells; a border adds 3 cells). Border crossings cost from under 25 up to 110 ms of one frame; in the "nothing distant" run 11 of 18 crossings were over 25 ms (27.3, 110.0, 32.0, 51.8, 43.7, 74.3, 27.5, 25.8, 27.6, 60.1, 27.7 ms). The hitch cannot be pinned to a part of the load (no timestamps inside it); S6 adds them.
- Median frame time during the flight is 2.0 to 2.1 ms: the stalls are rare and large, not a general slowdown.
- The loading screens at the start of every run (the first 600 or so frames of the file) show wall times of 160 to 625 ms; they are not counted above.

## 5. A hang found by the measurement (not caused by it, as far as I can tell)

`heavy_lod_flight_on` (distant terrain and object paging on, the fast flight) **stopped responding at the fourth border crossing in three of three runs**: twice with `frame counters` on and once with it off, so the counters are not the cause. The log stops growing (no further lines for minutes), the process stays alive with no CPU use (19.0 s of CPU at the first sample, 19.1 s 20 s later), and the frame file stops at about 8,230 to 8,330 frames. The same flight with nothing distant ran to the end (22,300 frames, 63 cells loaded). Two minidumps of the hung process are in the session scratchpad (`s0hang.dmp`, `s0hang2.dmp`). The thread stacks, symbolised against `build-jolt`'s PDBs with the usual dbghelp walker, show many threads waiting on condition variables, one inside `vsg::IntrusiveAllocator::deallocate` (reported as `IntrusiveAllocator.cpp:995`) and one inside `vsg::CommandGraph::record` (`CommandGraph.cpp:140`); most frames are unsymbolised and I did not find which thread holds what. It looks like a deadlock between a thread that frees VSG objects and recording or compiling threads. It was provoked by a speed the real game cannot reach (Athletics 1500, collision off), so it may not affect play, but it is a hang and should be looked at before the tile system adds more asynchronous scene changes.

## 6. A weaker device: software Vulkan (decision 0010, item 27)

The Linux image's Mesa software device (llvmpipe, LLVM 20.1.2, Mesa 25.2.8) under `xvfb`, in a container limited to 12 GiB and 8 CPUs, running the existing Linux build in the work volume (revision `2dc039ff12`, which has the device report of task 0023 but **not** this task's new keys or counters). 640 x 360, Balmora, the same 3000-up pose, 30 running frames.

| Run | Settings | Result | Peak process RSS |
| --- | --- | --- | --- |
| default 7168 view | none | **runs**, screenshot taken, GPU timing works (`timestamps` present, 64 valid bits, 1 ns) | 794 MiB |
| weak caps | `texture max size = 512`, 7168 view, distant terrain and object paging off | runs, log says `Texture cap: objects 512, terrain 512 texels` | 687 MiB |
| 30000 view | distant terrain and object paging on | runs | 849 MiB |

- **It works.** The engine starts, loads Morrowind and renders Balmora on a CPU-only Vulkan device. Frame times (median about 23 ms for the default scene in the last frames, 47 ms for the 30000 view) say nothing about a GPU and are not used. Memory is the point: the texture cap lowers the resident set by 107 MiB (13%), and the long view adds 55 MiB.
- **Not run:** `texture memory budget = 2048` (this older Linux build knows only `off` and `auto`; the MiB form came later, so the log says "unknown '2048', using off").
- The Linux binary does not have the new device report keys, so I read them from `vulkaninfo` for the same device: llvmpipe advertises **one** queue family (graphics, compute, transfer and sparse binding, one queue), so there is no async compute or transfer; sparse binding and sparse residency (2D, buffer, aliased) true, multi-draw indirect and draw-indirect-first-instance true, pipeline statistics queries true, mesh and task shaders true (Mesa 25.2), `VK_EXT_memory_budget` and `VK_EXT_memory_priority` present, `maxImageDimension2D` 16384 (the RTX 4070: 32768), `maxStorageBufferRange` 134,217,728 bytes (the RTX: 4,294,967,295), subgroup size 8 (RTX: 32), driver Mesa llvmpipe. **A software device claims more than a small real GPU has**, so it is a test of "runs at all" and of memory behaviour, not of what a low-end card lacks; the real floor needs a real report from a real weak card.

## 7. Pops and cut-offs, as frames

The engine takes one screenshot a run, so a walk was done as eight poses 1024 units apart along the view direction (3000 units up, 30000 view), each with and without distant terrain and object paging (`pops` files in the scratchpad), with a frame difference taken in PowerShell (`System.Drawing`).

- **The cut-off of distant objects is large and obvious.** At the first pose, with `object paging` and `distant terrain` off, the horizon holds terrain only: no towers (the tall tower complex on the ridge at the centre of the picture is simply gone), no rocks, no trees on the far hills; with them on, all are there. The difference image is white along the whole horizon band (about 2.7% of the pixels differ by more than 16 of 255; by band, rows 180 to 450 of the 720 hold nearly all of it). Without object paging the world beyond about the loaded cells is a bare landscape; with it, a populated one. At the last pose, the same: the tower complex and the far rocks are absent without paging, present with it.
- **Terrain reaches the horizon either way** in these frames (far mountains are in both); at 30000 units without `distant terrain` the loaded 3 x 3 cells already reach 20000 units or more from a camera standing near a cell edge, so the cut line of the terrain itself was not in frame. (I did not measure how far the loaded terrain reaches.)
- **I could not separate level-of-detail swaps from camera motion in this data.** Consecutive poses 1024 units apart differ in 20 to 45% of their pixels (parallax of the whole scene), so a swap hidden in that is invisible to a difference. Looking at the frames themselves (poses 0, 3, 4 and 7 and their difference images), I saw no terrain pop at that step size, but a step this large cannot show one. The method that can is the forced-level comparison of research note 0018, section 5.3 (the same pose with a level on and off), which needs S1's viewer; spec 0055 asks for it.
- **Grass was not looked at:** the game has no groundcover without a mod and none is installed (decision 0010, item 28: wait).

## 8. What was not measured

| Item (note 0018, section 5) | Status |
| --- | --- |
| Draw commands actually issued | Not possible with this engine; the census is an upper bound (section 1) |
| CPU time split: update, cull, record, submit | Not measured; no scopes exist for it |
| Terrain batch build time (`Builder::createBatch`), distant object chunk build time | Not measured; no logging added (S0 changes no behaviour) |
| Disk reads per second, cold and warm caches | Not measured |
| Resource cache statistics (`components/resource/cachestats.*`) | Not read |
| Compile cost on the main thread per chunk | Not measured |
| Grass: instances and draws at 1500, 3000 and 6144 units, the cut-off line | Not measured: no groundcover mod installed or chosen |
| A fast straight flight at 2000 units a second for 60 s with collision off, view sweeps, ten border crossings at walking speed | Replaced by the flight in section 4 (about 18 crossings at an unrealistic speed); a walking-speed crossing run is not done because the walk script from the Balmora start climbs a hill and does not cross a border |
| A second weak real GPU | Not available; emulated (section 6) |
| Frame time and memory at view distance 30000 at 1440p or on a weaker CPU | Not run |
| Texture and mesh memory split (GPU memory by kind) | Only the totals above |

## 9. What this says for the next stages

- **Tiles must reduce draw commands.** The long view costs 4.8 times the commands for a ground that is mostly static, and the frame is CPU-bound (spec 0055). Terrain tiles and later object clusters are the lever; GPU culling (S9) is not needed to fix this machine, but it matters for weaker CPUs.
- **Distant objects are what the eye misses** (section 7), so S4 (object clusters) is the visible win and S1 (terrain tiles) the structural one.
- **A hitch of up to 110 ms at each border crossing** is the streaming problem (S6); S1 must read tiles off the main thread so as not to add to it.
- **The hang in section 5** should be understood before more asynchronous loading is added.
- **A real weak-GPU report is still needed** for tier floors (design doc, section 8); the emulation proves the engine runs on a software device and gives memory behaviour only.

## 10. Reproducing

```
# Counters and the device report (the keys listed in task 0054)
BENCH_FILTER=heavy_lod BENCH_RENDERING='frame counters = true' tools/benchmark.sh
# a report only
[Rendering] device report = true   (device-report.json in the user data folder)
```
The run files used for this note (frame stats files, logs, screenshots, the difference images and the two minidumps) are in the session scratchpad and are not committed.
