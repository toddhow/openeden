# Research 0023: what the volumetric clouds cost (task 0042)

Written 2026-11-01. Measured on the project owner's Windows machine (NVIDIA GeForce RTX 4070, 1280 x 720, Vulkan), with `tools/benchmark.sh` and its scene `heavy_volume_clouds` (a cloudy afternoon over Seyda Neen, the camera held still and looking up at the sky by the `look` option's Lua script; `BENCH_RENDERING` sets the tier). GPU timestamps of each pass (`[Rendering] gpu timing`), median and 95th percentile over the last 400 frames of a 600-frame run. The pass is `gpu_volumetric_clouds`: the march, the resolve, the history copy and the ground shadow map together (the noise is built in the first three frames only).

| Tier | March image | Steps | Cloud pass, median / p95 (ms) | D8 budget (ms) |
| --- | --- | --- | --- | --- |
| `low` | 160 x 90 (1/8) | 24 | 0.141 / 0.150 | 1.0 |
| `medium` | 320 x 180 (1/4) | 40 | 0.184 / 0.194 | 1.8 |
| `high` | 640 x 360 (1/2) | 64 | 0.319 / 0.333 | 3.0 |

The same scene in overcast weather (a thick deck over the whole sky, rays ending early) gave 0.139, 0.175 and 0.311 ms median: the same, within the noise, so a thick deck is not a worse case on this machine.

All three are far under the budgets of decision D8. The numbers are for 720p and a GPU of the high end; what scales with them: the resolve and copy passes with the screen's pixel count (they run at full size), the march with the tier's image size and the steps. At 4K the resolve and copy would cost about nine times what they do here. The march skips empty space with one shape lookup per step, so a clear sky costs less and a sky full of thick, broken cloud costs more than this scene; the ground shadow map is a fixed 256 by 256 by 12 samples whatever the tier.

## Memory

- The march image (RGBA16F, 8 bytes a texel): `low` 0.1 MB, `medium` 0.5 MB, `high` 1.8 MB at 720p.
- The resolved picture and its history (RGBA16F at the screen's size): 7.4 MB each at 720p, 33 MB each at 1080p.
- Noise: the shape texture 8 MB (128 cubed RGBA8), the detail texture 0.1 MB, the curl texture 0.06 MB; the ground shadow map 1 MB (257 by 256 RGBA32F).

## What was not measured

- A weaker GPU, a larger window, or a frame with the sky seen through the cloud-heavy parts of a long view distance with the distant terrain on.
- Several tiers' smoothness against each other by eye (only `medium` frames were viewed closely).
- The cost of the shadow map alone (it shares the pass's timestamps).
