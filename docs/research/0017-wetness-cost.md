# 0017 - Rain wetness and puddles cost (task 0037)

Date: 2026-11-01 - Task 0037 (stage e) - Status: measurement, one scene, one run each.

Machine and method of note 0009 (NVIDIA GeForce RTX 4070, 1280 x 720), `tools/benchmark.sh` with a scenes file of two Seyda Neen scenes (the start position, 600 frames, `set timescale to 30` so the ground is soaked within the first seconds): rain (`ChangeWeather "Bitter Coast Region" 4`) and clear (weather 0). `BENCH_RENDERING` was `hdr`, `pbr`, `atmosphere` and `gi exterior` (the cell baked, so the shelter lookup runs), with `wetness` off or on. GPU time of the main scene pass, median over the last 400 frames, in milliseconds.

| Scene | wetness off | wetness on |
| --- | --- | --- |
| rain, main scene (GPU) | 0.245 | 0.318 |
| rain, main scene, 95th percentile | 0.269 | 0.335 |
| clear, main scene (GPU) | 0.203 | 0.198 |

**Reading.** Wetness adds no pass. In clear weather the shader's first test (`wetness` zero) skips everything and the cost is lost in noise. In rain it costs about 0.07 ms of GPU time in this scene (30 percent of a small pass): each lit pixel does the shelter lookup of the sky visibility field (72 buffer reads, the same lookup as the exterior ambient, once more), the puddle noise (four hashes twice over), and, in puddles only, the sky image. A scene seen mostly as terrain pays the lookup per terrain pixel. Not measured: a closer scene with many objects, the editor, a machine without the baked file (no lookup, so cheaper), and wall time.

A saving if it matters: both lookups read the same interpolated field, so a surface that already took `gi exterior`'s ambient could reuse its sample (the ambient uses the surface normal's bias, the shelter the up direction's, so they differ a little).
