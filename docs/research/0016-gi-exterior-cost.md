# 0016 - Exterior sky-visibility field cost (task 0036)

Date: 2026-11-01 - Task 0036 (stages a to e) - Status: measurement, one scene, four runs.

Machine and method of note 0009 (NVIDIA GeForce RTX 4070, 1280 x 720), `tools/benchmark.sh` with `BENCH_FILTER=balmora_longview` (the long view over Balmora with 30000 units of distant terrain: an exterior of terrain and sky, few objects). `BENCH_RENDERING` was `hdr`, `pbr` and `atmosphere` (off) or the same with `gi exterior` (on), the nine cells around Balmora baked with `openmw --bake-gi-exterior "-4,-3:-2,-1"` (3 s a cell, about 450 KB of JSON each, kept outside the repository). GPU time of each pass, median over the last 400 frames, in milliseconds. Runs in the order off, on, off, on.

| Pass | off | on | off (2nd) | on (2nd) |
| --- | --- | --- | --- | --- |
| main scene (GPU) | 0.357 | 0.535 | 0.359 | 0.624 |
| main scene, 95th percentile | 0.370 | 1.054 | 0.377 | 0.838 |
| wall time per frame | 3.93 | 4.04 | 4.06 | 4.24 |

**Reading.** The field adds no pass: its whole cost is the lookup in the terrain's and the objects' fragment shaders, about 0.18 to 0.27 ms of GPU time here (the scene's main pass went from 0.36 to 0.54 to 0.62 ms), and 0.1 to 0.2 ms of wall time. The lookup reads four columns, two levels each, of nine numbers (72 buffer reads) per lit pixel, plus the cosine weighted visibility; in this scene most of the screen is terrain, which takes it. A scene full of objects costs the same per pixel. The sky update (the sky projected to nine coefficients, 768 steps) runs on the CPU only when the sky colours move, and the sky environment image (a 32 by 32 map and its chain, 1365 texels) only when they move by more than 0.004; neither shows in the frame time. Not measured: a closer scene (the lookup is per lit pixel, so the cost follows the pixels of a frame, not the scene), an object heavy exterior, the 5 MB pool upload when the loaded cells change (a stall of a frame at a cell crossing is possible), and the parse of nine files (about 0.5 s of CPU when a game starts in an exterior, not timed).

A possible saving if the cost matters: read the visibility as 16-bit values packed two to a float, or store only the three coefficients of the z axis for a sky that depends on height alone (the sky has no azimuth in this version), which would cut the reads by two thirds.
