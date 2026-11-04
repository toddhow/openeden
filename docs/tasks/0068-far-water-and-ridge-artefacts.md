# Task: Far water speckle and white ridge strips under the whole-map view
Design doc section: `docs/tasks/0066-world-lod-whole-map-view.md` (stage a log: far water at 600,000, white strips at 100,000; stage c log: far water speckle with and without the haze)
Goal: find why open water far from the camera speckles white and blue, and why white wavy strips follow some far ridges, when the view reaches far (`[WorldLod] map view`, or a large `view distance`), and fix both without changing the default frame.
Inherited code touched: `files/shaders/water/main.frag`, `files/shaders/water/main.vert`, `files/shaders/lib/view/reflect.glsl`, `files/shaders/lib/view/raytrace.glsl`, `files/shaders/terrain/*` (to be confirmed from the findings), at commit `14527ba896`.
Inputs / outputs: shaders only (and a pure C++ twin of a distance falloff if one is added and tested); no setting is added unless the fix needs one.
Constraints: with `map view = false` and numeric `view distance` the frame must be unchanged (old binary against new binary, PSNR set, a relaxation of byte-identity because clouds, water and NPCs animate; `[WorldLod] enabled = false` must be byte-identical). If the cause is the default path's behaviour (visible without map view), the default look is NOT changed without toddhow's answer. Every test game runs with `OPENEDEN_QUIET_WINDOW=1`.
Acceptance criteria:
- [ ] The cause of each artefact is named with evidence (code path and a frame or number), not a guess.
- [ ] A frame from 3,000 and from 25,000 units up over the Balmora coast with `map view` on shows no white or blue speckle on open water and no white strips on ridges (toddhow's eye is the final judge).
- [ ] The same poses with `map view` off look the same as before the fix (PSNR set, viewed).
- [ ] A pure distance-falloff function, if added, has tests written first and run red.
Out of scope: `rendermanager.cpp`, `viewlimits.*`, `worldlod.hpp` (another change is editing them), the haze function (task 0066 stage c), new water features.

## Stage log
