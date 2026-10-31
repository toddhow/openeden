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

## Proposed, not adopted: Slang shader compiler

**Status: proposed, not adopted.** Nothing in the build uses Slang. Recorded here so the facts are ready if toddhow approves the draft decision in `docs/research/0005-slang-shaders.md` (section 9); until then, glslang (item 3 above) remains the only shader compiler.

- **Version:** `v2026.19` (2026-09-29 release tag)
- **Source (Windows x86_64):** https://github.com/shader-slang/slang/releases/download/v2026.19/slang-2026.19-windows-x86_64.zip
  - **SHA-256:** `fc922f214b5d115f663932e59e80e0f6b6cc6fae140979cf307da6459b6f8f33`
  - **SHA-512:** `cdd3329697a7307e0e46a03c45f279af9dd437cf842829f7dee579408959bd821982d9a486a4b01b865b19e67292c378ee962d340244c3062a3915b4f2cb967d`
- **Source (Linux x86_64):** https://github.com/shader-slang/slang/releases/download/v2026.19/slang-2026.19-linux-x86_64-glibc-2.28.tar.gz
  - **SHA-256:** `bd7afcb0956dee9304f44ed115e9aaa7140704271b5c3294bcb237e3ae67adc7` (GitHub's published digest; not yet downloaded or tested)
- **License:** Apache-2.0 WITH LLVM-exception (GPL-3.0-or-later compatible). Bundled third-party notices: glslang, SPIRV-Tools, lz4, mimalloc, miniz, cmark, unordered_dense.
- **Purpose / Why:** compile new shaders written in the Slang language to SPIR-V 1.0 at build time, from task 0012 (Track V stage V4) on; vsgopenmw's GLSL shaders stay on glslang until ported (research note 0005 explains why Slang is not a drop-in replacement for them).
- **Proposed build configuration:** host tool only. CMake downloads the pinned archive, checks the hash and runs `slangc` at build time; nothing from Slang ships with the game. Embedding the compiler library for runtime compilation would add about 27 MB (`slang.dll`, `slang-compiler.dll`, `slang-glsl-module.dll`) or about 33 MB with the optimizer (`slang-glslang.dll`), and needs its own decision.
