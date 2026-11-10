# Task 0008: Track V stage V0 — Groundwork for the Vulkan move

**Status:** CLOSED 2026-11-10 by toddhow (remaining checks are follow-ups, not blockers; listed in `docs/toddhow-checks.md`, "Known open items from Track V"). Earlier status: draft, awaiting toddhow's approval.
**Design doc section:** `docs/decisions/0002-vulkan-renderer-on-vulkanscenegraph.md`; background `docs/research/0001-vulkan-renderer.md`; guide `CLAUDE.md`, Track V.
**Goal:** prepare everything the renderer import (task 0009) needs, without changing what OpenEden renders: the Track V branch, pinned VSG dependencies building on Windows and Linux, a known-good vsgopenmw baseline built against those versions, a screenshot comparison rig with reference images from today's OpenSceneGraph renderer, and a measured performance baseline.
**Workflow:** full loop (guide, "Full loop required": every Track V stage).

---

## Context (facts this spec relies on)

- **OpenEden base:** branch `creativemotion-m12-runtime` at `442eec6e2f23fef1907e9ce8074f4cf55b0b1273`, the tip of the stacked branches `ik-collision-followups` → `inertial-locomotion` → `compat-morrowind` → `creativemotion-m12-runtime`. It is OpenMW 0.51 plus Jolt, the plugin system and CreativeMotion.
- **vsgopenmw:** https://github.com/vsgopenmw-dev/vsgopenmw, GPL-3.0. Pin `master` at `2830e7e2b4f18ef08ee24eff45a13568ec917061` (2026-09-08). Its merge base with OpenMW is `f724b05c57ee2f32e37a1087053a01dbc0847ea9` (OpenMW master, 2023-10-27); OpenEden is 5,201 upstream commits past it. The VSG rewrite is in 18 commits on top: 945 files changed, +87,235 / −7,524.
- **vsgopenmw's VSG requirement:** `find_package(vsg 1.0.9 REQUIRED)` and `find_package(vsgXchange 1.0.3 REQUIRED)` (its root `CMakeLists.txt`, lines 120 and 124). Its branch `vsg_v116` (2024-07-22, not merged into `master`) ported it to VSG 1.1.6 and needed VSG pull request #1244 ("ShaderSet fix"), merged upstream on 2024-07-21.
- **Current releases:** VulkanSceneGraph `v1.1.16` (2026-08-21), vsgXchange `v1.1.14` (2026-08-21), both MIT. VSG requires C++17, CMake 3.7+, Vulkan 1.1+, and glslang 14.0+ for runtime shader compilation (VSG `README.md` at `v1.1.16`, "Prerequisites").
- **Shaders compile at runtime:** vsgopenmw reads GLSL with `vsg::read_cast<vsg::ShaderStage>` (`components/vsgutil/readshader.cpp`) and compiles with `vsg::ShaderCompileSettings` (`components/pipeline/graphics.cpp:93`), so VSG must be built with glslang.
- **No OpenSceneGraph libraries in vsgopenmw:** its OSG `find_package` block is commented out (root `CMakeLists.txt`, about lines 391–409); it compiles against OSG headers bundled in `extern/osg` (231 files) plus `components/vsgadapters/osg` (`Matrixf`, `Matrixd`, `Quat`, `MatrixDecomposition`) for math.
- **Screenshot tooling exists upstream of us:** vsgopenmw commit `2830e7e2b4` adds `--auto-screenshot=N`, `--auto-quit=N` (frames counted in `State_Running`), an `openmw.debug.takeScreenshot()` Lua binding and `tools/render_gold.sh`. It touches `apps/openmw/arguments.hpp`, `main.cpp`, `mwlua/debugbindings.cpp`, `options.cpp`, `vsgengine.cpp`. The same flags were written against stock OpenMW 0.50 by vsgopenmw's maintainer (`VSG_MERGE_PLAN.md`, "Test rig").
- **Today's Windows build:** see the guide's Pitfalls (vcpkg deps in `deps/`, Qt 6.6.3, `-j 8`, install outside the build directory). Linux builds and integration tests run in the Ubuntu 24.04 CI-equivalent container described in the guide.

## Inputs / outputs

- **Branch:** `vulkan`, created from `creativemotion-m12-runtime` at `442eec6e2f` (or its tip at the time, see open question 1). All Track V work lands on it; it merges back only after V2 (task 0010).
- **Dependencies, fetched like Jolt in `extern/CMakeLists.txt`, pinned by tag and SHA-512:**
  - `vsg` at `v1.1.16`, built with shader compilation (glslang) enabled;
  - `vsgXchange` at `v1.1.14`;
  - glslang at a version ≥ 14.0 that VSG 1.1.16 accepts (verify against VSG's CMake, record it);
  - the Vulkan headers and loader: on Windows from the LunarG Vulkan SDK (record the version used), on Linux from Ubuntu 24.04 packages added to `CI/install_debian_deps.sh` (`libvulkan-dev` and whatever glslang/SPIR-V packages the build needs; verify package names on 24.04).
  - A CMake option `OPENEDEN_BUILD_VSG` (default `ON` on the `vulkan` branch) that builds them; in V0 nothing in OpenEden links them yet.
- **`docs/dependencies.md`** (create it, per the guide's guardrail): VSG, vsgXchange, glslang, Vulkan headers and loader, each with version, licence and why. Note VSG's `include/vsg/vk/vulkan.h` is Apache-2.0 (VSG `README.md`).
- **vsgopenmw remote and import branch in OpenEden's repository:**
  - remote `vsgopenmw` → https://github.com/vsgopenmw-dev/vsgopenmw.git;
  - branch `vsg-import` created at `2830e7e2b4`;
  - on `vsg-import` only: the minimal commits that make vsgopenmw build and run against VSG `v1.1.16` and vsgXchange `v1.1.14` (start from the changes on vsgopenmw's `vsg_v116` branch), each commit message naming what VSG API change it answers. These commits are what task 0009 merges.
- **Baseline note** `docs/research/0002-vsgopenmw-baseline.md`: how `vsg-import` was built on Windows and Linux, which patches were needed, what works and what does not when running it with toddhow's Morrowind data (it is expected that movement does not work, per `VSG_MERGE_PLAN.md`, "Known bugs on master"), with screenshots kept **outside the repository**.
- **Screenshot rig on today's OpenSceneGraph build (on `vulkan`):**
  - port `--auto-screenshot=N`, `--auto-quit=N` and `openmw.debug.takeScreenshot()` from vsgopenmw `2830e7e2b4` to OpenEden's engine (`apps/openmw/engine.cpp` instead of `vsgengine.cpp`), as a separate commit naming the source commit;
  - `tools/render_compare.sh` (bash, runs on Linux and in Git Bash on Windows): runs a list of scenes with isolated config and user-data directories (never the user's real ones), `--skip-menu --new-game --start <cell>` or `--load-savegame`, takes one screenshot per scene and quits; compares two image sets and reports per-image mean absolute error and maximum channel difference;
  - a scene list `tools/render_scenes.txt` covering at least: an exterior town (Balmora), a coast with water (Seyda Neen), an interior with many lights (a Vivec canton interior), night with fog, an actor-heavy scene, and an example-suite scene that needs no game data (so CI can use it);
  - reference images from the OSG build at fixed settings (resolution 1280×720, fixed time of day and weather where the console allows, shadows on and off). Images made from Morrowind data stay outside the repository (game-derived content, guide guardrail "No game assets"); only example-suite images may be committed, and only once the example suite's licence is recorded in `docs/dependencies.md`.
- **Performance baseline** in the same research note: frame time, CPU and GPU time (OpenMW's `F3`/`F4` profiler overlay and `OPENMW_OSG_STATS_FILE` as used by `scripts/integration_tests.py`) for each scene on toddhow's PC, so V2 and V4 can compare.

## Constraints

- **Nothing OpenEden renders changes in V0.** The OSG build on `vulkan` must render the same as `creativemotion-m12-runtime`, apart from the new debug flags (which do nothing unless passed).
- **No game assets** in the repository: no Morrowind screenshots, saves or data.
- **Pins are exact:** tags plus SHA-512 for fetched archives, as Jolt is pinned (`extern/CMakeLists.txt`).
- **Windows build rules from the guide's Pitfalls apply** (`-j 8`, install prefix outside the build directory, CMake caches have absolute paths).
- **Do not push** `vulkan` or `vsg-import`, and never push to vsgopenmw's or OpenMW's repositories.

## Acceptance criteria

- [ ] Branch `vulkan` exists at the agreed base; branch `vsg-import` exists at `2830e7e2b4` plus only the VSG-compatibility commits.
- [ ] VSG `v1.1.16`, vsgXchange `v1.1.14` and glslang build from `extern/` on Windows (MSVC, the guide's toolchain) and Linux (GCC in the CI container), with runtime shader compilation available (a unit test compiles a trivial vertex shader through `vsg::ShaderStage` + `ShaderCompileSettings` and gets SPIR-V back).
- [ ] `docs/dependencies.md` lists every new dependency with version, licence and source.
- [ ] `vsg-import` builds `openmw` on Windows and Linux against the pinned versions, and with toddhow's Morrowind data starts and renders an exterior cell; what works and fails is written in `docs/research/0002-vsgopenmw-baseline.md`.
- [ ] `--auto-screenshot`, `--auto-quit` and `openmw.debug.takeScreenshot()` work on OpenEden's OSG build; a unit or integration test covers the option parsing, and a Lua integration test calls `takeScreenshot()` without error.
- [ ] `tools/render_compare.sh` produces reference images for every scene in `tools/render_scenes.txt` from the OSG build, twice in a row, and comparing the two runs reports differences only below a threshold the spec implementer measures and records (render noise floor); the example-suite scene runs headless under xvfb in the CI container.
- [ ] The performance baseline for every scene is recorded.
- [ ] All existing gates still pass on `vulkan`: `components-tests`, `openmw-tests`, `openmw-cs-tests`, the 32 Lua integration tests (Linux container), clang-format 14 on changed lines.

## Out of scope

- Any OpenEden code using VSG (task 0009 onward).
- Changing OpenEden's renderer, math or settings.
- Fixing vsgopenmw's own bugs on `vsg-import` beyond what is needed to build and render (movement and the navigator are task 0010's).
- Removing anything OSG-related.

## Open questions for toddhow

1. **Base of `vulkan`:** the stacked branches are not merged into one main line yet. Proposed: base `vulkan` on the stack tip (`creativemotion-m12-runtime`), so all current work is included; alternative: first merge the stack into a `main` branch (part of M7) and branch from that.
2. **VSG version:** pin the latest (`v1.1.16`, proposed, so Track V never has to upgrade mid-way) or the newest version closest to what vsgopenmw was written for (fewer compatibility patches now, an upgrade later)?
3. **Windows Vulkan SDK:** require the LunarG Vulkan SDK installed on developer machines and CI (proposed: simplest, includes validation layers useful for debugging), or fetch Vulkan headers and loader with CMake so no SDK install is needed?
4. **Reference scenes:** the proposed list above, or particular places toddhow wants watched (mods, heavy scenes)?
