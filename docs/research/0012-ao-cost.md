# 0012 - Ambient occlusion cost (task 0029)

Date: 2026-11-01 - Task 0029 (stage c) - Status: measurement.

Machine and method of note 0009 (RTX 4070, 1280 x 720 as the benchmark sets it), `tools/benchmark.sh` with `BENCH_FILTER` set to one scene and `BENCH_RENDERING` set per row, GPU time of each pass (`gpu timing`), median and 95th percentile over the last 400 frames, in milliseconds. One run per row (no repeat), the Linux build container idle. The raw tables are in the session scratchpad (`scratchpad/refl/bench/`).

Settings: `hdr`, `gbuffer`, `pbr`; "ao" adds `ao = true` with the defaults (radius 100, strength 1, 2 slices, 6 steps each side).

| Scene | Pass | Without ao | With ao |
| --- | --- | --- | --- |
| `heavy_ebonheart_crowd` (interior, many NPCs and lights) | main scene | 0.083 (p95 0.124) | 0.096 (0.096) |
| | ao (half resolution GTAO) | none | 0.162 (0.164) |
| | tonemap (with the 5 by 5 filter and the subtraction) | 0.010 (0.015) | 0.128 (0.129) |
| `heavy_balmora_longview` (exterior, long view, much sky) | main scene | 0.714 (0.741) | 0.577 (0.878) |
| | ao | none | 0.057 (0.058) |
| | tonemap | 0.035 (0.042) | 0.266 (0.268) |

**Reading.**
- The two passes that ao adds (the occlusion pass and the dearer tonemap) come to about 0.28 ms in the interior and 0.29 ms in the exterior at this resolution: under 0.3 ms of GPU time, the same order as the whole material model (note 0010). The frame stays CPU bound in these scenes (note 0009), so none of it shows in the frame rate.
- The tonemap is where most of the cost moved: it does the 5 by 5 filter of the half resolution image, three reads per tap (occlusion, depth, normal). A separate filter pass at half resolution would cost less per pixel; it was not tried.
- The exterior's occlusion pass is cheaper because pixels with no opaque surface (the sky) return at once.
- The main-pass time moved by 0.01 to 0.14 ms in the other direction between the rows (the exterior's is lower with ao on); that is the noise of this method (note 0010), not an effect of ao. The extra colour output (a fifth, RGBA16F) costs memory bandwidth in the main pass, which these runs cannot separate from the noise.
- Not measured: other resolutions, MSAA on, higher `ao slices` and `ao steps`, a weaker GPU.
