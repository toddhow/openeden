# Task 0363: Finish the move to Slang and delete our own GLSL

Status: **DRAFT, stage a (survey and plan) only.** Nothing but this document is changed. Follows task 0072 (stages a to j), decision 0009, research note 0035.
Design doc section: `files/shaders/CLAUDE.md`, `components/render/CLAUDE.md`.
Date: 2026-11-20 · toddhow's decisions (2026-11-20): finish the move to Slang, then delete our own GLSL shaders; cut the define sets by turning most `#ifdef` defines into runtime values so that every reachable set of every family is prebuilt at build time; what is still not prebuilt compiles on demand through the existing Slang runtime (`vsgUtil::SlangRuntime`) and the draw waits (load screens wait, the cache keeps it); the mods' `.omwfx` post-processing keeps glslang (decision 0009).

Goal: one shader language in the game for everything we ship, with no set that has to fall back to GLSL.
Inherited code touched (in later stages, not this one): `components/pipeline/{graphics,terrain,mode}.*`, `components/vsgutil/{shaderpermutation,slangruntime,readshader,passstages}.*`, `components/resource/shadersettings.cpp`, `files/shaders/**`, about 48 test files.
Out of scope: the `.omwfx` translation path and glslang for mod passes; any visual change.

## 0. Summary

1. **The define sets cannot be prebuilt as they are.** Terrain fragment alone is 43,320 reachable sets (`PipelineTerrainDefines`, `testterraindefines.cpp`): at the measured 0.37 s and 54 to 218 KB per set that is **4.5 CPU hours and about 3.8 GB** (my estimate from the 40 listed sets, mean 88.7 KB). The object fragment is open-ended (17,856 for one draw kind, 71,424 for a skinned one with a height file, task 0072 stage f).
2. **Most defines change control flow only.** The view descriptor set already has every binding in its layout and a dummy resource for each (`components/view/descriptors.cpp`: `dummyProbes`, `dummySkyVis`, `dummyCloudShadow`, `dummyPointShadowMap`, `dummyPuddleMap` and ten more) and the object pipeline layout is already ONE fixed layout (`Pipeline::Graphics::Graphics`: "A fixed layout may allow the implementation to cheaply switch pipelines"). So GI, sky visibility, cloud shadows, wetness, cover, point shadows, PBR, parallax quality, coverage, LOD fade and the debug views can become **specialization constants** (machinery exists: `Resource::settingsConstantTable()` already ships about 40 constants, and `Graphics::create` already passes ids 0 to 3 and the UV set ids).
3. **Only three kinds of define change the interface** and stay compile-time (or are made constant by dummy resources, plan B below): the G-buffer output count, the vertex to fragment varyings, and which texture units/buffers the shader statically uses.
4. **Set counts** (the "today" columns are measured or cited; "after" are estimates, the 20 and 9 are measured on the traced sets):

| Stage | Today, listed (permutation files in the build) | Today, reachable | After plan M (settings cut) | After plan B (settings + fixed bindings) |
| --- | --- | --- | --- | --- |
| Terrain fragment | 40 | **43,320** | **20** (5 map states x 4 output states) | 20 |
| Terrain vertex | 4 | 4 | 2 | 2 |
| Water fragment | 12 | 12 | 2 | 1 to 2 |
| Water vertex | 1 | 2 (`WATER_CELL`) | 1 to 2 | 1 |
| Object fragment | 76 | open-ended (17,856 to 71,424 per draw kind) | content only: **20** of the 76 traced sets stay distinct once control defines are dropped; real content classes bounded by texture/varying combinations (a few hundred at most, estimate) | **about 8** (9 traced classes once texture units are dropped too; 4 output states x particle or not) |
| Object vertex | 21 | content-driven, 12,288 to 76,800 | 17 of the 21 traced stay distinct | **about 2** |
| Tonemap | 16 | 16 (32 with GRADE) | 16 (keep, see 2.3) | 16 |
| GUI, sky (12), compute (4), other post (9) | 1 each | 1 each | unchanged | unchanged |

   SPIR-V on disk: 169 sets, 13.1 MB today (`default/main.frag` 76 files 8.30 MB, terrain fragment 3.55 MB, water 0.76 MB, tonemap 0.17 MB, object vertex 0.28 MB), to roughly 6 to 8 MB after plan B (estimate, section 2.4).
5. **GPU risk is real but controllable:** use specialization constants (resolved by the driver at pipeline creation), NOT uniform branches, in the object and terrain fragment shaders. Measure with three interleaved runs per variant (section 3).
6. **Without GLSL the pipeline needs a different path** (section 4): a stage is made from SPIR-V directly; an unlisted interface class compiles on the Slang worker and the *requesting thread* waits (loader threads block, the main thread only behind a loading screen with the event pump running). With plan B the unlisted case should not occur for built-in content, which is the argument for B.
7. **Order:** b constants infrastructure with water as the pilot (and the default flip), c terrain, d object shader (M then B), e the small families, f runtime and test changes, g the deletion (section 5).

## 1. Survey

### 1.1 What defines exist and where they come from

- `Pipeline::Mode` (`components/pipeline/mode.hpp`): 62 enumerators; `vertexModes` holds 16 and `fragmentModes` 27 of them (the loader's content modes). `Pipeline::Graphics::create(const Options&)` (`graphics.cpp` lines 262 to 355) adds the settings-driven ones: `ALPHA_TO_COVERAGE`, `ADJUST_COVERAGE`, `GBUFFER`, `FORCE_PER_PIXEL_LIGHTING`, `PBR`, `PBR_PHYSICAL`, `GI`, `GI_SKY`, `WETNESS*`, `COVER*`, `POINT_SHADOW`, `PARALLAX_POM*`, `GBUFFER_MATERIAL`, `GBUFFER_AMBIENT`, `GBUFFER_MOTION`.
- `Pipeline::terrainDefines` (`components/pipeline/terrain.cpp` lines 24 to 80): `AMBIENT_CUBE`, `TERRAIN_MAPS`, `TERRAIN_PBR`, `PARALLAX_POM` (+`_LOW`, `_HIGH`), `GBUFFER` (+`_MATERIAL`, `_AMBIENT`, `AO_TERRAIN`), `POINT_SHADOW`, `GI_SKY`, `WETNESS` (+`_PUDDLES`, `_DEBUG`), `COVER` (+`_DEBUG`).
- Settings files' `#define` lines (`Resource::ShaderSettings`, `components/resource/shadersettings.cpp`): `shadow.settings` gives `CLOUD_SHADOW`, `CLOUD_SHADOW_DEBUG`, `VOLUME_CLOUD_SHADOW`, `DEBUG_SHADOW` (and `DEBUG_CHUNKS` for the terrain). The numeric settings (fog, light attenuation, shadows, AO radius...) are already specialization constants for the twins (`settingsConstantTable`, ids from `settingsConstantBase`).
- The names each stage imports (`#pragma import_defines`): object fragment 45, object vertex 16, terrain fragment 17, terrain vertex 1, water fragment `BUMP_MAP`, water vertex `WATER_CELL`, tonemap `AO BLOOM DEBUG_VIEW GRADE SSS`.

### 1.2 Reachable sets today (cited)

| Family | Listed | Reachable | Source of the number |
| --- | --- | --- | --- |
| Terrain fragment | 40 (`terrain/main.frag.sets`) | 43,320 | `PipelineTerrainDefines.the_settings_reach_43320_fragment_sets...` (`apps/components_tests/pipeline/testterraindefines.cpp` line 286): 25,920 with pbr + 120 hdr-only + 17,280 added by `POINT_SHADOW` |
| Terrain vertex | 4 | 4 | same file, `the_settings_reach_four_vertex_sets...` |
| Water fragment | 12 (`water/main.frag.sets`) | 12 | every set carries `BUMP_MAP`; the other four defines come from the shadow settings (`CLOUD_SHADOW_DEBUG` needs `CLOUD_SHADOW`) |
| Object fragment | 76 | 17,856 to 71,424 per draw kind | task 0072 stage f, item 4 (`countsets.py`) |
| Object vertex | 21 | 12,288 with `GBUFFER_MOTION` beside `SKIN`, 76,800 with UV counts 1 to 8 | same |
| Tonemap | 16 | 16 | `post/tonemap.frag.sets` |
| Everything else | 1 per stage | 1 | task 0072 stages a to c |

Listed counts are the permutation files of the build in `I:/Projects/OpenEden-work/combat3/build/resources/shaders` (it compiles this worktree's sources).

### 1.3 Which defines change the interface

I scanned the object fragment's include tree with the `#ifdef` stack at every `layout(...)` declaration (a throw-away script in the scratchpad, not committed). Result for `default/main.frag`:

- **Texture set bindings** (`lib/material/descriptors_frag.glsl`): `DIFFUSE_MAP`, `DARK_MAP`, `DETAIL_MAP`, `DECAL_MAP`, `GLOW_MAP`, `BUMP_MAP`, `ENV_MAP`, `NORMAL_MAP`, `SPECULAR_MAP`, `MATERIAL_MAP`, `HEIGHT_MAP`, `MATERIAL` (the `MaterialData` block), `PARTICLE_OCCLUSION` (`lib/particle/weatherocclusion.glsl`).
- **View set bindings** (set 0): `CLOUD_SHADOW`, `VOLUME_CLOUD_SHADOW`, `GI`, `GI_SKY`, `AMBIENT_CUBE`, wetness puddles, `POINT_SHADOW` (with per-pixel lighting), light grid, lights and indices (per-pixel lighting or `PRECIPITATION_LIT`), `SOFT_PARTICLE` (reflect depth).
- **Outputs**: `GBUFFER` (locations 1 to 3: normal, linear depth, motion), `GBUFFER_MATERIAL` (4), `GBUFFER_AMBIENT` (5).
- **Inputs** (varyings, `lib/inout.glsl`): `NORMAL`, `COLOR`, `TEXCOORD` with the UV set count, `PARTICLE`, `NORMAL_MAP` (tangent), `ENV_MAP`.
- Everything else changes control flow only (PBR, PBR_PHYSICAL, wetness, cover, parallax, coverage, LOD fade, debug views, precipitation, force per pixel).

Terrain fragment (`terrain/bindings.glsl`, `Pipeline::terrain`): `TERRAIN_MAPS` adds the normal layer array and the layer flags, `TERRAIN_PBR` bindings 8 and 9, `PARALLAX_POM` bindings 10 and 11; the G-buffer defines change the outputs; its view-set defines (`GI_SKY`, `AMBIENT_CUBE`, `CLOUD_SHADOW`, `VOLUME_CLOUD_SHADOW`, `WETNESS*`, `COVER*`, `POINT_SHADOW`) are the same control-only kind because the view layout is shared.

For the view set: **its layout is the same for every pipeline** (`getViewDescriptorSetLayout`, `components/pipeline/layout.cpp`) and every binding always holds a valid descriptor (the dummies above). A shader that statically uses a binding the current frame does not feed therefore reads a dummy, which is valid.

## 2. Proposal per define

Three mechanisms, chosen per define:

- **K: keep as a compile-time define** (interface-changing, few values).
- **C: specialization constant** (control flow only; the driver folds it at pipeline creation; a pipeline per key exists today anyway, so the pipeline count does not change).
- **R: runtime branch** (a uniform/push flag read by every pixel). **Proposed for nothing in the object and terrain fragment shaders** (section 3); allowed for debug-only views.

Related defines are folded into one integer constant where they nest (`PBR`/`PBR_PHYSICAL` -> `cPbrLook` 0..2; `WETNESS`/`_PUDDLES`/`_DEBUG` -> `cWetness` 0..3; `COVER`/`_DEBUG` -> `cCover` 0..2; `PARALLAX_POM`/`_LOW`/`_HIGH` -> `cParallaxQuality` 0..3). This avoids derived booleans, because **Slang is expected to emit `OpSpecConstantOp` for `a && b`, which `SlangPermutations` forbids today** (task 0072: "no `OpSpecConstantOp` in any twin"). Either the CPU passes every derived value as its own constant (my choice) or toddhow relaxes the rule (decision 3).

### 2.1 Object fragment (`default/main.frag`, 45 imported names + 4 shadow settings)

| Group | Defines | Mechanism | Reason |
| --- | --- | --- | --- |
| Output state | `GBUFFER`, `GBUFFER_MATERIAL`, `GBUFFER_AMBIENT` | **K** (4 states: none, 4, 5, 6 colour outputs) | the render pass has that many attachments; a shader that writes fewer leaves them undefined |
| Particle path | `PARTICLE` | **K** | a different input set and shading path (12 sets for the whole particle draw, task 0072 stage f) |
| Varyings | `NORMAL`, `COLOR`, `TEXCOORD`, `UVSETS_n`, `NORMAL_MAP`, `ENV_MAP` | **K in plan M; C in plan B** | a fragment input the vertex stage does not write is a link error; plan B makes the vertex stage write a fixed interface (the driver strips unused varyings at link time, RECALLED-VERIFY) |
| Texture units | `DIFFUSE_MAP`, `DARK_MAP`, `DETAIL_MAP`, `DECAL_MAP`, `GLOW_MAP`, `BUMP_MAP`, `SPECULAR_MAP`, `MATERIAL_MAP`, `HEIGHT_MAP`, `MATERIAL`, `PARTICLE_OCCLUSION` | **K in plan M; C in plan B** | the layout already has all units; what differs is the per-material descriptor set, which holds only the units the mode uses. A shader that statically uses a binding the set did not write is invalid (whether a dead specialization branch counts as static use is RECALLED-VERIFY, so assume it does). Plan B writes a shared dummy into every missing unit |
| Settings, view set | `GI`, `GI_SKY`, `CLOUD_SHADOW`, `VOLUME_CLOUD_SHADOW`, `CLOUD_SHADOW_DEBUG`, `DEBUG_SHADOW`, `AMBIENT_CUBE`, `POINT_SHADOW`, `SOFT_PARTICLE` (fragment part), `PRECIPITATION_LIT`, `FORCE_PER_PIXEL_LIGHTING` | **C** | bindings always valid (dummies); control flow only |
| Settings, lighting | `PBR`, `PBR_PHYSICAL`, `WETNESS*`, `COVER*`, `MATERIAL_MAP_RMAOS`, `RECONSTRUCT_NORMAL_Z`, `PARALLAX`, `PARALLAX_POM*`, `PARALLAX_DISCARD`, `LOD_FADE`, `ADJUST_COVERAGE`, `ALPHA_TO_COVERAGE`, `GBUFFER_MOTION` (fragment) | **C** | control flow only; `ALPHA_TO_COVERAGE` writes `gl_SampleMask`: check that declaring the builtin and not writing it is harmless (RECALLED-VERIFY, tested in stage d) |

Measured evidence: of the 76 traced fragment sets, dropping the control-only names (28 defines in my split) leaves **20 distinct sets**; also dropping the texture units leaves **9**. These are the numbers behind "20 after M, about 8 after B".

### 2.2 Object vertex (`default/main.vert`, 16 names)

`VERTEX NORMAL COLOR TEXCOORD`, `SKIN`, `MORPH`, `INSTANCE`, `INSTANCE_TINT`, `BILLBOARD`, `TEXMAT`, `PARTICLE`, `ENV_MAP`, `NORMAL_MAP` change vertex inputs (the pipeline's vertex input state is built from the modes, `graphics.cpp` lines 453 to 520), the storage buffers (`BONE_BINDING`, `MORPH_BINDING`, `PARTICLE_BINDING`) and the varyings: **K in plan M** (of the 21 traced sets, dropping `GBUFFER_MOTION`, `LOD_SWAY` and `SOFT_PARTICLE` leaves 17). `LOD_SWAY` and `SOFT_PARTICLE`: **C**. `GBUFFER_MOTION` (adds a previous-position varying): **K in M, C in B**. Plan B: constants for the presence of attributes and of the three buffers (an absent vertex attribute reads as undefined, which the spec allows, RECALLED-VERIFY), dummy buffers bound in the object set; target about 2 SPIR-V (particle or not).

### 2.3 Terrain, water, tonemap

- **Terrain fragment (43,320 -> 20):** `TERRAIN_MAPS`, `TERRAIN_PBR`, `PARALLAX_POM*` -> **K** as a 5-state "map class" (off; maps; maps + pbr; maps + parallax; maps + pbr + parallax), since they add bindings to a per-pipeline layout built in `Pipeline::terrain`; the parallax *quality* inside a class is **C**. G-buffer outputs (4 states) **K**. `GI_SKY`, `AMBIENT_CUBE`, `CLOUD_SHADOW*`, `VOLUME_CLOUD_SHADOW`, `DEBUG_SHADOW`, `DEBUG_CHUNKS`, `WETNESS*`, `COVER*`, `POINT_SHADOW`, `AO_TERRAIN` -> **C**. 5 x 4 = **20**. (`PipelineTerrainDefines` models all of this; its 43,320 test becomes the test of the 20.)
- **Terrain vertex (4 -> 2):** `TERRAIN_MAPS` stays **K**, `DEBUG_CHUNKS` **C**.
- **Water (12 -> 1 or 2):** the four shadow-setting defines -> **C**; `BUMP_MAP` is in all 12 listed sets, so it is the only compile-time define (2 states at most). Vertex `WATER_CELL` (task 0129) -> **C**, a pure vertex branch.
- **Tonemap (16, keep):** `AO`, `BLOOM`, `SSS`, `DEBUG_VIEW`, `GRADE` choose which input textures the pass binds (the pass layout is built per pass); the family is fully prebuilt today (167 KB for all 16) and runs once per frame on the whole screen. Keep **K**.
- Sky, GUI, compute, remaining post, particles: one set each, nothing to cut.

### 2.4 Build size and time (measured with `slangc`, -O2, this machine, 24 cores, shared with other work)

| Compile | Time | SPIR-V |
| --- | --- | --- |
| object fragment, big set (22 defines: GI, GI_SKY, POINT_SHADOW, PBR_PHYSICAL, WETNESS_PUDDLES, parallax...) | 1.03 s, 0.87 s | 225,152 B |
| object fragment, small set (6 defines) | 0.38 s, 0.34 s | 40,876 B |
| object fragment, a 40-define union (not a valid real set: some defines conflict) | 0.99 s | 162,112 B |
| terrain fragment (`TERRAIN_MAPS TERRAIN_PBR GBUFFER`) | 0.37 s | 53,356 B |
| terrain fragment, biggest listed set (file in the build) | n/a | 218,816 B |

Derived: all 43,320 terrain sets would cost about 16,000 CPU seconds (4.5 h; about 11 minutes on 24 cores) and about 3.8 GB (mean 88.7 KB of the 40 listed). Plan B: an object fragment ubershader with every control path in is the size of the largest real set plus the unused paths, **estimated 250 to 350 KB and 1.5 s each** (not measured: my all-on union is not a valid combination), times 8 = 2 to 2.8 MB, 12 s; terrain 20 x about 150 KB = 3 MB, 8 s; the rest as today (1.3 MB). **Total about 6 to 8 MB and well under a minute of CPU** against 13.1 MB and about 100 CPU seconds for the 169 sets today. Estimates; stages b and c measure them.

## 3. GPU-cost risk and how it is measured

- **Uniform/runtime branches in the object and terrain fragment shaders are the risk**, not because of divergence (a uniform branch does not diverge) but because the compiled shader's register count is the maximum over all paths: every draw, including the plain unlit ones, would run at the occupancy of the PBR + GI + wetness + puddles + parallax worst case. This is why the proposal uses specialization constants: the driver specializes and folds the branches at pipeline creation, and the specialized shader is what it is today. Whether a driver removes the dead branch is vendor behaviour (NVIDIA yes in my recollection, AMD and Intel RECALLED-VERIFY); on the one GPU available here it is checked by measurement, not assumed.
- Second risk, on the CPU side: **driver pipeline-creation time rises** because each pipeline starts from a bigger SPIR-V module and folds it. The stutter work of 2026-11-10 (the persistent pipeline cache, `[Rendering] pipeline cache`) is the mitigation; measure `compile.vsg` and frames over 25 ms with a cold and a warm cache.
- Third: SPIR-V rules. A derivative-using sample (`texture()` with implicit LOD) inside a branch is fine when the condition is a specialization constant (uniform); no `OpSpecConstantOp` (derived values come from the CPU).
- **Measurement protocol** (every stage that moves defines to constants, per family): the same binary pair (before = defines, after = constants; or one binary with a switch while both exist), `tools/benchmark.sh` scenes Ebonheart crowd, Balmora long view, Balmora walk, plus the living test profile `main` at Seyda Neen (all features on); `[Rendering] gpu timing` on; **three interleaved runs per variant** (A B A B A B), last 400 frames; report wall-ms median, GPU main-pass ms, frames over 25 ms, the `compile.vsg` scope. Acceptance: GPU main pass within +2 percent or +0.03 ms (whichever is larger) of the before run, wall median equal within the spread of repeats, no new frames over 25 ms with a warm cache; a define that fails stays K. Machine quiet (`tasklist` check): stage j showed 60 to 80 percent artefacts on a shared one. A second driver: Mesa llvmpipe in the Docker image for correctness and instruction-count comparison only, not for speed.

## 4. What changes when there is no GLSL to fall back on

Today the game reads GLSL for every stage, computes the define set from the GLSL text, creates the stage from GLSL and then **replaces its code with the twin** if one exists (`Pipeline::Graphics::createShaderStage`, `vsgUtil::slangTwinFor`, `slangTwin`, `withSlangTwin`, `SlangStageChoice`, `takeSlangTwinsTogether`, `checkTwinAgainstGlsl`, `importedDefines`, `effectiveDefines`, `stageDefines`). Without GLSL:

1. **Stage creation from SPIR-V.** `Graphics::createShaderStage`, `Pipeline::terrain`, and the GUI, post and sky loaders build a `vsg::ShaderStage` from the permutation file for the stage's *interface class* (the defines that stay K) and set the constants as `specializationConstants` (the pattern of `Graphics::create`, ids 0 to 3). The set of K defines is computed in C++ from the Mode/terrain options (a table next to `Pipeline::Mode`), no longer from `#pragma import_defines` of the GLSL; the `defines` line of each `.interface` record already holds the names a set may contain (it is the test's list today), so the table can be checked against the records.
2. **No mixed pipelines.** `takeSlangTwinsTogether` and the "both stages or neither" rule disappear (it existed only because VSG compiles a GLSL pipeline's stages together).
3. **`SlangPermutations`, interface records, goldens.** `every_permutation_has_the_interface_of_its_glsl_stage` compares with the live GLSL and goes; `every_permutation_has_the_recorded_interface` (records only) stays and becomes the test. `.sets` files shrink to the interface classes (object fragment 77 lines -> about 8 to 20, terrain 40 -> 20), the `.interface` files are regenerated (`default/main.frag.interface` is 6,410 lines for 76 sets today; it would be about 1,000). **Golden data: the records are rewritten in a deliberate format change, so toddhow reviews them; they are never regenerated to make a failing twin pass** (task 0072 stage j rule). The interface-exception files shrink with the sets.
4. **Runtime compiler.** `SlangRuntime` stays (shipped library 33.8 MB in `resources/slang`, about +237 MiB peak RSS when loaded; with plan B it is a safety net and a developer tool, loaded on the first miss as now). Its request key already holds source hash, defines and constants; its check `checkTwinAgainstGlsl` becomes a check against the record of the nearest class, or is dropped: the build-time `SlangPermutations` already validates every listed set and a runtime-compiled one is checked only for entry point, Vulkan version and no `OpSpecConstantOp`.
5. **A set that is not prebuilt (mods, new defines): the draw waits.** Today `slangTwinFor` returns Pending, the pipeline is built from the GLSL, flagged with `slangPendingKey` and not cached (`Graphics::getOrCreate`). New: `SlangRuntime::waitFor(request, timeout)` blocks on the worker's completion.
   - **Loader/preload threads** (`Resource::SceneManager` template reads, `CellPreloader`): block up to 20 s, then log an error and leave the model out (a missing object, not a wrong one).
   - **Main thread behind a loading screen:** block with the event pump (loading screens already pump SDL events, `CLAUDE.md` "Input arrives while the game loads"), so the freeze monitor (5 s bar, `IsHungAppWindow`) stays satisfied; the screen can say "Compiling shaders".
   - **Main thread in play** (equipment changes, spell models, incremental cell build): never block. Options (decision 5): (a) leave that part out and retry next frame (`Misc::BuildQueue` already has "unbuilt objects behave like disabled"; extend it to model parts), (b) block up to a bound (a stall of 0.4 to 1 s per compile, the measured cost of a big set), (c) make it unreachable (plan B and the shipped classes cover every built-in combination).
   - **Cache:** the existing `<user data>/openeden/shadercache` stays; a warm-up list (`OPENEDEN_SHADER_TRACE` output) can feed an optional pre-compile in `tools/testprofile/prepare.sh`. No shipped cache.
6. **Settings.** `[Rendering] slang shaders` goes (always on) in the deletion stage; until then it stays so the GLSL remains a switch back. The settings defines of `Resource::ShaderSettings` (`shadow.settings` `#define` lines) become constants in `settingsConstantTable`; the GLSL settings reader stays for `.omwfx`.
7. **What stays GLSL on purpose:** the `.omwfx` translation and its prelude (decision 0009). Slang's own bundled glslang is inside its libraries and is not ours.

## 5. Stages, evidence and tests

Evidence per stage: (1) **new Slang against old Slang**, same binary pair, frames compared with the stage f/j scene matrix (7 scenes: Seyda Neen clear, rain, sunset, night, Balmora, Arrille's Tradehouse, the Guild of Mages) plus the three benchmark scenes. Because only control flow moves into constants, I expect **byte-identical frames** (stronger than the GLSL-against-Slang rounding of stage j); any difference is a bug or a float reordering to be explained. (2) The interface records of every class equal what the old sets declared (plan M exactly; plan B a deliberate superset, listed). (3) The measurement of section 3. (4) Morrowind: the `tools/morrowind_check` Balmora frame `1c5d47ad0b5dc32d` with `slang shaders` on matches the GLSL frame within the noise rules of task 0072 (stage j: rounding level, rain the largest at 5,814 px over 4 of 921,600), `tools/worldobjects_check` 110 objects OK, and the `hdr` variant (`b451a8a6ca771e79`). (5) The Linux gate (GCC `-Werror`, shaders built on Linux) once per section, as the guide says.

| Stage | What | Evidence particular to it |
| --- | --- | --- |
| a (this) | survey and plan | none |
| b | **Constants infrastructure + water pilot + default flip decision.** `Pipeline::Constants` (ids, a table, the CPU derives every constant; `lib/modes.slang`), a test that every `Mode` is K, C or removed; water 12 -> 1 or 2 sets and its vertex stage; a switch `[Rendering] shader constants` (default off) so the old sets stay buildable while the stage lands; the quiet-machine re-measurement and, if it passes, the flip of `slang shaders` (decision 6) | water frames (calm, rain, sunset, under water, cloudy, the 100,000-unit view) byte-identical; GPU timing on water-heavy poses; how Slang emits the constants (SPIR-V inspected) |
| c | **Terrain.** 43,320 -> 20 fragment, 4 -> 2 vertex; `PipelineTerrainDefines` model rewritten around the map class and the output state; terrain GLSL deleted after the gate | the terrain scene set of stage e (Balmora, Ascadian hillside, rain, ash storm, night, map view at 25,000, local map) byte-identical; point shadow and cloud shadow terrain frames; GPU main pass at Balmora and the hdr profile |
| d | **Object shader**, two steps: d1 plan M (settings-driven defines to constants, content defines stay K; 76 -> about 20 traced classes); d2 plan B (fixed texture bindings with shared dummies written by `Pipeline::Builder` and the NIF material code, fixed varyings, attribute presence as constants) | object scene matrix, the 18 profile sets, particles (smoke, brazier, spell sparks, soft particles, weather occlusion), skinned actors, the merged body (`testmergedbodydefines.cpp`), clusters (`LOD_FADE`), grass (`INSTANCE_TINT`), parallax on the Faultier stand-ins; CPU cost of the larger descriptor set in a crowd (Ebonheart 400-frame wall median); draw-call count unchanged |
| e | **Everything else:** GUI, sky, compute, post, particle stages made from SPIR-V directly; tonemap kept at 16 | the 7-scene matrix; the HUD corner; the `.omwfx` pass with a real mod (not run in 0072) |
| f | **Runtime and tests:** `SlangRuntime::waitFor`, loader and loading-screen behaviour, the "Compiling shaders" string (OpenMW YAML localisation), the warm-up list, `.sets` and `.interface` rewritten and reviewed, the GLSL-pinning tests moved | a cold-cache run with a deliberately unlisted set (a test-only define): the wait on a loader thread and behind a loading screen, no frame over the freeze bar; the flights of 0072 stage h (longest frame) |
| g | **Delete the GLSL** family by family in the order e, c, b, d (safest first): `files/shaders/**/*.glsl|frag|vert|comp` (117 files, 7,062 lines; 107 `.slang` files stay), the GLSL loaders, the `slang shaders` setting, `checkTwinAgainstGlsl` | the full gate, Linux included; a build from a clean folder (stale-build pitfall); no `.glsl` in the install; the editor's shader use (Eden Studio shares the cache folder) |

### Tests that pin GLSL and need toddhow's approval to change

A grep for shader file names in `apps/components_tests`, `openmw_tests` and `opencs_tests` finds **48 files**; I classified them by name and by reading the few I opened, not every line:

1. **Compare the GLSL with Slang or with its own text** (change or retire): `vsg/testslangpermutations.cpp` (`every_permutation_has_the_interface_of_its_glsl_stage`), `vsg/testslanginterfacerecords.cpp` (`records_equal_the_live_glsl_interfaces`, the recorder), `vsg/testshaderpermutation.cpp` (13 tests), `vsg/testslangruntime.cpp` (75, among them the byte-for-byte reproduction of every listed set and the twin-against-GLSL check), `vsg/testpassstages.cpp`, `vsg/testpostpasses.cpp` and `testpostpasses.oldstrings.inc` (the old strings are the switch-off proof of stage g and become fixtures of the final GLSL), `vsg/testshaderdefines.cpp`, `pipeline/testterraindefines.cpp` (imports from the GLSL `#pragma`, and its 43,320 count), `vsg/testomwfxvertex.cpp`.
2. **Read the GLSL text for constants shared with a C++ twin** (mechanical: read the `.slang` of the same name; for example `testaerialmath.cpp` line 221 reads `lib/view/fog.glsl`): `render/testaerialmath`, `testambientcube`, `testambientcubeexterior`, `testaoterrain`, `testcovermath`, `testdepthmath`, `testfogcurve`, `testforceperpixel`, `testimagespace`, `testimagespacepivot`, `testparallaxmath`, `testpointshadowmath`, `testpointshadowshader`, `testprecipitationmath`, `testpuddlemapgpu`, `testpuddlemapshader`, `testpuddlemaptopsshader`, `testpuddletopo`, `testsunglow`, `testtonemapconstants`, `testwatermath`; `terrain/testbatchorigin`, `testbordersharing`, `testchunkcomplement`, `testchunkfadeinstance`, `testterrainfade`; `pipeline/testgbufferblend`, `testinstancetint`; `lod/testalbedosplat`; `openmw_tests/mwrender/testmergedbodydefines.cpp`.
3. **Compile GLSL with glslang** on the GPU or in memory (compare Slang and GLSL results, or only check the GLSL compiles): `render/testskyvisgpu`, `testvolumegpu`, `testcloudvolumegpu`, `testaotemporalpass`, `testtaapass`, `testprobebake`, `vsg/testambientcubecompile.cpp` (`vsg::ShaderCompiler` over `terrain/main.frag`), and `SlangLightGridGpu` (GLSL against Slang on a real device: it becomes a golden-value test, which loses the independent GLSL check).

No existing test is edited until toddhow approves each group (guide rule); in each stage the old test stays green while the GLSL exists and is replaced in the stage that deletes it.

## Decisions for toddhow

1. **Plan M, then B, or M only?** M (settings-driven defines to constants) removes the combinatorial explosion (terrain 43,320 -> 20; object fragment 71,424+ -> about 20 traced classes) but leaves the *content* defines (texture units, varyings, attributes) open-ended, so a mod can still reach an unbuilt class and wait. B (a fixed texture binding set with shared dummy descriptors, fixed varyings) closes it (object fragment about 8, vertex about 2) at the cost of a larger per-material descriptor set (17 writes instead of about 3) and a change in the NIF/material builder. **Recommend M first, measure, then B.**
2. **Mechanism: specialization constants, not uniform branches**, for all control-flow defines in the object and terrain fragment shaders (section 3); uniform branches only for debug views. Agree?
3. **The `OpSpecConstantOp` rule.** The CPU supplies every derived constant (my proposal; no change to the test), or relax the rule to allow `a && b` of constants? I recommend keeping it: drivers may fold `OpSpecConstantOp` less well (RECALLED-VERIFY).
4. **Tonemap stays at 16 compile-time sets** (small, fully prebuilt, whole-screen pass). Agree, or fold them too?
5. **What the main thread does for an unbuilt set during play** (equipment change, spell effect): retry next frame without the part (needs the `BuildQueue` idea for model parts), a bounded block, or "made unreachable by plan B". I recommend the first as the rare case and B as the aim.
6. **Default flip before deletion.** `[Rendering] slang shaders` is still off by default (stage j never flipped it: a first profile pass showed +60 to 80 percent CPU that two later pairs did not reproduce). Stage b's first act would be the quiet-machine re-measurement; if it passes, flip the default (GLSL still present as a switch back) before any deletion. OK?
7. **Runtime compiler fate.** Keep the 33.8 MB Slang library shipped as the safety net (with plan B it is almost never loaded; your instruction says keep), or ship prebuilt SPIR-V only and compile on demand in developer builds? Noted for the size budget.
8. **Golden data.** The `.sets` and `.interface` files shrink and are rewritten once per family (about 6,410 lines of `default/main.frag.interface` to about 1,000). You review them; the old files stay in git history. OK to rewrite instead of keeping per-set records for sets that no longer exist?
9. **Tests.** Approve the three groups of test changes in section 5 per stage; in particular retiring `SlangLightGridGpu`'s GLSL half and the `oldstrings` fixtures.
10. **Loading screen text.** A new user-visible string ("Compiling shaders") needs YAML localisation; it only shows in the rare on-demand case.
11. **Validation.** No Vulkan validation layers are installed here (guide, Pitfalls). Three plan steps (a fragment input the vertex stage never writes; absent vertex attributes; an unwritten sample-mask output) rely on the spec text as I recall it, not on a validator. I propose installing the Vulkan SDK validation layer on the test machine before stage d; it is your machine.

## Not verified / not done in this stage

- Nothing was built or run beyond eight `slangc` compiles (timings above: one machine, one run each except two repeated, shared with other work) and the file and line counts.
- "The view descriptors always hold a valid dummy" and "the object pipeline layout is fixed" are read from `components/view/descriptors.cpp`, `components/pipeline/layout.cpp` and `Graphics::Graphics`, not exercised.
- Slang's handling of specialization-constant branches (dead-code removal in the SPIR-V, whether nested integer compares emit `OpSpecConstantOp`) is unmeasured; stage b's pilot measures it on the water shader first.
- Driver folding of specialization constants on AMD and Intel, the effect on register pressure, and pipeline creation time are unmeasured (only the one NVIDIA GPU is available).
- The plan B ubershader size (250 to 350 KB, 1.5 s) is an estimate; the one 40-define union I compiled is not a valid combination.
- "20 distinct sets after dropping control defines" comes from the 76 *traced* sets, not from the content space; real content (mods) may add classes under plan M.
- The test classification in section 5 is by file name plus the few files I opened.
- Linux gate, clang-format, Lua tests: not applicable (docs only).
