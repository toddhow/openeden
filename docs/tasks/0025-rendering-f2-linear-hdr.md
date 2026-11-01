# Task: Rendering foundation F2, the linear HDR path
Design doc section: `docs/research/0008-modern-rendering-proposal.md`, sections 3 and 4 (F2); `docs/decisions/0009-slang-for-new-shaders.md` (draft; the new shaders of this task are GLSL, see Constraints).
Goal: render the scene into a 16-bit float target in linear light, then apply exposure, a tonemap and optional bloom on the way to the swapchain, all behind one setting that is off by default, so that later features (F3 stored depth and normals, F4 the material model, then ambient occlusion, atmosphere and the rest) are built on a float, linear scene.
Inherited code touched, at commit `823ac88856`: `apps/openmw/mwrender/rendermanager.cpp` (`RenderManager::RenderManager`, `updateSceneChildren`, `updateSceneData`, `syncExtents`, `onFrame`, `updatePostProcess`), `apps/openmw/mwrender/renderpass.hpp`, `apps/openmw/mwrender/reflect.hpp` (`Reflect`), `apps/openmw/mwrender/postprocess.cpp` (`createCopies`, `updateImages`), `components/render/renderpass.hpp`, `components/render/engine.*`, `components/view/collectlights.cpp` (`CollectLights::pack`), the shaders `files/shaders/default/main.frag`, `default/main.vert`, `terrain/main.frag`, `water/main.frag`, `sky/*/main.frag`, `lib/material/terms.glsl`, `lib/view/fog.glsl`, `components/settings/categories/rendering.hpp`, `files/settings-default.cfg`.
Inputs / outputs: new `[Rendering]` settings (`hdr`, `hdr tonemap`, `hdr exposure`, `hdr bloom`, `hdr bloom strength`); new `components/render/hdrtarget.*` (the float target and its render pass), `components/render/tonemap.*` (the resolve pass) and `files/shaders/lib/view/linear.glsl`; one flag in the scene uniform (see Design); no change to any file format, save or Lua API.

Status: started 2026-11-01, toddhow's word ("do all the graphics foundations: F2, F3, F4"); this agent does F2 only. Full loop (Track V renderer).

## Design

**Two paths, chosen once at start.** `[Rendering] hdr = false` (default): the code that runs is the code that ran before this task; no float image, no extra pass, no changed shader result. `true`: the path below. The setting is read at start; changing it needs a restart. Headless (`Render::Headless`) and the editor's hosted engine never take the HDR path (the setting is read only by `MWRender::RenderManager`); a build log line says so.

**Render order with `hdr` on.**
1. Compute (light grid, particles) as before.
2. The main scene render graph draws into a `Render::HdrTarget`: a multisampled `VK_FORMAT_R16G16B16A16_SFLOAT` colour image, its resolved single-sample copy (usable as a sampled image and a transfer source), a multisampled `D32_SFLOAT` depth and its resolve, with the same sample count as the swapchain path (`Engine::framebufferSamples`). Same render pass shape as `Render::createMultisampledRenderPass` (store on, so the suspend-and-resume used by the water copy keeps working), with the resolve colour left in `SHADER_READ_ONLY_OPTIMAL`.
3. `Reflect` (the opaque-scene copy the water and soft particles read) and `PostProcess::depthCopy` take their sources from the HdrTarget, not the window. The colour copy is float on this path.
4. `Render::Tonemap`: a fullscreen triangle in a render graph on the window's own (main) render pass reads the resolved float colour, applies exposure, tonemap and the linear to sRGB encode, and writes the swapchain image. From here on everything is the existing 8-bit pipeline: the screenshot copy, the `.omwfx` chain (`PostProcess`), the GUI pass and present.
5. Bloom (stage d): a downsample and upsample chain on half-resolution float targets, added in step 4 by `Tonemap`.

**Linear light.** The shaders compute on display-encoded (gamma) numbers today; research note 0008 section 2. With `hdr` on only the main view lights in linear space.
- **The transfer function is a pure power law, gamma 2.2, on the CPU and the GPU alike** (`Render::Hdr::displayGamma`, `lib/view/linear.glsl`). Not the piecewise sRGB curve: today's swapchain holds the lighting's numbers as they are and the display shows them with a roughly 2.2 curve, so a pure power is the closest match, it makes a product of encoded values equal the encoded product of the linear ones (texture times light stays as it is), and encode and decode are exact inverses. It is sign-preserving (`sign(x) * |x|^2.2`): Morrowind has negative (darkness) point lights, and a plain `pow` of a negative is NaN, which bloom would spread.
- **The flag.** `Scene::fogColor.w` is 2 for the linear main view. It is 1 in the main view today (`updateSceneData`) and the other views leave it at 0 or an editor colour's alpha, so the shaders test `> 1.5`, never `!= 0`. It is not a new field: growing `Pipeline::Data::Scene` past 640 bytes brightened interiors once (`apps/openmw/mwrender/CLAUDE.md`). Preview, local map, the editor and the shadow and occluder views never set it, so they keep their present maths and render to their 8-bit targets as before. `updatePostProcess` hands the `.omwfx` uniforms the display-encoded values (kept as copies), with `fogColor.w` 1: the chain runs on the 8-bit picture.
- **CPU conversions, once:** ambient, sun diffuse and specular, fog, sky colour, point light colours (`CollectLights::pack`, linear option set on the main view's instance only), the clear colour.
- **Shader conversions, behind the flag:** material colours, vertex colours, the environment map, and water and sky constants. For textures, the maps are combined on encoded values first (`diffuse * dark * detail * 2`, then the decal mix), and the result is converted once: the detail map's times-two is not neutral in linear light (0.5 encoded would become 0.44), so converting each map first would darken every detail-mapped mesh. Glow and env-map additions are converted on their own. Normal, specular and other data textures are not converted.
- **Deviation from note 0008, which says "textures read as sRGB again on this path":** conversion is in the shader after filtering, not by hardware sRGB image views, because the same texture objects are shared with the 8-bit views (preview, local map) and the GUI reads its textures through the same loader. Cost: a conversion per sample; filtering and mipmaps stay on encoded values, as today. Revisit in F4.
- **The Slang sun** (`sky/sun/sun.slang`, `[Rendering] slang shaders`) gets the same linear branch as the GLSL sun; the Slang-versus-GLSL frame check is rerun with `hdr` on and off.
- **Mixes that are artistic, not physical, stay on encoded colours:** the sky's gradient (`sky/atmosphere`), fog (`lib/view/fog.glsl`), the cloud tint and the moon's sum are computed on encoded values and converted once, because a mix in linear light is much brighter and hazier than the picture these were tuned for (seen in the first frames). Lighting sums (ambient, sun, point lights, emission) and alpha blending are linear.
- With the flag, lighting is not clamped to 1 (`clamp lighting` still applies in the other views). Without that the float target would carry nothing the 8-bit one does not.


**Tonemap.** `vanilla` (default of the path): exposure, then clip to 1, then encode. It is the nearest thing to today's picture: lighting that stays under 1 looks the same as now, up to the differences below. `reinhard` (extended, white point 4) and `aces` (the Narkowicz fit of the ACES curve) are soft shoulders for values over 1. Exposure is manual: `hdr exposure` in stops (EV), default 0. No automatic exposure in this task.

**Expected differences from today even with `vanilla`, `exposure 0`** (the path is not meant to be pixel equal to the default path, which is why it is a setting): additive terms (ambient plus diffuse, fog mixes, alpha blending, particle blending) are summed in linear light, where today they are summed on encoded values; multiplicative terms (texture times light) are almost identical (a pure power law gives the same product). Unclamped lighting can exceed 1 and then clips at the end.

**Mod `.omwfx` rule.** Techniques run after the tonemap on the 8-bit picture, as today. A technique that sets `hdr` expects a float scene and tonemaps for itself (research 0008, section 2); the port does not offer a float scene to the chain (`apps/openmw/mwrender/postprocess.hpp`, "Not ported yet"). Rule implemented: if any enabled technique has `getHDR()` true, the built-in tonemap runs with the identity-clip curve `vanilla` and the exposure forced to 0, and the log says once that the built-in tonemap stepped aside. The technique still receives the 8-bit picture (the float scene for it is a later step; stated as not done). Exact API (`Fx::Technique::getHDR`) to be confirmed at implementation, see the stage log.

**Not on this path (stated, not hidden).** Auto exposure; HDR display output (HDR10 or scRGB swapchain); bloom thresholds beyond one strength; temporal anything; the editor (stays 8-bit); headless capture; the planar reflection of the water is unchanged (water uses screen space reflection of the opaque copy); shadow cascade views and the sun occlusion view are depth only.

**Room for F3, F4 and later.**
- F3 (stored depth, normals, velocity): `HdrTarget` owns the attachments by name in one place (`colour`, `colourResolve`, `depth`, `depthResolve`); F3 adds a normals attachment (and a velocity one) to the same render pass and framebuffer, with a second colour output `layout(location=1)` guarded by a define, and the resolved copies sampled by later passes. The render pass builder takes a list of extra colour formats from the start (empty now).
- F4 (material model): the linear flag and `linear.glsl` are where shaders learn they are in the float path; a PBR evaluation replaces `materialTerms` for the same flag.
- Baked GI and an optional ray-traced tier: nothing in this task requires either; an irradiance term is just one more added light in `materialTerms`, and ray queries would read the float colour and depth the same way a screen-space pass does. `Render::HdrTarget` does not assume ray tracing and no extension is requested.

## Constraints
Determinism: no change. Allocation: nothing allocated per frame; images are created at start and on resize. Threading: all new code runs on the main thread (record thread reads only). Off by default and unchanged when off: with `hdr = false` the shader results are those of `823ac88856` (the new shader branches test a flag that is below 1.5 in every view), no new image or pass exists, `OPENEDEN_FRAME_STATS_FILE` is as before; proved by frame comparison (below). New shaders are GLSL (the Slang toolchain exists, but the passes are one-screen-triangle code and the rest of the path is GLSL; porting later is the decision 0009 route). Ownership: `RenderManager` owns the `HdrTarget` and `Tonemap` by `std::unique_ptr`; images by `vsg::ref_ptr` as the surrounding code does. Resize: the target is rebuilt in `syncExtents`; old images go through the compile context's `detach`.

## Implementation rules found before coding (reviewer advice, kept here so they are not lost)
- **Graphs.** `mRenderGraph` stays the scene graph (it already works with a framebuffer: `SuspendRenderPass`, `setClearValues`, the compile view's framebuffer branch), now on the HdrTarget's framebuffer. The tonemap gets its own window graph (`mEngine.createRenderGraph()`), and the GUI graph takes its framebuffer and window from that one, not from `mRenderGraph`.
- **Compile views.** The main view is registered with the compile manager on the HdrTarget's framebuffer. The tonemap pipeline, and `PostProcess::createPresent` (which today compiles for `mMainView`, assumed compatible with the GUI pass), are compiled for a view registered on the window, so they match the swapchain's render pass. Sites to check: `contextSelectionFunction`, `setMainView`, `colorImageView(`, `depthImageView(`, `getPresentLayout()`.
- **Sources.** `Reflect::update` and `PostProcess::updateImages` take the source images and layouts from the HdrTarget when it exists (resolved single-sample images only).
- **Command order** in the command graph: scene graph, bloom, tonemap, post-process node, screenshot switch (the screenshot must see the tonemapped swapchain).
- **Resize.** Only images and the framebuffer are rebuilt; the `vsg::RenderPass` object is reused (pipelines were compiled against it); the framebuffer is swapped in the same `syncExtents` call that sets the render areas.
- **GPU timing.** `tonemap` and `bloom` passes are registered before the first frame (the table holds 16).
- **Gate.** No validation layer: render pass incompatibility fails silently. The F2a gate is quantitative: HDR-on F2a against the default path by `diffmap`, within the noise floor of two default runs.

## Interfaces (pure parts, for tests written from this spec alone)
Header `components/render/hdrmath.hpp`, namespace `Render::Hdr`, header-only, no Vulkan types:
```cpp
constexpr float displayGamma = 2.2f;
float toLinear(float encoded);           // sign(x) * |x|^2.2 ; toLinear(0) == 0 ; NaN in gives NaN out
float toEncoded(float linear);           // sign(x) * |x|^(1/2.2) ; exact inverse of toLinear
vsg::vec3 toLinear(const vsg::vec3&);    // per channel
vsg::vec4 toLinear(const vsg::vec4&);    // rgb converted, alpha untouched
vsg::vec3 toEncoded(const vsg::vec3&);
enum class Tonemap { Vanilla, Reinhard, Aces };
std::optional<Tonemap> parseTonemap(std::string_view name);   // "vanilla" | "reinhard" | "aces", else nullopt
float exposureScale(float stops);        // 2^stops
float tonemapChannel(Tonemap curve, float linear);  // result in [0,1] for every finite input; negative input gives 0
struct TonemapChoice { Tonemap curve; float exposureStops; bool steppedAside; };
TonemapChoice resolveTonemap(Tonemap curve, float exposureStops, bool techniqueWantsHdr);
bool useHdrPath(bool settingOn, bool hostedByWindow);   // true only when both are true
```
Curves: `Vanilla` is `clamp(x, 0, 1)`. `Reinhard` is the extended form `x * (1 + x / (W * W)) / (1 + x)` with W = 4 (named constant `reinhardWhitePoint`), clamped to [0,1]. `Aces` is Narkowicz's fit `(x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)`, clamped to [0,1]. `resolveTonemap` returns `{Vanilla, 0, true}` when `techniqueWantsHdr`, else `{curve, exposureStops, false}`. The shader constants in `lib/view/linear.glsl` and the tonemap shader are written from `displayGamma` and the curves above; the unit tests check the CPU functions, and the frame checks cover the GPU.
Settings, `[Rendering]`, in `components/settings/categories/rendering.hpp`: `hdr` (bool, false), `hdr tonemap` (string, `vanilla`), `hdr exposure` (float, 0, clamped to -8..8), `hdr bloom` (bool, false), `hdr bloom strength` (float, 0.05, clamped to 0..1).

## Baseline for the frame comparison
Captured before the first change of this task, from the build at `823ac88856` (rendering identical to its parent): `scratchpad/hdr/base_*` (Seyda Neen noon, Caldera Mages Guild with a torch, Seyda Neen midnight, the sea, Bitter Coast rain), two runs each, 1280x720, same console scripts. A main-menu frame and an inventory frame (preview and map textures) are captured when a way to get them is found.

## Stages (each commits working, default off unchanged)
- **F2a Float target plumbing, identity tonemap.** HdrTarget, Tonemap with `vanilla` clip and no encode (the scene is still lit on encoded values), Reflect and depth copy sourced from the target, resize, screenshot, post-process chain. Result must look like the default path within quantisation.
- **F2b Linear lighting.** `linear.glsl`, the flag, CPU conversions, shader conversions (objects, particles, groundcover, terrain, water, sky), unclamped lighting. Tonemap now encodes.
- **F2c Tonemap and exposure.** `hdr tonemap`, `hdr exposure`, the `.omwfx` `hdr` rule.
- **F2d Bloom.** Optional, `hdr bloom`, `hdr bloom strength`.

## Acceptance criteria
- [ ] `[Rendering] hdr = false`: frames equal the pre-task build (Seyda Neen exterior, Caldera Mages Guild interior with point lights, night, water, main menu), by `diffmap`, to within the measured run-to-run noise, with GPU timing off; no HdrTarget or extra pass is created.
- [x] F2a: with `hdr` on, the scenes look like the default path (viewed and by `diffmap`), with the screenshot, `.omwfx` chain and GUI working, MSAA 0 and 4. Not a window resize (not verified), and the water differs (see the F2a log).
- [x] F2b: with `hdr` on, exterior, interior with point lights, night, water, sky, rain/particles and terrain look sane (viewed, compared with the default path), differences explained in the stage log.
- [x] F2c: `vanilla`, `reinhard` and `aces` each draw; exposure of +1 and -1 EV visibly brightens and darkens; a technique with `hdr` makes the built-in curve step aside.
- [x] F2d: bloom draws on bright lights and sun, and is off with `hdr bloom = false`.
- [ ] The editor (`openmw-cs`, `apps/opencs`) still builds and draws a cell (screenshot).
- [ ] Unit tests for the pure parts (colour conversions on the CPU, tonemap curves, exposure, the HDR-on/off decision) pass: `components-tests`, `openmw-tests`, `eden-studio-tests`; Linux gate builds with `-Werror`; the whole tree passes `CI/check_clang_format.sh`; a fresh reviewer read each stage.
Out of scope: F3 and F4; automatic exposure; HDR display output; changing any default; porting shaders to Slang; PBR; the editor's rendering; VR.

## Stage log

### 2026-11-01, stage F2a: float target plumbing, identity tonemap (Windows, RTX 4070, `build-jolt`)

**What was built, in plain language.** With `[Rendering] hdr = true` the scene is drawn into a 16-bit float image (`Render::HdrTarget`) and a second, tiny pass (`Render::Tonemap`) copies it onto the swapchain; everything after that (screenshot, `.omwfx` chain, GUI, present) is the old 8-bit pipeline. In this stage the scene is still lit on display-encoded numbers and the tonemap is the identity (clip at 1), so the picture should be the same as without the setting. `Reflect` (the water's copy of the opaque scene) and the depth copy for post-processing read the float target's single-sample images. A resize replaces the images and framebuffer and keeps the render pass.

**Verified.**
- Frames, `coc "ToddTest"` (an interior with no light objects, so nothing flickers) and Seyda Neen, with `[Input] camera sensitivity = 0.01` so the camera holds still (without it the mouse moves the view between runs): default path run twice differs by a mean sum of 0 to 665 (noise floor); default path of this build against the capture of the build before this task (`base_ns2`): sum 366, 3 pixels over 6 (unchanged when off). HDR on against off, antialiasing 0 and 4: interior sum 262 and 3124, 0 pixels over 6 (identical to within rounding); exterior: only the water differs (below). Post-processing enabled with `chain = adjustments`, antialiasing 0: identical (sum 262).
- With post-processing and antialiasing 4 both paths draw (viewed); the frames' views differed there (mouse), so no number.
- GPU timing on: a `tonemap` pass is timed (median 0.08 ms at 1280x720 on this machine).
- Tests: `components-tests` 2423 and `openmw-tests` 799 pass (with another session's unfinished test file left out of the build; see Not verified).
- A fresh reviewer read the diff; its findings were fixed (the resize-time shader recompilation is cached, dependencies without by-region, member order, an error when the tonemap view is not registered, comments).

**Finding, not fixed (off means unchanged).** The water differs between the paths. The default path's water is a flat grey with no reflections (`m_sea`, `base_sea1`); the HDR path's has waves, sparkle and a reflection of the player, which the OSG build also showed (`compare/osg/sea.png`). Both run the same shader; what differs is the copy of the scene's colour and depth that the water reads. The default path's main render pass has only an external-to-subpass dependency and no dependency from its writes to the later copy, while the HDR pass has both; a missing dependency would make the copy read unfinished data. This is a hypothesis: no validation layer is installed, and I did not change the default path to test it. For toddhow: it may be a latent bug in the default path's water.

**Not verified.** Window resize (cannot be driven here; the code path replaces the images, framebuffer and tonemap draw in `syncExtents`), the main menu frame (the capture rig does not capture outside a game; the menu ran 25 s with HDR and antialiasing 4 without a crash), a device without float blending, the editor build (`openmw-cs` was not built: it only gained an inline `Engine::hasWindow`), the Linux gate, `eden-studio-tests` (another session's unfinished tests do not compile), clang-format over the whole tree.

### 2026-11-01, stage F2b: linear lighting

**What was built.** With `hdr` on, the main view lights in linear space. The CPU converts the scene colours once (`updateSceneData`: fog, ambient, sky, sun, the clear colour; `CollectLights::setLinear` for the point lights, sign-preserving so darkness lights stay negative) and marks the view with `Scene::fogColor.w = 2`. The shaders (`lib/view/linear.glsl`) convert what the CPU cannot: textures (combined first on encoded values, then converted once), vertex and material colours, the environment map, glow, water and sky constants, and the Slang sun. Lighting is not clamped to 1 in that view. The tonemap now encodes. The fog mix, the sky's gradient, the cloud tint and the moon's sum stay on encoded colours and are converted once: the first frames showed a heavy haze and a bright sky when they were mixed in linear light. The post-processing uniforms still get the display-encoded colours. `linear.glsl` had to be added to `files/shaders/CMakeLists.txt`, or the default path stops compiling its shaders (every shader that includes `terms.glsl` includes it).

**Verified.**
- Default path with the new shaders against the default path before them: ToddTest interior sum 223 and Seyda Neen sum 179 (noise floor), after the shader change.
- HDR on, viewed side by side with off (`scratchpad/hdr/m_*.png`): Seyda Neen noon (terrain, fog, sky, trees, NPC: very close; slightly more contrast), a rainy sea and Bitter Coast rain (rain particles, fog, stone wall: close), Caldera guild with a torch (point light: close), ToddTest interior (close), night (see below).
- `components-tests` 2426 pass (new: `view/testcollectlights.cpp`: colours converted only when asked, intensity untouched, darkness light keeps its sign and is not NaN).

**Differences seen, by design or open.** Water: see stage F2a. Night: the sky has a stronger purple glow in the HDR frame; the sky layers (clouds, stars) are alpha blended over the gradient, and blending in linear light is brighter than blending on encoded values. Not tuned. The linear-light fog and sky mixes are deliberately kept on encoded values, so fog density matches.

**Not verified.** The Slang sun with `hdr` on (the Slang branch is written and compiles, a frame with both settings on was not taken); particles with `hdr` on beyond rain; the editor.

### 2026-11-01, stages F2c and F2d: tonemap, exposure, the `hdr` technique rule, bloom (built and committed together)

**What was built.**
- `hdr tonemap` (`vanilla`, `reinhard`, `aces`) and `hdr exposure` (EV) reach the tonemap pass as push constants. An unknown curve name logs a warning and uses `vanilla`.
- The `.omwfx` rule: `RenderManager::onFrame` checks the enabled chain each frame; if a technique that runs (parsed, and no render error) sets `hdr`, the built-in tonemap only clips and encodes at exposure 0 (`Render::Hdr::resolveTonemap`), and the log says so once. The technique still gets the 8-bit picture, as stated in the Design section; a float scene for it is not offered. No shipped technique sets `hdr` (`grep` of `files/data/shaders`), so it was tested with a technique written for the purpose (`scratchpad/hdr/hdrdata/shaders/hdrtest.omwfx`).
- Bloom (`Render::Bloom`, `[Rendering] hdr bloom`, `hdr bloom strength`): a chain of up to six half-size float images, a 13-tap downsample down, a 3x3 tent upsample added back up, the largest image added to the scene inside the tonemap before exposure. No threshold. Bloom stays on when a technique steps the curve aside.

**Verified (Windows).**
- Exposure: mean brightness of one frame 78.3 at 0 EV, 131.0 at +2 EV; viewed at +1 and -1 EV (brighter, darker). `aces` and `reinhard` draw (viewed `aces`; `reinhard` drew, not compared closely).
- Stepping aside: with the test technique enabled and `hdr exposure = 2`, the frame's mean is 78.3, the 0 EV value; the log line appears.
- Bloom on at strength 0.15 in Seyda Neen: a soft glow on the sky and mist, nothing else changed (viewed); also with antialiasing 4. Bloom off (the default): no object exists.
- `components-tests` pass for everything of this task (38 tests in the HDR, tonemap, CollectLights and Slang suites run alone; one test of another session fails in the full run: `ManifestEditorLibraryTest`); `openmw-tests` 799.
- A fresh reviewer read both stages' diffs. Its findings were fixed: a failed technique no longer steps the curve aside, a NaN texel no longer spreads in the bloom chain, comments.

**Not verified.** Bloom after a window resize (the code path replaces the chain and the tonemap source in `syncExtents`; cannot be driven here); GPU timing with every pass registered at once (the table holds 16; this adds `tonemap` and `bloom` to about 11 passes); the Slang sun with `hdr` on; the whole-tree clang-format check and the Linux gate are recorded at the end of this log.

### 2026-11-01, close-out

- **Linux gate** (Docker, `-Werror`, at `ed098cbc02`): `components-tests` 2444, `openmw-tests` 799 and the 32 Lua integration tests pass. The first run found a shadowed lambda parameter in `bloom.cpp` (fixed in that commit).
- **clang-format 14**: every file of this task passes. `CI/check_clang_format.sh` over the whole tree at `3043df905e` still flags 17 files, none of them this task's: all are the Eden Studio session's (`apps/opencs/*`, `apps/opencs_tests/*`, `sdk/*`, `components/extension/manifest_io.cpp`, `apps/components_tests/extension/test_manifest_editor_entry.cpp`). They have uncommitted edits in the same files, so I left them alone.
- **Not done, for toddhow:** the editor build (`openmw-cs`) and `eden-studio-tests` were not rebuilt after this task (another session's work in progress did not compile at the time); a resize test; the Slang sun with `hdr` on; a main-menu and an inventory frame (the capture rig does not capture outside a game).
- **Commits:** F2a `f4bb5f2cd3`, F2b `fe426b1346` and `31ecfa4996`, F2c and F2d `3043df905e`, shadow fix `ed098cbc02`.
- **For F3:** `HdrTarget`'s constructor takes `extraColorFormats` (render pass, framebuffer and resolved views already handle them; the shaders need a second `layout(location=1)` output behind a define, and the main view's pipelines are compiled against this render pass, so add attachments before the scene's pipelines compile). Depth is resolved sample zero into a single-sample depth that stays in `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`. `Reflect` and `PostProcess` are fed the resolved images each frame (`setSource`, `setDepthSource`), the model for any new consumer. Stored velocity needs the previous frame's bone matrices (not built). The linear view is marked by `scene.fogColor.w == 2`; materials (F4) hook in at `materialTerms` and `linearView()`.
