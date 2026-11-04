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

**Settings** (`[Rendering]`, read at start, all off or neutral by default): `ao temporal` (bool, false; needs `ao`, hence `hdr`, `gbuffer`, `pbr`), `ao temporal history` (0.5 to 0.98, default 0.9: the weight the history settles at), `ao temporal clamp` (0 to 4, default 1.0, set from the frames of stage d; 0 = no neighbourhood clamp), `ao temporal debug` (`none`, `raw`, `weight`, `reject`, `static`; the debug values replace the occlusion value in the resolved image and are shown by `gbuffer debug view = ao`). With `ao temporal` on but `ao` effectively off: a warning, nothing changes.

**Why a compute pass replaces the fragment pass.** Moving the noise needs a change in the generating shader. Editing `ao.frag`/`ao.slang` would change SPIR-V that `PostPassesSwitchOff` pins (off must stay byte-identical in its shaders), and a Slang-only fragment stage cannot pair with the GLSL `fullscreen.vert`. So, with `ao temporal` on, a Slang compute pass (`post/aotemporal.slang`) **generates the occlusion itself** (the same GTAO maths, `Render::Ao::gtaoSliceVisibility` and `horizonCosine`, written again in Slang with a time term in the noise) and accumulates it; the fragment pass is not made. With it off, the fragment pass and everything else is exactly as before. The accumulated image is the one the tonemap reads.

**The pass** (`Render::AoTemporalPass`, three dispatches over the half-resolution image in a node placed after the velocity pass and before the TAA/bloom, 8 by 8 threads):

1. `genMain`: for the half-resolution pixel `p`, the full-resolution texel `t = min(2p + 1, size - 1)` (what the fragment pass's nearest sample at the half pixel's centre reads); depth, normal and the GTAO result as `ao.slang` computes them, with `noise.x' = frac(noise.x + offset.x)` and `noise.y' = frac(noise.y + offset.y)` where `offset = Temporal::noiseOffset(frame)` (R2, cycle 64). Writes the raw value to an R32F image. A pixel with no surface is 1.
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
constexpr float defaultClampGamma = 1.0f;            // set by the frames of stage d (the first draft had 1.5)

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

Status at the end of stages b and c (2026-11-04). "Proof" names the frames or tests; folders are under `I:\Projects\OpenEden-work\aotemporal\` (`runs/<label>/frames`, `cmp/` for the pictures, `tools/aoimg.ps1` for the measures, `run.sh` and `scene/` for the scripted scenes).

- [x] **Off is unchanged.** `ao temporal = false` with `ao` on: the exterior still scene (Seyda Neen, normal picture) from the build of task 0075's end (`exe-E`) against this build: **0.000%** of the pixels differ by more than 8; the interior (Arrille's Tradehouse) with `gbuffer debug view = ao`, which has no lights to flicker: 0.004% (the NPC's idle animation); the interior's normal picture differs by 25.6% between the two builds and by 27.1% between two runs of one build (flickering torches), so it says nothing. `PostPassesSwitchOff` and `SlangPermutations` pass unchanged (`ao.frag` and `ao.slang` are not touched). `ao temporal = true` without `ao`: a warning in the log and the same renderer (`w-noao`); an unknown debug value: a warning and `none` (`w-msaa`). Nothing exists when off: `mAoTemporal` is null (code read, `rendermanager.cpp`).
- [x] **The pure parts are tested** from the interface section, written by a separate agent that saw only the interface and a stub, run red first (62 of 82 failed against the stub, `red.log`; 20 passed by luck), all 82 pass with the maths in. 10 more tests (stage b): the pass's frame state with and without TAA, the `static` view, and the shader twin (constants, the 24-bit pack, the filter weights, the settings constants' ids, the push block of six vectors, the bindings, the entry points).
- [~] **`static` is not the old picture, by design** (see the stage log, "The centre texel rule"): `ao temporal debug = static` against `ao temporal = false`: 20.6% of the pixels differ by more than 8 grey levels (`d7.png`): the fragment pass's fixed dot pattern is gone, which is the point; the acceptance sentence of the first draft ("equal within rounding") cannot hold because the old picture depends on the rounding of a nearest-sample tie that a compute pass cannot repeat (found by frames: `v1` to `v7`).
- [x] **Noise falls.** Exterior still scene, 24 pictures 45 frames apart, `gbuffer debug view = ao`, the tonemap's filter included, 1280 x 720, default settings (history 0.9, clamp 1.0): mean per-pixel temporal standard deviation (a) shipped pass 0.0000, (b) `raw` (noise moved, no history) 0.0088 on the ground, 0.0049 on a wall, 0.0076 over the picture, **(c) accumulation 0.0028, 0.0020, 0.0026: 3.1 to 2.4 times lower than (b)**. Error (rms, 0 to 1) against a converged reference (the same pass at 8 slices, history 0.98, no clamp, averaged over 24 pictures: `cmp/ref-new.png`): (a) 0.0375 ground, 0.0238 wall, 0.0617 whole; (b) 0.0194, 0.0091, 0.0177; **(c) 0.0153, 0.0069, 0.0137: 2.5 to 4.5 times lower than (a)** and 1.3 times lower than (b). Two honest limits: the reference is of the new pass, so (a)'s error contains the fixed pattern the new pass does not have (against its own 8-slice reference, (a) has 0.0139, 0.0066, 0.0175: that is variance alone, and (c) is not better than that on the ground); and the history 0.95 has lower temporal std (0.0021) but the same error. Table of settings in the stage log.
- [x] **No ghost behind a moving actor or a door.** The skinned NPC of the scenes stands or idles, so the moving rigid test is a barrel that a global script slides across the floor of Arrille's Tradehouse (no velocity in the motion image: the worst case for this pass; a swing door was not exercised: the doors of the two interiors tried are out of the camera's reach, see the stage log) over 100 frames (60 fps limit, `ao strength = 4` so that the occlusion shows, pictures 10 frames apart): the picture of the accumulated pass against the `static` one (box blur radius 6, rms over the 200 x 130 pixels round the barrel): still 0.0155 to 0.0176 before it appears; while it moves 0.0445 (clamp 1.0) against 0.0713 (no clamp), 0.0684 (clamp 3), 0.0548 (clamp 1.5), 0.0362 (clamp 0.7), 0.0356 (history 0.7); when it stops 0.019. A dark blob and a light crescent follow it (`bds.png`): lag, not a trail that stays. The neighbourhood clamp is what limits it (without it the blob is twice as strong) and the depth and normal tests keep it from spreading.
- [x] **Resets.** A camera cut of 1500 units at frame 120 (`ao temporal debug = weight`): the blend over the picture is 0.014 on the cut frame and then 0.50, 0.67, 0.80 on frames 121, 122 and 124 (the cumulative average: count / (count + 1)) and 0.899 at 130, as the formula says. A resize of the window (1280 x 720 to 968 x 1421 client, `resize.ps1`): the pass is rebuilt, no error line in the log, the blend is back to 0.90 within 6 frames. Not run: a loaded save and a loading screen (the same camera-gap rule as TAA, a unit test of `historyReset` only).
- [x] **With TAA on and off.** Both work. Camera turning at 2 pixels a frame over the exterior (`cm`): the accumulated picture is smooth and keeps its edges with both (viewed, `cm.png`; slightly softer tree edges than the raw sample, from the bilinear history lookup), noise at the same level with TAA on and off in the still scene. With TAA on the history uses the state's frame and jitter delta.
- [~] **Interplay by frames:** `sss`, `volumetrics`, `atmosphere`, `wetness`, `pbr`, shadows (object, actor) and TAA on with `ao temporal` on against off in the interior, and `clouds`, `cover`, `volumetric clouds`, terrain and player shadows added in the exterior: no new artefact (`alli.png`, `all.png`); the exterior's occlusion is faint, so the pictures differ by 0.000% over 8 there. Not run: `gi` (needs baked probes, interiors only; the occlusion is separate from it), MSAA with the pass on (started without an error, `w-msaa`, no frame viewed).
- [x] **Cost** (RTX 4070, `gpu timing`, medians of the 500 rendered frames, `gpu_main_scene` above 0.1 ms): 1280 x 720 exterior `ao` 0.150 ms, `ao temporal` 0.174, interior 0.167 and 0.198; 1920 x 1080 exterior 0.361 and 0.418, interior 0.407 and 0.474; with TAA on 0.174 / 0.199 and the TAA pass 0.097 at 720p. CPU frame time within the spread (2.48, 2.52 ms exterior, 1.33, 1.26 interior). Memory: R32F raw + two RGBA32F at half resolution (18.7 MB at 1080p).
- [~] Documentation and tests: `components/render/CLAUDE.md`, `apps/openmw/mwrender/CLAUDE.md`, `files/shaders/CLAUDE.md` and the root changelog updated; `eden-studio` rebuilt (a stale exe would fail with "Default setting ... is not initialized"). The fresh review is in the stage log.

Out of scope: contact shadows (decision below); narrowing the tonemap's 5x5 filter; accumulating the ambient image or anything but the occlusion; a velocity for rigid moving objects; a spatially wider or learned denoiser; making the setting the default.

## Decisions for toddhow

1. **Contact shadows** are not built (research note 0038 section 6). Do you want a sun-diffuse G-buffer output and a contact-shadow task?
2. **The variance clamp.** Salvi's variance box is the subject of a patent noted in task 0075 (the claims were not read). `ao temporal clamp` defaults to 1.0 (Salvi's gamma) because the frames show it halves the trail behind a rigid mover against no clamp (0.045 against 0.071 rms) at no cost in noise; 0 switches it off. Keep the default, or ship with 0 until the patent is read?
3. **The constant `defaultClampGamma`** was 1.5 in the first draft and is pinned by one test line (the constants test of `testaotemporalmath.cpp`), which I changed to 1.0 with the setting: your approval of that one edit of the independent writer's test.
4. **The tonemap's 5x5 filter** stays as the last step. With the noise gone it could be narrower (sharper occlusion edges); a separate change.

## Stage log

### 2026-11-04, stage a: the research note, this spec, the pure maths and 82 tests

`docs/research/0038-temporal-ambient-occlusion.md` (Jimenez et al. 2016 could not be fetched: three attempts failed, so its numbers (a 4x4 pattern, 6 temporal rotations) are from a search snippet and marked recalled; XeGTAO's README was read, no code opened). Contact shadows are left out (section 6 of the note: no sun-diffuse term exists). The tests were written by a separate Sonnet agent from the interface section and a stub header alone: 62 of 82 failed against the stub (`red.log`), 20 passed by luck; with the maths in all 82 pass at the first run.

### 2026-11-04, stages b and c: the pass, the frames

**What was built.** `Render::AoTemporalPass` (three dispatches, `post/aotemporal.slang`), the settings, the node after the velocity graph, the tonemap's source, resize and timing. A compute pass replaces the fragment pass because `ao.frag` and `ao.slang` cannot change (`PostPassesSwitchOff`) and a Slang fragment stage cannot pair with the GLSL vertex stage.

**Found by the frames and fixed.**
- *The centre texel rule.* The first compute version sampled the depth with a nearest sampler as `ao.slang` does and matched the fragment pass within 1.1% of the pixels (over 8 grey levels), but a Load by `floor(uv * size)` made 46% of the picture differ and almost white. A half-resolution pixel's centre is a corner of four full-resolution texels; the fragment pass lets the hardware's tie decide and measures its samples from the corner, so the old picture has a fixed dot pattern that no accumulation can remove. A bias of -0.001 texel (the lower texel) got to 9.6%; the final rule takes the lower right texel (+0.001 bias, deterministic), its centre as the position and the samples from there. The picture is then a cleaner one and `static` differs from the old pass in 20.6% of the pixels (`v1` to `v7` in `cmp/`). The legacy picture's dots are therefore a bias, not noise: the error against the new pass's converged reference counts them.
- *The clamp.* Salvi's variance clamp was added because the spec asked for it; frames show that gamma 1.5 left a trail stronger than 1.0 or 0.7 behind a rigid mover and that 0 was worst, with no cost in noise: the default is 1.0 (ghost rms 0.0445 against 0.0713 without, 0.0548 at 1.5, 0.0362 at 0.7; the mean level of the ground rises 0.003 at 1.0 and 0.005 at 0.7, a small loss of contrast). `defaultClampGamma` 1.5 to 1.0 changed one line of the independent writer's test (awaiting your approval).

**Measurements** (in `I:\Projects\OpenEden-work\aotemporal\runs` and `cmp`, tools `tools/aoimg.ps1`, `stats2.py`).

| Exterior still scene, 24 pictures 45 frames apart | temporal std ground / wall / whole | rms error vs converged reference ground / wall / whole |
| --- | --- | --- |
| (a) shipped pass (`ao temporal = false`) | 0 / 0 / 0.0004 | 0.0375 / 0.0238 / 0.0617 |
| (b) `raw` (noise moved, no history) | 0.0088 / 0.0049 / 0.0076 | 0.0194 / 0.0091 / 0.0177 |
| (c) accumulation, clamp 1.5 | 0.0030 / 0.0020 / 0.0024 | 0.0167 / 0.0069 / 0.0139 |
| (c) clamp 1.0 (the default) | 0.0028 / 0.0020 / 0.0026 | 0.0153 / 0.0069 / 0.0137 |
| (c) clamp 0.7 | 0.0027 / 0.0021 / 0.0027 | 0.0137 / 0.0069 / 0.0131 |
| (c) clamp 0 | 0.0031 / 0.0018 / 0.0023 | 0.0175 / 0.0071 / 0.0137 |
| (c) history 0.95 (clamp 1.5) | 0.0021 / 0.0014 / 0.0018 | 0.0170 / 0.0069 / 0.0140 |

The reference is the pass's own at 8 slices and history 0.98, averaged over 24 pictures (`ref-new.png`); against the shipped pass's own 8-slice picture (`ref-old.png`) the shipped pass has 0.0139 / 0.0066 / 0.0175 (variance alone: its dots are shared with its reference), and the two references differ by 0.0263 / 0.0226 / 0.0498. A lower history gives a smaller trail (0.0356 at 0.7) and more noise; 0.9 is Salvi's TAA default and the cumulative start makes the first frames converge fast.

**Rigid mover** (a barrel slid by a global script, Arrille's Tradehouse, `ao strength = 4`, 60 fps limit, blur radius 6, rms of accumulated minus `static` over 200 x 130 pixels round the barrel; pictures 10 frames apart, frames 150 to 190 while it moves, 200 on when it is still): before it appears 0.0155 to 0.0176, while it moves 0.0445 to 0.0548 (clamp 1.0 and 1.5) and 0.0713 without a clamp, at rest 0.019. The NPC (idle) shows as a blob in the same pictures: its animation, not a trail. A swing door was not exercised: in the two interiors tried the doors are upstairs or out of the start position's view (`AODOOR` lines in `runs/probe`) and the camera placements found no good view; the barrel is the same case for the history (a rigid body with no velocity).

**Camera cut, resize, TAA, cost, interplay:** see the acceptance list. The cut frame's blend is 0.014 and then 0.50, 0.67, 0.80 (frames 121, 122, 124), 0.899 at 130; the resize rebuilds the pass with no error. Cost: +0.02 to +0.03 ms at 720p, +0.06 ms at 1080p against the `ao` pass.

**Not done or not proven.** The Linux gate (GCC `-Werror`; whole-file clang-format 14 was run in the `openeden-cf14` image on every C++ file added or changed) and clang-tidy; the Lua tests; a synchronisation validation layer (none installed: the barriers follow `TaaPass`); MSAA with the pass on (starts, no frame viewed); `gi` on; a loaded save and a loading screen; a swing door; a device other than this NVIDIA one; the grazing-angle limit (below). Contact shadows are not built.

### 2026-11-04, review of stages b and c (a fresh Sonnet reviewer, read-only)

Verdict: no blocker (no synchronisation, layout, index-order or sign error found; checked by reading: the matrix and rotation indexing, the jitter's sign, the octahedral pack against the C++ twin, the barriers, the integration order, off means unchanged). Findings and what was done:
1. A `R16F` storage image is not a guaranteed storage format (it needs `shaderStorageImageExtendedFormats`): the raw image is R32F now (`format("r32f")`).
2. The history could be read uninitialised if `beginFrame` runs three times with no frame recorded (the counter counts calls, as `TaaPass` does, noted there in the review of stage c): a tap holding NaN or infinity is skipped and a tap of weight 0 adds nothing, so `0 * NaN` cannot enter the sums. The counting itself is not changed.
3. The push packing was untested: a test with a matrix of distinct entries pins `depthRow` and the rows of the rotation with the projection in `w`; the shader's unused `noiseAlpha` constants were removed (the offset comes from the CPU).
4. The root changelog row and this stage log (done); the interface section's `defaultClampGamma` and the path in the acceptance header were wrong (fixed); the test-line edit is disclosed in the decisions.
5. A pipeline that fails to build at start throws, as TAA's does (the fragment pass is not made as a fallback); a failure after a resize logs and leaves the occlusion off. Left as it is, consistent with TAA.
6. TAA's `state().frame` is the jitter index plus one (the counter is advanced after use): harmless (period 64). Left.
7. **Known limit:** at a grazing angle (distant ground) one half-resolution pixel spans about 5% of the depth, more than the 2% to 7% window of `depthMismatch`, so taps of the history are partly rejected there and the accumulation gain is smaller; the occlusion is near 1 at that distance. Not measured.
8. The tonemap's `aoFiltered` reads depth and normal at the half-resolution texel's corner (a hardware tie), while the pass computed the value for the lower right texel: its weights may compare another texel than the value belongs to. It predates this task; storing the pass's depth and normal and reading them in the tonemap would fix it (the tonemap's twins are pinned; not done).
