# Task: Rendering R7, subsurface scattering (the cheapest tier)
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R7, tier 2 in its cheapest form); builds on `docs/tasks/0025-rendering-f2-linear-hdr.md`, `0026-rendering-f3-stored-gbuffer.md`, `0027-rendering-f4-material-model.md` (the skin mark) and `0029-rendering-ao.md` (the tonemap-side pass pattern).
Goal: let light spread a little under the skin of NPCs and creatures marked as skin, red farther than green and green farther than blue, so faces and hands lose their hard plastic edge; off by default and frame-identical when off.
Inherited code touched (to be confirmed at the commit that starts stage b): `components/render/tonemap.*` (`Tonemap::setSource`, the fragment shader), `components/render/gbuffermath.hpp` (`DebugView`), `apps/openmw/mwrender/rendermanager.cpp` (`tonemapParameters`, the two `setSource` call sites, where the projection is known), `components/settings/categories/rendering.hpp` and `files/settings-default.cfg`.
Inputs / outputs: new `[Rendering]` settings `sss` (bool, false), `sss strength` (0 to 1, default 1), `sss width` (game units, default 1.5); new `components/render/sssmath.hpp` (pure maths and its CPU twin); one more variant of the tonemap's fragment shader; one more debug view (`sss`). No new image, no new pass, no new G-buffer output, no change to any save, Lua API, extension contract or file format.

## The design, in plain language

Real skin is not a hard surface. Light enters, bounces around under the surface and leaves a little way from where it came in, and red light travels farthest, which is why a shadow edge on a hand has a soft red glow and why a face lit from one side is not a plain cut-out of lit and unlit. Morrowind's skin is plain diffuse lighting, so it looks like painted plastic.

The cheapest way to imitate this is a blur of the lit picture, only over skin, only across pixels that belong to the same skin surface, with a wider blur for red than for blue. We already store everything that needs: the F4 material output marks skin pixels (`HdrTarget::materialView()`, alpha), and the F3 outputs give each pixel's depth and normal, so a blur can refuse to cross from a face onto the wall behind it.

Two things keep the cheap version from ruining the picture:

- **Detail is kept.** A plain blur would smear the skin texture (pores, freckles, lip lines). Instead each channel gets *a wide blur minus a very narrow blur*, added back onto the pixel. Detail finer than the narrow blur is untouched; only soft light is redistributed.
- **Only skin pays.** The tonemap shader starts with one read of the skin mark and returns at once for everything else.

It runs inside the tonemap (the same place the occlusion step does its work), on the linear HDR colour, before the occlusion subtraction and before bloom. A real separable pass at full quality (two passes, the diffuse light kept apart from the specular) is the tier above this one and is not built here.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | Where it runs | Inside the tonemap's fragment shader, a variant chosen at start | A separate pass after the scene that writes a blurred copy (an extra image and a pass, the separable form) |
| D2 | What is blurred | The final lit colour of skin pixels (specular included, which on stock skin is faint) | Diffuse light only, which needs the diffuse and specular lights stored apart (a new output) |
| D3 | Kernel | One gather, 16 taps in a golden-angle spiral, turned per pixel by a fixed noise; per channel Gaussian widths 1.0, 0.45 and 0.2 times `sss width`, minus a narrow Gaussian of 0.12 times the width | A true separable two pass kernel (Jimenez 2015) |
| D4 | Which pixels count as neighbours | Skin-marked, within one width of depth (fading to two widths), normals within about 70 degrees | Depth only |
| D5 | Thin things (transmission: ears, leaves, lanterns) | Not built: they need a thickness estimate the engine does not have | Fake thickness from the normal against the light |
| D6 | Needs | `hdr`, `gbuffer` and `pbr` (the skin mark is in the material output) | Mark skin without `pbr` (a new output) |

## The maths (stage a), `components/render/sssmath.hpp`, namespace `Render::Sss`, no game types

- `useSss(setting, gbuffer, materialOutput)`; `focalPixels(fov, height)` and `pixelsPerUnit(depth, focal)` (how many pixels one game unit covers at a depth, so the blur has a size in the world, not on the screen).
- `gaussian(distance, sigma)`; `tapOffset(index, count, reach, rotation)` the spiral; `surfaceWeight(depthDelta, width, normalDot)` how much a neighbour counts.
- `scatter(centre, taps, count, strength, width)`: for each channel, `centre + strength * (wideAverage - narrowAverage)`, the pixel itself counting with weight 1 in both averages, never below zero.

## Constraints
Determinism: the per-pixel rotation is a fixed noise of the pixel position (no time). Allocation: none per frame. Threading: main thread sets the push constants; the record thread reads them. Off by default and unchanged when off: with `sss = false` there is no variant, no define, no binding; the frame equals the commit before the task by `diffmap`. With `sss strength` 0 the picture equals the picture without it.

## Stages
- **a. Maths and tests** (no renderer change): `sssmath.hpp`, `testsss.cpp`.
- **b. The step in the tonemap:** the variant, the three bindings, the push constants, the settings, the debug view (`gbuffer debug view = sss`), the wiring in `RenderManager`.
- **c. Frames, cost and notes:** close ups of faces and hands in several races, night, interior torchlight; GPU time with `gpu timing` and `tools/benchmark.sh` into `docs/research/0014-sss-cost.md`; module guides.

## Acceptance criteria
- [ ] Unit tests for the maths pass.
- [ ] `sss = false`: frames of the default path and of `hdr + gbuffer + pbr` equal the commit before by `diffmap` within noise.
- [ ] With `sss`: close ups of NPC faces and hands in several races look softer at light edges with texture detail kept; no halo across the jaw line onto the background; viewed by eye (night, noon, interior torchlight).
- [ ] The debug view shows only skin pixels changed.
- [ ] Cost measured and written down.
- [ ] The Linux gate builds with `-Werror`; the whole tree passes `CI/check_clang_format.sh`; a fresh reviewer read each stage.
Out of scope: transmission, a separable kernel, diffuse and specular kept apart, per-race tuning, creatures other than what the skin mark already covers, any Lua API, saves.

## Stage log
