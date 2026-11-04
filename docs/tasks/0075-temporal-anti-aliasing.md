# Task: Temporal anti-aliasing (TAA)
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, section 4 (F3, "temporal anti-aliasing with motion vectors") and item 3 of the priority list; the technique is surveyed in `docs/research/0037-temporal-anti-aliasing.md` (read it first; every source is named there, and what could not be read is marked).
Goal: remove the stair-stepping of edges and the shimmer of fine detail with a sub-pixel projection jitter and a history resolve in linear HDR before the tonemap, behind one setting that is off by default, without MSAA's cost, and leave a documented temporal state (frame index, jitter, history validity) for the next feature (accumulated ambient occlusion).
Inherited code touched, at commit `6c7983a8cd`: `apps/openmw/mwrender/rendermanager.cpp` and `.hpp` (`RenderManager::RenderManager`, `onFrame`, `updateVelocity`, `syncExtents`, `setFirstPersonMode`), `components/settings/categories/rendering.hpp`, `files/settings-default.cfg`, `files/shaders/CMakeLists.txt`, `apps/components_tests/CMakeLists.txt`, `components/CMakeLists.txt`. Read, not changed: `components/render/velocity.*`, `tonemap.*`, `bloom.*`, `hdrtarget.*`, `components/view/cloudvolume.*` (the model for a compute pass with a history), `files/shaders/post/velocity.frag`, `files/shaders/default/main.vert` (`GBUFFER_MOTION`).
Inputs / outputs: new `[Rendering]` settings `taa`, `taa sharpness`, `taa jitter scale`, `taa history`, `taa clipping`, `taa debug`; new `components/render/taamath.hpp` (pure), `taa.hpp`/`taa.cpp` (the pass and the jittered projection), `files/shaders/post/taa.slang` (Slang, decision 0009), tests in `apps/components_tests/render/`; the camera's projection gains a clip-space shift when the setting is on; bloom and the tonemap read the resolved image instead of the target's colour. No change to any file format, save, Lua API or extension contract.

Status: started 2026-11-04 on toddhow's order of five rendering features (TAA first). Full loop (Track V renderer): this spec, tests from this spec first (a separate test writer), implementation, a fresh reviewer, a frame viewed before every render commit.

## Facts this is built on (checked at `6c7983a8cd`)

- **The scene draws in `HdrTarget`** (`hdr`), a float target; with `gbuffer` it also stores the view-space normal, the linear depth (R32F, distance along the view axis, 0 where nothing opaque was drawn), the skinned actors' screen motion and more (`HdrTarget::linearDepthView()`, `objectMotionView()`). `Render::Velocity` (`velocity.cpp`) turns the linear depth and one matrix (`prevProjection * prevView * currentInvView`, formed in double) into `currentUV - previousUV` (RG16F) and adds the object motion; it returns 0 where the linear depth is 0 (`files/shaders/post/velocity.frag`). Camera: `RenderManager::updateVelocity` resets the previous matrices on the first frame, after a jump over 4096 units (`Velocity::cameraJumped`) and after a gap over 250 ms.
- **The camera's projection** is `mProjectionMatrix` (`vsg::Perspective`, `rendermanager.cpp` `updateProjectionMatrix`). Its `transform()` is read by `updateVelocity`, the fog and cloud volumes' `setParams`, the AO projection, the SSS focal length, `updatePostProcess`, `getProjectionMatrix()` and `sceneData.projection`; `Shadow::updateCascades(*mSceneCamera, ...)` and the intersector take `mSceneCamera`. `pc.projection` in `default/main.vert` comes from the camera the view draws with (`vsg::View::camera`). The first-person arms and weapon draw with another projection, `vsgUtil::FieldOfView` over the same `Perspective` (`setFirstPersonMode`, `components/vsgutil/projection.hpp`).
- **The depth buffer is reversed**; the linear depth image has no convention (task 0066).
- **`GBUFFER_MOTION`** (`default/main.vert`) multiplies the current and the previous-bones vertex by the same `pc.projection`, so a jitter in that projection cancels in the difference.
- **Command order today** (`rendermanager.cpp`, around the `afterScene` count): scene, velocity, AO, bloom, tonemap, post-process, the GUI graph. Compute passes in the command graph and their barriers: `View::CloudVolume` (`components/view/cloudvolume.cpp`) is the model: storage images in `GENERAL`, a `vsg::Switch` for the first frames' layout transition, `vsgUtil::CompileContext` for a runtime compile.
- **MSAA**: `Engine::framebufferSamples()`; the HDR target's extra outputs resolve by averaging (`hdrtarget.hpp`).

## Design

**One setting, chosen once at start.** `[Rendering] taa = false` (default). It takes effect only with `hdr` and `gbuffer` on and a single-sampled scene (`useTaa`): otherwise a warning is logged and the renderer is exactly as without it.

**MSAA and TAA: they do not coexist. TAA requires `framebufferSamples() == 1`.** With `[Video] antialiasing` above 0 and `taa` on, the game logs "taa needs a single-sampled scene ([Video] antialiasing = 0); temporal anti-aliasing is off" and runs on MSAA; the user's antialiasing setting is never changed. Why: the extra outputs resolve by averaging, so at every silhouette (where TAA matters) the stored depth and velocity are blends of two surfaces; resolving each sample would mean multisampled storage images and a larger resolve for a technique whose point is to avoid MSAA's cost; and an MSAA picture under a TAA gains little for twice the memory traffic. Recorded as a decision for toddhow in "Decisions".

**The jitter.** `Render::Taa::haltonJitter(frame)` gives the Halton (2, 3) sample of the frame in pixels, in -0.5 to 0.5, period `jitterPeriod` (16), scaled by `taa jitter scale` (0 to 1, default 1). The frame counter is advanced only in `RenderManager::onFrame` (never from a clock), so a scripted run is repeatable. It is applied as `Render::Taa::jitterMatrix` (identity with `m(3, 0) = 2 jx / width`, `m(3, 1) = 2 jy / height`, VSG's `(column, row)` indexing: `clip.x += ndcX * clip.w`) multiplied in front of the projection, in a small `JitteredProjection` (a `vsg::ProjectionMatrix` holding the base projection and a shared `JitterOffset`; `changeExtent` does nothing, because `vsg::RelativeProjection::changeExtent` would rescale the offset on a resize). Only two cameras get it: **the view's camera** (a second `vsg::Camera`, `mRenderCamera`, sharing `mViewMatrix` and `mSceneCamera`'s viewport state) and **the first-person projection**. `mProjectionMatrix` and `mSceneCamera` stay unjittered, so every consumer listed above keeps the true camera (cascade fitting, picking, velocity, the volumes, AO, SSS, post-processing). With `taa` off the wrapper does not exist and the view's camera is `mSceneCamera`, as before: unchanged by construction.

**The resolve is a Slang compute pass** (`files/shaders/post/taa.slang`, loaded whenever `taa` is on, whatever `slang shaders` says, like the clouds; a missing file turns TAA off with a log line). No render pass, no GLSL vertex stage, so the "a pipeline takes its stages' twins together" rule does not arise. Two dispatches in a node placed after the AO graph and before bloom:

1. `resolveMain` (per pixel): the current colour and its 3 by 3 neighbourhood in **YCoCg of the luma-compressed colour** (`compress(c) = c / (1 + luma(c))`, so a lamp or the sun cannot dominate the box); the neighbourhood box (`taa clipping`: `variance`, the moments' mean plus or minus `varianceGamma` times the standard deviation, limited to the min/max box, after Salvi 2016; or `minmax`); the velocity of the closest-depth pixel of the 3 by 3 (a pixel with no depth in the whole neighbourhood is sky: its velocity is the **rotation-only** reprojection of its view direction with the same `toPreviousClip` matrix applied to `(dir, 0)`, because `velocity.frag` gives 0 for it); the history read with a 5-tap Catmull-Rom filter at `uv - velocity`; the history clipped toward the box centre; a **history weight** `finalHistoryWeight`: `taa history` (default 0.9), reduced by the pixel speed, forced to 0 for a reset, outside the screen, or on **disocclusion** (the stored log-depth of the history at the reprojected pixel against the depth this surface had in the previous camera, `prevClip.w` of the same matrix: `depthReject`); the result `expand(lerp(compress(current), compress(history), weight))`, written with `log2(1 + linearDepth)` (or -1 for sky) in alpha as the next frame's depth for the disocclusion test.
2. `outputMain` (per pixel): reads the resolve, writes the **history** (the same colour, unsharpened) and the **output** image (the same colour sharpened by `taa sharpness`: an unsharp mask on the plus-shaped neighbourhood in the compressed domain, limited to the range of those five, so it cannot ring; 0 is an exact copy).

Images (RGBA16F, the target's size, usage storage and sampled): resolve, history, output. Layouts: resolve and history `GENERAL` for good, output `GENERAL` while written and `SHADER_READ_ONLY_OPTIMAL` after, where bloom and the tonemap read it (`Taa::result()`). A barrier from `ALL_COMMANDS` (the scene, velocity and AO passes, including the render passes' layout transitions, as the screenshot code argues) to the compute stage opens the node and a second from compute to the fragment stage with the layout transition closes it. The first three frames after a build also discard the history image's contents (`UNDEFINED` to `GENERAL`, the model of `CloudVolume`'s `mFirstSwitch`), and take no history from it.

**Resets (`historyReset`).** The history weight is 0 on the first frame, after a resize (the images are rebuilt), when the previous frame is more than 250 ms older (a menu, a loading screen, a loaded save), when the camera moved more than 512 units in a frame (a teleport; `Velocity`'s 4096 is for its own purpose) and when the view direction turned more than 60 degrees in a frame (a scripted camera cut). A **weather change needs no reset**: the clip box follows a colour change within a frame or two (the spec's weather check measures it).

**Bloom and the tonemap read the resolved image** (`mTaa->result()`) in place of `mHdrTarget->colorView()`, at start and after every resize. The G-buffer images the AO, SSS and the tonemap's AO term read stay the current frame's.

**Sharing the temporal state (for ambient occlusion accumulation, the next feature).** `Taa::TemporalState` (frame index, the jitter in pixels and NDC of the current frame and the previous one, `historyValid`, and the matrix `toPreviousClip`) is public on `Render::Taa`, and `RenderManager::temporalState()` returns it (empty when `taa` is off). Whatever accumulates over time must jitter and reset with it.

**Velocity is not changed.** The existing velocity pass, with `gbuffer` on and `taa` off, writes what it wrote before. The sky is handled inside the resolve.

**Settings** (`[Rendering]`, read at start): `taa` (bool, false), `taa sharpness` (0 to 1, default 0.25), `taa jitter scale` (0 to 1, default 1), `taa history` (0.5 to 0.98, default 0.9), `taa clipping` (`variance` or `minmax`, default `variance`: toddhow's decision, see below), `taa debug` (`none`, `weight`, `reject`, `velocity`: the resolve writes a diagnostic colour in place of the picture; the history is then not meaningful and the sharpness is ignored). The defaults are first guesses, set from frames in the stage log.

## Constraints

Determinism: the jitter depends on the frame counter only; the resolve has no random numbers. Allocation: images and descriptors at start and on a resize, nothing per frame (the parameter push constants are a value updated in place). Threading: all new CPU code runs on the main thread; the jitter and the push constants are written between frames and read when recording, as `Velocity::setMatrix` does. Off by default and unchanged when off: with `taa = false` no image, pass, descriptor, define, camera, wrapper or timer exists, the view's camera is `mSceneCamera`, bloom and the tonemap read the target's colour, and a frame equals the commit before (frame comparison below). Ownership: `RenderManager` owns the `Render::Taa` by `std::unique_ptr`; it owns the images and the compute node; the jittered projection is owned by the cameras that hold it (`vsg::ref_ptr`, VSG's convention, as all scene-graph objects). Resize: `Taa::setSource` rebuilds the images and descriptors in `syncExtents`; retired objects go to `CompileContext::detach`. Hot paths: no per-frame heap allocation in the added code.

## Interfaces (pure parts, for tests written from this spec alone)

Header `components/render/taamath.hpp`, namespace `Render::Taa`, header-only. Floats unless stated; vectors are `vsg::vec2/vec3` and `vsg::dvec2`, matrices `vsg::dmat4`. Combining two `vsg::vec3` with `*` is deleted by the project's maths rules: the functions below take components.

```cpp
constexpr std::uint32_t jitterPeriod = 16;
constexpr float defaultHistoryWeight = 0.9f;          // the setting's default (Salvi 2016: 10% current)
constexpr float velocityFullPixels = 16.f;            // the pixel speed at which the history weight is lowest
constexpr float movingWeightShare = 0.5f;             // the weight at that speed, as a share of the base weight
constexpr float depthRejectLow = 0.03f;               // log2(1 + d) difference below which the history's depth agrees
constexpr float depthRejectHigh = 0.12f;              // ... and above which it is rejected outright
constexpr float varianceGamma = 1.0f;                 // Salvi 2016: "typically gamma = 1"
constexpr double resetDistance = 512.0;               // game units per frame
constexpr double resetViewCos = 0.5;                  // cos 60 degrees
constexpr double resetGapSeconds = 0.25;

double radicalInverse(std::uint32_t index, std::uint32_t base);          // the van der Corput radical inverse; index 0 gives 0; base 2: 1 -> 0.5, 2 -> 0.25, 3 -> 0.75; base 3: 1 -> 1/3, 2 -> 2/3, 3 -> 1/9
vsg::dvec2 haltonJitter(std::uint32_t frame);                            // i = frame % jitterPeriod + 1; (radicalInverse(i,2) - 0.5, radicalInverse(i,3) - 0.5); each in [-0.5, 0.5); a period repeats exactly
vsg::dvec2 jitterPixels(std::uint32_t frame, float scale);               // haltonJitter(frame) * clamp(scale, 0, 1)
vsg::dvec2 jitterNdc(const vsg::dvec2& pixels, std::uint32_t width, std::uint32_t height);   // (2 x / width, 2 y / height); a zero extent gives (0, 0)
vsg::dmat4 jitterMatrix(const vsg::dvec2& ndc);                          // identity with m(3,0) = ndc.x and m(3,1) = ndc.y ((column, row) as VSG indexes); applied to a clip position (x, y, z, w) it adds (ndc.x * w, ndc.y * w)

bool useTaa(bool settingOn, bool hdrPath, bool gbuffer, std::uint32_t samples);   // true only when all are true and samples == 1
enum class Clipping { Variance, MinMax };   std::optional<Clipping> parseClipping(std::string_view);   // "variance", "minmax" (case sensitive), else nullopt
enum class DebugView { None, Weight, Reject, Velocity };   std::optional<DebugView> parseDebugView(std::string_view);   // "none", "weight", "reject", "velocity"

struct ResetInput { bool havePrevious; bool sizeChanged; double gapSeconds; double cameraDistance; double viewDirectionCos; };
bool historyReset(const ResetInput&);      // true when !havePrevious, sizeChanged, gapSeconds > resetGapSeconds, cameraDistance > resetDistance or viewDirectionCos < resetViewCos

float luma(const vsg::vec3& rgb);          // Rec. 709: 0.2126 r + 0.7152 g + 0.0722 b
vsg::vec3 compress(const vsg::vec3& c);    // c / (1 + luma(c)); components below 0 are first taken as 0
vsg::vec3 expand(const vsg::vec3& c);      // c / (1 - luma(c)), the luma limited to 0.9999 so that the result stays finite; expand(compress(c)) == c for c >= 0
vsg::vec3 rgbToYCoCg(const vsg::vec3& c);  // Y = (r + 2 g + b) / 4, Co = (r - b) / 2, Cg = (-r + 2 g - b) / 4
vsg::vec3 ycocgToRgb(const vsg::vec3& c);  // the inverse

struct Box { vsg::vec3 min; vsg::vec3 max; };
Box varianceBox(const vsg::vec3& mean, const vsg::vec3& meanOfSquares, float gamma);  // mean +- gamma * sqrt(max(meanOfSquares - mean^2, 0)) per component
Box intersect(const Box& a, const Box& b);  // per component max of the mins and min of the maxes; if that is empty (min > max) the component collapses to the midpoint of the two boxes' nearest edges
vsg::vec3 clipToBox(const vsg::vec3& colour, const Box& box, float* clippedShare = nullptr);   // colour if inside; else the point where the segment from the box centre to colour crosses the box; *clippedShare = distance moved / distance from the centre to colour (0 inside), when given

float historyWeightFor(float baseWeight, float speedPixels);   // baseWeight * (1 - (1 - movingWeightShare) * saturate(speedPixels / velocityFullPixels))
float encodeDepth(float linearDepth);                          // linearDepth > 0 ? log2(1 + linearDepth) : -1
float depthReject(float expectedPreviousLinearDepth, float storedEncodedDepth);   // 0 keep ... 1 reject: stored < 0 (sky) with expected > 0 -> 1; else smoothstep(depthRejectLow, depthRejectHigh, |encodeDepth(expected) - stored|)
float finalHistoryWeight(float baseWeight, float speedPixels, float depthRejectAmount, bool historyUsable);   // 0 if !historyUsable; else historyWeightFor(...) * (1 - depthRejectAmount)

vsg::vec3 resolveColour(const vsg::vec3& current, const vsg::vec3& history, float historyWeight);   // expand(lerp(compress(current), compress(history), weight)); weight 0 gives current, 1 history

vsg::vec3 sharpenCross(const vsg::vec3& centre, const vsg::vec3& north, const vsg::vec3& south, const vsg::vec3& east, const vsg::vec3& west, float sharpness);   // in the compressed domain: c + k (c - mean of the four), per component limited to the range of the five, then expanded; sharpness <= 0 returns centre; a flat neighbourhood is unchanged

struct CatmullRom5 { vsg::vec2 offset[5]; float weight[5]; };
CatmullRom5 catmullRom5(const vsg::vec2& fraction);   // fraction in [0,1)^2 = the sample position's fractional texel coordinate; offsets are in texels from the centre of the texel containing the sample, to be read with bilinear filtering; weights sum to 1; fraction (0,0) gives total weight 1 at offset ~(0,0) in the sum of weight*offset

vsg::vec2 previousUv(const vsg::vec2& uv, const vsg::vec2& velocity);   // uv - velocity
vsg::vec3 viewPositionFromDepth(const vsg::vec2& uv, float linearDepth, const vsg::vec2& projectionXY);   // ((2 u - 1) d / px, (2 v - 1) d / py, -d), as velocity.frag
float expectedPreviousDepth(const vsg::vec3& viewPosition, const vsg::mat4& toPreviousClip);   // the w of toPreviousClip * (viewPosition, 1): the distance along the previous camera's view axis
bool skyPreviousUv(const vsg::vec2& uv, const vsg::vec2& projectionXY, const vsg::mat4& toPreviousClip, vsg::vec2& out);   // the previous UV of the direction (x, y, -1, 0) of the pixel through toPreviousClip; false if w <= 0 (behind the previous camera)
```

Header `components/render/taa.hpp` (Vulkan side, tested only by the pure parts above and by what the frames show):

```cpp
class JitterOffset : public vsg::Inherit<vsg::Object, JitterOffset> { public: vsg::dvec2 ndc{ 0.0, 0.0 }; };
class JitteredProjection : public vsg::Inherit<vsg::ProjectionMatrix, JitteredProjection> { public: ref_ptr<ProjectionMatrix> base; ref_ptr<JitterOffset> offset; vsg::dmat4 transform() const override;  /* = jitterMatrix(offset->ndc) * base->transform() */ void changeExtent(...) override {} };
struct TemporalState { std::uint32_t frame; vsg::dvec2 jitterPixels; vsg::dvec2 jitterNdc; vsg::dvec2 previousJitterPixels; bool historyValid; vsg::mat4 toPreviousClip; };
class Taa;   // setSource, node, result, advance(...), state, GPU-timed pass "taa"
```

The shader (`taa.slang`) repeats the constants of the header (`jitterPeriod` is not in it; `depthRejectLow`, `depthRejectHigh`, `movingWeightShare`, `velocityFullPixels`, `varianceGamma`), and its push-constant block layout; a test reads them back from the file (`RenderTaaShader`), the way `testcloudvolumegpu.cpp` does.

## Stages (each commits working, default off unchanged)

- **a. Pure maths and settings** (`taamath.hpp`, the settings, `useTaa`, tests written first and run red).
- **b. Jitter in the camera:** `JitteredProjection`, `mRenderCamera`, the first-person wrapper, the frame counter in `onFrame`, `taa jitter scale`; no resolve yet (`taa` on with jitter and the scene shown raw jitters visibly, which proves the jitter reaches the pixels; with the camera still the velocity debug view stays mid-grey).
- **c. The resolve and the output pass:** `taa.slang`, `Taa`, the command-graph insertion, bloom and tonemap sources, resets, resize, timing.
- **d. Proofs, tuning and cost:** frames and measurements below, the defaults, the documentation.

## Acceptance criteria

- [ ] **Off is unchanged.** With `taa = false` (hdr + gbuffer on, and hdr + gbuffer off) a still scene (Seyda Neen exterior at noon, an interior without flickering lights, rain) equals the commit before within the run-to-run spread of two runs of the old build (`OPENEDEN_FIXED_FRAME_TIME`, `--random-seed`, `diffmap`); no camera, wrapper, image, descriptor or pass exists (log line and a code read).
- [ ] **The jitter sequence is deterministic and correct:** the unit tests of `radicalInverse`, `haltonJitter` (period, range, first values), `jitterNdc`, `jitterMatrix`; a run twice gives the same frame index to jitter mapping in the log.
- [ ] **Resets:** the unit tests of `historyReset`; in game, a `coc` teleport, a resize, a loading screen and a loaded save each give one frame with weight 0 and no smear (frames before and after).
- [ ] **Weather:** a clear to rain change (`ChangeWeather`) shows no ghost of the old colours beyond two frames.
- [ ] **Edge quality against MSAA 4x on a still scene** (`antialiasing = 4`, `taa` off, against `antialiasing = 0`, `taa` on after 20 frames): the same scene crops viewed side by side; the edge profile of a roof line and a tree silhouette is no worse than MSAA's by eye, and the count of stair-step pixels (a measure defined in the stage log) is lower than with neither.
- [ ] **Sub-pixel stability:** with the camera creeping at 0.1 pixel per frame (a Lua script), the frame-to-frame luminance change of a fine detail region (power lines, a fence, a grating) is lower with `taa` than without, and no crawl is seen in the frames.
- [ ] **No ghosting trail:** a scripted capture of an NPC walking in front of a bright sky: the trail behind the actor is measured (pixels whose colour differs from the true background by more than a threshold, in the frames after the actor passed) and shown; the spec records the result honestly. The actor's translation across the ground is not in the velocity buffer (task 0026, stage c), so the claim rests on the neighbourhood clip; if a visible trail remains it is recorded as a known limit with its length in frames.
- [ ] **The sky does not smear while the camera turns** (the resolve's rotation-only reprojection): frames of a slow turn over clouds and stars.
- [ ] **Interplay checked by frames:** with `ao`, `volumetrics`, `volumetric clouds`, `sss`, `wetness`, the LOD cluster dither and fog on, each viewed once with `taa` on (still and moving camera): no new artefact, or the artefact is recorded.
- [ ] **Cost:** `tools/benchmark.sh` `heavy_*` scenes with `gpu timing`, `taa` off against on, the pass `taa` in milliseconds (median and 95th percentile) at 1280 by 720, and the CPU frame time; the 16-entry GPU pass table still fits with every feature on.
- [ ] **MSAA:** with `antialiasing = 4` and `taa = true` the log says TAA is off and the picture is the MSAA one.
- [ ] Unit tests of the pure parts pass in `components-tests`; the shader constants test passes; `eden-studio` builds and starts (the settings defaults file changed); the Linux gate builds with `-Werror` (run once before a push, by the lead); clang-format 14 on the changed lines; a fresh reviewer read each stage.
- [ ] `components/render/CLAUDE.md`, `apps/openmw/mwrender/CLAUDE.md` and the root changelog updated.

Out of scope: an upscaler; TAA for the preview, the local map and the editor; a velocity for objects that move as a whole (a per-object previous matrix); temporal accumulation of ambient occlusion (the next feature, it reads `TemporalState`); a motion-adaptive sharpen; VR; making TAA the default.

## Decisions for toddhow

1. **Patent.** The variance clipping box (Salvi 2016) is the subject of a granted NVIDIA patent (research note 0037, section 9; the claims were not read). The implementation has the plain min/max clip as `taa clipping = minmax`. Which is the default, and does the project ship the variance box at all?
2. **MSAA.** TAA refuses to run with MSAA on (above). Agree?
3. **Defaults** of the sliders, once the frames are in (stage log).

## Stage log

(filled in as the stages land)
