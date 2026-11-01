# 0020 - Snow and ash cover cost (task 0039)

Date: 2026-11-01 - Task 0039 (stage d) - Status: measurement, one scene, one run each.

Machine and method of note 0009 and 0017 (NVIDIA GeForce RTX 4070, 1280 x 720), `tools/benchmark.sh` with two Seyda Neen scenes (the start position, 600 frames, `set timescale to 30`): snow (`ChangeWeather "Bitter Coast Region" 8`) and clear (weather 0). `BENCH_RENDERING` was `hdr`, `pbr`, `atmosphere` and `gi exterior` (the cell baked, so the shelter lookup runs), with `cover` off or on (`cover build time = 5`, so the ground is buried within the first seconds). GPU time of the main scene pass, median over the last 400 frames, in milliseconds. The other passes did not move.

| Scene | cover off | cover on |
| --- | --- | --- |
| snow, main scene (GPU) | 0.601 | 0.630 |
| snow, main scene, 95th percentile | 0.686 | 0.834 |
| clear, main scene (GPU) | 0.275 | 0.301 |

**Reading.** Cover adds no pass. In snow it costs about 0.03 ms of GPU time here: each lit pixel facing upwards does the shelter lookup of the sky visibility field (the same one task 0037's wetness does) and one noise. Walls skip both (the first test is the upwardness). In clear weather the shader's first test (no cover in the scene's alpha) skips everything, so the 0.026 ms measured is the size of the run to run noise plus the longer shader (an extra branch in every lit pixel), not work done. Not measured: wetness and cover together, a closer scene with many objects, a machine without the baked file (no lookup), wall time.

A saving if it matters: wetness and cover both call `skyVisUp` at the same pixel; a surface that takes both could share the sample.
