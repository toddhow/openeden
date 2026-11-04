# Task: Shadows from point lights
Design doc section: `docs/research/0008-modern-rendering-proposal.md` (point-light shadows are not listed there; toddhow ordered them third of five rendering features, 2026-11-04); the technique is surveyed in `docs/research/0039-point-light-shadows.md` (read it first: every source is named there, and what could not be re-read is marked).
Goal: let the nearest few point lights (torches, candles, lanterns, braziers) cast shadows from the actors and objects the shadow settings already allow, behind settings that are off by default, so that interiors lit by point lights show shadows on walls and floors.
Inherited code touched, at commit `30cb575bfb`: `components/view/shadow.*` (read, `createShadowView` reused), `components/view/collectlights.*` (`CollectLights`: read for the light list), `components/view/descriptors.*` and `components/pipeline/layout.cpp` (new bindings 17 to 19 in the view set), `components/pipeline/graphics.*`, `mode.hpp`, `terrain.cpp` (a new define), `files/shaders/lib/view/bindings.glsl` and `.slang`, `files/shaders/lib/light/pbrlighting.glsl`/`.slang`, `pixellighting.glsl`/`.slang` (the loops over the light grid), `files/shaders/default/main.frag`, `default.frag.slang`, `terrain/main.frag`, `terrain.frag.slang`, `water/main.frag` and its twin (the water's point-light specular), `apps/openmw/mwrender/rendermanager.cpp`/`.hpp` (`RenderManager::RenderManager`, `createCastShadowState`, `onFrame`), `apps/openmw/mwrender/map.cpp`, `preview.cpp`, `apps/opencs/view/render/scenehost.cpp` (stand-in descriptors), `components/settings/categories/shadows.hpp`, `files/settings-default.cfg`.
Inputs / outputs: new `[Shadows]` settings `point light shadows`, `point light shadow count`, `point light shadow resolution`, `point light shadow filter taps`, `point light shadow bias`, `point light shadow debug`; new pure headers `components/render/pointshadowmath.hpp`, `pointshadowselect.hpp`, `pointshadowgpu.hpp`; `components/view/pointshadow.hpp`/`.cpp` (the atlas, the faces' views and cameras); `files/shaders/lib/light/pointshadow.slang` and its GLSL twin `pointshadow.glsl`; tests in `apps/components_tests/render/`. No change to any file format, save, Lua API or extension contract.

Status: started 2026-11-04 on toddhow's order of five rendering features (TAA and AO accumulation done, this is the third). Full loop (Track V renderer): this spec, tests from this spec first (a separate test writer, from the interface section only), implementation, a fresh reviewer, a frame viewed before every render commit.

## Facts this is built on (checked at `30cb575bfb`; the research note's section 1 has the rest)

- Only the sun casts shadows: cascades in one `D32_SFLOAT` 2D array, `SampleCmp` with compare `GREATER`, reversed depth (`components/view/shadow.cpp`, `files/shaders/lib/view/shadow.slang`). A cascade is a `vsg::RenderGraph` over one array layer, with a `vsg::View` from `View::createShadowView` in its own command graph at submit order -3 or -1 (`RenderManager` constructor, the loop over `mShadow->shadow->numCascades`).
- `View::Shadow` exists only with `[Shadows] enable shadows`. The point-light shadows do not depend on it; they use the four caster settings (`terrain shadows`, `object shadows`, `actor shadows`, `player shadows`) and not `enable indoor shadows` (that one belongs to the sun, which has no meaning indoors).
- The cascades' objects cast only in exteriors (`createCastShadowScene`: `exterior && mObjectShadows`). The sun has no use for indoor objects; a point light does, so the point-light caster scene ignores the exterior rule.
- `CollectLights::pack()` writes `(colour, radius)` and `(eye position, 0)` per light and the count into element 0's `position.w`, during the main view's record traversal. `vsg::ViewDependentState::pointLights` (pairs of the model-view matrix and the `vsg::PointLight`) keeps the list of the last traversal until the next one clears it, so on the main thread in `onFrame` it holds the last frame's lights.
- A light's **radius never changes and its colour flickers** (`LightController::apply`, `components/mwanimation/light.cpp`).
- Lit surfaces light point lights per vertex unless `NORMAL_MAP`, `SPECULAR_MAP` or `PBR` makes the pixel path (`default/main.frag`, `PER_PIXEL_LIGHTING`). `terrain/main.frag` has `pbrPointLights` under `TERRAIN_PBR` and `pointLightsPerPixel` for maps. A per-vertex sum cannot lose one light's share in the fragment stage, so **`point light shadows` requires `pbr`** (hence `hdr` and `gbuffer`), like `ao`, `gi` and `sss`.
- The view descriptor set has bindings 0 to 16 (`lib/view/bindings.glsl`); five places create one: `RenderManager` (the main view and `createCastShadowState`), `map.cpp`, `preview.cpp` and the editor's `SceneHost`. Each must add the new bindings' stand-ins.

## Design

**Settings** (`[Shadows]`, read at start, off by default): `point light shadows` (bool, false; needs `hdr`, `gbuffer` and `pbr`, else a warning and it stays off), `point light shadow count` (1 to 8, default 4: shadowed lights at once), `point light shadow resolution` (128 to 2048, default 512: texels a face), `point light shadow filter taps` (1 to 16, default 8), `point light shadow bias` (0.25 to 6, default 1.5: texels, scales both the normal offset and the distance bias), `point light shadow debug` (bool, false: the object shader writes the first shadowed light's shadow term as a grey level instead of the lit colour, for the frame checks).

**The maps.** One `D32_SFLOAT` 2D array `res x res x (6 * count)`; a **slot** is six consecutive layers, face `f` of slot `k` is layer `6 k + f`. Faces: 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z in world axes (Z up), each a right-handed `vsg::LookAt` (eye = the light, centre = eye + forward, up as the table) with a **custom projection** (`View::PointShadowProjection : vsg::ProjectionMatrix`): reversed depth, finite far, `clip.x = x/tanHalf`, `clip.y = y/tanHalf`, `clip.z = n (f - d) / (f - n)` for `w = d` (so `clip.z / w = Render::DepthMath::ndcDepth(n, f, d)`), and the sampling coordinate `uv = 0.5 + 0.5 clip.xy / w` in the same orientation the cascades use (`clipSpaceToUv`). `tanHalf = 1 + 2 g / (res - 2 g)` with `g` = 2 guard texels (`Render::PointShadow::tanHalfFov`): the field of view is a little wider than 90 degrees so that a filter tap near an edge reads real data. `n` = 4 game units (`nearPlane`), `f` = `1.1 * Render::lightRange(radius)` (`farMargin`), fixed for the life of a slot assignment. Faces are drawn with **no culling and no depth clamp**, through a base view of their own (own override states), compiled for the context by `compileManager->add` once; every face view is made with `View::createShadowView` (null `ViewDependentState`). One command graph per slot (submit order the same as the cascades'), its children rebuilt on the main thread in `onFrame` to the faces that need drawing. The first build draws **all six faces of every assigned slot every frame**; caching is a follow-up the measurement may ask for (research note section 6).

**Casters.** The point-light scene is `terrain` if `terrain shadows`, `objects` if `object shadows` (also indoors), `actors` if `actor shadows`, the player's group if `player shadows`; the faces' view mask is `MWRender::Shadow::viewMask()`. With `enable indoor shadows` false nothing changes for point lights. The scene follows `setSkyEnabled` (exterior or interior) as the cascades' do.

**Selecting the lights** (pure, `Render::PointShadow::select`, tested): candidates are the lights of the last traversal that cast (colour positive, radius above 0). Score `importance(radius, distance) = radius^2 / (distance^2 + 1)` from the radius and the camera distance only (no colour: it flickers). A candidate must be within `reach + maxDistance` of the camera (reach = `2 * radius`; `maxDistance` 2048). A holder keeps its slot while eligible; a non-holder takes a free slot, or displaces the weakest holder that has held its slot at least `minHoldFrames` (30) when its score exceeds `holdFactor` (1.25) times that holder's. Order among equals: score, then position quantised to 1/16 unit (x, y, z), then radius; the caller's id is used only to recognise a holder from the last call. The new holder's `fade` starts at 0 and rises by `1/fadeFrames` (8) a call; the shader multiplies the shadow by `fade` (`lerp(1, shadow, fade)`). A displaced light's shadow ends at once (no fade-out in the first build).

**Receiver** (Slang `lib/light/pointshadow.slang`, GLSL twin `pointshadow.glsl`; C++ twin `pointshadowmath.hpp`, change them together). For light index `L` in the cluster's list, `entry = shadowBuffer[2 + L]`; if `entry.x < 0` (no slot) the factor is 1. Else `slotViewPos = shadowBuffer[2 + maxLights + slot].xyz`, `far = ....w`. `toReceiver = mat3(scene.invView) * (viewPos - slotViewPos)` (world axes; both positions are camera relative, so no precision is lost), the geometric normal `N = mat3(scene.invView) * normalize(frag_in.viewNormal)` (the vertex normal, as the sun's bias uses). 1. `d0` = the major-axis distance of `toReceiver`; `texel(d) = 2 d tanHalf / res`. 2. Normal offset: `P = toReceiver + N * bias * normalOffsetTexels * texel(d0)`. 3. For each of `taps` Poisson offsets (the first 16 of the sun's table, scaled by `pcfRadiusTexels * texel(d0)` in the plane spanned by the tangent basis of `N`): tap position `Q = P + t1 * o.x + t2 * o.y`; its face = `faceOf(Q)`, `(u, v, dist)` = `project(Q, tanHalf)`, compare value `depthValue(n, far, max(dist - bias * depthBiasTexels * texel(dist), n))`, `SampleCmp` of layer `6 slot + face` (hardware compare `GREATER`, bilinear). 4. The shadow is the mean of the taps; `result = lerp(1, shadow, entry.y)`. The point-light loops multiply a light's diffuse and specular by it. With `point light shadow debug`, `main.frag` writes the first shadowed light's result.

**Shader integration.** A new `Pipeline::Mode::POINT_SHADOW` (`Pipeline::Graphics::pointShadowEnabled`, added in `Graphics::create` for lit draws that are not particles and have `colorMode != 3`, as GI) and a terrain define `POINT_SHADOW`; with the setting off no define exists and the SPIR-V of every stage is byte-identical to the commit before. The loops `pbrPointLights` and `pointLightsPerPixel` (and the twins) gain `#ifdef POINT_SHADOW` code that reads a file-scope normal set by the caller. The per-vertex `pointLightDiffuse` is untouched (surfaces that reach it are not lit per pixel, which `pbr` rules out).

**Binding layout** (`components/render/pointshadowgpu.hpp`, `lib/view/bindings.*`): 17 `VIEW_POINT_SHADOW_MAP_BINDING` (sampled image, `2D_ARRAY`), 18 `VIEW_POINT_SHADOW_SAMPLER_BINDING` (sampler, compare `GREATER`, linear, clamp to edge), 19 `VIEW_POINT_SHADOW_DATA_BINDING` (storage buffer of `vec4`): `[0] = (slotCount, resolution, tanHalf, near)`, `[1] = (normalOffsetTexels, depthBiasTexels, pcfRadiusTexels, taps)`, then `maxLights` entries `(slot or -1, fade, 0, 0)` indexed by the light's index in the light buffer, then one `(view-space light x, y, z, far)` per slot. Stand-ins: a 1 x 1 x 1 depth image, a sampler, a buffer of two zero vec4s (slot count 0).

**Frame timing.** The selection runs in `onFrame` on the last traversal's lights: the maps are drawn with the light positions chosen in this call and sampled in the same frame (the slot's position is the one the faces were drawn from, in view space relative to this frame's camera), so a moving light's shadow is never out of step with its map. A light that appears is shadowed one frame after it first gets drawn.

## Constraints

Determinism: the selection depends only on its inputs (no clock, no pointer order, no unordered container); the shader has no random numbers. Allocation: the maps, views and buffers at start; per frame only the children vector of the slots' graphs and values written in place. Threading: selection, camera and buffer updates on the main thread between frames; the shadow graphs may record on worker threads like the cascades'. Off by default and unchanged when off: with the setting off no image, view, command graph, define or descriptor beyond the stand-ins exists, and a frame equals the commit before (frame comparison below). Ownership: `RenderManager` owns `View::PointShadow` by `std::unique_ptr`; it owns the image, the render graphs, the framebuffers and the data buffer.

## Interfaces (pure parts, for tests written from this spec alone)

Header-only, namespace `Render::PointShadow`, doubles unless stated. `vsg::dvec3` and `vsg::dmat4` from `<vsg/maths/...>`. Combining two vectors with `*` is deleted by the project's maths rules; use `vsg::dot`, `vsg::cross` and component arithmetic.

`components/render/pointshadowmath.hpp`:

```cpp
constexpr int facesPerLight = 6;
constexpr int maxSlots = 8;
constexpr int maxTaps = 16;
constexpr double nearPlane = 4.0;           // game units
constexpr double farMargin = 1.1;           // far plane = farMargin * the light's reach
constexpr double defaultGuardTexels = 2.0;

struct FaceAxes { vsg::dvec3 right; vsg::dvec3 up; vsg::dvec3 forward; };
// Faces 0..5 = +X, -X, +Y, -Y, +Z, -Z (forward). The up axes are in the face table below; right = cross(forward, up)
// for every face (what vsg::LookAt uses), so each row is right-handed and orthonormal.
FaceAxes faceAxes(int face);                // face outside 0..5: the face 0 axes
int faceOf(const vsg::dvec3& direction);    // the face whose forward axis has the largest component of |direction| (> or the first on ties in the order x, y, z); the sign picks + or -; the zero vector gives 0
double tanHalfFov(double guardTexels, double resolution);   // 1 + 2 g / (res - 2 g); g <= 0 gives 1; res <= 4 g (impossible) gives 1

struct FaceCoord { int face = 0; double u = 0.5; double v = 0.5; double distance = 0.0; };
// direction in world axes, from the light to the point. face = faceOf; distance = dot(direction, forward) (>= 0 for the chosen
// face); u = 0.5 + 0.5 * dot(direction, right) / (distance * tanHalf); v = 0.5 + 0.5 * dot(direction, up) / (distance * tanHalf).
// A zero vector returns the default FaceCoord.
FaceCoord project(const vsg::dvec3& direction, double tanHalf);

double depthValue(double far, double distance);           // Render::DepthMath::ndcDepth(nearPlane, far, distance)
double texelWorldSize(double distance, double tanHalf, double resolution);   // 2 distance tanHalf / resolution
struct Bias { double normalOffsetTexels = 1.0; double depthBiasTexels = 1.0; double scale = 1.5; };   // both multiplied by scale
vsg::dvec3 offsetReceiver(const vsg::dvec3& toReceiver, const vsg::dvec3& unitNormal, double tanHalf, double resolution, const Bias& bias);
    // toReceiver + unitNormal * scale * normalOffsetTexels * texelWorldSize(project(toReceiver).distance, ...)
double compareDepth(double far, double distance, double tanHalf, double resolution, const Bias& bias);
    // depthValue(far, max(distance - scale * depthBiasTexels * texelWorldSize(distance, ...), nearPlane))
void tangentBasis(const vsg::dvec3& unitNormal, vsg::dvec3& t1, vsg::dvec3& t2);   // t1, t2 unit, orthogonal to each other and to the normal, right-handed (cross(t1, t2) = normal), continuous (no branch at the poles other than the usual one); deterministic
vsg::dvec2 pcfTap(int index);   // the sun's 16-entry Poisson disk (files/shaders/lib/view/shadow.slang `poissonDisk`), index clamped to 0..15

vsg::dmat4 faceViewMatrix(int face, const vsg::dvec3& lightPosition);          // the transform of vsg::LookAt(eye = lightPosition, centre = lightPosition + forward, up)
vsg::dmat4 faceProjectionMatrix(double tanHalf, double far);                    // the matrix described in Design (clip.x = x / tanHalf, clip.y = y / tanHalf, w = -z_view, clip.z = nearPlane (far + z_view) / (far - nearPlane))

// The CPU reference of the receiver: the lit fraction (0 shadowed .. 1 lit) as the mean over `taps` of
// stored(face, u, v) < compare ... i.e. a tap is lit when compareDepth > stored(face, u, v) (compare op GREATER).
// `stored` is any callable (int face, double u, double v) -> double.
template <class Sampler>
double visibility(const vsg::dvec3& toReceiver, const vsg::dvec3& unitNormal, double far, double tanHalf, double resolution, const Bias& bias, int taps, double pcfRadiusTexels, Sampler&& stored);
```

Face table (so the tests can fix it): face 0: forward (1,0,0), up (0,0,1); face 1: (-1,0,0), (0,0,1); face 2: (0,1,0), (0,0,1); face 3: (0,-1,0), (0,0,1); face 4: (0,0,1), (0,-1,0); face 5: (0,0,-1), (0,1,0). `right = cross(forward, up)`.

`components/render/pointshadowselect.hpp`:

```cpp
struct Candidate { std::uint64_t id = 0; vsg::dvec3 position; float radius = 0.f; bool casts = true; };
struct Slot { bool used = false; std::uint64_t id = 0; vsg::dvec3 position; float radius = 0.f; std::uint32_t heldFrames = 0; float fade = 0.f; };
struct SelectionParams { int maxSlots = 4; double holdFactor = 1.25; std::uint32_t minHoldFrames = 30; double maxDistance = 2048.0; std::uint32_t fadeFrames = 8; };
double importance(float radius, double distance);       // radius^2 / (distance^2 + 1)
double reach(float radius);                               // 2 * radius
struct Selection { std::vector<Slot> slots; std::vector<int> slotOfCandidate; };   // slots.size() == maxSlots; slotOfCandidate[i] = the slot of candidates[i] or -1
Selection select(const std::vector<Candidate>& candidates, const vsg::dvec3& camera, const std::vector<Slot>& previous, const SelectionParams& params);
```
Rules: (1) eligible = `casts`, radius > 0, finite position, and `distance(camera, position) <= reach(radius) + maxDistance`; an ineligible candidate gets -1. (2) A previous slot whose `id` equals an eligible candidate's id keeps its index; its `heldFrames` rises by 1 (saturating), `fade` by `1 / fadeFrames` (at most 1), `position` and `radius` are taken from the candidate. If two eligible candidates share an id, the first in the canonical order holds it and the other is treated as a non-holder. (3) A previous slot whose id has no eligible candidate becomes unused. Slots beyond `previous.size()` start unused; `previous` longer than `maxSlots` is truncated. (4) Non-holders in canonical order (score descending, then position quantised to 1/16 unit lexicographically x, y, z, then radius ascending, then input index) take the lowest free slot, with `heldFrames` 0 and `fade` 0; with none free, a non-holder displaces the holder (not assigned in this call) with `heldFrames >= minHoldFrames` and the lowest score (ties: the lowest slot index) if its own score is greater than `holdFactor` times that holder's score. (5) The result does not depend on the order of `candidates` except through step 4's final tie-break (the input index), which only matters between candidates that are equal in every other key.

`components/render/pointshadowgpu.hpp`:

```cpp
constexpr std::size_t headerVec4s = 2;
std::size_t lightEntryOffset();                          // headerVec4s
std::size_t slotOffset(std::size_t maxLights);           // headerVec4s + maxLights
std::size_t bufferVec4s(std::size_t maxLights);          // slotOffset(maxLights) + maxSlots
struct Params { int slotCount = 0; double resolution = 512; double tanHalf = 1.0; double near = nearPlane; double normalOffsetTexels = 1.0; double depthBiasTexels = 1.0; double pcfRadiusTexels = 1.5; int taps = 8; double biasScale = 1.5; };
// Fills `out` (resized to bufferVec4s(maxLights)): the header, then for light i in 0..maxLights: (slotOfLight[i] or -1, fadeOfLight[i], 0, 0) (lights beyond the vectors: -1, 0), then the slots' vec4. The header's normal offset and depth bias are multiplied by biasScale.
void pack(const Params& params, std::size_t maxLights, const std::vector<int>& slotOfLight, const std::vector<float>& fadeOfLight, const std::vector<vsg::vec4>& slotViewPositionAndFar, std::vector<vsg::vec4>& out);
```

Shader parity (tests read the files): `files/shaders/lib/light/pointshadow.slang` and `.glsl` each contain the 16 entries of a `poissonDisk`-like table equal to `pcfTap(0..15)`, the constants `POINT_SHADOW_HEADER_VEC4S` (= `headerVec4s`) and `POINT_SHADOW_MAX_SLOTS` (= `maxSlots`), and the face table as arrays named `pointShadowFaceForward`, `pointShadowFaceUp`, `pointShadowFaceRight` of 6 entries each equal to `faceAxes`. `bindings.glsl` and `bindings.slang` define the three binding numbers 17, 18, 19.

## Acceptance criteria

- [ ] **Pure parts:** the tests of the interfaces above pass: face selection and projection against the matrices (the clip position of a point through `faceProjectionMatrix * faceViewMatrix` equals `project`'s `(u, v)` and `depthValue`, 10,000 random points, 1e-9), the face table is orthonormal and right-handed and equals `vsg::LookAt`'s, edges and corners pick a deterministic face, the guard band, bias, tangent basis, the selection rules (permutation invariance, hysteresis, minimum hold, fade, the cap, ineligible lights, determinism under 1,000 random frames, stability under a moving camera: with 30 lights and the camera moving 0.5 unit a frame the number of slot changes in 600 frames is below a stated bound), the buffer layout, and the shader parity.
- [ ] **Geometric proof (a measured test, not by eye):** a software shadow map of a known sphere occluder (the `stored` callable casts the texel's ray against the sphere and returns `depthValue`) and a plane receiver: `visibility` is 0 at points whose segment to the light crosses the sphere (inside the shadow by more than two texels), 1 at points whose segment misses it by more than two texels, and the edge lies within one texel of the analytic shadow outline, for light, sphere and plane positions that put the edge on a face's centre, near a face's edge and across a cube edge. In the game: a frame of the statue scene (below) where the luminance edge of the shadow on the wall is within 3 pixels of the position the twin predicts from the measured light, caster and wall positions.
- [ ] **No leak:** with the chosen bias, a wall of the thinnest realistic thickness (2 units, the twin's test, and a frame with a thin partition) between light and receiver gives `visibility` 0 at 1 unit behind the wall's far side for the default resolution, and the same test shows the leak distance at `point light shadow bias` 6.
- [ ] **Off means unchanged:** with `point light shadows = false` (the default) the SPIR-V of every stage is byte-identical to the commit before (the `*.permutations` manifests and a hash of every built `.spv`), no `POINT_SHADOW` define is requested in a shader trace, and the frames of the benchmark scenes equal the commit before (three off runs and the new off run compared pairwise as the stage logs of tasks 0075 and 0076 do). The editor starts (`eden-studio` rebuilt).
- [ ] **On:** in Arrille's Tradehouse (or another lit interior with torches) a torch or lantern casts shadows of a barrel or a statue on the wall and floor; an NPC walking past a lantern casts a moving shadow; a door opening changes the shadow; a cell with more lights than slots shows the cap (only `point light shadow count` lights shadowed, the selection stable while the camera turns and walks); the flickering light's shadow does not flicker in shape, only in brightness; frames viewed for each.
- [ ] **Casters follow the settings:** with `actor shadows = false` and `player shadows = false` an NPC casts none, with `object shadows = false` a barrel casts none, with all four false nothing is drawn into the maps (a frame for each).
- [ ] **Cost:** the GPU time of the faces per shadowed light (`gpu timing`) and the CPU frame time with 1, 2, 4 and 8 shadowed lights at 256, 512 and 1024 on the benchmark scenes (`tools/benchmark.sh`), the memory of the maps, and the tap count's receiver cost, in the stage log.
- [ ] **Interplay:** with `taa`, `volumetrics`, `gi`, `ao` and `ao temporal` on there is no new artefact beyond what the stage log lists, and the shadows do not shimmer under TAA.
- [ ] A fresh reviewer's findings are fixed or recorded.

## Out of scope

Light shafts from point lights (the volumetric fog's fill pass), static map caching and a second map for dynamic casters (the first build draws every face every frame; stage e decides from the measurement), a fade-out of a displaced light, point-light shadows in the editor's views and the map and preview scenes (they get the stand-ins), shadows from the projectile lights and the spell glow lights beyond what the selection gives them, contact-hardening (PCSS) penumbras, dual paraboloid and cube-map storage, per-vertex lit surfaces (the setting needs `pbr`).

## Stages

a: the pure headers and their tests (tests first, a separate writer); b: the settings, the maps, the faces' views and cameras, the command graphs, the data buffer and the selection in `onFrame` (frames of the maps through the debug term); c: the object shader's sampling (GLSL and Slang), the statue scene; d: terrain and water; e: the proof frames, the cost, caching if asked; f: the reviewer, the guides and the changelog.

## Stage log

(empty: written as the stages are done)
