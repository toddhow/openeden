# Task: Rendering R2, ambient occlusion
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R2); builds on `docs/tasks/0025-rendering-f2-linear-hdr.md`, `0026-rendering-f3-stored-gbuffer.md`, `0027-rendering-f4-material-model.md` and `0028-rendering-gi-baked-probes.md` ("What the next features must know").
Goal: darken the ambient light in creases, corners and under objects, from the stored depth and normals, without touching direct light, so interiors with one flat ambient colour (or a baked probe ambient) gain contact and depth; off by default and frame-identical when off.
Inherited code touched (to be confirmed at the commit that starts stage b): `components/render/tonemap.*` (`Tonemap::setSource`, the fragment shader's inputs), `components/render/hdrtarget.*` and `gbuffermath.hpp` (`GBuffer::extraColorFormats`), `components/pipeline/graphics.cpp` (`gbufferOutputs`, `GBufferBlendState`, `writesGBuffer`), `files/shaders/default/main.frag` and `files/shaders/terrain/main.frag` (the G-buffer outputs), `apps/openmw/mwrender/rendermanager.cpp` (graph order, `updateVelocity` as the model for a per-frame pass), `components/settings/categories/rendering.hpp`, `files/settings-default.cfg`, `files/shaders/CMakeLists.txt`, `components/CMakeLists.txt`, `apps/components_tests/CMakeLists.txt`. Read-only use: `components/render/velocity.*` (the model for a fullscreen pass over the linear depth), `components/render/gputiming.*`.
Inputs / outputs: new `[Rendering]` settings `ao` (bool, false), `ao radius` (game units), `ao strength`, `ao slices`, `ao steps`, `ao temporal` (bool); new files `components/render/aomath.hpp` (pure maths and its CPU twin), `components/render/ambientocclusion.*` (the pass), `files/shaders/ao/*`; one more colour output of the main pass (stage b). No change to any save, Lua API, extension contract or file format.

## The design, in plain language

**Ambient occlusion** answers "how much of the sky (or the room) can this point see?". A point in a corner sees little, so the ambient light that reaches it should be small; a point on an open floor sees a lot. We estimate it from what the camera already stored for every pixel: how far away the surface is (linear depth, F3) and which way it faces (normal, F3). Around each pixel we look along a few directions in the picture, find how high the surrounding surface rises above the pixel's own plane (the "horizon"), and turn the two horizons of each direction into a visibility number with the closed form of ground truth ambient occlusion (GTAO, Jimenez, Wu, Pesce, Jarabo, 2016). It runs at half resolution, then a small blur that does not cross edges removes the noise.

**It must multiply the ambient light and nothing else.** Direct light (torches, the sun) is not occluded by this method, and darkening it too would be wrong. But by the time the picture is drawn, ambient and direct light are one sum. So the main pass also writes **the ambient part of each pixel's colour** into a new output (stage b), and the final step takes it back out: `colour = colour - ambient * (1 - ao)`. With the baked probes of task 0028 the stored ambient already includes the probe's own openness (rays that hit a wall near by carry less ambient), so `ao strength` lets the user choose how much of this second darkening to apply.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | How to multiply only the ambient | A new colour output of the main pass holding the ambient part (RGBA16F), subtracted in the tonemap's shader | Darken the whole colour (wrong for lit surfaces); use last frame's occlusion inside the main pass (one frame late, ghosts on cuts) |
| D2 | Algorithm | GTAO, 2 slices of 6 steps each side at half resolution | HBAO, or a ray-marched approach |
| D3 | Filter | Spatial, depth and normal aware (a 5 by 5 cross); optional temporal accumulation using F3's velocity | Temporal only |
| D4 | Where it applies | Opaque G-buffer-writing draws (the same set as the normal output); transparent things, sky, water and particles get none | A prepass that includes them |
| D5 | Terrain | Writes the ambient output like the other opaque draws | Leave terrain out |
| D6 | Needs | `hdr` and `gbuffer` (not `pbr`; with `pbr` the ambient it writes has the material's scale) | Also require `pbr` |
| D7 | Contact shadows | Out of scope here (they need the lights; a later stage if cheap) | Include |

## The maths (stage a), `components/render/aomath.hpp`, namespace `Render::Ao`, no game types

All angles in radians. The shader's twin is `files/shaders/lib/ao/gtao.glsl` (stage b); change both together.

- `float gtaoSliceVisibility(float h1, float h2, float n)`: with `n` the angle of the slice's projected normal from the view vector, and `h1 <= 0 <= h2` the horizon angles on each side measured from the view vector, clamped as `h1 = n + max(h1 - n, -pi/2)`, `h2 = n + min(h2 - n, pi/2)`; the result is `1/4 (-cos(2 h1 - n) + cos n + 2 h1 sin n) + 1/4 (-cos(2 h2 - n) + cos n + 2 h2 sin n)`. A surface facing the camera with nothing around it (`n = 0`, `h1 = -pi/2`, `h2 = pi/2`) gives 1; a surface hidden by a wall right at its plane (`h1 = h2 = 0`, `n = 0`) gives 0.
- `float horizonCosine(vec3 delta, float radius, float falloff)`: the cosine of the angle between the view vector (+Z of the frame `delta` is given in) and a sample's offset from the pixel; a sample at or beyond `radius`, or at the pixel, is the "no occluder" value `-1`; with `falloff` in 0 to 1 nearer samples are also pulled towards `-1` the farther they are, so occluders fade out at the radius.
- `float aoCurve(float visibility, float strength)`: `pow(visibility, strength)`; strength 1 is the plain GTAO value, 0 gives 1 (no occlusion).
- `float bilateralWeight(float depthDifference, float depth, float normalDot)`: the weight of a neighbour in the spatial filter, `0` when the surfaces differ (relative depth difference over 5%, or the normals more than about 45 degrees apart) and smoothly 1 when they match.
- `vec3 removeOcclusion(vec3 colour, vec3 ambient, float ao)`: `max(colour - ambient * (1 - ao), 0)` per channel, the tonemap's operation. With `ao = 1` it returns `colour`.

## The pass (stage b)

`Render::AmbientOcclusion`, modelled on `Render::Velocity` (a fullscreen pass in its own render graph over the linear depth and normal images, registered with the compile manager after `Engine::setup`, rebuilt by `setSource` on resize, the render pass kept). Half resolution `R8_UNORM`; a second pass blurs. Its graph runs between the scene and the tonemap, timed with `GpuTimer` (`timeChild`) under the name `ao`. The tonemap gets two more sampled images (the ambient output and the filtered occlusion) and the `AO` variant of its fragment shader, chosen in `setSource` like the debug views. Debug view `ao` in `gbuffer debug view`.

## Constraints
Determinism: no randomness except a fixed interleaved-gradient noise by pixel position (no time) unless `ao temporal` is on, where the noise rotates with the frame index. Allocation: none per frame. Threading: main thread builds and updates; the record thread reads the values. Off by default and unchanged when off: with `ao = false` there is no extra output, no define, no image, no pass, and the frame equals the commit before the task by `diffmap`. With `ao` on and strength 0 the picture equals the picture with `ao` off within the quantisation of the extra output.

## Stages
- **a. Maths and tests** (no renderer change): `aomath.hpp`, `testao.cpp`.
- **b. Pass and apply:** the ambient output in `default/main.frag`, the terrain, the pass, the tonemap, settings, the debug view.
- **c. Cost and temporal:** GPU time with `gpu timing` and `tools/benchmark.sh` into `docs/research/0012-ao-cost.md`; optional temporal accumulation.

## Acceptance criteria
- [ ] Unit tests for the maths pass (the limits above, symmetry, monotonic in the horizon, the filter weights, `removeOcclusion`).
- [ ] `ao = false`: frames of the default path, of `hdr + gbuffer` and of `hdr + gbuffer + pbr + gi` equal the commit before by `diffmap` within noise.
- [ ] With `ao`: a corner of a room is darker than the open floor in the ambient only; a torch-lit surface keeps its direct light; viewed by eye in an interior, a night exterior and an NPC scene.
- [ ] Cost measured and written down.
- [ ] The Linux gate builds with `-Werror`; the whole tree passes `CI/check_clang_format.sh`; a fresh reviewer read each stage; the editor's SceneHost still builds and draws.
Out of scope: contact shadows, screen-space reflections, multi-bounce colour bleeding, temporal anti-aliasing, ray-traced occlusion (it can replace the pass and write the same `ao` image), any Lua API, saves.

## What the next features must know
(Filled in with the stage log.)

## Stage log
(Nothing built yet; see the report that came with this spec.)
