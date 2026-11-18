# Task 0322: world LOD refresh check (baked far terrain tiles and object clusters, Morrowind and Skyrim SE)

Related: tasks 0055, 0059, 0066, 0087, 0088, 0133 (stages 1 to 3), the rotation fix of task 0151.
Status: checked on current main (`3afdbe0d9a`, rebased onto `c14f282175`; no LOD, baker, renderer-LOD or world-view file differs between the two); nothing was found broken, so no code changed. Same hand ran everything; no fresh reviewer.

Goal: after the renderer and the Skyrim pack changed a great deal since 2026-11-12, show that both games still bake and draw their LOD correctly.

## 1. Bakes

| Pack | Now | Recorded (task) | Same? |
| --- | --- | --- | --- |
| Morrowind terrain (`--bake-lod-terrain`) | SHA-256 `e46ba2bacd362ed1a2a3b738...`, 51,973,008 bytes, 36 s | `e46ba2ba...` | yes |
| Morrowind objects (`--bake-lod-objects`) | `ab774e94f3a7b6cd62a00218...`, 186,685,856 bytes, 47 s | `ab774e94f3a7b6cd...`, 497 clusters, 186,685,856 | yes (byte for byte) |
| Skyrim WhiterunWorld terrain | 49,991,192 bytes, 3,908 tiles (2,880 / 744 / 208 / 56 / 16 / 4), 189 s | 49,991,192 bytes (stage 3) | yes (size) |
| Skyrim WhiterunWorld objects | 1,037,218,448 bytes, **2,496 clusters** (1,807 / 505 / 141 / 43), 264 s | 1,043,666,264 bytes, 2,496 clusters | clusters equal, pack 6.4 MB (0.6 percent) smaller |

The smaller Skyrim object pack is explained by data, not by the baker: the bake sees 264,064 references (stage 2: 263,654) and 210,087 statics and trees (211,449), and **3,705 initially disabled (stage 2: 1,253)**. Task 0293 folds enable parents into a placement's state (garrisons, camps and other enable-parented groups start disabled), so about 2,450 more placements are correctly left out of the clusters. (This is the likely cause: the bake log line was compared with stage 2's, the placements themselves were not diffed.)

Harness trap found: `tools/neutral_session_check/check.sh ... lodbake|lodobjbake` with a small frame count (I first used 400) quits the game before the pack write on the worker finishes: the log shows "3908 tiles in all" and no `.edenpak` exists. Use at least 60,000 frames.

## 2. Flights (LOD, map view and object clusters on, haze on, tier auto = high)

Morrowind (`mwposes`: a Lua pose script, static camera, one picture per pose; Balmora start, positions relative to it, Red Mountain absolute). 12 frames viewed (contact sheets in `keep`): ground, 5,000, 12,000 and 25,000 up over Balmora, lava at 25,000 east, Red Mountain at 8,500. The horizon fills with tiles and clusters under the haze, the lava strips and towers draw, no hole, crack, wrong colour or pop was seen. Two ground poses put the camera inside rock (the start is on a slope; the pose script's fault, not the LOD's). Stats over the run (47,457 frames from 3,000): median 4.54 ms, p99 9.52, p99.9 22.2, resident 1,856 MiB, device 559 MiB; clusters 73 shown, about 374 MiB held, 3,278 cluster draws. No stale-pack or refusal line in the log. (Not comparable with the earlier 25,000-unit pose numbers: this run includes teleports and ground poses.)

Skyrim WhiterunWorld (`lodobjview`, the stage 3 walk and poses: Whiterun's plain north, down, east, west, south at ground level, and 9,000 up north and east). 7 frames viewed for one run (on), the other runs' frames not all viewed:
- Dragonsreach, houses, the far forest and the mountains stand upright and where the land puts them (the 0151 rotation is right in the clusters); far snow-covered trees and rocks fill the band 10,000 to 25,000 units out; the mountains come from the baked tiles.
- Compared with the stage 1 frames (`skylod\shots_on`, tiles only, magenta flora) the ground patchwork (one base texture per quadrant, `neutral terrain layers` off) is identical in shape and no seam or lighting mismatch between tile ground and live terrain is visible.
- Seven stills a run cannot show a fade or a pop, so none was looked for in motion.

| Run | median ms | p99 | p99.9 | frames over 16.7 ms | worst ms | resident MiB | device MiB |
| --- | --- | --- | --- | --- | --- | --- | --- |
| clusters on, run 1 | 4.11 | 7.83 | 28.6 | 180 | 2,380 (cold first run) | 4,354 | 1,420 |
| clusters on, run 2 | 4.11 | 7.86 | 28.6 | 154 | 51 | 4,969 | 1,420 |
| clusters on, run 3 | 4.00 | 7.25 | 32.3 | 160 | 631 | 5,019 | 1,436 |
| tiles only (off) | 3.64 | 5.63 | 17.8 | 50 | 35 | 3,267 | 748 |
| stage 3 on / off | 3.46 / 3.41 | 5.42 / 5.05 | 22.2 / 15.4 | 112 / 7 | | 5,277 / 3,325 | 1,487 / 781 |

Clusters held about 809 MiB (stage 3: 806; the budget 1,024), 114 to 118 shown, about 3,300 cluster draws and 6.9M cluster triangles, as in stage 3. Differences from stage 3: both runs are 0.2 to 0.65 ms slower (the machine was shared with three other running games; the tiles-only run is also +0.23 ms), and the cost of clusters over tiles is 0.47 ms against 0.05 then, which was NOT separated from machine load or from renderer changes (no build of the older commit exists any more). The 2.4 s and 0.6 s worst frames appear in one run each (first adoption with a cold pipeline cache; the second run has none).

## 3. Morrowind with LOD off unchanged

`tools/worldobjects_check` against `integ2/build`: 110 lines, OK. Fixed Balmora frame (`--start Balmora --random-seed 7`, `OPENEDEN_FIXED_FRAME_TIME`, frame 600): `1c5d47ad0b5dc32d` for both the reference and this build.

## 4. Findings

| # | Finding | Status |
| --- | --- | --- |
| 1 | Both Morrowind packs byte-identical to the recorded ones | fine |
| 2 | Skyrim terrain pack identical in size; object pack 0.6 percent smaller, probably from enable-parent folding (task 0293) | explained by the bake counts; an old Skyrim object pack must be rebaked anyway (rotation fix) |
| 3 | A Skyrim session tries the legacy Morrowind pack path first and logs `LOD clusters: ...\openeden\lod\objects.edenpak is not usable: ... cannot open` (Warning) before the neutral world adopts its own pack | cosmetic, not changed |
| 4 | Cluster cost over tiles 0.47 ms median (0.05 in stage 3) | not attributed (shared machine); a controlled A/B needs a build of an earlier commit |
| 5 | A small frame count in the bake harness quits before the pack is written | documented above; not changed |

## 5. Not run

The Linux gate, clang-format, clang-tidy, the Lua tests, `components-tests` and `openmw-tests` (no code changed; the `Lod*` tests were not rerun), terrain maps or `neutral terrain layers` on with LOD, a continuous climb (the pose script gives stills, so a fade or pop in motion is not shown), a Tamriel (not WhiterunWorld) bake, a mod list, a weak GPU or a lower tier.

## 6. Decisions for toddhow

1. Is the 0.47 ms cluster cost (against 0.05 in stage 3) worth a controlled A/B? It needs a build of `d816f27a60` beside this one.
2. Should the legacy-path warning in a Skyrim session be removed (try the neutral pack path first)?

Frames: `I:\Projects\OpenEden-work\lodrefresh\keep` (contact sheets `mwsheet*.png`, `skon*.png`, `skoff*.png`, `cmp_old_new.png`; single frames in `mw_shots`, `shots_on`, `shots_off`).
