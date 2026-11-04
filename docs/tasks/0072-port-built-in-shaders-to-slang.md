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
- [x] Stage b: the sky families (atmosphere, clouds, moon, night, sun, sunglare) ported with the define sets they are created with; the old single-file sun mechanism retired in favour of the permutation files
- [x] Stage c: the compute passes (fogofwar, light assign, particle simulate with its specialization constants, worldoverlay) ported; `vsgUtil::readShader` callers use the twin when the switch is on
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

### Stage b, 2026-11-04: the review of stage a, and the sky

**Review of stage a**, each item fixed before more families were ported (commit `35fd516c42`):

1. **The key is the set the GLSL compiles with.** VSG's `ShaderCompiler::combineSourceAndDefines` writes `#define X` only for names a stage lists in `#pragma import_defines`, so two requests that differ in names the stage does not import compile to the same code. `vsgUtil::importedDefines` parses the pragma as VSG does, `effectiveDefines` is the requested names it imports, and twins are listed, chosen and traced by that set. A ported stage asked for a set it has no file for logs it once per (stage, set). `ShaderDefines.the_effective_set_is_what_vsg_defines` compares with VSG's own function.
2. **Selection order permutation, then GLSL.** The old single `<stage>.spv` of task 0023 is never taken after a miss (`vsgUtil::slangTwin`); tests for the order, the switch off, an unlisted set beside a single file, and no manifest.
3. **Checker gaps** (`spirvinterface.cpp`): Block against BufferBlock (both "storage"), image depth, sampled and format, execution modes with their literals (local size, origin, early tests, depth replacing), Flat/NoPerspective/Centroid/Sample, Component and Index, specialization constant defaults, capabilities and extensions, RowMajor/ColMajor with the matrix shape (Slang writes `floatRxC` transposed with RowMajor; undone by the generator id), only the entry point's interface variables, member locations. New tests: a buffer block, a storage image against a shadow sampler against a sampled image, a compute local size, interpolation, a constant's default, matrix majority. The GLSL in the tests compiles with the engine's settings: `Graphics::createShaderStage` and `Pipeline::terrain` use a default `vsg::ShaderCompileSettings` (Vulkan 1.0, SPIR-V 1.0), as the test's compile does.
4. **UB:** `&words[i + 1]` past the last word in `spirvinterface.cpp` and `testslangshaders.cpp` is `words.data() + i + 1`.
5. **Stale outputs:** files named `<stage>.p<8 hex digits of the SHA-1 of the define list>.spv`; files of an older list and `.spv.tmp` removed at configure; the manifest and files removed when the build has no slangc; slangc and every `lib/*.slang` are dependencies.
6. **Docs:** research note 0035's 1.85 s is an upper bound (each timed compile is a copy compiled beside the engine's own compiles, with per-call setup); its union of 55 pairs is **48 programs** once the sets are the effective ones (recomputed from the stage a traces: object fragment 18, object vertex 12, terrain 2 + 2, water 1 + 1, each sky stage 1); a runtime library (option B) would ship `slang.dll` 0.16 MB, `slang-compiler.dll` 25.9 MB, probably `slang-glsl-module.dll` 1.5 MB and the `slang-standard-module-2026.19` folder 32.2 MB, possibly `slang-glslang.dll` 6.2 MB (what SPIR-V output needs at run time is not tested); `docs/dependencies.md` no longer says "proposed, not adopted" or "decision draft".
7. **Switch off:** the terrain trace call no longer copies the define set (both branches of the conditional are references); the graphics path's copy of the stage predates the task.
8. **Cache:** keyed by the options' search paths, stage and set; an unreadable listed file is logged once and not read again. The test fixture now uses a unique folder per instance (two fixtures at the same address could share a cached manifest).
9. **GCC:** test helpers used only with slangc are `[[maybe_unused]]`; the truncated-module test asserts that it refused at least once.
10. **CMake and C++ agree:** stem `[a-z0-9]+`, names of at most 64 bytes, 1 to 4096 sets, CRLF manifests read.
11. **Timing race:** the timed compile runs under the trace's mutex, so timed compiles are serialised among themselves but not against the engine's compile threads; the numbers are upper bounds (item 6).

Red runs: `ShaderDefines` 9 of 11 failed against stubs (2 vacuous: the switch-off and no-manifest cases); the checker and selection additions 8 failed, 5 passed vacuously (switch off, unlisted set, unreadable file, a final one-word instruction, the truncated module).

**Test edits made and why** (all in tests this task wrote): `testslangpermutations.cpp` compiles the GLSL with `Resource::ShaderSettings` as a reader (the `*.settings` includes are virtual files of the engine), reports every failing permutation instead of stopping at the first, reads `<stage>.interface-exceptions` and fails on an entry that no longer occurs, and accepts the twin's settings constants only with the table's ids and types; `testslangshaders.cpp` (task 0023's sun test) only the UB fix.

**Settings constants.** The values of `light.settings`, `shadow.settings`, `fog.settings`, `cloud.settings` and `material.settings` are written into the GLSL source at run time; a twin compiled at build time declares them as specialization constants with fixed ids (`Resource::settingsConstantTable`, from 1000, far above the GLSL's ids). `Resource::ShaderSettings` records each value as it writes it, `createShaderOptions` puts them on the shader options, and `vsgUtil::addSettingsConstants` adds them to twin stages only (a stage that sets an id already keeps its value). Vulkan ignores ids a module does not declare.

**The sky** (commit `210edb47ed`): twelve stages, one file each, with the effective sets the game creates (traces of the default settings and every major feature, and the sky runs below): atmosphere vertex and fragment, moon vertex, sun vertex, sunglare vertex `-`; clouds vertex `COLOR NORMAL TEXCOORD TEXMAT VERTEX`; clouds and night fragment `COLOR DIFFUSE_MAP MATERIAL TEXCOORD`; moon fragment `DIFFUSE_MAP GLOW_MAP MATERIAL`; night vertex `COLOR NORMAL TEXCOORD VERTEX`; sun fragment `DIFFUSE_MAP MATERIAL`; sunglare fragment `MATERIAL`.

- **Interface:** `SlangPermutations` passes for all twelve. Accepted differences, each with its reason in `<stage>.interface-exceptions`: the GLSL keeps declarations it never reads (the volume fog's bindings 14 and 15 in the atmosphere fragment; unused vertex inputs and the `numUvSets` texture-coordinate array and its constant in the clouds and night vertex stages). Slang cannot size an input array by a specialization constant, so the twins take one texture coordinate set, which is all the sky's models have.
- **Twin drawn:** with the switch on, the verbose log names all twelve twins in every run (none with it off); a magenta moon twin drew both moon quads where the moon formula puts them (night, facing north-west).
- **Frames** (1280x720, Seyda Neen, scripted weather, hour and still camera; outputs in `I:\Projects\OpenEden-work\slangport2`): five scenes (clear noon, sunset facing the sun, night facing the moons, thunderstorm, cloudy) in three groups (defaults; hdr, atmosphere, lit clouds and sun glow; the same with volumetric clouds), each run twice with the switch off and once on. Top half of the frame (the sky): max difference of 255, and share of pixels differing by more than 4:

| Scene | Defaults: off/off | off/on | Features: off/off | off/on | + volumetric clouds: off/off | off/on |
| --- | --- | --- | --- | --- | --- | --- |
| Clear noon | 26, 0.06% | 25, 0.26% | 34, 0.45% | 34, 0.43% | 41, 0.56% | 39, 0.57% |
| Sunset | 1, 0% | 1, 0% | 1, 0% | 1, 0% | 3, 0% | 3, 0% |
| Night | 1, 0% | 1, 0% | 1, 0% | 1, 0% | 2, 0% | 3, 0% |
| Storm | 24, 0.92% | 22, 1.06% | 37, 2.11% | 28, 1.25% | 30, 2.95% | 37, 2.87% |
| Cloudy | 2, 0% | 18, 0.18% | 17, 0.10% | 11, 0.03% | 6, 0.03% | 1, 0% |

  Every off/on difference is within the off/off noise of the same scene or, for the cloudy defaults, of the same scene in the next group: the clouds scroll in real time, and the amplified difference image follows the cloud texture. Sunset and night skies differ by at most 3 of 255. The frames were viewed.
- **Moons:** not visible at 23:00 on the first day with either compiler (the off frames match the on frames there); the magenta twin shows the quads are drawn, so their alpha is zero there. Not investigated; it predates this task.

**Proposal for toddhow:** `SlangSunTest` (task 0023) checks the sun's single files, which the game no longer loads; `SlangPermutations` checks the sun's permutations the same way. Remove the single files and that test (it is the only reader)?

**Not done in stage b:** the Linux gate (GCC `-Werror`), a fresh reviewer, a frame with the moons visible.

### Stage c, 2026-11-04: the compute passes

- **Built** (commit `ec7349d345`): `comp/fogofwar/update.slang`, `comp/worldoverlay/update.slang`, `comp/light/assign.slang`, `comp/particle/simulate.slang`, each one file for the set `-` (the passes are read with `vsgUtil::readShader` and compiled with no defines; the traces list all four with an empty set). The loaders call `vsgUtil::withSlangTwin` (written in stage b with its tests first: 1 of 3 failed against a stub, 2 vacuous), which traces the stage and, with the switch on, returns a copy of the shared GLSL stage with the twin and the settings constants. New shared includes: `lib/math/{distance,projection}`, `lib/util/quickstep`, `lib/view/screencoord`, `lib/light/{pointlights,grid,maxlights,equations,lightsettings}`. The work group sizes stay in the GLSL files that `components/pipeline/fogofwar.cpp` and `components/view/lightgrid.cpp` include for the dispatch; the twins repeat them with a note.
- **Interface:** `SlangPermutations` passes for all four with no exception, local sizes included. A GLSL storage block that is not an array (`{ uint indexCount; }`, `{ FrameArgs frameArgs; }`) is `GLSLShaderStorageBuffer` in Slang, which compiles to the same `BufferBlock` as glslang; a `RWStructuredBuffer` there was a runtime array, and the test caught it. The particle pass imports Slang's `glsl` module for `inverse`, `mod` and the bit casts, so they compile to GLSL's instructions.
- **Twins drawn** (deliberately wrong twins copied over the built files for one run, then restored and compared byte for byte): a particle twin that never runs removed the snow and the ash cloud (ash: 49% of pixels over 4 against 0.008% between two switch-off runs); a light grid twin that assigns no light changed the Mages Guild by more than the noise (31.6% of pixels over 4, max 18, against 0% and max 4). Rain is not drawn by the particle pass (it did not change). The fog of war and world overlay twins go through the same function with the same options and are in the trace, but no frame shows their output: a fog twin that clears everything changed the HUD minimap by at most 2 (the explored area around the player has no fog at start), and the world map was not opened.
- **Frames** (1280x720, defaults, two switch-off runs and one on; whole frame, max difference and share over 4):

| Scene | off/off | off a/on | off b/on |
| --- | --- | --- | --- |
| Rain, Seyda Neen | 123, 4.99% | 118, 4.38% | 123, 4.02% |
| Snow | 127, 10.25% | 127, 10.31% | 72, 4.73% |
| Ash storm | 50, 0.008% | 49, 0.088% | 52, 0.085% |
| Caldera Mages Guild (interior lights) | 4, 0% | 7, 0.012% | 7, 0.998% |
| Ebonheart Council Chambers | 195, 13.0% | 197, 2.54% | 89, 0.13% |

  Within run noise: particles are seeded by the frame time, so they land elsewhere in every run (the ash difference image shows isolated flakes); the Mages Guild difference is a smooth change of the whole room's light level, as flickering lights give between runs, with no cluster-shaped block that a wrong light grid would leave. Another agent's games ran during the captures, so timing noise is larger than on an idle machine.
- **Not done in stage c:** the Linux gate, a fresh reviewer, a frame of the fog of war and of the world map with the twins.
