# 0009 — New shaders are written in Slang; glslang stays for vsgopenmw's GLSL until it is ported

Date: 2026-11-01 · Status: **accepted** (toddhow: "New shaders should be written in Slang not GLSL", 2026-11-01) · Drafted 2026-11-01 in task 0023 (F1)

Numbered 0009 because 0004 is a draft on the branch `research/slang-shaders` (research note 0005, section 9) and 0008 is the hosting decision. This is that draft, brought up to date with what task 0023 built and measured.

## Proposal
- **New shaders are written in Slang** and compiled to SPIR-V 1.0 at build time by a pinned `slangc` (v2026.19 to start), by `openeden_add_slang_shader()`. The game loads the `.spv` files through VSG's SPIR-V reader; it never runs Slang.
- **vsgopenmw's GLSL stays on glslang** (runtime compilation through `vsg::ShaderCompiler`) until each shader is ported. A port fixes the three incompatibilities of research note 0005 (separate sampler objects, `.st` swizzles, interface arrays sized by a specialization constant) and moves generated settings to specialization constants.
- **A shader written in Slang is switched on by a setting, off by default** (`[Rendering] slang shaders`), while the GLSL version exists. When the GLSL version is deleted, the setting goes with it.
- **glslang is removed** only when no shader is compiled at run time, or this decision is revisited if mod shaders need Slang inside the game.

## What task 0023 showed
- **The pipeline works end to end.** The sun shader, ported by hand (`files/shaders/sky/sun/sun.slang`), compiles in the normal build, declares the same descriptor sets, bindings, uniform byte layouts, push constants, interface locations and entry point as the GLSL (a unit test compares the SPIR-V's declarations, `apps/components_tests/vsg/testslangshaders.cpp`), and draws the same frame: in the sun's disc the largest difference to the GLSL frame is 2 to 3 of 255 on one channel, where two GLSL frames of the same scene differ by 2 (`docs/tasks/0023-rendering-groundwork-f0-f1.md`, stage log).
- **Two Slang habits that mattered, both found by the test or by reading the SPIR-V:**
  - `SV_VertexID` makes Slang read the `BaseVertex` built-in too, which needs the `shaderDrawParameters` feature. `SV_VulkanVertexID` is `gl_VertexIndex`, as the GLSL uses.
  - Slang writes `float4x4` as the transposed SPIR-V matrix with `RowMajor`. With `-matrix-layout-column-major` the bytes are those of GLSL's `mat4`, so C++ uploads the same data, and `mul(M, v)` is GLSL's `M * v`. The frame comparison, not the layout test, proves the maths.
- **`slangc` validates its output** with the SPIRV-Tools inside it unless `-skip-spirv-validation` is given, which the build does not give. A separate `spirv-val` run is not installed on this machine.
- **Cost:** a 63 MB (Windows) or 29 MB (Linux) download per fresh build folder, hash-checked; about 0.2 s per shader stage at build time.

## Consequences
- Two shader languages coexist during Track V and for as long as GLSL shaders remain.
- Each port is checked with a layout test and a frame comparison (`tools/render_compare.sh`, or the sun-style comparison in task 0023), and with the validation layer once it is installed.
- New shaders take settings as specialization constants or uniforms, not generated `#include`s, so that they can be compiled offline.
- Slang releases about every two weeks; pins are refreshed on purpose, never automatically, and a refresh needs a re-run of the layout test.
- The licence, versions and hashes are in `docs/dependencies.md`.

## What toddhow decides
1. Approve this decision (the research note's question 3 in `docs/research/0008-modern-rendering-proposal.md`, section 9).
2. Whether `slang shaders` stays a setting for the whole of Track V or the sun's GLSL is deleted once V4 is finished.

## Note, 2026-11-04 (task 0072)
toddhow asked to start porting the existing shaders. Task 0072 keeps this decision's rule that the game does not run Slang: stage a compiles a Slang twin once per define set at build time (`openeden_add_slang_permutations()`), and the game picks the file whose define set matches exactly, else the GLSL. Whether the game should embed the Slang library for the object shader's open-ended define sets (research note 0035, option B) is left to toddhow at task 0072's stage h; until then this decision stands unchanged.
