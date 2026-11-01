# 0019 - Cloud shading and cloud shadows cost (task 0038)

Date: 2026-11-01 - Task 0038 (stage d) - Status: measurement, one scene, a few runs each.

Machine and method of note 0009 (NVIDIA GeForce RTX 4070, 1280 x 720), `tools/benchmark.sh` with a one-scene file: the start in `ToddTest`, a console script that sets cloudy weather in the Bitter Coast Region, enters Seyda Neen and sets noon (`coc` forces the weather at once), 600 frames. `BENCH_RENDERING` was `hdr` and `atmosphere` (the shadows need both), plus `clouds` and `cloud shadows`. Median over the last 400 frames, in milliseconds.

| Setting | wall | GPU main scene |
| --- | --- | --- |
| off (run 1) | 2.52 | 0.174 |
| off (run 2) | 2.49 | 0.183 |
| off (run 3) | 2.95 | 0.221 |
| `clouds` | 2.70 and 3.02 | 0.188 and 0.244 |
| `clouds` and `cloud shadows` | 3.25 (before the upload throttle), 2.71 | 0.143 and 0.138 |

**Reading.** Three runs of the same off setting differ by 0.4 ms of wall time and 0.05 ms of GPU time, more than any difference between the settings, so the cost is below what this scene can measure. What each part does per frame:
- Lit clouds: the sky shader, on the pixels of the two cloud layers, adds a thickness, an exponential and a phase function (a few tens of operations); no new pass, texture or buffer.
- Cloud shadows: every pixel that takes the sun's shadow (`shadow()`) does one ray-to-layer computation, a dome lookup (a loop of at most four steps), and two bilinear reads of a 64 by 64 grid (eight reads of the header buffer per layer in a transition, four otherwise). Pixels indoors or with no clouds return at the first test (strength 0).
- The data: the header buffer is 2,100 vec4 (34 KB) and is uploaded when the scroll has moved by 0.002 of the picture (about every third of a second at the cloudy weather's speed), when the weather's picture changes, or when the strength or rotation changes. The first run of "both" re-uploaded it every frame and showed 0.55 ms more wall time than `clouds` alone, which is why the throttle was added.
- Not measured: a closer scene with many objects and a lot of terrain on screen (the shadow term runs per lit pixel), the editor (it never takes the setting), Linux, a weak GPU. A scene built to be heavy on pixels is the place to look first if this ever matters; a cheaper variant would take the layer lookup once per vertex or per low-resolution cell.
