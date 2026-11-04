# Task: Temporal accumulation of ambient occlusion
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, R2 (ambient occlusion: "temporal accumulation and contact shadows" noted as not built); the technique is surveyed in `docs/research/0038-temporal-ambient-occlusion.md` (read it first; every source is named there, and what could not be read is marked). Builds on `docs/tasks/0029-rendering-ao.md` (the pass) and `docs/tasks/0075-temporal-anti-aliasing.md` (the hand-off `RenderManager::temporalState()`).
Goal: let the ambient occlusion converge over frames instead of being a fixed noise pattern the tonemap has to blur: move the slice directions and step offsets every frame, accumulate the results with a history reprojected by the velocity image and rejected by stored depth and normal, behind settings that are off by default, with or without TAA.
Inherited code touched, at commit `4d2b636e20`: `apps/openmw/mwrender/rendermanager.cpp` and `.hpp` (`RenderManager::RenderManager`, the command graph's `afterScene` count, `setStore`/start-up `setSource` calls, `syncExtents`, `updateVelocity`, `aoSources`), `components/settings/categories/rendering.hpp`, `files/settings-default.cfg`, `files/shaders/CMakeLists.txt`, `apps/components_tests/CMakeLists.txt`, `components/CMakeLists.txt`. Read, not changed: `components/render/ambientocclusion.*`, `aomath.hpp`, `files/shaders/post/ao.frag`, `ao.slang` (see "Why a compute pass replaces the fragment pass"), `tonemap.slang` (`aoFiltered`), `components/render/taa.*`, `taamath.hpp`, `velocity.*`.
Inputs / outputs: new `[Rendering]` settings `ao temporal`, `ao temporal history`, `ao temporal clamp`, `ao temporal debug`; new `components/render/aotemporalmath.hpp` (pure), `aotemporal.hpp`/`aotemporal.cpp` (the pass), `files/shaders/post/aotemporal.slang` (Slang, decision 0009), tests in `apps/components_tests/render/`. The AO image the tonemap reads becomes the accumulated one. No change to any file format, save, Lua API or extension contract.

Status: started 2026-11-04 on toddhow's order of five rendering features (TAA done, this is the second). Full loop (Track V renderer): this spec, tests from this spec first (a separate test writer, from the interface section only), implementation, a fresh reviewer, a frame viewed before every render commit.

## Facts this is built on (checked at `4d2b636e20`)

- **The pass today** (task 0029): `Render::AmbientOcclusion` draws a fullscreen triangle at half resolution (`Ao::halfExtent`) into an R8 image with `post/ao.frag` (GLSL) or its twin `ao.slang`: GTAO, `ao slices` (2) slice directions, `ao steps` (6) steps on each side, 24 depth reads, from the stored linear depth and view-space normal. The slice angle and the step offset come from interleaved gradient noise of the pixel position, `gradientNoise(fragCoord.xy)` and the same at `+ (5.588238, 5.588238)`: **no time term** (`ao.slang`, `fragMain`). `ao radius`, `slices` and `steps` are specialization constants 1100 to 1102 (`aosettings.slang`, `Resource::settingsConstantTable`).
- **The tonemap's AO variant** (`tonemap.slang`/`tonemap.frag`, `aoFiltered`): a 5x5 filter over the half-resolution image at the 25 taps' texel centres, each weighted by `Render::Ao::bilateralWeight` (depth within 2 to 5 percent, normal dot 0.707 to 0.9) from the full-resolution depth and normal, then `color - ambient * (1 - pow(ao, strength))`. It samples `gAo` with `.r` at exact texel centres, so any image whose first channel is the occlusion and whose size is `aoExtent` works (`Tonemap::AoSources`, `aoSources()`).
- **`PostPassesSwitchOff`** (`apps/components_tests/vsg/testpostpasses.cpp`) compares the SPIR-V of the files in `post/` that the game loads (`ao.frag`, `tonemap.frag`, ...) with the strings they were taken from, and `SlangPermutations` checks every Slang twin of them against its GLSL. A new Slang-only fragment stage cannot sit beside the GLSL vertex stage (`files/shaders/CLAUDE.md`, "A mixed pipeline draws nothing").
- **The depth buffer is reversed; the linear depth image has no convention** (0 where nothing opaque was drawn). **Velocity** (`Render::Velocity`, RG16F, `currentUV - previousUV`, 0 where there is no depth) exists whenever the G-buffer does, with or without TAA; it holds camera motion and skinned actors' bone motion, not the motion of rigid objects as a whole (task 0026, stage c).
- **TAA's hand-off** (task 0075): `RenderManager::temporalState()` returns a `Render::Taa::TemporalState` (frame index, jitter in pixels and NDC, previous jitter, `historyValid`, `toPreviousClip`), null without TAA. `updateVelocity` computes `Taa::historyReset(...)` from the camera on every frame (first frame, a move over 512 units, a turn over 60 degrees, a gap over 250 ms) whether TAA is on or not.
- **GPU timing:** the pass table holds 16 entries (`components/render/CLAUDE.md`); TAA made it 13 with everything on; this task replaces `ao` by `ao temporal` (never both).

## Design

**Settings** (`[Rendering]`, read at start, all off or neutral by default): `ao temporal` (bool, false; needs `ao`, hence `hdr`, `gbuffer`, `pbr`), `ao temporal history` (0.5 to 0.98, default 0.9: the weight the history settles at), `ao temporal clamp` (0 to 4, default **set from frames, stage d**; 0 = no neighbourhood clamp), `ao temporal debug` (`none`, `raw`, `weight`, `reject`, `static`; the debug values replace the occlusion value in the resolved image and are shown by `gbuffer debug view = ao`). With `ao temporal` on but `ao` effectively off: a warning, nothing changes.

**Why a compute pass replaces the fragment pass.** Moving the noise needs a change in the generating shader. Editing `ao.frag`/`ao.slang` would change SPIR-V that `PostPassesSwitchOff` pins (off must stay byte-identical in its shaders), and a Slang-only fragment stage cannot pair with the GLSL `fullscreen.vert`. So, with `ao temporal` on, a Slang compute pass (`post/aotemporal.slang`) **generates the occlusion itself** (the same GTAO maths, `Render::Ao::gtaoSliceVisibility` and `horizonCosine`, written again in Slang with a time term in the noise) and accumulates it; the fragment pass is not made. With it off, the fragment pass and everything else is exactly as before. The accumulated image is the one the tonemap reads.

**The pass** (`Render::AoTemporalPass`, three dispatches over the half-resolution image in a node placed after the velocity pass and before the TAA/bloom, 8 by 8 threads):

1. `genMain`: for the half-resolution pixel `p`, the full-resolution texel `t = min(2p + 1, size - 1)` (what the fragment pass's nearest sample at the half pixel's centre reads); depth, normal and the GTAO result as `ao.slang` computes them, with `noise.x' = frac(noise.x + offset.x)` and `noise.y' = frac(noise.y + offset.y)` where `offset = Temporal::noiseOffset(frame)` (R2, cycle 64). Writes the raw value to an R16F image. A pixel with no surface is 1.
2. `resolveMain`: reads the raw value and its 3x3 neighbourhood, the pixel's depth and normal, and the velocity at `t`; the history position `Temporal::historyUv(uv, velocity, jitterDelta)` (jitter delta zero without TAA); the four history texels around it, each with its stored depth and normal: weight `bilinear * (1 - depthMismatch) * normalMatch`, depth compared as the difference of `log2(1 + d)` between the surface's depth expressed in the previous camera (`w` of `toPreviousClip * (viewPosition, 1)`) and the stored one, normal as the dot of the current normal rotated into the previous view (`relativeRotation`) with the stored one; the history value and count are the weight-normalised sums; the history value is limited to the variance range of the same-surface neighbourhood (`ao temporal clamp`, 0 = off); `blend = historyBlend(base, count, confidence)` and the result `lerp(raw, history, blend)`, the new count `nextCount(count, confidence)`. Writes the result image (RGBA32F: value, encoded depth, count, packed normal). `base` is 0 on a reset, in the first `warmupFrames` frames after a build, and when `ao temporal debug = static`.
3. `copyMain`: the history image takes the result image (a separate image, as TAA, so that neighbouring pixels never read what another thread of the same dispatch is writing).

The result image is `GENERAL` while written and `SHADER_READ_ONLY_OPTIMAL` after (barrier closing the node), where the tonemap samples its first channel. The history image stays `GENERAL`.

**Debug values.** `raw`: the unaccumulated value of this frame (the rotation on, no history) - configuration (b) of the measurement. `weight`: the history blend in the first channel (white 1). `reject`: `1 - confidence`. `static`: the noise offset is 0 and the history is not used, so the value equals the fragment pass's (compared in a frame, stage d).

**Frame index, jitter, resets.** With TAA on, `frame` is `temporalState()->frame` and the jitter delta is `jitterDeltaUv(...)` from the state's jitters; with TAA off a counter advanced by `AoTemporalPass::beginFrame` is used and the delta is zero. The reset is `Taa::historyReset` (same camera data in `updateVelocity`), plus the pass's own three warm-up frames after a build (a resize rebuilds it through `setSource`; the images are discarded on those frames, the model of `TaaPass`).

**Contact shadows: not built** (research note 0038 section 6): the renderer has no separate sun-diffuse term to darken, so any application darkens specular, emissive, point lights and fog too; a correct one needs a new G-buffer output and changes to two large shaders and to the tonemap's twins. Recorded as a decision for toddhow.

## Constraints

Determinism: the noise offset depends on the frame counter only (never a clock); the resolve has no random numbers. Allocation: images and descriptors at start and on a resize, nothing per frame (push constants are values updated in place). Threading: all new CPU code runs on the main thread, written between frames and read when recording, as `Velocity::setMatrix` and `TaaPass::beginFrame`. Off by default and unchanged when off: with `ao temporal = false` no image, pass, descriptor, node or timer entry exists, `mAmbientOcclusion` is made as before and a frame equals the commit before (frame comparison below). Ownership: `RenderManager` owns the `Render::AoTemporalPass` by `std::unique_ptr`; it owns the images and the compute node. Resize: `setSource` rebuilds the images and descriptors in `syncExtents`; retired objects go to `CompileContext::detach`. Hot paths: no per-frame heap allocation in the added code. Units: the occlusion radius is in game units, as before.

## Interfaces (pure parts, for tests written from this spec alone)

Header `components/render/aotemporalmath.hpp`, namespace `Render::Ao::Temporal`, header-only. Floats unless stated. Combining two `vsg::vec3` with `*` is deleted by the project's maths rules: the functions below take components.

```cpp
constexpr std::uint32_t noiseCycle = 64;
constexpr double noiseAlpha1 = 0.7548776662466927;   // Roberts' R2: 1 / p, p the plastic constant (p^3 = p + 1)
constexpr double noiseAlpha2 = 0.5698402909980532;   // 1 / p^2
constexpr float defaultHistoryWeight = 0.9f;         // the setting's default
constexpr float maxHistoryWeight = 0.98f;            // the setting's upper limit
constexpr float maxCount = 255.f;                    // the count stops growing here
constexpr float depthRejectLow = 0.02f;              // |log2(1 + expected) - stored| below which depth agrees
constexpr float depthRejectHigh = 0.1f;              // ... and above which it is another surface
constexpr float normalRejectLow = 0.8f;              // dot of the unit normals above which ... (see normalMatch)
constexpr float normalRejectHigh = 0.95f;
constexpr std::uint32_t warmupFrames = 3;
constexpr float defaultClampGamma = 1.5f;            // provisional; stage d decides

vsg::vec2 noiseOffset(std::uint32_t frame);     // n = frame % noiseCycle; (frac(0.5 + n * noiseAlpha1), frac(0.5 + n * noiseAlpha2)), computed in double; both in [0, 1); frame and frame + noiseCycle give the same answer; consecutive frames differ
float rotateNoise(float noise, float offset);   // frac(noise + offset); for noise in [0, 1) and offset in [0, 1) the result is in [0, 1); offset 0 returns noise unchanged

float encodeDepth(float linearDepth);           // linearDepth > 0 ? log2(1 + linearDepth) : -1
float depthMismatch(float expectedPreviousLinearDepth, float storedEncodedDepth);   // 0 same surface .. 1 another one. stored < 0 means nothing was drawn: with expected > 0 -> 1; with expected <= 0 -> 0. expected <= 0 with stored >= 0 -> 1. Else smoothstep(depthRejectLow, depthRejectHigh, |encodeDepth(expected) - stored|)
float normalMatch(float dot);                   // smoothstep(normalRejectLow, normalRejectHigh, dot): 0 at or below the low, 1 at or above the high

std::uint32_t packNormal(const vsg::vec3& unitNormal);   // octahedral, 12 bits per axis: x * 4096 + y, always < 2^24 so a float holds it exactly; the input is any non-zero vector (it is normalised first); the zero vector packs as (0, 0, 1)
vsg::vec3 unpackNormal(std::uint32_t packed);            // a unit vector within 0.002 radians of the packed one

struct Rotation3 { float m[3][3]; };            // row major: result[i] = sum_j m[i][j] * v[j]
Rotation3 relativeRotation(const vsg::dmat4& currentView, const vsg::dmat4& previousView);   // maps a direction in the current view space to the previous view space: R_previous * transpose(R_current), R the 3x3 rotation parts (vsg's (column, row) indexing). Equal views give the identity
vsg::vec3 apply(const Rotation3& rotation, const vsg::vec3& v);

vsg::vec2 jitterDeltaUv(const vsg::dvec2& jitterPixels, const vsg::dvec2& previousJitterPixels, std::uint32_t width, std::uint32_t height);   // ((previous.x - current.x) / width, (previous.y - current.y) / height); a zero extent gives (0, 0)
vsg::vec2 historyUv(const vsg::vec2& uv, const vsg::vec2& velocity, const vsg::vec2& jitterDelta);   // uv - velocity + jitterDelta

struct Tap { int x; int y; float weight; };
std::array<Tap, 4> bilinearTaps(const vsg::vec2& uv, std::uint32_t width, std::uint32_t height);   // p = uv * size - 0.5 per axis; x0 = floor(p.x), y0 = floor(p.y), f = p - (x0, y0); order (x0,y0) (x0+1,y0) (x0,y0+1) (x0+1,y0+1); weights (1-fx)(1-fy), fx(1-fy), (1-fx)fy, fx fy; a tap outside [0, width) x [0, height) has weight 0 and x, y clamped into range; so an interior position's weights sum to 1

float tapWeight(float bilinear, float depthMismatchAmount, float normalMatchAmount);   // bilinear * (1 - depthMismatchAmount) * normalMatchAmount
float historyBlend(float baseWeight, float count, float confidence);   // confidence * min(clamp(baseWeight, 0, maxHistoryWeight), count / (count + 1)); count <= 0 or confidence <= 0 give 0
float nextCount(float count, float confidence);    // min(1 + max(count, 0) * clamp(confidence, 0, 1), maxCount)
float accumulate(float current, float history, float blend);   // current + (history - current) * clamp(blend, 0, 1)

struct Range { float lo; float hi; };
Range varianceRange(float mean, float meanOfSquares, float gamma);   // mean -/+ gamma * sqrt(max(meanOfSquares - mean^2, 0)); gamma <= 0 gives {0, 1} (no clamp)
float clampToRange(float value, const Range& range);

bool useAoTemporal(bool settingOn, bool aoPass, bool gbuffer);   // true only when all three are true
enum class DebugView { None, Raw, Weight, Reject, Static };
std::optional<DebugView> parseDebugView(std::string_view);       // "none", "raw", "weight", "reject", "static" (case sensitive), else nullopt
bool historyUsable(bool reset, std::uint32_t framesSinceBuild);   // !reset && framesSinceBuild >= warmupFrames
```

Header `components/render/aotemporal.hpp` (Vulkan side, tested by the pure parts and by what the frames show, plus pass-state tests): `Render::AoTemporalPass` with `Settings`, `FrameInput` and `setSource`, `node`, `result`, `resultExtent`, `beginFrame`, `frame`. The shader (`aotemporal.slang`) repeats the constants of the header and the push-constant block layout; a test reads them back from the file (`RenderAoTemporalShader`), the way `RenderTaaShader` does.

## Stages (each commits working, default off unchanged)

- **a. Research note, this spec, the pure maths and settings**, tests written first from the interface section and run red against a stub.
- **b. The pass:** `aotemporal.slang`, `AoTemporalPass`, the command-graph node, the tonemap's source, resize, timing; debug views; `static` equals the fragment pass.
- **c. Proofs:** frames and measurements below.
- **d. Tuning, cost, interplay, documentation, the fresh review.**

## Acceptance criteria

- [ ] **Off is unchanged.** `ao temporal = false` with `ao` on: a frame from the commit before this task against this build, in a still interior and a still exterior (`OPENEDEN_FIXED_FRAME_TIME`, `--random-seed`), within the spread of two runs of one build; no `AoTemporalPass`, image, node or timer exists (code read); `PostPassesSwitchOff` and `SlangPermutations` pass unchanged. `ao temporal = true` without `ao`: a warning and the same renderer.
- [ ] **The pure parts are tested** (from this section, red first), and the shader's constants and push block are read back.
- [ ] **`static` equals the old pass.** `ao temporal debug = static` against `ao temporal = false` in a still scene, shown by `gbuffer debug view = ao`: the same picture within rounding (reported as the share of pixels that differ by more than 1/255 and the maximum).
- [ ] **Noise falls.** In a still scene, three configurations of the same noise pattern and sample count (configurations (a) the shipped pass, (b) `raw`, (c) accumulation, the view `gbuffer debug view = ao`): (c)'s per-pixel temporal standard deviation over N frames is lower than (b)'s by a factor stated in the stage log; (c)'s error against a reference (the shipped pass with `ao slices 8`, `ao steps 12`) is lower than (a)'s. The estimator (code in the stage log's folder) and the numbers are recorded for an interior and an exterior.
- [ ] **No ghost behind a moving actor or on a door.** A scripted scene with an NPC walking across a wall and a swing door opened by a script with the camera still: the occlusion value in the region the mover left, a few frames after, equals the still scene's within noise, with and without TAA; the mover's own pixels (skinned: velocity) and a rigid door (no velocity: rejected by depth and normal) are shown in frames.
- [ ] **Resets.** A camera cut of 1500 units and a resize: the history blend (`weight` view) is black for the frames of the reset and the next frames are clean; no error line in the log.
- [ ] **With TAA on and off.** Both work; with TAA on the history uses the state's frame and jitter. Frames and the noise measure for both.
- [ ] **Interplay by frames:** all of `sss`, `gi`, `volumetrics`, `pbr`, shadows and atmosphere on together with `ao temporal` on: no new artefact against `ao temporal` off.
- [ ] **Cost** in ms (`gpu timing`, the `ao temporal` pass against the `ao` pass) at 720p and 1080p on the benchmark scenes; CPU frame time.
- [ ] `components/render/CLAUDE.md`, `apps/openmw/mwrender/CLAUDE.md`, `files/shaders/CLAUDE.md` and the root changelog updated; `eden-studio` rebuilt and starts.

Out of scope: contact shadows (decision below); narrowing the tonemap's 5x5 filter; accumulating the ambient image or anything but the occlusion; a velocity for rigid moving objects; a spatially wider or learned denoiser; making the setting the default.

## Decisions for toddhow

1. **Contact shadows** are not built (research note 0038 section 6). Do you want a sun-diffuse G-buffer output and a contact-shadow task?
2. **The variance clamp.** Salvi's variance box is the subject of a patent noted in task 0075; `ao temporal clamp` defaults to a value decided by frames and 0 switches it off. Which default?
3. **The tonemap's 5x5 filter** stays as the last step. With the noise gone it could be narrower (sharper occlusion edges); a separate change.

## Stage log

(empty)
