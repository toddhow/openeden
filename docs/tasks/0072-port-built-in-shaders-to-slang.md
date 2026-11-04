# Task: Port the built-in shaders to Slang
Design doc section: decision 0009 (`docs/decisions/0009-slang-for-new-shaders.md`, "glslang stays for vsgopenmw's GLSL until it is ported"); research notes 0005 (sections 6 and 7) and 0035 (the evidence for this task)
Goal: every built-in shader exists as Slang and is used by default, each family proven to declare the same interface and draw the same frames as its GLSL, before any GLSL file is deleted.
Inherited code touched (at `203f4fd03e`): `Pipeline::Graphics::createShaderStage` (`components/pipeline/graphics.cpp`), `Pipeline::terrain` (`components/pipeline/terrain.cpp`), `vsgAdapters::mygui::Render::registerShader` (`components/vsgadapters/mygui/render.cpp`), `createShaderOptions` (`components/resource/resourcesystem.cpp`), `files/shaders/CMakeLists.txt`; later stages: the compute pass loaders in `components/pipeline/{fogofwar,lightgrid,particle,worldoverlay}.cpp`, the GLSL in `components/render/{tonemap,bloom,velocity,ambientocclusion}.cpp` and `apps/openmw/mwrender/postprocess.cpp`, `Resource::ShaderSettings`.
Inputs / outputs: `.slang` sources under `files/shaders`; built `<stage>.p<NNNN>.spv` files and `<stage>.permutations` manifests under `resources/shaders`; `[Rendering] slang shaders` (exists, off by default); the shader vsg::Options value `openeden.preferSlang`; `OPENEDEN_SHADER_TRACE` and `OPENEDEN_SHADER_TRACE_TIMING` (diagnostics).
Constraints:
- Full loop (Track V renderer rules): tests first with a red run, a fresh reviewer for every family, frames viewed before every commit that can change rendering.
- With `slang shaders` off nothing changes: the GLSL path stays exactly as it was until the last stage.
- An unlisted define set always draws with the GLSL while the GLSL exists, so a missing permutation can never draw something wrong.
- No new runtime dependency without toddhow's decision (option B of note 0035); `slangc` and `spirv-val` stay build tools.
- Determinism: permutation files and manifests are generated in a fixed order; the manifest reader is strict.
- New SDK-free code only in `components/vsgutil` (no dependency on other components) and `files/shaders`.
- GCC `-Werror -Wshadow -Wunused -Wdangling-else`, 120 columns, clang-format 14.
Acceptance criteria:
- [x] Stage a: `vsgUtil::canonicalDefines`, `parsePermutationManifest`, `selectPermutation`, `findSlangPermutation`, `prefersSlang`, `recordShaderUse` and `readSpirvInterface`/`compareSpirvInterfaces` exist with tests written first (red run recorded), `openeden_add_slang_permutations()` builds one file per define set with the existing flags and `spirv-val`, and the GUI is the first ported family
- [x] Stage a: `SlangPermutations.every_permutation_has_the_interface_of_its_glsl_stage` passes for every built permutation and fails when a binding is moved
- [x] Stage a: with the switch on, the GUI is drawn by the Slang twin (shown by a deliberately wrong twin) and its frames are within run noise of the GLSL's, exact in a still region
- [ ] Stage b: the sky families (atmosphere, clouds, moon, night, sun, sunglare) ported with the define sets they are created with; the old single-file sun mechanism retired in favour of the permutation files
- [ ] Stage c: the compute passes (fogofwar, light assign, particle simulate with its specialization constants, worldoverlay) ported; `vsgUtil::readShader` callers use the twin when the switch is on
- [ ] Stage d: water ported
- [ ] Stage e: terrain ported, its define sets enumerated completely from the settings that produce them (a test lists the settings combinations and checks that each has a file)
- [ ] Stage f: the object shader (`default/main.vert`, `main.frag`) ported with the recorded define sets plus the sets of a long play-through and of the Faultier PBR test data; the texture-coordinate arrays redesigned (note 0005, section 7); frame time on the benchmark scenes no worse than the GLSL's
- [ ] Stage g: the GLSL inside C++ (tonemap, bloom, velocity, ambient occlusion, post-processing's own stages) moved to `.slang` files; `.omwfx` mod shaders keep GLSL input (decision 0009)
- [ ] Stage h: toddhow decides option B of note 0035 (the Slang library in the game with an on-disk cache) or keeps glslang for the object shader's unlisted sets; implemented as decided
- [ ] Stage i: shared `lib/*.glsl` includes become Slang modules (`import`) once every consumer is Slang; duplicated declarations (the sun's own copy of `Scene`) removed
- [ ] Stage j: `[Rendering] slang shaders` on by default, then each family's GLSL deleted only after toddhow agrees, with `SlangPermutations` changed to compare against recorded interfaces
- [ ] With the switch off, every frame of `tools/render_scenes.txt` is unchanged against the commit before each stage (byte-identical where the scene is still, within run noise elsewhere, the noise measured by two runs of the same binary)
- [ ] Linux: the permutation build and `SlangPermutations` pass in the Linux gate
Out of scope: changing what any shader draws; new rendering features; the `.omwfx` language; removing glslang (stage h decides).

## Stages, in order

| Stage | What | Why this order |
| --- | --- | --- |
| a | Infrastructure (permutation build, manifest, selection, trace, interface checker) and the GUI | Smallest family, no defines, exercises every new piece |
| b | Sky (6 families, 12 stages) | Few, fixed define sets; small; the sun already has a twin |
| c | Compute passes (4) | Leaf passes, one define set each, one with specialization constants |
| d | Water | One family, one or two define sets, many includes (exercises shared code) |
| e | Terrain | Define sets come from settings only: can be enumerated completely |
| f | Object shader | The largest and the only one whose define sets are open-ended |
| g | GLSL inside C++ | Independent passes, moved into files first |
| h | Runtime compiler decision | Needs the data from f |
| i | Shared includes as Slang modules | Only once every consumer is Slang |
| j | Default on, then GLSL deleted family by family | Only with toddhow's agreement |

## Open questions for toddhow

1. **The architecture:** accept the hybrid (note 0035, option D): build-time permutations now, and the decision about the Slang library in the game (option B, about 26 MB) taken at stage h with measured data? Recommendation: yes.
2. **What "identical" means:** within the noise of two runs of the same binary, and exact in still regions, since the two compilers round differently? Recommendation: yes.
3. **The switch:** keep `[Rendering] slang shaders` off by default until stage f is proven, then turn it on for everyone before deleting GLSL? Recommendation: yes.
4. **Mods' `.omwfx` shaders** keep GLSL input through glslang, as decision 0009 says? Recommendation: yes.
5. **Deleting GLSL:** family by family, each with your approval? Recommendation: yes.
6. **Build time:** the terrain's complete enumeration may add a minute or two to a clean build (about 0.2 s per stage and set); acceptable, or batch several sets per `slangc` run first? Recommendation: batch only if it exceeds two minutes.

## Stage log

### Stage a, 2026-11-04: infrastructure and the GUI

- **Built:**
  - `components/vsgutil/shaderpermutation.{hpp,cpp}`: canonical define sets, the strict `.permutations` reader (limits: 4096 lines, 64 KiB, identifiers of at most 64 bytes, file names `<stem>.<vert|frag|comp>[.p<4 digits>].spv`), exact-set selection, `findSlangPermutation` (manifests read once per path, mutex-guarded), `prefersSlang` (a bool on the shader `vsg::Options`, set from `[Rendering] slang shaders` in `createShaderOptions`), and the trace (`OPENEDEN_SHADER_TRACE`, `OPENEDEN_SHADER_TRACE_TIMING`).
  - `components/vsgutil/spirvinterface.{hpp,cpp}`: the SPIR-V interface reader of the task 0023 sun test as a library, with specialization constants, storage buffers, runtime arrays and bounds checks (a truncated module or a bad word count is refused, never read past the end).
  - `openeden_add_slang_permutations()` in `files/shaders/CMakeLists.txt`: one `slangc` run per define set, checked by `spirv-val`, and a generated manifest.
  - `Graphics::createShaderStage` uses an exact-set permutation when the switch is on and falls back to the old single-file twin (the sun) and then to the GLSL; `Pipeline::terrain` and `Graphics` write the trace; the GUI renderer uses its twin when the switch is on.
  - `files/shaders/gui/gui.slang`: the GUI's vertex and fragment stages.
- **Tests (written first):** `ShaderPermutation` (12) and `SpirvInterface` (7) ran red against stubs: 17 of 20 failed (3 passed vacuously: the empty set, two compiles of the same source, the truncation loop), then all passed. `SlangPermutations` (1) checks every built permutation against its GLSL; it failed as intended when the GUI texture's binding was moved to 1, and passed again after the revert. `components-tests` 5231 pass on Windows. Eden Studio starts.
- **Frames and measurements:** research note 0035, sections 2, 3 and 6.
- **Not done in stage a:** the Linux gate, the fresh review, and a frame from a binary of the commit before (see note 0035, section 6).
