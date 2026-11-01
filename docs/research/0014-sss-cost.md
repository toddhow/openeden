# 0014 - Subsurface scattering cost (task 0033)

Date: 2026-11-01 - Task 0033 (stage c) - Status: measurement, one scene.

Machine and method of note 0009 (RTX 4070, 1280 x 720), `tools/benchmark.sh` with `BENCH_FILTER=ebonheart`, `BENCH_RENDERING` set to `hdr`, `gbuffer`, `pbr` (and `sss` for the second row), GPU time of each pass (`gpu timing`), median over the last 400 frames, in milliseconds. One run per row.

| Scene | Pass | Without sss | With sss |
| --- | --- | --- | --- |
| `heavy_ebonheart_crowd` | tonemap (where the step runs) | 0.010 | 0.011 |
| | main scene | 0.083 | 0.083 |

**Reading.** In this scene the step costs about 0.001 ms. That is a lower bound: the crowd is far from the camera, so few pixels are skin and almost every pixel returns after one read of the skin mark. The worst case (a face filling the screen, 16 taps with four reads each over every skin pixel) was not measured here. Estimate, not measured: a few hundred thousand skin pixels times 65 reads is under 0.2 ms on this GPU.
