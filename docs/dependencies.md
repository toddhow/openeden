# Dependencies

This document records third-party libraries and runtime dependencies introduced into OpenEden, tracking version pins, licenses, sources, and architecture rationale.

---

## Track V: Vulkan Renderer Dependencies (Stage V0)

Stage V0 introduces the dependencies required for the VulkanSceneGraph (VSG) renderer migration without altering the active gameplay renderer (which remains OpenSceneGraph until Stage V1).

### 1. VulkanSceneGraph (`vsg`)

- **Version:** `v1.1.16` (2026-08-21 release tag)
- **Source:** https://github.com/vsg-dev/VulkanSceneGraph/archive/refs/tags/v1.1.16.zip
- **Archive Hash (SHA-512):** `0b23ace8d665f9b94d5b8058cbf9072b659dcbfe10168348584bf5e72adfb2e16bdf75e12bc47cf83d6f1afc72603fa97ebc9b90ba8084873628a2bd48234730`
- **License:** MIT
  - *Note:* `include/vsg/vk/vulkan.h` is derived from the Vulkan-Headers project and licensed under Apache-2.0 (documented in upstream VSG `README.md`).
- **Purpose / Why:** Modern Vulkan-native retained scene graph replacement for OpenSceneGraph. Provides Vulkan device management, command recording, pipeline layouts, descriptor sets, and multi-threaded rendering graphs.
- **Build Configuration:** Built via CMake `FetchContent` as a shared library (`BUILD_SHARED_LIBS=ON`) with runtime shader compilation enabled (`VSG_SUPPORTS_ShaderCompiler=ON`).

### 2. vsgXchange

- **Version:** `v1.1.14` (2026-08-21 release tag)
- **Source:** https://github.com/vsg-dev/vsgXchange/archive/refs/tags/v1.1.14.zip
- **Archive Hash (SHA-512):** `57d6f626e54843fa813b85a64d1e24e9b09fb5fabc3fba08d66104d41e04cbdbfaf9a5aff3b30c4ded75c09dedc49146f9eb719c1a8ee713c807031fc436389b`
- **License:** MIT
- **Purpose / Why:** Standard asset I/O plugin suite for VulkanSceneGraph, providing image and model loaders (glTF 2.0, DDS, PNG, JPEG, KTX, etc.).
- **Build Configuration:** Built via CMake `FetchContent` against `vsg`.

### 3. glslang

- **Version:** `14.3.0` (2024-06-25 release tag)
- **Source:** https://github.com/KhronosGroup/glslang/archive/refs/tags/14.3.0.zip
- **Archive Hash (SHA-512):** `e5dd6405a837dadc19287d04f3ab25fe33c199deec308bcbc9a1a7d8a9f4ee004ce75264130f31f7c9bbb4dcecd74c95df35925f706826ca2a85ff02f6a2e7c1`
- **License:** BSD 3-Clause (Khronos / Google) and Apache 2.0 with LLVM Exception.
- **Purpose / Why:** Compiles GLSL shader stages at runtime into SPIR-V bytecode. Required by VSG's `vsg::ShaderCompiler` to enable runtime shader hot-reloading and shader pipeline setup.
- **Build Configuration:** Built via CMake `FetchContent` as static libraries (`BUILD_SHARED_LIBS=OFF`, `BUILD_EXTERNAL=OFF`, `ENABLE_OPT=OFF`, `ENABLE_GLSLANG_BINARIES=OFF`).

### 4. Vulkan-Headers

- **Version:** `vulkan-sdk-1.3.296.0` (Vulkan 1.3 specification)
- **Source:** https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.3.296.0.zip
- **Archive Hash (SHA-512):** `ab0d7a03e76c7a3e80ee5c05456e5e076e51864ac837c97c9795b3ca2d11cdc6ceea963ffb637a8dafbd5a53720555b9f697ef0adff7dcad6a34478a767e9631`
- **License:** Apache-2.0
- **Purpose / Why:** Provides standard C/C++ Vulkan headers (`vulkan/vulkan.h`, `vulkan/vulkan_core.h`, etc.) for host builds without requiring a pre-installed LunarG SDK on Windows or Linux development environments.

### 5. Vulkan Loader / Import Library

- **Windows:** On Windows systems, `C:\Windows\System32\vulkan-1.dll` is installed by graphics drivers (NVIDIA, AMD, Intel). OpenEden generates an import library `vulkan-1.lib` using the MSVC `lib.exe` utility and `extern/vulkan/vulkan-1.def`, which lists the 265 standard Vulkan entry points. This eliminates any hard requirement for a local LunarG SDK installation.
- **Linux:** On Ubuntu 24.04 and Debian-based systems, `libvulkan-dev` (from `CI/install_debian_deps.sh`) provides `libvulkan.so` and system loader libraries.

## Track V: Stage V1 runtime dependencies

### 6. SDL2 with Vulkan support (Windows runtime)

- **Version:** `2.32.10`, the same release as the SDL2 in OpenMW's Windows dependency set (vcpkg).
- **Source:** https://github.com/libsdl-org/SDL/archive/release-2.32.10.tar.gz
- **Archive Hash (SHA-512):** `d5622d6bb7266f7942a7b8ad43e8a22524893bf0c2ea1af91204838d9b78d32768843f6faa248757427b8404b8c6443776d4afa6b672cd8571a4e0c03a829383` (the hash vcpkg's `sdl2` port uses for this release)
- **License:** Zlib
- **Purpose / Why:** The SDL2 in OpenMW's prebuilt vcpkg package set is built without vcpkg's `vulkan` feature, so its Windows video driver cannot create a Vulkan window ("Vulkan support is either not configured in SDL"). OpenEden still links that SDL2, and ships a Vulkan-enabled build of the same release next to the executables. The exported API is identical.
- **Build Configuration:** `extern/CMakeLists.txt` builds it with `ExternalProject` (`SDL_SHARED=ON`, `SDL_STATIC=OFF`, `SDL_TEST=OFF`, `SDL_VULKAN=ON`, Release), and `apps/openmw/CMakeLists.txt` copies `SDL2.dll` next to `openmw.exe` after each build. Windows only; controlled by `OPENEDEN_SDL2_VULKAN_RUNTIME` (default ON). Linux uses the distribution's SDL2, which has Vulkan support.

### VSG runtime DLLs on Windows

`vsg-17rd.dll` and `vsgXchangerd.dll` are written next to the executables (`extern/CMakeLists.txt` sets the `vsg` and `vsgXchange` targets' runtime output directory). Before this, they stayed under `_deps/` and `openmw.exe` could run against a stale copy.

## Track V: Stage V2 dependencies

### 7. assimp (Open Asset Import Library)

- **Version:** `6.0.5`, the release vcpkg's `assimp` port uses.
- **Source:** https://github.com/assimp/assimp/archive/refs/tags/v6.0.5.tar.gz
- **Archive Hash (SHA-512):** `57326194bf3a8e2ae793c739878231067fdd6d031531e910d4d20fc8a673eacf48f75e11bd21ca2e6421682f78585d445bffb647bdcb52e01b2cd4d3ff0e2c62` (the hash vcpkg's `assimp` port uses for this release)
- **License:** BSD-3-Clause; its bundled third-party code is BSD-2-Clause, ISC, MIT and zlib, all GPL-3.0-compatible.
- **Purpose / Why:** OpenMW 0.51 read COLLADA (`.dae`) models through OpenSceneGraph's plugin, which is gone on the Vulkan build. Without it, non-NIF models had no collision: actors with `.dae` models (the example suite's player) got no physics body and could not move, the navmesh missed `.dae` buildings, and launching a magic bolt with a `.dae` model crashed. `components/resource/physicsshapemanager.cpp` now reads these models with assimp to build their collision shapes. The same models are drawn and animated through `components/vsgadapters/dae/` (task 0010). Approved by toddhow (2026-10-29).
- **Build Configuration:** `extern/CMakeLists.txt` fetches it with `FetchContent` as a static library with only the COLLADA importer (`ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF`, `ASSIMP_BUILD_COLLADA_IMPORTER=ON`, no exporters, tools, tests or install), against the engine's zlib (`ASSIMP_BUILD_ZLIB=OFF`). `components` links `assimp::assimp`. The COLLADA animation reader (`vsgadapters/dae/daeanim.cpp`) uses the pugixml that assimp compiles into its library (`contrib/pugixml`, MIT), through `OPENEDEN_PUGIXML_INCLUDE_DIR`. On Windows assimp finds and links the dependency set's minizip (`minizip.dll`, already deployed next to the executables); on Linux it uses the distribution's minizip or its bundled copy. vsgXchange's own assimp reader stays off (`vsgXchange_assimp=OFF`) because it builds VSG's stock shader pipelines, not vsgopenmw's.

## Track V: Stage V6 dependencies

### 8. vsgQt

- **Version:** `v0.5.0` (2026-08-21 release tag)
- **Source:** https://github.com/vsg-dev/vsgQt/archive/refs/tags/v0.5.0.tar.gz
- **Archive Hash (SHA-512):** `1e22e2171b3a470e09b04bce5f27e1fa2ceda38a9ea62c2da558664d47cf289d918f33fc324292c4e237af2bd0eb6affe7ce357639c1151d7f93a9c7cedd6d37`
- **License:** MIT
- **Purpose / Why:** The editor's Vulkan viewport: a `QWindow` that owns a `vsg::Window`, embedded with `QWidget::createWindowContainer` (task 0014).
- **Build Configuration:** Fetched by `extern/CMakeLists.txt` only when the editor is built, with `QT_PACKAGE_NAME=Qt6` and its examples off. Linux uses XCB only, so a Wayland session needs `QT_QPA_PLATFORM=xcb`.

## Track V: Stage V6, removed

- **OpenSceneGraph** (OpenMW's fork on Linux, vcpkg's 3.6.5 on Windows, its plugins and `extern/osgQt`) and **COLLADA-DOM** (OSG's `.dae` plugin): removed from the source, CMake, `CI/install_debian_deps.sh`, the Windows and Flatpak scripts (task 0014, D1 and D2). Their licences no longer apply to anything OpenEden builds. OpenMW's prebuilt Windows vcpkg archive (`CI/deps_versions.msvc.sh`) still contains OSG, as it is OpenMW's package set; nothing links or copies it now.
- **Qt's OpenGL and OpenGL Widgets modules:** no longer looked up or deployed. Ubuntu's `qt6-base-dev` still pulls the runtime package in as its own dependency.

## Slang shader compiler: adopted

Adopted as a build tool since task 0023 (decision 0009, accepted 2026-11-01); its entry is under "Rendering groundwork (task 0023)" below. This section said "proposed, not adopted" until task 0072, stage b (2026-11-04), which corrected it.

## Rendering groundwork (task 0023)

### Slang (`slangc`, build-time tool)

- **Version:** `v2026.19` (2026-09-29 release tag). Fetched by `extern/CMakeLists.txt` with `FetchContent` into `build/extern/fetched/slang`, behind the option `OPENEDEN_BUILD_SLANG_SHADERS` (on by default; no archive for other platforms, where nothing is built and `[Rendering] slang shaders` falls back to the GLSL).
- **Windows x86_64:** https://github.com/shader-slang/slang/releases/download/v2026.19/slang-2026.19-windows-x86_64.zip, 63,221,930 bytes
  - **SHA-512:** `cdd3329697a7307e0e46a03c45f279af9dd437cf842829f7dee579408959bd821982d9a486a4b01b865b19e67292c378ee962d340244c3062a3915b4f2cb967d`
  - **SHA-256:** `fc922f214b5d115f663932e59e80e0f6b6cc6fae140979cf307da6459b6f8f33` (matches GitHub's published digest)
- **Linux x86_64:** https://github.com/shader-slang/slang/releases/download/v2026.19/slang-2026.19-linux-x86_64-glibc-2.28.tar.gz, 29,164,768 bytes
  - **SHA-512:** `68bbb47ec6157d28b9fd353ad700facdd73d5fb56d6b5ef4a88bf8038c8e0f23ef951e596800322a3031573480262419a51900140f94314b45ae21c6dba0b5a7`
  - **SHA-256:** `bd7afcb0956dee9304f44ed115e9aaa7140704271b5c3294bcb237e3ae67adc7` (matches GitHub's published digest; downloaded and hashed on 2026-11-01)
- **License:** `Apache-2.0 WITH LLVM-exception`, read from the archive's `LICENSE` on 2026-11-01 and again on 2026-11-04 (task 0072). Compatible with GPL-3.0-or-later. The archive's `third-party-notices` folder lists the notices of what Slang itself bundles (glslang, SPIRV-Tools and others, all permissive).
- **Purpose / Why:** compiles `.slang` shaders to SPIR-V 1.0 at build time (`openeden_add_slang_shader()` in `files/shaders/CMakeLists.txt`). As a build tool nothing of Slang is linked into the game; the `.spv` files it writes are shipped, and since stage h so is the library (below). Research notes `docs/research/0005-slang-shaders.md` and `0035-porting-the-built-in-shaders-to-slang.md`; decision `docs/decisions/0009-slang-for-new-shaders.md` (accepted). Since task 0072 the built-in shaders are being ported to it, one family at a time, each built once per define set (`openeden_add_slang_permutations()`), and drawn only with `[Rendering] slang shaders` on.
- **In the game (adopted, toddhow 2026-11-04, "Yes ship it"; task 0072 stage h):** with `[Rendering] slang shaders` on, the game loads the compiler library at run time (`LoadLibrary`/`dlopen`, never linked) and compiles a define set that has no prebuilt `.spv` from the `.slang` sources, with an on-disk cache under the user data folder (`<user data>/openeden/shadercache`); with the setting off nothing is loaded. The same archive, the same pinned version; no new download.
  - **What the game ships, measured** (Windows, 2026-11-04, a probe program that loaded the library, compiled four listed permutations at run time and compared the bytes with the build's `slangc` output, then the game's own run-time path over every listed permutation): `slang.dll` (159,744 bytes, the API), `slang-compiler.dll` (25,918,976), `slang-glslang.dll` (6,215,680) and `slang-glsl-module.dll` (1,491,456), 33,785,856 bytes in all, installed in `resources/slang/` beside `resources/shaders` (the build copies them there from the pinned archive). With these the output of every one of the 170 listed permutations was **byte-identical** to the build's (`SlangRuntimeLibrary.every_permutation_the_build_wrote_is_reproduced_byte_for_byte`). **Not needed:** the `slang-standard-module-2026.19` folder, `slang-llvm.dll` (CPU targets), `gfx.dll`, `slang-rt.dll`, `slang.exe`, `slangc.exe`, `slangd.exe`, `slangi.exe`. `import glsl` works because the global session is created with `enableGLSL`.
  - **Two silent failures, both found by measuring, both avoided by loading the two libraries by their full path before the first compile** (`DynamicCompiler::load`, `components/vsgutil/slangruntime.cpp`): Slang loads `slang-glslang.dll` and `slang-glsl-module.dll` later, by bare name, through the process's default DLL search, which does not include `resources/slang`. (1) Without glslang the compile **succeeds and returns different, smaller, unoptimised code** (79,560 bytes against 89,956 for the object shader); `IGlobalSession::setDownstreamCompilerPath` did not fix it. (2) Without the glsl module Slang compiles it from its own source and **writes `slang-glsl-module.bin` into the library's folder** (the install folder), and takes about 1.3 s the first time. A Windows library loaded under a name is what a later load by that name returns, so loading by full path first is enough. The game also compiles `post/fullscreen.slang` at its first compile and refuses the runtime compiler for the session if the bytes are not those of the shipped `post/omwfx.vert` permutation.
  - **Linux x86_64** (the pinned archive's `lib/`, read with `tar tzvf` on 2026-11-04, not built or run here): `libslang-compiler.so.0.2026.19` (34,300,376 bytes), `libslang-glslang-2026.19.so` (10,166,992), `libslang-glsl-module-2026.19.so` (1,339,720, probably not needed), `libslang.so` (a link), `libslang-rt`, `libgfx` (not needed). The Linux load path is written from this listing (the three needed `.so` files are copied by the build and loaded the same way) and **has not run**.
  - **Memory and time (Windows, RTX 4070 machine, 2026-11-04):** global session about 100 ms warm and 300 to 1,350 ms on a cold disk; in the game 12 sets compiled in 1,781 ms in all (148 ms each on average, including the GLSL check that compiles the GLSL stage as well), the same 12 read from the cache in 4.0 ms; peak working set of the game with `slang shaders` on: 754 MiB with the setting off, 985 MiB on a run that compiled (+231 MiB: the compiler's heap stays until the process ends), 736 MiB on a run that only read the cache (the library is not loaded). One global session is not thread-safe (`slang.h`, `IGlobalSession`), so the game serialises compiles.
  - **Validation:** `slangc` validates its output with the SPIRV-Tools inside it unless `-skip-spirv-validation` is given; the API's equivalent is the compiler option `SkipSPIRVValidation` (`slang.h`, `CompilerOptionName`), which the game leaves off. The build's separate `spirv-val` run has no in-process equivalent; the game's checks are listed in the task 0072 stage h log.
- **Runner note:** the download is about 63 MB on Windows and 29 MB on Linux on each fresh build folder. `FetchContent` checks the SHA-512, so a changed or corrupt download stops the configure step.

### SPIRV-Tools and SPIRV-Headers (`spirv-val`, build-time tool)

Slang's release archive does not contain `spirv-val` (its SPIRV-Tools is compiled into `slang-compiler`/`slang-glslang`), and Khronos publishes no standalone binary. So the build compiles the validator from source, from pinned tags, only when a `.slang` shader needs checking. Chosen over the LunarG Vulkan SDK (a machine-wide installer, not usable on a clean runner without an install step) and over `apt`/vcpkg (not pinned, and not the same version on Windows and Linux).

- **Version:** both at the tag `vulkan-sdk-1.4.357.0`.
- **SPIRV-Tools:** https://github.com/KhronosGroup/SPIRV-Tools/archive/refs/tags/vulkan-sdk-1.4.357.0.zip, 4,985,921 bytes
  - **SHA-512:** `98ddcf34189abf79a2d4ac5f554e6432c09de64c2d1c1652ab452813d51200ff07065fcdc7ce61b8a76fe6dc6fd6393878bd2bdc4a6104499afcc10f430f7e71`
  - **SHA-256:** `09f70a9197d9cd28e1e10a2fca5ffde8c5e269f363eed9afa838ad88ed2649c9`
- **SPIRV-Headers:** https://github.com/KhronosGroup/SPIRV-Headers/archive/refs/tags/vulkan-sdk-1.4.357.0.zip, 667,873 bytes
  - **SHA-512:** `651e1d08e95e56a8512045d79b89afb7fe3ccfb9ba862a2e084440c65a9267e45b218b1df95d03cd3b2da476a51bac46cf66067ec1c4b1cbbcb55748293c7251`
  - **SHA-256:** `7d40c8c1983c1aba6f6f2fcbcf7be345d33a5693aeee0cc5b8c579fb8e3f2fea`
- **License:** Apache-2.0 (SPIRV-Tools `LICENSE`, read 2026-11-01; SPIRV-Headers is MIT-style/Apache-2.0 per its own `LICENSE`, permissive). Compatible with GPL-3.0-or-later.
- **Purpose / Why:** `openeden_add_slang_shader()` runs `spirv-val --target-env vulkan1.0` on every compiled `.spv` before it is put in place, so an invalid shader fails the build.
- **Build configuration:** `FetchContent` in `extern/CMakeLists.txt`, with tests, install rules and `-Werror` off. Its directory is `EXCLUDE_FROM_ALL`: only the `spirv-val` target is built, and only when `.slang` shaders are built. Nothing of it is linked into or shipped with the game.
- **Python (Windows only, when none is found):** SPIRV-Tools generates its grammar tables with Python 3 while it configures and builds. Linux and the GitHub runners have one. A Windows machine without Python (this one) gets the embeddable package, used as a build tool only and not shipped:
  - **Source:** https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip, 11,133,606 bytes (Python 3.12.10)
  - **SHA-512:** `98375769d8181a1553d361afdcda1cef34fd59cced20c2c8dc7ef0de349ad917057b59b6a66572bd6126bc352ef784f31a08e937ad185f6bc8d6c9271ab29411`
  - **SHA-256:** `4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3`
  - **License:** Python Software Foundation License (permissive, GPL-compatible).

## CI only: the Vulkan Runtime loader (Windows runner)

- **What:** `vulkan-1.dll` from LunarG's Vulkan Runtime 1.4.363.0, copied beside the unit-test programs in `.github/workflows/windows.yml`. The GitHub Windows runner has neither the Vulkan SDK nor a GPU, and the test programs link the loader. It is never installed by the game, never redistributed and never committed.
- **Source:** https://sdk.lunarg.com/sdk/download/1.4.363.0/windows/VulkanRT-X64-1.4.363.0-Components.zip, 18,211,191 bytes
- **SHA-256:** `a25a927aa8b9f0371048f1861cf88ac3b9bc9b1fb332c42d897c8ab32695769a`
- **License:** MIT and Apache 2.0 components (the zip's `VulkanRT-License.txt`); the Apache-2.0 and GPL-3.0 compatibility question recorded for the Eden archive work applies here only to a CI tool, not to anything we ship.

## Task 0052: Eden Archive reference library (`edenarchive/`)

Recorded before either library was added to `extern/CMakeLists.txt` (root `CLAUDE.md`, Guardrails). Both are fetched as source archives, pinned by tag and SHA-512, and built by `edenarchive/CMakeLists.txt` as static libraries with their SIMD code in the intrinsics form (no assembler). Checked on 2026-11-01 by downloading each archive and reading its licence files.

### zstd

- **Version:** `v1.5.7`
- **Source:** https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.zip
- **Archive hash (SHA-512):** `d2958014b11ba0fb579b5fd85260caf66955f28e67d9c0ff7b77970ab2ce4d293a55e4ea9c7f00680fb84726777d7cb0e3c2ae2661d97daa8f57b8126d23a7d3`
- **License:** dual, BSD (the file `LICENSE`, three clauses, Meta Platforms) or GPLv2 (`COPYING`), per its `README.md` ("dual-licensed under BSD OR GPLv2"). OpenEden uses the BSD option. The FSF's licence list calls the modified (3-clause) BSD licence compatible with the GNU GPL (https://www.gnu.org/licenses/license-list.html#ModifiedBSD, read 2026-11-01).
- **Purpose:** the codec of compressed chunks (codec 1) in the Eden Archive Format.
- **Note:** OpenMW's Windows vcpkg set also contains a `zstd.h`; the pinned copy is named by the macro `EDENARCHIVE_ZSTD_H` and a unit test compares the header's version number with the linked library's.

### BLAKE3

- **Version:** `1.8.2`
- **Source:** https://github.com/BLAKE3-team/BLAKE3/archive/refs/tags/1.8.2.zip
- **Archive hash (SHA-512):** `90d8196cddf9fc0d6668a0759c67db1523bd0f11e17703c14cb1e629fba224d4f5829437e1121e4bc16f8a2411a6633ab1a1c4f08ee774b6aaaca4e70b879e4c`
- **License:** CC0 1.0 (`LICENSE_CC0`), alternatively Apache 2.0 (`LICENSE_A2`) or Apache 2.0 with LLVM exceptions (`LICENSE_A2LLVM`), per its `README.md` ("Licenses"). OpenEden uses it under the Apache-2.0 option, which is also what the library's own licence is.
- **Purpose:** the hash of the Eden Archive Format (content, chunk, path, header, metadata and manifest digests).

### Licence of the reference library itself

`edenarchive/` is Apache-2.0 (decision 8 of the archive design doc). The FSF's licence list states that the Apache License 2.0 is "a free software license, compatible with version 3 of the GNU GPL" and not with GPL version 2: https://www.gnu.org/licenses/license-list.html#apache2 (read 2026-11-01). So the GPL-3.0-or-later engine may link and include it; the library must therefore never include engine code, and only `components/vfs/edenarchive.*` and the tools in `apps/edenarchive/` (GPL) depend on both.

## Task 0059: meshoptimizer (object cluster baker)

Recorded before it was added to `extern/CMakeLists.txt` (root `CLAUDE.md`, Guardrails). Checked on 2026-11-02 by downloading the archive and reading `LICENSE.md` and the `README.md` licence section.

- **Version:** `v1.3` (released 2026-09-25, more than two weeks before it was pinned)
- **Source:** https://github.com/zeux/meshoptimizer/archive/refs/tags/v1.3.zip (653,284 bytes)
- **Archive hash (SHA-512):** `bb037ba6c9ac1c616bc796afc55ca3d2955ec69189db0093171615903d6e632ed24b2cdb4d38087562046b0e4d43ee962df04dd95891932f53df7ffca1998b98`
- **License:** MIT (`LICENSE.md`, "Copyright (c) 2016-2026 Arseny Kapoulkine"). MIT is compatible with the GPL-3.0-or-later (FSF licence list, Expat license: https://www.gnu.org/licenses/license-list.html#Expat). The README (section "License") asks that the product's user-facing documentation or credits name the library: a credit line is needed in the project's credits (toddhow's file).
- **Purpose:** the mesh simplifier (`simplifier.cpp`) and the index and vertex helpers that the object cluster baker uses (every `src/*.cpp` is compiled, since the simplifier uses the allocator and index code). Only the baker (`components/lod/objectcluster*`) calls it; the running game does not.
- **How it is built:** the sources are fetched by `extern/CMakeLists.txt` and compiled by it into the static library `openeden_meshoptimizer` with the project's third-party flags (warnings off, position independent), not by the library's own CMake file.
