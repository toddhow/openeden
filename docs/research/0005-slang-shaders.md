# 0005 — Slang as OpenEden's shader compiler and language

Date: 2026-10-29 · Asked by: toddhow · Status: research, nothing decided. Feeds task 0012 (Track V stage V4); a switch needs a new decision record approved by toddhow (draft in section 9).

The question: can OpenEden replace glslang with Slang while keeping vsgopenmw's GLSL shaders (Slang accepts GLSL input), and later write new shaders in Slang?

## 1. Summary

- **Slang is not a drop-in replacement for glslang today.** Slang v2026.19 compiles 142 of the 3,189 shader variants that glslang 14.3.0 compiles from vsgopenmw's shaders. It fails all variants of the main object fragment shader and 705 of 801 variants of the main object vertex shader.
- **The failures have three causes, all in Slang's GLSL front end:**
  1. It does not know Vulkan GLSL's separate `texture2DArray` and `sampler` types.
  2. It does not accept the `.st`/`.pq` swizzles.
  3. It aborts with an internal error on interface arrays sized by a specialization constant (`vec2 texCoord[numUvSets]`).

  The first two take a few lines of source change in `lib/view/shadow.glsl` and four call sites. The third is a design limit, not a bug fix away: Slang gives every interface member its own location when it compiles, and cannot do that for an array whose size is only known at pipeline creation. Working around it changes vertex input locations, so the C++ in `components/pipeline/graphics.cpp` has to change too.
- **Once those three are worked around in scratch copies, Slang compiles all 3,189 variants**, and every output passes `spirv-val`. Every descriptor set, binding, descriptor type, buffer byte layout and push constant block matches glslang's, and so does every interface location Slang keeps. It removes unused resources, spec constants and interface variables, which is harmless here because vsgopenmw writes its pipeline layouts by hand.
- **glslang's current output is not clean either.** 1,557 of the 3,189 variants glslang compiles today fail `spirv-val`: every variant that includes the shadow code (`Expected Image to have the same type as Result Type Image`). Drivers accept it, but it is worth fixing whichever compiler stays.
- **Writing new shaders in Slang works with VSG.** A port of the sun shader to the Slang language compiles to SPIR-V 1.0 with the same sets, bindings, byte layouts, push constants and locations as the GLSL version, and passes `spirv-val`. VSG accepts precompiled SPIR-V unchanged: a `vsg::ShaderModule` whose `code` is filled is never sent to glslang, and VSG reads `.spv` files.
- **Recommendation:**
  - Do not swap the compiler as part of V2 or V3.
  - In task 0012, write new shaders (starting with whatever replaces `.omwfx` post-processing) in Slang, compiled at build time to SPIR-V (option A in section 6).
  - Port vsgopenmw's shaders only when V4 touches them anyway, after the spec-constant array and generated-settings designs change (section 7).
  - Retire glslang only when no shader is compiled at runtime, or decide then to embed Slang for runtime variants (option B, about 27–33 MB).

## 2. How shaders are compiled today

Measured on `vulkan` at `4d87e04d60`.

- **Which files:** `files/shaders/CMakeLists.txt` installs only vsgopenmw's shaders: 24 stages (`default`, `terrain`, `water`, `gui`, six `sky/*` passes, four `comp/*` compute shaders) plus their `lib/` includes. `compatibility/` and `core/` are OpenMW's OpenGL shaders left over from the merge; nothing installs or loads them.
- **Loading:** `components/resource/resourcesystem.cpp` builds shader options with two readers: `Resource::ShaderSettings` (virtual files) and `vsg::glsl`. `vsgUtil::readShader` (`components/vsgutil/readshader.cpp`) calls `vsg::read_cast<vsg::ShaderStage>`.
- **Includes:** `vsg::glsl::createShader` runs `vsg::insertIncludes`, which pastes each `#include` in recursively, so the compiler only ever sees one flattened string. It searches the including file's directory first, then the parents', then the shader root.
- **Generated settings includes:** `components/resource/shadersettings.cpp` writes four virtual files from settings and content fallbacks, as `const` declarations:
  - `light.settings`: `LightAttenuation_*` from the game's fallback values.
  - `shadow.settings`: shadows on or off, PCSS, shadow map count, sample counts, distances, `DEBUG_SHADOW`.
  - `material.settings`: `cClampLighting`.
  - `terrain.settings`: `DEBUG_CHUNKS`.

  Their values are baked into the shader text, so shader binaries depend on the user's settings and on the loaded content's fallback values.
- **Defines:** `Pipeline::Graphics::createShaderStage` (`components/pipeline/graphics.cpp`) puts one define per active `Pipeline::Mode` into `ShaderCompileSettings::defines`. VSG's `ShaderCompiler::combineSourceAndDefines` then writes `#define X` only for names listed in the shader's own `#pragma import_defines (...)`.
  - `default/main.vert` imports 10 defines and `default/main.frag` imports 12, so up to 1,024 and 4,096 combinations; 801 and 2,329 of them compile.
  - The sky shaders import 1 to 5 each.
- **Specialization constants:**
  - Object shaders: `numUvSets` (id 0), `alphaTestMode` (1), `colorMode` (2), `specular` (3), and per-texture UV set ids from 4 + binding.
  - Particle compute: six booleans (ids 0–5).
  - They are set in `Graphics::create` and `Pipeline::Particle`. `numUvSets` also sizes interface arrays (`lib/inout_texcoord.glsl`, `lib/attributes.glsl`).
- **Compiler settings:** VSG 1.1.16's `ShaderCompiler` with defaults:
  - Vulkan 1.0 client, SPIR-V 1.0.
  - `defaultVersion` 450, because the shaders have no `#version`.
  - No optimizer: glslang is built with `ENABLE_OPT=OFF`.

  The engine creates a Vulkan 1.0 instance (`WindowTraits::vulkanVersion` default; `apps/openmw/vsgengine.cpp`).
- **Pipeline layouts are written by hand** in C++ (`Graphics::Graphics`, `getCompatibleDescriptorSetLayouts`, the compute pipelines), not taken from SPIR-V reflection. A compiler swap is safe as long as every resource a shader uses has the same set, binding and type, push constants keep the same offsets, and spec constant ids are unchanged.
- **Shared headers:** `comp/fogofwar/constants.glsl` and the `workgroupsize.glsl` files are also `#include`d by C++ (`apps/openmw/mwrender/map.cpp`, `worldmap.cpp`). Any Slang port has to keep them valid in both languages.

## 3. Method

All work ran in a scratch folder outside the repository (`C:\Users\smith\AppData\Local\Temp\oes`). The harness scripts are copied to the session notes folder, `slang-spike-scripts/`. No file in the repository was changed apart from this note and the proposal in `docs/dependencies.md`.

1. **Tools.**
   - Slang **v2026.19** (released 2026-09-29; the newest release, v2026.20, was 12 days old):
     - archive `https://github.com/shader-slang/slang/releases/download/v2026.19/slang-2026.19-windows-x86_64.zip`
     - SHA-256 `fc922f214b5d115f663932e59e80e0f6b6cc6fae140979cf307da6459b6f8f33`, matching GitHub's published digest
     - licence `Apache-2.0 WITH LLVM-exception`, read from the archive's `LICENSE`
   - glslang **14.3.0** built from the same source the game fetches (`extern/CMakeLists.txt`). It was built with its binaries and SPIRV-Tools at the commits glslang 14.3.0 pins in `known_good.json`:
     - SPIRV-Tools `0cfe9e7219148716dfd30b37f4d21753f098707a`
     - SPIRV-Headers `2acb319af38d43be3ea76bfabf3998e5281d8d12`

     That gives `glslang.exe` (glslangValidator), `spirv-val` and `spirv-dis` without the LunarG SDK.
2. **Inputs exactly as the game builds them.** `prep.ts` re-implements `vsg::insertIncludes`, `ShaderSettings` (default settings from `files/settings-default.cfg`, light fallbacks from `files/openmw.cfg`, floats formatted as `std::ostream` formats them) and `combineSourceAndDefines`. It writes one flattened file per variant, with `#version 450` prepended to stand in for glslang's default version.
   - Variants: every subset of each stage's `import_defines` (5,246 files), plus one per stage with every define and the other settings (shadows on, `DEBUG_SHADOW`, `DEBUG_CHUNKS`). That is 5,270 in total.
   - Many define combinations never occur in the game (for example `DIFFUSE_MAP` without `TEXCOORD`) and fail in both compilers, so the comparison is over the 3,189 variants glslang accepts.
3. **Compile.**
   - glslang, as VSG calls it:
     ```
     glslang -V --target-env vulkan1.0 -S <vert|frag|comp> -o out.spv in.<ext>
     ```
   - Slang:
     ```
     slangc in.<ext> -lang glsl -stage <vertex|fragment|compute> -entry main -target spirv -profile spirv_1_0 -fvk-use-entrypoint-name -o out.spv
     ```
     Slang's default target is SPIR-V 1.5, which needs Vulkan 1.2. `-profile spirv_1_0` is honoured with `warning[E50011]: SPIR-V version too old`. Every output was checked with `spirv-val --target-env vulkan1.0`.
4. **Compare.** `reflect.ts` reads, from each SPIR-V binary:
   - entry points and execution modes;
   - capabilities;
   - each descriptor's set, binding, type, count and NonWritable flag, with the full block layout (member offsets, array and matrix strides);
   - push constant blocks;
   - spec constant ids, types and defaults;
   - input and output locations and types;
   - built-ins.

   `compare.ts` diffs the two compilers' results variant by variant. `leaves.ts` checks that buffer blocks differing as text have the same byte layout, and `locations.ts` expands I/O blocks into per-location entries.

## 4. Results

### 4.1 Per stage

Columns:
- **variants:** files generated.
- **glslang:** variants that compile.
- **glslang valid:** of those, how many pass `spirv-val`.
- **Slang as-is:** compiled unchanged.
- **Slang + edits:** after the shadow-sampler and swizzle edits (section 4.2, items 1 and 2).
- **Slang + edits + fixed UV arrays:** also with `[numUvSets]` replaced by `[4]`, an experiment that is not layout compatible (item 3).
- **valid:** of the last column, how many pass `spirv-val`.

| Stage | variants | glslang | glslang valid | Slang as-is | Slang + edits | Slang + edits + fixed UV arrays | valid |
|---|---|---|---|---|---|---|---|
| comp/fogofwar/update.comp | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| comp/light/assign.comp | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| comp/particle/simulate.comp | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| comp/worldoverlay/update.comp | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| default/main.frag | 4097 | 2329 | 776 | **0** | 24 | 2329 | 2329 |
| default/main.vert | 1025 | 801 | 801 | **96** | 96 | 801 | 801 |
| gui/gui.frag | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| gui/gui.vert | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| sky/atmosphere/main.frag | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| sky/atmosphere/main.vert | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| sky/clouds/main.frag | 17 | 3 | 3 | 3 | 3 | 3 | 3 |
| sky/clouds/main.vert | 33 | 5 | 5 | **0** | 0 | 5 | 5 |
| sky/moon/main.frag | 9 | 2 | 2 | 2 | 2 | 2 | 2 |
| sky/moon/main.vert | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| sky/night/main.frag | 17 | 7 | 7 | 7 | 7 | 7 | 7 |
| sky/night/main.vert | 33 | 8 | 8 | **4** | 4 | 8 | 8 |
| sky/sun/main.frag | 5 | 2 | 2 | 2 | 2 | 2 | 2 |
| sky/sun/main.vert | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| sky/sunglare/main.frag | 3 | 2 | 2 | 2 | 2 | 2 | 2 |
| sky/sunglare/main.vert | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| terrain/main.frag | 2 | 2 | **0** | **0** | 2 | 2 | 2 |
| terrain/main.vert | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| water/main.frag | 3 | 2 | **0** | **0** | 2 | 2 | 2 |
| water/main.vert | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| **Total** | 5270 | 3189 | 1632 | 142 | 170 | 3189 | 3189 |

The last column compiles exactly the same 3,189 variants as glslang, none more and none fewer. Slang rejects nothing glslang accepts once the three causes are worked around.

### 4.2 Why Slang fails

Minimal repros are in `repro/` in the scratch folder; each fails or passes on its own as described.

1. **Separate image and sampler objects** (`lib/view/shadow.glsl`, included by the default, terrain and water fragment shaders):
   ```
   error[E30015]: undefined identifier
    97 | layout(set=VIEW_SET, binding=VIEW_SHADOW_MAP_BINDING) uniform texture2DArray shadowMap;
       |                                                               ^^^^^^^^^^^^^^ undefined identifier 'texture2DArray'.
   error[E30015]: undefined identifier
    98 | layout(set=VIEW_SET, binding=VIEW_SHADOW_SAMPLER_BINDING) uniform sampler shadowSampler;
       |                                                                   ^^^^^^^ undefined identifier 'sampler'.
   ```
   Slang's GLSL mode lacks the `GL_KHR_vulkan_glsl` separate texture and sampler types and their constructors (`sampler2DArrayShadow(tex, smp)`).
   - **Workaround:** Slang's own types are accepted inside a GLSL-mode file and keep the same two descriptors, `SAMPLED_IMAGE` at binding 6 and `SAMPLER` at binding 7 (`repro/separate_slang.frag`):
     - `uniform Texture2DArray shadowMap;`
     - `uniform SamplerComparisonState shadowSampler;`
     - `shadowMap.SampleCmp(shadowSampler, vec3(uv, cascade), depth)`
   - The PCSS blocker search and the shadow debug view sample the same texture without comparison (`texture(sampler2DArray(shadowMap, shadowSampler), …)`). In Slang that needs a `SamplerState`; declaring it on the same binding works and is legal in Vulkan.
   - Those reads currently go through a comparison-enabled sampler. That is a latent Vulkan issue (non-comparison sampling through a sampler with `compareEnable` is undefined) independent of Slang.
2. **`.st` swizzles** (shadow code and `default/main.frag`): `error[E30027]: 'st' is not a member of 'vector<float,4>'.` Slang accepts only `xyzw` and `rgba`. Workaround: `.st` → `.xy`, `.pq` → `.zw`.
3. **Interface arrays sized by a specialization constant** (`vec2 texCoord[numUvSets]` in `lib/inout_texcoord.glsl` and `lib/attributes.glsl`):
   ```
   error[E99997]: Slang compilation aborted due to an exception of class Slang::InternalError unexpected: needed a known integer value
   ```
   - It reproduces with a six-line shader (`repro/specarray_out.vert`). A spec-constant-sized local array compiles (`repro/specarray_local.vert`).
   - **Why it is hard:** glslang emits the I/O block with a single `Location` on the block, and the driver assigns member locations after specialization. Slang flattens blocks into separate variables, each with an explicit `Location`, which cannot depend on a spec constant.
   - Replacing the size with a fixed maximum compiles (last column above) but moves every later location. Vertex inputs 6–8 then have no matching attribute from `Graphics::create`, which binds exactly `numUvSets` texture-coordinate attributes. So this needs a design change (section 7), not a compiler flag.

Both compilers accept everything else in the shaders:
- `layout(set=CONST, binding=CONST)` with `const int` names;
- `constant_id = START_UVSET_CONSTANTS + DIFFUSE_UNIT` expressions (`repro/constlayout.frag` gives `set2.b7` and spec id 11 in both);
- `std140`/`std430` blocks, push constants, storage images with formats, atomics, `shared`, `gl_VertexIndex`, `discard`.

Slang warns about `#pragma import_defines` (`warning[E15601]: unknown pragma ignored`) and otherwise ignores it.

### 4.3 Does the SPIR-V match? (drop-in safety)

On the 142 variants that compile unchanged, and on all 3,189 patched variants, every difference falls into one of these groups, none of which changes a pipeline layout:

| Difference | Seen in | Effect on VSG pipelines |
|---|---|---|
| Buffer blocks written differently. Slang wraps arrays inside a uniform block in a one-member struct, glslang nests the block in an anonymous struct. | 13 stages | None. All 5,530 such differences have identical byte layouts (offset and type of every scalar, vector and matrix column), checked by `leaves.ts`. |
| Image `Depth` operand 0 ("not depth") vs 2 ("unknown") | 12 stages | None for descriptors. The same descriptor type, and Slang's choice is what makes its shadow lookups valid. |
| Slang adds `NonWritable` on read-only storage buffers | 4 stages | None; more precise. |
| Slang removes unused descriptors, spec constants, vertex inputs and interface variables | many | None. Layouts are hand-written in C++, Vulkan ignores specialization entries for ids a module lacks, and unused vertex attributes are allowed. A vertex output Slang removes because nothing writes it gives the fragment shader the same undefined value glslang's declared-but-unwritten output gives. Worth one validation-layer run (`VSGOPENMW_VALIDATION=1`). |
| I/O blocks split into one variable per member; `gl_PerVertex` emitted as a loose `Position` built-in | all graphics stages | None. Every location Slang keeps has the same number and type as glslang's block member (`locations.ts`: no mismatches on the 142 unpatched variants). |
| Push constants | none | Identical in every variant. |
| Matrices: glslang `ColMajor` with matrix×vector; Slang `RowMajor` with vector×matrix | all | The same arithmetic: Slang reads the same memory as the transpose and multiplies on the other side. |

Not covered by reflection: whether the arithmetic Slang generates gives the same pixels. That needs one in-game run of `tools/render_compare` with a Slang build, which this spike did not do. V2 is still changing the renderer in another session.

### 4.4 glslang's own output fails validation

`spirv-val` from the SPIRV-Tools commit glslang 14.3.0 pins rejects every variant that includes `lib/view/shadow.glsl`:

```
error: line 561: Expected Image to have the same type as Result Type Image
  %117 = OpSampledImage %116 %110 %114
```

`sampler2DArrayShadow(texture2DArray, sampler)` creates a sampled-image type with `Depth=1` from an image declared with `Depth=0`. The game works because drivers are lenient. Possible fixes (none tested here):
- a newer glslang;
- a combined `sampler2DArrayShadow` binding, which is a layout change;
- the Slang declarations in 4.2, item 1.

### 4.5 New shaders in the Slang language

`slangport/sun.slang` reimplements `sky/sun/main.vert` and `main.frag` in Slang syntax, both entry points in one file, using `[[vk::binding(b, set)]]`, `[[vk::push_constant]]`, `[vk::constant_id(n)]` and `[[vk::location(n)]]`. Compiled with:

```
slangc sun.slang -entry vertexMain|fragmentMain -stage vertex|fragment -target spirv -profile spirv_1_0 -o x.spv
```

Both stages pass `spirv-val`, and the reflection matches the GLSL stages: `set0.b0` scene, `set2.b0` diffuse map, `set2.b1` material with identical byte layouts, the same push constant block, and `texCoord` at location 0. Without `-fvk-use-entrypoint-name`, Slang names the entry point `main`, which is VSG's default `ShaderStage::entryPointName`.

Slang's own preprocessor also handles the repository's include layout directly. `slangc files/shaders/comp/light/assign.comp -lang glsl … -I files/shaders -I <dir with generated *.settings>` gives the same reflection as the flattened input. So an offline build step does not need VSG's include code.

## 5. Cost measurements

- **Compile time per process**, five runs each, uncontended, Windows 11, MSVC Release glslang. This is mostly process start; Slang loads its 25 MB compiler and its core module on each start.

  | Input | glslang 14.3.0 | slangc 2026.19 |
  |---|---|---|
  | `default/main.frag`, all defines | 81–88 ms | 203–211 ms |
  | `comp/light/assign.comp` | 79–86 ms | 177–188 ms |

  In-process cost, which is what option B would pay, was not measured. It needs a small C++ harness around `slang::IGlobalSession`, where the session start is paid once.
- **SPIR-V size:** Slang's output is smaller (17 KB vs 25 KB for the all-defines object fragment shader) because it removes unused code. Unoptimized (`-O0`) it is still smaller (18.8 KB).
- **Runtime binary size for option B:**
  - Windows minimum: `slang.dll` 0.16 MB + `slang-compiler.dll` 25.9 MB + `slang-glsl-module.dll` 1.5 MB, about 27 MB. That set compiles GLSL to SPIR-V with `-O0`.
  - The default optimization level also needs `slang-glslang.dll` (6.2 MB, which carries `spirv-opt`): `error[E00100]: failed to load downstream compiler 'spirv-opt'` without it. About 33 MB in total.
  - Not needed: `slang-llvm.dll` (84 MB, CPU targets) and the 31 MB `slang-standard-module-*` folder (neural and workgraph modules).
  - For comparison, glslang is linked statically; the stand-alone `glslang.exe`, including SPIRV-Tools' optimizer, is 4.3 MB.
- **Linux:** Slang publishes prebuilt `linux-x86_64` archives (glibc 2.27 and 2.28 builds, about 29 MB `.tar.gz`; SHA-256 recorded in section 8) that should run on Ubuntu 24.04 (glibc 2.39). Not tested in the CI container in this spike. Ubuntu 24.04 has no `slang` package, so CI would download the pinned archive as it fetches VSG. Building Slang from source is possible but much heavier than glslang.

## 6. Integration options with VSG

**A. Offline: compile at build time and ship SPIR-V.**
- **How:** a CMake step runs the pinned `slangc` on the host for each shader and variant, and installs the `.spv` files under `resources/shaders`. At runtime, VSG reads them with its `vsg::spirv` reader, or the engine fills `ShaderModule::code`. VSG's `ShaderCompiler::apply` skips any module that already has code.
- **For:** no compiler in the game, so no runtime cost and no extra DLLs; shader errors fail the build, not the game; the same SPIR-V on every machine.
- **Against:**
  - Every variant has to be known at build time. vsgopenmw makes variants at runtime from the mode defines (up to 801 vertex and 2,329 fragment variants for the object shader alone) and bakes the user's settings and the content's fallback values into the source (`*.settings`).
  - Before shaders can move offline, the settings must become specialization constants or uniform data, and the mode defines must either be enumerated at build time or become spec constants.
- **Cost:** a host tool download in CMake for Windows and Linux, about 90 MB of archive at build time only; build time grows with the number of variants (about 0.2 s each as a process; batching entry points per `slangc` run would reduce it).

**B. Embedded: link the Slang compiler library and compile at runtime.**
- **How:** add a `vsg::ShaderCompiler` subclass, or a hook in `Pipeline::Graphics::createShaderStage`, that calls Slang's C++ API with the same flattened source and defines, and fills `ShaderModule::code`. VSG's own compiler stays for anything not routed through it.
- **For:** keeps vsgopenmw's runtime variant scheme unchanged; modders' shaders could be compiled at load time, relevant to task 0012's `.omwfx` question.
- **Against:**
  - About 27–33 MB of extra DLLs on every platform.
  - In-process compile time not yet measured.
  - Slang has no OpenEden-side cache, and VSG has no SPIR-V cache, so startup and first-sight hitches depend on compile speed.
  - It needs the source fixes in 4.2 to compile the existing shaders at all.
- **Cost:** a new runtime dependency (pinned prebuilt archive, as in A) and packaging on Windows and Linux.

**Mixed (recommended path):** keep glslang for vsgopenmw's runtime GLSL while those shaders depend on runtime variants. Write new shaders in Slang and compile them offline (A).

## 7. Recommended migration path

1. **Now (V2, V3): no change.** glslang stays; the compiler question does not block V2.
2. **Independent fix, any time:** make `lib/view/shadow.glsl` produce valid SPIR-V (section 4.4). Check the comparison-sampler use in the PCSS and debug reads (4.2, item 1).
3. **Task 0012 (V4): Slang for new shaders.**
   - Add a pinned Slang host tool to the build (option A) once toddhow approves it as a dependency.
   - Write new shaders in the Slang language, starting with whatever post-processing replaces `.omwfx` (task 0012 open question 1). New shaders take settings as spec constants or uniforms, not generated `#include`s, so they can be compiled offline.
   - Keep shared C++/shader headers (`workgroupsize.glsl`, `constants.glsl`) valid in both languages, or replace them with generated headers.
4. **When V4 reworks an existing vsgopenmw shader, port it to Slang**, which removes the three blockers:
   - the separate sampler and swizzle edits are mechanical;
   - the texture-coordinate arrays need a design change, for example a fixed maximum with C++ always binding that many attributes, a per-count define, or reading UVs from a storage buffer by `gl_VertexIndex`;
   - the `*.settings` constants become spec constants.

   Compare each ported stage with `tools/render_compare` and a validation-layer run.
5. **Retire glslang** when no runtime GLSL remains: either all shaders are precompiled, or toddhow chooses option B for mod shaders or runtime variants. Only then can VSG be built with `VSG_SUPPORTS_ShaderCompiler=OFF` and the glslang fetch be removed.

**Reasons not to switch at all**, for toddhow to weigh:
- glslang works for every shader today.
- Slang's GLSL support is incomplete in exactly the places vsgopenmw relies on.
- Slang adds 27–33 MB if embedded, or a 90 MB host-tool download to every build.
- Slang releases about every two weeks, so pins need refreshing.

What Slang buys in return:
- a modern language with modules, generics and interfaces for the new shaders V4 and later stages need (post-processing, PBR or ray tracing after Track V);
- valid SPIR-V on the shadow path;
- output for other targets (HLSL, Metal, WGSL) if OpenEden ever needs them, though decision 0002 rules out other backends today.

## 8. Dependency facts (for `docs/dependencies.md` if adopted)

- **Version:** Slang `v2026.19`, released 2026-09-29: https://github.com/shader-slang/slang/releases/tag/v2026.19
- **Windows x86_64:** `slang-2026.19-windows-x86_64.zip`, 63,221,930 bytes
  - SHA-256 `fc922f214b5d115f663932e59e80e0f6b6cc6fae140979cf307da6459b6f8f33`
  - SHA-512 `cdd3329697a7307e0e46a03c45f279af9dd437cf842829f7dee579408959bd821982d9a486a4b01b865b19e67292c378ee962d340244c3062a3915b4f2cb967d`
- **Linux x86_64:** `slang-2026.19-linux-x86_64-glibc-2.28.tar.gz`, 29,164,768 bytes, SHA-256 `bd7afcb0956dee9304f44ed115e9aaa7140704271b5c3294bcb237e3ae67adc7` (GitHub's published digest; not downloaded in this spike).
- **Licence:** `Apache-2.0 WITH LLVM-exception`, compatible with GPL-3.0-or-later. Bundled third-party notices: glslang, SPIRV-Tools, lz4, mimalloc, miniz, cmark, unordered_dense, all permissive.
- **Not yet decided:** host tool only (A) or runtime library (B).

## 9. Draft decision record (for toddhow; not adopted)

> **0004 — New shaders are written in Slang; glslang stays for vsgopenmw's GLSL until it is ported**
>
> Date: (when decided) · Decided by: toddhow
>
> **Decision.**
> - From Track V stage V4 (task 0012), new shaders are written in Slang and compiled to SPIR-V 1.0 at build time by a pinned `slangc` (initially v2026.19), loaded through VSG as precompiled SPIR-V.
> - vsgopenmw's existing GLSL shaders stay on glslang (runtime compilation through `vsg::ShaderCompiler`) until each is ported. A port fixes the three incompatibilities found in research note 0005 (separate sampler objects, `.st` swizzles, spec-constant-sized interface arrays) and moves generated settings to specialization constants.
> - glslang is removed when no shader is compiled at runtime, or this decision is revisited if runtime compilation of mod shaders needs Slang embedded in the game.
>
> **Why.** Slang is not a drop-in replacement (research 0005: 142 of 3,189 variants compile unchanged). Once the incompatibilities are handled it produces SPIR-V with identical pipeline interfaces. It is a better language for the new shader work V4 and later stages need, and offline compilation keeps the game free of a 27–33 MB runtime compiler.
>
> **Consequences.**
> - The build fetches a pinned Slang release on Windows and Linux.
> - Two shader languages coexist during Track V.
> - Each port is checked with `tools/render_compare` and validation layers.
> - New shaders take settings as specialization constants, not generated includes.
> - Slang's release cadence (about every two weeks) means pins are refreshed deliberately, not automatically.

## 10. Not done in this spike

- No runtime test. Nothing was rendered with Slang SPIR-V; that needs a build with a Slang path and a `tools/render_compare` run, after V2 settles.
- No in-process compile timings (option B).
- No Linux run of `slangc`.
- No check of which define combinations the game actually requests. The comparison covers every combination glslang accepts, which includes all the requested ones.

## Sources

- Slang releases and documentation: https://github.com/shader-slang/slang, https://shader-slang.org/slang/user-guide/
- VSG 1.1.16 source: `src/vsg/utils/ShaderCompiler.cpp` (`compile`, `combineSourceAndDefines`), `src/vsg/state/ShaderModule.cpp` (`insertIncludes`), `src/vsg/io/glsl.cpp`, `src/vsg/io/spirv.cpp`
- OpenEden `vulkan` at `4d87e04d60`: `files/shaders/`, `components/resource/shadersettings.cpp`, `components/resource/resourcesystem.cpp`, `components/pipeline/graphics.cpp`, `components/pipeline/mode.hpp`, `extern/CMakeLists.txt`
- Harness and logs: `C:\Users\smith\AppData\Local\Temp\oes` (`results.json` and `compare.json` per run); scripts copied to the session notes folder (`slang-spike-scripts/`)
