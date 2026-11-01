# 0015 - Atmosphere cost (task 0034)

Date: 2026-11-01 - Task 0034 (stages a to c) - Status: measurement, one scene, four runs.

Machine and method of note 0009 (RTX 4070, 1280 x 720), `tools/benchmark.sh` with `BENCH_FILTER=balmora_longview` (the long view over Balmora, an exterior with the sky in view), `BENCH_RENDERING` set to `hdr` (off) or `hdr`, `atmosphere`, `atmosphere look = physical` (on), GPU time of each pass, median over the last 400 frames, in milliseconds. Two runs of each, in the order off, on, on, off.

| Pass | off | on | on (2nd) | off (2nd) |
| --- | --- | --- | --- | --- |
| main scene (GPU) | 0.408 | 0.553 | 0.397 | 0.403 |
| tonemap (GPU) | 0.022 | 0.035 | 0.021 | 0.023 |
| wall time per frame | 3.62 | 3.96 | 3.74 | 4.13 |

**Reading.** The atmosphere adds no pass and no image. The only GPU change is one more multiply by a colour in the cloud shader, and the only CPU work is a few dozen floating-point operations once per frame in the weather manager. The second pair of runs shows no difference (0.397 against 0.403 ms for the main scene). The first "on" run is higher in every pass, including the tonemap, which the atmosphere does not touch, so it is run-to-run noise (the GPU clock), not cost. Not measured: the weather was not fixed in these runs (it follows the real clock), though the frames of this scene are all sky and terrain with no weather-dependent geometry.
