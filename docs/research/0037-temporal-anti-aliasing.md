# Temporal anti-aliasing as games use it (research for task 0075)

Date: 2026-11-04. Author: the TAA agent (task 0075). Nothing here is code from any engine; the formulas in the task are written from the descriptions in the papers and talks named below. What I could read myself is marked **read**; what I know from the literature but could not open this session is marked **recalled** and is to be verified before anyone relies on a number from it. No dependency is needed (nothing is added to `docs/dependencies.md`).

## 1. Sources

| Source | What it is | Status |
| --- | --- | --- |
| B. Karis, "High Quality Temporal Supersampling", SIGGRAPH 2014, course "Advances in Real-Time Rendering in Games", part I. Course page `https://advances.realtimerendering.com/s2014/index.html` | Unreal Engine 4's TAA: a geometry phase (sub-pixel jitter) and a resolve pass; no spatial edge finding; sections on HDR, ghosting, translucency, edges in motion, flicker and post-processing integration | **read** (the course page's abstract; the slides, `epic/TemporalAA.pptx`, I could not open, so every detail attributed to it below is **recalled**) |
| M. Salvi, "An Excursion in Temporal Supersampling", GDC 2016 (NVIDIA), `https://developer.download.nvidia.com/gameworks/events/GDC2016/msalvi_temporal_supersampling.pdf` | Introduces variance clipping | **read** (text extracted from the PDF) |
| L. J. F. Pedersen, "Temporal Reprojection Anti-Aliasing in INSIDE", GDC 2016 (Playdead), `https://www.gdcvault.com/play/1022970` | A production TAA with a velocity-based feedback and a clip toward the box centre | abstract **read**; the slides **recalled** |
| L. Yang, S. Liu, M. Salvi, "A Survey of Temporal Antialiasing Techniques", Computer Graphics Forum 39(2), Eurographics 2020, `https://diglib.eg.org/handle/10.1111/cgf14018` | The overview: reprojection, history rectification (clamp, clip, variance clip), history weight, resampling, sharpening, ghosting and its cures | listing **read**; content **recalled** |
| L. Yang, D. Nehab, P. V. Sander, P. Sitthi-amorn, J. Lawrence, H. Hoppe, "Amortized Supersampling", ACM TOG 28(5), SIGGRAPH Asia 2009 | The origin of reusing the previous frame's resolved colour through a reprojection with an exponential average | **recalled** |
| J. H. Halton, "On the efficiency of certain quasi-random sequences of points in evaluating multi-dimensional integrals", Numerische Mathematik 2, 1960 | The radical-inverse sequence (bases 2 and 3 for the jitter) | **recalled** |
| J. Jimenez, "Filmic SMAA: Sharp Morphological and Temporal Antialiasing", SIGGRAPH 2016 (Advances course) | A temporal resolve with a 5-tap Catmull-Rom history lookup, and the argument for sharpening the resolve | **recalled** |
| Patent US10116916B2 (NVIDIA; Salvi, Patney, Lefohn; "Method for data reuse and applications to spatio-temporal supersampling and de-noising"), `https://patents.google.com/patent/US10116916B2/en` | Described in section 9 | description **read** (the claims I could not open) |

## 2. What the technique is

Spatial anti-aliasing (MSAA, FXAA) works on one frame. Temporal anti-aliasing gets the extra samples from earlier frames: the projection is shifted by a different sub-pixel amount every frame (the jitter), so each frame samples the scene at slightly different places inside the pixel; the resolve pass blends the current frame with the previous frame's resolved result, moved to where the same surface was (reprojection). Salvi (**read**, slides 12 and 13): the pixel colour is a moving exponential average, "typically we blend 10% of the current frame with 90% of the past frame". The resolved colour of the last frame is all that is kept (not the individual samples), which is what makes it cheap, and what makes it fragile: an average cannot tell old data that is still valid from old data that belongs to something else.

The three problems every TAA solves are therefore: (1) where was this pixel last frame (reprojection: velocity and depth), (2) is the old colour still valid there (rectification and rejection), (3) the blur of resampling the old colour at a non-integer position and the softness of the average (filtering and sharpening).

## 3. Jitter

- **Sequence.** Halton with bases 2 and 3 (Halton 1960), `(H2(i) - 0.5, H3(i) - 0.5)` for `i = 1 .. period`, repeating. Low-discrepancy and cheap, and what Karis names (**recalled**). A period of 8 to 16 is common (**recalled**); more samples give a better-converged edge but the history is only a few frames deep anyway (with a 90% feedback the effective depth is about 10 frames). The index is a frame counter, never the clock (determinism rule of the project).
- **Amplitude.** The jitter stays inside the pixel: plus or minus half a pixel (**recalled**, Karis). A scale below 1 trades edge quality for stability.
- **How it is applied.** As a translation in clip space: `x_clip += (2 jx / width) * w_clip`, `y_clip += (2 jy / height) * w_clip` (Vulkan's NDC y points down, as does the image's row order, so a pixel offset in image coordinates is the NDC offset directly). Applied to the projection the camera draws the scene with. It must not reach anything that reconstructs positions from the **unjittered** camera: the velocity pass, the fog and cloud volumes, the AO's projection, shadow cascade fitting.
- **Velocity and the jitter.** A velocity buffer that is computed as `currentUV - previousUV` from the unjittered camera needs no correction. The skinned-actor motion output of this renderer is the difference of two screen positions that both go through the same (jittered) projection, so the jitter cancels (`default/main.vert`, `GBUFFER_MOTION`). The history therefore lives on the unjittered pixel grid and the current frame's samples are the jittered ones.

## 4. Reprojection

`previousUV = currentUV - velocity`. For static scenery the velocity comes from depth and the two cameras; for the sky (no depth) only the rotation matters (the point is at infinity), so the direction is moved with the previous camera's rotation. Pedersen and Karis both take the velocity of the **closest** pixel of a 3 by 3 neighbourhood (**recalled**), so that a pixel on the edge of a moving foreground object follows the object, not the background behind it.

## 5. Rectifying the history

- **Clamp.** Limit the old colour to the range of the current frame's 3 by 3 neighbourhood (min and max per channel). It removes ghosts but also turns still detail into noise.
- **Clip.** Do not clamp each channel; move the old colour along the line to the box centre until it enters the box (Karis, Pedersen: **recalled**; Salvi **read**, slides 18 to 20: "connects with a segment the old pixel colour with the new sample colour ... clipping this segment against the convex hull"). A clip keeps the hue the old colour had instead of snapping channels independently.
- **Variance clipping** (Salvi, **read**): build the box from the first two raw moments of the 3 by 3 neighbourhood: its mean `mu` and standard deviation `sigma`, box = `mu +- gamma sigma`, "we typically use gamma = 1", larger gamma "more temporally stable results at the cost of increased ghosting", and the new box can be clamped against the plain min/max box so that it is never larger. Salvi also warns (**read**, slide 39) that a thin feature that falls between the jittered samples removes its contribution from the current distribution, the variance collapses and the history is clipped away: flicker on thin geometry, cured with a better spatial pre-filter.
- **Colour space.** A box in YCoCg (a decorrelated luma and two chroma axes) is a tighter fit than one in RGB (Karis, **recalled**).
- **HDR.** The scene here is linear HDR before the tonemap; a very bright pixel (a lamp, the sun) is an outlier of a thousand times the neighbours, which makes a box useless and an average flicker. The fix in the literature is to work in a compressed domain, either by tonemapping before the resolve and inverting after, or by weighting each sample with `1 / (1 + luma)` (Karis, **recalled**). Because the project's tonemap is the last step and bloom and the sun glow depend on unclipped values, this work resolves in a **reversible luma compression**, `c / (1 + luma(c))`, and expands the result: the stored colours stay linear HDR.

## 6. The history weight

Salvi (**read**): 10% current, 90% history is typical. Pedersen (**recalled**) lowers the feedback when the history had to be clipped a long way and when the pixel moves fast: the old colour is less reliable then (a bilinear or even a bicubic lookup at a non-integer position is blurrier the more the pixel moves). Disocclusion (something appeared that was hidden last frame) is detected by comparing the surface's depth now, expressed in the previous camera, with the depth stored in the history at the reprojected position; a mismatch sets the weight to 0. This work stores a log-scaled linear depth in the alpha channel of the history for that test.

## 7. Resampling and sharpening

- Reading the history with a bilinear filter blurs a little more every frame it is moved; the answer in practice is a Catmull-Rom filter, reduced to five bilinear taps by merging the two middle weights (Karis, Jimenez: **recalled**).
- The average itself is soft. A mild sharpen after the resolve restores contrast; it is applied to the **displayed** picture and not to the stored history, or it would be applied again every frame and ring (Karis: **recalled**). A contrast-adaptive sharpen that clamps to the neighbours' range avoids halos; the idea is what AMD's FidelityFX CAS popularised (not used here; its code is not read or copied; the project's own cross filter below is a plain unsharp mask limited to the local range).

## 8. Things TAA cannot see

- **Transparent surfaces and particles** write no depth and no velocity in this renderer (task 0026's transparency rule): the history behind them is reprojected, not the transparent layer itself, so rain, fog sheets, water and glowing particles leave a faint smear when the camera or the object moves fast. Karis handled translucency with a stencil bit and a lower weight (**recalled**); here the answer is the clamp plus a fast-motion weight drop.
- **Moving objects that move as a whole** (an NPC walking across the ground, a swinging door) have only camera motion in the velocity buffer (task 0026, stage c: only skinned bone motion is added). The history of such an object comes from where the camera says the surface was, which is the wrong place; the neighbourhood clip rejects it in most cases and leaves a trail of a frame or two where it does not. This is the largest limit of this implementation and is measured in the task.
- **Shader aliasing and fine detail** that the jitter turns into noise (a specular sparkle) can flicker; the clip's gamma controls how much is kept.

## 9. Legal note for toddhow

- Salvi's variance clipping is the subject of **a granted US patent assigned to NVIDIA (US10116916B2, priority 2016-03-17, listed active to 2037)**. Its description (the claims I could not read) says the mean and variance of the neighbourhood colours build an axis-aligned box that the history is clamped or clipped to, which is the method of the talk. Neighbourhood **clipping to a min/max box** is older (Karis 2014, cited by Salvi as Malan 2012 and Karis 2014) and is the fallback. I am not a lawyer and did not read the claims. The implementation keeps both methods behind `[Rendering] taa clipping` so that dropping the variance box is one setting and one default; **toddhow decides the default** (the spec's open question).
- The Playdead INSIDE temporal code is, according to a search snippet, under the MIT licence; it was not read and nothing was taken from it.

## 10. How it fits this renderer

- The HDR target (`hdr`) is a linear float target; `gbuffer` stores linear depth, normals and (task 0026) velocity from camera motion and skinned bones. A TAA resolve slots between the scene and the bloom and tonemap, reads the target's colour, the linear depth and the velocity, and writes a colour image that bloom and the tonemap read in place of the target's.
- MSAA: the extra outputs of an MSAA target are averaged at edges, so the velocity and depth at an object's silhouette are blends; a TAA on top of an MSAA picture pays for both and gains little. **Decision: TAA requires the scene to be single-sampled.** With `[Video] antialiasing` above 0 the game logs a warning and runs without TAA, and the setting is never changed. (The alternative, resolving per sample, would give up the stored images' simple layout for a cost the technique exists to avoid.)
- The AO, SSS and volumetrics read this frame's jittered, aliased G-buffer. AO's darkening is applied in the tonemap, after the resolve, from the current frame's half-resolution image: it is not temporally accumulated (that is the next feature's job) and its silhouette edges are not anti-aliased by the resolve. The volumetric fog and clouds are smooth by themselves and pass through the resolve like any colour; the clouds have a temporal filter of their own (task 0042) and the extra filtering lowers their noise further.
