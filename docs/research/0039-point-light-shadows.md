# Shadows from many point lights (task 0077)

2026-11-04. Research for `docs/tasks/0077-point-light-shadows.md`. Nothing here is code taken from an engine or an SDK: the
techniques are described in the literature and the numbers below are derived or marked as estimates. Sources are named
where they are used. Papers and books were cited from memory of the authors' own descriptions and were **not re-read in
this session**; where a claim rests on one it says "verify". Engine facts cite a file and function in this repository.

## 1. What the game has today (checked in the code)

| Fact | Where |
| --- | --- |
| Only the sun casts shadows: 1 to 4 cascades in one `D32_SFLOAT` 2D array, `SampleCmp` with compare `GREATER`, reversed depth. | `components/view/shadow.cpp` (`Shadow::Shadow`), `files/shaders/lib/view/shadow.slang` |
| The casters of the cascades come from four settings (`terrain shadows`, `object shadows`, `actor shadows`, `player shadows`); objects cast only in exteriors (`exterior && mObjectShadows`). | `RenderManager::createCastShadowScene`, `MWRender::Shadow::viewMask` |
| A cascade is a `vsg::RenderGraph` with its own framebuffer on one array layer, a `vsg::View` made by `View::createShadowView` (null `ViewDependentState`, shared view id), in its own command graph at submit order -3 or -1. | `RenderManager` constructor (`mShadow->shadow->renderGraph(i)`) |
| Point lights: a storage buffer of (colour, radius) and (eye position) pairs filled during the main view's record traversal; element 0's `position.w` holds the count. A compute pass assigns up to 50 lights per cluster. | `View::CollectLights::pack`, `files/shaders/comp/light/assign.comp`, `lib/light/maxlights.glsl` |
| Lighting by point lights is per vertex for plain surfaces and per pixel only with a normal map, a specular map or `PBR`. | `default/main.frag` (`PER_PIXEL_LIGHTING`), `default/main.vert` (`pointLightDiffuse`) |
| A light's **radius is constant and its colour flickers**: `LightController::apply` writes `light.color = mBaseColor * mBrightness` (brightness 0.25 to 1) and never touches `intensity`, which is the record's radius. The light's position never changes except with its parent. | `components/mwanimation/light.cpp` |
| The light's reach is `2 * radius` (fade from `radius` to `2 * radius`) unless `classic falloff` is on. | `files/shaders/lib/light/equations.slang` (`lightRange`), `components/render/probebake.cpp` (`lightRange`) |
| The depth buffer is reversed: stored depth is `n (f - d) / ((f - n) d)`, compare `GREATER`. | `components/render/depthmath.hpp` (`ndcDepth`) |

## 2. Representations

| Option | What it is | For | Against |
| --- | --- | --- | --- |
| Cube map per light (`VK_IMAGE_VIEW_TYPE_CUBE` / `CUBE_ARRAY`) | 6 square faces, hardware picks the face and filters across edges | One `SampleCmp` per tap, seamless edges | Needs the `imageCubeArray` device feature for an array; face orientation conventions of the API must be matched exactly; the compare value is still the major-axis depth |
| Dual paraboloid (Brabec, Annen, Seidel, "Shadow Mapping for Hemispherical and Omnidirectional Light Sources", 2002; verify) | 2 maps, a non-linear warp in the vertex shader | 2 passes instead of 6 | Needs the warp in every caster's vertex shader (the game has one shared object vertex shader and skinning, morph and instancing variants) and tessellation for large triangles: straight edges become curves. Rejected |
| **One 2D array of square faces, 6 layers per light, face chosen in the shader** | the cascades' own storage kind | Same machinery as the cascades (`SampleCmp`, `D32_SFLOAT`, one framebuffer per layer); no extra device feature; we own the face table, so the C++ twin is exact; a tap can choose its face on its own, so filtering across an edge is correct | A few more ALU per tap than a hardware cube lookup |
| A single large 2D atlas with faces as tiles | one image, viewports | One framebuffer | Tiles bleed into each other under bilinear filtering and need padding; reuse of a tile needs allocation logic. A layer per face gets the same without a padding rule |

**Choice: a 2D array, six layers per shadowed light ("slot"), a guard band round each face.** The face field of view is
widened a little beyond 90 degrees (`tanHalf = 1 + 2 g / (res - 2 g)` for `g` guard texels) so that the bilinear footprint
of a tap near a face edge reads real data. The sampling code projects each tap itself, so a tap a few texels beyond the
edge picks the neighbouring face and the filter is continuous across the edge.

Memory: `D32_SFLOAT` 4 bytes per texel: 512 px per face, 4 lights: 4 x 6 x 512^2 x 4 B = 24 MiB; 256 px: 6 MiB; 1024 px:
96 MiB. (Exact: derived.)

## 3. Which lights get a shadow

Interiors of Morrowind have tens of point lights in view; a cell can have several hundred. Only a handful can be shadowed.

- **Score** from quantities that do not flicker: the light's radius `R` (constant, section 1) and its distance `D` from the
  camera: `score = R^2 / (D^2 + e)`. The colour is left out of the score on purpose: it changes every frame (flicker) and
  would swap lights in and out. A light also needs a reach that can touch the view: `D < R_reach + maxDistance`. A light
  with colour zero or a negative colour (a darkness light) does not cast a shadow.
- **Hysteresis.** A light that holds a slot keeps it unless a candidate scores above `hold * score(holder)` (`hold` 1.25),
  and a slot's holder is not displaced earlier than `minHoldFrames` after it was assigned. The lowest-scoring holder is the
  one displaced. This is the "Schmitt trigger" every game uses in one form or another; no source is needed.
- **Determinism.** The order that decides ties must not come from memory addresses. Candidates are ordered by score, then
  by their quantised world position (1/16 unit, x then y then z), then by radius bits; the caller's opaque id is used to
  recognise a holder from the last frame, never to order. This is a pure function of its inputs, so it can be tested and
  repeated.
- **Fade.** A light that gains a slot fades its shadow in over a few frames and one that loses it fades out (the slot is
  kept until the fade is over), so a change in the selected set is not a pop. The shader multiplies the shadow term by
  the slot's fade.
- **Cap.** At most `point light shadow count` slots (1 to 8, default 4), whatever the number of lights.

## 4. Resolution tiers

A face of `res` texels covers 90 degrees plus the guard, so a texel at distance `d` spans `2 d tanHalf / res` world units.
For a light of reach 2R = 1000 units (a Morrowind brazier) at d = 300: `res` 256 gives 2.9 units (about 4 cm) a texel,
512 gives 1.5 and 1024 gives 0.7. Characters are about 128 units tall, so 512 is the reasonable default and 256 the weak
tier. Tiers: **low** 256 and 2 lights, **medium** 512 and 4, **high** 1024 and 6 (estimates; to be tuned with measured cost,
section 10).

## 5. Caster culling

- **Frustum:** each face is a `vsg::View` with a camera; the record traversal culls with the camera's frustum, which
  includes the far plane, so a face does not draw anything beyond the light's reach and sees only its 90 degree wedge.
  No extra code is needed (the cascades rely on the same culling).
- **Range:** the far plane is the light's reach (with a margin, section 8). A model whose bounding sphere lies beyond it
  is culled with the far plane.
- **A light's own model:** a lantern's glass, a torch's holder or a candle's mesh would shadow the whole room if it sat in
  front of the light. The near plane protects only what is closer than `n`. Choice: near plane `n` = a constant (4 units)
  and, for the model that carries the light, nothing special in the first build: the light is placed by the content at the
  flame, usually outside the mesh's solid parts (a lantern's glass is transparent and blended, which the caster pass does
  not draw because blended draws are not written to depth: Section 8 checks this in a frame). A frame with a lantern is a
  test.
- **Depth clamp off:** the cascades turn depth clamp on so that casters in front of the near plane still write. For a
  perspective face that would write geometry behind the near plane at near depth, which is the failure above; the point
  faces keep clamp off and clip.
- **Cull mode:** the cascades cull front faces by setting. Interior walls are single-sided shells; drawing only their
  back faces would draw nothing for a wall seen from inside. The point faces draw both sides (`cull none`) and use a
  normal offset for the receiver (section 7).

## 6. Static versus dynamic casters (caching)

Rendering every face every frame costs (section 10) `faces x (scene draws in the wedge)`. The usual remedy (verify; this
is common knowledge of engine programmers, no single source): render a light's static casters once into its map and
re-render only when something changes; with a moving caster in range keep a second map, or re-render the whole.
Rules that are cheap to decide here:

1. A map must be redone when the light has moved by more than a tolerance, when it is assigned, or when its far plane
   changes. Flicker changes only the colour (section 1), so it never forces a redo.
2. A caster in range that moves (an actor, a door being opened) forces a redo of the faces its bounding sphere touches.
3. Static objects change when the world changes (a cell loaded, an object moved by script or deleted).

The first build re-renders all faces of all slots **every frame** (always correct). Measured cost then decides whether
caching is needed (task 0077, stage e). The design keeps a per-slot "needs render" flag so rule 1 alone can be added
without a second map.

## 7. Bias, normal offset and leaking

- **Depth bias** in a perspective map is best expressed in world units along the sampling axis, not in depth units, since
  the depth is non-linear. The receiver's distance is reduced by `depthBiasTexels * texelWorld(d)` before it is turned into
  the map's depth value.
- **Normal offset** (the receiver is moved along its surface normal by a fraction of a texel's world size before the
  lookup): from Holbert's GDC talk "Saying 'Goodbye' to Shadow Acne" / "Normal Offset Shadows" (verify the title and
  year); the idea only is used. It handles slopes without a slope-scaled hardware bias, and it uses the geometric
  normal (the vertex normal, not the normal map, as the sun does: `default/main.frag`'s comment "the shadow bias uses the
  geometry normal"). Offset: `normalOffsetTexels * texelWorld(d)`; the texel size grows with `d`, so the offset does too.
- **Light leaking through thin walls** is the offset's price: an offset larger than the wall's thickness reads the far
  side. Morrowind walls are commonly a few units thick or one-sided shells. At 512 px and `d` = 200 the texel is about
  0.9 units, so an offset of 1.5 texels is 1.4 units. The test in task 0077 puts a wall of the thinnest realistic thickness
  between light and receiver and measures the leak against the chosen bias; the setting `point light shadow bias` lets
  the user trade.
- **Receiver in front of an occluder's back face** is solved by drawing both sides (section 5), at the cost of the bias
  and offset.

## 8. Filtering

Percentage closer filtering (Reeves, Salesin, Cook, "Rendering Antialiased Shadows with Depth Maps", 1987) over `N` taps
(4, 8 or 16 of the 16 point Poisson disk the sun already uses) laid out in the receiver's tangent plane with radius
`pcfRadiusTexels * texelWorld(d)`. Each tap is a world position, so it picks its own face, and the hardware compare
filters 2 x 2 texels more. PCSS (Fernando, "Percentage-Closer Soft Shadows", NVIDIA, 2005) needs a blocker search and a
light size; Morrowind's lights have no size, and a fixed penumbra is close enough. The taps are not rotated per pixel
(deterministic frames, no noise for the temporal passes); with `taa` on the 4-tap pattern can be rotated by the
frame's jitter later (not built).

The **far plane with flicker:** the radius does not flicker, so the far plane is `lightRange(R) * 1.1`, fixed for the
life of the slot. If the radius ever changes (a script), the slot is re-assigned when the new reach exceeds the slot's
far plane.

## 9. Interplay

- **`pbr` (required).** Plain surfaces light point lights per vertex (section 1); a per-vertex sum cannot lose one
  light's contribution in the fragment stage. With `pbr` on, every lit surface takes the per-pixel path (`PBR_LIT`), where
  the loop over the lights can scale each light. So `point light shadows` needs `hdr`, `gbuffer` and `pbr` (as `ao`, `gi`
  and `sss` do). Without it the setting is refused with a log line. The alternative (skip shadowed lights in the vertex
  sum) is not built: it needs the slot data in the vertex stage and in every vertex twin.
- **TAA:** the maps are static in screen space so nothing changes; the 8 tap pattern is stable.
- **`volumetrics`:** light shafts from point lights would sample the maps from the froxel fill pass; a possible
  follow-up, not required.
- **`gi` and `ao`:** unaffected; the baked probes are not shadowed by this (they already include occlusion).

## 10. Cost model

Per shadowed light and frame: 6 faces x (record + draw all casters in the 90 degree wedge within reach). The GPU cost is
dominated by vertex work and fill of D32 at `res^2` (512 px: 0.26 Mtexel a face, 1.6 Mtexel a light), tiny against the main
pass's 0.9 Mpixel plus G-buffer; the CPU cost (a record traversal per face) usually matters more: the notes in
`apps/openmw/mwrender/CLAUDE.md` say the game is CPU-limited (the frame is 2 to 4 ms with the GPU main pass at 0.35 ms).
Estimates before measuring: 0.1 to 0.3 ms GPU and 0.3 to 1 ms CPU per light with every face redrawn in an interior of
a few hundred draws. These are estimates and are replaced by the stage-e measurements. Receiver cost: `taps x (face
select + projection + SampleCmp)` per shadowed light per pixel, about 30 ALU a tap; 4 lights x 8 taps = 32 lookups, which
is why the cap and the tap count are settings.

Weak GPUs: a software device (Mesa llvmpipe) draws the 24 faces on the CPU; the tier table lowers count and resolution
(section 4), and `point light shadows` is off unless the user turns it on (never part of `auto`).

## 11. Decisions the design rests on

1. 2D array, 6 layers per slot, own projection (reversed depth, finite far), face chosen in the shader, guard band.
2. `pbr` is required (per-pixel lighting).
3. Selection by non-flickering score with hysteresis, minimum hold time and a deterministic order.
4. Both sides drawn, no depth clamp, normal offset plus a distance bias.
5. Every face every frame in the first build; caching only if the measurement asks for it.
6. A sampling include written in Slang (`lib/light/pointshadow.slang`) and its GLSL twin for the shaders still in GLSL
   (object, terrain, water).

## 12. Sources

- L. Williams, "Casting Curved Shadows on Curved Surfaces", SIGGRAPH 1978 (shadow maps).
- W. Reeves, D. Salesin, R. Cook, "Rendering Antialiased Shadows with Depth Maps", SIGGRAPH 1987 (PCF).
- S. Brabec, T. Annen, H.-P. Seidel, "Shadow Mapping for Hemispherical and Omnidirectional Light Sources", 2002.
- R. Fernando, "Percentage-Closer Soft Shadows", NVIDIA, SIGGRAPH 2005 sketch.
- Akenine-Moller, Haines, Hoffman, "Real-Time Rendering", 4th edition, chapter 7 (shadows): general survey.
- D. Holbert, "Saying 'Goodbye' to Shadow Acne" / normal offset (verify).
- Khronos, "Vulkan 1.3 specification": `imageCubeArray` device feature, image layouts, depth clamp, cube map face selection.
- This repository: the files named in section 1.
