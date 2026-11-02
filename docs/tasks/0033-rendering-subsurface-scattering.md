# Task: Rendering R7, subsurface scattering (the cheapest tier)
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 5 (R7, tier 2 in its cheapest form); builds on `docs/tasks/0025-rendering-f2-linear-hdr.md`, `0026-rendering-f3-stored-gbuffer.md`, `0027-rendering-f4-material-model.md` (the skin mark) and `0029-rendering-ao.md` (the tonemap-side pass pattern).
Goal: let light spread a little under the skin of NPCs and creatures marked as skin, red farther than green and green farther than blue, so faces and hands lose their hard plastic edge; off by default and frame-identical when off.
Inherited code touched (to be confirmed at the commit that starts stage b): `components/render/tonemap.*` (`Tonemap::setSource`, the fragment shader), `components/render/gbuffermath.hpp` (`DebugView`), `apps/openmw/mwrender/rendermanager.cpp` (`tonemapParameters`, the two `setSource` call sites, where the projection is known), `components/settings/categories/rendering.hpp` and `files/settings-default.cfg`.
Inputs / outputs: new `[Rendering]` settings `sss` (bool, false), `sss strength` (0 to 1, default 0.8; was 0.5), `sss width` (game units, default 1.0; was 0.7); new `components/render/sssmath.hpp` (pure maths and its CPU twin); one more variant of the tonemap's fragment shader; one more debug view (`sss`). No new image, no new pass, no new G-buffer output, no change to any save, Lua API, extension contract or file format.

## The design, in plain language

Real skin is not a hard surface. Light enters, bounces around under the surface and leaves a little way from where it came in, and red light travels farthest, which is why a shadow edge on a hand has a soft red glow and why a face lit from one side is not a plain cut-out of lit and unlit. Morrowind's skin is plain diffuse lighting, so it looks like painted plastic.

The cheapest way to imitate this is a blur of the lit picture, only over skin, only across pixels that belong to the same skin surface, with a wider blur for red than for blue. We already store everything that needs: the F4 material output marks skin pixels (`HdrTarget::materialView()`, alpha), and the F3 outputs give each pixel's depth and normal, so a blur can refuse to cross from a face onto the wall behind it.

Two things keep the cheap version from ruining the picture:

- **Detail and darkness are kept.** A plain blur would smear the skin texture (pores, freckles, lip lines), and the first try (the wide blur minus a very narrow blur, added onto the pixel) let a black nose and a dark mouth take red from the bright fur around them. So each channel is *multiplied by a gain*: the wide blur divided by a very narrow blur, limited to 0.5 to 2. Black stays black, detail finer than the narrow blur is untouched, only soft light is redistributed. (Changed after the frame check; the stage a tests were changed with it.)
- **Only skin pays.** The tonemap shader starts with one read of the skin mark and returns at once for everything else.

It runs inside the tonemap (the same place the occlusion step does its work), on the linear HDR colour, before the occlusion subtraction and before bloom. A real separable pass at full quality (two passes, the diffuse light kept apart from the specular) is the tier above this one and is not built here.

### Decisions toddhow can change (my recommendation first)

| # | Decision | Recommended | Alternative |
| --- | --- | --- | --- |
| D1 | Where it runs | Inside the tonemap's fragment shader, a variant chosen at start | A separate pass after the scene that writes a blurred copy (an extra image and a pass, the separable form) |
| D2 | What is blurred | The final lit colour of skin pixels (specular included, which on stock skin is faint) | Diffuse light only, which needs the diffuse and specular lights stored apart (a new output) |
| D3 | Kernel | One gather, 16 taps in a golden-angle spiral (not turned per pixel: that made the face dotted); per channel Gaussian widths 1.0, 0.45 and 0.2 times `sss width`, divided by a narrow Gaussian of 0.12 times the width | A true separable two pass kernel (Jimenez 2015) |
| D4 | Which pixels count as neighbours | Skin-marked, within one width of depth (fading to two widths), normals within about 70 degrees | Depth only |
| D5 | Thin things (transmission: ears, leaves, lanterns) | Not built: they need a thickness estimate the engine does not have | Fake thickness from the normal against the light |
| D6 | Needs | `hdr`, `gbuffer` and `pbr` (the skin mark is in the material output) | Mark skin without `pbr` (a new output) |

## The maths (stage a), `components/render/sssmath.hpp`, namespace `Render::Sss`, no game types

- `useSss(setting, gbuffer, materialOutput)`; `focalPixels(fov, height)` and `pixelsPerUnit(depth, focal)` (how many pixels one game unit covers at a depth, so the blur has a size in the world, not on the screen).
- `gaussian(distance, sigma)`; `tapOffset(index, count, reach, rotation)` the spiral; `surfaceWeight(depthDelta, width, normalDot)` how much a neighbour counts.
- `scatter(centre, taps, count, strength, width)`: for each channel, `centre * (1 + strength * (gain - 1))` with `gain = clamp((wideAverage + e) / (narrowAverage + e), 0.5, 2)`, the pixel itself counting with weight 1 in both averages, never below zero.

## Constraints
Determinism: no noise and no time, the tap pattern is fixed. Allocation: none per frame. Threading: main thread sets the push constants; the record thread reads them. Off by default and unchanged when off: with `sss = false` there is no variant, no define, no binding; the frame equals the commit before the task by `diffmap`. With `sss strength` 0 the picture equals the picture without it.

## Stages
- **a. Maths and tests** (no renderer change): `sssmath.hpp`, `testsss.cpp`.
- **b. The step in the tonemap:** the variant, the three bindings, the push constants, the settings, the debug view (`gbuffer debug view = sss`), the wiring in `RenderManager`.
- **c. Frames, cost and notes:** close ups of faces and hands in several races, night, interior torchlight; GPU time with `gpu timing` and `tools/benchmark.sh` into `docs/research/0014-sss-cost.md`; module guides.

## Acceptance criteria
- [x] Unit tests for the maths pass.
- [ ] `sss = false`: frames of the default path and of `hdr + gbuffer + pbr` equal the commit before by `diffmap` within noise.
- [ ] With `sss`: close ups of NPC faces and hands in several races look softer at light edges with texture detail kept; no halo across the jaw line onto the background; viewed by eye (night, noon, interior torchlight).
- [x] The debug view shows only skin pixels changed (the head lit up, nothing else).
- [x] Cost measured and written down (`docs/research/0014-sss-cost.md`: one scene, a lower bound).
- [x] The Linux gate builds with `-Werror` (components-tests 2710, openmw-tests 799, 32 Lua tests pass); the whole tree passes `CI/check_clang_format.sh`; a fresh reviewer read stage b (stage a is the maths, read with it).
Out of scope: transmission, a separable kernel, diffuse and specular kept apart, per-race tuning, creatures other than what the skin mark already covers, any Lua API, saves.

## Stage log

### 2026-11-01, stages a, b and c (Windows, RTX 4070, `build-jolt`)

**Done and verified.** The maths and tests first (`testsss.cpp`, `testssspass.cpp`; `components-tests` 2740, `openmw-tests` 799, `eden-studio-tests` 393 pass). The tonemap variant, the three bindings, the push constants, the settings, the debug view, the wiring. Frames viewed with a Lua spawn rig (an NPC created in front of a static camera, scratchpad `v13/npcmod_*`): Arrille (an Argonian), Jobasha (Khajiit) and Fargoth (a Bosmer) at noon, Arrille at night, Fargoth in the guild; the skin mark view and the sss debug view show only the head. Two designs failed the frame check and were replaced: the difference form took red into dark noses and mouths, and a per-pixel spiral turn made faces dotted. At the defaults (strength 0.5, width 0.7) the face is softer and keeps its detail; strength 1 and width 1.5 is visibly muddier. A fresh reviewer read stage b and found no defect; it asked for the spec to follow the code (done), a warning for the debug view without `sss` (done) and the guide section (done).

**Not verified.** Frame identity with `sss` off against a binary from before the task (the weather follows the real clock, so frames an hour apart differ; two runs of the new binary agree at noise level, and with `sss` off the variant, bindings and push constants are the old ones by construction); hands and other body parts close up; Dunmer, Orc, Redguard and Breton skins; torchlit interiors with many lights; the worst case for cost (a face filling the screen); the editor (it never makes an `HdrTarget`); antialiasing with `sss` on (the skin mark averages at edges and counts when half is skin; not looked at).

**Decisions for toddhow.** D1 to D6 as recommended. The defaults are mild (strength 0.5, width 0.7) because there is no albedo to tell lit skin from dark texture; the eyes are inside the head model and get the same treatment.

### 2026-11-01, stronger defaults and no skin mark on eyes (decision 0010 answer 5)
- **Defaults:** `sss strength` 0.5 to 0.8 and `sss width` 0.7 to 1.0 (`files/settings-default.cfg`). Chosen from frames of Fargoth (Bosmer), Arrille (Argonian) and Jobasha (Khajiit), close-ups at noon, Fargoth at night and Arrille in a torchlit room (scratchpad `v13/out/before_x_*`, `after_x_*`), at 0.5/0.7, 0.8/1.0 and 1.0/1.2 and with `sss` off. 1.0/1.2 washes the fur on Jobasha's muzzle and Arrille's face with a bluish-green sheen (a sign that the wide blur reaches across too much of the texture), 0.8/1.0 is clearly softer than 0.5/0.7 at light edges with no sheen, night is unaffected. The 0.8/1.0 frames made with the new defaults (settings file with only `sss = true`) match the choice.
- **Eyes:** the marking happens per model (`Npc` calls `OverrideTable::markSubsurfaceModel` for the head and the skin body parts), so it cannot tell eyes from face inside a head model. In stock Morrowind the head models carry the eyes in the face texture (`b_n_breton_f_head_01.nif` has one shape and one texture), so the stock eyes stay part of the face. For heads that have separate eye shapes (replacers), `OverrideTable::resolveValues` no longer gives the mark to a texture whose name has the word "eye" or "eyes" in it (`Render::Pbr::isEyeTexture`; "eyepiece" and "eyebrow" do not count). A `texture` entry in a material file can still set `subsurface` explicitly, and it wins. Tests (`testeyeskin.cpp`) were written first.
- **Not verified:** a head with separate eye shapes (no such mesh is installed here; the rule is tested on the table, not on a loaded mesh); hands (a close-up frame was made, no visible difference from the face check).
