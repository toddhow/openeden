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
