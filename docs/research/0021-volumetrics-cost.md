# Research 0021: what the volumetric fog costs (task 0041)

Written 2026-11-01. Measured on the project owner's Windows machine (NVIDIA GeForce RTX 4070, 1280 x 720, Vulkan), `tools/benchmark.sh`'s method with a copy of the script: GPU timestamps of each pass (`[Rendering] gpu timing`), median and 95th percentile over the last 300 frames of a 600-frame run. The scene is `heavy_volume_fog` of `tools/render_scenes.txt`: foggy dusk at Seyda Neen, standing still, `hdr` and `atmosphere` on, shadows off unless said.

## Numbers (the volume's own pass, `gpu_volumetrics`)

| Setting | Grid | Volume pass, median / p95 (ms) | D10 budget (ms) |
| --- | --- | --- | --- |
| volumetrics off | none | (no pass) | |
| `low` (temporal never on) | 80 x 45 x 32 | 0.029 / 0.031 | 1.0 |
| `medium`, temporal on | 160 x 90 x 64 | 0.073 / 0.075 | 2.0 |
| `high`, temporal on | 240 x 135 x 96 | 0.218 / 0.221 | 3.5 |
| `medium`, temporal on, terrain and actor shadows on | 160 x 90 x 64 | 0.164 / 0.190 | 2.0 |
| `medium`, temporal off | 160 x 90 x 64 | 0.230 / 0.499 | 2.0 |

All of them are far under the budgets of decision D10. The scene has no lamps in view and almost no occluders, so the numbers are a floor: what grows with the scene is the number of lights a froxel reads from the light grid (at most `maxLightsPerCluster`, 50) and the shadow lookups.

**Noise.** Other work was using the same machine and the same GPU during the runs, so the last two rows are not ordered the way the work is (temporal off does less work than temporal on and was measured later, at a higher GPU clock load): read the table as "about 0.03, 0.07 to 0.23 and 0.2 to 0.25 ms", not as ratios. The main scene's own time moved from 0.117 ms (off) to 0.12 to 0.44 ms with the volume on, because the forward shaders read a 3D texture in `applyFog`; that part is inside the same noise and was not separated.

## What was not measured

- A scene with many lamps in view, a night street, a scene with a large amount of shadow-casting geometry (object shadows crash the game on this machine; see `components/render/CLAUDE.md`), and a weaker GPU than an RTX 4070. The `heavy_volume_interior` scene was added for the lamps case but its frames did not show the torch, so it is not measured.
- The cost of the shadow cascades themselves, which the volume uses only if the player has turned them on (decision D3): not measured here; the existing `shadow cascade N` passes report it.
- Memory: the images are RGBA16F, 8 bytes a froxel: `low` 0.9 MB each, `medium` 7.4 MB, `high` 24.9 MB. With the temporal blend there are four images (the samples, the result, the blend and the history), so `high` takes about 100 MB of video memory.
