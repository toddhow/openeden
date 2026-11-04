# 0035 — Porting the built-in shaders to Slang: inventory, permutations and the architecture

Date: 2026-11-04 · For task 0072 (`docs/tasks/0072-port-built-in-shaders-to-slang.md`) · Builds on research note 0005 and decision 0009

toddhow asked on 2026-11-04 to start porting the shaders to Slang. Decision 0009 says new shaders are Slang and the existing GLSL is ported later; this note gives the facts the port needs. Measured on Windows (RTX 4070, i7-13700K), `build-jolt`, Slang `v2026.19`, with a copy of toddhow's config (vanilla Morrowind, Tribunal, Bloodmoon). Raw traces and frames: `I:\Projects\OpenEden-work\slangport` (not committed).

## 1. Inventory

**Shader files** (`files/shaders`, counted by `inventory.py` in the session scratchpad: an entry is a `.vert`, `.frag` or `.comp` file; includes are followed transitively):

| Family | Entry stages | Lines with includes | `#if` names tested | Loaded by |
| --- | --- | --- | --- | --- |
| `default` (all objects) | `main.vert`, `main.frag` | 684, 3,070 | 17, 61 | `Pipeline::Graphics::createShaderStage` (Mode defines) |
| `terrain` | `main.vert`, `main.frag` | 397, 2,535 | 6, 31 | `Pipeline::terrain` (`components/pipeline/terrain.cpp`, its own defines) |
| `water` | `main.vert`, `main.frag` | 253, 1,658 | 11, 28 | `Graphics` with `BUMP_MAP` (`apps/openmw/mwrender/water.cpp`) |
| `sky/atmosphere`, `clouds`, `moon`, `night`, `sun`, `sunglare` | 2 each (12) | 8 to 411 | 0 to 16 | `Graphics` with Mode defines (`mwrender/weather.cpp`) |
| `gui` | `gui.vert`, `gui.frag` | 15, 11 | 0 | `vsgAdapters::mygui::Render::registerShader` |
| `comp/fogofwar`, `light`, `particle`, `worldoverlay` | 1 each (4) | 45 to 348 | 1 to 4 | `vsgUtil::readShader` in `components/pipeline/{fogofwar,lightgrid,particle,worldoverlay}.cpp`; the particle pass uses six specialization constants |

- **Totals:** 24 entry stages, 77 `.glsl` include files, 72 distinct names in `#if`/`#ifdef` lines (the Mode defines plus include guards and constants). All 77 includes are used by some entry.
- **GLSL inside C++ (not under `files/shaders`):** `components/render/tonemap.cpp`, `bloom.cpp`, `velocity.cpp`, `ambientocclusion.cpp`, `apps/openmw/mwrender/postprocess.cpp` (three stages), and `components/fx/vulkanpass.cpp` (the header of every `.omwfx` pass). These are compiled at run time with VSG's glslang too.
- **Already Slang:** `sky/sun/sun.slang` (the sun's twin, task 0023), `sky/sun/proceduralsun.slang`, `comp/volume/volume.slang`, `comp/cloud/cloud.slang` (no GLSL pair).

## 2. Permutations the game creates

`OPENEDEN_SHADER_TRACE=<file>` (new in task 0072, `vsgUtil::recordShaderUse`) writes each distinct (stage, define set) pair the first time a run creates it, from `Graphics::createShaderStage` and `Pipeline::terrain`. Scenes: Balmora, Seyda Neen, Caldera Mages Guild and Caius Cosades' house (Vivec Arena Canton timed out at 90 s on the first run and was replaced).

| Run | Distinct pairs |
| --- | --- |
| Default settings (3 scenes) | 33 |
| Every major `[Rendering]` feature on, shadows, 4× MSAA with alpha to coverage, distant terrain (4 scenes) | 39 |
| Union | **55** requested pairs; **48 programs** once each set is the effective one (the names the stage imports, task 0072 stage b): `default/main.frag` 18, `default/main.vert` 12, terrain 2 + 2, water 1 + 1, each sky stage 1 |

- **Theoretical count:** note 0005 found up to 801 vertex and 2,329 fragment variants for the object shader alone. The real set is small because vanilla content uses few Mode combinations, but mods add more (normal, specular and height maps, parallax), so a list recorded from runs is never complete for the object shader.
- **Which defines come from where:** content decides `NORMAL`, `COLOR`, `TEXCOORD`, `SKIN`, `MORPH`, `PARTICLE`, the texture maps and `MATERIAL`; settings decide `GBUFFER*`, `PBR*`, `GI*`, `WETNESS*`, `COVER*`, `PARALLAX*`, `ALPHA_TO_COVERAGE`. The terrain's sets come from settings only, so they can be enumerated completely; the sky's, water's and GUI's are fixed or nearly so.

## 3. Compile cost

- **glslang in the engine:** the first compile of each pair, timed by compiling a copy through `vsg::ShaderCompiler` (`OPENEDEN_SHADER_TRACE_TIMING=1`): about 57 ms for the small sky stages (most of it per-call setup, since an 8-line stage costs the same), 10 to 70 ms for the object shader, **1.85 s** in total over the 55 pairs, an upper bound: each timed compile is a copy compiled beside the engine's own compile threads and includes per-call setup (stage b review). VSG has no SPIR-V cache, so the compiles repeat every start.
- **slangc as a process:** 160 ms for the GUI fragment stage, 210 ms for the 619-line cloud march, per stage and define set; mostly start-up and loading Slang's core module. In-process compile time through the Slang API is **not measured**.

## 4. Architecture options

| Option | What it means | For | Against |
| --- | --- | --- | --- |
| A. Build-time permutations | `slangc` compiles each listed define set at build time; the game picks the file whose set matches exactly (built in stage a: `openeden_add_slang_permutations`, `vsgUtil::findSlangPermutation`) | No compiler in the game; errors fail the build; no start-up compile; same SPIR-V everywhere | Every set must be known at build time; the object shader's sets are open-ended (mods) |
| B. Runtime Slang library | Link `slang.dll`/`libslang.so` and compile on demand with the same defines, with an on-disk SPIR-V cache keyed by source hash, define set, compiler version and flags | Keeps the open-ended define scheme; can retire glslang | Ships 25.9 MB (`slang-compiler.dll`) plus 0.16 MB (`slang.dll`), probably 1.5 MB (`slang-glsl-module.dll`) and 32.2 MB (the `slang-standard-module-2026.19` folder), and possibly 6.2 MB (`slang-glslang.dll`); which of these SPIR-V output needs at run time is not tested; in-process speed unmeasured; contradicts decision 0009's "the game never runs Slang" |
| C. Specialization constants | Turn defines into `[SpecializationConstant]` booleans | One module per stage | Only for defines that do not change the interface: `TEXCOORD`, `SKIN`, `GBUFFER*` add inputs, outputs and bindings, which must be static in SPIR-V |
| D. Hybrid (recommended) | A for every family whose sets are finite (GUI, sky, compute, water, terrain: sets enumerated from the settings); for the object shader A with the recorded sets and the GLSL as the fallback while it exists; decide B (with a cache) before the object shader's GLSL can be deleted | Moves most of the code to Slang now without a runtime dependency; keeps every frame correct (an unlisted set draws with the GLSL) | Two compilers coexist until the decision on B |

- **Licence (B and the existing build tool):** `Apache-2.0 WITH LLVM-exception`, read in the `LICENSE` file of the pinned release archive (`build-jolt/extern/fetched/slang/LICENSE`); Apache-2.0 code may be combined with GPL-3.0-or-later. Bundled notices: glslang, SPIRV-Tools, lz4, mimalloc, miniz, cmark, unordered_dense. `docs/dependencies.md` said "proposed, not adopted" until task 0072 stage b corrected it.
- **Sizes in the release:** `slang-compiler.dll` 25.9 MB, `slang-glslang.dll` 6.2 MB, `slang-glsl-module.dll` 1.5 MB, the `slang-standard-module-2026.19` folder 32.2 MB, `slang-llvm.dll` 84.4 MB (CPU targets; not needed for SPIR-V, unverified), `slang.dll` 0.16 MB.

## 5. Verification method

1. **Interface:** for every built permutation, the Slang SPIR-V must declare exactly what the GLSL stage declares when compiled with the same defines: entry point, descriptor sets and bindings with the byte layout of every block, push constants, input and output locations, built-ins, specialization constants. `vsgUtil::readSpirvInterface` (our own SPIR-V reader, no new dependency; taken from the sun test of task 0023 and extended) and the test `SlangPermutations.every_permutation_has_the_interface_of_its_glsl_stage`, which finds every `.permutations` file the build wrote, so a new family is checked without editing the test. Proven able to fail: moving the GUI texture's binding from 0 to 1 failed it with both lines named.
2. **Frames:** fixed scenes captured with `--auto-screenshot` at 1280×720, switch off, on, and off again with the same binary. GLSL and Slang compile to different instructions, so floating-point rounding can differ by a unit or two (the sun differed by 2 to 3 of 255, task 0023). "Identical" means: the difference between off and on is within the difference between two off runs, and regions that do not animate are exact.
3. **That the twin is used:** a deliberately wrong Slang output (a magenta GUI) must show in a captured frame, then be reverted.
4. **Frame time:** the benchmark scenes (`tools/benchmark.sh`) for families that are drawn often (object, terrain, water).

## 6. Stage a results (GUI)

- **Interface:** both GUI stages match their GLSL (the test passes; the deliberate binding move fails it).
- **Twin used:** with `[Rendering] slang shaders = true` and the magenta twin, every GUI element in the Caius Cosades frame was magenta.
- **Frames:** off against on, same binary, four scenes: the static bottom-right HUD (minimap and cell name) in Caius Cosades' house is exact (0 pixels differ); elsewhere the differences are those of animated scenery behind translucent HUD parts. Off against off (Caldera): mean 0.645, max 11; off against on (Caldera): mean 0.269, max 9. So the GUI port is within run noise, and exact where the scene behind it is still.
- **Not shown:** a byte-identical frame with the switch off against the commit before task 0072 (no binary of that commit was built); the switch-off path differs only by the trace call (a no-op without the variable) and a value on the shader options that nothing reads when it is false.

## 7. Risks

- **Rounding and precision:** Slang may reorder or fuse floating-point operations differently from glslang; frames are compared, not bytes.
- **Matrix layout:** `-matrix-layout-column-major` keeps GLSL's `mat4` bytes; `mul(M, v)` is GLSL's `M * v` (note 0005). Every port with matrices needs the frame check.
- **Block layouts:** std140 uniform blocks and std430 storage buffers must keep their offsets; the interface checker compares every leaf offset and stride.
- **Samplers:** GLSL's combined samplers map to `Sampler2D`; separate sampler objects and comparison samplers (`lib/view/shadow.glsl`) need care (note 0005, section 4.2).
- **Texture-coordinate arrays sized by a specialization constant** in the object shader (note 0005, section 7) need a design change.
- **Settings baked into the source:** `Resource::ShaderSettings` feeds `*.settings` values into the GLSL; a Slang port must take them as specialization constants or uniforms (decision 0009).
- **The editor** shares the pipeline code; Eden Studio must start after each family (it did after stage a).
- **Build time:** about 0.2 s per stage and define set; the terrain's full enumeration could reach hundreds of sets, so batching several sets per `slangc` run may be needed.
- **CI:** GitHub's runners download the pinned archive already; more permutations mean longer builds.
- **Linux:** the permutation build uses the same `slangc` flags; not yet run on Linux (no gate run in stage a).
