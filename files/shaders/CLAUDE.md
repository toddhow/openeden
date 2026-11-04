# Shaders: `files/shaders`

Rules and pitfalls for the shaders. Rendering code has its own guides (`components/render/CLAUDE.md`, `apps/openmw/mwrender/CLAUDE.md`).

## Slang twins of the GLSL (task 0072)

- **New shaders are Slang** (decision 0009). Existing GLSL is ported family by family under task 0072; the GLSL stays until toddhow agrees to delete it.
- **One SPIR-V file per define set.** `openeden_add_slang_permutations()` (this folder's `CMakeLists.txt`) compiles a `.slang` source once per listed define set into `<stage>.p<NNNN>.spv` and writes `<stage>.permutations`. A set is the canonical list (sorted, one space between), `-` for no defines.
- **Exact match or GLSL.** With `[Rendering] slang shaders` on, `vsgUtil::findSlangPermutation` returns the file built for exactly the stage's define set; any other set draws with the GLSL. So list every set a family is created with: run the game with `OPENEDEN_SHADER_TRACE=<file>` (and `OPENEDEN_SHADER_TRACE_TIMING=1` for glslang times) and copy the lines of that stage.
- **Same interface or a bug.** A twin must declare exactly what its GLSL stage declares with the same defines (bindings, block offsets, push constants, locations, built-ins, specialization constants): the C++ side builds pipeline layouts by hand. `SlangPermutations` in `components-tests` checks every built permutation; it finds new families by itself.
- **Prove the twin is drawn** with a deliberately wrong output (task 0072 used a magenta GUI), then revert; compare frames switch off, on, and off again with the same binary.
- **Pitfalls:** `-matrix-layout-column-major` keeps GLSL's `mat4` bytes and `mul(M, v)` is `M * v`; `SV_VulkanVertexID` is `gl_VertexIndex` (`SV_VertexID` needs `shaderDrawParameters`); a key given twice in a test `settings.cfg` is a fatal error box, not a warning.
