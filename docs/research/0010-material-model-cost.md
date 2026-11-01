# 0010 — Material model cost (task 0027)

Date: 2026-11-01 · Task 0027 (F4, R1) · Status: measurement.

Same machine and method as note 0009 section 1 (RTX 4070, 1280 x 720): `tools/benchmark.sh` with `BENCH_FILTER=balmora_longview` and `BENCH_RENDERING` set per row, GPU main-scene pass (`gpu_main_scene`), median over the last 400 frames, in milliseconds. Only this one heavy scene was run. The raw tables are in the session scratchpad (`scratchpad/pbr/bench_*.txt`).

| Configuration | Run 1 | Run 2 |
| --- | --- | --- |
| `hdr` | 0.410 | 0.415 (and 0.385) |
| `hdr` + `gbuffer` | 0.610 | 0.617 |
| `hdr` + `gbuffer` + `pbr` (vanilla look, material output stored) | 0.598 | 0.722 |
| `hdr` + `gbuffer` + `pbr` (physical look) | 0.523 | 0.445 (p95 0.741) |
| `hdr` + `pbr` (vanilla look, no G-buffer) | 0.417 | not repeated |

Wall-clock frame time stayed between 3.7 and 3.9 ms in every row (the scene is CPU-bound, note 0009 section 3), so none of this shows in the frame rate.

**Reading, and how far to trust it.**
- The GPU numbers move by 0.1 to 0.2 ms between two runs of the same configuration (the `physical` rows are lower than `gbuffer` alone in the first pass and the `vanilla` row is higher in the second), so this scene and this method cannot separate the lighting change from noise. What can be said: with `pbr` the main pass stays in the range of 0.4 to 0.75 ms, the same range as `gbuffer` alone, and the frame is not GPU-bound here.
- `hdr` + `gbuffer` was 0.37 to 0.47 ms in task 0026 and is 0.61 ms now with `pbr` off; I did not find why (the water fix of commit `20f635c3d2` changes the copy that runs outside the timed main pass, and no shader path with `pbr` off changed), so treat the `gbuffer` row as the baseline of today and not as a regression of this task.
- The scene has a long view and few lights near the camera. The per-pixel lighting loop of `pbr` costs most where many point lights overlap a pixel (interiors with several torches); a benchmark scene for that does not exist yet (`tools/render_scenes.txt` has only exteriors as heavy scenes).
- With `pbr` off nothing of this exists: no define, no output, no table (checked by frame comparison, task 0027 stage log).
