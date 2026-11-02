# Research 0024: what ambient occlusion on the terrain costs (task 0048)

Written 2026-11-02. Measured on the project owner's Windows machine (the same one as note 0023: an NVIDIA GeForce RTX 4070 at 1280 x 720 on Vulkan; this run's log did not print the card's name, so that is taken from the earlier note), with `tools/benchmark.sh` and its scene `heavy_balmora_longview` (30000 units of distant terrain and paged objects over Balmora, `hdr`, `gbuffer`, `pbr` and `ao` on), `ao terrain` off and on, two runs each, GPU timestamps of the last 300 frames of a 600-frame run.

| Run | `ao terrain` | Main scene pass, median / p95 (ms) | AO pass (ms) | Tonemap (ms) |
| --- | --- | --- | --- | --- |
| 1 | off | 1.215 / 1.289 | 0.114 | 0.538 |
| 2 | on | 0.520 / 0.546 | 0.050 | 0.229 |
| 3 | off | 0.869 / 0.884 | 0.050 | 0.218 |
| 4 | on | 0.483 / 0.510 | 0.045 | 0.204 |

**Reading:** the difference between off and on is smaller than the difference between two runs of the same setting (run 1 against run 3: 0.35 ms in the main pass, most likely the GPU's clock still ramping up, since the AO and tonemap passes show the same drop). The terrain's new work is one more output value and about ten multiplies per terrain pixel, so no cost above 0.1 ms is expected, and none can be told apart from the noise here. The AO pass itself is unchanged by the setting: it already read the terrain's depth and normals. A clean A/B would need the same warm GPU state for both settings (run each setting several times, alternately).

Not measured: a weaker GPU, a larger window, the editor (it never makes an `HdrTarget`).
