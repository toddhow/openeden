# 0145: Model LOD chains, stage a gate measurement (task 0062)

Date 2026-11-18. Same agent wrote the census change, its tests and the runs; no fresh reviewer. Windows, RTX 4070, i7-13700K,
1280 x 720 and 320 x 180, vsync off, fixed frame time, `[Rendering] gpu timing` and `frame counters` on, 200-frame medians, one
run per cell of the tables (not repeated; other agents' builds were running on the machine).
Harness: `tools/modellod_check/run.sh` (Morrowind), `skyrim.sh` (Skyrim SE through `tools/neutral_session_check/check.sh`), `summarize.py`.

## What was built (measurement only)

- `Render::SceneCensus` now applies `vsgUtil::DistanceLOD` ranges for an eye (`setEye`), tracking the transforms above each
  node, and counts `lodNodes` and `lodChildrenSkipped`. Four tests (`RenderSceneCensus*`, written with the code, not run red first).
- New frame-stats columns (with `[Rendering] frame counters`): `ctr_scene_lod_skipped`, `ctr_object_draws`,
  `ctr_object_triangles` (the engine's loaded objects and actors, `Scene::getObjects()` and `getActors()`, ranges applied).
  The old `ctr_scene_*` columns now apply ranges too (no longer an upper bound for models with a `NiLODNode`).
- Choice recorded: the spec's "software device" run (Mesa llvmpipe under Docker) was NOT run, see "Not run".

## Method for "how much of the GPU time is object triangles"

A resolution of 320 x 180 removes nearly all fragment work, so the GPU time left in the main pass is geometry, draw and fixed
cost: an upper bound for what removing triangles could save. Shadow cascade passes have a fixed map size and draw only
geometry, so all their time counts as geometry. Shadows on = sun cascades (3, 1024) for objects, actors and terrain; no
point-light shadows.

## Morrowind (Balmora start, Morrowind + Tribunal + Bloodmoon)

| Pose | Wall ms (shadows off / on) | GPU sum ms, 1280 (off / on) | Main pass 1280 / 320 (off) | Shadow cascades ms (on) | Census draws / triangles | Object draws / triangles | LOD children skipped |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ground, 7168 | 3.68 / 4.84 | 0.14 / 0.41 | 0.097 / 0.048 | 0.28 | 6,548 / 459,719 | 6,463 / 425,580 | 0 |
| ground, 15000 distant | 3.90 / 5.12 | 0.23 / 0.57 | 0.180 / 0.120 | 0.36 | 10,187 / 899,834 | 6,463 / 425,580 | 141 |
| air 3000 up, 7168 | 3.87 / 6.14 | 0.26 / 0.73 | 0.191 / 0.083 | 0.39 | 6,978 / 410,970 | 6,893 / 376,831 | 0 |
| Vivec 900 up (teleport, no collision), 15000 distant | 5.64 / 6.26 | 0.25 / 0.56 | 0.193 / 0.147 | 0.29 | 14,786 / 1,007,674 | 7,222 / 442,540 | 322 |
| Ebonheart council (interior) | 2.64 / 3.85 | 0.05 / 0.12 | 0.021 / 0.013 | 0 | 1,593 / 105,215 | 1,581 / 104,119 | 0 |

The ground census triangles equal note 0025's 459,719 exactly: no Balmora object model has a `NiLODNode`. At 15000 and at
Vivec the 141 and 322 skipped children are models that do have one.

## Skyrim SE (WhiterunWorld, neutral world, five masters, no actors, no grass, view distance 7168)

| Pose | Wall ms (off / on) | GPU sum 1280 (off / on) | Main pass 1280 / 320 (off) | Shadow cascades ms (on) | Census draws / triangles | Object draws / triangles |
| --- | --- | --- | --- | --- | --- | --- |
| Whiterun street (25670,-7633) | 3.42 / 4.08 | 0.35 / 0.71 | 0.306 / 0.112 | 0.49 | 2,836 / 1,168,971 | 2,710 / 1,131,735 |
| plain outside (18000,-12400) | 1.82 / 2.37 | 0.13 / 0.34 | 0.100 / 0.064 | 0.21 | 1,616 / 634,081 | 1,493 / 596,845 |

Skyrim meshes are about six times denser (414 triangles per draw in the street, 66 in Balmora), which the spec asked to look
at; the frame is still CPU-bound by a factor of ten.

## Findings

1. **Both games are CPU-bound at every pose.** The whole GPU frame (all passes) is 0.05 to 0.8 ms, the wall frame 1.6 to 6.3 ms;
   `wait_ms` was about 0.15. A change that lowers triangles and keeps the draw count cannot lower the frame time here.
2. **The geometry-bound GPU time is small.** At 320 x 180 the main pass is 0.013 to 0.15 ms; shadows add 0.2 to 0.5 ms, nearly
   all geometry. The total at the most expensive pose with shadows (Skyrim street) is about 0.6 ms.
3. **Objects are the bulk of the triangles** (Balmora ground 93 percent, Skyrim street 97 percent). If chains halved the
   geometry cost, the upper bound of the saving is: Balmora ground 0.01 to 0.03 ms (shadows off), 0.08 to 0.17 ms (on); Skyrim
   street 0.05 to 0.15 ms (off), 0.2 to 0.3 ms (on). Nowhere near 0.5 ms, and at most 3.5 percent of the wall frame (Balmora ground, shadows on).
4. **Draw counts do not fall with chains** (a level is an index range of the same draw), and the CPU is the limit. Chains
   also add scene graph nodes to each placed model unless only the current level is attached: a CPU cost the gate numbers do not include.
5. NiLODNode models already skip children by their own ranges (141 at 15000, 322 at Vivec); the census now says so. At those
   poses that is at most 3 percent of the triangles.

## Gate verdict (task 0062 stage a)

Gate: continue if at least 10 percent of the frame's GPU time (or software-device time) at the ground pose is in object
triangles that levels 1 to 3 could remove, or if shadows on show a gain of at least 0.5 ms.

- The literal 10 percent reading is met only because the GPU is idle: with shadows on the removable part is 20 to 40 percent
  of 0.41 ms at Balmora ground (0.08 to 0.17 ms). That says how small the GPU time is, not that a gain reaches the frame.
- The 0.5 ms criterion is **not met**: at most about 0.17 ms (Morrowind) and 0.3 ms (Skyrim street) with shadows on.

**Verdict: no-go on this machine (measured, not built); conditional go for a triangle-limited device.** Recommend marking task
0062 "measured, not built" and not starting stage b. The gate wording (a share of GPU time) should be changed to an absolute
number (milliseconds, or percent of the wall frame) before anyone reads it again; I did not change it.

## What would change the verdict

- A measured weak GPU or software device where main pass + cascades exceed the frame's CPU time. By extrapolation (not
  measured): a GPU about ten times slower than the 4070 would put Skyrim street shadows-on at about 6 ms GPU against 4 ms CPU,
  and chains would be worth up to about 2 ms; a 3 times slower one would not.
- Point-light shadows on (4 to 8 slots, six faces each) multiply the geometry passes.
- Skyrim with actors, grass and a 15000 view (the street run has none): more triangles, still CPU-bound at about 3.4 ms.

## Not run

- The software-device (llvmpipe) comparison: needs a Linux build under Docker, and its frame times are CPU raster, a proxy only;
  the real-device numbers already answer the question for this machine.
- The paper estimate (simplifier on the 1,175 Morrowind models, item 3): the gate fails on the upper bound alone (the saving
  with all triangles beyond level 1 gone is the geometry time above), so it would only refine a number that cannot reach the
  criterion. `components/lod/clusterbuild.*` has the simplifier if toddhow wants it.
- Repeats (one run per cell; spread between runs of one setting not measured), frames viewed (nothing visual changed), Skyrim
  actors, grass, a 15000 view and interiors, the Linux gate, clang-format 14, clang-tidy, the Lua tests.
- Morrowind unchanged: `tools/worldobjects_check` 110 objects OK against `integ2`, Balmora frame `1c5d47ad0b5dc32d` with the new build
  (new columns exist only with `frame counters`).
